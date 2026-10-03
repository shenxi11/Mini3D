/*
 * 模块名: Mirror02Tests
 * 功能概述: 验证持久 Mirror 候选、应用快照与格式 3 严格往返。
 * 对外接口: Catch2 [mirror02-core]。
 * 依赖关系: Scene、SceneSerializer、Catch2。
 * 输入输出: 可编辑源与参数到历史快照、JSON 和原子拒绝断言。
 * 异常与错误: 断言失败报告回归。
 * 维护说明: 不启动 Qt 或 GPU，UI 入口由编辑器专项检查。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace mini3d::core;

TEST_CASE("Mirror candidates retain source and apply restores through immutable snapshots",
          "[mirror02-core]") {
    Scene scene;
    const auto entity = scene.createEntity("Half");
    auto source = modeling::createEditableCube();
    std::string error;
    REQUIRE(scene.installGeometry(*scene.prepareEditableGeometry(entity, source, error)));
    const auto before = *scene.geometrySnapshot(entity);
    const auto meshId = scene.find(entity)->editableMesh;
    modeling::MirrorOptions options;
    const auto candidate = scene.prepareMirror(entity, options, error);
    INFO(error);
    REQUIRE(candidate);
    REQUIRE_FALSE(scene.editableMesh(meshId)->content->mirror);
    REQUIRE(scene.installGeometry(*candidate));
    const auto revision = scene.editableMesh(meshId)->evaluationRevision;
    REQUIRE(scene.editableMesh(meshId)->content->source == source);
    REQUIRE(scene.editableMesh(meshId)->content->mirror == options);
    REQUIRE(scene.editableMesh(meshId)->content->mirrorEvaluation);
    source.vertices[0].position.x -= 0.1F;
    const auto edited = scene.prepareEditableGeometry(entity, source, error);
    INFO(error);
    REQUIRE(edited);
    REQUIRE(edited->content()->mirror == options);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision == revision);
    options.threshold = -1;
    REQUIRE_FALSE(scene.prepareMirror(entity, options, error));
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision == revision);
    REQUIRE(scene.editableMesh(meshId)->content->mirror);
    const auto applied = scene.prepareAppliedMirror(entity, error);
    INFO(error);
    REQUIRE(applied);
    const auto evaluated = scene.editableMesh(meshId)->content->mirrorEvaluation->mesh;
    REQUIRE(applied->content()->source == evaluated);
    REQUIRE_FALSE(applied->content()->mirror);
    REQUIRE(scene.installGeometry(*applied));
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision > revision);
    REQUIRE(scene.installGeometry(*candidate));
    REQUIRE(scene.editableMesh(meshId)->content->mirror == modeling::MirrorOptions{});
    REQUIRE(scene.installGeometry(before));
    REQUIRE_FALSE(scene.editableMesh(meshId)->content->mirror);
}

TEST_CASE("Format 3 keeps optional Mirror and rejects invalid values atomically",
          "[mirror02-core]") {
    Scene scene;
    const auto entity = scene.createEntity("Cube");
    std::string error;
    REQUIRE(scene.installGeometry(*scene.prepareEditableGeometry(
        entity, modeling::createEditableCube(), error)));
    REQUIRE(scene.installGeometry(*scene.prepareMirror(entity, modeling::MirrorOptions{}, error)));
    SceneDocumentData data;
    data.nodes = scene.nodes();
    data.editableMeshes = scene.editableMeshes();
    const auto json = nlohmann::json::parse(SceneSerializer::encode(data));
    REQUIRE(json["editableMeshes"][0]["modifiers"][0]["type"] == "Mirror");
    SceneDocumentData loaded;
    REQUIRE(SceneSerializer::decode(json.dump(), loaded, error));
    REQUIRE(loaded.editableMeshes[0].mirror == modeling::MirrorOptions{});
    Scene restored;
    REQUIRE(restored.replaceNodes(loaded.nodes, loaded.editableMeshes));
    REQUIRE(restored.editableMeshes()[0].mirror == modeling::MirrorOptions{});
    for (const auto& bad : {nlohmann::json(-1), nlohmann::json("NaN")}) {
        auto invalid = json;
        invalid["editableMeshes"][0]["modifiers"][0]["threshold"] = bad;
        REQUIRE_FALSE(SceneSerializer::decode(invalid.dump(), loaded, error));
        REQUIRE(loaded.editableMeshes[0].mirror == modeling::MirrorOptions{});
    }
    auto legacy = json;
    legacy["editableMeshes"][0].erase("modifiers");
    REQUIRE(SceneSerializer::decode(legacy.dump(), loaded, error));
    REQUIRE_FALSE(loaded.editableMeshes[0].mirror);
}
