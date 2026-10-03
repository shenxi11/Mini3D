/*
 * 模块名: BevelEditorTests
 * 功能概述: 验证倒角独立预览、闭合结果选区、单条历史及F9/保存/撤销联动。
 * 对外接口: Catch2 [bevel-editor]；依赖关系: Qt Test、真实窗口、Core倒角/镜像。
 * 输入输出: 选边、宽度和上下文变化到候选、历史、保存点与模态状态断言。
 * 异常与错误: 隐藏受影响面、无效宽度和错类型参数不得修改真源或保存点。
 * 维护说明: 文件只写QTemporaryDir；F9以独立CPU结果验证冻结原边和原before。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/workbench/CommitSpinBox.h"
#include "editor/workbench/LastOperationPanel.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>

using namespace mini3d;
namespace {
core::EntityId enterBevel(editor::SceneViewModel& model) {
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Edge);
    model.selectComponent({5, 6}, editor::SelectionOperation::Replace);
    return entity;
}
const core::EditableMeshRecord& meshRecord(const editor::SceneViewModel& model,
                                           core::EntityId entity) {
    return *model.scene()->editableMesh(model.scene()->find(entity)->editableMesh);
}
void pointAtViewport(renderer_gl::ViewportWidget& viewport) {
    const QPoint point(viewport.width() * 2 / 3, viewport.height() / 2);
    QMouseEvent event(QEvent::MouseMove, point, viewport.mapToGlobal(point), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&viewport, &event);
    viewport.setFocus();
}
} // namespace

TEST_CASE("Bevel preview cancel F9 and Undo keep one frozen source and the result face selection",
          "[bevel-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterBevel(model);
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("倒角事务.m3dscene"));
    REQUIRE(model.saveScene(path));
    const auto before = meshRecord(model, entity);
    const auto selection = model.componentSelection();
    const auto count = model.undoStack()->count();
    REQUIRE(model.beginBevelEdge());
    REQUIRE(model.componentBevel());
    REQUIRE_FALSE(model.componentInset());
    REQUIRE_FALSE(model.componentExtrusion());
    REQUIRE_FALSE(model.finishComponentTransform(true));
    for (const auto width : {.1, .2}) {
        REQUIRE(model.previewBevelEdge(width));
        const auto expected = core::modeling::bevelEdge(before.content->source, {5, 6}, width);
        REQUIRE(expected.mesh);
        REQUIRE(model.componentPreview()->source == *expected.mesh);
        REQUIRE(model.displayedComponentSelection().domain() == editor::SelectionDomain::Face);
        REQUIRE(model.displayedComponentSelection().selectedIds() ==
                std::set<editor::ComponentId>{{expected.bevelFace}});
        REQUIRE(model.componentSelection() == selection);
        REQUIRE(meshRecord(model, entity).content == before.content);
        REQUIRE(meshRecord(model, entity).evaluationRevision == before.evaluationRevision);
        REQUIRE(model.undoStack()->count() == count);
        REQUIRE_FALSE(model.isModified());
    }
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE(model.componentSelection() == selection);
    REQUIRE(meshRecord(model, entity).content == before.content);
    REQUIRE(model.beginBevelEdge());
    REQUIRE(model.previewBevelEdge(.2));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE(model.lastOperationBevelWidth() == .2);
    REQUIRE_FALSE(model.lastOperationInsetThickness());
    REQUIRE_FALSE(model.lastOperationWorldOffset());
    const auto* command = model.undoStack()->command(count);
    for (const auto width : {.1, .3, .25}) {
        // 当前源已没有原倒角边(5,6)；新选另一条边仍必须重算冻结的原边。
        model.setSelectionDomain(editor::SelectionDomain::Edge);
        model.selectComponent({1, 4}, editor::SelectionOperation::Replace);
        REQUIRE(model.adjustLastBevel(width));
        const auto expected = core::modeling::bevelEdge(before.content->source, {5, 6}, width);
        REQUIRE(expected.mesh);
        REQUIRE(meshRecord(model, entity).content->source == *expected.mesh);
        REQUIRE(model.componentSelection().domain() == editor::SelectionDomain::Face);
        REQUIRE(model.componentSelection().selectedIds() ==
                std::set<editor::ComponentId>{{expected.bevelFace}});
        REQUIRE(model.lastOperationBevelWidth() == width);
        REQUIRE(model.undoStack()->command(count) == command);
        REQUIRE(model.undoStack()->count() == count + 1);
        REQUIRE(model.isModified());
    }
    const auto after = meshRecord(model, entity);
    const auto afterSelection = model.componentSelection();
    model.undo();
    REQUIRE(meshRecord(model, entity).content == before.content);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE_FALSE(model.isModified());
    REQUIRE_FALSE(model.lastOperationBevelWidth());
    model.redo();
    REQUIRE(meshRecord(model, entity).content == after.content);
    REQUIRE(model.componentSelection() == afterSelection);
    REQUIRE(model.saveScene(path));
    const auto saved = meshRecord(model, entity);
    REQUIRE(model.adjustLastBevel(.25));
    REQUIRE_FALSE(model.isModified());
    for (const auto width : {0., -.1, 1., std::numeric_limits<double>::quiet_NaN()}) {
        REQUIRE_FALSE(model.adjustLastBevel(width));
        REQUIRE(meshRecord(model, entity).content == saved.content);
        REQUIRE(meshRecord(model, entity).evaluationRevision == saved.evaluationRevision);
        REQUIRE(model.componentSelection() == afterSelection);
        REQUIRE(model.lastOperationBevelWidth() == .25);
        REQUIRE(model.undoStack()->command(count) == command);
        REQUIRE_FALSE(model.isModified());
    }
    REQUIRE_FALSE(model.adjustLastInset(.2));
    REQUIRE_FALSE(model.adjustLastOperation({0, 0, .2}));
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.adjustLastBevel(.35));
    REQUIRE(model.isModified());
    REQUIRE(model.undoStack()->count() == count + 1);
    editor::SceneViewModel disk;
    REQUIRE(disk.openScene(path));
    REQUIRE(meshRecord(disk, entity).content->source == saved.content->source);
    REQUIRE_FALSE(disk.lastOperationBevelWidth());
}

TEST_CASE(
    "Bevel rejects unsupported selection hidden affected faces and invalid previews atomically",
    "[bevel-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterBevel(model);
    const auto before = meshRecord(model, entity);
    const auto count = model.undoStack()->count();
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    REQUIRE_FALSE(model.beginBevelEdge());
    model.setSelectionDomain(editor::SelectionDomain::Edge);
    model.selectComponent({5, 6}, editor::SelectionOperation::Replace);
    model.selectComponent({6, 7}, editor::SelectionOperation::Add);
    REQUIRE_FALSE(model.beginBevelEdge());
    for (const auto face : {1, 6, 4, 3}) {
        model.setSelectionDomain(editor::SelectionDomain::Face);
        model.selectComponent({static_cast<core::modeling::FaceId>(face)},
                              editor::SelectionOperation::Replace);
        REQUIRE(model.hideSelection());
        model.setSelectionDomain(editor::SelectionDomain::Edge);
        model.selectComponent({5, 6}, editor::SelectionOperation::Replace);
        REQUIRE(model.bevelEdgeDisabledReason().contains(QStringLiteral("隐藏")));
        REQUIRE_FALSE(model.beginBevelEdge());
        REQUIRE(model.revealHidden());
        REQUIRE(meshRecord(model, entity).content == before.content);
        REQUIRE(model.undoStack()->count() == count);
    }
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({2}, editor::SelectionOperation::Replace);
    REQUIRE(model.hideSelection());
    model.setSelectionDomain(editor::SelectionDomain::Edge);
    model.selectComponent({5, 6}, editor::SelectionOperation::Replace);
    REQUIRE(model.beginBevelEdge());
    REQUIRE(model.previewBevelEdge(.2));
    const auto preview = model.componentPreview();
    const auto previewSelection = model.displayedComponentSelection();
    for (const auto width : {0., -.1, 1., std::numeric_limits<double>::quiet_NaN()}) {
        REQUIRE_FALSE(model.previewBevelEdge(width));
        REQUIRE(model.hasComponentTransform());
        REQUIRE(model.componentPreview() == preview);
        REQUIRE(model.displayedComponentSelection() == previewSelection);
        REQUIRE_FALSE(model.finishComponentTransform(true));
        REQUIRE(meshRecord(model, entity).content == before.content);
        REQUIRE(model.undoStack()->count() == count);
    }
    REQUIRE_FALSE(model.previewComponentTransform(glm::dmat4(1)));
    REQUIRE_FALSE(model.previewInsetFace(.1));
    REQUIRE(model.previewBevelEdge(.1));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE(model.lastOperationBevelWidth() == .1);
    REQUIRE(model.revealHidden());
    REQUIRE(model.beginInsetFace());
    REQUIRE_FALSE(model.previewBevelEdge(.1));
    REQUIRE(model.previewInsetFace(.02));
    REQUIRE(model.finishComponentTransform(true));
    const auto inset = meshRecord(model, entity);
    REQUIRE(model.lastOperationInsetThickness() == .02);
    REQUIRE_FALSE(model.lastOperationBevelWidth());
    REQUIRE_FALSE(model.adjustLastBevel(.2));
    REQUIRE(meshRecord(model, entity).content == inset.content);
    REQUIRE(model.beginExtrudeRegion());
    const auto offset = model.componentExtrusion()->normal * .025;
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1), offset)));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.lastOperationWorldOffset() == offset);
    REQUIRE_FALSE(model.lastOperationBevelWidth());
    REQUIRE_FALSE(model.adjustLastBevel(.2));
}

TEST_CASE("Bevel prepares mirrored candidates and keeps local widths under a transformed parent",
          "[bevel-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    core::Transform transform;
    transform.position = {3, -2, 5};
    transform.scale = {-2, 1.5F, .5F};
    transform.rotation = glm::angleAxis(glm::radians(35.F), glm::vec3(0, 1, 0));
    REQUIRE(model.setTransform(parent, transform));
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(entity, parent));
    REQUIRE(model.setEditMode(true));
    auto source = meshRecord(model, entity).content->source;
    for (auto& vertex : source.vertices)
        vertex.position.x += 1.5F;
    REQUIRE(model.replaceEditableMesh(entity, source));
    core::modeling::MirrorOptions options;
    REQUIRE(model.setMirrorOptions(entity, options));
    model.setSelectionDomain(editor::SelectionDomain::Edge);
    model.selectComponent({5, 6}, editor::SelectionOperation::Replace);
    model.setProportionalEditingEnabled(true);
    model.setProportionalConnected(false);
    REQUIRE(model.setProportionalRadius(3));
    const auto before = meshRecord(model, entity);
    const auto selection = model.componentSelection();
    const auto count = model.undoStack()->count();
    REQUIRE(model.beginBevelEdge());
    REQUIRE(model.previewBevelEdge(.1));
    REQUIRE(model.componentPreview()->mirror == options);
    REQUIRE(model.componentPreview()->mirrorEvaluation);
    const auto expected = core::modeling::bevelEdge(source, {5, 6}, .1);
    REQUIRE(expected.mesh);
    const auto mirrored = core::modeling::evaluateMirror(*expected.mesh, options);
    REQUIRE(mirrored.evaluation);
    REQUIRE(model.componentPreview()->source == *expected.mesh);
    REQUIRE(model.componentPreview()->mirrorEvaluation->mesh == mirrored.evaluation->mesh);
    REQUIRE(meshRecord(model, entity).content == before.content);
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.adjustLastBevel(.2));
    const auto adjusted = core::modeling::bevelEdge(source, {5, 6}, .2);
    REQUIRE(adjusted.mesh);
    const auto adjustedMirror = core::modeling::evaluateMirror(*adjusted.mesh, options);
    REQUIRE(adjustedMirror.evaluation);
    REQUIRE(meshRecord(model, entity).content->source == *adjusted.mesh);
    REQUIRE(meshRecord(model, entity).content->mirror == options);
    REQUIRE(meshRecord(model, entity).content->mirrorEvaluation->mesh ==
            adjustedMirror.evaluation->mesh);
    REQUIRE(model.undoStack()->count() == count + 1);
    model.undo();
    REQUIRE(meshRecord(model, entity).content == before.content);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE(model.scene()->find(parent)->transform.scale == transform.scale);
    REQUIRE(model.scene()->find(parent)->transform.rotation == transform.rotation);
}

TEST_CASE("Bevel menu Ctrl B numeric confirmation and F9 share the same real window transaction",
          "[bevel-editor][bevel-ui]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* modal = window.findChild<editor::ObjectTransformSession*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* panel = window.findChild<editor::LastOperationPanel*>();
    auto* action = window.findChild<QAction*>(QStringLiteral("BevelEdge"));
    REQUIRE(model);
    REQUIRE(viewport);
    REQUIRE(modal);
    REQUIRE(router);
    REQUIRE(panel);
    REQUIRE(action);
    const auto entity = enterBevel(*model);
    const auto before = meshRecord(*model, entity);
    const auto selection = model->componentSelection();
    const auto count = model->undoStack()->count();
    router->setKeymap(editor::EditorKeymap::Blender);
    viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
    QTest::qWait(30);
    pointAtViewport(*viewport);
    action->trigger();
    REQUIRE(modal->isActive());
    REQUIRE(model->componentBevel());
    QTest::keyClick(viewport, Qt::Key_Escape);
    REQUIRE_FALSE(modal->isActive());
    REQUIRE(model->componentSelection() == selection);
    REQUIRE(meshRecord(*model, entity).content == before.content);
    pointAtViewport(*viewport);
    QTest::keyClick(viewport, Qt::Key_B, Qt::ControlModifier);
    REQUIRE(modal->isActive());
    REQUIRE(model->componentBevel());
    QTest::keyClicks(viewport, ".2");
    QTest::keyClick(viewport, Qt::Key_Return);
    REQUIRE_FALSE(modal->isActive());
    REQUIRE(model->lastOperationBevelWidth() == .2);
    REQUIRE(model->undoStack()->count() == count + 1);
    const auto* command = model->undoStack()->command(count);
    pointAtViewport(*viewport);
    QTest::keyClick(viewport, Qt::Key_F9);
    QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    auto* width =
        panel->findChild<editor::CommitSpinBox*>(QStringLiteral("LastOperationBevelWidth"));
    auto* thickness =
        panel->findChild<editor::CommitSpinBox*>(QStringLiteral("LastOperationInsetThickness"));
    REQUIRE(width);
    REQUIRE(thickness);
    REQUIRE(width->isVisible());
    REQUIRE(width->hasFocus());
    REQUIRE_FALSE(thickness->isVisible());
    width->selectAll();
    QTest::keyClicks(width, ".3");
    QTest::keyClick(width->findChild<QLineEdit*>(), Qt::Key_Return);
    REQUIRE(model->lastOperationBevelWidth() == .3);
    REQUIRE(model->undoStack()->command(count) == command);
    REQUIRE(model->undoStack()->count() == count + 1);
    REQUIRE(meshRecord(*model, entity).content->source ==
            *core::modeling::bevelEdge(before.content->source, {5, 6}, .3).mesh);
    window.hide();
}

TEST_CASE("Hidden bevel result rejects F9 without changing a saved mesh or history",
          "[bevel-editor][bevel-hidden-result]") {
    editor::SceneViewModel model;
    const auto entity = enterBevel(model);
    REQUIRE(model.beginBevelEdge());
    REQUIRE(model.previewBevelEdge(.2));
    REQUIRE(model.finishComponentTransform(true));
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(model.saveScene(directory.filePath("hidden-result.m3dscene")));
    const auto before = meshRecord(model, entity).content;
    const auto count = model.undoStack()->count();
    const auto index = model.undoStack()->index();
    REQUIRE(model.hideSelection());
    REQUIRE_FALSE(model.adjustLastBevel(.3));
    REQUIRE(meshRecord(model, entity).content == before);
    REQUIRE(model.lastOperationBevelWidth() == .2);
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.revealHidden());
    REQUIRE(model.adjustLastBevel(.3));
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE(model.isModified());
}
