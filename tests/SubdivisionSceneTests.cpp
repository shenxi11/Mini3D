/*
 * 模块名: SubdivisionSceneTests
 * 功能概述: 验证细分修改器的原子快照、固定求值顺序、源面可见性和格式 3 往返。
 * 对外接口: Catch2 [subdivision-scene] 纯 CPU 用例
 * 依赖关系: Scene、SceneSerializer、ViewportVisibility、Catch2、nlohmann/json
 * 输入输出: 可编辑源笼与修改器参数到候选、应用快照、来源映射及严格 JSON 断言。
 * 异常与错误: 失败不能改写实时源、revision 或解码输出，停用参数仍须合法。
 * 维护说明: 不启动 Qt/GL；编辑器命令和真实界面由上层专项用例验收。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"
#include "core/ViewportVisibility.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <utility>

using namespace mini3d::core;
namespace {
EntityId createEditableCube(Scene& scene, std::string& error) {
    const auto entity = scene.createEntity("Cube", 0, PrimitiveKind::Cube);
    const auto prepared =
        scene.prepareEditableGeometry(entity, modeling::createEditableCube(), error);
    INFO(error);
    REQUIRE(prepared);
    REQUIRE(scene.installGeometry(*prepared));
    return entity;
}

std::shared_ptr<const EditableMeshContent> content(const Scene& scene, EntityId entity) {
    return scene.editableMesh(scene.find(entity)->editableMesh)->content;
}

SceneDocumentData document(const Scene& scene) {
    SceneDocumentData data;
    data.nodes = scene.nodes();
    data.editableMeshes = scene.editableMeshes();
    return data;
}

void requireRejectedDocument(const nlohmann::json& json, const SceneDocumentData& original,
                             std::string& error) {
    auto output = original;
    const auto before = SceneSerializer::encode(output);
    REQUIRE_FALSE(SceneSerializer::decode(json.dump(), output, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(SceneSerializer::encode(output) == before);
}
} // namespace

TEST_CASE("Subdivision candidates preserve the source and advance revisions only when installed",
          "[subdivision-scene]") {
    Scene scene;
    std::string error;
    const auto entity = createEditableCube(scene, error);
    const auto meshId = scene.find(entity)->editableMesh;
    const auto original = content(scene, entity);
    const auto initialRevision = scene.editableMesh(meshId)->evaluationRevision;
    const auto first = scene.prepareSubdivision(entity, modeling::SubdivisionOptions{}, error);
    INFO(error);
    REQUIRE(first);
    REQUIRE(content(scene, entity) == original);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision == initialRevision);
    REQUIRE(first->content()->source == modeling::createEditableCube());
    REQUIRE(first->content()->source.vertices.size() == 8);
    REQUIRE(first->content()->source.faces.size() == 6);
    REQUIRE(first->content()->evaluatedMesh().vertices.size() == 26);
    REQUIRE(first->content()->evaluatedMesh().faces.size() == 24);
    REQUIRE(first->content()->displayedDerived().triangleSources.size() == 48);
    REQUIRE(scene.installGeometry(*first));
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision > initialRevision);
    const auto enabled = *scene.geometrySnapshot(entity);
    const auto enabledRevision = scene.editableMesh(meshId)->evaluationRevision;
    const auto second =
        scene.prepareSubdivision(entity, modeling::SubdivisionOptions{true, 2}, error);
    INFO(error);
    REQUIRE(second);
    REQUIRE(second->content()->evaluatedMesh().vertices.size() == 98);
    REQUIRE(second->content()->evaluatedMesh().faces.size() == 96);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision == enabledRevision);
    REQUIRE(scene.installGeometry(*second));
    const auto disabled =
        scene.prepareSubdivision(entity, modeling::SubdivisionOptions{false, 2}, error);
    REQUIRE(disabled);
    REQUIRE(disabled->content()->subdivision == modeling::SubdivisionOptions{false, 2});
    REQUIRE_FALSE(disabled->content()->subdivisionEvaluation);
    REQUIRE(disabled->content()->evaluatedMesh() == original->source);
    REQUIRE(disabled->content()->displayedDerived().mesh.indices.size() == 36);
    REQUIRE(scene.installGeometry(*disabled));
    const auto disabledContent = content(scene, entity);
    const auto disabledRevision = scene.editableMesh(meshId)->evaluationRevision;
    for (const auto levels : {0, 3}) {
        REQUIRE_FALSE(
            scene.prepareSubdivision(entity, modeling::SubdivisionOptions{false, levels}, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(content(scene, entity) == disabledContent);
        REQUIRE(scene.editableMesh(meshId)->evaluationRevision == disabledRevision);
    }
    modeling::EditableMesh triangle;
    triangle.vertices = {{1, {0, 0, 0}}, {2, {1, 0, 0}}, {3, {0, 1, 0}}};
    triangle.faces = {{1, {{1, 1}, {2, 2}, {3, 3}}}};
    const auto triangular = scene.prepareEditableGeometry(entity, triangle, error);
    INFO(error);
    REQUIRE(triangular); // 停用修改器不运行四边网格算法。
    REQUIRE_FALSE(triangular->content()->subdivisionEvaluation);
    REQUIRE(scene.installGeometry(*triangular));
    const auto triangleContent = content(scene, entity);
    REQUIRE_FALSE(scene.prepareSubdivision(entity, modeling::SubdivisionOptions{}, error));
    REQUIRE(content(scene, entity) == triangleContent);
    REQUIRE(scene.installGeometry(*disabled));
    const auto removed = scene.prepareSubdivision(entity, std::nullopt, error);
    REQUIRE(removed);
    REQUIRE_FALSE(removed->content()->subdivision);
    REQUIRE_FALSE(removed->content()->subdivisionEvaluation);
    REQUIRE(removed->content()->source == original->source);
    REQUIRE(scene.installGeometry(*removed));
    REQUIRE_FALSE(scene.prepareAppliedSubdivision(entity, error));
    const auto removedRevision = scene.editableMesh(meshId)->evaluationRevision;
    REQUIRE(scene.installGeometry(enabled));
    REQUIRE(content(scene, entity) == first->content());
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision > removedRevision);
    REQUIRE_FALSE(scene.prepareSubdivision(999, modeling::SubdivisionOptions{}, error));
}

TEST_CASE(
    "Applying subdivision bakes the final chain while applying Mirror retains later subdivision",
    "[subdivision-scene]") {
    Scene scene;
    std::string error;
    const auto entity = createEditableCube(scene, error);
    const auto meshId = scene.find(entity)->editableMesh;
    const auto subdivision =
        scene.prepareSubdivision(entity, modeling::SubdivisionOptions{}, error);
    REQUIRE(subdivision);
    REQUIRE(scene.installGeometry(*subdivision));
    const auto singleApplied = scene.prepareAppliedSubdivision(entity, error);
    REQUIRE(singleApplied);
    REQUIRE(singleApplied->content()->source == subdivision->content()->evaluatedMesh());
    REQUIRE_FALSE(singleApplied->content()->mirror);
    REQUIRE_FALSE(singleApplied->content()->subdivision);
    REQUIRE(scene.installGeometry(*singleApplied));
    REQUIRE(content(scene, entity)->source.vertices.size() == 26);
    REQUIRE(scene.installGeometry(*subdivision));
    const auto mirror = scene.prepareMirror(entity, modeling::MirrorOptions{}, error);
    INFO(error);
    REQUIRE(mirror);
    REQUIRE(mirror->content()->subdivision == modeling::SubdivisionOptions{});
    REQUIRE(mirror->content()->mirrorEvaluation->mesh.vertices.size() == 16);
    REQUIRE(mirror->content()->evaluatedMesh().vertices.size() == 52);
    REQUIRE(mirror->content()->evaluatedMesh().faces.size() == 48);
    REQUIRE(scene.installGeometry(*mirror));
    const auto combined = *scene.geometrySnapshot(entity);
    const auto before = content(scene, entity);
    const auto revision = scene.editableMesh(meshId)->evaluationRevision;
    auto sourceEdit = modeling::createEditableCube();
    sourceEdit.vertices.front().position.x -= .1F;
    const auto edited = scene.prepareEditableGeometry(entity, sourceEdit, error);
    INFO(error);
    REQUIRE(edited);
    REQUIRE(edited->content()->source == sourceEdit);
    REQUIRE(edited->content()->mirror == before->mirror);
    REQUIRE(edited->content()->subdivision == before->subdivision);
    REQUIRE(edited->content()->subdivisionEvaluation);
    auto isolated = sourceEdit;
    isolated.vertices.push_back({99, {3, 3, 3}});
    REQUIRE_FALSE(scene.prepareEditableGeometry(entity, isolated, error));
    REQUIRE(content(scene, entity) == before);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision == revision);
    const auto removedMirror = scene.prepareMirror(entity, std::nullopt, error);
    REQUIRE(removedMirror);
    REQUIRE_FALSE(removedMirror->content()->mirror);
    REQUIRE(removedMirror->content()->subdivision == before->subdivision);
    REQUIRE(removedMirror->content()->evaluatedMesh().vertices.size() == 26);
    const auto removedSubdivision = scene.prepareSubdivision(entity, std::nullopt, error);
    REQUIRE(removedSubdivision);
    REQUIRE(removedSubdivision->content()->mirror == before->mirror);
    REQUIRE_FALSE(removedSubdivision->content()->subdivision);
    REQUIRE(removedSubdivision->content()->evaluatedMesh().vertices.size() == 16);
    const auto appliedMirror = scene.prepareAppliedMirror(entity, error);
    INFO(error);
    REQUIRE(appliedMirror);
    REQUIRE(appliedMirror->content()->source == before->mirrorEvaluation->mesh);
    REQUIRE_FALSE(appliedMirror->content()->mirror);
    REQUIRE(appliedMirror->content()->subdivision == before->subdivision);
    REQUIRE(appliedMirror->content()->evaluatedMesh() == before->evaluatedMesh());
    REQUIRE(scene.installGeometry(*appliedMirror));
    REQUIRE(content(scene, entity)->source.vertices.size() == 16);
    REQUIRE(content(scene, entity)->source.faces.size() == 12);
    REQUIRE(scene.installGeometry(combined));
    const auto appliedSubdivision = scene.prepareAppliedSubdivision(entity, error);
    INFO(error);
    REQUIRE(appliedSubdivision);
    REQUIRE(appliedSubdivision->content()->source == before->evaluatedMesh());
    REQUIRE_FALSE(appliedSubdivision->content()->mirror);
    REQUIRE_FALSE(appliedSubdivision->content()->mirrorEvaluation);
    REQUIRE_FALSE(appliedSubdivision->content()->subdivision);
    REQUIRE_FALSE(appliedSubdivision->content()->subdivisionEvaluation);
    REQUIRE(scene.installGeometry(*appliedSubdivision));
    REQUIRE(content(scene, entity)->source.vertices.size() == 52);
    REQUIRE(content(scene, entity)->source.faces.size() == 48);
    const auto appliedRevision = scene.editableMesh(meshId)->evaluationRevision;
    REQUIRE(scene.installGeometry(combined));
    REQUIRE(content(scene, entity) == before);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision > appliedRevision);
}

TEST_CASE("Final subdivision and Mirror face origins share the source visibility contract",
          "[subdivision-scene]") {
    Scene scene;
    std::string error;
    const auto entity = createEditableCube(scene, error);
    const auto mirror = scene.prepareMirror(entity, modeling::MirrorOptions{}, error);
    REQUIRE(mirror);
    REQUIRE(scene.installGeometry(*mirror));
    const auto subdivision =
        scene.prepareSubdivision(entity, modeling::SubdivisionOptions{true, 2}, error);
    INFO(error);
    REQUIRE(subdivision);
    const auto evaluated = subdivision->content();
    REQUIRE(evaluated->source.vertices.size() == 8);
    REQUIRE(evaluated->evaluatedMesh().vertices.size() == 196);
    REQUIRE(evaluated->evaluatedMesh().faces.size() == 192);
    std::map<modeling::FaceId, std::size_t> descendants;
    for (const auto& face : evaluated->evaluatedMesh().faces) {
        const auto intermediate = evaluated->subdivisionEvaluation->faces.at(face.id);
        const auto original = evaluated->mirrorEvaluation->faces.at(intermediate).source;
        REQUIRE(evaluated->sourceFace(face.id) == original);
        ++descendants[original];
    }
    for (const auto& face : evaluated->source.faces) {
        REQUIRE(descendants.at(face.id) == 32);
    }
    ViewportVisibility visibility;
    visibility.editedEntity = entity;
    visibility.faces.insert(1);
    std::size_t hiddenTriangles = 0;
    for (std::size_t i = 0; i < evaluated->displayedDerived().triangleSources.size(); ++i) {
        const auto triangleFace = evaluated->displayedDerived().triangleSources[i].face;
        const auto visible = visibility.isTriangleVisible(entity, *evaluated, i);
        REQUIRE(visible == (evaluated->sourceFace(triangleFace) != 1));
        hiddenTriangles += !visible;
    }
    REQUIRE(hiddenTriangles == 64);
    const auto disabled =
        scene.prepareSubdivision(entity, modeling::SubdivisionOptions{false, 2}, error);
    REQUIRE(disabled);
    REQUIRE_FALSE(disabled->content()->subdivisionEvaluation);
    REQUIRE(disabled->content()->evaluatedMesh() == mirror->content()->evaluatedMesh());
    for (const auto& face : disabled->content()->evaluatedMesh().faces) {
        REQUIRE(disabled->content()->sourceFace(face.id) ==
                disabled->content()->mirrorEvaluation->faces.at(face.id).source);
    }
}

TEST_CASE("Format 3 persists the bounded modifier chain and rejects malformed stacks atomically",
          "[subdivision-scene]") {
    Scene scene;
    std::string error;
    const auto entity = createEditableCube(scene, error);
    auto data = document(scene);
    for (int mask = 0; mask < 4; ++mask) {
        auto& resource = data.editableMeshes.front();
        resource.mirror = (mask & 1) ? std::optional{modeling::MirrorOptions{}} : std::nullopt;
        resource.subdivision =
            (mask & 2) ? std::optional{modeling::SubdivisionOptions{true, 2}} : std::nullopt;
        const auto text = SceneSerializer::encode(data);
        const auto json = nlohmann::json::parse(text);
        const auto& mesh = json["editableMeshes"][0];
        REQUIRE(mesh["modifiers"].is_array());
        REQUIRE(mesh["modifiers"].size() ==
                static_cast<std::size_t>((mask & 1) + ((mask & 2) ? 1 : 0)));
        REQUIRE_FALSE(mesh.contains("modifier"));
        REQUIRE_FALSE(mesh.contains("subdivisionEvaluation"));
        REQUIRE_FALSE(mesh.contains("mirrorEvaluation"));
        REQUIRE_FALSE(mesh.contains("indices"));
        REQUIRE(json["editorState"]["upAxis"] == "Y");
        SceneDocumentData loaded;
        REQUIRE(SceneSerializer::decode(text, loaded, error));
        REQUIRE(loaded.editableMeshes[0].mirror == resource.mirror);
        REQUIRE(loaded.editableMeshes[0].subdivision == resource.subdivision);
        REQUIRE(loaded.editableMeshes[0].source == resource.source);
        REQUIRE(SceneSerializer::encode(loaded) == text);
        Scene restored;
        REQUIRE(restored.replaceNodes(loaded.nodes, loaded.editableMeshes));
        REQUIRE(content(restored, entity)->source.vertices.size() == 8);
        const auto expectedFaces = (mask & 2) ? ((mask & 1) ? 192 : 96) : ((mask & 1) ? 12 : 6);
        REQUIRE(content(restored, entity)->evaluatedMesh().faces.size() == expectedFaces);
    }
    const auto valid = nlohmann::json::parse(SceneSerializer::encode(data));
    REQUIRE(valid["editableMeshes"][0]["modifiers"][0]["type"] == "Mirror");
    REQUIRE(valid["editableMeshes"][0]["modifiers"][1]["type"] == "Subdivision");
    const auto mirror = valid["editableMeshes"][0]["modifiers"][0];
    const auto subdivision = valid["editableMeshes"][0]["modifiers"][1];
    auto legacy = valid;
    legacy["editableMeshes"][0]["modifier"] = mirror;
    legacy["editableMeshes"][0].erase("modifiers");
    SceneDocumentData loaded;
    REQUIRE(SceneSerializer::decode(legacy.dump(), loaded, error));
    REQUIRE(loaded.editableMeshes[0].mirror == modeling::MirrorOptions{});
    REQUIRE_FALSE(loaded.editableMeshes[0].subdivision);
    legacy["editableMeshes"][0].erase("modifier");
    REQUIRE(SceneSerializer::decode(legacy.dump(), loaded, error));
    REQUIRE_FALSE(loaded.editableMeshes[0].mirror);
    REQUIRE_FALSE(loaded.editableMeshes[0].subdivision);
    auto disabled = valid;
    disabled["editableMeshes"][0]["modifiers"][1]["enabled"] = false;
    REQUIRE(SceneSerializer::decode(disabled.dump(), loaded, error));
    REQUIRE(loaded.editableMeshes[0].subdivision == modeling::SubdivisionOptions{false, 2});
    Scene disabledScene;
    REQUIRE(disabledScene.replaceNodes(loaded.nodes, loaded.editableMeshes));
    REQUIRE_FALSE(content(disabledScene, entity)->subdivisionEvaluation);
    REQUIRE(content(disabledScene, entity)->evaluatedMesh().faces.size() == 12);
    for (int failure = 0; failure < 14; ++failure) {
        auto invalid = valid;
        auto& stack = invalid["editableMeshes"][0]["modifiers"];
        switch (failure) {
            case 0:
                stack = nlohmann::json::object();
                break;
            case 1:
                stack = nlohmann::json::array({mirror, mirror});
                break;
            case 2:
                stack = nlohmann::json::array({subdivision, subdivision});
                break;
            case 3:
                stack = nlohmann::json::array({subdivision, mirror});
                break;
            case 4:
                stack[1]["type"] = "Unknown";
                break;
            case 5:
                stack[1]["enabled"] = 1;
                break;
            case 6:
                stack[0]["enabled"] = 0;
                break;
            case 7:
                stack[1]["levels"] = 1.0;
                break;
            case 8:
                stack[1]["enabled"] = false;
                stack[1]["levels"] = 3;
                break;
            case 9:
                stack[1]["levels"] = "1";
                break;
            case 10:
                stack[1]["levels"] = std::numeric_limits<std::uint64_t>::max();
                break;
            case 11:
                stack.push_back(subdivision);
                break;
            case 12:
                stack[1] = "Subdivision";
                break;
            case 13:
                invalid["editableMeshes"][0]["modifier"] = mirror;
                break;
        }
        INFO("Invalid modifier fixture " << failure);
        requireRejectedDocument(invalid, data, error);
    }
    const auto before = content(scene, entity);
    const auto revision = scene.editableMesh(scene.find(entity)->editableMesh)->evaluationRevision;
    data.editableMeshes[0].subdivision = modeling::SubdivisionOptions{false, 3};
    REQUIRE_FALSE(scene.replaceNodes(data.nodes, data.editableMeshes));
    REQUIRE(content(scene, entity) == before);
    REQUIRE(scene.editableMesh(scene.find(entity)->editableMesh)->evaluationRevision == revision);
}
