/*
 * 模块名: PreparedNewSubtreeTests
 * 功能概述: 验证新导入子树候选的完整准备、原子安装与稳定历史回放。
 * 对外接口: Catch2 [prepared-new-subtree] 纯 CPU 用例。
 * 依赖关系: Scene、SceneSerializer、Catch2；无 Qt 或资源解析。
 * 输入输出: 前序节点参数到结构、属性、身份及失败无发布断言。
 * 异常与错误: 非法候选、来源失效或容量失效不得发布部分子树。
 * 维护说明: 仅验证 Core 安装/收回边界，文档和历史服务由 API 集成测试覆盖。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace mini3d::core;
namespace {
std::string encodedScene(const Scene& scene) {
    SceneDocumentData data;
    data.nodes = scene.nodes();
    data.editableMeshes = scene.editableMeshes();
    data.collections = scene.collections();
    data.lighting = scene.lighting();
    return SceneSerializer::encode(data);
}
std::vector<Scene::SubtreeNodeOptions> newSubtreeOptions() {
    std::vector<Scene::SubtreeNodeOptions> options(4);
    options[0].name = "导入包装根";
    options[1].name = "Branch";
    options[1].parentIndex = 0;
    options[2].name = "Leaf";
    options[2].parentIndex = 1;
    options[3].name = "Sibling";
    options[3].parentIndex = 0;
    return options;
}
} // namespace

TEST_CASE("Prepared new subtree publishes all properties and replays the same nodes",
          "[api][scene][prepared-new-subtree]") {
    Scene scene;
    const auto parent = scene.createEntity("Existing parent", 0, PrimitiveKind::Cube);
    const auto tail = scene.createEntity("Existing child", parent);
    Transform parentTransform;
    parentTransform.position = {10, 0, 0};
    parentTransform.scale = {2, 1, 1};
    REQUIRE(scene.setTransform(parent, parentTransform));
    std::string error;
    const auto editable =
        scene.prepareEditableGeometry(parent, modeling::createEditableCube(), error);
    REQUIRE(editable);
    REQUIRE(scene.installGeometry(*editable));
    const auto oldGeometry = scene.geometrySnapshot(parent);
    REQUIRE(oldGeometry);
    const auto oldContent = scene.editableMesh(scene.find(parent)->editableMesh)->content;
    REQUIRE(scene.replaceCollections({{7, "Existing group", true, {tail}}}));
    const auto before = encodedScene(scene);
    auto options = newSubtreeOptions();
    options[0].transform.position = {2, -3, 4};
    options[0].transform.rotation = glm::quat(glm::radians(glm::vec3(17, -33, 12))) * 3.25F;
    options[0].transform.scale = {-2, 3, 0.5F};
    options[1].visible = false;
    options[2].transform.position = {7, 1, -4};
    options[2].surface.tint = {0.3F, 0.5F, 0.7F};
    options[2].meshRenderer = MeshRendererComponent{41, 73};
    options[3].meshRenderer = MeshRendererComponent{42, 0};
    auto prepared = scene.prepareNewSubtree(options, parent, error);
    REQUIRE(prepared);
    CHECK(error.empty());
    CHECK(encodedScene(scene) == before);
    const auto identities = prepared->entityIdMap();
    REQUIRE(identities.size() == options.size());
    CHECK(prepared->rootId() == identities.at(1));
    for (const auto& [index, id] : identities) {
        CHECK(index >= 1);
        CHECK(index <= options.size());
        CHECK(scene.find(id) == nullptr);
    }
    std::string installed;
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(scene.installPreparedSubtree(*prepared));
        CHECK_FALSE(scene.installPreparedSubtree(*prepared));
        CHECK(prepared->entityIdMap() == identities);
        CHECK(scene.find(parent)->children == std::vector<EntityId>{tail, identities.at(1)});
        CHECK(scene.find(identities.at(1))->children ==
              std::vector<EntityId>{identities.at(2), identities.at(4)});
        CHECK(scene.find(identities.at(2))->children == std::vector<EntityId>{identities.at(3)});
        CHECK(glm::vec3(scene.worldMatrix(prepared->rootId())[3]) == glm::vec3(14, -3, 4));
        for (std::size_t index = 0; index < options.size(); ++index) {
            const auto& input = options[index];
            const auto* node = scene.find(identities.at(index + 1));
            REQUIRE(node);
            CHECK(node->name == input.name);
            CHECK(node->parent == (index == 0 ? parent : identities.at(*input.parentIndex + 1)));
            CHECK(node->transform.position == input.transform.position);
            CHECK(node->transform.rotation == glm::normalize(input.transform.rotation));
            CHECK(node->transform.scale == input.transform.scale);
            CHECK(node->surface == input.surface);
            CHECK(node->visible == input.visible);
            CHECK(node->primitive == PrimitiveKind::Empty);
            CHECK(node->editableMesh == 0);
            CHECK_FALSE(node->camera);
            CHECK_FALSE(node->light);
            REQUIRE(node->meshRenderer.has_value() == input.meshRenderer.has_value());
            if (input.meshRenderer) {
                CHECK(node->meshRenderer->mesh == input.meshRenderer->mesh);
                CHECK(node->meshRenderer->material == input.meshRenderer->material);
            }
        }
        CHECK_FALSE(scene.isVisible(identities.at(3)));
        CHECK(scene.collections()[0].members == std::set<EntityId>{tail});
        if (cycle == 0)
            installed = encodedScene(scene);
        CHECK(encodedScene(scene) == installed);
        REQUIRE(scene.removePreparedSubtree(*prepared));
        CHECK_FALSE(scene.removePreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == before);
        CHECK(scene.installGeometry(*oldGeometry));
        CHECK(scene.editableMesh(scene.find(parent)->editableMesh)->content == oldContent);
    }
}

TEST_CASE("Invalid new subtree inputs never publish nodes or consume their identities",
          "[api][scene][prepared-new-subtree]") {
    Scene scene;
    const auto existing = scene.createEntity("Existing", 0, PrimitiveKind::Cube);
    const auto before = encodedScene(scene);
    auto options = newSubtreeOptions();
    EntityId parent = existing;
    std::size_t maximumEntities = 2048;
    SECTION("empty input") {
        options.clear();
    }
    SECTION("missing external parent") {
        parent = 999;
    }
    SECTION("root has an internal parent") {
        options[0].parentIndex = 0;
    }
    SECTION("second root is not allowed") {
        options[2].parentIndex.reset();
    }
    SECTION("self parent") {
        options[2].parentIndex = 2;
    }
    SECTION("forward parent") {
        options[2].parentIndex = 3;
    }
    SECTION("out of range parent") {
        options[2].parentIndex = 999;
    }
    SECTION("last node has no name") {
        options.back().name.clear();
    }
    SECTION("nonfinite position") {
        options[2].transform.position.y = std::numeric_limits<float>::infinity();
    }
    SECTION("zero scale") {
        options[2].transform.scale.x = 0;
    }
    SECTION("zero rotation") {
        options[2].transform.rotation = glm::quat(0, 0, 0, 0);
    }
    SECTION("invalid surface") {
        options[2].surface.tint.z = -0.1F;
    }
    SECTION("invalid mesh binding") {
        options[2].meshRenderer = MeshRendererComponent{0, 73};
    }
    SECTION("zero entity limit") {
        maximumEntities = 0;
    }
    SECTION("candidate exceeds requested limit") {
        maximumEntities = options.size() - 1;
    }
    std::string error;
    CHECK_FALSE(scene.prepareNewSubtree(options, parent, error, maximumEntities));
    CHECK_FALSE(error.empty());
    CHECK(encodedScene(scene) == before);
    CHECK(scene.find(existing)->children.empty());
    CHECK(scene.createEntity("After rejection") == existing + 1);
}

TEST_CASE("New subtree preparation rejects composed nonfinite world matrices",
          "[api][scene][prepared-new-subtree]") {
    Scene scene;
    auto options = newSubtreeOptions();
    EntityId parent = 0;
    SECTION("new ancestor and child overflow while each local TRS is valid") {
        options[0].transform.scale = glm::vec3(1.0e30F);
        options[2].transform.scale = glm::vec3(1.0e30F);
    }
    SECTION("existing finite parent times wrapper overflows") {
        parent = scene.createEntity("Large parent");
        Transform transform;
        transform.scale = glm::vec3(1.0e30F);
        REQUIRE(scene.setTransform(parent, transform));
        options[0].transform.scale = transform.scale;
    }
    SECTION("existing ancestor composition is already nonfinite") {
        const auto ancestor = scene.createEntity("Large ancestor");
        parent = scene.createEntity("Large parent", ancestor);
        Transform transform;
        transform.scale = glm::vec3(1.0e30F);
        REQUIRE(scene.setTransform(ancestor, transform));
        REQUIRE(scene.setTransform(parent, transform));
    }
    const auto before = encodedScene(scene);
    std::string error;
    CHECK_FALSE(scene.prepareNewSubtree(options, parent, error));
    CHECK_FALSE(error.empty());
    CHECK(encodedScene(scene) == before);
}

TEST_CASE("New subtree preparation uses the runtime reverse-chain matrix association",
          "[api][scene][prepared-new-subtree][prepared-new-subtree-world-association]") {
    Scene scene;
    EntityId parent = 0;
    std::vector<float> scales{0.001F, 0.001F, 1.0e20F, 1.0e20F};
    SECTION("all four nodes are new") {
        REQUIRE(scene.nodes().empty());
    }
    SECTION("the first small scale belongs to an existing external parent") {
        parent = scene.createEntity("Existing small ancestor");
        Transform transform;
        transform.scale = glm::vec3(scales.front());
        REQUIRE(scene.setTransform(parent, transform));
        scales.erase(scales.begin());
    }
    SECTION("the overflow pair spans an existing parent and a new node") {
        for (std::size_t index = 0; index + 1 < scales.size(); ++index) {
            parent = scene.createEntity("Existing ancestor", parent);
            Transform transform;
            transform.scale = glm::vec3(scales[index]);
            REQUIRE(scene.setTransform(parent, transform));
        }
        REQUIRE(std::isfinite(scene.worldMatrix(parent)[0][0]));
        scales.erase(scales.begin(), scales.end() - 1);
    }
    std::vector<Scene::SubtreeNodeOptions> options(scales.size());
    for (std::size_t index = 0; index < options.size(); ++index) {
        options[index].name = "Matrix chain";
        options[index].transform.scale = glm::vec3(scales[index]);
        REQUIRE(options[index].transform.isValid());
        if (index != 0)
            options[index].parentIndex = index - 1;
    }
    const auto before = encodedScene(scene);
    std::string error;
    CHECK_FALSE(scene.prepareNewSubtree(options, parent, error));
    CHECK_FALSE(error.empty());
    CHECK(encodedScene(scene) == before);
}

TEST_CASE("New subtree count and identity limits are checked before publication",
          "[api][scene][prepared-new-subtree]") {
    Scene scene;
    std::string error;
    SECTION("exact default boundary is accepted and one over is rejected") {
        std::vector<Scene::SubtreeNodeOptions> options(2048);
        for (std::size_t index = 0; index < options.size(); ++index) {
            options[index].name = "Node";
            if (index != 0)
                options[index].parentIndex = 0;
        }
        options.push_back(options.back());
        CHECK_FALSE(scene.prepareNewSubtree(options, 0, error));
        CHECK(scene.nodes().empty());
        options.pop_back();
        auto prepared = scene.prepareNewSubtree(options, 0, error);
        REQUIRE(prepared);
        CHECK(prepared->entityIdMap().at(1) == 1);
        CHECK(prepared->entityIdMap().size() == 2048);
        REQUIRE(scene.installPreparedSubtree(*prepared));
        CHECK(scene.nodes().size() == 2048);
        REQUIRE(scene.removePreparedSubtree(*prepared));
        CHECK(scene.nodes().empty());
        REQUIRE(scene.installPreparedSubtree(*prepared));
        CHECK(scene.nodes().size() == 2048);
    }
    SECTION("entity exhaustion rejects the whole candidate") {
        SceneNode high;
        high.id = std::numeric_limits<EntityId>::max() - 2;
        high.name = "High identity";
        REQUIRE(scene.replaceNodes({high}));
        const auto before = encodedScene(scene);
        CHECK_FALSE(scene.prepareNewSubtree(newSubtreeOptions(), 0, error));
        CHECK_FALSE(error.empty());
        CHECK(encodedScene(scene) == before);
        CHECK(scene.createEntity("Last available") == high.id + 1);
    }
}

TEST_CASE("New subtree candidates reject changed provenance or capacity as a whole",
          "[api][scene][prepared-new-subtree]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    std::string error;
    auto prepared = scene.prepareNewSubtree(newSubtreeOptions(), parent, error);
    REQUIRE(prepared);
    const auto identities = prepared->entityIdMap();
    SECTION("another Scene cannot use the candidate") {
        Scene other;
        const auto before = encodedScene(other);
        CHECK_FALSE(other.installPreparedSubtree(*prepared));
        CHECK_FALSE(other.removePreparedSubtree(*prepared));
        CHECK(encodedScene(other) == before);
        REQUIRE(scene.installPreparedSubtree(*prepared));
    }
    SECTION("replacement in the same Scene invalidates the candidate") {
        REQUIRE(scene.replaceNodes(scene.nodes()));
        const auto before = encodedScene(scene);
        CHECK_FALSE(scene.installPreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == before);
    }
    SECTION("missing external parent rejects every new node") {
        REQUIRE(scene.removeEntity(parent));
        const auto before = encodedScene(scene);
        CHECK_FALSE(scene.installPreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == before);
    }
    SECTION("intervening child uses reserved sibling capacity") {
        const auto capacity = scene.find(parent)->children.capacity();
        for (std::size_t index = 0; index < capacity; ++index)
            REQUIRE(scene.createEntity("Later child", parent) != 0);
        const auto before = encodedScene(scene);
        CHECK_FALSE(scene.installPreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == before);
    }
    SECTION("abandoning a prepared candidate publishes nothing") {
        const auto before = encodedScene(scene);
        prepared.reset();
        CHECK(encodedScene(scene) == before);
        for (const auto& [index, id] : identities)
            CHECK(scene.find(id) == nullptr);
        auto fresh = scene.prepareNewSubtree(newSubtreeOptions(), parent, error);
        REQUIRE(fresh);
        CHECK(fresh->rootId() > identities.rbegin()->second);
        REQUIRE(scene.installPreparedSubtree(*fresh));
        CHECK(scene.nodes().size() == 5);
    }
}

TEST_CASE("New subtree undo waits for later child and collection edits to be reversed",
          "[api][scene][prepared-new-subtree]") {
    Scene scene;
    const auto existing = scene.createEntity("Existing");
    REQUIRE(scene.replaceCollections({{7, "Existing group", true, {existing}}}));
    const auto before = encodedScene(scene);
    std::string error;
    auto prepared = scene.prepareNewSubtree(newSubtreeOptions(), 0, error);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedSubtree(*prepared));
    const auto installed = encodedScene(scene);
    SECTION("later child must be undone first") {
        const auto extra = scene.createEntity("Later child", prepared->rootId());
        const auto changed = encodedScene(scene);
        CHECK_FALSE(scene.removePreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == changed);
        REQUIRE(scene.removeEntity(extra));
    }
    SECTION("later membership must be undone first") {
        const auto groups = scene.collections();
        auto changed = groups;
        changed[0].members.insert(prepared->rootId());
        REQUIRE(scene.replaceCollections(changed));
        const auto changedScene = encodedScene(scene);
        CHECK_FALSE(scene.removePreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == changedScene);
        REQUIRE(scene.replaceCollections(groups));
    }
    CHECK(encodedScene(scene) == installed);
    REQUIRE(scene.removePreparedSubtree(*prepared));
    CHECK(encodedScene(scene) == before);
    REQUIRE(scene.installPreparedSubtree(*prepared));
    CHECK(encodedScene(scene) == installed);
}
