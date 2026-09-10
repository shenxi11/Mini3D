/*
 * 模块名: EditorCommandTests
 * 功能概述: 验证变换历史和后续子树命令的回放契约。
 * 对外接口: Catch2 用例
 * 依赖关系: 编辑器 ViewModel、Catch2
 * 输入输出: 编辑意图到实体状态、历史数量断言。
 * 异常与错误: 回放或状态不一致时测试失败。
 * 维护说明: 本文件不创建真实窗口，复用编辑器测试 main。
 */
#include "editor/SceneTreeModel.h"
#include "editor/SceneViewModel.h"

#include <QAbstractItemModelTester>
#include <catch2/catch_test_macros.hpp>
using namespace mini3d;
TEST_CASE("Lighting and instance surface edits undo without modifying shared assets",
          "[appearance]") {
    editor::SceneViewModel model;
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    core::SurfaceStyle surface;
    surface.tint = {0.5F, 0.2F, 0.1F};
    surface.useTexture = false;
    REQUIRE(model.setSurface(id, surface));
    REQUIRE(model.scene()->find(id)->surface == surface);
    model.undo();
    REQUIRE(model.scene()->find(id)->surface == core::SurfaceStyle{});
    model.redo();
    auto light = model.scene()->lighting();
    light.direction = {0, -1, 0};
    REQUIRE(model.setLighting(light));
    model.undo();
    REQUIRE(model.scene()->lighting() == core::Lighting{});
    light.direction = {0, 0, 0};
    REQUIRE_FALSE(model.setLighting(light));
    REQUIRE(model.undoStack()->canRedo());
}
TEST_CASE("Transform history replays values and ignores invalid or unchanged edits", "[command]") {
    editor::SceneViewModel model;
    const auto id = model.scene()->find(model.scene()->roots().front())->children.front();
    const auto initial = model.scene()->find(id)->transform;
    REQUIRE(model.setTransform(id, initial));
    REQUIRE(model.undoStack()->count() == 0);
    REQUIRE(model.setTransformComponent(id, 0, 0, 3));
    REQUIRE(model.undoStack()->count() == 1);
    REQUIRE(model.scene()->find(id)->transform.position.x == 3);
    model.undo();
    REQUIRE(model.scene()->find(id)->transform.position == initial.position);
    model.redo();
    REQUIRE(model.scene()->find(id)->transform.position.x == 3);
    REQUIRE_FALSE(model.setTransformComponent(id, 2, 0, 0));
    REQUIRE(model.undoStack()->count() == 1);
    model.undo();
    REQUIRE(model.setTransformComponent(id, 0, 1, 4));
    REQUIRE_FALSE(model.undoStack()->canRedo());
    REQUIRE(model.renameEntity(id, QStringLiteral("renamed")));
    REQUIRE(model.undoStack()->count() == 2);
    model.undo();
    REQUIRE(model.scene()->find(id)->name == "立方体");
}

TEST_CASE("Preview commits once or restores without consuming redo", "[command]") {
    editor::SceneViewModel model;
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    const auto initial = model.scene()->find(id)->transform;
    const auto baseline = model.undoStack()->count();
    model.beginTransformEdit(id);
    auto preview = initial;
    for (int i = 1; i <= 20; ++i) {
        preview.position.x = static_cast<float>(i);
        model.previewTransform(preview);
    }
    REQUIRE(model.undoStack()->count() == baseline);
    REQUIRE(model.scene()->find(id)->transform.position.x == 20);
    model.finishTransformEdit(true);
    REQUIRE(model.undoStack()->count() == baseline + 1);
    model.undo();
    REQUIRE(model.scene()->find(id)->transform.position == initial.position);
    model.beginTransformEdit(id);
    model.previewTransform(preview);
    model.cancelTransformEdit();
    REQUIRE(model.scene()->find(id)->transform.position == initial.position);
    REQUIRE(model.undoStack()->canRedo());
    model.beginTransformEdit(id);
    model.finishTransformEdit(true);
    REQUIRE(model.undoStack()->canRedo());
    model.beginTransformEdit(id);
    model.previewTransform(preview);
    model.selection()->setSelectedEntity(core::kInvalidEntity);
    REQUIRE(model.scene()->find(id)->transform.position == initial.position);
    model.redo();
    REQUIRE(model.scene()->find(id)->transform.position.x == 20);
}

TEST_CASE("Transform duplicate and delete round trip a hierarchy with shared resources",
          "[command]") {
    editor::SceneViewModel model;
    editor::SceneTreeModel tree(model);
    QAbstractItemModelTester tester(&tree, QAbstractItemModelTester::FailureReportingMode::Fatal);
    const auto root = model.importGltf(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/Box.glb"));
    REQUIRE(root != core::kInvalidEntity);
    const auto meshes = model.assets()->meshCount();
    const auto child = model.scene()->find(root)->children.front();
    const auto children = model.scene()->find(root)->children;
    REQUIRE(model.setTransformComponent(root, 0, 0, 2));
    model.duplicateSelected();
    const auto copy = model.selection()->selectedEntity();
    REQUIRE(copy != root);
    REQUIRE(model.assets()->meshCount() == meshes);
    REQUIRE(model.scene()->find(copy)->transform.position.x == 2);
    REQUIRE(model.setTransformComponent(copy, 0, 0, 4));
    REQUIRE(model.scene()->find(root)->transform.position.x == 2);
    model.deleteSelected();
    REQUIRE(model.scene()->find(copy) == nullptr);
    REQUIRE(model.selection()->selectedEntity() == core::kInvalidEntity);
    REQUIRE(model.undoStack()->count() == 5);
    model.undo();
    REQUIRE(model.selection()->selectedEntity() == copy);
    REQUIRE(model.scene()->find(copy)->transform.position.x == 4);
    model.undo();
    REQUIRE(model.scene()->find(copy)->transform.position.x == 2);
    model.undo();
    REQUIRE(model.scene()->find(copy) == nullptr);
    REQUIRE(model.selection()->selectedEntity() == root);
    model.undo();
    REQUIRE(model.scene()->find(root)->transform.position.x == 0);
    for (int i = 0; i < 4; ++i) {
        model.redo();
    }
    REQUIRE(model.scene()->find(copy) == nullptr);
    model.selection()->setSelectedEntity(root);
    model.deleteSelected();
    REQUIRE(model.scene()->find(child) == nullptr);
    model.undo();
    REQUIRE(model.scene()->find(root)->children == children);
    REQUIRE(model.scene()->find(child)->parent == root);
    REQUIRE(model.selection()->selectedEntity() == root);
    REQUIRE(model.assets()->meshCount() == meshes);
    model.selection()->setSelectedEntity(core::kInvalidEntity);
    const auto count = model.undoStack()->count();
    model.duplicateSelected();
    model.deleteSelected();
    REQUIRE(model.undoStack()->count() == count);
}
