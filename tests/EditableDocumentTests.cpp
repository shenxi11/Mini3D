/*
 * 模块名: EditableDocumentTests
 * 功能概述: 验证真实可编辑网格的历史、文件和 GPU 刷新闭环。
 * 对外接口: Catch2 测试；依赖关系: SceneViewModel、Qt Test、真实 OpenGL。
 * 输入输出: 临时文件及编辑意图到网格、保存点、帧缓冲断言。
 * 异常与错误: 失败必须保持原文档/历史；维护说明: 不覆盖用户模型。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/RayCaster.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
QByteArray readBytes(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}
core::modeling::EditableMesh sourceMesh(const editor::SceneViewModel& model, core::EntityId id) {
    return model.scene()->editableMesh(model.scene()->find(id)->editableMesh)->content->source;
}
} // namespace

TEST_CASE("Editable document commits one snapshot and preserves save undo redo and copies",
          "[editable-document]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    editor::SceneViewModel model;
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(id));
    const auto meshId = model.scene()->find(id)->editableMesh;
    const auto cube = sourceMesh(model, id);
    REQUIRE(model.undoStack()->count() == 2);
    REQUIRE(model.makeEditable(id));
    REQUIRE(model.undoStack()->count() == 2);
    model.undo();
    REQUIRE(model.scene()->find(id)->primitive == core::PrimitiveKind::Cube);
    model.redo();
    REQUIRE(model.scene()->find(id)->editableMesh == meshId);
    const auto path = directory.filePath(QStringLiteral("编辑工程.m3dscene"));
    REQUIRE(model.saveScene(path));
    REQUIRE_FALSE(model.isModified());
    auto changed = cube;
    changed.vertices[6].position.y = 1.4F;
    REQUIRE(model.replaceEditableMesh(id, changed));
    REQUIRE(model.isModified());
    const auto revision = model.scene()->editableMesh(meshId)->geometryRevision;
    model.undo();
    REQUIRE(sourceMesh(model, id) == cube);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.scene()->editableMesh(meshId)->geometryRevision > revision);
    model.redo();
    REQUIRE(sourceMesh(model, id) == changed);
    const auto index = model.undoStack()->index();
    auto broken = changed;
    broken.faces[0].corners[0].vertex = 123456;
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    REQUIRE_FALSE(model.replaceEditableMesh(id, broken));
    REQUIRE(failures.count() == 1);
    REQUIRE(sourceMesh(model, id) == changed);
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(model.replaceEditableMesh(id, changed));
    REQUIRE(model.undoStack()->index() == index);
    model.duplicateSelected();
    const auto copy = model.selection()->selectedEntity();
    REQUIRE(model.scene()->find(copy)->editableMesh != meshId);
    REQUIRE(sourceMesh(model, copy) == changed);
    REQUIRE(model.replaceEditableMesh(copy, cube));
    REQUIRE(sourceMesh(model, id) == changed);
    model.deleteSelected();
    REQUIRE_FALSE(model.scene()->find(copy));
    model.undo();
    REQUIRE(sourceMesh(model, copy) == cube);
    REQUIRE(model.saveScene(path));
    const auto bytes = readBytes(path);
    REQUIRE(bytes.contains("\"version\": 3"));
    REQUIRE(model.openScene(path));
    REQUIRE_FALSE(model.requiresSaveAs());
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.undoStack()->count() == 0);
    REQUIRE(sourceMesh(model, id) == changed);
    REQUIRE(sourceMesh(model, copy) == cube);
    REQUIRE(model.scene()->find(id)->editableMesh == meshId);
    REQUIRE(model.replaceEditableMesh(id, cube));
    const auto oldAssets = model.assets();
    const auto oldIndex = model.undoStack()->index();
    const auto oldSelection = model.selection()->selectedEntity();
    auto invalid = QJsonDocument::fromJson(bytes).object();
    invalid["editableMeshes"] = QJsonArray{};
    const auto corrupt = directory.filePath("broken.m3dscene");
    writeBytes(corrupt, QJsonDocument(invalid).toJson());
    REQUIRE_FALSE(model.openScene(corrupt));
    REQUIRE(model.assets() == oldAssets);
    REQUIRE(model.filePath() == path);
    REQUIRE(model.undoStack()->index() == oldIndex);
    REQUIRE(model.selection()->selectedEntity() == oldSelection);
    REQUIRE(sourceMesh(model, id) == cube);
    REQUIRE(model.isModified());
    const auto camera = model.createCamera();
    const auto light = model.createDirectionalLight();
    const auto sphere = model.createEntity(core::PrimitiveKind::Sphere);
    const auto history = model.undoStack()->count();
    REQUIRE_FALSE(model.makeEditable(camera));
    REQUIRE_FALSE(model.makeEditable(light));
    REQUIRE_FALSE(model.makeEditable(sphere));
    REQUIRE(model.undoStack()->count() == history);
}

TEST_CASE("Legacy documents require a different first save path and preserve original bytes",
          "[editable-document]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    for (int version : {1, 2}) {
        editor::SceneViewModel model;
        model.newScene();
        const auto id = model.createEntity(core::PrimitiveKind::Cube);
        core::SceneDocumentData data;
        data.nodes = model.scene()->nodes();
        auto legacy =
            QJsonDocument::fromJson(QByteArray::fromStdString(core::SceneSerializer::encode(data)))
                .object();
        legacy["version"] = version;
        legacy.remove("editableMeshes");
        const auto bytes = QJsonDocument(legacy).toJson();
        const auto oldPath = directory.filePath(QString("old%1.m3dscene").arg(version));
        writeBytes(oldPath, bytes);
        REQUIRE(model.openScene(oldPath));
        REQUIRE(model.requiresSaveAs());
        REQUIRE_FALSE(model.isModified());
        REQUIRE(model.makeEditable(id));
        REQUIRE_FALSE(model.saveScene(oldPath));
        REQUIRE(model.isModified());
        REQUIRE(model.requiresSaveAs());
        REQUIRE(model.filePath() == oldPath);
        REQUIRE(readBytes(oldPath) == bytes);
        REQUIRE_FALSE(model.saveScene(directory.filePath("missing/new.m3dscene")));
        REQUIRE(model.requiresSaveAs());
        const auto newPath = directory.filePath(QString("new%1.m3dscene").arg(version));
        REQUIRE(model.saveScene(newPath));
        REQUIRE_FALSE(model.requiresSaveAs());
        REQUIRE_FALSE(model.isModified());
        REQUIRE(readBytes(oldPath) == bytes);
        REQUIRE(model.openScene(newPath));
        REQUIRE(sourceMesh(model, id) == core::modeling::createEditableCube());
        REQUIRE(model.saveScene(newPath));
    }
}

TEST_CASE("Editable geometry refreshes actual GPU output bounds and cache after undo and reopen",
          "[editable-document][editable-gpu]") {
    QTemporaryDir directory;
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    // 几何一致性比较排除坐标轴的深度栅格舍入；覆盖层另有独立交互测试。
    viewport->setOverlayVisible(false);
    model->newScene();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    model->selection()->setSelectedEntity(0);
    const auto primitiveFrame = viewport->grabFramebuffer();
    REQUIRE_FALSE(primitiveFrame.isNull());
    auto* context = viewport->context();
    REQUIRE(model->makeEditable(id));
    REQUIRE(viewport->grabFramebuffer() == primitiveFrame);
    auto mesh = sourceMesh(*model, id);
    mesh.vertices[6].position.y = 1.4F;
    REQUIRE(model->replaceEditableMesh(id, mesh));
    const auto changedFrame = viewport->grabFramebuffer();
    REQUIRE(changedFrame != primitiveFrame);
    const auto bounds = renderer_gl::RayCaster::worldBounds(*model->scene(), *model->assets(), id);
    REQUIRE(bounds.maximum.y == 1.4F);
    REQUIRE(renderer_gl::RayCaster::pick(*model->scene(), *model->assets(),
                                         {{0, 1, 5}, {0, 0, -1}}) == id);
    model->undo();
    REQUIRE(viewport->grabFramebuffer() == primitiveFrame);
    model->redo();
    REQUIRE(viewport->grabFramebuffer() == changedFrame);
    const auto path = directory.filePath("editable-gpu.m3dscene");
    REQUIRE(model->saveScene(path));
    const auto sceneCapture = qEnvironmentVariable("MINI3D_TEST_EDITABLE_SCENE");
    if (!sceneCapture.isEmpty()) {
        writeBytes(sceneCapture, readBytes(path));
    }
    REQUIRE(model->openScene(path));
    REQUIRE(viewport->grabFramebuffer() == changedFrame);
    REQUIRE(viewport->context() == context);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_EDITABLE_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture));
    }
    REQUIRE(window.close());
}

TEST_CASE("Save action offers a new filename for legacy projects",
          "[editable-document][editable-gpu]") {
    QTemporaryDir directory;
    core::Scene scene;
    scene.createEntity("旧立方体", 0, core::PrimitiveKind::Cube);
    core::SceneDocumentData data;
    data.nodes = scene.nodes();
    auto legacy =
        QJsonDocument::fromJson(QByteArray::fromStdString(core::SceneSerializer::encode(data)))
            .object();
    legacy["version"] = 2;
    legacy.remove("editableMeshes");
    const auto original = directory.filePath("legacy.m3dscene");
    const auto bytes = QJsonDocument(legacy).toJson();
    writeBytes(original, bytes);
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    REQUIRE(model->openScene(original));
    QString proposed;
    QTimer::singleShot(100, &window, [&] {
        auto* dialog = window.findChild<QFileDialog*>();
        if (dialog) {
            proposed = dialog->selectedFiles().value(0);
            dialog->reject();
        }
    });
    window.findChild<QAction*>(QStringLiteral("SaveScene"))->trigger();
    REQUIRE(proposed.endsWith("legacy-v3.m3dscene"));
    REQUIRE(model->requiresSaveAs());
    REQUIRE(readBytes(original) == bytes);
    REQUIRE(window.close());
}
