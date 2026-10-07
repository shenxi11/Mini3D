/*
 * 模块名: AnimationUiTests
 * 功能概述: 在真实Qt/GL窗口验证时间轴、显示身份、导航原子性与草稿GRS。
 * 对外接口: Catch2 [animation-ui]。
 * 依赖关系: MainWindow、SceneViewModel、ViewportWidget、Qt Test。
 * 输入输出: 隔离场景与真实输入到姿态/基础源/版本及PNG证据。
 * 异常与错误: 身份错误、源被预览污染或导航失败仍改变视图即失败。
 * 维护说明: 不开启通信、不修改用户模型或偏好；证据只写E盘验证目录。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/AnimationDraftGesture.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDockWidget>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <iostream>

using namespace mini3d;
namespace {
struct AnimationWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model;
    renderer_gl::ViewportWidget* viewport;
    core::EntityId cube;
    AnimationWindow() {
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        model = window.findChild<editor::SceneViewModel*>();
        viewport = window.findChild<renderer_gl::ViewportWidget*>();
        REQUIRE(model);
        REQUIRE(viewport);
        auto* timeline = window.findChild<QDockWidget*>(QStringLiteral("AnimationDock"));
        REQUIRE(timeline);
        timeline->show();
        timeline->raise();
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
        REQUIRE(model->setAnimationPreview(true));
        viewport->setFocus();
    }
    ~AnimationWindow() { window.hide(); }
};
void requestRealPaint(renderer_gl::ViewportWidget& viewport) {
    const auto before = viewport.lastRenderedFrame() ? viewport.lastRenderedFrame()->frameId : 0;
    viewport.update();
    for (int attempt = 0; attempt < 100; ++attempt) {
        QTest::qWait(10);
        if (viewport.lastRenderedFrame() && viewport.lastRenderedFrame()->frameId > before) {
            INFO(viewport.lastRenderedFrame()->resources.error.toStdString());
            REQUIRE(viewport.lastRenderedFrame()->resources.ready);
            return;
        }
    }
    FAIL("Animation viewport did not produce a new real paint");
}
} // namespace
TEST_CASE("Animation real paint freezes the same complete identity as timeline", "[animation-ui]") {
    AnimationWindow fixture;
    const auto base = fixture.model->scene()->find(fixture.cube)->transform;
    REQUIRE(fixture.model->setAnimationFrame(13));
    requestRealPaint(*fixture.viewport);
    const auto pose = fixture.model->installedAnimationPose();
    REQUIRE(pose);
    REQUIRE(pose->numerics->find(fixture.cube)->local.position.x == 1.0F);
    REQUIRE(pose->numerics->find(fixture.cube)->rotationEulerXYZDegrees->y == 180);
    REQUIRE(fixture.viewport->lastRenderedFrame()->resources.ready);
    REQUIRE(fixture.viewport->lastRenderedFrame()->state.animation == pose->identity);
    REQUIRE(fixture.model->scene()->find(fixture.cube)->transform.position == base.position);
    auto* keys = fixture.window.findChild<QTableWidget*>(QStringLiteral("AnimationKeyTable"));
    REQUIRE(keys);
    REQUIRE(keys->rowCount() == 4);
    const auto capture = fixture.viewport->grabStampedFramebuffer();
    REQUIRE(capture);
    REQUIRE_FALSE(capture->image.isNull());
    REQUIRE(capture->frame.state.animation == pose->identity);
    const QString output = qEnvironmentVariable("MINI3D_ANIMATION_UI_EVIDENCE",
        QStringLiteral("E:/Mini3D/out/validation/native-animation-20261007-66e24b/r3-ui-evidence"));
    REQUIRE(QDir().mkpath(output));
    REQUIRE(capture->image.save(output + QStringLiteral("/frame-13.png")));
    REQUIRE(fixture.window.grab().save(output + QStringLiteral("/timeline-frame-13.png")));
}
TEST_CASE("Animation viewport rejects another document at the same source revision", "[animation-ui]") {
    AnimationWindow fixture;
    auto wrong = std::make_shared<renderer_gl::InstalledPose>(*fixture.model->installedAnimationPose());
    wrong->identity.documentId = QStringLiteral("different-document");
    const auto document = fixture.model->apiDocumentState();
    const auto identity = fixture.model->installedAnimationPose()->identity;
    const auto sessionRevision = fixture.model->animationSessionRevision();
    fixture.viewport->setFrameDisplayStateProvider([wrong, document, identity, sessionRevision] {
        return renderer_gl::FrameDisplayState{
            {document.document.instanceId, document.document.documentId,
             document.documentRevision, document.historyRevision},
            identity, sessionRevision, wrong};
    });
    REQUIRE_FALSE(fixture.viewport->observationState());
    REQUIRE_FALSE(fixture.viewport->grabStampedFramebuffer());
}
TEST_CASE("Animation restored subtrees reuse actual viewport uploads across base and preview",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationPreview(false));
    REQUIRE(fixture.model->makeEditable(fixture.cube));
    REQUIRE(fixture.model->setAnimationPreview(true));
    requestRealPaint(*fixture.viewport);
    fixture.model->duplicateSelected();
    const auto copy = fixture.model->selection()->selectedEntity();
    REQUIRE(copy != fixture.cube);
    requestRealPaint(*fixture.viewport);
    auto verifyReuse = [&] {
        const auto* record = fixture.model->scene()->editableMesh(
            fixture.model->scene()->find(copy)->editableMesh);
        REQUIRE(record);
        REQUIRE(fixture.model->installedAnimationPose()->geometry->entries.at(copy)
                    .evaluationRevision == record->evaluationRevision);
        const auto uploads = fixture.viewport->editableMeshUploadCount();
        const auto context = fixture.viewport->contextGeneration();
        REQUIRE(uploads > 0);
        REQUIRE(fixture.model->setAnimationPreview(false));
        requestRealPaint(*fixture.viewport);
        REQUIRE(fixture.viewport->editableMeshUploadCount() == uploads);
        REQUIRE(fixture.viewport->contextGeneration() == context);
        REQUIRE(fixture.model->setAnimationPreview(true));
        requestRealPaint(*fixture.viewport);
        REQUIRE(fixture.viewport->editableMeshUploadCount() == uploads);
        REQUIRE(fixture.viewport->contextGeneration() == context);
    };
    verifyReuse();
    fixture.model->deleteSelected();
    REQUIRE_FALSE(fixture.model->scene()->find(copy));
    requestRealPaint(*fixture.viewport);
    fixture.model->undo();
    REQUIRE(fixture.model->scene()->find(copy));
    requestRealPaint(*fixture.viewport);
    verifyReuse();
}
TEST_CASE("Animation observation rejects a document change between frozen input and final check",
          "[animation-ui]") {
    AnimationWindow fixture;
    const auto document = fixture.model->apiDocumentState();
    const auto pose = fixture.model->installedAnimationPose();
    const auto sessionRevision = fixture.model->animationSessionRevision();
    const auto reads = std::make_shared<int>(0);
    fixture.viewport->setFrameDisplayStateProvider([document, pose, sessionRevision, reads] {
        renderer_gl::FrameDocumentStamp result{document.document.instanceId,
            document.document.documentId, document.documentRevision, document.historyRevision};
        if (++*reads > 1)
            result.documentId = QStringLiteral("changed-during-observation");
        return renderer_gl::FrameDisplayState{result, pose->identity, sessionRevision, pose};
    });
    REQUIRE_FALSE(fixture.viewport->observationState());
    REQUIRE(*reads == 2);
}
TEST_CASE("Animation observation rejects a seek triggered by synchronous viewport notification",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->renameEntity(fixture.cube, QStringLiteral("同步观察通知")));
    bool sought = false;
    const auto connection = QObject::connect(fixture.viewport,
        &renderer_gl::ViewportWidget::viewportChanged, fixture.viewport, [&] {
            if (!sought) {
                sought = true;
                REQUIRE(fixture.model->setAnimationFrame(25));
            }
        });
    REQUIRE_FALSE(fixture.viewport->observationState());
    REQUIRE(sought);
    QObject::disconnect(connection);
    const auto state = fixture.viewport->observationState();
    REQUIRE(state);
    REQUIRE(state->animation == fixture.model->installedAnimationPose()->identity);
    REQUIRE(state->animation->frame == 25);
}
TEST_CASE("Animation navigation final guard rejects before changing the real camera", "[animation-ui]") {
    AnimationWindow fixture;
    const auto before = fixture.viewport->editorCameraSnapshot();
    REQUIRE(before);
    const auto state = fixture.model->apiDocumentState();
    const auto pose = fixture.model->installedAnimationPose();
    const auto guard = fixture.model->exchangeBeforeCommitGuard([state] {
        return std::optional<editor::api::ApiError>{{editor::api::ErrorCode::Cancelled,
            QStringLiteral("isolated navigation cancellation"), {}, editor::api::Recovery::CorrectInput, state}};
    });
    fixture.viewport->zoomViewNavigation(1);
    fixture.model->exchangeBeforeCommitGuard(guard);
    REQUIRE(fixture.viewport->editorCameraSnapshot()->state() == before->state());
    REQUIRE(fixture.model->apiDocumentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.model->installedAnimationPose() == pose);
}
TEST_CASE("Animation draft numeric rotation preserves turns and escape separates gesture from draft", "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationFrame(1));
    const auto source = fixture.model->scene()->animation();
    REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {false, true, false}));
    auto* gesture = fixture.window.findChild<editor::AnimationDraftGesture*>();
    REQUIRE(gesture);
    REQUIRE(gesture->start(editor::TransformOperation::Rotate));
    QTest::keyClick(fixture.viewport, Qt::Key_Y);
    QTest::keyClicks(fixture.viewport, QStringLiteral("720"));
    REQUIRE(fixture.model->animationDraftValues()->at(1).y == 720);
    QTest::keyClick(fixture.viewport, Qt::Key_Escape);
    REQUIRE_FALSE(gesture->isActive());
    REQUIRE(fixture.model->animationMode() == renderer_gl::AnimationMode::PoseDraft);
    REQUIRE(fixture.model->animationDraftValues()->at(1).y == 0);
    REQUIRE(fixture.model->scene()->animation() == source);
    QTest::keyClick(fixture.viewport, Qt::Key_Escape);
    REQUIRE(fixture.model->animationMode() == renderer_gl::AnimationMode::PreviewPaused);
    REQUIRE(fixture.model->scene()->animation() == source);
}
TEST_CASE("Animation focus cannot publish bounds prepared before a guard seeks another frame",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationFrame(13));
    requestRealPaint(*fixture.viewport);
    const auto before = fixture.viewport->editorCameraSnapshot();
    REQUIRE(before);
    REQUIRE_FALSE(fixture.viewport->focusEntities({fixture.cube}, [&] {
        REQUIRE(fixture.model->setAnimationFrame(25));
        return true;
    }));
    REQUIRE(fixture.model->animationFrame() == 25);
    REQUIRE(fixture.viewport->editorCameraSnapshot()->state() == before->state());
}
TEST_CASE("Animation view update cannot publish a candidate prepared before guard reentry",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationFrame(13));
    requestRealPaint(*fixture.viewport);
    const auto before = fixture.viewport->editorCameraSnapshot();
    REQUIRE(before);
    renderer_gl::ViewUpdate update;
    update.orthographic = !before->isOrthographic();
    REQUIRE_FALSE(fixture.viewport->applyViewUpdate(update, [&] {
        REQUIRE(fixture.model->setAnimationFrame(25));
        return true;
    }));
    REQUIRE(fixture.model->animationFrame() == 25);
    REQUIRE(fixture.viewport->isOrthographic() == before->isOrthographic());
    REQUIRE(fixture.viewport->editorCameraSnapshot()->state() == before->state());
    std::optional<core::CameraState> reentrantCamera;
    REQUIRE_FALSE(fixture.viewport->applyViewUpdate(update, [&] {
        fixture.viewport->zoomViewNavigation(1);
        reentrantCamera = fixture.viewport->editorCameraSnapshot()->state();
        return true;
    }));
    REQUIRE(reentrantCamera);
    REQUIRE(*reentrantCamera != before->state());
    REQUIRE(fixture.viewport->editorCameraSnapshot()->state() == *reentrantCamera);
    REQUIRE(fixture.viewport->isOrthographic() == before->isOrthographic());
}
TEST_CASE("Animation axis projection preserves saved camera while actual navigation rebinds playing pose",
          "[animation-ui]") {
    AnimationWindow fixture;
    const auto saved = fixture.model->editorCamera();
    const auto document = fixture.model->apiDocumentState();
    const auto session = fixture.model->animationSessionRevision();
    QSignalSpy writeback(fixture.viewport, &renderer_gl::ViewportWidget::cameraChanged);
    fixture.viewport->setCameraView(renderer_gl::EditorView::Top);
    fixture.viewport->setOrthographic(true);
    REQUIRE(fixture.model->editorCamera() == saved);
    REQUIRE(fixture.model->apiDocumentState().documentRevision == document.documentRevision);
    REQUIRE(fixture.model->animationSessionRevision() == session);
    REQUIRE(writeback.count() == 0);
    REQUIRE(fixture.model->setAnimationLoop(true));
    REQUIRE(fixture.model->playAnimation());
    const auto playingSession = fixture.model->animationSessionRevision();
    const auto before = fixture.model->installedAnimationPose();
    const auto history = fixture.model->undoStack()->count();
    fixture.viewport->zoomViewNavigation(1);
    const auto after = fixture.model->installedAnimationPose();
    REQUIRE(after);
    REQUIRE(after != before);
    REQUIRE(after->geometry == before->geometry);
    REQUIRE(fixture.model->animationMode() == renderer_gl::AnimationMode::Playing);
    REQUIRE(fixture.model->animationSessionRevision() == playingSession);
    REQUIRE(fixture.model->apiDocumentState().documentRevision == document.documentRevision + 1);
    REQUIRE(after->identity.sourceRevision == document.documentRevision + 1);
    REQUIRE(writeback.count() == 1);
    REQUIRE(fixture.model->undoStack()->count() == history);
    REQUIRE(fixture.model->pauseAnimation());
}
TEST_CASE("Animation view guard preserves real pole navigation even when saved camera is unchanged",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationPreview(false));
    fixture.viewport->setCameraView(renderer_gl::EditorView::Top);
    fixture.viewport->orbitViewNavigation({0, 0});
    requestRealPaint(*fixture.viewport);
    const auto before = fixture.viewport->editorCameraSnapshot();
    const auto observation = fixture.viewport->observationState();
    REQUIRE(before);
    REQUIRE(observation);
    std::optional<renderer_gl::EditorCamera> navigated;
    renderer_gl::ViewUpdate update;
    update.orthographic = !before->isOrthographic();
    REQUIRE_FALSE(fixture.viewport->applyViewUpdate(update, [&] {
        fixture.viewport->orbitViewNavigation({0, 1});
        navigated = fixture.viewport->editorCameraSnapshot();
        return true;
    }));
    REQUIRE(navigated);
    REQUIRE(navigated->state() == before->state());
    REQUIRE(navigated->viewMatrix() != before->viewMatrix());
    REQUIRE(fixture.viewport->editorCameraSnapshot()->viewMatrix() == navigated->viewMatrix());
    REQUIRE(fixture.viewport->isOrthographic() == before->isOrthographic());
    REQUIRE(fixture.viewport->observationState()->viewportRevision > observation->viewportRevision);
}
TEST_CASE("Animation draft focus cancels the gesture but preserves the complete draft",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationFrame(1));
    REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {false, true, false}));
    auto* gesture = fixture.window.findChild<editor::AnimationDraftGesture*>();
    REQUIRE(gesture);
    REQUIRE(gesture->start(editor::TransformOperation::Rotate));
    QTest::keyClick(fixture.viewport, Qt::Key_Y);
    QTest::keyClicks(fixture.viewport, QStringLiteral("720"));
    REQUIRE(fixture.model->animationDraftValues()->at(1).y == 720);
    auto* field = fixture.window.findChild<QWidget*>(QStringLiteral("AnimationDraft10"));
    REQUIRE(field);
    field->setFocus();
    REQUIRE_FALSE(gesture->isActive());
    REQUIRE(fixture.model->animationMode() == renderer_gl::AnimationMode::PoseDraft);
    REQUIRE(fixture.model->animationDraftValues()->at(1).y == 0);
    fixture.model->cancelAnimationDraft();
    REQUIRE(fixture.model->animationMode() == renderer_gl::AnimationMode::PreviewPaused);
}
TEST_CASE("Animation draft panel stays inside the real window at the configured DPI",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationFrame(1));
    REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {true, true, true}));
    REQUIRE(fixture.model->setAnimationDraftChannel(
        core::AnimationChannel::RotationEulerXYZDegrees, {0, 720, 0}));
    requestRealPaint(*fixture.viewport);
    auto* panel = fixture.window.findChild<QWidget*>(QStringLiteral("AnimationDraftPanel"));
    REQUIRE(panel);
    REQUIRE(panel->isVisible());
    const QRect windowRect(QPoint(0, 0), fixture.window.size());
    const QRect panelRect(panel->mapTo(&fixture.window, QPoint(0, 0)), panel->size());
    REQUIRE(windowRect.contains(panelRect));
    for (int group = 0; group < 3; ++group)
        for (int axis = 0; axis < 3; ++axis) {
            auto* field = fixture.window.findChild<QWidget*>(
                QStringLiteral("AnimationDraft%1%2").arg(group).arg(axis));
            REQUIRE(field);
            REQUIRE(field->isVisible());
            REQUIRE(field->isEnabled());
        }
    const QString output = qEnvironmentVariable("MINI3D_ANIMATION_UI_EVIDENCE",
        QStringLiteral("E:/Mini3D/out/validation/native-animation-20261007-66e24b/r3-ui-evidence"));
    REQUIRE(QDir().mkpath(output));
    const auto screenshot = fixture.window.grab();
    const auto ratio = fixture.window.devicePixelRatioF();
    REQUIRE(screenshot.width() == qRound(fixture.window.width() * ratio));
    REQUIRE(screenshot.height() == qRound(fixture.window.height() * ratio));
    std::cout << "Animation UI scale=" << qEnvironmentVariable("QT_SCALE_FACTOR").toStdString()
              << " actualDPR=" << ratio << " logical=" << fixture.window.width() << 'x'
              << fixture.window.height() << " pixels=" << screenshot.width() << 'x'
              << screenshot.height() << '\n';
    REQUIRE(screenshot.save(output + QStringLiteral("/draft-rotation-720.png")));
    fixture.model->cancelAnimationDraft();
}
TEST_CASE("Animation repeated Escape cannot cancel the complete draft after cancelling its gesture",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationFrame(1));
    REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {true, true, false}));
    REQUIRE(fixture.model->setAnimationDraftChannel(core::AnimationChannel::Position, {4, 0, 0}));
    auto* gesture = fixture.window.findChild<editor::AnimationDraftGesture*>();
    REQUIRE(gesture);
    REQUIRE(gesture->start(editor::TransformOperation::Rotate));
    QTest::keyClick(fixture.viewport, Qt::Key_Y);
    QTest::keyClicks(fixture.viewport, QStringLiteral("45"));
    QTest::keyClick(fixture.viewport, Qt::Key_Escape);
    QKeyEvent repeated(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, {}, true);
    QApplication::sendEvent(fixture.viewport, &repeated);
    REQUIRE_FALSE(gesture->isActive());
    REQUIRE(fixture.model->animationMode() == renderer_gl::AnimationMode::PoseDraft);
    REQUIRE(fixture.model->animationDraftValues()->at(0).x == 4);
    REQUIRE(fixture.model->animationDraftValues()->at(1).y == 0);
}
TEST_CASE("Animation gesture discards its state when a view provider cancels the draft",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationFrame(1));
    const auto source = fixture.model->scene()->animation();
    REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {true, false, false}));
    auto* gesture = fixture.window.findChild<editor::AnimationDraftGesture*>();
    REQUIRE(gesture);
    REQUIRE(gesture->start(editor::TransformOperation::Move));
    QTest::keyClick(fixture.viewport, Qt::Key_X);
    const auto camera = fixture.viewport->editorCameraSnapshot();
    REQUIRE(camera);
    bool cancelled = false;
    fixture.model->setAnimationViewProvider([&] {
        if (!cancelled) {
            cancelled = true;
            fixture.model->cancelAnimationDraft();
        }
        return *camera;
    });
    QTest::keyClicks(fixture.viewport, QStringLiteral("1"));
    REQUIRE(cancelled);
    REQUIRE_FALSE(gesture->isActive());
    REQUIRE(fixture.model->animationMode() == renderer_gl::AnimationMode::PreviewPaused);
    REQUIRE(fixture.model->scene()->animation() == source);
}
TEST_CASE("Animation rejected gesture rollback stays active until Escape can restore its start",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationFrame(1));
    REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {true, false, false}));
    REQUIRE(fixture.model->setAnimationDraftChannel(core::AnimationChannel::Position, {4, 0, 0}));
    auto* gesture = fixture.window.findChild<editor::AnimationDraftGesture*>();
    REQUIRE(gesture);
    REQUIRE(gesture->start(editor::TransformOperation::Move));
    QTest::keyClick(fixture.viewport, Qt::Key_X);
    QTest::keyClicks(fixture.viewport, QStringLiteral("1"));
    REQUIRE(fixture.model->animationDraftValues()->at(0).x == 5);
    const auto source = fixture.model->apiDocumentState();
    const auto guard = fixture.model->exchangeBeforeCommitGuard([source] {
        return std::optional<editor::api::ApiError>{{editor::api::ErrorCode::Cancelled,
            QStringLiteral("isolated rollback rejection"), {},
            editor::api::Recovery::CorrectInput, source}};
    });
    QTest::keyClick(fixture.viewport, Qt::Key_Escape);
    REQUIRE(gesture->isActive());
    REQUIRE(fixture.model->animationDraftValues()->at(0).x == 5);
    REQUIRE_FALSE(fixture.model->commitAnimationDraft());
    fixture.model->exchangeBeforeCommitGuard(guard);
    REQUIRE_FALSE(fixture.model->commitAnimationDraft());
    QTest::keyClick(fixture.viewport, Qt::Key_Escape);
    REQUIRE_FALSE(gesture->isActive());
    REQUIRE(fixture.model->animationMode() == renderer_gl::AnimationMode::PoseDraft);
    REQUIRE(fixture.model->animationDraftValues()->at(0).x == 4);
}
TEST_CASE("Animation gesture starts at the actual pointer entry instead of a previous position",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->setAnimationFrame(1));
    REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {true, false, false}));
    auto* gesture = fixture.window.findChild<editor::AnimationDraftGesture*>();
    REQUIRE(gesture);
    const QPointF oldPosition(10, 10), entered(300, 100);
    QMouseEvent previous(QEvent::MouseMove, oldPosition, oldPosition, Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.viewport, &previous);
    QEnterEvent enter(entered, entered, fixture.viewport->mapToGlobal(entered.toPoint()));
    QApplication::sendEvent(fixture.viewport, &enter);
    const auto camera = fixture.viewport->editorCameraSnapshot();
    REQUIRE(camera);
    const auto basis = glm::dmat3(glm::inverse(camera->viewMatrix()));
    const auto expected = static_cast<double>(camera->worldUnitsPerPixel({0, 0, 0})) * basis[0];
    REQUIRE(gesture->start(editor::TransformOperation::Move));
    QMouseEvent next(QEvent::MouseMove, entered + QPointF(1, 0), entered + QPointF(1, 0),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(fixture.viewport, &next);
    REQUIRE(glm::length(fixture.model->animationDraftValues()->at(0) - expected) < 1.0e-6);
    gesture->cancel();
}
TEST_CASE("Animation rejected orthographic action reflects the actual unchanged projection",
          "[animation-ui]") {
    AnimationWindow fixture;
    REQUIRE(fixture.model->beginAnimationDraft(fixture.cube, {true, false, false}));
    auto* action = fixture.window.findChild<QAction*>(QStringLiteral("OrthographicView"));
    REQUIRE(action);
    const auto before = fixture.viewport->isOrthographic();
    action->trigger();
    REQUIRE(fixture.viewport->isOrthographic() == before);
    REQUIRE(action->isChecked() == before);
    fixture.model->cancelAnimationDraft();
    const auto source = fixture.model->apiDocumentState();
    const auto pose = fixture.model->installedAnimationPose();
    fixture.model->exchangeBeforeCommitGuard([source] {
        return std::optional<editor::api::ApiError>{{editor::api::ErrorCode::Cancelled,
            QStringLiteral("isolated projection cancellation"), {},
            editor::api::Recovery::CorrectInput, source}};
    });
    action->trigger();
    REQUIRE(fixture.viewport->isOrthographic() == before);
    REQUIRE(action->isChecked() == before);
    REQUIRE(fixture.model->apiDocumentState().documentRevision == source.documentRevision);
    REQUIRE(fixture.model->installedAnimationPose() == pose);
}
