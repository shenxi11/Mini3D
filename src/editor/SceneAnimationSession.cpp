/*
 * 模块名: SceneAnimationSession
 * 功能概述: 管理独立动画会话、单调播放时钟、姿态草稿与原子观察相机导航。
 * 对外接口: SceneViewModel动画控制、草稿、录键与显示包装查询。
 * 依赖关系: Core动画求值、Renderer冻结几何、Qt Timer/ElapsedTimer。
 * 输入输出: 用户意图到不可变显示姿态；tick不写文档、基础TRS或历史。
 * 异常与错误: 候选失败保留旧姿态；播放故障停钟并退出预览，历史故障另行恢复。
 * 维护说明: 应用线程串行；计时直接来自elapsed，tick复用基础输入与局部几何盒。
 */
#include "SceneViewModel.h"

#include "api/MeshApiSupport.h"
#include "renderer_gl/RayCaster.h"

#include <QDeadlineTimer>
#include <QScopedValueRollback>
#include <algorithm>
#include <cmath>
#include <new>
#include <unordered_set>

namespace mini3d::editor {
namespace {
// 仅标识 Provider 获取或调用失败，不把几何等候选分配故障降为无视图许可。
struct AnimationViewAllocationFailure final : std::bad_alloc {};

constexpr std::array<core::AnimationChannel, 3> animationChannels{
    core::AnimationChannel::Position, core::AnimationChannel::RotationEulerXYZDegrees,
    core::AnimationChannel::Scale};

bool hasChannel(const std::array<bool, 3>& mask) {
    return std::any_of(mask.begin(), mask.end(), [](bool enabled) { return enabled; });
}

void upsertKey(core::SceneAnimation& animation, core::EntityId entity,
               core::AnimationChannel channel, std::uint32_t frame, const glm::dvec3& value,
               core::AnimationInterpolation interpolation) {
    auto& keys = animation.tracks[{entity, channel}].keys;
    const auto found = std::lower_bound(keys.begin(), keys.end(), frame,
                                        [](const auto& key, auto time) { return key.frame < time; });
    const core::AnimationKeyframe key{frame, value, interpolation};
    if (found != keys.end() && found->frame == frame)
        *found = key;
    else
        keys.insert(found, key);
}
} // namespace

AnimationMode SceneViewModel::animationMode() const {
    return animationMode_;
}
core::FrameTime SceneViewModel::animationFrame() const {
    return animationFrame_;
}
std::uint64_t SceneViewModel::animationEvaluationId() const {
    return animationEvaluationId_;
}
std::uint64_t SceneViewModel::animationSessionRevision() const {
    return animationSessionRevision_;
}
bool SceneViewModel::isAnimationLoopEnabled() const {
    return animationLoop_;
}
std::shared_ptr<const renderer_gl::InstalledPose> SceneViewModel::installedAnimationPose() const {
    if (apiSubmitting_ || animationMode_ == AnimationMode::Base || !installedAnimationPose_)
        return {};
    const auto& identity = installedAnimationPose_->identity;
    const auto& state = apiDocumentState();
    if (identity.instanceId != state.document.instanceId ||
        identity.documentId != state.document.documentId ||
        identity.sourceRevision != state.documentRevision || identity.frame != animationFrame_ ||
        identity.mode != animationMode_ || identity.evaluationId != animationEvaluationId_)
        return {};
    return installedAnimationPose_;
}

renderer_gl::EditorCamera SceneViewModel::animationView() const {
    try {
        const auto provider = animationViewProvider_;
        if (provider)
            return provider();
    } catch (const std::bad_alloc&) {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
        throw AnimationViewAllocationFailure{};
    }
    renderer_gl::EditorCamera result;
    result.setState(editorCamera_);
    return result;
}
void SceneViewModel::setAnimationViewProvider(
    std::function<renderer_gl::EditorCamera()> provider) {
    animationViewProvider_ = std::move(provider);
}

SceneViewModel::AnimationPreparationSource SceneViewModel::animationPreparationState() const {
    AnimationPreparationSource source;
    source.document = apiDocumentState();
    source.mode = animationMode_;
    source.frame = animationFrame_;
    source.sessionRevision = animationSessionRevision_;
    source.evaluationId = animationEvaluationId_;
    source.pose = installedAnimationPose_;
    // 活动姿态持有不可变掩码；tick 不复制临时隐藏集合。
    if (!source.pose)
        source.visibility = viewportVisibility_;
    source.selection = selection_.selectedEntity();
    source.previewCamera = previewCamera_;
    source.editedEntity = editedEntity_;
    source.objectGesture = transformEdit_.has_value();
    source.componentGesture = componentTransform_.has_value();
    source.submitting = apiSubmitting_;
    source.busy = externalBusy_;
    source.pivot = transformPivot_;
    source.editorCamera = editorCamera_;
    return source;
}

SceneViewModel::AnimationPreparationSource SceneViewModel::animationPreparationSource() const {
    auto source = animationPreparationState();
    // Provider 可以重入；其前的身份与包装不能在回调后改贴当前来源。
    source.view = animationView();
    return source;
}

bool SceneViewModel::matchesAnimationPreparationSource(const AnimationPreparationSource& source,
                                                       bool* viewFailed) const {
    const auto fieldsMatch = [&] {
        const auto& state = apiDocumentState();
        const auto& visibility = source.pose ? source.pose->geometry->visibility : source.visibility;
        return source.document.document == state.document &&
               source.document.documentRevision == state.documentRevision &&
               source.document.historyRevision == state.historyRevision &&
               source.mode == animationMode_ && source.frame == animationFrame_ &&
               source.sessionRevision == animationSessionRevision_ &&
               source.evaluationId == animationEvaluationId_ && source.pose == installedAnimationPose_ &&
               source.selection == selection_.selectedEntity() && source.previewCamera == previewCamera_ &&
               source.editedEntity == editedEntity_ && source.objectGesture == transformEdit_.has_value() &&
               source.componentGesture == componentTransform_.has_value() &&
               source.submitting == apiSubmitting_ && source.busy == externalBusy_ &&
               source.pivot == transformPivot_ && source.editorCamera == editorCamera_ &&
               visibility.hiddenObjects == viewportVisibility_.hiddenObjects &&
               visibility.localRoot == viewportVisibility_.localRoot &&
               visibility.editedEntity == viewportVisibility_.editedEntity &&
               visibility.vertices == viewportVisibility_.vertices &&
               visibility.edges == viewportVisibility_.edges && visibility.faces == viewportVisibility_.faces;
    };
    const auto reject = [this] {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
        return false;
    };
    if (!fieldsMatch())
        return reject();
    // 历史恢复已放弃姿态候选时，不再调用已失败的 Provider；非视图凭据仍全部检查。
    if (viewFailed && *viewFailed)
        return true;
    renderer_gl::EditorCamera currentView;
    try {
        currentView = animationView();
    } catch (const AnimationViewAllocationFailure&) {
        if (!viewFailed)
            throw;
        *viewFailed = true;
        return fieldsMatch() ? true : reject();
    }
    if (!fieldsMatch() || source.view.viewMatrix() != currentView.viewMatrix() ||
        source.view.projectionMatrix() != currentView.projectionMatrix())
        return reject();
    return true;
}

bool SceneViewModel::permitAnimationCommit(const AnimationPreparationSource& source, bool* viewFailed) {
    if (const auto failure = checkBeforeCommit(source, viewFailed)) {
        emit operationFailed(failure->message);
        return false;
    }
    return true;
}

std::shared_ptr<renderer_gl::InstalledPose> SceneViewModel::prepareAnimationPose(
    std::span<const core::AnimationPoseInput> inputs, const core::SceneAnimation& animation,
    core::FrameTime frame, AnimationMode mode,
    std::shared_ptr<const renderer_gl::PoseGeometry> geometry, QString& error,
    const renderer_gl::EditorCamera* view) const {
    const auto source = animationPreparationSource();
    if (!matchesAnimationPreparationSource(source)) {
        error = QStringLiteral("姿态准备期间来源或视图已变化。");
        return {};
    }
    auto sampled = core::evaluateAnimationPose(inputs, animation, frame);
    if (!sampled.pose) {
        error = QStringLiteral("动画在第%1帧无法形成有效姿态（对象%2，错误%3）。")
                    .arg(frame)
                    .arg(sampled.validation.entity)
                    .arg(static_cast<int>(sampled.validation.error));
        return {};
    }
    auto preview = source.previewCamera;
    if (geometry && preview) {
        const auto camera = geometry->entries.find(preview);
        if (camera == geometry->entries.end() || !camera->second.visible)
            preview = 0;
    }
    if (!geometry ||
        !renderer_gl::validatePoseGeometry(*sampled.pose, *geometry,
                                           view ? *view : source.view, preview, error)) {
        if (error.isEmpty())
            error = QStringLiteral("动画几何快照不完整。");
        return {};
    }
    if (!matchesAnimationPreparationSource(source)) {
        error = QStringLiteral("姿态准备期间来源或视图已变化。");
        return {};
    }
    auto result = std::make_shared<renderer_gl::InstalledPose>();
    const auto& state = source.document;
    result->identity = {state.document.instanceId, state.document.documentId,
                        state.documentRevision, 0, frame, mode};
    result->numerics = std::make_shared<const core::EvaluatedPose>(std::move(*sampled.pose));
    result->geometry = std::move(geometry);
    return result;
}

std::optional<PreparedAnimationReplay> SceneViewModel::prepareAnimationReplay(
    std::vector<core::AnimationPoseInput> inputs, const core::SceneAnimation& animation,
    std::shared_ptr<const renderer_gl::PoseGeometry> geometry, QString& error) const {
    const auto source = animationPreparationSource();
    if (!matchesAnimationPreparationSource(source)) {
        error = QStringLiteral("历史姿态准备期间来源已变化。");
        return std::nullopt;
    }
    auto pose = prepareAnimationPose(inputs, animation, source.frame,
                                     AnimationMode::PreviewPaused, std::move(geometry), error);
    if (!pose || !matchesAnimationPreparationSource(source)) {
        if (error.isEmpty())
            error = QStringLiteral("历史姿态准备期间来源已变化。");
        return std::nullopt;
    }
    return PreparedAnimationReplay{std::move(pose), std::move(inputs), true,
                                    source.mode, source.sessionRevision, source.frame};
}

std::optional<PreparedAnimationReplay> SceneViewModel::prepareSharedAnimationReplay(
    QString& error, std::shared_ptr<const renderer_gl::PoseGeometry> geometry) const {
    const auto source = animationPreparationSource();
    if (!source.pose || !source.pose->numerics || !matchesAnimationPreparationSource(source)) {
        error = QStringLiteral("没有可复用的完整正式姿态。");
        return std::nullopt;
    }
    if (!geometry)
        geometry = source.pose->geometry;
    auto preview = source.previewCamera;
    if (preview) {
        const auto camera = geometry->entries.find(preview);
        if (camera == geometry->entries.end() || !camera->second.visible)
            preview = 0;
    }
    if (!renderer_gl::validatePoseGeometry(*source.pose->numerics, *geometry,
                                           source.view, preview, error))
        return std::nullopt;
    if (!matchesAnimationPreparationSource(source)) {
        error = QStringLiteral("共享姿态准备期间来源或视图已变化。");
        return std::nullopt;
    }
    auto pose = std::make_shared<renderer_gl::InstalledPose>(*source.pose);
    pose->geometry = std::move(geometry);
    return PreparedAnimationReplay{std::move(pose), {}, false,
                                    source.mode, source.sessionRevision, source.frame};
}

bool SceneViewModel::stageSharedAnimationPose(
    QString& error, std::shared_ptr<const renderer_gl::PoseGeometry> geometry) {
    pendingAnimationReplay_.reset();
    pendingAnimationCommand_ = nullptr;
    if (animationMode_ == AnimationMode::Base)
        return true;
    pendingAnimationReplay_ = prepareSharedAnimationReplay(error, std::move(geometry));
    return pendingAnimationReplay_.has_value();
}

void SceneViewModel::publishPreparedAnimationPose() {
    if (!pendingAnimationReplay_)
        return;
    auto prepared = std::move(*pendingAnimationReplay_);
    pendingAnimationReplay_.reset();
    if (animationMode_ == AnimationMode::Base)
        return;
    const auto& state = apiDocumentState();
    auto& identity = prepared.pose->identity;
    Q_ASSERT(identity.instanceId == state.document.instanceId &&
             identity.documentId == state.document.documentId);
    identity.sourceRevision = state.documentRevision;
    identity.evaluationId = ++animationEvaluationId_;
    identity.mode = animationMode_;
    animationFrame_ = identity.frame;
    if (prepared.replacesInputs)
        animationInputs_ = std::move(prepared.inputs);
    if (prepared.geometryToBind) {
        Q_ASSERT(prepared.geometryToBind.get() == prepared.pose->geometry.get());
        Q_ASSERT(!installedAnimationPose_ ||
                 prepared.geometryToBind.get() != installedAnimationPose_->geometry.get());
        for (auto& [entity, entry] : prepared.geometryToBind->entries) {
            if (!entry.content)
                continue;
            const auto* node = scene_->find(entity);
            const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
            Q_ASSERT(record && record->content == entry.content);
            entry.evaluationRevision = record->evaluationRevision;
        }
        prepared.geometryToBind.reset();
    }
    installedAnimationPose_ = std::move(prepared.pose);
    const auto localRoot = installedAnimationPose_->geometry->visibility.localRoot;
    if (viewportVisibility_.localRoot != localRoot) {
        viewportVisibility_.localRoot = localRoot;
        historyViewportVisibilityNotification_ = true;
    }
    if (previewCamera_) {
        const auto camera = installedAnimationPose_->geometry->entries.find(previewCamera_);
        if (camera == installedAnimationPose_->geometry->entries.end() || !camera->second.visible) {
            previewCamera_ = 0;
            historyPreviewCameraNotification_ = true;
        }
    }
    animationPoseNotification_ = true;
}

void SceneViewModel::advanceAnimationSession() {
    ++animationSessionRevision_;
    animationSessionNotification_ = true;
}

void SceneViewModel::clearAnimationPreview(const QString& diagnostic) {
    animationTimer_.stop();
    animationClock_.invalidate();
    animationDraft_.reset();
    pendingAnimationReplay_.reset();
    installedAnimationPose_.reset();
    animationInputs_.clear();
    if (animationMode_ != AnimationMode::Base) {
        animationMode_ = AnimationMode::Base;
        advanceAnimationSession();
        animationPoseNotification_ = true;
    }
    if (!diagnostic.isEmpty())
        emit operationFailed(diagnostic);
}

bool SceneViewModel::rejectAnimationEdit(bool requiresBase) {
    if (apiSubmitting_ || animationMode_ == AnimationMode::Playing ||
        animationMode_ == AnimationMode::PoseDraft ||
        (requiresBase && animationMode_ != AnimationMode::Base)) {
        emit operationFailed(animationMode_ == AnimationMode::PreviewPaused
                                 ? QStringLiteral("请先关闭动画预览，再编辑基础变换或几何。")
                                 : QStringLiteral("请先暂停播放或结束姿态草稿，再执行此操作。"));
        return true;
    }
    return false;
}
bool SceneViewModel::rejectAnimationSessionChange() const {
    return apiSubmitting_ || transformEdit_ || componentTransform_ || isEditMode() ||
           !externalBusy_.isEmpty();
}

api::AnimationControllerState SceneViewModel::animationControllerState() const {
    api::AnimationControllerState result;
    switch (animationMode_) {
        case AnimationMode::Base: result.mode = QStringLiteral("base"); break;
        case AnimationMode::PreviewPaused: result.mode = QStringLiteral("preview_paused"); break;
        case AnimationMode::Playing: result.mode = QStringLiteral("playing"); break;
        case AnimationMode::PoseDraft: result.mode = QStringLiteral("pose_draft"); break;
    }
    result.frame = animationFrame_;
    result.loop = animationLoop_;
    result.sessionRevision = animationSessionRevision_;
    result.evaluationId = animationEvaluationId_;
    return result;
}
api::ApiError SceneViewModel::animationFailure(api::ErrorCode code, const QString& message,
                                               const QString& field) const {
    const auto recovery = code == api::ErrorCode::Busy ? api::Recovery::Wait
        : (code == api::ErrorCode::RevisionConflict || code == api::ErrorCode::StaleDocument)
            ? api::Recovery::Refetch
        : code == api::ErrorCode::DeadlineExceeded ? api::Recovery::QueryResult
        : code == api::ErrorCode::Internal ? api::Recovery::None : api::Recovery::CorrectInput;
    return {code, message, field, recovery, apiDocumentState()};
}
api::AnimationControlRequest SceneViewModel::animationControlRequest() const {
    api::AnimationControlRequest request;
    request.document = apiDocumentState().document;
    request.expectedDocumentRevision = apiDocumentState().documentRevision;
    request.expectedSessionRevision = animationSessionRevision_;
    return request;
}
std::optional<api::ApiError>
SceneViewModel::validateAnimationControl(const api::AnimationControlRequest& request) const {
    if (request.document != apiDocumentState().document)
        return animationFailure(api::ErrorCode::StaleDocument, QStringLiteral("文档句柄已失效。"), "document");
    if (request.expectedDocumentRevision != apiDocumentState().documentRevision)
        return animationFailure(api::ErrorCode::RevisionConflict, QStringLiteral("文档版本已变化。"), "expectedDocumentRevision");
    if (request.expectedSessionRevision != animationSessionRevision_)
        return animationFailure(api::ErrorCode::RevisionConflict, QStringLiteral("动画会话已变化。"), "expectedSessionRevision");
    if (!apiBusyReasons(false).isEmpty())
        return animationFailure(api::ErrorCode::Busy, QStringLiteral("请先结束当前交互并返回对象模式。"));
    return api::checkMeshEnvelope(request, apiDocumentState());
}
api::AnimationControlResult SceneViewModel::animationControlResult(
    bool changed, std::optional<api::ApiError> diagnostic) const {
    api::AnimationControlResult result;
    static_cast<api::AnimationControllerState&>(result) = animationControllerState();
    result.state = apiDocumentState();
    result.status = changed ? api::ResultStatus::Committed : api::ResultStatus::NoChange;
    result.diagnostic = std::move(diagnostic);
    return result;
}
bool SceneViewModel::reportAnimationControl(const api::ApiResult<api::AnimationControlResult>& result) {
    if (result.error)
        emit operationFailed(result.error->message);
    else if (result.value->diagnostic)
        emit operationFailed(result.value->diagnostic->message);
    return result.hasValue();
}
bool SceneViewModel::setAnimationPreview(bool enabled) {
    api::AnimationSetPreviewRequest request;
    static_cast<api::AnimationControlRequest&>(request) = animationControlRequest();
    request.enabled = enabled;
    return reportAnimationControl(setAnimationPreviewExplicit(request));
}
api::ApiResult<api::AnimationControlResult>
SceneViewModel::setAnimationPreviewExplicit(const api::AnimationSetPreviewRequest& request) {
    using Result = api::ApiResult<api::AnimationControlResult>;
    if (const auto failure = validateAnimationControl(request))
        return Result::failure(*failure);
    if (animationMode_ == AnimationMode::Playing || animationMode_ == AnimationMode::PoseDraft)
        return Result::failure(animationFailure(api::ErrorCode::Busy,
            QStringLiteral("播放或姿态草稿期间不能切换动画预览。")));
    try {
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source))
            return Result::failure(validateAnimationControl(request).value_or(
                animationFailure(api::ErrorCode::RevisionConflict, QStringLiteral("预览准备期间来源已变化。"))));
        if ((request.enabled && animationMode_ == AnimationMode::PreviewPaused) ||
            (!request.enabled && animationMode_ == AnimationMode::Base)) {
            if (const auto failure = checkBeforeCommit(source, nullptr, &request))
                return Result::failure(*failure);
            return Result::success(animationControlResult(false));
        }
        if (rejectAnimationSessionChange())
            return Result::failure(animationFailure(api::ErrorCode::Busy,
                QStringLiteral("请先返回对象模式并结束当前手势，再切换动画预览。")));
        if (!request.enabled) {
            if (const auto failure = checkBeforeCommit(source, nullptr, &request))
                return Result::failure(*failure);
            clearAnimationPreview();
            flushHistoryNotifications();
            emit apiStateChanged();
            return Result::success(animationControlResult(true));
        }
        std::string diagnostic;
        auto inputs = scene_->animationPoseInputs(diagnostic);
        if (!inputs)
            return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
                QString::fromStdString(diagnostic)));
        QString error;
        auto geometry = renderer_gl::makePoseGeometry(*scene_, assets_, viewportVisibility_, *inputs);
        auto pose = prepareAnimationPose(*inputs, scene_->animation(), animationFrame_,
                                         AnimationMode::PreviewPaused, std::move(geometry), error);
        if (!pose) {
            if (!matchesAnimationPreparationSource(source))
                return Result::failure(validateAnimationControl(request).value_or(
                    animationFailure(api::ErrorCode::RevisionConflict,
                        QStringLiteral("预览求值期间来源已变化。"))));
            return Result::failure(animationFailure(api::ErrorCode::UnsupportedTransform, error));
        }
        if (const auto failure = checkBeforeCommit(source, nullptr, &request))
            return Result::failure(*failure);
        animationMode_ = AnimationMode::PreviewPaused;
        pendingAnimationReplay_ = PreparedAnimationReplay{std::move(pose), std::move(*inputs), true};
        publishPreparedAnimationPose();
        advanceAnimationSession();
        flushHistoryNotifications();
        emit apiStateChanged();
        return Result::success(animationControlResult(true));
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未切换动画预览。")));
    }
}

bool SceneViewModel::setAnimationFrame(core::FrameTime frame) {
    api::AnimationSetFrameRequest request;
    static_cast<api::AnimationControlRequest&>(request) = animationControlRequest();
    request.frame = frame;
    return reportAnimationControl(setAnimationFrameExplicit(request));
}
api::ApiResult<api::AnimationControlResult>
SceneViewModel::setAnimationFrameExplicit(const api::AnimationSetFrameRequest& request) {
    using Result = api::ApiResult<api::AnimationControlResult>;
    if (const auto failure = validateAnimationControl(request))
        return Result::failure(*failure);
    if (animationMode_ != AnimationMode::PreviewPaused || isEditMode())
        return Result::failure(animationFailure(animationMode_ == AnimationMode::Base
            ? api::ErrorCode::PreviewDisabled : api::ErrorCode::Busy,
            QStringLiteral("只有暂停预览时可以定位帧。")));
    if (!core::isAnimationFrameValid(request.frame))
        return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
            QStringLiteral("动画时间必须是1到100000之间的有限帧数。"), "frame"));
    try {
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source))
            return Result::failure(validateAnimationControl(request).value_or(
                animationFailure(api::ErrorCode::RevisionConflict, QStringLiteral("定位准备期间来源已变化。"))));
        if (request.frame == animationFrame_) {
            if (const auto failure = checkBeforeCommit(source, nullptr, &request))
                return Result::failure(*failure);
            return Result::success(animationControlResult(false));
        }
        QString error;
        auto pose = prepareAnimationPose(animationInputs_, scene_->animation(), request.frame,
                                         animationMode_, installedAnimationPose_->geometry, error);
        if (!pose) {
            if (!matchesAnimationPreparationSource(source))
                return Result::failure(validateAnimationControl(request).value_or(
                    animationFailure(api::ErrorCode::RevisionConflict,
                        QStringLiteral("跳帧求值期间来源已变化。"))));
            return Result::failure(animationFailure(api::ErrorCode::UnsupportedTransform, error));
        }
        if (const auto failure = checkBeforeCommit(source, nullptr, &request))
            return Result::failure(*failure);
        pendingAnimationReplay_ = PreparedAnimationReplay{std::move(pose), {}, false};
        publishPreparedAnimationPose();
        advanceAnimationSession();
        flushHistoryNotifications();
        emit apiStateChanged();
        return Result::success(animationControlResult(true));
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未定位动画帧。")));
    }
}

core::FrameTime SceneViewModel::elapsedAnimationFrame(bool* atEnd) const {
    const auto& settings = scene_->animation().settings;
    const double start = settings.startFrame;
    const double end = settings.endFrame;
    const auto elapsed = animationClock_.isValid() ? animationClock_.nsecsElapsed() / 1.0e9 : 0.0;
    const auto frame = animationPlayStart_ + elapsed * settings.fps;
    if (atEnd)
        *atEnd = !animationLoop_ && frame >= end;
    if (animationLoop_ && start != end)
        return start + std::fmod(frame - start, end - start);
    return std::min(frame, end);
}

bool SceneViewModel::playAnimation() {
    api::AnimationPlayRequest request;
    static_cast<api::AnimationControlRequest&>(request) = animationControlRequest();
    return reportAnimationControl(playAnimationExplicit(request));
}
api::ApiResult<api::AnimationControlResult>
SceneViewModel::playAnimationExplicit(const api::AnimationPlayRequest& request) {
    using Result = api::ApiResult<api::AnimationControlResult>;
    if (const auto failure = validateAnimationControl(request))
        return Result::failure(*failure);
    if (animationMode_ == AnimationMode::Base || animationMode_ == AnimationMode::PoseDraft)
        return Result::failure(animationFailure(animationMode_ == AnimationMode::Base
            ? api::ErrorCode::PreviewDisabled : api::ErrorCode::Busy,
            QStringLiteral("请先开启动画预览并结束姿态草稿。")));
    if (animationMode_ != AnimationMode::Playing && isEditMode())
        return Result::failure(animationFailure(api::ErrorCode::Busy,
            QStringLiteral("请先返回对象模式，再开始播放。")));
    try {
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source))
            return Result::failure(validateAnimationControl(request).value_or(
                animationFailure(api::ErrorCode::RevisionConflict, QStringLiteral("播放准备期间来源已变化。"))));
        const auto settings = scene_->animation().settings;
        if (animationMode_ == AnimationMode::Playing || settings.startFrame == settings.endFrame) {
            if (const auto failure = checkBeforeCommit(source, nullptr, &request))
                return Result::failure(*failure);
            return Result::success(animationControlResult(false));
        }
        auto frame = animationFrame_;
        if (frame < settings.startFrame || frame >= settings.endFrame)
            frame = settings.startFrame;
        QString error;
        auto pose = prepareAnimationPose(animationInputs_, scene_->animation(), frame,
                                         AnimationMode::Playing, installedAnimationPose_->geometry,
                                         error);
        if (!pose) {
            if (!matchesAnimationPreparationSource(source))
                return Result::failure(validateAnimationControl(request).value_or(
                    animationFailure(api::ErrorCode::RevisionConflict,
                        QStringLiteral("播放求值期间来源已变化。"))));
            return Result::failure(animationFailure(api::ErrorCode::UnsupportedTransform, error));
        }
        if (const auto failure = checkBeforeCommit(source, nullptr, &request))
            return Result::failure(*failure);
        animationMode_ = AnimationMode::Playing;
        animationPlayStart_ = frame;
        animationClock_.start();
        animationTimer_.start();
        pendingAnimationReplay_ = PreparedAnimationReplay{std::move(pose), {}, false};
        publishPreparedAnimationPose();
        advanceAnimationSession();
        flushHistoryNotifications();
        emit apiStateChanged();
        return Result::success(animationControlResult(true));
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未开始播放。")));
    }
}

bool SceneViewModel::pauseAnimation() {
    api::AnimationPauseRequest request;
    static_cast<api::AnimationControlRequest&>(request) = animationControlRequest();
    return reportAnimationControl(pauseAnimationExplicit(request));
}
api::ApiResult<api::AnimationControlResult>
SceneViewModel::pauseAnimationExplicit(const api::AnimationPauseRequest& request) {
    using Result = api::ApiResult<api::AnimationControlResult>;
    if (const auto failure = validateAnimationControl(request))
        return Result::failure(*failure);
    if (animationMode_ == AnimationMode::PoseDraft)
        return Result::failure(animationFailure(api::ErrorCode::Busy,
            QStringLiteral("请先结束姿态草稿。")));
    bool stopped = false;
    std::optional<api::ApiError> diagnostic;
    const QDeadlineTimer deadline(request.timeoutMs, Qt::PreciseTimer);
    try {
        auto source = animationPreparationState();
        bool viewFailed = false;
        try {
            source.view = animationView();
        } catch (const AnimationViewAllocationFailure&) {
            // 只放弃视图包装；实际 CAS/Busy/deadline/外部 guard 仍必须获准且只执行一次。
            viewFailed = true;
        }
        if (const auto failure = checkBeforeCommit(source, &viewFailed, &request))
            return Result::failure(*failure);
        if (animationMode_ != AnimationMode::Playing)
            return Result::success(animationControlResult(false));
        if (viewFailed) {
            stopped = true;
            clearAnimationPreview();
            diagnostic = animationFailure(api::ErrorCode::LimitExceeded,
                QStringLiteral("播放已停止，观察包装内存不足，已退出动画预览。"));
            flushHistoryNotifications();
            emit apiStateChanged();
            return Result::success(animationControlResult(true, std::move(diagnostic)));
        }
        const auto frame = elapsedAnimationFrame();
        // 准入后立即停钟；最后求值失败也不能让用户的暂停意图失效。
        animationTimer_.stop();
        animationClock_.invalidate();
        animationMode_ = AnimationMode::PreviewPaused;
        advanceAnimationSession();
        stopped = true;
        QString error;
        const auto pausedSource = animationPreparationSource();
        if (!matchesAnimationPreparationSource(pausedSource)) {
            clearAnimationPreview();
            diagnostic = animationFailure(api::ErrorCode::RevisionConflict,
                QStringLiteral("暂停准备期间来源已变化，已停止并退出预览。"));
            flushHistoryNotifications();
            emit apiStateChanged();
            return Result::success(animationControlResult(true, std::move(diagnostic)));
        }
        auto pose = prepareAnimationPose(animationInputs_, scene_->animation(), frame,
                                         animationMode_, pausedSource.pose->geometry, error);
        if (deadline.hasExpired() || !matchesAnimationPreparationSource(pausedSource)) {
            const bool expired = deadline.hasExpired();
            clearAnimationPreview();
            diagnostic = animationFailure(expired ? api::ErrorCode::DeadlineExceeded
                : api::ErrorCode::RevisionConflict,
                expired ? QStringLiteral("播放已停止，暂停姿态准备超过截止时间。")
                        : QStringLiteral("暂停准备期间来源已变化，已停止并退出预览。"));
            flushHistoryNotifications();
            emit apiStateChanged();
            return Result::success(animationControlResult(true, std::move(diagnostic)));
        }
        if (pose) {
            pendingAnimationReplay_ = PreparedAnimationReplay{std::move(pose), {}, false};
            publishPreparedAnimationPose();
        } else {
            // 保留最后有效帧并以暂停身份重新包装；失效时间只进入诊断。
            auto fallback = std::make_shared<renderer_gl::InstalledPose>(*pausedSource.pose);
            pendingAnimationReplay_ = PreparedAnimationReplay{std::move(fallback), {}, false};
            publishPreparedAnimationPose();
            diagnostic = animationFailure(api::ErrorCode::UnsupportedTransform, error);
        }
    } catch (const std::bad_alloc&) {
        if (!stopped)
            return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
                QStringLiteral("暂停未获准，内存不足，播放状态保持不变。")));
        // 不能留下Paused状态搭配Playing包装；无法包装时明确故障退出Base。
        clearAnimationPreview();
        diagnostic = animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，播放已停止并退出动画预览。"));
    }
    flushHistoryNotifications();
    emit apiStateChanged();
    return Result::success(animationControlResult(true, std::move(diagnostic)));
}

bool SceneViewModel::setAnimationLoop(bool enabled) {
    api::AnimationSetLoopRequest request;
    static_cast<api::AnimationControlRequest&>(request) = animationControlRequest();
    request.enabled = enabled;
    return reportAnimationControl(setAnimationLoopExplicit(request));
}
api::ApiResult<api::AnimationControlResult>
SceneViewModel::setAnimationLoopExplicit(const api::AnimationSetLoopRequest& request) {
    using Result = api::ApiResult<api::AnimationControlResult>;
    if (const auto failure = validateAnimationControl(request))
        return Result::failure(*failure);
    if (animationMode_ == AnimationMode::Playing || animationMode_ == AnimationMode::PoseDraft)
        return Result::failure(animationFailure(api::ErrorCode::Busy,
            QStringLiteral("请先暂停或结束草稿，再修改循环。")));
    try {
        const auto source = animationPreparationSource();
        if (const auto failure = checkBeforeCommit(source, nullptr, &request))
            return Result::failure(*failure);
        const bool changed = animationLoop_ != request.enabled;
        if (!changed)
            return Result::success(animationControlResult(false));
        animationLoop_ = request.enabled;
        advanceAnimationSession();
        flushHistoryNotifications();
        emit apiStateChanged();
        return Result::success(animationControlResult(true));
    } catch (const std::bad_alloc&) {
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
            QStringLiteral("内存不足，未修改循环。")));
    }
}

void SceneViewModel::animationTick() {
    if (animationMode_ != AnimationMode::Playing || apiSubmitting_)
        return;
    bool atEnd = false;
    const auto frame = elapsedAnimationFrame(&atEnd);
    try {
        QString error;
        if (frame != animationFrame_ || atEnd) {
            auto pose = prepareAnimationPose(animationInputs_, scene_->animation(), frame,
                                             atEnd ? AnimationMode::PreviewPaused : animationMode_,
                                             installedAnimationPose_->geometry, error);
            if (!pose) {
                clearAnimationPreview(error);
            } else {
                if (atEnd) {
                    animationTimer_.stop();
                    animationClock_.invalidate();
                    animationMode_ = AnimationMode::PreviewPaused;
                    advanceAnimationSession();
                }
                pendingAnimationReplay_ = PreparedAnimationReplay{std::move(pose), {}, false};
                publishPreparedAnimationPose();
            }
        }
    } catch (const std::bad_alloc&) {
        clearAnimationPreview(QStringLiteral("内存不足，动画播放已停止。"));
    }
    flushHistoryNotifications();
    if (animationMode_ != AnimationMode::Playing)
        emit apiStateChanged();
}

void SceneViewModel::suspendAnimation() {
    if (animationMode_ != AnimationMode::Playing)
        return;
    // 隐藏属于宿主停态，不经API许可；暂停到最后实际显示帧，恢复不补跑。
    animationTimer_.stop();
    animationClock_.invalidate();
    animationMode_ = AnimationMode::PreviewPaused;
    advanceAnimationSession();
    try {
        auto pose = std::make_shared<renderer_gl::InstalledPose>(*installedAnimationPose_);
        pendingAnimationReplay_ = PreparedAnimationReplay{std::move(pose), {}, false};
        publishPreparedAnimationPose();
    } catch (const std::bad_alloc&) {
        clearAnimationPreview(QStringLiteral("窗口已停止播放，暂停包装内存不足，已退出动画预览。"));
    }
    flushHistoryNotifications();
    emit apiStateChanged();
}

bool SceneViewModel::commitEditorCamera(const renderer_gl::EditorCamera& candidate,
                                       const std::function<void()>& installView) {
    if (apiSubmitting_ || animationMode_ == AnimationMode::PoseDraft ||
        !candidate.state().isValid()) {
        emit operationFailed(QStringLiteral("当前不能修改观察相机。"));
        return false;
    }
    try {
        pendingAnimationReplay_.reset();
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source)) {
            emit operationFailed(QStringLiteral("导航准备期间来源已变化。"));
            return false;
        }
        std::optional<PreparedAnimationReplay> prepared;
        if (animationMode_ != AnimationMode::Base) {
            const auto frame = animationMode_ == AnimationMode::Playing
                                   ? elapsedAnimationFrame()
                                   : animationFrame_;
            QString error;
            auto pose = prepareAnimationPose(animationInputs_, scene_->animation(), frame,
                                             animationMode_, installedAnimationPose_->geometry,
                                             error, &candidate);
            if (!pose) {
                emit operationFailed(error);
                return false;
            }
            prepared = PreparedAnimationReplay{std::move(pose), {}, false,
                                                 animationMode_, animationSessionRevision_,
                                                 animationFrame_};
        } else {
            // 静态导航沿既有观察相机合同，只检查实际矩阵，不施加动画节点预算。
            const core::EvaluatedPose numerics;
            renderer_gl::PoseGeometry geometry;
            geometry.assets = assets_;
            QString error;
            if (!renderer_gl::validatePoseGeometry(numerics, geometry, candidate, 0, error)) {
                emit operationFailed(error);
                return false;
            }
        }
        pendingAnimationReplay_ = std::move(prepared);
        if (!permitAnimationCommit(source))
            return false;
        QScopedValueRollback submitting(apiSubmitting_, true);
        const bool contentChanged = editorCamera_ != candidate.state();
        editorCamera_ = candidate.state();
        if (installView)
            installView();
        if (contentChanged) {
            historyDocumentNotification_ = true;
            recordApiCommit(true, false);
        } else {
            publishPreparedAnimationPose();
            apiSubmitting_ = false;
            flushHistoryNotifications();
        }
        return true;
    } catch (const std::bad_alloc&) {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
        emit operationFailed(QStringLiteral("内存不足，未修改观察相机。"));
        return false;
    }
}

std::optional<std::array<glm::dvec3, 3>> SceneViewModel::animationValues(
    core::EntityId entity, std::array<bool, 3> mask) const {
    const auto* node = scene_->find(entity);
    if (!node)
        return std::nullopt;
    const auto* evaluated = installedAnimationPose_ && animationMode_ != AnimationMode::Base
                                ? installedAnimationPose_->numerics->find(entity)
                                : nullptr;
    const auto& local = evaluated ? evaluated->local : node->transform;
    std::array<glm::dvec3, 3> result{glm::dvec3(local.position), glm::dvec3(0),
                                    glm::dvec3(local.scale)};
    for (std::size_t index = 0; index < mask.size(); ++index) {
        if (!mask[index])
            continue;
        const auto track = scene_->animation().tracks.find({entity, animationChannels[index]});
        if (track != scene_->animation().tracks.end() &&
            (animationMode_ != AnimationMode::Base || index == 1)) {
            result[index] = core::detail::sampleValidatedAnimationTrack(track->second, animationFrame_);
        } else if (index == 1) {
            const auto angles = core::canonicalEulerXYZDegrees(glm::dquat(local.rotation));
            if (!angles)
                return std::nullopt;
            result[1] = *angles;
        }
    }
    return result;
}

bool SceneViewModel::beginAnimationDraft(core::EntityId entity, std::array<bool, 3> mask) {
    if (animationMode_ != AnimationMode::PreviewPaused || rejectAnimationSessionChange() ||
        previewCamera_ != 0 || animationFrame_ != std::floor(animationFrame_) ||
        selection_.selectedEntity() != entity || !hasChannel(mask) ||
        !viewportVisibility_.isVisible(*scene_, entity)) {
        emit operationFailed(QStringLiteral("姿态草稿需要对象模式、整数帧、选中可见对象和普通编辑视图。"));
        return false;
    }
    try {
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source)) {
            emit operationFailed(QStringLiteral("草稿准备期间来源或选区已变化。"));
            return false;
        }
        const auto values = animationValues(entity, mask);
        if (!values) {
            emit operationFailed(QStringLiteral("无法取得通道值或规范基础旋转。"));
            return false;
        }
        auto formal = std::make_shared<renderer_gl::InstalledPose>(*source.pose);
        auto draft = std::make_shared<renderer_gl::InstalledPose>(*source.pose);
        if (!permitAnimationCommit(source))
            return false;
        animationDraft_ = AnimationDraft{entity, mask, *values, std::move(formal)};
        animationMode_ = AnimationMode::PoseDraft;
        pendingAnimationReplay_ = PreparedAnimationReplay{std::move(draft), {}, false};
        publishPreparedAnimationPose();
        advanceAnimationSession();
        flushHistoryNotifications();
        emit apiStateChanged();
        return true;
    } catch (const std::bad_alloc&) {
        emit operationFailed(QStringLiteral("内存不足，未开始姿态草稿。"));
        return false;
    }
}

std::optional<std::array<glm::dvec3, 3>> SceneViewModel::animationDraftValues() const {
    return animationDraft_ ? std::optional(animationDraft_->values) : std::nullopt;
}
std::array<bool, 3> SceneViewModel::animationDraftMask() const {
    return animationDraft_ ? animationDraft_->mask : std::array<bool, 3>{};
}

core::SceneAnimation SceneViewModel::draftAnimation() const {
    auto animation = scene_->animation();
    for (std::size_t index = 0; index < animationDraft_->mask.size(); ++index) {
        if (!animationDraft_->mask[index])
            continue;
        const auto channel = animationChannels[index];
        const auto frame = static_cast<std::uint32_t>(animationFrame_);
        auto interpolation = core::AnimationInterpolation::Linear;
        if (const auto track = animation.tracks.find({animationDraft_->entity, channel});
            track != animation.tracks.end()) {
            for (const auto& key : track->second.keys)
                if (key.frame == frame)
                    interpolation = key.interpolation;
        }
        upsertKey(animation, animationDraft_->entity, channel, frame,
                   animationDraft_->values[index], interpolation);
    }
    return animation;
}

bool SceneViewModel::setAnimationDraftChannel(core::AnimationChannel channel,
                                              const glm::dvec3& value) {
    const auto index = std::find(animationChannels.begin(), animationChannels.end(), channel);
    if (animationMode_ != AnimationMode::PoseDraft || !animationDraft_ || apiSubmitting_ ||
        index == animationChannels.end() ||
        !animationDraft_->mask[static_cast<std::size_t>(index - animationChannels.begin())]) {
        emit operationFailed(QStringLiteral("该通道不属于当前草稿的编辑许可。"));
        return false;
    }
    const auto offset = static_cast<std::size_t>(index - animationChannels.begin());
    if (!core::validateAnimationValue(channel, value).isValid()) {
        emit operationFailed(QStringLiteral("草稿通道数值无效。"));
        return false;
    }
    if (animationDraft_->values[offset] == value)
        return permitFileCommit(nullptr);
    try {
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source)) {
            emit operationFailed(QStringLiteral("草稿通道准备期间来源已变化。"));
            return false;
        }
        const auto previous = animationDraft_->values[offset];
        animationDraft_->values[offset] = value;
        core::SceneAnimation candidate;
        try {
            candidate = draftAnimation();
        } catch (...) {
            animationDraft_->values[offset] = previous;
            throw;
        }
        animationDraft_->values[offset] = previous;
        QString error;
        auto pose = prepareAnimationPose(animationInputs_, candidate, animationFrame_,
                                         AnimationMode::PoseDraft,
                                         installedAnimationPose_->geometry, error);
        if (!pose) {
            emit operationFailed(error);
            return false;
        }
        if (!permitAnimationCommit(source))
            return false;
        animationDraft_->values[offset] = value;
        pendingAnimationReplay_ = PreparedAnimationReplay{std::move(pose), {}, false};
        publishPreparedAnimationPose();
        advanceAnimationSession();
        flushHistoryNotifications();
        emit apiStateChanged();
        return true;
    } catch (const std::bad_alloc&) {
        emit operationFailed(QStringLiteral("内存不足，未修改草稿通道。"));
        return false;
    }
}

bool SceneViewModel::commitAnimationDraft() {
    if (animationMode_ != AnimationMode::PoseDraft || !animationDraft_ || apiSubmitting_ ||
        externalBusy_.contains(QStringLiteral("animation_gesture")))
        return false;
    try {
        const auto result = commitAnimationDefinition(draftAnimation(), QStringLiteral("确认姿态草稿"), true);
        if (result.error)
            emit operationFailed(result.error->message);
        return result.hasValue();
    } catch (const std::bad_alloc&) {
        emit operationFailed(QStringLiteral("内存不足，草稿仍可修正或取消。"));
        return false;
    }
}

void SceneViewModel::cancelAnimationDraft() {
    if (animationMode_ != AnimationMode::PoseDraft || !animationDraft_ || apiSubmitting_)
        return;
    try {
        const auto source = animationPreparationSource();
        if (!permitAnimationCommit(source))
            return;
        auto formal = std::move(animationDraft_->formalPose);
        animationDraft_.reset();
        animationMode_ = AnimationMode::PreviewPaused;
        pendingAnimationReplay_ = PreparedAnimationReplay{std::move(formal), {}, false};
        publishPreparedAnimationPose();
        advanceAnimationSession();
        flushHistoryNotifications();
        emit apiStateChanged();
    } catch (const std::bad_alloc&) {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
        emit operationFailed(QStringLiteral("内存不足，未取消姿态草稿。"));
    }
}

bool SceneViewModel::recordAnimationKeyframes(core::EntityId entity, std::array<bool, 3> mask,
                                              core::AnimationInterpolation interpolation) {
    if (rejectAnimationEdit() || isEditMode() || transformEdit_ || componentTransform_ ||
        animationFrame_ != std::floor(animationFrame_) || !hasChannel(mask))
        return false;
    try {
        const auto values = animationValues(entity, mask);
        if (!values) {
            emit operationFailed(QStringLiteral("无法录键：目标不存在或基础旋转不可逆解。"));
            return false;
        }
        auto candidate = scene_->animation();
        for (std::size_t index = 0; index < mask.size(); ++index)
            if (mask[index])
                upsertKey(candidate, entity, animationChannels[index],
                           static_cast<std::uint32_t>(animationFrame_), (*values)[index],
                           interpolation);
        return replaceAnimation(candidate, QStringLiteral("录制对象关键帧"));
    } catch (const std::bad_alloc&) {
        emit operationFailed(QStringLiteral("内存不足，未录制关键帧。"));
        return false;
    }
}

bool SceneViewModel::deleteAnimationKeyframe(core::EntityId entity, core::AnimationChannel channel,
                                            std::uint32_t frame) {
    if (rejectAnimationEdit())
        return false;
    auto candidate = scene_->animation();
    const auto track = candidate.tracks.find({entity, channel});
    if (track == candidate.tracks.end())
        return permitFileCommit(nullptr);
    std::erase_if(track->second.keys, [frame](const auto& key) { return key.frame == frame; });
    if (track->second.keys.empty())
        candidate.tracks.erase(track);
    return replaceAnimation(candidate, QStringLiteral("删除关键帧"));
}

bool SceneViewModel::moveAnimationKeyframe(core::EntityId entity, core::AnimationChannel channel,
                                          std::uint32_t from, std::uint32_t to) {
    if (rejectAnimationEdit() || !core::isAnimationFrameValid(to))
        return false;
    auto candidate = scene_->animation();
    const auto track = candidate.tracks.find({entity, channel});
    if (track == candidate.tracks.end())
        return false;
    auto& keys = track->second.keys;
    const auto source = std::find_if(keys.begin(), keys.end(),
                                     [from](const auto& key) { return key.frame == from; });
    if (source == keys.end())
        return false;
    if (from == to)
        return permitFileCommit(nullptr);
    if (std::any_of(keys.begin(), keys.end(), [to](const auto& key) { return key.frame == to; })) {
        emit operationFailed(QStringLiteral("目标时间已有关键帧，请显式编辑该键。"));
        return false;
    }
    source->frame = to;
    std::sort(keys.begin(), keys.end(), [](const auto& left, const auto& right) {
        return left.frame < right.frame;
    });
    return replaceAnimation(candidate, QStringLiteral("移动关键帧"));
}

bool SceneViewModel::setAnimationKeyframeInterpolation(
    core::EntityId entity, core::AnimationChannel channel, std::uint32_t frame,
    core::AnimationInterpolation interpolation) {
    if (rejectAnimationEdit())
        return false;
    auto candidate = scene_->animation();
    const auto track = candidate.tracks.find({entity, channel});
    if (track == candidate.tracks.end())
        return false;
    for (auto& key : track->second.keys) {
        if (key.frame == frame) {
            key.interpolation = interpolation;
            return replaceAnimation(candidate, QStringLiteral("修改关键帧插值"));
        }
    }
    return false;
}

std::shared_ptr<const renderer_gl::PoseGeometry> SceneViewModel::preparePoseGeometry(
    std::span<const core::AnimationPoseInput> inputs,
    std::shared_ptr<const renderer_gl::PoseGeometry> retained,
    const std::map<core::EntityId, core::EntityId>* copies,
    std::optional<std::pair<core::EntityId, bool>> visibility,
    const std::vector<core::SceneCollection>* collections,
    const core::ViewportVisibility* mask, std::optional<core::EntityId> selection) const {
    const auto& frozenVisibility = mask ? *mask : viewportVisibility_;
    auto current = installedAnimationPose_ ? installedAnimationPose_->geometry : nullptr;
    if (!current && !retained && !visibility && !collections)
        return renderer_gl::makePoseGeometry(*scene_, assets_, frozenVisibility, inputs);
    if (!current) {
        std::string error;
        const auto currentInputs = scene_->animationPoseInputs(error);
        if (!currentInputs)
            return {};
        current = renderer_gl::makePoseGeometry(*scene_, assets_, viewportVisibility_, *currentInputs);
        if (!current)
            return {};
    }
    auto result = std::make_shared<renderer_gl::PoseGeometry>();
    result->assets = assets_;
    result->visibility = frozenVisibility;
    result->entries.reserve(inputs.size());
    std::unordered_set<core::EntityId> collectionHidden;
    for (const auto& collection : collections ? *collections : scene_->collections())
        if (!collection.visible) {
            collectionHidden.insert(collection.members.begin(), collection.members.end());
            if (copies)
                for (const auto member : collection.members)
                    if (const auto copied = copies->find(member); copied != copies->end())
                        collectionHidden.insert(copied->second);
        }
    struct VisibilityState {
        bool ancestorsVisible = true;
        bool inLocal = false;
        bool sourceVisible = true;
    };
    std::unordered_map<core::EntityId, VisibilityState> states;
    states.reserve(inputs.size());
    for (const auto& input : inputs) {
        const renderer_gl::PoseGeometryEntry* source = nullptr;
        if (current) {
            const auto found = current->entries.find(input.entity);
            if (found != current->entries.end())
                source = &found->second;
        }
        if (!source && retained) {
            const auto found = retained->entries.find(input.entity);
            if (found != retained->entries.end())
                source = &found->second;
        }
        if (!source)
            return {};
        auto entry = *source;
        entry.entity = input.entity;
        entry.parent = input.parent;
        const auto* node = scene_->find(input.entity);
        auto localVisible = node ? node->visible : entry.visible;
        if (visibility && visibility->first == input.entity)
            localVisible = visibility->second;
        VisibilityState state{true, frozenVisibility.localRoot == 0};
        if (input.parent) {
            const auto parent = states.find(input.parent);
            if (parent == states.end())
                return {};
            state = parent->second;
        }
        state.ancestorsVisible = state.ancestorsVisible && localVisible &&
                                 !collectionHidden.contains(input.entity) &&
                                 !frozenVisibility.hiddenObjects.contains(input.entity);
        state.sourceVisible = state.sourceVisible && localVisible &&
                              !collectionHidden.contains(input.entity);
        state.inLocal = state.inLocal || input.entity == frozenVisibility.localRoot;
        entry.visible = state.ancestorsVisible && state.inLocal;
        states.emplace(input.entity, state);
        result->entries.emplace(input.entity, std::move(entry));
    }
    if (result->visibility.localRoot) {
        const auto root = states.find(result->visibility.localRoot);
        const auto selected = selection ? result->entries.find(*selection) : result->entries.end();
        if (root == states.end() || !root->second.sourceVisible ||
            (selection && *selection && selected != result->entries.end() && !selected->second.visible)) {
            result->visibility.localRoot = 0;
            for (auto& [entity, entry] : result->entries)
                entry.visible = states.at(entity).ancestorsVisible;
        }
    }
    return result;
}

bool SceneViewModel::commitViewportVisibility(core::ViewportVisibility candidate, bool fromSelection) {
    if ((animationMode_ != AnimationMode::Playing || !fromSelection) && rejectAnimationEdit())
        return false;
    if (apiSubmitting_)
        return false;
    const bool same = candidate.hiddenObjects == viewportVisibility_.hiddenObjects &&
                      candidate.localRoot == viewportVisibility_.localRoot &&
                      candidate.editedEntity == viewportVisibility_.editedEntity &&
                      candidate.vertices == viewportVisibility_.vertices &&
                      candidate.edges == viewportVisibility_.edges &&
                      candidate.faces == viewportVisibility_.faces;
    if (same)
        return permitFileCommit(nullptr);
    try {
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source)) {
            emit operationFailed(QStringLiteral("可见性准备期间来源已变化。"));
            return false;
        }
        QString error;
        auto geometry = animationMode_ == AnimationMode::Base
                            ? nullptr
                            : preparePoseGeometry(animationInputs_, {}, nullptr, std::nullopt,
                                                   nullptr, &candidate);
        if (animationMode_ != AnimationMode::Base && !geometry) {
            emit operationFailed(QStringLiteral("无法准备完整视口可见性快照。"));
            return false;
        }
        if (!stageSharedAnimationPose(error, std::move(geometry)) || !permitAnimationCommit(source)) {
            if (!error.isEmpty())
                emit operationFailed(error);
            return false;
        }
        viewportVisibility_ = std::move(candidate);
        publishPreparedAnimationPose();
        notifyViewportVisibility();
        flushHistoryNotifications();
        return true;
    } catch (const std::bad_alloc&) {
        pendingAnimationReplay_.reset();
        emit operationFailed(QStringLiteral("内存不足，未修改视口可见性。"));
        return false;
    }
}

std::shared_ptr<const renderer_gl::PoseGeometry> SceneViewModel::captureSubtreeGeometry(
    const core::Scene::PreparedSubtree& prepared) const {
    auto result = std::make_shared<renderer_gl::PoseGeometry>();
    result->assets = assets_;
    result->visibility = viewportVisibility_;
    result->entries.reserve(prepared.entityIds().size());
    std::unordered_set<core::EntityId> collectionHidden;
    for (const auto& collection : scene_->collections())
        if (!collection.visible)
            collectionHidden.insert(collection.members.begin(), collection.members.end());
    for (const auto& [original, target] : prepared.entityIdMap()) {
        const auto* node = scene_->find(original);
        if (!node)
            node = scene_->find(target);
        if (!node)
            continue;
        renderer_gl::PoseGeometryEntry entry;
        if (installedAnimationPose_) {
            const auto found = installedAnimationPose_->geometry->entries.find(node->id);
            if (found != installedAnimationPose_->geometry->entries.end())
                entry = found->second;
        }
        entry.entity = target;
        entry.parent = node->parent;
        // retain中的visible保存局部对象/集合许可；最终祖先及会话掩码在候选merge传播。
        entry.visible = node->visible && !collectionHidden.contains(node->id);
        entry.camera = node->camera;
        entry.light = node->light;
        entry.hasGeometry = node->editableMesh != 0 || node->meshRenderer.has_value() ||
                            node->primitive != core::PrimitiveKind::Empty;
        if (!installedAnimationPose_) {
            if (const auto* record = scene_->editableMesh(node->editableMesh)) {
                entry.content = record->content;
                entry.evaluationRevision = record->evaluationRevision;
                for (const auto& vertex : record->content->evaluatedMesh().vertices)
                    entry.localBounds.expand(vertex.position);
            } else {
                entry.localBounds = renderer_gl::RayCaster::localBounds(*node, *assets_);
            }
        }
        result->entries.emplace(target, std::move(entry));
    }
    return result;
}

std::optional<PreparedAnimationReplay> SceneViewModel::prepareTransformReplay(
    core::EntityId entity, const core::Transform& transform, QString& error) const {
    std::string diagnostic;
    auto inputs = scene_->animationPoseInputs(diagnostic);
    if (!inputs) {
        error = QString::fromStdString(diagnostic);
        return std::nullopt;
    }
    const auto found = std::find_if(inputs->begin(), inputs->end(),
                                    [entity](const auto& input) { return input.entity == entity; });
    if (found == inputs->end()) {
        error = QStringLiteral("历史目标对象不存在。");
        return std::nullopt;
    }
    found->base = transform;
    auto geometry = preparePoseGeometry(*inputs);
    return prepareAnimationReplay(std::move(*inputs), scene_->animation(), std::move(geometry), error);
}

std::optional<PreparedAnimationReplay> SceneViewModel::prepareSubtreeReplay(
    const core::Scene::PreparedSubtree& prepared, bool present,
    std::shared_ptr<const renderer_gl::PoseGeometry> retained, QString& error,
    std::optional<core::EntityId> selection) const {
    std::string diagnostic;
    auto inputs = scene_->animationPoseInputs(prepared, present, diagnostic);
    if (!inputs) {
        error = QString::fromStdString(diagnostic);
        return std::nullopt;
    }
    auto geometry = preparePoseGeometry(*inputs, retained, &prepared.entityIdMap(),
                                       std::nullopt, nullptr, nullptr, selection);
    auto replay = prepareAnimationReplay(std::move(*inputs),
                                        present ? prepared.animationAfter() : prepared.animationBefore(),
                                        geometry, error);
    if (replay) {
        Q_ASSERT(!installedAnimationPose_ || geometry != installedAnimationPose_->geometry);
        Q_ASSERT(geometry != retained);
        replay->geometryToBind = std::const_pointer_cast<renderer_gl::PoseGeometry>(geometry);
    }
    return replay;
}

std::optional<PreparedAnimationReplay> SceneViewModel::prepareParentReplay(
    const core::Scene::PreparedParentChange& prepared, bool forward, QString& error) const {
    std::string diagnostic;
    auto inputs = scene_->animationPoseInputs(prepared, forward, diagnostic);
    if (!inputs) {
        error = QString::fromStdString(diagnostic);
        return std::nullopt;
    }
    auto geometry = preparePoseGeometry(*inputs);
    return prepareAnimationReplay(std::move(*inputs), scene_->animation(), std::move(geometry), error);
}

void SceneViewModel::replayHistory(bool forward) {
    if (rejectAnimationEdit())
        return;
    const bool available = forward ? history_.canRedo() : history_.canUndo();
    if (!available)
        return;
    pendingAnimationReplay_.reset();
    pendingAnimationCommand_ = nullptr;
    QString diagnostic;
    std::optional<AnimationPreparationSource> source;
    bool viewFailed = false;
    try {
        // 先保留完整非视图来源，Provider 抛错也不能丢失历史恢复的准入凭据。
        source = animationPreparationState();
        source->view = animationView();
        if (!matchesAnimationPreparationSource(*source)) {
            emit operationFailed(QStringLiteral("历史准备期间来源已变化。"));
            return;
        }
        if (animationMode_ == AnimationMode::PreviewPaused) {
            const auto index = forward ? history_.index() : history_.index() - 1;
            const auto* command = dynamic_cast<const AnimationReplay*>(history_.command(index));
            if (!command || !command->supportsAnimationReplay()) {
                diagnostic = QStringLiteral("该历史尚无姿态候选适配，已关闭动画预览后恢复历史。");
            } else {
                pendingAnimationReplay_ = command->prepareReplay(forward, diagnostic);
                if (!pendingAnimationReplay_ && diagnostic.isEmpty())
                    diagnostic = QStringLiteral("当前时间的历史姿态无法准备，已关闭预览。");
            }
        }
    } catch (const AnimationViewAllocationFailure&) {
        viewFailed = true;
        diagnostic = QStringLiteral("历史姿态准备内存不足，已关闭预览后恢复历史。");
    } catch (const std::bad_alloc&) {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
        if (!source) {
            emit operationFailed(QStringLiteral("历史来源准备内存不足，未恢复历史。"));
            return;
        }
        diagnostic = QStringLiteral("历史姿态准备内存不足，已关闭预览后恢复历史。");
    }
    try {
        if (!permitAnimationCommit(*source, &viewFailed))
            return;
    } catch (const std::bad_alloc&) {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
        emit operationFailed(QStringLiteral("历史提交守卫内存不足，未恢复历史。"));
        return;
    }
    if (viewFailed)
        diagnostic = QStringLiteral("历史姿态准备内存不足，已关闭预览后恢复历史。");
    cancelTransformEdit();
    QScopedValueRollback submitting(apiSubmitting_, true);
    if (!diagnostic.isEmpty())
        clearAnimationPreview();
    if (forward)
        history_.redo();
    else
        history_.undo();
    recordApiCommit(true, true);
    if (!diagnostic.isEmpty())
        emit operationFailed(diagnostic);
}
} // namespace mini3d::editor
