/*
 * 模块名: Appearance
 * 功能概述: 保存实例表面样式和场景方向光，不拥有纹理资源。
 * 对外接口: SurfaceStyle、Lighting
 * 依赖关系: GLM、标准数学库
 * 输入输出: 有限颜色/强度与方向到渲染参数。
 * 异常与错误: isValid 拒绝非法数值或零方向。
 * 维护说明: 颜色为乘色，不改动共享导入材质；不实现 PBR。
 */
#pragma once
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
namespace mini3d::core {
/** @brief 检查有限的 RGB [0,1] 输入。 */
inline bool isValidColor(const glm::vec3& value) {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(value[i]) || value[i] < 0 || value[i] > 1) {
            return false;
        }
    }
    return true;
}
/** @brief 每实例乘色及纹理/顶点色开关；默认保持导入或内置外观。 */
struct SurfaceStyle {
    glm::vec3 tint{1};
    bool useTexture = true;
    bool useVertexColor = true;
    [[nodiscard]] bool isValid() const {
        return isValidColor(tint);
    }
    bool operator==(const SurfaceStyle&) const = default;
};
/** @brief 单方向光（方向指向光源）及无方向环境项；全场景共享。 */
struct Lighting {
    glm::vec3 direction{0.45F, 1, 0.65F};
    glm::vec3 color{1};
    float intensity = 0.72F;
    float ambient = 0.28F;
    [[nodiscard]] bool isValid() const {
        const float length = glm::length(direction);
        return std::isfinite(length) && length > 0.001F && isValidColor(color) &&
               std::isfinite(intensity) && intensity >= 0 && intensity <= 10 &&
               std::isfinite(ambient) && ambient >= 0 && ambient <= 1;
    }
    bool operator==(const Lighting&) const = default;
};
} // namespace mini3d::core
