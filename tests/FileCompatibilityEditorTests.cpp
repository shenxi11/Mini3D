/*
 * 模块名: FileCompatibilityEditorTests
 * 功能概述: 验证资源子网格往返、坏文件加载隔离和旧版原件保护。
 * 对外接口: Catch2 [file-compatibility-editor]。
 * 依赖关系: SceneViewModel、SceneDocument、Qt 临时文件、Catch2。
 * 输入输出: 临时 glTF/工程文件到文档、选区、历史及保存点断言。
 * 异常与错误: 读写失败必须保留当前编辑状态和原文件字节。
 * 维护说明: 仅写测试临时目录，不创建窗口，不修改用户资源。
 */
#include "assets/SceneDocument.h"
#include "editor/SceneViewModel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>

using namespace mini3d;
namespace {
QByteArray readFileBytes(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
void writeFileBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}
std::string documentState(const editor::SceneViewModel& model) {
    core::SceneDocumentData data;
    data.nodes = model.scene()->nodes();
    data.editableMeshes = model.scene()->editableMeshes();
    data.camera = model.editorCamera();
    data.lighting = model.scene()->lighting();
    data.cursor = model.cursor3D();
    return core::SceneSerializer::encode(data);
}
QString writeTwoMeshGltf(const QTemporaryDir& directory) {
    const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
    writeFileBytes(directory.filePath("triangles.bin"),
                   QByteArray(reinterpret_cast<const char*>(positions.data()),
                              static_cast<qsizetype>(sizeof(positions))));
    const auto path = directory.filePath(QStringLiteral("两个子网格.gltf"));
    writeFileBytes(path, R"({
        "asset": {"version": "2.0"},
        "buffers": [{"uri": "triangles.bin", "byteLength": 36}],
        "bufferViews": [{"buffer": 0, "byteLength": 36}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3,
                       "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}},
                                   {"attributes": {"POSITION": 0}}]}],
        "nodes": [{"name": "Two primitives", "mesh": 0}],
        "scenes": [{"nodes": [0]}],
        "scene": 0
    })");
    return path;
}
} // namespace

TEST_CASE("Rejected file loads preserve editor data selections history and the save point",
          "[file-compatibility-editor]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto sourcePath = writeTwoMeshGltf(directory);
    editor::SceneViewModel model;
    model.newScene();
    REQUIRE(model.importGltf(sourcePath) != 0);
    REQUIRE(model.assets()->meshCount() == 2);
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(cube));
    const core::modeling::MirrorOptions mirror{core::modeling::MirrorAxis::Z, false, false, false,
                                               0.025};
    REQUIRE(model.setMirrorOptions(cube, mirror));
    REQUIRE(model.setCursorPosition({1.25F, -2.5F, 3.75F}));
    model.setCursorVisible(false);
    const auto path = directory.filePath(QStringLiteral("完整工程.m3dscene"));
    REQUIRE(model.saveScene(path));
    const auto savedBytes = readFileBytes(path);
    const auto saved = QJsonDocument::fromJson(savedBytes).object();
    const auto references = saved["assets"].toArray();
    REQUIRE(references.size() == 2);
    REQUIRE(references[1].toObject()["meshIndex"].toInt() == 1);
    REQUIRE(references[1].toObject()["path"].toString() == QStringLiteral("两个子网格.gltf"));
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(reopened.assets()->meshFromSource(sourcePath, 1) != core::kInvalidAsset);
    REQUIRE(documentState(reopened) == documentState(model));
    REQUIRE(reopened.mirrorOptions(cube) == mirror);

    REQUIRE(model.renameEntity(cube, QStringLiteral("待保存名字")));
    REQUIRE(model.renameEntity(cube, QStringLiteral("可重做名字")));
    model.undo();
    model.selection()->setSelectedEntity(cube);
    REQUIRE(model.setEditMode(true));
    model.selectComponent({1, 0}, editor::SelectionOperation::Replace);
    model.selectComponent({2, 0}, editor::SelectionOperation::Add);
    REQUIRE(model.componentSelection().selectedIds().size() == 2);
    const auto before = documentState(model);
    const auto assets = model.assets();
    const auto selection = model.componentSelection();
    const auto cursor = model.cursor3D();
    const auto history = model.undoStack();
    const auto index = history->index();
    const auto count = history->count();
    const auto cleanIndex = history->cleanIndex();
    const auto* undoCommand = history->command(index - 1);
    const auto* redoCommand = history->command(index);
    REQUIRE(model.isModified());
    REQUIRE(history->canRedo());
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    const auto rejectedPath = directory.filePath("rejected.m3dscene");
    for (int failure = 0; failure < 7; ++failure) {
        CAPTURE(failure);
        auto invalid = saved;
        auto meshes = invalid["editableMeshes"].toArray();
        auto mesh = meshes[0].toObject();
        auto resources = invalid["assets"].toArray();
        switch (failure) {
            case 1: {
                auto resource = resources[0].toObject();
                resource["path"] = "missing.gltf";
                resources[0] = resource;
                invalid["assets"] = resources;
                break;
            }
            case 2:
                mesh["vertices"] = QJsonArray{};
                break;
            case 3: {
                auto modifier = mesh["modifier"].toObject();
                modifier["threshold"] = -1;
                mesh["modifier"] = modifier;
                break;
            }
            case 4: {
                auto state = invalid["editorState"].toObject();
                state["upAxis"] = "Z";
                invalid["editorState"] = state;
                break;
            }
            case 5: {
                auto resource = resources[1].toObject();
                resource["meshIndex"] = 99;
                resources[1] = resource;
                invalid["assets"] = resources;
                break;
            }
            case 6: {
                auto state = invalid["editorState"].toObject();
                auto badCursor = state["cursor3D"].toObject();
                badCursor["position"] = QJsonArray{1, 2};
                state["cursor3D"] = badCursor;
                invalid["editorState"] = state;
                break;
            }
        }
        meshes[0] = mesh;
        invalid["editableMeshes"] = meshes;
        writeFileBytes(rejectedPath,
                       failure == 0 ? QByteArray("{broken") : QJsonDocument(invalid).toJson());
        REQUIRE_FALSE(model.openScene(rejectedPath));
        REQUIRE(failures.count() == failure + 1);
        REQUIRE(documentState(model) == before);
        REQUIRE(model.assets() == assets);
        REQUIRE(model.assets()->meshCount() == 2);
        REQUIRE(model.selection()->selectedEntity() == cube);
        REQUIRE(model.isEditMode());
        REQUIRE(model.editedEntity() == cube);
        REQUIRE(model.componentSelection() == selection);
        REQUIRE(model.cursor3D() == cursor);
        REQUIRE(history->index() == index);
        REQUIRE(history->count() == count);
        REQUIRE(history->cleanIndex() == cleanIndex);
        REQUIRE(history->command(index - 1) == undoCommand);
        REQUIRE(history->command(index) == redoCommand);
        REQUIRE(model.filePath() == path);
        REQUIRE_FALSE(model.requiresSaveAs());
        REQUIRE(model.isModified());
    }
    model.redo();
    REQUIRE(model.scene()->find(cube)->name == "可重做名字");
    model.undo();
    model.undo();
    REQUIRE_FALSE(model.isModified());
    REQUIRE(readFileBytes(path) == savedBytes);
}

TEST_CASE("Legacy aliases and failed save as preserve original bytes and the upgrade guard",
          "[file-compatibility-editor]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(QDir(directory.path()).mkdir("write-target-directory"));
    for (int version : {1, 2, 3}) {
        CAPTURE(version);
        editor::SceneViewModel model;
        model.newScene();
        const auto cube = model.createEntity(core::PrimitiveKind::Cube);
        if (version >= 2) {
            REQUIRE(model.createCamera() != 0);
        }
        if (version == 3) {
            REQUIRE(model.createDirectionalLight() != 0);
        }
        auto legacy =
            QJsonDocument::fromJson(QByteArray::fromStdString(documentState(model))).object();
        legacy["version"] = version;
        legacy.remove("animation");
        if (version < 3)
            legacy.remove("editableMeshes");
        legacy.remove("editorState");
        const auto original = directory.filePath(QString("legacy%1.m3dscene").arg(version));
        const auto bytes = QJsonDocument(legacy).toJson();
        writeFileBytes(original, bytes);
        REQUIRE(model.openScene(original));
        REQUIRE(model.requiresSaveAs());
        REQUIRE(model.makeEditable(cube));
        REQUIRE(model.setCursorPosition({2, -1, 4}));
        const auto before = documentState(model);
        const auto index = model.undoStack()->index();
        const auto cleanIndex = model.undoStack()->cleanIndex();
        const auto alias = directory.filePath(QString("./legacy%1.m3dscene").arg(version));
        REQUIRE_FALSE(model.saveScene(alias));
        REQUIRE_FALSE(model.saveScene(directory.filePath("write-target-directory")));
        REQUIRE(model.requiresSaveAs());
        REQUIRE(model.filePath() == original);
        REQUIRE(documentState(model) == before);
        REQUIRE(model.undoStack()->index() == index);
        REQUIRE(model.undoStack()->cleanIndex() == cleanIndex);
        REQUIRE(model.isModified());
        REQUIRE(readFileBytes(original) == bytes);
        const auto upgraded = directory.filePath(QString("upgraded%1.m3dscene").arg(version));
        REQUIRE(model.saveScene(upgraded));
        REQUIRE_FALSE(model.requiresSaveAs());
        REQUIRE_FALSE(model.isModified());
        REQUIRE(readFileBytes(original) == bytes);
        const auto current = QJsonDocument::fromJson(readFileBytes(upgraded)).object();
        REQUIRE(current["version"].toInt() == 4);
        REQUIRE(current["editorState"].toObject()["upAxis"].toString() == "Y");
        REQUIRE(model.openScene(upgraded));
        REQUIRE(model.cursor3D().position == glm::vec3(2, -1, 4));
        REQUIRE(model.scene()->find(cube)->editableMesh != 0);
        REQUIRE(model.scene()->nodes().size() == static_cast<std::size_t>(version));
    }
}

TEST_CASE("Invalid save inputs do not overwrite an existing file", "[file-compatibility-editor]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("existing.m3dscene");
    const QByteArray original("original bytes");
    writeFileBytes(path, original);
    core::Scene scene;
    assets::AssetManager assetManager;
    core::Cursor3D cursor;
    cursor.position.x = std::numeric_limits<float>::infinity();
    QString error;
    REQUIRE_FALSE(assets::SceneDocument::write(path, scene, assetManager, {}, error, cursor));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(readFileBytes(path) == original);
    core::CameraState camera;
    camera.focusRadius = -1;
    REQUIRE_FALSE(assets::SceneDocument::write(path, scene, assetManager, camera, error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(readFileBytes(path) == original);
}

TEST_CASE("Rejected open preserves an active object transform preview and transaction",
          "[file-compatibility-editor][file-compatibility-preview]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    const auto path = directory.filePath("before.m3dscene");
    REQUIRE(model.saveScene(path));
    const auto before = model.scene()->find(cube)->transform;
    auto preview = before;
    preview.position = {2, 3, -1};
    const auto count = model.undoStack()->count();
    const auto cleanIndex = model.undoStack()->cleanIndex();
    model.beginTransformEdit(cube);
    model.previewTransform(preview);
    const auto badPath = directory.filePath("broken.m3dscene");
    writeFileBytes(badPath, "{broken");
    QSignalSpy finishes(&model, &editor::SceneViewModel::transformEditFinished);
    REQUIRE_FALSE(model.openScene(badPath));
    REQUIRE(model.scene()->find(cube)->transform.localMatrix() == preview.localMatrix());
    REQUIRE(finishes.isEmpty());
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE(model.undoStack()->cleanIndex() == cleanIndex);
    REQUIRE(model.filePath() == path);
    model.finishTransformEdit(true);
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE(model.scene()->find(cube)->transform.localMatrix() == preview.localMatrix());
    model.undo();
    REQUIRE(model.scene()->find(cube)->transform.localMatrix() == before.localMatrix());
}

TEST_CASE("Rejected open preserves an active component candidate selection and transaction",
          "[file-compatibility-editor][file-compatibility-preview]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    REQUIRE(model.editedEntity() == cube);
    model.selectAllComponents();
    const auto before = documentState(model);
    const auto selection = model.componentSelection();
    const auto count = model.undoStack()->count();
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1), glm::dvec3(2, 0, 0))));
    const auto preview = model.componentPreview();
    REQUIRE(preview);
    const auto badPath = directory.filePath("broken.m3dscene");
    writeFileBytes(badPath, "{broken");
    REQUIRE_FALSE(model.openScene(badPath));
    REQUIRE(model.hasComponentTransform());
    REQUIRE(model.componentPreview() == preview);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE(documentState(model) == before);
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE(documentState(model) != before);
    model.undo();
    REQUIRE(documentState(model) == before);
    REQUIRE(model.componentSelection() == selection);
}
