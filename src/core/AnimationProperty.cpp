/*
 * 模块名: AnimationProperty
 * 功能概述: 校验登记域、稳定 ID、最终缩放邻接并按 double 原值取样。
 * 对外接口: AnimationProperty.h 声明的纯函数。
 * 依赖关系: Animation 既有数值域、标准容器/数学函数。
 * 输入输出: 完整 typed 作者定义到精确采样和结构化失败。
 * 异常与错误: 任意错误拒绝候选；不改作者值、不把 Bezier 当 Linear。
 * 维护说明: 精确端点返回存储值；不按 epsilon 合并时间。
 */
#include "AnimationProperty.h"

#include "Animation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace mini3d::core {
namespace {
bool isKnownProperty(PropertyId property) {
    return property == PropertyId::Position || property == PropertyId::RotationEulerXYZDegrees ||
           property == PropertyId::Scale || property == PropertyId::PbrRoughness ||
           property == PropertyId::SunIrradiance;
}
bool propertyMatchesOwner(PropertyId property, PropertyOwnerKind owner) {
    return owner == PropertyOwnerKind::Material ? property == PropertyId::PbrRoughness
        : owner == PropertyOwnerKind::Entity && property != PropertyId::PbrRoughness;
}
bool validScalarValue(PropertyId property, double value) {
    if (!std::isfinite(value)) return false;
    if (property == PropertyId::PbrRoughness) return value >= 0 && value <= 1;
    if (property == PropertyId::SunIrradiance)
        return value >= 0 && value <= std::numeric_limits<float>::max() &&
               std::isfinite(static_cast<float>(value));
    const auto channel = property == PropertyId::Position ? AnimationChannel::Position
        : property == PropertyId::Scale ? AnimationChannel::Scale
        : AnimationChannel::RotationEulerXYZDegrees;
    return validateAnimationValue(channel, glm::dvec3(value)).isValid();
}
double sampleValidatedCurve(const ScalarCurve& curve, FrameTime time) {
    const auto right = std::lower_bound(curve.keys.begin(), curve.keys.end(), time,
        [](const ScalarKey& key, FrameTime frame) { return key.time < frame; });
    if (right == curve.keys.begin()) return right->value;
    if (right == curve.keys.end()) return curve.keys.back().value;
    if (right->time == time) return right->value;
    const auto& left = *(right - 1);
    if (left.outgoing == PropertyInterpolation::Constant) return left.value;
    return std::lerp(left.value, right->value, (time - left.time) / (right->time - left.time));
}
} // namespace
std::optional<PropertyId> parsePropertyId(std::string_view name) {
    if (name == "transform.position") return PropertyId::Position;
    if (name == "transform.rotation_euler_xyz_degrees") return PropertyId::RotationEulerXYZDegrees;
    if (name == "transform.scale") return PropertyId::Scale;
    if (name == "material.pbr.roughness") return PropertyId::PbrRoughness;
    if (name == "light.sun.irradiance") return PropertyId::SunIrradiance;
    return std::nullopt;
}
bool isPropertyComponentValid(PropertyId property, PropertyComponent component) {
    if (!isKnownProperty(property)) return false;
    if (property == PropertyId::PbrRoughness || property == PropertyId::SunIrradiance)
        return component == PropertyComponent::Scalar;
    return component == PropertyComponent::X || component == PropertyComponent::Y ||
           component == PropertyComponent::Z;
}
PropertyValidation validateScalarCurve(const ScalarCurve& curve) {
    const auto fail = [&](PropertyError error, KeyId key = KeyId{}) {
        return PropertyValidation{error, std::nullopt, curve.id, key};
    };
    if (!curve.id.isValid()) return fail(PropertyError::InvalidId);
    if (!isKnownProperty(curve.property)) return fail(PropertyError::UnknownProperty);
    if (!isPropertyComponentValid(curve.property, curve.component)) return fail(PropertyError::InvalidComponent);
    if (curve.keys.size() > kAnimationMaximumScalarKeys) return fail(PropertyError::KeyLimitExceeded);
    if (curve.keys.empty()) return fail(PropertyError::EmptyCurve);
    std::set<KeyId> ids;
    for (std::size_t index = 0; index < curve.keys.size(); ++index) {
        const auto& key = curve.keys[index];
        if (!key.id.isValid()) return fail(PropertyError::InvalidId, key.id);
        if (!ids.insert(key.id).second) return fail(PropertyError::DuplicateId, key.id);
        if (!isAnimationFrameValid(key.time)) return fail(PropertyError::InvalidTime, key.id);
        if (index != 0 && curve.keys[index - 1].time >= key.time)
            return fail(PropertyError::KeysNotOrdered, key.id);
        if (key.outgoing != PropertyInterpolation::Constant && key.outgoing != PropertyInterpolation::Linear)
            return fail(PropertyError::UnsupportedInterpolation, key.id);
        if (!validScalarValue(curve.property, key.value)) return fail(PropertyError::InvalidValue, key.id);
        if (curve.property == PropertyId::Scale && index != 0 &&
            curve.keys[index - 1].outgoing == PropertyInterpolation::Linear &&
            std::signbit(curve.keys[index - 1].value) != std::signbit(key.value))
            return fail(PropertyError::ScaleCrossesZero, key.id);
    }
    return {};
}
std::optional<double> sampleScalarCurve(const ScalarCurve& curve, FrameTime time) {
    if (!isAnimationFrameValid(time) || !validateScalarCurve(curve).isValid()) return std::nullopt;
    const double value = sampleValidatedCurve(curve, time);
    return validScalarValue(curve.property, value) ? std::optional(value) : std::nullopt;
}
PropertyValidation validatePropertyAnimation(const PropertyAnimation& animation,
    const DocumentUuid& document, std::span<const PropertyOwnerCapabilities> owners,
    std::size_t legacyCurves, std::size_t legacyKeys) {
    if (document.isNil()) return {PropertyError::InvalidDocument};
    if (animation.rationalFps && !isRationalFpsValid(*animation.rationalFps)) return {PropertyError::InvalidFps};
    if (animation.loopPolicy != AnimationLoopPolicy::InclusiveFrameStep &&
        animation.loopPolicy != AnimationLoopPolicy::LegacyHalfOpen)
        return {PropertyError::InvalidTime};
    if (legacyCurves > kAnimationMaximumScalarCurves) return {PropertyError::CurveLimitExceeded};
    if (legacyKeys > kAnimationMaximumScalarKeys) return {PropertyError::KeyLimitExceeded};
    std::size_t curveCount = legacyCurves, keyCount = legacyKeys;
    for (const auto& action : animation.actions) for (const auto& slot : action.actionSlots) {
        if (slot.curves.size() > kAnimationMaximumScalarCurves - curveCount)
            return {PropertyError::CurveLimitExceeded};
        curveCount += slot.curves.size();
        for (const auto& curve : slot.curves) {
            if (curve.keys.size() > kAnimationMaximumScalarKeys - keyCount)
                return {PropertyError::KeyLimitExceeded};
            keyCount += curve.keys.size();
        }
    }
    std::map<OwnerRef, bool> available;
    for (const auto& owner : owners) {
        const bool valid = std::visit([](const auto& value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, EntityOwner>) return value.entity != 0;
            else return value.material.isValid();
        }, owner.owner);
        if (!valid || !available.emplace(owner.owner, owner.hasSun).second) return {PropertyError::InvalidOwner};
    }
    std::map<ActionId, const ActionDefinition*> actions;
    std::set<ActionSlotId> slots;
    std::set<CurveId> curves;
    std::set<KeyId> keys;
    for (const auto& action : animation.actions) {
        if (!action.id.isValid()) return {PropertyError::InvalidId};
        if (!actions.emplace(action.id, &action).second) return {PropertyError::DuplicateId};
        for (const auto& slot : action.actionSlots) {
            if (slot.ownerKind != PropertyOwnerKind::Entity && slot.ownerKind != PropertyOwnerKind::Material)
                return {PropertyError::InvalidOwner};
            if (!slot.id.isValid()) return {PropertyError::InvalidId};
            if (!slots.insert(slot.id).second) return {PropertyError::DuplicateId};
            std::set<std::pair<PropertyId, PropertyComponent>> addresses;
            for (const auto& curve : slot.curves) {
                if (!curves.insert(curve.id).second) return {PropertyError::DuplicateId, {}, curve.id};
                if (!propertyMatchesOwner(curve.property, slot.ownerKind)) return {PropertyError::InvalidOwner, {}, curve.id};
                if (!addresses.emplace(curve.property, curve.component).second)
                    return {PropertyError::DuplicateProperty, {}, curve.id};
                const auto checked = validateScalarCurve(curve);
                if (!checked.isValid()) return checked;
                for (const auto& key : curve.keys)
                    if (!keys.insert(key.id).second) return {PropertyError::DuplicateId, {}, curve.id, key.id};
            }
        }
    }
    std::set<PropertyAddress> targets;
    for (const auto& binding : animation.bindings) {
        if (binding.documentUuid != document) return {PropertyError::InvalidDocument};
        const auto owner = available.find(binding.owner);
        if (owner == available.end()) return {PropertyError::MissingOwner};
        const auto action = actions.find(binding.action);
        if (action == actions.end()) return {PropertyError::MissingAction};
        const auto slot = std::find_if(action->second->actionSlots.begin(), action->second->actionSlots.end(),
            [&](const auto& value) { return value.id == binding.slot; });
        if (slot == action->second->actionSlots.end()) return {PropertyError::MissingSlot};
        const auto kind = std::holds_alternative<EntityOwner>(binding.owner) ? PropertyOwnerKind::Entity : PropertyOwnerKind::Material;
        if (slot->ownerKind != kind) return {PropertyError::InvalidOwner};
        for (const auto& curve : slot->curves) {
            PropertyAddress address{document, binding.owner, curve.property, curve.component};
            if (curve.property == PropertyId::SunIrradiance && !owner->second)
                return {PropertyError::InvalidOwner, address, curve.id};
            if (!targets.insert(address).second) return {PropertyError::DuplicateProperty, address, curve.id};
        }
    }
    return {};
}
std::vector<SampledProperty> sampleValidatedPropertyAnimation(const PropertyAnimation& animation, FrameTime time) {
    std::vector<SampledProperty> result;
    for (const auto& binding : animation.bindings) {
        const auto action = std::find_if(animation.actions.begin(), animation.actions.end(),
            [&](const auto& value) { return value.id == binding.action; });
        const auto slot = std::find_if(action->actionSlots.begin(), action->actionSlots.end(),
            [&](const auto& value) { return value.id == binding.slot; });
        for (const auto& curve : slot->curves)
            result.push_back({{binding.documentUuid, binding.owner, curve.property, curve.component},
                              curve.id, sampleValidatedCurve(curve, time)});
    }
    return result;
}
} // namespace mini3d::core
