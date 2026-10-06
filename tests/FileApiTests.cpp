/*
 * 模块名: FileApiTests
 * 功能概述: 验证受控文件业务、完整导入子树及显式新建/打开的原子提交合同。
 * 对外接口: Catch2 [file-api] 测试。
 * 依赖关系: EditorApiService、ApiJsonCodec、SceneViewModel、Assets、路径策略、Qt、Catch2。
 * 输入输出: 隔离 E:/D: 临时文件、共享样例及显式对象到 typed/wire 和状态断言。
 * 异常与错误: 覆盖授权撤销、竞争目标、dirty、依赖/数量/字节限额和最终许可拒绝。
 * 维护说明: 不启动服务或构建；GUI 入口仅建立合法夹具和验证唯一历史回放。
 */
#include "assets/SceneDocument.h"
#include "core/modeling/ObjExporter.h"
#include "editor/SceneViewModel.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"
#include "editor/automation/FilePathPolicy.h"
#include "renderer_gl/PrimitiveFactory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <thread>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using namespace mini3d;
namespace {
namespace api = editor::api;
QString temporaryTemplate() {
    const auto root = qEnvironmentVariable("TMPDIR");
    REQUIRE(QDir::isAbsolutePath(root));
    REQUIRE(QDir(root).exists());
    return QDir(root).filePath("mini3d-file-api-XXXXXX");
}
void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}
void resizeFile(const QString& path, qint64 size) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadWrite));
    REQUIRE(file.resize(size));
}
QByteArray readBytes(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
QJsonObject localTransform(const core::Transform& value = {}) {
    return {{"space", "local"},
            {"translation", QJsonArray{value.position.x, value.position.y, value.position.z}},
            {"rotationQuaternion",
             QJsonArray{value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w}},
            {"scale", QJsonArray{value.scale.x, value.scale.y, value.scale.z}}};
}
struct FileFixture {
    QTemporaryDir directory{temporaryTemplate()};
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    FileFixture() {
        REQUIRE(directory.isValid());
        model.newScene();
        policies();
    }
    void policies(bool replace = true) {
        QString error;
        const auto prepared = editor::automation::FilePathPolicy::create({directory.path()},
                                                                         {directory.path()}, error);
        INFO(error.toStdString());
        REQUIRE(prepared);
        const auto policy = *prepared;
        service.setFileAccessPolicies(
            policy.readPolicy(),
            [policy](const QString& path, QString& reason) {
                return policy.authorizeNewFile(path, reason);
            },
            replace ? assets::FileReadPolicy{[policy](const QString& path, QString& reason) {
                return policy.authorizeWrite(path, reason);
            }}
                    : assets::FileReadPolicy{});
    }
    template <typename Request> Request request() const {
        Request result;
        result.document = service.documentState().document;
        result.expectedDocumentRevision = service.documentState().documentRevision;
        return result;
    }
    QJsonObject context() const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        return {{"document", state["document"]},
                {"expectedDocumentRevision", state["documentRevision"]}};
    }
    core::EntityId create(core::PrimitiveKind primitive = core::PrimitiveKind::Cube,
                          const QString& name = QStringLiteral("文件目标"),
                          core::EntityId parent = 0) {
        auto next = request<api::EntityCreateRequest>();
        next.primitive = primitive;
        next.name = name;
        next.parentId = parent;
        const auto result = service.createEntity(next);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    core::EntityId dirtyRedo() {
        const auto id = create();
        REQUIRE(model.saveScene(directory.filePath("old.m3dscene")));
        REQUIRE(model.renameEntity(id, QStringLiteral("未保存名称")));
        create(core::PrimitiveKind::Sphere, QStringLiteral("保留 redo"));
        model.undo();
        model.selection()->setSelectedEntity(id);
        REQUIRE(model.isModified());
        REQUIRE(model.undoStack()->canRedo());
        return id;
    }
};
struct GltfFixture {
    QString root;
    QJsonObject document;
    QByteArray buffer;
    explicit GltfFixture(const QString& directory) : root(directory) {
        const std::array<float, 9> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
        buffer = QByteArray(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
        document = {
            {"asset", QJsonObject{{"version", "2.0"}}},
            {"buffers",
             QJsonArray{QJsonObject{{"uri", "data.bin"}, {"byteLength", buffer.size()}}}},
            {"bufferViews", QJsonArray{QJsonObject{{"buffer", 0}, {"byteLength", buffer.size()}}}},
            {"accessors", QJsonArray{QJsonObject{{"bufferView", 0},
                                                 {"componentType", 5126},
                                                 {"count", 3},
                                                 {"type", "VEC3"},
                                                 {"min", QJsonArray{0, 0, 0}},
                                                 {"max", QJsonArray{1, 1, 0}}}}},
            {"meshes", QJsonArray{QJsonObject{
                           {"primitives", QJsonArray{QJsonObject{
                                              {"attributes", QJsonObject{{"POSITION", 0}}}}}}}}},
            {"nodes", QJsonArray{QJsonObject{{"name", "Parent"}, {"children", QJsonArray{1, 2}}},
                                 QJsonObject{{"name", "Triangle"},
                                             {"mesh", 0},
                                             {"translation", QJsonArray{1, 0, 0}}},
                                 QJsonObject{{"name", "Instance"}, {"mesh", 0}}}},
            {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}},
            {"scene", 0}};
    }
    void image() {
        const std::array<float, 6> coordinates{0, 0, 1, 0, 0, 1};
        const auto offset = buffer.size();
        buffer.append(reinterpret_cast<const char*>(coordinates.data()), sizeof(coordinates));
        auto buffers = document["buffers"].toArray();
        auto firstBuffer = buffers[0].toObject();
        firstBuffer.insert("byteLength", buffer.size());
        buffers[0] = firstBuffer;
        document.insert("buffers", buffers);
        auto views = document["bufferViews"].toArray();
        const auto viewIndex = views.size();
        views.append(QJsonObject{
            {"buffer", 0}, {"byteOffset", offset}, {"byteLength", qsizetype(sizeof(coordinates))}});
        document.insert("bufferViews", views);
        auto accessors = document["accessors"].toArray();
        const auto accessorIndex = accessors.size();
        accessors.append(QJsonObject{
            {"bufferView", viewIndex}, {"componentType", 5126}, {"count", 3}, {"type", "VEC2"}});
        document.insert("accessors", accessors);
        QImage image(2, 2, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        REQUIRE(image.save(QDir(root).filePath("image.png")));
        document.insert("images", QJsonArray{QJsonObject{{"uri", "image.png"}}});
        document.insert("textures", QJsonArray{QJsonObject{{"source", 0}}});
        document.insert("materials",
                        QJsonArray{QJsonObject{
                            {"pbrMetallicRoughness",
                             QJsonObject{{"baseColorTexture", QJsonObject{{"index", 0}}}}}}});
        auto meshes = document["meshes"].toArray();
        auto mesh = meshes[0].toObject();
        auto primitives = mesh["primitives"].toArray();
        auto primitive = primitives[0].toObject();
        auto attributes = primitive["attributes"].toObject();
        attributes.insert("TEXCOORD_0", accessorIndex);
        primitive.insert("attributes", attributes);
        primitive.insert("material", 0);
        primitives[0] = primitive;
        mesh.insert("primitives", primitives);
        meshes[0] = mesh;
        document.insert("meshes", meshes);
    }
    QString write(const QString& name = QStringLiteral("模型.gltf")) const {
        writeBytes(QDir(root).filePath("data.bin"), buffer);
        const auto path = QDir(root).filePath(name);
        writeBytes(path, QJsonDocument(document).toJson(QJsonDocument::Compact));
        return path;
    }
};
std::string sceneValue(const FileFixture& fixture) {
    core::SceneDocumentData data;
    data.nodes = fixture.model.scene()->nodes();
    data.editableMeshes = fixture.model.scene()->editableMeshes();
    data.collections = fixture.model.scene()->collections();
    data.lighting = fixture.model.scene()->lighting();
    data.camera = fixture.model.editorCamera();
    data.cursor = fixture.model.cursor3D();
    return core::SceneSerializer::encode(data);
}
struct Remembered {
    api::DocumentState state;
    std::string scene;
    QString path;
    bool modified, requiresSaveAs;
    int count, index, clean;
    const QUndoCommand* redo;
    core::EntityId selection, preview;
    std::shared_ptr<const assets::AssetManager> assets;
    quint64 componentRevision;
    editor::ComponentSelection components;
    QStringList busy;
    bool editMode;
};
Remembered remember(FileFixture& fixture) {
    const auto* history = fixture.model.undoStack();
    return {fixture.service.documentState(),
            sceneValue(fixture),
            fixture.model.filePath(),
            fixture.model.isModified(),
            fixture.model.requiresSaveAs(),
            history->count(),
            history->index(),
            history->cleanIndex(),
            history->canRedo() ? history->command(history->index()) : nullptr,
            fixture.model.selection()->selectedEntity(),
            fixture.model.previewCamera(),
            fixture.model.assets(),
            fixture.model.componentSelectionRevision(),
            fixture.model.componentSelection(),
            fixture.model.apiBusyReasons(true),
            fixture.model.isEditMode()};
}
void unchanged(FileFixture& fixture, const Remembered& before) {
    const auto& state = fixture.service.documentState();
    REQUIRE(state.document == before.state.document);
    REQUIRE(state.documentRevision == before.state.documentRevision);
    REQUIRE(state.historyRevision == before.state.historyRevision);
    REQUIRE(sceneValue(fixture) == before.scene);
    REQUIRE(fixture.model.filePath() == before.path);
    REQUIRE(fixture.model.isModified() == before.modified);
    REQUIRE(fixture.model.requiresSaveAs() == before.requiresSaveAs);
    const auto* history = fixture.model.undoStack();
    REQUIRE(history->count() == before.count);
    REQUIRE(history->index() == before.index);
    REQUIRE(history->cleanIndex() == before.clean);
    REQUIRE(history->canRedo() == (before.redo != nullptr));
    if (before.redo)
        REQUIRE(history->command(history->index()) == before.redo);
    REQUIRE(fixture.model.selection()->selectedEntity() == before.selection);
    REQUIRE(fixture.model.previewCamera() == before.preview);
    REQUIRE(fixture.model.assets() == before.assets);
    REQUIRE(fixture.model.componentSelectionRevision() == before.componentRevision);
    REQUIRE(fixture.model.componentSelection() == before.components);
    REQUIRE(fixture.model.apiBusyReasons(true) == before.busy);
    REQUIRE(fixture.model.isEditMode() == before.editMode);
}
QJsonObject importParams(FileFixture& fixture, const QString& path, core::EntityId parent = 0) {
    auto params = fixture.context();
    params.insert("path", path);
    params.insert("name", QStringLiteral("导入包装"));
    params.insert("parentId", QString::number(parent));
    params.insert("transform", localTransform());
    return params;
}
QJsonObject exportParams(FileFixture& fixture, const QString& path, core::EntityId entity,
                         const QString& mode = QStringLiteral("source"), bool overwrite = false) {
    auto params = fixture.context();
    params.insert("path", path);
    params.insert("entityId", QString::number(entity));
    params.insert("mode", mode);
    params.insert("overwrite", overwrite);
    return params;
}
QJsonObject call(FileFixture& fixture, bool wire, const QString& method, const QJsonObject& params,
                 api::BeforeCommitGuard guard = {}) {
    if (wire)
        return api::ApiJsonCodec::invoke(fixture.service, method, params, api::FileAccess::External,
                                         std::move(guard));
    auto previous = fixture.service.exchangeBeforeCommitGuard(std::move(guard));
    const auto restore = qScopeGuard([&] {
        fixture.service.exchangeBeforeCommitGuard(std::move(previous));
    });
    const auto envelope = [&](api::MutationRequest& request) {
        const auto document = params["document"].toObject();
        request.document = {document["instanceId"].toString(), document["documentId"].toString()};
        request.expectedDocumentRevision =
            params["expectedDocumentRevision"].toString().toULongLong();
        if (params.contains("timeoutMs"))
            request.timeoutMs = params["timeoutMs"].toInt();
        if (params.contains("clientSessionId"))
            request.clientSessionId = params["clientSessionId"].toString();
        if (params.contains("mutationSequence"))
            request.mutationSequence = params["mutationSequence"].toString().toULongLong();
    };
    if (method == "file.saveAs" || method == "document.open") {
        api::FileRequest request;
        envelope(request);
        request.path = params["path"].toString();
        request.ifDirty =
            params["ifDirty"] == "discard" ? api::IfDirty::Discard : api::IfDirty::Reject;
        return method == "file.saveAs"
                   ? api::ApiJsonCodec::response(fixture.service.saveAs(request))
                   : api::ApiJsonCodec::response(fixture.service.openDocument(request));
    }
    if (method == "file.save") {
        api::FileSaveRequest request;
        envelope(request);
        request.overwrite = params["overwrite"].toBool();
        return api::ApiJsonCodec::response(fixture.service.save(request));
    }
    if (method == "file.importGltf") {
        api::ImportGltfRequest request;
        envelope(request);
        request.path = params["path"].toString();
        request.name = params["name"].toString();
        request.parentId = params["parentId"].toString().toULongLong();
        const auto transform = api::ApiJsonCodec::parseTransform(params["transform"]);
        REQUIRE(transform.hasValue());
        request.transform = *transform.value;
        return api::ApiJsonCodec::response(fixture.service.importGltf(request));
    }
    if (method == "file.exportObj") {
        api::ExportObjRequest request;
        envelope(request);
        request.path = params["path"].toString();
        request.entityId = params["entityId"].toString().toULongLong();
        request.mode = params["mode"] == "evaluated" ? api::ExportObjMode::Evaluated
                                                     : api::ExportObjMode::Source;
        request.overwrite = params["overwrite"].toBool();
        return api::ApiJsonCodec::response(fixture.service.exportObj(request));
    }
    api::DocumentNewRequest request;
    envelope(request);
    request.ifDirty = params["ifDirty"] == "discard" ? api::IfDirty::Discard : api::IfDirty::Reject;
    return api::ApiJsonCodec::response(fixture.service.newDocument(request));
}
QString errorCode(const QJsonObject& response) {
    INFO(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString());
    REQUIRE(response.contains("error"));
    REQUIRE_FALSE(response.contains("result"));
    return response["error"].toObject()["data"].toObject()["code"].toString();
}
QJsonObject writeParams(FileFixture& fixture, const QString& method, core::EntityId entity,
                        const QString& path) {
    if (method == "file.exportObj")
        return exportParams(fixture, path, entity, "source", true);
    auto params = fixture.context();
    if (method == "file.save")
        params.insert("overwrite", true);
    else
        params.insert("path", path);
    return params;
}
QJsonObject readParams(FileFixture& fixture, const QString& method, const QString& path) {
    if (method == "file.importGltf")
        return importParams(fixture, path);
    auto params = fixture.context();
    params.insert("path", path);
    params.insert("ifDirty", "discard");
    return params;
}
bool moveDirectory(const QString& from, const QString& to) {
    const auto source = QDir::toNativeSeparators(from).toStdWString();
    const auto target = QDir::toNativeSeparators(to).toStdWString();
    return MoveFileExW(source.c_str(), target.c_str(), 0);
}
DWORD rawAttributes(const QString& path) {
    const auto native = QDir::toNativeSeparators(path).toStdWString();
    return GetFileAttributesW(native.c_str());
}
void requireOrdinaryAncestors(QString path) {
    for (;;) {
        const auto attributes = rawAttributes(path);
        INFO(path.toStdString());
        REQUIRE(attributes != INVALID_FILE_ATTRIBUTES);
        REQUIRE_FALSE(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
        const auto parent = QFileInfo(path).absolutePath();
        if (parent == path)
            break;
        path = parent;
    }
}
} // namespace

TEST_CASE("File codec consumes the frozen 9 valid and 19 invalid params samples", "[file-api]") {
    const auto samples =
        QJsonDocument::fromJson(
            readBytes(QStringLiteral(MINI3D_API_SCHEMA_DIRECTORY "/m5-files-examples.json")))
            .object();
    REQUIRE(samples["valid"].toArray().size() == 9);
    REQUIRE(samples["invalid"].toArray().size() == 19);
    for (const auto value : samples["valid"].toArray()) {
        const auto sample = value.toObject();
        const auto method = sample["method"].toString();
        INFO(method.toStdString());
        const auto decoded = api::ApiJsonCodec::decodeRequest(method, sample["params"]);
        REQUIRE(decoded.hasValue());
        const auto canonical = api::ApiJsonCodec::canonicalParams(method, sample["params"]);
        REQUIRE(canonical.hasValue());
        REQUIRE(api::ApiJsonCodec::decodeRequest(method, *canonical.value).hasValue());
    }
    for (const auto value : samples["invalid"].toArray()) {
        const auto sample = value.toObject();
        INFO(sample["description"].toString().toStdString());
        const auto decoded =
            api::ApiJsonCodec::decodeRequest(sample["method"].toString(), sample["params"]);
        REQUIRE_FALSE(decoded.hasValue());
        REQUIRE(decoded.error->code == api::ErrorCode::InvalidArgument);
        REQUIRE(decoded.error->protocolCode == -32602);
        REQUIRE_FALSE(
            api::ApiJsonCodec::canonicalParams(sample["method"].toString(), sample["params"])
                .hasValue());
    }
    FileFixture fixture;
    for (const auto method : {"file.save", "document.new", "document.open"}) {
        auto params = fixture.context();
        if (QString(method) == "document.open")
            params.insert("path", fixture.directory.filePath("a.m3dscene"));
        const auto omitted = api::ApiJsonCodec::canonicalParams(method, params);
        REQUIRE(omitted.hasValue());
        params.insert(QString(method) == "file.save" ? "overwrite" : "ifDirty",
                      QString(method) == "file.save" ? QJsonValue(false) : QJsonValue("reject"));
        REQUIRE(api::ApiJsonCodec::canonicalParams(method, params).value == omitted.value);
        params.insert("unknown", true);
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest(method, params).hasValue());
    }
    auto saveAs = fixture.context();
    saveAs.insert("path", fixture.directory.filePath("new.m3dscene"));
    saveAs.insert("overwrite", false);
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("file.saveAs", saveAs).hasValue());
}

TEST_CASE("File describe reports methods limits and independent replacement gates", "[file-api]") {
    FileFixture fixture;
    auto description = fixture.service.describe();
    REQUIRE(description.hasValue());
    REQUIRE(description.value->methods.size() == 43);
    for (const auto name : {"batch.createEntities", "batch.setTransforms"}) {
        REQUIRE(std::any_of(description.value->methods.begin(), description.value->methods.end(),
                            [name](const auto& method) {
                                return method.name == QString::fromLatin1(name);
                            }));
    }
    for (const auto name :
         {"fileReadBytes", "fileReadDependencies", "importedEntities", "exportObjBytes"})
        REQUIRE(description.value->limits.contains(name));
    REQUIRE(description.value->limits.at("fileReadBytes") == 67108864);
    REQUIRE(description.value->limits.at("fileReadDependencies") == 128);
    REQUIRE(description.value->limits.at("importedEntities") == 2048);
    REQUIRE(description.value->limits.at("exportObjBytes") == 67108864);
    fixture.service.setObservationAvailable(true);
    REQUIRE(fixture.service.describe().value->methods.size() == 47);
    fixture.service.setFileAccessPolicies({}, {});
    description = fixture.service.describe();
    for (const auto& method : description.value->methods) {
        if (method.name.startsWith("file.") || method.name == "document.open")
            REQUIRE_FALSE(method.externalEnabled);
        if (method.name == "document.new")
            REQUIRE(method.externalEnabled);
    }
}

TEST_CASE("Current-path save is explicit preserves history and marks only successful save points",
          "[file-api]") {
    for (const bool wire : {false, true}) {
        FileFixture fixture;
        const auto target = fixture.dirtyRedo();
        const auto before = remember(fixture);
        const auto original = readBytes(before.path);
        auto params = fixture.context();
        auto denied = call(fixture, wire, "file.save", params);
        REQUIRE_FALSE(denied.contains("result"));
        unchanged(fixture, before);
        REQUIRE(readBytes(before.path) == original);
        params.insert("overwrite", true);
        const auto saved = call(fixture, wire, "file.save", params)["result"].toObject();
        REQUIRE(saved["status"] == "saved");
        REQUIRE(saved["path"] == before.path);
        REQUIRE_FALSE(saved["undoable"].toBool());
        REQUIRE_FALSE(saved["selectionChanged"].toBool());
        REQUIRE(fixture.model.undoStack()->count() == before.count);
        REQUIRE(fixture.model.undoStack()->index() == before.index);
        REQUIRE(fixture.model.undoStack()->canRedo());
        REQUIRE(fixture.model.undoStack()->command(before.index) == before.redo);
        REQUIRE(fixture.model.selection()->selectedEntity() == target);
        REQUIRE(fixture.service.documentState().documentRevision == before.state.documentRevision);
        REQUIRE(fixture.service.documentState().historyRevision ==
                before.state.historyRevision + 1);
        REQUIRE_FALSE(fixture.model.isModified());
        REQUIRE(readBytes(before.path) != original);
        const auto clean = remember(fixture);
        params = fixture.context();
        params.insert("overwrite", true);
        REQUIRE(call(fixture, wire, "file.save", params)["result"].toObject()["status"] == "saved");
        unchanged(fixture, clean);
        fixture.model.undo();
        REQUIRE(fixture.model.isModified());
        fixture.model.redo();
        REQUIRE_FALSE(fixture.model.isModified());
    }
}

TEST_CASE("Save without a path or with a legacy original requires saveAs without writing",
          "[file-api]") {
    for (const bool wire : {false, true}) {
        FileFixture fixture;
        fixture.create();
        auto before = remember(fixture);
        auto params = fixture.context();
        params.insert("overwrite", true);
        REQUIRE(errorCode(call(fixture, wire, "file.save", params)) == "UNSUPPORTED_OPERATION");
        unchanged(fixture, before);
        const auto legacy = fixture.directory.filePath(QStringLiteral("旧版原件.m3dscene"));
        REQUIRE(fixture.model.saveScene(legacy));
        auto json = QJsonDocument::fromJson(readBytes(legacy)).object();
        json.insert("version", 2);
        json.remove("collections");
        json.remove("editableMeshes");
        const auto legacyBytes = QJsonDocument(json).toJson();
        writeBytes(legacy, legacyBytes);
        REQUIRE(fixture.model.openScene(legacy));
        REQUIRE(fixture.model.requiresSaveAs());
        before = remember(fixture);
        params = fixture.context();
        params.insert("overwrite", true);
        REQUIRE(errorCode(call(fixture, wire, "file.save", params)) == "UNSUPPORTED_OPERATION");
        unchanged(fixture, before);
        REQUIRE(readBytes(legacy) == legacyBytes);
        params = fixture.context();
        params.insert("path", fixture.directory.filePath("upgraded.m3dscene"));
        REQUIRE(call(fixture, wire, "file.saveAs", params)["result"].toObject()["status"] ==
                "saved");
        REQUIRE_FALSE(fixture.model.requiresSaveAs());
        REQUIRE(readBytes(legacy) == legacyBytes);
    }
}

TEST_CASE("File writers honor one final refusal without changing bytes or editor state",
          "[file-api]") {
    for (const bool wire : {false, true}) {
        for (const auto method : {"file.save", "file.saveAs", "file.exportObj"}) {
            FileFixture fixture;
            const auto entity = fixture.dirtyRedo();
            const auto before = remember(fixture);
            const auto original = readBytes(before.path);
            const auto target = QString(method) == "file.save"
                                    ? before.path
                                    : fixture.directory.filePath(QString(method) + ".out");
            if (QString(method) == "file.exportObj")
                writeBytes(target, "protected OBJ bytes");
            int calls = 0;
            const auto response =
                call(fixture, wire, method, writeParams(fixture, method, entity, target), [&] {
                    ++calls;
                    return api::ApiError{api::ErrorCode::Cancelled, "final refusal", "path",
                                         api::Recovery::None, before.state};
                });
            REQUIRE(errorCode(response) == "CANCELLED");
            REQUIRE(calls == 1);
            unchanged(fixture, before);
            REQUIRE(readBytes(before.path) == original);
            if (QString(method) == "file.saveAs")
                REQUIRE_FALSE(QFileInfo::exists(target));
            if (QString(method) == "file.exportObj")
                REQUIRE(readBytes(target) == "protected OBJ bytes");
        }
    }
}

TEST_CASE("Replacing file policies clears old coverage both before and at final commit",
          "[file-api]") {
    for (const bool wire : {false, true}) {
        for (const bool atCommit : {false, true}) {
            for (const auto method : {"file.save", "file.exportObj"}) {
                FileFixture fixture;
                const auto entity = fixture.dirtyRedo();
                const auto before = remember(fixture);
                const auto path = QString(method) == "file.save"
                                      ? before.path
                                      : fixture.directory.filePath("protected.obj");
                if (QString(method) == "file.exportObj")
                    writeBytes(path, "old OBJ");
                const auto original = readBytes(path);
                const auto revoke = [&] {
                    const auto allow = [](const QString&, QString&) {
                        return true;
                    };
                    fixture.service.setFileAccessPolicies(allow, allow);
                };
                if (!atCommit)
                    revoke();
                int calls = 0;
                const auto response =
                    call(fixture, wire, method, writeParams(fixture, method, entity, path), [&] {
                        ++calls;
                        revoke();
                        return std::optional<api::ApiError>{};
                    });
                REQUIRE(errorCode(response) == "PERMISSION_DENIED");
                REQUIRE(calls == (atCommit ? 1 : 0));
                REQUIRE(readBytes(path) == original);
                unchanged(fixture, before);
            }
        }
    }
}

TEST_CASE("Final writer policy and target rechecks preserve racing bytes and save points",
          "[file-api]") {
    for (const bool wire : {false, true}) {
        for (const auto method : {"file.saveAs", "file.exportObj"}) {
            for (const auto change : {"file", "directory", "policy", "io"}) {
                FileFixture fixture;
                const auto entity = fixture.dirtyRedo();
                const auto before = remember(fixture);
                const auto parent = fixture.directory.filePath("write-parent");
                REQUIRE(QDir().mkpath(parent));
                const auto path = QDir(parent).filePath("new.out");
                auto params = writeParams(fixture, method, entity, path);
                params.insert("overwrite", false);
                if (QString(method) == "file.saveAs")
                    params.remove("overwrite");
                int calls = 0;
                // IO 场景仅取消策略的物理路径检查，提交发布仍必须遵守真正的 NewOnly。
                if (QString(change) == "io") {
                    const auto allow = [](const QString&, QString&) {
                        return true;
                    };
                    fixture.service.setFileAccessPolicies(allow, allow, allow);
                }
                const auto response = call(fixture, wire, method, params, [&] {
                    ++calls;
                    if (QString(change) == "file")
                        writeBytes(path, "racing writer bytes");
                    else if (QString(change) == "directory")
                        REQUIRE(QDir().mkpath(path));
                    else if (QString(change) == "policy")
                        fixture.service.setFileAccessPolicies({},
                                                              [](const QString&, QString& reason) {
                                                                  reason = "revoked root";
                                                                  return false;
                                                              });
                    else
                        REQUIRE(QDir().rename(parent, parent + "-held"));
                    return std::optional<api::ApiError>{};
                });
                const auto code = errorCode(response);
                REQUIRE(calls == 1);
                if (QString(change) == "io")
                    REQUIRE(code == "IO_ERROR");
                else
                    REQUIRE((code == "PATH_DENIED" || code == "OVERWRITE_DENIED"));
                unchanged(fixture, before);
                if (QString(change) == "file")
                    REQUIRE(readBytes(path) == "racing writer bytes");
                else if (QString(change) == "directory")
                    REQUIRE(QFileInfo(path).isDir());
                else
                    REQUIRE_FALSE(QFileInfo::exists(path));
            }
        }
    }
}

TEST_CASE("API NewOnly final root recheck rejects a real swapped reparse ancestor",
          "[file-api][path-link]") {
    auto root = QDir::fromNativeSeparators(qEnvironmentVariable("MINI3D_FILE_API_REPARSE_FIXTURE"));
    if (root.isEmpty())
        SKIP("Set MINI3D_FILE_API_REPARSE_FIXTURE to an isolated precreated junction fixture");
    root = QDir::cleanPath(root);
    REQUIRE(root.startsWith("E:/CodexTemp/", Qt::CaseInsensitive));
    REQUIRE(QFileInfo(root).isDir());
    const auto approved = QDir(root).filePath("approved");
    const auto live = QDir(approved).filePath("live");
    const auto held = QDir(approved).filePath("held-live");
    const auto staged = QDir(approved).filePath("staged-link");
    const auto outside = QDir(root).filePath("outside");
    requireOrdinaryAncestors(live);
    requireOrdinaryAncestors(outside);
    REQUIRE(rawAttributes(staged) != INVALID_FILE_ATTRIBUTES);
    REQUIRE(rawAttributes(staged) & FILE_ATTRIBUTE_REPARSE_POINT);
    REQUIRE_FALSE(QFileInfo::exists(held));
    for (const bool wire : {false, true}) {
        for (const auto method : {"file.save", "file.saveAs", "file.exportObj"}) {
            FileFixture fixture;
            const auto entity = fixture.dirtyRedo();
            const auto name = QString("%1-%2.out").arg(method).arg(wire ? "wire" : "typed");
            const auto path = QDir(live).filePath(name);
            const auto external = QDir(outside).filePath(name);
            if (QString(method) == "file.save") {
                REQUIRE(fixture.model.saveScene(path));
                REQUIRE(QFile::remove(path));
            }
            REQUIRE_FALSE(QFileInfo::exists(path));
            writeBytes(path + ".marker", "approved marker");
            writeBytes(external, "outside protected");
            QString error;
            const auto policy = editor::automation::FilePathPolicy::create({}, {approved}, error);
            INFO(error.toStdString());
            REQUIRE(policy);
            fixture.service.setFileAccessPolicies(
                {},
                [policy](const QString& p, QString& reason) {
                    return policy->authorizeNewFile(p, reason);
                },
                [policy](const QString& p, QString& reason) {
                    return policy->authorizeWrite(p, reason);
                });
            const auto before = remember(fixture);
            auto params = writeParams(fixture, method, entity, path);
            if (QString(method) != "file.saveAs")
                params.insert("overwrite", false);
            bool movedOriginal = false, movedLink = false;
            const auto restore = qScopeGuard([&] {
                if (movedLink)
                    CHECK(moveDirectory(live, staged));
                if (movedOriginal)
                    CHECK(moveDirectory(held, live));
            });
            int calls = 0;
            const auto response = call(fixture, wire, method, params, [&] {
                ++calls;
                movedOriginal = moveDirectory(live, held);
                INFO(GetLastError());
                REQUIRE(movedOriginal);
                movedLink = moveDirectory(staged, live);
                INFO(GetLastError());
                REQUIRE(movedLink);
                return std::optional<api::ApiError>{};
            });
            REQUIRE(errorCode(response) == "PATH_DENIED");
            REQUIRE(calls == 1);
            unchanged(fixture, before);
            REQUIRE(readBytes(external) == "outside protected");
            REQUIRE(readBytes(QDir(held).filePath(name + ".marker")) == "approved marker");
            REQUIRE_FALSE(QFileInfo::exists(QDir(held).filePath(name)));
        }
    }
}

TEST_CASE("Approved existing external targets report explicit overwrite refusal",
          "[file-api][file-api-overwrite-classification]") {
    for (const bool wire : {false, true}) {
        FileFixture fixture;
        const auto entity = fixture.dirtyRedo();
        const auto before = remember(fixture);
        const auto obj = fixture.directory.filePath("existing.obj");
        writeBytes(obj, "existing OBJ bytes");
        for (const auto method : {"file.save", "file.exportObj"}) {
            auto params = writeParams(fixture, method, entity,
                                      QString(method) == "file.save" ? before.path : obj);
            params.insert("overwrite", false);
            int calls = 0;
            REQUIRE(errorCode(call(fixture, wire, method, params, [&] {
                        ++calls;
                        return std::optional<api::ApiError>{};
                    })) == "OVERWRITE_DENIED");
            REQUIRE(calls == 0);
            unchanged(fixture, before);
            REQUIRE(readBytes(obj) == "existing OBJ bytes");
        }
        QTemporaryDir outside(temporaryTemplate());
        REQUIRE(outside.isValid());
        const auto outsidePath = outside.filePath("outside.obj");
        writeBytes(outsidePath, "unapproved bytes");
        REQUIRE(errorCode(call(fixture, wire, "file.exportObj",
                               exportParams(fixture, outsidePath, entity))) == "PATH_DENIED");
        REQUIRE(readBytes(outsidePath) == "unapproved bytes");
        const auto deny = [](const QString&, QString& reason) {
            reason = "not an approved target";
            return false;
        };
        fixture.service.setFileAccessPolicies({}, deny, deny);
        REQUIRE(errorCode(call(fixture, wire, "file.exportObj",
                               exportParams(fixture, obj, entity))) == "PATH_DENIED");
        REQUIRE(readBytes(obj) == "existing OBJ bytes");
        unchanged(fixture, before);
    }
}

TEST_CASE("Import publishes the explicit complete preorder subtree as one replayable command",
          "[file-api]") {
    for (const bool wire : {false, true}) {
        FileFixture fixture;
        const auto parent = fixture.dirtyRedo();
        core::Transform parentTransform;
        parentTransform.position = {3, -2, 1};
        parentTransform.rotation = glm::angleAxis(0.7F, glm::vec3(0, 1, 0));
        REQUIRE(fixture.model.setTransform(parent, parentTransform));
        const auto unrelated = fixture.create(core::PrimitiveKind::Plane, "unrelated selection");
        fixture.model.selection()->setSelectedEntity(unrelated);
        GltfFixture source(fixture.directory.path());
        auto meshes = source.document["meshes"].toArray();
        auto mesh = meshes[0].toObject();
        auto primitives = mesh["primitives"].toArray();
        primitives.append(primitives[0]);
        mesh.insert("primitives", primitives);
        meshes[0] = mesh;
        source.document.insert("meshes", meshes);
        const auto path = source.write();
        core::Transform placement;
        placement.position = {4, 2, -3};
        placement.rotation = glm::angleAxis(-0.3F, glm::vec3(0, 0, 1));
        placement.scale = {2, 3, 1};
        auto params = importParams(fixture, path, parent);
        params.insert("name", "explicit wrapper");
        params.insert("transform", localTransform(placement));
        const auto before = remember(fixture);
        const auto* scene = fixture.model.scene().get();
        int guards = 0;
        const auto response = call(fixture, wire, "file.importGltf", params, [&] {
            ++guards;
            return std::optional<api::ApiError>{};
        });
        INFO(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString());
        REQUIRE(response.contains("result"));
        const auto result = response["result"].toObject();
        const auto values = result["created"].toObject()["entityIds"].toArray();
        REQUIRE(values.size() == 8);
        std::vector<core::EntityId> ids;
        for (const auto value : values)
            ids.push_back(value.toString().toULongLong());
        REQUIRE(result["rootEntityId"] == values[0]);
        REQUIRE(result["affectedEntityIds"] == values);
        REQUIRE(result["path"] == path);
        REQUIRE(result["warnings"].isArray());
        REQUIRE(result["status"] == "committed");
        REQUIRE(result["undoable"].toBool());
        REQUIRE_FALSE(result["selectionChanged"].toBool());
        REQUIRE(guards == 1);
        REQUIRE(fixture.model.scene().get() == scene);
        REQUIRE(fixture.model.selection()->selectedEntity() == unrelated);
        REQUIRE(fixture.model.undoStack()->count() == before.count + 1);
        REQUIRE(fixture.service.documentState().documentRevision ==
                before.state.documentRevision + 1);
        REQUIRE(fixture.service.documentState().historyRevision ==
                before.state.historyRevision + 1);
        const std::array<core::EntityId, 8> parents{parent, ids[0], ids[1], ids[2],
                                                    ids[2], ids[1], ids[5], ids[5]};
        for (std::size_t i = 0; i < ids.size(); ++i)
            REQUIRE(fixture.model.scene()->find(ids[i])->parent == parents[i]);
        REQUIRE(fixture.model.scene()->find(ids[0])->name == "explicit wrapper");
        REQUIRE(fixture.model.scene()->find(ids[1])->name == "Parent");
        REQUIRE(fixture.model.scene()->find(ids[2])->name == "Triangle");
        REQUIRE(fixture.model.scene()->find(ids[5])->name == "Instance");
        REQUIRE(fixture.model.scene()->find(ids[2])->children ==
                std::vector<core::EntityId>{ids[3], ids[4]});
        REQUIRE(fixture.model.scene()->find(ids[5])->children ==
                std::vector<core::EntityId>{ids[6], ids[7]});
        const auto expected = parentTransform.localMatrix() * placement.localMatrix() *
                              fixture.model.scene()->find(ids[2])->transform.localMatrix();
        const auto actual = fixture.model.scene()->worldMatrix(ids[3]);
        for (int column = 0; column < 4; ++column)
            REQUIRE(glm::length(expected[column] - actual[column]) < 1e-5F);
        const auto installed = sceneValue(fixture);
        int reads = 0;
        fixture.service.setFileAccessPolicies(
            [&](const QString&, QString&) {
                ++reads;
                return false;
            },
            {});
        REQUIRE(QFile::remove(path));
        REQUIRE(QFile::remove(fixture.directory.filePath("data.bin")));
        fixture.model.undo();
        REQUIRE(sceneValue(fixture) == before.scene);
        REQUIRE(fixture.model.selection()->selectedEntity() == unrelated);
        for (const auto id : ids)
            REQUIRE(fixture.model.scene()->find(id) == nullptr);
        fixture.model.redo();
        REQUIRE(sceneValue(fixture) == installed);
        fixture.model.selection()->setSelectedEntity(ids[3]);
        fixture.model.undo();
        REQUIRE(fixture.model.selection()->selectedEntity() == 0);
        fixture.model.undo(); // 原 GUI/API 对象命令仍作用于同一 Scene。
        REQUIRE(fixture.model.scene()->find(unrelated) == nullptr);
        fixture.model.redo();
        fixture.model.redo();
        REQUIRE(sceneValue(fixture) == installed);
        REQUIRE(reads == 0);
    }
}

TEST_CASE("Import failures preserve dirty redo and never leak candidate identities", "[file-api]") {
    for (const bool wire : {false, true}) {
        for (const auto failure : {"buffer", "image", "malformed", "missing", "guard"}) {
            FileFixture fixture;
            fixture.dirtyRedo();
            GltfFixture source(fixture.directory.path());
            source.image();
            const auto path = source.write();
            const auto before = remember(fixture);
            if (QString(failure) == "malformed")
                writeBytes(path, "{broken gltf");
            if (QString(failure) == "missing")
                REQUIRE(QFile::remove(path));
            if (QString(failure) == "buffer" || QString(failure) == "image") {
                const auto blocked = QString(failure) == "buffer" ? "data.bin" : "image.png";
                fixture.service.setFileAccessPolicies(
                    [blocked](const QString& p, QString& reason) {
                        if (QFileInfo(p).fileName() == blocked) {
                            reason = "dependency denied";
                            return false;
                        }
                        return true;
                    },
                    {});
            }
            int guards = 0;
            const auto response =
                call(fixture, wire, "file.importGltf", importParams(fixture, path), [&] {
                    ++guards;
                    return api::ApiError{api::ErrorCode::Cancelled,
                                         "import refusal",
                                         {},
                                         api::Recovery::None,
                                         before.state};
                });
            INFO("wire=" << wire << " fixture=" << failure);
            INFO(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString());
            const auto code = errorCode(response);
            if (QString(failure) == "guard") {
                REQUIRE(code == "CANCELLED");
                REQUIRE(guards == 1);
            } else {
                REQUIRE(guards == 0);
                REQUIRE(code ==
                        (QString(failure) == "malformed" ? "INVALID_ARGUMENT" : "PATH_DENIED"));
            }
            REQUIRE_FALSE(QJsonDocument(response).toJson().contains("\"created\""));
            REQUIRE_FALSE(QJsonDocument(response).toJson().contains("\"entityIds\""));
            REQUIRE_FALSE(QJsonDocument(response).toJson().contains("rootEntityId"));
            unchanged(fixture, before);
        }
    }
}

TEST_CASE("OBJ export uses the explicit world-space object and leaves all editor state unchanged",
          "[file-api]") {
    for (const bool wire : {false, true}) {
        for (const auto geometry : {"primitive", "editable", "imported"}) {
            FileFixture fixture;
            const auto parent = fixture.create(core::PrimitiveKind::Empty, "parent");
            core::Transform transform;
            transform.position = {4, 1, -2};
            transform.rotation = glm::angleAxis(0.5F, glm::vec3(0, 1, 0));
            transform.scale = {-2, 1, 1};
            REQUIRE(fixture.model.setTransform(parent, transform));
            auto entity = fixture.create(core::PrimitiveKind::Cube, "explicit OBJ", parent);
            if (QString(geometry) == "editable") {
                REQUIRE(fixture.model.makeEditable(entity));
                REQUIRE(fixture.model.setMirrorOptions(entity, core::modeling::MirrorOptions{}));
                REQUIRE(fixture.model.setSubdivisionOptions(entity,
                                                            core::modeling::SubdivisionOptions{}));
            }
            if (QString(geometry) == "imported") {
                GltfFixture source(fixture.directory.path());
                const auto response = call(fixture, wire, "file.importGltf",
                                           importParams(fixture, source.write(), parent));
                REQUIRE(response.contains("result"));
                entity = response["result"]
                             .toObject()["created"]
                             .toObject()["entityIds"]
                             .toArray()[2]
                             .toString()
                             .toULongLong();
            }
            const auto wrong = fixture.create(core::PrimitiveKind::Plane, "wrong GUI object");
            fixture.model.selection()->setSelectedEntity(wrong);
            const auto before = remember(fixture);
            QByteArray sourceBytes;
            for (const auto mode : {"source", "evaluated"}) {
                const auto path = fixture.directory.filePath(QString(mode) + ".obj");
                int guards = 0;
                const auto response = call(fixture, wire, "file.exportObj",
                                           exportParams(fixture, path, entity, mode), [&] {
                                               ++guards;
                                               return std::optional<api::ApiError>{};
                                           });
                REQUIRE(response.contains("result"));
                const auto result = response["result"].toObject();
                REQUIRE(result["status"] == "saved");
                REQUIRE(result["entityId"] == QString::number(entity));
                REQUIRE(result["mode"] == mode);
                REQUIRE_FALSE(result["undoable"].toBool());
                REQUIRE_FALSE(result["selectionChanged"].toBool());
                REQUIRE(guards == 1);
                const auto bytes = readBytes(path);
                REQUIRE(result["byteLength"].toInteger() == bytes.size());
                const auto& node = *fixture.model.scene()->find(entity);
                const glm::dmat4 world(fixture.model.scene()->worldMatrix(entity));
                core::modeling::ObjExportResult expected;
                if (node.editableMesh) {
                    const auto& content =
                        *fixture.model.scene()->editableMesh(node.editableMesh)->content;
                    expected = core::modeling::encodeObj(
                        QString(mode) == "source" ? content.source : content.evaluatedMesh(), world,
                        node.name);
                } else if (node.meshRenderer)
                    expected = core::modeling::encodeObj(
                        fixture.model.assets()->mesh(node.meshRenderer->mesh)->data, world,
                        node.name);
                else
                    expected = core::modeling::encodeObj(
                        renderer_gl::PrimitiveFactory::createCube(), world, node.name);
                REQUIRE(expected.text);
                REQUIRE(bytes == QByteArray::fromStdString(*expected.text));
                if (QString(mode) == "source")
                    sourceBytes = bytes;
                else
                    REQUIRE((bytes == sourceBytes) == (QString(geometry) != "editable"));
                unchanged(fixture, before);
            }
        }
    }
}

TEST_CASE("Explicit dirty discard is prepared before new or open can reset a document",
          "[file-api]") {
    for (const bool wire : {false, true}) {
        for (const auto method : {"document.new", "document.open"}) {
            FileFixture fixture;
            fixture.dirtyRedo();
            const auto path = fixture.directory.filePath("replacement.m3dscene");
            {
                editor::SceneViewModel replacement;
                replacement.newScene();
                replacement.createEntity(core::PrimitiveKind::Sphere);
                REQUIRE(replacement.saveScene(path));
            }
            const auto before = remember(fixture);
            auto params = fixture.context();
            if (QString(method) == "document.open")
                params.insert("path", path);
            int guards = 0;
            const auto reject = [&] {
                ++guards;
                return api::ApiError{api::ErrorCode::Cancelled,
                                     "discard refusal",
                                     {},
                                     api::Recovery::None,
                                     before.state};
            };
            REQUIRE(errorCode(call(fixture, wire, method, params, reject)) == "UNSAVED_CHANGES");
            REQUIRE(guards == 0);
            unchanged(fixture, before);
            params.insert("ifDirty", "discard");
            REQUIRE(errorCode(call(fixture, wire, method, params, reject)) == "CANCELLED");
            REQUIRE(guards == 1);
            unchanged(fixture, before);
            guards = 0;
            const auto response = call(fixture, wire, method, params, [&] {
                ++guards;
                return std::optional<api::ApiError>{};
            });
            REQUIRE(response.contains("result"));
            const auto result = response["result"].toObject();
            REQUIRE(result["status"] == "opened");
            REQUIRE(result.contains("path"));
            REQUIRE(result["path"] == (QString(method) == "document.new" ? QString{} : path));
            REQUIRE_FALSE(result["undoable"].toBool());
            REQUIRE(result["selectionChanged"].toBool());
            REQUIRE(guards == 1);
            const auto& state = fixture.service.documentState();
            REQUIRE(state.document.instanceId == before.state.document.instanceId);
            REQUIRE(state.document.documentId != before.state.document.documentId);
            REQUIRE(state.documentRevision == 1);
            REQUIRE(state.historyRevision == 1);
            REQUIRE(fixture.model.undoStack()->count() == 0);
            REQUIRE_FALSE(fixture.model.isModified());
            REQUIRE_FALSE(fixture.model.undoStack()->canUndo());
            REQUIRE_FALSE(fixture.model.undoStack()->canRedo());
            REQUIRE(fixture.model.selection()->selectedEntity() == 0);
            REQUIRE(fixture.model.previewCamera() == 0);
            REQUIRE(fixture.model.scene()->nodes().size() ==
                    (QString(method) == "document.new" ? 0 : 1));
            if (QString(method) == "document.open")
                REQUIRE(fixture.model.scene()->nodes()[0].primitive == core::PrimitiveKind::Sphere);
            api::EntityGetRequest stale;
            stale.document = before.state.document;
            stale.entityId = before.selection;
            REQUIRE(fixture.service.entity(stale).error->code == api::ErrorCode::StaleDocument);
        }
    }
}

TEST_CASE("Discard open failures retain the entire original dirty document", "[file-api]") {
    for (const bool wire : {false, true}) {
        for (const auto reason : {"malformed", "missing", "dependency"}) {
            FileFixture fixture;
            fixture.dirtyRedo();
            const auto before = remember(fixture);
            const auto path = fixture.directory.filePath("candidate.m3dscene");
            if (QString(reason) == "malformed")
                writeBytes(path, "not a scene");
            if (QString(reason) == "dependency") {
                editor::SceneViewModel candidate;
                candidate.newScene();
                GltfFixture source(fixture.directory.path());
                REQUIRE(candidate.importGltf(source.write()) != 0);
                REQUIRE(candidate.saveScene(path));
                fixture.service.setFileAccessPolicies(
                    [](const QString& p, QString& error) {
                        if (QFileInfo(p).suffix() == "gltf") {
                            error = "scene dependency denied";
                            return false;
                        }
                        return true;
                    },
                    {});
            }
            auto params = fixture.context();
            params.insert("path", path);
            params.insert("ifDirty", "discard");
            int guards = 0;
            const auto response = call(fixture, wire, "document.open", params, [&] {
                ++guards;
                return std::optional<api::ApiError>{};
            });
            REQUIRE(errorCode(response) ==
                    (QString(reason) == "malformed" ? "INVALID_ARGUMENT" : "PATH_DENIED"));
            REQUIRE(guards == 0);
            unchanged(fixture, before);
        }
    }
}

TEST_CASE("Read budgets reject oversized top files and aggregate dependencies before commit",
          "[file-api][file-api-limits]") {
    for (const bool wire : {false, true}) {
        for (const auto method : {"file.importGltf", "document.open"}) {
            for (const bool dependency : {false, true}) {
                FileFixture fixture;
                fixture.dirtyRedo();
                GltfFixture source(fixture.directory.path());
                auto path = source.write();
                if (QString(method) == "document.open") {
                    editor::SceneViewModel candidate;
                    candidate.newScene();
                    REQUIRE(candidate.importGltf(path) != 0);
                    path = fixture.directory.filePath("large-read.m3dscene");
                    REQUIRE(candidate.saveScene(path));
                }
                if (dependency)
                    resizeFile(fixture.directory.filePath("data.bin"), api::limits::fileReadBytes);
                else
                    resizeFile(path, api::limits::fileReadBytes + 1);
                const auto before = remember(fixture);
                int guards = 0;
                const auto response =
                    call(fixture, wire, method, readParams(fixture, method, path), [&] {
                        ++guards;
                        return std::optional<api::ApiError>{};
                    });
                REQUIRE(errorCode(response) == "LIMIT_EXCEEDED");
                REQUIRE(guards == 0);
                unchanged(fixture, before);
            }
        }
    }
}

TEST_CASE("Read path budgets count the top file and normalize repeated dependency aliases",
          "[file-api][file-api-limits]") {
    for (const bool wire : {false, true}) {
        for (const auto method : {"file.importGltf", "document.open"}) {
            for (const auto scenario : {"boundary", "aliases", "exceeded"}) {
                FileFixture fixture;
                fixture.dirtyRedo();
                GltfFixture source(fixture.directory.path());
                const int topFiles = QString(method) == "document.open" ? 2 : 1;
                const int dependencyCount = int(api::limits::fileReadDependencies) - topFiles +
                                            (QString(scenario) == "exceeded" ? 1 : 0);
                QJsonArray buffers;
                for (int i = 0; i < dependencyCount; ++i) {
                    const auto name = QString("buffer%1.bin").arg(i);
                    writeBytes(fixture.directory.filePath(name), source.buffer);
                    buffers.append(
                        QJsonObject{{"uri", name}, {"byteLength", source.buffer.size()}});
                }
                if (QString(scenario) == "aliases") {
                    REQUIRE(QDir().mkpath(fixture.directory.filePath("alias")));
                    for (const auto uri : {"buffer0.bin", "./buffer0.bin", "alias/../buffer0.bin"})
                        buffers.append(
                            QJsonObject{{"uri", uri}, {"byteLength", source.buffer.size()}});
                }
                source.document.insert("buffers", buffers);
                auto path = source.write();
                if (QString(method) == "document.open") {
                    editor::SceneViewModel candidate;
                    candidate.newScene();
                    REQUIRE(candidate.importGltf(path) != 0);
                    path = fixture.directory.filePath("read-paths.m3dscene");
                    REQUIRE(candidate.saveScene(path));
                }
                const auto before = remember(fixture);
                int guards = 0;
                const auto response =
                    call(fixture, wire, method, readParams(fixture, method, path), [&] {
                        ++guards;
                        return std::optional<api::ApiError>{};
                    });
                if (QString(scenario) == "exceeded") {
                    REQUIRE(errorCode(response) == "LIMIT_EXCEEDED");
                    REQUIRE(guards == 0);
                    unchanged(fixture, before);
                } else {
                    INFO(QJsonDocument(response).toJson(QJsonDocument::Compact).toStdString());
                    REQUIRE(response.contains("result"));
                    REQUIRE(guards == 1);
                }
            }
        }
    }
}

TEST_CASE("Repeated authorization rechecks growing cached dependency file sizes",
          "[file-api][file-api-limits]") {
    for (const bool wire : {false, true}) {
        for (const auto method : {"file.importGltf", "document.open"}) {
            FileFixture fixture;
            fixture.dirtyRedo();
            GltfFixture source(fixture.directory.path());
            if (QString(method) == "document.open") {
                auto meshes = source.document["meshes"].toArray();
                auto mesh = meshes[0].toObject();
                auto values = mesh["primitives"].toArray();
                values.append(values[0]);
                mesh.insert("primitives", values);
                meshes[0] = mesh;
                source.document.insert("meshes", meshes);
            }
            auto path = source.write();
            // 先建首次缓存；受控读取之后仍须重新核对所有原始依赖来源。
            if (QString(method) == "file.importGltf")
                REQUIRE(fixture.model.importGltf(path) != 0);
            else {
                editor::SceneViewModel candidate;
                candidate.newScene();
                REQUIRE(candidate.importGltf(path) != 0);
                path = fixture.directory.filePath("repeated-source.m3dscene");
                REQUIRE(candidate.saveScene(path));
            }
            const auto before = remember(fixture);
            int authorizations = 0;
            fixture.service.setFileAccessPolicies(
                [&](const QString& p, QString&) {
                    if (QFileInfo(p).fileName() == "data.bin" && ++authorizations == 2)
                        resizeFile(p, api::limits::fileReadBytes);
                    return true;
                },
                {});
            int guards = 0;
            const auto response =
                call(fixture, wire, method, readParams(fixture, method, path), [&] {
                    ++guards;
                    return std::optional<api::ApiError>{};
                });
            REQUIRE(errorCode(response) == "LIMIT_EXCEEDED");
            REQUIRE(authorizations == 2);
            REQUIRE(guards == 0);
            unchanged(fixture, before);
        }
    }
}

TEST_CASE("Import entity limits include the wrapper and each multi-primitive child",
          "[file-api][file-api-limits]") {
    for (const bool wire : {false, true}) {
        for (const bool primitives : {false, true}) {
            for (const bool exceeded : {false, true}) {
                FileFixture fixture;
                fixture.dirtyRedo();
                GltfFixture source(fixture.directory.path());
                const int childCount = primitives ? 2 : 0;
                const int count =
                    int(api::limits::importedEntities) - 1 - childCount + (exceeded ? 1 : 0);
                QJsonArray nodes, roots;
                for (int i = 0; i < count; ++i) {
                    QJsonObject node{{"name", QString("Node%1").arg(i)}};
                    if (primitives && i == 0)
                        node.insert("mesh", 0);
                    nodes.append(node);
                    roots.append(i);
                }
                if (primitives) {
                    auto meshes = source.document["meshes"].toArray();
                    auto mesh = meshes[0].toObject();
                    auto values = mesh["primitives"].toArray();
                    values.append(values[0]);
                    mesh.insert("primitives", values);
                    meshes[0] = mesh;
                    source.document.insert("meshes", meshes);
                }
                source.document.insert("nodes", nodes);
                source.document.insert("scenes", QJsonArray{QJsonObject{{"nodes", roots}}});
                const auto path = source.write();
                const auto before = remember(fixture);
                const auto originalCount = fixture.model.scene()->nodes().size();
                int guards = 0;
                const auto response =
                    call(fixture, wire, "file.importGltf", importParams(fixture, path), [&] {
                        ++guards;
                        return std::optional<api::ApiError>{};
                    });
                if (exceeded) {
                    REQUIRE(errorCode(response) == "LIMIT_EXCEEDED");
                    REQUIRE(guards == 0);
                    unchanged(fixture, before);
                } else {
                    REQUIRE(response.contains("result"));
                    REQUIRE(response["result"]
                                .toObject()["created"]
                                .toObject()["entityIds"]
                                .toArray()
                                .size() == qsizetype(api::limits::importedEntities));
                    REQUIRE(fixture.model.scene()->nodes().size() ==
                            api::limits::importedEntities + originalCount);
                    REQUIRE(guards == 1);
                }
            }
        }
    }
}

TEST_CASE("Large candidate names and OBJ output enforce their formal 64 MiB limits",
          "[file-api][file-api-limits]") {
    for (const bool wire : {false, true}) {
        for (const bool importing : {true, false}) {
            if (importing) {
                FileFixture fixture;
                fixture.dirtyRedo();
                GltfFixture source(fixture.directory.path());
                auto nodes = source.document["nodes"].toArray();
                auto first = nodes[0].toObject();
                first.insert("name",
                             QString(qsizetype(api::limits::candidateBytes / 2 + 2048), 'N'));
                nodes[0] = first;
                source.document.insert("nodes", nodes);
                const auto path = source.write();
                REQUIRE(QFileInfo(path).size() < qint64(api::limits::fileReadBytes));
                const auto before = remember(fixture);
                int guards = 0;
                REQUIRE(errorCode(call(fixture, wire, "file.importGltf",
                                       importParams(fixture, path), [&] {
                                           ++guards;
                                           return std::optional<api::ApiError>{};
                                       })) == "LIMIT_EXCEEDED");
                REQUIRE(guards == 0);
                unchanged(fixture, before);
            } else {
                FileFixture fixture;
                const auto entity = fixture.create();
                core::SceneDocumentData data;
                data.nodes = fixture.model.scene()->nodes();
                data.nodes[0].name.assign(api::limits::exportObjBytes + 1, 'N');
                data.camera = fixture.model.editorCamera();
                data.cursor = fixture.model.cursor3D();
                data.lighting = fixture.model.scene()->lighting();
                const auto path = fixture.directory.filePath("long-name.m3dscene");
                writeBytes(path, QByteArray::fromStdString(core::SceneSerializer::encode(data)));
                REQUIRE(fixture.model.openScene(path));
                fixture.model.selection()->setSelectedEntity(entity);
                const auto state = fixture.service.documentState();
                const auto* node = fixture.model.scene()->find(entity);
                const auto assets = fixture.model.assets();
                const auto output = fixture.directory.filePath("too-large.obj");
                writeBytes(output, "protected output");
                int guards = 0;
                REQUIRE(errorCode(call(fixture, wire, "file.exportObj",
                                       exportParams(fixture, output, entity, "source", true), [&] {
                                           ++guards;
                                           return std::optional<api::ApiError>{};
                                       })) == "LIMIT_EXCEEDED");
                REQUIRE(guards == 0);
                REQUIRE(readBytes(output) == "protected output");
                REQUIRE(fixture.model.scene()->find(entity) == node);
                REQUIRE(node->name.size() == api::limits::exportObjBytes + 1);
                REQUIRE(fixture.model.assets() == assets);
                REQUIRE(fixture.model.filePath() == path);
                REQUIRE_FALSE(fixture.model.isModified());
                REQUIRE(fixture.model.undoStack()->count() == 0);
                REQUIRE(fixture.model.selection()->selectedEntity() == entity);
                REQUIRE(fixture.service.documentState().document == state.document);
                REQUIRE(fixture.service.documentState().documentRevision == state.documentRevision);
                REQUIRE(fixture.service.documentState().historyRevision == state.historyRevision);
            }
        }
    }
}

TEST_CASE("Six file methods reject invalid envelopes stale state and Busy before IO or guard",
          "[file-api]") {
    for (const bool wire : {false, true}) {
        for (const auto method : {"file.save", "file.saveAs", "file.importGltf", "file.exportObj",
                                  "document.open", "document.new"}) {
            for (const auto reason : {"timeout", "session", "sequence", "identity", "revision",
                                      "busy", "preview", "edit"}) {
                FileFixture fixture;
                const auto entity = fixture.dirtyRedo();
                GltfFixture source(fixture.directory.path());
                const auto gltf = source.write();
                const auto originalBytes = readBytes(fixture.model.filePath());
                if (QString(reason) == "busy")
                    fixture.service.setExternalBusy("dialog", true);
                if (QString(reason) == "preview") {
                    fixture.model.beginTransformEdit(entity);
                    auto transform = fixture.model.scene()->find(entity)->transform;
                    transform.position.x = 12;
                    fixture.model.previewTransform(transform);
                }
                if (QString(reason) == "edit") {
                    REQUIRE(fixture.model.makeEditable(entity));
                    fixture.model.selection()->setSelectedEntity(entity);
                    REQUIRE(fixture.model.setEditMode(true));
                    fixture.model.selectComponent({1}, editor::SelectionOperation::Replace);
                }
                const auto before = remember(fixture);
                const auto target = fixture.directory.filePath("must-not-write.out");
                auto params = writeParams(fixture, method, entity, target);
                if (QString(method) == "file.importGltf")
                    params = importParams(fixture, gltf);
                if (QString(method) == "document.open") {
                    params = fixture.context();
                    params.insert("path", before.path);
                    params.insert("ifDirty", "discard");
                }
                if (QString(method) == "document.new") {
                    params = fixture.context();
                    params.insert("ifDirty", "discard");
                }
                QString expected = "INVALID_ARGUMENT";
                if (QString(reason) == "timeout")
                    params.insert("timeoutMs", 0);
                else if (QString(reason) == "session")
                    params.insert("clientSessionId", "invalid-session");
                else if (QString(reason) == "sequence")
                    params.insert("mutationSequence", "0");
                else if (QString(reason) == "identity") {
                    auto handle = params["document"].toObject();
                    handle.insert("documentId", "33333333-3333-4333-8333-333333333333");
                    params.insert("document", handle);
                    expected = "STALE_DOCUMENT";
                } else if (QString(reason) == "revision") {
                    params.insert("expectedDocumentRevision",
                                  QString::number(before.state.documentRevision - 1));
                    expected = "REVISION_CONFLICT";
                } else
                    expected = "BUSY";
                int guards = 0, reads = 0, writes = 0;
                fixture.service.setFileAccessPolicies(
                    [&](const QString&, QString&) {
                        ++reads;
                        return true;
                    },
                    [&](const QString&, QString&) {
                        ++writes;
                        return true;
                    },
                    [&](const QString&, QString&) {
                        ++writes;
                        return true;
                    });
                const auto response = call(fixture, wire, method, params, [&] {
                    ++guards;
                    return std::optional<api::ApiError>{};
                });
                INFO(method);
                INFO(reason);
                REQUIRE(errorCode(response) == expected);
                REQUIRE(guards == 0);
                REQUIRE(reads == 0);
                REQUIRE(writes == 0);
                REQUIRE(readBytes(before.path) == originalBytes);
                REQUIRE_FALSE(QFileInfo::exists(target));
                unchanged(fixture, before);
            }
        }
    }
}

TEST_CASE("Typed file targets dirty modes and transforms are validated without candidate commits",
          "[file-api]") {
    FileFixture fixture;
    const auto entity = fixture.dirtyRedo();
    GltfFixture source(fixture.directory.path());
    const auto path = source.write();
    const auto before = remember(fixture);
    int guards = 0;
    fixture.service.exchangeBeforeCommitGuard([&] {
        ++guards;
        return std::optional<api::ApiError>{};
    });
    auto fresh = fixture.request<api::DocumentNewRequest>();
    fresh.ifDirty = static_cast<api::IfDirty>(999);
    REQUIRE(fixture.service.newDocument(fresh).error->code == api::ErrorCode::InvalidArgument);
    auto open = fixture.request<api::FileRequest>();
    open.path = before.path;
    open.ifDirty = static_cast<api::IfDirty>(999);
    REQUIRE(fixture.service.openDocument(open).error->code == api::ErrorCode::InvalidArgument);
    open.ifDirty = api::IfDirty::Discard;
    open.path = fixture.directory.filePath("never-save.m3dscene");
    REQUIRE(fixture.service.saveAs(open).error->code == api::ErrorCode::InvalidArgument);
    auto imported = fixture.request<api::ImportGltfRequest>();
    imported.path = path;
    imported.name = "typed import";
    imported.parentId = std::numeric_limits<core::EntityId>::max();
    REQUIRE(fixture.service.importGltf(imported).error->code == api::ErrorCode::NotFound);
    imported.parentId = 0;
    imported.transform.scale.x = 0;
    REQUIRE(fixture.service.importGltf(imported).error->code == api::ErrorCode::InvalidArgument);
    imported.transform.scale.x = 1;
    imported.transform.rotation.w = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(fixture.service.importGltf(imported).error->code == api::ErrorCode::InvalidArgument);
    auto exported = fixture.request<api::ExportObjRequest>();
    exported.path = fixture.directory.filePath("never-export.obj");
    exported.entityId = entity;
    exported.mode = static_cast<api::ExportObjMode>(999);
    REQUIRE(fixture.service.exportObj(exported).error->code == api::ErrorCode::InvalidArgument);
    exported.mode = api::ExportObjMode::Source;
    exported.entityId = 0;
    REQUIRE(fixture.service.exportObj(exported).error->code == api::ErrorCode::InvalidArgument);
    exported.entityId = std::numeric_limits<core::EntityId>::max();
    REQUIRE(fixture.service.exportObj(exported).error->code == api::ErrorCode::NotFound);
    REQUIRE(guards == 0);
    REQUIRE_FALSE(QFileInfo::exists(exported.path));
    REQUIRE_FALSE(QFileInfo::exists(open.path));
    unchanged(fixture, before);
}

TEST_CASE("All six typed file services reject a wrong thread without executing the guard",
          "[file-api]") {
    FileFixture fixture;
    const auto entity = fixture.dirtyRedo();
    GltfFixture source(fixture.directory.path());
    const auto gltf = source.write();
    const auto before = remember(fixture);
    auto save = fixture.request<api::FileSaveRequest>();
    save.overwrite = true;
    auto saveAs = fixture.request<api::FileRequest>();
    saveAs.path = fixture.directory.filePath("wrong-thread.m3dscene");
    auto open = fixture.request<api::FileRequest>();
    open.path = before.path;
    open.ifDirty = api::IfDirty::Discard;
    auto imported = fixture.request<api::ImportGltfRequest>();
    imported.path = gltf;
    imported.name = "wrong thread import";
    auto exported = fixture.request<api::ExportObjRequest>();
    exported.path = fixture.directory.filePath("wrong-thread.obj");
    exported.entityId = entity;
    auto fresh = fixture.request<api::DocumentNewRequest>();
    fresh.ifDirty = api::IfDirty::Discard;
    int guards = 0;
    auto previous = fixture.service.exchangeBeforeCommitGuard([&] {
        ++guards;
        return std::optional<api::ApiError>{};
    });
    const auto restore = qScopeGuard([&] {
        fixture.service.exchangeBeforeCommitGuard(std::move(previous));
    });
    std::array<std::optional<api::ApiError>, 6> failures;
    std::thread thread([&] {
        failures[0] = fixture.service.save(save).error;
        failures[1] = fixture.service.saveAs(saveAs).error;
        failures[2] = fixture.service.importGltf(imported).error;
        failures[3] = fixture.service.exportObj(exported).error;
        failures[4] = fixture.service.openDocument(open).error;
        failures[5] = fixture.service.newDocument(fresh).error;
    });
    thread.join();
    for (const auto& error : failures) {
        REQUIRE(error);
        REQUIRE(error->code == api::ErrorCode::Internal);
    }
    REQUIRE(guards == 0);
    REQUIRE_FALSE(QFileInfo::exists(exported.path));
    REQUIRE_FALSE(QFileInfo::exists(saveAs.path));
    unchanged(fixture, before);
}

TEST_CASE("Approved replacement commit IO failures preserve original bytes and complete history",
          "[file-api][file-api-replacement-io]") {
    for (const bool wire : {false, true}) {
        for (const auto method : {"file.save", "file.exportObj"}) {
            FileFixture fixture;
            const auto entity = fixture.dirtyRedo();
            const auto before = remember(fixture);
            const auto path = QString(method) == "file.save"
                                  ? before.path
                                  : fixture.directory.filePath("locked-original.obj");
            if (QString(method) == "file.exportObj")
                writeBytes(path, "locked original OBJ bytes");
            const auto original = readBytes(path);
            const auto files = QDir(fixture.directory.path()).entryList(QDir::Files | QDir::Hidden);
            HANDLE locked = INVALID_HANDLE_VALUE;
            const auto release = qScopeGuard([&] {
                if (locked != INVALID_HANDLE_VALUE)
                    CHECK(CloseHandle(locked));
            });
            int guards = 0;
            const auto response =
                call(fixture, wire, method, writeParams(fixture, method, entity, path), [&] {
                    ++guards;
                    const auto native = QDir::toNativeSeparators(path).toStdWString();
                    // 临时内容已经写完；允许读取/写入，但真正替换必须取得的 Delete 共享被锁定。
                    locked = CreateFileW(native.c_str(), GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
                    INFO(GetLastError());
                    REQUIRE(locked != INVALID_HANDLE_VALUE);
                    REQUIRE(readBytes(path) == original);
                    return std::optional<api::ApiError>{};
                });
            REQUIRE(errorCode(response) == "IO_ERROR");
            REQUIRE(guards == 1);
            REQUIRE(readBytes(path) == original);
            REQUIRE(QDir(fixture.directory.path()).entryList(QDir::Files | QDir::Hidden) == files);
            unchanged(fixture, before);
        }
    }
}
