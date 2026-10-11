/*
 * 模块名: AnimationProperty
 * 功能概述: 定义登记属性地址、最小 Action/Slot/标量曲线和精确子帧取样。
 * 对外接口: PropertyAnimation、属性/曲线/绑定校验与纯取样函数。
 * 依赖关系: SceneIdentity、EntityId、C++ 标准库。
 * 输入输出: typed 作者定义到完整解析取样或定位失败。
 * 异常与错误: 未支持插值、错误 owner/分量/引用整次拒绝；分配异常传播。
 * 维护说明: 不提供字符串反射、NLA、Driver 或 Socket；V4 通道保持独立。
 */
#pragma once

#include "EntityId.h"
#include "SceneIdentity.h"

#include <span>

namespace mini3d::core {
inline constexpr std::size_t kAnimationMaximumScalarCurves = 9000;
inline constexpr std::size_t kAnimationMaximumScalarKeys = 300000;
enum class PropertyId { Position, RotationEulerXYZDegrees, Scale, PbrRoughness, SunIrradiance };
enum class PropertyComponent { X, Y, Z, Scalar };
enum class PropertyOwnerKind { Entity, Material };
struct EntityOwner {
    EntityId entity = kInvalidEntity;
    auto operator<=>(const EntityOwner&) const = default;
};
struct MaterialOwner {
    MaterialId material;
    auto operator<=>(const MaterialOwner&) const = default;
};
using OwnerRef = std::variant<EntityOwner, MaterialOwner>;
struct PropertyAddress {
    DocumentUuid documentUuid;
    OwnerRef owner;
    PropertyId property = PropertyId::Position;
    PropertyComponent component = PropertyComponent::X;
    auto operator<=>(const PropertyAddress&) const = default;
};
enum class PropertyInterpolation { Constant, Linear, Bezier };
struct ScalarKey {
    KeyId id;
    FrameTime time = 1;
    double value = 0;
    PropertyInterpolation outgoing = PropertyInterpolation::Linear;
    bool operator==(const ScalarKey&) const = default;
};
struct ScalarCurve {
    CurveId id;
    PropertyId property = PropertyId::Position;
    PropertyComponent component = PropertyComponent::X;
    std::vector<ScalarKey> keys;
    bool operator==(const ScalarCurve&) const = default;
};
struct ActionSlotDefinition {
    ActionSlotId id;
    PropertyOwnerKind ownerKind = PropertyOwnerKind::Entity;
    std::vector<ScalarCurve> curves;
    bool operator==(const ActionSlotDefinition&) const = default;
};
struct ActionDefinition {
    ActionId id;
    std::vector<ActionSlotDefinition> actionSlots;
    bool operator==(const ActionDefinition&) const = default;
};
struct OwnerBinding {
    DocumentUuid documentUuid;
    OwnerRef owner;
    ActionId action;
    ActionSlotId slot;
    bool operator==(const OwnerBinding&) const = default;
};
/** @brief 未指定有理 fps 时映射旧 settings.fps/1；设置变化不移动 keys。 */
struct PropertyAnimation {
    std::optional<RationalFps> rationalFps;
    AnimationLoopPolicy loopPolicy = AnimationLoopPolicy::InclusiveFrameStep;
    std::vector<ActionDefinition> actions;
    std::vector<OwnerBinding> bindings;
    [[nodiscard]] bool hasAuthorData() const {
        return rationalFps.has_value() || !actions.empty() || !bindings.empty();
    }
    bool operator==(const PropertyAnimation&) const = default;
};
struct PropertyOwnerCapabilities {
    OwnerRef owner;
    bool hasSun = false;
};
enum class PropertyError {
    None, InvalidDocument, InvalidTime, InvalidFps, InvalidId, DuplicateId,
    UnknownProperty, InvalidComponent, InvalidOwner, MissingOwner, MissingAction,
    MissingSlot, DuplicateProperty, EmptyCurve, KeysNotOrdered, InvalidValue,
    ScaleCrossesZero, UnsupportedInterpolation, CurveLimitExceeded, KeyLimitExceeded
};
struct PropertyValidation {
    PropertyError error = PropertyError::None;
    std::optional<PropertyAddress> address;
    CurveId curve;
    KeyId key;
    [[nodiscard]] bool isValid() const { return error == PropertyError::None; }
};
/** @brief 登记地址严格校验；不解析任意属性或 Socket。 */
[[nodiscard]] std::optional<PropertyId> parsePropertyId(std::string_view name);
[[nodiscard]] bool isPropertyComponentValid(PropertyId property, PropertyComponent component);
[[nodiscard]] PropertyValidation validateScalarCurve(const ScalarCurve& curve);
[[nodiscard]] std::optional<double> sampleScalarCurve(const ScalarCurve& curve, FrameTime time);
/** @brief 先计量曲线/键，再校验完整定义及解析后的唯一目标；legacy 为 XYZ 展开计数。 */
[[nodiscard]] PropertyValidation validatePropertyAnimation(
    const PropertyAnimation& animation, const DocumentUuid& document,
    std::span<const PropertyOwnerCapabilities> owners,
    std::size_t legacyCurves = 0, std::size_t legacyKeys = 0);
struct SampledProperty {
    PropertyAddress address;
    CurveId curve;
    double value = 0;
};
/** @brief unchecked 快入口：整份定义与有限 [1,100000] time 均须已校验；不分配稳定 ID。 */
[[nodiscard]] std::vector<SampledProperty> sampleValidatedPropertyAnimation(
    const PropertyAnimation& animation, FrameTime time);
} // namespace mini3d::core
