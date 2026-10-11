/*
 * 模块名: PreparedMaterialSlotsTests
 * 功能概述: 验证可信材质槽准备、首安装发行与精确历史回放。
 * 对外接口: Catch2 [j1-author] Core 用例。
 * 依赖关系: Scene、V5 SceneSerializer、Catch2；不依赖 Qt 或 GPU。
 * 输入输出: 真实 Core 候选到完整 V5、稳定身份和源保护断言。
 * 异常与错误: 非法槽/来源或首次水位变化拒绝，保留守卫合法改动。
 * 维护说明: Undo 保留 spent 水位，普通 setter 不获得重用旧 ID 的权限。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"
#include "core/modeling/EditableMesh.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace mini3d::core;
namespace {
std::string snapshot(const Scene& scene) {
    return SceneSerializer::encode(SceneDocumentData(scene));
}
void addMaterial(Scene& scene, MaterialId material) {
    auto data = scene.materialData();
    MaterialDefinition definition;
    definition.id = material;
    const StableAssetId asset{material.value};
    data.assets.emplace(asset, SceneAssetDefinition{asset, SceneAssetKind::Material, definition});
    std::string error;
    auto prepared = scene.prepareMaterialData(data, error);
    REQUIRE(prepared);
    REQUIRE(scene.installPreparedMaterialData(*prepared));
}
} // namespace

TEST_CASE("First material slot issues once and trusted redo restores its spent identity",
          "[scene][j1-author]") {
    Scene scene;
    const auto entity = scene.createEntity("Cube", 0, PrimitiveKind::Cube);
    addMaterial(scene, MaterialId{31});
    const auto before = snapshot(scene);
    std::string error;
    auto prepared = scene.prepareMaterialSlots(entity, {{MaterialSlotId{51}, MaterialId{31}}}, error);
    REQUIRE(prepared);
    REQUIRE(prepared->hasChanges());
    REQUIRE(snapshot(scene) == before);
    REQUIRE(scene.canInstallPreparedMaterialSlots(*prepared));
    REQUIRE(scene.installPreparedMaterialSlots(*prepared));
    const auto published = snapshot(scene);
    for (int replay = 0; replay < 3; ++replay) {
        REQUIRE(scene.canRestorePreparedMaterialSlots(*prepared));
        REQUIRE(scene.restorePreparedMaterialSlots(*prepared));
        REQUIRE(scene.find(entity)->materialSlots.empty());
        REQUIRE(scene.idHighWatermarks().materialSlot == 51);
        const auto undone = snapshot(scene);
        REQUIRE_FALSE(scene.setMaterialSlots(entity, {{MaterialSlotId{51}, MaterialId{31}}}));
        REQUIRE_FALSE(scene.prepareMaterialSlots(entity, {{MaterialSlotId{51}, MaterialId{31}}}, error));
        REQUIRE(snapshot(scene) == undone);
        REQUIRE(scene.canInstallPreparedMaterialSlots(*prepared));
        REQUIRE(scene.installPreparedMaterialSlots(*prepared));
        REQUIRE(snapshot(scene) == published);
    }
    REQUIRE(scene.restorePreparedMaterialSlots(*prepared));
    SceneDocumentData decoded;
    REQUIRE(SceneSerializer::decode(snapshot(scene), decoded, error));
    Scene reopened;
    REQUIRE(reopened.replaceDocumentSnapshot(decoded, error));
    REQUIRE(reopened.idHighWatermarks().materialSlot == 51);
    REQUIRE_FALSE(reopened.setMaterialSlots(entity, {{MaterialSlotId{51}, MaterialId{31}}}));
    REQUIRE_FALSE(reopened.installPreparedMaterialSlots(*prepared));
    auto next = reopened.prepareMaterialSlots(entity, {{MaterialSlotId{52}, MaterialId{31}}}, error);
    REQUIRE(next);
    REQUIRE(reopened.installPreparedMaterialSlots(*next));
}

TEST_CASE("Prepared material slots reject source token node materials and first water changes",
          "[scene][j1-author]") {
    for (int change = 0; change < 5; ++change) {
        CAPTURE(change);
        Scene scene;
        const auto entity = scene.createEntity("Target", 0, PrimitiveKind::Cube);
        const auto other = scene.createEntity("Other", 0, PrimitiveKind::Cube);
        addMaterial(scene, MaterialId{1});
        std::string error;
        auto prepared = scene.prepareMaterialSlots(entity, {{MaterialSlotId{1}, MaterialId{1}}}, error);
        REQUIRE(prepared);
        if (change == 0) {
            REQUIRE(scene.renameEntity(entity, "Changed"));
        } else if (change == 1) {
            addMaterial(scene, MaterialId{2});
        } else if (change == 2) {
            REQUIRE(scene.setMaterialSlots(other, {{MaterialSlotId{2}, MaterialId{1}}}));
            REQUIRE(scene.setMaterialSlots(other, {}));
        } else if (change == 3) {
            auto data = SceneDocumentData(scene);
            REQUIRE(scene.replaceDocumentSnapshot(data, error));
        } else {
            Scene copied = scene;
            REQUIRE_FALSE(copied.installPreparedMaterialSlots(*prepared));
        }
        const auto afterGuard = snapshot(scene);
        if (change != 4)
            REQUIRE_FALSE(scene.canInstallPreparedMaterialSlots(*prepared));
        if (change != 4)
            REQUIRE_FALSE(scene.installPreparedMaterialSlots(*prepared));
        REQUIRE(snapshot(scene) == afterGuard);
    }
}

TEST_CASE("Material slot preparation rejects invalid references without consuming IDs",
          "[scene][j1-author]") {
    Scene scene;
    const auto entity = scene.createEntity("Cube", 0, PrimitiveKind::Cube);
    const auto other = scene.createEntity("Other", 0, PrimitiveKind::Cube);
    addMaterial(scene, MaterialId{1});
    REQUIRE(scene.setMaterialSlots(other, {{MaterialSlotId{5}, MaterialId{1}}}));
    std::string error;
    const auto before = snapshot(scene);
    for (const auto& slots : std::vector<std::vector<MaterialSlotBinding>>{
            {{MaterialSlotId{}, MaterialId{1}}}, {{MaterialSlotId{6}, MaterialId{9}}},
            {{MaterialSlotId{6}, MaterialId{1}}, {MaterialSlotId{6}, MaterialId{1}}},
            {{MaterialSlotId{5}, MaterialId{1}}}}) {
        REQUIRE_FALSE(scene.prepareMaterialSlots(entity, slots, error));
        REQUIRE(snapshot(scene) == before);
    }
    auto unchanged = scene.prepareMaterialSlots(entity, {}, error);
    REQUIRE(unchanged);
    REQUIRE_FALSE(unchanged->hasChanges());
    auto privateCandidate = scene.prepareMaterialSlots(entity, {{MaterialSlotId{6}, MaterialId{1}}}, error);
    REQUIRE(privateCandidate);
    privateCandidate.reset();
    REQUIRE(snapshot(scene) == before);
}

TEST_CASE("Sun is prepared as one device and rejects every component conflict",
          "[scene][j1-author]") {
    Scene scene;
    Scene::EntityCreateOptions options;
    options.name = "Sun";
    options.sun = SunComponent{2, {.25, .5, 1}};
    std::string error;
    const auto before = snapshot(scene);
    for (int conflict = 0; conflict < 5; ++conflict) {
        CAPTURE(conflict);
        auto invalid = options;
        if (conflict == 0) invalid.camera = CameraComponent{};
        if (conflict == 1) invalid.light = LightComponent{};
        if (conflict == 2) invalid.primitive = PrimitiveKind::Cube;
        if (conflict == 3) invalid.sun->irradiance = -1;
        if (conflict == 4) invalid.sun->irradiance = double(std::numeric_limits<float>::max()) * 2;
        REQUIRE_FALSE(scene.prepareEntityForCommit(invalid, error));
        REQUIRE(snapshot(scene) == before);
    }
    const auto mesh = modeling::createEditableCube();
    REQUIRE_FALSE(scene.prepareEntityForCommit(options, error, &mesh));
    REQUIRE(snapshot(scene) == before);
    REQUIRE_FALSE(scene.prepareEntityBatchForCommit({options}, error, 8, 1024 * 1024));
    REQUIRE(snapshot(scene) == before);
    auto prepared = scene.prepareEntityForCommit(options, error);
    REQUIRE(prepared);
    REQUIRE(snapshot(scene) == before);
    REQUIRE(scene.installPreparedEntity(*prepared));
    REQUIRE(scene.find(prepared->entityId())->sun == options.sun);
    const auto published = snapshot(scene);
    REQUIRE(scene.removePreparedEntity(*prepared));
    REQUIRE(scene.installPreparedEntity(*prepared));
    REQUIRE(snapshot(scene) == published);
}
