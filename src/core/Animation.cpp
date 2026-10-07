/*
 * 模块名: Animation
 * 功能概述: 实现最终动画定义校验、double 取样与稳定 Euler 转换。
 * 对外接口: Animation.h 声明的纯函数。
 * 依赖关系: C++ 标准库、GLM；不读取或改写 Scene。
 * 输入输出: 动画原值到严格校验、确定性取样或规范旋转。
 * 异常与错误: 非法输入返回错误或空值；分配异常传播。
 * 维护说明: Linear 使用 std::lerp，不通过放宽缩放原值门槛掩盖舍入。
 */
#include "Animation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_set>

namespace mini3d::core {
namespace {
bool validChannel(AnimationChannel channel) {
    return channel == AnimationChannel::Position ||
           channel == AnimationChannel::RotationEulerXYZDegrees || channel == AnimationChannel::Scale;
}

AnimationValidation definitionBudget(const SceneAnimation& animation) {
    if (animation.tracks.size() > kAnimationMaximumTracks) {
        return {AnimationError::TrackLimitExceeded};
    }
    std::size_t keyCount = 0;
    for (const auto& [id, track] : animation.tracks) {
        if (track.keys.size() > kAnimationMaximumTrackKeys) {
            return {AnimationError::TrackKeyLimitExceeded, id.first, id.second};
        }
        if (track.keys.size() > kAnimationMaximumKeys - keyCount) {
            return {AnimationError::TotalKeyLimitExceeded, id.first, id.second};
        }
        keyCount += track.keys.size();
    }
    return {};
}

std::optional<glm::dquat> normalizedQuaternion(const glm::dquat& rotation) {
    for (int component = 0; component < 4; ++component) {
        if (!std::isfinite(rotation[component])) {
            return std::nullopt;
        }
    }
    const double length = glm::length(rotation);
    if (!std::isfinite(length) || length == 0.0) {
        return std::nullopt;
    }
    return rotation / length;
}

double canonicalDegrees(double radians) {
    const double degrees = radians * (180.0 / std::numbers::pi);
    return degrees <= -180.0 ? 180.0 : degrees;
}
} // namespace

bool isAnimationFrameValid(FrameTime frame) {
    return std::isfinite(frame) && frame >= 1.0 && frame <= kAnimationMaximumFrame;
}

AnimationValidation validateAnimationValue(AnimationChannel channel, const glm::dvec3& value) {
    if (!validChannel(channel)) {
        return {AnimationError::InvalidChannel, kInvalidEntity, channel};
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(value[axis])) {
            return {AnimationError::InvalidValue, kInvalidEntity, channel};
        }
        if (channel == AnimationChannel::RotationEulerXYZDegrees) {
            if (std::abs(value[axis]) > kAnimationMaximumRotationDegrees) {
                return {AnimationError::InvalidValue, kInvalidEntity, channel};
            }
            continue;
        }
        // 原值须在 float 可表示域内；不能让越界值借舍入变成合法边界。
        if (std::abs(value[axis]) > static_cast<double>(std::numeric_limits<float>::max())) {
            return {AnimationError::InvalidValue, kInvalidEntity, channel};
        }
        if (channel == AnimationChannel::Scale &&
            std::abs(value[axis]) < kAnimationMinimumScale) {
            return {AnimationError::InvalidScale, kInvalidEntity, channel};
        }
        const float actual = static_cast<float>(value[axis]);
        if (!std::isfinite(actual)) {
            return {AnimationError::InvalidValue, kInvalidEntity, channel};
        }
        if (channel == AnimationChannel::Scale &&
            std::abs(actual) < static_cast<float>(kAnimationMinimumScale)) {
            return {AnimationError::InvalidScale, kInvalidEntity, channel};
        }
    }
    return {};
}

AnimationValidation validateAnimationTrack(AnimationChannel channel, const AnimationTrack& track) {
    if (!validChannel(channel)) {
        return {AnimationError::InvalidChannel, kInvalidEntity, channel};
    }
    if (track.keys.size() > kAnimationMaximumTrackKeys) {
        return {AnimationError::TrackKeyLimitExceeded, kInvalidEntity, channel};
    }
    if (track.keys.empty()) {
        return {AnimationError::EmptyTrack, kInvalidEntity, channel};
    }
    for (std::size_t index = 0; index < track.keys.size(); ++index) {
        const auto& key = track.keys[index];
        AnimationValidation validation;
        if (!isAnimationFrameValid(key.frame)) {
            validation.error = AnimationError::InvalidFrame;
        } else if (key.interpolation != AnimationInterpolation::Constant &&
                   key.interpolation != AnimationInterpolation::Linear) {
            validation.error = AnimationError::InvalidInterpolation;
        } else if (index != 0 && track.keys[index - 1].frame >= key.frame) {
            validation.error = AnimationError::KeysNotOrdered;
        } else {
            validation = validateAnimationValue(channel, key.value);
        }
        if (!validation.isValid()) {
            validation.channel = channel;
            validation.frame = key.frame;
            return validation;
        }
        if (channel == AnimationChannel::Scale && index != 0 &&
            track.keys[index - 1].interpolation == AnimationInterpolation::Linear) {
            const auto& previous = track.keys[index - 1];
            for (int axis = 0; axis < 3; ++axis) {
                if (std::signbit(previous.value[axis]) != std::signbit(key.value[axis])) {
                    return {AnimationError::ScaleCrossesZero, kInvalidEntity, channel, key.frame};
                }
            }
        }
    }
    return {};
}

AnimationValidation validateSceneAnimation(const SceneAnimation& animation,
                                            std::span<const EntityId> entities) {
    const auto budget = definitionBudget(animation);
    if (!budget.isValid()) {
        return budget;
    }
    const auto& settings = animation.settings;
    if (settings.fps < 1 || settings.fps > 120 || !isAnimationFrameValid(settings.startFrame) ||
        !isAnimationFrameValid(settings.endFrame) || settings.startFrame > settings.endFrame) {
        return {AnimationError::InvalidSettings};
    }
    std::unordered_set<EntityId> available;
    available.reserve(entities.size());
    for (const EntityId entity : entities) {
        if (entity == kInvalidEntity) {
            return {AnimationError::InvalidEntity};
        }
        if (!available.insert(entity).second) {
            return {AnimationError::DuplicateEntity, entity};
        }
    }
    for (const auto& [id, track] : animation.tracks) {
        if (id.first == kInvalidEntity) {
            return {AnimationError::InvalidEntity, id.first, id.second};
        }
        if (!available.contains(id.first)) {
            return {AnimationError::MissingEntity, id.first, id.second};
        }
        auto validation = validateAnimationTrack(id.second, track);
        if (!validation.isValid()) {
            validation.entity = id.first;
            return validation;
        }
    }
    return {};
}

AnimationValidation validateAnimationPreflightBudget(std::size_t nodeCount,
                                                     std::size_t uniqueFrameCount) {
    if (nodeCount > kAnimationMaximumPoseNodes) {
        return {AnimationError::PoseNodeLimitExceeded};
    }
    if (nodeCount != 0 && uniqueFrameCount > kAnimationMaximumPreflightNodeVisits / nodeCount) {
        return {AnimationError::PreflightLimitExceeded};
    }
    return {};
}

namespace detail {
glm::dvec3 sampleValidatedAnimationTrack(const AnimationTrack& track, FrameTime frame) {
    const auto next = std::lower_bound(
        track.keys.begin(), track.keys.end(), frame,
        [](const AnimationKeyframe& key, FrameTime time) { return key.frame < time; });
    if (next == track.keys.begin()) {
        return next->value;
    }
    if (next == track.keys.end()) {
        return track.keys.back().value;
    }
    if (static_cast<FrameTime>(next->frame) == frame) {
        return next->value;
    }
    const auto& previous = *(next - 1);
    if (previous.interpolation == AnimationInterpolation::Constant) {
        return previous.value;
    }
    const double factor = (frame - previous.frame) / (next->frame - previous.frame);
    glm::dvec3 result;
    for (int axis = 0; axis < 3; ++axis) {
        result[axis] = std::lerp(previous.value[axis], next->value[axis], factor);
    }
    return result;
}
} // namespace detail

std::optional<glm::dvec3> sampleAnimationTrack(AnimationChannel channel, const AnimationTrack& track,
                                              FrameTime frame) {
    if (!isAnimationFrameValid(frame) || !validateAnimationTrack(channel, track).isValid()) {
        return std::nullopt;
    }
    const auto value = detail::sampleValidatedAnimationTrack(track, frame);
    return validateAnimationValue(channel, value).isValid() ? std::optional(value) : std::nullopt;
}

std::optional<glm::dquat> quaternionFromEulerXYZDegrees(const glm::dvec3& degrees) {
    if (!validateAnimationValue(AnimationChannel::RotationEulerXYZDegrees, degrees).isValid()) {
        return std::nullopt;
    }
    glm::dvec3 radians;
    for (int axis = 0; axis < 3; ++axis) {
        radians[axis] = std::remainder(degrees[axis], 360.0) * (std::numbers::pi / 180.0);
    }
    const auto x = glm::angleAxis(radians.x, glm::dvec3(1, 0, 0));
    const auto y = glm::angleAxis(radians.y, glm::dvec3(0, 1, 0));
    const auto z = glm::angleAxis(radians.z, glm::dvec3(0, 0, 1));
    return normalizedQuaternion(z * y * x);
}

std::optional<glm::dvec3> canonicalEulerXYZDegrees(const glm::dquat& rotation) {
    const auto normalized = normalizedQuaternion(rotation);
    if (!normalized) {
        return std::nullopt;
    }
    const auto matrix = glm::mat3_cast(*normalized);
    const double cosineY = std::hypot(matrix[0][0], matrix[0][1]);
    const double y = std::atan2(-matrix[0][2], cosineY);
    const double x = cosineY <= 1.0e-8 ? 0.0 : std::atan2(matrix[1][2], matrix[2][2]);
    const double z = cosineY <= 1.0e-8 ? std::atan2(-matrix[1][0], matrix[1][1])
                                    : std::atan2(matrix[0][1], matrix[0][0]);
    const glm::dvec3 result(canonicalDegrees(x), y * (180.0 / std::numbers::pi), canonicalDegrees(z));
    const auto reconstructed = quaternionFromEulerXYZDegrees(result);
    if (!reconstructed) {
        return std::nullopt;
    }
    double sameError = 0.0;
    double oppositeError = 0.0;
    for (int component = 0; component < 4; ++component) {
        sameError = std::max(sameError, std::abs((*reconstructed)[component] - (*normalized)[component]));
        oppositeError =
            std::max(oppositeError, std::abs((*reconstructed)[component] + (*normalized)[component]));
    }
    return std::min(sameError, oppositeError) <= 2.0e-8 ? std::optional(result) : std::nullopt;
}
} // namespace mini3d::core
