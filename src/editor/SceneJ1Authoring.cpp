/*
 * 模块名: SceneJ1Authoring
 * 功能概述: 实现 J1 材料、材质槽、Sun 和 typed 动画的共享作者业务。
 * 对外接口: SceneViewModel 的十一个 J1 Explicit 入口。
 * 依赖关系: Core 可信候选/完整求值、AnimationEditing、Qt 唯一撤销栈。
 * 输入输出: 强类型作者请求到单笔历史或独立完整 Sample 结果。
 * 异常与错误: 非法参数、过期来源、资源不支持和最终守卫拒绝均不发布候选。
 * 维护说明: 同应用线程同步；不创建后台工作，不安装 Sample 姿态或更改选择。
 */
#include "AnimationEditing.h"
#include "EditCommand.h"
#include "SceneViewModel.h"

#include <QDeadlineTimer>
#include <QScopeGuard>
#include <algorithm>
#include <limits>
#include <new>
#include <set>

namespace mini3d::editor {
namespace {
struct ResolvedPropertyCurve {
    api::PropertyCurveIdentity identity;
    const core::ScalarCurve* curve = nullptr;
};

std::optional<ResolvedPropertyCurve> resolvePropertyCurve(
    const core::PropertyAnimation& animation, const core::PropertyAddress& address) {
    for (const auto& binding : animation.bindings) {
        if (binding.documentUuid != address.documentUuid || binding.owner != address.owner)
            continue;
        for (const auto& action : animation.actions) {
            if (action.id != binding.action)
                continue;
            for (const auto& slot : action.actionSlots) {
                if (slot.id != binding.slot)
                    continue;
                for (const auto& curve : slot.curves)
                    if (curve.property == address.property && curve.component == address.component)
                        return ResolvedPropertyCurve{{action.id, slot.id, curve.id}, &curve};
            }
        }
    }
    return std::nullopt;
}

std::optional<double> effectivePropertyValue(const core::EvaluatedSceneState& state,
                                             const core::PropertyAddress& address) {
    const auto& content = *state.content;
    const auto trace = std::find_if(content.propertySources.begin(), content.propertySources.end(),
        [&](const auto& value) { return value.address == address; });
    if (trace != content.propertySources.end())
        return trace->effectiveValue;
    if (const auto* owner = std::get_if<core::MaterialOwner>(&address.owner))
        return content.materials.at(owner->material).effective.roughness;
    const auto entity = std::get<core::EntityOwner>(address.owner).entity;
    const auto* node = state.find(entity);
    const auto axis = static_cast<int>(address.component);
    if (address.property == core::PropertyId::Position)
        return node->localPosition[axis];
    if (address.property == core::PropertyId::Scale)
        return node->localScale[axis];
    if (address.property == core::PropertyId::RotationEulerXYZDegrees) {
        const auto euler = node->rotationEulerXYZDegrees
            ? node->rotationEulerXYZDegrees : core::canonicalEulerXYZDegrees(node->localRotation);
        return euler ? std::optional<double>{(*euler)[axis]} : std::nullopt;
    }
    return content.authors->nodes.at(entity).sun->irradiance;
}
} // namespace

/** @brief 复核当前正式查询身份、可选内容 CAS 和实际 Busy；不读取或修改 UI。 */
std::optional<api::ApiError>
SceneViewModel::validateJ1AnimationQuery(const api::AnimationQueryRequest& request) const {
    const auto& state = apiDocumentState();
    if (request.document != state.document)
        return api::ApiError{api::ErrorCode::StaleDocument, QStringLiteral("文档句柄已失效。"),
                             "document", api::Recovery::Refetch, state};
    if (request.expectedDocumentRevision && *request.expectedDocumentRevision != state.documentRevision)
        return api::ApiError{api::ErrorCode::RevisionConflict, QStringLiteral("文档版本已变化。"),
                             "expectedDocumentRevision", api::Recovery::Refetch, state};
    const auto reasons = apiBusyReasons(false);
    if (!reasons.isEmpty())
        return api::ApiError{api::ErrorCode::Busy,
                             QStringLiteral("请先结束当前交互：%1").arg(reasons.join(", ")),
                             {}, api::Recovery::Wait, state};
    return std::nullopt;
}

/** @brief 创建无纹理材料；成功才公开 MaterialId，失败不推进资产水位。 */
api::ApiResult<api::MaterialMutationResult>
SceneViewModel::createMaterialExplicit(const api::MaterialCreateRequest& request) {
    using Result = api::ApiResult<api::MaterialMutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    if (!core::isPbrMaterialValid(request.parameters))
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("PBR 参数须为合法线性颜色和 0～1 作者值。"), "parameters"));
    if (scene_->idHighWatermarks().asset == std::numeric_limits<std::uint64_t>::max())
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("稳定资产身份已耗尽。")));
    try {
        const core::MaterialId id{scene_->idHighWatermarks().asset + 1};
        auto materials = scene_->materialData();
        core::MaterialDefinition definition;
        definition.id = id;
        definition.parameters = request.parameters;
        const core::StableAssetId asset{id.value};
        materials.assets.emplace(asset, core::SceneAssetDefinition{asset,
            core::SceneAssetKind::Material, std::move(definition)});
        std::string diagnostic;
        auto prepared = scene_->prepareMaterialData(materials, diagnostic);
        if (!prepared)
            return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                QString::fromStdString(diagnostic)));
        return commitMaterialData(std::move(*prepared), id, QStringLiteral("创建材料"), request);
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未创建材料。")));
    }
}

/** @brief 返回完整正式材料值；只读身份和作者精度，不应用动画样本。 */
api::ApiResult<api::MaterialGetResult>
SceneViewModel::getMaterialExplicit(const api::MaterialGetRequest& request) const {
    using Result = api::ApiResult<api::MaterialGetResult>;
    api::AnimationQueryRequest query;
    query.document = request.document;
    if (const auto failure = validateJ1AnimationQuery(query))
        return Result::failure(*failure);
    if (!request.materialId.isValid())
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("材料身份不能为零。"), "materialId"));
    const auto* material = scene_->materialData().find(request.materialId);
    if (!material)
        return Result::failure(animationFailure(api::ErrorCode::NotFound,
            QStringLiteral("材料不存在。"), "materialId"));
    try {
        return Result::success({apiDocumentState(), *material});
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未返回材料。")));
    }
}

/** @brief 更新已有材料的完整参数；保持其 definitionVersion 和稳定身份。 */
api::ApiResult<api::MaterialMutationResult>
SceneViewModel::updateMaterialExplicit(const api::MaterialUpdateRequest& request) {
    using Result = api::ApiResult<api::MaterialMutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    if (!request.materialId.isValid() || !core::isPbrMaterialValid(request.parameters))
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("材料身份或完整 PBR 参数无效。"), "parameters"));
    const auto* material = scene_->materialData().find(request.materialId);
    if (!material)
        return Result::failure(animationFailure(api::ErrorCode::NotFound,
            QStringLiteral("材料不存在。"), "materialId"));
    if (!material->textures.empty())
        return Result::failure(animationFailure(api::ErrorCode::UnsupportedOperation,
            QStringLiteral("J1 作者不支持已声明的纹理。"), "materialId"));
    try {
        auto materials = scene_->materialData();
        materials.assets.at(core::StableAssetId{request.materialId.value}).material->parameters =
            request.parameters;
        std::string diagnostic;
        auto prepared = scene_->prepareMaterialData(materials, diagnostic);
        if (!prepared)
            return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                QString::fromStdString(diagnostic)));
        return commitMaterialData(std::move(*prepared), request.materialId,
                                  QStringLiteral("修改材料"), request);
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未修改材料。")));
    }
}

/** @brief 一次可信材料候选进入唯一历史；完整预览也由同一候选形成。 */
api::ApiResult<api::MaterialMutationResult>
SceneViewModel::commitMaterialData(core::Scene::PreparedMaterialData candidate,
    core::MaterialId material, const QString& label, const api::MutationRequest& request) {
    using Result = api::ApiResult<api::MaterialMutationResult>;
    const QDeadlineTimer deadline(request.timeoutMs, Qt::PreciseTimer);
    pendingAnimationReplay_.reset();
    const auto source = animationPreparationSource();
    if (!matchesAnimationPreparationSource(source) || !scene_->canInstallPreparedMaterialData(candidate))
        return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
            QStringLiteral("材料候选的来源或资产水位已变化。")));
    api::MaterialMutationResult result;
    result.materialId = material;
    if (!candidate.hasChanges()) {
        if (const auto failure = checkBeforeCommit(source, nullptr, nullptr, &request))
            return Result::failure(*failure);
        if (const auto failure = validateApiMutation(request))
            return Result::failure(*failure);
        if (!scene_->canInstallPreparedMaterialData(candidate))
            return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                QStringLiteral("材料候选来源已变化。")));
        if (deadline.hasExpired())
            return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
                QStringLiteral("材料请求超过截止时间。")));
        result.command.state = apiDocumentState();
        return Result::success(std::move(result));
    }
    auto prepared = std::make_shared<core::Scene::PreparedMaterialData>(std::move(candidate));
    for (const auto& node : scene_->nodes())
        if (std::any_of(node.materialSlots.begin(), node.materialSlots.end(),
            [&](const auto& slot) { return slot.material == material; }))
            result.command.affectedEntityIds.push_back(node.id);
    const auto apply = [this, prepared](bool forward) {
        const bool installed = forward ? scene_->installPreparedMaterialData(*prepared)
                                       : scene_->restorePreparedMaterialData(*prepared);
        Q_ASSERT(installed);
        queueHistoryNotifications(false);
    };
    const auto replay = [this, prepared](bool forward, QString& error) {
        auto geometry = std::make_shared<renderer_gl::PoseGeometry>(*installedAnimationPose_->geometry);
        geometry->materials = std::make_shared<const core::SceneMaterialData>(
            forward ? prepared->after() : prepared->before());
        return prepareSharedAnimationReplay(error, std::move(geometry));
    };
    auto command = std::make_unique<EditCommand>(label, [apply] { apply(false); },
                                                 [apply] { apply(true); }, replay);
    auto pendingReplayCleanup = qScopeGuard([this] {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
    });
    if (animationMode_ != AnimationMode::Base) {
        QString error;
        pendingAnimationReplay_ = replay(true, error);
        if (!pendingAnimationReplay_)
            return Result::failure(animationFailure(api::ErrorCode::UnsupportedTransform, error));
        pendingAnimationCommand_ = command.get();
    }
    result.command.status = api::ResultStatus::Committed;
    result.command.undoable = true;
    if (const auto failure = checkBeforeCommit(source, nullptr, nullptr, &request))
        return Result::failure(*failure);
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    if (deadline.hasExpired())
        return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
            QStringLiteral("材料请求超过截止时间。")));
    if (!scene_->canInstallPreparedMaterialData(*prepared))
        return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
            QStringLiteral("材料候选来源或资产水位已变化。")));
    pendingReplayCleanup.dismiss();
    pushHistory(command.release());
    result.command.state = apiDocumentState();
    return Result::success(std::move(result));
}

/** @brief 零槽发行首槽，单槽只改材料引用；多槽明确拒绝，不隐式重映射面。 */
api::ApiResult<api::EntityAssignMaterialResult>
SceneViewModel::assignMaterialExplicit(const api::EntityAssignMaterialRequest& request) {
    using Result = api::ApiResult<api::EntityAssignMaterialResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    const QDeadlineTimer deadline(request.timeoutMs, Qt::PreciseTimer);
    if (!request.entityId || !request.materialId.isValid())
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("对象或材料身份不能为零。")));
    const auto* node = scene_->find(request.entityId);
    const auto* material = scene_->materialData().find(request.materialId);
    if (!node || !material)
        return Result::failure(animationFailure(api::ErrorCode::NotFound,
            QStringLiteral("对象或材料不存在。")));
    if (node->materialSlots.size() > 1 || !material->textures.empty())
        return Result::failure(animationFailure(api::ErrorCode::UnsupportedOperation,
            QStringLiteral("J1 分配只支持零槽或单槽及无纹理材料。")));
    if (node->materialSlots.empty() &&
        scene_->idHighWatermarks().materialSlot == std::numeric_limits<std::uint64_t>::max())
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("材质槽稳定身份已耗尽。")));
    try {
        pendingAnimationReplay_.reset();
        auto materialSlots = node->materialSlots;
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source))
            return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                QStringLiteral("材质分配准备期间来源已变化。")));
        if (materialSlots.empty())
            materialSlots.push_back({core::MaterialSlotId{scene_->idHighWatermarks().materialSlot + 1},
                              request.materialId});
        else
            materialSlots.front().material = request.materialId;
        std::string diagnostic;
        auto candidate = scene_->prepareMaterialSlots(request.entityId, materialSlots, diagnostic);
        if (!candidate)
            return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                QString::fromStdString(diagnostic)));
        api::EntityAssignMaterialResult result;
        result.entityId = request.entityId;
        result.materialId = request.materialId;
        result.materialSlotId = materialSlots.front().id;
        if (!candidate->hasChanges()) {
            if (const auto failure = checkBeforeCommit(source, nullptr, nullptr, &request))
                return Result::failure(*failure);
            if (const auto failure = validateApiMutation(request))
                return Result::failure(*failure);
            if (!scene_->canInstallPreparedMaterialSlots(*candidate))
                return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                    QStringLiteral("材质分配候选来源已变化。")));
            if (deadline.hasExpired())
                return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
                    QStringLiteral("材质分配超过截止时间。")));
            result.command.state = apiDocumentState();
            return Result::success(std::move(result));
        }
        auto prepared = std::make_shared<core::Scene::PreparedMaterialSlots>(std::move(*candidate));
        const auto apply = [this, prepared](bool forward) {
            const bool installed = forward ? scene_->installPreparedMaterialSlots(*prepared)
                                           : scene_->restorePreparedMaterialSlots(*prepared);
            Q_ASSERT(installed);
            queueEntityNotification(prepared->entityId());
        };
        const auto replay = [this, prepared](bool forward, QString& error) {
            auto geometry = std::make_shared<renderer_gl::PoseGeometry>(*installedAnimationPose_->geometry);
            geometry->entries.at(prepared->entityId()).materialSlots =
                forward ? prepared->after() : prepared->before();
            return prepareSharedAnimationReplay(error, std::move(geometry));
        };
        auto command = std::make_unique<EditCommand>(QStringLiteral("分配材料"),
            [apply] { apply(false); }, [apply] { apply(true); }, replay);
        auto pendingReplayCleanup = qScopeGuard([this] {
            pendingAnimationReplay_.reset();
            pendingAnimationCommand_ = nullptr;
        });
        if (animationMode_ != AnimationMode::Base) {
            QString error;
            pendingAnimationReplay_ = replay(true, error);
            if (!pendingAnimationReplay_)
                return Result::failure(animationFailure(api::ErrorCode::UnsupportedTransform, error));
            pendingAnimationCommand_ = command.get();
        }
        result.command.status = api::ResultStatus::Committed;
        result.command.affectedEntityIds = {request.entityId};
        result.command.undoable = true;
        if (const auto failure = checkBeforeCommit(source, nullptr, nullptr, &request))
            return Result::failure(*failure);
        if (const auto failure = validateApiMutation(request))
            return Result::failure(*failure);
        if (deadline.hasExpired())
            return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
                QStringLiteral("材质分配超过截止时间。")));
        if (!scene_->canInstallPreparedMaterialSlots(*prepared))
            return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                QStringLiteral("材质分配候选来源或水位已变化。")));
        pendingReplayCleanup.dismiss();
        pushHistory(command.release());
        result.command.state = apiDocumentState();
        return Result::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未分配材料。")));
    }
}

/** @brief Sun 与空对象在同一次 provisional prepare 和唯一历史中创建。 */
api::ApiResult<api::MutationResult>
SceneViewModel::createSunExplicit(const api::SunCreateRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    try {
        core::Scene::EntityCreateOptions options;
        options.name = request.name.toUtf8().toStdString();
        options.parent = request.parentId;
        options.transform = request.transform;
        options.visible = request.visible;
        options.sun = request.sun;
        return commitEntityCreate(options, false, nullptr, &request);
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未创建 Sun。")));
    }
}

/** @brief 只更新已有 Sun；完整值通过守卫后成为一笔可精确撤销的历史。 */
api::ApiResult<api::MutationResult>
SceneViewModel::updateSunExplicit(const api::SunUpdateRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    const QDeadlineTimer deadline(request.timeoutMs, Qt::PreciseTimer);
    const auto* node = scene_->find(request.entityId);
    if (!request.entityId || !core::isSunComponentValid(request.sun))
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("Sun 身份或完整参数无效。"), "sun"));
    if (!node)
        return Result::failure(animationFailure(api::ErrorCode::NotFound,
            QStringLiteral("对象不存在。"), "entityId"));
    if (!node->sun)
        return Result::failure(animationFailure(api::ErrorCode::UnsupportedOperation,
            QStringLiteral("目标不是已有 Sun。"), "entityId"));
    try {
        pendingAnimationReplay_.reset();
        const auto before = *node->sun;
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source))
            return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                QStringLiteral("Sun 更新准备期间来源已变化。")));
        api::MutationResult result;
        if (before == request.sun) {
            if (const auto failure = checkBeforeCommit(source, nullptr, nullptr, &request))
                return Result::failure(*failure);
            if (const auto failure = validateApiMutation(request))
                return Result::failure(*failure);
            const auto* current = scene_->find(request.entityId);
            if (!current || current->sun != before)
                return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                    QStringLiteral("Sun 作者值已变化。")));
            if (deadline.hasExpired())
                return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
                    QStringLiteral("Sun 更新超过截止时间。")));
            result.state = apiDocumentState();
            return Result::success(std::move(result));
        }
        const auto apply = [this, id = request.entityId](const core::SunComponent& value) {
            const bool installed = scene_->setSun(id, value);
            Q_ASSERT(installed);
            queueEntityNotification(id);
        };
        const auto replay = [this, id = request.entityId, before, after = request.sun](
                                bool forward, QString& error) {
            auto geometry = std::make_shared<renderer_gl::PoseGeometry>(*installedAnimationPose_->geometry);
            geometry->entries.at(id).sun = forward ? after : before;
            return prepareSharedAnimationReplay(error, std::move(geometry));
        };
        auto command = std::make_unique<EditCommand>(QStringLiteral("修改 Sun"),
            [apply, before] { apply(before); }, [apply, after = request.sun] { apply(after); }, replay);
        auto pendingReplayCleanup = qScopeGuard([this] {
            pendingAnimationReplay_.reset();
            pendingAnimationCommand_ = nullptr;
        });
        if (animationMode_ != AnimationMode::Base) {
            QString error;
            pendingAnimationReplay_ = replay(true, error);
            if (!pendingAnimationReplay_)
                return Result::failure(animationFailure(api::ErrorCode::UnsupportedTransform, error));
            pendingAnimationCommand_ = command.get();
        }
        result.status = api::ResultStatus::Committed;
        result.affectedEntityIds = {request.entityId};
        result.undoable = true;
        if (const auto failure = checkBeforeCommit(source, nullptr, nullptr, &request))
            return Result::failure(*failure);
        if (const auto failure = validateApiMutation(request))
            return Result::failure(*failure);
        const auto* current = scene_->find(request.entityId);
        if (!current || current->sun != before)
            return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                QStringLiteral("Sun 作者值已变化。")));
        if (deadline.hasExpired())
            return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
                QStringLiteral("Sun 更新超过截止时间。")));
        pendingReplayCleanup.dismiss();
        pushHistory(command.release());
        result.state = apiDocumentState();
        return Result::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未修改 Sun。")));
    }
}

/** @brief 强类型插键/替换委托同一候选服务；替换保留已有 KeyId。 */
api::ApiResult<api::AnimationPropertyKeyResult>
SceneViewModel::upsertAnimationPropertyKeyframeExplicit(
    const api::AnimationUpsertPropertyKeyframeRequest& request) {
    using Result = api::ApiResult<api::AnimationPropertyKeyResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    AnimationEditing editing(*this);
    const auto prepared = editing.prepareKeyResult(request.address, request.time, request.value,
                                                   request.outgoing, request.linkedEdit);
    if (prepared.error)
        return Result::failure(*prepared.error);
    return editing.commitResult(QStringLiteral("写入属性关键帧"), &request);
}

/** @brief 精确时间删除；未命中仍经过最终守卫且返回 no_change/nullopt。 */
api::ApiResult<api::AnimationPropertyKeyResult>
SceneViewModel::deleteAnimationPropertyKeyframeExplicit(
    const api::AnimationDeletePropertyKeyframeRequest& request) {
    using Result = api::ApiResult<api::AnimationPropertyKeyResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    AnimationEditing editing(*this);
    const auto prepared = editing.prepareKeyRemovalResult(request.address, request.time,
                                                          request.linkedEdit);
    if (prepared.error)
        return Result::failure(*prepared.error);
    return editing.commitResult(QStringLiteral("删除属性关键帧"), &request);
}

/** @brief 按精确子帧分页正式键；续页冻结内容版本，所有身份保持 uint64。 */
api::ApiResult<api::AnimationPropertyKeyframePageResult>
SceneViewModel::readAnimationPropertyKeyframesExplicit(
    const api::AnimationReadPropertyKeyframesRequest& request) const {
    using Result = api::ApiResult<api::AnimationPropertyKeyframePageResult>;
    if (const auto failure = validateJ1AnimationQuery(request))
        return Result::failure(*failure);
    if (const auto failure = validateAnimationPropertyAddress(*scene_, request.address,
                                                             apiDocumentState(), false))
        return Result::failure(*failure);
    if (request.afterTime && !request.expectedDocumentRevision)
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("续页必须冻结内容版本。"), "expectedDocumentRevision"));
    if (request.limit < 1 || request.limit > int(api::limits::sourcePageMaximum) ||
        (request.afterTime && !core::isAnimationFrameValid(*request.afterTime)))
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("属性键分页参数无效。"), request.afterTime ? "afterTime" : "limit"));
    try {
        api::AnimationPropertyKeyframePageResult result;
        result.state = apiDocumentState();
        result.address = request.address;
        const auto resolved = resolvePropertyCurve(scene_->animation().properties, request.address);
        if (resolved) {
            result.curveIdentity = resolved->identity;
            const auto& keys = resolved->curve->keys;
            auto iterator = request.afterTime
                ? std::upper_bound(keys.begin(), keys.end(), *request.afterTime,
                    [](auto time, const auto& key) { return time < key.time; }) : keys.begin();
            for (; iterator != keys.end(); ++iterator) {
                if (result.keys.size() == std::size_t(request.limit)) {
                    result.nextAfterTime = result.keys.back().time;
                    break;
                }
                result.keys.push_back(*iterator);
            }
        }
        return Result::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未返回属性键页。")));
    }
}

/** @brief 正式作者在完整 double 状态中独立取样；独立计数不推进安装会话。 */
api::ApiResult<api::AnimationPropertySampleResult>
SceneViewModel::sampleAnimationPropertiesExplicit(
    const api::AnimationSamplePropertiesRequest& request) const {
    using Result = api::ApiResult<api::AnimationPropertySampleResult>;
    if (const auto failure = validateJ1AnimationQuery(request))
        return Result::failure(*failure);
    if (!core::isAnimationFrameValid(request.frame) || request.addresses.empty())
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("取样帧或属性地址集合无效。"), "frame"));
    if (request.addresses.size() > api::limits::sourcePageMaximum)
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("取样地址数量超过既有页面预算。"), "addresses"));
    if (animationSampleEvaluationId_ == std::numeric_limits<std::uint64_t>::max())
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("独立取样身份已耗尽。")));
    try {
        std::set<core::PropertyAddress> addresses;
        for (const auto& address : request.addresses) {
            if (const auto failure = validateAnimationPropertyAddress(*scene_, address,
                                                                     apiDocumentState(), false))
                return Result::failure(*failure);
            if (!addresses.insert(address).second)
                return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                    QStringLiteral("取样地址不能重复。"), "addresses"));
        }
        const auto preparedSource = animationPreparationState();
        std::string diagnostic;
        const auto inputs = scene_->animationPoseInputs(diagnostic);
        if (!inputs)
            return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
                QString::fromStdString(diagnostic)));
        const auto geometry = renderer_gl::makePoseGeometry(*scene_, assets_, {}, *inputs);
        if (!geometry || !geometry->hasCompleteResourceIdentity)
            return Result::failure(animationFailure(api::ErrorCode::UnsupportedOperation,
                QStringLiteral("完整取样所需的作者资源或稳定资源身份尚未就绪。")));
        auto source = animationSceneSource(preparedSource, scene_->animation(), request.frame,
                                           AnimationMode::PreviewPaused);
        source.purpose = core::EvaluationPurpose::Sample;
        source.evaluationSessionId = animationSampleSessionId_;
        source.evaluationId = animationSampleEvaluationId_ + 1;
        const bool joint = !scene_->animation().properties.actions.empty() ||
            !scene_->materialData().assets.empty() ||
            std::any_of(geometry->entries.begin(), geometry->entries.end(), [](const auto& item) {
                return item.second.sun.has_value() || !item.second.materialSlots.empty();
            });
        if (joint)
            source.evaluationProfile = core::EvaluationProfile::JointPreviewJ1;
        const auto evaluated = core::evaluateSceneState(*inputs, scene_->animation(),
            geometry->authors, geometry->sceneGeometry, source);
        if (!evaluated.state)
            return Result::failure(animationFailure(api::ErrorCode::UnsupportedTransform,
                QStringLiteral("正式作者的完整属性求值失败。"), "frame"));
        api::AnimationPropertySampleResult result;
        result.state = preparedSource.document;
        result.frame = request.frame;
        result.source = evaluated.state->source;
        result.samples.reserve(request.addresses.size());
        for (const auto& address : request.addresses) {
            const auto value = effectivePropertyValue(*evaluated.state, address);
            if (!value)
                return Result::failure(animationFailure(api::ErrorCode::UnsupportedTransform,
                    QStringLiteral("完整状态不能表达请求属性。"), "addresses"));
            result.samples.push_back({address, *value});
        }
        if (const auto failure = validateJ1AnimationQuery(request))
            return Result::failure(*failure);
        if (apiDocumentState().document != preparedSource.document.document ||
            apiDocumentState().documentRevision != preparedSource.document.documentRevision ||
            apiDocumentState().historyRevision != preparedSource.document.historyRevision)
            return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                QStringLiteral("属性取样来源已变化。")));
        animationSampleEvaluationId_ = result.source.evaluationId;
        return Result::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未返回完整属性取样。")));
    }
}

/** @brief 规范有理设置写入 override；不移动作者键或新增播放时钟。 */
api::ApiResult<api::MutationResult>
SceneViewModel::setRationalAnimationSettingsExplicit(
    const api::AnimationSetRationalSettingsRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    const auto& settings = request.settings;
    if (!core::isRationalFpsValid(settings.rationalFps) ||
        !core::isAnimationFrameValid(settings.startFrame) ||
        !core::isAnimationFrameValid(settings.endFrame) || settings.startFrame > settings.endFrame ||
        (settings.loopPolicy != core::AnimationLoopPolicy::InclusiveFrameStep &&
         settings.loopPolicy != core::AnimationLoopPolicy::LegacyHalfOpen))
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("有理帧率、播放范围或循环策略无效。"), "settings"));
    try {
        auto candidate = scene_->animation();
        candidate.settings.startFrame = settings.startFrame;
        candidate.settings.endFrame = settings.endFrame;
        candidate.properties.rationalFps = settings.rationalFps;
        candidate.properties.loopPolicy = settings.loopPolicy;
        return commitAnimationDefinition(candidate, QStringLiteral("修改有理动画设置"), false, &request);
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未修改有理动画设置。")));
    }
}
} // namespace mini3d::editor
