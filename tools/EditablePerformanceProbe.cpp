/*
 * 模块名: EditablePerformanceProbe
 * 功能概述: 在真实 MainWindow/GL 视口中测量单规模可编辑四边格的选择和组件预览。
 * 对外接口: main；--vertices 10000/50000/100000 --output 新路径 --samples N --warmup N
 *            可选 --diagnostics 独立输出 CPU 诊断，不计入验收。
 * 依赖关系: 编辑器真实信号连接、ComponentPicker、Qt、Windows Psapi。
 * 输入输出: 固定世界范围的精确源网格到样本、P50/P95/max、换帧反馈和机器信息 JSON。
 * 异常与错误: 非 Release、错误规模、已有输出或状态不一致拒绝；超时/慢测不冒充通过。
 * 维护说明: 每个进程只测一个规模；保留真实同步覆盖层和换帧路径，诊断不替代验收。
 */
#include "core/modeling/EditableMesh.h"
#include "core/modeling/MeshValidation.h"
#include "core/modeling/VertexTransform.h"
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/ComponentPicker.h"
#include "editor/operations/KeymapRouter.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDataStream>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QProcess>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <glm/ext/matrix_transform.hpp>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>
// Windows SDK 的 psapi.h 依赖 windows.h；保留其包含顺序。
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

#ifdef NDEBUG
using namespace mini3d;
namespace {
constexpr int kFrameTimeoutMs = 10000;
constexpr double kSlowFirstSampleMs = 3000;

struct Options {
    int vertices = 0;
    int columns = 0;
    int rows = 0;
    int samples = 20;
    int warmup = 2;
    QString output;
};

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

double milliseconds(const QElapsedTimer& timer) {
    return static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
}

QJsonObject memory() {
    PROCESS_MEMORY_COUNTERS counters{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)),
            "Psapi working-set query failed");
    return {{"currentWorkingSetMiB", static_cast<double>(counters.WorkingSetSize) / 1048576.0},
            {"cumulativeProcessPeakWorkingSetMiB",
             static_cast<double>(counters.PeakWorkingSetSize) / 1048576.0}};
}

QJsonObject historyState(const editor::SceneViewModel& model) {
    const auto* history = model.undoStack();
    return {{"count", history->count()},
            {"index", history->index()},
            {"cleanIndex", history->cleanIndex()},
            {"isClean", history->isClean()}};
}

core::modeling::EditableMesh createGrid(int columns, int rows) {
    core::modeling::EditableMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(columns) * rows);
    mesh.faces.reserve(static_cast<std::size_t>(columns - 1) * (rows - 1));
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const auto id = static_cast<std::uint64_t>(y * columns + x + 1);
            mesh.vertices.push_back(
                {id, {-5.0F + 10.0F * x / (columns - 1), -5.0F + 10.0F * y / (rows - 1), 0}});
        }
    }
    std::uint64_t nextCorner = 1;
    for (int y = 0; y < rows - 1; ++y) {
        for (int x = 0; x < columns - 1; ++x) {
            core::modeling::EditableFace face;
            face.id = mesh.faces.size() + 1;
            const auto first = static_cast<std::uint64_t>(y * columns + x + 1);
            for (const auto vertex : {first, first + 1, first + columns + 1, first + columns}) {
                core::modeling::MeshCorner corner;
                corner.id = nextCorner++;
                corner.vertex = vertex;
                face.corners.push_back(corner);
            }
            mesh.faces.push_back(std::move(face));
        }
    }
    return mesh;
}

QString modelHash(const core::modeling::EditableMesh& mesh) {
    // source-canonical-v1：按点/面 ID 排序、保留面角绕序，固定小端和单精度字段。
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    std::vector<const core::modeling::EditableVertex*> vertices;
    std::vector<const core::modeling::EditableFace*> faces;
    for (const auto& vertex : mesh.vertices)
        vertices.push_back(&vertex);
    for (const auto& face : mesh.faces)
        faces.push_back(&face);
    std::sort(vertices.begin(), vertices.end(), [](const auto* a, const auto* b) {
        return a->id < b->id;
    });
    std::sort(faces.begin(), faces.end(), [](const auto* a, const auto* b) {
        return a->id < b->id;
    });
    stream << quint64(vertices.size());
    for (const auto* vertex : vertices)
        stream << quint64(vertex->id) << vertex->position.x << vertex->position.y
               << vertex->position.z;
    stream << quint64(faces.size());
    for (const auto* face : faces) {
        stream << quint64(face->id) << quint64(face->material) << quint64(face->corners.size());
        for (const auto& corner : face->corners) {
            stream << quint64(corner.id) << quint64(corner.vertex) << corner.uv.x << corner.uv.y
                   << corner.color.x << corner.color.y << corner.color.z
                   << quint8(corner.normal.has_value());
            if (corner.normal)
                stream << corner.normal->x << corner.normal->y << corner.normal->z;
        }
    }
    require(stream.status() == QDataStream::Ok, "Canonical source encoding failed");
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject meshCounts(const core::EditableMeshContent& content) {
    std::set<core::modeling::EdgeKey> edges;
    std::size_t corners = 0;
    for (const auto& face : content.source.faces) {
        corners += face.corners.size();
        for (std::size_t i = 0; i < face.corners.size(); ++i)
            edges.emplace(face.corners[i].vertex,
                          face.corners[(i + 1) % face.corners.size()].vertex);
    }
    return {{"sourceVertices", static_cast<qint64>(content.source.vertices.size())},
            {"sourceFaces", static_cast<qint64>(content.source.faces.size())},
            {"sourceEdges", static_cast<qint64>(edges.size())},
            {"sourceCorners", static_cast<qint64>(corners)},
            {"derivedTriangles", static_cast<qint64>(content.derived.mesh.indices.size() / 3)},
            {"derivedRenderVertices", static_cast<qint64>(content.derived.mesh.vertices.size())}};
}

QJsonObject statistics(std::vector<double> values) {
    if (values.empty())
        return {{"samples", 0},
                {"p50Ms", QJsonValue::Null},
                {"p95Ms", QJsonValue::Null},
                {"maxMs", QJsonValue::Null}};
    std::sort(values.begin(), values.end());
    const auto percentile = [&](double fraction) {
        const auto index = static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1;
        return values[index];
    };
    return {{"samples", static_cast<qint64>(values.size())},
            {"p50Ms", percentile(.50)},
            {"p95Ms", percentile(.95)},
            {"maxMs", values.back()}};
}

QJsonObject measureStage(const Options& options, const QStringList& timingKeys,
                         const std::function<QJsonObject(int)>& sample) {
    QJsonArray measured;
    QJsonArray warmups;
    QJsonObject first;
    bool truncated = false;
    bool promoted = false;
    QString reason;
    for (int index = 0; index < options.warmup + options.samples; ++index) {
        auto row = sample(index);
        if (index == 0)
            first = row;
        const auto slowApi =
            std::max({row.value("totalApiMs").toDouble(), row.value("beginApiMs").toDouble(),
                      row.value("previewApiMs").toDouble()});
        if (row.value("frameTimedOut").toBool() || (index == 0 && slowApi > kSlowFirstSampleMs)) {
            measured.append(row);
            promoted = index < options.warmup;
            truncated = true;
            reason =
                row.value("frameTimedOut").toBool()
                    ? QStringLiteral(
                          "Actual preview-to-frameSwapped exceeded 10000 ms or no frame arrived")
                    : QStringLiteral("First observed synchronous action exceeded 3000 ms; not "
                                     "repeatedly sampled");
            break;
        }
        if (index < options.warmup)
            warmups.append(row);
        else
            measured.append(row);
    }
    QJsonObject metrics;
    for (const auto& key : timingKeys) {
        std::vector<double> values;
        for (const auto& row : measured) {
            const auto value = row.toObject().value(key);
            if (value.isDouble())
                values.push_back(value.toDouble());
        }
        metrics[key] = statistics(std::move(values));
    }
    return {{"requestedSamples", options.samples},
            {"measuredSamples", measured.size()},
            {"requestedWarmup", options.warmup},
            {"completedWarmup", warmups.size()},
            {"warmupPromotedToSample", promoted},
            {"truncated", truncated},
            {"reason", reason},
            {"firstObservation", first},
            {"metrics", metrics},
            {"samples", measured},
            {"warmupSamples", warmups}};
}

bool meetsBudget(const QJsonObject& stage, const QString& key, double limit) {
    const auto metric = stage.value("metrics").toObject().value(key).toObject();
    return !stage.value("truncated").toBool() && metric.value("samples").toInt() > 0 &&
           metric.value("p95Ms").toDouble() <= limit;
}

struct FrameTiming {
    double apiMs = 0;
    double feedbackMs = 0;
    bool presented = false;
};

/** @brief 忽略尚未重绘本视口的旧合成帧，换帧反馈必须先经过真实 Paint。 */
class PaintObservation final : public QObject {
  public:
    explicit PaintObservation(renderer_gl::ViewportWidget& viewport) : viewport_(viewport) {
        viewport_.installEventFilter(this);
    }
    ~PaintObservation() override {
        viewport_.removeEventFilter(this);
    }
    bool painted = false;

  protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Paint)
            painted = true;
        return false;
    }

  private:
    renderer_gl::ViewportWidget& viewport_;
};

FrameTiming measureFrame(renderer_gl::ViewportWidget& viewport,
                         const std::function<void()>& action) {
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QElapsedTimer timer;
    FrameTiming result;
    PaintObservation observation(viewport);
    const auto connection = QObject::connect(&viewport, &QOpenGLWidget::frameSwapped, &loop, [&] {
        if (observation.painted && !result.presented) {
            result.feedbackMs = milliseconds(timer);
            result.presented = true;
            loop.quit();
        }
    });
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start();
    watchdog.start(kFrameTimeoutMs);
    action();
    result.apiMs = milliseconds(timer);
    if (!result.presented && timer.elapsed() < kFrameTimeoutMs)
        loop.exec();
    QObject::disconnect(connection);
    // 同步 API 可以阻塞 Qt 的 watchdog，必须用原始墙钟另行判断，不能等迟到帧伪通过。
    result.presented = result.presented && result.feedbackMs <= kFrameTimeoutMs;
    return result;
}

void settleFrame(renderer_gl::ViewportWidget& viewport) {
    const auto timing = measureFrame(viewport, [&] {
        viewport.update();
    });
    require(timing.presented, "Actual GL frame settlement timed out after 10000 ms");
}

glm::vec2 project(const renderer_gl::EditorCamera& camera, const glm::vec3& point,
                  glm::ivec2 size) {
    const auto clip = camera.viewProjectionMatrix() * glm::vec4(point, 1);
    require(clip.w > 0, "Fixture is behind the camera");
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    require(std::abs(ndc.x) <= 1 && std::abs(ndc.y) <= 1 && std::abs(ndc.z) <= 1,
            "Fixture is clipped; selection measurements are invalid");
    return {(ndc.x + 1) * size.x / 2.0F, (1 - ndc.y) * size.y / 2.0F};
}

QJsonObject gitMetadata() {
    QProcess process;
    process.start(QStringLiteral("git"),
                  {QStringLiteral("-C"), QStringLiteral(MINI3D_EDIT_PROBE_SOURCE_DIR),
                   QStringLiteral("status"), QStringLiteral("--porcelain")});
    const bool known = process.waitForFinished(5000) && process.exitCode() == 0;
    QJsonObject metadata{{"buildSha", QStringLiteral(MINI3D_EDIT_PROBE_BUILD_SHA)},
                         {"buildShaMethod", "Git HEAD at CMake configure"},
                         {"dirtyMethod", "runtime git status --porcelain"}};
    metadata["dirty"] = known ? QJsonValue(!process.readAllStandardOutput().isEmpty())
                              : QJsonValue(QJsonValue::Null);
    const auto fileHash = [](const QString& path) -> QJsonValue {
        QFile file(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
            return QJsonValue(QJsonValue::Null);
        return QString::fromLatin1(hash.result().toHex());
    };
    metadata["probeSha256"] = fileHash(QCoreApplication::applicationFilePath());
    QJsonObject sourceHashes;
    for (const auto* relative :
         {"src/core/Scene.cpp", "src/core/Scene.h", "src/core/modeling/VertexTransform.cpp",
          "src/core/modeling/VertexTransform.h", "src/core/modeling/MeshDerivation.cpp",
          "src/core/modeling/MeshDerivation.h", "src/core/modeling/MeshValidation.cpp",
          "src/core/modeling/MeshValidation.h", "src/editor/ComponentSelection.cpp",
          "src/editor/ComponentSelection.h", "src/editor/SceneViewModel.cpp",
          "src/editor/operations/ComponentPicker.cpp", "src/editor/operations/ComponentPicker.h",
          "src/editor/operations/ComponentInteraction.cpp",
          "src/editor/operations/ComponentInteraction.h", "src/editor/workbench/WorkbenchShell.cpp",
          "tools/EditablePerformanceProbe.cpp"}) {
        const auto path = QString::fromUtf8(relative);
        sourceHashes[path] = fileHash(QStringLiteral(MINI3D_EDIT_PROBE_SOURCE_DIR) + '/' + path);
    }
    metadata["sourceSha256AtMeasurement"] = sourceHashes;
    return metadata;
}

void writeReport(const QString& path, const QJsonObject& report) {
    require(!path.isEmpty() && !QFileInfo::exists(path), "Choose a new JSON output path");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    require(file.open(QIODevice::WriteOnly), "Report open failed; parent directory must exist");
    const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    require(file.write(bytes) == bytes.size() && file.commit(), "Report atomic commit failed");
}
} // namespace

int main(int argc, char* argv[]) {
    QSurfaceFormat::setDefaultFormat(renderer_gl::ViewportWidget::defaultSurfaceFormat());
    QApplication application(argc, argv);
    QLoggingCategory::setFilterRules(QStringLiteral("*.info=false"));
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Release real editable-grid/GL performance probe; one scale per process"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("vertices"),
                      QStringLiteral("Exactly 10000, 50000 or 100000 source vertices"),
                      QStringLiteral("count")});
    parser.addOption({QStringLiteral("output"),
                      QStringLiteral("New JSON path; parent directory must exist"),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("samples"), QStringLiteral("Measured samples per action"),
                      QStringLiteral("count"), QStringLiteral("20")});
    parser.addOption({QStringLiteral("warmup"), QStringLiteral("Warmup samples per action"),
                      QStringLiteral("count"), QStringLiteral("2")});
    parser.addOption({QStringLiteral("diagnostics"),
                      QStringLiteral("Separate one-shot CPU diagnostics; not acceptance metrics")});
    parser.process(application);
    Options options;
    options.output = parser.value(QStringLiteral("output"));
    QJsonObject report{
        {"referenceVersion", "Mini3D_V2.0_R2/PERF-01/quad-grid-v1"},
        {"buildConfiguration", "Release"},
        {"expected",
         QJsonObject{{"synchronousApiP95Ms", 50}, {"previewStartToFrameSwappedP95Ms", 100}}},
        {"limitations",
         QJsonArray{
             "Each process measures one scale, a planar front-facing quad grid, vertex selection, "
             "no modifiers and no proportional editing.",
             "Click/box timings are synchronous CPU query/application including real UI "
             "signal/overlay work; they are not presentation latency.",
             "Preview feedback is wall-clock API start to real frameSwapped, including synchronous "
             "work, queued render/upload and compositor/vsync effects; not input-device latency.",
             "First observed action over 3000 ms or preview feedback timeout is retained as one "
             "explicit measured row; truncated results cannot pass acceptance.",
             "Psapi current/peak covers the whole process, including setup, source hash buffers, "
             "Qt/UI, GL caches and the probe; peak is cumulative, not a stage-local peak.",
             "P50/P95 use nearest rank on actually recorded samples; cold first observations and "
             "warmups are reported separately.",
             "Optional one-shot CPU diagnostics are separate from acceptance; budget decisions "
             "always retain the real editor path."}}};
    try {
        bool verticesValid = false;
        bool samplesValid = false;
        bool warmupValid = false;
        options.vertices = parser.value(QStringLiteral("vertices")).toInt(&verticesValid);
        options.samples = parser.value(QStringLiteral("samples")).toInt(&samplesValid);
        options.warmup = parser.value(QStringLiteral("warmup")).toInt(&warmupValid);
        require(verticesValid && samplesValid && warmupValid && options.samples > 0 &&
                    options.warmup >= 0 && options.warmup <= INT_MAX - options.samples,
                "Invalid --vertices/--samples/--warmup");
        if (options.vertices == 10000) {
            options.columns = 100;
            options.rows = 100;
        } else if (options.vertices == 50000) {
            options.columns = 250;
            options.rows = 200;
        } else if (options.vertices == 100000) {
            options.columns = 400;
            options.rows = 250;
        } else {
            require(false, "--vertices must be exactly 10000, 50000 or 100000");
        }
        require(!options.output.isEmpty() && !QFileInfo::exists(options.output),
                "--output must name a new JSON path");
        report["caseId"] = QStringLiteral("editable-quad-grid-%1").arg(options.vertices);
        report["build"] = gitMetadata();
        report["requestedSamples"] = options.samples;
        report["requestedWarmup"] = options.warmup;
        QSettings cpu(QStringLiteral(
                          "HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                      QSettings::NativeFormat);
        report["machine"] =
            QJsonObject{{"cpu", cpu.value(QStringLiteral("ProcessorNameString")).toString()},
                        {"logicalProcessors", QThread::idealThreadCount()},
                        {"architecture", QSysInfo::currentCpuArchitecture()},
                        {"os", QSysInfo::prettyProductName()},
                        {"kernelVersion", QSysInfo::kernelVersion()},
                        {"qt", qVersion()}};
        report["memoryAtProcessSetup"] = memory();
        editor::MainWindow window;
        auto* model = window.findChild<editor::SceneViewModel*>();
        auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
        auto* router = window.findChild<editor::KeymapRouter*>();
        require(model && viewport && router,
                "Real editor model/viewport/keymap connections unavailable");
        const auto screen = QApplication::primaryScreen();
        window.resize(screen ? QSize(1920, 1080).boundedTo(screen->availableGeometry().size())
                             : QSize(1920, 1080));
        const auto startup = measureFrame(*viewport, [&] {
            window.show();
        });
        require(startup.presented && viewport->isValid(), "Real visible GL startup timed out");
        report["startupFirstFrameMs"] = startup.feedbackMs;
        report["keymap"] = router->keymap() == editor::EditorKeymap::Blender ? "Blender" : "Legacy";
        viewport->makeCurrent();
        QOpenGLFunctions_4_1_Core gl;
        require(gl.initializeOpenGLFunctions(), "OpenGL 4.1 functions unavailable");
        report["gpu"] = QJsonObject{
            {"vendor", reinterpret_cast<const char*>(gl.glGetString(GL_VENDOR))},
            {"renderer", reinterpret_cast<const char*>(gl.glGetString(GL_RENDERER))},
            {"openGLVersion", reinterpret_cast<const char*>(gl.glGetString(GL_VERSION))},
            {"shadingLanguageVersion",
             reinterpret_cast<const char*>(gl.glGetString(GL_SHADING_LANGUAGE_VERSION))}};
        viewport->doneCurrent();
        model->newScene();
        const auto entity = model->createEntity(core::PrimitiveKind::Cube);
        require(entity != 0 && model->makeEditable(entity), "Cube-to-editable setup failed");
        const auto mesh = createGrid(options.columns, options.rows);
        QElapsedTimer timer;
        timer.start();
        require(model->replaceEditableMesh(entity, mesh, QStringLiteral("性能四边格")),
                "Real editable source installation failed");
        QJsonObject setup{{"replaceEditableMeshApiMs", milliseconds(timer)}};
        const auto meshId = model->scene()->find(entity)->editableMesh;
        const auto before = model->scene()->editableMesh(meshId)->content;
        require(before->source.vertices.size() == static_cast<std::size_t>(options.vertices) &&
                    !before->mirror && !before->subdivision,
                "Fixture count or modifier contract violated");
        const auto hash = modelHash(before->source);
        if (parser.isSet(QStringLiteral("diagnostics"))) {
            // 诊断不计入验收样本，不用离线计算替代真实编辑器与换帧路径。
            QJsonObject diagnostic{{"timingKind", "one-shot CPU diagnosis, not acceptance"}};
            timer.restart();
            const auto validation = core::modeling::validateEditableMesh(before->source);
            diagnostic["validateEditableMeshMs"] = milliseconds(timer);
            require(validation.isValid(), "Diagnostic validation failed");
            timer.restart();
            auto derived = core::modeling::deriveMesh(before->source);
            diagnostic["deriveMeshIncludingValidationMs"] = milliseconds(timer);
            require(derived.derived.has_value(), "Diagnostic derivation failed");
            const auto delta = glm::translate(glm::dmat4(1), glm::dvec3(.001, 0, 0));
            timer.restart();
            auto transformed =
                core::modeling::transformVertices(before->source, {1}, glm::dmat4(1), delta);
            diagnostic["transformVerticesIncludingDerivationMs"] = milliseconds(timer);
            require(transformed.mesh.has_value(), "Diagnostic transform failed");
            std::string error;
            core::Scene diagnosticScene;
            const auto diagnosticEntity = diagnosticScene.createEntity("diagnostic");
            timer.restart();
            auto candidate = diagnosticScene.prepareTransformedEditableGeometry(
                diagnosticEntity, before->source, {1}, glm::dmat4(1), delta, error);
            diagnostic["scenePreparationMs"] = milliseconds(timer);
            require(candidate.has_value(), "Diagnostic Scene preparation failed");
            report["cpuDiagnostics"] = diagnostic;
        }
        report["modelHash"] = hash;
        report["modelHashSchema"] = "SHA-256/source-canonical-v1; stable ID order; LE; float32; "
                                    "ordered corners with all source attributes";
        report["mesh"] = meshCounts(*before);
        report["grid"] = QJsonObject{{"columns", options.columns},
                                     {"rows", options.rows},
                                     {"worldMinimum", QJsonArray{-5, -5, 0}},
                                     {"worldMaximum", QJsonArray{5, 5, 0}},
                                     {"normal", "+Z"}};
        model->setProportionalEditingEnabled(false);
        viewport->setCameraView(renderer_gl::EditorView::Front);
        viewport->setOrthographic(false);
        viewport->setShadingMode(renderer_gl::ViewportShading::Material);
        require(viewport->focusSelection(), "Front camera focus failed");
        settleFrame(*viewport);
        timer.restart();
        require(model->setEditMode(true), "Real Edit-mode entry failed");
        setup["setEditModeApiMs"] = milliseconds(timer);
        setup["defaultEditSelectionCount"] =
            static_cast<qint64>(model->componentSelection().selectedIds().size());
        timer.restart();
        model->clearComponentSelection();
        setup["initialClearSelectionPreparationMs"] = milliseconds(timer);
        model->setSelectionDomain(editor::SelectionDomain::Vertex);
        require(!model->isProportionalEditingEnabled(), "Proportional editing must be disabled");
        settleFrame(*viewport);
        report["setup"] = setup;
        report["memoryAfterEditSetup"] = memory();
        const glm::ivec2 viewportSize(viewport->width(), viewport->height());
        report["resolution"] =
            QJsonObject{{"requestedWindowLogical", QJsonArray{1920, 1080}},
                        {"actualWindowLogical", QJsonArray{window.width(), window.height()}},
                        {"viewportLogical", QJsonArray{viewportSize.x, viewportSize.y}},
                        {"viewportPhysical",
                         QJsonArray{qRound(viewportSize.x * viewport->devicePixelRatioF()),
                                    qRound(viewportSize.y * viewport->devicePixelRatioF())}}};
        report["dpr"] = viewport->devicePixelRatioF();
        const auto camera = viewport->editorCameraSnapshot();
        require(camera && camera->view() == renderer_gl::EditorView::Front,
                "Actual Front camera snapshot unavailable");
        const auto world = model->scene()->worldMatrix(entity);
        require(world == glm::mat4(1), "Fixture must have identity object transform");
        const auto clickId = static_cast<std::uint64_t>((options.rows / 2) * options.columns +
                                                        options.columns / 2 + 1);
        const auto click =
            project(*camera, before->source.vertices[clickId - 1].position, viewportSize);
        const auto bottomLeft = project(*camera, {-5, -5, 0}, viewportSize);
        const auto topRight = project(*camera, {5, 5, 0}, viewportSize);
        const auto center = (bottomLeft + topRight) * .5F;
        const glm::vec2 halfExtent = glm::abs(topRight - bottomLeft) * (std::sqrt(.1F) * .5F);
        const auto boxFirst = center - halfExtent;
        const auto boxSecond = center + halfExtent;
        // 在计时外独立计算平面夹具的完整 ROI，不用查询返回值验证查询自身。
        const auto minimum = glm::dvec2(glm::min(boxFirst, boxSecond));
        const auto maximum = glm::dvec2(glm::max(boxFirst, boxSecond));
        const auto projection = glm::dmat4(camera->viewProjectionMatrix());
        std::set<editor::ComponentId> expectedBox;
        for (const auto& vertex : before->source.vertices) {
            const auto clip = projection * glm::dvec4(vertex.position, 1);
            const glm::dvec2 pixel{(clip.x / clip.w + 1) * viewportSize.x * .5,
                                   (1 - clip.y / clip.w) * viewportSize.y * .5};
            if (pixel.x >= minimum.x && pixel.x <= maximum.x && pixel.y >= minimum.y &&
                pixel.y <= maximum.y)
                expectedBox.insert({vertex.id});
        }
        require(expectedBox.size() > 1, "Projected box fixture must contain multiple vertices");
        report["selectionFixture"] =
            QJsonObject{{"domain", "Vertex"},
                        {"clickExpectedSourceVertex", static_cast<qint64>(clickId)},
                        {"clickLogical", QJsonArray{click.x, click.y}},
                        {"boxFirstLogical", QJsonArray{boxFirst.x, boxFirst.y}},
                        {"boxSecondLogical", QJsonArray{boxSecond.x, boxSecond.y}},
                        {"boxExpectedSourceVertices", static_cast<qint64>(expectedBox.size())},
                        {"boxFractionOfProjectedMeshArea", .1}};
        const auto baselineHistory = historyState(*model);
        QJsonObject actual;
        bool accepted = true;
        for (const bool xRay : {false, true}) {
            viewport->setXRayEnabled(xRay);
            settleFrame(*viewport);
            for (const bool box : {false, true}) {
                const auto name =
                    QStringLiteral("%1_%2").arg(xRay ? "xray" : "visible", box ? "box" : "click");
                const auto stage = measureStage(
                    options, {"selectionClearPreparationMs", "queryMs", "applyMs", "totalApiMs"},
                    [&](int) {
                        QElapsedTimer clear;
                        clear.start();
                        model->clearComponentSelection();
                        const auto preparationMs = milliseconds(clear);
                        settleFrame(*viewport);
                        std::set<editor::ComponentId> hits;
                        QElapsedTimer total;
                        QElapsedTimer query;
                        total.start();
                        query.start();
                        if (box) {
                            hits = editor::boxSelectComponents(
                                *model->scene(), *model->assets(), entity,
                                editor::SelectionDomain::Vertex, *camera, boxFirst, boxSecond,
                                viewportSize, xRay, model->viewportVisibility());
                        } else {
                            const auto hit = editor::pickComponent(
                                *model->scene(), *model->assets(), entity,
                                editor::SelectionDomain::Vertex, *camera, click, viewportSize, xRay,
                                model->viewportVisibility());
                            if (hit)
                                hits.insert(*hit);
                        }
                        const auto queryMs = milliseconds(query);
                        QElapsedTimer apply;
                        apply.start();
                        if (box)
                            model->selectComponents(hits, editor::SelectionOperation::Replace);
                        else if (!hits.empty())
                            model->selectComponent(*hits.begin(),
                                                   editor::SelectionOperation::Replace);
                        const auto applyMs = milliseconds(apply);
                        const auto totalMs = milliseconds(total);
                        require(hits == (box ? expectedBox
                                             : std::set<editor::ComponentId>{{clickId, 0}}),
                                "Expected projected source vertices were not hit");
                        require(model->componentSelection().selectedIds() == hits,
                                "Real selection application did not match query");
                        require(historyState(*model) == baselineHistory,
                                "Selection unexpectedly changed history");
                        return QJsonObject{{"selectionClearPreparationMs", preparationMs},
                                           {"queryMs", queryMs},
                                           {"applyMs", applyMs},
                                           {"totalApiMs", totalMs},
                                           {"hitCount", static_cast<qint64>(hits.size())},
                                           {"xRay", xRay},
                                           {"memoryAfterApplication", memory()}};
                    });
                auto result = stage;
                result["meetsApiP95Budget"] = meetsBudget(stage, "totalApiMs", 50);
                result["timingKind"] = "synchronous query/application, not frame feedback";
                accepted = accepted && result.value("meetsApiP95Budget").toBool();
                actual[name] = result;
                report["actual"] = actual;
            }
        }
        viewport->setXRayEnabled(false);
        timer.restart();
        model->clearComponentSelection();
        model->selectComponent({1, 0}, editor::SelectionOperation::Replace);
        actual["transformSelectionPreparationMs"] = milliseconds(timer);
        const auto selectedBeforeTransform = model->componentSelection();
        require(selectedBeforeTransform.selectedIds() == std::set<editor::ComponentId>{{1, 0}},
                "Transform must select exactly one source corner vertex");
        settleFrame(*viewport);
        const auto transform = measureStage(
            options, {"beginApiMs", "previewApiMs", "previewFeedbackMs", "cancelApiMs"},
            [&](int index) {
                QElapsedTimer begin;
                begin.start();
                require(model->beginComponentTransform(QStringLiteral("性能单角点移动")),
                        "Real component begin failed");
                const auto beginMs = milliseconds(begin);
                settleFrame(*viewport);
                const auto offset = .001 * (1 + index % 3);
                const auto delta = glm::translate(glm::dmat4(1), glm::dvec3(offset, 0, 0));
                bool previewValid = false;
                const auto timing = measureFrame(*viewport, [&] {
                    previewValid = model->previewComponentTransform(delta);
                });
                const auto preview = model->componentPreview();
                require(previewValid && preview && preview->source != before->source,
                        "Nonzero real component preview was not published");
                const auto expected = before->source.vertices.front().position +
                                      glm::vec3(static_cast<float>(offset), 0, 0);
                require(glm::length(preview->source.vertices.front().position - expected) <
                                1.0e-6F &&
                            preview->source.vertices[1] == before->source.vertices[1],
                        "Preview did not recompute the single moved vertex from frozen before");
                require(model->scene()->editableMesh(meshId)->content == before &&
                            historyState(*model) == baselineHistory,
                        "Preview unexpectedly changed committed source/history");
                QElapsedTimer cancel;
                cancel.start();
                require(model->finishComponentTransform(false), "Real component cancel failed");
                const auto cancelMs = milliseconds(cancel);
                require(!model->hasComponentTransform() &&
                            model->componentSelection() == selectedBeforeTransform &&
                            model->scene()->editableMesh(meshId)->content == before &&
                            historyState(*model) == baselineHistory,
                        "Cancellation did not restore source, selection or history");
                QJsonObject row{{"beginApiMs", beginMs},
                                {"previewApiMs", timing.apiMs},
                                {"previewFeedbackMs", timing.presented
                                                          ? QJsonValue(timing.feedbackMs)
                                                          : QJsonValue(QJsonValue::Null)},
                                {"frameTimedOut", !timing.presented},
                                {"frameTimeoutMs", kFrameTimeoutMs},
                                {"cancelApiMs", cancelMs},
                                {"nonzeroWorldXOffset", offset},
                                {"memoryAfterCancel", memory()}};
                if (timing.presented)
                    settleFrame(*viewport);
                return row;
            });
        auto transformResult = transform;
        const bool transformAccepted = meetsBudget(transform, "beginApiMs", 50) &&
                                       meetsBudget(transform, "previewApiMs", 50) &&
                                       meetsBudget(transform, "previewFeedbackMs", 100);
        transformResult["meetsApiAndActualFeedbackBudgets"] = transformAccepted;
        actual["componentTransform"] = transformResult;
        report["actual"] = actual;
        accepted = accepted && transformAccepted;
        report["modelHashAfterCancel"] =
            modelHash(model->scene()->editableMesh(meshId)->content->source);
        require(report.value("modelHashAfterCancel").toString() == hash,
                "Cancelled source hash changed");
        report["correctness"] =
            QJsonObject{{"sourceHashRestored", true},
                        {"historyUnchanged", historyState(*model) == baselineHistory},
                        {"historyBefore", baselineHistory},
                        {"historyAfter", historyState(*model)},
                        {"singleCornerSelectionRestored",
                         model->componentSelection() == selectedBeforeTransform}};
        report["memoryAtCompletion"] = memory();
        report["status"] = accepted ? "passed" : "measured_not_accepted";
        report["meetsAllBudgetsWithCompleteSamples"] = accepted;
        writeReport(options.output, report);
        std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
        return accepted ? 0 : 3;
    } catch (const std::exception& error) {
        report["status"] = "failed";
        report["meetsAllBudgetsWithCompleteSamples"] = false;
        report["error"] = QString::fromUtf8(error.what());
        std::cerr << error.what() << '\n';
        if (!options.output.isEmpty() && !QFileInfo::exists(options.output)) {
            try {
                writeReport(options.output, report);
            } catch (const std::exception& outputError) {
                std::cerr << outputError.what() << '\n';
            }
        }
        return 1;
    }
}
#else
int main() {
    std::cerr << "Use the Release mini3d_edit_performance target; Debug is not a measurement.\n";
    return 2;
}
#endif
