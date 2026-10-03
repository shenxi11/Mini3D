/*
 * 模块名: Transform
 * 功能概述: 第三周场景数据与变换支持。
 * 对外接口: Transform
 * 依赖关系: C++ 标准库、GLM
 * 输入输出: 输入场景操作，输出节点状态或验证结果。
 * 异常与错误: 非法操作拒绝且保留既有状态；分配失败由运行时报告。
 * 维护说明: 不依赖 Qt/OpenGL，关联关系使用稳定 ID。
 */
#include "Transform.h"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
namespace mini3d::core {
glm::mat4 Transform::localMatrix() const {
    return glm::translate(glm::mat4(1.0F), position) * glm::mat4_cast(glm::normalize(rotation)) *
           glm::scale(glm::mat4(1.0F), scale);
}
bool Transform::isValid() const {
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(position[axis]) || !std::isfinite(scale[axis]) ||
            std::abs(scale[axis]) < 0.001F) {
            return false;
        }
    }
    for (int component = 0; component < 4; ++component) {
        if (!std::isfinite(rotation[component])) {
            return false;
        }
    }
    const float length = glm::length(rotation);
    return std::isfinite(length) && length > 0.000001F;
}
std::optional<Transform> scaleTransform(const Transform& before, const glm::mat4& parentWorld,
                                        const glm::mat3& worldBasis, const glm::vec3& factors,
                                        bool local) {
    if (!before.isValid())
        return std::nullopt;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(factors[axis]) || factors[axis] == 0)
            return std::nullopt;
    }
    if (factors == glm::vec3(1))
        return before;
    auto result = before;
    if (local || (factors.x == factors.y && factors.y == factors.z)) {
        result.scale *= factors;
    } else {
        // 对照Blender对象Resize：归一化真实世界轴，不分解含剪切的矩阵或改动旋转。
        auto axes = glm::dmat3(parentWorld) * glm::dmat3(before.localMatrix());
        const auto determinant = glm::determinant(axes);
        if (!std::isfinite(determinant) || determinant == 0)
            return std::nullopt;
        for (int axis = 0; axis < 3; ++axis)
            axes[axis] = glm::normalize(axes[axis]);
        const glm::dmat3 basis(worldBasis);
        const auto stretched = basis * glm::dmat3(glm::scale(glm::dmat4(1), glm::dvec3(factors))) *
                               glm::transpose(basis) * axes;
        // 先统一反射方向，再逐轴对照原方向恢复符号；包括已有负缩放/镜像父节点。
        const double orientation = glm::determinant(stretched) < 0 ? -1 : 1;
        for (int axis = 0; axis < 3; ++axis) {
            const double sign = glm::dot(stretched[axis] * orientation, axes[axis]) < 0
                                    ? -orientation
                                    : orientation;
            result.scale[axis] *= static_cast<float>(sign * glm::length(stretched[axis]));
        }
    }
    return result.isValid() ? std::optional(result) : std::nullopt;
}
} // namespace mini3d::core
