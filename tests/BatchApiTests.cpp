/*
 * 模块名: BatchApiTests
 * 功能概述: 验证 M6 有界同类批次的冻结边界、整组发布和唯一共享历史。
 * 对外接口: Catch2 [batch-api] 用例，无独立 main 或网络监听。
 * 依赖关系: SceneViewModel、EditorApiService、ApiJsonCodec、正式 M6 JSON 样例。
 * 输入输出: typed/wire 请求到完整场景、版本、保存点、选区与通知断言。
 * 异常与错误: 后项非法、最终许可及来源拒绝均不得发布部分结果或改变历史。
 * 维护说明: 夹具直改 Core 仅模拟来源失效；正式验证不增加生产故障注入钩子。
 */
#include "editor/SceneViewModel.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <thread>

using namespace mini3d;
namespace {
namespace api = editor::api;

std::string encodedScene(const editor::SceneViewModel& model) {
    core::SceneDocumentData data;
    data.nodes = model.scene()->nodes();
    data.editableMeshes = model.scene()->editableMeshes();
    data.collections = model.scene()->collections();
    data.lighting = model.scene()->lighting();
    data.camera = model.editorCamera();
    data.cursor = model.cursor3D();
    return core::SceneSerializer::encode(data);
}
struct BatchFixture {
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    BatchFixture() {
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
    api::EntityGetRequest entity(core::EntityId id) const {
        api::EntityGetRequest request;
        request.document = service.documentState().document;
        request.entityId = id;
        return request;
    }
    core::EntityId create(const QString& name, core::EntityId parent = 0,
                          core::PrimitiveKind primitive = core::PrimitiveKind::Empty) {
        auto request = mutation<api::EntityCreateRequest>();
        request.name = name;
        request.parentId = parent;
        request.primitive = primitive;
        const auto result = service.createEntity(request);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    api::BatchCreateEntitiesRequest creates(std::size_t count = 2,
                                            core::EntityId parent = 0) const {
        auto request = mutation<api::BatchCreateEntitiesRequest>();
        for (std::size_t index = 0; index < count; ++index) {
            api::BatchEntityCreateItem item;
            item.name = QStringLiteral("批次对象 %1").arg(index);
            item.parentId = parent;
            item.primitive = core::PrimitiveKind::Cube;
            request.items.push_back(std::move(item));
        }
        return request;
    }
    api::BatchSetTransformsRequest transforms(const std::vector<core::EntityId>& ids) const {
        auto request = mutation<api::BatchSetTransformsRequest>();
        for (const auto id : ids)
            request.items.push_back({id, model.scene()->find(id)->transform});
        return request;
    }
    QJsonObject wire() const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        return {{"document", state["document"]},
                {"expectedDocumentRevision", state["documentRevision"]}};
    }
};
QJsonObject transformJson(const core::Transform& value = {}) {
    return {{"space", "local"},
            {"translation", QJsonArray{value.position.x, value.position.y, value.position.z}},
            {"rotationQuaternion",
             QJsonArray{value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w}},
            {"scale", QJsonArray{value.scale.x, value.scale.y, value.scale.z}}};
}
QJsonObject createItemJson(const QString& name = QStringLiteral("wire 对象")) {
    return {{"primitive", "cube"},
            {"name", name},
            {"parentId", "0"},
            {"transform", transformJson()},
            {"surface", QJsonObject{{"tint", QJsonArray{0.2, 0.3, 0.4}},
                                    {"useVertexColor", false},
                                    {"useTexture", true}}}};
}
QJsonObject transformParams(BatchFixture& fixture, const api::BatchSetTransformsRequest& request) {
    auto params = fixture.wire();
    QJsonArray items;
    for (const auto& item : request.items)
        items.append(QJsonObject{{"entityId", QString::number(item.entityId)},
                                 {"transform", transformJson(item.transform)}});
    params.insert("items", items);
    return params;
}
struct Remembered {
    api::DocumentState state;
    std::shared_ptr<const core::Scene> scene;
    std::string encoded;
    int count, index, clean;
    bool modified, isClean;
    const QUndoCommand* redo;
    core::EntityId selection;
    QString path;
    bool requiresSaveAs;
};
Remembered remember(BatchFixture& fixture) {
    const auto* stack = fixture.model.undoStack();
    return {fixture.service.documentState(),
            fixture.model.scene(),
            encodedScene(fixture.model),
            stack->count(),
            stack->index(),
            stack->cleanIndex(),
            fixture.model.isModified(),
            stack->isClean(),
            stack->canRedo() ? stack->command(stack->index()) : nullptr,
            fixture.model.selection()->selectedEntity(),
            fixture.model.filePath(),
            fixture.model.requiresSaveAs()};
}
void unchanged(BatchFixture& fixture, const Remembered& before) {
    const auto& state = fixture.service.documentState();
    const auto* stack = fixture.model.undoStack();
    REQUIRE(state.document == before.state.document);
    REQUIRE(state.documentRevision == before.state.documentRevision);
    REQUIRE(state.historyRevision == before.state.historyRevision);
    REQUIRE(fixture.model.scene() == before.scene);
    REQUIRE(encodedScene(fixture.model) == before.encoded);
    REQUIRE(stack->count() == before.count);
    REQUIRE(stack->index() == before.index);
    REQUIRE(stack->cleanIndex() == before.clean);
    REQUIRE(stack->isClean() == before.isClean);
    REQUIRE(stack->canRedo() == (before.redo != nullptr));
    if (before.redo)
        REQUIRE(stack->command(before.index) == before.redo);
    REQUIRE(fixture.model.isModified() == before.modified);
    REQUIRE(fixture.model.selection()->selectedEntity() == before.selection);
    REQUIRE(fixture.model.filePath() == before.path);
    REQUIRE(fixture.model.requiresSaveAs() == before.requiresSaveAs);
}
void prepareRedo(BatchFixture& fixture, core::EntityId selected, bool dirty = false) {
    fixture.model.selection()->setSelectedEntity(selected);
    const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
    if (dirty)
        REQUIRE(fixture.model.renameEntity(selected, QStringLiteral("保留的脏状态")));
    REQUIRE(fixture.model.renameEntity(selected, QStringLiteral("待重做名称")));
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.undoStack()->canRedo());
    REQUIRE(fixture.model.isModified() == dirty);
}
struct Notifications {
    QObject context;
    int about = 0, structure = 0, scene = 0, state = 0;
    std::vector<core::EntityId> entities;
    explicit Notifications(editor::SceneViewModel& model) {
        QObject::connect(&model, &editor::SceneViewModel::structureAboutToChange, &context, [this] {
            ++about;
        });
        QObject::connect(&model, &editor::SceneViewModel::structureChanged, &context, [this] {
            ++structure;
        });
        QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &context, [this] {
            ++scene;
        });
        QObject::connect(&model, &editor::SceneViewModel::apiStateChanged, &context, [this] {
            ++state;
        });
        QObject::connect(&model, &editor::SceneViewModel::entityChanged, &context,
                         [this](core::EntityId id) {
                             entities.push_back(id);
                         });
    }
    void none() const {
        REQUIRE(about == 0);
        REQUIRE(structure == 0);
        REQUIRE(scene == 0);
        REQUIRE(state == 0);
        REQUIRE(entities.empty());
    }
};
} // namespace

TEST_CASE("M6 codec consumes frozen samples and describes all 61 authorized methods",
          "[batch-api][api-canonical]") {
    const auto directory = QDir(QString::fromUtf8(MINI3D_API_SCHEMA_DIRECTORY));
    QFile file(directory.filePath("m6-examples.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto examples = QJsonDocument::fromJson(file.readAll(), &error).object();
    REQUIRE(error.error == QJsonParseError::NoError);
    REQUIRE(examples["valid"].toArray().size() == 4);
    REQUIRE(examples["invalid"].toArray().size() == 19);
    for (const auto& category : {"valid", "invalid"}) {
        const bool expected = QString::fromLatin1(category) == "valid";
        for (const auto& value : examples[category].toArray()) {
            const auto sample = value.toObject();
            const auto method = sample["method"].toString();
            CAPTURE(category, method);
            REQUIRE(api::ApiJsonCodec::decodeRequest(method, sample["params"]).hasValue() ==
                    expected);
            const auto canonical = api::ApiJsonCodec::canonicalParams(method, sample["params"]);
            REQUIRE(canonical.hasValue() == expected);
            if (!expected)
                continue;
            REQUIRE(api::ApiJsonCodec::canonicalParams(method, *canonical.value).value ==
                    canonical.value);
            const auto originalItems = sample["params"].toObject()["items"].toArray();
            const auto canonicalItems = (*canonical.value)["items"].toArray();
            REQUIRE(canonicalItems.size() == originalItems.size());
            for (qsizetype index = 0; index < originalItems.size(); ++index) {
                const auto original = originalItems[index].toObject();
                const auto normalized = canonicalItems[index].toObject();
                for (const auto& key : original.keys())
                    REQUIRE(normalized.contains(key));
                for (const auto& key : {"name", "primitive", "parentId", "entityId"})
                    if (original.contains(key))
                        REQUIRE(normalized[key] == original[key]);
            }
            auto metadata = sample["params"].toObject();
            metadata.insert("clientSessionId", "AAAAAAAA-AAAA-4AAA-8AAA-AAAAAAAAAAAA");
            metadata.insert("mutationSequence", "18446744073709551615");
            REQUIRE(api::ApiJsonCodec::canonicalParams(method, metadata).value == canonical.value);
            metadata.insert("timeoutMs", 1);
            REQUIRE(api::ApiJsonCodec::canonicalParams(method, metadata).value != canonical.value);
        }
    }
    BatchFixture fixture;
    fixture.service.setObservationAvailable(true);
    const auto description = fixture.service.describe();
    REQUIRE(description.hasValue());
    REQUIRE(description.value->methods.size() == 61);
    REQUIRE(description.value->limits.at("batchItems") == 64);
    REQUIRE(description.value->limits.at("candidateBytes") == 64 * 1024 * 1024);
    QFile methods(directory.filePath("methods.json"));
    REQUIRE(methods.open(QIODevice::ReadOnly));
    const auto contract = QJsonDocument::fromJson(methods.readAll()).object()["methods"].toArray();
    REQUIRE(contract.size() == 61);
    for (const auto& value : contract) {
        const auto method = value.toObject();
        const auto found = std::find_if(description.value->methods.begin(),
                                        description.value->methods.end(), [&](const auto& entry) {
                                            return entry.name == method["name"].toString();
                                        });
        REQUIRE(found != description.value->methods.end());
        REQUIRE(found->kind == method["kind"].toString());
        REQUIRE(found->permission == method["permission"].toString());
    }
    auto params = fixture.wire();
    params.insert("items", QJsonArray{QJsonObject{{"entityId", "9007199254740993"},
                                                  {"transform", transformJson()}}});
    const auto decoded = api::ApiJsonCodec::decodeRequest("batch.setTransforms", params);
    REQUIRE(decoded.hasValue());
    REQUIRE(std::get<api::BatchSetTransformsRequest>(*decoded.value).items.front().entityId ==
            9007199254740993ULL);
    REQUIRE((*api::ApiJsonCodec::canonicalParams("batch.setTransforms", params).value)["items"] ==
            params["items"]);
}

TEST_CASE("M6 strict item paths and canonical order preserve all business inputs", "[batch-api]") {
    BatchFixture fixture;
    auto params = fixture.wire();
    params.insert("items", QJsonArray{createItemJson("first"), createItemJson("second")});
    const auto canonical = api::ApiJsonCodec::canonicalParams("batch.createEntities", params);
    REQUIRE(canonical.hasValue());
    for (int change = 0; change < 6; ++change) {
        auto changed = params;
        auto items = changed["items"].toArray();
        auto second = items[1].toObject();
        switch (change) {
            case 0:
                second.insert("name", "different");
                break;
            case 1:
                second.insert("parentId", "5");
                break;
            case 2:
                second.insert("primitive", "sphere");
                break;
            case 3: {
                auto transform = transformJson();
                transform.insert("translation", QJsonArray{1, 2, 3});
                second.insert("transform", transform);
                break;
            }
            case 4: {
                auto surface = second["surface"].toObject();
                surface.insert("useTexture", false);
                second.insert("surface", surface);
                break;
            }
            case 5: {
                const auto first = items[0].toObject();
                items[0] = items[1].toObject();
                items[1] = first;
                break;
            }
        }
        if (change != 5)
            items[1] = second;
        changed.insert("items", items);
        const auto digest = api::ApiJsonCodec::canonicalParams("batch.createEntities", changed);
        REQUIRE(digest.hasValue());
        REQUIRE(digest.value != canonical.value);
    }
    for (int failure = 0; failure < 4; ++failure) {
        auto bad = params;
        auto items = bad["items"].toArray();
        auto second = items[1].toObject();
        QString field;
        if (failure == 0) {
            second.insert("extra", true);
            field = "items[1].extra";
        } else if (failure == 1) {
            auto transform = transformJson();
            transform.insert("translation", QJsonArray{1e100, 0, 0});
            second.insert("transform", transform);
            field = "items[1].transform.translation[0]";
        } else if (failure == 2) {
            auto surface = second["surface"].toObject();
            surface.insert("tint", QJsonArray{1.00000001, 0, 0});
            second.insert("surface", surface);
            field = "items[1].surface.tint[0]";
        } else {
            second.insert("clientSessionId", "11111111-1111-4111-8111-111111111111");
            field = "items[1].clientSessionId";
        }
        items[1] = second;
        bad.insert("items", items);
        const auto rejected = api::ApiJsonCodec::decodeRequest("batch.createEntities", bad);
        REQUIRE_FALSE(rejected.hasValue());
        REQUIRE(rejected.error->fieldPath == field);
    }
}

TEST_CASE("Complete create batches notify only complete structure and replay as one command",
          "[batch-api]") {
    BatchFixture fixture;
    const auto parent = fixture.create("已有父");
    const auto tail = fixture.create("原有兄弟", parent);
    const auto selected = fixture.create("保留选区");
    fixture.model.selection()->setSelectedEntity(selected);
    const auto before = remember(fixture);
    auto request = fixture.creates(4, parent);
    const core::PrimitiveKind kinds[]{core::PrimitiveKind::Empty, core::PrimitiveKind::Cube,
                                      core::PrimitiveKind::Sphere, core::PrimitiveKind::Plane};
    for (std::size_t index = 0; index < request.items.size(); ++index) {
        auto& item = request.items[index];
        item.primitive = kinds[index];
        item.transform.position = {float(index), -2, 3};
        item.transform.scale = {-2, 3, 0.5F};
        item.transform.rotation = {2, 0, 0, 0};
        item.surface = {{0.2F, 0.3F, 0.4F}, false, true};
    }
    Notifications notifications(fixture.model);
    int reentries = 0;
    QObject::connect(&fixture.model, &editor::SceneViewModel::structureAboutToChange,
                     &notifications.context, [&] {
                         const auto nested =
                             fixture.service.createEntities(fixture.creates(2, parent));
                         REQUIRE_FALSE(nested.hasValue());
                         REQUIRE(nested.error->code == api::ErrorCode::Busy);
                         ++reentries;
                     });
    QObject::connect(
        &fixture.model, &editor::SceneViewModel::structureChanged, &notifications.context, [&] {
            REQUIRE(fixture.model.scene()->nodes().size() ==
                    (fixture.model.scene()->find(parent)->children.size() == 1 ? 3 : 7));
        });
    const auto result = fixture.service.createEntities(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->createdEntityIds.size() == 4);
    REQUIRE(result.value->affectedEntityIds == result.value->createdEntityIds);
    REQUIRE(result.value->status == api::ResultStatus::Committed);
    REQUIRE(result.value->undoable);
    REQUIRE_FALSE(result.value->selectionChanged);
    REQUIRE(fixture.model.scene() == before.scene);
    REQUIRE(fixture.model.undoStack()->count() == before.count + 1);
    REQUIRE(fixture.model.undoStack()->command(before.index)->childCount() == 0);
    REQUIRE(result.value->state.documentRevision == before.state.documentRevision + 1);
    REQUIRE(result.value->state.historyRevision == before.state.historyRevision + 1);
    REQUIRE(notifications.about == 1);
    REQUIRE(notifications.structure == 1);
    REQUIRE(notifications.scene == 1);
    REQUIRE(notifications.entities.empty());
    REQUIRE(reentries == 1);
    const auto ids = result.value->createdEntityIds;
    REQUIRE(fixture.model.scene()->find(parent)->children ==
            std::vector<core::EntityId>{tail, ids[0], ids[1], ids[2], ids[3]});
    for (std::size_t index = 0; index < ids.size(); ++index) {
        const auto* node = fixture.model.scene()->find(ids[index]);
        REQUIRE(node);
        REQUIRE(node->name == request.items[index].name.toUtf8().toStdString());
        REQUIRE(node->primitive == kinds[index]);
        REQUIRE(node->parent == parent);
        REQUIRE(node->transform.position == request.items[index].transform.position);
        REQUIRE(node->transform.scale == request.items[index].transform.scale);
        REQUIRE(node->transform.rotation == glm::quat(1, 0, 0, 0));
        REQUIRE(node->surface == request.items[index].surface);
    }
    const auto after = encodedScene(fixture.model);
    for (int cycle = 0; cycle < 3; ++cycle) {
        const auto undo = fixture.service.undo(fixture.history());
        REQUIRE(undo.hasValue());
        REQUIRE(encodedScene(fixture.model) == before.encoded);
        REQUIRE(fixture.model.undoStack()->index() == before.index);
        REQUIRE(fixture.model.selection()->selectedEntity() == selected);
        const auto redo = fixture.service.redo(fixture.history());
        REQUIRE(redo.hasValue());
        REQUIRE(encodedScene(fixture.model) == after);
        REQUIRE(fixture.model.undoStack()->index() == before.index + 1);
        REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    }
}

TEST_CASE("Wire batches return flat command results and compose through returned identities",
          "[batch-api]") {
    BatchFixture fixture;
    auto params = fixture.wire();
    params.insert("items", QJsonArray{createItemJson("first"), createItemJson("second")});
    const auto created = api::ApiJsonCodec::invoke(fixture.service, "batch.createEntities", params);
    REQUIRE(created.contains("result"));
    const auto result = created["result"].toObject();
    REQUIRE_FALSE(result.contains("command"));
    REQUIRE(result["status"] == "committed");
    REQUIRE(result["selectionChanged"] == false);
    const auto ids = result["created"].toObject()["entityIds"].toArray();
    REQUIRE(ids.size() == 2);
    REQUIRE(ids[0].isString());
    REQUIRE(result["affectedEntityIds"] == ids);
    params = fixture.wire();
    core::Transform first, second;
    first.position = {3, 4, 5};
    second.position = {-3, -4, -5};
    params.insert(
        "items",
        QJsonArray{QJsonObject{{"entityId", ids[1]}, {"transform", transformJson(second)}},
                   QJsonObject{{"entityId", ids[0]}, {"transform", transformJson(first)}}});
    const auto changed = api::ApiJsonCodec::invoke(fixture.service, "batch.setTransforms", params);
    REQUIRE(changed.contains("result"));
    const auto command = changed["result"].toObject();
    REQUIRE_FALSE(command.contains("command"));
    REQUIRE(command["status"] == "committed");
    REQUIRE(command["created"].toObject()["entityIds"].toArray().isEmpty());
    REQUIRE(command["affectedEntityIds"] == QJsonArray{ids[1], ids[0]});
    REQUIRE(fixture.model.undoStack()->count() == 2);
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->nodes().empty());
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->nodes().size() == 2);
}

TEST_CASE("Typed and wire create transform batches enforce zero 64 and 65 item boundaries",
          "[batch-api]") {
    for (const auto count : {0, 64, 65}) {
        DYNAMIC_SECTION("create items=" << count) {
            BatchFixture fixture;
            const auto before = remember(fixture);
            Notifications notifications(fixture.model);
            const auto result = fixture.service.createEntities(fixture.creates(std::size_t(count)));
            REQUIRE(result.hasValue() == (count == 64));
            if (count == 64) {
                REQUIRE(result.value->createdEntityIds.size() == 64);
                REQUIRE(fixture.model.scene()->nodes().size() == 64);
                REQUIRE(fixture.model.undoStack()->count() == 1);
            } else {
                REQUIRE(result.error->code == (count == 0 ? api::ErrorCode::InvalidArgument
                                                          : api::ErrorCode::LimitExceeded));
                unchanged(fixture, before);
                notifications.none();
            }
            auto params = fixture.wire();
            QJsonArray items;
            for (int index = 0; index < count; ++index)
                items.append(createItemJson());
            params.insert("items", items);
            REQUIRE(api::ApiJsonCodec::decodeRequest("batch.createEntities", params).hasValue() ==
                    (count == 64));
        }
        DYNAMIC_SECTION("transform items=" << count) {
            BatchFixture fixture;
            const auto created = fixture.service.createEntities(fixture.creates(64));
            REQUIRE(created.hasValue());
            auto request = fixture.transforms(created.value->createdEntityIds);
            if (count == 0)
                request.items.clear();
            else if (count == 65)
                request.items.push_back(request.items.front());
            for (auto& item : request.items)
                item.transform.position.x = 1;
            const auto before = remember(fixture);
            Notifications notifications(fixture.model);
            const auto result = fixture.service.setTransforms(request);
            REQUIRE(result.hasValue() == (count == 64));
            if (count == 64) {
                REQUIRE(result.value->affectedEntityIds.size() == 64);
                REQUIRE(fixture.model.undoStack()->count() == 2);
            } else {
                REQUIRE(result.error->code == (count == 0 ? api::ErrorCode::InvalidArgument
                                                          : api::ErrorCode::LimitExceeded));
                unchanged(fixture, before);
                notifications.none();
            }
            REQUIRE(api::ApiJsonCodec::decodeRequest("batch.setTransforms",
                                                     transformParams(fixture, request))
                        .hasValue() == (count == 64));
        }
    }
}

TEST_CASE("Invalid later create item preserves dirty redo clean point versions and selection",
          "[batch-api]") {
    for (int failure = 0; failure < 8; ++failure) {
        DYNAMIC_SECTION("failure=" << failure) {
            BatchFixture fixture;
            const auto selected = fixture.create("用户对象");
            prepareRedo(fixture, selected, failure % 2 != 0);
            auto request = fixture.creates();
            auto& last = request.items.back();
            auto expected = api::ErrorCode::InvalidArgument;
            switch (failure) {
                case 0:
                    last.parentId = 999;
                    expected = api::ErrorCode::NotFound;
                    break;
                case 1:
                    last.name = " \t ";
                    break;
                case 2:
                    last.name = QString::fromUcs4(U"\U0001f603").repeated(257);
                    break;
                case 3:
                    last.primitive = static_cast<core::PrimitiveKind>(999);
                    break;
                case 4:
                    last.transform.scale.x = 0;
                    break;
                case 5:
                    last.transform.rotation = {0, 0, 0, 0};
                    break;
                case 6:
                    last.surface.tint.x = 1.1F;
                    break;
                case 7:
                    last.parentId = selected + 1;
                    expected = api::ErrorCode::NotFound;
                    break;
            }
            const auto before = remember(fixture);
            Notifications notifications(fixture.model);
            const auto result = fixture.service.createEntities(request);
            REQUIRE_FALSE(result.hasValue());
            REQUIRE(result.error->code == expected);
            REQUIRE(result.error->fieldPath.startsWith("items[1]."));
            unchanged(fixture, before);
            notifications.none();
        }
    }
}

TEST_CASE("Invalid later transform and duplicate target are domain refusals with zero publication",
          "[batch-api]") {
    for (int failure = 0; failure < 6; ++failure) {
        DYNAMIC_SECTION("failure=" << failure) {
            BatchFixture fixture;
            const auto first = fixture.create("first");
            const auto second = fixture.create("second");
            prepareRedo(fixture, second, failure % 2 != 0);
            auto request = fixture.transforms({first, second});
            request.items.front().transform.position.x = 5;
            auto& last = request.items.back();
            auto expected = api::ErrorCode::InvalidArgument;
            switch (failure) {
                case 0:
                    last.entityId = first;
                    // wire 保留合法形状；目标唯一性由本体拒绝。
                    REQUIRE(api::ApiJsonCodec::decodeRequest("batch.setTransforms",
                                                             transformParams(fixture, request))
                                .hasValue());
                    break;
                case 1:
                    last.entityId = 0;
                    break;
                case 2:
                    last.entityId = 999;
                    expected = api::ErrorCode::NotFound;
                    break;
                case 3:
                    last.transform.scale.y = 0.0001F;
                    break;
                case 4:
                    last.transform.rotation = {0, 0, 0, 0};
                    break;
                case 5:
                    last.transform.position.z = std::numeric_limits<float>::infinity();
                    break;
            }
            const auto before = remember(fixture);
            Notifications notifications(fixture.model);
            const auto result = fixture.service.setTransforms(request);
            REQUIRE_FALSE(result.hasValue());
            REQUIRE(result.error->code == expected);
            unchanged(fixture, before);
            notifications.none();
        }
    }
}

TEST_CASE("Final permission refusal protects both batch histories including no change",
          "[batch-api]") {
    for (int operation = 0; operation < 3; ++operation) {
        DYNAMIC_SECTION("operation=" << operation) {
            BatchFixture fixture;
            const auto target = fixture.create("target");
            prepareRedo(fixture, target, operation == 1);
            const auto before = remember(fixture);
            Notifications notifications(fixture.model);
            int guards = 0;
            auto previous = fixture.service.exchangeBeforeCommitGuard([&] {
                ++guards;
                return std::optional<api::ApiError>{{api::ErrorCode::PermissionDenied,
                                                     QStringLiteral("最终提交许可拒绝"),
                                                     {},
                                                     api::Recovery::None,
                                                     fixture.service.documentState()}};
            });
            const auto restore = qScopeGuard([&] {
                fixture.service.exchangeBeforeCommitGuard(std::move(previous));
            });
            auto request = fixture.transforms({target});
            if (operation == 1)
                request.items.front().transform.position.x = 3;
            const auto result = operation == 0 ? fixture.service.createEntities(fixture.creates())
                                               : fixture.service.setTransforms(request);
            REQUIRE_FALSE(result.hasValue());
            REQUIRE(result.error->code == api::ErrorCode::PermissionDenied);
            REQUIRE(guards == 1);
            unchanged(fixture, before);
            notifications.none();
        }
    }
}

TEST_CASE(
    "Post guard whole group preflight rejects missing parents changed children and mesh source",
    "[batch-api]") {
    for (int operation = 0; operation < 5; ++operation) {
        DYNAMIC_SECTION("source change=" << operation) {
            BatchFixture fixture;
            const auto parent = fixture.create("parent");
            const auto child = fixture.create("child", parent, core::PrimitiveKind::Cube);
            const auto selected = fixture.create("selected");
            REQUIRE(fixture.model.makeEditable(child));
            prepareRedo(fixture, selected);
            auto transforms = fixture.transforms({child});
            transforms.items.front().transform.position.x = 5;
            const auto creates = fixture.creates(2, parent);
            const auto scene = std::const_pointer_cast<core::Scene>(fixture.model.scene());
            std::optional<Remembered> afterGuard;
            Notifications notifications(fixture.model);
            auto previous = fixture.service.exchangeBeforeCommitGuard([&] {
                if (operation == 0) {
                    REQUIRE(scene->removeEntity(parent));
                } else if (operation == 1 || operation == 3) {
                    auto transform = scene->find(parent)->transform;
                    transform.position.y = 6;
                    REQUIRE(scene->installTransformSnapshot(parent, transform));
                } else if (operation == 2) {
                    REQUIRE(scene->createEntity("新插入兄弟", parent) != 0);
                } else {
                    const auto mesh = scene->find(child)->editableMesh;
                    auto source = scene->editableMesh(mesh)->content->source;
                    for (auto& vertex : source.vertices)
                        vertex.position.x += 0.1F;
                    std::string error;
                    const auto candidate = scene->prepareEditableGeometry(child, source, error);
                    REQUIRE(candidate);
                    REQUIRE(scene->installGeometry(*candidate));
                }
                afterGuard = remember(fixture);
                return std::optional<api::ApiError>{};
            });
            const auto restore = qScopeGuard([&] {
                fixture.service.exchangeBeforeCommitGuard(std::move(previous));
            });
            const auto result = operation < 3 ? fixture.service.createEntities(creates)
                                              : fixture.service.setTransforms(transforms);
            REQUIRE_FALSE(result.hasValue());
            REQUIRE(result.error->code == api::ErrorCode::RevisionConflict);
            REQUIRE(afterGuard);
            unchanged(fixture, *afterGuard);
            notifications.none();
        }
    }
}

TEST_CASE("Guard changing unrelated shared history invalidates both prepared batch revisions",
          "[batch-api]") {
    for (const bool create : {false, true}) {
        DYNAMIC_SECTION("create=" << create) {
            BatchFixture fixture;
            const auto target = fixture.create("target");
            const auto other = fixture.create("无关对象");
            REQUIRE(fixture.model.renameEntity(other, "guard 将撤销的无关修改"));
            auto creates = fixture.creates();
            auto transforms = fixture.transforms({target});
            transforms.items.front().transform.position.x = 3;
            std::optional<Remembered> afterGuard;
            int guards = 0;
            auto previous = fixture.service.exchangeBeforeCommitGuard([&] {
                // Undo 也执行提交守卫；只注入一次历史变化，内层仍正常授权。
                if (++guards != 1)
                    return std::optional<api::ApiError>{};
                fixture.model.undo();
                afterGuard = remember(fixture);
                return std::optional<api::ApiError>{};
            });
            const auto restore = qScopeGuard([&] {
                fixture.service.exchangeBeforeCommitGuard(std::move(previous));
            });
            const auto result = create ? fixture.service.createEntities(creates)
                                       : fixture.service.setTransforms(transforms);
            REQUIRE_FALSE(result.hasValue());
            REQUIRE(guards == 2);
            REQUIRE(result.error->code == api::ErrorCode::RevisionConflict);
            REQUIRE(result.error->fieldPath == "expectedDocumentRevision");
            REQUIRE(afterGuard);
            unchanged(fixture, *afterGuard);
        }
    }
}

TEST_CASE("Final overlay accepts parent child pairs whose single item intermediate overflows",
          "[batch-api]") {
    BatchFixture fixture;
    const auto parent = fixture.create("parent");
    const auto child = fixture.create("child", parent, core::PrimitiveKind::Cube);
    core::Transform parentBefore, childBefore;
    parentBefore.scale = {1e20F, 1, 1};
    childBefore.scale = {1e10F, 1, 1};
    REQUIRE(fixture.model.setTransform(parent, parentBefore));
    REQUIRE(fixture.model.setTransform(child, childBefore));
    auto request = fixture.transforms({child, parent});
    request.items[0].transform.scale.x = 1e20F;
    request.items[1].transform.scale.x = 1e10F;
    const auto intermediate =
        fixture.model.scene()->worldMatrix(parent) * request.items[0].transform.localMatrix();
    REQUIRE_FALSE(std::isfinite(intermediate[0][0]));
    const auto before = remember(fixture);
    Notifications notifications(fixture.model);
    const auto result = fixture.service.setTransforms(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->affectedEntityIds == std::vector<core::EntityId>{child, parent});
    REQUIRE(notifications.entities == result.value->affectedEntityIds);
    REQUIRE(notifications.scene == 1);
    REQUIRE(notifications.structure == 0);
    REQUIRE(std::isfinite(fixture.model.scene()->worldMatrix(child)[0][0]));
    const auto after = encodedScene(fixture.model);
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(fixture.service.undo(fixture.history()).hasValue());
        REQUIRE(encodedScene(fixture.model) == before.encoded);
        REQUIRE(fixture.service.redo(fixture.history()).hasValue());
        REQUIRE(encodedScene(fixture.model) == after);
    }
}

TEST_CASE("Final matrices and actual primitive editable imported bounds reject overflow atomically",
          "[batch-api]") {
    for (int operation = 0; operation < 5; ++operation) {
        DYNAMIC_SECTION("overflow=" << operation) {
            BatchFixture fixture;
            const auto parent = fixture.create("parent");
            const auto child = fixture.create("child", parent, core::PrimitiveKind::Cube);
            const auto selected = fixture.create("selected");
            const auto scene = std::const_pointer_cast<core::Scene>(fixture.model.scene());
            auto creates = fixture.creates(2);
            auto transforms = fixture.transforms({parent, selected});
            transforms.items.back().transform.position.x = 4;
            if (operation == 0) {
                auto value = scene->find(parent)->transform;
                value.scale.x = 1e20F;
                REQUIRE(scene->installTransformSnapshot(parent, value));
                creates.items.back().parentId = parent;
                creates.items.back().transform.scale.x = 1e20F;
            } else if (operation == 1) {
                auto value = scene->find(child)->transform;
                value.scale.x = 1e20F;
                REQUIRE(scene->installTransformSnapshot(child, value));
                transforms.items.front().transform.scale.x = 1e20F;
            } else if (operation == 2) {
                creates.items.back().transform.position.x =
                    std::numeric_limits<float>::max() * 0.9F;
                creates.items.back().transform.scale.x = std::numeric_limits<float>::max();
            } else if (operation == 3) {
                REQUIRE(fixture.model.makeEditable(child));
                const auto mesh = scene->find(child)->editableMesh;
                auto source = scene->editableMesh(mesh)->content->source;
                for (auto& vertex : source.vertices)
                    vertex.position.x += 5;
                REQUIRE(fixture.model.replaceEditableMesh(child, source));
                transforms.items.front().transform.scale.x = std::numeric_limits<float>::max() / 2;
            } else {
                const auto root = fixture.model.importGltf(
                    QDir(QString::fromUtf8(MINI3D_SAMPLE_DIRECTORY)).filePath("Duck.glb"));
                REQUIRE(root != 0);
                core::EntityId imported = 0;
                for (const auto& node : scene->nodes())
                    if (node.meshRenderer) {
                        imported = node.id;
                        break;
                    }
                REQUIRE(imported != 0);
                REQUIRE(scene->setParent(imported, 0));
                const auto bounds = fixture.model.assets()
                                        ->mesh(scene->find(imported)->meshRenderer->mesh)
                                        ->data.bounds();
                const auto largest =
                    std::max({std::abs(bounds.minimum.x), std::abs(bounds.minimum.y),
                              std::abs(bounds.minimum.z), std::abs(bounds.maximum.x),
                              std::abs(bounds.maximum.y), std::abs(bounds.maximum.z)});
                REQUIRE(largest > 2);
                transforms = fixture.transforms({imported, selected});
                transforms.items.front().transform = {};
                transforms.items.front().transform.scale =
                    glm::vec3(float(2 * double(std::numeric_limits<float>::max()) / largest));
                transforms.items.back().transform.position.x = 4;
            }
            prepareRedo(fixture, selected, operation % 2 != 0);
            creates.expectedDocumentRevision = fixture.service.documentState().documentRevision;
            transforms.expectedDocumentRevision = fixture.service.documentState().documentRevision;
            const auto before = remember(fixture);
            Notifications notifications(fixture.model);
            const auto result = operation == 0 || operation == 2
                                    ? fixture.service.createEntities(creates)
                                    : fixture.service.setTransforms(transforms);
            REQUIRE_FALSE(result.hasValue());
            REQUIRE(result.error->code == api::ErrorCode::UnsupportedTransform);
            unchanged(fixture, before);
            notifications.none();
        }
    }
}

TEST_CASE("Exact queried rotations are no change and partial targets return only actual changes",
          "[batch-api]") {
    BatchFixture fixture;
    auto create = fixture.mutation<api::EntityCreateRequest>();
    create.name = "非幂等归一化旋转";
    create.transform.rotation = {0.038737595081329346F, 0.16603848338127136F, -0.3174383044242859F,
                                 0.4536607563495636F};
    const auto result = fixture.service.createEntity(create);
    REQUIRE(result.hasValue());
    const auto first = result.value->createdEntityIds.front();
    const auto second = fixture.create("second");
    const auto third = fixture.create("third");
    prepareRedo(fixture, second);
    const auto before = remember(fixture);
    const auto exact = fixture.model.scene()->find(first)->transform;
    int guards = 0;
    auto previous = fixture.service.exchangeBeforeCommitGuard([&] {
        ++guards;
        return std::optional<api::ApiError>{};
    });
    const auto restore = qScopeGuard([&] {
        fixture.service.exchangeBeforeCommitGuard(std::move(previous));
    });
    Notifications notifications(fixture.model);
    for (int roundTrip = 0; roundTrip < 2; ++roundTrip) {
        const auto request = fixture.transforms({third, first, second});
        if (roundTrip == 0) {
            const auto noChange = fixture.service.setTransforms(request);
            REQUIRE(noChange.hasValue());
            REQUIRE(noChange.value->status == api::ResultStatus::NoChange);
            REQUIRE(noChange.value->affectedEntityIds.empty());
            REQUIRE_FALSE(noChange.value->undoable);
        } else {
            const auto noChange = api::ApiJsonCodec::invoke(
                fixture.service, "batch.setTransforms", transformParams(fixture, request),
                api::FileAccess::External, [&] {
                    ++guards;
                    return std::optional<api::ApiError>{};
                });
            REQUIRE(noChange["result"].toObject()["status"] == "no_change");
            REQUIRE(noChange["result"].toObject()["affectedEntityIds"].toArray().isEmpty());
        }
        REQUIRE(fixture.model.scene()->find(first)->transform.rotation == exact.rotation);
        unchanged(fixture, before);
        notifications.none();
    }
    REQUIRE(guards == 2);
    auto changed = fixture.transforms({third, second, first});
    changed.items[0].transform.position.x = 1;
    changed.items[1].transform.rotation = {2, 0, 0, 0};
    changed.items[2].transform.position.z = 2;
    const auto committed = fixture.service.setTransforms(changed);
    REQUIRE(committed.hasValue());
    REQUIRE(committed.value->affectedEntityIds == std::vector<core::EntityId>{third, first});
    REQUIRE(notifications.entities == committed.value->affectedEntityIds);
    REQUIRE(notifications.scene == 1);
    REQUIRE(notifications.structure == 0);
    REQUIRE(fixture.model.scene()->find(first)->transform.rotation == exact.rotation);
    REQUIRE(fixture.model.undoStack()->count() == before.index + 1);
    REQUIRE_FALSE(fixture.model.undoStack()->canRedo());
    REQUIRE(fixture.model.isModified());
    REQUIRE(fixture.model.undoStack()->cleanIndex() == before.clean);
    const auto after = encodedScene(fixture.model);
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(fixture.service.undo(fixture.history()).hasValue());
        REQUIRE(encodedScene(fixture.model) == before.encoded);
        REQUIRE(fixture.model.undoStack()->isClean());
        REQUIRE(fixture.service.redo(fixture.history()).hasValue());
        REQUIRE(encodedScene(fixture.model) == after);
    }
}

TEST_CASE("Batch history preserves earlier GUI geometry snapshots and exact mesh revisions",
          "[batch-api]") {
    BatchFixture fixture;
    const auto cube = fixture.model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(fixture.model.makeEditable(cube));
    const auto scene = std::const_pointer_cast<core::Scene>(fixture.model.scene());
    const auto meshId = scene->find(cube)->editableMesh;
    const auto oldSnapshot = scene->geometrySnapshot(cube);
    REQUIRE(oldSnapshot);
    auto changedSource = scene->editableMesh(meshId)->content->source;
    for (auto& vertex : changedSource.vertices)
        vertex.position.x += 0.1F;
    REQUIRE(fixture.model.replaceEditableMesh(cube, changedSource));
    REQUIRE(fixture.model.renameEntity(cube, "旧 GUI 命令"));
    const auto mesh = *scene->editableMesh(meshId);
    const auto before = remember(fixture);
    const auto created = fixture.service.createEntities(fixture.creates(2, cube));
    REQUIRE(created.hasValue());
    auto transforms = fixture.transforms({cube});
    transforms.items.front().transform.position = {3, 4, 5};
    REQUIRE(fixture.service.setTransforms(transforms).hasValue());
    const auto after = encodedScene(fixture.model);
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(fixture.service.undo(fixture.history()).hasValue());
        REQUIRE(fixture.service.undo(fixture.history()).hasValue());
        REQUIRE(encodedScene(fixture.model) == before.encoded);
        REQUIRE(scene->editableMesh(meshId)->content == mesh.content);
        REQUIRE(scene->editableMesh(meshId)->topologyRevision == mesh.topologyRevision);
        REQUIRE(scene->editableMesh(meshId)->geometryRevision == mesh.geometryRevision);
        REQUIRE(scene->editableMesh(meshId)->evaluationRevision == mesh.evaluationRevision);
        REQUIRE(fixture.service.redo(fixture.history()).hasValue());
        REQUIRE(fixture.service.redo(fixture.history()).hasValue());
        REQUIRE(encodedScene(fixture.model) == after);
        REQUIRE(scene->editableMesh(meshId)->content == mesh.content);
    }
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    fixture.model.undo();
    fixture.model.undo();
    REQUIRE(scene->editableMesh(meshId)->content != mesh.content);
    REQUIRE(scene->installGeometry(*oldSnapshot));
    fixture.model.redo();
    fixture.model.redo();
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    REQUIRE(encodedScene(fixture.model) == after);
}

TEST_CASE("Typed batch context envelope thread and active preview refusals leave state unchanged",
          "[batch-api]") {
    for (int failure = 0; failure < 8; ++failure) {
        DYNAMIC_SECTION("envelope=" << failure) {
            BatchFixture fixture;
            const auto target = fixture.create("target");
            prepareRedo(fixture, target);
            auto creates = fixture.creates();
            auto transforms = fixture.transforms({target});
            transforms.items.front().transform.position.x = 3;
            const auto invalidate = [failure](api::MutationRequest& request) {
                switch (failure) {
                    case 0:
                        request.document.documentId = "00000000-0000-0000-0000-000000000000";
                        break;
                    case 1:
                        --request.expectedDocumentRevision;
                        break;
                    case 2:
                        request.timeoutMs = 0;
                        break;
                    case 3:
                        request.timeoutMs = 30001;
                        break;
                    case 4:
                        request.clientSessionId = "invalid-session";
                        break;
                    case 5:
                        request.mutationSequence = 0;
                        break;
                }
            };
            invalidate(creates);
            invalidate(transforms);
            if (failure == 6) {
                fixture.model.beginTransformEdit(target);
                auto preview = fixture.model.scene()->find(target)->transform;
                preview.position.y = 8;
                fixture.model.previewTransform(preview);
            } else if (failure == 7) {
                fixture.service.setBusyProvider([] {
                    return QStringList{"window_menu"};
                });
            }
            const auto expected = failure == 0   ? api::ErrorCode::StaleDocument
                                  : failure == 1 ? api::ErrorCode::RevisionConflict
                                  : failure >= 6 ? api::ErrorCode::Busy
                                                 : api::ErrorCode::InvalidArgument;
            const auto before = remember(fixture);
            Notifications notifications(fixture.model);
            const auto first = fixture.service.createEntities(creates);
            const auto second = fixture.service.setTransforms(transforms);
            REQUIRE_FALSE(first.hasValue());
            REQUIRE_FALSE(second.hasValue());
            REQUIRE(first.error->code == expected);
            REQUIRE(second.error->code == expected);
            unchanged(fixture, before);
            notifications.none();
        }
    }
    BatchFixture fixture;
    const auto target = fixture.create("target");
    prepareRedo(fixture, target);
    const auto creates = fixture.creates();
    const auto transforms = fixture.transforms({target});
    const auto before = remember(fixture);
    Notifications notifications(fixture.model);
    int guards = 0;
    auto previous = fixture.service.exchangeBeforeCommitGuard([&] {
        ++guards;
        return std::optional<api::ApiError>{};
    });
    const auto restore = qScopeGuard([&] {
        fixture.service.exchangeBeforeCommitGuard(std::move(previous));
    });
    std::array<std::optional<api::ApiError>, 2> errors;
    std::thread worker([&] {
        errors[0] = fixture.service.createEntities(creates).error;
        errors[1] = fixture.service.setTransforms(transforms).error;
    });
    worker.join();
    for (const auto& error : errors) {
        REQUIRE(error);
        REQUIRE(error->code == api::ErrorCode::Internal);
    }
    REQUIRE(guards == 0);
    unchanged(fixture, before);
    notifications.none();
}
