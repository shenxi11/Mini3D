/*
 * 模块名: PreparedSubtreeTests
 * 功能概述: 验证联合设备创建和子树复制候选的原子发布及重复历史回放。
 * 对外接口: Catch2 [prepared-subtree] 纯 CPU 用例
 * 依赖关系: Scene、SceneSerializer、Catch2，无 Qt/GL
 * 输入输出: 合法/非法设备和复制候选到结构、身份、集合及几何快照断言。
 * 异常与错误: 守卫拒绝不得发布部分节点或集合成员。
 * 维护说明: 按唯一历史逆序撤销后再重做，不占用共享文件或测试数据。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"

#include <catch2/catch_test_macros.hpp>
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
} // namespace

TEST_CASE("Prepared devices publish all properties and preserve their identity on replay",
          "[api][scene][prepared-subtree]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    Scene::EntityCreateOptions options;
    options.name = "完整设备";
    options.parent = parent;
    options.transform.position = {3, -2, 7};
    options.transform.scale = {-1, 2, 3};
    options.surface.tint = {0.3F, 0.5F, 0.8F};
    options.visible = false;
    SECTION("camera") {
        options.camera = CameraComponent{73, 0.25F, 2300};
    }
    SECTION("directional light") {
        options.light = LightComponent{{0.7F, 0.2F, 0.4F}, 3.5F};
    }
    std::string error;
    auto prepared = scene.prepareEntity(options, error);
    REQUIRE(prepared);
    CHECK(error.empty());
    CHECK(scene.find(prepared->entityId()) == nullptr);
    CHECK(scene.find(parent)->children.empty());
    CHECK(prepared->meshId() == 0);
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(scene.installPreparedEntity(*prepared));
        const auto* node = scene.find(prepared->entityId());
        REQUIRE(node);
        CHECK(node->name == options.name);
        CHECK(node->parent == parent);
        CHECK(node->camera == options.camera);
        CHECK(node->light == options.light);
        CHECK(node->transform.position == options.transform.position);
        CHECK(node->transform.scale == options.transform.scale);
        CHECK(node->surface == options.surface);
        CHECK_FALSE(node->visible);
        CHECK(node->primitive == PrimitiveKind::Empty);
        CHECK_FALSE(node->meshRenderer);
        REQUIRE(scene.removePreparedEntity(*prepared));
        CHECK(scene.find(prepared->entityId()) == nullptr);
        CHECK(scene.find(parent)->children.empty());
    }
}

TEST_CASE("Invalid device unions publish neither a node nor a mesh",
          "[api][scene][prepared-subtree]") {
    Scene scene;
    const auto existing = scene.createEntity("Existing", 0, PrimitiveKind::Cube);
    const auto geometry = scene.geometrySnapshot(existing);
    const auto before = encodedScene(scene);
    Scene::EntityCreateOptions options;
    options.name = "Invalid device";
    std::string error;
    for (const auto camera : {CameraComponent{0, 0.1F, 1000}, CameraComponent{180, 0.1F, 1000},
                              CameraComponent{45, 0.0001F, 1000}, CameraComponent{45, 1, 1},
                              CameraComponent{45, 0.1F, std::numeric_limits<float>::infinity()}}) {
        options.camera = camera;
        CHECK_FALSE(scene.prepareEntity(options, error));
        CHECK_FALSE(error.empty());
        CHECK(encodedScene(scene) == before);
    }
    options.camera.reset();
    for (const auto light : {LightComponent{{-0.1F, 1, 1}, 1}, LightComponent{{1, 1, 1}, -1},
                             LightComponent{{1, 1, 1}, 11},
                             LightComponent{{1, 1, 1}, std::numeric_limits<float>::quiet_NaN()}}) {
        options.light = light;
        CHECK_FALSE(scene.prepareEntity(options, error));
        CHECK_FALSE(error.empty());
        CHECK(encodedScene(scene) == before);
    }
    options.camera = CameraComponent{};
    options.light = LightComponent{};
    CHECK_FALSE(scene.prepareEntity(options, error));
    options.light.reset();
    options.primitive = PrimitiveKind::Cube;
    CHECK_FALSE(scene.prepareEntity(options, error));
    options.primitive = PrimitiveKind::Empty;
    const auto mesh = modeling::createEditableCube();
    CHECK_FALSE(scene.prepareEntity(options, error, &mesh));
    options.camera.reset();
    options.light = LightComponent{};
    options.primitive = PrimitiveKind::Sphere;
    CHECK_FALSE(scene.prepareEntity(options, error));
    options.primitive = PrimitiveKind::Empty;
    CHECK_FALSE(scene.prepareEntity(options, error, &mesh));
    CHECK(encodedScene(scene) == before);
    REQUIRE(geometry);
    CHECK(scene.installGeometry(*geometry));
}

TEST_CASE("Prepared subtree copies properties assets mesh identities and fixed collection members",
          "[api][scene][prepared-subtree]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto source = scene.createEntity("Root", parent);
    const auto edited = scene.createEntity("Edited", source, PrimitiveKind::Cube);
    const auto imported = scene.createEntity("Imported", source);
    const auto camera = scene.createEntity("Camera", imported);
    const auto light = scene.createEntity("Light", source);
    const auto tail = scene.createEntity("Tail", parent);
    REQUIRE(scene.setMeshRenderer(imported, {41, 73}));
    REQUIRE(scene.setCamera(camera, {63, 0.3F, 700}));
    REQUIRE(scene.setLight(light, {{0.3F, 0.5F, 0.9F}, 2.5F}));
    Transform transform;
    transform.position = {2, -3, 4};
    transform.rotation = glm::quat(glm::radians(glm::vec3(20, 35, -10)));
    transform.scale = {-2, 3, 0.5F};
    REQUIRE(scene.setTransform(source, transform));
    SurfaceStyle surface;
    surface.tint = {0.4F, 0.7F, 0.2F};
    REQUIRE(scene.setSurface(edited, surface));
    REQUIRE(scene.setVisible(light, false));
    std::string error;
    const auto editable =
        scene.prepareEditableGeometry(edited, modeling::createEditableCube(), error);
    REQUIRE(editable);
    REQUIRE(scene.installGeometry(*editable));
    const auto mirror = scene.prepareMirror(edited, modeling::MirrorOptions{}, error);
    REQUIRE(mirror);
    REQUIRE(scene.installGeometry(*mirror));
    const auto subdivision =
        scene.prepareSubdivision(edited, modeling::SubdivisionOptions{}, error);
    REQUIRE(subdivision);
    REQUIRE(scene.installGeometry(*subdivision));
    const std::vector<SceneCollection> groups{{7, "Root group", false, {source}},
                                              {11, "Mesh group", true, {edited}}};
    REQUIRE(scene.replaceCollections(groups));
    const auto originalNodes = scene.nodes();
    const auto original = encodedScene(scene);
    const auto originalMesh = scene.find(edited)->editableMesh;
    const auto content = scene.editableMesh(originalMesh)->content;
    const auto originalRevision = scene.editableMesh(originalMesh)->geometryRevision;
    const auto oldGeometry = scene.geometrySnapshot(edited);
    auto prepared = scene.prepareDuplicateSubtree(source, error);
    REQUIRE(prepared);
    CHECK(error.empty());
    const auto identities = prepared->entityIdMap();
    CHECK(identities.size() == 5);
    CHECK(prepared->rootId() == identities.at(source));
    CHECK(encodedScene(scene) == original);
    CHECK(scene.editableMesh(originalMesh)->geometryRevision == originalRevision);
    for (const auto& [from, to] : identities) {
        CHECK(from != to);
        CHECK(scene.find(to) == nullptr);
    }
    MeshId copiedMesh = 0;
    std::uint64_t revision = 0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(scene.installPreparedSubtree(*prepared));
        CHECK_FALSE(scene.installPreparedSubtree(*prepared));
        CHECK(prepared->entityIdMap() == identities);
        CHECK(scene.find(parent)->children ==
              std::vector<EntityId>{source, tail, identities.at(source)});
        for (const auto& sourceNode : originalNodes) {
            const auto mapped = identities.find(sourceNode.id);
            if (mapped == identities.end())
                continue;
            const auto* node = scene.find(mapped->second);
            REQUIRE(node);
            CHECK(node->name == sourceNode.name + (sourceNode.id == source ? " Copy" : ""));
            CHECK(node->parent ==
                  (sourceNode.id == source ? parent : identities.at(sourceNode.parent)));
            std::vector<EntityId> children;
            for (const auto child : sourceNode.children)
                children.push_back(identities.at(child));
            CHECK(node->children == children);
            CHECK(node->transform.position == sourceNode.transform.position);
            CHECK(node->transform.rotation == sourceNode.transform.rotation);
            CHECK(node->transform.scale == sourceNode.transform.scale);
            CHECK(node->surface == sourceNode.surface);
            CHECK(node->visible == sourceNode.visible);
            CHECK(node->primitive == sourceNode.primitive);
            CHECK(node->camera == sourceNode.camera);
            CHECK(node->light == sourceNode.light);
        }
        const auto* assetCopy = scene.find(identities.at(imported));
        REQUIRE(assetCopy->meshRenderer);
        CHECK(assetCopy->meshRenderer->mesh == 41);
        CHECK(assetCopy->meshRenderer->material == 73);
        const auto newMesh = scene.find(identities.at(edited))->editableMesh;
        CHECK(newMesh != originalMesh);
        if (cycle == 0)
            copiedMesh = newMesh;
        CHECK(newMesh == copiedMesh);
        REQUIRE(scene.editableMesh(newMesh));
        CHECK(scene.editableMesh(newMesh)->content == content);
        CHECK(scene.editableMesh(newMesh)->geometryRevision > revision);
        revision = scene.editableMesh(newMesh)->geometryRevision;
        const auto copiedBefore = scene.geometrySnapshot(identities.at(edited));
        auto shifted = content->source;
        for (auto& vertex : shifted.vertices)
            vertex.position.x += 0.125F;
        const auto copiedAfter =
            scene.prepareEditableGeometry(identities.at(edited), shifted, error);
        REQUIRE(copiedBefore);
        REQUIRE(copiedAfter);
        REQUIRE(scene.installGeometry(*copiedAfter));
        CHECK(scene.editableMesh(newMesh)->content->source == shifted);
        CHECK(scene.editableMesh(originalMesh)->content == content);
        REQUIRE(scene.installGeometry(*copiedBefore));
        CHECK(scene.editableMesh(newMesh)->content == content);
        CHECK(scene.editableMesh(originalMesh)->geometryRevision == originalRevision);
        CHECK(scene.collections()[0].members == std::set<EntityId>{source, identities.at(source)});
        CHECK(scene.collections()[1].members == std::set<EntityId>{edited, identities.at(edited)});
        CHECK_FALSE(scene.isVisible(identities.at(source)));
        if (cycle == 0) {
            const auto copyBefore = scene.geometrySnapshot(identities.at(edited));
            auto changedSource = content->source;
            changedSource.vertices[0].position.x -= 0.2F;
            const auto changed =
                scene.prepareEditableGeometry(identities.at(edited), changedSource, error);
            REQUIRE(changed);
            REQUIRE(scene.installGeometry(*changed));
            CHECK(scene.editableMesh(newMesh)->content->source == changedSource);
            CHECK(scene.editableMesh(originalMesh)->content == content);
            CHECK(scene.editableMesh(originalMesh)->geometryRevision == originalRevision);
            REQUIRE(copyBefore);
            REQUIRE(scene.installGeometry(*copyBefore));
            CHECK(scene.editableMesh(newMesh)->content == content);
            revision = scene.editableMesh(newMesh)->geometryRevision;
        }
        REQUIRE(scene.removePreparedSubtree(*prepared));
        CHECK_FALSE(scene.removePreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == original);
        CHECK(scene.collections() == groups);
        CHECK(scene.find(parent)->children == std::vector<EntityId>{source, tail});
    }
    REQUIRE(oldGeometry);
    CHECK(scene.installGeometry(*oldGeometry));
    CHECK(scene.editableMesh(originalMesh)->content == content);
}

TEST_CASE("Prepared subtree scale and identity exhaustion reject without publication",
          "[api][scene][prepared-subtree]") {
    Scene scene;
    const auto root = scene.createEntity("Root");
    const auto child = scene.createEntity("Child", root);
    const auto before = encodedScene(scene);
    std::string error;
    CHECK_FALSE(scene.prepareDuplicateSubtree(999, error));
    CHECK_FALSE(scene.prepareDuplicateSubtree(root, error, 0));
    CHECK_FALSE(scene.prepareDuplicateSubtree(root, error, 1));
    CHECK(encodedScene(scene) == before);
    CHECK(scene.find(root)->children == std::vector<EntityId>{child});
    SECTION("exact 2048 boundary and one over") {
        for (std::size_t index = 2; index < 2048; ++index)
            REQUIRE(scene.createEntity("Leaf", root) != 0);
        auto prepared = scene.prepareDuplicateSubtree(root, error);
        REQUIRE(prepared);
        CHECK(prepared->entityIdMap().size() == 2048);
        REQUIRE(scene.installPreparedSubtree(*prepared));
        CHECK(scene.nodes().size() == 4096);
        REQUIRE(scene.removePreparedSubtree(*prepared));
        CHECK(scene.nodes().size() == 2048);
        REQUIRE(scene.createEntity("One over", root) != 0);
        CHECK_FALSE(scene.prepareDuplicateSubtree(root, error));
        CHECK(scene.nodes().size() == 2049);
    }
    SECTION("entity IDs are exhausted before a two-node duplicate") {
        SceneNode highRoot;
        highRoot.id = std::numeric_limits<EntityId>::max() - 2;
        highRoot.name = "High root";
        SceneNode highChild;
        highChild.id = 1;
        highChild.parent = highRoot.id;
        highChild.name = "Child";
        REQUIRE(scene.replaceNodes({highRoot, highChild}));
        const auto highBefore = encodedScene(scene);
        CHECK_FALSE(scene.prepareDuplicateSubtree(highRoot.id, error));
        CHECK(encodedScene(scene) == highBefore);
    }
    SECTION("editable mesh IDs are exhausted before copying a binding") {
        SceneNode meshNode;
        meshNode.id = 1;
        meshNode.name = "High mesh";
        meshNode.editableMesh = std::numeric_limits<MeshId>::max() - 1;
        EditableMeshResource mesh;
        mesh.id = meshNode.editableMesh;
        mesh.source = modeling::createEditableCube();
        REQUIRE(scene.replaceNodes({meshNode}, {mesh}));
        const auto highBefore = encodedScene(scene);
        CHECK_FALSE(scene.prepareDuplicateSubtree(meshNode.id, error));
        CHECK(encodedScene(scene) == highBefore);
    }
}

TEST_CASE("Prepared subtree guards reject wrong scenes documents parents and collections",
          "[api][scene][prepared-subtree]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto source = scene.createEntity("Root", parent);
    REQUIRE(scene.createEntity("Child", source) != 0);
    REQUIRE(scene.replaceCollections({{7, "Group", true, {source}}}));
    std::string error;
    auto prepared = scene.prepareDuplicateSubtree(source, error);
    REQUIRE(prepared);
    SECTION("another Scene cannot publish the candidate") {
        Scene other;
        const auto before = encodedScene(other);
        CHECK_FALSE(other.installPreparedSubtree(*prepared));
        CHECK(encodedScene(other) == before);
        REQUIRE(scene.installPreparedSubtree(*prepared));
    }
    SECTION("a replacement document invalidates the original candidate") {
        REQUIRE(scene.replaceNodes(scene.nodes(), scene.editableMeshes(), scene.collections()));
        const auto before = encodedScene(scene);
        CHECK_FALSE(scene.installPreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == before);
    }
    SECTION("a missing external parent prevents all publication") {
        REQUIRE(scene.removeEntity(parent));
        const auto before = encodedScene(scene);
        CHECK_FALSE(scene.installPreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == before);
    }
    SECTION("a missing collection prevents all publication") {
        REQUIRE(scene.replaceCollections({}));
        const auto before = encodedScene(scene);
        CHECK_FALSE(scene.installPreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == before);
        REQUIRE(scene.replaceCollections({{7, "Group", true, {source}}}));
        REQUIRE(scene.installPreparedSubtree(*prepared));
        CHECK(scene.collections()[0].members.contains(prepared->rootId()));
    }
}

TEST_CASE("Prepared subtree undo requires later child and collection changes to be reversed",
          "[api][scene][prepared-subtree]") {
    Scene scene;
    const auto source = scene.createEntity("Root");
    REQUIRE(scene.createEntity("Child", source) != 0);
    REQUIRE(scene.replaceCollections({{7, "Group", true, {source}}}));
    std::string error;
    auto prepared = scene.prepareDuplicateSubtree(source, error);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedSubtree(*prepared));
    const auto installed = encodedScene(scene);
    SECTION("a later child must be removed first") {
        const auto extra = scene.createEntity("Later", prepared->rootId());
        const auto changed = encodedScene(scene);
        CHECK_FALSE(scene.removePreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == changed);
        REQUIRE(scene.removeEntity(extra));
    }
    SECTION("a later reassignment must be undone first") {
        const auto groups = scene.collections();
        auto changed = groups;
        changed[0].members.erase(prepared->rootId());
        changed.push_back({11, "Later group", true, {prepared->rootId()}});
        REQUIRE(scene.replaceCollections(changed));
        const auto beforeUndo = encodedScene(scene);
        CHECK_FALSE(scene.removePreparedSubtree(*prepared));
        CHECK(encodedScene(scene) == beforeUndo);
        REQUIRE(scene.replaceCollections(groups));
    }
    CHECK(encodedScene(scene) == installed);
    REQUIRE(scene.removePreparedSubtree(*prepared));
    REQUIRE(scene.installPreparedSubtree(*prepared));
    CHECK(encodedScene(scene) == installed);
}
