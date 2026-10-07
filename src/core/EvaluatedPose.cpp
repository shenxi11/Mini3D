/*
 * 模块名: EvaluatedPose
 * 功能概述: 迭代计算整份数值姿态并拒绝不可用的实际 float 消费结果。
 * 对外接口: EvaluatedPose::find、evaluateAnimationPose。
 * 依赖关系: Animation、Transform、GLM 和标准容器。
 * 输入输出: 父先子后基础输入与动画到完整 Pose 或带实体上下文的失败。
 * 异常与错误: 任意失败不返回部分 Pose，源输入保持不变；分配异常传播。
 * 维护说明: 每层规范化刚性 quaternion；不从反射/剪切 world 反提设备方向。
 */
#include "EvaluatedPose.h"

#include <cmath>
#include <glm/matrix.hpp>

namespace mini3d::core {
namespace {
bool finiteMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

bool finiteMatrix(const glm::mat3& matrix) {
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

bool canNormalizeDirection(const glm::vec3& direction) {
    const float length = glm::length(direction);
    if (!std::isfinite(length) || length == 0.0F) {
        return false;
    }
    const auto normalized = glm::normalize(direction);
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(normalized[axis])) {
            return false;
        }
    }
    return glm::length(normalized) > 0.0F;
}

std::optional<glm::quat> unitQuaternion(const glm::quat& rotation) {
    for (int component = 0; component < 4; ++component) {
        if (!std::isfinite(rotation[component])) {
            return std::nullopt;
        }
    }
    const float length = glm::length(rotation);
    if (!std::isfinite(length) || length == 0.0F) {
        return std::nullopt;
    }
    return rotation / length;
}

AnimationPoseResult failedPose(AnimationError error, EntityId entity = kInvalidEntity) {
    return {std::nullopt, {error, entity}};
}
} // namespace

const EvaluatedPoseNode* EvaluatedPose::find(EntityId entity) const {
    const auto found = indices.find(entity);
    return found == indices.end() ? nullptr : &nodes[found->second];
}

AnimationPoseResult evaluateAnimationPose(std::span<const AnimationPoseInput> inputs,
                                          const SceneAnimation& animation, FrameTime frame) {
    if (inputs.size() > kAnimationMaximumPoseNodes) {
        return failedPose(AnimationError::PoseNodeLimitExceeded);
    }
    if (!isAnimationFrameValid(frame)) {
        return failedPose(AnimationError::InvalidFrame);
    }
    EvaluatedPose candidate;
    candidate.frame = frame;
    candidate.indices.reserve(inputs.size());
    std::vector<EntityId> entities;
    entities.reserve(inputs.size());
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto& input = inputs[index];
        if (input.entity == kInvalidEntity) {
            return failedPose(AnimationError::InvalidEntity);
        }
        if (candidate.indices.contains(input.entity)) {
            return failedPose(AnimationError::DuplicateEntity, input.entity);
        }
        if (input.parent != kInvalidEntity && !candidate.indices.contains(input.parent)) {
            return failedPose(AnimationError::InvalidParentOrder, input.entity);
        }
        if (!input.base.isValid()) {
            return failedPose(AnimationError::InvalidBaseTransform, input.entity);
        }
        candidate.indices.emplace(input.entity, index);
        entities.push_back(input.entity);
    }
    const auto definition = validateSceneAnimation(animation, entities);
    if (!definition.isValid()) {
        return {std::nullopt, definition};
    }
    candidate.nodes.reserve(inputs.size());
    for (const auto& input : inputs) {
        EvaluatedPoseNode node;
        node.entity = input.entity;
        node.local = input.base;
        for (const auto channel : {AnimationChannel::Position,
                                   AnimationChannel::RotationEulerXYZDegrees,
                                   AnimationChannel::Scale}) {
            const auto track = animation.tracks.find({input.entity, channel});
            if (track == animation.tracks.end()) {
                continue;
            }
            const auto value = detail::sampleValidatedAnimationTrack(track->second, frame);
            auto validation = validateAnimationValue(channel, value);
            if (!validation.isValid()) {
                validation.entity = input.entity;
                return {std::nullopt, validation};
            }
            if (channel == AnimationChannel::Position) {
                node.local.position = glm::vec3(value);
            } else if (channel == AnimationChannel::Scale) {
                node.local.scale = glm::vec3(value);
            } else {
                const auto rotation = quaternionFromEulerXYZDegrees(value);
                if (!rotation) {
                    return failedPose(AnimationError::InvalidLocalTransform, input.entity);
                }
                node.local.rotation = glm::quat(*rotation);
                node.rotationSource = AnimationRotationSource::AnimationTrack;
                node.rotationEulerXYZDegrees = value;
            }
        }
        if (!node.local.isValid()) {
            return failedPose(AnimationError::InvalidLocalTransform, input.entity);
        }
        const auto local = node.local.localMatrix();
        if (!finiteMatrix(local)) {
            return failedPose(AnimationError::InvalidLocalTransform, input.entity);
        }
        const auto localRotation = unitQuaternion(node.local.rotation);
        if (!localRotation) {
            return failedPose(AnimationError::InvalidRigidRotation, input.entity);
        }
        const EvaluatedPoseNode* parent = input.parent == kInvalidEntity
                                              ? nullptr
                                              : candidate.find(input.parent);
        node.world = parent ? parent->world * local : local;
        if (!finiteMatrix(node.world)) {
            return failedPose(AnimationError::InvalidWorldTransform, input.entity);
        }
        node.worldInverse = glm::inverse(node.world);
        if (!finiteMatrix(node.worldInverse)) {
            return failedPose(AnimationError::InvalidWorldInverse, input.entity);
        }
        node.worldNormal = glm::transpose(glm::inverse(glm::mat3(node.world)));
        if (!finiteMatrix(node.worldNormal)) {
            return failedPose(AnimationError::InvalidWorldNormal, input.entity);
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (!canNormalizeDirection(node.worldNormal[axis])) {
                return failedPose(AnimationError::InvalidWorldNormal, input.entity);
            }
        }
        const auto rigid = unitQuaternion(parent ? parent->rigidWorldRotation * *localRotation
                                                  : *localRotation);
        if (!rigid || !canNormalizeDirection(*rigid * glm::vec3(0, 0, -1)) ||
            !canNormalizeDirection(*rigid * glm::vec3(0, 1, 0))) {
            return failedPose(AnimationError::InvalidRigidRotation, input.entity);
        }
        node.rigidWorldRotation = *rigid;
        candidate.nodes.push_back(std::move(node));
    }
    return {std::move(candidate), {}};
}
} // namespace mini3d::core
