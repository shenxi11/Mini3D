/*
 * 模块名: ObjectTransformSessionTests
 * 功能概述: 验证对象模态变换的真实输入、唯一历史、连续精细和取消恢复。
 * 对外接口: Catch2 [object-transform-ui] 用例
 * 依赖关系: Qt Test、MainWindow、ObjectTransformSession、真实 OpenGL 视口
 * 输入输出: Qt 布局坐标与按键到对象、HUD 和撤销历史断言。
 * 异常与错误: 误提交、预览累积、失焦残留或 F3/Q 生命周期失效即失败。
 * 维护说明: 不读取 OS 光标，不写用户设置；可用 200% DPI 和可选截图复核。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
// 记录导致安全取消的窗口事件，便于全量 UI 回归定位异步焦点/布局干扰。
class ModalLifecycleTrace final : public QObject {
  public:
    editor::ObjectTransformSession* modal = nullptr;
    QStringList events;
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (modal && modal->isActive() &&
            (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate ||
             event->type() == QEvent::UngrabMouse || event->type() == QEvent::Resize ||
             event->type() == QEvent::Hide || event->type() == QEvent::MouseMove)) {
            events.append(
                QStringLiteral("%1:%2").arg(watched->objectName()).arg(int(event->type())));
            if (event->type() == QEvent::MouseMove) {
                const auto* mouse = static_cast<QMouseEvent*>(event);
                events.append(QStringLiteral("mouse %1,%2 spontaneous=%3")
                                  .arg(mouse->position().x())
                                  .arg(mouse->position().y())
                                  .arg(event->spontaneous()));
            }
        }
        return false;
    }
};
struct TransformFixture {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::ObjectTransformSession* modal = window.findChild<editor::ObjectTransformSession*>();
    core::EntityId id = 0;
    QPoint start;
    ModalLifecycleTrace lifecycle;
    TransformFixture() {
        lifecycle.modal = modal;
        qApp->installEventFilter(&lifecycle);
        QObject::connect(viewport, &renderer_gl::ViewportWidget::viewModeChanged, &lifecycle,
                         [this] {
                             if (modal->isActive())
                                 lifecycle.events.append(QStringLiteral("viewModeChanged"));
                         });
        QObject::connect(viewport, &renderer_gl::ViewportWidget::cameraChanged, &lifecycle, [this] {
            if (modal->isActive())
                lifecycle.events.append(QStringLiteral("cameraChanged"));
        });
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        model->newScene();
        id = model->createEntity(core::PrimitiveKind::Cube);
        viewport->setEditorCamera({{0, 0, 5}, {0, 0, 0}, 0, 50});
        REQUIRE(viewport->editorCameraSnapshot().has_value());
        window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Blender);
        start = QPoint(viewport->width() * 3 / 4, viewport->height() / 2);
        viewport->setFocus();
        if (qEnvironmentVariable("QT_SCALE_FACTOR") == QStringLiteral("2")) {
            REQUIRE(viewport->devicePixelRatioF() >= 2);
        }
    }
    ~TransformFixture() {
        window.hide();
    }
    void move(QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const auto global = viewport->mapToGlobal(point);
        REQUIRE(window.childAt(window.mapFromGlobal(global)) == viewport);
        QMouseEvent event(QEvent::MouseMove, point, global, Qt::NoButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(viewport, &event);
    }
    void begin(Qt::Key key) {
        lifecycle.events.clear();
        viewport->setFocus();
        move(start);
        QTest::keyClick(viewport, key);
        REQUIRE(modal->isActive());
        QTest::qWait(10);
        INFO(lifecycle.events.join(QStringLiteral(", ")).toStdString());
        REQUIRE(modal->isActive());
    }
    const core::Transform& transform() const {
        return model->scene()->find(id)->transform;
    }
    void numericMove() {
        begin(Qt::Key_G);
        QTest::keyClicks(viewport, "x2");
        REQUIRE(transform().position == glm::vec3(2, 0, 0));
    }
};
} // namespace

TEST_CASE("Rotated numeric scaling preserves mesh rotation history and saved transforms",
          "[object-transform-ui][rotated-scale-ui]") {
    TransformFixture f;
    REQUIRE(f.model->makeEditable(f.id));
    REQUIRE(f.model->setTransformComponent(f.id, 1, 0, 25));
    REQUIRE(f.model->setTransformComponent(f.id, 1, 1, 40));
    REQUIRE(f.model->setTransformComponent(f.id, 1, 2, 15));
    const auto before = f.transform();
    const auto meshId = f.model->scene()->find(f.id)->editableMesh;
    const auto mesh = *f.model->scene()->editableMesh(meshId);
    const auto history = f.model->undoStack()->index();
    QSignalSpy rejected(f.modal, &editor::ObjectTransformSession::operationRejected);
    for (const auto* input : {"x2", "y2", "z2", "x-2"}) {
        INFO("numeric scale=" << input);
        f.begin(Qt::Key_S);
        QTest::keyClicks(f.viewport, input);
        REQUIRE(f.transform().scale != before.scale);
        REQUIRE(f.transform().rotation == before.rotation);
        REQUIRE(f.transform().position == before.position);
        REQUIRE(f.model->undoStack()->index() == history);
        const auto preview = f.transform();
        QTest::keyClick(f.viewport, Qt::Key_Return);
        REQUIRE_FALSE(f.modal->isActive());
        REQUIRE(f.model->undoStack()->count() == history + 1);
        REQUIRE(f.model->undoStack()->index() == history + 1);
        f.model->undo();
        REQUIRE(f.transform().localMatrix() == before.localMatrix());
        f.model->redo();
        REQUIRE(f.transform().localMatrix() == preview.localMatrix());
        f.model->undo();
        f.begin(Qt::Key_S);
        QTest::keyClicks(f.viewport, input);
        QTest::keyClick(f.viewport, Qt::Key_Escape);
        REQUIRE(f.transform().localMatrix() == before.localMatrix());
        REQUIRE(f.model->undoStack()->canRedo());
        REQUIRE(f.model->undoStack()->index() == history);
        REQUIRE(f.model->scene()->editableMesh(meshId)->content == mesh.content);
        REQUIRE(f.model->scene()->editableMesh(meshId)->geometryRevision == mesh.geometryRevision);
    }
    f.begin(Qt::Key_S);
    QTest::keyClicks(f.viewport, "xx2");
    REQUIRE(f.transform().scale == glm::vec3(2, 1, 1));
    REQUIRE(f.transform().rotation == before.rotation);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    f.begin(Qt::Key_S);
    QTest::keyClicks(f.viewport, "y2");
    QTest::keyClick(f.viewport, Qt::Key_Return);
    const auto saved = f.transform();
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("旋转缩放.m3dscene"));
    REQUIRE(f.model->saveScene(path));
    REQUIRE(f.model->openScene(path));
    REQUIRE(f.transform().position == saved.position);
    REQUIRE(glm::distance(f.transform().scale, saved.scale) < 1.0e-6F);
    REQUIRE(std::abs(glm::dot(f.transform().rotation, saved.rotation)) > 0.999999F);
    REQUIRE(f.model->scene()->find(f.id)->editableMesh == meshId);
    REQUIRE(f.model->scene()->editableMesh(meshId)->content->source == mesh.content->source);
    REQUIRE(rejected.isEmpty());
}

TEST_CASE("Modal G X 2 R Y 45 and S exclude Z commit one reversible edit",
          "[object-transform-ui]") {
    TransformFixture f;
    const auto history = f.model->undoStack()->count();
    f.window.findChild<QAction*>(QStringLiteral("MoveTool"))->trigger();
    f.numericMove();
    REQUIRE(f.model->undoStack()->count() == history);
    auto* hud = f.window.findChild<QLabel*>(QStringLiteral("ObjectTransformHud"));
    REQUIRE(hud->isVisible());
    REQUIRE(hud->text().contains(QStringLiteral("全局")));
    REQUIRE(hud->testAttribute(Qt::WA_TransparentForMouseEvents));
    const auto capture = qEnvironmentVariable("MINI3D_TEST_MODAL_CAPTURE");
    if (!capture.isEmpty()) {
        QTest::qWait(50);
        REQUIRE(f.window.grab().save(capture));
    }
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE_FALSE(hud->isVisible());
    REQUIRE(f.model->undoStack()->count() == history + 1);
    REQUIRE(f.viewport->transformTool() == renderer_gl::GizmoTool::Move);
    REQUIRE(f.model->selection()->selectedEntity() == f.id);
    f.model->undo();
    REQUIRE(f.transform().position == glm::vec3(0));
    f.model->redo();
    REQUIRE(f.transform().position == glm::vec3(2, 0, 0));
    f.begin(Qt::Key_R);
    QTest::keyClicks(f.viewport, "y45");
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.model->undoStack()->count() == history + 2);
    REQUIRE(glm::distance(f.transform().rotation * glm::vec3(1, 0, 0),
                          glm::vec3(std::sqrt(0.5F), 0, -std::sqrt(0.5F))) < 1.0e-5F);
    f.model->undo();
    REQUIRE(f.transform().rotation == glm::quat(1, 0, 0, 0));
    f.begin(Qt::Key_S);
    QTest::keyClick(f.viewport, Qt::Key_Z, Qt::ShiftModifier);
    QTest::keyClicks(f.viewport, ".5");
    REQUIRE(f.transform().scale == glm::vec3(0.5F, 0.5F, 1));
    QTest::mouseClick(f.viewport, Qt::LeftButton, Qt::NoModifier, f.start);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->count() == history + 2);
    f.model->undo();
    REQUIRE(f.transform().scale == glm::vec3(1));
    f.model->redo();
    REQUIRE(f.transform().scale == glm::vec3(0.5F, 0.5F, 1));
}

TEST_CASE("Modal cancellation interruptions restore before without consuming redo",
          "[object-transform-ui]") {
    TransformFixture f;
    REQUIRE(f.model->setTransformComponent(f.id, 0, 1, 1));
    f.model->undo();
    const auto history = f.model->undoStack()->count();
    const auto index = f.model->undoStack()->index();
    const auto check = [&] {
        REQUIRE_FALSE(f.modal->isActive());
        REQUIRE(f.transform().position == glm::vec3(0));
        REQUIRE(f.model->undoStack()->count() == history);
        REQUIRE(f.model->undoStack()->index() == index);
        REQUIRE(f.model->undoStack()->canRedo());
        REQUIRE(QWidget::mouseGrabber() != f.viewport);
    };
    f.numericMove();
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    check();
    f.numericMove();
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::NoModifier, f.start);
    check();
    for (auto type : {QEvent::WindowDeactivate, QEvent::UngrabMouse}) {
        f.numericMove();
        QEvent event(type);
        QApplication::sendEvent(f.viewport, &event);
        check();
    }
    f.numericMove();
    QFocusEvent focus(QEvent::FocusOut);
    QApplication::sendEvent(f.viewport, &focus);
    check();
    f.numericMove();
    QResizeEvent resize(f.viewport->size(), f.viewport->size());
    QApplication::sendEvent(f.viewport, &resize);
    check();
    f.numericMove();
    f.window.findChild<QLineEdit*>(QStringLiteral("EntityName"))->setFocus();
    check();
    f.numericMove();
    QInputMethodEvent preedit(QStringLiteral("zhong"), {});
    QApplication::sendEvent(f.viewport, &preedit);
    check();
    QInputMethodEvent clear;
    QApplication::sendEvent(f.viewport, &clear);
    f.numericMove();
    f.window.hide();
    check();
}

TEST_CASE("Modal rejects invalid numeric values and repairs them without partial history",
          "[object-transform-ui]") {
    TransformFixture f;
    const auto history = f.model->undoStack()->count();
    f.begin(Qt::Key_S);
    QTest::keyClicks(f.viewport, "0");
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.modal->isActive());
    REQUIRE(f.transform().scale == glm::vec3(1));
    REQUIRE(f.modal->statusText().contains(QStringLiteral("缩放过小")));
    QTest::keyClick(f.viewport, Qt::Key_Backspace);
    QTest::keyClicks(f.viewport, "2/3");
    REQUIRE(f.transform().scale == glm::vec3(2));
    REQUIRE(f.modal->statusText().contains(QStringLiteral("2/3")));
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->count() == history);
    QTest::keyClick(f.viewport, Qt::Key_Backspace);
    QKeyEvent repeatBackspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier, {}, true);
    QApplication::sendEvent(f.viewport, &repeatBackspace);
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.transform().scale == glm::vec3(2));
    REQUIRE(f.model->undoStack()->count() == history + 1);
    for (auto key : {Qt::Key_G, Qt::Key_R, Qt::Key_S}) {
        f.begin(key);
        QTest::keyClick(f.viewport, Qt::Key_Return);
        INFO("no-op key=" << int(key)
                          << "; events=" << f.lifecycle.events.join(", ").toStdString());
        INFO("position=" << f.transform().position.x << "," << f.transform().position.y << ","
                         << f.transform().position.z);
        INFO("scale=" << f.transform().scale.x << "," << f.transform().scale.y << ","
                      << f.transform().scale.z);
        INFO("history=" << f.model->undoStack()->undoText().toStdString());
        REQUIRE(f.model->undoStack()->count() == history + 1);
    }
}

TEST_CASE("Modal mouse offsets use fixed before and Shift precision is continuous",
          "[object-transform-ui]") {
    TransformFixture f;
    f.begin(Qt::Key_G);
    QTest::keyClick(f.viewport, Qt::Key_X);
    const auto camera = *f.viewport->editorCameraSnapshot();
    const auto view = camera.viewMatrix();
    const float units = camera.worldUnitsPerPixel({0, 0, 0});
    f.move(f.start + QPoint(40, 0));
    REQUIRE(std::abs(f.transform().position.x - 40 * units) < 1.0e-5F);
    f.move(f.start + QPoint(40, 0));
    REQUIRE(std::abs(f.transform().position.x - 40 * units) < 1.0e-5F);
    QTest::keyPress(f.viewport, Qt::Key_Shift);
    REQUIRE(std::abs(f.transform().position.x - 40 * units) < 1.0e-5F);
    f.move(f.start + QPoint(60, 0), Qt::ShiftModifier);
    REQUIRE(std::abs(f.transform().position.x - 42 * units) < 1.0e-5F);
    QTest::keyRelease(f.viewport, Qt::Key_Shift);
    REQUIRE(std::abs(f.transform().position.x - 42 * units) < 1.0e-5F);
    f.move(f.start + QPoint(70, 0));
    REQUIRE(std::abs(f.transform().position.x - 52 * units) < 1.0e-5F);
    QWheelEvent wheel(f.start, f.viewport->mapToGlobal(f.start), {}, {0, 120}, Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(f.viewport, &wheel);
    REQUIRE(f.viewport->editorCameraSnapshot()->viewMatrix() == view);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(f.transform().position == glm::vec3(0));
}

TEST_CASE("Ctrl reverses snap preference and explicit numbers remain exact",
          "[object-transform-ui]") {
    TransformFixture f;
    auto* snap = f.window.findChild<QAction*>(QStringLiteral("SnapTransform"));
    const float units = f.viewport->editorCameraSnapshot()->worldUnitsPerPixel({0, 0, 0});
    for (bool preference : {false, true}) {
        snap->setChecked(preference);
        f.begin(Qt::Key_G);
        QTest::keyClick(f.viewport, Qt::Key_X);
        const int pixels = qRound(0.37F / units);
        const float raw = pixels * units;
        f.move(f.start + QPoint(pixels, 0));
        REQUIRE(std::abs(f.transform().position.x - (preference ? 0.5F : raw)) < 1.0e-5F);
        QTest::keyPress(f.viewport, Qt::Key_Control);
        REQUIRE(std::abs(f.transform().position.x - (preference ? raw : 0.5F)) < 1.0e-5F);
        QTest::keyRelease(f.viewport, Qt::Key_Control);
        QTest::keyClicks(f.viewport, ".37");
        REQUIRE(std::abs(f.transform().position.x - 0.37F) < 1.0e-5F);
        QTest::keyClick(f.viewport, Qt::Key_Escape);
        REQUIRE(f.transform().position == glm::vec3(0));
        REQUIRE(snap->isChecked() == preference);
    }
}

TEST_CASE("Repeated axis switches direction space and model mutations cancel safely",
          "[object-transform-ui]") {
    TransformFixture f;
    REQUIRE(f.model->setTransformComponent(f.id, 1, 2, 90));
    const auto before = f.transform();
    f.numericMove();
    QTest::keyClick(f.viewport, Qt::Key_X);
    REQUIRE(f.modal->statusText().contains(QStringLiteral("局部")));
    REQUIRE(glm::distance(f.transform().position, glm::vec3(0, 2, 0)) < 1.0e-5F);
    QTest::keyClick(f.viewport, Qt::Key_X);
    REQUIRE(f.transform().position == glm::vec3(2, 0, 0));
    f.model->selection()->setSelectedEntity(0);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.transform().position == before.position);
    f.model->selection()->setSelectedEntity(f.id);
    f.numericMove();
    f.model->deleteSelected();
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->scene()->find(f.id) == nullptr);
    f.model->undo();
    REQUIRE(f.transform().position == before.position);
    REQUIRE(f.transform().rotation == before.rotation);
    f.model->selection()->setSelectedEntity(f.id);
    f.numericMove();
    f.model->newScene();
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->count() == 0);
    REQUIRE(f.model->scene()->roots().empty());
}

TEST_CASE("F3 and Q close before starting modal and preserve one operation history",
          "[object-transform-ui]") {
    TransformFixture f;
    auto* popup = f.window.findChild<editor::OperatorSearchPopup*>();
    auto* query = f.window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    REQUIRE(f.window.findChild<editor::QuickFavorites*>()->addOperator(
        QStringLiteral("object.translate")));
    const auto history = f.model->undoStack()->count();
    for (auto key : {Qt::Key_F3, Qt::Key_Q}) {
        f.move(f.start);
        QTest::keyClick(f.viewport, key);
        REQUIRE(popup->isVisible());
        query->setText(QStringLiteral("Translate Object"));
        QTest::keyClick(query, Qt::Key_Return);
        REQUIRE_FALSE(popup->isVisible());
        REQUIRE(f.modal->isActive());
        REQUIRE(QApplication::focusWidget() == f.viewport);
        QTest::qWait(30);
        REQUIRE(f.modal->isActive());
        QTest::keyClicks(f.viewport, "x2");
        REQUIRE(f.transform().position == glm::vec3(2, 0, 0));
        QTest::keyClick(f.viewport, Qt::Key_Escape);
        REQUIRE(f.transform().position == glm::vec3(0));
        REQUIRE(f.model->undoStack()->count() == history);
    }
}

TEST_CASE("Starting modal discards an unfinished handle preview before taking its snapshot",
          "[object-transform-ui][modal-start]") {
    TransformFixture f;
    const auto history = f.model->undoStack()->count();
    f.model->beginTransformEdit(f.id);
    auto preview = f.transform();
    preview.position.x = 3;
    f.model->previewTransform(preview);
    REQUIRE(f.transform().position.x == 3);
    f.begin(Qt::Key_G);
    INFO("start events=" << f.lifecycle.events.join(", ").toStdString());
    INFO("position=" << f.transform().position.x << "," << f.transform().position.y << ","
                     << f.transform().position.z);
    INFO("start=" << f.start.x() << "," << f.start.y());
    REQUIRE(f.transform().position == glm::vec3(0));
    QTest::keyClicks(f.viewport, "x2");
    REQUIRE(f.transform().position == glm::vec3(2, 0, 0));
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(f.transform().position == glm::vec3(0));
    REQUIRE(f.model->undoStack()->count() == history);
}

TEST_CASE("Modal mouse plane rotation and scale retain independent exact snapshots",
          "[object-transform-ui]") {
    TransformFixture f;
    const float units = f.viewport->editorCameraSnapshot()->worldUnitsPerPixel({0, 0, 0});
    f.begin(Qt::Key_G);
    QTest::keyClick(f.viewport, Qt::Key_Z, Qt::ShiftModifier);
    f.move(f.start + QPoint(20, -30));
    REQUIRE(glm::distance(f.transform().position, glm::vec3(20 * units, 30 * units, 0)) < 1.0e-4F);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    f.start = QPoint(f.viewport->width() / 2 + 100, f.viewport->height() / 2);
    f.begin(Qt::Key_R);
    QTest::keyClick(f.viewport, Qt::Key_Z);
    f.move(f.start + QPoint(-100, -100));
    REQUIRE(glm::distance(f.transform().rotation * glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)) <
            0.011F);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    f.begin(Qt::Key_S);
    f.move(f.start + QPoint(50, 0));
    REQUIRE(std::abs(f.transform().scale.x - 1.5F) < 0.003F);
    const auto preview = f.transform().scale;
    f.move(f.start + QPoint(50, 0));
    REQUIRE(f.transform().scale == preview);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(f.transform().scale == glm::vec3(1));
    REQUIRE(f.model->undoStack()->count() == 1);
}

TEST_CASE("Modal protects hidden targets and cameras but allows rotated world and local scaling",
          "[object-transform-ui]") {
    TransformFixture f;
    QSignalSpy rejected(f.modal, &editor::ObjectTransformSession::operationRejected);
    REQUIRE(f.model->setVisible(f.id, false));
    REQUIRE_FALSE(f.modal->start(editor::TransformOperation::Move));
    REQUIRE(rejected.count() == 1);
    REQUIRE(f.model->setVisible(f.id, true));
    const auto cameraId = f.model->createCamera();
    f.model->selection()->setSelectedEntity(f.id);
    REQUIRE(f.model->setPreviewCamera(cameraId));
    REQUIRE_FALSE(f.modal->start(editor::TransformOperation::Move));
    REQUIRE(f.model->setPreviewCamera(0));
    REQUIRE(f.model->setTransformComponent(f.id, 1, 2, 35));
    const auto history = f.model->undoStack()->count();
    f.begin(Qt::Key_S);
    QTest::keyClicks(f.viewport, "x2");
    REQUIRE_FALSE(f.modal->statusText().contains(QStringLiteral("剪切")));
    REQUIRE(f.modal->isActive());
    const float c = std::cos(glm::radians(35.0F)), s = std::sin(glm::radians(35.0F));
    REQUIRE(glm::distance(f.transform().scale, glm::vec3(std::sqrt(4 * c * c + s * s),
                                                         std::sqrt(4 * s * s + c * c), 1)) <
            1.0e-5F);
    QTest::keyClick(f.viewport, Qt::Key_X);
    REQUIRE(f.transform().scale == glm::vec3(2, 1, 1));
    QKeyEvent heldAxis(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"), true);
    QApplication::sendEvent(f.viewport, &heldAxis);
    REQUIRE(f.modal->statusText().contains(QStringLiteral("局部")));
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->count() == history + 1);
}

TEST_CASE("Ending modal cancels old camera drag and outside clicks never commit",
          "[object-transform-ui]") {
    TransformFixture f;
    QTest::mousePress(f.viewport, Qt::MiddleButton, Qt::NoModifier, f.start);
    f.begin(Qt::Key_G);
    const auto camera = f.viewport->editorCameraSnapshot()->viewMatrix();
    QTest::mouseRelease(f.viewport, Qt::MiddleButton, Qt::NoModifier, f.start);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    // 若旧中键状态残留，这次单击的 release 会被 cameraDragActive_ 错误阻止拾取。
    f.model->selection()->setSelectedEntity(0);
    QTest::mouseClick(f.viewport, Qt::LeftButton, Qt::NoModifier, f.viewport->rect().center());
    REQUIRE(f.model->selection()->selectedEntity() == f.id);
    REQUIRE(f.viewport->editorCameraSnapshot()->viewMatrix() == camera);
    f.numericMove();
    QMouseEvent outside(QEvent::MouseButtonPress, QPointF(-5, 20),
                        f.viewport->mapToGlobal(QPoint(-5, 20)), Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier);
    QApplication::sendEvent(f.viewport, &outside);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.transform().position == glm::vec3(0));
    REQUIRE(f.model->undoStack()->count() == 1);
}
