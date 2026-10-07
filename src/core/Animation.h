/*
 * 模块名: Animation
 * 功能概述: 定义原生对象动画的完整 XYZ 关键帧、严格校验和确定性取样。
 * 对外接口: SceneAnimation、校验/取样函数、连续 Euler 与 quaternion 转换。
 * 依赖关系: EntityId、C++ 标准库、GLM；不依赖 Scene、Qt 或 OpenGL。
 * 输入输出: 只读动画定义和稳定实体 ID 到校验结果或双精度取样值。
 * 异常与错误: 非法候选返回结构化失败；分配异常传播，不改动输入。
 * 维护说明: 原始角度不折叠；预算是 R1 施工候选，后续与 API 单点限额核对。
 */
#pragma once

#include "EntityId.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace mini3d::core {
using FrameTime = double;
inline constexpr std::uint32_t kAnimationMaximumFrame = 100000;
inline constexpr std::size_t kAnimationMaximumTracks = 3000;
inline constexpr std::size_t kAnimationMaximumKeys = 100000;
inline constexpr std::size_t kAnimationMaximumTrackKeys = 10000;
inline constexpr std::size_t kAnimationMaximumPoseNodes = 10000;
inline constexpr std::size_t kAnimationMaximumPreflightNodeVisits = 40000;
inline constexpr double kAnimationMinimumScale = 0.001;
inline constexpr double kAnimationMaximumRotationDegrees = 3.6e9;

/** @brief 完整 XYZ 通道；Rotation 的原值为连续度数，不能按最短弧解释。 */
enum class AnimationChannel { Position, RotationEulerXYZDegrees, Scale };
/** @brief 插值属于左键到下一键；Constant 到下一键精确命中时才切换。 */
enum class AnimationInterpolation { Constant, Linear };

/** @brief 播放设置；范围不限制合法取样时间，也不会隐式删除或移动关键帧。 */
struct AnimationSettings {
    std::uint32_t fps = 24;
    std::uint32_t startFrame = 1;
    std::uint32_t endFrame = 250;
    bool operator==(const AnimationSettings&) const = default;
};

/** @brief 持久关键帧；完整 double 原值和插值参与精确相等判断。 */
struct AnimationKeyframe {
    std::uint32_t frame = 1;
    glm::dvec3 value{0.0};
    AnimationInterpolation interpolation = AnimationInterpolation::Linear;
    bool operator==(const AnimationKeyframe&) const = default;
};

/** @brief 非空且时间严格递增的规范轨道；最后一个键删除后由编辑层移除轨道。 */
struct AnimationTrack {
    std::vector<AnimationKeyframe> keys;
    bool operator==(const AnimationTrack&) const = default;
};

using AnimationTrackId = std::pair<EntityId, AnimationChannel>;
/** @brief 以稳定 ID/通道排序的正式定义；不持有运行期姿态、资源或文档身份。 */
struct SceneAnimation {
    AnimationSettings settings;
    std::map<AnimationTrackId, AnimationTrack> tracks;
    bool operator==(const SceneAnimation&) const = default;
};

/** @brief 数值或预算失败分类；涉及键或实体时附带可定位的上下文。 */
enum class AnimationError {
    None,
    InvalidSettings,
    InvalidFrame,
    InvalidChannel,
    InvalidInterpolation,
    InvalidValue,
    InvalidScale,
    ScaleCrossesZero,
    EmptyTrack,
    KeysNotOrdered,
    InvalidEntity,
    DuplicateEntity,
    MissingEntity,
    InvalidParentOrder,
    TrackLimitExceeded,
    TotalKeyLimitExceeded,
    TrackKeyLimitExceeded,
    PoseNodeLimitExceeded,
    PreflightLimitExceeded,
    InvalidBaseTransform,
    InvalidLocalTransform,
    InvalidWorldTransform,
    InvalidWorldInverse,
    InvalidWorldNormal,
    InvalidRigidRotation
};

/** @brief error 为 None 表示通过；失败不返回或安装任何部分候选。 */
struct AnimationValidation {
    AnimationError error = AnimationError::None;
    EntityId entity = kInvalidEntity;
    std::optional<AnimationChannel> channel;
    std::uint32_t frame = 0;
    [[nodiscard]] bool isValid() const {
        return error == AnimationError::None;
    }
};

/** @brief 仅接受有限的 1..100000 帧时间，允许子帧；不受播放范围限制。 */
[[nodiscard]] bool isAnimationFrameValid(FrameTime frame);
/** @brief 先检查 double 原值，再检查 Position/Scale 的实际 float 转换；不做修正。 */
[[nodiscard]] AnimationValidation validateAnimationValue(AnimationChannel channel,
                                                         const glm::dvec3& value);
/** @brief 检查完整最终轨道及全部 Scale 新邻接；不只检查被修改的单个键。 */
[[nodiscard]] AnimationValidation validateAnimationTrack(AnimationChannel channel,
                                                         const AnimationTrack& track);
/** @brief 在扫描键值前核算规模，再检查设置、绑定及全部最终轨道；空轨也先检查绑定。 */
[[nodiscard]] AnimationValidation validateSceneAnimation(const SceneAnimation& animation,
                                                         std::span<const EntityId> entities);
/** @brief 校验计数 N×|S|，除法避免乘法溢出；调用层负责时间合法性、去重和包含当前时间。 */
[[nodiscard]] AnimationValidation validateAnimationPreflightBudget(std::size_t nodeCount,
                                                                  std::size_t uniqueFrameCount);
/** @brief checked 入口，先验证完整轨道和时间；非法输入返回空，不改动轨道。 */
[[nodiscard]] std::optional<glm::dvec3> sampleAnimationTrack(AnimationChannel channel,
                                                           const AnimationTrack& track,
                                                           FrameTime frame);
/** @brief 原值合法后仅为三角运算约减，返回 double 单位 qz×qy×qx，不修改连续角。 */
[[nodiscard]] std::optional<glm::dquat>
quaternionFromEulerXYZDegrees(const glm::dvec3& degrees);
/**
 * @brief 首次旋转录键的规范逆解，不声称恢复历史转数。
 * 使用稳定 y=atan2(-r20,hypot(r00,r10))，等价于正交矩阵的 asin 解；奇异阈值 1e-8。
 * x/z 属于 (-180,180]、y 属于 [-90,90]；double 域 q/-q 重构误差超过 2e-8 返回空。
 */
[[nodiscard]] std::optional<glm::dvec3> canonicalEulerXYZDegrees(const glm::dquat& rotation);

namespace detail {
/** @brief 内部已验证入口：track 必须非空/严格有序/数值合法，frame 必须合法。
 * 只在 checked 入口或完整定义验证之后调用，不重复扫描关键帧；精确键/端值直接返回原值。
 */
[[nodiscard]] glm::dvec3 sampleValidatedAnimationTrack(const AnimationTrack& track, FrameTime frame);
} // namespace detail
} // namespace mini3d::core
