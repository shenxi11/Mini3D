/*
 * 模块名: AnimationPerformanceTests
 * 功能概述: 以中立夹具测量原生动画的完整 Core 求值、真实窗口换帧和正式编辑反馈。
 * 对外接口: Catch2 隐藏标签 [.][animation-performance]，仅显式 Release 验收。
 * 依赖关系: MainWindow、SceneViewModel、ViewportWidget、Qt Test、Windows Psapi。
 * 输入输出: 102 节点/300 轨/9000 键/48500 源点到独立三轮统计、内存及真实 PNG/JSON。
 * 异常与错误: 非 Release、无已验证 E 盘目录、覆盖目标、身份失配或超时均不能通过。
 * 维护说明: 固定预热后 30 秒×3；换帧回调只存数值戳，不插入额外求值或逐帧断言。
 */
#include "core/EvaluatedPose.h"
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/api/EditorApiService.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QScopeGuard>
#include <QScreen>
#include <QSettings>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
// Windows SDK 的 psapi.h 依赖 windows.h，保留此顺序。
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

using namespace mini3d;
#ifdef NDEBUG
namespace {
constexpr int kFps = 24;
constexpr int kWarmupMs = 5000;
constexpr int kRoundMs = 30000;
constexpr int kRounds = 3;
constexpr int kCoreSamples = kFps * kRoundMs / 1000;
constexpr int kFrameTimeoutMs = 10000;
constexpr std::uint32_t kLastFrame = 697;
constexpr double kCoreP95BudgetMs = 5.0;
constexpr double kSwapP95BudgetMs = 41.7;

void requireProbe(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

double milliseconds(const QElapsedTimer& timer) {
    return static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
}

QJsonObject statistics(std::vector<double> values) {
    requireProbe(!values.empty(), "No timing samples were observed");
    std::sort(values.begin(), values.end());
    const auto percentile = [&](double fraction) {
        return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
    };
    return {{"sampleCount", static_cast<qint64>(values.size())},
            {"p50Ms", percentile(.50)}, {"p95Ms", percentile(.95)},
            {"p99Ms", percentile(.99)}, {"maxMs", values.back()}};
}

QJsonArray samplesJson(const std::vector<double>& values) {
    QJsonArray result;
    for (const auto value : values)
        result.append(value);
    return result;
}

QJsonObject processMemory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    requireProbe(GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)),
        "GetProcessMemoryInfo failed");
    constexpr double mib = 1048576.0;
    return {{"currentWorkingSetMiB", counters.WorkingSetSize / mib},
            {"currentPrivateUsageMiB", counters.PrivateUsage / mib},
            {"cumulativeProcessPeakWorkingSetMiB", counters.PeakWorkingSetSize / mib},
            {"cumulativeProcessPeakPagefileUsageMiB", counters.PeakPagefileUsage / mib}};
}

QJsonObject historyState(const editor::SceneViewModel& model) {
    const auto* history = model.undoStack();
    return {{"count", history->count()}, {"index", history->index()},
            {"cleanIndex", history->cleanIndex()}, {"isClean", history->isClean()},
            {"canUndo", history->canUndo()}, {"canRedo", history->canRedo()}};
}

QJsonValue mirrorJson(const std::optional<core::modeling::MirrorOptions>& options) {
    if (!options)
        return QJsonValue::Null;
    return QJsonObject{{"axis", static_cast<int>(options->axis)}, {"enabled", options->enabled},
        {"merge", options->merge}, {"clipping", options->clipping},
        {"threshold", options->threshold}};
}

QJsonValue subdivisionJson(const std::optional<core::modeling::SubdivisionOptions>& options) {
    if (!options)
        return QJsonValue::Null;
    return QJsonObject{{"enabled", options->enabled}, {"levels", options->levels}};
}

QJsonArray meshState(const core::Scene& scene) {
    QJsonArray result;
    for (const auto& node : scene.nodes()) {
        if (!node.editableMesh)
            continue;
        const auto* record = scene.editableMesh(node.editableMesh);
        requireProbe(record && record->content, "Fixture editable content is missing");
        const auto& content = *record->content;
        result.append(QJsonObject{
            {"entityId", QString::number(node.id)}, {"meshId", QString::number(node.editableMesh)},
            {"sourceVertices", static_cast<qint64>(content.source.vertices.size())},
            {"sourceFaces", static_cast<qint64>(content.source.faces.size())},
            {"evaluatedVertices", static_cast<qint64>(content.evaluatedMesh().vertices.size())},
            {"renderVertices", static_cast<qint64>(content.displayedDerived().mesh.vertices.size())},
            {"renderTriangles", static_cast<qint64>(content.displayedDerived().mesh.indices.size() / 3)},
            {"topologyRevision", QString::number(record->topologyRevision)},
            {"geometryRevision", QString::number(record->geometryRevision)},
            {"evaluationRevision", QString::number(record->evaluationRevision)},
            {"mirror", mirrorJson(content.mirror)},
            {"subdivision", subdivisionJson(content.subdivision)}});
    }
    return result;
}

// 只在阶段边界采集；不可变 content 指针与 revision 证明源网格未被 tick 替换。
struct SourceBoundary {
    std::vector<core::SceneNode> nodes;
    std::vector<std::pair<core::MeshId, core::EditableMeshRecord>> meshes;
    explicit SourceBoundary(const core::Scene& scene) : nodes(scene.nodes()) {
        for (const auto& node : nodes)
            if (node.editableMesh)
                meshes.emplace_back(node.editableMesh, *scene.editableMesh(node.editableMesh));
    }
    bool matches(const core::Scene& scene) const {
        if (scene.nodes().size() != nodes.size())
            return false;
        for (const auto& before : nodes) {
            const auto* after = scene.find(before.id);
            if (!after || after->parent != before.parent || after->children != before.children ||
                after->transform.position != before.transform.position ||
                after->transform.rotation != before.transform.rotation ||
                after->transform.scale != before.transform.scale ||
                after->editableMesh != before.editableMesh || after->camera != before.camera ||
                after->light != before.light || after->surface != before.surface ||
                after->visible != before.visible || after->primitive != before.primitive)
                return false;
        }
        for (const auto& [id, before] : meshes) {
            const auto* after = scene.editableMesh(id);
            if (!after || after->content != before.content ||
                after->topologyRevision != before.topologyRevision ||
                after->geometryRevision != before.geometryRevision ||
                after->evaluationRevision != before.evaluationRevision ||
                after->content->mirror != before.content->mirror ||
                after->content->subdivision != before.content->subdivision)
                return false;
        }
        return true;
    }
};

core::modeling::EditableMesh createGrid(bool rotor, bool mirrorSource) {
    constexpr int columns = 25, rows = 20;
    core::modeling::EditableMesh mesh;
    mesh.vertices.reserve(columns * rows);
    mesh.faces.reserve((columns - 1) * (rows - 1));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const float x = static_cast<float>(column) / (columns - 1);
            const float z = static_cast<float>(row) / (rows - 1);
            // 旋翼长端/短端不同，Y 轴转动可见；镜像源完全位于 X 正侧。
            mesh.vertices.push_back({static_cast<std::uint64_t>(row * columns + column + 1),
                {rotor ? -.25F + 3.0F * x : (mirrorSource ? .05F + x : x - .5F),
                 0, rotor ? -.12F + .24F * z : z - .5F}});
        }
    }
    std::uint64_t cornerId = 1;
    for (int row = 0; row < rows - 1; ++row) {
        for (int column = 0; column < columns - 1; ++column) {
            core::modeling::EditableFace face;
            face.id = mesh.faces.size() + 1;
            const auto first = static_cast<std::uint64_t>(row * columns + column + 1);
            for (const auto vertex : {first, first + columns, first + columns + 1, first + 1}) {
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

// 最后安装的应用过滤器先于产品键位路由；只隔离自建窗口，不阻断绘制和生命周期。
class FixtureInputIsolation final : public QObject {
  public:
    explicit FixtureInputIsolation(QWidget& window) : window_(window) {
        qApp->installEventFilter(this);
    }
    ~FixtureInputIsolation() override { qApp->removeEventFilter(this); }
    QJsonObject counters() const {
        return {{"blockedInputEvents", blocked_}, {"spontaneousInputEvents", spontaneous_}};
    }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        const auto* widget = qobject_cast<QWidget*>(watched);
        if (!widget || (widget != &window_ && !window_.isAncestorOf(widget)))
            return false;
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
            ++blocked_;
            spontaneous_ += event->spontaneous();
            event->accept();
            return true;
        default:
            return false;
        }
    }

  private:
    QWidget& window_;
    qint64 blocked_ = 0, spontaneous_ = 0;
};

/** @brief 独立真实窗口夹具；正式内容全经 ViewModel 写入，不打开或覆写任何作品。 */
struct AnimationPerformanceWindow {
    editor::MainWindow window;
    std::unique_ptr<FixtureInputIsolation> inputIsolation;
    editor::SceneViewModel* model = nullptr;
    renderer_gl::ViewportWidget* viewport = nullptr;
    std::vector<core::EntityId> animated;
    core::EntityId parent = 0, rotor = 0, camera = 0, light = 0, child = 0, grandchild = 0;

    AnimationPerformanceWindow() {
        model = window.findChild<editor::SceneViewModel*>();
        viewport = window.findChild<renderer_gl::ViewportWidget*>();
        requireProbe(model && viewport, "Real MainWindow wiring is unavailable");
        inputIsolation = std::make_unique<FixtureInputIsolation>(window);
        window.setAttribute(Qt::WA_ShowWithoutActivating);
        const auto* screen = QApplication::primaryScreen();
        window.resize(screen ? QSize(1600, 900).boundedTo(screen->availableGeometry().size())
                             : QSize(1600, 900));
        window.show();
        requireProbe(QTest::qWaitForWindowExposed(&window), "MainWindow was not exposed");
        auto* timeline = window.findChild<QDockWidget*>(QStringLiteral("AnimationDock"));
        requireProbe(timeline, "Real animation timeline is unavailable");
        timeline->show();
        model->newScene();
        requireProbe(model->scene()->nodes().empty() && model->undoStack()->count() == 0,
                     "Neutral fixture did not begin with an empty document");
        parent = model->createEntity(core::PrimitiveKind::Empty);
        camera = model->createCamera();
        light = model->createDirectionalLight();
        requireProbe(parent && camera && light, "Parent/Camera/Light setup failed");
        requireProbe(model->setParent(camera, parent) && model->setParent(light, parent),
                     "Device hierarchy setup failed");
        animated = {parent, camera, light};
        for (int index = 0; index < 97; ++index) {
            const auto entity = model->createEntity(core::PrimitiveKind::Cube);
            requireProbe(entity && model->makeEditable(entity), "Editable object setup failed");
            const auto grid = createGrid(index == 0, index == 1);
            requireProbe(model->replaceEditableMesh(entity, grid, QStringLiteral("动画性能中立格")),
                         "Validated 500-point grid installation failed");
            core::Transform transform;
            transform.position = index == 0 ? glm::vec3(0, 1.5F, 0)
                : glm::vec3((index % 10 - 4.5F) * 1.8F, .05F, (index / 10 - 4.5F) * 1.8F);
            requireProbe(model->setTransform(entity, transform), "Grid base placement failed");
            requireProbe(model->setParent(entity, parent), "Grid parenting failed");
            if (index == 0) {
                rotor = entity;
                core::SurfaceStyle style;
                style.tint = {.95F, .22F, .08F};
                requireProbe(model->setSurface(entity, style), "Rotor appearance setup failed");
            }
            if (index == 1) {
                core::modeling::MirrorOptions mirror;
                mirror.clipping = false;
                requireProbe(model->setMirrorOptions(entity, mirror), "Mirror fixture setup failed");
                requireProbe(model->setSubdivisionOptions(entity, core::modeling::SubdivisionOptions{}),
                             "Mirror-to-Subdivision fixture setup failed");
            }
            animated.push_back(entity);
        }
        child = model->createEntity(core::PrimitiveKind::Empty);
        grandchild = model->createEntity(core::PrimitiveKind::Cube);
        requireProbe(child && grandchild && model->setParent(child, parent) &&
                         model->setParent(grandchild, child), "Untracked descendant setup failed");
        core::Transform descendantTransform;
        descendantTransform.position = {0, .6F, 0};
        requireProbe(model->setTransform(grandchild, descendantTransform),
                     "Untracked descendant placement failed");

        core::SceneAnimation animation;
        animation.settings = {kFps, 1, kLastFrame};
        const std::array channels{core::AnimationChannel::Position,
            core::AnimationChannel::RotationEulerXYZDegrees, core::AnimationChannel::Scale};
        for (std::size_t index = 0; index < animated.size(); ++index) {
            const auto entity = animated[index];
            const auto& base = model->scene()->find(entity)->transform;
            const auto baseEuler = core::canonicalEulerXYZDegrees(glm::dquat(base.rotation));
            requireProbe(baseEuler.has_value(), "Fixture base rotation has no canonical Euler");
            for (const auto channel : channels) {
                auto& keys = animation.tracks[{entity, channel}].keys;
                for (int key = 0; key < 30; ++key) {
                    const auto progress = key / 29.0;
                    const auto wave = std::sin(progress * 6.283185307179586);
                    glm::dvec3 value;
                    if (channel == core::AnimationChannel::Position) {
                        value = glm::dvec3(base.position);
                        value.x += entity == parent ? progress * 2.0 : wave * .10;
                    } else if (channel == core::AnimationChannel::RotationEulerXYZDegrees) {
                        value = *baseEuler;
                        value.y += entity == rotor ? progress * 720.0 : wave * 8.0;
                    } else {
                        value = glm::dvec3(base.scale) * (1.0 + wave * .02);
                    }
                    keys.push_back({static_cast<std::uint32_t>(1 + key * kFps), value,
                                    core::AnimationInterpolation::Linear});
                }
            }
        }
        requireProbe(core::validateAnimationPreflightBudget(102, 30).isValid(),
                     "Neutral definition exceeds preflight node visits");
        const auto historyBefore = model->undoStack()->count();
        requireProbe(model->replaceAnimation(animation), "Official animation definition rejected");
        requireProbe(model->undoStack()->count() == historyBefore + 1,
                     "Animation definition did not enter the shared history once");
        requireProbe(model->scene()->nodes().size() == 102 && animated.size() == 100 &&
                         model->scene()->animation().tracks.size() == 300,
                     "Fixture node/track count is incorrect");
        std::size_t actualVertices = 0, actualKeys = 0;
        for (const auto& node : model->scene()->nodes())
            if (node.editableMesh)
                actualVertices += model->scene()->editableMesh(node.editableMesh)->content->source.vertices.size();
        for (const auto& [id, track] : model->scene()->animation().tracks)
            actualKeys += track.keys.size();
        requireProbe(actualVertices == 48500 && actualKeys == 9000 &&
                         model->scene()->animation() == animation,
                     "Installed fixture source/definition differs from the declared scale");
        requireProbe(viewport->focusAll(), "Actual viewport could not frame the fixture");
        model->selection()->setSelectedEntity(rotor);
        requireProbe(model->setAnimationPreview(true) && model->setAnimationLoop(true),
                     "Official looping preview setup failed");
    }
    ~AnimationPerformanceWindow() {
        model->suspendAnimation();
        window.hide();
    }
};

QJsonObject editorViewState(const AnimationPerformanceWindow& fixture) {
    const auto& saved = fixture.model->editorCamera();
    const auto camera = fixture.viewport->editorCameraSnapshot();
    requireProbe(camera.has_value(), "Actual editor camera is unavailable");
    const auto vector = [](const glm::vec3& value) {
        return QJsonArray{value.x, value.y, value.z};
    };
    const auto matrix = [](const glm::mat4& value) {
        QJsonArray values;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                values.append(value[column][row]);
        return values;
    };
    return {{"savedPosition", vector(saved.position)}, {"savedTarget", vector(saved.target)},
        {"maximumDistance", saved.maximumDistance}, {"focusRadius", saved.focusRadius},
        {"actualViewMatrixColumnMajor", matrix(camera->viewMatrix())},
        {"actualProjectionMatrixColumnMajor", matrix(camera->projectionMatrix())}};
}

struct FrameExpectation {
    editor::api::DocumentState document;
    editor::api::AnimationControllerState controller;
    std::uint64_t contextGeneration = 0;
};

FrameExpectation currentExpectation(const AnimationPerformanceWindow& fixture,
                                    const editor::api::DocumentState& document,
                                    const editor::api::AnimationControllerState& controller) {
    const auto pose = fixture.model->installedAnimationPose();
    requireProbe(pose && pose->identity.sourceRevision == document.documentRevision &&
                     pose->identity.evaluationId == controller.evaluationId &&
                     pose->identity.frame == controller.frame &&
                     fixture.model->animationMode() == renderer_gl::AnimationMode::PreviewPaused,
                 "Operation result does not describe the installed paused pose");
    return {document, controller, fixture.viewport->contextGeneration()};
}

bool matchesFrame(const renderer_gl::RenderedFrame& frame, const FrameExpectation& expected,
                  std::uint64_t previousFrameId) {
    const auto& state = frame.state;
    return frame.frameId > previousFrameId &&
        frame.contextGeneration == expected.contextGeneration && frame.resources.ready &&
        state.document.instanceId == expected.document.document.instanceId &&
        state.document.documentId == expected.document.document.documentId &&
        state.document.documentRevision == expected.document.documentRevision &&
        state.document.historyRevision == expected.document.historyRevision &&
        state.sessionRevision == expected.controller.sessionRevision && state.animation &&
        state.animation->instanceId == expected.document.document.instanceId &&
        state.animation->documentId == expected.document.document.documentId &&
        state.animation->sourceRevision == expected.document.documentRevision &&
        state.animation->evaluationId == expected.controller.evaluationId &&
        state.animation->frame == expected.controller.frame &&
        state.animationMode == renderer_gl::AnimationMode::PreviewPaused &&
        state.animation->mode == renderer_gl::AnimationMode::PreviewPaused;
}

// 同步调用计时止于 API 返回；验证及换帧等待只进入总反馈耗时。
QJsonObject measureFeedback(AnimationPerformanceWindow& fixture,
    const std::function<void()>& action, const std::function<FrameExpectation()>& expectation) {
    auto& viewport = *fixture.viewport;
    const auto previousFrameId = viewport.lastRenderedFrame() ? viewport.lastRenderedFrame()->frameId : 0;
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QElapsedTimer clock;
    std::optional<FrameExpectation> expected;
    bool presented = false;
    double feedbackMs = 0;
    std::uint64_t presentedFrameId = 0;
    const auto connection = QObject::connect(&viewport, &QOpenGLWidget::frameSwapped, &loop, [&] {
        const auto& frame = viewport.lastRenderedFrame();
        if (expected && frame && matchesFrame(*frame, *expected, previousFrameId)) {
            feedbackMs = milliseconds(clock);
            presentedFrameId = frame->frameId;
            presented = true;
            loop.quit();
        }
    });
    const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    clock.start();
    watchdog.start(kFrameTimeoutMs);
    action();
    const auto synchronousMs = milliseconds(clock);
    expected = expectation();
    if (!presented && clock.elapsed() < kFrameTimeoutMs)
        loop.exec();
    presented = presented && feedbackMs <= kFrameTimeoutMs;
    return {{"synchronousApiMs", synchronousMs},
        {"operationStartToMatchingFrameSwappedMs", presented ? QJsonValue(feedbackMs) : QJsonValue::Null},
        {"frameTimedOut", !presented}, {"beforeFrameId", QString::number(previousFrameId)},
        {"presentedFrameId", QString::number(presentedFrameId)},
        {"contextGeneration", QString::number(expected->contextGeneration)},
        {"documentRevision", QString::number(expected->document.documentRevision)},
        {"historyRevision", QString::number(expected->document.historyRevision)},
        {"sessionRevision", QString::number(expected->controller.sessionRevision)},
        {"evaluationId", QString::number(expected->controller.evaluationId)},
        {"frame", expected->controller.frame}, {"mode", expected->controller.mode}};
}

void settleFrame(AnimationPerformanceWindow& fixture) {
    const auto result = measureFeedback(fixture, [&] { fixture.viewport->update(); }, [&] {
        return currentExpectation(fixture, fixture.model->apiDocumentState(),
                                  fixture.model->animationControllerState());
    });
    requireProbe(!result.value("frameTimedOut").toBool(), "Real matching frame settlement timed out");
}

template <typename Request> Request mutationRequest(const editor::SceneViewModel& model) {
    Request request;
    request.document = model.apiDocumentState().document;
    request.expectedDocumentRevision = model.apiDocumentState().documentRevision;
    return request;
}

QJsonObject measurePause(AnimationPerformanceWindow& fixture) {
    auto request = mutationRequest<editor::api::AnimationPauseRequest>(*fixture.model);
    request.expectedSessionRevision = fixture.model->animationSessionRevision();
    editor::api::ApiResult<editor::api::AnimationControlResult> result;
    return measureFeedback(fixture, [&] { result = fixture.window.apiService().pauseAnimation(request); }, [&] {
        if (result.error)
            throw std::runtime_error(result.error->message.toStdString());
        requireProbe(result.hasValue() && !result.value->diagnostic &&
                         result.value->status == editor::api::ResultStatus::Committed,
                     "Pause did not commit a complete paused controller result");
        return currentExpectation(fixture, result.value->state, *result.value);
    });
}

struct SwapStamp {
    qint64 elapsedNs = 0;
    std::uint64_t frameId = 0, context = 0, evaluation = 0, sourceRevision = 0, session = 0;
    double animationFrame = 0;
    bool ready = false, identityMatches = false, playing = false;
};

QJsonObject measurePlayback(AnimationPerformanceWindow& fixture, int durationMs) {
    auto& model = *fixture.model;
    auto& viewport = *fixture.viewport;
    requireProbe(model.setAnimationFrame(1), "Round reset failed");
    settleFrame(fixture);
    const auto beforeDocument = model.apiDocumentState();
    const auto inputBefore = fixture.inputIsolation->counters();
    const auto viewBefore = editorViewState(fixture);
    const auto beforeHistory = historyState(model);
    const auto beforeAnimation = model.scene()->animation();
    const SourceBoundary beforeSource(*model.scene());
    const auto beforeMeshes = meshState(*model.scene());
    const auto beforeGeometry = model.installedAnimationPose()->geometry;
    const auto uploadsBefore = viewport.editableMeshUploadCount();
    const auto context = viewport.contextGeneration();
    requireProbe(uploadsBefore >= 97, "The 97 editable meshes were not uploaded during warmup");
    const auto memoryBefore = processMemory();
    requireProbe(model.playAnimation(), "Official playback failed to start");
    const auto initialEvaluation = model.animationEvaluationId();
    const auto initialSession = model.animationSessionRevision();
    const auto initialFrameId = viewport.lastRenderedFrame()->frameId;
    std::vector<SwapStamp> stamps;
    stamps.reserve(10000);
    QEventLoop loop;
    QTimer finish;
    finish.setSingleShot(true);
    finish.setTimerType(Qt::PreciseTimer);
    QElapsedTimer clock;
    clock.start();
    const auto connection = QObject::connect(&viewport, &QOpenGLWidget::frameSwapped, &loop, [&] {
        // 不复制完整 ViewportState，不构造 JSON，不调用求值或断言。
        SwapStamp stamp;
        stamp.elapsedNs = clock.nsecsElapsed();
        const auto& frame = viewport.lastRenderedFrame();
        if (frame) {
            stamp.frameId = frame->frameId;
            stamp.context = frame->contextGeneration;
            stamp.ready = frame->resources.ready;
            stamp.sourceRevision = frame->state.document.documentRevision;
            stamp.session = frame->state.sessionRevision;
            const auto& identity = frame->state.animation;
            if (identity) {
                stamp.evaluation = identity->evaluationId;
                stamp.animationFrame = identity->frame;
                stamp.playing = frame->state.animationMode == renderer_gl::AnimationMode::Playing &&
                    identity->mode == renderer_gl::AnimationMode::Playing;
                stamp.identityMatches = frame->state.document.instanceId == beforeDocument.document.instanceId &&
                    frame->state.document.documentId == beforeDocument.document.documentId &&
                    frame->state.document.documentRevision == beforeDocument.documentRevision &&
                    frame->state.document.historyRevision == beforeDocument.historyRevision &&
                    identity->instanceId == beforeDocument.document.instanceId &&
                    identity->documentId == beforeDocument.document.documentId &&
                    identity->sourceRevision == beforeDocument.documentRevision &&
                    stamp.session == initialSession;
            }
        }
        stamps.push_back(stamp);
    });
    const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
    QObject::connect(&finish, &QTimer::timeout, &loop, [&] {
        const auto remainingNs = static_cast<qint64>(durationMs) * 1000000 - clock.nsecsElapsed();
        if (remainingNs > 0) {
            // 不放宽30秒门禁：系统毫秒定时边界提前时，补足实际单调时钟时长。
            finish.start(static_cast<int>((remainingNs + 999999) / 1000000));
            return;
        }
        loop.quit();
    });
    finish.start(durationMs);
    loop.exec();
    const auto durationNs = clock.nsecsElapsed();
    QObject::disconnect(connection);
    const bool stillPlaying = model.animationMode() == renderer_gl::AnimationMode::Playing;
    const bool loopEnabled = model.isAnimationLoopEnabled();
    const auto memoryAtStop = processMemory();
    const auto pause = measurePause(fixture);
    const auto uploadsAfter = viewport.editableMeshUploadCount();
    const auto afterMeshes = meshState(*model.scene());
    const auto afterHistory = historyState(model);
    const auto afterDocument = model.apiDocumentState();
    const auto inputAfter = fixture.inputIsolation->counters();
    const auto viewAfter = editorViewState(fixture);
    const bool definitionUnchanged = model.scene()->animation() == beforeAnimation;
    const bool sourceUnchanged = beforeSource.matches(*model.scene());
    const bool documentUnchanged = afterDocument.document == beforeDocument.document &&
        afterDocument.documentRevision == beforeDocument.documentRevision &&
        afterDocument.historyRevision == beforeDocument.historyRevision;

    std::vector<double> allIntervals, freshIntervals;
    allIntervals.reserve(stamps.size());
    freshIntervals.reserve(stamps.size());
    // 只核完整 24fps 墙钟槽；尾部不足一槽的时间不进入 missed ratio 分母。
    const auto expectedSlots = static_cast<std::size_t>(durationNs * kFps / 1000000000LL);
    std::vector<bool> covered(expectedSlots, false);
    std::uint64_t previousFrameId = initialFrameId, previousEvaluation = initialEvaluation;
    double previousAnimationFrame = 1;
    qint64 previousSwapNs = -1, previousFreshNs = -1;
    std::size_t freshCount = 0, invalidCount = 0, duplicateOrOldCount = 0;
    qint64 firstFreshNs = -1;
    QJsonArray rawStamps;
    for (const auto& stamp : stamps) {
        if (previousSwapNs >= 0)
            allIntervals.push_back((stamp.elapsedNs - previousSwapNs) / 1.0e6);
        previousSwapNs = stamp.elapsedNs;
        const bool valid = stamp.ready && stamp.identityMatches && stamp.playing && stamp.context == context;
        const bool fresh = valid && stamp.frameId > previousFrameId &&
            stamp.evaluation > previousEvaluation && stamp.animationFrame != previousAnimationFrame;
        if (!valid)
            ++invalidCount;
        else if (!fresh)
            ++duplicateOrOldCount;
        if (fresh) {
            if (firstFreshNs < 0)
                firstFreshNs = stamp.elapsedNs;
            ++freshCount;
            if (previousFreshNs >= 0)
                freshIntervals.push_back((stamp.elapsedNs - previousFreshNs) / 1.0e6);
            previousFreshNs = stamp.elapsedNs;
            previousFrameId = stamp.frameId;
            previousEvaluation = stamp.evaluation;
            previousAnimationFrame = stamp.animationFrame;
            const auto slot = static_cast<std::size_t>(stamp.elapsedNs * kFps / 1000000000LL);
            if (slot < covered.size())
                covered[slot] = true;
        }
        rawStamps.append(QJsonArray{static_cast<double>(stamp.elapsedNs) / 1.0e6,
            QString::number(stamp.frameId), QString::number(stamp.context),
            QString::number(stamp.evaluation), stamp.animationFrame,
            QString::number(stamp.sourceRevision), QString::number(stamp.session),
            stamp.ready, stamp.identityMatches, stamp.playing, fresh});
    }
    requireProbe(!allIntervals.empty() && !freshIntervals.empty() && expectedSlots > 0,
                 "No measurable real animation frameSwapped sequence was observed");
    const auto coveredSlots = static_cast<std::size_t>(std::count(covered.begin(), covered.end(), true));
    const auto allStats = statistics(allIntervals);
    const auto freshStats = statistics(freshIntervals);
    const bool invariants = stillPlaying && loopEnabled && documentUnchanged && definitionUnchanged &&
        sourceUnchanged && beforeHistory == afterHistory && uploadsAfter == uploadsBefore &&
        viewBefore == viewAfter &&
        viewport.contextGeneration() == context &&
        model.installedAnimationPose()->geometry == beforeGeometry && beforeMeshes == afterMeshes &&
        !pause.value("frameTimedOut").toBool() && invalidCount == 0;
    return {{"requestedDurationMs", durationMs}, {"actualDurationMs", durationNs / 1.0e6},
        {"inputIsolationBefore", inputBefore}, {"inputIsolationAfter", inputAfter},
        {"blockedInputEventDelta", inputAfter.value("blockedInputEvents").toInteger() -
            inputBefore.value("blockedInputEvents").toInteger()},
        {"editorViewBefore", viewBefore}, {"editorViewAfter", viewAfter},
        {"firstNewAnimationSwapWaitMs", firstFreshNs / 1.0e6},
        {"lastNewAnimationSwapToStopMs", (durationNs - previousFreshNs) / 1.0e6},
        {"memoryBefore", memoryBefore}, {"memoryAtPlaybackStop", memoryAtStop},
        {"memoryAfterPause", processMemory()}, {"pause", pause},
        {"allFrameSwappedIntervals", allStats}, {"newAnimationFrameSwappedIntervals", freshStats},
        {"allFrameSwappedIntervalSamplesMs", samplesJson(allIntervals)},
        {"newAnimationFrameSwappedIntervalSamplesMs", samplesJson(freshIntervals)},
        {"swapSignalCount", static_cast<qint64>(stamps.size())},
        {"newAnimationFrameCount", static_cast<qint64>(freshCount)},
        {"invalidIdentityOrResourceSwapCount", static_cast<qint64>(invalidCount)},
        {"duplicateOrOldAnimationSwapCount", static_cast<qint64>(duplicateOrOldCount)},
        {"fpsSlotCount", static_cast<qint64>(expectedSlots)},
        {"fpsSlotsWithNewAnimationFrame", static_cast<qint64>(coveredSlots)},
        {"missedRatio24Fps", static_cast<double>(expectedSlots - coveredSlots) / expectedSlots},
        {"missedRatioDefinition", "Uncovered complete wall-clock 1/24s slots / complete slots; only a new "
            "paint frameId and new evaluationId with a changed animation frame can cover a slot. "
            "Repeated compositions do not cover slots; the final partial slot is excluded."},
        {"rawSwapStampColumns", QJsonArray{"elapsedMs", "frameId", "contextGeneration", "evaluationId",
            "animationFrame", "documentRevision", "sessionRevision", "resourcesReady", "identityMatches",
            "playing", "newAnimationFrame"}}, {"rawSwapStamps", rawStamps},
        {"historyBefore", beforeHistory}, {"historyAfterPause", afterHistory},
        {"documentRevisionBefore", QString::number(beforeDocument.documentRevision)},
        {"documentRevisionAfterPause", QString::number(afterDocument.documentRevision)},
        {"historyRevisionBefore", QString::number(beforeDocument.historyRevision)},
        {"historyRevisionAfterPause", QString::number(afterDocument.historyRevision)},
        {"meshesBefore", beforeMeshes}, {"meshesAfterPause", afterMeshes},
        {"editableMeshSuccessfulUploadsBefore", QString::number(uploadsBefore)},
        {"editableMeshSuccessfulUploadsAfter", QString::number(uploadsAfter)},
        {"editableMeshSuccessfulUploadDelta", static_cast<qint64>(uploadsAfter) - static_cast<qint64>(uploadsBefore)},
        {"documentAndHistoryRevisionsUnchanged", documentUnchanged},
        {"formalDefinitionUnchanged", definitionUnchanged}, {"baseTrsContentAndMeshRevisionsUnchanged", sourceUnchanged},
        {"poseGeometryPointerReused", model.installedAnimationPose()->geometry == beforeGeometry},
        {"stayedPlayingUntilStop", stillPlaying}, {"loopEnabled", loopEnabled},
        {"phaseInvariantsPassed", invariants},
        {"acceptancePassed", invariants && durationNs >= static_cast<qint64>(durationMs) * 1000000 &&
            allStats.value("p95Ms").toDouble() <= kSwapP95BudgetMs &&
            freshStats.value("p95Ms").toDouble() <= kSwapP95BudgetMs}};
}

QJsonObject measureCore(AnimationPerformanceWindow& fixture) {
    requireProbe(fixture.model->animationMode() == renderer_gl::AnimationMode::PreviewPaused,
                 "Core measurement requires stopped playback");
    std::string error;
    const auto inputs = fixture.model->scene()->animationPoseInputs(error);
    requireProbe(inputs && inputs->size() == 102, "Core fixture input preparation failed");
    const auto& definition = fixture.model->scene()->animation();
    // 输入构造与预热在计时外；完整 checked 入口自身的 9000 键校验与结果分配保留。
    for (int index = 0; index < 48; ++index) {
        const auto warm = core::evaluateAnimationPose(*inputs, definition, 1 + index * .5);
        requireProbe(warm.pose && warm.validation.isValid(), "Core warmup failed");
    }
    const auto memoryBefore = processMemory();
    std::vector<double> samples;
    samples.reserve(kCoreSamples);
    QElapsedTimer timer;
    double checksum = 0;
    for (int index = 0; index < kCoreSamples; ++index) {
        const auto frame = 1 + (kLastFrame - 1.0) * index / (kCoreSamples - 1);
        timer.start();
        const auto result = core::evaluateAnimationPose(*inputs, definition, frame);
        const auto elapsed = milliseconds(timer);
        samples.push_back(elapsed);
        requireProbe(result.pose && result.validation.isValid() && result.pose->nodes.size() == 102,
                     "A measured Core evaluation rejected the neutral fixture");
        checksum += result.pose->nodes.back().world[3].x;
    }
    const auto metrics = statistics(samples);
    return {{"metrics", metrics}, {"samplesMs", samplesJson(samples)}, {"resultChecksum", checksum},
        {"memoryBefore", memoryBefore}, {"memoryAfter", processMemory()},
        {"timingDefinition", "Complete checked core::evaluateAnimationPose, including definition validation "
            "and result allocation; excludes inputs construction, result consumption/destruction, GUI, "
            "geometry, upload and frameSwapped. Playback is paused throughout this independent lane."},
        {"acceptancePassed", metrics.value("p95Ms").toDouble() <= kCoreP95BudgetMs}};
}

QJsonObject machineState(const AnimationPerformanceWindow& fixture) {
    auto& viewport = *fixture.viewport;
    viewport.makeCurrent();
    QOpenGLFunctions_4_1_Core gl;
    requireProbe(gl.initializeOpenGLFunctions(), "Actual OpenGL functions are unavailable");
    const auto gpu = QJsonObject{
        {"vendor", reinterpret_cast<const char*>(gl.glGetString(GL_VENDOR))},
        {"renderer", reinterpret_cast<const char*>(gl.glGetString(GL_RENDERER))},
        {"version", reinterpret_cast<const char*>(gl.glGetString(GL_VERSION))}};
    viewport.doneCurrent();
    QSettings cpu(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                  QSettings::NativeFormat);
    const auto* screen = fixture.window.screen();
    const auto& rendered = viewport.lastRenderedFrame();
    return {{"cpu", cpu.value(QStringLiteral("ProcessorNameString")).toString()},
        {"logicalProcessors", QThread::idealThreadCount()}, {"architecture", QSysInfo::currentCpuArchitecture()},
        {"os", QSysInfo::prettyProductName()}, {"kernelVersion", QSysInfo::kernelVersion()},
        {"qt", qVersion()}, {"gpu", gpu},
        {"requestedSwapInterval", renderer_gl::ViewportWidget::defaultSurfaceFormat().swapInterval()},
        {"surfaceSwapInterval", viewport.format().swapInterval()},
        {"contextSwapInterval", viewport.context()->format().swapInterval()},
        {"surfaceSwapBehavior", static_cast<int>(viewport.format().swapBehavior())},
        {"screenRefreshRateHz", screen ? screen->refreshRate() : 0},
        {"screenName", screen ? screen->name() : QString{}},
        {"windowLogicalSize", QJsonArray{fixture.window.width(), fixture.window.height()}},
        {"windowPixels", QJsonArray{qRound(fixture.window.width() * fixture.window.devicePixelRatioF()),
                                   qRound(fixture.window.height() * fixture.window.devicePixelRatioF())}},
        {"windowDpr", fixture.window.devicePixelRatioF()},
        {"viewportLogicalSize", QJsonArray{viewport.width(), viewport.height()}},
        {"viewportPixels", QJsonArray{rendered->state.pixelSize.width(), rendered->state.pixelSize.height()}},
        {"viewportDpr", viewport.devicePixelRatioF()},
        {"windowExposed", fixture.window.windowHandle() && fixture.window.windowHandle()->isExposed()},
        {"contextGeneration", QString::number(viewport.contextGeneration())}};
}

void writeNewJson(const QString& path, const QJsonObject& report) {
    QFile file(path);
    requireProbe(file.open(QIODevice::WriteOnly | QIODevice::NewOnly),
                 "New JSON evidence path could not be opened (overwrite is forbidden)");
    const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
    requireProbe(file.write(bytes) == bytes.size() && file.flush(), "JSON evidence write failed");
}

void writeNewPng(const QString& path, const QImage& image) {
    requireProbe(!image.isNull(), "Real screenshot is empty");
    QFile file(path);
    requireProbe(file.open(QIODevice::WriteOnly | QIODevice::NewOnly),
                 "New PNG evidence path could not be opened (overwrite is forbidden)");
    requireProbe(image.save(&file, "PNG") && file.flush(), "Real PNG evidence write failed");
}
} // namespace
#endif

TEST_CASE("Native animation Release neutral fixture performance acceptance",
          "[.][animation-performance]") {
#ifndef NDEBUG
    FAIL("Animation performance acceptance requires an explicit Release build");
#else
    const QString output = qEnvironmentVariable("MINI3D_ANIMATION_PERFORMANCE_EVIDENCE");
    const QFileInfo directory(output);
    REQUIRE(!output.isEmpty());
    REQUIRE(QDir::isAbsolutePath(output));
    REQUIRE(directory.isDir());
    const auto canonical = directory.canonicalFilePath();
    REQUIRE(canonical.startsWith(QStringLiteral("E:/"), Qt::CaseInsensitive));
    REQUIRE(canonical.size() > 3);
    const QStorageInfo storage(canonical);
    REQUIRE(storage.isValid());
    REQUIRE(storage.isReady());
    REQUIRE_FALSE(storage.isReadOnly());
    REQUIRE(storage.bytesAvailable() >= 32 * 1024 * 1024);
    const QString jsonPath = canonical + QStringLiteral("/animation-performance.json");
    const QString viewportPath = canonical + QStringLiteral("/animation-performance-viewport.png");
    const QString windowPath = canonical + QStringLiteral("/animation-performance-window.png");
    REQUIRE_FALSE(QFileInfo::exists(jsonPath));
    REQUIRE_FALSE(QFileInfo::exists(viewportPath));
    REQUIRE_FALSE(QFileInfo::exists(windowPath));
    QJsonObject report{
        {"schema", "mini3d-native-animation-r5-performance-v1"}, {"buildConfiguration", "Release"},
        {"fixture", QJsonObject{{"nodes", 102}, {"animatedObjects", 100}, {"editableMeshes", 97},
            {"tracks", 300}, {"keysPerTrack", 30}, {"totalKeys", 9000}, {"sourceVertices", 48500},
            {"gridColumns", 25}, {"gridRows", 20}, {"untrackedDescendants", 2},
            {"fps", kFps}, {"startFrame", 1}, {"endFrame", static_cast<int>(kLastFrame)},
            {"definitionPreflightNodeVisits", 102 * 30},
            {"editPreflightNodeVisitUpperBound", 102 * 31},
            {"initialization", "All objects, source meshes, modifiers and the final definition use "
                "the official SceneViewModel paths and shared undo history."}}},
        {"budgets", QJsonObject{{"coreP95Ms", kCoreP95BudgetMs}, {"frameSwappedP95Ms", kSwapP95BudgetMs}}},
        {"limitations", QJsonArray{
            "The visible isolated fixture does not activate itself; its last-installed application filter "
                "blocks only its own widget mouse/key/wheel/shortcut input. Paint/timers/focus/lifecycle remain live. "
                "This is a no-navigation workload, not an interactive input-latency benchmark.",
            "Operation timings enter MainWindow EditorApiService, including admission and commit guards, "
                "but exclude bridge/MCP transport and request serialization.",
            "Pass criteria are Core/adjacent-swap P95 and correctness invariants only; missed ratio, "
                "boundary waits, memory and operation latency have no newly invented acceptance thresholds.",
            "frameSwapped is Qt/compositor feedback, not a timestamp of monitor scanout.",
            "All swaps and fresh animation frames are reported separately; average FPS is not a pass criterion.",
            "Synchronous action timing is API wall time, not mouse-event queue latency; feedback includes the API.",
            "Psapi peaks are cumulative since process start, including fixture/history/Qt/GL/probe buffers. "
                "Stage working set/private usage is not exact history-command allocation.",
            "Undo intentionally retains the redo branch; retained process memory alone does not prove a leak.",
            "No independent modifierRevision exists: actual options, immutable content identities, "
                "topologyRevision, geometryRevision and evaluationRevision are checked at stage boundaries.",
            "The runner must verify the supplied E: directory ancestry/storage/writability and redirect child temps."}},
        {"memoryBeforeWindow", processMemory()}, {"completed", false}, {"acceptancePassed", false}};
    try {
        AnimationPerformanceWindow fixture;
        settleFrame(fixture);
        report["machine"] = machineState(fixture);
        report["historyAfterFixture"] = historyState(*fixture.model);
        report["memoryAfterFixture"] = processMemory();
        report["initialMeshes"] = meshState(*fixture.model->scene());
        report["fixtureEntityIds"] = QJsonObject{{"parentEmpty", QString::number(fixture.parent)},
            {"rotor", QString::number(fixture.rotor)}, {"camera", QString::number(fixture.camera)},
            {"light", QString::number(fixture.light)}, {"untrackedChild", QString::number(fixture.child)},
            {"untrackedGrandchild", QString::number(fixture.grandchild)}};
        std::cout << "Animation performance: real Release warmup 5s, followed by 30s x 3; evidence="
                  << canonical.toStdString() << std::endl;
        const auto warmup = measurePlayback(fixture, kWarmupMs);
        report["warmup"] = warmup;
        QJsonArray rounds;
        bool passed = warmup.value("phaseInvariantsPassed").toBool();
        for (int round = 0; round < kRounds; ++round) {
            std::cout << "Animation performance round " << (round + 1) << '/' << kRounds << std::endl;
            QJsonObject row{{"round", round + 1}, {"core", measureCore(fixture)}};
            row["playback"] = measurePlayback(fixture, kRoundMs);
            passed = passed && row.value("core").toObject().value("acceptancePassed").toBool() &&
                row.value("playback").toObject().value("acceptancePassed").toBool();
            rounds.append(row);
            report["rounds"] = rounds;
        }
        auto& model = *fixture.model;
        const SourceBoundary sourceBeforeEdits(*model.scene());
        const auto definitionBeforeEdits = model.scene()->animation();
        const auto historyBeforeEdits = historyState(model);
        const auto baseHistoryCount = model.undoStack()->count();
        const auto baseHistoryIndex = model.undoStack()->index();
        const auto uploadsBeforeEdits = fixture.viewport->editableMeshUploadCount();
        QJsonObject edits{{"historyBefore", historyBeforeEdits}, {"memoryBefore", processMemory()}};
        auto seekRequest = mutationRequest<editor::api::AnimationSetFrameRequest>(model);
        seekRequest.expectedSessionRevision = model.animationSessionRevision();
        seekRequest.frame = 25;
        editor::api::ApiResult<editor::api::AnimationControlResult> seek;
        const auto seekTiming = measureFeedback(fixture, [&] { seek = fixture.window.apiService().setAnimationFrame(seekRequest); }, [&] {
            if (seek.error)
                throw std::runtime_error(seek.error->message.toStdString());
            requireProbe(seek.hasValue() && seek.value->status == editor::api::ResultStatus::Committed,
                         "Measured seek must be a real changed frame");
            return currentExpectation(fixture, seek.value->state, *seek.value);
        });
        edits["seek"] = seekTiming;
        report["edits"] = edits;

        auto singleRequest = mutationRequest<editor::api::AnimationUpsertKeyframesRequest>(model);
        singleRequest.onConflict = editor::api::AnimationConflict::Replace;
        const auto& rotation = model.scene()->animation().tracks.at(
            {fixture.rotor, core::AnimationChannel::RotationEulerXYZDegrees}).keys.at(1);
        editor::api::AnimationUpsertItem single;
        single.entityId = fixture.rotor;
        single.channel = core::AnimationChannel::RotationEulerXYZDegrees;
        single.frame = rotation.frame;
        single.value = rotation.value + glm::dvec3(0, .25, 0);
        singleRequest.items.push_back(single);
        editor::api::ApiResult<editor::api::MutationResult> singleResult;
        const auto singleTiming = measureFeedback(fixture, [&] {
            singleResult = fixture.window.apiService().upsertAnimationKeyframes(singleRequest);
        }, [&] {
            if (singleResult.error)
                throw std::runtime_error(singleResult.error->message.toStdString());
            requireProbe(singleResult.hasValue() && singleResult.value->status == editor::api::ResultStatus::Committed &&
                model.undoStack()->count() == baseHistoryCount + 1 &&
                model.undoStack()->index() == baseHistoryIndex + 1 &&
                singleResult.value->state.documentRevision == singleRequest.expectedDocumentRevision + 1,
                "Single-key measurement did not commit exactly one shared history entry");
            return currentExpectation(fixture, singleResult.value->state, model.animationControllerState());
        });
        edits["singleKey"] = singleTiming;
        edits["memoryAfterSingleKey"] = processMemory();
        report["edits"] = edits;

        auto batchRequest = mutationRequest<editor::api::AnimationUpsertKeyframesRequest>(model);
        batchRequest.onConflict = editor::api::AnimationConflict::Replace;
        batchRequest.items.reserve(1024);
        for (const auto& [id, track] : model.scene()->animation().tracks) {
            for (const auto& key : track.keys) {
                editor::api::AnimationUpsertItem item;
                item.entityId = id.first;
                item.channel = id.second;
                item.frame = key.frame;
                item.interpolation = key.interpolation;
                item.value = key.value;
                if (item.channel == core::AnimationChannel::RotationEulerXYZDegrees)
                    item.value.y += .25;
                else
                    item.value.x += .001;
                batchRequest.items.push_back(item);
                if (batchRequest.items.size() == 1024)
                    break;
            }
            if (batchRequest.items.size() == 1024)
                break;
        }
        requireProbe(batchRequest.items.size() == 1024, "Batch did not contain 1024 unique changed existing keys");
        editor::api::ApiResult<editor::api::MutationResult> batchResult;
        const auto batchTiming = measureFeedback(fixture, [&] {
            batchResult = fixture.window.apiService().upsertAnimationKeyframes(batchRequest);
        }, [&] {
            if (batchResult.error)
                throw std::runtime_error(batchResult.error->message.toStdString());
            requireProbe(batchResult.hasValue() && batchResult.value->status == editor::api::ResultStatus::Committed &&
                model.undoStack()->count() == baseHistoryCount + 2 &&
                model.undoStack()->index() == baseHistoryIndex + 2 &&
                batchResult.value->state.documentRevision == batchRequest.expectedDocumentRevision + 1,
                "1024-key measurement did not commit exactly one shared history entry");
            return currentExpectation(fixture, batchResult.value->state, model.animationControllerState());
        });
        edits["batch1024"] = batchTiming;
        edits["batchItemCount"] = static_cast<int>(batchRequest.items.size());
        edits["batchReusesExistingThirtyFrameTimes"] = true;
        edits["singleAndBatchResultStatus"] = QStringLiteral("committed");
        edits["historyAfterBatch"] = historyState(model);
        edits["memoryAfterBatch"] = processMemory();
        model.undo();
        model.undo();
        settleFrame(fixture);
        edits["historyAfterUndo"] = historyState(model);
        edits["memoryAfterUndoWithRedoRetained"] = processMemory();
        edits["exactDefinitionRestored"] = model.scene()->animation() == definitionBeforeEdits;
        edits["baseTrsContentRevisionsAndOptionsUnchanged"] = sourceBeforeEdits.matches(*model.scene());
        edits["redoBranchRetained"] = model.undoStack()->count() == baseHistoryCount + 2 &&
            model.undoStack()->index() == baseHistoryIndex && model.undoStack()->canRedo();
        edits["editableMeshSuccessfulUploadDelta"] =
            static_cast<qint64>(fixture.viewport->editableMeshUploadCount()) - static_cast<qint64>(uploadsBeforeEdits);
        const bool editPassed = !seekTiming.value("frameTimedOut").toBool() &&
            !singleTiming.value("frameTimedOut").toBool() && !batchTiming.value("frameTimedOut").toBool() &&
            edits.value("exactDefinitionRestored").toBool() &&
            edits.value("baseTrsContentRevisionsAndOptionsUnchanged").toBool() &&
            edits.value("redoBranchRetained").toBool() && edits.value("editableMeshSuccessfulUploadDelta").toInteger() == 0;
        edits["acceptancePassed"] = editPassed;
        report["edits"] = edits;
        passed = passed && editPassed;

        requireProbe(model.setAnimationFrame(175), "Evidence frame seek failed");
        settleFrame(fixture);
        const auto capture = fixture.viewport->grabStampedFramebuffer();
        requireProbe(capture && matchesFrame(capture->frame,
            currentExpectation(fixture, model.apiDocumentState(), model.animationControllerState()), 0),
            "Real viewport screenshot does not match the evidence pose");
        writeNewPng(viewportPath, capture->image);
        writeNewPng(windowPath, fixture.window.grab().toImage());
        report["screenshots"] = QJsonObject{{"viewport", viewportPath}, {"window", windowPath},
            {"frame", capture->frame.state.animation->frame},
            {"frameId", QString::number(capture->frame.frameId)},
            {"evaluationId", QString::number(capture->frame.state.animation->evaluationId)},
            {"contextGeneration", QString::number(capture->frame.contextGeneration)}};
        report["completed"] = true;
        report["acceptancePassed"] = passed;
        report["memoryBeforeWindowTeardown"] = processMemory();
    } catch (const std::exception& error) {
        report["error"] = QString::fromUtf8(error.what());
        writeNewJson(jsonPath, report);
        FAIL("Animation performance probe incomplete: " << error.what());
    }
    report["memoryAfterWindowTeardown"] = processMemory();
    writeNewJson(jsonPath, report);
    std::cout << "Animation performance JSON: " << jsonPath.toStdString() << std::endl;
    INFO(jsonPath.toStdString());
    REQUIRE(report.value("completed").toBool());
    REQUIRE(report.value("acceptancePassed").toBool());
#endif
}
