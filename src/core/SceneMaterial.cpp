/*
 * 模块名: SceneMaterial
 * 功能概述: 严格检查线性颜色、PBR 闭区间和 Sun 物理作者域。
 * 对外接口: SceneMaterial.h 校验与只读查找。
 * 依赖关系: 标准数学/容器。
 * 输入输出: 作者定义到合法域与 typed 材质引用。
 * 异常与错误: 错误 kind、重复纹理角色或不可消费数值返回 false。
 * 维护说明: 不钳制作者 roughness，不把旧无量纲灯转换成 Sun。
 */
#include "SceneMaterial.h"

#include <cmath>
#include <limits>
#include <set>

namespace mini3d::core {
namespace {
bool unitValue(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }
bool colorValid(const Color3& color) {
    return unitValue(color.x) && unitValue(color.y) && unitValue(color.z);
}
} // namespace
const MaterialDefinition* SceneMaterialData::find(MaterialId id) const {
    const auto found = assets.find(StableAssetId{id.value});
    return found != assets.end() && found->second.kind == SceneAssetKind::Material &&
           found->second.material && found->second.material->id == id
        ? &*found->second.material : nullptr;
}
bool isPbrMaterialValid(const PbrMaterialParameters& parameters) {
    return colorValid(parameters.baseColor) && unitValue(parameters.roughness) &&
           unitValue(parameters.metallic) && unitValue(parameters.coverage);
}
bool isMaterialDefinitionValid(const MaterialDefinition& definition) {
    if (!definition.id.isValid() || definition.definitionVersion == 0 ||
        definition.model != MaterialModel::PbrMetallicRoughness || !isPbrMaterialValid(definition.parameters))
        return false;
    std::set<MaterialTextureRole> roles;
    for (const auto& texture : definition.textures) {
        if (!texture.texture.isValid() || !roles.insert(texture.role).second ||
            (texture.role != MaterialTextureRole::BaseColor && texture.role != MaterialTextureRole::Roughness &&
             texture.role != MaterialTextureRole::Metallic)) return false;
    }
    return true;
}
bool isSceneMaterialDataValid(const SceneMaterialData& data) {
    for (const auto& [id, asset] : data.assets) {
        if (!id.isValid() || asset.id != id || asset.kind != SceneAssetKind::Material || !asset.material ||
            asset.material->id.value != id.value || !isMaterialDefinitionValid(*asset.material)) return false;
    }
    return true;
}
bool isSunComponentValid(const SunComponent& sun) {
    return colorValid(sun.color) && std::isfinite(sun.irradiance) && sun.irradiance >= 0 &&
           sun.irradiance <= std::numeric_limits<float>::max() &&
           std::isfinite(static_cast<float>(sun.irradiance));
}
} // namespace mini3d::core
