/*
 * 模块名: EditableSceneTests
 * 功能概述: 验证文档网格绑定、独立副本、原子候选及单调版本。
 * 对外接口: Catch2 测试；依赖关系: Core，无 Qt/GL。
 * 输入输出: CPU 场景/快照到断言；异常与错误: 测试失败报告。
 * 维护说明: 不将容器地址或 revision 写为持久身份。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"
#include "core/modeling/VertexTransform.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>
#include <nlohmann/json.hpp>

using namespace mini3d::core;
namespace {
void requireSameDerived(const modeling::DerivedMesh& actual,
                        const modeling::DerivedMesh& expected) {
    REQUIRE(actual.mesh.indices == expected.mesh.indices);
    REQUIRE(actual.mesh.vertices.size() == expected.mesh.vertices.size());
    REQUIRE(actual.vertexSources.size() == expected.vertexSources.size());
    REQUIRE(actual.triangleSources.size() == expected.triangleSources.size());
    for (std::size_t i = 0; i < actual.mesh.vertices.size(); ++i) {
        REQUIRE(actual.mesh.vertices[i].position == expected.mesh.vertices[i].position);
        REQUIRE(actual.mesh.vertices[i].normal == expected.mesh.vertices[i].normal);
        REQUIRE(actual.mesh.vertices[i].uv == expected.mesh.vertices[i].uv);
        REQUIRE(actual.mesh.vertices[i].color == expected.mesh.vertices[i].color);
        REQUIRE(actual.vertexSources[i].vertex == expected.vertexSources[i].vertex);
        REQUIRE(actual.vertexSources[i].corner == expected.vertexSources[i].corner);
    }
    for (std::size_t i = 0; i < actual.triangleSources.size(); ++i) {
        REQUIRE(actual.triangleSources[i].face == expected.triangleSources[i].face);
        REQUIRE(actual.triangleSources[i].material == expected.triangleSources[i].material);
    }
}

void appendTransformPolygon(modeling::EditableMesh& mesh, const std::vector<glm::vec3>& points,
                            std::uint64_t base) {
    modeling::EditableFace face;
    face.id = base + 1;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto vertex = base + 10 + i;
        mesh.vertices.push_back({vertex, points[i]});
        modeling::MeshCorner corner;
        corner.id = base + 100 + i;
        corner.vertex = vertex;
        corner.uv = {static_cast<float>(i), -0.25F};
        corner.color = {0.2F, 0.1F * static_cast<float>(i + 1), 0.8F};
        if (i % 2 == 0) {
            corner.normal = glm::vec3(0, 0, 3);
        }
        face.corners.push_back(corner);
    }
    mesh.faces.push_back(std::move(face));
}
} // namespace

TEST_CASE("Editable scene installs immutable snapshots with monotonic revisions",
          "[editable-scene]") {
    Scene scene;
    const auto id = scene.createEntity("Cube", 0, PrimitiveKind::Cube);
    const auto before = scene.geometrySnapshot(id);
    std::string error;
    auto cube = modeling::createEditableCube();
    const auto prepared = scene.prepareEditableGeometry(id, cube, error);
    REQUIRE(prepared);
    REQUIRE(scene.find(id)->editableMesh == 0);
    REQUIRE(scene.installGeometry(*prepared));
    const auto meshId = scene.find(id)->editableMesh;
    REQUIRE(meshId != 0);
    REQUIRE(scene.find(id)->primitive == PrimitiveKind::Empty);
    REQUIRE_FALSE(scene.find(id)->meshRenderer);
    REQUIRE(scene.editableMesh(meshId)->content->source == cube);
    const auto revision = scene.editableMesh(meshId)->geometryRevision;
    cube.vertices[0].position.x -= 0.2F;
    const auto changed = scene.prepareEditableGeometry(id, cube, error);
    REQUIRE(changed);
    REQUIRE(scene.installGeometry(*changed));
    REQUIRE(scene.find(id)->editableMesh == meshId);
    REQUIRE(scene.editableMesh(meshId)->geometryRevision > revision);
    const auto changedRevision = scene.editableMesh(meshId)->geometryRevision;
    REQUIRE(scene.installGeometry(*prepared));
    REQUIRE(scene.editableMesh(meshId)->geometryRevision > changedRevision);
    REQUIRE(scene.editableMesh(meshId)->content->source == modeling::createEditableCube());
    REQUIRE(scene.installGeometry(*before));
    REQUIRE(scene.find(id)->primitive == PrimitiveKind::Cube);
    REQUIRE(scene.editableMeshes().empty());
    const auto lastRevision = scene.editableMesh(meshId)->geometryRevision;
    REQUIRE(scene.installGeometry(*prepared));
    REQUIRE(scene.find(id)->editableMesh == meshId);
    REQUIRE(scene.editableMesh(meshId)->topologyRevision > lastRevision);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision > lastRevision);
}

TEST_CASE("Editable scene copies get independent identities and reject invalid candidates",
          "[editable-scene]") {
    Scene scene;
    const auto id = scene.createEntity("Cube", 0, PrimitiveKind::Cube);
    std::string error;
    const auto original = modeling::createEditableCube();
    REQUIRE(scene.installGeometry(*scene.prepareEditableGeometry(id, original, error)));
    const auto meshId = scene.find(id)->editableMesh;
    const auto copy = scene.duplicateSubtree(id);
    const auto copyMesh = scene.find(copy)->editableMesh;
    REQUIRE(copyMesh != meshId);
    auto changed = original;
    changed.vertices[0].position.x -= 0.2F;
    REQUIRE(scene.installGeometry(*scene.prepareEditableGeometry(copy, changed, error)));
    REQUIRE(scene.editableMesh(meshId)->content->source == original);
    REQUIRE(scene.editableMesh(copyMesh)->content->source == changed);
    const auto revision = scene.editableMesh(copyMesh)->geometryRevision;
    changed.vertices[0].id = changed.vertices[1].id;
    REQUIRE_FALSE(scene.prepareEditableGeometry(copy, changed, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(scene.editableMesh(copyMesh)->geometryRevision == revision);
    const auto subtree = scene.snapshotSubtree(copy);
    REQUIRE(scene.removeEntity(copy));
    REQUIRE(scene.restoreSubtree(subtree));
    REQUIRE(scene.find(copy)->editableMesh == copyMesh);
    REQUIRE_FALSE(scene.setCamera(copy, {}));
    REQUIRE_FALSE(scene.setLight(copy, {}));
    const auto camera = scene.createEntity("Camera");
    REQUIRE(scene.setCamera(camera, {}));
    REQUIRE_FALSE(scene.prepareEditableGeometry(camera, original, error));
    REQUIRE(scene.replaceNodes(scene.nodes(), scene.editableMeshes()));
    const auto stable = scene.nodes();
    auto invalid = stable;
    invalid[0].primitive = PrimitiveKind::Cube;
    REQUIRE_FALSE(scene.replaceNodes(invalid, scene.editableMeshes()));
    REQUIRE(scene.find(id)->primitive == PrimitiveKind::Empty);
    REQUIRE_FALSE(scene.replaceNodes(stable));
    invalid = stable;
    invalid[1].editableMesh = meshId;
    REQUIRE_FALSE(scene.replaceNodes(invalid, scene.editableMeshes()));
}

TEST_CASE("Format 3 round trips polygon IDs and corner attributes without render caches",
          "[editable-scene][serializer-v3]") {
    constexpr std::uint64_t base = std::uint64_t{1} << 54;
    SceneDocumentData data;
    SceneNode node;
    node.id = 7;
    node.name = "可编辑立方体";
    node.editableMesh = base;
    data.nodes.push_back(node);
    auto cube = modeling::createEditableCube();
    for (auto& vertex : cube.vertices) {
        vertex.id += base;
    }
    for (auto& face : cube.faces) {
        face.id += base;
        for (auto& corner : face.corners) {
            corner.id += base;
            corner.vertex += base;
        }
    }
    cube.faces[0].corners[0].normal.reset();
    cube.faces[0].corners[0].uv = {-0.25F, 4};
    data.editableMeshes.push_back({base, cube});
    const auto text = SceneSerializer::encode(data);
    const auto json = nlohmann::json::parse(text);
    REQUIRE(json["version"] == 3);
    REQUIRE(json["editableMeshes"][0]["id"].get<std::uint64_t>() == base);
    REQUIRE_FALSE(json["editableMeshes"][0].contains("revision"));
    REQUIRE_FALSE(json["editableMeshes"][0].contains("indices"));
    SceneDocumentData loaded;
    std::string error;
    REQUIRE(SceneSerializer::decode(text, loaded, error));
    REQUIRE(loaded.sourceVersion == 3);
    REQUIRE(loaded.nodes[0].editableMesh == base);
    REQUIRE(loaded.editableMeshes[0].source == cube);
    REQUIRE(SceneSerializer::encode(loaded) == text);
    Scene scene;
    REQUIRE(scene.replaceNodes(loaded.nodes, loaded.editableMeshes));
    REQUIRE(scene.editableMesh(base)->content->derived.mesh.indices.size() == 36);
    const auto copy = scene.duplicateSubtree(7);
    REQUIRE(scene.find(copy)->editableMesh > base);
}

TEST_CASE("Format 3 rejects corrupt geometry and binding without changing the output",
          "[editable-scene][serializer-v3]") {
    Scene scene;
    const auto id = scene.createEntity("保留", 0, PrimitiveKind::Cube);
    std::string error;
    REQUIRE(scene.installGeometry(
        *scene.prepareEditableGeometry(id, modeling::createEditableCube(), error)));
    SceneDocumentData data;
    data.nodes = scene.nodes();
    data.editableMeshes = scene.editableMeshes();
    const auto valid = nlohmann::json::parse(SceneSerializer::encode(data));
    for (int failure = 0; failure < 16; ++failure) {
        auto broken = valid;
        auto& mesh = broken["editableMeshes"][0];
        switch (failure) {
            case 0:
                mesh["id"] = 0;
                break;
            case 1:
                mesh["vertices"][1]["id"] = mesh["vertices"][0]["id"];
                break;
            case 2:
                mesh["faces"][0]["corners"][0]["vertex"] = 999;
                break;
            case 3:
                mesh["faces"][0]["corners"][0]["normal"] = {0, 0, 0};
                break;
            case 4:
                mesh["faces"][0]["corners"][0]["uv"] = {0};
                break;
            case 5:
                mesh["faces"][0]["material"] = 900;
                break;
            case 6:
                broken["entities"][0]["primitive"] = "Cube";
                break;
            case 7:
                broken["entities"][0]["editableMesh"] = 999;
                break;
            case 8:
                broken["editableMeshes"].push_back(mesh);
                break;
            case 9:
                broken["editableMeshes"] = nlohmann::json::object();
                break;
            case 10:
                mesh["vertices"][0]["position"][0] = nullptr;
                break;
            case 11:
                broken["entities"][0]["camera"] = {
                    {"fieldOfView", 45}, {"nearPlane", 0.1}, {"farPlane", 1000}};
                break;
            case 12:
                broken["version"] = 2;
                break;
            case 13:
                broken["entities"][0].erase("editableMesh");
                break;
            case 14:
                mesh["id"] = -1;
                break;
            case 15:
                broken.erase("editableMeshes");
                break;
        }
        auto output = data;
        INFO("Corrupt fixture " << failure);
        REQUIRE_FALSE(SceneSerializer::decode(broken.dump(), output, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(output.nodes[0].editableMesh == data.nodes[0].editableMesh);
        REQUIRE(output.editableMeshes[0].source == data.editableMeshes[0].source);
    }
}

TEST_CASE("Legacy format 1 and 2 preserve their fields when migrated to format 3",
          "[editable-scene][serializer-v3]") {
    Scene scene;
    const auto id = scene.createEntity("Camera");
    REQUIRE(scene.setCamera(id, {60, 0.25F, 500}));
    SceneDocumentData data;
    data.nodes = scene.nodes();
    data.assets.push_back({27, "../model.glb", 5});
    const auto valid = nlohmann::json::parse(SceneSerializer::encode(data));
    for (int version : {1, 2}) {
        auto legacy = valid;
        legacy["version"] = version;
        legacy.erase("editableMeshes");
        if (version == 1) {
            legacy["entities"][0].erase("camera");
        }
        SceneDocumentData output;
        std::string error;
        REQUIRE(SceneSerializer::decode(legacy.dump(), output, error));
        REQUIRE(output.sourceVersion == version);
        REQUIRE(output.editableMeshes.empty());
        REQUIRE(output.nodes[0].camera.has_value() == (version == 2));
        REQUIRE(output.assets[0].path == "../model.glb");
        REQUIRE(output.assets[0].meshIndex == 5);
        REQUIRE(nlohmann::json::parse(SceneSerializer::encode(output))["version"] == 3);
    }
}

TEST_CASE("Scene transformed candidates match full preparation and preserve snapshot revisions",
          "[editable-scene][vertex-transform][perf-regression]") {
    Scene scene;
    const auto id = scene.createEntity("Cube", 0, PrimitiveKind::Cube);
    auto source = modeling::createEditableCube();
    source.faces[0].corners[0].uv = {-0.25F, 4};
    source.faces[0].corners[0].color = {0.2F, 0.4F, 0.8F};
    std::string error;
    const auto initial = scene.prepareEditableGeometry(id, source, error);
    REQUIRE(initial);
    REQUIRE(scene.installGeometry(*initial));
    std::set<modeling::VertexId> selected;
    for (const auto& vertex : source.vertices) {
        selected.insert(vertex.id);
    }
    SECTION("whole source without modifiers") {}
    SECTION("partial source without modifiers") {
        selected = {source.vertices.front().id};
    }
    SECTION("Mirror then Subdivision remain fully evaluated") {
        modeling::MirrorOptions mirror;
        mirror.clipping = false;
        const auto mirrored = scene.prepareMirror(id, mirror, error);
        REQUIRE(mirrored);
        REQUIRE(scene.installGeometry(*mirrored));
        const auto subdivided = scene.prepareSubdivision(id, modeling::SubdivisionOptions{}, error);
        REQUIRE(subdivided);
        REQUIRE(scene.installGeometry(*subdivided));
    }
    const auto meshId = scene.find(id)->editableMesh;
    const auto before = *scene.geometrySnapshot(id);
    const auto beforeRecord = *scene.editableMesh(meshId);
    const auto world = glm::translate(glm::dmat4(1), glm::dvec3(2, 3, -1)) *
                       glm::scale(glm::dmat4(1), glm::dvec3(-2, 0.7, 1.5));
    const auto delta = glm::translate(glm::dmat4(1), glm::dvec3(0.1, 0.05, 0.02)) *
                       glm::scale(glm::dmat4(1), glm::dvec3(1.1, 0.9, 1.2));
    const auto transformed = modeling::transformVertices(source, selected, world, delta);
    INFO(transformed.error);
    REQUIRE(transformed.mesh);
    const auto complete = scene.prepareEditableGeometry(id, *transformed.mesh, error);
    INFO(error);
    REQUIRE(complete);
    const auto reused =
        scene.prepareTransformedEditableGeometry(id, source, selected, world, delta, error);
    INFO(error);
    REQUIRE(reused);
    REQUIRE(reused->content()->source == complete->content()->source);
    REQUIRE(reused->content()->mirror == complete->content()->mirror);
    REQUIRE(reused->content()->subdivision == complete->content()->subdivision);
    REQUIRE(reused->content()->evaluatedMesh() == complete->content()->evaluatedMesh());
    requireSameDerived(reused->content()->derived, complete->content()->derived);
    requireSameDerived(reused->content()->displayedDerived(),
                       complete->content()->displayedDerived());
    const auto trusted =
        scene.prepareTransformedEditableGeometry(id, before, selected, world, delta, error);
    INFO(error);
    REQUIRE(trusted);
    REQUIRE(trusted->content()->source == complete->content()->source);
    REQUIRE(trusted->content()->mirror == complete->content()->mirror);
    REQUIRE(trusted->content()->subdivision == complete->content()->subdivision);
    REQUIRE(trusted->content()->evaluatedMesh() == complete->content()->evaluatedMesh());
    requireSameDerived(trusted->content()->derived, complete->content()->derived);
    requireSameDerived(trusted->content()->displayedDerived(),
                       complete->content()->displayedDerived());
    // 候选尚未安装时，真源与三个 revision 均应保持原值。
    REQUIRE(scene.editableMesh(meshId)->content == beforeRecord.content);
    REQUIRE(scene.editableMesh(meshId)->topologyRevision == beforeRecord.topologyRevision);
    REQUIRE(scene.editableMesh(meshId)->geometryRevision == beforeRecord.geometryRevision);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision == beforeRecord.evaluationRevision);
    REQUIRE(scene.installGeometry(*trusted));
    REQUIRE(scene.editableMesh(meshId)->content->source == *transformed.mesh);
    const auto changedRevision = scene.editableMesh(meshId)->geometryRevision;
    REQUIRE(changedRevision > beforeRecord.geometryRevision);
    REQUIRE(scene.installGeometry(before));
    REQUIRE(scene.editableMesh(meshId)->content == beforeRecord.content);
    REQUIRE(scene.editableMesh(meshId)->geometryRevision > changedRevision);
}

TEST_CASE("Scene transformed preparation validates no-op source and rejects invalid targets",
          "[editable-scene][vertex-transform][perf-regression]") {
    Scene scene;
    const auto id = scene.createEntity("Cube", 0, PrimitiveKind::Cube);
    const auto source = modeling::createEditableCube();
    std::string error;
    const auto initial = scene.prepareEditableGeometry(id, source, error);
    REQUIRE(initial);
    REQUIRE(scene.installGeometry(*initial));
    const auto meshId = scene.find(id)->editableMesh;
    const auto beforeRecord = *scene.editableMesh(meshId);
    const std::set<modeling::VertexId> selected{source.vertices.back().id};
    const auto unchanged = scene.prepareTransformedEditableGeometry(
        id, source, selected, glm::dmat4(1), glm::dmat4(1), error);
    REQUIRE(unchanged);
    REQUIRE(error.empty());
    REQUIRE(unchanged->content()->source == source);
    requireSameDerived(unchanged->content()->derived, beforeRecord.content->derived);
    auto invalid = source;
    invalid.faces[0].corners[1].id = invalid.faces[0].corners[0].id;
    REQUIRE_FALSE(scene.prepareTransformedEditableGeometry(id, invalid, selected, glm::dmat4(1),
                                                           glm::dmat4(1), error));
    REQUIRE_FALSE(error.empty());
    for (int failure = 0; failure < 6; ++failure) {
        invalid = source;
        switch (failure) {
            case 0:
                invalid.vertices[0].id = invalid.vertices[1].id;
                break;
            case 1:
                invalid.faces[0].corners[0].vertex = 999;
                break;
            case 2:
                invalid.faces[0].corners[0].uv.x = std::numeric_limits<float>::quiet_NaN();
                break;
            case 3:
                invalid.faces[0].corners[0].normal = glm::vec3(0);
                break;
            case 4:
                invalid.vertices[0].position.x = std::numeric_limits<float>::quiet_NaN();
                break;
            case 5:
                std::reverse(invalid.faces[0].corners.begin(), invalid.faces[0].corners.end());
                break;
        }
        INFO("Invalid no-op source " << failure);
        REQUIRE_FALSE(scene.prepareTransformedEditableGeometry(id, invalid, selected, glm::dmat4(1),
                                                               glm::dmat4(1), error));
        REQUIRE_FALSE(error.empty());
    }
    REQUIRE_FALSE(scene.prepareTransformedEditableGeometry(
        id, source, selected, glm::dmat4(1), glm::scale(glm::dmat4(1), glm::dvec3(0, 1, 1)),
        error));
    REQUIRE_FALSE(error.empty());
    auto externalMaterial = source;
    externalMaterial.faces[0].material = 999;
    REQUIRE_FALSE(scene.prepareTransformedEditableGeometry(id, externalMaterial, selected,
                                                           glm::dmat4(1), glm::dmat4(1), error));
    REQUIRE_FALSE(error.empty());
    const auto camera = scene.createEntity("Camera");
    const auto light = scene.createEntity("Light");
    REQUIRE(scene.setCamera(camera, {}));
    REQUIRE(scene.setLight(light, {}));
    for (const auto target : {EntityId{0}, camera, light}) {
        REQUIRE_FALSE(scene.prepareTransformedEditableGeometry(
            target, source, selected, glm::dmat4(1), glm::dmat4(1), error));
        REQUIRE_FALSE(error.empty());
    }
    REQUIRE(scene.find(id)->editableMesh == meshId);
    REQUIRE(scene.editableMesh(meshId)->content == beforeRecord.content);
    REQUIRE(scene.editableMesh(meshId)->topologyRevision == beforeRecord.topologyRevision);
    REQUIRE(scene.editableMesh(meshId)->geometryRevision == beforeRecord.geometryRevision);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision == beforeRecord.evaluationRevision);
}

TEST_CASE("Trusted transforms match every full derived field for reordered sparse mixed faces",
          "[editable-scene][vertex-transform][perf-regression]") {
    constexpr std::uint64_t base = std::uint64_t{1} << 54;
    modeling::EditableMesh source;
    appendTransformPolygon(source, {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}}, base);
    appendTransformPolygon(source, {{10, 0, 0}, {12, 0, 0}, {12, 2, 0.3F}, {10, 2, 0}},
                           base + 1000);
    appendTransformPolygon(source, {{20, 0, 0}, {22, 0, 0}, {22, 2, 0}, {21, 0.5F, 0}, {20, 2, 0}},
                           base + 2000);
    std::reverse(source.vertices.begin(), source.vertices.end());
    std::reverse(source.faces.begin(), source.faces.end());
    std::set<modeling::VertexId> selected{source.faces[0].corners[3].vertex};
    auto delta = glm::translate(glm::dmat4(1), glm::dvec3(0.02, 0.03, 0.01));
    SECTION("partial concave face") {}
    SECTION("whole face with unchanged other faces") {
        selected.clear();
        for (const auto& corner : source.faces[0].corners) {
            selected.insert(corner.vertex);
        }
    }
    SECTION("whole source with rotation and nonuniform scale") {
        selected.clear();
        for (const auto& vertex : source.vertices) {
            selected.insert(vertex.id);
        }
        delta = glm::rotate(glm::dmat4(1), 0.2, glm::dvec3(0, 0, 1)) *
                glm::scale(glm::dmat4(1), glm::dvec3(1.1, 0.8, 1.2));
    }
    SECTION("whole source negative determinant and hard normals") {
        selected.clear();
        for (const auto& vertex : source.vertices) {
            selected.insert(vertex.id);
        }
        delta = glm::scale(glm::dmat4(1), glm::dvec3(-1.2, 0.8, 1.1));
    }
    SECTION("partial source without hard normals") {
        for (auto& face : source.faces) {
            for (auto& corner : face.corners) {
                corner.normal.reset();
            }
        }
    }
    Scene scene;
    const auto id = scene.createEntity("Mixed");
    std::string error;
    const auto initial = scene.prepareEditableGeometry(id, source, error);
    INFO(error);
    REQUIRE(initial);
    REQUIRE(scene.installGeometry(*initial));
    const auto before = *scene.geometrySnapshot(id);
    const auto saved = before.content()->source;
    const auto world = glm::translate(glm::dmat4(1), glm::dvec3(1, -2, 3)) *
                       glm::scale(glm::dmat4(1), glm::dvec3(-2, 0.7, 1.5));
    const auto full = modeling::transformVertices(source, selected, world, delta);
    INFO(full.error);
    REQUIRE(full.mesh);
    REQUIRE(full.derived);
    const auto trusted =
        scene.prepareTransformedEditableGeometry(id, before, selected, world, delta, error);
    INFO(error);
    REQUIRE(trusted);
    REQUIRE(trusted->content()->source == *full.mesh);
    requireSameDerived(trusted->content()->derived, *full.derived);
    REQUIRE(before.content()->source == saved);
    REQUIRE(scene.editableMesh(scene.find(id)->editableMesh)->content == before.content());
}

TEST_CASE("Trusted partial transforms retriangulate changed concavity and move isolated vertices",
          "[editable-scene][vertex-transform][perf-regression]") {
    modeling::EditableMesh source;
    appendTransformPolygon(source, {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}, 500);
    source.vertices.push_back({999, {10, 10, 10}});
    std::set<modeling::VertexId> selected{source.faces[0].corners[0].vertex};
    auto delta = glm::translate(glm::dmat4(1), glm::dvec3(1.5, 1.5, 0));
    bool concavityChanges = true;
    SECTION("convex becomes concave with a different ear order") {}
    SECTION("isolated source vertex has no render faces") {
        selected = {999};
        delta = glm::translate(glm::dmat4(1), glm::dvec3(0.5, -0.25, 1));
        concavityChanges = false;
    }
    Scene scene;
    const auto id = scene.createEntity("Polygon");
    std::string error;
    const auto initial = scene.prepareEditableGeometry(id, source, error);
    REQUIRE(initial);
    REQUIRE(scene.installGeometry(*initial));
    const auto before = *scene.geometrySnapshot(id);
    const auto trusted =
        scene.prepareTransformedEditableGeometry(id, before, selected, glm::dmat4(1), delta, error);
    INFO(error);
    REQUIRE(trusted);
    const auto complete = modeling::transformVertices(source, selected, glm::dmat4(1), delta);
    REQUIRE(complete.mesh);
    REQUIRE(complete.derived);
    REQUIRE(trusted->content()->source == *complete.mesh);
    requireSameDerived(trusted->content()->derived, *complete.derived);
    if (concavityChanges) {
        REQUIRE(trusted->content()->derived.mesh.indices != before.content()->derived.mesh.indices);
    } else {
        REQUIRE(trusted->content()->source.vertex(999)->position == glm::vec3(10.5F, 9.75F, 11));
        requireSameDerived(trusted->content()->derived, before.content()->derived);
    }
}

TEST_CASE("Trusted geometry failures preserve the installed preview and fixed before",
          "[editable-scene][vertex-transform][perf-regression]") {
    modeling::EditableMesh source;
    appendTransformPolygon(source, {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}, 500);
    Scene scene;
    const auto id = scene.createEntity("Polygon");
    std::string error;
    const auto initial = scene.prepareEditableGeometry(id, source, error);
    REQUIRE(initial);
    REQUIRE(scene.installGeometry(*initial));
    const auto mesh = scene.find(id)->editableMesh;
    const auto before = *scene.geometrySnapshot(id);
    const auto vertex = source.faces[0].corners[0].vertex;
    const auto good = scene.prepareTransformedEditableGeometry(
        id, before, {vertex}, glm::dmat4(1),
        glm::translate(glm::dmat4(1), glm::dvec3(-0.1, -0.05, 0)), error);
    REQUIRE(good);
    REQUIRE(scene.installGeometry(*good));
    const auto record = *scene.editableMesh(mesh);
    std::set<modeling::VertexId> selected{vertex};
    auto invalid = glm::translate(glm::dmat4(1), glm::dvec3(2, 0, 0));
    SECTION("coincident edge endpoints") {}
    SECTION("projected crossing with nonzero area") {
        invalid = glm::translate(glm::dmat4(1), glm::dvec3(3, 0.5, 0));
    }
    SECTION("projected edge contact") {
        invalid = glm::translate(glm::dmat4(1), glm::dvec3(1, 2, 0));
    }
    SECTION("relative face area below the unchanged threshold") {
        selected = {source.faces[0].corners[2].vertex, source.faces[0].corners[3].vertex};
        invalid = glm::translate(glm::dmat4(1), glm::dvec3(0, -2 + 1.0e-12, 0));
    }
    SECTION("finite coordinate overflow") {
        invalid = glm::translate(glm::dmat4(1), glm::dvec3(1.0e300, 0, 0));
    }
    SECTION("nonfinite matrix") {
        invalid[3][0] = std::numeric_limits<double>::quiet_NaN();
    }
    SECTION("singular scale") {
        invalid = glm::scale(glm::dmat4(1), glm::dvec3(0, 1, 1));
    }
    SECTION("missing selection identity") {
        selected = {999};
    }
    const auto complete = modeling::transformVertices(source, selected, glm::dmat4(1), invalid);
    REQUIRE_FALSE(complete.mesh);
    REQUIRE_FALSE(scene.prepareTransformedEditableGeometry(id, before, selected, glm::dmat4(1),
                                                           invalid, error));
    REQUIRE(error == complete.error);
    REQUIRE(scene.editableMesh(mesh)->content == record.content);
    REQUIRE(scene.editableMesh(mesh)->topologyRevision == record.topologyRevision);
    REQUIRE(scene.editableMesh(mesh)->geometryRevision == record.geometryRevision);
    REQUIRE(scene.editableMesh(mesh)->evaluationRevision == record.evaluationRevision);
    REQUIRE(before.content()->source == source);
    // 单位增量仍从固定 before 计算，不把已经安装的有效预览再次累积。
    const auto noOp = scene.prepareTransformedEditableGeometry(id, before, {vertex}, glm::dmat4(1),
                                                               glm::dmat4(1), error);
    REQUIRE(noOp);
    REQUIRE(error.empty());
    REQUIRE(noOp->content() == before.content());
    REQUIRE(scene.editableMesh(mesh)->content == record.content);
    REQUIRE(scene.installGeometry(before));
    REQUIRE(scene.editableMesh(mesh)->content == before.content());
    REQUIRE(scene.editableMesh(mesh)->geometryRevision > record.geometryRevision);
    REQUIRE(scene.editableMesh(mesh)->topologyRevision > record.topologyRevision);
    REQUIRE(scene.editableMesh(mesh)->evaluationRevision > record.evaluationRevision);
}

TEST_CASE("Trusted transform snapshots belong to their Scene entity binding and document lifetime",
          "[editable-scene][vertex-transform][perf-regression]") {
    Scene scene;
    const auto id = scene.createEntity("Cube", 0, PrimitiveKind::Cube);
    const auto primitive = *scene.geometrySnapshot(id);
    const auto source = modeling::createEditableCube();
    std::string error;
    const auto initial = scene.prepareEditableGeometry(id, source, error);
    REQUIRE(initial);
    REQUIRE(scene.installGeometry(*initial));
    const auto before = *scene.geometrySnapshot(id);
    const auto beforeMesh = scene.find(id)->editableMesh;
    const std::set<modeling::VertexId> selected{source.vertices[0].id};
    const auto reject = [&](Scene& target, EntityId entity,
                            const Scene::GeometrySnapshot& snapshot) {
        REQUIRE_FALSE(target.prepareTransformedEditableGeometry(
            entity, snapshot, selected, glm::dmat4(1), glm::dmat4(1), error));
        REQUIRE_FALSE(error.empty());
    };
    reject(scene, id, Scene::GeometrySnapshot{});
    reject(scene, id, primitive);
    reject(scene, 0, before);
    const auto copy = scene.duplicateSubtree(id);
    reject(scene, copy, before);
    Scene copied = scene;
    reject(copied, id, before);
    const auto copiedBefore = *copied.geometrySnapshot(id);
    REQUIRE(copied.prepareTransformedEditableGeometry(id, copiedBefore, selected, glm::dmat4(1),
                                                      glm::dmat4(1), error));
    reject(scene, id, copiedBefore);
    Scene other;
    REQUIRE(other.replaceNodes(scene.nodes(), scene.editableMeshes()));
    reject(other, id, before);
    REQUIRE(scene.installGeometry(primitive));
    reject(scene, id, before);
    const auto rebound = scene.prepareEditableGeometry(id, source, error);
    REQUIRE(rebound);
    REQUIRE(scene.installGeometry(*rebound));
    REQUIRE(scene.find(id)->editableMesh != beforeMesh);
    reject(scene, id, before);
    scene = Scene{};
    REQUIRE(scene.createEntity("New Cube") == id);
    const auto newDocument = scene.prepareEditableGeometry(id, source, error);
    REQUIRE(newDocument);
    REQUIRE(scene.installGeometry(*newDocument));
    reject(scene, id, before);
}

TEST_CASE("Trusted no-op source keeps current modifier evaluation after parameter changes",
          "[editable-scene][vertex-transform][perf-regression]") {
    Scene scene;
    const auto id = scene.createEntity("Cube");
    const auto source = modeling::createEditableCube();
    std::string error;
    const auto initial = scene.prepareEditableGeometry(id, source, error);
    REQUIRE(initial);
    REQUIRE(scene.installGeometry(*initial));
    const auto before = *scene.geometrySnapshot(id);
    modeling::MirrorOptions mirror;
    mirror.clipping = false;
    const auto modified = scene.prepareMirror(id, mirror, error);
    REQUIRE(modified);
    REQUIRE(scene.installGeometry(*modified));
    const auto subdivided = scene.prepareSubdivision(id, modeling::SubdivisionOptions{}, error);
    REQUIRE(subdivided);
    REQUIRE(scene.installGeometry(*subdivided));
    const auto current = scene.editableMesh(scene.find(id)->editableMesh)->content;
    const auto trusted = scene.prepareTransformedEditableGeometry(
        id, before, {source.vertices[0].id}, glm::dmat4(1), glm::dmat4(1), error);
    INFO(error);
    REQUIRE(trusted);
    REQUIRE(trusted->content()->source == source);
    REQUIRE(trusted->content()->mirror == current->mirror);
    REQUIRE(trusted->content()->subdivision == current->subdivision);
    REQUIRE(trusted->content()->evaluatedMesh() == current->evaluatedMesh());
    requireSameDerived(trusted->content()->derived, current->derived);
    requireSameDerived(trusted->content()->displayedDerived(), current->displayedDerived());
}

TEST_CASE("Scene reload replaces trusted snapshot origin only after successful validation",
          "[editable-scene][vertex-transform][perf-regression]") {
    Scene scene;
    const auto id = scene.createEntity("Cube");
    const auto source = modeling::createEditableCube();
    std::string error;
    const auto initial = scene.prepareEditableGeometry(id, source, error);
    REQUIRE(initial);
    REQUIRE(scene.installGeometry(*initial));
    const auto mesh = scene.find(id)->editableMesh;
    const auto before = *scene.geometrySnapshot(id);
    const auto beforeRecord = *scene.editableMesh(mesh);
    auto loadedNodes = scene.nodes();
    auto loadedMeshes = scene.editableMeshes();
    loadedMeshes[0].source.vertices[0].position.x -= 0.2F;
    bool succeeds = true;
    SECTION("same Scene and identities with changed loaded source") {}
    SECTION("invalid loaded parent preserves the current document and trusted before") {
        loadedNodes[0].parent = id;
        succeeds = false;
    }
    REQUIRE(scene.replaceNodes(loadedNodes, loadedMeshes) == succeeds);
    REQUIRE(scene.find(id)->editableMesh == mesh);
    const std::set<modeling::VertexId> selected{source.vertices[0].id};
    const auto oldNoOp = scene.prepareTransformedEditableGeometry(
        id, before, selected, glm::dmat4(1), glm::dmat4(1), error);
    if (succeeds) {
        REQUIRE_FALSE(oldNoOp);
        REQUIRE_FALSE(error.empty());
        REQUIRE(scene.editableMesh(mesh)->content->source == loadedMeshes[0].source);
        REQUIRE_FALSE(scene.prepareTransformedEditableGeometry(
            id, before, selected, glm::dmat4(1),
            glm::translate(glm::dmat4(1), glm::dvec3(0.01, 0.02, 0)), error));
        REQUIRE_FALSE(error.empty());
    } else {
        REQUIRE(oldNoOp);
        REQUIRE(error.empty());
        REQUIRE(oldNoOp->content() == before.content());
        REQUIRE(scene.editableMesh(mesh)->content == beforeRecord.content);
        REQUIRE(scene.editableMesh(mesh)->topologyRevision == beforeRecord.topologyRevision);
        REQUIRE(scene.editableMesh(mesh)->geometryRevision == beforeRecord.geometryRevision);
        REQUIRE(scene.editableMesh(mesh)->evaluationRevision == beforeRecord.evaluationRevision);
    }
    const auto current = *scene.geometrySnapshot(id);
    const auto delta = glm::translate(glm::dmat4(1), glm::dvec3(0.01, 0.02, 0));
    const auto trusted = scene.prepareTransformedEditableGeometry(
        id, succeeds ? current : before, selected, glm::dmat4(1), delta, error);
    INFO(error);
    REQUIRE(trusted);
    const auto full =
        modeling::transformVertices(current.content()->source, selected, glm::dmat4(1), delta);
    REQUIRE(full.mesh);
    REQUIRE(full.derived);
    REQUIRE(trusted->content()->source == *full.mesh);
    requireSameDerived(trusted->content()->derived, *full.derived);
    REQUIRE(scene.editableMesh(mesh)->content == current.content());
}
