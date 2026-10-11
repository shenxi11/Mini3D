/*
 * 模块名: SceneAnimationEditing
 * 功能概述: 正式动画候选通过数值预检后进入唯一撤销栈，prepared内容在版本推进后通知。
 * 对外接口: SceneViewModel::replaceAnimation、prepared历史通知适配。
 * 依赖关系: SceneViewModel、Core动画求值、Qt Undo。
 * 输入输出: 完整动画定义到精确before/after历史；不修改基础TRS。
 * 异常与错误: 非法候选、超预算、过期来源及内存不足均保持旧内容和历史。
 * 维护说明: 精确no_change在push前返回；预检只检查当前时间和改变的键/新邻接端点。
 */
#include "AnimationEditing.h"
#include "EditCommand.h"
#include "SceneViewModel.h"
#include "core/EvaluatedPose.h"

#include <QSignalBlocker>
#include <QDeadlineTimer>
#include <QScopeGuard>
#include <algorithm>
#include <map>
#include <new>
#include <set>
#include <utility>

namespace mini3d::editor {
namespace {
std::set<core::OwnerRef> changedPropertyOwners(const core::SceneAnimation& before,
                                              const core::SceneAnimation& after) {
    const auto resolve = [](const core::PropertyAnimation& animation) {
        std::map<core::PropertyAddress, const core::ScalarCurve*> curves;
        for (const auto& binding : animation.bindings)
            for (const auto& action : animation.actions) {
                if (action.id != binding.action)
                    continue;
                for (const auto& slot : action.actionSlots) {
                    if (slot.id != binding.slot)
                        continue;
                    for (const auto& curve : slot.curves)
                        curves.emplace(core::PropertyAddress{binding.documentUuid, binding.owner,
                            curve.property, curve.component}, &curve);
                }
            }
        return curves;
    };
    const auto oldCurves = resolve(before.properties), newCurves = resolve(after.properties);
    std::set<core::OwnerRef> result;
    const auto collect = [&](const auto& current, const auto& other) {
        for (const auto& [address, curve] : current) {
            const auto found = other.find(address);
            if (found == other.end() || *found->second != *curve)
                result.insert(address.owner);
        }
    };
    collect(oldCurves, newCurves);
    collect(newCurves, oldCurves);
    return result;
}

bool preflightAnimationEdit(const std::vector<core::AnimationPoseInput>& inputs,
                            const core::SceneAnimation& before, const core::SceneAnimation& after,
                            core::FrameTime currentFrame, QString& error,
                            const std::function<bool(core::FrameTime, QString&)>& preparePose,
                            api::ErrorCode* failureCode = nullptr) {
    if (failureCode)
        *failureCode = api::ErrorCode::UnsupportedTransform;
    const auto frames = collectAnimationEditTimes(before, after, currentFrame, inputs.size());
    if (!core::validateAnimationPreflightBudget(inputs.size(), frames.size()).isValid()) {
        error = QStringLiteral("动画编辑的姿态预检超出40000节点访问预算。");
        if (failureCode)
            *failureCode = api::ErrorCode::LimitExceeded;
        return false;
    }
    for (const auto frame : frames) {
        if (!preparePose(frame, error))
            return false;
    }
    return true;
}
} // namespace
std::optional<api::ApiError>
SceneViewModel::preflightSubtreeAnimation(const core::Scene::PreparedSubtree& prepared,
                                          bool present, std::optional<core::EntityId> selection) {
    pendingAnimationReplay_.reset();
    const auto& before = present ? prepared.animationBefore() : prepared.animationAfter();
    const auto& after = present ? prepared.animationAfter() : prepared.animationBefore();
    if (animationMode_ == AnimationMode::Base && before.tracks.empty() && after.tracks.empty())
        return std::nullopt;
    std::string diagnostic;
    auto inputs = scene_->animationPoseInputs(prepared, present, diagnostic);
    QString error;
    auto code = api::ErrorCode::LimitExceeded;
    if (inputs) {
        const auto retained = captureSubtreeGeometry(prepared);
        const auto geometry = preparePoseGeometry(*inputs, retained, &prepared.entityIdMap(),
                                                   std::nullopt, nullptr, nullptr, selection);
        std::shared_ptr<renderer_gl::InstalledPose> current;
        const auto prepare = [&](core::FrameTime frame, QString& message) {
            auto pose = prepareAnimationPose(*inputs, after, frame,
                                             AnimationMode::PreviewPaused, geometry, message);
            if (frame == animationFrame_)
                current = pose;
            return bool(pose);
        };
        if (preflightAnimationEdit(*inputs, before, after, animationFrame_, error, prepare, &code)) {
            if (animationMode_ != AnimationMode::Base) {
                pendingAnimationReplay_ =
                    PreparedAnimationReplay{std::move(current), std::move(*inputs), true,
                                             animationMode_, animationSessionRevision_, animationFrame_};
                pendingAnimationReplay_->pose->prepareSceneGeometryBinding();
                // merge每次新建候选；已安装与历史retained几何仅作只读来源。
                Q_ASSERT(!installedAnimationPose_ || geometry != installedAnimationPose_->geometry);
                Q_ASSERT(geometry != retained);
                pendingAnimationReplay_->geometryToBind =
                    std::const_pointer_cast<renderer_gl::PoseGeometry>(geometry);
            }
            return std::nullopt;
        }
    }
    return api::ApiError{code, inputs ? error : QString::fromStdString(diagnostic), "entityId",
                         api::Recovery::CorrectInput, apiDocumentState()};
}
std::optional<api::ApiError>
SceneViewModel::preflightParentAnimation(const core::Scene::PreparedParentChange& prepared,
                                         bool forward) {
    pendingAnimationReplay_.reset();
    if (animationMode_ == AnimationMode::Base && scene_->animation().tracks.empty())
        return std::nullopt;
    std::string diagnostic;
    auto inputs = scene_->animationPoseInputs(prepared, forward, diagnostic);
    QString error;
    auto code = api::ErrorCode::LimitExceeded;
    if (inputs) {
        const auto geometry = preparePoseGeometry(*inputs);
        std::shared_ptr<renderer_gl::InstalledPose> current;
        const auto prepare = [&](core::FrameTime frame, QString& message) {
            auto pose = prepareAnimationPose(*inputs, scene_->animation(), frame,
                                             AnimationMode::PreviewPaused, geometry, message);
            if (frame == animationFrame_)
                current = pose;
            return bool(pose);
        };
        if (preflightAnimationEdit(*inputs, scene_->animation(), scene_->animation(),
                                   animationFrame_, error, prepare, &code)) {
            if (animationMode_ != AnimationMode::Base)
                pendingAnimationReplay_ =
                    PreparedAnimationReplay{std::move(current), std::move(*inputs), true,
                                             animationMode_, animationSessionRevision_, animationFrame_};
            return std::nullopt;
        }
    }
    return api::ApiError{code, inputs ? error : QString::fromStdString(diagnostic), "parentId",
                         api::Recovery::CorrectInput, apiDocumentState()};
}
void SceneViewModel::queueHistoryNotifications(bool structure,
                                               std::optional<core::EntityId> selection) {
    historySceneNotification_ = true;
    historyStructureNotification_ |= structure;
    if (selection)
        historySelection_ = selection;
}
void SceneViewModel::flushHistoryNotifications() {
    const bool sceneChanged = std::exchange(historySceneNotification_, false);
    const bool structureChanged = std::exchange(historyStructureNotification_, false);
    const auto selection = std::exchange(historySelection_, std::nullopt);
    const bool documentChanged = std::exchange(historyDocumentNotification_, false);
    const bool lastOperationChanged = std::exchange(historyLastOperationNotification_, false);
    const bool poseChanged = std::exchange(animationPoseNotification_, false);
    const bool sessionChanged = std::exchange(animationSessionNotification_, false);
    const bool previewCameraChanged = std::exchange(historyPreviewCameraNotification_, false);
    const bool viewportVisibilityChanged = std::exchange(historyViewportVisibilityNotification_, false);
    const bool componentSelectionChanged = std::exchange(historyComponentSelectionNotification_, false);
    const auto entities = historyEntities_;
    const auto entityCount = std::exchange(historyEntityCount_, 0);
    const auto previousSelection = selection_.selectedEntity();
    if (selection) {
        // 先安装有效选区，树结束reset后才通知选区消费者，避免使用重置中的QModelIndex。
        const QSignalBlocker blocker(&selection_);
        selection_.installSelectedEntity(*selection);
    }
    if (structureChanged)
        emit this->structureChanged();
    if (previewCameraChanged)
        emit this->previewCameraChanged(previewCamera_);
    if (viewportVisibilityChanged)
        emit this->viewportVisibilityChanged();
    if (poseChanged)
        emit animationPoseChanged();
    if (sessionChanged)
        emit animationSessionChanged();
    if (selection_.selectedEntity() != previousSelection)
        emit selection_.selectedEntityChanged(selection_.selectedEntity());
    if (componentSelectionChanged)
        emit this->componentSelectionChanged();
    if (sceneChanged) {
        emit animationChanged();
        emit this->sceneChanged();
    }
    for (std::size_t index = 0; index < entityCount; ++index)
        emit entityChanged(entities[index]);
    if (documentChanged)
        emit this->documentChanged();
    if (lastOperationChanged)
        emit this->lastOperationChanged();
}
void SceneViewModel::queueEntityNotification(core::EntityId entity) {
    if (apiSubmitting_) {
        const auto end = historyEntities_.begin() + historyEntityCount_;
        if (std::find(historyEntities_.begin(), end, entity) == end &&
            historyEntityCount_ < historyEntities_.size())
            historyEntities_[historyEntityCount_++] = entity;
        queueHistoryNotifications(false);
    } else {
        emit entityChanged(entity);
        emit sceneChanged();
    }
}
bool SceneViewModel::replaceAnimation(const core::SceneAnimation& animation, const QString& label) {
    const auto result = commitAnimationDefinition(animation, label, false);
    if (result.error)
        emit operationFailed(result.error->message);
    return result.hasValue();
}
api::ApiResult<api::MutationResult>
SceneViewModel::commitAnimationDefinition(const core::SceneAnimation& animation,
                                          const QString& label, bool fromDraft,
                                          const api::MutationRequest* request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (request) {
        if (const auto failure = validateApiMutation(*request))
            return Result::failure(*failure);
    }
    QDeadlineTimer deadline;
    if (request)
        deadline.setRemainingTime(request->timeoutMs, Qt::PreciseTimer);
    if ((!fromDraft && (apiSubmitting_ || isEditMode() ||
                        animationMode_ == AnimationMode::Playing ||
                        animationMode_ == AnimationMode::PoseDraft)) ||
        (fromDraft && (animationMode_ != AnimationMode::PoseDraft || !animationDraft_ ||
                       apiSubmitting_ || externalBusy_.contains(QStringLiteral("animation_gesture")))))
        return Result::failure(animationFailure(api::ErrorCode::Busy,
                                                 QStringLiteral("当前模式不能修改正式动画。")));
    pendingAnimationReplay_.reset();
    try {
        api::MutationResult result;
        result.state = apiDocumentState();
        if (animation.tracks.size() > core::kAnimationMaximumTracks)
            return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
                QStringLiteral("动画轨道超过预算。"), "items"));
        std::size_t keyCount = 0;
        for (const auto& [id, track] : animation.tracks) {
            if (track.keys.size() > core::kAnimationMaximumTrackKeys ||
                track.keys.size() > core::kAnimationMaximumKeys - keyCount)
                return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
                    QStringLiteral("动画关键帧超过预算。"), "items"));
            keyCount += track.keys.size();
        }
        if (const auto failure = validateAnimationScalarBudget(animation, apiDocumentState()))
            return Result::failure(*failure);
        const auto source = animationPreparationSource();
        const auto sourceFailure = [&] {
            if (request) {
                if (const auto failure = validateApiMutation(*request))
                    return *failure;
            }
            return animationFailure(api::ErrorCode::RevisionConflict,
                QStringLiteral("动画定义准备期间来源或交互准入已变化。"));
        };
        if (!matchesAnimationPreparationSource(source))
            return Result::failure(sourceFailure());
        std::string diagnostic;
        auto candidate = scene_->prepareAnimation(animation, diagnostic);
        if (!candidate)
            return Result::failure(animationFailure(api::ErrorCode::InvalidArgument,
                                                     QString::fromStdString(diagnostic)));
        if (!candidate->hasChanges()) {
            if (const auto failure = checkBeforeCommit(source, nullptr, nullptr, request))
                return Result::failure(*failure);
            if (request) {
                if (const auto failure = validateApiMutation(*request))
                    return Result::failure(*failure);
                if (deadline.hasExpired())
                    return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
                        QStringLiteral("动画请求超过截止时间，未发布候选。")));
            }
            if (!scene_->canInstallPreparedAnimation(*candidate))
                return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                    QStringLiteral("动画候选来源或 ID 水位已变化，未修改历史。")));
            if (fromDraft) {
                auto formal = std::move(animationDraft_->formalPose);
                animationDraft_.reset();
                animationMode_ = AnimationMode::PreviewPaused;
                pendingAnimationReplay_ = PreparedAnimationReplay{std::move(formal), {}, false};
                publishPreparedAnimationPose();
                advanceAnimationSession();
                flushHistoryNotifications();
                emit apiStateChanged();
            }
            return Result::success(std::move(result));
        }
        auto inputs = scene_->animationPoseInputs(diagnostic);
        QString error;
        std::shared_ptr<renderer_gl::InstalledPose> current;
        auto geometry = inputs ? preparePoseGeometry(*inputs) : nullptr;
        const auto prepare = [&](core::FrameTime frame, QString& message) {
            auto pose = prepareAnimationPose(*inputs, candidate->after(), frame,
                                             AnimationMode::PreviewPaused, geometry, message);
            if (frame == animationFrame_)
                current = pose;
            return bool(pose);
        };
        auto failureCode = api::ErrorCode::UnsupportedTransform;
        if (!inputs)
            return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
                QString::fromStdString(diagnostic)));
        if (!preflightAnimationEdit(*inputs, candidate->before(), candidate->after(),
                                               animationFrame_, error, prepare, &failureCode)) {
            if (!matchesAnimationPreparationSource(source))
                return Result::failure(sourceFailure());
            return Result::failure(animationFailure(failureCode,
                error));
        }
        for (const auto& [id, track] : candidate->before().tracks) {
            const auto found = candidate->after().tracks.find(id);
            if (found == candidate->after().tracks.end() || found->second != track)
                result.affectedEntityIds.push_back(id.first);
        }
        for (const auto& [id, track] : candidate->after().tracks) {
            const auto found = candidate->before().tracks.find(id);
            if (found == candidate->before().tracks.end() || found->second != track)
                result.affectedEntityIds.push_back(id.first);
        }
        for (const auto& owner : changedPropertyOwners(candidate->before(), candidate->after())) {
            if (const auto* entity = std::get_if<core::EntityOwner>(&owner))
                result.affectedEntityIds.push_back(entity->entity);
            else {
                const auto material = std::get<core::MaterialOwner>(owner).material;
                for (const auto& node : scene_->nodes())
                    if (std::any_of(node.materialSlots.begin(), node.materialSlots.end(),
                        [&](const auto& slot) { return slot.material == material; }))
                        result.affectedEntityIds.push_back(node.id);
            }
        }
        std::sort(result.affectedEntityIds.begin(), result.affectedEntityIds.end());
        result.affectedEntityIds.erase(
            std::unique(result.affectedEntityIds.begin(), result.affectedEntityIds.end()),
            result.affectedEntityIds.end());
        result.status = api::ResultStatus::Committed;
        result.undoable = true;
        auto prepared = std::make_shared<core::Scene::PreparedAnimation>(std::move(*candidate));
        const auto apply = [this, prepared](bool forward) {
            const bool installed = forward ? scene_->installPreparedAnimation(*prepared)
                                           : scene_->restorePreparedAnimation(*prepared);
            Q_ASSERT(installed);
            queueHistoryNotifications(false);
        };
        auto command = std::make_unique<EditCommand>(
            label,
            [apply] {
                apply(false);
            },
            [apply] {
                apply(true);
            },
            [this, prepared](bool forward, QString& error) {
                std::string diagnostic;
                auto inputs = scene_->animationPoseInputs(diagnostic);
                if (!inputs) {
                    error = QString::fromStdString(diagnostic);
                    return std::optional<PreparedAnimationReplay>{};
                }
                auto geometry = preparePoseGeometry(*inputs);
                return prepareAnimationReplay(std::move(*inputs),
                                               forward ? prepared->after() : prepared->before(),
                                               std::move(geometry), error);
            });
        auto pendingReplayCleanup = qScopeGuard([this] {
            pendingAnimationReplay_.reset();
            pendingAnimationCommand_ = nullptr;
        });
        if (animationMode_ != AnimationMode::Base) {
            pendingAnimationReplay_ =
                PreparedAnimationReplay{std::move(current), std::move(*inputs), true,
                                         animationMode_, animationSessionRevision_, animationFrame_};
            pendingAnimationCommand_ = command.get();
        }
        if (const auto failure = checkBeforeCommit(source, nullptr, nullptr, request))
            return Result::failure(*failure);
        if (request) {
            if (const auto failure = validateApiMutation(*request))
                return Result::failure(*failure);
            if (deadline.hasExpired())
                return Result::failure(animationFailure(api::ErrorCode::DeadlineExceeded,
                    QStringLiteral("动画请求超过截止时间，未发布候选。")));
        }
        if (!scene_->canInstallPreparedAnimation(*prepared)) {
            return Result::failure(animationFailure(api::ErrorCode::RevisionConflict,
                QStringLiteral("动画候选来源已变化，未修改历史。")));
        }
        if (fromDraft) {
            animationDraft_.reset();
            animationMode_ = AnimationMode::PreviewPaused;
            advanceAnimationSession();
        }
        pendingReplayCleanup.dismiss();
        pushHistory(command.release());
        result.state = apiDocumentState();
        return Result::success(std::move(result));
    } catch (const std::bad_alloc&) {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
        return Result::failure(animationFailure(api::ErrorCode::LimitExceeded,
                                                 QStringLiteral("内存不足，未提交动画候选。")));
    }
}
} // namespace mini3d::editor
