/*
 * 模块名: ObjExportEditorTests
 * 功能概述: 验证所选源/求值 OBJ、静态资源、失败保护和真实文件菜单。
 * 对外接口: Catch2 [obj-export-editor]。
 * 依赖关系: SceneViewModel、MainWindow、Qt临时目录/文件对话框。
 * 输入输出: 导出意图到真实文件字节与文档/选择/历史不变断言。
 * 异常与错误: 预览中或无几何时拒绝，失败不覆盖原目标、不改变工程。
 * 维护说明: 仅写临时目录；文本检查不代表外部 Blender 已实测。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
QByteArray objBytes(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
int rows(const QByteArray& text, const QByteArray& prefix) {
    int count = 0;
    for (const auto& line : text.split('\n'))
        count += line.startsWith(prefix);
    return count;
}
std::string objDocumentState(const editor::SceneViewModel& model) {
    core::SceneDocumentData data;
    data.nodes = model.scene()->nodes();
    data.editableMeshes = model.scene()->editableMeshes();
    data.camera = model.editorCamera();
    data.cursor = model.cursor3D();
    data.lighting = model.scene()->lighting();
    return core::SceneSerializer::encode(data);
}
} // namespace

TEST_CASE("Selected OBJ exports source or evaluated polygons and preserves saved editor state",
          "[obj-export-editor]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    core::Transform transform;
    transform.position = {3, 0, 0};
    REQUIRE(model.setTransform(parent, transform));
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(cube, parent));
    REQUIRE(model.makeEditable(cube));
    REQUIRE(model.setMirrorOptions(cube, core::modeling::MirrorOptions{}));
    REQUIRE(model.saveScene(directory.filePath("scene.m3dscene")));
    REQUIRE(model.setEditMode(true));
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    const auto before = objDocumentState(model);
    const auto source = model.scene()->editableMesh(model.scene()->find(cube)->editableMesh);
    const auto selection = model.componentSelection();
    const auto count = model.undoStack()->count();
    const auto clean = model.undoStack()->cleanIndex();
    const auto path = model.filePath();
    const auto assets = model.assets();
    QSignalSpy completions(&model, &editor::SceneViewModel::operationCompleted);
    const auto sourcePath = directory.filePath(QStringLiteral("源.obj"));
    const auto evaluatedPath = directory.filePath(QStringLiteral("求值.obj"));
    REQUIRE(model.exportObj(sourcePath, false));
    REQUIRE(model.exportObj(evaluatedPath, true));
    const auto sourceText = objBytes(sourcePath);
    const auto evaluatedText = objBytes(evaluatedPath);
    REQUIRE(rows(sourceText, "v ") == 8);
    REQUIRE(rows(sourceText, "f ") == 6);
    REQUIRE(rows(evaluatedText, "v ") == 16);
    REQUIRE(rows(evaluatedText, "f ") == 12);
    REQUIRE(sourceText.contains("v 2.5 "));
    REQUIRE(sourceText.contains("right-handed, Y-up"));
    REQUIRE(objDocumentState(model) == before);
    REQUIRE(model.scene()->editableMesh(model.scene()->find(cube)->editableMesh) == source);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE(model.assets() == assets);
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE(model.undoStack()->cleanIndex() == clean);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.filePath() == path);
    REQUIRE(completions.count() == 2);
}

TEST_CASE("OBJ exports actual static primitive and imported triangles without making them editable",
          "[obj-export-editor]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    const auto path = directory.filePath("static.obj");
    REQUIRE(model.exportObj(path, true));
    REQUIRE(rows(objBytes(path), "v ") == 24);
    REQUIRE(rows(objBytes(path), "f ") == 12);
    REQUIRE(model.scene()->find(cube)->editableMesh == 0);
    REQUIRE(model.importGltf(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/BoxTextured.glb")) != 0);
    core::EntityId imported = 0;
    for (const auto& node : model.scene()->nodes()) {
        if (node.meshRenderer) {
            imported = node.id;
            break;
        }
    }
    REQUIRE(imported != 0);
    model.selection()->setSelectedEntity(imported);
    const auto& data =
        model.assets()->mesh(model.scene()->find(imported)->meshRenderer->mesh)->data;
    REQUIRE(model.exportObj(path, false));
    REQUIRE(rows(objBytes(path), "v ") == static_cast<int>(data.vertices.size()));
    REQUIRE(rows(objBytes(path), "f ") == static_cast<int>(data.indices.size() / 3));
    REQUIRE(model.scene()->find(imported)->editableMesh == 0);
}

TEST_CASE("Rejected or failed OBJ writes preserve files active previews and document history",
          "[obj-export-editor]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("original.obj");
    QFile original(path);
    REQUIRE(original.open(QIODevice::WriteOnly));
    REQUIRE(original.write("original bytes") == 14);
    original.close();
    editor::SceneViewModel model;
    model.newScene();
    model.createCamera();
    REQUIRE_FALSE(model.objExportDisabledReason().isEmpty());
    REQUIRE_FALSE(model.exportObj(path, true));
    REQUIRE(objBytes(path) == "original bytes");
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    const auto count = model.undoStack()->count();
    auto preview = model.scene()->find(cube)->transform;
    preview.position.x = 2;
    model.beginTransformEdit(cube);
    model.previewTransform(preview);
    REQUIRE_FALSE(model.exportObj(path, false));
    REQUIRE(model.scene()->find(cube)->transform.position == preview.position);
    model.cancelTransformEdit();
    REQUIRE(model.setEditMode(true));
    model.selectAllComponents();
    REQUIRE(model.beginComponentTransform());
    const auto candidate = model.componentPreview();
    REQUIRE_FALSE(model.exportObj(path, true));
    REQUIRE(model.hasComponentTransform());
    REQUIRE(model.componentPreview() == candidate);
    model.cancelTransformEdit();
    const auto before = objDocumentState(model);
    REQUIRE(QDir(directory.path()).mkdir("not-a-file"));
    REQUIRE_FALSE(model.exportObj(directory.filePath("not-a-file"), true));
    REQUIRE(objDocumentState(model) == before);
    // 进入Edit自动转换Cube形成一条历史，导出不增加任何历史。
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE(objBytes(path) == "original bytes");
}

TEST_CASE("Both OBJ menu choices use save dialogs with overwrite confirmation and safe cancel",
          "[obj-export-editor][obj-export-ui]") {
    editor::MainWindow window;
    auto* model = window.findChild<editor::SceneViewModel*>();
    REQUIRE(model);
    model->newScene();
    model->createEntity(core::PrimitiveKind::Cube);
    const auto before = objDocumentState(*model);
    const auto count = model->undoStack()->count();
    for (const auto* name : {"ExportObjSource", "ExportObjEvaluated"}) {
        auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
        REQUIRE(action);
        bool validDialog = false;
        QTimer::singleShot(0, &window, [&validDialog] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (dialog) {
                validDialog = dialog->objectName() == "ExportObjDialog" &&
                              dialog->acceptMode() == QFileDialog::AcceptSave &&
                              !dialog->testOption(QFileDialog::DontConfirmOverwrite) &&
                              dialog->defaultSuffix() == "obj";
                dialog->reject();
            }
        });
        action->trigger();
        REQUIRE(validDialog);
        REQUIRE(objDocumentState(*model) == before);
        REQUIRE(model->undoStack()->count() == count);
    }
}
