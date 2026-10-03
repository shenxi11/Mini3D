/*
 * 模块名: MirrorEditorTests
 * 功能概述: 验证 Mirror 卡片业务入口、组件夹持及唯一历史保存。
 * 对外接口: Catch2 [mirror02-editor]。
 * 依赖关系: SceneViewModel、Qt 临时目录、Catch2。
 * 输入输出: 参数与组件变换到可撤销快照和格式 3 文件。
 * 异常与错误: 断言失败报告回归。
 * 维护说明: 不做全量窗口/DPI/耐久验收。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"

#include <QAction>
#include <QPushButton>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>

using namespace mini3d;

TEST_CASE("Mirror apply uses one undo stack and reopens with current modifier state",
          "[mirror02-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(entity));
    const auto* node = model.scene()->find(entity);
    const auto source = model.scene()->editableMesh(node->editableMesh)->content->source;
    const auto initialIndex = model.undoStack()->index();
    core::modeling::MirrorOptions options;
    REQUIRE(model.setMirrorOptions(entity, options));
    REQUIRE(model.undoStack()->index() == initialIndex + 1);
    REQUIRE(model.mirrorOptions(entity) == options);
    REQUIRE(model.scene()->editableMesh(node->editableMesh)->content->source == source);
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("mirror02.m3dscene"));
    REQUIRE(model.saveScene(path));
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(reopened.mirrorOptions(entity) == options);
    const auto evaluated = model.scene()->editableMesh(node->editableMesh)
                               ->content->mirrorEvaluation->mesh;
    REQUIRE(model.applyMirror(entity));
    REQUIRE(model.undoStack()->index() == initialIndex + 2);
    REQUIRE_FALSE(model.mirrorOptions(entity));
    REQUIRE(model.scene()->editableMesh(node->editableMesh)->content->source == evaluated);
    model.undo();
    REQUIRE(model.mirrorOptions(entity) == options);
    REQUIRE(model.scene()->editableMesh(node->editableMesh)->content->source == source);
    model.redo();
    REQUIRE_FALSE(model.mirrorOptions(entity));
    REQUIRE(model.scene()->editableMesh(node->editableMesh)->content->source == evaluated);
}

TEST_CASE("Mirror clipping constrains a selected source boundary during component move",
          "[mirror02-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(entity));
    core::modeling::EditableMesh half;
    half.vertices = {{1, {0, -1, 0}}, {2, {1, -1, 0}},
                     {3, {1, 1, 0}}, {4, {0, 1, 0}}};
    core::modeling::EditableFace face;
    face.id = 1;
    for (std::uint64_t id = 1; id <= 4; ++id)
        face.corners.push_back({id, id, {0, 0}, std::nullopt, {1, 1, 1}});
    half.faces.push_back(face);
    REQUIRE(model.replaceEditableMesh(entity, half));
    REQUIRE(model.setMirrorOptions(entity, core::modeling::MirrorOptions{}));
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Vertex);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1),
                                                           glm::dvec3(-0.25, 0.5, 0))));
    REQUIRE(model.finishComponentTransform(true));
    const auto* content = model.scene()->editableMesh(model.scene()->find(entity)->editableMesh)
                              ->content.get();
    REQUIRE(content->source.vertex(1)->position.x == 0);
    REQUIRE(content->source.vertex(1)->position.y == -0.5F);
    REQUIRE(content->mirrorEvaluation);
}

TEST_CASE("Mirror card and registered actions follow editable selection", "[mirror02-editor]") {
    editor::MainWindow window;
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* add = window.findChild<QPushButton*>(QStringLiteral("MirrorAddButton"));
    auto* apply = window.findChild<QAction*>(QStringLiteral("MirrorApply"));
    REQUIRE(model);
    REQUIRE(add);
    REQUIRE(apply);
    model->newScene();
    const auto entity = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE_FALSE(add->isEnabled());
    REQUIRE(model->makeEditable(entity));
    REQUIRE(add->isEnabled());
    add->click();
    REQUIRE(model->mirrorOptions(entity));
    REQUIRE(apply->isEnabled());
    apply->trigger();
    REQUIRE_FALSE(model->mirrorOptions(entity));
}

TEST_CASE("Rejected Mirror evaluation does not capture clipping vertices",
          "[mirror02-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(entity));
    core::modeling::EditableMesh mesh;
    mesh.vertices = {{1, {.2F, 0, 0}}, {2, {.2F, 1, 0}},
                     {3, {0, 0, -1}}, {4, {1, 1, -1}},
                     {5, {0, 1, 1}}, {6, {1, 0, 1}}};
    for (const auto& [faceId, corners] :
         {std::pair{1ULL, std::array<std::uint64_t, 4>{1, 3, 4, 2}},
          std::pair{2ULL, std::array<std::uint64_t, 4>{2, 5, 6, 1}}}) {
        core::modeling::EditableFace face;
        face.id = faceId;
        for (const auto vertex : corners)
            face.corners.push_back({faceId * 10 + vertex, vertex, {0, 0}, std::nullopt,
                                    {1, 1, 1}});
        mesh.faces.push_back(std::move(face));
    }
    REQUIRE(model.replaceEditableMesh(entity, mesh));
    REQUIRE(model.setMirrorOptions(entity, core::modeling::MirrorOptions{}));
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Vertex);
    model.selectComponents({{1}, {2}}, editor::SelectionOperation::Replace);
    REQUIRE(model.beginComponentTransform());
    REQUIRE_FALSE(model.previewComponentTransform(
        glm::translate(glm::dmat4(1), glm::dvec3(-0.4, 0, 0))));
    REQUIRE(model.previewComponentTransform(
        glm::translate(glm::dmat4(1), glm::dvec3(0.2, 0, 0))));
    REQUIRE(model.componentPreview()->source.vertex(1)->position.x == .4F);
    REQUIRE(model.componentPreview()->source.vertex(2)->position.x == .4F);
    REQUIRE(model.finishComponentTransform(false));
}
