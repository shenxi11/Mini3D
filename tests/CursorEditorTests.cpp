/*
 * 模块名: CursorEditorTests
 * 功能概述: 验证游标辅助状态、持久化、创建位置和真实窗口交互。
 * 对外接口: Catch2 [cursor-editor]/[cursor-ui]；依赖关系: Qt Test、ViewModel、真实 GL。
 * 输入输出: 临时工程与输入事件到状态、历史及可选截图。
 * 异常与错误: 非法定位不改变游标；维护说明: 不写用户工程或偏好，截图来自真实窗口。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/ComponentInteraction.h"
#include "editor/operations/ComponentPicker.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/LoopCutSession.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/workbench/CommitSpinBox.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "editor/workbench/WorkbenchShell.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace mini3d;
namespace {
struct CursorWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::KeymapRouter* router = window.findChild<editor::KeymapRouter*>();
    CursorWindow() {
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        model->newScene();
        router->setKeymap(editor::EditorKeymap::Blender);
        viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
        QTest::qWait(30);
        pointAt();
    }
    ~CursorWindow() {
        window.hide();
    }
    void pointAt() {
        viewport->setFocus();
        const auto point = viewport->rect().center();
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
    }
    QAction* action(const char* name) {
        auto* result = window.findChild<QAction*>(QString::fromLatin1(name));
        REQUIRE(result);
        return result;
    }
    QPoint project(glm::vec3 point) {
        const auto camera = viewport->editorCameraSnapshot();
        REQUIRE(camera);
        const auto clip = camera->viewProjectionMatrix() * glm::vec4(point, 1);
        const auto ndc = glm::vec3(clip) / clip.w;
        return {qRound((ndc.x + 1) * viewport->width() * .5),
                qRound((1 - ndc.y) * viewport->height() * .5)};
    }
    void capture(const QString& suffix) {
        const auto prefix = qEnvironmentVariable("MINI3D_TEST_CURSOR_CAPTURE");
        if (!prefix.isEmpty()) {
            QTest::qWait(60);
            REQUIRE(window.grab().save(prefix + suffix + QStringLiteral(".png")));
        }
    }
};
} // namespace

TEST_CASE("Cursor is auxiliary state while creation has exactly one undoable world placement",
          "[cursor-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("游标工程.m3dscene"));
    REQUIRE(model.saveScene(path));
    QSignalSpy dirty(&model, &editor::SceneViewModel::documentChanged);
    REQUIRE(model.cursor3D() == core::Cursor3D{});
    REQUIRE(model.setCursorPosition({1.25F, 2.5F, -3}));
    model.setCursorVisible(false);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.undoStack()->count() == 0);
    REQUIRE(model.scene()->nodes().empty());
    REQUIRE(dirty.isEmpty());
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(reopened.cursor3D() == core::Cursor3D{});
    REQUIRE(model.saveScene(path));
    REQUIRE(reopened.openScene(path));
    REQUIRE(reopened.cursor3D() == model.cursor3D());
    for (auto kind : {core::PrimitiveKind::Cube, core::PrimitiveKind::Sphere,
                      core::PrimitiveKind::Plane, core::PrimitiveKind::Empty}) {
        const auto before = model.undoStack()->index();
        const auto position = model.cursor3D().position;
        const auto entity = model.createEntity(kind);
        REQUIRE(model.scene()->find(entity)->transform.position == position);
        REQUIRE(model.scene()->find(entity)->parent == 0);
        REQUIRE(model.undoStack()->index() == before + 1);
        REQUIRE(model.setCursorPosition(position + glm::vec3(1, 0, 0)));
        model.undo();
        REQUIRE_FALSE(model.scene()->find(entity));
        REQUIRE(model.cursor3D().position == position + glm::vec3(1, 0, 0));
        model.redo();
        REQUIRE(model.scene()->find(entity)->transform.position == position);
    }
    const auto before = model.cursor3D();
    const auto history = model.undoStack()->count();
    REQUIRE_FALSE(model.setCursorPosition({std::numeric_limits<float>::infinity(), 0, 0}));
    REQUIRE(model.cursor3D() == before);
    REQUIRE(model.undoStack()->count() == history);
    QFile invalid(dir.filePath(QStringLiteral("invalid.m3dscene")));
    REQUIRE(invalid.open(QIODevice::WriteOnly));
    REQUIRE(invalid.write("{broken") == 7);
    invalid.close();
    REQUIRE_FALSE(model.openScene(invalid.fileName()));
    REQUIRE(model.cursor3D() == before);
    REQUIRE(model.undoStack()->count() == history);
    model.newScene();
    REQUIRE(model.cursor3D() == core::Cursor3D{});
}

TEST_CASE("Cursor to selection uses parent world origin or unique selected mesh vertices",
          "[cursor-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    REQUIRE_FALSE(model.moveCursorToSelection());
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(cube, parent));
    core::Transform transform;
    transform.position = {2, 3, -1};
    transform.rotation = glm::quat(glm::radians(glm::vec3(25, 45, 15)));
    transform.scale = {-2, 3, 1};
    REQUIRE(model.setTransform(parent, transform));
    REQUIRE(model.setTransformComponent(cube, 0, 0, .7));
    const auto history = model.undoStack()->count();
    REQUIRE(model.moveCursorToSelection());
    REQUIRE(glm::length(model.cursor3D().position -
                        glm::vec3(model.scene()->worldMatrix(cube)[3])) < 1e-5F);
    REQUIRE(model.undoStack()->count() == history);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    model.selectComponent({3}, editor::SelectionOperation::Add);
    const auto center = model.selectedComponentCenter();
    REQUIRE(center);
    const auto beforeSelection = model.componentSelection();
    const auto editHistory = model.undoStack()->count();
    REQUIRE(model.moveCursorToSelection());
    REQUIRE(glm::length(glm::dvec3(model.cursor3D().position) - *center) < 1e-5);
    REQUIRE(model.componentSelection() == beforeSelection);
    REQUIRE(model.undoStack()->count() == editHistory);
    REQUIRE(model.beginComponentTransform());
    auto delta = glm::dmat4(1);
    delta[3].x = 5;
    REQUIRE(model.previewComponentTransform(delta));
    REQUIRE(model.moveCursorToSelection());
    REQUIRE_FALSE(model.hasComponentTransform());
    REQUIRE(glm::length(glm::dvec3(model.cursor3D().position) - *center) < 1e-5);
    model.clearComponentSelection();
    REQUIRE_FALSE(model.moveCursorToSelection());
}

TEST_CASE("Cursor numeric sidebar submits cancels rejects invalid input and creates at cursor",
          "[cursor-ui]") {
    CursorWindow f;
    f.window.findChild<editor::WorkbenchShell*>()->setSidebarVisible(true);
    QTest::qWait(30);
    auto* sidebar = f.window.findChild<QScrollArea*>(QStringLiteral("ViewportSidebar"));
    auto* field = f.window.findChild<editor::CommitSpinBox*>(QStringLiteral("CursorPositionX"));
    REQUIRE(sidebar);
    REQUIRE(field);
    sidebar->ensureWidgetVisible(field);
    const auto type = [field](const char* text) {
        field->setFocus();
        field->selectAll();
        QTest::keyClicks(field, text);
    };
    type("2.75");
    REQUIRE(f.model->cursor3D().position.x == 0);
    QTest::keyClick(field, Qt::Key_Escape);
    REQUIRE(field->value() == 0);
    type("2.75");
    QTest::keyClick(field, Qt::Key_Return);
    REQUIRE(f.model->cursor3D().position.x == 2.75F);
    REQUIRE_FALSE(f.model->isModified());
    REQUIRE(f.model->undoStack()->count() == 0);
    for (const auto* invalid : {"bad", "nan", "1e999"}) {
        type(invalid);
        QTest::keyClick(field, Qt::Key_Return);
        REQUIRE_FALSE(field->validationMessage().isEmpty());
        REQUIRE(f.model->cursor3D().position.x == 2.75F);
        QTest::keyClick(field, Qt::Key_Escape);
    }
    auto* input = field->findChild<QLineEdit*>();
    QInputMethodEvent composition(QStringLiteral("游标"), {});
    QApplication::sendEvent(input, &composition);
    REQUIRE(f.model->cursor3D().position.x == 2.75F);
    QInputMethodEvent endComposition;
    QApplication::sendEvent(input, &endComposition);
    f.action("CreateCube")->trigger();
    const auto cube = f.model->selection()->selectedEntity();
    REQUIRE(f.model->scene()->find(cube)->transform.position == glm::vec3(2.75F, 0, 0));
    f.action("CursorToOrigin")->trigger();
    REQUIRE(f.model->cursor3D().position == glm::vec3(0));
    f.action("CursorToSelection")->trigger();
    REQUIRE(f.model->cursor3D().position == glm::vec3(2.75F, 0, 0));
    REQUIRE(f.viewport->focusAll());
    f.capture(QStringLiteral("-numeric"));
    const auto camera = f.model->createCamera();
    REQUIRE(f.model->setPreviewCamera(camera));
    REQUIRE_FALSE(field->isEnabled());
    REQUIRE_FALSE(f.action("PlaceCursor")->isEnabled());
    const auto previous = f.model->cursor3D();
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::ShiftModifier, f.viewport->rect().center());
    REQUIRE_FALSE(f.model->setCursorPosition({0, 0, 0}));
    REQUIRE(f.model->cursor3D() == previous);
}

TEST_CASE("Cursor placement preserves selection and cancels independently of other sessions",
          "[cursor-ui]") {
    CursorWindow f;
    const auto cube = f.model->createEntity(core::PrimitiveKind::Cube);
    const auto history = f.model->undoStack()->count();
    const auto point = f.project({.1F, .1F, .5F});
    const auto camera = f.viewport->editorCameraSnapshot();
    const auto expected = editor::locateCursor(*f.model->scene(), *f.model->assets(), *camera,
                                               {float(point.x()), float(point.y())},
                                               {f.viewport->width(), f.viewport->height()});
    REQUIRE(expected.position);
    REQUIRE(expected.surface);
    QSignalSpy picks(f.viewport, &renderer_gl::ViewportWidget::pickRequested);
    QSignalSpy placements(f.viewport, &renderer_gl::ViewportWidget::cursorPlacementRequested);
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::ShiftModifier, point);
    REQUIRE(placements.count() == 1);
    REQUIRE(glm::length(f.model->cursor3D().position - *expected.position) < 1e-5F);
    REQUIRE(f.model->selection()->selectedEntity() == cube);
    REQUIRE(f.model->undoStack()->count() == history);
    REQUIRE(picks.isEmpty());
    const auto previous = f.model->cursor3D();
    auto* place = f.action("PlaceCursor");
    place->trigger();
    REQUIRE(f.viewport->isCursorPlacementEnabled());
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE_FALSE(f.viewport->isCursorPlacementEnabled());
    REQUIRE_FALSE(place->isChecked());
    REQUIRE(f.viewport->cursor().shape() == Qt::ArrowCursor);
    REQUIRE(f.model->cursor3D() == previous);
    f.router->setKeymap(editor::EditorKeymap::Legacy);
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::ShiftModifier, f.project({-1, 0, 1}));
    REQUIRE(f.model->cursor3D() == previous);
    place->trigger();
    QTest::mouseClick(f.viewport, Qt::LeftButton, Qt::NoModifier, f.project({-1, 0, 1}));
    REQUIRE_FALSE(f.viewport->isCursorPlacementEnabled());
    REQUIRE(std::abs(f.model->cursor3D().position.y) < 1e-5F);
    REQUIRE(picks.isEmpty());
    f.router->setKeymap(editor::EditorKeymap::Blender);
    auto* modal = f.window.findChild<editor::ObjectTransformSession*>();
    f.pointAt();
    REQUIRE(modal->start(editor::TransformOperation::Move));
    const auto beforeCancel = f.model->cursor3D();
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::ShiftModifier, point);
    REQUIRE_FALSE(modal->isActive());
    REQUIRE(f.model->cursor3D() == beforeCancel);
    REQUIRE(f.model->setEditMode(true));
    place->trigger();
    f.action("BoxSelectComponents")->trigger();
    auto* box = f.window.findChild<editor::ComponentInteraction*>();
    REQUIRE(box->isBoxSelecting());
    REQUIRE_FALSE(f.viewport->isCursorPlacementEnabled());
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::ShiftModifier, point);
    REQUIRE_FALSE(box->isBoxSelecting());
    REQUIRE(f.model->cursor3D() == beforeCancel);
    REQUIRE(f.viewport->cursor().shape() == Qt::ArrowCursor);
    auto* loop = f.window.findChild<editor::LoopCutSession*>();
    f.pointAt();
    REQUIRE(loop->start());
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::ShiftModifier, point);
    REQUIRE(loop->stage() == editor::LoopCutStage::Inactive);
    REQUIRE(f.model->cursor3D() == beforeCancel);
    for (auto type : {QEvent::FocusOut, QEvent::WindowDeactivate}) {
        place->trigger();
        REQUIRE(f.viewport->isCursorPlacementEnabled());
        QEvent event(type);
        QApplication::sendEvent(f.viewport, &event);
        REQUIRE_FALSE(f.viewport->isCursorPlacementEnabled());
    }
    place->trigger();
    f.model->newScene();
    REQUIRE_FALSE(f.viewport->isCursorPlacementEnabled());
}

TEST_CASE("Cursor search actions execute real placement and overlay retains GL rendering",
          "[cursor-ui]") {
    CursorWindow f;
    f.model->createEntity(core::PrimitiveKind::Cube);
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    auto context =
        registry->captureContext(editor::InputArea::Viewport, editor::EditorKeymap::Blender);
    const auto hits = registry->search(QStringLiteral("游标"), context);
    REQUIRE(hits.size() == 6);
    for (const auto& hit : hits)
        REQUIRE(registry->descriptor(hit.id)->undoable ==
                (hit.id == QStringLiteral("selection.to_cursor")));
    REQUIRE(f.model->setCursorPosition({1.3F, 0, .5F}));
    f.pointAt();
    QTest::keyClick(f.viewport, Qt::Key_F3);
    auto* popup = f.window.findChild<editor::OperatorSearchPopup*>();
    REQUIRE(popup);
    REQUIRE(popup->isVisible());
    auto* search = popup->findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    REQUIRE(search);
    search->setText(QStringLiteral("Cursor to World Origin"));
    QTest::keyClick(search, Qt::Key_Return);
    REQUIRE(f.model->cursor3D().position == glm::vec3(0));
    f.pointAt();
    REQUIRE(registry->execute(QStringLiteral("cursor.place"), context));
    REQUIRE(f.viewport->isCursorPlacementEnabled());
    QTest::mouseClick(f.viewport, Qt::LeftButton, Qt::NoModifier, f.project({1.3F, 0, .5F}));
    REQUIRE_FALSE(f.viewport->isCursorPlacementEnabled());
    auto* glContext = f.viewport->context();
    REQUIRE(glContext);
    REQUIRE(f.viewport->rect().contains(f.project(f.model->cursor3D().position)));
    REQUIRE(f.viewport->isOverlayVisible());
    REQUIRE(f.model->previewCamera() == 0);
    f.model->setCursorVisible(false);
    QTest::qWait(30);
    const auto hidden = f.viewport->grabFramebuffer();
    REQUIRE_FALSE(hidden.isNull());
    f.model->setCursorVisible(true);
    QTest::qWait(30);
    const auto shown = f.viewport->grabFramebuffer();
    REQUIRE(shown != hidden);
    f.model->setCursorVisible(false);
    QTest::qWait(30);
    REQUIRE(f.viewport->grabFramebuffer() == hidden);
    // 下一帧去掉游标后完全相同，验证覆盖层不污染后续场景绘制状态。
    f.viewport->setOverlayVisible(false);
    QTest::qWait(30);
    const auto noOverlay = f.viewport->grabFramebuffer();
    f.model->setCursorVisible(true);
    QTest::qWait(30);
    REQUIRE(f.viewport->grabFramebuffer() == noOverlay);
    f.viewport->setOverlayVisible(true);
    QTest::qWait(30);
    REQUIRE(f.viewport->grabFramebuffer() == shown);
    REQUIRE(f.viewport->context() == glContext);
    f.window.findChild<editor::WorkbenchShell*>()->setSidebarVisible(true);
    QTest::qWait(30);
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::ShiftModifier, f.project({.1F, .1F, .5F}));
    f.capture(QStringLiteral("-surface"));
    const auto prefix = qEnvironmentVariable("MINI3D_TEST_CURSOR_CAPTURE");
    if (!prefix.isEmpty())
        REQUIRE(f.model->saveScene(prefix + QStringLiteral(".m3dscene")));
}
