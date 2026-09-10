/*
 * 模块名: SceneTests
 * 功能概述: 验证场景标识、父子关系与变换边界。
 * 对外接口: SceneTests
 * 依赖关系: C++ 标准库、GLM、Catch2
 * 输入输出: 输入场景操作，输出节点状态或验证结果。
 * 异常与错误: 非法操作拒绝且保留既有状态；分配失败由运行时报告。
 * 维护说明: 不依赖 Qt/OpenGL，关联关系使用稳定 ID。
 */
#include "core/Scene.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>
using namespace mini3d::core;
TEST_CASE("Scene IDs remain stable and roots have deterministic order", "[scene]") {
    Scene scene;
    const auto a = scene.createEntity("父节点");
    const auto b = scene.createEntity("Cube", a, PrimitiveKind::Cube);
    const auto c = scene.createEntity("Sphere");
    REQUIRE(scene.roots() == std::vector<EntityId>{a, c});
    REQUIRE(scene.find(a)->children == std::vector<EntityId>{b});
    REQUIRE(scene.find(b)->parent == a);
    REQUIRE(scene.find(b)->primitive == PrimitiveKind::Cube);
    REQUIRE(scene.createEntity("", a) == kInvalidEntity);
    REQUIRE(scene.createEntity("Invalid", 999) == kInvalidEntity);
    REQUIRE(scene.removeEntity(c));
    REQUIRE(scene.createEntity("New") > c);
    REQUIRE(scene.find(b)->name == "Cube");
}
TEST_CASE("Reparent preserves local transform and rejects cycles atomically", "[scene]") {
    Scene scene;
    const auto a = scene.createEntity("A");
    const auto b = scene.createEntity("B", a);
    const auto c = scene.createEntity("C", b);
    REQUIRE_FALSE(scene.setParent(a, c));
    REQUIRE_FALSE(scene.setParent(a, a));
    REQUIRE_FALSE(scene.setParent(b, 999));
    REQUIRE_FALSE(scene.setParent(999, a));
    REQUIRE(scene.find(a)->parent == 0);
    REQUIRE(scene.find(a)->children == std::vector<EntityId>{b});
    REQUIRE(scene.setParent(c, a));
    REQUIRE(scene.find(b)->children.empty());
    REQUIRE(scene.find(a)->children == std::vector<EntityId>{b, c});
    REQUIRE(scene.setParent(c, a));
    REQUIRE(scene.find(a)->children.size() == 2);
    REQUIRE(scene.setParent(c, 0));
    REQUIRE(scene.roots() == std::vector<EntityId>{a, c});
}
TEST_CASE("World transform composes parent rotation and nonuniform negative scale", "[scene]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto child = scene.createEntity("Child", parent);
    Transform a;
    a.position = {5, 6, 7};
    a.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3(0, 0, 1));
    a.scale = {-2, 3, 0.5F};
    Transform b;
    b.position = {1, 2, 3};
    REQUIRE(scene.setTransform(parent, a));
    REQUIRE(scene.setTransform(child, b));
    REQUIRE(glm::length(glm::vec3(scene.worldMatrix(child)[3]) - glm::vec3(-1, 4, 8.5F)) < 1.0e-5F);
    REQUIRE(scene.setParent(child, 0));
    REQUIRE(scene.find(child)->transform.position == b.position);
    REQUIRE(glm::length(glm::vec3(scene.worldMatrix(child)[3]) - b.position) < 1.0e-5F);
    REQUIRE(scene.worldMatrix(999) == glm::mat4(1.0F));
}
TEST_CASE("Visibility inherits and subtree deletion does not remove siblings", "[scene]") {
    Scene scene;
    const auto a = scene.createEntity("A");
    const auto b = scene.createEntity("B", a);
    const auto c = scene.createEntity("C", b);
    const auto d = scene.createEntity("D", a);
    REQUIRE(scene.setVisible(a, false));
    REQUIRE_FALSE(scene.isVisible(c));
    REQUIRE(scene.find(c)->visible);
    REQUIRE(scene.setVisible(a, true));
    REQUIRE(scene.isVisible(c));
    REQUIRE(scene.removeEntity(b));
    REQUIRE(scene.find(b) == nullptr);
    REQUIRE(scene.find(c) == nullptr);
    REQUIRE(scene.find(a)->children == std::vector<EntityId>{d});
    REQUIRE_FALSE(scene.removeEntity(b));
    REQUIRE_FALSE(scene.isVisible(b));
    REQUIRE(scene.removeEntity(a));
    REQUIRE(scene.roots().empty());
}
TEST_CASE("Invalid transforms and names do not mutate scene state", "[scene]") {
    Scene scene;
    const auto id = scene.createEntity("Cube");
    Transform value;
    value.scale.x = 0;
    REQUIRE_FALSE(scene.setTransform(id, value));
    REQUIRE(scene.find(id)->transform.scale == glm::vec3(1));
    value = Transform{};
    value.position.x = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_FALSE(scene.setTransform(id, value));
    value = Transform{};
    value.rotation = {0, 0, 0, 0};
    REQUIRE_FALSE(scene.setTransform(id, value));
    value.rotation = {2, 0, 0, 0};
    REQUIRE(scene.setTransform(id, value));
    REQUIRE(scene.find(id)->transform.rotation.w == 1.0F);
    REQUIRE_FALSE(scene.renameEntity(id, ""));
    REQUIRE(scene.renameEntity(id, "方块"));
    REQUIRE(scene.find(id)->name == "方块");
    REQUIRE_FALSE(scene.setVisible(999, true));
    REQUIRE_FALSE(scene.setTransform(999, Transform{}));
}

TEST_CASE("Camera and light components follow hierarchy and survive subtree operations",
          "[scene][camera-light]") {
    Scene scene;
    const auto parent = scene.createEntity("Rig");
    const auto camera = scene.createEntity("Camera", parent);
    const auto light = scene.createEntity("Light", parent);
    REQUIRE(scene.setCamera(camera, {60, 0.2F, 200}));
    REQUIRE(scene.setLight(light, {{1, 0.5F, 0}, 2}));
    REQUIRE_FALSE(scene.setCamera(light, {}));
    REQUIRE_FALSE(scene.setLight(camera, {}));
    REQUIRE_FALSE(scene.setMeshRenderer(camera, {1, 1}));
    REQUIRE_FALSE(scene.setCamera(camera, {180, 1, 2}));
    REQUIRE_FALSE(scene.setLight(light, {{1, 1, 1}, -1}));
    Transform rig;
    rig.position = {2, 3, 4};
    rig.rotation = glm::quat(glm::radians(glm::vec3(0, 90, 0)));
    rig.scale = {-2, 3, 4};
    REQUIRE(scene.setTransform(parent, rig));
    const auto lighting = scene.effectiveLighting();
    REQUIRE(glm::length(lighting.direction - glm::vec3(1, 0, 0)) < 1.0e-5F);
    REQUIRE(lighting.intensity == 2);
    const auto vp = scene.cameraViewProjection(camera, 2);
    REQUIRE(vp.has_value());
    const auto ahead = glm::vec3(scene.worldMatrix(camera)[3]) - glm::vec3(5, 0, 0);
    auto clip = *vp * glm::vec4(ahead, 1);
    REQUIRE(std::abs(clip.x / clip.w) < 1.0e-5F);
    REQUIRE(std::abs(clip.y / clip.w) < 1.0e-5F);
    REQUIRE(clip.z / clip.w > -1);
    REQUIRE(clip.z / clip.w < 1);
    REQUIRE_FALSE(scene.cameraViewProjection(camera, 0));
    REQUIRE_FALSE(scene.cameraViewProjection(light, 1));
    const auto copy = scene.duplicateSubtree(parent);
    const auto copied = scene.find(copy)->children;
    REQUIRE(scene.find(copied[0])->camera == scene.find(camera)->camera);
    REQUIRE(scene.find(copied[1])->light == scene.find(light)->light);
    REQUIRE(scene.setLight(copied[1], {{0, 1, 0}, 3}));
    REQUIRE(scene.effectiveLighting().intensity == 2);
    REQUIRE(scene.setVisible(parent, false));
    REQUIRE_FALSE(scene.cameraViewProjection(camera, 1));
    REQUIRE(scene.effectiveLighting().intensity == 3);
    REQUIRE(scene.setVisible(copy, false));
    REQUIRE(scene.effectiveLighting().intensity == 0);
    const auto snapshot = scene.snapshotSubtree(parent);
    REQUIRE(scene.removeEntity(parent));
    REQUIRE(scene.restoreSubtree(snapshot));
    REQUIRE(scene.find(camera)->camera == CameraComponent{60, 0.2F, 200});
    REQUIRE(scene.find(light)->light == LightComponent{{1, 0.5F, 0}, 2});
    auto invalid = scene.nodes();
    invalid[1].light = LightComponent{};
    REQUIRE_FALSE(scene.replaceNodes(invalid));
    REQUIRE(scene.find(camera)->camera.has_value());
    REQUIRE(scene.removeEntity(parent));
    REQUIRE(scene.removeEntity(copy));
    REQUIRE(scene.effectiveLighting() == scene.lighting());
}

TEST_CASE("Subtree snapshots restore IDs and ordering while copies share asset references",
          "[scene]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto first = scene.createEntity("First", parent);
    const auto root = scene.createEntity("Root", parent);
    const auto child = scene.createEntity("Child", root, PrimitiveKind::Cube);
    const auto last = scene.createEntity("Last", parent);
    Transform transform;
    transform.position = {1, 2, 3};
    transform.scale = {-2, 3, 4};
    REQUIRE(scene.setTransform(root, transform));
    REQUIRE(scene.setVisible(child, false));
    REQUIRE(scene.setMeshRenderer(child, {7, 8}));
    const auto snapshot = scene.snapshotSubtree(root);
    REQUIRE(snapshot.rootId() == root);
    REQUIRE_FALSE(scene.restoreSubtree(snapshot));
    REQUIRE(scene.find(parent)->children == std::vector<EntityId>{first, root, last});
    const auto copy = scene.duplicateSubtree(root);
    REQUIRE(copy > last);
    const auto copyChild = scene.find(copy)->children.front();
    REQUIRE(copyChild != child);
    REQUIRE(scene.find(copyChild)->parent == copy);
    REQUIRE(scene.find(copy)->parent == parent);
    REQUIRE(scene.find(copy)->name == "Root Copy");
    REQUIRE(scene.find(copy)->transform.scale == transform.scale);
    REQUIRE_FALSE(scene.find(copyChild)->visible);
    REQUIRE(scene.find(copyChild)->meshRenderer->mesh == 7);
    REQUIRE(scene.find(copyChild)->meshRenderer->material == 8);
    REQUIRE(scene.removeEntity(root));
    REQUIRE(scene.find(child) == nullptr);
    REQUIRE(scene.restoreSubtree(snapshot));
    REQUIRE(scene.find(parent)->children == std::vector<EntityId>{first, root, last, copy});
    REQUIRE(scene.find(root)->children == std::vector<EntityId>{child});
    REQUIRE(scene.find(child)->parent == root);
    REQUIRE(scene.find(root)->transform.position == transform.position);
    REQUIRE(scene.createEntity("New") > copyChild);
    REQUIRE(scene.removeEntity(parent));
    REQUIRE_FALSE(scene.restoreSubtree(snapshot));
    REQUIRE(scene.roots().size() == 1);
    REQUIRE(scene.duplicateSubtree(root) == kInvalidEntity);
    REQUIRE_FALSE(scene.restoreSubtree(scene.snapshotSubtree(root)));
}
