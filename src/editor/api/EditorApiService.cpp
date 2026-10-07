/*
 * 模块名: EditorApiService
 * 功能概述: 在当前应用线程校验文档/版本/Busy，并复用 ViewModel 提交边界。
 * 对外接口: EditorApiService.h。
 * 依赖关系: SceneViewModel、Core、CPU RayCaster、Qt 文件信息。
 * 输入输出: 显式对象请求到分页快照、共享历史或内部文件结果。
 * 异常与错误: 已知拒绝在业务调用之前返回；不解析 UI 中文提示为错误码。
 * 维护说明: 不运行嵌套事件循环；所有响应值在一次同步调用内复制。
 */
#include "EditorApiService.h"

#include "MeshApiSupport.h"
#include "core/modeling/MeshValidation.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/RayCaster.h"

#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mini3d::editor::api {
namespace {
struct NormalizedPathLess {
    bool operator()(const QString& left, const QString& right) const {
#ifdef Q_OS_WIN
        return CompareStringOrdinal(reinterpret_cast<LPCWCH>(left.utf16()), int(left.size()),
                                    reinterpret_cast<LPCWCH>(right.utf16()), int(right.size()),
                                    TRUE) == CSTR_LESS_THAN;
#else
        return left < right;
#endif
    }
};
/** @brief 单次 open/import 的磁盘预算；每次授权重检大小，规范化同一路径只计一次。 */
struct FileReadQuota {
    std::function<std::optional<ApiError>(const QString&)> authorize;
    DocumentState state;
    std::map<QString, std::size_t, NormalizedPathLess> sizes;
    std::size_t bytes = 0;
    std::optional<ApiError> failure;

    bool check(const QString& path, QString& reason) {
        if (const auto denied = authorize(path)) {
            failure = denied;
            reason = denied->message;
            return false;
        }
        const QFileInfo file(path);
        auto normalized = file.canonicalFilePath();
        if (normalized.isEmpty())
            normalized = file.absoluteFilePath();
        normalized = QDir::cleanPath(normalized);
        const auto found = sizes.find(normalized);
        const auto previous = found == sizes.end() ? 0 : found->second;
        const auto size = std::size_t(std::max<qint64>(0, file.size()));
        if ((found == sizes.end() && sizes.size() >= limits::fileReadDependencies) ||
            size > limits::fileReadBytes || size > limits::fileReadBytes - (bytes - previous)) {
            failure = ApiError{ErrorCode::LimitExceeded,
                               QStringLiteral("单次读取超过 64 MiB 或 128 个依赖路径限额。"),
                               "path", Recovery::CorrectInput, state};
            reason = failure->message;
            return false;
        }
        bytes = bytes - previous + size;
        sizes[normalized] = size;
        return true;
    }
};
} // namespace
EditorApiService::EditorApiService(SceneViewModel& model) : model_(model) {}
void EditorApiService::setBusyProvider(std::function<QStringList()> provider) {
    busyProvider_ = std::move(provider);
}
void EditorApiService::setExternalBusy(const QString& reason, bool busy) {
    model_.setExternalBusy(reason, busy);
}
void EditorApiService::setFileAccessPolicies(assets::FileReadPolicy readPolicy,
                                             assets::FileReadPolicy writePolicy,
                                             assets::FileReadPolicy replacePolicy) {
    readPolicy_ = std::move(readPolicy);
    writePolicy_ = std::move(writePolicy);
    replacePolicy_ = std::move(replacePolicy);
}
void EditorApiService::setObservationAvailable(bool available) {
    observationAvailable_ = available;
}
const DocumentState& EditorApiService::documentState() const {
    return model_.apiDocumentState();
}
BeforeCommitGuard EditorApiService::exchangeBeforeCommitGuard(BeforeCommitGuard guard) {
    return model_.exchangeBeforeCommitGuard(std::move(guard));
}
std::optional<ApiError> EditorApiService::checkBeforeCommit() const {
    return model_.checkBeforeCommit();
}
ApiError EditorApiService::error(ErrorCode code, const QString& message, const QString& field,
                                 Recovery recovery) const {
    return {code, message, field, recovery, documentState()};
}
std::optional<ApiError> EditorApiService::checkThread() const {
    if (QThread::currentThread() != model_.thread())
        return error(ErrorCode::Internal, QStringLiteral("API 必须在编辑器应用线程执行。"));
    return std::nullopt;
}
QStringList EditorApiService::busyReasons(bool forMutation) const {
    auto reasons = model_.apiBusyReasons(forMutation);
    if (busyProvider_)
        reasons.append(busyProvider_());
    reasons.removeAll(QString{});
    reasons.removeDuplicates();
    reasons.sort();
    return reasons;
}
std::optional<ApiError>
EditorApiService::checkContext(const DocumentHandle& document, bool mutation, bool snapshot,
                               std::optional<std::uint64_t> documentRevision,
                               std::optional<std::uint64_t> historyRevision) const {
    if (const auto failure = checkThread())
        return failure;
    const auto& state = documentState();
    if (document != state.document)
        return error(ErrorCode::StaleDocument, QStringLiteral("文档句柄已失效。"),
                     QStringLiteral("document"), Recovery::Refetch);
    if (documentRevision && *documentRevision != state.documentRevision)
        return error(ErrorCode::RevisionConflict, QStringLiteral("文档版本已变化，请重新查询。"),
                     QStringLiteral("expectedDocumentRevision"), Recovery::Refetch);
    if (historyRevision && *historyRevision != state.historyRevision)
        return error(ErrorCode::RevisionConflict, QStringLiteral("共享历史已变化，请重新查询。"),
                     QStringLiteral("expectedHistoryRevision"), Recovery::Refetch);
    if (mutation || snapshot) {
        const auto reasons = busyReasons(mutation);
        if (!reasons.isEmpty())
            return error(ErrorCode::Busy,
                         QStringLiteral("请先结束当前交互：%1").arg(reasons.join(", ")), {},
                         Recovery::Wait);
    }
    return std::nullopt;
}
std::optional<ApiError>
EditorApiService::validateContext(const DocumentHandle& document, bool mutation, bool snapshot,
                                  std::optional<std::uint64_t> documentRevision,
                                  std::optional<std::uint64_t> historyRevision) const {
    return checkContext(document, mutation, snapshot, documentRevision, historyRevision);
}
ApiResult<SystemDescription> EditorApiService::describe() const {
    if (const auto failure = checkThread())
        return ApiResult<SystemDescription>::failure(*failure);
    SystemDescription result;
    result.state = documentState();
    for (const auto& name : {"system.describe", "document.current", "scene.getSummary",
                             "scene.listEntities", "entity.get", "history.getState",
                             "mesh.getSummary", "mesh.readSourcePage", "animation.getState",
                             "animation.listTracks", "animation.readKeyframes", "animation.sample"})
        result.methods.push_back({QString::fromLatin1(name),
                                  QStringLiteral("query"),
                                  QStringLiteral("scene.read"),
                                  {},
                                  true});
    for (const auto& name : {"entity.create",         "entity.update",
                             "history.undo",          "history.redo",
                             "mesh.create",           "mesh.extrudeRegion",
                             "mesh.insetFace",        "entity.duplicate",
                             "entity.delete",         "entity.setParent",
                             "collection.create",     "collection.update",
                             "collection.delete",     "collection.assign",
                             "camera.create",         "camera.update",
                             "light.create",          "light.update",
                             "mesh.makeEditable",     "mesh.transformComponents",
                             "mesh.bevelEdge",        "mesh.loopCut",
                             "mesh.deleteComponents", "mesh.fillFace",
                             "modifier.setMirror",    "modifier.setSubdivision",
                             "modifier.apply",        "batch.createEntities",
                             "batch.setTransforms", "animation.setSettings", "animation.upsertKeyframes",
                             "animation.deleteKeyframes", "animation.removeTrack", "animation.moveKeyframe"})
        result.methods.push_back({QString::fromLatin1(name),
                                  QStringLiteral("mutation"),
                                  QStringLiteral("scene.write"),
                                  {},
                                  true});
    for (const auto& name : {"animation.setPreview", "animation.setFrame", "animation.play",
                             "animation.pause", "animation.setLoop"})
        result.methods.push_back({QString::fromLatin1(name), QStringLiteral("mutation"),
                                  QStringLiteral("viewport.control"), {}, true});
    result.methods.push_back({QStringLiteral("file.saveAs"), QStringLiteral("file"),
                              QStringLiteral("file.write"), QStringLiteral("M3-02 path policy"),
                              bool(writePolicy_)});
    result.methods.push_back({QStringLiteral("document.open"), QStringLiteral("file"),
                              QStringLiteral("file.read"),
                              QStringLiteral("M3-02 recursive asset path policy"), bool(readPolicy_)});
    result.methods.push_back(
        {QStringLiteral("file.save"), QStringLiteral("file"), QStringLiteral("file.write"),
         QStringLiteral("M5-04 write path policy"), bool(writePolicy_) || bool(replacePolicy_)});
    result.methods.push_back(
        {QStringLiteral("file.importGltf"), QStringLiteral("file"), QStringLiteral("file.read"),
         QStringLiteral("M3-02 recursive asset path policy"), bool(readPolicy_)});
    result.methods.push_back(
        {QStringLiteral("file.exportObj"), QStringLiteral("file"), QStringLiteral("file.write"),
         QStringLiteral("M5-04 write path policy"), bool(writePolicy_) || bool(replacePolicy_)});
    result.methods.push_back({QStringLiteral("document.new"),
                              QStringLiteral("file"),
                              QStringLiteral("scene.write"),
                              {},
                              true});
    if (observationAvailable_) {
        for (const auto& name : {"viewport.getState", "viewport.capture"})
            result.methods.push_back({QString::fromLatin1(name), QStringLiteral("query"),
                                      QStringLiteral("viewport.observe"), {}, true});
        for (const auto& name : {"viewport.setView", "viewport.focus"})
            result.methods.push_back({QString::fromLatin1(name), QStringLiteral("mutation"),
                                      QStringLiteral("viewport.control"), {}, true});
    }
#define MINI3D_API_LIMIT(name) result.limits.emplace(QStringLiteral(#name), limits::name)
    MINI3D_API_LIMIT(requestBytes);
    MINI3D_API_LIMIT(responseBytes);
    MINI3D_API_LIMIT(jsonDepth);
    MINI3D_API_LIMIT(jsonNodes);
    MINI3D_API_LIMIT(connections);
    MINI3D_API_LIMIT(queuedRequests);
    MINI3D_API_LIMIT(meshVertices);
    MINI3D_API_LIMIT(meshFaces);
    MINI3D_API_LIMIT(meshCorners);
    MINI3D_API_LIMIT(sourcePageDefault);
    MINI3D_API_LIMIT(sourcePageMaximum);
    MINI3D_API_LIMIT(batchItems);
    MINI3D_API_LIMIT(candidateBytes);
    MINI3D_API_LIMIT(animationTracks);
    MINI3D_API_LIMIT(animationTotalKeyframes);
    MINI3D_API_LIMIT(animationTrackKeyframes);
    MINI3D_API_LIMIT(animationBatchItems);
    MINI3D_API_LIMIT(animationSampleEntities);
    MINI3D_API_LIMIT(animationPoseNodes);
    MINI3D_API_LIMIT(animationPreflightVisits);
    MINI3D_API_LIMIT(fileReadBytes);
    MINI3D_API_LIMIT(fileReadDependencies);
    MINI3D_API_LIMIT(importedEntities);
    MINI3D_API_LIMIT(exportObjBytes);
    MINI3D_API_LIMIT(captureLongestEdge);
    MINI3D_API_LIMIT(capturePixels);
    MINI3D_API_LIMIT(capturePngBytes);
    MINI3D_API_LIMIT(mutationTimeoutMs);
    MINI3D_API_LIMIT(mutationTimeoutMaximumMs);
    MINI3D_API_LIMIT(captureTimeoutMs);
    MINI3D_API_LIMIT(captureTimeoutMaximumMs);
    MINI3D_API_LIMIT(cachedResultsPerSession);
    MINI3D_API_LIMIT(cachedResultBytes);
    MINI3D_API_LIMIT(sessions);
    MINI3D_API_LIMIT(disconnectedSessionRetentionMs);
#undef MINI3D_API_LIMIT
    return ApiResult<SystemDescription>::success(std::move(result));
}
ApiResult<CurrentDocument> EditorApiService::currentDocument() const {
    if (const auto failure = checkThread())
        return ApiResult<CurrentDocument>::failure(*failure);
    return ApiResult<CurrentDocument>::success({documentState(), model_.filePath(),
                                                model_.isModified(), model_.requiresSaveAs(),
                                                busyReasons(true)});
}
ApiResult<SceneSummary> EditorApiService::sceneSummary(const DocumentRequest& request) const {
    if (const auto failure = checkContext(request.document, false, true))
        return ApiResult<SceneSummary>::failure(*failure);
    const auto scene = model_.scene();
    SceneSummary result;
    result.state = documentState();
    result.entityCount = scene->nodes().size();
    result.collectionCount = scene->collections().size();
    result.rootIds = scene->roots();
    for (const auto& collection : scene->collections())
        result.collections.push_back(
            {collection.id,
             QString::fromUtf8(collection.name.data(), qsizetype(collection.name.size())),
             collection.visible,
             {collection.members.begin(), collection.members.end()}});
    return ApiResult<SceneSummary>::success(std::move(result));
}
ApiResult<EntitySnapshot> EditorApiService::snapshotEntity(const core::SceneNode& node) const {
    const auto scene = model_.scene();
    EntitySnapshot result;
    result.entityId = node.id;
    result.parentId = node.parent;
    result.childIds = node.children;
    result.name = QString::fromUtf8(node.name.data(), qsizetype(node.name.size()));
    result.primitive = node.primitive;
    result.meshId = node.editableMesh;
    result.meshKind = node.editableMesh                              ? QStringLiteral("editable")
                      : node.meshRenderer                            ? QStringLiteral("imported")
                      : node.primitive != core::PrimitiveKind::Empty ? QStringLiteral("primitive")
                                                                     : QStringLiteral("none");
    result.localTransform = node.transform;
    result.worldMatrix = scene->worldMatrix(node.id);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(result.worldMatrix[column][row]))
                return ApiResult<EntitySnapshot>::failure(
                    error(ErrorCode::UnsupportedTransform,
                          QStringLiteral("世界矩阵超出有限 float 范围，请调整对象或父节点变换。"),
                          QStringLiteral("worldMatrix"), Recovery::CorrectInput));
        }
    }
    result.surface = node.surface;
    result.camera = node.camera;
    result.light = node.light;
    for (const auto& collection : scene->collections())
        if (collection.members.contains(node.id)) {
            result.collectionId = collection.id;
            break;
        }
    result.visible = node.visible;
    result.effectiveVisible = scene->isVisible(node.id);
    result.viewportVisible = model_.viewportVisibility().isVisible(*scene, node.id);
    core::Aabb bounds;
    if (const auto* mesh = scene->editableMesh(node.editableMesh)) {
        for (const auto& vertex : mesh->content->evaluatedMesh().vertices)
            bounds.expand(vertex.position);
    } else {
        bounds = renderer_gl::RayCaster::localBounds(node, *model_.assets());
    }
    if (bounds.isValid()) {
        result.localBounds = bounds;
        const auto worldBounds = bounds.transformed(result.worldMatrix);
        if (!worldBounds.isValid())
            return ApiResult<EntitySnapshot>::failure(
                error(ErrorCode::UnsupportedTransform,
                      QStringLiteral("几何世界边界超出有限 float 范围，请调整对象或父节点变换。"),
                      QStringLiteral("bounds.world"), Recovery::CorrectInput));
        result.worldBounds = worldBounds;
    }
    return ApiResult<EntitySnapshot>::success(std::move(result));
}
ApiResult<EntityResult> EditorApiService::entity(const EntityGetRequest& request) const {
    if (const auto failure = checkContext(request.document, false, true))
        return ApiResult<EntityResult>::failure(*failure);
    const auto* node = model_.scene()->find(request.entityId);
    if (!node)
        return ApiResult<EntityResult>::failure(
            error(ErrorCode::NotFound, QStringLiteral("对象不存在。"), QStringLiteral("entityId"),
                  Recovery::Refetch));
    auto snapshot = snapshotEntity(*node);
    if (!snapshot.hasValue())
        return ApiResult<EntityResult>::failure(*snapshot.error);
    return ApiResult<EntityResult>::success({documentState(), std::move(*snapshot.value)});
}
ApiResult<EntityListResult> EditorApiService::listEntities(const EntityListRequest& request) const {
    if (const auto failure =
            checkContext(request.document, false, true, request.expectedDocumentRevision))
        return ApiResult<EntityListResult>::failure(*failure);
    if (request.limit < 1 || std::size_t(request.limit) > limits::sourcePageMaximum)
        return ApiResult<EntityListResult>::failure(
            error(ErrorCode::InvalidArgument,
                  QStringLiteral("每页数量须在 1～%1 之间。").arg(limits::sourcePageMaximum),
                  QStringLiteral("limit"), Recovery::CorrectInput));
    if (request.afterEntityId != 0 && !request.expectedDocumentRevision)
        return ApiResult<EntityListResult>::failure(error(
            ErrorCode::InvalidArgument, QStringLiteral("后续分页必须携带首次查询的文档版本。"),
            QStringLiteral("expectedDocumentRevision"), Recovery::CorrectInput));
    auto nodes = model_.scene()->nodes();
    std::sort(nodes.begin(), nodes.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    EntityListResult result;
    result.state = documentState();
    for (const auto& node : nodes) {
        if (node.id <= request.afterEntityId)
            continue;
        if (result.entities.size() == std::size_t(request.limit)) {
            result.nextAfterEntityId = result.entities.back().entityId;
            break;
        }
        auto snapshot = snapshotEntity(node);
        if (!snapshot.hasValue())
            return ApiResult<EntityListResult>::failure(*snapshot.error);
        result.entities.push_back(std::move(*snapshot.value));
    }
    return ApiResult<EntityListResult>::success(std::move(result));
}
ApiResult<HistoryState> EditorApiService::historyState(const DocumentRequest& request) const {
    if (const auto failure = checkContext(request.document, false, true))
        return ApiResult<HistoryState>::failure(*failure);
    const auto* history = model_.undoStack();
    return ApiResult<HistoryState>::success({documentState(), history->count(), history->index(),
                                             history->cleanIndex(), history->isClean(),
                                             history->canUndo(), history->canRedo(),
                                             history->undoText(), history->redoText()});
}
ApiResult<MutationResult> EditorApiService::createEntity(const EntityCreateRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.createEntityExplicit(request);
}
ApiResult<MutationResult> EditorApiService::updateEntity(const EntityUpdateRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.updateEntityExplicit(request);
}
ApiResult<MutationResult>
EditorApiService::createEntities(const BatchCreateEntitiesRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.createEntitiesExplicit(request);
}
ApiResult<MutationResult>
EditorApiService::setTransforms(const BatchSetTransformsRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.setTransformsExplicit(request);
}
ApiResult<EntityDuplicateResult>
EditorApiService::duplicateEntity(const EntityDuplicateRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<EntityDuplicateResult>::failure(*failure);
    return model_.duplicateEntityExplicit(request);
}
ApiResult<MutationResult> EditorApiService::deleteEntity(const EntityDeleteRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.deleteEntityExplicit(request);
}
ApiResult<MutationResult> EditorApiService::setParent(const EntitySetParentRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.setParentExplicit(request);
}
ApiResult<CollectionMutationResult>
EditorApiService::createCollection(const CollectionCreateRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<CollectionMutationResult>::failure(*failure);
    return model_.createCollectionExplicit(request);
}
ApiResult<CollectionMutationResult>
EditorApiService::updateCollection(const CollectionUpdateRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<CollectionMutationResult>::failure(*failure);
    return model_.updateCollectionExplicit(request);
}
ApiResult<CollectionMutationResult>
EditorApiService::deleteCollection(const CollectionDeleteRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<CollectionMutationResult>::failure(*failure);
    return model_.deleteCollectionExplicit(request);
}
ApiResult<CollectionMutationResult>
EditorApiService::assignCollection(const CollectionAssignRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<CollectionMutationResult>::failure(*failure);
    return model_.assignCollectionExplicit(request);
}
ApiResult<MutationResult> EditorApiService::createCamera(const CameraCreateRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.createCameraExplicit(request);
}
ApiResult<MutationResult> EditorApiService::updateCamera(const CameraUpdateRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.updateCameraExplicit(request);
}
ApiResult<MutationResult> EditorApiService::createLight(const LightCreateRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.createLightExplicit(request);
}
ApiResult<MutationResult> EditorApiService::updateLight(const LightUpdateRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.updateLightExplicit(request);
}
ApiResult<MeshCreateResult> EditorApiService::createMesh(const MeshCreateRequest& request) {
    if (const auto failure = checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MeshCreateResult>::failure(*failure);
    return model_.createMeshExplicit(request);
}
ApiResult<MeshExtrudeResult> EditorApiService::extrudeRegion(const MeshExtrudeRequest& request) {
    if (const auto failure = checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MeshExtrudeResult>::failure(*failure);
    return model_.extrudeMeshExplicit(request);
}
ApiResult<MeshInsetResult> EditorApiService::insetFace(const MeshInsetRequest& request) {
    if (const auto failure = checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MeshInsetResult>::failure(*failure);
    return model_.insetMeshExplicit(request);
}
ApiResult<MeshCommandResult>
EditorApiService::makeEditable(const MeshMakeEditableRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MeshCommandResult>::failure(*failure);
    return model_.makeEditableExplicit(request);
}
ApiResult<MeshTransformComponentsResult>
EditorApiService::transformComponents(const MeshTransformComponentsRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MeshTransformComponentsResult>::failure(*failure);
    return model_.transformComponentsExplicit(request);
}
ApiResult<MeshBevelEdgeResult> EditorApiService::bevelEdge(const MeshBevelEdgeRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MeshBevelEdgeResult>::failure(*failure);
    return model_.bevelEdgeExplicit(request);
}
ApiResult<MeshLoopCutResult> EditorApiService::loopCut(const MeshLoopCutRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MeshLoopCutResult>::failure(*failure);
    return model_.loopCutExplicit(request);
}
ApiResult<MeshDeleteComponentsResult>
EditorApiService::deleteComponents(const MeshDeleteComponentsRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MeshDeleteComponentsResult>::failure(*failure);
    return model_.deleteComponentsExplicit(request);
}
ApiResult<MeshFillFaceResult> EditorApiService::fillFace(const MeshFillFaceRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<MeshFillFaceResult>::failure(*failure);
    return model_.fillFaceExplicit(request);
}
ApiResult<ModifierCommandResult>
EditorApiService::setMirror(const ModifierSetMirrorRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<ModifierCommandResult>::failure(*failure);
    return model_.setMirrorExplicit(request);
}
ApiResult<ModifierCommandResult>
EditorApiService::setSubdivision(const ModifierSetSubdivisionRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<ModifierCommandResult>::failure(*failure);
    return model_.setSubdivisionExplicit(request);
}
ApiResult<ModifierCommandResult>
EditorApiService::applyModifier(const ModifierApplyRequest& request) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return ApiResult<ModifierCommandResult>::failure(*failure);
    return model_.applyModifierExplicit(request);
}
ApiResult<MeshSummaryResult> EditorApiService::meshSummary(const MeshTargetRequest& request) const {
    using Result = ApiResult<MeshSummaryResult>;
    if (const auto failure = checkContext(request.document, false, true))
        return Result::failure(*failure);
    const auto target = meshTarget(*model_.scene(), documentState(), request.entityId, request.meshId);
    if (!target.hasValue())
        return Result::failure(*target.error);
    const auto& record = **target.value;
    const auto& source = record.content->source;
    const auto validation = core::modeling::validateEditableMesh(source);
    MeshSummaryResult result;
    result.state = documentState();
    result.mesh = meshIdentity(request.entityId, request.meshId, record);
    result.modifiers = modifierState(*record.content);
    result.source.vertexCount = source.vertices.size();
    result.source.faceCount = source.faces.size();
    result.source.cornerCount = meshCornerCount(source);
    result.source.edgeCount = validation.edgeCount;
    result.source.boundaryEdgeCount = validation.boundaryEdgeCount;
    core::Aabb bounds;
    for (const auto& vertex : source.vertices)
        bounds.expand(vertex.position);
    if (bounds.isValid())
        result.source.bounds = bounds;
    const auto& derived = record.content->displayedDerived();
    result.evaluated.vertexCount = record.content->evaluatedMesh().vertices.size();
    result.evaluated.faceCount = record.content->evaluatedMesh().faces.size();
    result.evaluated.triangleCount = derived.mesh.indices.size() / 3;
    bounds = {};
    for (const auto& vertex : record.content->evaluatedMesh().vertices)
        bounds.expand(vertex.position);
    if (bounds.isValid())
        result.evaluated.bounds = bounds;
    return Result::success(std::move(result));
}
ApiResult<MeshSourcePageResult>
EditorApiService::readSourcePage(const MeshSourcePageRequest& request) const {
    using Result = ApiResult<MeshSourcePageResult>;
    if (const auto failure = checkContext(request.document, false, true))
        return Result::failure(*failure);
    const auto invalid = [this](const QString& field, const QString& message) {
        return Result::failure(meshArgumentError(documentState(), field, message));
    };
    if (request.domain != MeshDomain::Vertices && request.domain != MeshDomain::Faces)
        return invalid("domain", QStringLiteral("分页仅支持 vertices/faces。"));
    const auto expectedField = request.domain == MeshDomain::Vertices ? MeshField::Position : MeshField::CornerAttributes;
    const auto fields = request.fields.value_or(std::vector<MeshField>{expectedField});
    if (fields.size() > 1 || (!fields.empty() && fields.front() != expectedField))
        return invalid("fields", QStringLiteral("字段集合不属于指定分页域。"));
    if (request.limit < 1 || std::size_t(request.limit) > limits::sourcePageMaximum)
        return invalid("limit", QStringLiteral("分页数量超出允许范围。"));
    const auto target = meshTarget(*model_.scene(), documentState(), request.entityId, request.meshId);
    if (!target.hasValue())
        return Result::failure(*target.error);
    const auto& record = **target.value;
    std::uint64_t after = 0;
    if (request.cursor) {
        const auto& cursor = *request.cursor;
        if (cursor.document != request.document || cursor.entityId != request.entityId ||
            cursor.meshId != request.meshId || cursor.domain != request.domain || cursor.fields != fields || cursor.afterId == 0)
            return invalid("cursor", QStringLiteral("游标与目标、域或字段集合不匹配。"));
        if (cursor.documentRevision != documentState().documentRevision ||
            cursor.topologyRevision != record.topologyRevision || cursor.geometryRevision != record.geometryRevision)
            return Result::failure(error(ErrorCode::RevisionConflict, QStringLiteral("分页源版本已变化。"),
                                          "cursor", Recovery::Refetch));
        after = cursor.afterId;
    }
    MeshSourcePageResult result;
    result.state = documentState();
    result.mesh = meshIdentity(request.entityId, request.meshId, record);
    result.domain = request.domain;
    result.fields = fields;
    const auto makeCursor = [&](std::uint64_t last) {
        MeshSourceCursor cursor;
        cursor.document = request.document;
        cursor.entityId = request.entityId;
        cursor.meshId = request.meshId;
        cursor.documentRevision = result.state.documentRevision;
        cursor.topologyRevision = record.topologyRevision;
        cursor.geometryRevision = record.geometryRevision;
        cursor.domain = request.domain;
        cursor.fields = fields;
        cursor.afterId = last;
        return cursor;
    };
    const auto& source = record.content->source;
    if (request.domain == MeshDomain::Vertices) {
        std::vector<const core::modeling::EditableVertex*> vertices;
        for (const auto& vertex : source.vertices)
            vertices.push_back(&vertex);
        std::sort(vertices.begin(), vertices.end(), [](auto left, auto right) {return left->id < right->id;});
        if (after && std::none_of(vertices.begin(), vertices.end(), [after](auto vertex) {return vertex->id == after;}))
            return invalid("cursor.afterId", QStringLiteral("游标不是当前源顶点身份。"));
        for (const auto* vertex : vertices) {
            if (vertex->id <= after)
                continue;
            if (result.vertices.size() == std::size_t(request.limit)) {
                result.nextCursor = makeCursor(result.vertices.back().vertexId);
                break;
            }
            result.vertices.push_back({vertex->id, fields.empty() ? std::nullopt : std::optional(vertex->position)});
        }
    } else {
        std::vector<const core::modeling::EditableFace*> faces;
        for (const auto& face : source.faces)
            faces.push_back(&face);
        std::sort(faces.begin(), faces.end(), [](auto left, auto right) {return left->id < right->id;});
        if (after && std::none_of(faces.begin(), faces.end(), [after](auto face) {return face->id == after;}))
            return invalid("cursor.afterId", QStringLiteral("游标不是当前源面身份。"));
        std::size_t pageCorners = 0;
        for (const auto* face : faces) {
            if (face->id <= after)
                continue;
            if (result.faces.size() == std::size_t(request.limit)) {
                result.nextCursor = makeCursor(result.faces.back().faceId);
                break;
            }
            if (face->corners.size() > limits::meshVertices || face->corners.size() > limits::meshCorners - pageCorners)
                return Result::failure(error(ErrorCode::LimitExceeded, QStringLiteral("分页面角输出超出预算，请减小页大小。"),
                                              "limit", Recovery::CorrectInput));
            pageCorners += face->corners.size();
            MeshSourceFace value;
            value.faceId = face->id;
            for (const auto& corner : face->corners) {
                MeshSourceCorner item{corner.id, corner.vertex, std::nullopt};
                if (!fields.empty())
                    item.attributes = MeshCornerInput{corner.uv, corner.color, corner.normal};
                value.corners.push_back(std::move(item));
            }
            result.faces.push_back(std::move(value));
        }
    }
    return Result::success(std::move(result));
}
ApiResult<MutationResult> EditorApiService::changeHistory(const HistoryMutationRequest& request,
                                                          bool forward) {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision,
                         request.expectedHistoryRevision))
        return ApiResult<MutationResult>::failure(*failure);
    const auto* stack = model_.undoStack();
    MutationResult result;
    const auto selected = model_.selection()->selectedEntity();
    if (const auto failure = checkBeforeCommit())
        return ApiResult<MutationResult>::failure(*failure);
    if (forward ? stack->canRedo() : stack->canUndo()) {
        if (forward)
            model_.redo();
        else
            model_.undo();
        result.status = ResultStatus::Committed;
        result.undoable = true;
        result.selectionChanged = selected != model_.selection()->selectedEntity();
    }
    result.state = documentState();
    return ApiResult<MutationResult>::success(std::move(result));
}
ApiResult<MutationResult> EditorApiService::undo(const HistoryMutationRequest& request) {
    return changeHistory(request, false);
}
ApiResult<MutationResult> EditorApiService::redo(const HistoryMutationRequest& request) {
    return changeHistory(request, true);
}
std::optional<ApiError>
EditorApiService::validateFileMutation(const MutationRequest& request) const {
    if (const auto failure =
            checkContext(request.document, true, false, request.expectedDocumentRevision))
        return failure;
    return checkMeshEnvelope(request, documentState());
}
std::optional<ApiError> EditorApiService::authorizeReadPath(const QString& path,
                                                            FileAccess access) const {
    if (path.isEmpty())
        return meshArgumentError(documentState(), "path", QStringLiteral("文件路径不能为空。"));
    if (access == FileAccess::InternalTrusted)
        return std::nullopt;
    if (!readPolicy_)
        return error(ErrorCode::PermissionDenied, QStringLiteral("未授权外部文件读取。"));
    QString reason;
    if (!readPolicy_(path, reason))
        return error(ErrorCode::PathDenied, reason, "path", Recovery::CorrectInput);
    return std::nullopt;
}
std::optional<ApiError> EditorApiService::authorizeWriteTarget(const QString& path, bool overwrite,
                                                               FileAccess access) const {
    if (path.isEmpty())
        return meshArgumentError(documentState(), "path", QStringLiteral("文件路径不能为空。"));
    if (access != FileAccess::InternalTrusted) {
        const auto& policy = overwrite ? replacePolicy_ : writePolicy_;
        if (!policy)
            return error(ErrorCode::PermissionDenied,
                         overwrite ? QStringLiteral("未授权外部文件替换。")
                                   : QStringLiteral("未授权外部新文件写入。"));
        QString reason;
        if (!policy(path, reason)) {
            const QFileInfo target(path);
            QString replacementReason;
            // 新文件策略会拒绝所有既有路径；只借当前替换策略确认普通目标确属批准根。
            if (!overwrite && target.isFile() && !target.isSymLink() && replacePolicy_ &&
                replacePolicy_(path, replacementReason))
                return error(ErrorCode::OverwriteDenied,
                             QStringLiteral("目标已存在，禁止隐式覆盖。"), "path",
                             Recovery::CorrectInput);
            return error(ErrorCode::PathDenied, reason, "path", Recovery::CorrectInput);
        }
    }
    const QFileInfo target(path);
    if (!overwrite && (target.exists() || target.isSymLink()))
        return error(ErrorCode::OverwriteDenied, QStringLiteral("目标已存在，禁止隐式覆盖。"),
                     "path", Recovery::CorrectInput);
    if (target.isSymLink() || (target.exists() && !target.isFile()))
        return error(ErrorCode::PathDenied, QStringLiteral("目标必须为新路径或普通文件。"), "path",
                     Recovery::CorrectInput);
    return std::nullopt;
}
ApiResult<MutationResult> EditorApiService::saveToPath(const QString& path, bool overwrite,
                                                       FileAccess access) {
    using Result = ApiResult<MutationResult>;
    if (const auto failure = authorizeWriteTarget(path, overwrite, access))
        return Result::failure(*failure);
    MutationResult result;
    result.state = documentState();
    result.status = ResultStatus::Saved;
    result.path = QFileInfo(path).absoluteFilePath();
    const auto fileGuard = [this, &path, overwrite, access] {
        // 读取当前策略，不能用准备阶段的策略副本延续已撤销的覆盖权限。
        return authorizeWriteTarget(path, overwrite, access);
    };
    std::optional<ApiError> commitFailure;
    if (!model_.saveScene(path, &commitFailure, fileGuard, !overwrite)) {
        if (commitFailure)
            return Result::failure(*commitFailure);
        return Result::failure(
            error(ErrorCode::IoError, QStringLiteral("保存失败，未修改保存点。"), "path"));
    }
    result.state.documentRevision = documentState().documentRevision;
    result.state.historyRevision = documentState().historyRevision;
    return Result::success(std::move(result));
}
ApiResult<MutationResult> EditorApiService::saveAs(const FileRequest& request, FileAccess access) {
    if (const auto failure = validateFileMutation(request))
        return ApiResult<MutationResult>::failure(*failure);
    if (request.ifDirty != IfDirty::Reject)
        return ApiResult<MutationResult>::failure(
            meshArgumentError(documentState(), "ifDirty", QStringLiteral("另存不接受丢弃意图。")));
    return saveToPath(request.path, false, access);
}
ApiResult<MutationResult> EditorApiService::save(const FileSaveRequest& request,
                                                 FileAccess access) {
    if (const auto failure = validateFileMutation(request))
        return ApiResult<MutationResult>::failure(*failure);
    const auto path = model_.filePath();
    if (path.isEmpty() || model_.requiresSaveAs())
        return ApiResult<MutationResult>::failure(
            error(ErrorCode::UnsupportedOperation,
                  QStringLiteral("当前文档没有可保存路径或需要旧格式升级，请使用 file.saveAs。"),
                  "path", Recovery::CorrectInput));
    return saveToPath(path, request.overwrite, access);
}
ApiResult<MutationResult> EditorApiService::openDocument(const FileRequest& request,
                                                         FileAccess access) {
    using Result = ApiResult<MutationResult>;
    if (const auto failure = validateFileMutation(request))
        return Result::failure(*failure);
    if (const auto failure = authorizeReadPath(request.path, access))
        return Result::failure(*failure);
    FileReadQuota quota{[this, access](const QString& path) {
                            return authorizeReadPath(path, access);
                        },
                        documentState()};
    const auto policy = [&quota](const QString& path, QString& reason) {
        return quota.check(path, reason);
    };
    auto result = model_.openDocumentExplicit(request, policy);
    if (!result.hasValue() && quota.failure)
        return Result::failure(*quota.failure);
    return result;
}
ApiResult<ImportGltfResult> EditorApiService::importGltf(const ImportGltfRequest& request,
                                                         FileAccess access) {
    using Result = ApiResult<ImportGltfResult>;
    if (const auto failure = validateFileMutation(request))
        return Result::failure(*failure);
    if (const auto failure = authorizeReadPath(request.path, access))
        return Result::failure(*failure);
    FileReadQuota quota{[this, access](const QString& path) {
                            return authorizeReadPath(path, access);
                        },
                        documentState()};
    const auto policy = [&quota](const QString& path, QString& reason) {
        return quota.check(path, reason);
    };
    auto result = model_.importGltfExplicit(request, policy);
    if (!result.hasValue() && quota.failure)
        return Result::failure(*quota.failure);
    return result;
}
ApiResult<ExportObjResult> EditorApiService::exportObj(const ExportObjRequest& request,
                                                       FileAccess access) {
    if (const auto failure = validateFileMutation(request))
        return ApiResult<ExportObjResult>::failure(*failure);
    if (const auto failure = authorizeWriteTarget(request.path, request.overwrite, access))
        return ApiResult<ExportObjResult>::failure(*failure);
    return model_.exportObjExplicit(request, [this, &request, access] {
        return authorizeWriteTarget(request.path, request.overwrite, access);
    });
}
ApiResult<MutationResult> EditorApiService::newDocument(const DocumentNewRequest& request) {
    if (const auto failure = validateFileMutation(request))
        return ApiResult<MutationResult>::failure(*failure);
    return model_.newDocumentExplicit(request);
}
} // namespace mini3d::editor::api
