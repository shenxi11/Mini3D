/*
 * 模块名: ObjectTransformMath
 * 功能概述: 计算对象变换；旋转严格校验TRS，缩放保留旋转并换算局部分量。
 * 对外接口: ObjectTransformMath
 * 依赖关系: Core Transform、GLM
 * 输入输出: 固定 before 和绝对操作增量到候选，不持有模型或历史。
 * 异常与错误: 旋转不可表示或缩放结果非法时拒绝；before保持不变。
 * 维护说明: 零增量直接返回 before，避免归一化误差制造无效历史。
 */
#include "ObjectTransformMath.h"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>

namespace mini3d::editor {
namespace {
std::optional<core::Transform> extract(const glm::mat3& linear, const core::Transform& before,
                                       const glm::vec3& signHint) {
    auto result = before;
    glm::mat3 rotation(1);
    for (int axis = 0; axis < 3; ++axis) {
        const float length = glm::length(linear[axis]);
        if (!std::isfinite(length) || length < 0.001F) {
            return std::nullopt;
        }
        result.scale[axis] = std::copysign(length, signHint[axis]);
        rotation[axis] = linear[axis] / result.scale[axis];
    }
    result.rotation = glm::normalize(glm::quat_cast(rotation));
    if (!result.isValid()) {
        return std::nullopt;
    }
    const auto reconstructed = glm::mat3(result.localMatrix());
    for (int axis = 0; axis < 3; ++axis) {
        if (glm::length(linear[axis] - reconstructed[axis]) >
            1.0e-4F * std::max(1.0F, glm::length(linear[axis]))) {
            return std::nullopt;
        }
    }
    return result;
}
} // namespace

std::optional<core::Transform> ObjectTransformMath::translate(const core::Transform& before,
                                                              const glm::mat4& parentWorld,
                                                              const glm::vec3& worldDelta) {
    auto result = before;
    result.position += glm::vec3(glm::inverse(parentWorld) * glm::vec4(worldDelta, 0));
    return result.isValid() ? std::optional(result) : std::nullopt;
}

std::optional<core::Transform> ObjectTransformMath::rotate(const core::Transform& before,
                                                           const glm::mat4& parentWorld,
                                                           const glm::vec3& worldAxis,
                                                           float radians, int localAxis) {
    if (!std::isfinite(radians) || !before.isValid()) {
        return std::nullopt;
    }
    if (radians == 0) {
        return before;
    }
    if (localAxis >= 0 && localAxis < 3) {
        auto result = before;
        glm::vec3 axis(0);
        axis[localAxis] = 1;
        result.rotation = glm::normalize(before.rotation * glm::angleAxis(radians, axis));
        return result.isValid() ? std::optional(result) : std::nullopt;
    }
    const auto rotation = glm::mat3(glm::rotate(glm::mat4(1), radians, worldAxis));
    const auto parent = glm::mat3(parentWorld);
    return extract(glm::inverse(parent) * rotation * parent * glm::mat3(before.localMatrix()),
                   before, before.scale);
}

std::optional<core::Transform> ObjectTransformMath::scale(const core::Transform& before,
                                                          const glm::mat4& parentWorld,
                                                          const glm::mat3& worldBasis,
                                                          const glm::vec3& factors, bool local) {
    return core::scaleTransform(before, parentWorld, worldBasis, factors, local);
}
} // namespace mini3d::editor
