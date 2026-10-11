/*
 * 模块名: AnimationPreparedReplayTests
 * 功能概述: 验证真实变换历史数值候选与名称交换无写真源副作用。
 * 对外接口: Catch2 [animation-prepared-replay]。
 * 依赖关系: Core Scene；输入输出: prepared before/after与原字符串缓冲。
 * 异常与错误: 跨Scene候选拒绝，空名称拒绝保持双方缓冲。
 * 维护说明: 指针互换证明所测名称swap没有另分配，不宣称全局分配失败注入。
 */
#include "core/Scene.h"
#include <catch2/catch_test_macros.hpp>

using namespace mini3d::core;
TEST_CASE("Prepared transform history supplies exact numerical overlays without installation",
          "[animation][animation-prepared-replay]") {
    Scene scene;
    const auto parent = scene.createEntity("parent");
    const auto child = scene.createEntity("child", parent, PrimitiveKind::Cube);
    Transform after;
    after.position = {2, 3, 4};
    std::string error;
    auto prepared = scene.prepareTransformBatch({{parent, after}}, error, 1024, 64 * 1024 * 1024);
    REQUIRE(prepared);
    const auto forward = scene.animationPoseInputs(*prepared, true, error);
    const auto backward = scene.animationPoseInputs(*prepared, false, error);
    REQUIRE(forward);
    REQUIRE(backward);
    REQUIRE(forward->size() == 2);
    REQUIRE(forward->at(0).entity == parent);
    REQUIRE(forward->at(0).base.position == after.position);
    REQUIRE(forward->at(1).entity == child);
    REQUIRE(forward->at(1).parent == parent);
    REQUIRE(backward->at(0).base.position == glm::vec3(0));
    REQUIRE(scene.find(parent)->transform.position == glm::vec3(0));
    REQUIRE(scene.installPreparedTransformBatch(*prepared));
    REQUIRE(scene.animationPoseInputs(*prepared, false, error)->at(0).base.position == glm::vec3(0));
    REQUIRE(scene.find(parent)->transform.position == after.position);
    Scene other;
    REQUIRE_FALSE(other.animationPoseInputs(*prepared, true, error));
}
TEST_CASE("Prepared names exchange exact allocated buffers and can replay without copying",
          "[animation][animation-prepared-replay]") {
    Scene scene;
    const auto id = scene.createEntity(std::string(80, 'a'));
    std::string prepared(90, 'b');
    const auto* originalBuffer = scene.find(id)->name.data();
    const auto* candidateBuffer = prepared.data();
    REQUIRE(scene.exchangeEntityName(id, prepared));
    REQUIRE(scene.find(id)->name == std::string(90, 'b'));
    REQUIRE(scene.find(id)->name.data() == candidateBuffer);
    REQUIRE(prepared.data() == originalBuffer);
    REQUIRE(scene.exchangeEntityName(id, prepared));
    REQUIRE(scene.find(id)->name == std::string(80, 'a'));
    REQUIRE(scene.find(id)->name.data() == originalBuffer);
    std::string empty;
    REQUIRE_FALSE(scene.exchangeEntityName(id, empty));
    REQUIRE(scene.find(id)->name == std::string(80, 'a'));
    REQUIRE(empty.empty());
}
TEST_CASE("Verified collection snapshots exchange storage without rolling back identity watermarks",
          "[animation][animation-prepared-replay]") {
    Scene scene;
    const auto member = scene.createEntity("member");
    REQUIRE(scene.replaceCollections({{2, "original", true, {member}}}));
    std::vector<SceneCollection> prepared{{90, "candidate", false, {member}}};
    Scene candidate;
    REQUIRE(candidate.createEntity("member") == member);
    REQUIRE(candidate.replaceCollections(prepared));
    const auto* before = scene.collections().data();
    const auto* after = prepared.data();
    REQUIRE(scene.exchangeCollectionSnapshot(prepared));
    REQUIRE(scene.collections().data() == after);
    REQUIRE(prepared.data() == before);
    REQUIRE(scene.collections().front().id == 90);
    REQUIRE_FALSE(scene.collections().front().visible);
    REQUIRE(scene.exchangeCollectionSnapshot(prepared));
    REQUIRE(scene.collections().data() == before);
    REQUIRE(scene.collections().front().id == 2);
    REQUIRE(scene.createCollection("next") == 91);
    std::vector<SceneCollection> invalid{{91, "bad reference", true, {member + 100}}};
    REQUIRE_FALSE(scene.exchangeCollectionSnapshot(invalid));
    REQUIRE(scene.collections().front().id == 2);
}

TEST_CASE("Scene document author domain is stable across copying and internal node replacement",
          "[animation][animation-prepared-replay][j1-contract]") {
    Scene scene;
    const auto document = scene.documentUuid();
    REQUIRE_FALSE(document.isNil());
    const auto id = scene.createEntity("original", 0, PrimitiveKind::Cube);
    Scene copy = scene;
    REQUIRE(copy.documentUuid() == document);
    REQUIRE(copy.find(id));
    REQUIRE(scene.replaceNodes(scene.nodes()));
    REQUIRE(scene.documentUuid() == document);
    Scene replacement;
    const auto replacementDocument = replacement.documentUuid();
    REQUIRE(replacementDocument != document);
    scene = std::move(replacement);
    REQUIRE(scene.documentUuid() == replacementDocument);
}

TEST_CASE("Material candidates publish stable asset watermarks only on successful install",
          "[animation][animation-prepared-replay][j1-contract]") {
    Scene scene;
    SceneMaterialData candidate;
    MaterialDefinition definition;
    definition.id = MaterialId{31};
    definition.parameters.roughness = .20000000000000004;
    candidate.assets.emplace(StableAssetId{31}, SceneAssetDefinition{StableAssetId{31}, SceneAssetKind::Material, definition});
    std::string error;
    const auto before = scene.materialDataOwner();
    auto prepared = scene.prepareMaterialData(candidate, error);
    REQUIRE(prepared);
    REQUIRE(prepared->hasChanges());
    REQUIRE(scene.materialDataOwner() == before);
    REQUIRE(scene.idHighWatermarks().asset == 0);
    REQUIRE_FALSE(scene.materialData().find(definition.id));
    Scene other;
    REQUIRE_FALSE(other.installPreparedMaterialData(*prepared));
    REQUIRE(scene.installPreparedMaterialData(*prepared));
    const auto after = scene.materialDataOwner();
    REQUIRE(scene.idHighWatermarks().asset == 31);
    REQUIRE(scene.materialData().find(definition.id)->parameters.roughness == definition.parameters.roughness);
    REQUIRE(scene.restorePreparedMaterialData(*prepared));
    REQUIRE(scene.materialDataOwner() == before);
    REQUIRE(scene.idHighWatermarks().asset == 31);
    REQUIRE_FALSE(scene.prepareMaterialData(candidate, error));
    REQUIRE(scene.installPreparedMaterialData(*prepared));
    REQUIRE(scene.materialDataOwner() == after);
    auto noChange = scene.prepareMaterialData(scene.materialData(), error);
    REQUIRE(noChange);
    REQUIRE_FALSE(noChange->hasChanges());
    REQUIRE(scene.idHighWatermarks().asset == 31);
    const auto entity = scene.createEntity("mesh", 0, PrimitiveKind::Cube);
    REQUIRE(scene.setMaterialSlots(entity, {{MaterialSlotId{1}, definition.id}}));
    REQUIRE_FALSE(scene.prepareMaterialData({}, error));
    REQUIRE_FALSE(scene.restorePreparedMaterialData(*prepared));
    REQUIRE(scene.materialDataOwner() == after);
}

TEST_CASE("Duplicate material slots reserve privately share materials and replay their fixed IDs",
          "[animation][animation-prepared-replay][j1-contract]") {
    Scene scene;
    SceneMaterialData materials;
    MaterialDefinition definition;
    definition.id = MaterialId{31};
    materials.assets.emplace(StableAssetId{31}, SceneAssetDefinition{StableAssetId{31}, SceneAssetKind::Material, definition});
    std::string error;
    auto preparedMaterials = scene.prepareMaterialData(materials, error);
    REQUIRE(preparedMaterials);
    REQUIRE(scene.installPreparedMaterialData(*preparedMaterials));
    const auto entity = scene.createEntity("mesh", 0, PrimitiveKind::Cube);
    REQUIRE(scene.setMaterialSlots(entity, {{MaterialSlotId{9}, definition.id}}));
    const auto beforeWater = scene.idHighWatermarks();
    auto copy = scene.prepareDuplicateSubtree(entity, error);
    REQUIRE(copy);
    REQUIRE(scene.idHighWatermarks() == beforeWater);
    REQUIRE_FALSE(scene.find(copy->rootId()));
    REQUIRE(scene.installPreparedSubtree(*copy));
    const auto copySlots = scene.find(copy->rootId())->materialSlots;
    REQUIRE(copySlots == std::vector<MaterialSlotBinding>{{MaterialSlotId{10}, definition.id}});
    REQUIRE(scene.find(entity)->materialSlots.front().id == MaterialSlotId{9});
    REQUIRE(scene.idHighWatermarks().materialSlot == 10);
    REQUIRE(scene.removePreparedSubtree(*copy));
    REQUIRE(scene.idHighWatermarks().materialSlot == 10);
    REQUIRE(scene.installPreparedSubtree(*copy));
    REQUIRE(scene.find(copy->rootId())->materialSlots == copySlots);
    REQUIRE(scene.removePreparedSubtree(*copy));
    REQUIRE_FALSE(scene.setMaterialSlots(entity, {{MaterialSlotId{10}, definition.id}}));
    REQUIRE(scene.find(entity)->materialSlots.front().id == MaterialSlotId{9});
    REQUIRE(scene.idHighWatermarks().materialSlot == 10);
}

TEST_CASE("Typed animation candidates retain fixed identities and explicitly reject unsupported subtree edits",
          "[animation][animation-prepared-replay][j1-contract]") {
    Scene scene;
    const auto entity = scene.createEntity("animated", 0, PrimitiveKind::Cube);
    SceneAnimation animation;
    animation.properties.actions = {{ActionId{5}, {{ActionSlotId{7}, PropertyOwnerKind::Entity,
        {{CurveId{8}, PropertyId::Position, PropertyComponent::X, {{KeyId{10}, 1, 0}, {KeyId{11}, 24, 2}}}}}}}};
    animation.properties.bindings = {{scene.documentUuid(), EntityOwner{entity}, ActionId{5}, ActionSlotId{7}}};
    std::string error;
    const auto emptyWater = scene.idHighWatermarks();
    auto prepared = scene.prepareAnimation(animation, error);
    REQUIRE(prepared);
    REQUIRE(scene.idHighWatermarks() == emptyWater);
    REQUIRE(scene.animation().properties.actions.empty());
    REQUIRE(scene.installPreparedAnimation(*prepared));
    const auto publishedWater = scene.idHighWatermarks();
    REQUIRE(publishedWater.action == 5);
    REQUIRE(publishedWater.actionSlot == 7);
    REQUIRE(publishedWater.curve == 8);
    REQUIRE(publishedWater.key == 11);
    REQUIRE_FALSE(scene.prepareDuplicateSubtree(entity, error));
    REQUIRE(error.find("NotReady") != std::string::npos);
    REQUIRE_FALSE(scene.prepareRemoveSubtree(entity, error));
    REQUIRE(error.find("NotReady") != std::string::npos);
    REQUIRE(scene.find(entity));
    REQUIRE(scene.animation() == animation);
    REQUIRE(scene.restorePreparedAnimation(*prepared));
    REQUIRE(scene.animation().properties.actions.empty());
    REQUIRE(scene.idHighWatermarks() == publishedWater);
    REQUIRE_FALSE(scene.prepareAnimation(animation, error));
    REQUIRE(scene.installPreparedAnimation(*prepared));
    REQUIRE(scene.animation() == animation);
    REQUIRE(scene.idHighWatermarks() == publishedWater);
    auto invalid = animation;
    invalid.properties.bindings.front().documentUuid.bytes[15] ^= 1;
    REQUIRE_FALSE(scene.prepareAnimation(invalid, error));
    REQUIRE(scene.animation() == animation);
}

TEST_CASE("New Sun author values stay independent from legacy lights and geometric components",
          "[animation][animation-prepared-replay][j1-contract]") {
    Scene scene;
    const auto sun = scene.createEntity("Sun");
    REQUIRE(scene.setSun(sun, SunComponent{1.5000000000000002}));
    REQUIRE(scene.find(sun)->sun->irradiance == 1.5000000000000002);
    REQUIRE_FALSE(scene.setLight(sun, LightComponent{}));
    REQUIRE_FALSE(scene.setCamera(sun, CameraComponent{}));
    const auto legacy = scene.createEntity("legacy");
    REQUIRE(scene.setLight(legacy, LightComponent{}));
    REQUIRE_FALSE(scene.setSun(legacy, SunComponent{}));
    REQUIRE(scene.find(legacy)->light->intensity == 0.72F);
    const auto mesh = scene.createEntity("mesh", 0, PrimitiveKind::Cube);
    REQUIRE_FALSE(scene.setSun(mesh, SunComponent{}));
    REQUIRE_FALSE(scene.setSun(sun, SunComponent{-1}));
    REQUIRE(scene.find(sun)->sun->irradiance == 1.5000000000000002);
}
