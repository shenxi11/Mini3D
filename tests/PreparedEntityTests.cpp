/*
 * 模块名: PreparedEntityTests
 * 功能概述: 验证完整对象和自定义源网格的离线准备、发布与撤销。
 * 对外接口: Catch2 用例；依赖关系: Core Scene、EditableMesh。
 * 输入输出: 合法/非法候选到场景结构和几何身份断言。
 * 异常与错误: 失败候选不得留下可见节点；维护说明: 不引入 Qt 或第二个 Scene。
 */
#include "core/Scene.h"
#include "core/modeling/EditableMesh.h"

#include <catch2/catch_test_macros.hpp>

using namespace mini3d::core;

TEST_CASE("Prepared entity publishes complete properties and reuses identity", "[api][scene]") {
    Scene scene;
    const auto parent = scene.createEntity("parent");
    Scene::EntityCreateOptions options;
    options.name = "完整对象";
    options.parent = parent;
    options.primitive = PrimitiveKind::Cube;
    options.transform.position = {2, 3, 4};
    options.transform.scale = {-2, 1, 3};
    options.surface.tint = {0.2F, 0.4F, 0.6F};
    options.visible = false;
    std::string error;
    auto prepared = scene.prepareEntity(options, error);
    REQUIRE(prepared);
    const auto id = prepared->entityId();
    CHECK(scene.find(id) == nullptr);
    CHECK(scene.find(parent)->children.empty());
    REQUIRE(scene.installPreparedEntity(*prepared));
    CHECK(scene.find(id)->name == options.name);
    CHECK(scene.find(id)->transform.position == options.transform.position);
    CHECK(scene.find(id)->surface == options.surface);
    CHECK_FALSE(scene.find(id)->visible);
    REQUIRE(scene.removePreparedEntity(*prepared));
    CHECK(scene.find(id) == nullptr);
    REQUIRE(scene.installPreparedEntity(*prepared));
    CHECK(scene.find(id)->parent == parent);
    CHECK(scene.find(parent)->children == std::vector<EntityId>{id});
}

TEST_CASE("Prepared entity failures preserve structure and old geometry snapshots",
          "[api][scene]") {
    Scene scene;
    const auto cube = scene.createEntity("cube", 0, PrimitiveKind::Cube);
    const auto before = scene.geometrySnapshot(cube);
    Scene::EntityCreateOptions options;
    options.name = "invalid";
    options.parent = 999;
    std::string error;
    CHECK_FALSE(scene.prepareEntity(options, error));
    CHECK(scene.nodes().size() == 1);
    options.parent = 0;
    options.transform.scale.x = 0;
    CHECK_FALSE(scene.prepareEntity(options, error));
    CHECK(scene.nodes().size() == 1);
    options.transform.scale.x = 1;
    auto prepared = scene.prepareEntity(options, error);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedEntity(*prepared));
    REQUIRE(before);
    CHECK(scene.installGeometry(*before));
    CHECK(scene.find(cube)->primitive == PrimitiveKind::Cube);
}

TEST_CASE("Prepared entity from a previous document or another scene is rejected", "[api][scene]") {
    Scene scene;
    Scene other;
    Scene::EntityCreateOptions options;
    options.name = "unpublished";
    std::string error;
    auto prepared = scene.prepareEntity(options, error);
    REQUIRE(prepared);
    CHECK_FALSE(other.installPreparedEntity(*prepared));
    REQUIRE(scene.replaceNodes({}));
    CHECK_FALSE(scene.installPreparedEntity(*prepared));
    CHECK(scene.nodes().empty());
}

TEST_CASE("Prepared custom mesh publishes once and redo advances source revisions",
          "[api][scene]") {
    Scene scene;
    const auto existing = scene.createEntity("old", 0, PrimitiveKind::Cube);
    const auto snapshot = scene.geometrySnapshot(existing);
    Scene::EntityCreateOptions options;
    options.name = "装甲源网格";
    auto source = modeling::createEditableCube();
    std::string error;
    auto invalid = source;
    invalid.faces.front().corners.front().vertex = 999;
    CHECK_FALSE(scene.prepareEntity(options, error, &invalid));
    CHECK(scene.nodes().size() == 1);
    invalid = source;
    invalid.faces.front().material = 1;
    CHECK_FALSE(scene.prepareEntity(options, error, &invalid));
    auto prepared = scene.prepareEntity(options, error, &source);
    REQUIRE(prepared);
    CHECK(scene.find(prepared->entityId()) == nullptr);
    CHECK(scene.editableMesh(prepared->meshId()) == nullptr);
    REQUIRE(scene.installPreparedEntity(*prepared));
    REQUIRE(scene.editableMesh(prepared->meshId()));
    CHECK(scene.editableMesh(prepared->meshId())->content->source == source);
    const auto revision = scene.editableMesh(prepared->meshId())->geometryRevision;
    REQUIRE(scene.removePreparedEntity(*prepared));
    REQUIRE(scene.installPreparedEntity(*prepared));
    CHECK(scene.editableMesh(prepared->meshId())->geometryRevision > revision);
    REQUIRE(snapshot);
    CHECK(scene.installGeometry(*snapshot));
}

TEST_CASE("Confirmed transform snapshots install exactly and reject invalid input",
          "[api][scene]") {
    Scene scene;
    Scene::EntityCreateOptions options;
    options.name = "旋转快照";
    options.transform.rotation = {0.038737595081329346F, 0.16603848338127136F, -0.3174383044242859F,
                                  0.4536607563495636F};
    std::string error;
    auto prepared = scene.prepareEntity(options, error);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedEntity(*prepared));
    const auto id = prepared->entityId();
    const auto before = scene.find(id)->transform;
    auto after = before;
    after.position.x += 3;
    after.scale.z = -2;
    REQUIRE(scene.installTransformSnapshot(id, after));
    CHECK(scene.find(id)->transform.rotation == before.rotation);
    CHECK(scene.find(id)->transform.position == after.position);
    CHECK(scene.find(id)->transform.scale == after.scale);
    REQUIRE(scene.installTransformSnapshot(id, before));
    CHECK(scene.find(id)->transform.rotation == before.rotation);
    auto invalid = before;
    invalid.scale.x = 0;
    CHECK_FALSE(scene.installTransformSnapshot(id, invalid));
    CHECK_FALSE(scene.installTransformSnapshot(999, before));
    CHECK(scene.find(id)->transform.rotation == before.rotation);
    CHECK(scene.find(id)->transform.scale == before.scale);
}
