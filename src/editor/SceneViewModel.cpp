/*
 * 模块名: SceneViewModel
 * 功能概述: 第三周场景选择与属性编辑的 SceneViewModel 层。
 * 对外接口: SceneViewModel
 * 依赖关系: Qt Widgets/Core、mini3d_core
 * 输入输出: 输入用户意图或场景通知，输出模型状态或界面刷新。
 * 异常与错误: 通过返回值和 operationFailed 报告非法编辑。
 * 维护说明: 同步 UI 线程操作；不持有节点地址或 GPU 资源。
 */
#include "SceneViewModel.h"

#include "EditCommand.h"
#include "SubtreeCommand.h"
#include "TransformEntityCommand.h"
#include "assets/ObjDocument.h"
#include "assets/SceneDocument.h"
#include "core/modeling/DeleteComponents.h"
#include "core/modeling/FillFace.h"
#include "core/modeling/ObjExporter.h"
#include "core/modeling/ProportionalTransform.h"
#include "core/modeling/TopologySelection.h"
#include "core/modeling/VertexTransform.h"
#include "renderer_gl/EditorCamera.h"
#include "renderer_gl/PrimitiveFactory.h"
#include "renderer_gl/RayCaster.h"

#include <QDebug>
#include <QFileInfo>
#include <QScopedValueRollback>
#include "api/MeshApiSupport.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <new>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
namespace mini3d::editor {
namespace {
bool sameTransform(const core::Transform& left, const core::Transform& right) {
    return left.position == right.position && left.rotation == right.rotation &&
           left.scale == right.scale;
}
std::set<core::modeling::FaceId> selectedFaceIds(const ComponentSelection& selection) {
    std::set<core::modeling::FaceId> result;
    for (const auto id : selection.selectedIds())
        result.insert(id.first);
    return result;
}
constexpr std::size_t maximumApiSubtreeEntities = 2048;
api::ApiResult<std::vector<core::EntityId>>
apiSubtreeIds(const core::Scene& scene, core::EntityId root, const api::DocumentState& state) {
    using Result = api::ApiResult<std::vector<core::EntityId>>;
    if (!scene.find(root))
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("对象不存在。"),
                                "entityId", api::Recovery::Refetch, state});
    std::vector<core::EntityId> pending{root}, result;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        result.push_back(id);
        const auto& children = scene.find(id)->children;
        if (result.size() + pending.size() + children.size() > maximumApiSubtreeEntities)
            return Result::failure({api::ErrorCode::LimitExceeded,
                                    QStringLiteral("子树最多允许 2048 个对象。"), "entityId",
                                    api::Recovery::CorrectInput, state});
        pending.insert(pending.end(), children.begin(), children.end());
    }
    // 复制映射及 created/affected 的最坏十进制 ID 输出在准备节点前核算。
    constexpr std::size_t resultBaseBytes = 1024, resultBytesPerEntity = 256;
    const auto resultBytes = resultBaseBytes + result.size() * resultBytesPerEntity;
    if (resultBytes > api::limits::responseBytes || resultBytes > api::limits::cachedResultBytes)
        return Result::failure({api::ErrorCode::LimitExceeded,
                                QStringLiteral("子树返回结果超出 API 预算。"), "entityId",
                                api::Recovery::CorrectInput, state});
    std::sort(result.begin(), result.end());
    return Result::success(std::move(result));
}
std::set<core::modeling::EdgeKey> apiSourceEdges(const core::modeling::EditableMesh& source) {
    std::set<core::modeling::EdgeKey> result;
    for (const auto& face : source.faces)
        for (std::size_t index = 0; index < face.corners.size(); ++index)
            result.emplace(face.corners[index].vertex,
                           face.corners[(index + 1) % face.corners.size()].vertex);
    return result;
}
struct ApiComponentTargets {
    std::set<core::modeling::VertexId> vertices;
    std::set<core::modeling::EdgeKey> edges;
    std::set<core::modeling::FaceId> faces;
};
api::ApiResult<ApiComponentTargets> apiComponentTargets(const core::modeling::EditableMesh& source,
                                                        const api::MeshComponentsRequest& request,
                                                        const api::DocumentState& state,
                                                        bool allowFaces) {
    using Result = api::ApiResult<ApiComponentTargets>;
    using Domain = api::MeshComponentDomain;
    const auto invalid = [&](const QString& field, const QString& message) {
        return Result::failure(api::meshArgumentError(state, field, message));
    };
    if (request.domain != Domain::Vertices && request.domain != Domain::Edges &&
        !(allowFaces && request.domain == Domain::Faces))
        return invalid("domain", QStringLiteral("组件域不属于当前算子支持范围。"));
    if ((request.domain != Domain::Vertices && request.vertexIds) ||
        (request.domain != Domain::Edges && request.edges) ||
        (request.domain != Domain::Faces && request.faceIds))
        return invalid("domain", QStringLiteral("只允许当前组件域的目标字段。"));
    const auto field = request.domain == Domain::Edges   ? QStringLiteral("edges")
                       : request.domain == Domain::Faces ? QStringLiteral("faceIds")
                                                         : QStringLiteral("vertexIds");
    const auto count = request.domain == Domain::Edges ? (request.edges ? request.edges->size() : 0)
                       : request.domain == Domain::Faces
                           ? (request.faceIds ? request.faceIds->size() : 0)
                           : (request.vertexIds ? request.vertexIds->size() : 0);
    if (count == 0)
        return invalid(field, QStringLiteral("至少显式指定一个源组件。"));
    if (count > api::limits::meshVertices)
        return Result::failure({api::ErrorCode::LimitExceeded,
                                QStringLiteral("组件目标数量超出 API 限额。"), field,
                                api::Recovery::CorrectInput, state});
    const auto notFound = [&] {
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("指定源组件不存在。"),
                                field, api::Recovery::Refetch, state});
    };
    ApiComponentTargets result;
    if (request.domain == Domain::Vertices) {
        std::set<core::modeling::VertexId> available;
        for (const auto& vertex : source.vertices)
            available.insert(vertex.id);
        for (const auto id : *request.vertexIds) {
            if (id == 0 || !result.vertices.insert(id).second)
                return invalid(field, QStringLiteral("源组件 ID 必须非零且不能重复。"));
            if (!available.contains(id))
                return notFound();
        }
    } else if (request.domain == Domain::Edges) {
        const auto available = apiSourceEdges(source);
        for (const auto& edge : *request.edges) {
            const core::modeling::EdgeKey normalized(edge.first, edge.second);
            if (edge.first == 0 || edge.second == 0 || edge.first == edge.second ||
                !result.edges.insert(normalized).second)
                return invalid(field,
                               QStringLiteral("源边须有两个不同非零端点，且无向边不能重复。"));
            if (!available.contains(normalized))
                return notFound();
            result.vertices.insert(normalized.first);
            result.vertices.insert(normalized.second);
        }
    } else {
        std::map<core::modeling::FaceId, const core::modeling::EditableFace*> available;
        for (const auto& face : source.faces)
            available.emplace(face.id, &face);
        for (const auto id : *request.faceIds) {
            if (id == 0 || !result.faces.insert(id).second)
                return invalid(field, QStringLiteral("源组件 ID 必须非零且不能重复。"));
            const auto found = available.find(id);
            if (found == available.end())
                return notFound();
            for (const auto& corner : found->second->corners)
                result.vertices.insert(corner.vertex);
        }
    }
    return Result::success(std::move(result));
}
std::optional<api::ApiError> apiModelingResultBudget(std::size_t idCount, std::size_t edgeCount,
                                                     const api::DocumentState& state) {
    // uint64 最长 20 位；给键名、引号与分隔符保守留量，在候选发布前核算。
    const auto bytes = 1024 + idCount * 32 + edgeCount * 64;
    if (bytes > api::limits::responseBytes || bytes > api::limits::cachedResultBytes)
        return api::ApiError{api::ErrorCode::LimitExceeded,
                             QStringLiteral("建模结果超出 API 返回预算。"), "result",
                             api::Recovery::CorrectInput, state};
    return std::nullopt;
}
std::optional<api::ApiError> apiSourceEdge(const core::modeling::EditableMesh& source,
                                           core::modeling::EdgeKey edge, const QString& field,
                                           const api::DocumentState& state) {
    if (edge.first == 0 || edge.second == 0 || edge.first == edge.second)
        return api::meshArgumentError(state, field, QStringLiteral("源边须有两个不同非零端点。"));
    if (!apiSourceEdges(source).contains({edge.first, edge.second}))
        return api::ApiError{api::ErrorCode::NotFound, QStringLiteral("指定源边不存在。"), field,
                             api::Recovery::Refetch, state};
    return std::nullopt;
}
bool apiFiniteAffine(const glm::dmat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                return false;
    const auto determinant = glm::determinant(glm::dmat3(matrix));
    return matrix[0][3] == 0 && matrix[1][3] == 0 && matrix[2][3] == 0 && matrix[3][3] == 1 &&
           std::isfinite(determinant) && determinant != 0;
}
api::ErrorCode apiBatchFailureCode(core::Scene::BatchPrepareFailure failure) {
    switch (failure) {
        case core::Scene::BatchPrepareFailure::InvalidArgument:
            return api::ErrorCode::InvalidArgument;
        case core::Scene::BatchPrepareFailure::NotFound:
            return api::ErrorCode::NotFound;
        case core::Scene::BatchPrepareFailure::UnsupportedTransform:
            return api::ErrorCode::UnsupportedTransform;
        case core::Scene::BatchPrepareFailure::LimitExceeded:
            return api::ErrorCode::LimitExceeded;
    }
    Q_UNREACHABLE();
}
bool apiBatchBoundsFit(const core::Aabb& local, const glm::mat4& world) {
    if (!local.isValid())
        return true;
    // 逐角检查，避免 min/max 对 NaN 的处理掩盖实际溢出的角点。
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point((corner & 1) ? local.maximum.x : local.minimum.x,
                              (corner & 2) ? local.maximum.y : local.minimum.y,
                              (corner & 4) ? local.maximum.z : local.minimum.z);
        const auto transformed = world * glm::vec4(point, 1);
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(transformed[axis]))
                return false;
    }
    return true;
}
} // namespace
SceneViewModel::SceneViewModel(QObject* parent)
    : QObject(parent), scene_(std::make_shared<core::Scene>()), selection_(*scene_) {
    setObjectName(QStringLiteral("SceneViewModel"));
    editorCamera_ = renderer_gl::EditorCamera{}.state();
    savedCamera_ = editorCamera_;
    animationTimer_.setInterval(16);
    animationTimer_.setTimerType(Qt::PreciseTimer);
    connect(&animationTimer_, &QTimer::timeout, this, &SceneViewModel::animationTick);
    selection_.setSelectionGuard([this](core::EntityId id) {
        if (animationMode_ == AnimationMode::PoseDraft || apiSubmitting_) {
            emit operationFailed(QStringLiteral("姿态草稿或提交期间不能切换选区。"));
            return false;
        }
        if (id && viewportVisibility_.localRoot && !viewportVisibility_.isVisible(*scene_, id)) {
            auto visibility = viewportVisibility_;
            visibility.localRoot = 0;
            return commitViewportVisibility(std::move(visibility), true);
        }
        return true;
    });
    historyService_.setReplacementCommittedCallback([this] {
        recordApiCommit(true, true);
    });
    connect(this, &SceneViewModel::documentReset, this, [this] {
        lastPreviewCamera_ = core::kInvalidEntity;
        viewportVisibility_ = {};
        notifyViewportVisibility();
    });
    connect(&history_, &QUndoStack::cleanChanged, this, [this] {
        if (apiSubmitting_)
            historyDocumentNotification_ = true;
        else
            emit documentChanged();
    });
    connect(&history_, &QUndoStack::indexChanged, this, [this] {
        if (apiSubmitting_)
            historyLastOperationNotification_ = true;
        else
            emit lastOperationChanged();
    });
    connect(this, &SceneViewModel::editModeChanged, this, &SceneViewModel::lastOperationChanged);
    connect(this, &SceneViewModel::componentPreviewChanged, this,
            &SceneViewModel::lastOperationChanged);
    connect(&selection_, &SelectionModel::selectedEntityChanged, this,
            &SceneViewModel::cancelTransformEdit);
    connect(&selection_, &SelectionModel::selectedEntityChanged, this,
            &SceneViewModel::reconcileEditContext);
    connect(&selection_, &SelectionModel::selectedEntityChanged, this, [this] {
        const auto id = selection_.selectedEntity();
        if (animationMode_ == AnimationMode::Base && id && viewportVisibility_.localRoot &&
            !viewportVisibility_.isVisible(*scene_, id)) {
            viewportVisibility_.localRoot = 0;
            notifyViewportVisibility();
        }
    });
    connect(this, &SceneViewModel::sceneChanged, this, &SceneViewModel::reconcileEditContext);
    connect(this, &SceneViewModel::sceneChanged, this, [this] {
        finishComponentTransform(false);
        if (viewportVisibility_.localRoot && !scene_->isVisible(viewportVisibility_.localRoot)) {
            viewportVisibility_.localRoot = 0;
            notifyViewportVisibility();
        }
    });
    connect(this, &SceneViewModel::sceneChanged, this, [this] {
        if (previewCamera_ != 0 && !scene_->isVisible(previewCamera_)) {
            setPreviewCamera(0);
        }
    });
    const auto group = scene_->createEntity("示例");
    const auto cube = scene_->createEntity("立方体", group, core::PrimitiveKind::Cube);
    const auto sphere = scene_->createEntity("球体", group, core::PrimitiveKind::Sphere);
    const auto plane = scene_->createEntity("平面", group, core::PrimitiveKind::Plane);
    core::Transform transform;
    transform.position = {-1.5F, 0.5F, 0};
    scene_->setTransform(cube, transform);
    transform.position = {0, 0.5F, 0};
    scene_->setTransform(sphere, transform);
    transform.position = {1.6F, 0.02F, 0};
    scene_->setTransform(plane, transform);
}
std::shared_ptr<const core::Scene> SceneViewModel::scene() const {
    return scene_;
}
SceneViewModel::~SceneViewModel() {
    // QUndoStack 析构会改变 clean 状态，不向已经进入析构的窗口发布文档通知。
    disconnect(&history_, nullptr, this, nullptr);
}
SelectionModel* SceneViewModel::selection() {
    return &selection_;
}

const api::DocumentState& SceneViewModel::apiDocumentState() const {
    return apiDocumentState_.state();
}
api::BeforeCommitGuard SceneViewModel::exchangeBeforeCommitGuard(api::BeforeCommitGuard guard) {
    return std::exchange(beforeCommitGuard_, std::move(guard));
}
std::optional<api::ApiError> SceneViewModel::checkBeforeCommit() const {
    return checkBeforeCommit(animationPreparationSource());
}
std::optional<api::ApiError>
SceneViewModel::checkBeforeCommit(const AnimationPreparationSource& source, bool* viewFailed,
                                 const api::AnimationControlRequest* control,
                                 const api::MutationRequest* definition) const {
    const auto sourceFailure = [this, control, definition] {
        if (control) {
            if (const auto failure = validateAnimationControl(*control))
                return *failure;
        }
        if (definition) {
            if (const auto failure = validateApiMutation(*definition))
                return *failure;
        }
        return api::ApiError{api::ErrorCode::RevisionConflict,
                             QStringLiteral("姿态准备或提交守卫期间来源、视图或交互准入已变化。"), {},
                             api::Recovery::Refetch, apiDocumentState()};
    };
    if (!matchesAnimationPreparationSource(source, viewFailed)) {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
        return sourceFailure();
    }
    const auto prepared = pendingAnimationReplay_ ? pendingAnimationReplay_->pose : nullptr;
    const auto sourceMode = pendingAnimationReplay_ ? pendingAnimationReplay_->sourceMode
                                                   : AnimationMode::Base;
    const auto sourceSession = pendingAnimationReplay_ ? pendingAnimationReplay_->sourceSessionRevision
                                                      : 0;
    const auto sourceFrame = pendingAnimationReplay_ ? pendingAnimationReplay_->sourceFrame : 1;
    const auto guard = beforeCommitGuard_;
    auto failure = guard ? guard() : std::nullopt;
    if (!failure && !matchesAnimationPreparationSource(source, viewFailed))
        failure = sourceFailure();
    if (!failure && prepared && sourceSession && !(viewFailed && *viewFailed)) {
        const auto& identity = prepared->identity;
        const auto& state = apiDocumentState();
        if (identity.instanceId != state.document.instanceId ||
            identity.documentId != state.document.documentId ||
            identity.sourceRevision != state.documentRevision ||
            sourceFrame != animationFrame_ || sourceMode != animationMode_ ||
            sourceSession != animationSessionRevision_ ||
            !pendingAnimationReplay_ || pendingAnimationReplay_->pose != prepared)
            failure = api::ApiError{api::ErrorCode::RevisionConflict,
                                    QStringLiteral("姿态候选的文档、时间或会话来源已变化。"), {},
                                    api::Recovery::Refetch, state};
    }
    if (failure) {
        pendingAnimationReplay_.reset();
        pendingAnimationCommand_ = nullptr;
    }
    return failure;
}
bool SceneViewModel::permitFileCommit(std::optional<api::ApiError>* commitFailure) {
    if (const auto failure = checkBeforeCommit()) {
        if (commitFailure)
            *commitFailure = *failure;
        emit operationFailed(failure->message);
        return false;
    }
    return true;
}
QStringList SceneViewModel::apiBusyReasons(bool forMutation) const {
    QStringList reasons = externalBusy_.values();
    if (transformEdit_)
        reasons.append(QStringLiteral("object_transform"));
    if (componentTransform_)
        reasons.append(componentTransform_->loopCut ? QStringLiteral("loop_cut")
                                                    : QStringLiteral("component_preview"));
    if (apiSubmitting_)
        reasons.append(QStringLiteral("committing"));
    if (forMutation && isEditMode())
        reasons.append(QStringLiteral("edit_mode"));
    if (forMutation && previewCamera_ != 0)
        reasons.append(QStringLiteral("camera_preview"));
    if (forMutation && animationMode_ == AnimationMode::Playing)
        reasons.append(QStringLiteral("animation_playing"));
    if (forMutation && animationMode_ == AnimationMode::PoseDraft)
        reasons.append(QStringLiteral("animation_draft"));
    reasons.removeDuplicates();
    reasons.sort();
    return reasons;
}
void SceneViewModel::setExternalBusy(const QString& reason, bool busy) {
    if (reason.isEmpty())
        return;
    const bool changed = busy ? !externalBusy_.contains(reason) : externalBusy_.contains(reason);
    if (busy)
        externalBusy_.insert(reason);
    else
        externalBusy_.remove(reason);
    if (changed)
        emit apiStateChanged();
}
void SceneViewModel::recordApiCommit(bool contentChanged, bool historyChanged) {
    apiDocumentState_.recordCommit(contentChanged, historyChanged);
    publishPreparedAnimationPose();
    // 内容及包装都绑定正式版本后，才让同步消费者读取完整新结果。
    apiSubmitting_ = false;
    flushHistoryNotifications();
    emit apiStateChanged();
}
void SceneViewModel::pushHistory(QUndoCommand* command) {
    QScopedValueRollback submitting(apiSubmitting_, true);
    if (pendingAnimationCommand_ != command)
        pendingAnimationReplay_.reset();
    pendingAnimationCommand_ = nullptr;
    if (animationMode_ != AnimationMode::Base && !pendingAnimationReplay_)
        clearAnimationPreview(QStringLiteral("该编辑尚无姿态候选，已关闭动画预览。"));
    history_.push(command);
    recordApiCommit(true, true);
}

std::optional<api::ApiError>
SceneViewModel::validateApiMutation(const api::MutationRequest& request) const {
    const auto& state = apiDocumentState();
    if (request.document != state.document)
        return api::ApiError{api::ErrorCode::StaleDocument, QStringLiteral("文档句柄已失效。"),
                             QStringLiteral("document"), api::Recovery::Refetch, state};
    if (request.expectedDocumentRevision != state.documentRevision)
        return api::ApiError{api::ErrorCode::RevisionConflict, QStringLiteral("文档版本已变化。"),
                             QStringLiteral("expectedDocumentRevision"), api::Recovery::Refetch,
                             state};
    const auto reasons = apiBusyReasons();
    if (!reasons.isEmpty())
        return api::ApiError{api::ErrorCode::Busy,
                             QStringLiteral("请先结束当前交互：%1").arg(reasons.join(", ")),
                             {},
                             api::Recovery::Wait,
                             state};
    return api::checkMeshEnvelope(request, state);
}
api::ApiResult<api::MutationResult>
SceneViewModel::createEntityExplicit(const api::EntityCreateRequest& request) {
    if (const auto error = validateApiMutation(request))
        return api::ApiResult<api::MutationResult>::failure(*error);
    core::Scene::EntityCreateOptions options;
    options.name = request.name.toUtf8().toStdString();
    options.parent = request.parentId;
    options.primitive = request.primitive;
    options.transform = request.transform;
    options.surface = request.surface;
    return commitEntityCreate(options, false);
}
api::ApiResult<api::MutationResult>
SceneViewModel::createEntitiesExplicit(const api::BatchCreateEntitiesRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (animationMode_ != AnimationMode::Base)
        return Result::failure({api::ErrorCode::Busy, QStringLiteral("请先关闭动画预览再创建基础对象。"),
                                {}, api::Recovery::Wait, apiDocumentState()});
    const auto fail = [this](api::ErrorCode code, const QString& message, const QString& field) {
        const auto recovery =
            code == api::ErrorCode::NotFound || code == api::ErrorCode::RevisionConflict
                ? api::Recovery::Refetch
                : api::Recovery::CorrectInput;
        return Result::failure({code, message, field, recovery, apiDocumentState()});
    };
    if (request.items.empty())
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("创建批次不能为空。"), "items");
    if (request.items.size() > api::limits::batchItems)
        return fail(api::ErrorCode::LimitExceeded, QStringLiteral("创建批次最多允许 64 项。"),
                    "items");
    std::shared_ptr<core::Scene::PreparedEntityBatch> prepared;
    std::unique_ptr<EditCommand> command;
    api::MutationResult result;
    try {
        std::vector<core::Scene::EntityCreateOptions> options;
        options.reserve(request.items.size());
        for (std::size_t index = 0; index < request.items.size(); ++index) {
            const auto& item = request.items[index];
            const auto path = QStringLiteral("items[%1]").arg(index);
            if (item.name.trimmed().isEmpty() || item.name.toUcs4().size() > 256)
                return fail(api::ErrorCode::InvalidArgument,
                            QStringLiteral("名称须包含 1～256 个字符。"), path + ".name");
            if (item.parentId != 0 && !scene_->find(item.parentId))
                return fail(api::ErrorCode::NotFound, QStringLiteral("父对象不存在。"),
                            path + ".parentId");
            if (item.primitive != core::PrimitiveKind::Empty &&
                item.primitive != core::PrimitiveKind::Cube &&
                item.primitive != core::PrimitiveKind::Sphere &&
                item.primitive != core::PrimitiveKind::Plane)
                return fail(api::ErrorCode::InvalidArgument,
                            QStringLiteral("不支持的基础几何类型。"), path + ".primitive");
            if (!item.transform.isValid())
                return fail(api::ErrorCode::InvalidArgument, QStringLiteral("局部变换无效。"),
                            path + ".transform");
            if (!item.surface.isValid())
                return fail(api::ErrorCode::InvalidArgument,
                            QStringLiteral("表面颜色须在 0～1 之间。"), path + ".surface");
            core::Scene::EntityCreateOptions option;
            option.name = item.name.toUtf8().toStdString();
            option.parent = item.parentId;
            option.primitive = item.primitive;
            option.transform = item.transform;
            option.surface = item.surface;
            options.push_back(std::move(option));
        }
        std::string diagnostic;
        core::Scene::BatchPrepareFailure failure{};
        auto candidate = scene_->prepareEntityBatch(options, diagnostic, api::limits::batchItems,
                                                    api::limits::candidateBytes, &failure);
        if (!candidate)
            return fail(apiBatchFailureCode(failure), QString::fromStdString(diagnostic), "items");
        for (std::size_t index = 0; index < options.size(); ++index) {
            core::SceneNode node;
            node.primitive = options[index].primitive;
            const auto local = renderer_gl::RayCaster::localBounds(node, *assets_);
            if (!apiBatchBoundsFit(local, candidate->affectedWorldMatrices()[index].second))
                return fail(api::ErrorCode::UnsupportedTransform,
                            QStringLiteral("几何世界边界超出有限 float 范围。"),
                            QStringLiteral("items[%1].transform").arg(index));
        }
        if (const auto error =
                apiModelingResultBudget(candidate->entityIds().size() * 2, 0, apiDocumentState()))
            return Result::failure(*error);
        prepared = std::make_shared<core::Scene::PreparedEntityBatch>(std::move(*candidate));
        result.createdEntityIds = prepared->entityIds();
        result.affectedEntityIds = prepared->entityIds();
        result.status = api::ResultStatus::Committed;
        result.undoable = true;
        const auto apply = [this, prepared](bool forward) {
            emit structureAboutToChange();
            const bool installed = forward ? scene_->installPreparedEntityBatch(*prepared)
                                           : scene_->removePreparedEntityBatch(*prepared);
            Q_ASSERT(installed);
            std::optional<core::EntityId> selection;
            if (!forward && std::find(prepared->entityIds().begin(), prepared->entityIds().end(),
                                      selection_.selectedEntity()) != prepared->entityIds().end())
                selection = 0;
            queueHistoryNotifications(true, selection);
        };
        command = std::make_unique<EditCommand>(
            QStringLiteral("批量创建对象"),
            [apply] {
                apply(false);
            },
            [apply] {
                apply(true);
            });
        if (const auto error =
                checkBeforeCommit(animationPreparationSource(), nullptr, nullptr, &request))
            return Result::failure(*error);
        if (const auto error = validateApiMutation(request))
            return Result::failure(*error);
        if (!scene_->canInstallPreparedEntityBatch(*prepared))
            return fail(api::ErrorCode::RevisionConflict,
                        QStringLiteral("创建批次的父节点或来源已变化，请重新查询。"), "items");
    } catch (const std::bad_alloc&) {
        return fail(api::ErrorCode::LimitExceeded, QStringLiteral("创建批次准备内存不足。"),
                    "items");
    }
    // Qt 栈内部耗尽不属于可恢复事务；所有可恢复准备与许可都已在 push 前完成。
    pushHistory(command.release());
    result.state = apiDocumentState();
    return Result::success(std::move(result));
}
api::ApiResult<api::MutationResult>
SceneViewModel::setTransformsExplicit(const api::BatchSetTransformsRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (animationMode_ != AnimationMode::Base)
        return Result::failure({api::ErrorCode::Busy, QStringLiteral("请先关闭动画预览再编辑基础变换。"),
                                {}, api::Recovery::Wait, apiDocumentState()});
    const auto fail = [this](api::ErrorCode code, const QString& message, const QString& field) {
        const auto recovery =
            code == api::ErrorCode::NotFound || code == api::ErrorCode::RevisionConflict
                ? api::Recovery::Refetch
                : api::Recovery::CorrectInput;
        return Result::failure({code, message, field, recovery, apiDocumentState()});
    };
    if (request.items.empty())
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("变换批次不能为空。"), "items");
    if (request.items.size() > api::limits::batchItems)
        return fail(api::ErrorCode::LimitExceeded, QStringLiteral("变换批次最多允许 64 项。"),
                    "items");
    std::shared_ptr<core::Scene::PreparedTransformBatch> prepared;
    std::unique_ptr<EditCommand> command;
    api::MutationResult result;
    try {
        std::vector<core::Scene::TransformBatchItem> options;
        options.reserve(request.items.size());
        result.affectedEntityIds.reserve(request.items.size());
        for (std::size_t index = 0; index < request.items.size(); ++index) {
            const auto& item = request.items[index];
            const auto path = QStringLiteral("items[%1]").arg(index);
            if (item.entityId == 0 ||
                std::any_of(options.begin(), options.end(), [&](const auto& option) {
                    return option.entity == item.entityId;
                }))
                return fail(api::ErrorCode::InvalidArgument,
                            QStringLiteral("目标 ID 须非零且不能重复。"), path + ".entityId");
            const auto* node = scene_->find(item.entityId);
            if (!node)
                return fail(api::ErrorCode::NotFound, QStringLiteral("对象不存在。"),
                            path + ".entityId");
            if (!item.transform.isValid())
                return fail(api::ErrorCode::InvalidArgument, QStringLiteral("局部变换无效。"),
                            path + ".transform");
            auto after = item.transform;
            if (after.rotation != node->transform.rotation)
                after.rotation = glm::normalize(after.rotation);
            if (!sameTransform(node->transform, after))
                result.affectedEntityIds.push_back(item.entityId);
            options.push_back({item.entityId, item.transform});
        }
        std::string diagnostic;
        core::Scene::BatchPrepareFailure failure{};
        auto candidate = scene_->prepareTransformBatch(options, diagnostic, api::limits::batchItems,
                                                       api::limits::candidateBytes, &failure);
        if (!candidate)
            return fail(apiBatchFailureCode(failure), QString::fromStdString(diagnostic), "items");
        for (const auto& [id, world] : candidate->affectedWorldMatrices()) {
            const auto* node = scene_->find(id);
            core::Aabb local;
            if (const auto* mesh = scene_->editableMesh(node->editableMesh)) {
                for (const auto& vertex : mesh->content->evaluatedMesh().vertices)
                    local.expand(vertex.position);
            } else {
                local = renderer_gl::RayCaster::localBounds(*node, *assets_);
            }
            if (!apiBatchBoundsFit(local, world))
                return fail(api::ErrorCode::UnsupportedTransform,
                            QStringLiteral("目标或后代的几何世界边界超出有限 float 范围。"),
                            "items");
        }
        prepared = std::make_shared<core::Scene::PreparedTransformBatch>(std::move(*candidate));
        Q_ASSERT(prepared->hasChanges() == !result.affectedEntityIds.empty());
        if (prepared->hasChanges()) {
            const auto affected =
                std::make_shared<const std::vector<core::EntityId>>(result.affectedEntityIds);
            const auto apply = [this, prepared, affected](bool forward) {
                const bool installed = forward ? scene_->installPreparedTransformBatch(*prepared)
                                               : scene_->restorePreparedTransformBatch(*prepared);
                Q_ASSERT(installed);
                for (const auto id : *affected)
                    queueEntityNotification(id);
            };
            command = std::make_unique<EditCommand>(
                QStringLiteral("批量变换对象"),
                [apply] {
                    apply(false);
                },
                [apply] {
                    apply(true);
                },
                [this, prepared](bool forward, QString& error) {
                    std::string diagnostic;
                    auto inputs = scene_->animationPoseInputs(*prepared, forward, diagnostic);
                    if (!inputs) {
                        error = QString::fromStdString(diagnostic);
                        return std::optional<PreparedAnimationReplay>{};
                    }
                    auto geometry = preparePoseGeometry(*inputs);
                    return prepareAnimationReplay(std::move(*inputs), scene_->animation(),
                                                   std::move(geometry), error);
                });
            result.status = api::ResultStatus::Committed;
            result.undoable = true;
        }
        if (const auto error =
                checkBeforeCommit(animationPreparationSource(), nullptr, nullptr, &request))
            return Result::failure(*error);
        if (const auto error = validateApiMutation(request))
            return Result::failure(*error);
        if (!scene_->canInstallPreparedTransformBatch(*prepared))
            return fail(api::ErrorCode::RevisionConflict,
                        QStringLiteral("变换批次的目标、父链或几何来源已变化，请重新查询。"),
                        "items");
    } catch (const std::bad_alloc&) {
        return fail(api::ErrorCode::LimitExceeded, QStringLiteral("变换批次准备内存不足。"),
                    "items");
    }
    if (command)
        pushHistory(command.release());
    result.state = apiDocumentState();
    return Result::success(std::move(result));
}
api::ApiResult<api::MutationResult>
SceneViewModel::commitEntityCreate(const core::Scene::EntityCreateOptions& options,
                                   bool selectCreated, const core::modeling::EditableMesh* source) {
    using Result = api::ApiResult<api::MutationResult>;
    pendingAnimationReplay_.reset();
    if (animationMode_ != AnimationMode::Base)
        return Result::failure({api::ErrorCode::Busy, QStringLiteral("请先关闭动画预览再创建基础对象。"),
                                {}, api::Recovery::Wait, apiDocumentState()});
    const auto fail = [this](api::ErrorCode code, const QString& message, const QString& field) {
        return Result::failure(
            {code, message, field, api::Recovery::CorrectInput, apiDocumentState()});
    };
    const auto name = QString::fromUtf8(options.name.data(), qsizetype(options.name.size()));
    if (name.trimmed().isEmpty() || name.toUcs4().size() > 256)
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("名称须包含 1～256 个字符。"),
                    QStringLiteral("name"));
    if (options.parent != 0 && !scene_->find(options.parent))
        return fail(api::ErrorCode::NotFound, QStringLiteral("父对象不存在。"),
                    QStringLiteral("parentId"));
    if (!options.transform.isValid())
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("局部变换无效。"),
                    QStringLiteral("transform"));
    if (!options.surface.isValid())
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("表面颜色须在 0～1 之间。"),
                    QStringLiteral("surface"));
    if (options.camera && !options.camera->isValid())
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("相机参数无效。"),
                    QStringLiteral("camera"));
    if (options.light && !options.light->isValid())
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("方向光参数无效。"),
                    QStringLiteral("light"));
    std::string diagnostic;
    if (source) {
        if (const auto error = api::checkMeshCandidateBudget(*source, nullptr, apiDocumentState()))
            return Result::failure(*error);
    }
    auto candidate = scene_->prepareEntity(options, diagnostic, source);
    if (!candidate)
        return fail(source ? api::ErrorCode::InvalidTopology : api::ErrorCode::InvalidArgument,
                    QString::fromStdString(diagnostic), {});
    if (source) {
        if (const auto error =
                api::checkMeshCandidateBudget(*candidate->content(), apiDocumentState(), source))
            return Result::failure(*error);
    }
    auto prepared = std::make_shared<core::Scene::PreparedEntity>(std::move(*candidate));
    const auto id = prepared->entityId();
    const auto previousSelection = selection_.selectedEntity();
    const auto apply = [this, prepared, selectCreated, previousSelection](bool forward) {
        emit structureAboutToChange();
        const bool installed = forward ? scene_->installPreparedEntity(*prepared)
                                       : scene_->removePreparedEntity(*prepared);
        Q_ASSERT(installed);
        std::optional<core::EntityId> selection;
        if (selectCreated)
            selection = forward ? prepared->entityId() : previousSelection;
        else if (!forward && selection_.selectedEntity() == prepared->entityId())
            selection = 0;
        queueHistoryNotifications(true, selection);
    };
    auto command = std::make_unique<EditCommand>(
        source           ? QStringLiteral("创建网格")
        : options.camera ? QStringLiteral("创建相机")
        : options.light  ? QStringLiteral("创建方向光")
                         : QStringLiteral("创建对象"),
        [apply] {
            apply(false);
        },
        [apply] {
            apply(true);
        });
    api::MutationResult result;
    result.status = api::ResultStatus::Committed;
    result.createdEntityIds = {id};
    result.affectedEntityIds = {id};
    result.undoable = true;
    result.selectionChanged = selectCreated && previousSelection != id;
    if (const auto error = checkBeforeCommit())
        return Result::failure(*error);
    pushHistory(command.release());
    result.state = apiDocumentState();
    return Result::success(std::move(result));
}
api::ApiResult<api::MeshCreateResult>
SceneViewModel::createMeshExplicit(const api::MeshCreateRequest& request) {
    using Result = api::ApiResult<api::MeshCreateResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (const auto error = api::checkMeshEnvelope(request, apiDocumentState()))
        return Result::failure(*error);
    const auto invalid = [this](const QString& field, const QString& message) {
        return Result::failure(api::meshArgumentError(apiDocumentState(), field, message));
    };
    if (request.name.trimmed().isEmpty() || request.name.toUcs4().size() > 256)
        return invalid("name", QStringLiteral("名称须包含 1～256 个字符。"));
    if (!request.transform.isValid())
        return invalid("transform", QStringLiteral("局部变换无效。"));
    if (!request.surface.isValid())
        return invalid("surface", QStringLiteral("表面属性无效。"));
    if (request.positions.size() > api::limits::meshVertices ||
        request.faces.size() > api::limits::meshFaces)
        return Result::failure({api::ErrorCode::LimitExceeded,
                                QStringLiteral("创建网格数量超出限额。"), "positions/faces",
                                api::Recovery::CorrectInput, apiDocumentState()});
    std::size_t totalCorners = 0;
    for (std::size_t faceIndex = 0; faceIndex < request.faces.size(); ++faceIndex) {
        const auto& face = request.faces[faceIndex];
        const auto path = QStringLiteral("faces[%1]").arg(faceIndex);
        if (face.indices.size() < 3)
            return invalid(path + ".indices", QStringLiteral("每面至少包含三个不同顶点。"));
        if (face.indices.size() > api::limits::meshVertices ||
            face.indices.size() > api::limits::meshCorners - totalCorners)
            return Result::failure({api::ErrorCode::LimitExceeded,
                                    QStringLiteral("面环或总面角数量超出限额。"), path,
                                    api::Recovery::CorrectInput, apiDocumentState()});
        totalCorners += face.indices.size();
        if (face.corners && face.corners->size() != face.indices.size())
            return invalid(path + ".corners", QStringLiteral("面角属性须与索引环逐项匹配。"));
        std::set<std::size_t> indices;
        for (const auto index : face.indices)
            if (index >= request.positions.size() || !indices.insert(index).second)
                return invalid(path + ".indices", QStringLiteral("面索引越界或重复。"));
        if (face.corners) {
            for (const auto& corner : *face.corners) {
                if (!api::isFiniteFloat(corner.uv.x) || !api::isFiniteFloat(corner.uv.y))
                    return invalid(path + ".corners.uv", QStringLiteral("UV 必须有限。"));
                for (int axis = 0; axis < 3; ++axis)
                    if (!api::isFiniteFloat(corner.color[axis]) ||
                        (corner.normal && !api::isFiniteFloat((*corner.normal)[axis])))
                        return invalid(path + ".corners", QStringLiteral("颜色和硬法线必须有限。"));
                if (corner.normal && glm::length(glm::dvec3(*corner.normal)) == 0)
                    return invalid(path + ".corners.normal", QStringLiteral("硬法线不能为零。"));
            }
        }
    }
    core::modeling::EditableMesh source;
    api::MeshCreateResult result;
    source.vertices.reserve(request.positions.size());
    source.faces.reserve(request.faces.size());
    for (std::size_t index = 0; index < request.positions.size(); ++index) {
        const auto& position = request.positions[index];
        for (int axis = 0; axis < 3; ++axis)
            if (!api::isFiniteFloat(position[axis]))
                return invalid(QStringLiteral("positions[%1]").arg(index),
                                QStringLiteral("顶点坐标必须有限。"));
        const auto id = core::modeling::VertexId(index + 1);
        source.vertices.push_back({id, position});
        result.vertexIdsByInputIndex.push_back(id);
    }
    core::modeling::CornerId cornerId = 0;
    for (std::size_t index = 0; index < request.faces.size(); ++index) {
        const auto& input = request.faces[index];
        core::modeling::EditableFace face;
        face.id = index + 1;
        std::vector<core::modeling::CornerId> cornerIds;
        for (std::size_t corner = 0; corner < input.indices.size(); ++corner) {
            core::modeling::MeshCorner value;
            value.id = ++cornerId;
            value.vertex = input.indices[corner] + 1;
            if (input.corners) {
                const auto& attributes = (*input.corners)[corner];
                value.uv = attributes.uv;
                value.color = attributes.color;
                value.normal = attributes.normal;
            }
            cornerIds.push_back(value.id);
            face.corners.push_back(value);
        }
        result.faceIdsByInputIndex.push_back(face.id);
        result.cornerIdsByFace.push_back(std::move(cornerIds));
        source.faces.push_back(std::move(face));
    }
    core::Scene::EntityCreateOptions options;
    options.name = request.name.toUtf8().toStdString();
    options.parent = request.parentId;
    options.transform = request.transform;
    options.surface = request.surface;
    const auto committed = commitEntityCreate(options, false, &source);
    if (!committed.hasValue())
        return Result::failure(*committed.error);
    result.command = *committed.value;
    const auto entity = result.command.createdEntityIds.front();
    const auto mesh = scene_->find(entity)->editableMesh;
    result.mesh = api::meshIdentity(entity, mesh, *scene_->editableMesh(mesh));
    return Result::success(std::move(result));
}
api::ApiResult<const core::EditableMeshRecord*>
SceneViewModel::validateMeshMutation(const api::MeshMutationRequest& request) const {
    using Result = api::ApiResult<const core::EditableMeshRecord*>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (animationMode_ != AnimationMode::Base)
        return Result::failure({api::ErrorCode::Busy, QStringLiteral("请先关闭动画预览再编辑基础几何。"),
                                {}, api::Recovery::Wait, apiDocumentState()});
    if (const auto error = api::checkMeshEnvelope(request, apiDocumentState()))
        return Result::failure(*error);
    auto target = api::meshTarget(*scene_, apiDocumentState(), request.entityId, request.meshId);
    if (!target.hasValue())
        return target;
    if (const auto error = api::checkMeshRevisions(request, **target.value, apiDocumentState()))
        return Result::failure(*error);
    if (const auto error = api::checkMeshCandidateBudget((*target.value)->content->source, nullptr,
                                                         apiDocumentState()))
        return Result::failure(*error);
    return target;
}
api::ApiResult<api::MutationResult>
SceneViewModel::commitMeshCandidate(const api::MeshMutationRequest& request,
                                    const core::modeling::EditableMesh& candidate,
                                    const QString& label) {
    using Result = api::ApiResult<api::MutationResult>;
    api::MutationResult result;
    if (const auto error = commitMeshCandidate(request, candidate, label, result))
        return Result::failure(*error);
    return Result::success(std::move(result));
}
std::optional<api::ApiError>
SceneViewModel::commitMeshCandidate(const api::MeshMutationRequest& request,
                                    const core::modeling::EditableMesh& candidate,
                                    const QString& label, api::MutationResult& result) {
    const auto* current = scene_->editableMesh(request.meshId);
    if (candidate == current->content->source) {
        if (const auto error = checkBeforeCommit())
            return error;
        result.state = apiDocumentState();
        return std::nullopt;
    }
    if (const auto error =
            api::checkMeshCandidateBudget(candidate, current->content.get(), apiDocumentState()))
        return error;
    const auto before = scene_->geometrySnapshot(request.entityId);
    std::string diagnostic;
    const auto after = scene_->prepareEditableGeometry(request.entityId, candidate, diagnostic);
    if (!after)
        return api::ApiError{api::ErrorCode::InvalidTopology, QString::fromStdString(diagnostic),
                             "mesh", api::Recovery::CorrectInput, apiDocumentState()};
    if (const auto error =
            api::checkMeshCandidateBudget(*after->content(), apiDocumentState(), &candidate))
        return error;
    return commitPreparedMeshCandidate(request.entityId, *before, *after, label, result);
}
std::optional<api::ApiError> SceneViewModel::commitPreparedMeshCandidate(
    core::EntityId id, const core::Scene::GeometrySnapshot& before,
    const core::Scene::GeometrySnapshot& after, const QString& label, api::MutationResult& result) {
    if (before.content() == after.content()) {
        if (const auto error = checkBeforeCommit())
            return error;
        result.state = apiDocumentState();
        return std::nullopt;
    }
    result.status = api::ResultStatus::Committed;
    result.affectedEntityIds = {id};
    result.undoable = true;
    const auto apply = [this, id](const core::Scene::GeometrySnapshot& snapshot) {
        const bool installed = scene_->installGeometry(snapshot);
        Q_ASSERT(installed);
        queueEntityNotification(id);
    };
    auto command = std::make_unique<EditCommand>(
        label,
        [apply, before] {
            apply(before);
        },
        [apply, after] {
            apply(after);
        });
    if (const auto error = checkBeforeCommit())
        return error;
    pushHistory(command.release());
    result.state = apiDocumentState();
    return std::nullopt;
}
api::ApiResult<api::MeshExtrudeResult>
SceneViewModel::extrudeMeshExplicit(const api::MeshExtrudeRequest& request) {
    using Result = api::ApiResult<api::MeshExtrudeResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    const auto& source = (*target.value)->content->source;
    const auto invalid = [this](const QString& field, const QString& message) {
        return Result::failure(api::meshArgumentError(apiDocumentState(), field, message));
    };
    if (request.faceIds.empty())
        return invalid("faceIds", QStringLiteral("至少指定一个源面。"));
    if (request.faceIds.size() > api::limits::meshFaces)
        return Result::failure({api::ErrorCode::LimitExceeded, QStringLiteral("源面数量超出限额。"),
                                "faceIds", api::Recovery::CorrectInput, apiDocumentState()});
    std::set<core::modeling::FaceId> faces;
    std::set<core::modeling::FaceId> beforeFaces;
    for (const auto& face : source.faces)
        beforeFaces.insert(face.id);
    for (const auto face : request.faceIds) {
        if (face == 0 || !faces.insert(face).second)
            return invalid("faceIds", QStringLiteral("源面 ID 必须非零且不能重复。"));
        if (!beforeFaces.contains(face))
            return Result::failure({api::ErrorCode::NotFound, QStringLiteral("指定源面不存在。"),
                                    "faceIds", api::Recovery::Refetch, apiDocumentState()});
    }
    if (request.space != api::MeshSpace::Local && request.space != api::MeshSpace::World)
        return invalid("space", QStringLiteral("空间仅支持 local/world。"));
    for (int axis = 0; axis < 3; ++axis)
        if (!api::isFiniteFloat(request.offset[axis]))
            return invalid("offset", QStringLiteral("位移须为 float 范围的有限数值。"));
    api::MeshExtrudeResult result;
    result.capFaceIds.assign(faces.begin(), faces.end());
    if (request.offset == glm::dvec3(0)) {
        const auto region = core::modeling::analyzeExtrudeRegion(source, faces);
        if (!region.region)
            return Result::failure({api::ErrorCode::InvalidTopology,
                                    QString::fromStdString(region.error), "faceIds",
                                    api::Recovery::CorrectInput, apiDocumentState()});
        if (const auto error = checkBeforeCommit())
            return Result::failure(*error);
        result.command.state = apiDocumentState();
        result.mesh = api::meshIdentity(request.entityId, request.meshId, **target.value);
        return Result::success(std::move(result));
    }
    if (const auto error = api::checkMeshCandidateBudget(source, nullptr, apiDocumentState()))
        return Result::failure(*error);
    auto offset = request.offset;
    if (request.space == api::MeshSpace::World) {
        const auto linear = glm::dmat3(scene_->worldMatrix(request.entityId));
        const auto determinant = glm::determinant(linear);
        bool valid = std::isfinite(determinant) && determinant != 0;
        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                valid = valid && std::isfinite(linear[column][row]);
        if (valid) {
            offset = glm::inverse(linear) * offset;
            for (int axis = 0; axis < 3; ++axis)
                valid = valid && api::isFiniteFloat(offset[axis]);
        }
        if (!valid)
            return Result::failure({api::ErrorCode::UnsupportedTransform,
                                    QStringLiteral("世界位移无法映射为有效局部位移。"), "space",
                                    api::Recovery::CorrectInput, apiDocumentState()});
    }
    const auto candidate = core::modeling::extrudeRegion(source, faces, offset);
    if (!candidate.mesh)
        return Result::failure({api::ErrorCode::InvalidTopology, QString::fromStdString(candidate.error),
                                "faceIds/offset", api::Recovery::CorrectInput, apiDocumentState()});
    for (const auto& face : candidate.mesh->faces)
        if (!beforeFaces.contains(face.id))
            result.sideFaceIds.push_back(face.id);
    std::sort(result.sideFaceIds.begin(), result.sideFaceIds.end());
    const auto committed = commitMeshCandidate(request, *candidate.mesh, QStringLiteral("API 区域挤出"));
    if (!committed.hasValue())
        return Result::failure(*committed.error);
    result.command = *committed.value;
    result.mesh = api::meshIdentity(request.entityId, request.meshId, *scene_->editableMesh(request.meshId));
    return Result::success(std::move(result));
}
api::ApiResult<api::MeshInsetResult>
SceneViewModel::insetMeshExplicit(const api::MeshInsetRequest& request) {
    using Result = api::ApiResult<api::MeshInsetResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    if (request.faceId == 0 || !api::isFiniteFloat(request.thickness) || request.thickness <= 0)
        return Result::failure(api::meshArgumentError(apiDocumentState(), request.faceId == 0 ? "faceId" : "thickness",
                                                       QStringLiteral("源面 ID 非零，局部厚度须为有限正数。")));
    const auto& source = (*target.value)->content->source;
    std::set<core::modeling::FaceId> beforeFaces;
    for (const auto& face : source.faces)
        beforeFaces.insert(face.id);
    if (!beforeFaces.contains(request.faceId))
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("指定源面不存在。"),
                                "faceId", api::Recovery::Refetch, apiDocumentState()});
    if (const auto error = api::checkMeshCandidateBudget(source, nullptr, apiDocumentState()))
        return Result::failure(*error);
    const auto candidate = core::modeling::insetFace(source, request.faceId, request.thickness);
    if (!candidate.mesh)
        return Result::failure({api::ErrorCode::InvalidTopology, QString::fromStdString(candidate.error),
                                "faceId/thickness", api::Recovery::CorrectInput, apiDocumentState()});
    api::MeshInsetResult result;
    result.innerFaceIds = {request.faceId};
    for (const auto& face : candidate.mesh->faces)
        if (!beforeFaces.contains(face.id))
            result.rimFaceIds.push_back(face.id);
    std::sort(result.rimFaceIds.begin(), result.rimFaceIds.end());
    const auto committed = commitMeshCandidate(request, *candidate.mesh, QStringLiteral("API 面内插"));
    if (!committed.hasValue())
        return Result::failure(*committed.error);
    result.command = *committed.value;
    result.mesh = api::meshIdentity(request.entityId, request.meshId, *scene_->editableMesh(request.meshId));
    return Result::success(std::move(result));
}
api::ApiResult<api::MeshCommandResult>
SceneViewModel::makeEditableExplicit(const api::MeshMakeEditableRequest& request) {
    using Result = api::ApiResult<api::MeshCommandResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (request.entityId == 0)
        return Result::failure(api::meshArgumentError(apiDocumentState(), "entityId",
                                                      QStringLiteral("对象 ID 必须非零。")));
    const auto* node = scene_->find(request.entityId);
    if (!node)
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("对象不存在。"),
                                "entityId", api::Recovery::Refetch, apiDocumentState()});
    api::MeshCommandResult result;
    if (node->editableMesh != 0) {
        result.mesh = api::meshIdentity(request.entityId, node->editableMesh,
                                        *scene_->editableMesh(node->editableMesh));
        if (const auto error = checkBeforeCommit())
            return Result::failure(*error);
        result.command.state = apiDocumentState();
        return Result::success(std::move(result));
    }
    if (node->primitive != core::PrimitiveKind::Cube || node->camera || node->light)
        return Result::failure({api::ErrorCode::UnsupportedOperation,
                                QStringLiteral("当前仅支持将原生立方体转为可编辑网格。"),
                                "entityId", api::Recovery::CorrectInput, apiDocumentState()});
    const auto source = core::modeling::createEditableCube();
    if (const auto error = api::checkMeshCandidateBudget(source, nullptr, apiDocumentState()))
        return Result::failure(*error);
    const auto before = scene_->geometrySnapshot(request.entityId);
    std::string diagnostic;
    const auto after = scene_->prepareEditableGeometry(request.entityId, source, diagnostic);
    if (!after)
        return Result::failure({api::ErrorCode::InvalidTopology, QString::fromStdString(diagnostic),
                                "mesh", api::Recovery::CorrectInput, apiDocumentState()});
    if (const auto error =
            api::checkMeshCandidateBudget(*after->content(), apiDocumentState(), &source))
        return Result::failure(*error);
    if (const auto error =
            commitPreparedMeshCandidate(request.entityId, *before, *after,
                                        QStringLiteral("API 转为可编辑网格"), result.command))
        return Result::failure(*error);
    const auto mesh = scene_->find(request.entityId)->editableMesh;
    result.mesh = api::meshIdentity(request.entityId, mesh, *scene_->editableMesh(mesh));
    return Result::success(std::move(result));
}
api::ApiResult<api::MeshTransformComponentsResult>
SceneViewModel::transformComponentsExplicit(const api::MeshTransformComponentsRequest& request) {
    using Result = api::ApiResult<api::MeshTransformComponentsResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    const auto& source = (*target.value)->content->source;
    const auto selected = apiComponentTargets(source, request, apiDocumentState(), true);
    if (!selected.hasValue())
        return Result::failure(*selected.error);
    const auto invalid = [this](const QString& field, const QString& message) {
        return Result::failure(api::meshArgumentError(apiDocumentState(), field, message));
    };
    const auto& transform = request.transform;
    if (transform.space != api::MeshSpace::Local && transform.space != api::MeshSpace::World)
        return invalid("transform.space", QStringLiteral("空间仅支持 local/world。"));
    for (int axis = 0; axis < 3; ++axis) {
        if (!api::isFiniteFloat(transform.pivot[axis]))
            return invalid("transform.pivot", QStringLiteral("枢轴须为 float 范围的有限数值。"));
        if (!api::isFiniteFloat(transform.translation[axis]))
            return invalid("transform.translation",
                           QStringLiteral("位移须为 float 范围的有限数值。"));
        if (!api::isFiniteFloat(transform.scale[axis]) || std::abs(transform.scale[axis]) < 0.001)
            return invalid("transform.scale", QStringLiteral("缩放须有限且绝对值不小于 0.001。"));
    }
    double quaternionMaximum = 0;
    for (int component = 0; component < 4; ++component) {
        if (!api::isFiniteFloat(transform.rotation[component]))
            return invalid("transform.rotationQuaternion", QStringLiteral("四元数分量须有限。"));
        quaternionMaximum = std::max(quaternionMaximum, std::abs(transform.rotation[component]));
    }
    if (quaternionMaximum == 0)
        return invalid("transform.rotationQuaternion", QStringLiteral("四元数不能为零。"));
    // 先缩放再归一化，保留合法 double 小分量，不在 float 转换时把旋转截成零。
    const auto rotation = glm::normalize(transform.rotation / quaternionMaximum);
    const auto delta = glm::translate(glm::dmat4(1), transform.pivot + transform.translation) *
                       glm::mat4_cast(rotation) * glm::scale(glm::dmat4(1), transform.scale) *
                       glm::translate(glm::dmat4(1), -transform.pivot);
    const auto world = glm::dmat4(scene_->worldMatrix(request.entityId));
    if (!apiFiniteAffine(world))
        return Result::failure({api::ErrorCode::UnsupportedTransform,
                                QStringLiteral("对象的真实世界矩阵不是有限可逆仿射变换。"),
                                "transform.space", api::Recovery::CorrectInput,
                                apiDocumentState()});
    auto worldDelta = delta;
    if (transform.space == api::MeshSpace::Local && delta != glm::dmat4(1))
        worldDelta = world * delta * glm::affineInverse(world);
    if (!apiFiniteAffine(worldDelta))
        return Result::failure({api::ErrorCode::UnsupportedTransform,
                                QStringLiteral("增量无法映射为有限可逆的世界仿射变换。"),
                                "transform", api::Recovery::CorrectInput, apiDocumentState()});
    if (const auto error = api::checkMeshCandidateBudget(source, (*target.value)->content.get(),
                                                         apiDocumentState()))
        return Result::failure(*error);
    if (const auto error =
            apiModelingResultBudget(selected.value->vertices.size(), 0, apiDocumentState()))
        return Result::failure(*error);
    const auto before = scene_->geometrySnapshot(request.entityId);
    std::string diagnostic;
    const auto after = scene_->prepareTransformedEditableGeometry(
        request.entityId, *before, selected.value->vertices, world, worldDelta, diagnostic);
    if (!after)
        return Result::failure({api::ErrorCode::InvalidTopology, QString::fromStdString(diagnostic),
                                "transform", api::Recovery::CorrectInput, apiDocumentState()});
    if (const auto error =
            api::checkMeshCandidateBudget(*after->content(), apiDocumentState(), &source))
        return Result::failure(*error);
    api::MeshTransformComponentsResult result;
    const auto& transformed = after->content()->source;
    for (std::size_t index = 0; index < source.vertices.size(); ++index)
        if (source.vertices[index].position != transformed.vertices[index].position)
            result.affectedVertexIds.push_back(source.vertices[index].id);
    std::sort(result.affectedVertexIds.begin(), result.affectedVertexIds.end());
    if (const auto error = commitPreparedMeshCandidate(
            request.entityId, *before, *after, QStringLiteral("API 组件变换"), result.command))
        return Result::failure(*error);
    result.mesh =
        api::meshIdentity(request.entityId, request.meshId, *scene_->editableMesh(request.meshId));
    return Result::success(std::move(result));
}
api::ApiResult<api::MeshBevelEdgeResult>
SceneViewModel::bevelEdgeExplicit(const api::MeshBevelEdgeRequest& request) {
    using Result = api::ApiResult<api::MeshBevelEdgeResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    const auto& source = (*target.value)->content->source;
    if (const auto error = apiSourceEdge(source, request.edge, "edge", apiDocumentState()))
        return Result::failure(*error);
    if (!api::isFiniteFloat(request.width) || request.width <= 0)
        return Result::failure(api::meshArgumentError(apiDocumentState(), "width",
                                                      QStringLiteral("宽度须有限且大于零。")));
    const core::modeling::EdgeKey edge(request.edge.first, request.edge.second);
    const auto analysis = core::modeling::analyzeBevelEdge(source, edge);
    if (!analysis.bevel)
        return Result::failure({api::ErrorCode::InvalidTopology,
                                QString::fromStdString(analysis.error), "edge",
                                api::Recovery::CorrectInput, apiDocumentState()});
    if (request.width >= analysis.bevel->maximumWidth)
        return Result::failure(api::meshArgumentError(
            apiDocumentState(), "width", QStringLiteral("宽度须严格小于此边可用的局部上限。")));
    if (const auto error = api::checkMeshCandidateBudget(source, nullptr, apiDocumentState()))
        return Result::failure(*error);
    const auto candidate = core::modeling::bevelEdge(source, edge, request.width);
    if (!candidate.mesh)
        return Result::failure({api::ErrorCode::InvalidTopology,
                                QString::fromStdString(candidate.error), "edge/width",
                                api::Recovery::CorrectInput, apiDocumentState()});
    api::MeshBevelEdgeResult result;
    result.bevelFaceId = candidate.bevelFace;
    if (const auto error = commitMeshCandidate(request, *candidate.mesh,
                                               QStringLiteral("API 单边倒角"), result.command))
        return Result::failure(*error);
    result.mesh =
        api::meshIdentity(request.entityId, request.meshId, *scene_->editableMesh(request.meshId));
    return Result::success(std::move(result));
}
api::ApiResult<api::MeshLoopCutResult>
SceneViewModel::loopCutExplicit(const api::MeshLoopCutRequest& request) {
    using Result = api::ApiResult<api::MeshLoopCutResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    const auto& source = (*target.value)->content->source;
    if (const auto error = apiSourceEdge(source, request.seedEdge, "seedEdge", apiDocumentState()))
        return Result::failure(*error);
    if (!std::isfinite(request.slide) || request.slide <= -1 || request.slide >= 1)
        return Result::failure(api::meshArgumentError(
            apiDocumentState(), "slide", QStringLiteral("滑移须严格位于 -1～1 之间。")));
    if (const auto error = api::checkMeshCandidateBudget(source, nullptr, apiDocumentState()))
        return Result::failure(*error);
    const auto candidate = core::modeling::loopCut(
        source, {request.seedEdge.first, request.seedEdge.second}, request.slide);
    if (!candidate.mesh)
        return Result::failure({api::ErrorCode::InvalidTopology,
                                QString::fromStdString(candidate.error), "seedEdge/slide",
                                api::Recovery::CorrectInput, apiDocumentState()});
    api::MeshLoopCutResult result;
    if (candidate.cutEdges.size() > api::limits::meshCorners)
        return Result::failure({api::ErrorCode::LimitExceeded,
                                QStringLiteral("切线输出超出 API 限额。"), "cutEdges",
                                api::Recovery::CorrectInput, apiDocumentState()});
    if (const auto error =
            apiModelingResultBudget(0, candidate.cutEdges.size(), apiDocumentState()))
        return Result::failure(*error);
    result.cutEdges.assign(candidate.cutEdges.begin(), candidate.cutEdges.end());
    if (const auto error = commitMeshCandidate(request, *candidate.mesh,
                                               QStringLiteral("API 单次环切"), result.command))
        return Result::failure(*error);
    result.mesh =
        api::meshIdentity(request.entityId, request.meshId, *scene_->editableMesh(request.meshId));
    return Result::success(std::move(result));
}
api::ApiResult<api::MeshDeleteComponentsResult>
SceneViewModel::deleteComponentsExplicit(const api::MeshDeleteComponentsRequest& request) {
    using Result = api::ApiResult<api::MeshDeleteComponentsResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    const auto& source = (*target.value)->content->source;
    const auto selected = apiComponentTargets(source, request, apiDocumentState(), true);
    if (!selected.hasValue())
        return Result::failure(*selected.error);
    if (const auto error = api::checkMeshCandidateBudget(source, nullptr, apiDocumentState()))
        return Result::failure(*error);
    const auto candidate = request.domain == api::MeshComponentDomain::Vertices
                               ? core::modeling::deleteVertices(source, selected.value->vertices)
                           : request.domain == api::MeshComponentDomain::Edges
                               ? core::modeling::deleteEdges(source, selected.value->edges)
                               : core::modeling::deleteFaces(source, selected.value->faces);
    if (!candidate.mesh)
        return Result::failure({api::ErrorCode::InvalidTopology,
                                QString::fromStdString(candidate.error), "domain",
                                api::Recovery::CorrectInput, apiDocumentState()});
    std::set<core::modeling::VertexId> afterVertices;
    std::set<core::modeling::FaceId> afterFaces;
    std::set<core::modeling::CornerId> afterCorners;
    for (const auto& vertex : candidate.mesh->vertices)
        afterVertices.insert(vertex.id);
    for (const auto& face : candidate.mesh->faces) {
        afterFaces.insert(face.id);
        for (const auto& corner : face.corners)
            afterCorners.insert(corner.id);
    }
    api::MeshDeleteComponentsResult result;
    for (const auto& vertex : source.vertices)
        if (!afterVertices.contains(vertex.id))
            result.deletedVertexIds.push_back(vertex.id);
    for (const auto& face : source.faces) {
        if (!afterFaces.contains(face.id))
            result.deletedFaceIds.push_back(face.id);
        for (const auto& corner : face.corners)
            if (!afterCorners.contains(corner.id))
                result.deletedCornerIds.push_back(corner.id);
    }
    const auto beforeEdges = apiSourceEdges(source), afterEdges = apiSourceEdges(*candidate.mesh);
    std::set_difference(beforeEdges.begin(), beforeEdges.end(), afterEdges.begin(),
                        afterEdges.end(), std::back_inserter(result.deletedEdges));
    std::sort(result.deletedVertexIds.begin(), result.deletedVertexIds.end());
    std::sort(result.deletedFaceIds.begin(), result.deletedFaceIds.end());
    std::sort(result.deletedCornerIds.begin(), result.deletedCornerIds.end());
    if (const auto error =
            apiModelingResultBudget(result.deletedVertexIds.size() + result.deletedFaceIds.size() +
                                        result.deletedCornerIds.size(),
                                    result.deletedEdges.size(), apiDocumentState()))
        return Result::failure(*error);
    if (const auto error = commitMeshCandidate(request, *candidate.mesh,
                                               QStringLiteral("API 删除组件"), result.command))
        return Result::failure(*error);
    result.mesh =
        api::meshIdentity(request.entityId, request.meshId, *scene_->editableMesh(request.meshId));
    return Result::success(std::move(result));
}
api::ApiResult<api::MeshFillFaceResult>
SceneViewModel::fillFaceExplicit(const api::MeshFillFaceRequest& request) {
    using Result = api::ApiResult<api::MeshFillFaceResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    const auto& source = (*target.value)->content->source;
    const auto selected = apiComponentTargets(source, request, apiDocumentState(), false);
    if (!selected.hasValue())
        return Result::failure(*selected.error);
    if (const auto error = api::checkMeshCandidateBudget(source, nullptr, apiDocumentState()))
        return Result::failure(*error);
    const auto candidate =
        request.domain == api::MeshComponentDomain::Vertices
            ? core::modeling::fillFaceFromVertices(source, selected.value->vertices)
            : core::modeling::fillFaceFromEdges(source, selected.value->edges);
    if (!candidate.mesh)
        return Result::failure({api::ErrorCode::InvalidTopology,
                                QString::fromStdString(candidate.error), "domain",
                                api::Recovery::CorrectInput, apiDocumentState()});
    api::MeshFillFaceResult result;
    result.faceId = candidate.face;
    if (const auto error = commitMeshCandidate(request, *candidate.mesh, QStringLiteral("API 补面"),
                                               result.command))
        return Result::failure(*error);
    result.mesh =
        api::meshIdentity(request.entityId, request.meshId, *scene_->editableMesh(request.meshId));
    return Result::success(std::move(result));
}
api::ApiResult<api::ModifierCommandResult> SceneViewModel::commitModifierCandidate(
    const api::MeshMutationRequest& request, const core::Scene::GeometrySnapshot& before,
    const core::Scene::GeometrySnapshot& after, const core::modeling::EditableMesh& retainedSource,
    bool requeryRequired, const QString& label) {
    using Result = api::ApiResult<api::ModifierCommandResult>;
    if (before.content() != after.content())
        if (const auto error = api::checkMeshCandidateBudget(*after.content(), apiDocumentState(),
                                                             &retainedSource))
            return Result::failure(*error);
    api::ModifierCommandResult result;
    result.modifiers = api::modifierState(*after.content());
    result.requeryRequired = requeryRequired;
    if (const auto error =
            commitPreparedMeshCandidate(request.entityId, before, after, label, result.command))
        return Result::failure(*error);
    result.mesh =
        api::meshIdentity(request.entityId, request.meshId, *scene_->editableMesh(request.meshId));
    return Result::success(std::move(result));
}
api::ApiResult<api::ModifierCommandResult>
SceneViewModel::setMirrorExplicit(const api::ModifierSetMirrorRequest& request) {
    using Result = api::ApiResult<api::ModifierCommandResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    if (request.options && !request.options->isValid())
        return Result::failure(api::meshArgumentError(
            apiDocumentState(), "options", QStringLiteral("镜像轴或有限非负阈值无效。")));
    const auto& content = *(*target.value)->content;
    const auto before = scene_->geometrySnapshot(request.entityId);
    auto after = before;
    if (request.options != content.mirror) {
        if (const auto error = api::checkMeshCandidateBudget(
                content.source, request.options, content.subdivision, apiDocumentState()))
            return Result::failure(*error);
        std::string diagnostic;
        after = scene_->prepareMirror(request.entityId, request.options, diagnostic);
        if (!after)
            return Result::failure({api::ErrorCode::InvalidTopology,
                                    QString::fromStdString(diagnostic), "options",
                                    api::Recovery::CorrectInput, apiDocumentState()});
    }
    return commitModifierCandidate(request, *before, *after, content.source, false,
                                   QStringLiteral("API 镜像参数"));
}
api::ApiResult<api::ModifierCommandResult>
SceneViewModel::setSubdivisionExplicit(const api::ModifierSetSubdivisionRequest& request) {
    using Result = api::ApiResult<api::ModifierCommandResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    if (request.options && !request.options->isValid())
        return Result::failure(api::meshArgumentError(apiDocumentState(), "options.levels",
                                                      QStringLiteral("细分级数仅支持 1 或 2。")));
    const auto& content = *(*target.value)->content;
    const auto before = scene_->geometrySnapshot(request.entityId);
    auto after = before;
    if (request.options != content.subdivision) {
        if (const auto error = api::checkMeshCandidateBudget(content.source, content.mirror,
                                                             request.options, apiDocumentState()))
            return Result::failure(*error);
        std::string diagnostic;
        after = scene_->prepareSubdivision(request.entityId, request.options, diagnostic);
        if (!after)
            return Result::failure({api::ErrorCode::InvalidTopology,
                                    QString::fromStdString(diagnostic), "options",
                                    api::Recovery::CorrectInput, apiDocumentState()});
    }
    return commitModifierCandidate(request, *before, *after, content.source, false,
                                   QStringLiteral("API 细分参数"));
}
api::ApiResult<api::ModifierCommandResult>
SceneViewModel::applyModifierExplicit(const api::ModifierApplyRequest& request) {
    using Result = api::ApiResult<api::ModifierCommandResult>;
    const auto target = validateMeshMutation(request);
    if (!target.hasValue())
        return Result::failure(*target.error);
    if (request.modifier != api::ModifierKind::Mirror &&
        request.modifier != api::ModifierKind::Subdivision)
        return Result::failure(api::meshArgumentError(
            apiDocumentState(), "modifier", QStringLiteral("仅支持 mirror/subdivision。")));
    const auto& content = *(*target.value)->content;
    const bool mirror = request.modifier == api::ModifierKind::Mirror;
    if ((mirror && !content.mirror) || (!mirror && !content.subdivision))
        return Result::failure({api::ErrorCode::UnsupportedOperation,
                                QStringLiteral("指定修改器参数尚不存在。"), "modifier",
                                api::Recovery::CorrectInput, apiDocumentState()});
    const auto& source = mirror ? content.mirrorEvaluation->mesh : content.evaluatedMesh();
    if (const auto error = api::checkMeshCandidateBudget(
            source, std::nullopt, mirror ? content.subdivision : std::nullopt, apiDocumentState()))
        return Result::failure(*error);
    const auto before = scene_->geometrySnapshot(request.entityId);
    std::string diagnostic;
    const auto after = mirror ? scene_->prepareAppliedMirror(request.entityId, diagnostic)
                              : scene_->prepareAppliedSubdivision(request.entityId, diagnostic);
    if (!after)
        return Result::failure({api::ErrorCode::InvalidTopology, QString::fromStdString(diagnostic),
                                "modifier", api::Recovery::CorrectInput, apiDocumentState()});
    return commitModifierCandidate(request, *before, *after, source, true,
                                   mirror ? QStringLiteral("API 应用镜像")
                                          : QStringLiteral("API 应用细分"));
}
api::ApiResult<api::MutationResult>
SceneViewModel::updateEntityExplicit(const api::EntityUpdateRequest& request) {
    if (const auto error = validateApiMutation(request))
        return api::ApiResult<api::MutationResult>::failure(*error);
    return commitEntityUpdate(request.entityId, request.changes, QStringLiteral("更新对象"));
}
api::ApiResult<api::EntityDuplicateResult>
SceneViewModel::duplicateEntityExplicit(const api::EntityDuplicateRequest& request) {
    using Result = api::ApiResult<api::EntityDuplicateResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    const auto ids = apiSubtreeIds(*scene_, request.entityId, apiDocumentState());
    if (!ids.hasValue())
        return Result::failure(*ids.error);
    std::string diagnostic;
    auto candidate =
        scene_->prepareDuplicateSubtree(request.entityId, diagnostic, maximumApiSubtreeEntities);
    if (!candidate)
        return Result::failure({api::ErrorCode::InvalidArgument, QString::fromStdString(diagnostic),
                                "entityId", api::Recovery::CorrectInput, apiDocumentState()});
    auto prepared = std::make_shared<core::Scene::PreparedSubtree>(std::move(*candidate));
    api::EntityDuplicateResult result;
    if (const auto error = preflightSubtreeAnimation(*prepared, true))
        return Result::failure(*error);
    result.entityIdMap = prepared->entityIdMap();
    for (const auto& mapping : result.entityIdMap)
        result.command.createdEntityIds.push_back(mapping.second);
    std::sort(result.command.createdEntityIds.begin(), result.command.createdEntityIds.end());
    result.command.affectedEntityIds = result.command.createdEntityIds;
    result.command.status = api::ResultStatus::Committed;
    result.command.undoable = true;
    auto command = std::make_unique<SubtreeCommand>(
        *this, prepared, SubtreeCommand::Kind::Duplicate, request.entityId, false);
    pendingAnimationCommand_ = pendingAnimationReplay_ ? command.get() : nullptr;
    if (const auto error = checkBeforeCommit())
        return Result::failure(*error);
    if (!scene_->canInstallPreparedSubtree(*prepared)) {
        pendingAnimationReplay_.reset();
        return Result::failure({api::ErrorCode::RevisionConflict,
                                QStringLiteral("复制候选来源已变化。"), "entityId",
                                api::Recovery::Refetch, apiDocumentState()});
    }
    pushHistory(command.release());
    result.command.state = apiDocumentState();
    return Result::success(std::move(result));
}
api::ApiResult<api::MutationResult>
SceneViewModel::deleteEntityExplicit(const api::EntityDeleteRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    const auto ids = apiSubtreeIds(*scene_, request.entityId, apiDocumentState());
    if (!ids.hasValue())
        return Result::failure(*ids.error);
    std::string diagnostic;
    auto candidate =
        scene_->prepareRemoveSubtree(request.entityId, diagnostic, maximumApiSubtreeEntities);
    if (!candidate)
        return Result::failure({api::ErrorCode::InvalidArgument, QString::fromStdString(diagnostic),
                                "entityId", api::Recovery::CorrectInput, apiDocumentState()});
    auto prepared = std::make_shared<core::Scene::PreparedSubtree>(std::move(*candidate));
    const auto previousSelection = selection_.selectedEntity();
    if (const auto error = preflightSubtreeAnimation(*prepared, false))
        return Result::failure(*error);
    const bool restoresSelection =
        std::binary_search(ids.value->begin(), ids.value->end(), previousSelection);
    api::MutationResult result;
    result.status = api::ResultStatus::Committed;
    result.affectedEntityIds = *ids.value;
    result.undoable = true;
    result.selectionChanged = restoresSelection;
    auto command = std::make_unique<SubtreeCommand>(
        *this, prepared, SubtreeCommand::Kind::Delete, request.entityId, false, previousSelection);
    pendingAnimationCommand_ = pendingAnimationReplay_ ? command.get() : nullptr;
    if (const auto error = checkBeforeCommit())
        return Result::failure(*error);
    if (!scene_->canRemovePreparedSubtree(*prepared)) {
        pendingAnimationReplay_.reset();
        return Result::failure({api::ErrorCode::RevisionConflict,
                                QStringLiteral("删除候选来源已变化。"), "entityId",
                                api::Recovery::Refetch, apiDocumentState()});
    }
    pushHistory(command.release());
    result.state = apiDocumentState();
    return Result::success(std::move(result));
}
api::ApiResult<api::MutationResult>
SceneViewModel::setParentExplicit(const api::EntitySetParentRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    const auto fail = [this](api::ErrorCode code, const QString& message, const QString& field) {
        return Result::failure(
            {code, message, field, api::Recovery::CorrectInput, apiDocumentState()});
    };
    if (request.mode != QStringLiteral("keepLocal"))
        return fail(api::ErrorCode::UnsupportedOperation, QStringLiteral("只支持 keepLocal。"),
                    "mode");
    const auto* node = scene_->find(request.entityId);
    if (!node)
        return fail(api::ErrorCode::NotFound, QStringLiteral("对象不存在。"), "entityId");
    if (request.parentId && !scene_->find(request.parentId))
        return fail(api::ErrorCode::NotFound, QStringLiteral("父对象不存在。"), "parentId");
    for (auto ancestor = request.parentId; ancestor; ancestor = scene_->find(ancestor)->parent)
        if (ancestor == request.entityId)
            return fail(api::ErrorCode::InvalidArgument, QStringLiteral("不能形成自身或父子循环。"),
                        "parentId");
    api::MutationResult result;
    if (node->parent == request.parentId) {
        if (const auto error = checkBeforeCommit())
            return Result::failure(*error);
        result.state = apiDocumentState();
        return Result::success(std::move(result));
    }
    std::string diagnostic;
    auto candidate = scene_->prepareParentChange(request.entityId, request.parentId, diagnostic);
    if (!candidate)
        return fail(api::ErrorCode::UnsupportedOperation, QString::fromStdString(diagnostic),
                    "parentId");
    auto prepared = std::make_shared<core::Scene::PreparedParentChange>(std::move(*candidate));
    if (const auto error = preflightParentAnimation(*prepared, true))
        return Result::failure(*error);
    const auto apply = [this, prepared](bool forward) {
        emit structureAboutToChange();
        const bool installed = forward ? scene_->installPreparedParentChange(*prepared)
                                       : scene_->restorePreparedParentChange(*prepared);
        Q_ASSERT(installed);
        queueHistoryNotifications(true);
    };
    auto command = std::make_unique<EditCommand>(
        QStringLiteral("更换父对象"),
        [apply] {
            apply(false);
        },
        [apply] {
            apply(true);
        },
        [this, prepared](bool forward, QString& error) {
            return prepareParentReplay(*prepared, forward, error);
        });
    pendingAnimationCommand_ = pendingAnimationReplay_ ? command.get() : nullptr;
    result.status = api::ResultStatus::Committed;
    result.affectedEntityIds = {request.entityId};
    result.undoable = true;
    if (const auto error = checkBeforeCommit())
        return Result::failure(*error);
    if (!scene_->canInstallPreparedParentChange(*prepared)) {
        pendingAnimationReplay_.reset();
        return fail(api::ErrorCode::RevisionConflict, QStringLiteral("换父候选来源已变化。"),
                    "parentId");
    }
    pushHistory(command.release());
    result.state = apiDocumentState();
    return Result::success(std::move(result));
}
api::ApiResult<api::CollectionMutationResult>
SceneViewModel::commitCollections(const std::vector<core::SceneCollection>& after,
                                  core::CollectionId id, std::vector<core::EntityId> affected,
                                  const QString& label) {
    using Result = api::ApiResult<api::CollectionMutationResult>;
    pendingAnimationReplay_.reset();
    if (rejectAnimationEdit())
        return Result::failure({api::ErrorCode::Busy, QStringLiteral("当前动画状态不能编辑集合。"),
                                {}, api::Recovery::Wait, apiDocumentState()});
    const auto before = scene_->collections();
    auto candidate = *scene_;
    if (!candidate.replaceCollections(after))
        return Result::failure({api::ErrorCode::InvalidArgument,
                                QStringLiteral("集合名称、身份或成员归属无效。"), "collectionId",
                                api::Recovery::CorrectInput, apiDocumentState()});
    api::CollectionMutationResult result;
    result.collectionId = id;
    if (before == after) {
        if (const auto error = checkBeforeCommit())
            return Result::failure(*error);
        result.command.state = apiDocumentState();
        return Result::success(std::move(result));
    }
    const auto buffer = std::make_shared<std::vector<core::SceneCollection>>(after);
    const auto apply = [this, buffer] {
        const bool installed = scene_->exchangeCollectionSnapshot(*buffer);
        Q_ASSERT(installed);
        queueHistoryNotifications(false);
    };
    const auto prepare = [this, before, after](bool forward, QString& error)
        -> std::optional<PreparedAnimationReplay> {
        auto geometry = preparePoseGeometry(animationInputs_, {}, nullptr, std::nullopt,
                                             forward ? &after : &before);
        if (!geometry) {
            error = QStringLiteral("无法准备完整集合可见性快照。");
            return std::nullopt;
        }
        return prepareSharedAnimationReplay(error, std::move(geometry));
    };
    auto command = std::make_unique<EditCommand>(
        label,
        [apply] {
            apply();
        },
        [apply] {
            apply();
        }, prepare);
    if (animationMode_ != AnimationMode::Base) {
        QString error;
        pendingAnimationReplay_ = prepare(true, error);
        if (!pendingAnimationReplay_)
            return Result::failure({api::ErrorCode::UnsupportedTransform, error, {},
                                    api::Recovery::CorrectInput, apiDocumentState()});
        pendingAnimationCommand_ = command.get();
    }
    std::sort(affected.begin(), affected.end());
    result.command.status = api::ResultStatus::Committed;
    result.command.affectedEntityIds = std::move(affected);
    result.command.undoable = true;
    if (const auto error = checkBeforeCommit())
        return Result::failure(*error);
    if (scene_->collections() != before) {
        pendingAnimationReplay_.reset();
        return Result::failure({api::ErrorCode::RevisionConflict,
                                QStringLiteral("集合候选来源已变化。"), {},
                                api::Recovery::Refetch, apiDocumentState()});
    }
    pushHistory(command.release());
    result.command.state = apiDocumentState();
    return Result::success(std::move(result));
}
api::ApiResult<api::CollectionMutationResult>
SceneViewModel::createCollectionExplicit(const api::CollectionCreateRequest& request) {
    using Result = api::ApiResult<api::CollectionMutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (request.name.trimmed().isEmpty() || request.name.toUcs4().size() > 256)
        return Result::failure({api::ErrorCode::InvalidArgument,
                                QStringLiteral("名称须包含 1～256 个字符。"), "name",
                                api::Recovery::CorrectInput, apiDocumentState()});
    auto candidate = *scene_;
    const auto id = candidate.createCollection(request.name.toUtf8().toStdString());
    if (!id)
        return Result::failure({api::ErrorCode::LimitExceeded, QStringLiteral("集合身份已耗尽。"),
                                "collectionId", api::Recovery::None, apiDocumentState()});
    auto after = candidate.collections();
    after.back().visible = request.visible;
    return commitCollections(after, id, {}, QStringLiteral("新建集合"));
}
api::ApiResult<api::CollectionMutationResult>
SceneViewModel::updateCollectionExplicit(const api::CollectionUpdateRequest& request) {
    using Result = api::ApiResult<api::CollectionMutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (!request.name && !request.visible)
        return Result::failure({api::ErrorCode::InvalidArgument,
                                QStringLiteral("至少指定名称或显隐。"),
                                {},
                                api::Recovery::CorrectInput,
                                apiDocumentState()});
    if (request.name && (request.name->trimmed().isEmpty() || request.name->toUcs4().size() > 256))
        return Result::failure({api::ErrorCode::InvalidArgument,
                                QStringLiteral("名称须包含 1～256 个字符。"), "name",
                                api::Recovery::CorrectInput, apiDocumentState()});
    auto after = scene_->collections();
    for (auto& collection : after) {
        if (collection.id != request.collectionId)
            continue;
        if (request.name)
            collection.name = request.name->toUtf8().toStdString();
        if (request.visible)
            collection.visible = *request.visible;
        return commitCollections(after, collection.id,
                                 {collection.members.begin(), collection.members.end()},
                                 QStringLiteral("更新集合"));
    }
    return Result::failure({api::ErrorCode::NotFound, QStringLiteral("集合不存在。"),
                            "collectionId", api::Recovery::Refetch, apiDocumentState()});
}
api::ApiResult<api::CollectionMutationResult>
SceneViewModel::deleteCollectionExplicit(const api::CollectionDeleteRequest& request) {
    using Result = api::ApiResult<api::CollectionMutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    auto after = scene_->collections();
    const auto collection = std::find_if(after.begin(), after.end(), [&](const auto& value) {
        return value.id == request.collectionId;
    });
    if (collection == after.end())
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("集合不存在。"),
                                "collectionId", api::Recovery::Refetch, apiDocumentState()});
    std::vector<core::EntityId> affected(collection->members.begin(), collection->members.end());
    after.erase(collection);
    return commitCollections(after, request.collectionId, std::move(affected),
                             QStringLiteral("删除集合（保留对象）"));
}
api::ApiResult<api::CollectionMutationResult>
SceneViewModel::assignCollectionExplicit(const api::CollectionAssignRequest& request) {
    using Result = api::ApiResult<api::CollectionMutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (!scene_->find(request.entityId))
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("对象不存在。"),
                                "entityId", api::Recovery::Refetch, apiDocumentState()});
    auto after = scene_->collections();
    if (request.collectionId && std::none_of(after.begin(), after.end(), [&](const auto& value) {
            return value.id == request.collectionId;
        }))
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("集合不存在。"),
                                "collectionId", api::Recovery::Refetch, apiDocumentState()});
    for (auto& collection : after) {
        collection.members.erase(request.entityId);
        if (collection.id == request.collectionId)
            collection.members.insert(request.entityId);
    }
    return commitCollections(after, request.collectionId, {request.entityId},
                             QStringLiteral("更改集合成员"));
}
api::ApiResult<api::MutationResult>
SceneViewModel::createCameraExplicit(const api::CameraCreateRequest& request) {
    if (const auto error = validateApiMutation(request))
        return api::ApiResult<api::MutationResult>::failure(*error);
    core::Scene::EntityCreateOptions options;
    options.name = request.name.toUtf8().toStdString();
    options.parent = request.parentId;
    options.transform = request.transform;
    options.visible = request.visible;
    options.camera = request.camera;
    return commitEntityCreate(options, false);
}
api::ApiResult<api::MutationResult>
SceneViewModel::createLightExplicit(const api::LightCreateRequest& request) {
    if (const auto error = validateApiMutation(request))
        return api::ApiResult<api::MutationResult>::failure(*error);
    core::Scene::EntityCreateOptions options;
    options.name = request.name.toUtf8().toStdString();
    options.parent = request.parentId;
    options.transform = request.transform;
    options.visible = request.visible;
    options.light = request.light;
    return commitEntityCreate(options, false);
}
api::ApiResult<api::MutationResult>
SceneViewModel::updateCameraExplicit(const api::CameraUpdateRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    return commitCameraUpdate(request.entityId, request.camera);
}
api::ApiResult<api::MutationResult>
SceneViewModel::commitCameraUpdate(core::EntityId id, const core::CameraComponent& camera) {
    using Result = api::ApiResult<api::MutationResult>;
    if (animationMode_ == AnimationMode::Playing || animationMode_ == AnimationMode::PoseDraft ||
        apiSubmitting_)
        return Result::failure({api::ErrorCode::Busy,
                                QStringLiteral("当前动画状态不允许此相机属性编辑。"), {},
                                api::Recovery::Wait, apiDocumentState()});
    pendingAnimationReplay_.reset();
    const auto* node = scene_->find(id);
    if (!node)
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("对象不存在。"),
                                "entityId", api::Recovery::Refetch, apiDocumentState()});
    if (!node->camera)
        return Result::failure({api::ErrorCode::UnsupportedOperation,
                                QStringLiteral("目标不是已有相机。"), "entityId",
                                api::Recovery::CorrectInput, apiDocumentState()});
    if (!camera.isValid())
        return Result::failure({api::ErrorCode::InvalidArgument, QStringLiteral("相机参数无效。"),
                                "camera", api::Recovery::CorrectInput, apiDocumentState()});
    const auto before = *node->camera;
    api::MutationResult result;
    if (before == camera) {
        if (const auto error = checkBeforeCommit())
            return Result::failure(*error);
        result.state = apiDocumentState();
        return Result::success(std::move(result));
    }
    const auto apply = [this, id](const core::CameraComponent& value) {
        const bool installed = scene_->setCamera(id, value);
        Q_ASSERT(installed);
        queueEntityNotification(id);
    };
    const auto prepare = [this, id, before, after = camera](
                             bool forward, QString& error)
        -> std::optional<PreparedAnimationReplay> {
        auto geometry = std::make_shared<renderer_gl::PoseGeometry>(*installedAnimationPose_->geometry);
        const auto target = geometry->entries.find(id);
        if (target == geometry->entries.end()) {
            error = QStringLiteral("历史相机不存在。");
            return std::nullopt;
        }
        target->second.camera = forward ? after : before;
        return prepareSharedAnimationReplay(error, std::move(geometry));
    };
    auto command = std::make_unique<EditCommand>(
        QStringLiteral("相机"),
        [apply, before] {
            apply(before);
        },
        [apply, after = camera] {
            apply(after);
        }, prepare);
    if (animationMode_ != AnimationMode::Base) {
        QString error;
        pendingAnimationReplay_ = prepare(true, error);
        if (!pendingAnimationReplay_)
            return Result::failure({api::ErrorCode::UnsupportedTransform, error, "camera",
                                    api::Recovery::CorrectInput, apiDocumentState()});
        pendingAnimationCommand_ = command.get();
    }
    result.status = api::ResultStatus::Committed;
    result.affectedEntityIds = {id};
    result.undoable = true;
    if (const auto error = checkBeforeCommit())
        return Result::failure(*error);
    pushHistory(command.release());
    result.state = apiDocumentState();
    return Result::success(std::move(result));
}
api::ApiResult<api::MutationResult>
SceneViewModel::updateLightExplicit(const api::LightUpdateRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    return commitLightUpdate(request.entityId, request.light);
}
api::ApiResult<api::MutationResult>
SceneViewModel::commitLightUpdate(core::EntityId id, const core::LightComponent& light) {
    using Result = api::ApiResult<api::MutationResult>;
    if (animationMode_ == AnimationMode::Playing || animationMode_ == AnimationMode::PoseDraft ||
        apiSubmitting_)
        return Result::failure({api::ErrorCode::Busy,
                                QStringLiteral("当前动画状态不允许此灯光属性编辑。"), {},
                                api::Recovery::Wait, apiDocumentState()});
    pendingAnimationReplay_.reset();
    const auto* node = scene_->find(id);
    if (!node)
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("对象不存在。"),
                                "entityId", api::Recovery::Refetch, apiDocumentState()});
    if (!node->light)
        return Result::failure({api::ErrorCode::UnsupportedOperation,
                                QStringLiteral("目标不是已有方向光。"), "entityId",
                                api::Recovery::CorrectInput, apiDocumentState()});
    if (!light.isValid())
        return Result::failure({api::ErrorCode::InvalidArgument, QStringLiteral("方向光参数无效。"),
                                "light", api::Recovery::CorrectInput, apiDocumentState()});
    const auto before = *node->light;
    api::MutationResult result;
    if (before == light) {
        if (const auto error = checkBeforeCommit())
            return Result::failure(*error);
        result.state = apiDocumentState();
        return Result::success(std::move(result));
    }
    const auto apply = [this, id](const core::LightComponent& value) {
        const bool installed = scene_->setLight(id, value);
        Q_ASSERT(installed);
        queueEntityNotification(id);
    };
    const auto prepare = [this, id, before, after = light](
                             bool forward, QString& error)
        -> std::optional<PreparedAnimationReplay> {
        auto geometry = std::make_shared<renderer_gl::PoseGeometry>(*installedAnimationPose_->geometry);
        const auto target = geometry->entries.find(id);
        if (target == geometry->entries.end()) {
            error = QStringLiteral("历史灯光不存在。");
            return std::nullopt;
        }
        target->second.light = forward ? after : before;
        return prepareSharedAnimationReplay(error, std::move(geometry));
    };
    auto command = std::make_unique<EditCommand>(
        QStringLiteral("方向光"),
        [apply, before] {
            apply(before);
        },
        [apply, after = light] {
            apply(after);
        }, prepare);
    if (animationMode_ != AnimationMode::Base) {
        QString error;
        pendingAnimationReplay_ = prepare(true, error);
        if (!pendingAnimationReplay_)
            return Result::failure({api::ErrorCode::UnsupportedTransform, error, "light",
                                    api::Recovery::CorrectInput, apiDocumentState()});
        pendingAnimationCommand_ = command.get();
    }
    result.status = api::ResultStatus::Committed;
    result.affectedEntityIds = {id};
    result.undoable = true;
    if (const auto error = checkBeforeCommit())
        return Result::failure(*error);
    pushHistory(command.release());
    result.state = apiDocumentState();
    return Result::success(std::move(result));
}
api::ApiResult<api::MutationResult>
SceneViewModel::commitEntityUpdate(core::EntityId id, const api::EntityPatch& changes,
                                   const QString& label) {
    using Result = api::ApiResult<api::MutationResult>;
    pendingAnimationReplay_.reset();
    if (animationMode_ == AnimationMode::Playing || animationMode_ == AnimationMode::PoseDraft ||
        apiSubmitting_ || (changes.transform && animationMode_ != AnimationMode::Base))
        return Result::failure({api::ErrorCode::Busy,
                                QStringLiteral("当前动画状态不允许此对象属性编辑。"), {},
                                api::Recovery::Wait, apiDocumentState()});
    const auto fail = [this](api::ErrorCode code, const QString& message, const QString& field) {
        return Result::failure(
            {code, message, field, api::Recovery::CorrectInput, apiDocumentState()});
    };
    const auto* node = scene_->find(id);
    if (!node)
        return fail(api::ErrorCode::NotFound, QStringLiteral("对象不存在。"),
                    QStringLiteral("entityId"));
    if (!changes.name && !changes.transform && !changes.surface && !changes.visible)
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("至少指定一项对象属性。"), {});
    if (changes.name && (changes.name->trimmed().isEmpty() || changes.name->toUcs4().size() > 256))
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("名称须包含 1～256 个字符。"),
                    QStringLiteral("name"));
    if (changes.transform && !changes.transform->isValid())
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("局部变换无效。"),
                    QStringLiteral("transform"));
    if (changes.surface && !changes.surface->isValid())
        return fail(api::ErrorCode::InvalidArgument, QStringLiteral("表面颜色须在 0～1 之间。"),
                    QStringLiteral("surface"));
    struct Properties {
        std::string name;
        core::Transform transform;
        core::SurfaceStyle surface;
        bool visible;
    };
    const Properties before{node->name, node->transform, node->surface, node->visible};
    auto after = before;
    if (changes.name)
        after.name = changes.name->toUtf8().toStdString();
    if (changes.transform && !sameTransform(*changes.transform, before.transform)) {
        after.transform = *changes.transform;
        // 未改变的旋转保留已确认值；真正改变的旋转只在准备阶段归一化一次。
        if (after.transform.rotation != before.transform.rotation)
            after.transform.rotation = glm::normalize(after.transform.rotation);
    }
    if (changes.surface)
        after.surface = *changes.surface;
    if (changes.visible)
        after.visible = *changes.visible;
    api::MutationResult result;
    if (before.name == after.name && sameTransform(before.transform, after.transform) &&
        before.surface == after.surface && before.visible == after.visible) {
        if (const auto error = checkBeforeCommit())
            return Result::failure(*error);
        result.state = apiDocumentState();
        return Result::success(std::move(result));
    }
    const auto nameBuffer = std::make_shared<std::string>(after.name);
    const bool changesName = before.name != after.name;
    const auto apply = [this, id, nameBuffer, changesName](const Properties& value) {
        const bool renamed = !changesName || scene_->exchangeEntityName(id, *nameBuffer);
        // 历史安装已验证的精确快照，Undo/Redo 不重复归一化。
        const bool transformed = scene_->installTransformSnapshot(id, value.transform);
        const bool surfaced = scene_->setSurface(id, value.surface);
        const bool shown = scene_->setVisible(id, value.visible);
        Q_ASSERT(renamed && transformed && surfaced && shown);
        queueEntityNotification(id);
    };
    const bool renameOnly = changes.name && !changes.transform && !changes.surface && !changes.visible;
    const auto prepare = [this, id, before, after, renameOnly](bool forward, QString& error)
        -> std::optional<PreparedAnimationReplay> {
        if (renameOnly)
            return prepareSharedAnimationReplay(error);
        const auto& target = forward ? after : before;
        std::string diagnostic;
        auto inputs = scene_->animationPoseInputs(diagnostic);
        if (!inputs) {
            error = QString::fromStdString(diagnostic);
            return std::nullopt;
        }
        for (auto& input : *inputs)
            if (input.entity == id)
                input.base = target.transform;
        auto geometry = preparePoseGeometry(*inputs, {}, nullptr, {{id, target.visible}});
        return prepareAnimationReplay(std::move(*inputs), scene_->animation(),
                                       std::move(geometry), error);
    };
    auto command = std::make_unique<EditCommand>(
        label,
        [apply, before] {
            apply(before);
        },
        [apply, after] {
            apply(after);
        }, prepare);
    if (animationMode_ != AnimationMode::Base) {
        QString error;
        pendingAnimationReplay_ = prepare(true, error);
        if (!pendingAnimationReplay_)
            return fail(api::ErrorCode::UnsupportedTransform, error, {});
        pendingAnimationCommand_ = command.get();
    }
    result.status = api::ResultStatus::Committed;
    result.affectedEntityIds = {id};
    result.undoable = true;
    if (const auto error = checkBeforeCommit())
        return Result::failure(*error);
    pushHistory(command.release());
    result.state = apiDocumentState();
    return Result::success(std::move(result));
}

bool SceneViewModel::isEditMode() const {
    return editedEntity_ != core::kInvalidEntity;
}
core::EntityId SceneViewModel::editedEntity() const {
    return editedEntity_;
}
QString SceneViewModel::editModeDisabledReason() const {
    if (isEditMode()) {
        return {};
    }
    const auto id = selection_.selectedEntity();
    const auto* node = scene_->find(id);
    if (previewCamera_ != 0) {
        return QStringLiteral("相机预览为只读；请先返回编辑视图。");
    }
    if (!node || !viewportVisibility_.isVisible(*scene_, id) || node->camera || node->light) {
        return QStringLiteral("请选择可见网格；相机、灯光或隐藏对象不能进入编辑模式。");
    }
    if (node->editableMesh == 0 && node->primitive != core::PrimitiveKind::Cube) {
        return QStringLiteral("当前仅原生立方体和已有可编辑网格支持编辑模式。");
    }
    return {};
}
bool SceneViewModel::setEditMode(bool enabled) {
    if (rejectAnimationEdit())
        return false;
    if (enabled && animationMode_ == AnimationMode::PreviewPaused && !setAnimationPreview(false))
        return false;
    if (enabled == isEditMode()) {
        return true;
    }
    if (enabled) {
        const auto reason = editModeDisabledReason();
        if (!reason.isEmpty()) {
            emit operationFailed(reason);
            return false;
        }
        // 明确采用取消规则：不把尚未确认的对象预览固化到首次网格转换中。
        cancelTransformEdit();
        const auto id = selection_.selectedEntity();
        if (!makeEditable(id)) {
            return false;
        }
        editedEntity_ = id;
        editedMesh_ = scene_->find(id)->editableMesh;
        componentSelection_ = {};
        componentSelection_.selectAll(scene_->editableMesh(editedMesh_)->content->source);
    } else {
        cancelTransformEdit();
        editedEntity_ = core::kInvalidEntity;
        editedMesh_ = 0;
        componentSelection_ = {};
    }
    viewportVisibility_.clearElements();
    viewportVisibility_.editedEntity = editedEntity_;
    notifyComponentSelection();
    emit viewportVisibilityChanged();
    emit editModeChanged(enabled);
    return true;
}
const ComponentSelection& SceneViewModel::componentSelection() const {
    return componentSelection_;
}
quint64 SceneViewModel::componentSelectionRevision() const {
    return componentSelectionRevision_;
}
void SceneViewModel::notifyComponentSelection() {
    finishComponentTransform(false);
    filterHiddenSelection();
    ++componentSelectionRevision_;
    if (apiSubmitting_)
        historyComponentSelectionNotification_ = true;
    else
        emit componentSelectionChanged();
}
void SceneViewModel::reconcileEditContext() {
    if (!isEditMode()) {
        return;
    }
    const auto* node = scene_->find(editedEntity_);
    if (selection_.selectedEntity() != editedEntity_ || !node ||
        !scene_->isVisible(editedEntity_) || node->editableMesh != editedMesh_) {
        setEditMode(false);
        return;
    }
    const bool reconciled =
        componentSelection_.reconcile(scene_->editableMesh(editedMesh_)->content->source);
    if (filterHiddenSelection() || reconciled) {
        notifyComponentSelection();
    }
}
void SceneViewModel::setSelectionDomain(SelectionDomain domain) {
    if (isEditMode() &&
        componentSelection_.setDomain(scene_->editableMesh(editedMesh_)->content->source, domain)) {
        notifyComponentSelection();
    }
}
void SceneViewModel::selectComponent(ComponentId id, SelectionOperation operation) {
    if (!isComponentVisible(id))
        return;
    if (isEditMode() && componentSelection_.select(
                            scene_->editableMesh(editedMesh_)->content->source, id, operation)) {
        notifyComponentSelection();
    }
}
void SceneViewModel::selectAllComponents() {
    if (isEditMode() &&
        componentSelection_.selectAll(scene_->editableMesh(editedMesh_)->content->source)) {
        notifyComponentSelection();
    }
}
void SceneViewModel::selectComponents(const std::set<ComponentId>& ids,
                                      SelectionOperation operation) {
    if (isEditMode() && componentSelection_.selectMany(
                            scene_->editableMesh(editedMesh_)->content->source, ids, operation)) {
        notifyComponentSelection();
    }
}
void SceneViewModel::clearComponentSelection() {
    if (isEditMode() && componentSelection_.clear()) {
        notifyComponentSelection();
    }
}
bool SceneViewModel::selectEdgePath(core::modeling::EdgeKey seed, bool ring,
                                    SelectionOperation operation) {
    if (!isEditMode() || previewCamera_ != 0 ||
        componentSelection_.domain() != SelectionDomain::Edge) {
        emit operationFailed(QStringLiteral("Loop / Ring 需要编辑模式中的边选择。"));
        return false;
    }
    if (!isComponentVisible(ComponentId::edge(seed)))
        return false;
    const auto& mesh = scene_->editableMesh(editedMesh_)->content->source;
    const auto result = core::modeling::selectEdgePath(
        mesh, seed, ring ? core::modeling::EdgeSelectionKind::Ring
                        : core::modeling::EdgeSelectionKind::Loop);
    if (!result.edges) {
        emit operationFailed(QString::fromStdString(result.error));
        return false;
    }
    std::set<ComponentId> ids;
    for (const auto edge : *result.edges) {
        const auto id = ComponentId::edge(edge);
        if (isComponentVisible(id))
            ids.insert(id);
    }
    const auto before = componentSelection_;
    componentSelection_.selectMany(mesh, ids, operation);
    if (componentSelection_.selectedIds().contains(ComponentId::edge(seed)))
        componentSelection_.select(mesh, ComponentId::edge(seed), SelectionOperation::Add);
    if (componentSelection_ != before)
        notifyComponentSelection();
    return true;
}
bool SceneViewModel::selectConnected(std::optional<ComponentId> seed) {
    if (!isEditMode() || previewCamera_ != 0)
        return false;
    seed = seed ? seed : componentSelection_.activeId();
    if (!seed || !isComponentVisible(*seed)) {
        emit operationFailed(QStringLiteral("请将鼠标放在源组件上，或先选择一个活动组件。"));
        return false;
    }
    const auto& mesh = scene_->editableMesh(editedMesh_)->content->source;
    if (!ComponentSelection::elements(mesh, componentSelection_.domain()).contains(*seed))
        return false;
    auto vertex = seed->first;
    if (componentSelection_.domain() == SelectionDomain::Face) {
        for (const auto& face : mesh.faces)
            if (face.id == seed->first)
                vertex = face.corners.front().vertex;
    }
    const auto connected = core::modeling::connectedVertices(mesh, vertex);
    ComponentSelection projected;
    std::set<ComponentId> ids;
    for (const auto id : connected)
        ids.insert({id});
    projected.selectMany(mesh, ids, SelectionOperation::Replace);
    projected.setDomain(mesh, componentSelection_.domain());
    ids = projected.selectedIds();
    std::erase_if(ids, [this](ComponentId id) { return !isComponentVisible(id); });
    const auto before = componentSelection_;
    componentSelection_.selectMany(mesh, ids, SelectionOperation::Add);
    componentSelection_.select(mesh, *seed, SelectionOperation::Add);
    if (componentSelection_ != before)
        notifyComponentSelection();
    return true;
}
const core::ViewportVisibility& SceneViewModel::viewportVisibility() const {
    return viewportVisibility_;
}
bool SceneViewModel::isComponentVisible(ComponentId id) const {
    if (!isEditMode())
        return false;
    const auto& mesh = scene_->editableMesh(editedMesh_)->content->source;
    switch (componentSelection_.domain()) {
        case SelectionDomain::Vertex:
            return viewportVisibility_.isVertexVisible(mesh, id.first);
        case SelectionDomain::Edge:
            return viewportVisibility_.isEdgeVisible(mesh, {id.first, id.second});
        case SelectionDomain::Face:
            return viewportVisibility_.isFaceVisible(mesh, id.first);
    }
    return false;
}
bool SceneViewModel::filterHiddenSelection() {
    if (!isEditMode() || !viewportVisibility_.hasHiddenElements())
        return false;
    std::set<ComponentId> hidden;
    for (const auto id : componentSelection_.selectedIds())
        if (!isComponentVisible(id))
            hidden.insert(id);
    return componentSelection_.selectMany(scene_->editableMesh(editedMesh_)->content->source,
                                          hidden, SelectionOperation::Remove);
}
void SceneViewModel::notifyViewportVisibility() {
    cancelTransformEdit();
    notifyComponentSelection();
    emit viewportVisibilityChanged();
}
bool SceneViewModel::hideSelection() {
    if (rejectAnimationEdit() || previewCamera_ != 0)
        return false;
    auto candidate = viewportVisibility_;
    cancelTransformEdit();
    if (isEditMode()) {
        if (componentSelection_.selectedIds().empty())
            return false;
        for (const auto id : componentSelection_.selectedIds()) {
            switch (componentSelection_.domain()) {
                case SelectionDomain::Vertex:
                    candidate.vertices.insert(id.first);
                    break;
                case SelectionDomain::Edge:
                    candidate.edges.emplace(id.first, id.second);
                    break;
                case SelectionDomain::Face:
                    candidate.faces.insert(id.first);
                    break;
            }
        }
    } else {
        const auto id = selection_.selectedEntity();
        if (!viewportVisibility_.isVisible(*scene_, id))
            return false;
        candidate.hiddenObjects.insert(id);
    }
    if (!commitViewportVisibility(std::move(candidate)))
        return false;
    if (!isEditMode())
        selection_.setSelectedEntity(0);
    return true;
}
bool SceneViewModel::revealHidden() {
    if (rejectAnimationEdit() || previewCamera_ != 0)
        return false;
    auto candidate = viewportVisibility_;
    if (isEditMode())
        candidate.clearElements();
    else
        candidate.hiddenObjects.clear();
    return commitViewportVisibility(std::move(candidate));
}
bool SceneViewModel::toggleLocalView() {
    if (rejectAnimationEdit() || previewCamera_ != 0)
        return false;
    auto candidate = viewportVisibility_;
    if (viewportVisibility_.localRoot) {
        candidate.localRoot = 0;
    } else {
        const auto id = selection_.selectedEntity();
        if (!viewportVisibility_.isVisible(*scene_, id)) {
            emit operationFailed(QStringLiteral("请选择可见对象再进入局部视图。"));
            return false;
        }
        candidate.localRoot = id;
    }
    if (!commitViewportVisibility(std::move(candidate)))
        return false;
    emit operationCompleted(viewportVisibility_.localRoot
                                ? QStringLiteral("已进入局部视图：仅显示所选子树。")
                                : QStringLiteral("已退出局部视图：恢复原显示范围。"));
    return true;
}
bool SceneViewModel::rejectObjectEdit() {
    if (rejectAnimationEdit())
        return true;
    if (!isEditMode()) {
        return false;
    }
    emit operationFailed(QStringLiteral("当前是编辑模式；请先返回对象模式，再执行整对象操作。"));
    return true;
}

std::shared_ptr<const assets::AssetManager> SceneViewModel::assets() const {
    return assets_;
}

api::ApiResult<api::ImportGltfResult>
SceneViewModel::importGltfExplicit(const api::ImportGltfRequest& request,
                                   const assets::FileReadPolicy& policy) {
    using Result = api::ApiResult<api::ImportGltfResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (animationMode_ != AnimationMode::Base)
        return Result::failure({api::ErrorCode::Busy, QStringLiteral("请先关闭动画预览再导入。"),
                                {}, api::Recovery::Wait, apiDocumentState()});
    const auto invalid = [this](const QString& field, const QString& message) {
        return Result::failure(api::meshArgumentError(apiDocumentState(), field, message));
    };
    if (request.path.isEmpty())
        return invalid("path", QStringLiteral("文件路径不能为空。"));
    if (request.name.trimmed().isEmpty() || request.name.toUcs4().size() > 256)
        return invalid("name", QStringLiteral("名称须包含 1～256 个字符。"));
    if (!request.transform.isValid())
        return invalid("transform", QStringLiteral("局部变换无效。"));
    if (request.parentId && !scene_->find(request.parentId))
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("父对象不存在。"),
                                "parentId", api::Recovery::Refetch, apiDocumentState()});
    QScopedValueRollback submitting(apiSubmitting_, true);
    const auto imported = assets_->importGltf(request.path, policy);
    if (!imported.scene) {
        const auto code = imported.failure == assets::FileReadFailure::PathDenied
                              ? api::ErrorCode::PathDenied
                          : imported.failure == assets::FileReadFailure::InvalidData
                              ? api::ErrorCode::InvalidArgument
                              : api::ErrorCode::IoError;
        return Result::failure(
            {code, imported.error, "path", api::Recovery::CorrectInput, apiDocumentState()});
    }
    const auto limited = [this] {
        return Result::failure({api::ErrorCode::LimitExceeded,
                                QStringLiteral("导入子树超过节点、候选或返回结果预算。"), "path",
                                api::Recovery::CorrectInput, apiDocumentState()});
    };
    using Options = core::Scene::SubtreeNodeOptions;
    std::vector<Options> options;
    options.reserve(api::limits::importedEntities);
    // 只核算将发布的节点、输入副本、身份映射和命令；不可见资源缓存另有读盘预算。
    std::size_t candidateBytes =
        sizeof(core::Scene::PreparedSubtree) + options.capacity() * sizeof(Options) + 1024;
    constexpr auto nodeBytes = sizeof(core::SceneNode) + 12 * sizeof(void*) +
                               8 * sizeof(core::EntityId) + 4 * sizeof(std::size_t);
    const auto append = [&](Options node) {
        const auto bytes = nodeBytes + 2 * (node.name.capacity() + 1);
        if (options.size() >= api::limits::importedEntities ||
            bytes > api::limits::candidateBytes - candidateBytes)
            return false;
        candidateBytes += bytes;
        options.push_back(std::move(node));
        return true;
    };
    Options wrapper;
    wrapper.name = request.name.toUtf8().toStdString();
    wrapper.transform = request.transform;
    if (!append(std::move(wrapper)))
        return limited();
    struct PendingNode {
        std::size_t source, parent;
    };
    std::vector<PendingNode> pending;
    if (imported.scene->roots.size() > api::limits::importedEntities - options.size())
        return limited();
    for (auto root = imported.scene->roots.rbegin(); root != imported.scene->roots.rend(); ++root)
        pending.push_back({*root, 0});
    while (!pending.empty()) {
        const auto next = pending.back();
        pending.pop_back();
        const auto& source = imported.scene->nodes[next.source];
        const auto index = options.size();
        Options node;
        node.name = source.name;
        node.parentIndex = next.parent;
        node.transform = source.transform;
        if (source.meshes.size() == 1) {
            const auto mesh = source.meshes.front();
            node.meshRenderer = core::MeshRendererComponent{mesh, assets_->mesh(mesh)->material};
        }
        if (!append(std::move(node)))
            return limited();
        const auto children = source.meshes.size() > 1 ? source.meshes.size() : 0;
        const auto remaining = api::limits::importedEntities - options.size() - pending.size();
        if (source.children.size() > remaining || children > remaining - source.children.size())
            return limited();
        for (std::size_t primitive = 0; primitive < children; ++primitive) {
            const auto mesh = source.meshes[primitive];
            Options child;
            child.name = "子网格 " + std::to_string(primitive);
            child.parentIndex = index;
            child.meshRenderer = core::MeshRendererComponent{mesh, assets_->mesh(mesh)->material};
            if (!append(std::move(child)))
                return limited();
        }
        for (auto child = source.children.rbegin(); child != source.children.rend(); ++child)
            pending.push_back({*child, index});
    }
    std::size_t responseBytes = 1024 + options.size() * 128;
    for (const auto& warning : imported.scene->warnings) {
        const auto bytes = std::size_t(warning.toUtf8().size());
        if (bytes > (api::limits::responseBytes - responseBytes) / 6)
            return limited();
        responseBytes += bytes * 6;
    }
    std::string diagnostic;
    auto candidate = scene_->prepareNewSubtree(options, request.parentId, diagnostic,
                                               api::limits::importedEntities);
    if (!candidate)
        return invalid("transform", QString::fromStdString(diagnostic));
    auto prepared = std::make_shared<core::Scene::PreparedSubtree>(std::move(*candidate));
    api::ImportGltfResult result;
    result.command.state = apiDocumentState();
    result.command.status = api::ResultStatus::Committed;
    result.command.undoable = true;
    result.command.path = QFileInfo(request.path).absoluteFilePath();
    for (const auto& mapping : prepared->entityIdMap())
        result.command.createdEntityIds.push_back(mapping.second);
    result.command.affectedEntityIds = result.command.createdEntityIds;
    result.rootEntityId = prepared->rootId();
    result.warnings = imported.scene->warnings;
    const auto apply = [this, prepared, created = result.command.createdEntityIds](bool forward) {
        emit structureAboutToChange();
        const bool installed = forward ? scene_->installPreparedSubtree(*prepared)
                                       : scene_->removePreparedSubtree(*prepared);
        Q_ASSERT(installed);
        std::optional<core::EntityId> selection;
        if (!forward &&
            std::find(created.begin(), created.end(), selection_.selectedEntity()) != created.end())
            selection = 0;
        queueHistoryNotifications(true, selection);
    };
    auto command = std::make_unique<EditCommand>(
        QStringLiteral("导入 glTF 子树"),
        [apply] {
            apply(false);
        },
        [apply] {
            apply(true);
        });
    if (const auto error = checkBeforeCommit())
        return Result::failure(*error);
    pushHistory(command.release());
    result.command.state.documentRevision = apiDocumentState().documentRevision;
    result.command.state.historyRevision = apiDocumentState().historyRevision;
    return Result::success(std::move(result));
}
api::ApiResult<api::ExportObjResult>
SceneViewModel::exportObjExplicit(const api::ExportObjRequest& request,
                                  const api::BeforeCommitGuard& fileGuard) {
    using Result = api::ApiResult<api::ExportObjResult>;
    if (const auto error = validateApiMutation(request))
        return Result::failure(*error);
    if (animationMode_ != AnimationMode::Base)
        return Result::failure({api::ErrorCode::Busy, QStringLiteral("请先关闭动画预览再导出OBJ。"),
                                {}, api::Recovery::Wait, apiDocumentState()});
    if (request.path.isEmpty() || request.entityId == 0 ||
        (request.mode != api::ExportObjMode::Source &&
         request.mode != api::ExportObjMode::Evaluated))
        return Result::failure(
            api::meshArgumentError(apiDocumentState(),
                                   request.path.isEmpty()  ? "path"
                                   : request.entityId == 0 ? "entityId"
                                                           : "mode",
                                   QStringLiteral("导出路径、对象身份或几何模式无效。")));
    const auto* node = scene_->find(request.entityId);
    if (!node)
        return Result::failure({api::ErrorCode::NotFound, QStringLiteral("对象不存在。"),
                                "entityId", api::Recovery::Refetch, apiDocumentState()});
    if (node->camera || node->light ||
        (!node->editableMesh && !node->meshRenderer &&
         node->primitive == core::PrimitiveKind::Empty))
        return Result::failure({api::ErrorCode::UnsupportedOperation,
                                QStringLiteral("相机、灯光和空对象不能导出 OBJ。"), "entityId",
                                api::Recovery::CorrectInput, apiDocumentState()});
    QScopedValueRollback submitting(apiSubmitting_, true);
    const glm::dmat4 world(scene_->worldMatrix(request.entityId));
    core::modeling::ObjExportResult encoded;
    if (node->editableMesh) {
        const auto& content = *scene_->editableMesh(node->editableMesh)->content;
        encoded = core::modeling::encodeObj(request.mode == api::ExportObjMode::Evaluated
                                                ? content.evaluatedMesh()
                                                : content.source,
                                            world, node->name);
    } else if (node->meshRenderer) {
        const auto* mesh = assets_->mesh(node->meshRenderer->mesh);
        if (!mesh)
            return Result::failure({api::ErrorCode::NotFound, QStringLiteral("静态网格资源缺失。"),
                                    "entityId", api::Recovery::Refetch, apiDocumentState()});
        encoded = core::modeling::encodeObj(mesh->data, world, node->name);
    } else {
        const auto mesh = node->primitive == core::PrimitiveKind::Cube
                              ? renderer_gl::PrimitiveFactory::createCube()
                          : node->primitive == core::PrimitiveKind::Sphere
                              ? renderer_gl::PrimitiveFactory::createSphere()
                              : renderer_gl::PrimitiveFactory::createPlane();
        encoded = core::modeling::encodeObj(mesh, world, node->name);
    }
    if (!encoded.text)
        return Result::failure({api::ErrorCode::InvalidTopology,
                                QString::fromStdString(encoded.error), "entityId",
                                api::Recovery::CorrectInput, apiDocumentState()});
    if (encoded.text->size() > api::limits::exportObjBytes)
        return Result::failure({api::ErrorCode::LimitExceeded,
                                QStringLiteral("OBJ 文本超过 64 MiB 限额。"), "path",
                                api::Recovery::CorrectInput, apiDocumentState()});
    api::ExportObjResult result;
    result.command.state = apiDocumentState();
    result.command.status = api::ResultStatus::Saved;
    result.command.path = QFileInfo(request.path).absoluteFilePath();
    result.entityId = request.entityId;
    result.mode = request.mode;
    result.byteLength = encoded.text->size();
    std::optional<api::ApiError> failure;
    const auto beforeCommit = [&] {
        failure = checkBeforeCommit();
        if (!failure && fileGuard)
            failure = fileGuard();
        return !failure;
    };
    QString error;
    bool overwriteDenied = false;
    const auto mode = request.overwrite ? assets::ObjDocument::WriteMode::ReplaceExisting
                                        : assets::ObjDocument::WriteMode::NewOnly;
    if (!assets::ObjDocument::write(request.path, *encoded.text, error, beforeCommit, mode,
                                    &overwriteDenied)) {
        if (failure)
            return Result::failure(*failure);
        return Result::failure(
            {overwriteDenied ? api::ErrorCode::OverwriteDenied : api::ErrorCode::IoError, error,
             "path", api::Recovery::CorrectInput, apiDocumentState()});
    }
    return Result::success(std::move(result));
}
core::EntityId SceneViewModel::importGltf(const QString& path) {
    if (rejectAnimationEdit(true))
        return 0;
    if (rejectObjectEdit()) {
        return core::kInvalidEntity;
    }
    cancelTransformEdit();
    const auto imported = assets_->importGltf(path);
    if (imported.scene == nullptr) {
        qWarning().noquote() << imported.error;
        emit operationFailed(imported.error);
        return core::kInvalidEntity;
    }
    const auto previousSelection = selection_.selectedEntity();
    // 解码和层级验证均已完成，之后才通知树模型进入一次结构事务。
    emit structureAboutToChange();
    const QString baseName = QFileInfo(path).completeBaseName();
    const auto root =
        scene_->createEntity(baseName.isEmpty() ? "导入对象" : baseName.toUtf8().toStdString());
    std::function<void(std::size_t, core::EntityId)> instantiate = [&](std::size_t index,
                                                                       core::EntityId parent) {
        const auto& source = imported.scene->nodes[index];
        const auto id = scene_->createEntity(source.name, parent);
        scene_->setTransform(id, source.transform);
        for (std::size_t p = 0; p < source.meshes.size(); ++p) {
            const auto mesh = source.meshes[p];
            const auto meshNode = source.meshes.size() == 1
                                      ? id
                                      : scene_->createEntity("子网格 " + std::to_string(p), id);
            scene_->setMeshRenderer(meshNode, {mesh, assets_->mesh(mesh)->material});
        }
        for (const auto child : source.children) {
            instantiate(child, id);
        }
    };
    for (const auto child : imported.scene->roots) {
        instantiate(child, root);
    }
    emit structureChanged();
    selection_.setSelectedEntity(root);
    pushHistory(new SubtreeCommand(*this, root, SubtreeCommand::Kind::Created, previousSelection));
    emit sceneChanged();
    QString message =
        QStringLiteral("已导入 %1%2")
            .arg(path, imported.cacheHit ? QStringLiteral("（已复用资源）") : QString{});
    for (const auto& warning : imported.scene->warnings) {
        message += "\n" + warning;
    }
    qInfo().noquote() << message;
    emit operationCompleted(message);
    return root;
}
core::EntityId SceneViewModel::createEntity(core::PrimitiveKind primitive) {
    if (rejectObjectEdit()) {
        return core::kInvalidEntity;
    }
    cancelTransformEdit();
    const char* name = "空对象";
    switch (primitive) {
        case core::PrimitiveKind::Cube:
            name = "立方体";
            break;
        case core::PrimitiveKind::Sphere:
            name = "球体";
            break;
        case core::PrimitiveKind::Plane:
            name = "平面";
            break;
        case core::PrimitiveKind::Empty:
            break;
    }
    core::Scene::EntityCreateOptions options;
    options.name = name;
    options.primitive = primitive;
    options.transform.position = cursor_.position;
    const auto result = commitEntityCreate(options, true);
    if (!result.hasValue()) {
        emit operationFailed(result.error->message);
        return core::kInvalidEntity;
    }
    return result.value->createdEntityIds.front();
}
core::EntityId SceneViewModel::createCamera() {
    core::Transform transform;
    transform.position = editorCamera_.position;
    transform.rotation = glm::quat_cast(glm::transpose(
        glm::mat3(glm::lookAt(editorCamera_.position, editorCamera_.target, glm::vec3(0, 1, 0)))));
    return createCameraFromView(transform);
}
core::EntityId SceneViewModel::createCameraFromView(const core::Transform& transform) {
    if (rejectAnimationEdit(true))
        return 0;
    if (rejectObjectEdit()) {
        return core::kInvalidEntity;
    }
    if (!transform.isValid()) {
        return core::kInvalidEntity;
    }
    cancelTransformEdit();
    const auto previous = selection_.selectedEntity();
    emit structureAboutToChange();
    const auto id = scene_->createEntity("相机");
    scene_->setCamera(id, {});
    scene_->setTransform(id, transform);
    emit structureChanged();
    selection_.setSelectedEntity(id);
    pushHistory(new SubtreeCommand(*this, id, SubtreeCommand::Kind::Created, previous));
    emit sceneChanged();
    return id;
}
core::EntityId SceneViewModel::createDirectionalLight() {
    if (rejectAnimationEdit(true))
        return 0;
    if (rejectObjectEdit()) {
        return core::kInvalidEntity;
    }
    cancelTransformEdit();
    const auto previous = selection_.selectedEntity();
    emit structureAboutToChange();
    const auto id = scene_->createEntity("方向光");
    const auto& lighting = scene_->lighting();
    scene_->setLight(id, {lighting.color, lighting.intensity});
    core::Transform transform;
    const auto direction = glm::normalize(lighting.direction);
    const auto up = std::abs(direction.y) > 0.99F ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    transform.rotation =
        glm::quat_cast(glm::transpose(glm::mat3(glm::lookAt(glm::vec3(0), -direction, up))));
    scene_->setTransform(id, transform);
    emit structureChanged();
    selection_.setSelectedEntity(id);
    pushHistory(new SubtreeCommand(*this, id, SubtreeCommand::Kind::Created, previous));
    emit sceneChanged();
    return id;
}
bool SceneViewModel::setCamera(core::EntityId id, const core::CameraComponent& camera) {
    if (rejectAnimationEdit())
        return false;
    cancelTransformEdit();
    const auto result = commitCameraUpdate(id, camera);
    if (!result.hasValue())
        emit operationFailed(result.error->message);
    return result.hasValue();
}
bool SceneViewModel::setLight(core::EntityId id, const core::LightComponent& light) {
    if (rejectAnimationEdit())
        return false;
    cancelTransformEdit();
    const auto result = commitLightUpdate(id, light);
    if (!result.hasValue())
        emit operationFailed(result.error->message);
    return result.hasValue();
}
bool SceneViewModel::setPreviewCamera(core::EntityId id) {
    if (apiSubmitting_ || animationMode_ == AnimationMode::PoseDraft)
        return false;
    if (previewCamera_ == id)
        return permitFileCommit(nullptr);
    auto visibility = viewportVisibility_;
    if (id != 0) {
        const auto* node = scene_->find(id);
        if (!node || !node->camera || !scene_->isVisible(id)) {
            return false;
        }
        visibility.localRoot = 0;
    }
    try {
        pendingAnimationReplay_.reset();
        const auto source = animationPreparationSource();
        if (!matchesAnimationPreparationSource(source)) {
            emit operationFailed(QStringLiteral("实体相机预览准备期间来源已变化。"));
            return false;
        }
        if (animationMode_ != AnimationMode::Base) {
            auto geometry = preparePoseGeometry(animationInputs_, {}, nullptr, std::nullopt,
                                                 nullptr, &visibility);
            QString error;
            if (!geometry || !renderer_gl::validatePoseGeometry(*source.pose->numerics,
                                                                *geometry, source.view, id, error)) {
                emit operationFailed(error);
                return false;
            }
            pendingAnimationReplay_ = prepareSharedAnimationReplay(error, std::move(geometry));
            if (!pendingAnimationReplay_)
                return false;
        }
        if (!permitAnimationCommit(source))
            return false;
    } catch (const std::bad_alloc&) {
        pendingAnimationReplay_.reset();
        emit operationFailed(QStringLiteral("内存不足，未切换实体相机预览。"));
        return false;
    }
    if (isEditMode())
        setEditMode(false);
    cancelTransformEdit();
    const bool visibilityChanged = visibility.localRoot != viewportVisibility_.localRoot;
    viewportVisibility_ = std::move(visibility);
    previewCamera_ = id;
    if (id != core::kInvalidEntity)
        lastPreviewCamera_ = id;
    publishPreparedAnimationPose();
    if (visibilityChanged)
        notifyViewportVisibility();
    flushHistoryNotifications();
    emit previewCameraChanged(id);
    return true;
}
core::EntityId SceneViewModel::previewCamera() const {
    return previewCamera_;
}
core::EntityId SceneViewModel::previewCameraCandidate() const {
    const auto usable = [this](core::EntityId id) {
        const auto* node = scene_->find(id);
        return node && node->camera && scene_->isVisible(id);
    };
    for (auto id : {selection_.selectedEntity(), lastPreviewCamera_}) {
        if (usable(id)) {
            return id;
        }
    }
    core::EntityId result = core::kInvalidEntity;
    for (const auto& node : scene_->nodes()) {
        if (node.camera && usable(node.id) &&
            (result == core::kInvalidEntity || node.id < result)) {
            result = node.id;
        }
    }
    return result;
}

bool SceneViewModel::toggleCameraPreview() {
    if (previewCamera_ != core::kInvalidEntity) {
        return setPreviewCamera(core::kInvalidEntity);
    }
    const auto candidate = previewCameraCandidate();
    if (candidate == core::kInvalidEntity) {
        emit operationFailed(QStringLiteral("没有可见相机：请创建相机或在场景树显示已有相机。"));
        return false;
    }
    return setPreviewCamera(candidate);
}
void SceneViewModel::selectRay(const core::Ray& ray) {
    if (isEditMode()) {
        return; // 组件拾取由独立入口处理，不能将源笼点击解释成对象选择。
    }
    const auto pose = installedAnimationPose();
    if (animationMode_ != AnimationMode::Base && !pose)
        return;
    selection_.setSelectedEntity(
        renderer_gl::RayCaster::pick(*scene_, *assets_, ray, viewportVisibility_, pose.get()));
}
bool SceneViewModel::renameEntity(core::EntityId id, const QString& name) {
    cancelTransformEdit();
    const QString trimmed = name.trimmed();
    if (scene_->find(id) == nullptr || trimmed.isEmpty()) {
        emit operationFailed(QStringLiteral("名称不能为空。"));
        return false;
    }
    api::EntityPatch changes;
    changes.name = trimmed;
    const auto result = commitEntityUpdate(id, changes, QStringLiteral("重命名"));
    if (!result.hasValue())
        emit operationFailed(result.error->message);
    return result.hasValue();
}
bool SceneViewModel::setVisible(core::EntityId id, bool visible) {
    cancelTransformEdit();
    api::EntityPatch changes;
    changes.visible = visible;
    return commitEntityUpdate(id, changes, QStringLiteral("显示/隐藏")).hasValue();
}
bool SceneViewModel::setParent(core::EntityId id, core::EntityId parent) {
    if (rejectObjectEdit()) {
        return false;
    }
    cancelTransformEdit();
    // 先在模型通知前验证，失败时不触发无意义的树重置。
    if (scene_->find(id) == nullptr ||
        (parent != core::kInvalidEntity && scene_->find(parent) == nullptr)) {
        emit operationFailed(QStringLiteral("父对象已不存在。"));
        return false;
    }
    for (auto ancestor = parent; ancestor != core::kInvalidEntity;
         ancestor = scene_->find(ancestor)->parent) {
        if (ancestor == id) {
            emit operationFailed(QStringLiteral("不能将对象自身或其后代设为父对象。"));
            return false;
        }
    }
    if (scene_->find(id)->parent == parent) {
        return permitFileCommit(nullptr);
    }
    std::string diagnostic;
    auto candidate = scene_->prepareParentChange(id, parent, diagnostic);
    if (!candidate) {
        emit operationFailed(QString::fromStdString(diagnostic));
        return false;
    }
    auto prepared = std::make_shared<core::Scene::PreparedParentChange>(std::move(*candidate));
    if (const auto error = preflightParentAnimation(*prepared, true)) {
        emit operationFailed(error->message);
        return false;
    }
    const auto apply = [this, prepared](bool forward) {
        emit structureAboutToChange();
        const bool installed = forward ? scene_->installPreparedParentChange(*prepared)
                                       : scene_->restorePreparedParentChange(*prepared);
        Q_ASSERT(installed);
        queueHistoryNotifications(true);
    };
    auto command = std::make_unique<EditCommand>(
        QStringLiteral("更换父对象"),
        [apply] {
            apply(false);
        },
        [apply] {
            apply(true);
        },
        [this, prepared](bool forward, QString& error) {
            return prepareParentReplay(*prepared, forward, error);
        });
    pendingAnimationCommand_ = pendingAnimationReplay_ ? command.get() : nullptr;
    if (!permitFileCommit(nullptr) || !scene_->canInstallPreparedParentChange(*prepared)) {
        pendingAnimationReplay_.reset();
        return false;
    }
    pushHistory(command.release());
    return true;
}
bool SceneViewModel::setTransform(core::EntityId id, const core::Transform& transform) {
    if (rejectObjectEdit()) {
        return false;
    }
    cancelTransformEdit();
    if (scene_->find(id) == nullptr || !transform.isValid()) {
        emit operationFailed(
            QStringLiteral("变换被拒绝：请使用有限数值，且缩放绝对值不得小于 0.001。"));
        return false;
    }
    api::EntityPatch changes;
    changes.transform = transform;
    const auto result = commitEntityUpdate(id, changes, QStringLiteral("变换"));
    if (!result.hasValue())
        emit operationFailed(result.error->message);
    return result.hasValue();
}
void SceneViewModel::applyTransform(core::EntityId id, const core::Transform& transform) {
    if (apiSubmitting_)
        scene_->installTransformSnapshot(id, transform);
    else
        scene_->setTransform(id, transform);
    queueEntityNotification(id);
}
const QUndoStack* SceneViewModel::undoStack() const {
    return &history_;
}
void SceneViewModel::undo() {
    replayHistory(false);
}
void SceneViewModel::redo() {
    replayHistory(true);
}
bool SceneViewModel::setTransformComponent(core::EntityId id, int group, int axis, double value) {
    cancelTransformEdit();
    const auto* node = scene_->find(id);
    if (node == nullptr || group < 0 || group > 2 || axis < 0 || axis > 2 ||
        !std::isfinite(value)) {
        return false;
    }
    core::Transform transform = node->transform;
    if (group == 0) {
        transform.position[axis] = static_cast<float>(value);
    }
    if (group == 2) {
        transform.scale[axis] = static_cast<float>(value);
    }
    if (group == 1) {
        glm::vec3 euler = glm::eulerAngles(transform.rotation);
        euler[axis] = glm::radians(static_cast<float>(value));
        transform.rotation = glm::quat(euler);
    }
    return setTransform(id, transform);
}
void SceneViewModel::beginTransformEdit(core::EntityId id) {
    if (rejectAnimationEdit(true) || rejectObjectEdit() || !viewportVisibility_.isVisible(*scene_, id)) {
        return;
    }
    cancelTransformEdit();
    if (const auto* node = scene_->find(id)) {
        transformEdit_ = TransformEdit{id, node->transform};
    }
}
void SceneViewModel::previewTransform(const core::Transform& transform) {
    if (transformEdit_ && transform.isValid()) {
        applyTransform(transformEdit_->id, transform);
    }
}
void SceneViewModel::finishTransformEdit(bool commit) {
    if (!transformEdit_) {
        return;
    }
    const auto edit = *transformEdit_;
    const auto after = scene_->find(edit.id)->transform;
    transformEdit_.reset();
    if (!commit) {
        applyTransform(edit.id, edit.before);
    } else if (edit.before.position != after.position || edit.before.rotation != after.rotation ||
               edit.before.scale != after.scale) {
        pushHistory(new TransformEntityCommand(*this, edit.id, edit.before, after));
    }
    emit transformEditFinished();
}
void SceneViewModel::cancelTransformEdit() {
    finishComponentTransform(false);
    finishTransformEdit(false);
}
bool SceneViewModel::hasComponentTransform() const {
    return componentTransform_.has_value();
}
std::shared_ptr<const core::EditableMeshContent> SceneViewModel::componentPreview() const {
    return componentTransform_ ? componentTransform_->candidate.content() : nullptr;
}
std::shared_ptr<const core::EditableMeshContent>
SceneViewModel::displayedEditableMesh(core::EntityId id) const {
    if (componentTransform_ && componentTransform_->entity == id) {
        return componentPreview();
    }
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    return record ? record->content : nullptr;
}
std::optional<glm::dvec3> SceneViewModel::selectedComponentCenter() const {
    if (!isEditMode()) {
        return std::nullopt;
    }
    const auto& source = scene_->editableMesh(editedMesh_)->content->source;
    return core::modeling::vertexSelectionCenter(source,
                                                 componentSelection_.selectedVertices(source),
                                                 glm::dmat4(scene_->worldMatrix(editedEntity_)));
}
bool SceneViewModel::beginComponentTransform(const QString& label) {
    if (!beginComponentEdit(label, true))
        return false;
    const auto mirror = componentTransform_->before.content()->mirror;
    if (mirror && mirror->enabled && mirror->clipping) {
        std::string error;
        auto clip = core::modeling::MirrorClipSession::begin(
            componentTransform_->before.content()->source, componentTransform_->vertices,
            *mirror, error);
        if (!clip) {
            finishComponentTransform(false);
            emit operationFailed(QString::fromStdString(error));
            return false;
        }
        componentTransform_->mirrorClip = std::move(*clip);
    }
    return true;
}
bool SceneViewModel::beginComponentEdit(const QString& label, bool requiresSelection) {
    if (rejectAnimationEdit(true))
        return false;
    cancelTransformEdit();
    if (!isEditMode() || previewCamera_ != 0 || !scene_->isVisible(editedEntity_)) {
        emit operationFailed(QStringLiteral("请先进入可见网格的编辑模式。"));
        return false;
    }
    const auto& record = *scene_->editableMesh(editedMesh_);
    const auto selected = componentSelection_.selectedVertices(record.content->source);
    if (requiresSelection && selected.empty()) {
        emit operationFailed(QStringLiteral("请先选择需要变换的点、边或面。"));
        return false;
    }
    const auto before = *scene_->geometrySnapshot(editedEntity_);
    componentTransform_ = ComponentTransform{editedEntity_,
                                             editedMesh_,
                                             record.evaluationRevision,
                                             glm::dmat4(scene_->worldMatrix(editedEntity_)),
                                             before,
                                             before,
                                             componentSelection_,
                                             selected,
                                             label,
                                             true};
    emit componentPreviewChanged();
    return true;
}
bool SceneViewModel::isComponentTransformContextValid() const {
    if (!componentTransform_) {
        return false;
    }
    const auto& edit = *componentTransform_;
    const auto* node = scene_->find(edit.entity);
    return isEditMode() && editedEntity_ == edit.entity && node &&
           node->editableMesh == edit.mesh && scene_->isVisible(edit.entity) &&
           scene_->editableMesh(edit.mesh)->evaluationRevision == edit.baseRevision &&
           glm::dmat4(scene_->worldMatrix(edit.entity)) == edit.world &&
           componentSelection_ == edit.selection;
}
QString SceneViewModel::extrudeRegionDisabledReason() const {
    if (!isEditMode() || componentSelection_.domain() != SelectionDomain::Face) {
        return QStringLiteral("请进入编辑模式并切换到面选择，再执行区域挤出。");
    }
    const auto analysis = core::modeling::analyzeExtrudeRegion(
        scene_->editableMesh(editedMesh_)->content->source, selectedFaceIds(componentSelection_));
    return QString::fromStdString(analysis.error);
}
bool SceneViewModel::beginExtrudeRegion() {
    cancelTransformEdit();
    const auto reason = extrudeRegionDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    auto info =
        *core::modeling::analyzeExtrudeRegion(scene_->editableMesh(editedMesh_)->content->source,
                                              selectedFaceIds(componentSelection_))
             .region;
    const auto normalMatrix =
        glm::transpose(glm::inverse(glm::dmat3(scene_->worldMatrix(editedEntity_))));
    info.normal = glm::normalize(normalMatrix * info.normal);
    if (!std::isfinite(info.normal.x) || !std::isfinite(info.normal.y) ||
        !std::isfinite(info.normal.z)) {
        emit operationFailed(QStringLiteral("对象世界变换无法生成有效挤出法线。"));
        return false;
    }
    if (!beginComponentTransform(QStringLiteral("区域挤出")))
        return false;
    componentTransform_->extrusion = info;
    componentTransform_->valid = false; // 初始零距离仅显示 before，不能提交重叠拓扑。
    return true;
}
std::optional<core::modeling::ExtrudeRegionInfo> SceneViewModel::componentExtrusion() const {
    return componentTransform_ ? componentTransform_->extrusion : std::nullopt;
}
QString SceneViewModel::insetFaceDisabledReason() const {
    if (!isEditMode() || componentSelection_.domain() != SelectionDomain::Face ||
        componentSelection_.selectedIds().size() != 1)
        return QStringLiteral("请进入编辑模式并只选择一个共面凸面，再执行内插。");
    const auto id = componentSelection_.selectedIds().begin()->first;
    return QString::fromStdString(
        core::modeling::analyzeInsetFace(scene_->editableMesh(editedMesh_)->content->source, id)
            .error);
}
bool SceneViewModel::beginInsetFace() {
    cancelTransformEdit();
    const auto reason = insetFaceDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    const auto info =
        *core::modeling::analyzeInsetFace(scene_->editableMesh(editedMesh_)->content->source,
                                          componentSelection_.selectedIds().begin()->first)
             .face;
    if (!beginComponentTransform(QStringLiteral("面内插")))
        return false;
    componentTransform_->inset = info;
    componentTransform_->valid = false;
    return true;
}
std::optional<core::modeling::InsetFaceInfo> SceneViewModel::componentInset() const {
    return componentTransform_ ? componentTransform_->inset : std::nullopt;
}
bool SceneViewModel::publishComponentPreview(
    const std::optional<core::modeling::EditableMesh>& mesh, const std::string& reason,
    std::optional<ComponentSelection> selection) {
    auto& edit = *componentTransform_;
    if (!mesh) {
        edit.valid = false;
        emit operationFailed(QString::fromStdString(reason));
        return false;
    }
    std::string error;
    const auto candidate = scene_->prepareEditableGeometry(edit.entity, *mesh, error);
    if (!candidate) {
        edit.valid = false;
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    edit.candidate = *candidate;
    edit.previewSelection = std::move(selection);
    edit.valid = true;
    emit componentPreviewChanged();
    return true;
}
bool SceneViewModel::previewInsetFace(double localThickness) {
    if (!isComponentTransformContextValid()) {
        finishComponentTransform(false);
        return false;
    }
    auto& edit = *componentTransform_;
    if (!edit.inset) {
        emit operationFailed(QStringLiteral("当前操作不是面内插。"));
        return false;
    }
    const auto result = core::modeling::insetFace(
        edit.before.content()->source, edit.selection.selectedIds().begin()->first, localThickness);
    edit.insetThickness = localThickness;
    return publishComponentPreview(result.mesh, result.error);
}
QString SceneViewModel::bevelEdgeDisabledReason() const {
    if (!isEditMode() || previewCamera_ != 0 ||
        !viewportVisibility_.isVisible(*scene_, editedEntity_) ||
        componentSelection_.domain() != SelectionDomain::Edge ||
        componentSelection_.selectedIds().size() != 1)
        return QStringLiteral("请进入可见网格的编辑模式并只选择一条外凸源边，再执行倒角。");
    const auto selected = *componentSelection_.selectedIds().begin();
    const core::modeling::EdgeKey edge(selected.first, selected.second);
    const auto& source = scene_->editableMesh(editedMesh_)->content->source;
    const auto analysis = core::modeling::analyzeBevelEdge(source, edge);
    if (!analysis.bevel)
        return QString::fromStdString(analysis.error);
    for (const auto& face : source.faces) {
        const auto affected =
            std::any_of(face.corners.begin(), face.corners.end(), [edge](const auto& corner) {
                return corner.vertex == edge.first || corner.vertex == edge.second;
            });
        if (affected && !viewportVisibility_.isFaceVisible(face))
            return QStringLiteral("倒角邻面或端面包含隐藏元素；请先 Alt+H 恢复显示。");
    }
    return {};
}
bool SceneViewModel::beginBevelEdge() {
    cancelTransformEdit();
    const auto reason = bevelEdgeDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    const auto selected = *componentSelection_.selectedIds().begin();
    const auto info =
        *core::modeling::analyzeBevelEdge(scene_->editableMesh(editedMesh_)->content->source,
                                          {selected.first, selected.second})
             .bevel;
    if (!beginComponentEdit(QStringLiteral("边倒角"), true))
        return false;
    componentTransform_->bevel = info;
    componentTransform_->valid = false;
    return true;
}
std::optional<core::modeling::BevelEdgeInfo> SceneViewModel::componentBevel() const {
    return componentTransform_ ? componentTransform_->bevel : std::nullopt;
}
bool SceneViewModel::previewBevelEdge(double localWidth) {
    if (!isComponentTransformContextValid()) {
        finishComponentTransform(false);
        return false;
    }
    auto& edit = *componentTransform_;
    if (!edit.bevel) {
        emit operationFailed(QStringLiteral("当前操作不是边倒角。"));
        return false;
    }
    const auto result =
        core::modeling::bevelEdge(edit.before.content()->source, edit.bevel->edge, localWidth);
    edit.bevelWidth = localWidth;
    ComponentSelection selected;
    if (result.mesh) {
        selected.setDomain(*result.mesh, SelectionDomain::Face);
        selected.select(*result.mesh, {result.bevelFace}, SelectionOperation::Replace);
    }
    return publishComponentPreview(result.mesh, result.error, std::move(selected));
}
bool SceneViewModel::beginLoopCut() {
    if (!beginComponentEdit(QStringLiteral("环切"), false))
        return false;
    componentTransform_->loopCut = true;
    componentTransform_->valid = false;
    return true;
}
QString SceneViewModel::deleteComponentsDisabledReason(SelectionDomain domain) const {
    if (!isEditMode() || previewCamera_ != 0 || !scene_->isVisible(editedEntity_))
        return QStringLiteral("请先进入可见网格的编辑模式。");
    if (!componentSelection_.hasSelectionInDomain(
            scene_->editableMesh(editedMesh_)->content->source, domain))
        return QStringLiteral("当前选择没有该域的完整组件；请先选择对应的点、边或面。");
    return {};
}
bool SceneViewModel::deleteComponents(SelectionDomain domain) {
    const auto reason = deleteComponentsDisabledReason(domain);
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    const auto label = domain == SelectionDomain::Vertex ? QStringLiteral("删除点及关联面")
                       : domain == SelectionDomain::Edge ? QStringLiteral("删除边及关联面")
                                                         : QStringLiteral("删除面");
    if (!beginComponentEdit(label, true))
        return false;
    const auto& before = componentTransform_->before.content()->source;
    auto selected = componentTransform_->selection;
    selected.setDomain(before, domain);
    core::modeling::DeleteComponentsResult result;
    if (domain == SelectionDomain::Vertex) {
        result = core::modeling::deleteVertices(before, selected.selectedVertices(before));
    } else if (domain == SelectionDomain::Edge) {
        std::set<core::modeling::EdgeKey> edges;
        for (const auto id : selected.selectedIds())
            edges.emplace(id.first, id.second);
        result = core::modeling::deleteEdges(before, edges);
    } else {
        result = core::modeling::deleteFaces(before, selectedFaceIds(selected));
    }
    auto afterSelection = componentTransform_->selection;
    if (result.mesh)
        afterSelection.reconcile(*result.mesh);
    if (!publishComponentPreview(result.mesh, result.error, afterSelection)) {
        finishComponentTransform(false);
        return false;
    }
    return finishComponentTransform(true);
}
QString SceneViewModel::fillFaceDisabledReason() const {
    if (!isEditMode() || previewCamera_ != 0 || !scene_->isVisible(editedEntity_))
        return QStringLiteral("请先进入可见网格的编辑模式。");
    if (componentSelection_.domain() == SelectionDomain::Face)
        return QStringLiteral("补面需要点选择或边选择模式，请完整选择一个边界环。");
    if (componentSelection_.selectedIds().size() < 3)
        return QStringLiteral("请至少选择三个边界点或三条边界边。");
    return {};
}
bool SceneViewModel::fillFace() {
    const auto reason = fillFaceDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    if (!beginComponentEdit(QStringLiteral("补面"), true))
        return false;
    const auto& before = componentTransform_->before.content()->source;
    const auto& selected = componentTransform_->selection;
    core::modeling::FillFaceResult result;
    if (selected.domain() == SelectionDomain::Vertex) {
        result = core::modeling::fillFaceFromVertices(before, selected.selectedVertices(before));
    } else {
        std::set<core::modeling::EdgeKey> edges;
        for (const auto id : selected.selectedIds())
            edges.emplace(id.first, id.second);
        result = core::modeling::fillFaceFromEdges(before, edges);
    }
    ComponentSelection afterSelection;
    if (result.mesh) {
        afterSelection.setDomain(*result.mesh, SelectionDomain::Face);
        afterSelection.select(*result.mesh, {result.face}, SelectionOperation::Replace);
    }
    if (!publishComponentPreview(result.mesh, result.error, afterSelection)) {
        finishComponentTransform(false);
        return false;
    }
    return finishComponentTransform(true);
}
const ComponentSelection& SceneViewModel::displayedComponentSelection() const {
    return componentTransform_ && componentTransform_->previewSelection
               ? *componentTransform_->previewSelection
               : componentSelection_;
}
bool SceneViewModel::previewLoopCut(std::optional<core::modeling::EdgeKey> seed, double slide) {
    if (!isComponentTransformContextValid()) {
        finishComponentTransform(false);
        return false;
    }
    auto& edit = *componentTransform_;
    if (!edit.loopCut) {
        emit operationFailed(QStringLiteral("当前操作不是环切。"));
        return false;
    }
    auto result = seed ? core::modeling::loopCut(edit.before.content()->source, *seed, slide)
                       : core::modeling::LoopCutResult{{}, {}, "请将鼠标移到可见源边以预览环切。"};
    if (result.mesh && viewportVisibility_.hasHiddenElements()) {
        const auto band = core::modeling::analyzeLoopCut(edit.before.content()->source, *seed);
        for (const auto face : band.band->faces) {
            if (!viewportVisibility_.isFaceVisible(edit.before.content()->source, face)) {
                result.mesh.reset();
                result.error = "环切面带包含隐藏面；请先 Alt+H 恢复显示。";
                break;
            }
        }
    }
    if (!result.mesh) {
        edit.candidate = edit.before;
        edit.previewSelection.reset();
        edit.valid = false;
        emit componentPreviewChanged();
        emit operationFailed(QString::fromStdString(result.error));
        return false;
    }
    ComponentSelection selected;
    selected.setDomain(*result.mesh, SelectionDomain::Edge);
    std::set<ComponentId> ids;
    for (const auto edge : result.cutEdges)
        ids.insert(ComponentId::edge(edge));
    selected.selectMany(*result.mesh, ids, SelectionOperation::Replace);
    return publishComponentPreview(result.mesh, result.error, std::move(selected));
}
bool SceneViewModel::previewComponentTransform(const glm::dmat4& worldDelta) {
    if (!isComponentTransformContextValid()) {
        finishComponentTransform(false);
        return false;
    }
    auto& edit = *componentTransform_;
    if (edit.inset || edit.loopCut || edit.bevel) {
        edit.valid = false;
        emit operationFailed(QStringLiteral("内插/环切/倒角只接受自身参数，不接受变换矩阵。"));
        return false;
    }
    const auto publish = [this](auto result) {
        return publishComponentPreview(result.mesh, result.error);
    };
    if (edit.extrusion) {
        if (glm::dmat3(worldDelta) != glm::dmat3(1) || worldDelta[0][3] != 0 ||
            worldDelta[1][3] != 0 || worldDelta[2][3] != 0 || worldDelta[3][3] != 1) {
            edit.valid = false;
            emit operationFailed(QStringLiteral("区域挤出只接受位移增量。"));
            return false;
        }
        const auto offset = glm::inverse(edit.world) * glm::dvec4(glm::dvec3(worldDelta[3]), 0);
        edit.extrusionWorldOffset = glm::dvec3(worldDelta[3]);
        return publish(core::modeling::extrudeRegion(
            edit.before.content()->source, selectedFaceIds(edit.selection), glm::dvec3(offset)));
    }
    if (!proportionalEditingEnabled_ && !edit.mirrorClip) {
        std::string error;
        const auto candidate = scene_->prepareTransformedEditableGeometry(
            edit.entity, edit.before, edit.vertices, edit.world, worldDelta, error);
        if (!candidate) {
            edit.valid = false;
            emit operationFailed(QString::fromStdString(error));
            return false;
        }
        edit.candidate = *candidate;
        edit.previewSelection.reset();
        edit.valid = true;
        emit componentPreviewChanged();
        return true;
    }
    auto affected = edit.vertices;
    core::modeling::VertexTransformResult transformed;
    if (proportionalEditingEnabled_) {
        auto weights = core::modeling::proportionalWeights(edit.before.content()->source,
                                                           edit.vertices, edit.world,
                                                           proportionalRadius_,
                                                           proportionalConnected_);
        if (!weights.weights)
            return publish(core::modeling::VertexTransformResult{{}, weights.error});
        std::erase_if(*weights.weights, [this, &edit](const auto& entry) {
            return !viewportVisibility_.isVertexVisible(edit.before.content()->source, entry.first);
        });
        affected.clear();
        for (const auto& [id, weight] : *weights.weights)
            if (weight > 0)
                affected.insert(id);
        transformed = core::modeling::transformWeightedVertices(
            edit.before.content()->source, *weights.weights, edit.world, worldDelta);
    } else {
        transformed = core::modeling::transformVertices(edit.before.content()->source,
                                                         edit.vertices, edit.world, worldDelta);
    }
    if (!transformed.mesh || !edit.mirrorClip)
        return publish(std::move(transformed));
    auto clip = *edit.mirrorClip;
    transformed = clip.constrain(*transformed.mesh, affected);
    if (!publish(std::move(transformed)))
        return false;
    edit.mirrorClip = std::move(clip);
    return true;
}
bool SceneViewModel::finishComponentTransform(bool commit) {
    if (!componentTransform_) {
        return false;
    }
    if (commit && !isComponentTransformContextValid()) {
        finishComponentTransform(false);
        emit operationFailed(QStringLiteral("组件变换上下文已失效，本次候选已取消。"));
        return false;
    }
    if (commit && !componentTransform_->valid) {
        emit operationFailed(QStringLiteral("当前组件候选无效，请修正数值或取消。"));
        return false;
    }
    const auto edit = std::move(*componentTransform_);
    componentTransform_.reset();
    if (commit && edit.before.content()->source != edit.candidate.content()->source) {
        if (edit.extrusion || edit.inset || edit.bevel)
            pushModelingHistory(edit);
        else
            pushGeometryEdit(edit.entity, edit.before, edit.candidate, edit.label, edit.selection,
                             edit.previewSelection.value_or(edit.selection));
    }
    emit componentPreviewChanged();
    emit componentTransformFinished();
    return true;
}
void SceneViewModel::pushModelingHistory(const ComponentTransform& edit) {
    ReopenableGeometryEdit operation;
    operation.entity = edit.entity;
    operation.label = edit.label;
    operation.before = {edit.before, edit.selection};
    operation.after = {edit.candidate, edit.previewSelection.value_or(edit.selection)};
    if (edit.bevel)
        operation.parameters = BevelOperationParameters{edit.bevelWidth};
    else if (edit.inset)
        operation.parameters = edit.insetThickness;
    else
        operation.parameters = edit.extrusionWorldOffset;
    operation.recompute = [this, entity = edit.entity, before = edit.before, world = edit.world,
                           faces = selectedFaceIds(edit.selection),
                           edge = edit.bevel ? std::optional(edit.bevel->edge) : std::nullopt](
                              const GeometryOperationParameters& parameters, std::string& error) {
        if (const auto* bevel = std::get_if<BevelOperationParameters>(&parameters)) {
            const auto* node = scene_->find(entity);
            const auto* current = node ? scene_->editableMesh(node->editableMesh) : nullptr;
            if (!current || current->content->mirror != before.content()->mirror ||
                current->content->subdivision != before.content()->subdivision) {
                error = "倒角使用的修改器参数已变化，请重新执行倒角。";
                return std::optional<core::Scene::GeometrySnapshot>{};
            }
            for (const auto& face : before.content()->source.faces) {
                const auto affected = std::any_of(
                    face.corners.begin(), face.corners.end(), [&edge](const auto& corner) {
                        return corner.vertex == edge->first || corner.vertex == edge->second;
                    });
                if (affected &&
                    !viewportVisibility_.isFaceVisible(current->content->source, face.id)) {
                    error = "倒角邻面或端面包含隐藏元素；请先恢复显示再调整宽度。";
                    return std::optional<core::Scene::GeometrySnapshot>{};
                }
            }
            const auto candidate =
                core::modeling::bevelEdge(before.content()->source, *edge, bevel->width);
            if (!candidate.mesh) {
                error = candidate.error;
                return std::optional<core::Scene::GeometrySnapshot>{};
            }
            if (!viewportVisibility_.isFaceVisible(current->content->source, candidate.bevelFace)) {
                error = "倒角结果面已隐藏；请先 Alt+H 恢复显示再调整宽度。";
                return std::optional<core::Scene::GeometrySnapshot>{};
            }
            return scene_->prepareEditableGeometry(entity, *candidate.mesh, error);
        }
        if (const auto* thickness = std::get_if<double>(&parameters)) {
            const auto candidate =
                core::modeling::insetFace(before.content()->source, *faces.begin(), *thickness);
            if (!candidate.mesh) {
                error = candidate.error;
                return std::optional<core::Scene::GeometrySnapshot>{};
            }
            return scene_->prepareEditableGeometry(entity, *candidate.mesh, error);
        }
        const auto& value = std::get<glm::dvec3>(parameters);
        const auto local = glm::inverse(world) * glm::dvec4(value, 0);
        const auto candidate =
            core::modeling::extrudeRegion(before.content()->source, faces, glm::dvec3(local));
        if (!candidate.mesh) {
            error = candidate.error;
            return std::optional<core::Scene::GeometrySnapshot>{};
        }
        return scene_->prepareEditableGeometry(entity, *candidate.mesh, error);
    };
    operation.install = [this, entity = edit.entity](GeometryHistoryState state) {
        // 分配和选区复制已在服务的准备阶段完成；此段只安装快照与移动容器，不发通知。
        const bool installed = scene_->installGeometry(state.geometry);
        Q_ASSERT(installed);
        if (isEditMode() && editedEntity_ == entity) {
            componentSelection_ = std::move(state.selection);
            ++componentSelectionRevision_;
        }
    };
    operation.notify = [this, entity = edit.entity] {
        if (isEditMode() && editedEntity_ == entity) {
            if (apiSubmitting_)
                historyComponentSelectionNotification_ = true;
            else
                emit componentSelectionChanged();
        }
        queueEntityNotification(entity);
    };
    QScopedValueRollback submitting(apiSubmitting_, true);
    historyService_.push(std::move(operation));
    recordApiCommit(true, true);
}
QString SceneViewModel::lastOperationDisabledReason() const {
    if (!historyService_.canAdjust())
        return QStringLiteral("当前没有可调整的上一步。");
    if (componentTransform_ || transformEdit_)
        return QStringLiteral("请先确认或取消当前变换，再调整上一步。");
    if (!isEditMode() || editedEntity_ != historyService_.target())
        return QStringLiteral("请回到上一步对象的编辑模式，再调整参数。");
    return {};
}
QString SceneViewModel::repeatLastOperationDisabledReason() const {
    if (!historyService_.canAdjust())
        return QStringLiteral("当前历史末端不是可重复的挤出、内插或倒角。");
    if (componentTransform_ || transformEdit_)
        return QStringLiteral("请先确认或取消当前变换，再重复上一步。");
    if (!isEditMode())
        return QStringLiteral("请进入当前对象的编辑模式，再重复建模操作。");
    if (historyService_.insetThickness())
        return insetFaceDisabledReason();
    if (historyService_.bevelWidth())
        return bevelEdgeDisabledReason();
    return extrudeRegionDisabledReason();
}
bool SceneViewModel::repeatLastOperation() {
    const auto reason = repeatLastOperationDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    // 只复用参数，不调用F9冻结的before重算回调；begin始终读取当前源与选区。
    const auto inset = historyService_.insetThickness();
    const auto bevel = historyService_.bevelWidth();
    const auto extrusion = historyService_.worldOffset();
    bool valid = false;
    if (inset) {
        if (!beginInsetFace())
            return false;
        valid = previewInsetFace(*inset);
    } else if (bevel) {
        if (!beginBevelEdge())
            return false;
        valid = previewBevelEdge(*bevel);
    } else {
        if (!beginExtrudeRegion())
            return false;
        valid = previewComponentTransform(glm::translate(glm::dmat4(1), *extrusion));
    }
    if (!valid) {
        finishComponentTransform(false);
        return false;
    }
    return finishComponentTransform(true);
}
std::optional<glm::dvec3> SceneViewModel::lastOperationWorldOffset() const {
    return lastOperationDisabledReason().isEmpty() ? historyService_.worldOffset() : std::nullopt;
}
std::optional<double> SceneViewModel::lastOperationInsetThickness() const {
    return lastOperationDisabledReason().isEmpty() ? historyService_.insetThickness()
                                                   : std::nullopt;
}
std::optional<double> SceneViewModel::lastOperationBevelWidth() const {
    return lastOperationDisabledReason().isEmpty() ? historyService_.bevelWidth() : std::nullopt;
}
bool SceneViewModel::adjustLastInset(double localThickness) {
    if (rejectAnimationEdit())
        return false;
    if (animationMode_ == AnimationMode::PreviewPaused && !setAnimationPreview(false))
        return false;
    QScopedValueRollback submitting(apiSubmitting_, true);
    auto error = lastOperationDisabledReason();
    if (!error.isEmpty() || !historyService_.adjustInset(localThickness, error)) {
        emit operationFailed(error);
        return false;
    }
    emit lastOperationChanged();
    return true;
}
bool SceneViewModel::adjustLastBevel(double localWidth) {
    if (rejectAnimationEdit())
        return false;
    if (animationMode_ == AnimationMode::PreviewPaused && !setAnimationPreview(false))
        return false;
    QScopedValueRollback submitting(apiSubmitting_, true);
    auto error = lastOperationDisabledReason();
    if (!error.isEmpty() || !historyService_.adjustBevel(localWidth, error)) {
        emit operationFailed(error);
        return false;
    }
    emit lastOperationChanged();
    return true;
}
bool SceneViewModel::adjustLastOperation(const glm::dvec3& worldOffset) {
    if (rejectAnimationEdit())
        return false;
    if (animationMode_ == AnimationMode::PreviewPaused && !setAnimationPreview(false))
        return false;
    QScopedValueRollback submitting(apiSubmitting_, true);
    auto error = lastOperationDisabledReason();
    if (!error.isEmpty() || !historyService_.adjust(worldOffset, error)) {
        emit operationFailed(error);
        return false;
    }
    emit lastOperationChanged();
    return true;
}
void SceneViewModel::duplicateSelected() {
    if (rejectObjectEdit()) {
        return;
    }
    cancelTransformEdit();
    if (const auto id = selection_.selectedEntity(); scene_->find(id)) {
        try {
            std::string diagnostic;
            auto candidate = scene_->prepareDuplicateSubtree(
                id, diagnostic, std::numeric_limits<std::size_t>::max(),
                scene_->find(id)->name + " 副本");
            if (!candidate) {
                emit operationFailed(QString::fromStdString(diagnostic));
                return;
            }
            auto prepared = std::make_shared<core::Scene::PreparedSubtree>(std::move(*candidate));
            if (const auto error = preflightSubtreeAnimation(*prepared, true, prepared->rootId())) {
                emit operationFailed(error->message);
                return;
            }
            auto command = std::make_unique<SubtreeCommand>(
                *this, prepared, SubtreeCommand::Kind::Duplicate, id, true);
            pendingAnimationCommand_ = pendingAnimationReplay_ ? command.get() : nullptr;
            if (permitFileCommit(nullptr) && scene_->canInstallPreparedSubtree(*prepared))
                pushHistory(command.release());
            else
                pendingAnimationReplay_.reset();
        } catch (const std::bad_alloc&) {
            pendingAnimationReplay_.reset();
            pendingAnimationCommand_ = nullptr;
            emit operationFailed(QStringLiteral("内存不足，未复制子树。"));
        }
    }
}
void SceneViewModel::deleteSelected() {
    if (rejectObjectEdit()) {
        return;
    }
    cancelTransformEdit();
    if (const auto id = selection_.selectedEntity(); scene_->find(id)) {
        try {
            std::string diagnostic;
            auto candidate = scene_->prepareRemoveSubtree(id, diagnostic);
            if (!candidate) {
                emit operationFailed(QString::fromStdString(diagnostic));
                return;
            }
            auto prepared = std::make_shared<core::Scene::PreparedSubtree>(std::move(*candidate));
            if (const auto error = preflightSubtreeAnimation(*prepared, false)) {
                emit operationFailed(error->message);
                return;
            }
            auto command = std::make_unique<SubtreeCommand>(
                *this, prepared, SubtreeCommand::Kind::Delete, id, true);
            pendingAnimationCommand_ = pendingAnimationReplay_ ? command.get() : nullptr;
            if (permitFileCommit(nullptr) && scene_->canRemovePreparedSubtree(*prepared))
                pushHistory(command.release());
            else
                pendingAnimationReplay_.reset();
        } catch (const std::bad_alloc&) {
            pendingAnimationReplay_.reset();
            pendingAnimationCommand_ = nullptr;
            emit operationFailed(QStringLiteral("内存不足，未删除子树。"));
        }
    }
}
bool SceneViewModel::setSurface(core::EntityId id, const core::SurfaceStyle& surface) {
    cancelTransformEdit();
    api::EntityPatch changes;
    changes.surface = surface;
    return commitEntityUpdate(id, changes, QStringLiteral("表面材质")).hasValue();
}
bool SceneViewModel::setLighting(const core::Lighting& lighting) {
    if (rejectAnimationEdit())
        return false;
    pendingAnimationReplay_.reset();
    cancelTransformEdit();
    if (!lighting.isValid()) {
        emit operationFailed(QStringLiteral("光源方向不能为零；请使用有效的颜色和强度。"));
        return false;
    }
    const auto before = scene_->lighting();
    if (before == lighting) {
        return permitFileCommit(nullptr);
    }
    const auto apply = [this](const core::Lighting& value) {
        scene_->setLighting(value);
        queueHistoryNotifications(false);
    };
    auto command = std::make_unique<EditCommand>(
        QStringLiteral("光照"),
        [apply, before] {
            apply(before);
        },
        [apply, lighting] {
            apply(lighting);
        },
        [this](bool, QString& error) { return prepareSharedAnimationReplay(error); });
    QString error;
    if (!stageSharedAnimationPose(error)) {
        emit operationFailed(error);
        return false;
    }
    pendingAnimationCommand_ = pendingAnimationReplay_ ? command.get() : nullptr;
    if (!permitFileCommit(nullptr))
        return false;
    pushHistory(command.release());
    return true;
}
QString SceneViewModel::filePath() const {
    return filePath_;
}
bool SceneViewModel::requiresSaveAs() const {
    return !legacySourcePath_.isEmpty();
}
bool SceneViewModel::makeEditable(core::EntityId id) {
    if (rejectAnimationEdit(true))
        return false;
    const auto* node = scene_->find(id);
    if (!node || !scene_->isVisible(id) || node->camera || node->light) {
        emit operationFailed(QStringLiteral("请选择可见的几何对象；相机和灯光不能进入编辑模式。"));
        return false;
    }
    if (node->editableMesh != 0) {
        return true;
    }
    if (node->primitive != core::PrimitiveKind::Cube) {
        emit operationFailed(
            QStringLiteral("当前编辑接入仅支持原生立方体；此对象仍保持静态几何。"));
        return false;
    }
    return commitEditableMesh(id, core::modeling::createEditableCube(),
                              QStringLiteral("转为可编辑网格"));
}
bool SceneViewModel::replaceEditableMesh(core::EntityId id,
                                         const core::modeling::EditableMesh& source,
                                         const QString& label) {
    if (rejectAnimationEdit(true))
        return false;
    const auto* node = scene_->find(id);
    if (!node || node->editableMesh == 0) {
        emit operationFailed(QStringLiteral("对象尚未绑定可编辑网格。"));
        return false;
    }
    if (scene_->editableMesh(node->editableMesh)->content->source == source) {
        return true;
    }
    return commitEditableMesh(id, source, label);
}
std::optional<core::modeling::MirrorOptions> SceneViewModel::mirrorOptions(core::EntityId id) const {
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    return record ? record->content->mirror : std::nullopt;
}
bool SceneViewModel::setMirrorOptions(
    core::EntityId id, std::optional<core::modeling::MirrorOptions> options) {
    if (rejectAnimationEdit(true))
        return false;
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    if (!record) {
        emit operationFailed(QStringLiteral("请先将对象转换为可编辑网格。"));
        return false;
    }
    if (record->content->mirror == options)
        return true;
    cancelTransformEdit();
    std::string error;
    const auto before = *scene_->geometrySnapshot(id);
    const auto after = scene_->prepareMirror(id, std::move(options), error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    const auto selected = isEditMode() && editedEntity_ == id
                              ? std::optional(componentSelection_) : std::nullopt;
    pushGeometryEdit(id, before, *after, QStringLiteral("修改 Mirror"), selected, selected);
    return true;
}
bool SceneViewModel::applyMirror(core::EntityId id) {
    if (rejectAnimationEdit(true))
        return false;
    if (!mirrorOptions(id)) {
        emit operationFailed(QStringLiteral("当前对象没有 Mirror。"));
        return false;
    }
    cancelTransformEdit();
    std::string error;
    const auto before = *scene_->geometrySnapshot(id);
    const auto after = scene_->prepareAppliedMirror(id, error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    const auto selected = isEditMode() && editedEntity_ == id
                              ? std::optional(componentSelection_) : std::nullopt;
    auto afterSelection = selected;
    if (afterSelection)
        afterSelection->reconcile(after->content()->source);
    pushGeometryEdit(id, before, *after, QStringLiteral("应用 Mirror"), selected, afterSelection);
    return true;
}
std::optional<core::modeling::SubdivisionOptions>
SceneViewModel::subdivisionOptions(core::EntityId id) const {
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    return record ? record->content->subdivision : std::nullopt;
}
bool SceneViewModel::setSubdivisionOptions(
    core::EntityId id, std::optional<core::modeling::SubdivisionOptions> options) {
    if (rejectAnimationEdit(true))
        return false;
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    if (!record) {
        emit operationFailed(QStringLiteral("请先将对象转换为可编辑网格。"));
        return false;
    }
    if (record->content->subdivision == options)
        return true;
    cancelTransformEdit();
    std::string error;
    const auto before = *scene_->geometrySnapshot(id);
    const auto after = scene_->prepareSubdivision(id, std::move(options), error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    const auto selected =
        isEditMode() && editedEntity_ == id ? std::optional(componentSelection_) : std::nullopt;
    pushGeometryEdit(id, before, *after, QStringLiteral("修改细分"), selected, selected);
    return true;
}
bool SceneViewModel::applySubdivision(core::EntityId id) {
    if (rejectAnimationEdit(true))
        return false;
    if (!subdivisionOptions(id)) {
        emit operationFailed(QStringLiteral("当前对象没有细分。"));
        return false;
    }
    cancelTransformEdit();
    std::string error;
    const auto before = *scene_->geometrySnapshot(id);
    const auto after = scene_->prepareAppliedSubdivision(id, error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    const auto selected =
        isEditMode() && editedEntity_ == id ? std::optional(componentSelection_) : std::nullopt;
    auto afterSelection = selected;
    if (afterSelection)
        afterSelection->reconcile(after->content()->source);
    pushGeometryEdit(id, before, *after, QStringLiteral("应用细分链"), selected, afterSelection);
    return true;
}
bool SceneViewModel::changeCollections(const std::vector<core::SceneCollection>& after,
                                       const QString& label) {
    if (rejectAnimationEdit())
        return false;
    cancelTransformEdit();
    const auto result = commitCollections(after, 0, {}, label);
    if (!result.hasValue())
        emit operationFailed(result.error->message);
    return result.hasValue();
}
core::CollectionId SceneViewModel::createCollection(const QString& name) {
    auto candidate = *scene_;
    const auto id = candidate.createCollection(name.trimmed().toStdString());
    if (!id || !changeCollections(candidate.collections(), QStringLiteral("新建集合"))) {
        if (!id)
            emit operationFailed(QStringLiteral("集合名称不能为空，或集合编号已耗尽。"));
        return 0;
    }
    return id;
}
bool SceneViewModel::removeCollection(core::CollectionId id) {
    auto after = scene_->collections();
    if (std::erase_if(after, [id](const auto& collection) {
            return collection.id == id;
        }) == 0)
        return false;
    return changeCollections(after, QStringLiteral("删除集合（保留对象）"));
}
bool SceneViewModel::renameCollection(core::CollectionId id, const QString& name) {
    auto after = scene_->collections();
    for (auto& collection : after)
        if (collection.id == id) {
            collection.name = name.trimmed().toStdString();
            return changeCollections(after, QStringLiteral("重命名集合"));
        }
    return false;
}
bool SceneViewModel::setCollectionVisible(core::CollectionId id, bool visible) {
    auto after = scene_->collections();
    for (auto& collection : after)
        if (collection.id == id) {
            collection.visible = visible;
            return changeCollections(after, QStringLiteral("集合显隐"));
        }
    return false;
}
bool SceneViewModel::assignEntityToCollection(core::EntityId entity, core::CollectionId id) {
    if (!scene_->find(entity))
        return false;
    auto after = scene_->collections();
    if (id && std::none_of(after.begin(), after.end(), [id](const auto& collection) {
            return collection.id == id;
        }))
        return false;
    for (auto& collection : after) {
        collection.members.erase(entity);
        if (collection.id == id)
            collection.members.insert(entity);
    }
    return changeCollections(after, QStringLiteral("更改集合成员"));
}
bool SceneViewModel::commitEditableMesh(core::EntityId id,
                                        const core::modeling::EditableMesh& source,
                                        const QString& label) {
    std::string error;
    const auto before = scene_->geometrySnapshot(id);
    const auto after = scene_->prepareEditableGeometry(id, source, error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    cancelTransformEdit();
    const auto selected =
        isEditMode() && editedEntity_ == id ? std::optional(componentSelection_) : std::nullopt;
    auto afterSelection = selected;
    if (afterSelection) {
        afterSelection->reconcile(source);
    }
    pushGeometryEdit(id, *before, *after, label, selected, afterSelection);
    return true;
}
void SceneViewModel::pushGeometryEdit(core::EntityId id,
                                      const core::Scene::GeometrySnapshot& before,
                                      const core::Scene::GeometrySnapshot& after,
                                      const QString& label,
                                      const std::optional<ComponentSelection>& beforeSelection,
                                      const std::optional<ComponentSelection>& afterSelection) {
    const auto apply = [this, id](const core::Scene::GeometrySnapshot& snapshot,
                                  const std::optional<ComponentSelection>& selection) {
        const bool installed = scene_->installGeometry(snapshot);
        Q_ASSERT(installed);
        if (selection && isEditMode() && editedEntity_ == id) {
            componentSelection_ = *selection;
            notifyComponentSelection();
        }
        queueEntityNotification(id);
    };
    pushHistory(new EditCommand(
        label,
        [apply, before, beforeSelection] {
            apply(before, beforeSelection);
        },
        [apply, after, afterSelection] {
            apply(after, afterSelection);
        }));
}
bool SceneViewModel::isModified() const {
    return !history_.isClean() || editorCamera_ != savedCamera_;
}
const core::CameraState& SceneViewModel::editorCamera() const {
    return editorCamera_;
}
void SceneViewModel::setEditorCamera(const core::CameraState& camera) {
    if (!camera.isValid() || camera == editorCamera_)
        return;
    auto candidate = animationView();
    if (candidate.setState(camera))
        commitEditorCamera(candidate, {});
}
const core::Cursor3D& SceneViewModel::cursor3D() const {
    return cursor_;
}
bool SceneViewModel::setCursorPosition(const glm::vec3& position) {
    if (rejectAnimationEdit())
        return false;
    const core::Cursor3D candidate{position, cursor_.visible};
    if (previewCamera_ != core::kInvalidEntity || !candidate.isValid()) {
        emit operationFailed(QStringLiteral("无法定位游标：请返回编辑视图并输入有限坐标。"));
        return false;
    }
    cancelTransformEdit();
    if (cursor_ != candidate) {
        QString error;
        if (!stageSharedAnimationPose(error) || !permitFileCommit(nullptr)) {
            if (!error.isEmpty())
                emit operationFailed(error);
            return false;
        }
        cursor_ = candidate;
        recordApiCommit(true, false);
        emit cursorChanged();
    }
    return true;
}
void SceneViewModel::setCursorVisible(bool visible) {
    if (rejectAnimationEdit())
        return;
    if (cursor_.visible != visible) {
        QString error;
        if (!stageSharedAnimationPose(error) || !permitFileCommit(nullptr)) {
            if (!error.isEmpty())
                emit operationFailed(error);
            return;
        }
        cursor_.visible = visible;
        recordApiCommit(true, false);
        emit cursorChanged();
    }
}
std::optional<glm::vec3> SceneViewModel::cursorSelectionCenter() const {
    if (isEditMode()) {
        const auto center = selectedComponentCenter();
        return center ? std::optional(glm::vec3(*center)) : std::nullopt;
    }
    const auto id = selection_.selectedEntity();
    if (animationMode_ != AnimationMode::Base) {
        const auto pose = installedAnimationPose();
        const auto* node = pose ? pose->numerics->find(id) : nullptr;
        return node ? std::optional(glm::vec3(node->world[3])) : std::nullopt;
    }
    return scene_->find(id) ? std::optional(glm::vec3(scene_->worldMatrix(id)[3])) : std::nullopt;
}
bool SceneViewModel::moveCursorToSelection() {
    cancelTransformEdit();
    const auto center = cursorSelectionCenter();
    if (!center) {
        emit operationFailed(QStringLiteral("请先选择对象或网格组件，再将游标移到选择。"));
        return false;
    }
    return setCursorPosition(*center);
}
TransformPivot SceneViewModel::transformPivot() const {
    return transformPivot_;
}
SnapMode SceneViewModel::snapMode() const {
    return snapMode_;
}
void SceneViewModel::setSnapMode(SnapMode mode) {
    if (animationMode_ == AnimationMode::PoseDraft || apiSubmitting_)
        return;
    if (snapMode_ == mode)
        return;
    cancelTransformEdit();
    snapMode_ = mode;
    emit snapModeChanged();
}
void SceneViewModel::setTransformPivot(TransformPivot pivot) {
    if (animationMode_ == AnimationMode::PoseDraft || apiSubmitting_)
        return;
    if (transformPivot_ == pivot)
        return;
    cancelTransformEdit();
    transformPivot_ = pivot;
    emit transformPivotChanged();
}
QString SceneViewModel::transformPivotName() const {
    switch (transformPivot_) {
        case TransformPivot::Median:
            return QStringLiteral("选择质心");
        case TransformPivot::Active:
            return QStringLiteral("活动元素");
        case TransformPivot::Cursor:
            return QStringLiteral("3D 游标");
    }
    return {};
}
std::optional<glm::dvec3> SceneViewModel::transformPivotPosition() const {
    const auto objectCenter = isEditMode() ? std::nullopt : cursorSelectionCenter();
    const auto center =
        isEditMode() ? selectedComponentCenter()
                     : (objectCenter ? std::optional(glm::dvec3(*objectCenter)) : std::nullopt);
    if (!center)
        return std::nullopt;
    if (transformPivot_ == TransformPivot::Cursor)
        return glm::dvec3(cursor_.position);
    if (isEditMode() && transformPivot_ == TransformPivot::Active) {
        const auto& source = scene_->editableMesh(editedMesh_)->content->source;
        if (const auto local = componentSelection_.activePosition(source))
            return glm::dvec3(glm::dmat4(scene_->worldMatrix(editedEntity_)) *
                              glm::dvec4(*local, 1));
    }
    return center;
}
QString SceneViewModel::selectionToCursorDisabledReason() const {
    if (previewCamera_ != 0 || !viewportVisibility_.isVisible(*scene_, selection_.selectedEntity()))
        return QStringLiteral("请在编辑视图选择可见对象或网格组件。");
    if (!cursorSelectionCenter())
        return QStringLiteral("请先选择对象或网格组件。");
    return {};
}
bool SceneViewModel::moveSelectionToCursor() {
    cancelTransformEdit();
    const auto reason = selectionToCursorDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    if (isEditMode()) {
        const auto delta = glm::dvec3(cursor_.position) - *selectedComponentCenter();
        if (!beginComponentTransform(QStringLiteral("选择到游标（保持偏移）")))
            return false;
        if (!previewComponentTransform(glm::translate(glm::dmat4(1), delta))) {
            finishComponentTransform(false);
            return false;
        }
        return finishComponentTransform(true);
    }
    const auto id = selection_.selectedEntity();
    const auto* node = scene_->find(id);
    auto transform = node->transform;
    transform.position =
        glm::vec3(glm::inverse(scene_->worldMatrix(node->parent)) * glm::vec4(cursor_.position, 1));
    return setTransform(id, transform);
}
void SceneViewModel::newScene() {
    if (rejectAnimationEdit())
        return;
    assets::LoadedScene prepared;
    prepared.assets = std::make_shared<assets::AssetManager>();
    prepared.camera = renderer_gl::EditorCamera{}.state();
    auto state = apiDocumentState_;
    state.resetDocument();
    if (!permitFileCommit(nullptr))
        return;
    publishDocument(std::move(prepared), {}, {}, std::move(state));
}
void SceneViewModel::publishDocument(assets::LoadedScene&& prepared, QString&& path,
                                     QString&& legacyPath, api::ApiDocumentState&& state) {
    QScopedValueRollback submitting(apiSubmitting_, true);
    const auto previousMode = animationMode_;
    clearAnimationPreview();
    if (previousMode == AnimationMode::Base)
        advanceAnimationSession();
    animationFrame_ = 1;
    animationLoop_ = false;
    cancelTransformEdit();
    const bool wasEditing = isEditMode();
    editedEntity_ = core::kInvalidEntity;
    editedMesh_ = 0;
    componentSelection_ = {};
    viewportVisibility_.clearElements();
    viewportVisibility_.editedEntity = core::kInvalidEntity;
    if (previewCamera_) {
        previewCamera_ = 0;
        historyPreviewCameraNotification_ = true;
    }
    lastPreviewCamera_ = 0;
    selection_.installSelectedEntity(0);
    emit structureAboutToChange();
    *scene_ = std::move(prepared.scene);
    assets_ = std::move(prepared.assets);
    filePath_ = std::move(path);
    legacySourcePath_ = std::move(legacyPath);
    editorCamera_ = prepared.camera;
    cursor_ = prepared.cursor;
    savedCamera_ = editorCamera_;
    apiDocumentState_ = std::move(state);
    history_.clear();
    apiSubmitting_ = false;
    viewportVisibility_ = {};
    if (wasEditing)
        emit editModeChanged(false);
    notifyViewportVisibility();
    emit cursorChanged();
    flushHistoryNotifications();
    emit apiStateChanged();
    emit structureChanged();
    emit documentReset();
    emit sceneChanged();
    emit documentChanged();
}
bool SceneViewModel::openScene(const QString& path, std::optional<api::ApiError>* commitFailure,
                               const assets::FileReadPolicy& policy,
                               assets::FileReadFailure* readFailure) {
    return prepareAndOpenScene(path, commitFailure, policy, readFailure, nullptr);
}
bool SceneViewModel::prepareAndOpenScene(const QString& path,
                                         std::optional<api::ApiError>* commitFailure,
                                         const assets::FileReadPolicy& policy,
                                         assets::FileReadFailure* readFailure,
                                         api::MutationResult* result) {
    if (rejectAnimationEdit())
        return false;
    pendingAnimationReplay_.reset();
    if (commitFailure)
        commitFailure->reset();
    QScopedValueRollback submitting(apiSubmitting_, true);
    assets::LoadedScene loaded;
    QString error;
    if (!assets::SceneDocument::read(path, loaded, error, policy, readFailure)) {
        emit operationFailed(error);
        return false;
    }
    const QFileInfo file(path);
    auto preparedPath = file.absoluteFilePath();
    auto legacyPath = loaded.sourceVersion < 4 ? file.canonicalFilePath() : QString{};
    auto preparedState = apiDocumentState_;
    preparedState.resetDocument();
    api::MutationResult preparedResult;
    preparedResult.state = preparedState.state();
    preparedResult.status = api::ResultStatus::Opened;
    preparedResult.path = preparedPath;
    preparedResult.selectionChanged = selection_.selectedEntity() != 0;
    if (!permitFileCommit(commitFailure))
        return false;
    publishDocument(std::move(loaded), std::move(preparedPath), std::move(legacyPath),
                    std::move(preparedState));
    if (result)
        *result = std::move(preparedResult);
    emit operationCompleted(QStringLiteral("已打开 %1").arg(filePath_));
    return true;
}
api::ApiResult<api::MutationResult>
SceneViewModel::openDocumentExplicit(const api::FileRequest& request,
                                     const assets::FileReadPolicy& policy) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    if (request.ifDirty != api::IfDirty::Reject && request.ifDirty != api::IfDirty::Discard)
        return Result::failure(api::meshArgumentError(apiDocumentState(), "ifDirty",
                                                      QStringLiteral("仅支持 reject/discard。")));
    if (isModified() && request.ifDirty != api::IfDirty::Discard)
        return Result::failure({api::ErrorCode::UnsavedChanges,
                                QStringLiteral("当前文档有未保存更改，须明确 discard 才能打开。"),
                                "ifDirty", api::Recovery::None, apiDocumentState()});
    api::MutationResult result;
    std::optional<api::ApiError> commitFailure;
    auto readFailure = assets::FileReadFailure::None;
    if (!prepareAndOpenScene(request.path, &commitFailure, policy, &readFailure, &result)) {
        if (commitFailure)
            return Result::failure(*commitFailure);
        const auto code =
            readFailure == assets::FileReadFailure::PathDenied    ? api::ErrorCode::PathDenied
            : readFailure == assets::FileReadFailure::InvalidData ? api::ErrorCode::InvalidArgument
                                                                  : api::ErrorCode::IoError;
        return Result::failure({code, QStringLiteral("打开失败，当前文档和历史保持不变。"), "path",
                                api::Recovery::CorrectInput, apiDocumentState()});
    }
    return Result::success(std::move(result));
}
api::ApiResult<api::MutationResult>
SceneViewModel::newDocumentExplicit(const api::DocumentNewRequest& request) {
    using Result = api::ApiResult<api::MutationResult>;
    if (const auto failure = validateApiMutation(request))
        return Result::failure(*failure);
    if (request.ifDirty != api::IfDirty::Reject && request.ifDirty != api::IfDirty::Discard)
        return Result::failure(api::meshArgumentError(apiDocumentState(), "ifDirty",
                                                      QStringLiteral("仅支持 reject/discard。")));
    if (isModified() && request.ifDirty != api::IfDirty::Discard)
        return Result::failure({api::ErrorCode::UnsavedChanges,
                                QStringLiteral("当前文档有未保存更改，须明确 discard 才能新建。"),
                                "ifDirty", api::Recovery::None, apiDocumentState()});
    assets::LoadedScene prepared;
    prepared.assets = std::make_shared<assets::AssetManager>();
    prepared.camera = renderer_gl::EditorCamera{}.state();
    auto state = apiDocumentState_;
    state.resetDocument();
    api::MutationResult result;
    result.state = state.state();
    result.status = api::ResultStatus::Opened;
    result.selectionChanged = selection_.selectedEntity() != 0;
    QScopedValueRollback submitting(apiSubmitting_, true);
    if (const auto failure = checkBeforeCommit())
        return Result::failure(*failure);
    publishDocument(std::move(prepared), {}, {}, std::move(state));
    return Result::success(std::move(result));
}
bool SceneViewModel::isProportionalEditingEnabled() const {
    return proportionalEditingEnabled_;
}
bool SceneViewModel::isProportionalConnected() const {
    return proportionalConnected_;
}
double SceneViewModel::proportionalRadius() const {
    return proportionalRadius_;
}
void SceneViewModel::setProportionalEditingEnabled(bool enabled) {
    if (animationMode_ == AnimationMode::PoseDraft || apiSubmitting_)
        return;
    if (proportionalEditingEnabled_ == enabled)
        return;
    proportionalEditingEnabled_ = enabled;
    emit proportionalEditingChanged();
}
void SceneViewModel::setProportionalConnected(bool connected) {
    if (animationMode_ == AnimationMode::PoseDraft || apiSubmitting_)
        return;
    if (proportionalConnected_ == connected)
        return;
    proportionalConnected_ = connected;
    emit proportionalEditingChanged();
}
bool SceneViewModel::setProportionalRadius(double worldRadius) {
    if (animationMode_ == AnimationMode::PoseDraft || apiSubmitting_)
        return false;
    if (!std::isfinite(worldRadius) || worldRadius <= 0) {
        emit operationFailed(QStringLiteral("比例编辑半径必须为有限正数（世界单位）。"));
        return false;
    }
    if (proportionalRadius_ != worldRadius) {
        proportionalRadius_ = worldRadius;
        emit proportionalEditingChanged();
    }
    return true;
}
QString SceneViewModel::objExportDisabledReason() const {
    if (animationMode_ != AnimationMode::Base)
        return QStringLiteral("请先关闭动画预览，再导出基础几何。");
    if (transformEdit_ || componentTransform_)
        return QStringLiteral("请先确认或取消当前预览，再导出已确认几何。");
    const auto* node = scene_->find(selection_.selectedEntity());
    if (!node || node->camera || node->light ||
        (!node->editableMesh && !node->meshRenderer &&
         node->primitive == core::PrimitiveKind::Empty))
        return QStringLiteral("请选择包含几何的对象；相机、灯光和空对象不能导出 OBJ。");
    return {};
}
bool SceneViewModel::exportObj(const QString& path, bool evaluated) {
    const auto reason = objExportDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    const auto id = selection_.selectedEntity();
    const auto& node = *scene_->find(id);
    const glm::dmat4 world(scene_->worldMatrix(id));
    core::modeling::ObjExportResult result;
    if (node.editableMesh) {
        const auto& content = *scene_->editableMesh(node.editableMesh)->content;
        const auto& mesh = evaluated ? content.evaluatedMesh() : content.source;
        result = core::modeling::encodeObj(mesh, world, node.name);
    } else if (node.meshRenderer) {
        const auto* mesh = assets_->mesh(node.meshRenderer->mesh);
        if (!mesh) {
            emit operationFailed(QStringLiteral("所选对象缺少静态网格资源，无法导出。"));
            return false;
        }
        result = core::modeling::encodeObj(mesh->data, world, node.name);
    } else {
        const auto mesh = node.primitive == core::PrimitiveKind::Cube
                              ? renderer_gl::PrimitiveFactory::createCube()
                          : node.primitive == core::PrimitiveKind::Sphere
                              ? renderer_gl::PrimitiveFactory::createSphere()
                              : renderer_gl::PrimitiveFactory::createPlane();
        result = core::modeling::encodeObj(mesh, world, node.name);
    }
    QString error;
    if (!result.text) {
        error = QString::fromStdString(result.error);
    } else if (assets::ObjDocument::write(path, *result.text, error)) {
        emit operationCompleted(QStringLiteral("已导出 OBJ（%1）：%2")
                                    .arg(evaluated ? QStringLiteral("修改器结果")
                                                   : QStringLiteral("可编辑源"),
                                         path));
        return true;
    }
    emit operationFailed(error);
    return false;
}
bool SceneViewModel::saveScene(const QString& path, std::optional<api::ApiError>* commitFailure,
                               const api::BeforeCommitGuard& fileGuard, bool newOnly) {
    if (rejectAnimationEdit()) {
        if (commitFailure)
            *commitFailure = api::ApiError{api::ErrorCode::Busy,
                                           QStringLiteral("播放或草稿期间不能保存工程。"), {},
                                           api::Recovery::Wait, apiDocumentState()};
        return false;
    }
    pendingAnimationReplay_.reset();
    if (commitFailure)
        commitFailure->reset();
    QScopedValueRollback submitting(apiSubmitting_, true);
    cancelTransformEdit();
    if (requiresSaveAs()) {
        const QFileInfo target(path);
        const auto resolved =
            target.exists() ? target.canonicalFilePath() : target.absoluteFilePath();
#ifdef Q_OS_WIN
        constexpr auto pathCase = Qt::CaseInsensitive;
#else
        constexpr auto pathCase = Qt::CaseSensitive;
#endif
        if (resolved.compare(legacySourcePath_, pathCase) == 0) {
            emit operationFailed(
                QStringLiteral("旧版工程首次升级须另存到新路径，原文件保持不变。"));
            return false;
        }
    }
    auto targetPath = QFileInfo(path).absoluteFilePath();
    const bool savePointChanged = history_.cleanIndex() != history_.index() ||
                                  targetPath != filePath_ || savedCamera_ != editorCamera_ ||
                                  !legacySourcePath_.isEmpty();
    QString error;
    const auto beforeCommit = [this, commitFailure, &fileGuard] {
        if (!permitFileCommit(commitFailure))
            return false;
        if (fileGuard) {
            if (const auto failure = fileGuard()) {
                if (commitFailure)
                    *commitFailure = *failure;
                emit operationFailed(failure->message);
                return false;
            }
        }
        return true;
    };
    const auto writeMode = newOnly ? assets::SceneDocument::WriteMode::NewOnly
                                  : assets::SceneDocument::WriteMode::ReplaceExisting;
    bool overwriteDenied = false;
    if (!assets::SceneDocument::write(path, *scene_, *assets_, editorCamera_, error, cursor_,
                                      beforeCommit, writeMode, &overwriteDenied)) {
        if (overwriteDenied && commitFailure)
            *commitFailure = api::ApiError{api::ErrorCode::OverwriteDenied, error, "path",
                                           api::Recovery::CorrectInput, apiDocumentState()};
        emit operationFailed(error);
        return false;
    }
    filePath_ = std::move(targetPath);
    legacySourcePath_.clear();
    savedCamera_ = editorCamera_;
    history_.setClean();
    if (savePointChanged)
        recordApiCommit(false, true);
    emit documentChanged();
    emit operationCompleted(QStringLiteral("已保存 %1").arg(filePath_));
    return true;
}
} // namespace mini3d::editor
