/*
 * 模块名: AnimationEditing
 * 功能概述: 保留稳定身份准备登记属性的单笔候选，并为正式预检收集差量时间。
 * 对外接口: AnimationEditing.h 声明的命令服务与纯时间收集器。
 * 依赖关系: SceneViewModel、Core prepareAnimation、Qt 同步失败通知。
 * 输入输出: 属性地址到不可变 before/after；正式发布与历史由 ViewModel 唯一拥有。
 * 异常与错误: 准备/提交失败保留原候选；ID 耗尽与内存不足不会发布水位。
 * 维护说明: 新曲线只复用唯一独占槽；不修改 legacy、帧率、loop 或其他作者定义。
 */
#include "AnimationEditing.h"

#include "SceneViewModel.h"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <utility>

namespace mini3d::editor {
namespace {
using CurveMap = std::map<core::PropertyAddress, const core::ScalarCurve*>;

CurveMap resolvedCurves(const core::PropertyAnimation& animation) {
    std::map<std::pair<core::ActionId, core::ActionSlotId>, const core::ActionSlotDefinition*> actionSlots;
    for (const auto& action : animation.actions)
        for (const auto& slot : action.actionSlots)
            actionSlots.emplace(std::pair{action.id, slot.id}, &slot);
    CurveMap curves;
    for (const auto& binding : animation.bindings) {
        const auto slot = actionSlots.find({binding.action, binding.slot});
        if (slot == actionSlots.end())
            continue;
        for (const auto& curve : slot->second->curves)
            curves.emplace(core::PropertyAddress{binding.documentUuid, binding.owner,
                                                 curve.property, curve.component}, &curve);
    }
    return curves;
}

template <class Key, class Time, class Collect>
bool collectChangedKeys(const std::vector<Key>* previous, const std::vector<Key>& keys,
                         Time timeOf, Collect collect) {
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const auto& key = keys[index];
        bool unchanged = false;
        if (previous) {
            const auto found = std::lower_bound(previous->begin(), previous->end(), timeOf(key),
                [&](const auto& entry, auto time) { return timeOf(entry) < time; });
            if (found != previous->end() && *found == key) {
                const auto position = static_cast<std::size_t>(found - previous->begin());
                const bool sameLeft = index == 0 ? position == 0
                    : position > 0 && keys[index - 1] == (*previous)[position - 1];
                const bool sameRight = index + 1 == keys.size()
                    ? position + 1 == previous->size()
                    : position + 1 < previous->size() &&
                          keys[index + 1] == (*previous)[position + 1];
                unchanged = sameLeft && sameRight;
            }
        }
        if (!unchanged && !collect(timeOf(key)))
            return false;
    }
    return true;
}

struct SlotLocation {
    std::size_t action = 0;
    std::size_t slot = 0;
    bool operator==(const SlotLocation&) const = default;
};
struct CurveLocation {
    SlotLocation ownerSlot;
    std::size_t curve = 0;
};

std::optional<SlotLocation> bindingSlot(const core::PropertyAnimation& animation,
                                       const core::OwnerBinding& binding) {
    for (std::size_t action = 0; action < animation.actions.size(); ++action) {
        const auto& definition = animation.actions[action];
        if (definition.id != binding.action)
            continue;
        for (std::size_t slot = 0; slot < definition.actionSlots.size(); ++slot)
            if (definition.actionSlots[slot].id == binding.slot)
                return SlotLocation{action, slot};
    }
    return std::nullopt;
}

std::optional<CurveLocation> findCurve(const core::PropertyAnimation& animation,
                                      const core::PropertyAddress& address) {
    for (const auto& binding : animation.bindings) {
        if (binding.documentUuid != address.documentUuid || binding.owner != address.owner)
            continue;
        const auto location = bindingSlot(animation, binding);
        if (!location)
            continue;
        const auto& curves = animation.actions[location->action].actionSlots[location->slot].curves;
        for (std::size_t index = 0; index < curves.size(); ++index)
            if (curves[index].property == address.property &&
                curves[index].component == address.component)
                return CurveLocation{*location, index};
    }
    return std::nullopt;
}

std::size_t bindingCount(const core::PropertyAnimation& animation, SlotLocation location) {
    const auto& action = animation.actions[location.action];
    const auto slot = action.actionSlots[location.slot].id;
    return static_cast<std::size_t>(std::count_if(animation.bindings.begin(),
        animation.bindings.end(), [&](const auto& binding) {
            return binding.action == action.id && binding.slot == slot;
        }));
}

std::optional<SlotLocation> exclusiveSlot(const core::PropertyAnimation& animation,
                                         const core::PropertyAddress& address) {
    std::optional<SlotLocation> result;
    for (const auto& binding : animation.bindings) {
        if (binding.documentUuid != address.documentUuid || binding.owner != address.owner)
            continue;
        const auto location = bindingSlot(animation, binding);
        if (!location || bindingCount(animation, *location) != 1)
            continue;
        if (result && result != location)
            return std::nullopt;
        result = location;
    }
    return result;
}

template <class Id> std::optional<Id> reserveId(std::uint64_t& water) {
    if (water == std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    return Id{++water};
}

void removeEmptyCurve(core::PropertyAnimation& animation, CurveLocation location) {
    auto& action = animation.actions[location.ownerSlot.action];
    auto& slot = action.actionSlots[location.ownerSlot.slot];
    slot.curves.erase(slot.curves.begin() + location.curve);
    if (!slot.curves.empty())
        return;
    const auto slotId = slot.id;
    const auto actionId = action.id;
    std::erase_if(animation.bindings, [&](const auto& binding) {
        return binding.action == actionId && binding.slot == slotId;
    });
    action.actionSlots.erase(action.actionSlots.begin() + location.ownerSlot.slot);
    if (action.actionSlots.empty())
        animation.actions.erase(animation.actions.begin() + location.ownerSlot.action);
}

} // namespace

std::optional<api::ApiError> validateAnimationScalarBudget(
    const core::SceneAnimation& animation, const api::DocumentState& document) {
    const auto exceeded = [&] {
        return api::ApiError{api::ErrorCode::LimitExceeded,
            QStringLiteral("legacy XYZ 与 typed 动画合计超过标量曲线或键预算。"), {},
            api::Recovery::CorrectInput, document};
    };
    if (animation.tracks.size() > core::kAnimationMaximumScalarCurves / 3)
        return exceeded();
    std::size_t curves = animation.tracks.size() * 3, keys = 0;
    for (const auto& [id, track] : animation.tracks) {
        (void)id;
        if (track.keys.size() > (core::kAnimationMaximumScalarKeys - keys) / 3)
            return exceeded();
        keys += track.keys.size() * 3;
    }
    for (const auto& action : animation.properties.actions)
        for (const auto& slot : action.actionSlots) {
            if (slot.curves.size() > core::kAnimationMaximumScalarCurves - curves)
                return exceeded();
            curves += slot.curves.size();
            for (const auto& curve : slot.curves) {
                if (curve.keys.size() > core::kAnimationMaximumScalarKeys - keys)
                    return exceeded();
                keys += curve.keys.size();
            }
        }
    return std::nullopt;
}

std::optional<api::ApiError> validateAnimationPropertyAddress(
    const core::Scene& scene, const core::PropertyAddress& address,
    const api::DocumentState& document, bool forWrite) {
    const auto invalid = [&](const QString& field, const QString& message) {
        return api::ApiError{api::ErrorCode::InvalidArgument, message, "address." + field,
                             api::Recovery::CorrectInput, document};
    };
    if (address.documentUuid != scene.documentUuid())
        return invalid("documentUuid", QStringLiteral("属性作者域无效。"));
    if (address.property != core::PropertyId::Position &&
        address.property != core::PropertyId::RotationEulerXYZDegrees &&
        address.property != core::PropertyId::Scale &&
        address.property != core::PropertyId::PbrRoughness &&
        address.property != core::PropertyId::SunIrradiance)
        return invalid("property", QStringLiteral("未知登记属性。"));
    if (!core::isPropertyComponentValid(address.property, address.component))
        return invalid("component", QStringLiteral("属性分量无效。"));
    if (const auto* owner = std::get_if<core::EntityOwner>(&address.owner)) {
        if (!owner->entity || address.property == core::PropertyId::PbrRoughness)
            return invalid("owner", QStringLiteral("登记属性与实体 owner 不匹配。"));
        const auto* node = scene.find(owner->entity);
        if (!node)
            return api::ApiError{api::ErrorCode::NotFound, QStringLiteral("属性实体不存在。"),
                                 "address.owner.entityId", api::Recovery::Refetch, document};
        if (address.property == core::PropertyId::SunIrradiance && !node->sun)
            return invalid("owner", QStringLiteral("目标实体没有 Sun 组件。"));
    } else if (const auto* owner = std::get_if<core::MaterialOwner>(&address.owner)) {
        if (!owner->material.isValid() || address.property != core::PropertyId::PbrRoughness)
            return invalid("owner", QStringLiteral("登记属性与材料 owner 不匹配。"));
        const auto* material = scene.materialData().find(owner->material);
        if (!material)
            return api::ApiError{api::ErrorCode::NotFound, QStringLiteral("属性材料不存在。"),
                                 "address.owner.materialId", api::Recovery::Refetch, document};
        if (!material->textures.empty())
            return api::ApiError{api::ErrorCode::UnsupportedOperation,
                                 QStringLiteral("J1 作者不支持已声明的纹理。"), "address.owner",
                                 api::Recovery::CorrectInput, document};
    }
    if (forWrite && (address.property == core::PropertyId::RotationEulerXYZDegrees ||
                     address.property == core::PropertyId::Scale))
        return api::ApiError{api::ErrorCode::UnsupportedOperation,
                             QStringLiteral("J1 写入仅开放 Position、roughness 和 Sun irradiance。"),
                             "address.property", api::Recovery::CorrectInput, document};
    return std::nullopt;
}

std::set<core::FrameTime> collectAnimationEditTimes(const core::SceneAnimation& before,
    const core::SceneAnimation& after, core::FrameTime currentFrame, std::size_t nodeCount) {
    std::set<core::FrameTime> frames{currentFrame};
    const auto withinBudget = [&] {
        return core::validateAnimationPreflightBudget(nodeCount, frames.size()).isValid();
    };
    const auto collect = [&](core::FrameTime time) {
        frames.insert(time);
        return withinBudget();
    };
    if (!withinBudget())
        return frames;
    for (const auto& [id, track] : after.tracks) {
        const auto old = before.tracks.find(id);
        if (old != before.tracks.end() && old->second == track)
            continue;
        if (!collectChangedKeys(old == before.tracks.end() ? nullptr : &old->second.keys,
                track.keys, [](const auto& key) { return core::FrameTime(key.frame); }, collect))
            return frames;
    }
    const auto oldCurves = resolvedCurves(before.properties);
    const auto newCurves = resolvedCurves(after.properties);
    for (const auto& [address, curve] : newCurves) {
        const auto old = oldCurves.find(address);
        if (old != oldCurves.end() && *old->second == *curve)
            continue;
        if (!collectChangedKeys(old == oldCurves.end() ? nullptr : &old->second->keys,
                curve->keys, [](const auto& key) { return key.time; }, collect))
            return frames;
    }
    for (const auto& [address, curve] : oldCurves) {
        const auto afterCurve = newCurves.find(address);
        if (afterCurve != newCurves.end() && *afterCurve->second == *curve)
            continue;
        for (const auto& key : curve->keys) {
            if (afterCurve != newCurves.end()) {
                const auto& keys = afterCurve->second->keys;
                const auto found = std::lower_bound(keys.begin(), keys.end(), key.time,
                    [](const auto& entry, auto time) { return entry.time < time; });
                if (found != keys.end() && found->time == key.time)
                    continue;
            }
            if (!collect(key.time))
                return frames;
        }
    }
    return frames;
}

struct AnimationEditing::State {
    std::shared_ptr<const core::Scene> scene;
    api::DocumentState document;
    core::DocumentUuid documentUuid;
    core::SceneIdHighWatermarks ids;
    core::Scene::PreparedAnimation prepared;
    std::optional<api::PropertyKeyIdentity> keyIdentity;
};

AnimationEditing::AnimationEditing(SceneViewModel& model) : model_(model) {}
AnimationEditing::~AnimationEditing() = default;

bool AnimationEditing::prepareKey(const core::PropertyAddress& address, core::FrameTime time,
    double value, core::PropertyInterpolation outgoing, QString& error, LinkedActionEdit linked) {
    const auto result = prepareKeyResult(address, time, value, outgoing, linked);
    error = result.error ? result.error->message : QString{};
    return result.hasValue();
}

bool AnimationEditing::prepareKeyRemoval(const core::PropertyAddress& address,
    core::FrameTime time, QString& error, LinkedActionEdit linked) {
    const auto result = prepareKeyRemovalResult(address, time, linked);
    error = result.error ? result.error->message : QString{};
    return result.hasValue();
}
api::ApiResult<bool> AnimationEditing::prepareKeyResult(const core::PropertyAddress& address,
    core::FrameTime time, double value, core::PropertyInterpolation outgoing, LinkedActionEdit linked) {
    return prepareEdit(address, time, value, outgoing, false, linked);
}
api::ApiResult<bool> AnimationEditing::prepareKeyRemovalResult(const core::PropertyAddress& address,
    core::FrameTime time, LinkedActionEdit linked) {
    return prepareEdit(address, time, 0, core::PropertyInterpolation::Linear, true, linked);
}
api::ApiResult<bool> AnimationEditing::prepareEdit(const core::PropertyAddress& address,
    core::FrameTime time, double value, core::PropertyInterpolation outgoing, bool remove,
    LinkedActionEdit linked) {
    using Result = api::ApiResult<bool>;
    const auto fail = [&](api::ErrorCode code, const QString& message, const QString& field = {}) {
        return Result::failure({code, message, field, api::Recovery::CorrectInput,
                                model_.apiDocumentState()});
    };
    const auto exhausted = [&] {
        return fail(api::ErrorCode::LimitExceeded,
                    QStringLiteral("动画稳定 ID 已耗尽，未发布候选水位。"));
    };
    try {
        const auto scene = model_.scene();
        const auto document = model_.apiDocumentState();
        if (const auto failure = validateAnimationPropertyAddress(*scene, address, document, true))
            return Result::failure(*failure);
        if (!core::isAnimationFrameValid(time))
            return fail(api::ErrorCode::InvalidArgument, QStringLiteral("关键帧时间无效。"), "time");
        if (!remove) {
            if (outgoing == core::PropertyInterpolation::Bezier)
                return fail(api::ErrorCode::UnsupportedOperation,
                            QStringLiteral("尚不支持 Bezier 出射插值。"), "outgoing");
            if (outgoing != core::PropertyInterpolation::Constant &&
                outgoing != core::PropertyInterpolation::Linear)
                return fail(api::ErrorCode::InvalidArgument,
                            QStringLiteral("出射插值无效。"), "outgoing");
            if (!core::validateScalarCurve({core::CurveId{1}, address.property,
                    address.component, {{core::KeyId{1}, time, value, outgoing}}}).isValid())
                return fail(api::ErrorCode::InvalidArgument,
                            QStringLiteral("关键帧原值超出登记属性合法域。"), "value");
        }
        if (linked != LinkedActionEdit::RejectShared && linked != LinkedActionEdit::EditLinked)
            return fail(api::ErrorCode::InvalidArgument, QStringLiteral("共享动作编辑许可无效。"),
                        "linkedEdit");
        std::optional<api::PropertyKeyIdentity> identity;
        auto ids = scene->idHighWatermarks();
        auto candidate = scene->animation();
        auto& properties = candidate.properties;
        const auto location = findCurve(properties, address);
        if (location) {
            auto& curve = properties.actions[location->ownerSlot.action]
                              .actionSlots[location->ownerSlot.slot].curves[location->curve];
            auto key = std::lower_bound(curve.keys.begin(), curve.keys.end(), time,
                [](const auto& entry, auto frame) { return entry.time < frame; });
            const bool found = key != curve.keys.end() && key->time == time;
            if (found) {
                const auto& action = properties.actions[location->ownerSlot.action];
                identity = api::PropertyKeyIdentity{{action.id,
                    action.actionSlots[location->ownerSlot.slot].id, curve.id}, key->id};
            }
            const bool changes = remove ? found
                : !found || key->value != value || key->outgoing != outgoing;
            const bool shared = bindingCount(properties, location->ownerSlot) > 1;
            if (shared && changes && linked == LinkedActionEdit::RejectShared) {
                return fail(api::ErrorCode::UnsupportedOperation,
                    QStringLiteral("曲线由多个 owner 共享，需显式 EditLinked 才能共同编辑。"),
                    "linkedEdit");
            }
            if (remove && found) {
                if (shared && curve.keys.size() == 1) {
                    return fail(api::ErrorCode::UnsupportedOperation,
                        QStringLiteral("不能删除共享曲线最后一键或改变其他 owner 的绑定。"),
                        "linkedEdit");
                }
                curve.keys.erase(key);
                if (curve.keys.empty())
                    removeEmptyCurve(properties, *location);
            } else if (!remove && found && changes) {
                key->value = value;
                key->outgoing = outgoing;
            } else if (!remove && !found) {
                const auto id = reserveId<core::KeyId>(ids.key);
                if (!id)
                    return exhausted();
                curve.keys.insert(key, core::ScalarKey{*id, time, value, outgoing});
                const auto& action = properties.actions[location->ownerSlot.action];
                identity = api::PropertyKeyIdentity{{action.id,
                    action.actionSlots[location->ownerSlot.slot].id, curve.id}, *id};
            }
        } else if (!remove) {
            const auto keyId = reserveId<core::KeyId>(ids.key);
            const auto curveId = reserveId<core::CurveId>(ids.curve);
            if (!keyId || !curveId)
                return exhausted();
            core::ScalarCurve curve{*curveId, address.property, address.component,
                                     {{*keyId, time, value, outgoing}}};
            const auto target = exclusiveSlot(properties, address);
            if (target) {
                properties.actions[target->action].actionSlots[target->slot].curves.push_back(
                    std::move(curve));
                const auto& action = properties.actions[target->action];
                identity = api::PropertyKeyIdentity{{action.id,
                    action.actionSlots[target->slot].id, *curveId}, *keyId};
            } else {
                const auto actionId = reserveId<core::ActionId>(ids.action);
                const auto slotId = reserveId<core::ActionSlotId>(ids.actionSlot);
                if (!actionId || !slotId)
                    return exhausted();
                const auto kind = std::holds_alternative<core::EntityOwner>(address.owner)
                    ? core::PropertyOwnerKind::Entity : core::PropertyOwnerKind::Material;
                properties.actions.push_back({*actionId, {{*slotId, kind, {std::move(curve)}}}});
                properties.bindings.push_back(
                    {address.documentUuid, address.owner, *actionId, *slotId});
                identity = api::PropertyKeyIdentity{{*actionId, *slotId, *curveId}, *keyId};
            }
        }
        if (const auto failure = validateAnimationScalarBudget(candidate, document))
            return Result::failure(*failure);
        std::string diagnostic;
        auto prepared = scene->prepareAnimation(candidate, diagnostic);
        if (!prepared) {
            return fail(api::ErrorCode::InvalidArgument, QString::fromStdString(diagnostic));
        }
        auto state = std::make_unique<State>();
        state->scene = scene;
        state->document = document;
        state->documentUuid = scene->documentUuid();
        state->ids = scene->idHighWatermarks();
        state->prepared = std::move(*prepared);
        state->keyIdentity = identity;
        state_ = std::move(state);
        return Result::success(hasChanges());
    } catch (const std::bad_alloc&) {
        return fail(api::ErrorCode::LimitExceeded,
                    QStringLiteral("内存不足，未改变正式动画或原私有候选。"));
    }
}

bool AnimationEditing::commit(const QString& label, QString& error) {
    const auto result = commitResult(label);
    error = result.error ? result.error->message : QString{};
    return result.hasValue();
}
api::ApiResult<api::AnimationPropertyKeyResult>
AnimationEditing::commitResult(const QString& label, const api::MutationRequest* request) {
    using Result = api::ApiResult<api::AnimationPropertyKeyResult>;
    const auto fail = [&](api::ErrorCode code, const QString& message) {
        return Result::failure({code, message, {}, api::Recovery::Refetch, model_.apiDocumentState()});
    };
    if (request) {
        if (const auto failure = model_.validateApiMutation(*request))
            return Result::failure(*failure);
    }
    if (!state_)
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("尚未准备动画候选。"));
    const auto scene = model_.scene();
    const auto& document = model_.apiDocumentState();
    if (scene != state_->scene || document.document != state_->document.document ||
        document.documentRevision != state_->document.documentRevision ||
        document.historyRevision != state_->document.historyRevision ||
        scene->documentUuid() != state_->documentUuid || scene->idHighWatermarks() != state_->ids ||
        !scene->canInstallPreparedAnimation(state_->prepared)) {
        return fail(api::ErrorCode::RevisionConflict,
            QStringLiteral("动画候选的文档、版本、作者或 ID 水位已变化，请重新准备。"));
    }
    try {
        api::AnimationPropertyKeyResult result;
        result.keyIdentity = state_->keyIdentity;
        auto committed = model_.commitAnimationDefinition(state_->prepared.after(), label, false,
                                                          request);
        if (committed.error)
            return Result::failure(*committed.error);
        result.command = std::move(*committed.value);
        state_.reset();
        return Result::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return fail(api::ErrorCode::LimitExceeded, QStringLiteral("内存不足，未提交动画候选。"));
    }
}

void AnimationEditing::cancel() { state_.reset(); }
bool AnimationEditing::hasChanges() const { return state_ && state_->prepared.hasChanges(); }
} // namespace mini3d::editor
