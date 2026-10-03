/*
 * 模块名: P1UiEditorTests
 * 功能概述: 验证真实饼菜单、显示状态与Repeat当前选区的新历史契约。
 * 对外接口: Catch2 [p1-ui]、[repeat-editor]、[pie-editor]。
 * 依赖关系: Qt Test、Registry、SceneViewModel、真实OpenGL窗口。
 * 输入输出: 快捷键/菜单/参数到当前源几何、历史与真实帧缓冲。
 * 异常与错误: 过期上下文、非法选区或参数不改变文档和保存点。
 * 维护说明: 合成事件不替代真实IME/跨屏；文件只写临时目录。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/workbench/OperatorPiePopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>

using namespace mini3d;
namespace {
const core::modeling::EditableMesh& source(const editor::SceneViewModel& model,
                                           core::EntityId entity) {
    return model.scene()->editableMesh(model.scene()->find(entity)->editableMesh)->content->source;
}
core::EntityId enterFace(editor::SceneViewModel& model) {
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    return entity;
}
void pointAt(renderer_gl::ViewportWidget& viewport) {
    const QPoint point(viewport.width() / 2, viewport.height() / 2);
    QMouseEvent move(QEvent::MouseMove, point, viewport.mapToGlobal(point), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &move);
    viewport.setFocus();
}
} // namespace

TEST_CASE("Repeat extrudes current result as a new command while F9 adjusts only that command",
          "[p1-ui][repeat-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterFace(model);
    REQUIRE(model.beginExtrudeRegion());
    const auto offset = model.componentExtrusion()->normal * .25;
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1), offset)));
    REQUIRE(model.finishComponentTransform(true));
    const auto first = source(model, entity);
    const auto index = model.undoStack()->index();
    const auto* firstCommand = model.undoStack()->command(index - 1);
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(model.saveScene(directory.filePath("repeat.m3dscene")));
    REQUIRE(model.repeatLastOperationDisabledReason().isEmpty());
    REQUIRE(model.repeatLastOperation());
    REQUIRE(model.undoStack()->index() == index + 1);
    REQUIRE(model.undoStack()->command(index - 1) == firstCommand);
    const auto expected = core::modeling::extrudeRegion(first, {1}, offset);
    REQUIRE(expected.mesh);
    REQUIRE(source(model, entity) == *expected.mesh);
    REQUIRE(model.isModified());
    REQUIRE(model.adjustLastOperation(offset * 2.0));
    REQUIRE(model.undoStack()->index() == index + 1);
    const auto adjusted = core::modeling::extrudeRegion(first, {1}, offset * 2.0);
    REQUIRE(adjusted.mesh);
    REQUIRE(source(model, entity) == *adjusted.mesh);
    model.undo();
    REQUIRE(source(model, entity) == first);
    REQUIRE_FALSE(model.isModified());
    model.redo();
    REQUIRE(source(model, entity) == *adjusted.mesh);
}

TEST_CASE("Repeat uses another current editable object and its selected face not frozen identities",
          "[p1-ui][repeat-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto first = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(first));
    const auto second = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(second));
    model.selection()->setSelectedEntity(first);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    REQUIRE(model.beginInsetFace());
    REQUIRE(model.previewInsetFace(.1));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.adjustLastInset(.15));
    const auto firstAfter = source(model, first);
    const auto secondBefore = source(model, second);
    const auto index = model.undoStack()->index();
    model.selection()->setSelectedEntity(second);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({5}, editor::SelectionOperation::Replace);
    REQUIRE_FALSE(model.lastOperationDisabledReason().isEmpty());
    REQUIRE(model.repeatLastOperationDisabledReason().isEmpty());
    REQUIRE(model.repeatLastOperation());
    const auto expected = core::modeling::insetFace(secondBefore, 5, .15);
    REQUIRE(expected.mesh);
    REQUIRE(source(model, second) == *expected.mesh);
    REQUIRE(source(model, first) == firstAfter);
    REQUIRE(model.undoStack()->index() == index + 1);
    model.undo();
    REQUIRE(source(model, second) == secondBefore);
    REQUIRE(source(model, first) == firstAfter);
}

TEST_CASE(
    "Repeat invalid thickness and unsupported contexts preserve geometry selection and save point",
    "[p1-ui][repeat-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterFace(model);
    REQUIRE(model.beginInsetFace());
    REQUIRE(model.previewInsetFace(.3));
    REQUIRE(model.finishComponentTransform(true));
    const auto before = source(model, entity);
    const auto selected = model.componentSelection();
    const auto index = model.undoStack()->index();
    QTemporaryDir directory;
    REQUIRE(model.saveScene(directory.filePath("invalid-repeat.m3dscene")));
    REQUIRE_FALSE(model.repeatLastOperation()); // 当前中心面已变小，同宽度不再合法。
    REQUIRE(source(model, entity) == before);
    REQUIRE(model.componentSelection() == selected);
    REQUIRE_FALSE(model.hasComponentTransform());
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.setEditMode(false));
    REQUIRE_FALSE(model.repeatLastOperation());
    REQUIRE(model.undoStack()->index() == index);
    model.newScene();
    REQUIRE_FALSE(model.repeatLastOperation());
    REQUIRE(model.undoStack()->index() == 0);
}

TEST_CASE("Repeat bevel applies frozen width to the current object's edge not the former edge",
          "[p1-ui][repeat-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto first = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(first));
    const auto second = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(second));
    model.selection()->setSelectedEntity(first);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Edge);
    model.selectComponent({5, 6}, editor::SelectionOperation::Replace);
    REQUIRE(model.beginBevelEdge());
    REQUIRE(model.previewBevelEdge(.1));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.adjustLastBevel(.15));
    const auto firstAfter = source(model, first);
    const auto secondBefore = source(model, second);
    const auto index = model.undoStack()->index();
    model.selection()->setSelectedEntity(second);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Edge);
    model.selectComponent({1, 2}, editor::SelectionOperation::Replace);
    REQUIRE(model.repeatLastOperation());
    const auto expected = core::modeling::bevelEdge(secondBefore, {1, 2}, .15);
    REQUIRE(expected.mesh);
    REQUIRE(source(model, second) == *expected.mesh);
    REQUIRE(source(model, first) == firstAfter);
    REQUIRE(model.undoStack()->index() == index + 1);
    model.undo();
    REQUIRE(source(model, second) == secondBefore);
}

TEST_CASE("Z and view pies reuse registry modes protect text and reject stale document context",
          "[p1-ui][pie-editor]") {
    editor::MainWindow window;
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    REQUIRE(model);
    REQUIRE(viewport);
    REQUIRE(registry);
    REQUIRE(router);
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    router->setKeymap(editor::EditorKeymap::Blender);
    const auto entity = model->createEntity(core::PrimitiveKind::Cube);
    const auto index = model->undoStack()->index();
    QSignalSpy executed(registry, &editor::OperatorRegistry::executed);
    pointAt(*viewport);
    QTest::keyClick(viewport, Qt::Key_Z);
    auto* shade = window.findChild<QDialog*>(QStringLiteral("ShadingPiePopup"));
    REQUIRE(shade);
    REQUIRE(shade->isVisible());
    const auto capture = qEnvironmentVariable("MINI3D_TEST_P1_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        REQUIRE(QDir().mkpath(capture));
        REQUIRE(shade->grab().save(QDir(capture).filePath("shading-pie.png")));
    }
    QTest::keyClick(shade, Qt::Key_2);
    REQUIRE(viewport->shadingMode() == renderer_gl::ViewportShading::Solid);
    REQUIRE(executed.last()[0].toString() == QStringLiteral("view.shading_solid"));
    REQUIRE(model->undoStack()->index() == index);
    REQUIRE(model->scene()->find(entity));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    REQUIRE(window.findChild<QAction*>(QStringLiteral("ShadingSolid"))->isChecked());
    REQUIRE(window.findChild<QToolButton*>(QStringLiteral("ShadingModeButton"))->text() ==
            QStringLiteral("实体"));
    pointAt(*viewport);
    QTest::keyClick(viewport, Qt::Key_QuoteLeft);
    auto* view = window.findChild<QDialog*>(QStringLiteral("ViewPiePopup"));
    REQUIRE(view);
    if (!capture.isEmpty())
        REQUIRE(view->grab().save(QDir(capture).filePath("view-pie.png")));
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
                      QPoint(view->width() / 2, view->height() / 6));
    REQUIRE(viewport->editorCameraSnapshot()->view() == renderer_gl::EditorView::Front);
    REQUIRE(executed.last()[0].toString() == QStringLiteral("view.front"));
    REQUIRE(model->undoStack()->index() == index);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto* text = new QLineEdit(window.centralWidget());
    text->show();
    text->setFocus();
    pointAt(*viewport);
    text->setFocus();
    QTest::keyClicks(text, "z`R");
    REQUIRE(text->text() == QStringLiteral("z`R"));
    REQUIRE_FALSE(window.findChild<QDialog*>(QStringLiteral("ShadingPiePopup")));
    REQUIRE_FALSE(window.findChild<QDialog*>(QStringLiteral("ViewPiePopup")));
    text->hide();
    const auto stale = registry->captureContext(editor::InputArea::Viewport);
    auto* popup = new editor::OperatorPiePopup(*registry, stale, QStringLiteral("过期测试"),
                                               {QStringLiteral("view.shading_wireframe")}, &window);
    popup->openAt(viewport->mapToGlobal(viewport->rect().center()));
    model->newScene();
    QSignalSpy failures(registry, &editor::OperatorRegistry::executionFailed);
    QTest::keyClick(popup, Qt::Key_1);
    REQUIRE(popup->isVisible());
    auto* description = popup->findChild<QLabel*>(QStringLiteral("OperatorPieDescription"));
    REQUIRE(description);
    REQUIRE(description->text().contains(QStringLiteral("文档")));
    REQUIRE_FALSE(registry->execute(QStringLiteral("view.shading_wireframe"), stale));
    REQUIRE_FALSE(failures.isEmpty());
    REQUIRE(viewport->shadingMode() == renderer_gl::ViewportShading::Solid);
    popup->close();
    window.hide();
}

TEST_CASE("Shift R and registry Repeat create new history while F9 remains parameter replacement",
          "[p1-ui][repeat-editor][repeat-ui]") {
    editor::MainWindow window;
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    router->setKeymap(editor::EditorKeymap::Blender);
    const auto entity = enterFace(*model);
    REQUIRE(model->beginInsetFace());
    REQUIRE(model->previewInsetFace(.05));
    REQUIRE(model->finishComponentTransform(true));
    REQUIRE(registry->descriptor(QStringLiteral("mesh.inset_face"))->repeatable);
    REQUIRE_FALSE(registry->descriptor(QStringLiteral("view.front"))->repeatable);
    const auto beforeRepeat = source(*model, entity);
    const auto index = model->undoStack()->index();
    pointAt(*viewport);
    QTest::keyClick(viewport, Qt::Key_R, Qt::ShiftModifier);
    const auto expected = core::modeling::insetFace(beforeRepeat, 1, .05);
    REQUIRE(expected.mesh);
    REQUIRE(source(*model, entity) == *expected.mesh);
    REQUIRE(model->undoStack()->index() == index + 1);
    const auto context = registry->captureContext(editor::InputArea::Viewport);
    model->clearComponentSelection();
    REQUIRE_FALSE(registry->execute(QStringLiteral("history.repeat_last"), context));
    REQUIRE(model->undoStack()->index() == index + 1);
    REQUIRE_FALSE(window.findChild<QAction*>(QStringLiteral("RepeatLastOperation"))->isEnabled());
    window.hide();
}

TEST_CASE("Shading state survives first GL initialization and never changes document history",
          "[p1-ui][shading-editor]") {
    editor::MainWindow window;
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    const auto entity = enterFace(*model);
    REQUIRE(model->setEditMode(false));
    const auto before = source(*model, entity);
    const auto index = model->undoStack()->index();
    QSignalSpy changes(viewport, &renderer_gl::ViewportWidget::shadingModeChanged);
    viewport->setShadingMode(renderer_gl::ViewportShading::Wireframe);
    viewport->setShadingMode(renderer_gl::ViewportShading::Wireframe);
    REQUIRE(changes.count() == 1);
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    QTest::qWait(30);
    REQUIRE(viewport->shadingMode() == renderer_gl::ViewportShading::Wireframe);
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    const auto wire = viewport->grabFramebuffer();
    window.findChild<QAction*>(QStringLiteral("ShadingSolid"))->trigger();
    QTest::qWait(30);
    REQUIRE(viewport->grabFramebuffer() != wire);
    REQUIRE(model->undoStack()->index() == index);
    REQUIRE(source(*model, entity) == before);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_P1_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        REQUIRE(QDir().mkpath(capture));
        REQUIRE(window.grab().save(QDir(capture).filePath("shading-solid.png")));
        window.findChild<QAction*>(QStringLiteral("ShadingWireframe"))->trigger();
        QTest::qWait(30);
        REQUIRE(window.grab().save(QDir(capture).filePath("shading-wireframe.png")));
        window.findChild<QAction*>(QStringLiteral("ShadingMaterial"))->trigger();
        QTest::qWait(30);
        REQUIRE(window.grab().save(QDir(capture).filePath("shading-material.png")));
    }
    window.hide();
}
