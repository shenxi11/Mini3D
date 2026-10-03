/*
 * 模块名: HistoryEditorTests
 * 功能概述: 验证挤出重开意图、保存点、文件往返、上下文与真实模型快照。
 * 对外接口: Catch2 [history-editor]；依赖关系: SceneViewModel、临时文件。
 * 输入输出: 世界位移参数到源网格、选择、唯一历史和 dirty。
 * 异常与错误: 失败保留当前内容，不把导航、文本或其他数据命令当作建模操作。
 * 维护说明: 本项不验证 F9 按键和面板，UI 接入属于 HISTORY-02。
 */
#include "editor/SceneViewModel.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>

using namespace mini3d;
namespace {
core::EditableMeshRecord current(const editor::SceneViewModel& model, core::EntityId entity) {
    return *model.scene()->editableMesh(model.scene()->find(entity)->editableMesh);
}
void enterAndExtrude(editor::SceneViewModel& model, const glm::dvec3& worldOffset) {
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    REQUIRE(model.beginExtrudeRegion());
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1), worldOffset)));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.lastOperationWorldOffset() == worldOffset);
}
QByteArray readFile(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
} // namespace

TEST_CASE("Adjusting saved extrusion replaces the same operation and preserves file until save",
          "[history-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    enterAndExtrude(model, {0, 0, 0.25});
    const auto index = model.undoStack()->index();
    const auto* command = model.undoStack()->command(index - 1);
    const auto selected = model.componentSelection();
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("调整上一步.m3dscene"));
    REQUIRE(model.saveScene(path));
    const auto bytes = readFile(path);
    bool published = false;
    const auto connection = QObject::connect(&model, &editor::SceneViewModel::sceneChanged, [&] {
        published = true;
        REQUIRE(model.lastOperationWorldOffset() == glm::dvec3(0, 0, 0.75));
        REQUIRE(model.isModified());
        REQUIRE(model.undoStack()->index() == index);
    });
    REQUIRE(model.adjustLastOperation({0, 0, 0.75}));
    QObject::disconnect(connection);
    REQUIRE(published);
    REQUIRE(model.undoStack()->command(index - 1) == command);
    REQUIRE(model.undoStack()->count() == index);
    REQUIRE(model.undoStack()->cleanIndex() == -1);
    const auto after = current(model, entity);
    REQUIRE(after.content->source.faces.size() == 10);
    REQUIRE(after.content->source.vertices.size() == 12);
    REQUIRE(after.content->source.vertex(after.content->source.faces[0].corners[0].vertex)
                ->position.z == 1.25F);
    REQUIRE(readFile(path) == bytes);
    model.clearComponentSelection();
    model.undo();
    REQUIRE(current(model, entity).content->source.faces.size() == 6);
    REQUIRE(model.componentSelection() == selected);
    REQUIRE_FALSE(model.lastOperationWorldOffset());
    model.redo();
    REQUIRE(current(model, entity).content == after.content);
    REQUIRE(current(model, entity).evaluationRevision > after.evaluationRevision);
    REQUIRE(model.isModified());
    REQUIRE(model.saveScene(path));
    REQUIRE_FALSE(model.isModified());
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(current(reopened, entity).content->source == after.content->source);
    REQUIRE_FALSE(reopened.lastOperationWorldOffset());
}

TEST_CASE("Invalid and unchanged adjustments preserve all state and valid correction is absolute",
          "[history-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    enterAndExtrude(model, {0, 0, 0.25});
    QTemporaryDir directory;
    REQUIRE(model.saveScene(directory.filePath(QStringLiteral("原结果.m3dscene"))));
    const auto before = current(model, entity);
    const auto selected = model.componentSelection();
    const auto revision = model.componentSelectionRevision();
    const auto index = model.undoStack()->index();
    QSignalSpy changes(&model, &editor::SceneViewModel::sceneChanged);
    REQUIRE(model.adjustLastOperation({0, 0, 0.25}));
    for (auto invalid : {glm::dvec3(0), glm::dvec3(0.25, 0, 0),
                         glm::dvec3(0, 0, std::numeric_limits<double>::quiet_NaN())}) {
        REQUIRE_FALSE(model.adjustLastOperation(invalid));
        REQUIRE(current(model, entity).content == before.content);
        REQUIRE(current(model, entity).evaluationRevision == before.evaluationRevision);
        REQUIRE(model.componentSelection() == selected);
        REQUIRE(model.componentSelectionRevision() == revision);
        REQUIRE(model.undoStack()->index() == index);
        REQUIRE(model.undoStack()->count() == index);
        REQUIRE_FALSE(model.isModified());
        REQUIRE(model.lastOperationWorldOffset() == glm::dvec3(0, 0, 0.25));
    }
    REQUIRE(changes.empty());
    model.clearComponentSelection();
    REQUIRE(model.adjustLastOperation({0, 0, -0.25}));
    REQUIRE(model.componentSelection() == selected);
    REQUIRE(current(model, entity).content->source.faces.size() == 10);
    REQUIRE(current(model, entity)
                .content->source.vertex(before.content->source.faces[0].corners[0].vertex)
                ->position.z == 0.25F);
}

TEST_CASE("Last operation survives navigation but not new data or non-top history",
          "[history-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    enterAndExtrude(model, {0, 0, 0.25});
    auto camera = model.editorCamera();
    camera.position.x += 1;
    model.setEditorCamera(camera);
    REQUIRE(model.lastOperationWorldOffset());
    REQUIRE(model.beginComponentTransform());
    REQUIRE_FALSE(model.lastOperationWorldOffset());
    REQUIRE_FALSE(model.adjustLastOperation({0, 0, 0.5}));
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE(model.lastOperationWorldOffset());
    REQUIRE(model.setEditMode(false));
    REQUIRE_FALSE(model.lastOperationWorldOffset());
    REQUIRE(model.setEditMode(true));
    REQUIRE(model.lastOperationWorldOffset());
    auto surface = model.scene()->find(entity)->surface;
    surface.tint.x = 0.25F;
    REQUIRE(model.setSurface(entity, surface));
    REQUIRE_FALSE(model.lastOperationWorldOffset());
    model.undo();
    REQUIRE_FALSE(model.lastOperationWorldOffset());
    REQUIRE_FALSE(model.adjustLastOperation({0, 0, 0.5}));
    model.newScene();
    REQUIRE_FALSE(model.lastOperationWorldOffset());
}

TEST_CASE("Adjustment uses frozen world displacement under a negative nonuniform parent",
          "[history-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    core::Transform transform;
    transform.scale = {-2, 1.5F, 0.5F};
    transform.rotation = glm::angleAxis(glm::radians(35.0F), glm::vec3(0, 1, 0));
    REQUIRE(model.setTransform(parent, transform));
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(entity, parent));
    const auto world = glm::dmat4(model.scene()->worldMatrix(entity));
    const auto direction =
        glm::normalize(glm::transpose(glm::inverse(glm::dmat3(world))) * glm::dvec3(0, 0, 1));
    enterAndExtrude(model, direction * 0.25);
    REQUIRE(model.adjustLastOperation(direction * 0.75));
    const auto& source = current(model, entity).content->source;
    const auto cap = source.vertex(source.faces[0].corners[0].vertex)->position;
    const auto cube = core::modeling::createEditableCube();
    const auto original = cube.vertex(cube.faces[0].corners[0].vertex)->position;
    REQUIRE(glm::length(glm::dvec3(world * glm::dvec4(cap - original, 0)) - direction * 0.75) <
            1.0e-6);
    const auto& unchanged = model.scene()->find(parent)->transform;
    REQUIRE(unchanged.position == transform.position);
    REQUIRE(unchanged.rotation == transform.rotation);
    REQUIRE(unchanged.scale == transform.scale);
}

TEST_CASE("Only the newest extrusion is adjusted from its own before snapshot",
          "[history-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    enterAndExtrude(model, {0, 0, 0.25});
    const auto first = current(model, entity).content;
    const auto firstIndex = model.undoStack()->index();
    REQUIRE(model.beginExtrudeRegion());
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1), glm::dvec3(0, 0, 0.25))));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.adjustLastOperation({0, 0, 0.5}));
    const auto second = current(model, entity).content;
    REQUIRE(second->source.faces.size() == 14);
    REQUIRE(second->source.vertex(second->source.faces[0].corners[0].vertex)->position.z == 1.25F);
    REQUIRE(model.undoStack()->count() == firstIndex + 1);
    model.undo();
    REQUIRE(current(model, entity).content == first);
    REQUIRE_FALSE(model.lastOperationWorldOffset());
    model.redo();
    REQUIRE(current(model, entity).content == second);
    REQUIRE(model.lastOperationWorldOffset() == glm::dvec3(0, 0, 0.5));
}
