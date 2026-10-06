/*
 * 模块名: MeshApiTests
 * 功能概述: 验证冻结 M2 网格创建、源分页、算子及共享历史合同。
 * 对外接口: Catch2 测试用例。
 * 依赖关系: EditorApiService、ApiJsonCodec、SceneViewModel、Qt、Catch2。
 * 输入输出: 独立编辑器夹具到身份/候选预算/原子历史的断言。
 * 异常与错误: 同时覆盖 wire 参数拒绝、内核约束和版本冲突。
 * 维护说明: 不启动 GUI 模态或共享构建；临时文件随测试进程环境放置。
 */
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"
#include "editor/api/MeshApiSupport.h"
#include "editor/SceneViewModel.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <limits>

using namespace mini3d;
namespace {
namespace api = editor::api;
struct MeshFixture {
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    MeshFixture() { model.newScene(); }
    template <typename Request> Request mutation() const {
        Request result;
        result.document = service.documentState().document;
        result.expectedDocumentRevision = service.documentState().documentRevision;
        return result;
    }
    api::MeshCreateRequest quad() const {
        auto result = mutation<api::MeshCreateRequest>();
        result.name = QStringLiteral("开放四边面");
        result.positions = {{-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0}};
        result.faces = {{{0,1,2,3}, std::nullopt}};
        return result;
    }
    api::MeshIdentity createQuad() {
        const auto created = service.createMesh(quad());
        REQUIRE(created.hasValue());
        return created.value->mesh;
    }
    template <typename Request> Request meshMutation(const api::MeshIdentity& mesh) const {
        auto result = mutation<Request>();
        result.entityId = mesh.entityId;
        result.meshId = mesh.meshId;
        const auto* record = model.scene()->editableMesh(mesh.meshId);
        result.expectedTopologyRevision = record->topologyRevision;
        result.expectedGeometryRevision = record->geometryRevision;
        return result;
    }
    api::HistoryMutationRequest history() const {
        auto result = mutation<api::HistoryMutationRequest>();
        result.expectedHistoryRevision = service.documentState().historyRevision;
        return result;
    }
    api::MeshTargetRequest target(const api::MeshIdentity& mesh) const {
        api::MeshTargetRequest result;
        result.document = service.documentState().document;
        result.entityId = mesh.entityId;
        result.meshId = mesh.meshId;
        return result;
    }
    api::MeshSourcePageRequest page(const api::MeshIdentity& mesh, api::MeshDomain domain) const {
        api::MeshSourcePageRequest result;
        static_cast<api::MeshTargetRequest&>(result) = target(mesh);
        result.domain = domain;
        return result;
    }
    QJsonObject wireCreate() const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        return {{"document",state["document"]}, {"expectedDocumentRevision",state["documentRevision"]},
                {"name",QStringLiteral("wire 四边面")}, {"parentId","0"},
                {"transform",QJsonObject{{"space","local"},{"translation",QJsonArray{0,0,0}},
                                         {"rotationQuaternion",QJsonArray{0,0,0,1}}, {"scale",QJsonArray{1,1,1}}}},
                {"surface",QJsonObject{{"tint",QJsonArray{1,1,1}},{"useVertexColor",true},{"useTexture",false}}},
                {"positions",QJsonArray{QJsonArray{-1,-1,0},QJsonArray{1,-1,0},QJsonArray{1,1,0},QJsonArray{-1,1,0}}},
                {"faces",QJsonArray{QJsonObject{{"indices",QJsonArray{0,1,2,3}}}}}};
    }
    QJsonObject wireMeshMutation(const api::MeshIdentity& mesh) const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        const auto* record = model.scene()->editableMesh(mesh.meshId);
        return {{"document", state["document"]},
                {"expectedDocumentRevision", state["documentRevision"]},
                {"entityId", QString::number(mesh.entityId)}, {"meshId", QString::number(mesh.meshId)},
                {"expectedTopologyRevision", QString::number(record->topologyRevision)},
                {"expectedGeometryRevision", QString::number(record->geometryRevision)}};
    }
};
struct StateBefore {
    api::DocumentState document;
    int count, index, clean;
    core::EntityId selection;
};
StateBefore remember(MeshFixture& fixture) {
    const auto* history = fixture.model.undoStack();
    return {fixture.service.documentState(), history->count(), history->index(), history->cleanIndex(),
            fixture.model.selection()->selectedEntity()};
}
void unchanged(MeshFixture& fixture, const StateBefore& before) {
    REQUIRE(fixture.service.documentState().document == before.document.document);
    REQUIRE(fixture.service.documentState().documentRevision == before.document.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == before.document.historyRevision);
    REQUIRE(fixture.model.undoStack()->count() == before.count);
    REQUIRE(fixture.model.undoStack()->index() == before.index);
    REQUIRE(fixture.model.undoStack()->cleanIndex() == before.clean);
    REQUIRE(fixture.model.selection()->selectedEntity() == before.selection);
}
QString scratchTemplate() {
    const auto root = qEnvironmentVariable("TMPDIR");
    REQUIRE_FALSE(root.isEmpty());
    REQUIRE(QDir::isAbsolutePath(root));
    REQUIRE(QDir(root).exists());
    return QDir(root).filePath("mini3d-mesh-api-XXXXXX");
}
api::MeshCreateRequest cubeRequest(MeshFixture& fixture) {
    auto request = fixture.quad();
    request.positions.clear();
    request.faces.clear();
    const auto cube = core::modeling::createEditableCube();
    for (const auto& vertex : cube.vertices)
        request.positions.push_back(vertex.position);
    for (const auto& face : cube.faces) {
        api::MeshFaceInput input;
        input.corners.emplace();
        for (const auto& corner : face.corners) {
            input.indices.push_back(std::size_t(corner.vertex - 1));
            input.corners->push_back({corner.uv,corner.color,corner.normal});
        }
        request.faces.push_back(std::move(input));
    }
    return request;
}
} // namespace

TEST_CASE("M2 frozen samples are decoded by the shared codec", "[mesh-api][mesh-schema]") {
    MeshFixture fixture;
    QFile file(QDir(QString::fromUtf8(MINI3D_API_SCHEMA_DIRECTORY)).filePath("examples.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto samples = QJsonDocument::fromJson(file.readAll()).object()["m2"].toObject();
    const std::map<QString,QString> methods{{"create","mesh.create"},{"target","mesh.getSummary"},
        {"readSourcePage","mesh.readSourcePage"},{"extrudeRegion","mesh.extrudeRegion"},{"insetFace","mesh.insetFace"}};
    for (const auto& category : {"valid","invalid"}) {
        for (const auto& value : samples[category].toArray()) {
            const auto sample = value.toObject();
            const auto definition = sample["definition"].toString();
            CAPTURE(category, definition);
            QJsonValue params = sample["value"];
            QString method;
            if (definition == "faceInput" || definition == "cornerInput") {
                auto create = fixture.wireCreate();
                if (definition == "faceInput") {
                    create.insert("faces", QJsonArray{params});
                } else {
                    create.insert("faces", QJsonArray{QJsonObject{
                        {"indices", QJsonArray{0, 1, 2}},
                        {"corners", QJsonArray{params, QJsonObject{}, QJsonObject{}}}}});
                }
                params = create;
                method = "mesh.create";
            } else {
                REQUIRE(methods.contains(definition));
                method = methods.at(definition);
            }
            REQUIRE(api::ApiJsonCodec::decodeRequest(method, params).hasValue() ==
                    (QString::fromLatin1(category) == "valid"));
        }
    }
}
TEST_CASE("Five wire mesh methods compose using only returned identities and revisions",
          "[mesh-api][mesh-wire]") {
    MeshFixture fixture;
    const auto createResponse = api::ApiJsonCodec::invoke(fixture.service, "mesh.create",
                                                         fixture.wireCreate());
    REQUIRE_FALSE(createResponse.contains("error"));
    const auto created = createResponse["result"].toObject();
    const auto faceId = created["faceIdsByInputIndex"].toArray().first();
    REQUIRE(faceId.isString());
    REQUIRE(created["entityId"].isString());
    REQUIRE(created["meshId"].isString());
    QJsonObject target{{"document", created["document"]}, {"entityId", created["entityId"]},
                       {"meshId", created["meshId"]}};
    const auto summaryResponse = api::ApiJsonCodec::invoke(fixture.service, "mesh.getSummary", target);
    REQUIRE_FALSE(summaryResponse.contains("error"));
    const auto summary = summaryResponse["result"].toObject();
    REQUIRE(summary["source"].toObject()["faceCount"].toInt() == 1);
    auto page = target;
    page.insert("domain", "faces");
    const auto pageResponse = api::ApiJsonCodec::invoke(fixture.service, "mesh.readSourcePage", page);
    REQUIRE_FALSE(pageResponse.contains("error"));
    const auto sourceFace = pageResponse["result"].toObject()["faces"].toArray().first().toObject();
    REQUIRE(sourceFace["faceId"] == faceId);
    REQUIRE(sourceFace["material"] == "0");
    const auto corner = sourceFace["corners"].toArray().first().toObject();
    REQUIRE(corner["cornerId"] == created["cornerIdsByFace"].toArray().first().toArray().first());
    REQUIRE(corner["vertexId"] == created["vertexIdsByInputIndex"].toArray().first());
    REQUIRE(corner.contains("uv"));
    REQUIRE(corner["normal"].isNull());
    const auto mutation = [](const QJsonObject& result) {
        return QJsonObject{{"document", result["document"]},
                           {"expectedDocumentRevision", result["documentRevision"]},
                           {"entityId", result["entityId"]}, {"meshId", result["meshId"]},
                           {"expectedTopologyRevision", result["topologyRevision"]},
                           {"expectedGeometryRevision", result["geometryRevision"]}};
    };
    auto inset = mutation(created);
    inset.insert("faceId", faceId);
    inset.insert("thickness", .25);
    const auto insetResponse = api::ApiJsonCodec::invoke(fixture.service, "mesh.insetFace", inset);
    REQUIRE_FALSE(insetResponse.contains("error"));
    const auto inserted = insetResponse["result"].toObject();
    REQUIRE(inserted["innerFaceIds"].toArray() == QJsonArray{faceId});
    REQUIRE(inserted["rimFaceIds"].toArray().size() == 4);
    auto extrude = mutation(inserted);
    extrude.insert("faceIds", inserted["innerFaceIds"]);
    extrude.insert("space", "local");
    extrude.insert("offset", QJsonArray{0, 0, 1});
    const auto extrudeResponse = api::ApiJsonCodec::invoke(fixture.service, "mesh.extrudeRegion", extrude);
    REQUIRE_FALSE(extrudeResponse.contains("error"));
    const auto extruded = extrudeResponse["result"].toObject();
    REQUIRE(extruded["capFaceIds"] == inserted["innerFaceIds"]);
    REQUIRE(extruded["sideFaceIds"].toArray().size() == 4);
    REQUIRE(extruded["status"] == "committed");
    REQUIRE_FALSE(extruded["selectionChanged"].toBool());
    REQUIRE(fixture.model.undoStack()->count() == 3);
}
TEST_CASE("Mesh creation atomically publishes input identity maps without selecting", "[mesh-api][mesh-create]") {
    MeshFixture fixture;
    const auto parent = fixture.model.createEntity(core::PrimitiveKind::Empty);
    auto request = fixture.quad();
    request.parentId = parent;
    SECTION("Open boundary") {}
    SECTION("Empty") { request.positions.clear(); request.faces.clear(); }
    SECTION("Isolated vertices") { request.faces.clear(); }
    const auto before = remember(fixture);
    const auto created = fixture.service.createMesh(request);
    REQUIRE(created.hasValue());
    const auto& result = *created.value;
    REQUIRE(result.command.status == api::ResultStatus::Committed);
    REQUIRE(result.command.undoable);
    REQUIRE_FALSE(result.command.selectionChanged);
    REQUIRE(fixture.model.selection()->selectedEntity() == parent);
    REQUIRE(fixture.model.undoStack()->count() == before.count + 1);
    REQUIRE(result.command.state.documentRevision == before.document.documentRevision + 1);
    const auto* node = fixture.model.scene()->find(result.mesh.entityId);
    REQUIRE(node->parent == parent);
    REQUIRE(node->name == request.name.toUtf8().toStdString());
    REQUIRE(node->editableMesh == result.mesh.meshId);
    const auto& source = fixture.model.scene()->editableMesh(result.mesh.meshId)->content->source;
    REQUIRE(result.vertexIdsByInputIndex.size() == request.positions.size());
    REQUIRE(result.faceIdsByInputIndex.size() == request.faces.size());
    for (std::size_t face = 0; face < request.faces.size(); ++face) {
        REQUIRE(source.faces[face].id == result.faceIdsByInputIndex[face]);
        for (std::size_t corner = 0; corner < request.faces[face].indices.size(); ++corner) {
            REQUIRE(source.faces[face].corners[corner].id == result.cornerIdsByFace[face][corner]);
            REQUIRE(source.faces[face].corners[corner].vertex == result.vertexIdsByInputIndex[request.faces[face].indices[corner]]);
        }
    }
    const auto summary = fixture.service.meshSummary(fixture.target(result.mesh));
    REQUIRE(summary.hasValue());
    REQUIRE(summary.value->source.vertexCount == request.positions.size());
    REQUIRE(summary.value->source.boundaryEdgeCount == (request.faces.empty() ? 0 : 4));
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->find(result.mesh.entityId) == nullptr);
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->find(result.mesh.entityId)->editableMesh == result.mesh.meshId);
    REQUIRE(fixture.model.scene()->editableMesh(result.mesh.meshId)->topologyRevision > result.mesh.topologyRevision);
}
TEST_CASE("Mesh attributes and input identities survive save and reopen", "[mesh-api][mesh-create]") {
    MeshFixture fixture;
    auto request = fixture.quad();
    request.faces.front().corners = std::vector<api::MeshCornerInput>(4);
    (*request.faces.front().corners)[0] = {{.2F,.8F},{2,-1,.5F},glm::vec3(0,0,2)};
    const auto created = fixture.service.createMesh(request);
    REQUIRE(created.hasValue());
    const auto mesh = created.value->mesh;
    const auto source = fixture.model.scene()->editableMesh(mesh.meshId)->content->source;
    QTemporaryDir directory(scratchTemplate());
    REQUIRE(directory.isValid());
    auto save = fixture.mutation<api::FileRequest>();
    save.path = directory.filePath("属性往返.m3dscene");
    REQUIRE(fixture.service.saveAs(save,api::FileAccess::InternalTrusted).hasValue());
    REQUIRE_FALSE(fixture.model.isModified());
    MeshFixture reopened;
    auto open = reopened.mutation<api::FileRequest>();
    open.path = save.path;
    REQUIRE(reopened.service.openDocument(open,api::FileAccess::InternalTrusted).hasValue());
    REQUIRE(reopened.model.scene()->editableMesh(mesh.meshId)->content->source == source);
    const auto page = reopened.service.readSourcePage(reopened.page(mesh,api::MeshDomain::Faces));
    REQUIRE(page.hasValue());
    REQUIRE(page.value->faces.front().corners.front().attributes->color == glm::vec3(2,-1,.5F));
    REQUIRE(page.value->faces.front().corners.front().attributes->normal == glm::vec3(0,0,2));
}
TEST_CASE("Mesh summary counts evaluated geometry separately from render attribute splits",
          "[mesh-api][mesh-summary][mesh-modifier]") {
    MeshFixture fixture;
    const auto created = fixture.service.createMesh(cubeRequest(fixture));
    REQUIRE(created.hasValue());
    const auto mesh = created.value->mesh;
    SECTION("Source only") {}
    SECTION("Mirror") {
        core::modeling::MirrorOptions mirror;
        mirror.merge = false;
        REQUIRE(fixture.model.setMirrorOptions(mesh.entityId, mirror));
    }
    SECTION("Mirror then subdivision") {
        core::modeling::MirrorOptions mirror;
        mirror.merge = false;
        REQUIRE(fixture.model.setMirrorOptions(mesh.entityId, mirror));
        core::modeling::SubdivisionOptions subdivision;
        REQUIRE(fixture.model.setSubdivisionOptions(mesh.entityId, subdivision));
    }
    const auto content = fixture.model.scene()->editableMesh(mesh.meshId)->content;
    const auto before = remember(fixture);
    const auto summary = fixture.service.meshSummary(fixture.target(mesh));
    REQUIRE(summary.hasValue());
    REQUIRE(summary.value->source.vertexCount == content->source.vertices.size());
    REQUIRE(summary.value->evaluated.vertexCount == content->evaluatedMesh().vertices.size());
    REQUIRE(summary.value->evaluated.faceCount == content->evaluatedMesh().faces.size());
    REQUIRE(summary.value->evaluated.triangleCount == content->displayedDerived().mesh.indices.size() / 3);
    REQUIRE(content->displayedDerived().mesh.vertices.size() > summary.value->evaluated.vertexCount);
    const auto json = api::ApiJsonCodec::encode(*summary.value);
    REQUIRE(json["evaluated"].toObject()["vertexCount"].toInteger() ==
            qint64(content->evaluatedMesh().vertices.size()));
    unchanged(fixture, before);
}
TEST_CASE("Render split vertices use the byte budget rather than the geometry point limit",
          "[mesh-api][mesh-summary][mesh-budget]") {
    MeshFixture fixture;
    const auto cube = cubeRequest(fixture);
    auto request = fixture.quad();
    request.positions.clear();
    request.faces.clear();
    const auto cubeCount = api::limits::meshVertices / 24 + 1;
    for (std::size_t index = 0; index < cubeCount; ++index) {
        const auto first = request.positions.size();
        for (const auto& position : cube.positions)
            request.positions.push_back(position + glm::vec3(float(index) * 2, 0, 0));
        for (auto face : cube.faces) {
            for (auto& inputIndex : face.indices)
                inputIndex += first;
            request.faces.push_back(std::move(face));
        }
    }
    REQUIRE(request.positions.size() < api::limits::meshVertices);
    const auto created = fixture.service.createMesh(request);
    REQUIRE(created.hasValue());
    const auto content = fixture.model.scene()->editableMesh(created.value->mesh.meshId)->content;
    REQUIRE(content->displayedDerived().mesh.vertices.size() > api::limits::meshVertices);
    const auto summary = fixture.service.meshSummary(fixture.target(created.value->mesh));
    REQUIRE(summary.hasValue());
    REQUIRE(summary.value->evaluated.vertexCount == request.positions.size());
}
TEST_CASE("Typed and wire malformed meshes preserve scene history selection and redo", "[mesh-api][mesh-invalid]") {
    MeshFixture fixture;
    const auto mesh = fixture.createQuad();
    REQUIRE(fixture.model.renameEntity(mesh.entityId,QStringLiteral("临时名称")));
    fixture.model.undo();
    REQUIRE(fixture.model.undoStack()->canRedo());
    const auto before = remember(fixture);
    auto request = fixture.quad();
    SECTION("Missing input index") {request.faces.front().indices[0]=99;}
    SECTION("Repeated input index") {request.faces.front().indices[1]=0;}
    SECTION("Wrong corner count") {request.faces.front().corners=std::vector<api::MeshCornerInput>(3);}
    SECTION("Infinite position") {request.positions.front().x=std::numeric_limits<float>::infinity();}
    SECTION("Zero authored normal") {request.faces.front().corners=std::vector<api::MeshCornerInput>(4); (*request.faces.front().corners)[0].normal=glm::vec3(0);}
    SECTION("Invalid envelope") {request.timeoutMs=0;}
    const auto result = fixture.service.createMesh(request);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->code == api::ErrorCode::InvalidArgument);
    REQUIRE(result.error->protocolCode == -32602);
    unchanged(fixture,before);
    REQUIRE(fixture.model.scene()->nodes().size() == 1);
    auto wire = fixture.wireCreate();
    wire.insert("guess",true);
    const auto response = api::ApiJsonCodec::invoke(fixture.service,"mesh.create",wire);
    REQUIRE(response["error"].toObject()["code"].toInt() == -32602);
    unchanged(fixture,before);
}
TEST_CASE("Mesh quantity and total corner budgets are checked before topology", "[mesh-api][mesh-budget]") {
    MeshFixture fixture;
    auto request = fixture.quad();
    SECTION("Too many vertices") {request.positions.resize(api::limits::meshVertices+1);}
    SECTION("Too many faces") {request.faces.resize(api::limits::meshFaces+1,request.faces.front());}
    SECTION("Total corners") {
        request.positions.resize(api::limits::meshVertices);
        api::MeshFaceInput face;
        for (std::size_t index=0; index<api::limits::meshVertices; ++index) face.indices.push_back(index);
        request.faces.assign(api::limits::meshCorners/api::limits::meshVertices+1,face);
    }
    const auto before = remember(fixture);
    const auto result = fixture.service.createMesh(request);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->code == api::ErrorCode::LimitExceeded);
    unchanged(fixture,before);
    REQUIRE(fixture.model.scene()->nodes().empty());
}
TEST_CASE("Valid schema but invalid geometric topology has a domain error", "[mesh-api][mesh-invalid]") {
    MeshFixture fixture;
    auto wire = fixture.wireCreate();
    wire.insert("positions",QJsonArray{QJsonArray{0,0,0},QJsonArray{1,0,0},QJsonArray{2,0,0},QJsonArray{3,0,0}});
    REQUIRE(api::ApiJsonCodec::decodeRequest("mesh.create",wire).hasValue());
    const auto before=remember(fixture);
    const auto response=api::ApiJsonCodec::invoke(fixture.service,"mesh.create",wire);
    REQUIRE(response["error"].toObject()["data"].toObject()["code"] == "INVALID_TOPOLOGY");
    REQUIRE(response["error"].toObject()["code"] == -32010);
    unchanged(fixture,before);
}
TEST_CASE("Source pagination numerically sorts wide stable IDs and returns requested attributes", "[mesh-api][mesh-page]") {
    MeshFixture fixture;
    const auto created=fixture.service.createMesh(cubeRequest(fixture));
    REQUIRE(created.hasValue());
    const auto mesh=created.value->mesh;
    auto source=fixture.model.scene()->editableMesh(mesh.meshId)->content->source;
    constexpr std::uint64_t base=(std::uint64_t(1)<<53)+100;
    for (auto& vertex:source.vertices) vertex.id+=base;
    for (auto& face:source.faces) {
        face.id+=base;
        for (auto& corner:face.corners) {corner.id+=base;corner.vertex+=base;}
    }
    std::reverse(source.vertices.begin(),source.vertices.end());
    std::reverse(source.faces.begin(),source.faces.end());
    REQUIRE(fixture.model.replaceEditableMesh(mesh.entityId,source));
    const auto before=remember(fixture);
    auto request=fixture.page(mesh,api::MeshDomain::Vertices);
    request.limit=2;
    std::vector<std::uint64_t> ids;
    for (;;) {
        const auto page=fixture.service.readSourcePage(request);
        REQUIRE(page.hasValue());
        for (const auto& vertex:page.value->vertices) {
            REQUIRE(vertex.position.has_value());
            ids.push_back(vertex.vertexId);
        }
        const auto json=api::ApiJsonCodec::encode(*page.value);
        REQUIRE(json["vertices"].toArray().first().toObject()["vertexId"].isString());
        if (!page.value->nextCursor) break;
        request.cursor=page.value->nextCursor;
    }
    REQUIRE(ids.size()==source.vertices.size());
    REQUIRE(std::is_sorted(ids.begin(),ids.end()));
    REQUIRE(ids.front()>std::uint64_t(1)<<53);
    auto faces=fixture.page(mesh,api::MeshDomain::Faces);
    faces.fields=std::vector<api::MeshField>{};
    const auto page=fixture.service.readSourcePage(faces);
    REQUIRE(page.hasValue());
    REQUIRE(page.value->faces.front().faceId==base+1);
    REQUIRE_FALSE(page.value->faces.front().corners.front().attributes.has_value());
    const auto json=api::ApiJsonCodec::encode(*page.value);
    const auto corner=json["faces"].toArray().first().toObject()["corners"].toArray().first().toObject();
    REQUIRE(corner.contains("cornerId"));
    REQUIRE(corner.contains("vertexId"));
    REQUIRE_FALSE(corner.contains("uv"));
    unchanged(fixture,before);
}
TEST_CASE("Source cursors reject changed versions and mismatched query context", "[mesh-api][mesh-page]") {
    MeshFixture fixture;
    const auto mesh=fixture.createQuad();
    auto request=fixture.page(mesh,api::MeshDomain::Vertices);
    request.limit=1;
    const auto first=fixture.service.readSourcePage(request);
    REQUIRE(first.hasValue());
    REQUIRE(first.value->nextCursor.has_value());
    request.cursor=first.value->nextCursor;
    auto expected=api::ErrorCode::InvalidArgument;
    SECTION("Different entity") {++request.cursor->entityId;}
    SECTION("Different mesh") {++request.cursor->meshId;}
    SECTION("Different document") {request.cursor->document.documentId="00000000-0000-0000-0000-000000000000";}
    SECTION("Different domain") {request.cursor->domain=api::MeshDomain::Faces;}
    SECTION("Different fields") {request.fields=std::vector<api::MeshField>{};}
    SECTION("Nonexistent after identity") {request.cursor->afterId=999;}
    SECTION("Changed source revision") {++request.cursor->geometryRevision;expected=api::ErrorCode::RevisionConflict;}
    SECTION("Confirmed GUI change") {REQUIRE(fixture.model.renameEntity(mesh.entityId,"后续修改"));expected=api::ErrorCode::RevisionConflict;}
    const auto before=remember(fixture);
    const auto result=fixture.service.readSourcePage(request);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->code==expected);
    unchanged(fixture,before);
}
TEST_CASE("Inset output can directly feed extrusion and share GUI history", "[mesh-api][mesh-operators]") {
    MeshFixture fixture;
    const auto mesh=fixture.createQuad();
    const auto original=fixture.model.scene()->editableMesh(mesh.meshId)->content;
    REQUIRE(fixture.model.renameEntity(mesh.entityId,"GUI 命名"));
    const auto before=remember(fixture);
    auto inset=fixture.meshMutation<api::MeshInsetRequest>(mesh);
    inset.faceId=1;inset.thickness=.25;
    const auto inserted=fixture.service.insetFace(inset);
    REQUIRE(inserted.hasValue());
    REQUIRE(inserted.value->innerFaceIds==std::vector<core::modeling::FaceId>{1});
    REQUIRE(inserted.value->rimFaceIds.size()==4);
    REQUIRE_FALSE(inserted.value->command.selectionChanged);
    REQUIRE(fixture.model.undoStack()->count()==before.count+1);
    auto extrude=fixture.meshMutation<api::MeshExtrudeRequest>(inserted.value->mesh);
    extrude.faceIds=inserted.value->innerFaceIds;extrude.offset={0,0,1};
    const auto extruded=fixture.service.extrudeRegion(extrude);
    REQUIRE(extruded.hasValue());
    REQUIRE(extruded.value->capFaceIds==inserted.value->innerFaceIds);
    REQUIRE(extruded.value->sideFaceIds.size()==4);
    REQUIRE(fixture.model.undoStack()->count()==before.count+2);
    fixture.model.undo();
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->editableMesh(mesh.meshId)->content==original);
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->find(mesh.entityId)->name=="开放四边面");
    fixture.model.redo();
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    fixture.model.selection()->setSelectedEntity(mesh.entityId);
    REQUIRE(fixture.model.setEditMode(true));
    REQUIRE_FALSE(fixture.model.lastOperationDisabledReason().isEmpty());
}
TEST_CASE("Zero extrusion preserves clean redo and revisions but still validates identities", "[mesh-api][mesh-operators]") {
    MeshFixture fixture;
    const auto mesh=fixture.createQuad();
    QTemporaryDir directory(scratchTemplate());
    REQUIRE(directory.isValid());
    auto save=fixture.mutation<api::FileRequest>();save.path=directory.filePath("clean.m3dscene");
    REQUIRE(fixture.service.saveAs(save,api::FileAccess::InternalTrusted).hasValue());
    REQUIRE(fixture.model.renameEntity(mesh.entityId,"未确认后撤销"));fixture.model.undo();
    auto request=fixture.meshMutation<api::MeshExtrudeRequest>(mesh);request.faceIds={1};
    const auto before=remember(fixture);
    const auto content=fixture.model.scene()->editableMesh(mesh.meshId)->content;
    const auto result=fixture.service.extrudeRegion(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->command.status==api::ResultStatus::NoChange);
    REQUIRE_FALSE(result.value->command.undoable);
    REQUIRE(result.value->sideFaceIds.empty());
    REQUIRE(result.value->capFaceIds==request.faceIds);
    unchanged(fixture,before);
    REQUIRE(fixture.model.scene()->editableMesh(mesh.meshId)->content==content);
    REQUIRE(fixture.model.undoStack()->canRedo());
    REQUIRE_FALSE(fixture.model.isModified());
    request.faceIds={999};REQUIRE_FALSE(fixture.service.extrudeRegion(request).hasValue());
    request.faceIds={1};--request.expectedGeometryRevision;
    REQUIRE(fixture.service.extrudeRegion(request).error->code==api::ErrorCode::RevisionConflict);
    unchanged(fixture,before);
}
TEST_CASE("Zero extrusion still rejects disconnected regions and shells without a boundary",
          "[mesh-api][mesh-operators][mesh-zero-region]") {
    MeshFixture fixture;
    const auto created = fixture.service.createMesh(cubeRequest(fixture));
    REQUIRE(created.hasValue());
    const auto mesh = created.value->mesh;
    REQUIRE(fixture.model.renameEntity(mesh.entityId, QStringLiteral("撤销后保留分支")));
    fixture.model.undo();
    REQUIRE(fixture.model.undoStack()->canRedo());
    auto request = fixture.meshMutation<api::MeshExtrudeRequest>(mesh);
    SECTION("Opposite disconnected cube faces") {
        request.faceIds = {created.value->faceIdsByInputIndex[0],
                           created.value->faceIdsByInputIndex[1]};
    }
    SECTION("Entire closed cube shell") {
        request.faceIds = created.value->faceIdsByInputIndex;
    }
    const auto before = remember(fixture);
    const auto content = fixture.model.scene()->editableMesh(mesh.meshId)->content;
    const auto result = fixture.service.extrudeRegion(request);
    unchanged(fixture, before);
    REQUIRE(fixture.model.undoStack()->canRedo());
    REQUIRE(fixture.model.scene()->editableMesh(mesh.meshId)->content == content);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->code == api::ErrorCode::InvalidTopology);
}
TEST_CASE("Undo and a new topology branch reject old requests even when face IDs recur",
          "[mesh-api][mesh-operators][mesh-branch]") {
    MeshFixture fixture;
    const auto mesh = fixture.createQuad();
    auto inset = fixture.meshMutation<api::MeshInsetRequest>(mesh);
    inset.faceId = 1;
    inset.thickness = .25;
    const auto inserted = fixture.service.insetFace(inset);
    REQUIRE(inserted.hasValue());
    auto oldRequest = fixture.meshMutation<api::MeshInsetRequest>(inserted.value->mesh);
    oldRequest.faceId = inserted.value->rimFaceIds.front();
    oldRequest.thickness = .1;
    auto pageRequest = fixture.page(mesh, api::MeshDomain::Faces);
    pageRequest.limit = 1;
    const auto oldPage = fixture.service.readSourcePage(pageRequest);
    REQUIRE(oldPage.hasValue());
    REQUIRE(oldPage.value->nextCursor.has_value());
    pageRequest.cursor = oldPage.value->nextCursor;
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    auto extrude = fixture.meshMutation<api::MeshExtrudeRequest>(mesh);
    extrude.faceIds = {1};
    extrude.offset = {0, 0, 1};
    const auto branched = fixture.service.extrudeRegion(extrude);
    REQUIRE(branched.hasValue());
    REQUIRE_FALSE(fixture.model.undoStack()->canRedo());
    REQUIRE(std::find(branched.value->sideFaceIds.begin(), branched.value->sideFaceIds.end(),
                      oldRequest.faceId) != branched.value->sideFaceIds.end());
    const auto before = remember(fixture);
    const auto content = fixture.model.scene()->editableMesh(mesh.meshId)->content;
    const auto stale = fixture.service.insetFace(oldRequest);
    REQUIRE_FALSE(stale.hasValue());
    REQUIRE(stale.error->code == api::ErrorCode::RevisionConflict);
    // 新文档版本也不能把旧网格版本变成当前有效的组件请求。
    oldRequest.expectedDocumentRevision = fixture.service.documentState().documentRevision;
    const auto staleMesh = fixture.service.insetFace(oldRequest);
    REQUIRE_FALSE(staleMesh.hasValue());
    REQUIRE(staleMesh.error->code == api::ErrorCode::RevisionConflict);
    const auto stalePage = fixture.service.readSourcePage(pageRequest);
    REQUIRE_FALSE(stalePage.hasValue());
    REQUIRE(stalePage.error->code == api::ErrorCode::RevisionConflict);
    REQUIRE(fixture.model.scene()->editableMesh(mesh.meshId)->content == content);
    unchanged(fixture, before);
}
TEST_CASE("World extrusion uses the actual reflected nonuniform ancestor linear transform", "[mesh-api][mesh-world]") {
    MeshFixture fixture;
    auto parent=fixture.mutation<api::EntityCreateRequest>();parent.name="父节点";
    parent.transform.scale={-2,3,.5F};parent.transform.rotation=glm::angleAxis(.5F,glm::vec3(0,1,0));
    const auto parentResult=fixture.service.createEntity(parent);REQUIRE(parentResult.hasValue());
    auto create=fixture.quad();create.parentId=parentResult.value->createdEntityIds.front();
    create.transform.rotation=glm::angleAxis(.2F,glm::vec3(1,0,0));create.transform.scale={1,.75F,2};
    const auto created=fixture.service.createMesh(create);REQUIRE(created.hasValue());
    const auto mesh=created.value->mesh;
    const auto linear=glm::dmat3(fixture.model.scene()->worldMatrix(mesh.entityId));
    auto request=fixture.meshMutation<api::MeshExtrudeRequest>(mesh);request.faceIds={1};request.space=api::MeshSpace::World;
    request.offset=linear*glm::dvec3(0,0,1.25);
    const auto before=fixture.model.scene()->editableMesh(mesh.meshId)->content->source;
    const auto result=fixture.service.extrudeRegion(request);REQUIRE(result.hasValue());
    const auto& after=fixture.model.scene()->editableMesh(mesh.meshId)->content->source;
    const auto& cap=after.faces.front();
    for (std::size_t index=0;index<cap.corners.size();++index) {
        const auto delta=glm::dvec3(after.vertex(cap.corners[index].vertex)->position)-glm::dvec3(before.vertex(before.faces.front().corners[index].vertex)->position);
        REQUIRE(glm::length(linear*delta-request.offset)<1e-5);
    }
}
TEST_CASE("Mesh stale writes and busy interaction refuse without cancelling the GUI", "[mesh-api][mesh-invalid]") {
    MeshFixture fixture;
    const auto mesh=fixture.createQuad();
    auto request=fixture.meshMutation<api::MeshInsetRequest>(mesh);request.faceId=1;request.thickness=.2;
    auto expected=api::ErrorCode::RevisionConflict;
    SECTION("Document revision") {--request.expectedDocumentRevision;}
    SECTION("Topology revision") {--request.expectedTopologyRevision;}
    SECTION("Missing mesh binding") {++request.meshId;expected=api::ErrorCode::NotFound;}
    SECTION("Pending object preview") {fixture.model.beginTransformEdit(mesh.entityId);expected=api::ErrorCode::Busy;}
    SECTION("Edit mode") {fixture.model.selection()->setSelectedEntity(mesh.entityId);REQUIRE(fixture.model.setEditMode(true));expected=api::ErrorCode::Busy;}
    const auto before=remember(fixture);
    const auto content=fixture.model.scene()->editableMesh(mesh.meshId)->content;
    const auto result=fixture.service.insetFace(request);REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->code==expected);unchanged(fixture,before);
    REQUIRE(fixture.model.scene()->editableMesh(mesh.meshId)->content==content);
}
TEST_CASE("Modifier evaluation failure discards a valid operator candidate", "[mesh-api][mesh-modifier]") {
    MeshFixture fixture;
    const auto mesh=fixture.createQuad();
    auto source=fixture.model.scene()->editableMesh(mesh.meshId)->content->source;
    constexpr auto base=std::numeric_limits<std::uint64_t>::max()-24;
    for (auto& vertex:source.vertices) vertex.id+=base;
    for (auto& face:source.faces) {
        face.id+=base;
        for (auto& corner:face.corners) {corner.id+=base;corner.vertex+=base;}
    }
    REQUIRE(fixture.model.replaceEditableMesh(mesh.entityId,source));
    core::modeling::MirrorOptions options;options.merge=false;
    REQUIRE(fixture.model.setMirrorOptions(mesh.entityId,options));
    REQUIRE(fixture.model.renameEntity(mesh.entityId,"临时"));fixture.model.undo();
    const auto before=remember(fixture);
    const auto content=fixture.model.scene()->editableMesh(mesh.meshId)->content;
    auto request=fixture.meshMutation<api::MeshExtrudeRequest>(mesh);request.faceIds={base+1};request.offset={0,0,1};
    REQUIRE(core::modeling::extrudeRegion(source,{base+1},request.offset).mesh.has_value());
    const auto result=fixture.service.extrudeRegion(request);REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->code==api::ErrorCode::InvalidTopology);
    unchanged(fixture,before);
    REQUIRE(fixture.model.scene()->editableMesh(mesh.meshId)->content==content);
    REQUIRE(fixture.model.undoStack()->canRedo());
}
TEST_CASE("Evaluated output budgets include modifier topology and actual derived buffers", "[mesh-api][mesh-budget]") {
    MeshFixture fixture;
    auto create=fixture.quad();create.positions.resize(api::limits::meshVertices/2);
    const auto created=fixture.service.createMesh(create);REQUIRE(created.hasValue());
    const auto mesh=created.value->mesh;
    core::modeling::MirrorOptions mirror;mirror.merge=false;
    REQUIRE(fixture.model.setMirrorOptions(mesh.entityId,mirror));
    const auto before=remember(fixture);
    const auto content=fixture.model.scene()->editableMesh(mesh.meshId)->content;
    auto request=fixture.meshMutation<api::MeshExtrudeRequest>(mesh);request.faceIds={1};request.offset={0,0,1};
    const auto result=fixture.service.extrudeRegion(request);REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->code==api::ErrorCode::LimitExceeded);unchanged(fixture,before);
    REQUIRE(fixture.model.scene()->editableMesh(mesh.meshId)->content==content);
}
TEST_CASE("Two subdivision levels accept an inset whose actual geometry fits all limits",
          "[mesh-api][mesh-budget][mesh-subdivision-budget]") {
    MeshFixture fixture;
    const auto cube = cubeRequest(fixture);
    auto create = fixture.quad();
    create.positions.clear();
    create.faces.clear();
    for (std::size_t index = 0; index < 90; ++index) {
        const auto first = create.positions.size();
        for (const auto& position : cube.positions)
            create.positions.push_back(position + glm::vec3(float(index) * 2, 0, 0));
        for (auto face : cube.faces) {
            for (auto& inputIndex : face.indices)
                inputIndex += first;
            create.faces.push_back(std::move(face));
        }
    }
    const auto created = fixture.service.createMesh(create);
    REQUIRE(created.hasValue());
    const auto mesh = created.value->mesh;
    core::modeling::SubdivisionOptions subdivision;
    subdivision.levels = 2;
    REQUIRE(fixture.model.setSubdivisionOptions(mesh.entityId, subdivision));
    auto request = fixture.meshMutation<api::MeshInsetRequest>(mesh);
    request.faceId = created.value->faceIdsByInputIndex.front();
    request.thickness = .1;
    // 先证明内核候选及真实求值都在额度内，再要求 API 提交同一操作。
    const auto content = fixture.model.scene()->editableMesh(mesh.meshId)->content;
    const auto candidate = core::modeling::insetFace(content->source, request.faceId, request.thickness);
    REQUIRE(candidate.mesh.has_value());
    REQUIRE(candidate.mesh->vertices.size() == 724);
    REQUIRE(candidate.mesh->faces.size() == 544);
    REQUIRE(api::meshCornerCount(*candidate.mesh) == 2176);
    const auto evaluated = core::modeling::evaluateSubdivision(*candidate.mesh, subdivision.levels);
    REQUIRE(evaluated.evaluation.has_value());
    REQUIRE(evaluated.evaluation->mesh.vertices.size() == 8884);
    REQUIRE(evaluated.evaluation->mesh.faces.size() == 8704);
    REQUIRE(api::meshCornerCount(evaluated.evaluation->mesh) == 34816);
    const auto before = remember(fixture);
    const auto result = fixture.service.insetFace(request);
    INFO((result.error ? result.error->message.toStdString() : std::string{}));
    REQUIRE(result.hasValue());
    REQUIRE(result.value->command.status == api::ResultStatus::Committed);
    REQUIRE(result.value->innerFaceIds == std::vector<core::modeling::FaceId>{request.faceId});
    REQUIRE(fixture.model.undoStack()->count() == before.count + 1);
    REQUIRE(result.value->command.state.documentRevision == before.document.documentRevision + 1);
    REQUIRE(fixture.model.selection()->selectedEntity() == before.selection);
    const auto summary = fixture.service.meshSummary(fixture.target(mesh));
    REQUIRE(summary.hasValue());
    REQUIRE(summary.value->source.vertexCount == 724);
    REQUIRE(summary.value->evaluated.vertexCount == 8884);
    REQUIRE(summary.value->evaluated.faceCount == 8704);
}
TEST_CASE("Candidate byte budget totals capacities across all derived buffers",
          "[mesh-api][mesh-budget]") {
    MeshFixture fixture;
    core::EditableMeshContent content;
    content.derived.mesh.vertices.reserve(api::limits::candidateBytes / 2 / sizeof(core::MeshVertex) + 1);
    REQUIRE(content.derived.mesh.vertices.empty());
    REQUIRE_FALSE(api::checkMeshCandidateBudget(content, fixture.service.documentState()).has_value());
    core::modeling::EditableMesh retained;
    const core::modeling::EditableMesh* retainedSource = nullptr;
    SECTION("Two derived buffers") {
        content.derived.mesh.indices.reserve(api::limits::candidateBytes / 2 / sizeof(std::uint32_t) + 1);
    }
    SECTION("Caller still holds the source candidate") {
        retained.vertices.reserve(api::limits::candidateBytes / 2 / sizeof(core::modeling::EditableVertex) + 1);
        retainedSource = &retained;
    }
    REQUIRE(content.derived.mesh.indices.empty());
    const auto error = api::checkMeshCandidateBudget(content, fixture.service.documentState(), retainedSource);
    REQUIRE(error.has_value());
    REQUIRE(error->code == api::ErrorCode::LimitExceeded);
}

TEST_CASE("Canonical mesh parameters encode decoded attributes and paging defaults",
          "[mesh-api][api-canonical]") {
    MeshFixture fixture;
    const auto params = fixture.wireCreate();
    const auto canonical = api::ApiJsonCodec::canonicalParams("mesh.create", params);
    REQUIRE(canonical.hasValue());
    auto explicitDefaults = params;
    explicitDefaults.insert("timeoutMs", int(api::limits::mutationTimeoutMs));
    explicitDefaults.insert("faces", QJsonArray{QJsonObject{
        {"indices", QJsonArray{0, 1, 2, 3}},
        {"corners", QJsonArray{QJsonObject{}, QJsonObject{}, QJsonObject{}, QJsonObject{}}}}});
    auto positions = params["positions"].toArray();
    positions[0] = QJsonArray{-1.000000001, -1, 0};
    explicitDefaults.insert("positions", positions);
    const auto replay = api::ApiJsonCodec::canonicalParams("mesh.create", explicitDefaults);
    REQUIRE(replay.hasValue());
    REQUIRE(*replay.value == *canonical.value);
    REQUIRE(api::ApiJsonCodec::decodeRequest("mesh.create", *canonical.value).hasValue());
    const auto state = api::ApiJsonCodec::encodeState(fixture.service.documentState());
    QJsonObject page{{"document", state["document"]}, {"entityId", "1"},
                      {"meshId", "1"}, {"domain", "faces"}};
    const auto defaultPage = api::ApiJsonCodec::canonicalParams("mesh.readSourcePage", page);
    REQUIRE(defaultPage.hasValue());
    page.insert("fields", QJsonArray{"cornerAttributes"});
    page.insert("limit", int(api::limits::sourcePageDefault));
    const auto explicitPage = api::ApiJsonCodec::canonicalParams("mesh.readSourcePage", page);
    REQUIRE(explicitPage.hasValue());
    REQUIRE(*explicitPage.value == *defaultPage.value);
    page.insert("fields", QJsonArray{});
    const auto idOnly = api::ApiJsonCodec::canonicalParams("mesh.readSourcePage", page);
    REQUIRE(idOnly.hasValue());
    REQUIRE(*idOnly.value != *defaultPage.value);
}

TEST_CASE("Mesh guard refusal after preparation preserves source identity versions and history",
          "[mesh-api][api-commit-guard]") {
    for (int branch = 0; branch < 4; ++branch) {
        CAPTURE(branch);
        MeshFixture fixture;
        api::MeshIdentity mesh;
        if (branch)
            mesh = fixture.createQuad();
        const auto before = remember(fixture);
        const auto* record = branch ? fixture.model.scene()->editableMesh(mesh.meshId) : nullptr;
        const auto content = record ? record->content : nullptr;
        const api::ApiError rejected{api::ErrorCode::DeadlineExceeded, QStringLiteral("网格候选到期"),
                                     "deadline", api::Recovery::QueryResult, before.document, -32073};
        int calls = 0;
        const auto guard = [&]() -> std::optional<api::ApiError> { ++calls; return rejected; };
        QString method = "mesh.create";
        auto params = fixture.wireCreate();
        if (branch) {
            params = fixture.wireMeshMutation(mesh);
            const auto face = QString::number(content->source.faces.front().id);
            if (branch == 2) {
                method = "mesh.insetFace";
                params.insert("faceId", face);
                params.insert("thickness", 10);
                const auto invalidCandidate = api::ApiJsonCodec::invoke(fixture.service, method, params,
                                                                          api::FileAccess::External, guard);
                REQUIRE(invalidCandidate["error"].toObject()["data"].toObject()["code"] == "INVALID_TOPOLOGY");
                REQUIRE(calls == 0);
                params.insert("thickness", .25);
            } else {
                method = "mesh.extrudeRegion";
                params.insert("faceIds", QJsonArray{face});
                params.insert("space", "local");
                params.insert("offset", QJsonArray{0, 0, branch == 3 ? 0 : 1});
            }
        }
        const auto response = api::ApiJsonCodec::invoke(fixture.service, method, params,
                                                        api::FileAccess::External, guard);
        REQUIRE(response["error"].toObject() == api::ApiJsonCodec::encodeError(rejected));
        REQUIRE(calls == 1);
        unchanged(fixture, before);
        if (branch) {
            const auto* unchangedRecord = fixture.model.scene()->editableMesh(mesh.meshId);
            REQUIRE(unchangedRecord->content == content);
            REQUIRE(unchangedRecord->topologyRevision == mesh.topologyRevision);
            REQUIRE(unchangedRecord->geometryRevision == mesh.geometryRevision);
            REQUIRE(unchangedRecord->evaluationRevision == mesh.evaluationRevision);
        } else
            REQUIRE(fixture.model.scene()->nodes().empty());
        const auto retried = api::ApiJsonCodec::invoke(fixture.service, method, params);
        REQUIRE(retried.contains("result"));
        REQUIRE(retried["result"].toObject()["status"] == (branch == 3 ? "no_change" : "committed"));
        REQUIRE(calls == 1);
    }
}
