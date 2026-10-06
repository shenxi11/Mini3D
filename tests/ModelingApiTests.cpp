/*
 * 模块名: ModelingApiTests
 * 功能概述: 验证冻结 M5-02 六个显式建模事务及稳定源身份。
 * 对外接口: Catch2 [modeling-api] 测试。
 * 依赖关系: EditorApiService、ApiJsonCodec、SceneViewModel、Qt、Catch2。
 * 输入输出: 独立场景和 typed/wire 请求到候选、版本和共享历史断言。
 * 异常与错误: 覆盖合同拒绝、内核约束、Busy 和最终提交守卫。
 * 维护说明: 不启动 GUI 交互、不读选区补目标，构建由主代理串行执行。
 */
#include "core/modeling/MeshValidation.h"
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
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iterator>
#include <limits>
#include <set>
#include <type_traits>

using namespace mini3d;
namespace {
namespace api = editor::api;
using Domain = api::MeshComponentDomain;
using VertexId = core::modeling::VertexId;
using FaceId = core::modeling::FaceId;
using CornerId = core::modeling::CornerId;
using Edge = core::modeling::EdgeKey;
using Mesh = core::modeling::EditableMesh;

std::set<Edge> sourceEdges(const Mesh& source) {
    std::set<Edge> result;
    for (const auto& face : source.faces)
        for (std::size_t index = 0; index < face.corners.size(); ++index)
            result.emplace(face.corners[index].vertex,
                           face.corners[(index + 1) % face.corners.size()].vertex);
    return result;
}
std::vector<VertexId> sourceVertices(const Mesh& source) {
    std::vector<VertexId> result;
    for (const auto& vertex : source.vertices)
        result.push_back(vertex.id);
    std::sort(result.begin(), result.end());
    return result;
}
QJsonArray edgeJson(Edge edge) {
    return {QString::number(edge.first), QString::number(edge.second)};
}
QJsonArray idJson(const std::vector<VertexId>& values) {
    QJsonArray result;
    for (const auto value : values)
        result.append(QString::number(value));
    return result;
}
struct ModelingFixture {
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    ModelingFixture() {
        model.newScene();
    }
    template <typename Request> Request mutation() const {
        Request request;
        request.document = service.documentState().document;
        request.expectedDocumentRevision = service.documentState().documentRevision;
        return request;
    }
    core::EntityId primitive(core::PrimitiveKind kind = core::PrimitiveKind::Cube,
                             core::EntityId parent = 0, core::Transform transform = {}) {
        auto request = mutation<api::EntityCreateRequest>();
        request.name = QStringLiteral("显式建模对象");
        request.primitive = kind;
        request.parentId = parent;
        request.transform = transform;
        const auto result = service.createEntity(request);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    api::MeshIdentity cube(core::EntityId parent = 0, core::Transform transform = {}) {
        const auto entity = primitive(core::PrimitiveKind::Cube, parent, transform);
        auto request = mutation<api::MeshMakeEditableRequest>();
        request.entityId = entity;
        const auto result = service.makeEditable(request);
        REQUIRE(result.hasValue());
        return result.value->mesh;
    }
    const Mesh& source(const api::MeshIdentity& mesh) const {
        return model.scene()->editableMesh(mesh.meshId)->content->source;
    }
    template <typename Request> Request meshMutation(const api::MeshIdentity& mesh) const {
        auto request = mutation<Request>();
        request.entityId = mesh.entityId;
        request.meshId = mesh.meshId;
        const auto* record = model.scene()->editableMesh(mesh.meshId);
        request.expectedTopologyRevision = record->topologyRevision;
        request.expectedGeometryRevision = record->geometryRevision;
        return request;
    }
    api::HistoryMutationRequest history() const {
        auto request = mutation<api::HistoryMutationRequest>();
        request.expectedHistoryRevision = service.documentState().historyRevision;
        return request;
    }
    api::MeshIdentity hole(std::vector<VertexId>& loop) {
        const auto mesh = cube();
        const auto& face = source(mesh).faces.front();
        for (const auto& corner : face.corners)
            loop.push_back(corner.vertex);
        auto request = meshMutation<api::MeshDeleteComponentsRequest>(mesh);
        request.domain = Domain::Faces;
        request.faceIds = std::vector<FaceId>{face.id};
        const auto result = service.deleteComponents(request);
        REQUIRE(result.hasValue());
        return result.value->mesh;
    }
    QJsonObject context(core::EntityId entity) const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        QJsonObject result{{"document", state["document"]},
                           {"expectedDocumentRevision", state["documentRevision"]},
                           {"entityId", QString::number(entity)}};
        const auto* node = model.scene()->find(entity);
        if (node->editableMesh != 0) {
            const auto* record = model.scene()->editableMesh(node->editableMesh);
            result.insert("meshId", QString::number(node->editableMesh));
            result.insert("expectedTopologyRevision", QString::number(record->topologyRevision));
            result.insert("expectedGeometryRevision", QString::number(record->geometryRevision));
        }
        return result;
    }
};
struct Remembered {
    api::DocumentState state;
    int count, index, clean;
    const QUndoCommand* redo;
    core::EntityId selection;
    std::size_t entityCount;
    core::EntityId entity;
    core::MeshId mesh;
    core::PrimitiveKind primitive;
    std::shared_ptr<const core::EditableMeshContent> content;
    std::uint64_t topology = 0, geometry = 0, evaluation = 0;
};
Remembered remember(ModelingFixture& fixture, core::EntityId entity) {
    const auto* history = fixture.model.undoStack();
    const auto* node = fixture.model.scene()->find(entity);
    Remembered result{fixture.service.documentState(),
                      history->count(),
                      history->index(),
                      history->cleanIndex(),
                      history->canRedo() ? history->command(history->index()) : nullptr,
                      fixture.model.selection()->selectedEntity(),
                      fixture.model.scene()->nodes().size(),
                      entity,
                      node->editableMesh,
                      node->primitive,
                      nullptr};
    if (result.mesh != 0) {
        const auto* record = fixture.model.scene()->editableMesh(result.mesh);
        result.content = record->content;
        result.topology = record->topologyRevision;
        result.geometry = record->geometryRevision;
        result.evaluation = record->evaluationRevision;
    }
    return result;
}
void unchanged(ModelingFixture& fixture, const Remembered& before) {
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
    const auto* node = fixture.model.scene()->find(before.entity);
    REQUIRE(node->primitive == before.primitive);
    REQUIRE(node->editableMesh == before.mesh);
    if (before.mesh != 0) {
        const auto* record = fixture.model.scene()->editableMesh(before.mesh);
        REQUIRE(record->content == before.content);
        REQUIRE(record->topologyRevision == before.topology);
        REQUIRE(record->geometryRevision == before.geometry);
        REQUIRE(record->evaluationRevision == before.evaluation);
    }
}
void prepareRedo(ModelingFixture& fixture, core::EntityId other) {
    const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
    REQUIRE(fixture.model.renameEntity(other, QStringLiteral("待重做")));
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.undoStack()->canRedo());
    REQUIRE(fixture.model.undoStack()->isClean());
}
void historyRoundTrip(ModelingFixture& fixture, const api::MeshIdentity& mesh, const Mesh& before,
                      const Mesh& after) {
    const auto documentRevision = fixture.service.documentState().documentRevision;
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.source(mesh) == before);
    const auto afterUndo = fixture.model.scene()->editableMesh(mesh.meshId)->topologyRevision;
    REQUIRE(afterUndo > mesh.topologyRevision);
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    REQUIRE(fixture.source(mesh) == after);
    REQUIRE(fixture.model.scene()->find(mesh.entityId)->editableMesh == mesh.meshId);
    REQUIRE(fixture.model.scene()->editableMesh(mesh.meshId)->topologyRevision > afterUndo);
    REQUIRE(fixture.service.documentState().documentRevision == documentRevision + 2);
}
void selectWholeSource(api::MeshComponentsRequest& request, const Mesh& source, Domain domain) {
    request.domain = domain;
    if (domain == Domain::Vertices) {
        request.vertexIds = sourceVertices(source);
    } else if (domain == Domain::Edges) {
        const auto edges = sourceEdges(source);
        request.edges.emplace(edges.begin(), edges.end());
    } else {
        request.faceIds.emplace();
        for (const auto& face : source.faces)
            request.faceIds->push_back(face.id);
    }
}
template <typename Callback>
void dispatchModeling(ModelingFixture& fixture, const api::ApiRequest& request, Callback callback) {
    std::visit(
        [&](const auto& typed) {
            using Request = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<Request, api::MeshMakeEditableRequest>)
                callback(fixture.service.makeEditable(typed));
            else if constexpr (std::is_same_v<Request, api::MeshTransformComponentsRequest>)
                callback(fixture.service.transformComponents(typed));
            else if constexpr (std::is_same_v<Request, api::MeshBevelEdgeRequest>)
                callback(fixture.service.bevelEdge(typed));
            else if constexpr (std::is_same_v<Request, api::MeshLoopCutRequest>)
                callback(fixture.service.loopCut(typed));
            else if constexpr (std::is_same_v<Request, api::MeshDeleteComponentsRequest>)
                callback(fixture.service.deleteComponents(typed));
            else if constexpr (std::is_same_v<Request, api::MeshFillFaceRequest>)
                callback(fixture.service.fillFace(typed));
            else
                FAIL("Unexpected typed modeling request");
        },
        request);
}
QJsonObject modelingExamples() {
    QFile file(
        QDir(QString::fromUtf8(MINI3D_API_SCHEMA_DIRECTORY)).filePath("m5-modeling-examples.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto result = QJsonDocument::fromJson(file.readAll(), &error).object();
    REQUIRE(error.error == QJsonParseError::NoError);
    return result;
}
api::ApiRequest validTypedRequest(ModelingFixture& fixture, const QJsonObject& sample,
                                  core::EntityId& entity) {
    const auto method = sample["method"].toString();
    QJsonObject params = sample["value"].toObject();
    std::vector<VertexId> loop;
    api::MeshIdentity mesh;
    if (method == "mesh.makeEditable") {
        entity = fixture.primitive();
    } else {
        mesh = method == "mesh.fillFace" ? fixture.hole(loop) : fixture.cube();
        entity = mesh.entityId;
    }
    const auto context = fixture.context(entity);
    for (auto item = context.begin(); item != context.end(); ++item)
        params.insert(item.key(), item.value());
    if (method == "mesh.transformComponents")
        params.insert("vertexIds", idJson(sourceVertices(fixture.source(mesh))));
    if (method == "mesh.fillFace") {
        QJsonArray edges;
        for (std::size_t index = 0; index < loop.size(); ++index)
            edges.append(edgeJson({loop[index], loop[(index + 1) % loop.size()]}));
        params.insert("edges", edges);
    }
    auto decoded = api::ApiJsonCodec::decodeRequest(method, params);
    REQUIRE(decoded.hasValue());
    return std::move(*decoded.value);
}
} // namespace

TEST_CASE("M5 modeling codec consumes frozen samples and preserves canonical component identity",
          "[modeling-api][modeling-schema]") {
    const auto examples = modelingExamples();
    REQUIRE(examples["valid"].toArray().size() == 6);
    REQUIRE(examples["invalid"].toArray().size() == 24);
    for (const auto& category : {"valid", "invalid"}) {
        const bool expected = QString::fromLatin1(category) == "valid";
        for (const auto& item : examples[category].toArray()) {
            const auto sample = item.toObject();
            const auto method = "mesh." + sample["definition"].toString();
            CAPTURE(category, method);
            REQUIRE(api::ApiJsonCodec::decodeRequest(method, sample["value"]).hasValue() ==
                    expected);
            const auto canonical = api::ApiJsonCodec::canonicalParams(method, sample["value"]);
            REQUIRE(canonical.hasValue() == expected);
            if (!expected)
                continue;
            REQUIRE((*canonical.value)["entityId"] == "9007199254740993");
            REQUIRE((*canonical.value)["timeoutMs"] == int(api::limits::mutationTimeoutMs));
            REQUIRE(api::ApiJsonCodec::canonicalParams(method, *canonical.value).value ==
                    canonical.value);
            if (method == "mesh.bevelEdge")
                REQUIRE((*canonical.value)["edge"] == edgeJson({1, 2}));
            if (method == "mesh.loopCut")
                REQUIRE((*canonical.value)["slide"] == 0);
        }
    }
    auto fill = examples["valid"].toArray().last().toObject()["value"].toObject();
    fill.insert("edges", QJsonArray{QJsonArray{"1", "2"}, QJsonArray{"2", "1"}});
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("mesh.fillFace", fill).hasValue());
    ModelingFixture fixture;
    const auto description = fixture.service.describe();
    REQUIRE(description.hasValue());
    for (const auto& sample : examples["valid"].toArray()) {
        const auto name = sample.toObject()["method"].toString();
        REQUIRE(std::any_of(description.value->methods.begin(), description.value->methods.end(),
                            [&](const auto& method) {
                                return method.name == name;
                            }));
    }
}

TEST_CASE("Explicit makeEditable accepts hidden Cube and preserves primitive history and mesh IDs",
          "[modeling-api]") {
    ModelingFixture fixture;
    const auto cube = fixture.primitive();
    const auto other = fixture.primitive(core::PrimitiveKind::Empty);
    fixture.model.selection()->setSelectedEntity(other);
    REQUIRE(fixture.model.setVisible(cube, false));
    auto request = fixture.mutation<api::MeshMakeEditableRequest>();
    request.entityId = cube;
    const auto count = fixture.model.undoStack()->count();
    const auto result = fixture.service.makeEditable(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->command.status == api::ResultStatus::Committed);
    REQUIRE(result.value->command.undoable);
    REQUIRE_FALSE(result.value->command.selectionChanged);
    REQUIRE(result.value->mesh.entityId == cube);
    REQUIRE(result.value->mesh.meshId != 0);
    REQUIRE(fixture.model.selection()->selectedEntity() == other);
    REQUIRE(fixture.model.undoStack()->count() == count + 1);
    const auto source = fixture.source(result.value->mesh);
    REQUIRE(source == core::modeling::createEditableCube());
    const auto wire = api::ApiJsonCodec::encode(*result.value);
    REQUIRE(wire["meshId"] == QString::number(result.value->mesh.meshId));
    REQUIRE_FALSE(wire.contains("command"));
    REQUIRE_FALSE(wire.contains("mesh"));
    REQUIRE(fixture.service.undo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->find(cube)->editableMesh == 0);
    REQUIRE(fixture.model.scene()->find(cube)->primitive == core::PrimitiveKind::Cube);
    REQUIRE(fixture.service.redo(fixture.history()).hasValue());
    REQUIRE(fixture.model.scene()->find(cube)->editableMesh == result.value->mesh.meshId);
    REQUIRE(fixture.source(result.value->mesh) == source);
    prepareRedo(fixture, other);
    const auto before = remember(fixture, cube);
    request = fixture.mutation<api::MeshMakeEditableRequest>();
    request.entityId = cube;
    const auto repeated = fixture.service.makeEditable(request);
    REQUIRE(repeated.hasValue());
    REQUIRE(repeated.value->command.status == api::ResultStatus::NoChange);
    REQUIRE(repeated.value->mesh.meshId == result.value->mesh.meshId);
    unchanged(fixture, before);
}

TEST_CASE("Explicit component delta uses actual parent matrices for all component domains",
          "[modeling-api]") {
    for (const auto domain : {Domain::Vertices, Domain::Edges, Domain::Faces}) {
        for (const auto space : {api::MeshSpace::Local, api::MeshSpace::World}) {
            DYNAMIC_SECTION("domain=" << int(domain) << " space=" << int(space)) {
                ModelingFixture fixture;
                core::Transform parentTransform;
                parentTransform.position = {3, -2, 4};
                parentTransform.rotation = glm::angleAxis(0.7F, glm::normalize(glm::vec3(1, 2, 0)));
                parentTransform.scale = {-2, 3, 0.5F};
                const auto parent =
                    fixture.primitive(core::PrimitiveKind::Empty, 0, parentTransform);
                core::Transform local;
                local.position = {0.25F, -0.5F, 1};
                local.rotation = glm::angleAxis(0.4F, glm::vec3(0, 0, 1));
                local.scale = {1, 0.75F, 1.5F};
                const auto mesh = fixture.cube(parent, local);
                const auto other = fixture.primitive(core::PrimitiveKind::Empty);
                fixture.model.selection()->setSelectedEntity(other);
                fixture.model.setSnapMode(editor::SnapMode::Vertex);
                fixture.model.setProportionalEditingEnabled(true);
                REQUIRE(fixture.model.setProportionalRadius(100));
                const auto before = fixture.source(mesh);
                const auto world = glm::dmat4(fixture.model.scene()->worldMatrix(mesh.entityId));
                auto request = fixture.meshMutation<api::MeshTransformComponentsRequest>(mesh);
                selectWholeSource(request, before, domain);
                request.transform.space = space;
                request.transform.pivot = {0.2, -0.3, 0.1};
                request.transform.translation = {0.125, -0.25, 0.5};
                request.transform.rotation = 2.0 * glm::angleAxis(0.3, glm::dvec3(0, 1, 0));
                request.transform.scale = {-1, 1.25, 0.75};
                const auto count = fixture.model.undoStack()->count();
                const auto result = fixture.service.transformComponents(request);
                REQUIRE(result.hasValue());
                REQUIRE(result.value->command.status == api::ResultStatus::Committed);
                REQUIRE(result.value->affectedVertexIds == sourceVertices(before));
                REQUIRE(result.value->mesh.topologyRevision > mesh.topologyRevision);
                REQUIRE(result.value->mesh.geometryRevision > mesh.geometryRevision);
                REQUIRE(fixture.model.undoStack()->count() == count + 1);
                REQUIRE(fixture.model.selection()->selectedEntity() == other);
                const auto* node = fixture.model.scene()->find(mesh.entityId);
                REQUIRE(node->transform.position == local.position);
                REQUIRE(node->transform.rotation == local.rotation);
                REQUIRE(node->transform.scale == local.scale);
                const auto& delta = request.transform;
                const auto rotation = glm::normalize(delta.rotation);
                const auto after = fixture.source(mesh);
                for (const auto& vertex : before.vertices) {
                    const auto point = space == api::MeshSpace::Local
                                           ? glm::dvec3(vertex.position)
                                           : glm::dvec3(world * glm::dvec4(vertex.position, 1));
                    const auto transformed = delta.pivot + delta.translation +
                                             rotation * (delta.scale * (point - delta.pivot));
                    const auto expected =
                        space == api::MeshSpace::Local
                            ? transformed
                            : glm::dvec3(glm::affineInverse(world) * glm::dvec4(transformed, 1));
                    const auto actual = after.vertex(vertex.id)->position;
                    for (int axis = 0; axis < 3; ++axis)
                        REQUIRE(std::abs(double(actual[axis]) - expected[axis]) < 0.00001);
                }
                historyRoundTrip(fixture, result.value->mesh, before, after);
            }
        }
    }
}

TEST_CASE("Explicit identity component delta preserves source revisions redo and clean point",
          "[modeling-api]") {
    ModelingFixture fixture;
    core::Transform parentTransform;
    parentTransform.scale = {-2, 3, 0.5F};
    parentTransform.rotation = glm::angleAxis(0.75F, glm::vec3(0, 0, 1));
    const auto parent = fixture.primitive(core::PrimitiveKind::Empty, 0, parentTransform);
    const auto mesh = fixture.cube(parent);
    const auto other = fixture.primitive(core::PrimitiveKind::Empty);
    fixture.model.selection()->setSelectedEntity(other);
    prepareRedo(fixture, other);
    const auto before = remember(fixture, mesh.entityId);
    auto request = fixture.meshMutation<api::MeshTransformComponentsRequest>(mesh);
    selectWholeSource(request, fixture.source(mesh), Domain::Faces);
    request.transform.pivot = {100, -200, 300};
    request.transform.rotation = {-2, 0, 0, 0};
    const auto result = fixture.service.transformComponents(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->command.status == api::ResultStatus::NoChange);
    REQUIRE(result.value->affectedVertexIds.empty());
    unchanged(fixture, before);
}

TEST_CASE("Explicit bevel and midpoint loop cut return stable created topology and one history",
          "[modeling-api]") {
    for (const bool bevel : {true, false}) {
        DYNAMIC_SECTION((bevel ? "bevel" : "loop cut")) {
            ModelingFixture fixture;
            const auto mesh = fixture.cube();
            const auto other = fixture.primitive(core::PrimitiveKind::Empty);
            fixture.model.selection()->setSelectedEntity(other);
            const auto before = fixture.source(mesh);
            const auto count = fixture.model.undoStack()->count();
            api::MeshIdentity updated;
            if (bevel) {
                auto request = fixture.meshMutation<api::MeshBevelEdgeRequest>(mesh);
                request.edge.first = 2;
                request.edge.second = 1;
                request.width = 0.1;
                const auto result = fixture.service.bevelEdge(request);
                REQUIRE(result.hasValue());
                REQUIRE(result.value->command.status == api::ResultStatus::Committed);
                REQUIRE(result.value->bevelFaceId > before.faces.back().id);
                REQUIRE(std::any_of(fixture.source(mesh).faces.begin(),
                                    fixture.source(mesh).faces.end(), [&](const auto& face) {
                                        return face.id == result.value->bevelFaceId;
                                    }));
                REQUIRE(api::ApiJsonCodec::encode(*result.value)["bevelFaceId"] ==
                        QString::number(result.value->bevelFaceId));
                updated = result.value->mesh;
            } else {
                auto request = fixture.meshMutation<api::MeshLoopCutRequest>(mesh);
                request.seedEdge = {2, 1};
                const auto result = fixture.service.loopCut(request);
                REQUIRE(result.hasValue());
                REQUIRE(result.value->command.status == api::ResultStatus::Committed);
                REQUIRE_FALSE(result.value->cutEdges.empty());
                REQUIRE(
                    std::is_sorted(result.value->cutEdges.begin(), result.value->cutEdges.end()));
                const auto available = sourceEdges(fixture.source(mesh));
                for (const auto& edge : result.value->cutEdges) {
                    REQUIRE(edge.first < edge.second);
                    REQUIRE(available.contains(edge));
                }
                REQUIRE(fixture.source(mesh).vertices.size() > before.vertices.size());
                updated = result.value->mesh;
            }
            REQUIRE(fixture.model.undoStack()->count() == count + 1);
            REQUIRE(fixture.model.selection()->selectedEntity() == other);
            const auto after = fixture.source(mesh);
            historyRoundTrip(fixture, updated, before, after);
        }
    }
}

TEST_CASE("Explicit deletion reports exact stable identity differences in all domains",
          "[modeling-api]") {
    for (const auto domain : {Domain::Vertices, Domain::Edges, Domain::Faces}) {
        DYNAMIC_SECTION("domain=" << int(domain)) {
            ModelingFixture fixture;
            const auto mesh = fixture.cube();
            const auto before = fixture.source(mesh);
            auto request = fixture.meshMutation<api::MeshDeleteComponentsRequest>(mesh);
            request.domain = domain;
            if (domain == Domain::Vertices)
                request.vertexIds = std::vector<VertexId>{1};
            else if (domain == Domain::Edges)
                request.edges = std::vector<Edge>{{2, 1}};
            else
                request.faceIds = std::vector<FaceId>{before.faces.front().id};
            const auto count = fixture.model.undoStack()->count();
            const auto result = fixture.service.deleteComponents(request);
            REQUIRE(result.hasValue());
            REQUIRE(result.value->command.status == api::ResultStatus::Committed);
            REQUIRE(fixture.model.undoStack()->count() == count + 1);
            const auto after = fixture.source(mesh);
            std::set<VertexId> vertices;
            std::set<FaceId> faces;
            std::set<CornerId> corners;
            for (const auto& vertex : after.vertices)
                vertices.insert(vertex.id);
            for (const auto& face : after.faces) {
                faces.insert(face.id);
                for (const auto& corner : face.corners)
                    corners.insert(corner.id);
            }
            std::vector<VertexId> deletedVertices;
            std::vector<FaceId> deletedFaces;
            std::vector<CornerId> deletedCorners;
            for (const auto& vertex : before.vertices)
                if (!vertices.contains(vertex.id))
                    deletedVertices.push_back(vertex.id);
            for (const auto& face : before.faces) {
                if (!faces.contains(face.id))
                    deletedFaces.push_back(face.id);
                for (const auto& corner : face.corners)
                    if (!corners.contains(corner.id))
                        deletedCorners.push_back(corner.id);
            }
            std::vector<Edge> deletedEdges;
            const auto beforeEdges = sourceEdges(before), afterEdges = sourceEdges(after);
            std::set_difference(beforeEdges.begin(), beforeEdges.end(), afterEdges.begin(),
                                afterEdges.end(), std::back_inserter(deletedEdges));
            REQUIRE(result.value->deletedVertexIds == deletedVertices);
            REQUIRE(result.value->deletedFaceIds == deletedFaces);
            REQUIRE(result.value->deletedCornerIds == deletedCorners);
            REQUIRE(result.value->deletedEdges == deletedEdges);
            const auto json = api::ApiJsonCodec::encode(*result.value);
            REQUIRE(json["deletedCornerIds"].isArray());
            REQUIRE(json["deletedCornerIds"].toArray() == idJson(deletedCorners));
            historyRoundTrip(fixture, result.value->mesh, before, after);
        }
    }
}

TEST_CASE("Explicit fill uses unordered boundary targets and redo restores the same face ID",
          "[modeling-api]") {
    for (const auto domain : {Domain::Vertices, Domain::Edges}) {
        DYNAMIC_SECTION("domain=" << int(domain)) {
            ModelingFixture fixture;
            std::vector<VertexId> loop;
            const auto mesh = fixture.hole(loop);
            const auto before = fixture.source(mesh);
            auto request = fixture.meshMutation<api::MeshFillFaceRequest>(mesh);
            request.domain = domain;
            if (domain == Domain::Vertices) {
                std::reverse(loop.begin(), loop.end());
                request.vertexIds = loop;
            } else {
                request.edges.emplace();
                for (std::size_t index = 0; index < loop.size(); ++index)
                    request.edges->emplace_back(loop[(index + 1) % loop.size()], loop[index]);
                std::reverse(request.edges->begin(), request.edges->end());
            }
            const auto result = fixture.service.fillFace(request);
            REQUIRE(result.hasValue());
            REQUIRE(result.value->faceId > before.faces.back().id);
            REQUIRE(api::ApiJsonCodec::encode(*result.value)["faceId"] ==
                    QString::number(result.value->faceId));
            const auto after = fixture.source(mesh);
            REQUIRE(after.faces.size() == before.faces.size() + 1);
            REQUIRE(after.vertices == before.vertices);
            historyRoundTrip(fixture, result.value->mesh, before, after);
        }
    }
}

TEST_CASE("Every M5 modeling typed entry rejects invalid context and final guard atomically",
          "[modeling-api][modeling-envelope]") {
    const auto examples = modelingExamples()["valid"].toArray();
    for (const auto& item : examples) {
        const auto sample = item.toObject();
        const auto method = sample["method"].toString();
        for (int failure = 0; failure < 12; ++failure) {
            if (method == "mesh.makeEditable" && (failure == 6 || failure == 7 || failure == 11))
                continue;
            DYNAMIC_SECTION(method.toStdString() << " failure=" << failure) {
                ModelingFixture fixture;
                core::EntityId entity = 0;
                auto request = validTypedRequest(fixture, sample, entity);
                const auto other = fixture.primitive(core::PrimitiveKind::Empty);
                fixture.model.selection()->setSelectedEntity(other);
                prepareRedo(fixture, other);
                std::visit(
                    [&](auto& typed) {
                        using Request = std::decay_t<decltype(typed)>;
                        if constexpr (std::is_base_of_v<api::MutationRequest, Request>) {
                            typed.expectedDocumentRevision =
                                fixture.service.documentState().documentRevision;
                            switch (failure) {
                                case 0:
                                    typed.timeoutMs = 0;
                                    break;
                                case 1:
                                    typed.timeoutMs = 30001;
                                    break;
                                case 2:
                                    typed.mutationSequence = 0;
                                    break;
                                case 3:
                                    typed.clientSessionId = QStringLiteral("invalid-session");
                                    break;
                                case 4:
                                    typed.document.documentId =
                                        QStringLiteral("33333333-3333-4333-8333-333333333333");
                                    break;
                                case 5:
                                    --typed.expectedDocumentRevision;
                                    break;
                            }
                        }
                        if constexpr (std::is_base_of_v<api::MeshMutationRequest, Request>) {
                            if (failure == 6)
                                --typed.expectedTopologyRevision;
                            if (failure == 7)
                                --typed.expectedGeometryRevision;
                            if (failure == 11)
                                typed.meshId = std::numeric_limits<core::MeshId>::max();
                        }
                        if constexpr (std::is_base_of_v<api::EntityMutationRequest, Request> ||
                                      std::is_base_of_v<api::MeshMutationRequest, Request>) {
                            if (failure == 10)
                                typed.entityId = std::numeric_limits<core::EntityId>::max();
                        }
                    },
                    request);
                if (failure == 8)
                    fixture.service.setExternalBusy(QStringLiteral("test_pending_interaction"),
                                                    true);
                const auto before = remember(fixture, entity);
                int guardCalls = 0;
                if (failure == 9) {
                    fixture.service.exchangeBeforeCommitGuard(
                        [&]() -> std::optional<api::ApiError> {
                            ++guardCalls;
                            unchanged(fixture, before);
                            return api::ApiError{api::ErrorCode::Cancelled,
                                                 QStringLiteral("最终许可拒绝"),
                                                 {},
                                                 api::Recovery::None,
                                                 fixture.service.documentState()};
                        });
                }
                const auto expectedCode = failure < 4    ? api::ErrorCode::InvalidArgument
                                          : failure == 4 ? api::ErrorCode::StaleDocument
                                          : failure < 8  ? api::ErrorCode::RevisionConflict
                                          : failure == 8 ? api::ErrorCode::Busy
                                          : failure == 9 ? api::ErrorCode::Cancelled
                                                         : api::ErrorCode::NotFound;
                dispatchModeling(fixture, request, [&](const auto& result) {
                    REQUIRE_FALSE(result.hasValue());
                    REQUIRE(result.error->code == expectedCode);
                    if (failure < 4) {
                        const auto field = failure < 2    ? QStringLiteral("timeoutMs")
                                           : failure == 2 ? QStringLiteral("mutationSequence")
                                                          : QStringLiteral("clientSessionId");
                        REQUIRE(result.error->fieldPath == field);
                    }
                });
                if (failure == 9)
                    REQUIRE(guardCalls == 1);
                unchanged(fixture, before);
            }
        }
    }
}

TEST_CASE(
    "Explicit component targets reject incomplete duplicate unknown and oversized typed domains",
    "[modeling-api]") {
    for (const auto domain : {Domain::Vertices, Domain::Edges, Domain::Faces}) {
        for (int failure = 0; failure < 6; ++failure) {
            DYNAMIC_SECTION("domain=" << int(domain) << " failure=" << failure) {
                ModelingFixture fixture;
                const auto mesh = fixture.cube();
                const auto other = fixture.primitive(core::PrimitiveKind::Empty);
                prepareRedo(fixture, other);
                const auto before = remember(fixture, mesh.entityId);
                auto request = fixture.meshMutation<api::MeshDeleteComponentsRequest>(mesh);
                request.domain = domain;
                if (failure != 0) {
                    if (domain == Domain::Vertices)
                        request.vertexIds = std::vector<VertexId>{1};
                    else if (domain == Domain::Edges)
                        request.edges = std::vector<Edge>{{1, 2}};
                    else
                        request.faceIds = std::vector<FaceId>{1};
                }
                if (failure == 1) {
                    if (request.vertexIds)
                        request.vertexIds->clear();
                    if (request.edges)
                        request.edges->clear();
                    if (request.faceIds)
                        request.faceIds->clear();
                } else if (failure == 2) {
                    if (request.vertexIds)
                        request.vertexIds->push_back(1);
                    if (request.edges)
                        request.edges->emplace_back(2, 1);
                    if (request.faceIds)
                        request.faceIds->push_back(1);
                } else if (failure == 3) {
                    if (request.vertexIds)
                        request.vertexIds->front() = 0;
                    if (request.edges)
                        request.edges->front() = {1, 1};
                    if (request.faceIds)
                        request.faceIds->front() = 0;
                } else if (failure == 4) {
                    if (request.vertexIds)
                        request.vertexIds->front() = 99999;
                    if (request.edges)
                        request.edges->front() = {1, 3};
                    if (request.faceIds)
                        request.faceIds->front() = 99999;
                } else if (failure == 5) {
                    if (request.vertexIds)
                        request.vertexIds->resize(api::limits::meshVertices + 1, 1);
                    if (request.edges)
                        request.edges->resize(api::limits::meshVertices + 1, Edge(1, 2));
                    if (request.faceIds)
                        request.faceIds->resize(api::limits::meshFaces + 1, 1);
                }
                const auto result = fixture.service.deleteComponents(request);
                REQUIRE_FALSE(result.hasValue());
                REQUIRE(result.error->code == (failure == 4   ? api::ErrorCode::NotFound
                                               : failure == 5 ? api::ErrorCode::LimitExceeded
                                                              : api::ErrorCode::InvalidArgument));
                unchanged(fixture, before);
            }
        }
    }
    SECTION("wrong-domain optional remains invalid even when empty") {
        ModelingFixture fixture;
        const auto mesh = fixture.cube();
        const auto before = remember(fixture, mesh.entityId);
        auto request = fixture.meshMutation<api::MeshTransformComponentsRequest>(mesh);
        request.vertexIds = std::vector<VertexId>{1};
        request.edges.emplace();
        const auto result = fixture.service.transformComponents(request);
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error->code == api::ErrorCode::InvalidArgument);
        unchanged(fixture, before);
    }
}

TEST_CASE(
    "Explicit delta rejects invalid double inputs and overflowing float source without history",
    "[modeling-api]") {
    for (int failure = 0; failure < 9; ++failure) {
        DYNAMIC_SECTION("failure=" << failure) {
            ModelingFixture fixture;
            const auto mesh = fixture.cube();
            const auto before = remember(fixture, mesh.entityId);
            auto request = fixture.meshMutation<api::MeshTransformComponentsRequest>(mesh);
            selectWholeSource(request, fixture.source(mesh), Domain::Vertices);
            const auto infinity = std::numeric_limits<double>::infinity();
            switch (failure) {
                case 0:
                    request.transform.pivot.x = infinity;
                    break;
                case 1:
                    request.transform.translation.y = std::numeric_limits<double>::quiet_NaN();
                    break;
                case 2:
                    request.transform.scale.z = 0;
                    break;
                case 3:
                    request.transform.scale.x = 0.000999999999;
                    break;
                case 4:
                    request.transform.rotation = {0, 0, 0, 0};
                    break;
                case 5:
                    request.transform.rotation.x = infinity;
                    break;
                case 6:
                    request.transform.space = static_cast<api::MeshSpace>(99);
                    break;
                case 7:
                    request.transform.pivot.x = double(std::numeric_limits<float>::max()) * 2;
                    break;
                case 8:
                    request.transform.scale.x = std::numeric_limits<float>::max();
                    request.transform.translation.x = std::numeric_limits<float>::max();
                    break;
            }
            const auto result = fixture.service.transformComponents(request);
            REQUIRE_FALSE(result.hasValue());
            REQUIRE(result.error->code == (failure == 8 ? api::ErrorCode::InvalidTopology
                                                        : api::ErrorCode::InvalidArgument));
            unchanged(fixture, before);
        }
    }
    SECTION("tiny nonzero double quaternion is normalized without truncating to zero") {
        ModelingFixture fixture;
        const auto mesh = fixture.cube();
        const auto before = remember(fixture, mesh.entityId);
        auto request = fixture.meshMutation<api::MeshTransformComponentsRequest>(mesh);
        selectWholeSource(request, fixture.source(mesh), Domain::Vertices);
        request.transform.rotation = {1e-300, 0, 0, 0};
        const auto result = fixture.service.transformComponents(request);
        REQUIRE(result.hasValue());
        REQUIRE(result.value->command.status == api::ResultStatus::NoChange);
        unchanged(fixture, before);
    }
}

TEST_CASE("Explicit mutations reject a legal over-budget source before analysis or indexing",
          "[modeling-api][modeling-budget]") {
    ModelingFixture fixture;
    const auto mesh = fixture.cube();
    auto source = fixture.source(mesh);
    const auto& removedFace = source.faces.front();
    const Edge boundary(removedFace.corners[0].vertex, removedFace.corners[1].vertex);
    source.faces.erase(source.faces.begin());
    for (VertexId id = sourceVertices(source).back() + 1;
         source.vertices.size() <= api::limits::meshVertices; ++id)
        source.vertices.push_back({id, glm::vec3(0)});
    const auto validation = core::modeling::validateEditableMesh(source);
    REQUIRE(validation.isValid());
    REQUIRE(validation.boundaryEdgeCount == 4);
    REQUIRE(source.vertices.size() == api::limits::meshVertices + 1);
    REQUIRE(sourceEdges(source).contains(boundary));
    REQUIRE(fixture.model.replaceEditableMesh(mesh.entityId, source));

    const auto other = fixture.primitive(core::PrimitiveKind::Empty);
    fixture.model.selection()->setSelectedEntity(other);
    prepareRedo(fixture, other);
    const auto before = remember(fixture, mesh.entityId);
    int guardCalls = 0;
    const auto guard = [&]() -> std::optional<api::ApiError> {
        ++guardCalls;
        return std::nullopt;
    };
    fixture.service.exchangeBeforeCommitGuard(guard);
    const auto rejectWire = [&](const QString& method, const QJsonObject& params) {
        REQUIRE(api::ApiJsonCodec::decodeRequest(method, params).hasValue());
        const auto response = api::ApiJsonCodec::invoke(fixture.service, method, params,
                                                        api::FileAccess::External, guard);
        REQUIRE(response.contains("error"));
        const auto error = response["error"].toObject()["data"].toObject();
        CHECK(error["code"].toString() == QStringLiteral("LIMIT_EXCEEDED"));
    };
    const auto rejectComponent = [&](auto request, const QString& method) {
        const std::vector<VertexId> missing{std::numeric_limits<VertexId>::max()};
        request.vertexIds = missing;
        dispatchModeling(fixture, api::ApiRequest{request}, [&](const auto& result) {
            REQUIRE_FALSE(result.hasValue());
            CHECK(result.error->code == api::ErrorCode::LimitExceeded);
        });
        unchanged(fixture, before);
        auto params = fixture.context(mesh.entityId);
        params.insert("domain", "vertices");
        params.insert("vertexIds", idJson(missing));
        if (method == "mesh.transformComponents")
            params.insert("transform", QJsonObject{{"space", "local"},
                                                   {"pivot", QJsonArray{0, 0, 0}},
                                                   {"translation", QJsonArray{0, 0, 0}},
                                                   {"rotationQuaternion", QJsonArray{0, 0, 0, 1}},
                                                   {"scale", QJsonArray{1, 1, 1}}});
        rejectWire(method, params);
    };
    SECTION("bevel typed") {
        auto request = fixture.meshMutation<api::MeshBevelEdgeRequest>(mesh);
        request.edge = boundary;
        request.width = 0.1;
        const auto result = fixture.service.bevelEdge(request);
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error->code == api::ErrorCode::LimitExceeded);
    }
    SECTION("bevel wire") {
        auto params = fixture.context(mesh.entityId);
        params.insert("edge", edgeJson(boundary));
        params.insert("width", 0.1);
        rejectWire("mesh.bevelEdge", params);
    }
    SECTION("transformComponents typed and wire") {
        rejectComponent(fixture.meshMutation<api::MeshTransformComponentsRequest>(mesh),
                        "mesh.transformComponents");
    }
    SECTION("deleteComponents typed and wire") {
        rejectComponent(fixture.meshMutation<api::MeshDeleteComponentsRequest>(mesh),
                        "mesh.deleteComponents");
    }
    SECTION("fillFace typed and wire") {
        rejectComponent(fixture.meshMutation<api::MeshFillFaceRequest>(mesh), "mesh.fillFace");
    }
    SECTION("zero-offset extrude typed") {
        auto request = fixture.meshMutation<api::MeshExtrudeRequest>(mesh);
        request.faceIds = {source.faces.front().id};
        const auto result = fixture.service.extrudeRegion(request);
        REQUIRE_FALSE(result.hasValue());
        CHECK(result.error->code == api::ErrorCode::LimitExceeded);
    }
    SECTION("zero-offset extrude wire") {
        auto params = fixture.context(mesh.entityId);
        params.insert("faceIds", QJsonArray{QString::number(source.faces.front().id)});
        params.insert("space", "local");
        params.insert("offset", QJsonArray{0, 0, 0});
        rejectWire("mesh.extrudeRegion", params);
    }
    REQUIRE(guardCalls == 0);
    unchanged(fixture, before);
}

TEST_CASE("Explicit bevel and loop cut reject invalid widths seeds and slide endpoints",
          "[modeling-api]") {
    for (const bool bevel : {true, false}) {
        for (int failure = 0; failure < 7; ++failure) {
            DYNAMIC_SECTION("bevel=" << bevel << " failure=" << failure) {
                ModelingFixture fixture;
                const auto mesh = fixture.cube();
                const auto before = remember(fixture, mesh.entityId);
                Edge edge(1, 2);
                if (failure == 4)
                    edge = {0, 2};
                if (failure == 5)
                    edge = {1, 1};
                if (failure == 6)
                    edge = {1, 3};
                if (bevel) {
                    auto request = fixture.meshMutation<api::MeshBevelEdgeRequest>(mesh);
                    request.edge = edge;
                    request.width = failure == 0   ? 0
                                    : failure == 1 ? -0.1
                                    : failure == 2 ? std::numeric_limits<double>::infinity()
                                    : failure == 3 ? 1.0
                                                   : 0.1;
                    const auto result = fixture.service.bevelEdge(request);
                    REQUIRE_FALSE(result.hasValue());
                    REQUIRE(result.error->code == (failure == 6 ? api::ErrorCode::NotFound
                                                                : api::ErrorCode::InvalidArgument));
                } else {
                    auto request = fixture.meshMutation<api::MeshLoopCutRequest>(mesh);
                    request.seedEdge = edge;
                    request.slide = failure == 0   ? -1
                                    : failure == 1 ? 1
                                    : failure == 2 ? std::numeric_limits<double>::quiet_NaN()
                                    : failure == 3 ? 2
                                                   : 0;
                    const auto result = fixture.service.loopCut(request);
                    REQUIRE_FALSE(result.hasValue());
                    REQUIRE(result.error->code == (failure == 6 ? api::ErrorCode::NotFound
                                                                : api::ErrorCode::InvalidArgument));
                }
                unchanged(fixture, before);
            }
        }
    }
}

TEST_CASE("Explicit makeEditable and fill reject unsupported primitive and incomplete boundary",
          "[modeling-api]") {
    for (const auto primitive :
         {core::PrimitiveKind::Empty, core::PrimitiveKind::Plane, core::PrimitiveKind::Sphere}) {
        DYNAMIC_SECTION("primitive=" << int(primitive)) {
            ModelingFixture fixture;
            const auto entity = fixture.primitive(primitive);
            const auto before = remember(fixture, entity);
            auto request = fixture.mutation<api::MeshMakeEditableRequest>();
            request.entityId = entity;
            const auto result = fixture.service.makeEditable(request);
            REQUIRE_FALSE(result.hasValue());
            REQUIRE(result.error->code == api::ErrorCode::UnsupportedOperation);
            unchanged(fixture, before);
        }
    }
    for (int failure = 0; failure < 3; ++failure) {
        DYNAMIC_SECTION("fill failure=" << failure) {
            ModelingFixture fixture;
            std::vector<VertexId> loop;
            const auto mesh = failure == 0 ? fixture.cube() : fixture.hole(loop);
            const auto before = remember(fixture, mesh.entityId);
            auto request = fixture.meshMutation<api::MeshFillFaceRequest>(mesh);
            if (failure == 0)
                request.vertexIds = std::vector<VertexId>{5, 6, 7, 8};
            else if (failure == 1) {
                loop.pop_back();
                request.vertexIds = loop;
            } else {
                request.domain = Domain::Faces;
                request.faceIds = std::vector<FaceId>{2};
            }
            const auto result = fixture.service.fillFace(request);
            REQUIRE_FALSE(result.hasValue());
            REQUIRE(result.error->code == (failure == 2 ? api::ErrorCode::InvalidArgument
                                                        : api::ErrorCode::InvalidTopology));
            unchanged(fixture, before);
        }
    }
}

TEST_CASE("Explicit deletion can empty the source while preserving mesh identity and undo IDs",
          "[modeling-api]") {
    ModelingFixture fixture;
    const auto mesh = fixture.cube();
    const auto before = fixture.source(mesh);
    auto request = fixture.meshMutation<api::MeshDeleteComponentsRequest>(mesh);
    request.vertexIds = sourceVertices(before);
    const auto result = fixture.service.deleteComponents(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->mesh.meshId == mesh.meshId);
    REQUIRE(result.value->deletedVertexIds.size() == 8);
    REQUIRE(result.value->deletedFaceIds.size() == 6);
    REQUIRE(result.value->deletedCornerIds.size() == 24);
    REQUIRE(result.value->deletedEdges.size() == 12);
    const auto after = fixture.source(mesh);
    REQUIRE(after.vertices.empty());
    REQUIRE(after.faces.empty());
    historyRoundTrip(fixture, result.value->mesh, before, after);
}
