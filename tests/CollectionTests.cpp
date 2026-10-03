/*
 * 模块名: CollectionTests
 * 功能概述: 验证单层集合的独立层级、可见性、稳定身份、成员快照与版本化持久化。
 * 对外接口: Catch2 [collections] 纯 CPU 用例
 * 依赖关系: Scene、SceneSerializer、Catch2、nlohmann/json
 * 输入输出: 集合和对象操作到状态、JSON 往返与原子拒绝断言。
 * 异常与错误: 无效成员、重复身份或缺失集合不得发布部分状态。
 * 维护说明: 不启动 Qt/GL，不读写文件；实际文档 IO 由编辑器专项用例验收。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

using namespace mini3d::core;
namespace {
SceneDocumentData document(const Scene& scene) {
    SceneDocumentData data;
    data.nodes = scene.nodes();
    data.editableMeshes = scene.editableMeshes();
    data.collections = scene.collections();
    data.lighting = scene.lighting();
    return data;
}

void requireRejectedDocument(const nlohmann::json& json, const SceneDocumentData& original) {
    auto output = original;
    const auto before = SceneSerializer::encode(output);
    std::string error;
    REQUIRE_FALSE(SceneSerializer::decode(json.dump(), output, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(SceneSerializer::encode(output) == before);
    REQUIRE(output.sourceVersion == original.sourceVersion);
}
} // namespace

TEST_CASE("Collections preserve hierarchy transforms and input order while gating visibility",
          "[collections]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto child = scene.createEntity("Child", parent, PrimitiveKind::Cube);
    const auto leaf = scene.createEntity("Leaf", child, PrimitiveKind::Sphere);
    const auto unassigned = scene.createEntity("Unassigned", 0, PrimitiveKind::Plane);
    Transform transform;
    transform.position = {2, 3, -4};
    transform.rotation = glm::quat(glm::radians(glm::vec3(15, 30, 45)));
    transform.scale = {-2, 3, .5F};
    REQUIRE(scene.setTransform(parent, transform));
    transform.position = {-1, .5F, 2};
    REQUIRE(scene.setTransform(child, transform));
    const auto beforeParent = *scene.find(parent);
    const auto beforeChild = *scene.find(child);
    const auto beforeWorld = scene.worldMatrix(leaf);
    const std::vector<SceneCollection> groups{{15, "父集合", false, {parent}},
                                              {4, "子集合", true, {child}}};
    REQUIRE(scene.replaceCollections(groups));
    REQUIRE(scene.collections() == groups);
    REQUIRE_FALSE(scene.isVisible(parent));
    REQUIRE_FALSE(scene.isVisible(child));
    REQUIRE_FALSE(scene.isVisible(leaf));
    REQUIRE(scene.isVisible(unassigned));
    REQUIRE(scene.find(parent)->visible);
    REQUIRE(scene.find(child)->visible);
    REQUIRE(scene.find(parent)->parent == beforeParent.parent);
    REQUIRE(scene.find(parent)->children == beforeParent.children);
    REQUIRE(scene.find(child)->parent == beforeChild.parent);
    REQUIRE(scene.find(child)->children == beforeChild.children);
    REQUIRE(scene.find(parent)->transform.localMatrix() == beforeParent.transform.localMatrix());
    REQUIRE(scene.find(child)->transform.localMatrix() == beforeChild.transform.localMatrix());
    REQUIRE(scene.worldMatrix(leaf) == beforeWorld);
    const auto created = scene.createCollection("Empty");
    REQUIRE(created == 16);
    REQUIRE(scene.collections()[0] == groups[0]);
    REQUIRE(scene.collections()[1] == groups[1]);
    REQUIRE(scene.collections().back() == SceneCollection{created, "Empty", true, {}});
    auto withoutParentGroup = scene.collections();
    withoutParentGroup.erase(withoutParentGroup.begin());
    REQUIRE(scene.replaceCollections(withoutParentGroup));
    REQUIRE(scene.nodes().size() == 4);
    REQUIRE(scene.isVisible(parent));
    REQUIRE(scene.isVisible(child));
    REQUIRE(scene.isVisible(leaf));
    REQUIRE(scene.setVisible(parent, false));
    REQUIRE_FALSE(scene.isVisible(child));
    REQUIRE_FALSE(scene.isVisible(leaf));
    REQUIRE(scene.isVisible(unassigned));
    REQUIRE(scene.setVisible(parent, true));
    REQUIRE(scene.replaceCollections({}));
    REQUIRE(scene.find(parent));
    REQUIRE(scene.find(child));
    REQUIRE(scene.find(leaf));
    REQUIRE(scene.find(child)->parent == parent);
    REQUIRE(scene.worldMatrix(leaf) == beforeWorld);
    REQUIRE(scene.isVisible(leaf));
}

TEST_CASE("Collection replacement is atomic and never rewinds its identity allocator",
          "[collections]") {
    Scene scene;
    const auto first = scene.createEntity("First", 0, PrimitiveKind::Cube);
    const auto second = scene.createEntity("Second", first);
    REQUIRE(scene.createCollection("") == 0);
    REQUIRE(scene.createCollection("Initial") == 1);
    const std::vector<SceneCollection> groups{{50, "High", false, {first}},
                                              {10, "Low", true, {second}}};
    REQUIRE(scene.replaceCollections(groups));
    const auto before = SceneSerializer::encode(document(scene));
    for (int failure = 0; failure < 8; ++failure) {
        auto invalid = groups;
        switch (failure) {
            case 0:
                invalid[0].id = 0;
                break;
            case 1:
                invalid[0].id = std::numeric_limits<CollectionId>::max();
                break;
            case 2:
                invalid[1].id = invalid[0].id;
                break;
            case 3:
                invalid[0].name.clear();
                break;
            case 4:
                invalid[0].id = 1000;
                invalid[0].members.insert(999);
                break;
            case 5:
                invalid[0].members.insert(0);
                break;
            case 6:
                invalid[1].members.insert(first);
                break;
            case 7:
                invalid[0].members.insert(std::numeric_limits<EntityId>::max());
                break;
        }
        INFO("Invalid collection fixture " << failure);
        REQUIRE_FALSE(scene.replaceCollections(invalid));
        REQUIRE(SceneSerializer::encode(document(scene)) == before);
        REQUIRE_FALSE(scene.replaceNodes(scene.nodes(), {}, invalid));
        REQUIRE(SceneSerializer::encode(document(scene)) == before);
    }
    REQUIRE(scene.replaceCollections({}));
    REQUIRE(scene.createCollection("After removal") == 51);
    REQUIRE(scene.replaceCollections(groups));
    REQUIRE(scene.createCollection("After undo-style replacement") == 52);
    REQUIRE(scene.replaceNodes(scene.nodes(), {}, groups));
    REQUIRE(scene.createCollection("After document replacement") == 53);
    REQUIRE(scene.replaceNodes(scene.nodes()));
    REQUIRE(scene.collections().empty());
    REQUIRE(scene.createCollection("After clearing document groups") == 54);
    REQUIRE(scene.replaceCollections(
        {{std::numeric_limits<CollectionId>::max() - 1, "Last valid ID", true, {first}}}));
    REQUIRE(scene.createCollection("Exhausted") == 0);
    REQUIRE(scene.replaceCollections({}));
    REQUIRE(scene.createCollection("Still exhausted") == 0);
}

TEST_CASE("Subtree deletion restoration and duplication retain only their membership delta",
          "[collections]") {
    Scene scene;
    const auto outer = scene.createEntity("Outer");
    const auto root = scene.createEntity("Root", outer);
    const auto child = scene.createEntity("Child", root);
    const auto leaf = scene.createEntity("Leaf", child, PrimitiveKind::Cube);
    const auto unrelated = scene.createEntity("Unrelated");
    REQUIRE(scene.replaceCollections(
        {{8, "Roots", false, {root, unrelated}}, {9, "Leaves", true, {leaf}}}));
    const auto snapshot = scene.snapshotSubtree(root);
    const auto copy = scene.duplicateSubtree(root);
    REQUIRE(copy != 0);
    REQUIRE(copy != root);
    REQUIRE(scene.find(copy)->parent == outer);
    const auto copiedChild = scene.find(copy)->children.front();
    const auto copiedLeaf = scene.find(copiedChild)->children.front();
    REQUIRE(scene.collections()[0].members == std::set<EntityId>{root, unrelated, copy});
    REQUIRE(scene.collections()[1].members == std::set<EntityId>{leaf, copiedLeaf});
    REQUIRE_FALSE(scene.collections()[0].members.contains(copiedChild));
    REQUIRE_FALSE(scene.collections()[1].members.contains(copiedChild));
    REQUIRE_FALSE(scene.isVisible(copiedLeaf));
    REQUIRE(scene.removeEntity(root));
    REQUIRE_FALSE(scene.find(root));
    REQUIRE_FALSE(scene.find(child));
    REQUIRE_FALSE(scene.find(leaf));
    REQUIRE(scene.collections()[0].members == std::set<EntityId>{unrelated, copy});
    REQUIRE(scene.collections()[1].members == std::set<EntityId>{copiedLeaf});
    const auto newcomer = scene.createEntity("Added while deleted");
    auto edited = scene.collections();
    edited[0].name = "Edited roots";
    edited[0].visible = true;
    edited[0].members.insert(newcomer);
    REQUIRE(scene.replaceCollections(edited));
    REQUIRE(scene.restoreSubtree(snapshot));
    REQUIRE(scene.find(root)->parent == outer);
    REQUIRE(scene.find(child)->parent == root);
    REQUIRE(scene.find(leaf)->parent == child);
    REQUIRE(scene.find(outer)->children == std::vector<EntityId>{root, copy});
    REQUIRE(scene.collections()[0].name == "Edited roots");
    REQUIRE(scene.collections()[0].visible);
    REQUIRE(scene.collections()[0].members == std::set<EntityId>{root, unrelated, copy, newcomer});
    REQUIRE(scene.collections()[1].members == std::set<EntityId>{leaf, copiedLeaf});
    REQUIRE(scene.isVisible(leaf));
    REQUIRE(scene.removeEntity(root));
    auto missingGroup = scene.collections();
    missingGroup.erase(missingGroup.begin() + 1);
    REQUIRE(scene.replaceCollections(missingGroup));
    const auto beforeRejectedRestore = SceneSerializer::encode(document(scene));
    REQUIRE_FALSE(scene.restoreSubtree(snapshot));
    REQUIRE(SceneSerializer::encode(document(scene)) == beforeRejectedRestore);
    REQUIRE_FALSE(scene.find(root));
    missingGroup.push_back({9, "Recreated leaves", true, {copiedLeaf}});
    REQUIRE(scene.replaceCollections(missingGroup));
    REQUIRE(scene.restoreSubtree(snapshot));
    REQUIRE(scene.collections()[0].members == std::set<EntityId>{root, unrelated, copy, newcomer});
    REQUIRE(scene.collections()[1].members == std::set<EntityId>{leaf, copiedLeaf});
}

TEST_CASE("Format 3 collections round trip and reject malformed or legacy references atomically",
          "[collections]") {
    using Json = nlohmann::json;
    Scene scene;
    const auto parent = scene.createEntity("Parent", 0, PrimitiveKind::Cube);
    const auto child = scene.createEntity("Child", parent);
    const auto imported = scene.createEntity("Imported");
    REQUIRE(scene.setMeshRenderer(imported, {77, kInvalidAsset}));
    REQUIRE(scene.replaceCollections({{41, "中文集合", true, {parent, imported}},
                                      {12, "Hidden", false, {child}},
                                      {22, "Empty", false, {}}}));
    auto data = document(scene);
    data.assets.push_back({77, "models/场景.gltf", 1});
    data.cursor = {{1.25F, -2.5F, 3.75F}, false};
    const auto encoded = SceneSerializer::encode(data);
    const auto valid = Json::parse(encoded);
    REQUIRE(valid["version"] == 3);
    REQUIRE(valid["editorState"]["upAxis"] == "Y");
    REQUIRE(valid["collections"].size() == 3);
    REQUIRE(valid["collections"][0]["id"] == 41);
    REQUIRE(valid["collections"][1]["id"] == 12);
    REQUIRE(valid["collections"][2]["id"] == 22);
    REQUIRE(valid["collections"][0]["members"] == Json::array({parent, imported}));
    SceneDocumentData loaded;
    std::string error;
    REQUIRE(SceneSerializer::decode(encoded, loaded, error));
    REQUIRE(error.empty());
    REQUIRE(loaded.collections == data.collections);
    REQUIRE(loaded.assets[0].path == data.assets[0].path);
    REQUIRE(loaded.cursor == data.cursor);
    REQUIRE(SceneSerializer::encode(loaded) == encoded);
    Scene restored;
    REQUIRE(restored.replaceNodes(loaded.nodes, loaded.editableMeshes, loaded.collections));
    REQUIRE(restored.collections() == data.collections);
    REQUIRE(restored.find(child)->parent == parent);
    REQUIRE(restored.isVisible(parent));
    REQUIRE_FALSE(restored.isVisible(child));
    REQUIRE(restored.createCollection("Appended after load") == 42);
    const std::vector<std::pair<std::string, Json>> malformedFields{
        {"id", 0},
        {"id", std::numeric_limits<CollectionId>::max()},
        {"id", -1},
        {"id", 1.25},
        {"id", "41"},
        {"id", true},
        {"name", ""},
        {"name", false},
        {"visible", 1},
        {"members", Json::object()},
        {"members", Json::array({parent, parent})},
        {"members", Json::array({0})},
        {"members", Json::array({999})},
        {"members", Json::array({-1})},
        {"members", Json::array({1.25})},
        {"members", Json::array({"1"})},
        {"members", Json::array({true})},
        {"members", Json::array({std::numeric_limits<EntityId>::max()})}};
    for (const auto& [field, value] : malformedFields) {
        auto invalid = valid;
        invalid["collections"][0][field] = value;
        INFO("Invalid collection field " << field << ": " << value.dump());
        requireRejectedDocument(invalid, data);
    }
    for (const auto* field : {"id", "name", "visible", "members"}) {
        auto invalid = valid;
        invalid["collections"][0].erase(field);
        requireRejectedDocument(invalid, data);
    }
    for (const auto& malformed :
         {Json::object(), Json(nullptr), Json(true), Json::array({Json(nullptr)})}) {
        auto invalid = valid;
        invalid["collections"] = malformed;
        requireRejectedDocument(invalid, data);
    }
    auto duplicateId = valid;
    duplicateId["collections"][1]["id"] = duplicateId["collections"][0]["id"];
    requireRejectedDocument(duplicateId, data);
    auto duplicateMembership = valid;
    duplicateMembership["collections"][1]["members"].push_back(parent);
    requireRejectedDocument(duplicateMembership, data);
    auto absent = valid;
    absent.erase("collections");
    REQUIRE(SceneSerializer::decode(absent.dump(), loaded, error));
    REQUIRE(loaded.collections.empty());
    REQUIRE(Json::parse(SceneSerializer::encode(loaded))["collections"] == Json::array());
    for (const auto version : {1, 2}) {
        auto legacy = valid;
        legacy["version"] = version;
        requireRejectedDocument(legacy, data);
        legacy["collections"] = Json::array();
        REQUIRE(SceneSerializer::decode(legacy.dump(), loaded, error));
        REQUIRE(loaded.sourceVersion == version);
        REQUIRE(loaded.collections.empty());
        REQUIRE(loaded.nodes.size() == data.nodes.size());
        REQUIRE(loaded.nodes[1].parent == parent);
        REQUIRE(loaded.cursor == Cursor3D{});
        legacy.erase("collections");
        REQUIRE(SceneSerializer::decode(legacy.dump(), loaded, error));
        REQUIRE(loaded.collections.empty());
        legacy["collections"] = Json::object();
        requireRejectedDocument(legacy, data);
    }
}
