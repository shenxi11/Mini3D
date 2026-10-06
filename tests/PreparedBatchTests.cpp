/*
 * 模块名: PreparedBatchTests
 * 功能概述: 验证基础对象和 local TRS 整组候选、来源保护与精确历史回放。
 * 对外接口: Catch2 [prepared-batch] 纯 CPU 用例。
 * 依赖关系: Scene、SceneSerializer、Catch2；不依赖 Qt、Assets 或 Renderer。
 * 输入输出: 完整批次参数到结构、世界矩阵、预算与失败无发布断言。
 * 异常与错误: 任一候选失败不得留下部分实体或变换。
 * 维护说明: 分配故障在独立 harness 注入，避免修改全套测试的分配器。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace mini3d::core;
namespace {
constexpr std::size_t batchItems = 64;
constexpr std::size_t candidateBytes = 64 * 1024 * 1024;
using Failure = Scene::BatchPrepareFailure;

std::string encodedBatchScene(const Scene& scene) {
    SceneDocumentData data;
    data.nodes = scene.nodes();
    data.editableMeshes = scene.editableMeshes();
    data.collections = scene.collections();
    data.lighting = scene.lighting();
    return SceneSerializer::encode(data);
}
std::vector<Scene::EntityCreateOptions> createBatchOptions(EntityId parent = 0) {
    std::vector<Scene::EntityCreateOptions> options(4);
    const PrimitiveKind kinds[]{PrimitiveKind::Cube, PrimitiveKind::Sphere, PrimitiveKind::Empty,
                                PrimitiveKind::Plane};
    for (std::size_t index = 0; index < options.size(); ++index) {
        options[index].name = "完整基础对象 " + std::to_string(index);
        options[index].parent = parent;
        options[index].primitive = kinds[index];
        options[index].transform.position = {static_cast<float>(index), -3, 4};
        options[index].transform.scale = {-2, 3, 0.5F};
        options[index].transform.rotation = glm::quat(glm::radians(glm::vec3(17, -33, 12))) * 3.25F;
        options[index].surface.tint = {0.3F, 0.5F, 0.7F};
    }
    return options;
}
} // namespace

TEST_CASE("Prepared create batch publishes complete input order and replays exactly",
          "[api][scene][prepared-batch]") {
    Scene scene;
    const auto first = scene.createEntity("Existing parent", 0, PrimitiveKind::Cube);
    const auto second = scene.createEntity("Other parent");
    const auto tail = scene.createEntity("Existing tail", first);
    Transform parentTransform;
    parentTransform.position = {10, -2, 3};
    parentTransform.scale = {-2, 3, 0.5F};
    REQUIRE(scene.setTransform(first, parentTransform));
    std::string error;
    const auto editable = scene.prepareEditableGeometry(first, modeling::createEditableCube(), error);
    REQUIRE(editable);
    REQUIRE(scene.installGeometry(*editable));
    const auto oldGeometry = scene.geometrySnapshot(first);
    const auto mesh = scene.find(first)->editableMesh;
    const auto oldRevision = scene.editableMesh(mesh)->geometryRevision;
    const auto oldContent = scene.editableMesh(mesh)->content;
    const auto before = encodedBatchScene(scene);
    auto options = createBatchOptions(first);
    options[1].parent = second;
    options[2].parent = 0;
    auto prepared = scene.prepareEntityBatch(options, error, batchItems, candidateBytes);
    REQUIRE(prepared);
    CHECK(error.empty());
    CHECK(prepared->estimatedBytes() <= candidateBytes);
    CHECK(encodedBatchScene(scene) == before);
    const auto identities = prepared->entityIds();
    REQUIRE(identities.size() == options.size());
    REQUIRE(prepared->affectedWorldMatrices().size() == options.size());
    std::string installed;
    for (int cycle = 0; cycle < 8; ++cycle) {
        REQUIRE(scene.canInstallPreparedEntityBatch(*prepared));
        REQUIRE(scene.installPreparedEntityBatch(*prepared));
        CHECK_FALSE(scene.installPreparedEntityBatch(*prepared));
        CHECK(scene.find(first)->children == std::vector<EntityId>{tail, identities[0], identities[3]});
        CHECK(scene.find(second)->children == std::vector<EntityId>{identities[1]});
        for (std::size_t index = 0; index < options.size(); ++index) {
            const auto& input = options[index];
            const auto* node = scene.find(identities[index]);
            REQUIRE(node);
            CHECK(node->name == input.name);
            CHECK(node->parent == input.parent);
            CHECK(node->primitive == input.primitive);
            CHECK(node->transform.position == input.transform.position);
            CHECK(node->transform.rotation == glm::normalize(input.transform.rotation));
            CHECK(node->transform.scale == input.transform.scale);
            CHECK(node->surface == input.surface);
            CHECK(prepared->affectedWorldMatrices()[index].first == identities[index]);
            CHECK(prepared->affectedWorldMatrices()[index].second == scene.worldMatrix(identities[index]));
        }
        if (cycle == 0)
            installed = encodedBatchScene(scene);
        CHECK(encodedBatchScene(scene) == installed);
        REQUIRE(scene.removePreparedEntityBatch(*prepared));
        CHECK_FALSE(scene.removePreparedEntityBatch(*prepared));
        CHECK(encodedBatchScene(scene) == before);
        CHECK(scene.editableMesh(mesh)->content == oldContent);
        CHECK(scene.editableMesh(mesh)->geometryRevision == oldRevision);
    }
    REQUIRE(oldGeometry);
    CHECK(scene.installGeometry(*oldGeometry));
    CHECK(scene.editableMesh(mesh)->content == oldContent);
}

TEST_CASE("Invalid create batch items reject the entire candidate before consuming identities",
          "[api][scene][prepared-batch]") {
    Scene scene;
    const auto parent = scene.createEntity("Existing");
    auto options = createBatchOptions(parent);
    Failure expected = Failure::InvalidArgument;
    SECTION("second name is empty") { options[1].name.clear(); }
    SECTION("last local transform is invalid") { options.back().transform.scale.x = 0; }
    SECTION("last surface is invalid") { options.back().surface.tint.z = -0.1F; }
    SECTION("invalid primitive") { options[1].primitive = static_cast<PrimitiveKind>(99); }
    SECTION("camera is outside basic creation") { options[1].camera = CameraComponent{}; }
    SECTION("missing parent") {
        options.back().parent = 999;
        expected = Failure::NotFound;
    }
    SECTION("parent cannot refer to a new batch identity") {
        options.back().parent = parent + 1;
        expected = Failure::NotFound;
    }
    SECTION("empty batch") { options.clear(); }
    SECTION("over 64 items") {
        options.resize(batchItems + 1, options.front());
        expected = Failure::LimitExceeded;
    }
    const auto before = encodedBatchScene(scene);
    std::string error;
    Failure failure = Failure::UnsupportedTransform;
    CHECK_FALSE(scene.prepareEntityBatch(options, error, batchItems, candidateBytes, &failure));
    CHECK(failure == expected);
    CHECK_FALSE(error.empty());
    CHECK(encodedBatchScene(scene) == before);
    CHECK(scene.createEntity("After rejection") == parent + 1);
}

TEST_CASE("Prepared create batch rejects stale source and installation capacity without partial nodes",
          "[api][scene][prepared-batch]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent", 0, PrimitiveKind::Cube);
    std::string error;
    auto prepared = scene.prepareEntityBatch(createBatchOptions(parent), error, batchItems, candidateBytes);
    REQUIRE(prepared);
    SECTION("another scene") {
        Scene other;
        CHECK_FALSE(other.installPreparedEntityBatch(*prepared));
        CHECK(other.nodes().empty());
        CHECK(scene.canInstallPreparedEntityBatch(*prepared));
        return;
    }
    SECTION("new document source token") { REQUIRE(scene.replaceNodes(scene.nodes())); }
    SECTION("parent disappears") { REQUIRE(scene.removeEntity(parent)); }
    SECTION("parent transform changes") {
        Transform changed;
        changed.position.x = 4;
        REQUIRE(scene.setTransform(parent, changed));
    }
    SECTION("parent geometry binding changes") { REQUIRE(scene.setMeshRenderer(parent, {41, 0})); }
    SECTION("parent children change") { REQUIRE(scene.createEntity("Later child", parent) != 0); }
    SECTION("unrelated roots consume the reserved bucket capacity") {
        for (int index = 0; index < 256 && scene.canInstallPreparedEntityBatch(*prepared); ++index)
            REQUIRE(scene.createEntity("Later root") != 0);
    }
    const auto beforeAttempt = encodedBatchScene(scene);
    CHECK_FALSE(scene.canInstallPreparedEntityBatch(*prepared));
    CHECK_FALSE(scene.installPreparedEntityBatch(*prepared));
    CHECK(encodedBatchScene(scene) == beforeAttempt);
    for (const auto id : prepared->entityIds())
        CHECK(scene.find(id) == nullptr);
}

TEST_CASE("Create batch removal rejects later changes before extracting any node",
          "[api][scene][prepared-batch]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    REQUIRE(scene.replaceCollections({{7, "Group", true, {}}}));
    std::string error;
    auto prepared = scene.prepareEntityBatch(createBatchOptions(parent), error, batchItems, candidateBytes);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedEntityBatch(*prepared));
    const auto last = prepared->entityIds().back();
    SECTION("late child") { REQUIRE(scene.createEntity("Later child", last) != 0); }
    SECTION("last node property") { REQUIRE(scene.renameEntity(last, "Changed name")); }
    SECTION("last node geometry") { REQUIRE(scene.setMeshRenderer(last, {41, 0})); }
    SECTION("late collection membership") {
        auto groups = scene.collections();
        groups[0].members.insert(last);
        REQUIRE(scene.replaceCollections(groups));
    }
    const auto changed = encodedBatchScene(scene);
    CHECK_FALSE(scene.removePreparedEntityBatch(*prepared));
    CHECK(encodedBatchScene(scene) == changed);
    for (const auto id : prepared->entityIds())
        CHECK(scene.find(id) != nullptr);
}

TEST_CASE("Create batch exact limits and final capacity budget fail without publication",
          "[api][scene][prepared-batch]") {
    std::string error;
    Failure failure = Failure::InvalidArgument;
    auto options = createBatchOptions();
    options.resize(batchItems, options.front());
    Scene accepted;
    auto prepared = accepted.prepareEntityBatch(options, error, batchItems, candidateBytes);
    REQUIRE(prepared);
    REQUIRE(prepared->entityIds().size() == batchItems);
    const auto required = prepared->estimatedBytes();
    REQUIRE(accepted.installPreparedEntityBatch(*prepared));
    CHECK(accepted.nodes().size() == batchItems);
    Scene rejected;
    CHECK_FALSE(rejected.prepareEntityBatch(options, error, batchItems, required - 1, &failure));
    CHECK(failure == Failure::LimitExceeded);
    CHECK(rejected.nodes().empty());
    CHECK(rejected.createEntity("After final preparation refusal") == 1);
    Scene exhausted;
    SceneNode high;
    high.id = std::numeric_limits<EntityId>::max() - 3;
    high.name = "High identity";
    REQUIRE(exhausted.replaceNodes({high}));
    const auto before = encodedBatchScene(exhausted);
    CHECK_FALSE(exhausted.prepareEntityBatch(createBatchOptions(), error, batchItems, candidateBytes, &failure));
    CHECK(failure == Failure::LimitExceeded);
    CHECK(encodedBatchScene(exhausted) == before);
}

TEST_CASE("Transform batch validates the final overlay and replays exact before after",
          "[api][scene][prepared-batch]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto child = scene.createEntity("Child", parent, PrimitiveKind::Cube);
    const auto leaf = scene.createEntity("Untargeted leaf", child);
    Transform oldChild;
    oldChild.rotation = {0.038737595F, 0.16603848F, -0.3174383F, 0.45366076F};
    oldChild.scale = glm::vec3(1.0e30F);
    REQUIRE(scene.setTransform(child, oldChild));
    const auto exactBeforeRotation = scene.find(child)->transform.rotation;
    std::string error;
    const auto editable = scene.prepareEditableGeometry(child, modeling::createEditableCube(), error);
    REQUIRE(editable);
    REQUIRE(scene.installGeometry(*editable));
    const auto oldGeometry = scene.geometrySnapshot(child);
    const auto mesh = scene.find(child)->editableMesh;
    const auto oldRevision = scene.editableMesh(mesh)->geometryRevision;
    const auto before = encodedBatchScene(scene);
    auto parentAfter = scene.find(parent)->transform;
    parentAfter.scale = glm::vec3(1.0e30F);
    auto childAfter = scene.find(child)->transform;
    childAfter.scale = {1, -2, 0.5F};
    childAfter.position = {2, 3, -1};
    // 逐项装 parent 会先让旧 child 世界矩阵溢出；整组最终状态合法。
    std::vector<Scene::TransformBatchItem> options{{parent, parentAfter}, {child, childAfter}};
    auto prepared = scene.prepareTransformBatch(options, error, batchItems, candidateBytes);
    REQUIRE(prepared);
    CHECK(prepared->hasChanges());
    CHECK(prepared->entityIds() == std::vector<EntityId>{parent, child});
    REQUIRE(prepared->affectedWorldMatrices().size() == 3);
    CHECK(encodedBatchScene(scene) == before);
    std::string installed;
    for (int cycle = 0; cycle < 8; ++cycle) {
        REQUIRE(scene.canInstallPreparedTransformBatch(*prepared));
        REQUIRE(scene.installPreparedTransformBatch(*prepared));
        CHECK_FALSE(scene.installPreparedTransformBatch(*prepared));
        CHECK(scene.find(child)->transform.rotation == exactBeforeRotation);
        for (const auto& [id, world] : prepared->affectedWorldMatrices())
            CHECK(world == scene.worldMatrix(id));
        if (cycle == 0)
            installed = encodedBatchScene(scene);
        CHECK(encodedBatchScene(scene) == installed);
        CHECK(scene.find(leaf)->transform.scale == glm::vec3(1));
        REQUIRE(scene.restorePreparedTransformBatch(*prepared));
        CHECK_FALSE(scene.restorePreparedTransformBatch(*prepared));
        CHECK(encodedBatchScene(scene) == before);
        CHECK(scene.find(child)->transform.rotation == exactBeforeRotation);
        CHECK(scene.editableMesh(mesh)->geometryRevision == oldRevision);
    }
    REQUIRE(oldGeometry);
    CHECK(scene.installGeometry(*oldGeometry));
}

TEST_CASE("Invalid transform batch inputs and final descendant overflow leave all targets unchanged",
          "[api][scene][prepared-batch]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto child = scene.createEntity("Child", parent);
    Transform large;
    large.scale = glm::vec3(1.0e30F);
    REQUIRE(scene.setTransform(child, large));
    std::vector<Scene::TransformBatchItem> options{{parent, Transform{}}, {child, Transform{}}};
    Failure expected = Failure::InvalidArgument;
    SECTION("duplicate target") { options.back().entity = parent; }
    SECTION("last target is missing") {
        options.back().entity = 999;
        expected = Failure::NotFound;
    }
    SECTION("last TRS is invalid") { options.back().transform.scale.y = 0; }
    SECTION("empty batch") { options.clear(); }
    SECTION("over 64 items") {
        options.resize(batchItems + 1, options.front());
        expected = Failure::LimitExceeded;
    }
    SECTION("finite local target pair overflows the final world") {
        options.front().transform = large;
        options.back().transform = large;
        expected = Failure::UnsupportedTransform;
    }
    SECTION("untargeted descendant overflows") {
        options.resize(1);
        options.front().transform = large;
        expected = Failure::UnsupportedTransform;
    }
    const auto before = encodedBatchScene(scene);
    std::string error;
    Failure failure = Failure::LimitExceeded;
    CHECK_FALSE(scene.prepareTransformBatch(options, error, batchItems, candidateBytes, &failure));
    CHECK(failure == expected);
    CHECK_FALSE(error.empty());
    CHECK(encodedBatchScene(scene) == before);
}

TEST_CASE("Both batch matrices use the runtime reverse parent chain association",
          "[api][scene][prepared-batch]") {
    Scene scene;
    EntityId parent = 0;
    for (const auto scale : {0.001F, 0.001F, 1.0e20F}) {
        parent = scene.createEntity("Existing ancestor", parent);
        Transform transform;
        transform.scale = glm::vec3(scale);
        REQUIRE(scene.setTransform(parent, transform));
    }
    std::string error;
    Failure failure = Failure::InvalidArgument;
    SECTION("create leaf with overflow before small ancestors") {
        auto options = createBatchOptions(parent);
        options.resize(1);
        options[0].transform = Transform{};
        options[0].transform.scale = glm::vec3(1.0e20F);
        const auto before = encodedBatchScene(scene);
        CHECK_FALSE(scene.prepareEntityBatch(options, error, batchItems, candidateBytes, &failure));
        CHECK(encodedBatchScene(scene) == before);
    }
    SECTION("transform existing leaf with the same overflow association") {
        const auto leaf = scene.createEntity("Existing leaf", parent);
        Transform transform;
        transform.scale = glm::vec3(1.0e20F);
        const auto before = encodedBatchScene(scene);
        CHECK_FALSE(scene.prepareTransformBatch({{leaf, transform}}, error, batchItems, candidateBytes, &failure));
        CHECK(encodedBatchScene(scene) == before);
    }
    CHECK(failure == Failure::UnsupportedTransform);
}

TEST_CASE("Transform batch provenance freezes hierarchy transforms and geometry contents",
          "[api][scene][prepared-batch]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto target = scene.createEntity("Target", parent, PrimitiveKind::Cube);
    const auto child = scene.createEntity("Child", target);
    std::string error;
    const auto editable = scene.prepareEditableGeometry(target, modeling::createEditableCube(), error);
    REQUIRE(editable);
    REQUIRE(scene.installGeometry(*editable));
    Transform after;
    after.position.x = 4;
    auto prepared = scene.prepareTransformBatch({{target, after}}, error, batchItems, candidateBytes);
    REQUIRE(prepared);
    SECTION("another scene") {
        Scene other;
        CHECK_FALSE(other.installPreparedTransformBatch(*prepared));
        CHECK(other.nodes().empty());
        CHECK(scene.canInstallPreparedTransformBatch(*prepared));
        return;
    }
    SECTION("same address new document token") {
        REQUIRE(scene.replaceNodes(scene.nodes(), scene.editableMeshes(), scene.collections()));
    }
    SECTION("parent disappears") { REQUIRE(scene.removeEntity(parent)); }
    SECTION("target before changes") { REQUIRE(scene.setTransform(target, after)); }
    SECTION("external ancestor TRS changes") { REQUIRE(scene.setTransform(parent, after)); }
    SECTION("untargeted descendant TRS changes") { REQUIRE(scene.setTransform(child, after)); }
    SECTION("untargeted descendant binding changes") { REQUIRE(scene.setMeshRenderer(child, {41, 0})); }
    SECTION("new descendant changes validated closure") { REQUIRE(scene.createEntity("Later", child) != 0); }
    SECTION("target hierarchy changes") { REQUIRE(scene.setParent(target, 0)); }
    SECTION("editable content changes under the same mesh identity") {
        auto source = modeling::createEditableCube();
        for (auto& vertex : source.vertices)
            vertex.position.x += 0.2F;
        const auto changed = scene.prepareEditableGeometry(target, source, error);
        REQUIRE(changed);
        REQUIRE(scene.installGeometry(*changed));
    }
    const auto beforeAttempt = encodedBatchScene(scene);
    CHECK_FALSE(scene.canInstallPreparedTransformBatch(*prepared));
    CHECK_FALSE(scene.installPreparedTransformBatch(*prepared));
    CHECK(encodedBatchScene(scene) == beforeAttempt);
}

TEST_CASE("Transform restore checks the whole after state before changing a target",
          "[api][scene][prepared-batch]") {
    Scene scene;
    const auto first = scene.createEntity("First");
    const auto second = scene.createEntity("Second");
    Transform after;
    after.position.x = 4;
    std::string error;
    auto prepared = scene.prepareTransformBatch({{first, after}, {second, after}}, error, batchItems, candidateBytes);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedTransformBatch(*prepared));
    Transform intervening = after;
    intervening.position.y = 5;
    REQUIRE(scene.setTransform(second, intervening));
    const auto changed = encodedBatchScene(scene);
    CHECK_FALSE(scene.restorePreparedTransformBatch(*prepared));
    CHECK(encodedBatchScene(scene) == changed);
    CHECK(scene.find(first)->transform.position == after.position);
    REQUIRE(scene.installTransformSnapshot(second, after));
    REQUIRE(scene.restorePreparedTransformBatch(*prepared));
    CHECK(scene.find(first)->transform.position == glm::vec3(0));
    CHECK(scene.find(second)->transform.position == glm::vec3(0));
}

TEST_CASE("Transform count boundary no change and final matrix buffer budget are explicit",
          "[api][scene][prepared-batch]") {
    Scene scene;
    std::vector<Scene::TransformBatchItem> options;
    for (std::size_t index = 0; index < batchItems; ++index) {
        const auto id = scene.createEntity("Target");
        options.push_back({id, scene.find(id)->transform});
    }
    std::string error;
    auto unchanged = scene.prepareTransformBatch(options, error, batchItems, candidateBytes);
    REQUIRE(unchanged);
    CHECK_FALSE(unchanged->hasChanges());
    CHECK(unchanged->entityIds().size() == batchItems);
    CHECK(unchanged->affectedWorldMatrices().size() == batchItems);
    const auto before = encodedBatchScene(scene);
    Failure failure = Failure::InvalidArgument;
    CHECK_FALSE(scene.prepareTransformBatch(options, error, batchItems, unchanged->estimatedBytes() - 1, &failure));
    CHECK(failure == Failure::LimitExceeded);
    CHECK(encodedBatchScene(scene) == before);
    options.back().transform.rotation = glm::quat(glm::radians(glm::vec3(11, 27, -8))) * 3.25F;
    auto changed = scene.prepareTransformBatch(options, error, batchItems, candidateBytes);
    REQUIRE(changed);
    CHECK(changed->hasChanges());
    REQUIRE(scene.installPreparedTransformBatch(*changed));
    CHECK(scene.find(options.back().entity)->transform.rotation == glm::normalize(options.back().transform.rotation));
    for (const auto& [id, world] : changed->affectedWorldMatrices())
        CHECK(world == scene.worldMatrix(id));
    REQUIRE(scene.restorePreparedTransformBatch(*changed));
    CHECK(encodedBatchScene(scene) == before);
}
