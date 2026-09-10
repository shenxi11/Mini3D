/*
 * 模块名: SceneDocumentTests
 * 功能概述: 验证真实文件保存重开、相对路径迁移、资源失败隔离和历史保存点。
 * 对外接口: Catch2 用例
 * 依赖关系: SceneViewModel、SceneDocument、QTemporaryDir
 * 输入输出: 临时文档/资源到场景状态和脏标记断言。
 * 异常与错误: 失败必须保留旧场景、资源和路径。
 * 维护说明: 只写临时目录；不覆盖用户文件。
 */
#include "assets/SceneDocument.h"
#include "editor/SceneTreeModel.h"
#include "editor/SceneViewModel.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
using namespace mini3d;
TEST_CASE("Document round trip remaps shared meshes and survives moving its directory",
          "[document]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(QDir(directory.path()).mkdir("project"));
    auto project = directory.path() + "/project";
    REQUIRE(QFile::copy(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/BoxTextured.glb"),
                        project + "/model.glb"));
    editor::SceneViewModel model;
    model.newScene();
    const auto root = model.importGltf(project + "/model.glb");
    REQUIRE(root != 0);
    const auto nodes = model.scene()->nodes();
    core::EntityId meshNode = 0;
    for (const auto& node : nodes) {
        if (node.meshRenderer) {
            meshNode = node.id;
            break;
        }
    }
    REQUIRE(meshNode != 0);
    auto surface = model.scene()->find(meshNode)->surface;
    surface.tint = {0.2F, 0.4F, 0.6F};
    REQUIRE(model.setSurface(meshNode, surface));
    REQUIRE(model.setTransformComponent(root, 2, 0, -2));
    model.duplicateSelected();
    const auto duplicate = model.selection()->selectedEntity();
    auto light = model.scene()->lighting();
    light.ambient = 0.5F;
    REQUIRE(model.setLighting(light));
    const auto camera = model.editorCamera();
    REQUIRE(model.saveScene(project + "/scene.m3dscene"));
    REQUIRE_FALSE(model.isModified());
    const auto count = model.scene()->nodes().size();
    REQUIRE(QDir(directory.path()).rename("project", "moved"));
    const auto moved = directory.path() + "/moved/scene.m3dscene";
    editor::SceneViewModel loaded;
    REQUIRE(loaded.openScene(moved));
    REQUIRE(loaded.scene()->nodes().size() == count);
    REQUIRE(loaded.scene()->find(duplicate) != nullptr);
    REQUIRE(loaded.scene()->find(root)->transform.scale.x == -2);
    REQUIRE(loaded.scene()->find(meshNode)->surface == surface);
    REQUIRE(loaded.scene()->lighting() == light);
    REQUIRE(loaded.editorCamera() == camera);
    REQUIRE(loaded.assets()->meshCount() == 1);
    REQUIRE_FALSE(loaded.isModified());
    REQUIRE(loaded.undoStack()->count() == 0);
    const auto oldAssets = loaded.assets();
    const auto oldPath = loaded.filePath();
    REQUIRE(QFile::rename(directory.path() + "/moved/model.glb",
                          directory.path() + "/moved/missing.glb"));
    REQUIRE_FALSE(loaded.openScene(moved));
    REQUIRE(loaded.assets() == oldAssets);
    REQUIRE(loaded.filePath() == oldPath);
    REQUIRE(loaded.scene()->nodes().size() == count);
    REQUIRE(loaded.setTransformComponent(root, 0, 0, 3));
    REQUIRE_FALSE(loaded.saveScene(directory.path() + "/nonexistent/scene.m3dscene"));
    REQUIRE(loaded.isModified());
    REQUIRE(loaded.filePath() == oldPath);
}
TEST_CASE("Creation rename visibility and reparent undo return to the saved point", "[document]") {
    QTemporaryDir directory;
    editor::SceneViewModel model;
    editor::SceneTreeModel tree(model);
    QAbstractItemModelTester tester(&tree, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.newScene();
    REQUIRE_FALSE(model.isModified());
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto a = model.createEntity(core::PrimitiveKind::Cube);
    const auto b = model.createEntity(core::PrimitiveKind::Sphere);
    REQUIRE(model.setParent(a, parent));
    REQUIRE(model.setParent(b, parent));
    const auto siblings = model.scene()->find(parent)->children;
    REQUIRE(model.saveScene(directory.filePath("saved.m3dscene")));
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.setParent(a, 0));
    REQUIRE(model.renameEntity(a, QStringLiteral("Renamed")));
    REQUIRE(model.setVisible(a, false));
    REQUIRE(model.isModified());
    model.undo();
    model.undo();
    model.undo();
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.scene()->find(parent)->children == siblings);
    REQUIRE(model.scene()->find(a)->name == "立方体");
    model.undo();
    REQUIRE(model.isModified());
    model.redo();
    REQUIRE_FALSE(model.isModified());
    model.newScene();
    const auto created = model.createEntity(core::PrimitiveKind::Cube);
    model.undo();
    REQUIRE(model.scene()->find(created) == nullptr);
    REQUIRE_FALSE(model.isModified());
    model.redo();
    REQUIRE(model.scene()->find(created) != nullptr);
    const auto imported = model.importGltf(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/Box.glb"));
    REQUIRE(imported != 0);
    model.undo();
    REQUIRE(model.scene()->find(imported) == nullptr);
    model.redo();
    REQUIRE(model.scene()->find(imported) != nullptr);
}

TEST_CASE("One hundred cubes survive delete undo full history replay and document reload",
          "[document][release-regression]") {
    QTemporaryDir directory;
    editor::SceneViewModel model;
    editor::SceneTreeModel tree(model);
    QAbstractItemModelTester tester(&tree, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.newScene();
    const auto group = model.createEntity(core::PrimitiveKind::Empty);
    for (int i = 0; i < 100; ++i) {
        const auto id = model.createEntity(core::PrimitiveKind::Cube);
        REQUIRE(id != 0);
        REQUIRE(model.setParent(id, group));
        core::Transform transform;
        transform.position = {static_cast<float>(i % 10), 0, static_cast<float>(i / 10)};
        REQUIRE(model.setTransform(id, transform));
    }
    const auto expected = model.scene()->nodes();
    REQUIRE(expected.size() == 101);
    model.selection()->setSelectedEntity(group);
    model.deleteSelected();
    REQUIRE(model.scene()->roots().empty());
    model.undo();
    REQUIRE(model.scene()->nodes().size() == 101);
    while (model.undoStack()->canUndo()) {
        model.undo();
    }
    REQUIRE(model.scene()->roots().empty());
    REQUIRE_FALSE(model.isModified());
    while (model.undoStack()->canRedo()) {
        model.redo();
    }
    // 最后一条历史是整组删除，撤销它以核对恢复的稳定 ID 与局部值。
    REQUIRE(model.scene()->roots().empty());
    model.undo();
    const auto path = directory.filePath(QStringLiteral("中文场景.m3dscene"));
    REQUIRE(model.saveScene(path));
    REQUIRE(model.openScene(path));
    REQUIRE(model.undoStack()->count() == 0);
    const auto actual = model.scene()->nodes();
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        REQUIRE(actual[i].id == expected[i].id);
        REQUIRE(actual[i].parent == expected[i].parent);
        REQUIRE(actual[i].transform.position == expected[i].transform.position);
    }
}

TEST_CASE("Device edits preserve stable IDs through history and scene files", "[camera-light]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    editor::SceneViewModel model;
    editor::SceneTreeModel tree(model);
    QAbstractItemModelTester tester(&tree, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto camera = model.createCamera();
    const auto light = model.createDirectionalLight();
    REQUIRE(model.undoStack()->count() == 3);
    REQUIRE(model.setCamera(camera, {65, 0.2F, 250}));
    REQUIRE(model.setLight(light, {{0.3F, 0.6F, 1}, 2}));
    REQUIRE(model.setParent(camera, parent));
    REQUIRE(model.setParent(light, parent));
    REQUIRE(model.setTransformComponent(parent, 1, 1, 30));
    REQUIRE(model.renameEntity(camera, QStringLiteral("主相机")));
    const auto count = model.undoStack()->count();
    REQUIRE_FALSE(model.setCamera(camera, {45, 10, 1}));
    REQUIRE_FALSE(model.setLight(light, {{1, 1, 1}, 11}));
    REQUIRE(model.undoStack()->count() == count);
    model.selection()->setSelectedEntity(parent);
    model.duplicateSelected();
    const auto copy = model.selection()->selectedEntity();
    const auto children = model.scene()->find(copy)->children;
    REQUIRE(model.scene()->find(children[0])->camera == model.scene()->find(camera)->camera);
    REQUIRE(model.scene()->find(children[1])->light == model.scene()->find(light)->light);
    model.deleteSelected();
    model.undo();
    REQUIRE(model.scene()->find(copy)->children == children);
    model.selection()->setSelectedEntity(parent);
    REQUIRE(model.setPreviewCamera(camera));
    model.deleteSelected();
    REQUIRE(model.previewCamera() == 0);
    model.undo();
    REQUIRE(model.scene()->find(camera)->camera == core::CameraComponent{65, 0.2F, 250});
    const auto expected = model.scene()->nodes();
    while (model.undoStack()->canUndo()) {
        model.undo();
    }
    REQUIRE(model.scene()->nodes().empty());
    while (model.undoStack()->canRedo()) {
        model.redo();
    }
    model.undo(); // 撤销最后的父组删除，回到 expected。
    REQUIRE(model.scene()->nodes().size() == expected.size());
    REQUIRE(model.scene()->find(camera)->name == "主相机");
    REQUIRE(model.scene()->find(camera)->parent == parent);
    REQUIRE(model.scene()->find(light)->light == core::LightComponent{{0.3F, 0.6F, 1}, 2});
    const auto path = directory.filePath(QStringLiteral("设备场景.m3dscene"));
    REQUIRE(model.saveScene(path));
    const auto observer = model.editorCamera();
    REQUIRE(model.setPreviewCamera(camera));
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.editorCamera() == observer);
    REQUIRE(model.openScene(path));
    REQUIRE(model.previewCamera() == 0);
    REQUIRE(model.scene()->find(camera)->camera == core::CameraComponent{65, 0.2F, 250});
    REQUIRE(model.scene()->find(light)->light == core::LightComponent{{0.3F, 0.6F, 1}, 2});
    REQUIRE(model.scene()->find(camera)->parent == parent);
    REQUIRE(model.undoStack()->count() == 0);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.setPreviewCamera(camera));
    REQUIRE(model.setVisible(parent, false));
    REQUIRE(model.previewCamera() == 0);
    REQUIRE_FALSE(model.setPreviewCamera(camera));
    model.undo();
    REQUIRE(model.setPreviewCamera(camera));
    core::SceneDocumentData invalid;
    invalid.nodes = model.scene()->nodes();
    for (auto& node : invalid.nodes) {
        if (node.camera) {
            node.camera->nearPlane = -1;
        }
    }
    QFile broken(directory.filePath("invalid-camera.m3dscene"));
    REQUIRE(broken.open(QIODevice::WriteOnly));
    const auto text = core::SceneSerializer::encode(invalid);
    REQUIRE(broken.write(text.data(), static_cast<qint64>(text.size())) ==
            static_cast<qint64>(text.size()));
    broken.close();
    const auto historyIndex = model.undoStack()->index();
    REQUIRE_FALSE(model.openScene(broken.fileName()));
    REQUIRE(model.previewCamera() == camera);
    REQUIRE(model.filePath() == path);
    REQUIRE(model.undoStack()->index() == historyIndex);
    REQUIRE(model.scene()->find(camera)->camera->nearPlane == 0.2F);
    model.newScene();
    REQUIRE(model.previewCamera() == 0);
    REQUIRE(model.scene()->nodes().empty());
}

TEST_CASE("Existing version 1 showcase opens and upgrades without extra device nodes",
          "[camera-light]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(QDir(directory.path()).mkdir("samples"));
    REQUIRE(QDir(directory.path()).mkdir("scenes"));
    REQUIRE(QFile::copy(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/BoxTextured.glb"),
                        directory.filePath("samples/BoxTextured.glb")));
    REQUIRE(QFile::copy(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/../scenes/showcase.m3dscene"),
                        directory.filePath("scenes/showcase.m3dscene")));
    editor::SceneViewModel model;
    REQUIRE(model.openScene(directory.filePath("scenes/showcase.m3dscene")));
    const auto expected = model.scene()->nodes();
    const auto light = model.scene()->effectiveLighting();
    for (const auto& node : expected) {
        REQUIRE_FALSE(node.camera);
        REQUIRE_FALSE(node.light);
    }
    const auto path = directory.filePath("upgraded.m3dscene");
    REQUIRE(model.saveScene(path));
    REQUIRE(model.openScene(path));
    REQUIRE(model.scene()->nodes().size() == expected.size());
    REQUIRE(model.scene()->effectiveLighting() == light);
}

TEST_CASE("Empty Unicode scene saves and corrupt input preserves document and history",
          "[document][release-regression]") {
    QTemporaryDir directory;
    editor::SceneViewModel model;
    model.newScene();
    const auto path = directory.filePath(QStringLiteral("空场景.m3dscene"));
    REQUIRE(model.saveScene(path));
    REQUIRE(model.openScene(path));
    REQUIRE(model.scene()->roots().empty());
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    const auto historyCount = model.undoStack()->count();
    QFile broken(directory.filePath("broken.m3dscene"));
    REQUIRE(broken.open(QIODevice::WriteOnly));
    REQUIRE(broken.write("{broken") == 7);
    broken.close();
    REQUIRE_FALSE(model.openScene(broken.fileName()));
    REQUIRE(model.filePath() == path);
    REQUIRE(model.scene()->find(id) != nullptr);
    REQUIRE(model.selection()->selectedEntity() == id);
    REQUIRE(model.undoStack()->count() == historyCount);
    REQUIRE(model.isModified());
}
