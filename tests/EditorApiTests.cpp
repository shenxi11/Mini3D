/*
 * 模块名: EditorApiTests
 * 功能概述: 直接调用共享 API，验证契约边界、GUI 混合历史和失败原子性。
 * 对外接口: Catch2 [editor-api] 测试，不创建独立 main 或服务监听。
 * 依赖关系: SceneViewModel、EditorApiService、ApiJsonCodec、冻结契约样例。
 * 输入输出: 显式文档/对象/版本到场景、选区、保存点和 wire 断言。
 * 异常与错误: 未确认预览、旧句柄、无效完整更新和外部文件操作须零提交。
 * 维护说明: 临时文件仅在调用者验证并通过 TMPDIR 指定的目录内生成。
 */
#include "editor/SceneViewModel.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <limits>

using namespace mini3d;
namespace {
namespace api = editor::api;
struct ApiFixture {
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    ApiFixture() {
        model.newScene();
    }
    template <typename Request> Request mutation() const {
        Request request;
        request.document = service.documentState().document;
        request.expectedDocumentRevision = service.documentState().documentRevision;
        return request;
    }
    api::EntityCreateRequest createRequest(const QString& name = QStringLiteral("API 对象")) const {
        auto request = mutation<api::EntityCreateRequest>();
        request.primitive = core::PrimitiveKind::Cube;
        request.name = name;
        return request;
    }
    api::HistoryMutationRequest historyRequest() const {
        auto request = mutation<api::HistoryMutationRequest>();
        request.expectedHistoryRevision = service.documentState().historyRevision;
        return request;
    }
    api::EntityGetRequest entityRequest(core::EntityId id) const {
        api::EntityGetRequest request;
        request.document = service.documentState().document;
        request.entityId = id;
        return request;
    }
    QJsonObject wireMutation() const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        return {{"document", state["document"]},
                {"expectedDocumentRevision", state["documentRevision"]}};
    }
};
QJsonObject validTransform() {
    return {{"space", "local"},
            {"translation", QJsonArray{0, 0, 0}},
            {"rotationQuaternion", QJsonArray{0, 0, 0, 1}},
            {"scale", QJsonArray{1, 1, 1}}};
}
QJsonObject validCreate(ApiFixture& fixture) {
    auto result = fixture.wireMutation();
    result.insert("primitive", "cube");
    result.insert("name", QStringLiteral("合同对象"));
    result.insert("parentId", "0");
    result.insert("transform", validTransform());
    result.insert("surface", QJsonObject{{"tint", QJsonArray{1, 1, 1}},
                                         {"useVertexColor", true},
                                         {"useTexture", false}});
    return result;
}
QString temporaryTemplate() {
    const auto root = qEnvironmentVariable("TMPDIR");
    REQUIRE_FALSE(root.isEmpty());
    REQUIRE(QDir::isAbsolutePath(root));
    REQUIRE(QDir(root).exists());
    return QDir(root).filePath(QStringLiteral("mini3d-editor-api-XXXXXX"));
}
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
} // namespace

TEST_CASE("API codec consumes the frozen valid and invalid contract samples", "[editor-api]") {
    QFile examples(QDir(QString::fromUtf8(MINI3D_API_SCHEMA_DIRECTORY)).filePath("examples.json"));
    REQUIRE(examples.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(examples.readAll(), &parseError);
    REQUIRE(parseError.error == QJsonParseError::NoError);
    const auto root = document.object();
    for (const auto& category : {"valid", "invalid"}) {
        const bool expected = QString::fromLatin1(category) == "valid";
        for (const auto& item : root[category].toArray()) {
            const auto sample = item.toObject();
            const auto kind = sample["kind"].toString();
            CAPTURE(category, kind);
            if (kind == "uint64")
                REQUIRE(api::ApiJsonCodec::parseUint64(sample["value"]).hasValue() == expected);
            else if (kind == "transform")
                REQUIRE(api::ApiJsonCodec::parseTransform(sample["value"]).hasValue() == expected);
            else
                FAIL("Unknown frozen contract sample type");
        }
    }
    const auto maximum = api::ApiJsonCodec::parseUint64(QStringLiteral("18446744073709551615"));
    REQUIRE(maximum.hasValue());
    REQUIRE(*maximum.value == std::numeric_limits<std::uint64_t>::max());
    REQUIRE_FALSE(api::ApiJsonCodec::parseUint64(QStringLiteral("0"), {}, false).hasValue());
    for (const auto& text : {"+1", "-1", " 1", "1 ", "1.0", "", "00"})
        REQUIRE_FALSE(api::ApiJsonCodec::parseUint64(QString::fromLatin1(text)).hasValue());
    api::EntityResult result;
    result.entity.entityId = *maximum.value;
    result.entity.meshId = 9007199254740993ULL;
    const auto encoded = api::ApiJsonCodec::encode(result)["entity"].toObject();
    REQUIRE(encoded["entityId"].isString());
    REQUIRE(encoded["entityId"].toString() == "18446744073709551615");
    REQUIRE(encoded["meshId"].toString() == "9007199254740993");
}

TEST_CASE("API codec rejects unknown fields invalid floats and mistyped complete requests",
          "[editor-api]") {
    ApiFixture fixture;
    const auto before = fixture.service.documentState();
    const auto create = validCreate(fixture);
    REQUIRE(api::ApiJsonCodec::decodeRequest("entity.create", create).hasValue());
    for (int failure = 0; failure < 9; ++failure) {
        auto invalid = create;
        auto transform = validTransform();
        CAPTURE(failure);
        switch (failure) {
            case 0:
                invalid.insert("primitve", "sphere");
                break;
            case 1:
                invalid.insert("parentId", 1);
                break;
            case 2:
                transform.insert("unused", true);
                break;
            case 3:
                transform.insert("translation", QJsonArray{1e100, 0, 0});
                break;
            case 4:
                transform.insert("translation", QJsonArray{true, 0, 0});
                break;
            case 5:
                transform.insert("scale", QJsonArray{0.00099999999, 1, 1});
                break;
            case 6:
                invalid.insert("timeoutMs", 30001);
                break;
            case 7:
                invalid.insert("mutationSequence", "0");
                break;
            case 8: {
                auto surface = invalid["surface"].toObject();
                surface.insert("extra", true);
                invalid.insert("surface", surface);
                break;
            }
        }
        invalid.insert("transform", transform);
        const auto response = api::ApiJsonCodec::invoke(fixture.service, "entity.create", invalid);
        REQUIRE(response["error"].toObject()["code"].toInt() == -32602);
        REQUIRE(response["error"].toObject()["data"].toObject()["code"] == "INVALID_ARGUMENT");
        REQUIRE(fixture.model.scene()->nodes().empty());
        REQUIRE(fixture.model.undoStack()->count() == 0);
        REQUIRE(fixture.service.documentState().documentRevision == before.documentRevision);
    }
    for (const auto value :
         {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto transform = validTransform();
        transform.insert("translation", QJsonArray{value, 0, 0});
        REQUIRE_FALSE(api::ApiJsonCodec::parseTransform(transform).hasValue());
    }
    const auto supplementary = QString::fromUcs4(U"\U0001f603");
    auto unicodeName = create;
    unicodeName.insert("name", supplementary.repeated(256));
    REQUIRE(api::ApiJsonCodec::decodeRequest("entity.create", unicodeName).hasValue());
    unicodeName.insert("name", supplementary.repeated(257));
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("entity.create", unicodeName).hasValue());
    auto update = fixture.wireMutation();
    update.insert("entityId", "1");
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("entity.update", update).hasValue());
}

TEST_CASE("Complete API creation uses explicit parent transform surface and one shared history",
          "[editor-api]") {
    ApiFixture fixture;
    const auto parent = fixture.model.createEntity(core::PrimitiveKind::Empty);
    const auto selected = fixture.model.createEntity(core::PrimitiveKind::Sphere);
    REQUIRE(fixture.model.setCursorPosition({99, 88, 77}));
    auto request = fixture.createRequest(QStringLiteral("精确立方体"));
    request.parentId = parent;
    request.transform.position = {1, 2, 3};
    request.transform.rotation = {2, 0, 0, 0};
    request.transform.scale = {-2, 3, 4};
    request.surface = {{0.2F, 0.3F, 0.4F}, false, false};
    const auto before = fixture.service.documentState();
    const auto index = fixture.model.undoStack()->index();
    const auto result = fixture.service.createEntity(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->status == api::ResultStatus::Committed);
    REQUIRE_FALSE(result.value->selectionChanged);
    const auto created = result.value->createdEntityIds.front();
    const auto* node = fixture.model.scene()->find(created);
    REQUIRE(node->name == request.name.toUtf8().toStdString());
    REQUIRE(node->parent == parent);
    REQUIRE(node->transform.position == request.transform.position);
    REQUIRE(node->transform.rotation == glm::quat(1, 0, 0, 0));
    REQUIRE(node->transform.scale == request.transform.scale);
    REQUIRE(node->surface == request.surface);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.model.undoStack()->count() == index + 1);
    REQUIRE(result.value->state.documentRevision == before.documentRevision + 1);
    REQUIRE(result.value->state.historyRevision == before.historyRevision + 1);
    REQUIRE(fixture.service.undo(fixture.historyRequest()).hasValue());
    REQUIRE_FALSE(fixture.model.scene()->find(created));
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.service.redo(fixture.historyRequest()).hasValue());
    REQUIRE(fixture.model.scene()->find(created));
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.model.undoStack()->count() == index + 1);
    const auto apiRevision = fixture.service.documentState().documentRevision;
    REQUIRE(fixture.model.renameEntity(created, QStringLiteral("GUI 名字")));
    REQUIRE(fixture.service.documentState().documentRevision == apiRevision + 1);
    fixture.model.undo();
    REQUIRE(fixture.model.scene()->find(created)->name == request.name.toUtf8().toStdString());
    REQUIRE(fixture.service.redo(fixture.historyRequest()).hasValue());
    REQUIRE(fixture.model.scene()->find(created)->name == "GUI 名字");
}

TEST_CASE("API complete update rejects its last invalid field and preserves redo and clean point",
          "[editor-api]") {
    QTemporaryDir directory(temporaryTemplate());
    REQUIRE(directory.isValid());
    ApiFixture fixture;
    const auto target = fixture.model.createEntity(core::PrimitiveKind::Cube);
    const auto selected = fixture.model.createEntity(core::PrimitiveKind::Sphere);
    REQUIRE(fixture.model.setVisible(target, false));
    REQUIRE(fixture.model.saveScene(directory.filePath(QStringLiteral("保存点.m3dscene"))));
    fixture.model.undo();
    REQUIRE(fixture.model.undoStack()->canRedo());
    const auto before = *fixture.model.scene()->find(target);
    const auto state = fixture.service.documentState();
    const auto count = fixture.model.undoStack()->count();
    const auto index = fixture.model.undoStack()->index();
    const auto cleanIndex = fixture.model.undoStack()->cleanIndex();
    const auto* redo = fixture.model.undoStack()->command(index);
    auto update = fixture.mutation<api::EntityUpdateRequest>();
    update.entityId = target;
    update.changes.name = QStringLiteral("不能留下的名字");
    auto transform = before.transform;
    transform.position = {4, 5, 6};
    update.changes.transform = transform;
    auto invalidSurface = before.surface;
    invalidSurface.tint.x = 2;
    update.changes.surface = invalidSurface;
    update.changes.visible = false;
    const auto failure = fixture.service.updateEntity(update);
    REQUIRE_FALSE(failure.hasValue());
    REQUIRE(failure.error->code == api::ErrorCode::InvalidArgument);
    REQUIRE(failure.error->fieldPath == "surface");
    auto invalidCreate = fixture.createRequest();
    invalidCreate.surface = invalidSurface;
    REQUIRE_FALSE(fixture.service.createEntity(invalidCreate).hasValue());
    REQUIRE(fixture.model.scene()->nodes().size() == 2);
    const auto* unchanged = fixture.model.scene()->find(target);
    REQUIRE(unchanged->name == before.name);
    REQUIRE(unchanged->transform.position == before.transform.position);
    REQUIRE(unchanged->surface == before.surface);
    REQUIRE(unchanged->visible == before.visible);
    auto noChange = fixture.mutation<api::EntityUpdateRequest>();
    noChange.entityId = target;
    noChange.changes.name = QString::fromUtf8(before.name.data(), qsizetype(before.name.size()));
    noChange.changes.transform = before.transform;
    noChange.changes.surface = before.surface;
    noChange.changes.visible = before.visible;
    const auto noop = fixture.service.updateEntity(noChange);
    REQUIRE(noop.hasValue());
    REQUIRE(noop.value->status == api::ResultStatus::NoChange);
    REQUIRE_FALSE(noop.value->undoable);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.model.undoStack()->count() == count);
    REQUIRE(fixture.model.undoStack()->index() == index);
    REQUIRE(fixture.model.undoStack()->cleanIndex() == cleanIndex);
    REQUIRE(fixture.model.undoStack()->command(index) == redo);
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision);
    REQUIRE(fixture.service.redo(fixture.historyRequest()).hasValue());
    REQUIRE(fixture.model.undoStack()->isClean());
    auto complete = fixture.mutation<api::EntityUpdateRequest>();
    complete.entityId = target;
    complete.changes.name = QStringLiteral("完整更新");
    complete.changes.transform = transform;
    complete.changes.surface = core::SurfaceStyle{{0.2F, 0.4F, 0.6F}, false, true};
    complete.changes.visible = true;
    const auto historyBefore = fixture.model.undoStack()->count();
    REQUIRE(fixture.service.updateEntity(complete).hasValue());
    REQUIRE(fixture.model.undoStack()->count() == historyBefore + 1);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.model.scene()->find(target)->name == "完整更新");
    fixture.model.undo();
    REQUIRE(fixture.model.scene()->find(target)->name == before.name);
    REQUIRE_FALSE(fixture.model.scene()->find(target)->visible);
    fixture.model.redo();
    REQUIRE(fixture.model.scene()->find(target)->transform.position == transform.position);
    REQUIRE(fixture.model.scene()->find(target)->surface == *complete.changes.surface);
}

TEST_CASE("API snapshots and writes refuse unconfirmed previews without cancelling them",
          "[editor-api]") {
    ApiFixture fixture;
    const auto target = fixture.model.createEntity(core::PrimitiveKind::Cube);
    const auto state = fixture.service.documentState();
    fixture.model.beginTransformEdit(target);
    core::Transform preview;
    preview.position.x = 100;
    fixture.model.previewTransform(preview);
    REQUIRE(fixture.model.scene()->find(target)->transform.position.x == 100);
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.currentDocument().value->busyReasons.contains("object_transform"));
    REQUIRE(fixture.service.entity(fixture.entityRequest(target)).error->code ==
            api::ErrorCode::Busy);
    REQUIRE(fixture.service.createEntity(fixture.createRequest()).error->code ==
            api::ErrorCode::Busy);
    REQUIRE(fixture.model.scene()->find(target)->transform.position.x == 100);
    fixture.model.finishTransformEdit(false);
    REQUIRE(fixture.model.scene()->find(target)->transform.position.x == 0);
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.entity(fixture.entityRequest(target)).hasValue());
    QStringList external{"navigation"};
    fixture.service.setBusyProvider([&external] {
        return external;
    });
    REQUIRE(fixture.service.createEntity(fixture.createRequest()).error->code ==
            api::ErrorCode::Busy);
    REQUIRE(fixture.service.currentDocument().value->busyReasons.contains("navigation"));
    external.clear();
    fixture.service.setExternalBusy(QStringLiteral("file_dialog"), true);
    REQUIRE(fixture.service.entity(fixture.entityRequest(target)).error->code ==
            api::ErrorCode::Busy);
    fixture.service.setExternalBusy(QStringLiteral("file_dialog"), false);
    REQUIRE(fixture.service.currentDocument().value->busyReasons.empty());
    REQUIRE(fixture.model.setEditMode(true));
    const auto editState = fixture.service.documentState();
    REQUIRE(fixture.service.createEntity(fixture.createRequest()).error->code ==
            api::ErrorCode::Busy);
    REQUIRE(fixture.model.isEditMode());
    fixture.model.selectComponent({1}, editor::SelectionOperation::Replace);
    REQUIRE(fixture.model.beginComponentTransform());
    REQUIRE(fixture.model.previewComponentTransform(
        glm::translate(glm::dmat4(1), glm::dvec3(.1, 0, 0))));
    REQUIRE(fixture.service.entity(fixture.entityRequest(target)).error->code ==
            api::ErrorCode::Busy);
    REQUIRE(fixture.model.hasComponentTransform());
    REQUIRE(fixture.service.documentState().documentRevision == editState.documentRevision);
    REQUIRE(fixture.model.finishComponentTransform(false));
    REQUIRE(fixture.model.beginLoopCut());
    REQUIRE(fixture.service.currentDocument().value->busyReasons.contains("loop_cut"));
    REQUIRE(fixture.model.finishComponentTransform(false));
    REQUIRE(fixture.model.setEditMode(false));
    REQUIRE(fixture.service.currentDocument().value->busyReasons.empty());
}

TEST_CASE("API revisions cover GUI camera cursor undo redo and F9 without counting previews",
          "[editor-api]") {
    ApiFixture fixture;
    const auto initial = fixture.service.documentState();
    REQUIRE(fixture.model.setCursorPosition({1, 2, 3}));
    fixture.model.setCursorVisible(false);
    REQUIRE(fixture.service.documentState().documentRevision == initial.documentRevision + 2);
    REQUIRE(fixture.service.documentState().historyRevision == initial.historyRevision);
    REQUIRE_FALSE(fixture.model.isModified());
    REQUIRE(fixture.model.setCursorPosition({1, 2, 3}));
    fixture.model.setCursorVisible(false);
    REQUIRE(fixture.service.documentState().documentRevision == initial.documentRevision + 2);
    auto camera = fixture.model.editorCamera();
    camera.position.x += 1;
    fixture.model.setEditorCamera(camera);
    fixture.model.setEditorCamera(camera);
    REQUIRE(fixture.service.documentState().documentRevision == initial.documentRevision + 3);
    REQUIRE(fixture.service.documentState().historyRevision == initial.historyRevision);
    REQUIRE(fixture.model.isModified());
    const auto target = fixture.model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(fixture.model.setEditMode(true));
    fixture.model.setSelectionDomain(editor::SelectionDomain::Face);
    fixture.model.selectComponent({1}, editor::SelectionOperation::Replace);
    const auto beforePreview = fixture.service.documentState();
    REQUIRE(fixture.model.beginExtrudeRegion());
    REQUIRE(fixture.model.previewComponentTransform(
        glm::translate(glm::dmat4(1), glm::dvec3(0, 0, .25))));
    REQUIRE(fixture.service.documentState().documentRevision == beforePreview.documentRevision);
    REQUIRE(fixture.model.finishComponentTransform(true));
    const auto beforeF9 = fixture.service.documentState();
    const auto index = fixture.model.undoStack()->index();
    const auto count = fixture.model.undoStack()->count();
    REQUIRE(fixture.model.adjustLastOperation({0, 0, .5}));
    REQUIRE(fixture.service.documentState().documentRevision == beforeF9.documentRevision + 1);
    REQUIRE(fixture.service.documentState().historyRevision == beforeF9.historyRevision + 1);
    REQUIRE(fixture.model.undoStack()->index() == index);
    REQUIRE(fixture.model.undoStack()->count() == count);
    const auto adjusted = fixture.service.documentState();
    REQUIRE(fixture.model.adjustLastOperation({0, 0, .5}));
    REQUIRE_FALSE(fixture.model.adjustLastOperation({0, 0, 0}));
    REQUIRE(fixture.service.documentState().documentRevision == adjusted.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == adjusted.historyRevision);
    fixture.model.undo();
    fixture.model.redo();
    REQUIRE(fixture.model.scene()->find(target));
    REQUIRE(fixture.service.documentState().documentRevision == adjusted.documentRevision + 2);
    REQUIRE(fixture.service.documentState().historyRevision == adjusted.historyRevision + 2);
}

TEST_CASE("API query pages are stable snapshots and reject old versions and old document handles",
          "[editor-api]") {
    ApiFixture fixture;
    const auto parent = fixture.model.createEntity(core::PrimitiveKind::Empty);
    core::Transform parentTransform;
    parentTransform.position = {4, 0, 0};
    parentTransform.scale = {-2, 3, 1};
    REQUIRE(fixture.model.setTransform(parent, parentTransform));
    auto create = fixture.createRequest();
    create.parentId = parent;
    create.transform.position = {1, 2, 3};
    const auto child = fixture.service.createEntity(create).value->createdEntityIds.front();
    REQUIRE(fixture.model.setVisible(parent, false));
    const auto selected = fixture.model.selection()->selectedEntity();
    const auto state = fixture.service.documentState();
    const auto entity = fixture.service.entity(fixture.entityRequest(child));
    REQUIRE(entity.hasValue());
    REQUIRE(entity.value->entity.localTransform.position == create.transform.position);
    REQUIRE(entity.value->entity.worldMatrix[3] == glm::vec4(2, 6, 3, 1));
    REQUIRE(entity.value->entity.visible);
    REQUIRE_FALSE(entity.value->entity.effectiveVisible);
    REQUIRE(entity.value->entity.localBounds);
    REQUIRE(entity.value->entity.worldBounds);
    const auto json = api::ApiJsonCodec::encode(*entity.value)["entity"].toObject();
    REQUIRE(json["worldMatrix"].toArray()[12].toDouble() == 2);
    REQUIRE(json["bounds"].toObject()["kind"] == "evaluated");
    api::EntityListRequest page;
    page.document = state.document;
    page.limit = 1;
    const auto first = fixture.service.listEntities(page);
    REQUIRE(first.hasValue());
    REQUIRE(first.value->entities.size() == 1);
    REQUIRE(first.value->entities.front().entityId == parent);
    REQUIRE(first.value->nextAfterEntityId == parent);
    page.afterEntityId = parent;
    REQUIRE(fixture.service.listEntities(page).error->code == api::ErrorCode::InvalidArgument);
    page.expectedDocumentRevision = first.value->state.documentRevision;
    REQUIRE(fixture.service.listEntities(page).value->entities.front().entityId == child);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    fixture.model.undo();
    REQUIRE(fixture.service.listEntities(page).error->code == api::ErrorCode::RevisionConflict);
    auto staleHistory = fixture.historyRequest();
    --staleHistory.expectedHistoryRevision;
    const auto revision = fixture.service.documentState().documentRevision;
    REQUIRE(fixture.service.undo(staleHistory).error->code == api::ErrorCode::RevisionConflict);
    REQUIRE(fixture.service.documentState().documentRevision == revision);
    auto oldEntity = fixture.entityRequest(child);
    fixture.model.newScene();
    REQUIRE(fixture.service.documentState().document.documentId != state.document.documentId);
    REQUIRE(fixture.service.documentState().document.instanceId == state.document.instanceId);
    REQUIRE(fixture.service.documentState().documentRevision == 1);
    REQUIRE(fixture.service.documentState().historyRevision == 1);
    REQUIRE(fixture.service.entity(oldEntity).error->code == api::ErrorCode::StaleDocument);
}

TEST_CASE("Internal API file operations protect dirty documents overwrite and legacy originals",
          "[editor-api]") {
    QTemporaryDir directory(temporaryTemplate());
    REQUIRE(directory.isValid());
    ApiFixture fixture;
    const auto target = fixture.model.createEntity(core::PrimitiveKind::Cube);
    auto save = fixture.mutation<api::FileRequest>();
    save.path = directory.filePath(QStringLiteral("API 工程.m3dscene"));
    REQUIRE(fixture.service.saveAs(save).error->code == api::ErrorCode::PermissionDenied);
    REQUIRE_FALSE(QFileInfo::exists(save.path));
    const auto beforeSave = fixture.service.documentState();
    const auto saved = fixture.service.saveAs(save, api::FileAccess::InternalTrusted);
    REQUIRE(saved.hasValue());
    REQUIRE(saved.value->status == api::ResultStatus::Saved);
    REQUIRE_FALSE(saved.value->undoable);
    REQUIRE(saved.value->state.documentRevision == beforeSave.documentRevision);
    REQUIRE(saved.value->state.historyRevision == beforeSave.historyRevision + 1);
    REQUIRE_FALSE(fixture.model.isModified());
    const auto bytes = readBytes(save.path);
    auto overwrite = fixture.mutation<api::FileRequest>();
    overwrite.path = save.path;
    REQUIRE(fixture.service.saveAs(overwrite, api::FileAccess::InternalTrusted).error->code ==
            api::ErrorCode::OverwriteDenied);
    REQUIRE(readBytes(save.path) == bytes);
    auto missing = fixture.mutation<api::FileRequest>();
    missing.path = directory.filePath(QStringLiteral("不存在.m3dscene"));
    const auto beforeFailure = fixture.service.documentState();
    REQUIRE(fixture.service.openDocument(missing, api::FileAccess::InternalTrusted).error->code ==
            api::ErrorCode::IoError);
    REQUIRE(fixture.service.documentState().document == beforeFailure.document);
    REQUIRE(fixture.service.documentState().documentRevision == beforeFailure.documentRevision);
    REQUIRE(fixture.model.scene()->find(target));
    REQUIRE(fixture.model.undoStack()->count() == 1);
    REQUIRE(fixture.model.renameEntity(target, QStringLiteral("未保存")));
    auto open = fixture.mutation<api::FileRequest>();
    open.path = save.path;
    REQUIRE(fixture.service.openDocument(open, api::FileAccess::InternalTrusted).error->code ==
            api::ErrorCode::UnsavedChanges);
    fixture.model.undo();
    open = fixture.mutation<api::FileRequest>();
    open.path = save.path;
    REQUIRE(fixture.service.openDocument(open).error->code == api::ErrorCode::PermissionDenied);
    const auto oldHandle = fixture.entityRequest(target);
    REQUIRE(fixture.service.openDocument(open, api::FileAccess::InternalTrusted).hasValue());
    REQUIRE(fixture.service.entity(oldHandle).error->code == api::ErrorCode::StaleDocument);
    REQUIRE(fixture.model.undoStack()->count() == 0);
    const auto firstOpen = fixture.service.documentState().document.documentId;
    open = fixture.mutation<api::FileRequest>();
    open.path = save.path;
    REQUIRE(fixture.service.openDocument(open, api::FileAccess::InternalTrusted).hasValue());
    REQUIRE(fixture.service.documentState().document.documentId != firstOpen);
    auto legacyJson = QJsonDocument::fromJson(bytes).object();
    legacyJson.insert("version", 2);
    const auto legacyPath = directory.filePath(QStringLiteral("旧版原件.m3dscene"));
    const auto legacyBytes = QJsonDocument(legacyJson).toJson();
    writeBytes(legacyPath, legacyBytes);
    open = fixture.mutation<api::FileRequest>();
    open.path = legacyPath;
    REQUIRE(fixture.service.openDocument(open, api::FileAccess::InternalTrusted).hasValue());
    REQUIRE(fixture.model.requiresSaveAs());
    save = fixture.mutation<api::FileRequest>();
    save.path = legacyPath;
    REQUIRE(fixture.service.saveAs(save, api::FileAccess::InternalTrusted).error->code ==
            api::ErrorCode::OverwriteDenied);
    REQUIRE(readBytes(legacyPath) == legacyBytes);
    save.path = directory.filePath(QStringLiteral("升级新件.m3dscene"));
    REQUIRE(fixture.service.saveAs(save, api::FileAccess::InternalTrusted).hasValue());
    REQUIRE_FALSE(fixture.model.requiresSaveAs());
    REQUIRE(readBytes(legacyPath) == legacyBytes);
    const auto describe = api::ApiJsonCodec::encode(*fixture.service.describe().value);
    QFile methodContract(QDir(QString::fromUtf8(MINI3D_API_SCHEMA_DIRECTORY)).filePath("methods.json"));
    REQUIRE(methodContract.open(QIODevice::ReadOnly));
    const auto contract = QJsonDocument::fromJson(methodContract.readAll()).object();
    QStringList expectedMethods;
    for (const auto& method : contract["methods"].toArray()) {
        const auto name = method.toObject()["name"].toString();
        if (!name.startsWith("viewport."))
            expectedMethods.append(name);
    }
    QStringList actualMethods;
    for (const auto& method : describe["methods"].toArray())
        actualMethods.append(method.toObject()["name"].toString());
    actualMethods.sort();
    expectedMethods.sort();
    REQUIRE(actualMethods == expectedMethods);
    const auto contractLimits = contract["limits"].toObject();
    const auto reportedLimits = describe["limits"].toObject();
    REQUIRE(reportedLimits.size() == contractLimits.size());
    for (auto limit = contractLimits.begin(); limit != contractLimits.end(); ++limit)
        REQUIRE(reportedLimits[limit.key()] == limit.value());
    for (const auto& method : describe["methods"].toArray()) {
        const auto item = method.toObject();
        REQUIRE_FALSE(item["name"].toString().startsWith("viewport."));
        if (item["permission"] == "file.read" || item["permission"] == "file.write") {
            REQUIRE_FALSE(item["externalEnabled"].toBool());
            REQUIRE_FALSE(item["externalGate"].toString().isEmpty());
        }
    }
}

TEST_CASE("Queried API quaternion round trips preserve redo clean points and revisions",
          "[editor-api][api-review-regression]") {
    QTemporaryDir directory(temporaryTemplate());
    REQUIRE(directory.isValid());
    ApiFixture fixture;
    auto create = fixture.createRequest();
    create.transform.rotation = {0.038737595081329346F, 0.16603848338127136F, -0.3174383044242859F,
                                 0.4536607563495636F};
    const auto created = fixture.service.createEntity(create);
    REQUIRE(created.hasValue());
    const auto target = created.value->createdEntityIds.front();
    fixture.model.selection()->setSelectedEntity(target);
    REQUIRE(fixture.model.saveScene(directory.filePath(QStringLiteral("旋转保存点.m3dscene"))));
    fixture.model.createEntity(core::PrimitiveKind::Empty);
    fixture.model.undo();
    REQUIRE(fixture.model.undoStack()->isClean());
    REQUIRE(fixture.model.undoStack()->canRedo());
    const auto snapshot = fixture.service.entity(fixture.entityRequest(target));
    REQUIRE(snapshot.hasValue());
    const auto state = fixture.service.documentState();
    const auto count = fixture.model.undoStack()->count();
    const auto index = fixture.model.undoStack()->index();
    const auto clean = fixture.model.undoStack()->cleanIndex();
    const auto* redo = fixture.model.undoStack()->command(index);
    const auto selected = fixture.model.selection()->selectedEntity();

    SECTION("Strong typed query transform is an exact no change") {
        auto update = fixture.mutation<api::EntityUpdateRequest>();
        update.entityId = target;
        update.changes.transform = snapshot.value->entity.localTransform;
        const auto result = fixture.service.updateEntity(update);
        REQUIRE(result.hasValue());
        REQUIRE(result.value->status == api::ResultStatus::NoChange);
    }
    SECTION("JSON query transform is an exact no change") {
        auto update = fixture.wireMutation();
        update.insert("entityId", QString::number(target));
        update.insert("transform",
                      api::ApiJsonCodec::encode(*snapshot.value)["entity"].toObject()["transform"]);
        const auto result = api::ApiJsonCodec::invoke(fixture.service, "entity.update", update);
        REQUIRE(result.contains("result"));
        REQUIRE(result["result"].toObject()["status"] == "no_change");
    }

    REQUIRE(fixture.model.scene()->find(target)->transform.rotation ==
            snapshot.value->entity.localTransform.rotation);
    REQUIRE(fixture.model.selection()->selectedEntity() == selected);
    REQUIRE(fixture.model.undoStack()->count() == count);
    REQUIRE(fixture.model.undoStack()->index() == index);
    REQUIRE(fixture.model.undoStack()->cleanIndex() == clean);
    REQUIRE(fixture.model.undoStack()->command(index) == redo);
    REQUIRE(fixture.model.undoStack()->canRedo());
    REQUIRE(fixture.model.undoStack()->isClean());
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision);
}

TEST_CASE("API name and visibility updates never normalize an unchanged rotation",
          "[editor-api][api-review-regression]") {
    ApiFixture fixture;
    auto create = fixture.createRequest();
    create.transform.rotation = {0.038737595081329346F, 0.16603848338127136F, -0.3174383044242859F,
                                 0.4536607563495636F};
    const auto created = fixture.service.createEntity(create);
    REQUIRE(created.hasValue());
    const auto target = created.value->createdEntityIds.front();
    const auto rotation = fixture.model.scene()->find(target)->transform.rotation;
    auto update = fixture.mutation<api::EntityUpdateRequest>();
    update.entityId = target;
    update.changes.name = QStringLiteral("只改名字");
    REQUIRE(fixture.service.updateEntity(update).hasValue());
    REQUIRE(fixture.model.scene()->find(target)->transform.rotation == rotation);
    update = fixture.mutation<api::EntityUpdateRequest>();
    update.entityId = target;
    update.changes.visible = false;
    REQUIRE(fixture.service.updateEntity(update).hasValue());
    REQUIRE(fixture.model.scene()->find(target)->transform.rotation == rotation);
    fixture.model.undo();
    fixture.model.undo();
    REQUIRE(fixture.model.scene()->find(target)->transform.rotation == rotation);
    fixture.model.redo();
    fixture.model.redo();
    REQUIRE(fixture.model.scene()->find(target)->transform.rotation == rotation);
    REQUIRE(fixture.model.renameEntity(target, QStringLiteral("GUI 只改名字")));
    REQUIRE(fixture.model.scene()->find(target)->transform.rotation == rotation);
    REQUIRE(fixture.model.setVisible(target, true));
    REQUIRE(fixture.model.scene()->find(target)->transform.rotation == rotation);
}

TEST_CASE("API translation and scale history restore the exact unchanged quaternion",
          "[editor-api][api-transform-history-regression]") {
    ApiFixture fixture;
    auto create = fixture.createRequest();
    create.transform.rotation = {0.038737595081329346F, 0.16603848338127136F, -0.3174383044242859F,
                                 0.4536607563495636F};
    const auto created = fixture.service.createEntity(create);
    REQUIRE(created.hasValue());
    const auto target = created.value->createdEntityIds.front();
    const auto original = fixture.model.scene()->find(target)->transform;
    auto changed = original;
    SECTION("Only translation changes") {
        changed.position = {1, 2, 3};
    }
    SECTION("Only scale changes") {
        changed.scale = {-2, 3, 4};
    }
    auto update = fixture.mutation<api::EntityUpdateRequest>();
    update.entityId = target;
    update.changes.transform = changed;
    const auto result = fixture.service.updateEntity(update);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->status == api::ResultStatus::Committed);
    REQUIRE(fixture.model.scene()->find(target)->transform.rotation == original.rotation);
    REQUIRE(fixture.model.scene()->find(target)->transform.position == changed.position);
    REQUIRE(fixture.model.scene()->find(target)->transform.scale == changed.scale);
    REQUIRE(fixture.service.undo(fixture.historyRequest()).hasValue());
    REQUIRE(fixture.model.scene()->find(target)->transform.rotation == original.rotation);
    REQUIRE(fixture.model.scene()->find(target)->transform.position == original.position);
    REQUIRE(fixture.model.scene()->find(target)->transform.scale == original.scale);
    REQUIRE(fixture.service.redo(fixture.historyRequest()).hasValue());
    REQUIRE(fixture.model.scene()->find(target)->transform.rotation == original.rotation);
    REQUIRE(fixture.model.scene()->find(target)->transform.position == changed.position);
    REQUIRE(fixture.model.scene()->find(target)->transform.scale == changed.scale);
}

TEST_CASE("API rejects overflowing world matrices or geometric bounds without query side effects",
          "[editor-api][api-review-regression]") {
    ApiFixture fixture;
    core::EntityId target = 0;
    QString field;
    SECTION("Finite parent and child local scales overflow the world matrix") {
        auto parentRequest = fixture.createRequest(QStringLiteral("巨大父节点"));
        parentRequest.primitive = core::PrimitiveKind::Empty;
        parentRequest.transform.scale = {1e20F, 1, 1};
        const auto parent = fixture.service.createEntity(parentRequest);
        REQUIRE(parent.hasValue());
        auto childRequest = fixture.createRequest(QStringLiteral("巨大子节点"));
        childRequest.parentId = parent.value->createdEntityIds.front();
        childRequest.transform.scale = {1e20F, 1, 1};
        const auto child = fixture.service.createEntity(childRequest);
        REQUIRE(child.hasValue());
        target = child.value->createdEntityIds.front();
        REQUIRE_FALSE(std::isfinite(fixture.model.scene()->worldMatrix(target)[0][0]));
        field = QStringLiteral("worldMatrix");
    }
    SECTION("A finite matrix can still overflow existing geometric world bounds") {
        auto request = fixture.createRequest(QStringLiteral("边界溢出对象"));
        request.transform.position.x = std::numeric_limits<float>::max();
        request.transform.scale.x = std::numeric_limits<float>::max();
        const auto created = fixture.service.createEntity(request);
        REQUIRE(created.hasValue());
        target = created.value->createdEntityIds.front();
        const auto matrix = fixture.model.scene()->worldMatrix(target);
        REQUIRE(std::isfinite(matrix[0][0]));
        REQUIRE(std::isfinite(matrix[3][0]));
        field = QStringLiteral("bounds.world");
    }
    fixture.model.selection()->setSelectedEntity(target);
    const auto state = fixture.service.documentState();
    const auto count = fixture.model.undoStack()->count();
    const auto index = fixture.model.undoStack()->index();
    const auto clean = fixture.model.undoStack()->cleanIndex();
    core::SceneDocumentData before;
    before.nodes = fixture.model.scene()->nodes();
    const auto serialized = core::SceneSerializer::encode(before);
    const auto result = fixture.service.entity(fixture.entityRequest(target));
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->fieldPath == field);
    REQUIRE(api::ApiJsonCodec::encodeError(*result.error)["data"].toObject()["code"] ==
            "UNSUPPORTED_TRANSFORM");
    api::EntityListRequest page;
    page.document = state.document;
    const auto list = fixture.service.listEntities(page);
    REQUIRE_FALSE(list.hasValue());
    REQUIRE(list.error->fieldPath == field);
    const auto wire = api::ApiJsonCodec::response(result);
    REQUIRE_FALSE(wire.contains("result"));
    REQUIRE(wire.contains("error"));
    api::DocumentRequest summaryRequest{state.document};
    const auto summary = fixture.service.sceneSummary(summaryRequest);
    REQUIRE(summary.hasValue());
    REQUIRE(summary.value->entityCount == before.nodes.size());
    core::SceneDocumentData after;
    after.nodes = fixture.model.scene()->nodes();
    REQUIRE(core::SceneSerializer::encode(after) == serialized);
    REQUIRE(fixture.model.selection()->selectedEntity() == target);
    REQUIRE(fixture.model.undoStack()->count() == count);
    REQUIRE(fixture.model.undoStack()->index() == index);
    REQUIRE(fixture.model.undoStack()->cleanIndex() == clean);
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision);
}

TEST_CASE("Canonical business parameters use decoded values defaults and no ledger metadata",
          "[editor-api][api-canonical]") {
    ApiFixture fixture;
    const auto params = validCreate(fixture);
    const auto canonical = api::ApiJsonCodec::canonicalParams("entity.create", params);
    REQUIRE(canonical.hasValue());
    REQUIRE((*canonical.value)["timeoutMs"].toInt() == int(api::limits::mutationTimeoutMs));
    auto explicitDefaults = params;
    auto handle = explicitDefaults["document"].toObject();
    handle.insert("instanceId", handle["instanceId"].toString().toUpper());
    handle.insert("documentId", handle["documentId"].toString().toUpper());
    explicitDefaults.insert("document", handle);
    explicitDefaults.insert("timeoutMs", int(api::limits::mutationTimeoutMs));
    explicitDefaults.insert("clientSessionId", "12345678-1234-1234-1234-123456789abc");
    explicitDefaults.insert("mutationSequence", "7");
    const auto replay = api::ApiJsonCodec::canonicalParams("entity.create", explicitDefaults);
    REQUIRE(replay.hasValue());
    REQUIRE(*replay.value == *canonical.value);
    REQUIRE_FALSE(replay.value->contains("clientSessionId"));
    REQUIRE_FALSE(replay.value->contains("mutationSequence"));
    auto transform = validTransform();
    transform.insert("translation", QJsonArray{1.000000001, 0, 0});
    explicitDefaults.insert("transform", transform);
    const auto decodedFloat = api::ApiJsonCodec::canonicalParams("entity.create", explicitDefaults);
    REQUIRE(decodedFloat.hasValue());
    REQUIRE((*decodedFloat.value)["transform"].toObject()["translation"].toArray()[0] == 1);
    for (const auto& field : {"name", "expectedDocumentRevision", "timeoutMs"}) {
        auto changed = params;
        if (QString::fromLatin1(field) == "name")
            changed.insert(field, "different");
        else if (QString::fromLatin1(field) == "timeoutMs")
            changed.insert(field, 1);
        else
            changed.insert(field, "0");
        const auto other = api::ApiJsonCodec::canonicalParams("entity.create", changed);
        REQUIRE(other.hasValue());
        REQUIRE(*other.value != *canonical.value);
    }
    auto open = fixture.wireMutation();
    open.insert("path", "E:/approved/example.m3dscene");
    const auto openCanonical = api::ApiJsonCodec::canonicalParams("document.open", open);
    REQUIRE(openCanonical.hasValue());
    REQUIRE((*openCanonical.value)["ifDirty"] == "reject");
    open.insert("ifDirty", "reject");
    const auto explicitOpen = api::ApiJsonCodec::canonicalParams("document.open", open);
    REQUIRE(explicitOpen.hasValue());
    REQUIRE(*explicitOpen.value == *openCanonical.value);
    auto invalid = params;
    invalid.insert("unknown", true);
    const auto rejected = api::ApiJsonCodec::canonicalParams("entity.create", invalid);
    REQUIRE_FALSE(rejected.hasValue());
    REQUIRE(rejected.error->protocolCode == -32602);
}

TEST_CASE("Commit guards reject prepared entities and restore the previous guard",
          "[editor-api][api-commit-guard]") {
    ApiFixture fixture;
    const auto state = fixture.service.documentState();
    const api::ApiError rejected{api::ErrorCode::DeadlineExceeded, QStringLiteral("候选准备后到期"),
                                 "deadline", api::Recovery::QueryResult, state, -32071};
    int calls = 0;
    const auto guard = [&]() -> std::optional<api::ApiError> { ++calls; return rejected; };
    const auto params = validCreate(fixture);
    const auto result = api::ApiJsonCodec::invoke(fixture.service, "entity.create", params,
                                                  api::FileAccess::External, guard);
    REQUIRE(result["error"].toObject() == api::ApiJsonCodec::encodeError(rejected));
    REQUIRE(calls == 1);
    REQUIRE(fixture.model.scene()->nodes().empty());
    REQUIRE(fixture.model.undoStack()->count() == 0);
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision);
    REQUIRE(api::ApiJsonCodec::invoke(fixture.service, "entity.create", params).contains("result"));
    REQUIRE(calls == 1);
    int previousCalls = 0;
    fixture.service.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        ++previousCalls;
        return rejected;
    });
    REQUIRE(api::ApiJsonCodec::invoke(fixture.service, "entity.create", validCreate(fixture)).contains("result"));
    REQUIRE(previousCalls == 0);
    const auto direct = fixture.service.createEntity(fixture.createRequest());
    REQUIRE_FALSE(direct.hasValue());
    REQUIRE(api::ApiJsonCodec::encodeError(*direct.error) == api::ApiJsonCodec::encodeError(rejected));
    REQUIRE(previousCalls == 1);
    REQUIRE(fixture.model.undoStack()->count() == 2);
    fixture.service.exchangeBeforeCommitGuard({});
    REQUIRE(fixture.service.createEntity(fixture.createRequest()).hasValue());
}

TEST_CASE("Update and history no-change branches also check the scoped commit guard",
          "[editor-api][api-commit-guard]") {
    for (int branch = 0; branch < 6; ++branch) {
        CAPTURE(branch);
        ApiFixture fixture;
        const auto target = branch == 4 ? core::EntityId(0)
                                       : fixture.model.createEntity(core::PrimitiveKind::Cube);
        if (branch == 3)
            fixture.model.undo();
        QString method;
        auto params = fixture.wireMutation();
        if (branch < 2) {
            method = "entity.update";
            params.insert("entityId", QString::number(target));
            params.insert("name", branch == 0 ? QStringLiteral("更新候选")
                                              : QString::fromStdString(fixture.model.scene()->find(target)->name));
        } else {
            method = branch == 3 || branch == 5 ? "history.redo" : "history.undo";
            params.insert("expectedHistoryRevision", QString::number(fixture.service.documentState().historyRevision));
        }
        const auto state = fixture.service.documentState();
        const auto count = fixture.model.undoStack()->count();
        const auto index = fixture.model.undoStack()->index();
        const auto clean = fixture.model.undoStack()->cleanIndex();
        const auto selected = fixture.model.selection()->selectedEntity();
        const auto nodes = fixture.model.scene()->nodes().size();
        const api::ApiError rejected{api::ErrorCode::Cancelled, QStringLiteral("提交前取消"),
                                     "guard", api::Recovery::None, state};
        int calls = 0;
        const auto response = api::ApiJsonCodec::invoke(fixture.service, method, params,
            api::FileAccess::External, [&]() -> std::optional<api::ApiError> { ++calls; return rejected; });
        REQUIRE(response["error"].toObject() == api::ApiJsonCodec::encodeError(rejected));
        REQUIRE(calls == 1);
        REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
        REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision);
        REQUIRE(fixture.model.undoStack()->count() == count);
        REQUIRE(fixture.model.undoStack()->index() == index);
        REQUIRE(fixture.model.undoStack()->cleanIndex() == clean);
        REQUIRE(fixture.model.selection()->selectedEntity() == selected);
        REQUIRE(fixture.model.scene()->nodes().size() == nodes);
        const auto retried = api::ApiJsonCodec::invoke(fixture.service, method, params);
        REQUIRE(retried.contains("result"));
        const bool noChange = branch == 1 || branch == 4 || branch == 5;
        REQUIRE(retried["result"].toObject()["status"] == (noChange ? "no_change" : "committed"));
    }
}

TEST_CASE("File commit guards preserve the live document path and clean point",
          "[editor-api][api-commit-guard][api-file-policy]") {
    QTemporaryDir directory(temporaryTemplate());
    REQUIRE(directory.isValid());
    ApiFixture fixture;
    const auto target = fixture.model.createEntity(core::PrimitiveKind::Cube);
    const auto originalPath = directory.filePath("original.m3dscene");
    REQUIRE(fixture.model.saveScene(originalPath));
    const auto candidatePath = directory.filePath("candidate.m3dscene");
    QString method = "file.saveAs";
    SECTION("Save checks after temporary output is prepared") {}
    SECTION("Open checks after the candidate document is loaded") {
        method = "document.open";
        ApiFixture source;
        source.model.createEntity(core::PrimitiveKind::Sphere);
        REQUIRE(source.model.saveScene(candidatePath));
    }
    const auto state = fixture.service.documentState();
    const auto count = fixture.model.undoStack()->count();
    const auto index = fixture.model.undoStack()->index();
    const auto clean = fixture.model.undoStack()->cleanIndex();
    auto params = fixture.wireMutation();
    params.insert("path", candidatePath);
    const api::ApiError rejected{api::ErrorCode::DeadlineExceeded, QStringLiteral("文件候选到期"),
                                 "deadline", api::Recovery::QueryResult, state, -32072};
    int calls = 0;
    const auto guard = [&]() -> std::optional<api::ApiError> { ++calls; return rejected; };
    const auto response = api::ApiJsonCodec::invoke(fixture.service, method, params,
                                                    api::FileAccess::InternalTrusted, guard);
    REQUIRE(response["error"].toObject() == api::ApiJsonCodec::encodeError(rejected));
    REQUIRE(calls == 1);
    REQUIRE(fixture.model.filePath() == originalPath);
    REQUIRE(fixture.model.scene()->find(target)->primitive == core::PrimitiveKind::Cube);
    REQUIRE(fixture.service.documentState().document == state.document);
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision);
    REQUIRE(fixture.model.undoStack()->count() == count);
    REQUIRE(fixture.model.undoStack()->index() == index);
    REQUIRE(fixture.model.undoStack()->cleanIndex() == clean);
    REQUIRE_FALSE(fixture.model.isModified());
    if (method == "file.saveAs")
        REQUIRE_FALSE(QFileInfo::exists(candidatePath));
    else {
        const auto invalidPath = directory.filePath("invalid.m3dscene");
        writeBytes(invalidPath, "not-json");
        auto invalid = params;
        invalid.insert("path", invalidPath);
        const auto failedRead = api::ApiJsonCodec::invoke(fixture.service, method, invalid,
                                                          api::FileAccess::InternalTrusted, guard);
        REQUIRE(failedRead["error"].toObject()["data"].toObject()["code"] == "INVALID_ARGUMENT");
        REQUIRE(calls == 1);
    }
    REQUIRE(api::ApiJsonCodec::invoke(fixture.service, method, params,
                                     api::FileAccess::InternalTrusted).contains("result"));
    REQUIRE(calls == 1);
}

TEST_CASE("External API open applies its policy to dependencies before reading",
          "[editor-api][api-file-policy]") {
    QTemporaryDir directory(temporaryTemplate());
    REQUIRE(directory.isValid());
    const auto assetPath = directory.filePath("model.glb");
    REQUIRE(QFile::copy(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/Box.glb"), assetPath));
    ApiFixture source;
    REQUIRE(source.model.importGltf(assetPath) != 0);
    const auto scenePath = directory.filePath("imported.m3dscene");
    REQUIRE(source.model.saveScene(scenePath));
    ApiFixture fixture;
    const auto target = fixture.model.createEntity(core::PrimitiveKind::Cube);
    const auto originalPath = directory.filePath("original.m3dscene");
    REQUIRE(fixture.model.saveScene(originalPath));
    const auto state = fixture.service.documentState();
    bool sawAsset = false;
    fixture.service.setFileAccessPolicies([&](const QString& path, QString& error) {
        if (path == assetPath)
            sawAsset = true;
        error = "deliberately not a path-denial message";
        return path == scenePath;
    }, {});
    auto request = fixture.mutation<api::FileRequest>();
    request.path = scenePath;
    const auto denied = fixture.service.openDocument(request);
    REQUIRE_FALSE(denied.hasValue());
    REQUIRE(denied.error->code == api::ErrorCode::PathDenied);
    REQUIRE(sawAsset);
    REQUIRE(fixture.model.scene()->find(target)->primitive == core::PrimitiveKind::Cube);
    REQUIRE(fixture.service.documentState().document == state.document);
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision);
    REQUIRE(fixture.model.filePath() == originalPath);
    REQUIRE(fixture.model.undoStack()->count() == 1);
    REQUIRE_FALSE(fixture.model.isModified());
    fixture.service.setFileAccessPolicies([&](const QString& path, QString&) {
        return path == scenePath || path == assetPath;
    }, {});
    REQUIRE(fixture.service.openDocument(request).hasValue());
    REQUIRE(fixture.model.assets()->meshCount() == 1);
    fixture.service.setFileAccessPolicies({}, {});
    request = fixture.mutation<api::FileRequest>();
    request.path = scenePath;
    REQUIRE(fixture.service.openDocument(request).error->code == api::ErrorCode::PermissionDenied);
}

TEST_CASE("External save rechecks its policy and target before committing",
          "[editor-api][api-file-policy]") {
    QTemporaryDir directory(temporaryTemplate());
    REQUIRE(directory.isValid());
    ApiFixture fixture;
    fixture.model.createEntity(core::PrimitiveKind::Cube);
    bool targetAppears = false;
    SECTION("Policy becomes denied") {}
    SECTION("A new target appears") { targetAppears = true; }
    auto request = fixture.mutation<api::FileRequest>();
    request.path = directory.filePath("new.m3dscene");
    const auto state = fixture.service.documentState();
    const auto clean = fixture.model.undoStack()->cleanIndex();
    const QByteArray otherContent("other writer data");
    int checks = 0;
    fixture.service.setFileAccessPolicies({}, [&](const QString& path, QString& error) {
        REQUIRE(path == request.path);
        ++checks;
        if (checks == 2) {
            if (targetAppears)
                writeBytes(path, otherContent);
            else {
                error = "permission changed";
                return false;
            }
        }
        return true;
    });
    const auto denied = fixture.service.saveAs(request);
    REQUIRE_FALSE(denied.hasValue());
    REQUIRE(denied.error->code == (targetAppears ? api::ErrorCode::OverwriteDenied
                                              : api::ErrorCode::PathDenied));
    REQUIRE(checks == 2);
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision);
    REQUIRE(fixture.model.undoStack()->cleanIndex() == clean);
    REQUIRE(fixture.model.filePath().isEmpty());
    REQUIRE(fixture.model.isModified());
    if (targetAppears)
        REQUIRE(readBytes(request.path) == otherContent);
    else
        REQUIRE_FALSE(QFileInfo::exists(request.path));
}

TEST_CASE("SaveAs atomically refuses a target created by the last allowed guard",
          "[editor-api][api-file-policy][api-commit-guard][api-save-new-only]") {
    QTemporaryDir directory(temporaryTemplate());
    REQUIRE(directory.isValid());
    QString lastOperationError;
    ApiFixture fixture;
    QObject::connect(&fixture.model, &editor::SceneViewModel::operationFailed, &fixture.model,
                     [&](const QString& error) { lastOperationError = error; });
    const auto target = fixture.model.createEntity(core::PrimitiveKind::Cube);
    const auto originalPath = directory.filePath("saved.m3dscene");
    REQUIRE(fixture.model.saveScene(originalPath));
    REQUIRE(fixture.model.renameEntity(target, QStringLiteral("未保存更新")));
    fixture.service.setFileAccessPolicies({}, [](const QString&, QString&) { return true; });
    const auto newPath = directory.filePath("concurrent.m3dscene");
    REQUIRE_FALSE(QFileInfo::exists(newPath));
    const auto state = fixture.service.documentState();
    const auto count = fixture.model.undoStack()->count();
    const auto index = fixture.model.undoStack()->index();
    const auto clean = fixture.model.undoStack()->cleanIndex();
    const auto selected = fixture.model.selection()->selectedEntity();
    const auto originalBytes = readBytes(originalPath);
    const QByteArray marker("created after the last overwrite check");
    auto params = fixture.wireMutation();
    params.insert("path", newPath);
    int calls = 0;
    const auto response = api::ApiJsonCodec::invoke(fixture.service, "file.saveAs", params,
        api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
            ++calls;
            writeBytes(newPath, marker);
            return std::nullopt;
        });
    INFO(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString());
    INFO(lastOperationError.toStdString());
    REQUIRE(calls == 1);
    CHECK(response.contains("error"));
    CHECK(response["error"].toObject()["data"].toObject()["code"] == "OVERWRITE_DENIED");
    REQUIRE(QFileInfo::exists(newPath));
    CHECK(readBytes(newPath) == marker);
    CHECK(readBytes(originalPath) == originalBytes);
    CHECK(fixture.model.filePath() == originalPath);
    CHECK(fixture.model.isModified());
    CHECK(fixture.service.documentState().document == state.document);
    CHECK(fixture.service.documentState().documentRevision == state.documentRevision);
    CHECK(fixture.service.documentState().historyRevision == state.historyRevision);
    CHECK(fixture.model.undoStack()->count() == count);
    CHECK(fixture.model.undoStack()->index() == index);
    CHECK(fixture.model.undoStack()->cleanIndex() == clean);
    CHECK(fixture.model.selection()->selectedEntity() == selected);
    CHECK(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).size() == 2);
}

TEST_CASE("API NewOnly creates a new file while GUI saving retains replacement semantics",
          "[editor-api][api-save-new-only]") {
    QTemporaryDir directory(temporaryTemplate());
    REQUIRE(directory.isValid());
    QString lastOperationError;
    ApiFixture fixture;
    QObject::connect(&fixture.model, &editor::SceneViewModel::operationFailed, &fixture.model,
                     [&](const QString& error) { lastOperationError = error; });
    fixture.model.createEntity(core::PrimitiveKind::Cube);
    const auto existingPath = directory.filePath("gui-existing.m3dscene");
    const QByteArray marker("existing GUI target");
    writeBytes(existingPath, marker);
    REQUIRE(fixture.model.saveScene(existingPath));
    const auto sceneBytes = readBytes(existingPath);
    REQUIRE(sceneBytes != marker);
    fixture.service.setFileAccessPolicies({}, [](const QString&, QString&) { return true; });
    const auto newPath = directory.filePath("api-new.m3dscene");
    auto params = fixture.wireMutation();
    params.insert("path", newPath);
    const auto state = fixture.service.documentState();
    const auto response = api::ApiJsonCodec::invoke(fixture.service, "file.saveAs", params);
    INFO(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString());
    INFO(lastOperationError.toStdString());
    REQUIRE(response.contains("result"));
    REQUIRE(response["result"].toObject()["status"] == "saved");
    REQUIRE(readBytes(newPath) == sceneBytes);
    REQUIRE(fixture.model.filePath() == newPath);
    REQUIRE_FALSE(fixture.model.isModified());
    REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision + 1);
    REQUIRE(QDir(directory.path()).entryList(QDir::Files | QDir::Hidden).size() == 2);
}
