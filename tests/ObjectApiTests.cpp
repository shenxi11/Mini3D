/*
 * 模块名: ObjectApiTests
 * 功能概述: 验证 M5 对象子树、集合及相机方向光的显式 API 与唯一历史。
 * 对外接口: Catch2 [object-api] 用例，无独立 main 或服务监听。
 * 依赖关系: SceneViewModel、EditorApiService、ApiJsonCodec、冻结 M5 JSON 样例。
 * 输入输出: 显式目标与版本到结构、设备、复制映射及撤销状态断言。
 * 异常与错误: 非法参数、Busy、最后提交守卫均必须保留已确认场景与历史。
 * 维护说明: 不写生产文档；大子树仅在夹具中直接构造以验证 API 规模边界。
 */
#include "editor/SceneViewModel.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <limits>
#include <type_traits>

using namespace mini3d;
namespace {
namespace api = editor::api;
struct ObjectFixture {
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    ObjectFixture() {
        model.newScene();
    }
    template <typename Request> Request mutation() const {
        Request request;
        request.document = service.documentState().document;
        request.expectedDocumentRevision = service.documentState().documentRevision;
        return request;
    }
    api::HistoryMutationRequest history() const {
        auto request = mutation<api::HistoryMutationRequest>();
        request.expectedHistoryRevision = service.documentState().historyRevision;
        return request;
    }
    core::EntityId create(const QString& name, core::EntityId parent = 0,
                          core::PrimitiveKind primitive = core::PrimitiveKind::Empty) {
        auto request = mutation<api::EntityCreateRequest>();
        request.name = name;
        request.parentId = parent;
        request.primitive = primitive;
        auto result = service.createEntity(request);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    core::CollectionId collection(const QString& name = QStringLiteral("集合")) {
        auto request = mutation<api::CollectionCreateRequest>();
        request.name = name;
        auto result = service.createCollection(request);
        REQUIRE(result.hasValue());
        return result.value->collectionId;
    }
    void assign(core::EntityId id, core::CollectionId collectionId) {
        auto request = mutation<api::CollectionAssignRequest>();
        request.entityId = id;
        request.collectionId = collectionId;
        REQUIRE(service.assignCollection(request).hasValue());
    }
    core::EntityId camera(core::EntityId parent = 0) {
        auto request = mutation<api::CameraCreateRequest>();
        request.name = QStringLiteral("相机");
        request.parentId = parent;
        auto result = service.createCamera(request);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    core::EntityId light(core::EntityId parent = 0) {
        auto request = mutation<api::LightCreateRequest>();
        request.name = QStringLiteral("方向光");
        request.parentId = parent;
        auto result = service.createLight(request);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    api::EntityGetRequest entityRequest(core::EntityId id) const {
        api::EntityGetRequest request;
        request.document = service.documentState().document;
        request.entityId = id;
        return request;
    }
    QJsonObject wire() const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        return {{"document", state["document"]},
                {"expectedDocumentRevision", state["documentRevision"]}};
    }
};
QJsonObject transformJson() {
    return {{"space", "local"},
            {"translation", QJsonArray{3, 4, 5}},
            {"rotationQuaternion", QJsonArray{0, 0, 0, 2}},
            {"scale", QJsonArray{-2, 3, 4}}};
}
QJsonObject cameraJson(float fieldOfView = 55) {
    return {{"fieldOfView", fieldOfView}, {"nearPlane", 0.1}, {"farPlane", 1000}};
}
QJsonObject lightJson(float intensity = 2) {
    return {{"color", QJsonArray{0.5, 0.6, 0.7}}, {"intensity", intensity}};
}
struct Remembered {
    api::DocumentState state;
    int count, index, clean;
    const QUndoCommand* redo;
    core::EntityId selection;
    std::size_t entityCount;
    std::vector<core::SceneCollection> collections;
};
Remembered remember(ObjectFixture& fixture) {
    const auto* history = fixture.model.undoStack();
    return {fixture.service.documentState(),
            history->count(),
            history->index(),
            history->cleanIndex(),
            history->canRedo() ? history->command(history->index()) : nullptr,
            fixture.model.selection()->selectedEntity(),
            fixture.model.scene()->nodes().size(),
            fixture.model.scene()->collections()};
}
void unchanged(ObjectFixture& fixture, const Remembered& before) {
    REQUIRE(fixture.service.documentState().document == before.state.document);
    REQUIRE(fixture.service.documentState().documentRevision == before.state.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == before.state.historyRevision);
    REQUIRE(fixture.model.undoStack()->count() == before.count);
    REQUIRE(fixture.model.undoStack()->index() == before.index);
    REQUIRE(fixture.model.undoStack()->cleanIndex() == before.clean);
    REQUIRE(fixture.model.undoStack()->canRedo() == (before.redo != nullptr));
    if (before.redo)
        REQUIRE(fixture.model.undoStack()->command(before.index) == before.redo);
    REQUIRE(fixture.model.selection()->selectedEntity() == before.selection);
    REQUIRE(fixture.model.scene()->nodes().size() == before.entityCount);
    REQUIRE(fixture.model.scene()->collections() == before.collections);
}
void prepareRedo(ObjectFixture& fixture, core::EntityId id) {
    // 测试夹具直接设置保存点，之后只经真实共享历史制造 redo 分支。
    const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
    REQUIRE(fixture.model.renameEntity(id, QStringLiteral("待重做名称")));
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.undoStack()->canRedo());
    REQUIRE(fixture.model.undoStack()->isClean());
}
} // namespace

TEST_CASE("M5 codec consumes all frozen samples and canonicalizes every new method",
          "[object-api]") {
    QFile file(QDir(QString::fromUtf8(MINI3D_API_SCHEMA_DIRECTORY)).filePath("m5-examples.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto examples = QJsonDocument::fromJson(file.readAll(), &error).object();
    REQUIRE(error.error == QJsonParseError::NoError);
    REQUIRE(examples["valid"].toArray().size() == 11);
    for (const auto& category : {"valid", "invalid"}) {
        const bool expected = QString::fromLatin1(category) == "valid";
        for (const auto& item : examples[category].toArray()) {
            const auto sample = item.toObject();
            const auto method = sample["method"].toString();
            CAPTURE(category, method);
            REQUIRE(api::ApiJsonCodec::decodeRequest(method, sample["value"]).hasValue() ==
                    expected);
            const auto canonical = api::ApiJsonCodec::canonicalParams(method, sample["value"]);
            REQUIRE(canonical.hasValue() == expected);
            if (!expected)
                continue;
            REQUIRE(api::ApiJsonCodec::decodeRequest(method, *canonical.value).hasValue());
            REQUIRE(api::ApiJsonCodec::canonicalParams(method, *canonical.value).value ==
                    canonical.value);
            if (sample["value"].toObject().contains("entityId"))
                REQUIRE((*canonical.value)["entityId"] == "9007199254740993");
            for (const auto& key : sample["value"].toObject().keys())
                REQUIRE(canonical.value->contains(key));
            REQUIRE((*canonical.value)["timeoutMs"] == int(api::limits::mutationTimeoutMs));
            auto modified = sample["value"].toObject();
            const auto key = method.startsWith("camera.")   ? "camera"
                             : method.startsWith("light.")  ? "light"
                             : method == "entity.setParent" ? "parentId"
                             : method == "collection.create" || method == "collection.update"
                                 ? "name"
                             : method.startsWith("collection.") ? "collectionId"
                                                                : "entityId";
            modified.insert(key, key == QStringLiteral("camera")  ? QJsonValue(cameraJson(60))
                                 : key == QStringLiteral("light") ? QJsonValue(lightJson(3))
                                 : key == QStringLiteral("name")
                                     ? QJsonValue(QStringLiteral("另一个名字"))
                                     : QJsonValue(QStringLiteral("5")));
            const auto changed = api::ApiJsonCodec::canonicalParams(method, modified);
            REQUIRE(changed.hasValue());
            REQUIRE(changed.value != canonical.value);
        }
    }
    ObjectFixture fixture;
    auto parent = fixture.wire();
    parent.insert("entityId", "1");
    parent.insert("parentId", "0");
    REQUIRE((*api::ApiJsonCodec::canonicalParams("entity.setParent", parent).value)["mode"] ==
            "keepLocal");
    auto collection = fixture.wire();
    collection.insert("name", "defaults");
    REQUIRE(
        (*api::ApiJsonCodec::canonicalParams("collection.create", collection).value)["visible"] ==
        true);
    for (const auto& method : {"camera.create", "light.create"}) {
        auto params = fixture.wire();
        params.insert("name", "defaults");
        params.insert("parentId", "0");
        params.insert("transform", transformJson());
        params.insert(method == QStringLiteral("camera.create") ? "camera" : "light",
                      method == QStringLiteral("camera.create") ? cameraJson() : lightJson());
        const auto canonical = api::ApiJsonCodec::canonicalParams(method, params);
        REQUIRE(canonical.hasValue());
        REQUIRE((*canonical.value)["visible"] == true);
    }
}

TEST_CASE("Explicit duplication copies complete subtree collections and distinct mesh binding",
          "[object-api]") {
    ObjectFixture fixture;
    const auto outside = fixture.create("外部父");
    const auto root = fixture.create("父", outside);
    const auto child = fixture.create("子", root, core::PrimitiveKind::Cube);
    const auto grandchild = fixture.camera(child);
    const auto selected = fixture.create("用户选区");
    REQUIRE(fixture.model.makeEditable(child));
    const auto mesh = fixture.model.scene()->find(child)->editableMesh;
    const auto content = fixture.model.scene()->editableMesh(mesh)->content;
    const auto collection = fixture.collection();
    fixture.assign(child, collection);
    fixture.model.selection()->setSelectedEntity(selected);
    const auto before = remember(fixture);
    auto request = fixture.mutation<api::EntityDuplicateRequest>();
    request.entityId = root;
    const auto result = fixture.service.duplicateEntity(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->command.status == api::ResultStatus::Committed);
    REQUIRE(result.value->entityIdMap.size() == 3);
    const auto copyRoot = result.value->entityIdMap.at(root);
    const auto copyChild = result.value->entityIdMap.at(child);
    const auto copyGrandchild = result.value->entityIdMap.at(grandchild);
    REQUIRE(copyRoot != root);
    REQUIRE(fixture.model.scene()->find(copyRoot)->parent == outside);
    REQUIRE(fixture.model.scene()->find(copyChild)->parent == copyRoot);
    REQUIRE(fixture.model.scene()->find(copyGrandchild)->parent == copyChild);
    REQUIRE(fixture.model.scene()->find(copyGrandchild)->camera ==
            fixture.model.scene()->find(grandchild)->camera);
    REQUIRE(fixture.model.scene()->find(copyChild)->transform.position ==
            fixture.model.scene()->find(child)->transform.position);
    const auto copiedMesh = fixture.model.scene()->find(copyChild)->editableMesh;
    REQUIRE(copiedMesh != mesh);
    REQUIRE(fixture.model.scene()->editableMesh(copiedMesh)->content == content);
    REQUIRE(fixture.model.scene()->collections().front().members ==
            std::set<core::EntityId>{child, copyChild});
    REQUIRE(std::is_sorted(result.value->command.createdEntityIds.begin(),
                           result.value->command.createdEntityIds.end()));
    REQUIRE(result.value->command.createdEntityIds == result.value->command.affectedEntityIds);
    REQUIRE_FALSE(result.value->command.selectionChanged);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.model.undoStack()->count() == before.count + 1);
    REQUIRE(result.value->command.state.documentRevision == before.state.documentRevision + 1);
    const auto wire = api::ApiJsonCodec::encode(*result.value);
    REQUIRE(wire["entityIdMap"].toArray().size() == 3);
    REQUIRE(wire["entityIdMap"].toArray().first().toObject()["sourceEntityId"].isString());
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE_FALSE(fixture.model.scene()->find(copyRoot));
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.model.scene()->collections().front().members ==
            std::set<core::EntityId>{child});
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->find(copyChild)->editableMesh == copiedMesh);
    REQUIRE(fixture.model.scene()->editableMesh(copiedMesh)->content == content);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    fixture.model.selection()->setSelectedEntity(copyGrandchild);
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.selection()->selectedEntity() == 0);
}

TEST_CASE("Explicit deletion returns every subtree ID and restores only its deleted selection",
          "[object-api]") {
    for (const bool selectDeleted : {false, true}) {
        ObjectFixture fixture;
        const auto parent = fixture.create("保留父");
        const auto root = fixture.create("删除根", parent);
        const auto child = fixture.create("删除子", root);
        const auto sibling = fixture.create("保留兄弟", parent);
        const auto outside = fixture.create("外部选择");
        const auto collection = fixture.collection();
        fixture.assign(child, collection);
        fixture.model.selection()->setSelectedEntity(selectDeleted ? child : outside);
        auto request = fixture.mutation<api::EntityDeleteRequest>();
        request.entityId = root;
        const auto result = fixture.service.deleteEntity(request);
        REQUIRE(result.hasValue());
        REQUIRE(result.value->affectedEntityIds == std::vector<core::EntityId>{root, child});
        REQUIRE(result.value->createdEntityIds.empty());
        REQUIRE(result.value->selectionChanged == selectDeleted);
        REQUIRE(fixture.model.selection()->selectedEntity() == (selectDeleted ? 0 : outside));
        REQUIRE(fixture.model.scene()->find(sibling));
        REQUIRE(fixture.model.scene()->collections().front().members.empty());
        REQUIRE(fixture.service.undo(fixture.history()).hasValue());
        REQUIRE(fixture.model.scene()->find(root)->parent == parent);
        REQUIRE(fixture.model.scene()->find(parent)->children ==
                std::vector<core::EntityId>{root, sibling});
        REQUIRE(fixture.model.scene()->find(child)->parent == root);
        REQUIRE(fixture.model.scene()->collections().front().members ==
                std::set<core::EntityId>{child});
        REQUIRE(fixture.model.selection()->selectedEntity() == (selectDeleted ? child : outside));
        REQUIRE(fixture.service.redo(fixture.history()).hasValue());
        REQUIRE_FALSE(fixture.model.scene()->find(root));
        REQUIRE(fixture.model.selection()->selectedEntity() == (selectDeleted ? 0 : outside));
        const auto before = remember(fixture);
        request = fixture.mutation<api::EntityDeleteRequest>();
        request.entityId = root;
        REQUIRE(fixture.service.deleteEntity(request).error->code == api::ErrorCode::NotFound);
        unchanged(fixture, before);
    }
}

TEST_CASE(
    "Explicit reparent preserves exact local TRS and rejects cycles keepWorld missing parents",
    "[object-api]") {
    ObjectFixture fixture;
    const auto oldParent = fixture.create("原父");
    const auto child = fixture.create("子", oldParent);
    const auto sibling = fixture.create("兄弟", oldParent);
    const auto newParent = fixture.create("新父");
    const auto descendant = fixture.create("后代", child);
    core::Transform transform;
    transform.position = {1, 2, 3};
    transform.rotation = glm::quat(0.7F, 0.1F, 0.2F, 0.3F);
    transform.scale = {-2, 3, 4};
    REQUIRE(fixture.model.setTransform(child, transform));
    const auto exact = fixture.model.scene()->find(child)->transform;
    fixture.model.selection()->setSelectedEntity(sibling);
    auto request = fixture.mutation<api::EntitySetParentRequest>();
    request.entityId = child;
    request.parentId = newParent;
    const auto result = fixture.service.setParent(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->affectedEntityIds == std::vector<core::EntityId>{child});
    REQUIRE(fixture.model.scene()->find(child)->parent == newParent);
    REQUIRE(fixture.model.scene()->find(child)->transform.rotation == exact.rotation);
    REQUIRE(fixture.model.scene()->find(child)->transform.position == exact.position);
    REQUIRE(fixture.model.scene()->find(child)->transform.scale == exact.scale);
    REQUIRE(fixture.model.selection()->selectedEntity() == sibling);
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->find(oldParent)->children ==
            std::vector<core::EntityId>{child, sibling});
    REQUIRE(fixture.model.scene()->find(child)->transform.rotation == exact.rotation);
    prepareRedo(fixture, sibling);
    for (const auto parent : {child, descendant, core::EntityId(99999)}) {
        const auto before = remember(fixture);
        request = fixture.mutation<api::EntitySetParentRequest>();
        request.entityId = child;
        request.parentId = parent;
        REQUIRE_FALSE(fixture.service.setParent(request).hasValue());
        unchanged(fixture, before);
    }
    const auto before = remember(fixture);
    request = fixture.mutation<api::EntitySetParentRequest>();
    request.entityId = child;
    request.parentId = oldParent;
    request.mode = QStringLiteral("keepWorld");
    REQUIRE(fixture.service.setParent(request).error->code == api::ErrorCode::UnsupportedOperation);
    unchanged(fixture, before);
    request.mode = QStringLiteral("keepLocal");
    REQUIRE(fixture.service.setParent(request).value->status == api::ResultStatus::NoChange);
    unchanged(fixture, before);
}

TEST_CASE("Collection CRUD and assignment preserve object hierarchy and expose complete summary",
          "[object-api]") {
    ObjectFixture fixture;
    const auto root = fixture.create("父");
    const auto child = fixture.create("子", root);
    fixture.model.selection()->setSelectedEntity(child);
    auto create = fixture.mutation<api::CollectionCreateRequest>();
    create.name = QStringLiteral(" 精确中文组 ");
    create.visible = false;
    const auto created = fixture.service.createCollection(create);
    REQUIRE(created.hasValue());
    const auto id = created.value->collectionId;
    REQUIRE(id != 0);
    REQUIRE(fixture.model.scene()->collections().front().name ==
            create.name.toUtf8().toStdString());
    REQUIRE_FALSE(fixture.model.scene()->collections().front().visible);
    fixture.assign(root, id);
    REQUIRE(fixture.model.scene()->find(child)->parent == root);
    REQUIRE_FALSE(fixture.model.scene()->isVisible(child));
    const auto rootSnapshot = fixture.service.entity(fixture.entityRequest(root));
    const auto childSnapshot = fixture.service.entity(fixture.entityRequest(child));
    REQUIRE(rootSnapshot.value->entity.collectionId == id);
    REQUIRE(childSnapshot.value->entity.collectionId == 0);
    REQUIRE_FALSE(childSnapshot.value->entity.effectiveVisible);
    const api::DocumentRequest query{fixture.service.documentState().document};
    const auto summary = fixture.service.sceneSummary(query);
    REQUIRE(summary.hasValue());
    REQUIRE(summary.value->collections.size() == 1);
    REQUIRE(summary.value->collections.front().entityIds == std::vector<core::EntityId>{root});
    const auto summaryJson = api::ApiJsonCodec::encode(*summary.value);
    REQUIRE(summaryJson["collections"].toArray().first().toObject()["collectionId"] ==
            QString::number(id));
    auto update = fixture.mutation<api::CollectionUpdateRequest>();
    update.collectionId = id;
    update.name = QStringLiteral("更新名称");
    update.visible = true;
    const auto count = fixture.model.undoStack()->count();
    auto updated = fixture.service.updateCollection(update);
    REQUIRE(updated.hasValue());
    REQUIRE(updated.value->command.affectedEntityIds == std::vector<core::EntityId>{root});
    REQUIRE(fixture.model.undoStack()->count() == count + 1);
    REQUIRE(fixture.model.scene()->isVisible(child));
    auto deleted = fixture.mutation<api::CollectionDeleteRequest>();
    deleted.collectionId = id;
    const auto result = fixture.service.deleteCollection(deleted);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->command.affectedEntityIds == std::vector<core::EntityId>{root});
    REQUIRE(fixture.model.scene()->collections().empty());
    REQUIRE(fixture.model.scene()->find(root));
    REQUIRE(fixture.model.scene()->find(child)->parent == root);
    REQUIRE(fixture.model.selection()->selectedEntity() == child);
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->collections().front().members.contains(root));
    auto unassign = fixture.mutation<api::CollectionAssignRequest>();
    unassign.entityId = root;
    unassign.collectionId = 0;
    const auto unassigned = fixture.service.assignCollection(unassign);
    REQUIRE(unassigned.hasValue());
    REQUIRE(api::ApiJsonCodec::encode(*unassigned.value)["collectionId"] == "0");
    REQUIRE(fixture.model.scene()->collections().front().members.empty());
    REQUIRE(fixture.model.scene()->find(child)->parent == root);
}

TEST_CASE("Collections reject invalid complete updates and keep redo clean point on no change",
          "[object-api]") {
    ObjectFixture fixture;
    const auto entity = fixture.create("成员");
    const auto id = fixture.collection();
    fixture.assign(entity, id);
    prepareRedo(fixture, entity);
    const auto before = remember(fixture);
    auto create = fixture.mutation<api::CollectionCreateRequest>();
    create.name = QStringLiteral("  ");
    REQUIRE(fixture.service.createCollection(create).error->code ==
            api::ErrorCode::InvalidArgument);
    auto update = fixture.mutation<api::CollectionUpdateRequest>();
    update.collectionId = id;
    update.name = QStringLiteral("  ");
    update.visible = false;
    REQUIRE(fixture.service.updateCollection(update).error->code ==
            api::ErrorCode::InvalidArgument);
    REQUIRE(fixture.model.scene()->collections().front().visible);
    update.name = QStringLiteral("集合");
    update.visible = true;
    REQUIRE(fixture.service.updateCollection(update).value->command.status ==
            api::ResultStatus::NoChange);
    unchanged(fixture, before);
    update.collectionId = 99999;
    REQUIRE(fixture.service.updateCollection(update).error->code == api::ErrorCode::NotFound);
    auto remove = fixture.mutation<api::CollectionDeleteRequest>();
    remove.collectionId = 99999;
    REQUIRE(fixture.service.deleteCollection(remove).error->code == api::ErrorCode::NotFound);
    auto assign = fixture.mutation<api::CollectionAssignRequest>();
    assign.entityId = entity;
    assign.collectionId = id;
    REQUIRE(fixture.service.assignCollection(assign).value->command.status ==
            api::ResultStatus::NoChange);
    assign.collectionId = 99999;
    REQUIRE(fixture.service.assignCollection(assign).error->code == api::ErrorCode::NotFound);
    assign.entityId = 99999;
    assign.collectionId = id;
    REQUIRE(fixture.service.assignCollection(assign).error->code == api::ErrorCode::NotFound);
    unchanged(fixture, before);
}

TEST_CASE("Complete camera and light creation use explicit properties and one history entry",
          "[object-api]") {
    ObjectFixture fixture;
    const auto parent = fixture.create("父");
    const auto selected = fixture.create("选中对象", 0, core::PrimitiveKind::Cube);
    fixture.model.selection()->setSelectedEntity(selected);
    REQUIRE(fixture.model.setCursorPosition({100, 200, 300}));
    for (const bool camera : {true, false}) {
        const auto count = fixture.model.undoStack()->count();
        auto wire = fixture.wire();
        wire.insert("name", camera ? "完整相机" : "完整方向光");
        wire.insert("parentId", QString::number(parent));
        wire.insert("transform", transformJson());
        wire.insert("visible", false);
        wire.insert(camera ? "camera" : "light", camera ? cameraJson() : lightJson());
        const auto result = api::ApiJsonCodec::invoke(
            fixture.service, camera ? "camera.create" : "light.create", wire);
        REQUIRE(result.contains("result"));
        const auto id = result["result"]
                            .toObject()["created"]
                            .toObject()["entityIds"]
                            .toArray()
                            .first()
                            .toString()
                            .toULongLong();
        const auto* node = fixture.model.scene()->find(id);
        REQUIRE(node->parent == parent);
        REQUIRE(node->transform.position == glm::vec3(3, 4, 5));
        REQUIRE(node->transform.rotation == glm::quat(1, 0, 0, 0));
        REQUIRE(node->transform.scale == glm::vec3(-2, 3, 4));
        REQUIRE_FALSE(node->visible);
        REQUIRE(node->primitive == core::PrimitiveKind::Empty);
        REQUIRE(node->camera.has_value() == camera);
        REQUIRE(node->light.has_value() == !camera);
        REQUIRE(fixture.model.undoStack()->count() == count + 1);
        REQUIRE(fixture.model.selection()->selectedEntity() == selected);
        const auto snapshot =
            api::ApiJsonCodec::encode(
                *fixture.service.entity(fixture.entityRequest(id)).value)["entity"]
                .toObject();
        REQUIRE(snapshot[camera ? "camera" : "light"].isObject());
        REQUIRE(snapshot[camera ? "light" : "camera"].isNull());
        REQUIRE(snapshot["collectionId"] == "0");
        REQUIRE(fixture.service.undo(fixture.history()).hasValue());
        REQUIRE_FALSE(fixture.model.scene()->find(id));
        REQUIRE(fixture.model.selection()->selectedEntity() == selected);
        REQUIRE(fixture.service.redo(fixture.history()).hasValue());
        REQUIRE(fixture.model.scene()->find(id)->camera.has_value() == camera);
        REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    }
}

TEST_CASE("Device updates reject arbitrary components and preserve history for equal values",
          "[object-api]") {
    ObjectFixture fixture;
    const auto ordinary = fixture.create("普通对象", 0, core::PrimitiveKind::Cube);
    const auto camera = fixture.camera();
    const auto light = fixture.light();
    fixture.model.selection()->setSelectedEntity(ordinary);
    auto updateCamera = fixture.mutation<api::CameraUpdateRequest>();
    updateCamera.entityId = camera;
    updateCamera.camera.fieldOfView = 75;
    REQUIRE(fixture.service.updateCamera(updateCamera).hasValue());
    REQUIRE(fixture.model.scene()->find(camera)->camera->fieldOfView == 75);
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->find(camera)->camera->fieldOfView == 45);
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    auto updateLight = fixture.mutation<api::LightUpdateRequest>();
    updateLight.entityId = light;
    updateLight.light.color = {0.2F, 0.4F, 0.6F};
    updateLight.light.intensity = 4;
    REQUIRE(fixture.service.updateLight(updateLight).hasValue());
    REQUIRE(fixture.model.scene()->find(light)->light == updateLight.light);
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->find(light)->light->intensity == 0.72F);
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    prepareRedo(fixture, ordinary);
    const auto before = remember(fixture);
    updateCamera = fixture.mutation<api::CameraUpdateRequest>();
    updateCamera.entityId = camera;
    updateCamera.camera = *fixture.model.scene()->find(camera)->camera;
    REQUIRE(fixture.service.updateCamera(updateCamera).value->status ==
            api::ResultStatus::NoChange);
    updateLight = fixture.mutation<api::LightUpdateRequest>();
    updateLight.entityId = light;
    updateLight.light = *fixture.model.scene()->find(light)->light;
    REQUIRE(fixture.service.updateLight(updateLight).value->status == api::ResultStatus::NoChange);
    for (const auto wrong : {ordinary, light}) {
        updateCamera.entityId = wrong;
        REQUIRE(fixture.service.updateCamera(updateCamera).error->code ==
                api::ErrorCode::UnsupportedOperation);
    }
    for (const auto wrong : {ordinary, camera}) {
        updateLight.entityId = wrong;
        REQUIRE(fixture.service.updateLight(updateLight).error->code ==
                api::ErrorCode::UnsupportedOperation);
    }
    updateCamera.entityId = camera;
    updateCamera.camera.farPlane = updateCamera.camera.nearPlane;
    REQUIRE(fixture.service.updateCamera(updateCamera).error->code ==
            api::ErrorCode::InvalidArgument);
    updateLight.entityId = light;
    updateLight.light.intensity = 11;
    REQUIRE(fixture.service.updateLight(updateLight).error->code ==
            api::ErrorCode::InvalidArgument);
    auto createCamera = fixture.mutation<api::CameraCreateRequest>();
    createCamera.name = "不能创建相机";
    createCamera.camera.fieldOfView = 180;
    REQUIRE(fixture.service.createCamera(createCamera).error->code ==
            api::ErrorCode::InvalidArgument);
    auto createLight = fixture.mutation<api::LightCreateRequest>();
    createLight.name = "不能创建灯";
    createLight.light.color = {2, 1, 1};
    REQUIRE(fixture.service.createLight(createLight).error->code ==
            api::ErrorCode::InvalidArgument);
    unchanged(fixture, before);
    REQUIRE(fixture.model.scene()->find(ordinary)->primitive == core::PrimitiveKind::Cube);
    REQUIRE_FALSE(fixture.model.scene()->find(ordinary)->camera);
    REQUIRE_FALSE(fixture.model.scene()->find(ordinary)->light);
}

TEST_CASE("Directional lights retain smallest visible ID rule including collection visibility",
          "[object-api]") {
    ObjectFixture fixture;
    const auto first = fixture.light();
    const auto second = fixture.light();
    auto update = fixture.mutation<api::LightUpdateRequest>();
    update.entityId = first;
    update.light.intensity = 2;
    update.light.color = {1, 0, 0};
    REQUIRE(fixture.service.updateLight(update).hasValue());
    update = fixture.mutation<api::LightUpdateRequest>();
    update.entityId = second;
    update.light.intensity = 4;
    update.light.color = {0, 0, 1};
    REQUIRE(fixture.service.updateLight(update).hasValue());
    REQUIRE(fixture.model.scene()->effectiveLighting().color == glm::vec3(1, 0, 0));
    const auto collection = fixture.collection();
    fixture.assign(first, collection);
    auto visibility = fixture.mutation<api::CollectionUpdateRequest>();
    visibility.collectionId = collection;
    visibility.visible = false;
    REQUIRE(fixture.service.updateCollection(visibility).hasValue());
    REQUIRE(fixture.model.scene()->effectiveLighting().color == glm::vec3(0, 0, 1));
    auto hide = fixture.mutation<api::EntityUpdateRequest>();
    hide.entityId = second;
    hide.changes.visible = false;
    REQUIRE(fixture.service.updateEntity(hide).hasValue());
    REQUIRE(fixture.model.scene()->effectiveLighting().intensity == 0);
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->effectiveLighting().intensity == 4);
}

TEST_CASE("Every M5 method rejects the final guard after candidates without consuming redo",
          "[object-api][api-commit-guard]") {
    for (const auto& method :
         {"entity.duplicate", "entity.delete", "entity.setParent", "collection.create",
          "collection.update", "collection.delete", "collection.assign", "camera.create",
          "camera.update", "light.create", "light.update"}) {
        CAPTURE(method);
        ObjectFixture fixture;
        const auto root = fixture.create("根");
        const auto child = fixture.create("子", root);
        const auto other = fixture.create("选区");
        const auto collection = fixture.collection();
        fixture.assign(child, collection);
        const auto camera = fixture.camera();
        const auto light = fixture.light();
        fixture.model.selection()->setSelectedEntity(child);
        prepareRedo(fixture, other);
        const auto before = remember(fixture);
        const auto beforeCamera = *fixture.model.scene()->find(camera)->camera;
        const auto beforeLight = *fixture.model.scene()->find(light)->light;
        auto params = fixture.wire();
        const QString name = QString::fromLatin1(method);
        if (name.startsWith("entity."))
            params.insert("entityId", QString::number(root));
        if (name == "entity.setParent")
            params.insert("parentId", QString::number(other));
        if (name == "collection.create")
            params.insert("name", "准备后的集合");
        if (name == "collection.update" || name == "collection.delete")
            params.insert("collectionId", QString::number(collection));
        if (name == "collection.update") {
            params.insert("name", "准备后的集合");
            params.insert("visible", false);
        }
        if (name == "collection.assign") {
            params.insert("entityId", QString::number(other));
            params.insert("collectionId", QString::number(collection));
        }
        if (name.endsWith(".create") && (name.startsWith("camera.") || name.startsWith("light."))) {
            params.insert("name", "准备后的设备");
            params.insert("parentId", QString::number(root));
            params.insert("transform", transformJson());
        }
        if (name.startsWith("camera.")) {
            params.insert("camera", cameraJson(60));
            if (name.endsWith(".update"))
                params.insert("entityId", QString::number(camera));
        }
        if (name.startsWith("light.")) {
            params.insert("light", lightJson(3));
            if (name.endsWith(".update"))
                params.insert("entityId", QString::number(light));
        }
        int guardCalls = 0;
        const auto guard = [&]() -> std::optional<api::ApiError> {
            ++guardCalls;
            unchanged(fixture, before);
            REQUIRE(fixture.model.scene()->find(root)->parent == 0);
            return api::ApiError{api::ErrorCode::Cancelled, "guard refusal", "guard",
                                 api::Recovery::None, before.state};
        };
        const auto response = api::ApiJsonCodec::invoke(fixture.service, name, params,
                                                        api::FileAccess::External, guard);
        REQUIRE(response["error"].toObject()["data"].toObject()["code"] == "CANCELLED");
        REQUIRE(guardCalls == 1);
        unchanged(fixture, before);
        REQUIRE(fixture.model.scene()->find(camera)->camera == beforeCamera);
        REQUIRE(fixture.model.scene()->find(light)->light == beforeLight);
        REQUIRE_FALSE(fixture.service.checkBeforeCommit());
    }
}

TEST_CASE("M5 no-change branches also honor final guard without dropping redo or clean point",
          "[object-api][api-commit-guard]") {
    ObjectFixture fixture;
    const auto entity = fixture.create("成员");
    const auto collection = fixture.collection();
    fixture.assign(entity, collection);
    const auto camera = fixture.camera();
    const auto light = fixture.light();
    prepareRedo(fixture, entity);
    const auto before = remember(fixture);
    int calls = 0;
    fixture.service.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        ++calls;
        return api::ApiError{
            api::ErrorCode::DeadlineExceeded, "deadline", {}, api::Recovery::None, before.state};
    });
    auto parent = fixture.mutation<api::EntitySetParentRequest>();
    parent.entityId = entity;
    REQUIRE(fixture.service.setParent(parent).error->code == api::ErrorCode::DeadlineExceeded);
    auto update = fixture.mutation<api::CollectionUpdateRequest>();
    update.collectionId = collection;
    update.visible = true;
    REQUIRE(fixture.service.updateCollection(update).error->code ==
            api::ErrorCode::DeadlineExceeded);
    auto assign = fixture.mutation<api::CollectionAssignRequest>();
    assign.entityId = entity;
    assign.collectionId = collection;
    REQUIRE(fixture.service.assignCollection(assign).error->code ==
            api::ErrorCode::DeadlineExceeded);
    auto updateCamera = fixture.mutation<api::CameraUpdateRequest>();
    updateCamera.entityId = camera;
    REQUIRE(fixture.service.updateCamera(updateCamera).error->code ==
            api::ErrorCode::DeadlineExceeded);
    auto updateLight = fixture.mutation<api::LightUpdateRequest>();
    updateLight.entityId = light;
    REQUIRE(fixture.service.updateLight(updateLight).error->code ==
            api::ErrorCode::DeadlineExceeded);
    REQUIRE(calls == 5);
    unchanged(fixture, before);
    fixture.service.exchangeBeforeCommitGuard({});
}

TEST_CASE("M5 Busy rejects all methods and preserves active object preview", "[object-api]") {
    ObjectFixture fixture;
    const auto entity = fixture.create("预览对象");
    const auto collection = fixture.collection();
    const auto camera = fixture.camera();
    const auto light = fixture.light();
    fixture.model.selection()->setSelectedEntity(entity);
    fixture.model.beginTransformEdit(entity);
    auto preview = fixture.model.scene()->find(entity)->transform;
    preview.position = {9, 8, 7};
    fixture.model.previewTransform(preview);
    const auto before = remember(fixture);
    auto duplicate = fixture.mutation<api::EntityDuplicateRequest>();
    duplicate.entityId = entity;
    REQUIRE(fixture.service.duplicateEntity(duplicate).error->code == api::ErrorCode::Busy);
    auto remove = fixture.mutation<api::EntityDeleteRequest>();
    remove.entityId = entity;
    REQUIRE(fixture.service.deleteEntity(remove).error->code == api::ErrorCode::Busy);
    auto parent = fixture.mutation<api::EntitySetParentRequest>();
    parent.entityId = entity;
    REQUIRE(fixture.service.setParent(parent).error->code == api::ErrorCode::Busy);
    auto createCollection = fixture.mutation<api::CollectionCreateRequest>();
    createCollection.name = "忙时集合";
    REQUIRE(fixture.service.createCollection(createCollection).error->code == api::ErrorCode::Busy);
    auto updateCollection = fixture.mutation<api::CollectionUpdateRequest>();
    updateCollection.collectionId = collection;
    updateCollection.visible = false;
    REQUIRE(fixture.service.updateCollection(updateCollection).error->code == api::ErrorCode::Busy);
    auto deleteCollection = fixture.mutation<api::CollectionDeleteRequest>();
    deleteCollection.collectionId = collection;
    REQUIRE(fixture.service.deleteCollection(deleteCollection).error->code == api::ErrorCode::Busy);
    auto assign = fixture.mutation<api::CollectionAssignRequest>();
    assign.entityId = entity;
    assign.collectionId = collection;
    REQUIRE(fixture.service.assignCollection(assign).error->code == api::ErrorCode::Busy);
    auto createCamera = fixture.mutation<api::CameraCreateRequest>();
    createCamera.name = "忙时相机";
    REQUIRE(fixture.service.createCamera(createCamera).error->code == api::ErrorCode::Busy);
    auto updateCamera = fixture.mutation<api::CameraUpdateRequest>();
    updateCamera.entityId = camera;
    REQUIRE(fixture.service.updateCamera(updateCamera).error->code == api::ErrorCode::Busy);
    auto createLight = fixture.mutation<api::LightCreateRequest>();
    createLight.name = "忙时灯";
    REQUIRE(fixture.service.createLight(createLight).error->code == api::ErrorCode::Busy);
    auto updateLight = fixture.mutation<api::LightUpdateRequest>();
    updateLight.entityId = light;
    REQUIRE(fixture.service.updateLight(updateLight).error->code == api::ErrorCode::Busy);
    unchanged(fixture, before);
    REQUIRE(fixture.model.scene()->find(entity)->transform.position == preview.position);
    REQUIRE(fixture.model.apiBusyReasons().contains("object_transform"));
    fixture.model.cancelTransformEdit();
    REQUIRE(fixture.model.scene()->find(entity)->transform.position == glm::vec3(0));
}

TEST_CASE("M5 direct subtree mutations reject more than 2048 nodes before final guard",
          "[object-api]") {
    ObjectFixture fixture;
    auto scene = std::const_pointer_cast<core::Scene>(fixture.model.scene());
    const auto root = scene->createEntity("大子树");
    for (int index = 0; index < 2048; ++index)
        REQUIRE(scene->createEntity("节点", root) != 0);
    const auto before = remember(fixture);
    int calls = 0;
    fixture.service.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        ++calls;
        return std::nullopt;
    });
    auto duplicate = fixture.mutation<api::EntityDuplicateRequest>();
    duplicate.entityId = root;
    REQUIRE(fixture.service.duplicateEntity(duplicate).error->code ==
            api::ErrorCode::LimitExceeded);
    auto remove = fixture.mutation<api::EntityDeleteRequest>();
    remove.entityId = root;
    REQUIRE(fixture.service.deleteEntity(remove).error->code == api::ErrorCode::LimitExceeded);
    REQUIRE(calls == 0);
    unchanged(fixture, before);
    fixture.service.exchangeBeforeCommitGuard({});
}

TEST_CASE("M5 strict camera numeric boundaries reject rounding overflow and unknown device fields",
          "[object-api]") {
    ObjectFixture fixture;
    for (const int failure : {0, 1, 2, 3, 4, 5}) {
        auto params = fixture.wire();
        params.insert("entityId", "1");
        auto camera = cameraJson();
        CAPTURE(failure);
        switch (failure) {
            case 0:
                camera.insert("fieldOfView", 179.00000001);
                break;
            case 1:
                camera.insert("nearPlane", 0.00099999999);
                break;
            case 2:
                camera.insert("farPlane", 1000000.00001);
                break;
            case 3:
                camera.insert("nearPlane", 1.0);
                camera.insert("farPlane", 1.00000001);
                break;
            case 4:
                camera.insert("nearPlane", true);
                break;
            case 5:
                camera.insert("unsupported", true);
                break;
        }
        params.insert("camera", camera);
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("camera.update", params).hasValue());
    }
    auto params = fixture.wire();
    params.insert("entityId", "1");
    params.insert("light", QJsonObject{{"color", QJsonArray{1.00000001, 1, 1}}, {"intensity", 1}});
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("light.update", params).hasValue());
    params.insert("light", QJsonObject{{"color", QJsonArray{1, 1, 1}}, {"intensity", 10.00000001}});
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("light.update", params).hasValue());
    params.insert("visible", false);
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("light.update", params).hasValue());
}

TEST_CASE(
    "Every M5 direct typed mutation rejects invalid envelope without changing confirmed state",
    "[object-api][object-api-envelope]") {
    QFile file(QDir(QString::fromUtf8(MINI3D_API_SCHEMA_DIRECTORY)).filePath("m5-examples.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto examples = QJsonDocument::fromJson(file.readAll()).object()["valid"].toArray();
    REQUIRE(examples.size() == 11);
    for (const auto& item : examples) {
        const auto sample = item.toObject();
        const auto method = sample["method"].toString();
        for (int invalidEnvelope = 0; invalidEnvelope < 4; ++invalidEnvelope) {
            DYNAMIC_SECTION(method.toStdString() << " invalid envelope=" << invalidEnvelope) {
                ObjectFixture fixture;
                const auto root = fixture.create("根");
                const auto child = fixture.create("子", root);
                const auto other = fixture.create("保留选区对象");
                const auto collection = fixture.collection();
                fixture.assign(child, collection);
                const auto camera = fixture.camera();
                const auto light = fixture.light();
                fixture.model.selection()->setSelectedEntity(child);
                prepareRedo(fixture, other);
                const auto before = remember(fixture);
                const auto beforeCamera = *fixture.model.scene()->find(camera)->camera;
                const auto beforeLight = *fixture.model.scene()->find(light)->light;
                auto params = sample["value"].toObject();
                const auto context = fixture.wire();
                params.insert("document", context["document"]);
                params.insert("expectedDocumentRevision", context["expectedDocumentRevision"]);
                if (params.contains("entityId"))
                    params.insert("entityId",
                                  QString::number(method == "camera.update"       ? camera
                                                  : method == "light.update"      ? light
                                                  : method == "collection.assign" ? other
                                                                                  : root));
                if (params.contains("collectionId"))
                    params.insert("collectionId", QString::number(collection));
                if (method == "entity.setParent")
                    params.insert("parentId", QString::number(other));
                if (method == "camera.update")
                    params.insert("camera", cameraJson(60));
                const auto decoded = api::ApiJsonCodec::decodeRequest(method, params);
                REQUIRE(decoded.hasValue());
                auto typed = *decoded.value;
                // 在合法解码之后注入无效元数据，确保这里只测试 direct typed 业务入口。
                std::visit(
                    [invalidEnvelope](auto& request) {
                        using Request = std::decay_t<decltype(request)>;
                        if constexpr (std::is_base_of_v<api::MutationRequest, Request>) {
                            switch (invalidEnvelope) {
                                case 0:
                                    request.timeoutMs = 0;
                                    break;
                                case 1:
                                    request.timeoutMs = 30001;
                                    break;
                                case 2:
                                    request.mutationSequence = 0;
                                    break;
                                case 3:
                                    request.clientSessionId = QStringLiteral("invalid-session-id");
                                    break;
                            }
                        }
                    },
                    typed);
                const auto expectedField = invalidEnvelope < 2 ? QStringLiteral("timeoutMs")
                                           : invalidEnvelope == 2
                                               ? QStringLiteral("mutationSequence")
                                               : QStringLiteral("clientSessionId");
                const auto reject = [&](const auto& result) {
                    REQUIRE_FALSE(result.hasValue());
                    REQUIRE(result.error->code == api::ErrorCode::InvalidArgument);
                    REQUIRE(result.error->fieldPath == expectedField);
                };
                std::visit(
                    [&](const auto& request) {
                        using Request = std::decay_t<decltype(request)>;
                        if constexpr (std::is_same_v<Request, api::EntityDuplicateRequest>)
                            reject(fixture.service.duplicateEntity(request));
                        else if constexpr (std::is_same_v<Request, api::EntityDeleteRequest>)
                            reject(fixture.service.deleteEntity(request));
                        else if constexpr (std::is_same_v<Request, api::EntitySetParentRequest>)
                            reject(fixture.service.setParent(request));
                        else if constexpr (std::is_same_v<Request, api::CollectionCreateRequest>)
                            reject(fixture.service.createCollection(request));
                        else if constexpr (std::is_same_v<Request, api::CollectionUpdateRequest>)
                            reject(fixture.service.updateCollection(request));
                        else if constexpr (std::is_same_v<Request, api::CollectionDeleteRequest>)
                            reject(fixture.service.deleteCollection(request));
                        else if constexpr (std::is_same_v<Request, api::CollectionAssignRequest>)
                            reject(fixture.service.assignCollection(request));
                        else if constexpr (std::is_same_v<Request, api::CameraCreateRequest>)
                            reject(fixture.service.createCamera(request));
                        else if constexpr (std::is_same_v<Request, api::CameraUpdateRequest>)
                            reject(fixture.service.updateCamera(request));
                        else if constexpr (std::is_same_v<Request, api::LightCreateRequest>)
                            reject(fixture.service.createLight(request));
                        else if constexpr (std::is_same_v<Request, api::LightUpdateRequest>)
                            reject(fixture.service.updateLight(request));
                        else
                            FAIL("Unexpected M5 typed request");
                    },
                    typed);
                unchanged(fixture, before);
                REQUIRE(fixture.model.scene()->find(root)->parent == 0);
                REQUIRE(fixture.model.scene()->find(child)->parent == root);
                REQUIRE(fixture.model.scene()->find(camera)->camera == beforeCamera);
                REQUIRE(fixture.model.scene()->find(light)->light == beforeLight);
            }
        }
    }
}
