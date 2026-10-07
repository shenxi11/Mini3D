/*
 * 模块名: EvaluatedPose
 * 功能概述: 以父先子后的最小数值输入求值完整场景姿态，保留刚性方向来源。
 * 对外接口: AnimationPoseInput、EvaluatedPose、evaluateAnimationPose。
 * 依赖关系: Animation、Transform、C++ 标准库、GLM；无 SceneNode/Qt/GL 依赖。
 * 输入输出: 基础 TRS、层级与动画到完整 local/world/inverse/normal/rigid 数值结果。
 * 异常与错误: 任何节点失败均拒绝整次结果；分配异常传播，不改基础数据。
 * 维护说明: 不递归、不重走每个节点的父链；几何八角/设备投影/图像身份归 Editor 校验。
 */
#pragma once

#include "Animation.h"
#include "Transform.h"

#include <unordered_map>

namespace mini3d::core {
/** @brief 输入必须覆盖整份待求值场景，ID 唯一且父节点先于子节点；0 表示无父。 */
struct AnimationPoseInput {
    EntityId entity = kInvalidEntity;
    EntityId parent = kInvalidEntity;
    Transform base;
};

/** @brief 无 Rotation 轨使用基础 quaternion；只有动画轨来源才有连续 Euler 原值。 */
enum class AnimationRotationSource { BaseQuaternion, AnimationTrack };

/** @brief 一个实体的最终数值姿态；刚性 rotation 独立于负缩放和剪切矩阵。 */
struct EvaluatedPoseNode {
    EntityId entity = kInvalidEntity;
    Transform local;
    glm::mat4 world{1.0F};
    glm::mat4 worldInverse{1.0F};
    glm::mat3 worldNormal{1.0F};
    glm::quat rigidWorldRotation{1.0F, 0.0F, 0.0F, 0.0F};
    AnimationRotationSource rotationSource = AnimationRotationSource::BaseQuaternion;
    std::optional<glm::dvec3> rotationEulerXYZDegrees;
};

/** @brief 不含文档身份的完整数值结果；nodes 保持父先子后，indices 按 EntityId 访问。 */
struct EvaluatedPose {
    FrameTime frame = 1.0;
    std::vector<EvaluatedPoseNode> nodes;
    std::unordered_map<EntityId, std::size_t> indices;
    /** @brief 成功结果内查找实体；不存在返回 nullptr，指针受此结果生命周期约束。 */
    [[nodiscard]] const EvaluatedPoseNode* find(EntityId entity) const;
};

/** @brief 成功只返回完整 pose；失败只返回 validation，不泄露部分父链结果。 */
struct AnimationPoseResult {
    std::optional<EvaluatedPose> pose;
    AnimationValidation validation;
};

/**
 * @brief checked 完整求值，节点预算先于任何 Pose 缓冲分配，定义与层级统一验证。
 * 实际 float local/world/inverse/normal 必须有限，normal 的三列与 rigid 设备方向可规范化。
 * 此门禁不保证任意几何法线/射线/八角或投影可用；Editor 仍须验证实际消费者。
 * 不修改 inputs/animation，不复制 SceneNode，不调用 Scene::worldMatrix。
 */
[[nodiscard]] AnimationPoseResult evaluateAnimationPose(std::span<const AnimationPoseInput> inputs,
                                                        const SceneAnimation& animation,
                                                        FrameTime frame);
} // namespace mini3d::core
