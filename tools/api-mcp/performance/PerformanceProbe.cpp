/*
 * 模块名: API/MCP Release 性能探针
 * 功能概述: 在自建模型上测 typed API，提供真实管道和 OpenGL 观察供外部成对测量。
 * 对外接口: main；--scenario mesh/entities --size --report --trace --descriptor。
 * 依赖关系: 冻结 Mini3D Release 库、Qt、Windows 进程统计。
 * 输入输出: 同形夹具到 typed 样本、内存/CPU 证据及正式实例描述文件。
 * 异常与错误: 任一接口失败以错误退出；输出不含 secret 或 base64 图像。
 * 维护说明: 100k 仅复用现有只读夹具；所有窗口、PID 和文件均由本探针拥有。
 */
#include "PerformanceTrace.h"
#include "editor/SceneViewModel.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"
#include "editor/automation/LocalAutomationBridge.h"
#include "editor/observation/ObservationJsonCodec.h"
#include "editor/observation/ObservationService.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAbstractEventDispatcher>
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QOpenGLFunctions_4_1_Core>
#include <QSettings>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

using namespace mini3d;
namespace {
namespace api = editor::api;
namespace observation = editor::observation;
namespace perf = performance;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <typename T> void requireResult(const api::ApiResult<T>& result) {
    if (!result.hasValue())
        throw std::runtime_error(result.error->message.toStdString());
}
double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const auto index = std::max<std::size_t>(1, std::size_t(std::ceil(values.size() * fraction))) - 1;
    return values.at(index);
}
QJsonObject samples(const QString& method, const std::vector<double>& times,
                    const std::vector<double>& bytes, const std::vector<double>& warmups) {
    QJsonArray recorded, sizes, cold;
    for (const auto value : times)
        recorded.append(value);
    for (const auto value : bytes)
        sizes.append(value);
    for (const auto value : warmups)
        cold.append(value);
    return {{"method", method}, {"timingScope", "typed API domain call; result encoding excluded"},
            {"sampleCount", int(times.size())}, {"medianMs", percentile(times, .5)},
            {"p95Ms", percentile(times, .95)}, {"responseJsonBytesMedian", percentile(bytes, .5)},
            {"samplesMs", recorded}, {"responseJsonBytes", sizes}, {"warmupsMs", cold}};
}
QJsonObject memory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    require(GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters)), "GetProcessMemoryInfo failed");
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user),
            "GetProcessTimes failed");
    const auto ticks = [](const FILETIME& value) {
        return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
    };
    return {{"privateBytes", double(counters.PrivateUsage)},
            {"workingSetBytes", double(counters.WorkingSetSize)},
            {"peakWorkingSetBytes", double(counters.PeakWorkingSetSize)},
            {"processCpuMs", double(ticks(kernel) + ticks(user)) / 10000.0},
            {"epochMs", double(QDateTime::currentMSecsSinceEpoch())}};
}
core::modeling::EditableMesh createGrid(int columns, int rows) {
    // 与 tools/EditablePerformanceProbe.cpp 同一四边格算法，100k 使用 400×250。
    core::modeling::EditableMesh mesh;
    mesh.vertices.reserve(std::size_t(columns) * rows);
    mesh.faces.reserve(std::size_t(columns - 1) * (rows - 1));
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x)
            mesh.vertices.push_back({std::uint64_t(y * columns + x + 1),
                                     {-5.0F + 10.0F * x / (columns - 1),
                                      -5.0F + 10.0F * y / (rows - 1), 0}});
    std::uint64_t cornerId = 1;
    for (int y = 0; y < rows - 1; ++y) {
        for (int x = 0; x < columns - 1; ++x) {
            core::modeling::EditableFace face;
            face.id = mesh.faces.size() + 1;
            const auto first = std::uint64_t(y * columns + x + 1);
            for (const auto vertex : {first, first + 1, first + columns + 1, first + columns}) {
                core::modeling::MeshCorner corner;
                corner.id = cornerId++;
                corner.vertex = vertex;
                face.corners.push_back(corner);
            }
            mesh.faces.push_back(std::move(face));
        }
    }
    return mesh;
}
void saveJson(const QString& path, const QJsonObject& value) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), "Evidence path must be new");
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    require(file.write(bytes) == bytes.size(), "Evidence write failed");
}

/** @brief 独占模型/视口夹具；typed 测量完成后保留同形状态供 pipe 客户端。 */
class Probe final {
  public:
    Probe(QApplication& application, const QCommandLineParser& parser)
        : app_(application), size_(parser.value("size").toInt()),
          sampleCount_(parser.value("samples").toInt()),
          warmupCount_(parser.value("warmup").toInt()),
          captureCount_(parser.value("capture-samples").toInt()),
          captureEdge_(parser.value("capture-longest-edge").toInt()),
          scenario_(parser.value("scenario")), reportPath_(parser.value("report")),
          descriptor_(parser.value("descriptor")), api_(model_),
          observation_(api_, viewport_), bridge_(api_, &observation_) {
        require(sampleCount_ > 0 && warmupCount_ >= 0 && captureCount_ > 0,
                "Invalid sample counts");
        require(captureEdge_ == 960 || captureEdge_ == 1600, "Capture fixture edge must be 960 or 1600");
        require((scenario_ == "mesh" && (size_ == 1000 || size_ == 10000 || size_ == 100000)) ||
                    (scenario_ == "entities" && (size_ == 1000 || size_ == 10000)),
                "Unsupported performance fixture");
        perf::output.setFileName(parser.value("trace"));
        require(perf::output.open(QIODevice::WriteOnly | QIODevice::NewOnly),
                "Trace path must be new");
        QSettings cpu(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                      QSettings::NativeFormat);
        report_ = {{"schema", "mini3d-api-performance-v1"}, {"scenario", scenario_},
                   {"size", size_}, {"configuration", "Release /O2 /MD"},
                   {"qt", qVersion()}, {"cpu", cpu.value("ProcessorNameString").toString()},
                   {"logicalProcessors", QThread::idealThreadCount()},
                   {"os", QSysInfo::prettyProductName()}, {"kernel", QSysInfo::kernelVersion()},
                   {"pid", double(QCoreApplication::applicationPid())},
                   {"captureLongestEdge", captureEdge_},
                   {"percentileRule", "nearest rank; warmups excluded"}};
#ifdef PERFORMANCE_INSTRUMENTED
        report_.insert("instrumentation", "m7-phase-trace-v2");
#else
        report_.insert("instrumentation", "none; production objects linked unchanged");
#endif
        viewport_.setWindowTitle(QStringLiteral("Mini3D M7 performance owned probe"));
        viewport_.resize(captureEdge_, captureEdge_ == 1600 ? 1000 : 640);
        viewport_.setFrameDocumentProvider([this] {
            const auto& state = api_.documentState();
            return renderer_gl::FrameDocumentStamp{state.document.instanceId,
                                                   state.document.documentId,
                                                   state.documentRevision, state.historyRevision};
        });
        QObject::connect(&model_, &editor::SceneViewModel::sceneChanged, &viewport_, [this] {
            viewport_.setScene(model_.scene());
            viewport_.setAssets(model_.assets());
        });
        QObject::connect(&model_, &editor::SceneViewModel::documentReset, &viewport_, [this] {
            viewport_.setEditorCamera(model_.editorCamera());
        });
        QObject::connect(QAbstractEventDispatcher::instance(), &QAbstractEventDispatcher::awake,
                         &app_, [this] { ++wakeCount_; });
        QObject::connect(&telemetry_, &QTimer::timeout, &app_, [this] { tick(); });
        telemetry_.start(1000);
        QObject::connect(&viewport_, &QOpenGLWidget::frameSwapped, &app_, [this] {
            if (!firstFrame_) {
                firstFrame_ = true;
                QTimer::singleShot(0, &app_, [this] { startCapture(); });
            }
        });
        QTimer::singleShot(180000, &app_, [this] {
            qCritical("Performance probe exceeded its owned lifetime");
            app_.exit(3);
        });
    }
    ~Probe() {
        bridge_.stop();
        perf::flush();
    }
    void run() {
        model_.newScene();
        report_.insert("memoryBeforeFixture", memory());
        if (scenario_ == "mesh")
            measureMesh();
        else
            measureEntities();
        report_.insert("direct", direct_);
        viewport_.setScene(model_.scene());
        viewport_.setAssets(model_.assets());
        viewport_.setEditorCamera(model_.editorCamera());
        if (scenario_ == "mesh") {
            viewport_.show();
        } else {
            ready();
        }
    }

  private:
    template <typename Action, typename Prepare>
    void measure(const QString& method, Action action, Prepare prepare) {
        std::vector<double> times, bytes, warmups;
        for (int index = 0; index < warmupCount_ + sampleCount_; ++index) {
            prepare(index);
            const auto started = perf::nowNs();
            const auto result = action(index);
            const auto elapsed = double(perf::nowNs() - started) / 1e6;
            requireResult(result);
            if (index < warmupCount_) {
                warmups.push_back(elapsed);
            } else {
                times.push_back(elapsed);
                bytes.push_back(double(QJsonDocument(api::ApiJsonCodec::response(result))
                                           .toJson(QJsonDocument::Compact).size()));
            }
        }
        direct_.append(samples(method, times, bytes, warmups));
    }
    template <typename Action> void measure(const QString& method, Action action) {
        measure(method, action, [](int) {});
    }
    api::MeshTargetRequest target() const {
        return {api_.documentState().document, entityId_, meshId_};
    }
    void measureMesh() {
        const auto dimensions = size_ == 1000 ? std::pair(40, 25)
                                : size_ == 10000 ? std::pair(100, 100) : std::pair(400, 250);
        auto grid = createGrid(dimensions.first, dimensions.second);
        report_.insert("fixture", QJsonObject{{"columns", dimensions.first},
            {"rows", dimensions.second}, {"sourceVertexCount", double(grid.vertices.size())},
            {"sourceFaceCount", double(grid.faces.size())},
            {"sourceCornerCount", double(grid.faces.size() * 4)},
            {"derivedTriangleCount", double(grid.faces.size() * 2)},
            {"generator", "EditablePerformanceProbe.cpp:createGrid same algorithm"},
            {"readOnly", size_ == 100000}});
        api::MeshCreateRequest create;
        create.name = "performance-grid";
        for (const auto& vertex : grid.vertices)
            create.positions.push_back(vertex.position);
        for (const auto& face : grid.faces) {
            api::MeshFaceInput input;
            for (const auto& corner : face.corners)
                input.indices.push_back(std::size_t(corner.vertex - 1));
            create.faces.push_back(std::move(input));
        }
        if (size_ <= 10000) {
            measure("mesh.create", [&](int) {
                const auto result = api_.createMesh(create);
                if (result.hasValue()) {
                    entityId_ = result.value->mesh.entityId;
                    meshId_ = result.value->mesh.meshId;
                }
                return result;
            }, [&](int) {
                model_.newScene();
                create.document = api_.documentState().document;
                create.expectedDocumentRevision = api_.documentState().documentRevision;
            });
            report_.insert("fixtureInstallation", "1k/10k legal typed mesh.create; reset per creation sample");
        } else {
            entityId_ = model_.createEntity(core::PrimitiveKind::Cube);
            require(entityId_ != 0 && model_.makeEditable(entityId_), "100k fixture setup failed");
            require(model_.replaceEditableMesh(entityId_, grid, QStringLiteral("M7只读100k夹具")),
                    "Existing 100k fixture installation failed");
            meshId_ = model_.scene()->find(entityId_)->editableMesh;
            report_.insert("fixtureInstallation", "new owned model: makeEditable + replaceEditableMesh; API create limits unchanged");
        }
        api::MeshSourcePageRequest page;
        static_cast<api::MeshTargetRequest&>(page) = target();
        page.limit = 256;
        measure("mesh.readSourcePage.vertices256", [&](int) { return api_.readSourcePage(page); });
        page.domain = api::MeshDomain::Faces;
        measure("mesh.readSourcePage.faces256", [&](int) { return api_.readSourcePage(page); });
        measure("mesh.getSummary", [&](int) { return api_.meshSummary(target()); });
        if (size_ <= 10000) {
            api::MeshTransformComponentsRequest transform;
            transform.domain = api::MeshComponentDomain::Vertices;
            transform.vertexIds = std::vector<core::modeling::VertexId>{1};
            measure("mesh.transformComponents.vertex1", [&](int) {
                return api_.transformComponents(transform);
            }, [&](int index) {
                const auto& state = api_.documentState();
                const auto* record = model_.scene()->editableMesh(meshId_);
                transform.document = state.document;
                transform.expectedDocumentRevision = state.documentRevision;
                transform.entityId = entityId_;
                transform.meshId = meshId_;
                transform.expectedTopologyRevision = record->topologyRevision;
                transform.expectedGeometryRevision = record->geometryRevision;
                transform.transform.translation = {index % 2 ? -.001 : .001, 0, 0};
            });
            // 仅释放本探针测量创建的历史，给后续 pipe 相同初始几何与有界内存。
            model_.newScene();
            create.document = api_.documentState().document;
            create.expectedDocumentRevision = api_.documentState().documentRevision;
            const auto seeded = api_.createMesh(create);
            requireResult(seeded);
            entityId_ = seeded.value->mesh.entityId;
            meshId_ = seeded.value->mesh.meshId;
        }
    }
    void measureEntities() {
        // 只对这个新模型使用既有 Core 入口构造读取夹具，不改变 API 或用户场景。
        auto scene = std::const_pointer_cast<core::Scene>(model_.scene());
        for (int index = 0; index < size_; ++index)
            require(scene->createEntity("performance-empty") != 0, "Entity fixture failed");
        require(scene->nodes().size() == std::size_t(size_), "Entity fixture count mismatch");
        report_.insert("fixtureInstallation", "owned fresh Scene via existing Core::Scene::createEntity; no production API widening");
        report_.insert("fixture", QJsonObject{{"entityCount", size_}, {"primitive", "empty"},
                                              {"rootOnly", true}, {"historyCount", 0}});
        api::EntityListRequest page;
        page.document = api_.documentState().document;
        page.limit = 256;
        measure("scene.listEntities.first256", [&](int) { return api_.listEntities(page); });
        page.afterEntityId = std::uint64_t(size_ / 2);
        page.expectedDocumentRevision = api_.documentState().documentRevision;
        measure("scene.listEntities.middle256", [&](int) { return api_.listEntities(page); });
    }
    void startCapture() {
        try {
            viewport_.makeCurrent();
            QOpenGLFunctions_4_1_Core gl;
            require(gl.initializeOpenGLFunctions(), "Real OpenGL 4.1 unavailable");
            report_.insert("gpu", QJsonObject{{"vendor", reinterpret_cast<const char*>(gl.glGetString(GL_VENDOR))},
                {"renderer", reinterpret_cast<const char*>(gl.glGetString(GL_RENDERER))},
                {"version", reinterpret_cast<const char*>(gl.glGetString(GL_VERSION))}});
            viewport_.doneCurrent();
            require(viewport_.focusEntities({entityId_}), "Capture fixture focus failed");
            const auto focused = observation_.getState({api_.documentState().document});
            requireResult(focused);
            report_.insert("captureFixture", QJsonObject{{"framing", "focused-current-mesh"},
                {"focusMethod", "ViewportWidget::focusEntities"},
                {"entityIds", QJsonArray{QString::number(entityId_)}},
                {"view", observation::ObservationJsonCodec::encode(*focused.value)}});
            captureNext();
        } catch (const std::exception& exception) {
            qCritical("Performance capture startup failed: %s", exception.what());
            app_.exit(2);
        }
    }
    void captureNext() {
        try {
            const auto state = observation_.getState({api_.documentState().document});
            requireResult(state);
            observation::CaptureRequest request;
            request.document = api_.documentState().document;
            request.expectedDocumentRevision = api_.documentState().documentRevision;
            request.expectedViewportRevision = state.value->viewport.viewportRevision;
            request.longestEdge = captureEdge_;
            request.timeoutMs = 10000;
            const auto id = QStringLiteral("direct-capture-%1").arg(captureIndex_);
            perf::begin(id, "direct.viewport.capture");
            const auto started = perf::nowNs();
            observation_.capture(request, [this, started, id](auto result) {
                try {
                    const auto elapsed = double(perf::nowNs() - started) / 1e6;
                    requireResult(result);
                    if (captureIndex_ < warmupCount_) {
                        captureWarmups_.push_back(elapsed);
                    } else {
                        captureTimes_.push_back(elapsed);
                        captureBytes_.push_back(double(result.value->png.size()));
                    }
                    if (captureIndex_ == warmupCount_) {
                        QFile png(reportPath_ + ".png");
                        require(png.open(QIODevice::WriteOnly | QIODevice::NewOnly), "PNG path must be new");
                        require(png.write(result.value->png) == result.value->png.size(), "PNG evidence failed");
                        report_.insert("captureExample", QJsonObject{{"pngPath", reportPath_ + ".png"},
                            {"sha256", result.value->sha256}, {"width", result.value->outputPixelSize.width()},
                            {"height", result.value->outputPixelSize.height()},
                            {"originalWidth", result.value->originalPixelSize.width()},
                            {"originalHeight", result.value->originalPixelSize.height()},
                            {"byteLength", result.value->png.size()},
                            {"view", observation::ObservationJsonCodec::encode(result.value->view)},
                            {"overlayIncluded", result.value->overlayIncluded}});
                    }
                    perf::complete(id, result.value->png.size(), 0, 0, 0);
                    ++captureIndex_;
                    if (captureIndex_ < warmupCount_ + captureCount_)
                        QTimer::singleShot(0, &app_, [this] { captureNext(); });
                    else {
                        auto capture = samples("viewport.capture", captureTimes_, captureBytes_, captureWarmups_);
                        capture.insert("timingScope", "typed asynchronous capture until PNG completion; wire/base64 excluded");
                        capture.insert("responseBytesMeaning", "raw PNG bytes");
                        direct_.append(capture);
                        report_.insert("direct", direct_);
                        ready();
                    }
                } catch (const std::exception& exception) {
                    qCritical("Performance direct capture failed: %s", exception.what());
                    app_.exit(2);
                }
            });
        } catch (const std::exception& exception) {
            qCritical("Performance direct capture request failed: %s", exception.what());
            app_.exit(2);
        }
    }
    void ready() {
        editor::automation::LocalAutomationBridge::Options options;
        options.enabled = true;
        options.descriptorPath = descriptor_;
        options.permissions = {"scene.read", "viewport.observe"};
        if (scenario_ == "mesh")
            options.permissions.append("viewport.control");
        if (size_ <= 10000 && scenario_ == "mesh")
            options.permissions.append("scene.write");
        QString error;
        require(bridge_.start(options, error), qPrintable(error));
        report_.insert("bridgePermissions", QJsonArray::fromStringList(options.permissions));
        report_.insert("state", api::ApiJsonCodec::encodeState(api_.documentState()));
        report_.insert("entityId", QString::number(entityId_));
        report_.insert("meshId", QString::number(meshId_));
        report_.insert("memoryAfterDirect", memory());
        saveJson(reportPath_, report_);
        const QJsonObject notification{{"ready", true}, {"report", reportPath_},
            {"descriptor", descriptor_}, {"pid", double(QCoreApplication::applicationPid())}};
        const auto text = QJsonDocument(notification).toJson(QJsonDocument::Compact);
        std::fwrite(text.constData(), 1, std::size_t(text.size()), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
        ready_ = true;
    }
    void tick() {
        auto telemetry = memory();
        telemetry.insert("kind", "memory");
        telemetry.insert("eventDispatcherWakeCount", double(wakeCount_));
        telemetry.insert("connectionCount", double(bridge_.connectionCount()));
        telemetry.insert("queuedCount", double(bridge_.queuedCount()));
        telemetry.insert("historyCount", model_.undoStack()->count());
        perf::append(telemetry);
        perf::flush();
        if (ready_ && bridge_.connectionCount() > 0)
            sawConnection_ = true;
        if (sawConnection_ && bridge_.connectionCount() == 0 && bridge_.queuedCount() == 0 &&
            !observation_.isCapturing())
            app_.quit();
    }

    QApplication& app_;
    int size_, sampleCount_, warmupCount_, captureCount_, captureEdge_, captureIndex_ = 0;
    QString scenario_, reportPath_, descriptor_;
    editor::SceneViewModel model_;
    api::EditorApiService api_;
    renderer_gl::ViewportWidget viewport_;
    observation::ObservationService observation_;
    editor::automation::LocalAutomationBridge bridge_;
    QTimer telemetry_;
    QJsonObject report_;
    QJsonArray direct_;
    std::vector<double> captureTimes_, captureBytes_, captureWarmups_;
    core::EntityId entityId_ = 0;
    core::MeshId meshId_ = 0;
    std::uint64_t wakeCount_ = 0;
    bool firstFrame_ = false, ready_ = false, sawConnection_ = false;
};
} // namespace

/** @brief 启动独占探针；正常退出只停止自有桥和窗口，失败返回非零。 */
int main(int argc, char* argv[]) {
    QSurfaceFormat::setDefaultFormat(renderer_gl::ViewportWidget::defaultSurfaceFormat());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.addHelpOption();
    for (const auto& option : {QCommandLineOption{"scenario", "mesh or entities", "scenario", "mesh"},
                              QCommandLineOption{"size", "1000/10000/100000", "size", "1000"},
                              QCommandLineOption{"samples", "Recorded typed samples", "samples", "20"},
                              QCommandLineOption{"warmup", "Excluded warmups", "warmup", "3"},
                              QCommandLineOption{"capture-samples", "PNG samples", "samples", "10"},
                              QCommandLineOption{"capture-longest-edge", "Actual 960 or 1600 viewport fixture", "edge", "960"},
                              QCommandLineOption{"report", "New typed report JSON", "path"},
                              QCommandLineOption{"trace", "New scalar NDJSON evidence", "path"},
                              QCommandLineOption{"descriptor", "New owned bridge descriptor", "path"}})
        parser.addOption(option);
    parser.process(app);
    try {
        Probe probe(app, parser);
        QTimer::singleShot(0, &app, [&] {
            try {
                probe.run();
            } catch (const std::exception& exception) {
                qCritical("Performance fixture failed: %s", exception.what());
                app.exit(2);
            }
        });
        return app.exec();
    } catch (const std::exception& exception) {
        qCritical("Performance probe failed: %s", exception.what());
        return 2;
    }
}
