/*
 * 模块名: SceneAnimationApi
 * 功能概述: 组装显式动画定义请求，复用唯一正式候选预检与共享历史事务。
 * 对外接口: SceneViewModel 的五个动画定义 Explicit 入口。
 * 依赖关系: SceneViewModel、Core Animation、Qt DeadlineTimer。
 * 输入输出: 有界 DTO 到完整临时候选；成功仅发布一次内容和历史。
 * 异常与错误: 重复目标、冲突、非法值、过期请求和超预算不发布部分结果。
 * 维护说明: 不复制插值/邻接数学，不折叠连续 Euler，不改 GUI 草稿。
 */
#include "SceneViewModel.h"

#include <QDeadlineTimer>
#include <algorithm>
#include <new>
#include <set>
#include <tuple>

namespace mini3d::editor {
namespace {
bool validChannel(core::AnimationChannel channel) {
    return channel == core::AnimationChannel::Position ||
           channel == core::AnimationChannel::RotationEulerXYZDegrees ||
           channel == core::AnimationChannel::Scale;
}
bool validConflict(api::AnimationConflict value) {
    return value == api::AnimationConflict::Reject || value == api::AnimationConflict::Replace;
}
std::optional<api::ApiError> targetFailure(const core::Scene& scene,
    const api::AnimationKeyframeTarget& target, const api::DocumentState& state, const QString& path,
    const QString& frameField = QStringLiteral("frame")) {
    const auto invalid = [&](const QString& field) {
        return api::ApiError{api::ErrorCode::InvalidArgument, QStringLiteral("动画目标字段无效。"),
            path + field, api::Recovery::CorrectInput, state};
    };
    if (!target.entityId)
        return invalid("entityId");
    if (!validChannel(target.channel))
        return invalid("channel");
    if (!core::isAnimationFrameValid(target.frame))
        return invalid(frameField);
    if (!scene.find(target.entityId))
        return api::ApiError{api::ErrorCode::NotFound, QStringLiteral("动画目标对象不存在。"),
            path + "entityId", api::Recovery::CorrectInput, state};
    return std::nullopt;
}
} // namespace

api::ApiResult<api::MutationResult>
SceneViewModel::setAnimationSettingsExplicit(const api::AnimationSetSettingsRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    try {
        auto candidate = scene_->animation();
        candidate.settings = request.settings;
        return commitAnimationDefinition(candidate, QStringLiteral("修改动画设置"), false, &request);
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未修改动画设置。")));
    }
}
api::ApiResult<api::MutationResult>
SceneViewModel::upsertAnimationKeyframesExplicit(const api::AnimationUpsertKeyframesRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    if (request.items.empty() || request.items.size() > api::limits::animationBatchItems)
        return Result::failure(animationFailure(request.items.empty() ? api::ErrorCode::InvalidArgument
            : api::ErrorCode::LimitExceeded, QStringLiteral("关键帧批次数量须在1到1024之间。"), "items"));
    if (!validConflict(request.onConflict))
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("必须明确选择 reject 或 replace。"), "onConflict"));
    try {
        const QDeadlineTimer deadline(request.timeoutMs, Qt::PreciseTimer);
        std::set<std::tuple<core::EntityId, core::AnimationChannel, std::uint32_t>> targets;
        auto candidate = scene_->animation();
        for (std::size_t index = 0; index < request.items.size(); ++index) {
            const auto& item = request.items[index];
            const auto path = QStringLiteral("items[%1].").arg(index);
            if (deadline.hasExpired())
                return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
                    QStringLiteral("动画批次准备超过截止时间。")));
            if (const auto failure = targetFailure(*scene_, item, apiDocumentState(), path))
                return Result::failure(*failure);
            if (!targets.emplace(item.entityId, item.channel, item.frame).second)
                return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                    QStringLiteral("同一批次不接受重复关键帧目标。"), path + "frame"));
            if (!core::validateAnimationValue(item.channel, item.value).isValid())
                return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                    QStringLiteral("关键帧原始 XYZ 值无效。"), path + "value"));
            if (item.interpolation != core::AnimationInterpolation::Constant &&
                item.interpolation != core::AnimationInterpolation::Linear)
                return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                    QStringLiteral("插值必须为 constant 或 linear。"), path + "interpolation"));
            auto& keys = candidate.tracks[{item.entityId, item.channel}].keys;
            auto found = std::lower_bound(keys.begin(), keys.end(), item.frame,
                [](const auto& key, auto frame) { return key.frame < frame; });
            const core::AnimationKeyframe value{item.frame, item.value, item.interpolation};
            if (found != keys.end() && found->frame == item.frame) {
                if (request.onConflict == api::AnimationConflict::Reject)
                    return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                        QStringLiteral("目标帧已有关键帧；需明确 replace。"), path + "frame"));
                *found = value;
            } else {
                keys.insert(found, value);
            }
        }
        return commitAnimationDefinition(candidate, QStringLiteral("批量写入关键帧"), false, &request);
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未写入关键帧批次。")));
    }
}
api::ApiResult<api::MutationResult>
SceneViewModel::deleteAnimationKeyframesExplicit(const api::AnimationDeleteKeyframesRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    if (request.items.empty() || request.items.size() > api::limits::animationBatchItems)
        return Result::failure(animationFailure(request.items.empty() ? api::ErrorCode::InvalidArgument
            : api::ErrorCode::LimitExceeded, QStringLiteral("关键帧批次数量须在1到1024之间。"), "items"));
    try {
        const QDeadlineTimer deadline(request.timeoutMs, Qt::PreciseTimer);
        std::set<std::tuple<core::EntityId, core::AnimationChannel, std::uint32_t>> targets;
        auto candidate = scene_->animation();
        for (std::size_t index = 0; index < request.items.size(); ++index) {
            const auto& item = request.items[index];
            const auto path = QStringLiteral("items[%1].").arg(index);
            if (deadline.hasExpired())
                return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
                    QStringLiteral("动画批次准备超过截止时间。")));
            if (const auto failure = targetFailure(*scene_, item, apiDocumentState(), path))
                return Result::failure(*failure);
            if (!targets.emplace(item.entityId, item.channel, item.frame).second)
                return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                    QStringLiteral("同一批次不接受重复关键帧目标。"), path + "frame"));
            const auto found = candidate.tracks.find({item.entityId, item.channel});
            if (found == candidate.tracks.end())
                continue;
            auto& keys = found->second.keys;
            std::erase_if(keys, [&](const auto& key) { return key.frame == item.frame; });
            if (keys.empty())
                candidate.tracks.erase(found);
        }
        // 删除形成的新邻接由共享完整轨道校验检查，不能只检查删除点。
        return commitAnimationDefinition(candidate, QStringLiteral("批量删除关键帧"), false, &request);
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未删除关键帧批次。")));
    }
}
api::ApiResult<api::MutationResult>
SceneViewModel::removeAnimationTrackExplicit(const api::AnimationRemoveTrackRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    if (const auto failure = targetFailure(*scene_, {request.entityId, request.channel, 1},
                                           apiDocumentState(), {}))
        return Result::failure(*failure);
    try {
        auto candidate = scene_->animation();
        candidate.tracks.erase({request.entityId, request.channel});
        return commitAnimationDefinition(candidate, QStringLiteral("移除动画轨道"), false, &request);
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未移除动画轨道。")));
    }
}
api::ApiResult<api::MutationResult>
SceneViewModel::moveAnimationKeyframeExplicit(const api::AnimationMoveKeyframeRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    if (const auto failure = targetFailure(*scene_, {request.entityId, request.channel, request.fromFrame},
                                           apiDocumentState(), {}, "fromFrame"))
        return Result::failure(*failure);
    if (!core::isAnimationFrameValid(request.toFrame) || !validConflict(request.onConflict))
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("移动目标帧或冲突策略无效。"),
            validConflict(request.onConflict) ? "toFrame" : "onConflict"));
    try {
        auto candidate = scene_->animation();
        const auto track = candidate.tracks.find({request.entityId, request.channel});
        if (track == candidate.tracks.end())
            return Result::failure(animationFailure(api::ErrorCode::NotFound,
                QStringLiteral("源关键帧不存在。"), "fromFrame"));
        auto& keys = track->second.keys;
        const auto source = std::find_if(keys.begin(), keys.end(),
            [&](const auto& key) { return key.frame == request.fromFrame; });
        if (source == keys.end())
            return Result::failure(animationFailure(api::ErrorCode::NotFound,
                QStringLiteral("源关键帧不存在。"), "fromFrame"));
        if (request.fromFrame != request.toFrame) {
            const bool occupied = std::any_of(keys.begin(), keys.end(),
                [&](const auto& key) { return key.frame == request.toFrame; });
            if (occupied && request.onConflict == api::AnimationConflict::Reject)
                return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                    QStringLiteral("目标帧已有关键帧；需明确 replace。"), "toFrame"));
            auto value = *source;
            value.frame = request.toFrame;
            std::erase_if(keys, [&](const auto& key) {
                return key.frame == request.fromFrame || key.frame == request.toFrame;
            });
            const auto insert = std::lower_bound(keys.begin(), keys.end(), request.toFrame,
                [](const auto& key, auto frame) { return key.frame < frame; });
            keys.insert(insert, value);
        }
        return commitAnimationDefinition(candidate, QStringLiteral("移动关键帧"), false, &request);
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未移动关键帧。")));
    }
}
} // namespace mini3d::editor
