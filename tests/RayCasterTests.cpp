/*
 * 模块名: RayCasterTests
 * 功能概述: 验证最近命中、变换、可见性和导入资源包围盒。
 * 对外接口: Catch2 自动注册用例
 * 依赖关系: Core、CPU Assets、RayCaster、Catch2
 * 输入输出: 构建小场景并断言 ID/包围盒；不创建窗口或 GL Context。
 * 异常与错误: 查询错误或资源丢失时断言失败。
 * 维护说明: 非均匀缩放用例防止误归一化局部射线导致距离排序错误。
 */
#include "renderer_gl/RayCaster.h"

#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
using renderer_gl::RayCaster;

TEST_CASE("Picking returns nearest visible geometry and stable ties", "[picking]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto far = scene.createEntity("far", 0, core::PrimitiveKind::Cube);
    const auto near = scene.createEntity("near", 0, core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position.z = 2;
    REQUIRE(scene.setTransform(near, transform));
    const core::Ray ray{{0, 0, 5}, {0, 0, -1}};
    REQUIRE(RayCaster::pick(scene, assets, ray) == near);
    REQUIRE(scene.setVisible(near, false));
    REQUIRE(RayCaster::pick(scene, assets, ray) == far);
    REQUIRE(scene.setVisible(near, true));
    REQUIRE(scene.setTransform(near, {}));
    REQUIRE(RayCaster::pick(scene, assets, ray) == far);
    REQUIRE(RayCaster::pick(scene, assets, {{4, 0, 5}, {0, 0, -1}}) == 0);
    REQUIRE(RayCaster::pick(scene, assets, {{0, 0, 5}, {0, 0, 1}}) == 0);
    REQUIRE(RayCaster::pick(scene, assets, {{0, 0, 0}, {1, 0, 0}}) == far);
    REQUIRE(RayCaster::pick(scene, assets, {{0, 0, 0}, {0, 0, 0}}) == 0);
}

TEST_CASE("Local ray parameters stay comparable across parent rotation and signed scales",
          "[picking]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto far = scene.createEntity("far", 0, core::PrimitiveKind::Cube);
    core::Transform farTransform;
    farTransform.scale = {-4, 2, 4};
    REQUIRE(scene.setTransform(far, farTransform));
    const auto parent = scene.createEntity("parent");
    core::Transform parentTransform;
    parentTransform.position = {0, 0, 3};
    parentTransform.rotation = glm::angleAxis(glm::radians(45.0F), glm::vec3(0, 0, 1));
    parentTransform.scale = {-1, 2, 1};
    REQUIRE(scene.setTransform(parent, parentTransform));
    const auto near = scene.createEntity("near", parent, core::PrimitiveKind::Cube);
    core::Transform small;
    small.scale = {0.2F, 0.2F, 0.2F};
    REQUIRE(scene.setTransform(near, small));
    REQUIRE(RayCaster::pick(scene, assets, {{0, 0, 5}, {0, 0, -1}}) == near);
    REQUIRE(scene.setVisible(parent, false));
    REQUIRE(RayCaster::pick(scene, assets, {{0, 0, 5}, {0, 0, -1}}) == far);
    REQUIRE_FALSE(RayCaster::worldBounds(scene, assets, near).isValid());
}

TEST_CASE("Bounds include visible descendants but empty containers cannot be picked",
          "[picking][bounds]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto group = scene.createEntity("group");
    const auto cube = scene.createEntity("cube", group, core::PrimitiveKind::Cube);
    const auto plane = scene.createEntity("plane", group, core::PrimitiveKind::Plane);
    core::Transform transform;
    transform.position.x = 3;
    REQUIRE(scene.setTransform(cube, transform));
    auto bounds = RayCaster::worldBounds(scene, assets, group);
    REQUIRE(bounds.minimum == glm::vec3(-0.6F, -0.5F, -0.6F));
    REQUIRE(bounds.maximum == glm::vec3(3.5F, 0.5F, 0.6F));
    REQUIRE(RayCaster::pick(scene, assets, {{0, 2, 0}, {0, -1, 0}}) == plane);
    REQUIRE(scene.setVisible(cube, false));
    bounds = RayCaster::worldBounds(scene, assets, group);
    REQUIRE(bounds.minimum.y == 0);
    REQUIRE(bounds.maximum.y == 0);
    REQUIRE(scene.setVisible(plane, false));
    REQUIRE_FALSE(RayCaster::worldBounds(scene, assets, group).isValid());
    REQUIRE(RayCaster::pick(scene, assets, {{0, 2, 0}, {0, -1, 0}}) == 0);
    REQUIRE_FALSE(RayCaster::worldBounds(scene, assets, 999).isValid());
}

TEST_CASE("Imported mesh instances use shared CPU bounds with independent transforms",
          "[picking][assets]") {
    assets::AssetManager assets;
    const auto imported =
        assets.importGltf(QString::fromUtf8(MINI3D_SAMPLE_DIRECTORY) + "/BoxTextured.glb");
    REQUIRE(imported.scene != nullptr);
    core::AssetId mesh = 0;
    for (const auto& node : imported.scene->nodes) {
        if (!node.meshes.empty()) {
            mesh = node.meshes.front();
            break;
        }
    }
    REQUIRE(mesh != 0);
    core::Scene scene;
    const auto first = scene.createEntity("first");
    const auto second = scene.createEntity("second");
    REQUIRE(scene.setMeshRenderer(first, {mesh, 0}));
    REQUIRE(scene.setMeshRenderer(second, {mesh, 0}));
    core::Transform transform;
    transform.position.x = 5;
    REQUIRE(scene.setTransform(second, transform));
    const auto bounds = RayCaster::localBounds(*scene.find(first), assets);
    REQUIRE(bounds.minimum == assets.mesh(mesh)->data.bounds().minimum);
    const auto secondBounds = RayCaster::worldBounds(scene, assets, second);
    REQUIRE(secondBounds.minimum == bounds.minimum + glm::vec3(5, 0, 0));
    const auto center = (bounds.minimum + bounds.maximum) * 0.5F;
    REQUIRE(RayCaster::pick(scene, assets, {center + glm::vec3(0, 0, 5), {0, 0, -1}}) == first);
    REQUIRE(RayCaster::pick(scene, assets, {center + glm::vec3(5, 0, 5), {0, 0, -1}}) == second);
    REQUIRE(assets.meshCount() == 1);
}
