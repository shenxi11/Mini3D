/*
 * 模块名: AnimationApiService
 * 功能概述: 提供正式动画分页、纯数值取样以及双 CAS 控制边界。
 * 对外接口: EditorApiService 的十四个原生动画入口。
 * 依赖关系: SceneViewModel、Core EvaluatedPose、Qt DeadlineTimer。
 * 输入输出: 显式 DTO 到正式定义快照或类型化事务结果。
 * 异常与错误: 身份/版本/动态 Busy/截止时间拒绝保持旧状态；成功停止故障用诊断表达。
 * 维护说明: 保留实际外部提交守卫，不复制动画数学或共享历史，不安装 sample 姿态。
 */
#include "EditorApiService.h"

#include "MeshApiSupport.h"
#include "core/EvaluatedPose.h"
#include "editor/SceneViewModel.h"

#include <QDeadlineTimer>
#include <algorithm>
#include <new>
#include <set>

namespace mini3d::editor::api {
namespace {
static_assert(limits::animationTracks == core::kAnimationMaximumTracks);
static_assert(limits::animationTotalKeyframes == core::kAnimationMaximumKeys);
static_assert(limits::animationTrackKeyframes == core::kAnimationMaximumTrackKeys);
static_assert(limits::animationPoseNodes == core::kAnimationMaximumPoseNodes);
static_assert(limits::animationPreflightVisits == core::kAnimationMaximumPreflightNodeVisits);

bool validChannel(core::AnimationChannel channel) {
    return channel == core::AnimationChannel::Position ||
           channel == core::AnimationChannel::RotationEulerXYZDegrees ||
           channel == core::AnimationChannel::Scale;
}
/** @brief 单次动画提交作用域；实际外部 guard 只执行一次，前后均重查业务准入。 */
class AnimationCommitScope final {
  public:
    AnimationCommitScope(EditorApiService& service, const MutationRequest& request,
                         std::function<std::optional<ApiError>()> validate)
        : service_(service), deadline_(request.timeoutMs, Qt::PreciseTimer), validate_(std::move(validate)) {
        BeforeCommitGuard guard = [this]() -> std::optional<ApiError> {
            if (const auto failure = validate_())
                return failure;
            if (deadline_.hasExpired())
                return expired();
            if (previous_) {
                if (const auto failure = previous_())
                    return failure;
            }
            if (const auto failure = validate_())
                return failure;
            return deadline_.hasExpired() ? std::optional<ApiError>{expired()} : std::nullopt;
        };
        previous_ = service_.exchangeBeforeCommitGuard(std::move(guard));
    }
    ~AnimationCommitScope() { service_.exchangeBeforeCommitGuard(std::move(previous_)); }

  private:
    ApiError expired() const {
        return {ErrorCode::DeadlineExceeded, QStringLiteral("动画请求超过截止时间，未发布候选。"),
                {}, Recovery::QueryResult, service_.documentState()};
    }
    EditorApiService& service_;
    QDeadlineTimer deadline_;
    std::function<std::optional<ApiError>()> validate_;
    BeforeCommitGuard previous_;
};
} // namespace

std::optional<ApiError> EditorApiService::checkAnimationMutation(
    const MutationRequest& request, std::optional<std::uint64_t> sessionRevision) const {
    if (const auto failure = checkContext(request.document, false, false, request.expectedDocumentRevision))
        return failure;
    if (sessionRevision && *sessionRevision != model_.animationSessionRevision())
        return error(ErrorCode::RevisionConflict, QStringLiteral("动画会话已变化，请重新查询。"),
                     "expectedSessionRevision", Recovery::Refetch);
    if (const auto failure = checkMeshEnvelope(request, documentState()))
        return failure;
    const auto reasons = busyReasons(!sessionRevision.has_value());
    if (!reasons.isEmpty())
        return error(ErrorCode::Busy, QStringLiteral("请先结束当前交互：%1").arg(reasons.join(", ")),
                     {}, Recovery::Wait);
    return std::nullopt;
}
std::optional<ApiError> EditorApiService::checkAnimationQuery(const AnimationQueryRequest& request) const {
    if (const auto failure = checkContext(request.document, false, true, request.expectedDocumentRevision))
        return failure;
    // 不重复调用外部 provider，但其重入新增的模型物理 Busy 仍须生效。
    if (const auto failure = checkContext(request.document, false, false, request.expectedDocumentRevision))
        return failure;
    const auto reasons = model_.apiBusyReasons(false);
    if (!reasons.isEmpty())
        return error(ErrorCode::Busy,
                     QStringLiteral("请先结束当前交互：%1").arg(reasons.join(", ")),
                     {}, Recovery::Wait);
    return std::nullopt;
}
ApiResult<AnimationStateResult> EditorApiService::animationState(const AnimationQueryRequest& request) const {
    using Result = ApiResult<AnimationStateResult>;
    if (const auto failure = checkAnimationQuery(request))
        return Result::failure(*failure);
    AnimationStateResult result;
    static_cast<AnimationControllerState&>(result) = model_.animationControllerState();
    result.state = documentState();
    result.settings = model_.scene()->animation().settings;
    return Result::success(std::move(result));
}
ApiResult<AnimationTrackPageResult> EditorApiService::animationTracks(const AnimationListTracksRequest& request) const {
    using Result = ApiResult<AnimationTrackPageResult>;
    if (const auto failure = checkAnimationQuery(request))
        return Result::failure(*failure);
    if (request.afterTrack && !request.expectedDocumentRevision)
        return Result::failure(error(ErrorCode::InvalidArgument, QStringLiteral("续页必须冻结内容版本。"), "expectedDocumentRevision", Recovery::CorrectInput));
    if (request.limit < 1 || request.limit > int(limits::sourcePageMaximum) ||
        (request.afterTrack && (!request.afterTrack->entityId ||
                                !validChannel(request.afterTrack->channel))) ||
        (request.entityId && (!*request.entityId ||
            (request.afterTrack && request.afterTrack->entityId != *request.entityId))))
        return Result::failure(error(ErrorCode::InvalidArgument, QStringLiteral("轨道分页参数或游标无效。"),
                                     request.afterTrack ? "afterTrack" : "limit", Recovery::CorrectInput));
    const auto scene = model_.scene();
    if (request.entityId && !scene->find(*request.entityId))
        return Result::failure(error(ErrorCode::NotFound, QStringLiteral("动画目标对象不存在。"), "entityId", Recovery::CorrectInput));
    try {
        AnimationTrackPageResult result;
        result.state = documentState();
        const auto& tracks = scene->animation().tracks;
        auto iterator = request.afterTrack ? tracks.upper_bound({request.afterTrack->entityId, request.afterTrack->channel})
            : request.entityId ? tracks.lower_bound({*request.entityId, core::AnimationChannel::Position}) : tracks.begin();
        for (; iterator != tracks.end(); ++iterator) {
            const auto& [id, track] = *iterator;
            if (request.entityId && id.first != *request.entityId)
                break;
            if (result.tracks.size() == std::size_t(request.limit)) {
                result.nextAfterTrack = static_cast<const AnimationTrackCursor&>(result.tracks.back());
                break;
            }
            AnimationTrackSummary summary;
            summary.entityId = id.first;
            summary.channel = id.second;
            summary.keyframeCount = track.keys.size();
            result.tracks.push_back(summary);
        }
        return Result::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result::failure(error(ErrorCode::LimitExceeded, QStringLiteral("内存不足，未返回轨道页。")));
    }
}
ApiResult<AnimationKeyframePageResult> EditorApiService::animationKeyframes(const AnimationReadKeyframesRequest& request) const {
    using Result = ApiResult<AnimationKeyframePageResult>;
    if (const auto failure = checkAnimationQuery(request))
        return Result::failure(*failure);
    if (request.afterFrame && !request.expectedDocumentRevision)
        return Result::failure(error(ErrorCode::InvalidArgument, QStringLiteral("续页必须冻结内容版本。"), "expectedDocumentRevision", Recovery::CorrectInput));
    if (!request.entityId || !validChannel(request.channel) || request.limit < 1 ||
        request.limit > int(limits::sourcePageMaximum) ||
        (request.afterFrame && !core::isAnimationFrameValid(*request.afterFrame)))
        return Result::failure(error(ErrorCode::InvalidArgument, QStringLiteral("关键帧分页参数无效。"),
                                     request.afterFrame ? "afterFrame" : "channel", Recovery::CorrectInput));
    const auto scene = model_.scene();
    if (!scene->find(request.entityId))
        return Result::failure(error(ErrorCode::NotFound, QStringLiteral("动画目标对象不存在。"), "entityId", Recovery::CorrectInput));
    try {
        AnimationKeyframePageResult result;
        result.state = documentState();
        result.entityId = request.entityId;
        result.channel = request.channel;
        const auto found = scene->animation().tracks.find({request.entityId, request.channel});
        if (found != scene->animation().tracks.end()) {
            const auto& keys = found->second.keys;
            auto iterator = request.afterFrame ? std::upper_bound(keys.begin(), keys.end(), *request.afterFrame,
                [](auto frame, const auto& key) { return frame < key.frame; }) : keys.begin();
            for (; iterator != keys.end(); ++iterator) {
                if (result.keyframes.size() == std::size_t(request.limit)) {
                    result.nextAfterFrame = result.keyframes.back().frame;
                    break;
                }
                result.keyframes.push_back(*iterator);
            }
        }
        return Result::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result::failure(error(ErrorCode::LimitExceeded, QStringLiteral("内存不足，未返回关键帧页。")));
    }
}
ApiResult<AnimationSampleResult> EditorApiService::sampleAnimation(const AnimationSampleRequest& request) const {
    using Result = ApiResult<AnimationSampleResult>;
    // snapshot Busy 只含真实基础交互和实际窗口活动，不因 Playing/PoseDraft 模式拒绝。
    if (const auto failure = checkAnimationQuery(request))
        return Result::failure(*failure);
    if (!core::isAnimationFrameValid(request.frame) || request.entityIds.empty())
        return Result::failure(error(ErrorCode::InvalidArgument, QStringLiteral("取样帧或实体集合无效。"),
                                     "frame", Recovery::CorrectInput));
    if (request.entityIds.size() > limits::animationSampleEntities)
        return Result::failure(error(ErrorCode::LimitExceeded, QStringLiteral("取样最多请求256个实体。"), "entityIds", Recovery::CorrectInput));
    try {
        const auto scene = model_.scene();
        std::set<core::EntityId> entities;
        for (const auto entity : request.entityIds) {
            if (!entity || !entities.insert(entity).second)
                return Result::failure(error(ErrorCode::InvalidArgument, QStringLiteral("取样实体无效或重复。"), "entityIds", Recovery::CorrectInput));
            if (!scene->find(entity))
                return Result::failure(error(ErrorCode::NotFound, QStringLiteral("取样实体不存在。"), "entityIds", Recovery::CorrectInput));
        }
        std::string diagnostic;
        const auto inputs = scene->animationPoseInputs(diagnostic);
        if (!inputs)
            return Result::failure(error(ErrorCode::LimitExceeded, QString::fromStdString(diagnostic)));
        const auto evaluated = core::evaluateAnimationPose(*inputs, scene->animation(), request.frame);
        if (!evaluated.pose)
            return Result::failure(error(ErrorCode::UnsupportedTransform, QStringLiteral("正式动画完整姿态求值失败。"), "frame", Recovery::CorrectInput));
        AnimationSampleResult result;
        result.state = documentState();
        result.frame = request.frame;
        result.entities.reserve(request.entityIds.size());
        for (const auto entity : request.entityIds) {
            const auto* node = evaluated.pose->find(entity);
            AnimationSampleEntity value;
            value.entityId = entity;
            value.localTransform = node->local;
            value.worldMatrix = node->world;
            value.rotationSource = node->rotationSource == core::AnimationRotationSource::AnimationTrack
                ? QStringLiteral("animationTrack") : QStringLiteral("baseQuaternion");
            value.rotationEulerXYZDegrees = node->rotationEulerXYZDegrees;
            result.entities.push_back(std::move(value));
        }
        return Result::success(std::move(result));
    } catch (const std::bad_alloc&) {
        return Result::failure(error(ErrorCode::LimitExceeded, QStringLiteral("内存不足，未返回取样姿态。")));
    }
}

ApiResult<MutationResult> EditorApiService::setAnimationSettings(const AnimationSetSettingsRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<MutationResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request); });
    if (const auto failure = checkAnimationMutation(request)) return ApiResult<MutationResult>::failure(*failure);
    return model_.setAnimationSettingsExplicit(request);
}
ApiResult<MutationResult> EditorApiService::upsertAnimationKeyframes(const AnimationUpsertKeyframesRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<MutationResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request); });
    if (const auto failure = checkAnimationMutation(request)) return ApiResult<MutationResult>::failure(*failure);
    return model_.upsertAnimationKeyframesExplicit(request);
}
ApiResult<MutationResult> EditorApiService::deleteAnimationKeyframes(const AnimationDeleteKeyframesRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<MutationResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request); });
    if (const auto failure = checkAnimationMutation(request)) return ApiResult<MutationResult>::failure(*failure);
    return model_.deleteAnimationKeyframesExplicit(request);
}
ApiResult<MutationResult> EditorApiService::removeAnimationTrack(const AnimationRemoveTrackRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<MutationResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request); });
    if (const auto failure = checkAnimationMutation(request)) return ApiResult<MutationResult>::failure(*failure);
    return model_.removeAnimationTrackExplicit(request);
}
ApiResult<MutationResult> EditorApiService::moveAnimationKeyframe(const AnimationMoveKeyframeRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<MutationResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request); });
    if (const auto failure = checkAnimationMutation(request)) return ApiResult<MutationResult>::failure(*failure);
    return model_.moveAnimationKeyframeExplicit(request);
}
ApiResult<AnimationControlResult> EditorApiService::setAnimationPreview(const AnimationSetPreviewRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<AnimationControlResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request, request.expectedSessionRevision); });
    if (const auto failure = checkAnimationMutation(request, request.expectedSessionRevision)) return ApiResult<AnimationControlResult>::failure(*failure);
    return model_.setAnimationPreviewExplicit(request);
}
ApiResult<AnimationControlResult> EditorApiService::setAnimationFrame(const AnimationSetFrameRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<AnimationControlResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request, request.expectedSessionRevision); });
    if (const auto failure = checkAnimationMutation(request, request.expectedSessionRevision)) return ApiResult<AnimationControlResult>::failure(*failure);
    return model_.setAnimationFrameExplicit(request);
}
ApiResult<AnimationControlResult> EditorApiService::playAnimation(const AnimationPlayRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<AnimationControlResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request, request.expectedSessionRevision); });
    if (const auto failure = checkAnimationMutation(request, request.expectedSessionRevision)) return ApiResult<AnimationControlResult>::failure(*failure);
    return model_.playAnimationExplicit(request);
}
ApiResult<AnimationControlResult> EditorApiService::pauseAnimation(const AnimationPauseRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<AnimationControlResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request, request.expectedSessionRevision); });
    if (const auto failure = checkAnimationMutation(request, request.expectedSessionRevision)) return ApiResult<AnimationControlResult>::failure(*failure);
    return model_.pauseAnimationExplicit(request);
}
ApiResult<AnimationControlResult> EditorApiService::setAnimationLoop(const AnimationSetLoopRequest& request) {
    if (const auto failure = checkThread()) return ApiResult<AnimationControlResult>::failure(*failure);
    AnimationCommitScope guard(*this, request, [&] { return checkAnimationMutation(request, request.expectedSessionRevision); });
    if (const auto failure = checkAnimationMutation(request, request.expectedSessionRevision)) return ApiResult<AnimationControlResult>::failure(*failure);
    return model_.setAnimationLoopExplicit(request);
}
} // namespace mini3d::editor::api
