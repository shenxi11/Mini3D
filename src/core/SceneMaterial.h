/*
 * 模块名: SceneMaterial
 * 功能概述: 定义独立于旧表面/灯的最小 PBR 材质、实体槽和 Sun 作者值。
 * 对外接口: SceneMaterialData、MaterialDefinition、SunComponent 及校验。
 * 依赖关系: SceneIdentity、GLM 和标准容器；无 Qt/OpenGL。
 * 输入输出: binary64 作者参数与稳定资产引用到严格合法域判断。
 * 异常与错误: 未知模型、非法数值或资产 kind 不符返回 false。
 * 维护说明: MaterialId 与持久资产表共用 ID 空间；旧灯强度不换算。
 */
#pragma once

#include "SceneIdentity.h"

#include <glm/vec3.hpp>
#include <map>

namespace mini3d::core {
using Color3 = glm::dvec3;
enum class MaterialModel { PbrMetallicRoughness };
struct PbrMaterialParameters {
    Color3 baseColor{1.0};
    double roughness = 1;
    double metallic = 0;
    double coverage = 1;
    bool operator==(const PbrMaterialParameters&) const = default;
};
enum class MaterialTextureRole { BaseColor, Roughness, Metallic };
struct MaterialTextureBinding {
    MaterialTextureRole role = MaterialTextureRole::BaseColor;
    StableAssetId texture;
    bool operator==(const MaterialTextureBinding&) const = default;
};
struct MaterialDefinition {
    MaterialId id;
    std::uint64_t definitionVersion = 1;
    MaterialModel model = MaterialModel::PbrMetallicRoughness;
    PbrMaterialParameters parameters;
    std::vector<MaterialTextureBinding> textures;
    bool operator==(const MaterialDefinition&) const = default;
};
/** @brief 单一持久资产表记录；本切片只生产 Material，外部资产由后续集成。 */
struct SceneAssetDefinition {
    StableAssetId id;
    SceneAssetKind kind = SceneAssetKind::Material;
    std::optional<MaterialDefinition> material;
    bool operator==(const SceneAssetDefinition&) const = default;
};
struct SceneMaterialData {
    std::map<StableAssetId, SceneAssetDefinition> assets;
    [[nodiscard]] const MaterialDefinition* find(MaterialId id) const;
    bool operator==(const SceneMaterialData&) const = default;
};
struct MaterialSlotBinding {
    MaterialSlotId id;
    MaterialId material;
    bool operator==(const MaterialSlotBinding&) const = default;
};
struct SunComponent {
    double irradiance = 1;
    Color3 color{1.0};
    bool operator==(const SunComponent&) const = default;
};
[[nodiscard]] bool isPbrMaterialValid(const PbrMaterialParameters& parameters);
[[nodiscard]] bool isMaterialDefinitionValid(const MaterialDefinition& definition);
[[nodiscard]] bool isSceneMaterialDataValid(const SceneMaterialData& data);
[[nodiscard]] bool isSunComponentValid(const SunComponent& sun);
} // namespace mini3d::core
