/*
 * 模块名: AnimationObservationTests
 * 功能概述: 用真实 Qt/GL 验证指定动画帧的完整身份、主动失效和捕获重入隔离。
 * 对外接口: Catch2 [animation-observation]；不启动桥、不写用户场景。
 * 依赖关系: MainWindow、SceneViewModel、ObservationService/Codec、Qt Test。
 * 输入输出: 隔离动画、显式捕获请求到真实 PNG、绘制戳和结构化失败。
 * 异常与错误: 假身份、旧会话、活动播放/草稿和晚回调均不能产生成功截图。
 * 维护说明: 应用线程串行，条件等待仅用于测试；不构造伪造 PNG 充当真实帧。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/AnimationDraftGesture.h"
#include "editor/observation/ObservationJsonCodec.h"
#include "editor/observation/ObservationService.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QOpenGLContext>
#include <QPointer>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <set>

using namespace mini3d;
namespace {
namespace api = editor::api;
namespace observation = editor::observation;
using Viewport = renderer_gl::ViewportWidget;
using Error = api::ErrorCode;

/** @brief 提供正式应用装配的隔离动画窗口；析构隐藏窗口并停止会话。 */
struct AnimationObservationWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = nullptr;
    Viewport* viewport = nullptr;
    core::EntityId cube = 0;
    AnimationObservationWindow() {
        window.show();
        REQUIRE(QTest::qWaitForWindowExposed(&window));
        model = window.findChild<editor::SceneViewModel*>();
        viewport = window.findChild<Viewport*>();
        REQUIRE(model);
        REQUIRE(viewport);
        model->newScene();
        cube = model->createEntity(core::PrimitiveKind::Cube);
        REQUIRE(cube != 0);
        core::SceneAnimation animation;
        animation.settings = {24, 1, 49};
        animation.tracks[{cube, core::AnimationChannel::Position}] = {
            {{1, {0, 0, 0}, core::AnimationInterpolation::Linear},
             {49, {4, 0, 0}, core::AnimationInterpolation::Linear}}};
        animation.tracks[{cube, core::AnimationChannel::RotationEulerXYZDegrees}] = {
            {{1, {0, 0, 0}, core::AnimationInterpolation::Linear},
             {49, {0, 720, 0}, core::AnimationInterpolation::Linear}}};
        REQUIRE(model->replaceAnimation(animation));
    }
    ~AnimationObservationWindow() { window.hide(); }
    /** @brief 读取同应用线程的真实显示包，供错误身份的定点注入使用。 */
    renderer_gl::FrameDisplayState displayState() const {
        const auto state = model->apiDocumentState();
        return {{state.document.instanceId, state.document.documentId,
                 state.documentRevision, state.historyRevision},
                {state.document.instanceId, state.document.documentId, state.documentRevision,
                 model->animationEvaluationId(), model->animationFrame(), model->animationMode()},
                model->animationSessionRevision(), model->installedAnimationPose()};
    }
    /** @brief 冻结当前文档、观察版本及求值 ID；要求观察服务可用。 */
    observation::CaptureRequest request() {
        const auto view = window.observationService().getState({model->apiDocumentState().document});
        REQUIRE(view.hasValue());
        REQUIRE(view.value->viewport.animation);
        observation::CaptureRequest request;
        request.document = model->apiDocumentState().document;
        request.expectedDocumentRevision = model->apiDocumentState().documentRevision;
        request.expectedViewportRevision = view.value->viewport.viewportRevision;
        request.expectedEvaluationId = view.value->viewport.animation->evaluationId;
        request.longestEdge = 320;
        return request;
    }
    /** @brief 将正式动画安装为指定暂停帧，不使用 sample 冒充 GUI 显示。 */
    void preview(core::FrameTime frame = 13) {
        REQUIRE(model->setAnimationPreview(true));
        REQUIRE(model->setAnimationFrame(frame));
    }
};

/** @brief 持有一次真实异步请求；退出测试作用域时先取消，避免回调悬空。 */
struct CaptureAttempt {
    observation::ObservationService& service;
    std::optional<api::ApiResult<observation::CaptureResult>> result;
    std::optional<renderer_gl::RenderedFrame> completionFrame;
    QString id;
    int calls = 0;
    CaptureAttempt(AnimationObservationWindow& fixture, const observation::CaptureRequest& request)
        : service(fixture.window.observationService()) {
        id = service.capture(request, [this, viewport = fixture.viewport](auto completed) {
            ++calls;
            completionFrame = viewport->lastRenderedFrame();
            result = std::move(completed);
        });
    }
    ~CaptureAttempt() { service.cancelCapture(id); }
    void wait() {
        REQUIRE(QTest::qWaitFor([this] { return result.has_value(); }, 6000));
        REQUIRE(calls == 1);
        REQUIRE_FALSE(service.isCapturing());
    }
    void requireError(Error error) const {
        REQUIRE(result);
        REQUIRE(result->error);
        REQUIRE(result->error->code == error);
        REQUIRE_FALSE(result->hasValue());
        REQUIRE(calls == 1);
    }
};
} // namespace

TEST_CASE("Capture wire requires a canonical evaluation ID and permits real zero",
          "[animation-observation][observation-canonical]") {
    editor::SceneViewModel model;
    const auto state = model.apiDocumentState();
    QJsonObject params{
        {"document", QJsonObject{{"instanceId", state.document.instanceId},
                                  {"documentId", state.document.documentId}}},
        {"expectedDocumentRevision", QString::number(state.documentRevision)},
        {"expectedViewportRevision", "1"}, {"expectedEvaluationId", "0"}};
    const auto decoded = observation::ObservationJsonCodec::decodeRequest("viewport.capture", params);
    REQUIRE(decoded.hasValue());
    REQUIRE(std::get<observation::CaptureRequest>(*decoded.value).expectedEvaluationId == 0);
    const auto canonical = observation::ObservationJsonCodec::canonicalParams("viewport.capture", params);
    REQUIRE(canonical.hasValue());
    REQUIRE((*canonical.value)["expectedEvaluationId"] == "0");
    auto missing = params;
    missing.remove("expectedEvaluationId");
    const auto missingResult = observation::ObservationJsonCodec::decodeRequest("viewport.capture", missing);
    REQUIRE(missingResult.error);
    REQUIRE(missingResult.error->fieldPath == "expectedEvaluationId");
    for (const auto value : {QJsonValue(0), QJsonValue("00"), QJsonValue("-1"),
                            QJsonValue("18446744073709551616"), QJsonValue(true)}) {
        auto invalid = params;
        invalid["expectedEvaluationId"] = value;
        const auto result = observation::ObservationJsonCodec::decodeRequest("viewport.capture", invalid);
        REQUIRE(result.error);
        REQUIRE(result.error->fieldPath == "expectedEvaluationId");
    }
}

TEST_CASE("Initialized viewport never cleans derived resources during its base destructor",
          "[animation-observation][viewport-lifetime]") {
    auto viewport = std::make_unique<Viewport>();
    viewport->resize(640, 480);
    viewport->show();
    REQUIRE(QTest::qWaitForWindowExposed(viewport.get()));
    REQUIRE(QTest::qWaitFor([&] {
        return viewport->context() && viewport->context()->isValid();
    }));
    QObject observer;
    QOpenGLWidget* base = viewport.get();
    int invalidations = 0;
    bool lateCleanup = false;
    QObject::connect(viewport.get(), &Viewport::observationUnavailable, &observer, [&] {
        ++invalidations;
        // 信号发出时基类仍存活；动态类型已退化表示派生成员生命周期已结束。
        lateCleanup |= !base->metaObject()->inherits(&Viewport::staticMetaObject);
    });
    viewport.reset();
    REQUIRE(invalidations > 0);
    REQUIRE_FALSE(lateCleanup);
}

TEST_CASE("Window destroys its animation input filter before the referenced model",
          "[animation-observation][animation-window-lifetime]") {
    QObject observer;
    auto window = std::make_unique<editor::MainWindow>();
    window->show();
    REQUIRE(QTest::qWaitForWindowExposed(window.get()));
    auto* model = window->findChild<editor::SceneViewModel*>();
    QPointer<editor::AnimationDraftGesture> gesture =
        window->findChild<editor::AnimationDraftGesture*>();
    REQUIRE(model);
    REQUIRE(gesture);
    bool modelDestroyed = false;
    bool filterStillAlive = false;
    QObject::connect(model, &QObject::destroyed, &observer, [&] {
        modelDestroyed = true;
        filterStillAlive = !gesture.isNull();
        // 旧实现只记录错误顺序，不故意让后续控件析构事件访问已死模型。
        if (gesture)
            qApp->removeEventFilter(gesture.data());
    });
    window.reset();
    REQUIRE(modelDestroyed);
    REQUIRE_FALSE(filterStillAlive);
}

TEST_CASE("Base capture reports the actual controller identity including initial zero",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    REQUIRE(fixture.model->animationEvaluationId() == 0);
    auto request = fixture.request();
    REQUIRE(request.expectedEvaluationId == 0);
    {
        CaptureAttempt capture(fixture, request);
        capture.wait();
        REQUIRE(capture.result->hasValue());
        const auto& view = capture.result->value->view.viewport;
        REQUIRE(view.animation);
        REQUIRE(view.animation->evaluationId == fixture.model->animationEvaluationId());
        REQUIRE(view.animation->frame == fixture.model->animationFrame());
        REQUIRE(view.animation->mode == renderer_gl::AnimationMode::Base);
        REQUIRE(view.sessionRevision == fixture.model->animationSessionRevision());
        const auto json = observation::ObservationJsonCodec::encode(capture.result->value->view);
        REQUIRE(json["evaluationId"] == "0");
        REQUIRE(json["mode"] == "base");
        REQUIRE(json["frame"].toDouble() == fixture.model->animationFrame());
        REQUIRE(json["sessionRevision"] == QString::number(fixture.model->animationSessionRevision()));
    }
    fixture.preview(25);
    REQUIRE(fixture.model->setAnimationPreview(false));
    request = fixture.request();
    REQUIRE(request.expectedEvaluationId != 0);
    REQUIRE_FALSE(fixture.model->installedAnimationPose());
    {
        auto stale = request;
        stale.expectedEvaluationId = 0;
        CaptureAttempt capture(fixture, stale);
        REQUIRE(capture.id.isEmpty());
        capture.requireError(Error::StaleEvaluation);
    }
    CaptureAttempt current(fixture, request);
    current.wait();
    REQUIRE(current.result->hasValue());
    REQUIRE(current.result->value->view.viewport.animation->evaluationId == request.expectedEvaluationId);
}

TEST_CASE("Unassembled renderer still paints but cannot claim controlled observation identity",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    const auto request = fixture.request();
    fixture.viewport->setFrameDisplayStateProvider({});
    const auto before = fixture.viewport->lastRenderedFrame()
                            ? fixture.viewport->lastRenderedFrame()->frameId : 0;
    fixture.viewport->update();
    REQUIRE(QTest::qWaitFor([&] {
        return fixture.viewport->lastRenderedFrame() &&
               fixture.viewport->lastRenderedFrame()->frameId > before;
    }));
    REQUIRE(fixture.viewport->lastRenderedFrame()->resources.ready);
    REQUIRE_FALSE(fixture.viewport->lastRenderedFrame()->state.animation);
    REQUIRE_FALSE(fixture.viewport->observationState());
    REQUIRE_FALSE(fixture.viewport->grabStampedFramebuffer());
    CaptureAttempt capture(fixture, request);
    REQUIRE(capture.id.isEmpty());
    capture.requireError(Error::ViewportUnavailable);
}

TEST_CASE("Viewport rejects each pose identity field that differs from the frozen controller",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    const auto display = fixture.displayState();
    for (int field = 0; field < 9; ++field) {
        CAPTURE(field);
        auto invalid = display;
        auto pose = std::make_shared<renderer_gl::InstalledPose>(*display.pose);
        switch (field) {
            case 0: pose->identity.instanceId = "other-instance"; break;
            case 1: pose->identity.documentId = "other-document"; break;
            case 2: ++pose->identity.sourceRevision; break;
            case 3: ++pose->identity.evaluationId; break;
            case 4: pose->identity.frame += .5; break;
            case 5: pose->identity.mode = renderer_gl::AnimationMode::Playing; break;
            case 6: pose->numerics.reset(); break;
            case 7: pose->geometry.reset(); break;
            case 8: invalid.identity.frame = std::numeric_limits<double>::quiet_NaN(); break;
        }
        invalid.pose = std::move(pose);
        fixture.viewport->setFrameDisplayStateProvider([invalid] { return invalid; });
        REQUIRE_FALSE(fixture.viewport->observationState());
        REQUIRE_FALSE(fixture.viewport->grabStampedFramebuffer());
    }
    fixture.viewport->setFrameDisplayStateProvider([display] { return display; });
    REQUIRE(fixture.viewport->observationState());
}

TEST_CASE("Paused capture uses a new real paint and the actual stamp after a second grab paint",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    std::set<QString> hashes;
    for (const core::FrameTime target : {13.0, 13.5, 25.0, 37.0, 49.0}) {
        CAPTURE(target);
        REQUIRE(fixture.model->setAnimationFrame(target));
        const auto request = fixture.request();
        const auto document = fixture.model->apiDocumentState();
        const auto session = fixture.model->animationSessionRevision();
        const auto before = fixture.viewport->lastRenderedFrame()
                                ? fixture.viewport->lastRenderedFrame()->frameId : 0;
        QSignalSpy painted(fixture.viewport, &Viewport::framePainted);
        std::uint64_t firstPaint = 0;
        const auto connection = QObject::connect(fixture.viewport, &Viewport::framePainted,
                                                  fixture.viewport, [&] {
            if (firstPaint == 0)
                firstPaint = fixture.viewport->lastRenderedFrame()->frameId;
        });
        const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
        CaptureAttempt capture(fixture, request);
        capture.wait();
        REQUIRE(capture.result->hasValue());
        const auto& result = *capture.result->value;
        REQUIRE(painted.count() >= 2);
        REQUIRE(result.frameId > before);
        REQUIRE(result.frameId > firstPaint);
        REQUIRE(capture.completionFrame);
        REQUIRE(result.frameId == capture.completionFrame->frameId);
        REQUIRE(result.view.viewport.document == capture.completionFrame->state.document);
        REQUIRE(result.view.viewport.viewportRevision ==
                capture.completionFrame->state.viewportRevision);
        REQUIRE(result.view.viewport.animation == capture.completionFrame->state.animation);
        REQUIRE(result.view.viewport.sessionRevision == capture.completionFrame->state.sessionRevision);
        REQUIRE(result.contextGeneration == capture.completionFrame->contextGeneration);
        REQUIRE(result.contextGeneration == fixture.viewport->contextGeneration());
        REQUIRE(result.view.viewport.document.instanceId == document.document.instanceId);
        REQUIRE(result.view.viewport.document.documentId == document.document.documentId);
        REQUIRE(result.view.viewport.document.documentRevision == request.expectedDocumentRevision);
        REQUIRE(result.view.viewport.viewportRevision == request.expectedViewportRevision);
        REQUIRE(result.view.viewport.animation == fixture.model->installedAnimationPose()->identity);
        REQUIRE(result.view.viewport.animation->evaluationId == request.expectedEvaluationId);
        REQUIRE(result.view.viewport.animation->frame == target);
        REQUIRE(result.view.viewport.animation->mode == renderer_gl::AnimationMode::PreviewPaused);
        REQUIRE(result.view.viewport.sessionRevision == session);
        const auto image = QImage::fromData(result.png, "PNG");
        REQUIRE_FALSE(image.isNull());
        REQUIRE(image.size() == result.outputPixelSize);
        REQUIRE(result.sha256 == QString::fromLatin1(
            QCryptographicHash::hash(result.png, QCryptographicHash::Sha256).toHex()));
        REQUIRE(hashes.insert(result.sha256).second);
        const auto evaluation = fixture.model->animationEvaluationId();
        REQUIRE(fixture.model->setAnimationFrame(target));
        REQUIRE(fixture.model->animationEvaluationId() == evaluation);
    }
}

TEST_CASE("Base loop changes actively end pending capture without waiting for paint",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.viewport->setUpdatesEnabled(false);
    const auto request = fixture.request();
    const auto session = fixture.model->animationSessionRevision();
    CaptureAttempt capture(fixture, request);
    REQUIRE_FALSE(capture.id.isEmpty());
    REQUIRE(fixture.model->setAnimationLoop(true));
    REQUIRE(fixture.model->animationEvaluationId() == request.expectedEvaluationId);
    REQUIRE(fixture.model->animationSessionRevision() == session + 1);
    capture.requireError(Error::StaleEvaluation);
    REQUIRE_FALSE(fixture.window.observationService().isCapturing());
    REQUIRE_FALSE(fixture.window.observationService().cancelCapture(capture.id));
    fixture.viewport->setUpdatesEnabled(true);
}

TEST_CASE("Pending paused capture rejects seek loop preview and formal source changes immediately",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    fixture.viewport->setUpdatesEnabled(false);
    CaptureAttempt capture(fixture, fixture.request());
    REQUIRE_FALSE(capture.id.isEmpty());
    SECTION("Seek changes evaluation") {
        REQUIRE(fixture.model->setAnimationFrame(25));
        capture.requireError(Error::StaleEvaluation);
    }
    SECTION("Loop changes session only") {
        REQUIRE(fixture.model->setAnimationLoop(true));
        capture.requireError(Error::StaleEvaluation);
    }
    SECTION("Disable changes mode") {
        REQUIRE(fixture.model->setAnimationPreview(false));
        capture.requireError(Error::StaleEvaluation);
    }
    SECTION("Formal source edit changes document") {
        auto changed = fixture.model->scene()->animation();
        changed.tracks.at({fixture.cube, core::AnimationChannel::Position}).keys.back().value.x = 5;
        REQUIRE(fixture.model->replaceAnimation(changed));
        capture.requireError(Error::RevisionConflict);
    }
    REQUIRE_FALSE(fixture.window.observationService().isCapturing());
    fixture.viewport->setUpdatesEnabled(true);
}

TEST_CASE("Another Base document at the same revision actively invalidates capture",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.model->newScene();
    fixture.viewport->setUpdatesEnabled(false);
    const auto request = fixture.request();
    CaptureAttempt capture(fixture, request);
    REQUIRE_FALSE(capture.id.isEmpty());
    fixture.model->newScene();
    REQUIRE(fixture.model->apiDocumentState().documentRevision == request.expectedDocumentRevision);
    REQUIRE(fixture.model->apiDocumentState().document != request.document);
    capture.requireError(Error::StaleDocument);
    fixture.viewport->setUpdatesEnabled(true);
}

TEST_CASE("Playing Draft and physical Busy reject capture admission",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    SECTION("Playing") { REQUIRE(fixture.model->playAnimation()); }
    SECTION("Draft") {
        REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {true, false, false}));
    }
    auto request = fixture.request();
    SECTION("Physical Busy") {
        fixture.model->setExternalBusy(QStringLiteral("observation_test_interaction"), true);
    }
    CaptureAttempt capture(fixture, request);
    REQUIRE(capture.id.isEmpty());
    capture.requireError(Error::Busy);
    fixture.model->setExternalBusy(QStringLiteral("observation_test_interaction"), false);
}

TEST_CASE("A request replaced during the grab repaint cannot receive the previous result",
          "[animation-observation]") {
    AnimationObservationWindow fixture;
    fixture.preview();
    const auto request = fixture.request();
    auto& service = fixture.window.observationService();
    std::optional<api::ApiResult<observation::CaptureResult>> first, second;
    int firstCalls = 0, secondCalls = 0, paints = 0;
    QString firstId, secondId;
    std::uint64_t replacementAfterFrame = 0;
    firstId = service.capture(request, [&](auto result) {
        ++firstCalls;
        first = std::move(result);
        replacementAfterFrame = fixture.viewport->lastRenderedFrame()->frameId;
        secondId = service.capture(fixture.request(), [&](auto replacement) {
            ++secondCalls;
            second = std::move(replacement);
        });
    });
    const auto connection = QObject::connect(fixture.viewport, &Viewport::framePainted,
                                              fixture.viewport, [&] {
        if (++paints == 2)
            REQUIRE(service.cancelCapture(firstId));
    });
    REQUIRE(QTest::qWaitFor([&] { return second.has_value(); }, 6000));
    QObject::disconnect(connection);
    service.cancelCapture(firstId);
    service.cancelCapture(secondId);
    REQUIRE(first);
    REQUIRE(first->error);
    REQUIRE(first->error->code == Error::Cancelled);
    REQUIRE(firstCalls == 1);
    REQUIRE(secondCalls == 1);
    REQUIRE(second->hasValue());
    REQUIRE(second->value->captureId == secondId);
    REQUIRE(second->value->frameId > replacementAfterFrame);
    REQUIRE(second->value->view.viewport.animation->evaluationId == request.expectedEvaluationId);
    REQUIRE_FALSE(service.isCapturing());
}
