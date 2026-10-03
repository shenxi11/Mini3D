/*
 * 模块名: SubdivisionEditorTests
 * 功能概述: 验证细分卡片、源笼、固定链导出与一次历史/保存重开。
 * 对外接口: Catch2 [subdivision-editor]；依赖关系: SceneViewModel、真实Qt窗口。
 * 输入输出: 选项和应用意图到不可变源/求值内容及文件。
 * 异常与错误: 无效参数不改历史、保存点或源。
 * 维护说明: 专项不替代统一DPI/耐久验收。
 */
#include "core/ViewportVisibility.h"
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
const core::EditableMeshContent& subdivisionContent(const editor::SceneViewModel& model,
                                                    core::EntityId id) {
    return *model.scene()->editableMesh(model.scene()->find(id)->editableMesh)->content;
}
} // namespace

TEST_CASE("Subdivision options and application preserve source and one undo stack across save",
          "[subdivision-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(entity));
    const auto source = subdivisionContent(model, entity).source;
    const auto index = model.undoStack()->index();
    core::modeling::SubdivisionOptions options;
    REQUIRE(model.setSubdivisionOptions(entity, options));
    REQUIRE(subdivisionContent(model, entity).source == source);
    REQUIRE(subdivisionContent(model, entity).evaluatedMesh().faces.size() == 24);
    REQUIRE(model.undoStack()->index() == index + 1);
    REQUIRE(model.setSubdivisionOptions(entity, options));
    REQUIRE(model.undoStack()->index() == index + 1);
    options.levels = 2;
    REQUIRE(model.setSubdivisionOptions(entity, options));
    REQUIRE(subdivisionContent(model, entity).evaluatedMesh().faces.size() == 96);
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("subdivision.m3dscene");
    REQUIRE(model.saveScene(path));
    const auto savedIndex = model.undoStack()->index();
    auto invalid = options;
    invalid.levels = 3;
    REQUIRE_FALSE(model.setSubdivisionOptions(entity, invalid));
    REQUIRE(model.undoStack()->index() == savedIndex);
    REQUIRE_FALSE(model.isModified());
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(reopened.subdivisionOptions(entity) == options);
    REQUIRE(subdivisionContent(reopened, entity).source == source);
    REQUIRE(subdivisionContent(reopened, entity).evaluatedMesh().vertices.size() == 98);
    const auto evaluated = subdivisionContent(model, entity).evaluatedMesh();
    REQUIRE(model.applySubdivision(entity));
    REQUIRE_FALSE(model.subdivisionOptions(entity));
    REQUIRE(subdivisionContent(model, entity).source == evaluated);
    REQUIRE(model.undoStack()->index() == savedIndex + 1);
    model.undo();
    REQUIRE(model.subdivisionOptions(entity) == options);
    REQUIRE(subdivisionContent(model, entity).source == source);
    REQUIRE_FALSE(model.isModified());
    model.redo();
    REQUIRE(subdivisionContent(model, entity).source == evaluated);
}

TEST_CASE("Mirror Subdivision uses final OBJ and composed source face visibility",
          "[subdivision-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(entity));
    auto source = subdivisionContent(model, entity).source;
    for (auto& vertex : source.vertices)
        vertex.position.x += 2;
    REQUIRE(model.replaceEditableMesh(entity, source));
    REQUIRE(model.setMirrorOptions(entity, core::modeling::MirrorOptions{}));
    REQUIRE(model.setSubdivisionOptions(entity, core::modeling::SubdivisionOptions{}));
    const auto& content = subdivisionContent(model, entity);
    REQUIRE(content.evaluatedMesh().faces.size() == 48);
    REQUIRE(content.evaluatedMesh().vertices.size() == 52);
    REQUIRE(content.source == source);
    core::ViewportVisibility visibility;
    visibility.editedEntity = entity;
    visibility.faces.insert(source.faces.front().id);
    std::size_t hidden = 0;
    for (std::size_t i = 0; i < content.displayedDerived().triangleSources.size(); ++i)
        if (!visibility.isTriangleVisible(entity, content, i))
            ++hidden;
    REQUIRE(hidden == 16); // 源面及其Mirror副本，各4子面/8三角。
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto history = model.undoStack()->index();
    const auto path = directory.filePath("evaluated.obj");
    REQUIRE(model.exportObj(path, true));
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    std::size_t faces = 0;
    while (!file.atEnd())
        if (file.readLine().startsWith("f "))
            ++faces;
    REQUIRE(faces == 48);
    REQUIRE(model.undoStack()->index() == history);
    REQUIRE(model.applyMirror(entity));
    REQUIRE_FALSE(model.mirrorOptions(entity));
    REQUIRE(model.subdivisionOptions(entity));
    REQUIRE(subdivisionContent(model, entity).evaluatedMesh().faces.size() == 48);
    REQUIRE(model.applySubdivision(entity));
    REQUIRE_FALSE(model.subdivisionOptions(entity));
    REQUIRE(subdivisionContent(model, entity).source.faces.size() == 48);
    model.undo();
    REQUIRE(model.subdivisionOptions(entity));
    model.undo();
    REQUIRE(model.mirrorOptions(entity));
    REQUIRE(subdivisionContent(model, entity).source == source);
}

TEST_CASE("Subdivision card toggles level without editing evaluated topology",
          "[subdivision-editor]") {
    editor::MainWindow window;
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* add = window.findChild<QPushButton*>(QStringLiteral("SubdivisionAddButton"));
    auto* levels = window.findChild<QComboBox*>(QStringLiteral("SubdivisionLevels"));
    auto* enabled = window.findChild<QCheckBox*>(QStringLiteral("SubdivisionEnabled"));
    REQUIRE(model);
    REQUIRE(add);
    REQUIRE(levels);
    REQUIRE(enabled);
    model->newScene();
    const auto entity = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE_FALSE(add->isEnabled());
    REQUIRE(model->makeEditable(entity));
    REQUIRE(add->isEnabled());
    add->click();
    REQUIRE(model->subdivisionOptions(entity));
    levels->setCurrentIndex(1);
    REQUIRE(subdivisionContent(*model, entity).evaluatedMesh().faces.size() == 96);
    enabled->setChecked(false);
    REQUIRE(subdivisionContent(*model, entity).evaluatedMesh().faces.size() == 6);
    enabled->setChecked(true);
    REQUIRE(model->setEditMode(true));
    model->setSelectionDomain(editor::SelectionDomain::Vertex);
    model->selectAllComponents();
    REQUIRE(model->componentSelection().selectedIds().size() == 8);
    REQUIRE(subdivisionContent(*model, entity).source.faces.size() == 6);
}
