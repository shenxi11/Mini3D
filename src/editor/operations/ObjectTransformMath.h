/*
 * 模块名: ObjectTransformMath
 * 功能概述: 从固定对象 TRS 与父矩阵计算移动/旋转/缩放候选，不修改场景。
 * 对外接口: ObjectTransformMath、TransformOperation
 * 依赖关系: Core Transform、GLM、标准库，无 Qt/GL
 * 输入输出: before、父世界矩阵和变换增量到有效局部TRS；缩放保持原旋转。
 * 异常与错误: 非有限、奇异、零缩放或旋转产生不可表示剪切时返回空值。
 * 维护说明: 变换枢轴为当前单对象原点；其他枢轴在 PIVOT-01 接入。
 */
#pragma once

#include "core/Transform.h"

#include <optional>

namespace mini3d::editor {
enum class TransformOperation { Move, Rotate, Scale };

/** @brief 无状态候选计算；调用者始终传入同一 before，不传上一帧预览。 */
class ObjectTransformMath final {
  public:
    [[nodiscard]] static std::optional<core::Transform> translate(const core::Transform& before,
                                                                  const glm::mat4& parentWorld,
                                                                  const glm::vec3& worldDelta);
    /** @brief localAxis 为 0/1/2 时精确修改局部旋转；-1 使用给定世界轴。 */
    [[nodiscard]] static std::optional<core::Transform> rotate(const core::Transform& before,
                                                               const glm::mat4& parentWorld,
                                                               const glm::vec3& worldAxis,
                                                               float radians, int localAxis = -1);
    /** @brief factors为方向空间三轴倍率；世界缩放按轴长度换算，局部直接修改scale。 */
    [[nodiscard]] static std::optional<core::Transform> scale(const core::Transform& before,
                                                              const glm::mat4& parentWorld,
                                                              const glm::mat3& worldBasis,
                                                              const glm::vec3& factors, bool local);
};
} // namespace mini3d::editor
