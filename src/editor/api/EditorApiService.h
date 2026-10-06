/*
 * 模块名: EditorApiService
 * 功能概述: 将显式目标 API 请求路由到同一 ViewModel 业务和唯一撤销栈。
 * 对外接口: EditorApiService 的查询、对象/网格、历史及受策略控制的文件入口。
 * 依赖关系: ApiTypes、SceneViewModel；不执行 QAction 或持有可变场景副本。
 * 输入输出: 强类型请求到一致值快照或结构化失败。
 * 异常与错误: 旧文档、版本冲突、交互忙碌和文件边界在执行前拒绝。
 * 维护说明: 同步应用线程；Busy provider 只读，空文件策略保持外部入口关闭。
 */
#pragma once

#include "ApiTypes.h"
#include "assets/FileReadPolicy.h"

#include <functional>

namespace mini3d::editor {
class SceneViewModel;
}
namespace mini3d::editor::api {
/** @brief 非拥有服务；ViewModel 生命周期必须覆盖本服务。 */
class EditorApiService final {
  public:
    explicit EditorApiService(SceneViewModel& model);
    /** @brief 装配窗口活动查询；每次执行同步读取，不做轮询或取消交互。 */
    void setBusyProvider(std::function<QStringList()> provider);
    void setExternalBusy(const QString& reason, bool busy);
    /** @brief 装配实际文件能力；空策略关闭对应外部入口，不改变可信 GUI 调用。 */
    void setFileAccessPolicies(assets::FileReadPolicy readPolicy,
                               assets::FileReadPolicy writePolicy,
                               assets::FileReadPolicy replacePolicy = {});
    /** @brief 只在真实观察服务存活时发布视口能力，默认不声明可用。 */
    void setObservationAvailable(bool available);
    [[nodiscard]] ApiResult<SystemDescription> describe() const;
    [[nodiscard]] ApiResult<CurrentDocument> currentDocument() const;
    [[nodiscard]] ApiResult<SceneSummary> sceneSummary(const DocumentRequest& request) const;
    [[nodiscard]] ApiResult<EntityListResult> listEntities(const EntityListRequest& request) const;
    [[nodiscard]] ApiResult<EntityResult> entity(const EntityGetRequest& request) const;
    [[nodiscard]] ApiResult<HistoryState> historyState(const DocumentRequest& request) const;
    ApiResult<MutationResult> createEntity(const EntityCreateRequest& request);
    ApiResult<MutationResult> updateEntity(const EntityUpdateRequest& request);
    /** @brief 有界同类批次；完整准备后一次发布并形成唯一共享历史。 */
    ApiResult<MutationResult> createEntities(const BatchCreateEntitiesRequest& request);
    ApiResult<MutationResult> setTransforms(const BatchSetTransformsRequest& request);
    ApiResult<EntityDuplicateResult> duplicateEntity(const EntityDuplicateRequest& request);
    ApiResult<MutationResult> deleteEntity(const EntityDeleteRequest& request);
    ApiResult<MutationResult> setParent(const EntitySetParentRequest& request);
    ApiResult<CollectionMutationResult> createCollection(const CollectionCreateRequest& request);
    ApiResult<CollectionMutationResult> updateCollection(const CollectionUpdateRequest& request);
    ApiResult<CollectionMutationResult> deleteCollection(const CollectionDeleteRequest& request);
    ApiResult<CollectionMutationResult> assignCollection(const CollectionAssignRequest& request);
    ApiResult<MutationResult> createCamera(const CameraCreateRequest& request);
    ApiResult<MutationResult> updateCamera(const CameraUpdateRequest& request);
    ApiResult<MutationResult> createLight(const LightCreateRequest& request);
    ApiResult<MutationResult> updateLight(const LightUpdateRequest& request);
    ApiResult<MutationResult> undo(const HistoryMutationRequest& request);
    ApiResult<MutationResult> redo(const HistoryMutationRequest& request);
    /** @brief 外部调用必须装配写策略；提交前再次检查路径，始终拒绝覆盖。 */
    ApiResult<MutationResult> saveAs(const FileRequest& request,
                                     FileAccess access = FileAccess::External);
    /** @brief 外部调用复用读策略检查全部依赖；脏文档和读取失败保持当前文档。 */
    ApiResult<MutationResult> openDocument(const FileRequest& request,
                                           FileAccess access = FileAccess::External);
    /** @brief 保存当前路径；既有目标需要明确覆盖意图与独立替换策略。 */
    ApiResult<MutationResult> save(const FileSaveRequest& request,
                                   FileAccess access = FileAccess::External);
    ApiResult<ImportGltfResult> importGltf(const ImportGltfRequest& request,
                                           FileAccess access = FileAccess::External);
    ApiResult<ExportObjResult> exportObj(const ExportObjRequest& request,
                                         FileAccess access = FileAccess::External);
    /** @brief 仅显式 discard 丢弃脏文档；完整空文档准备后才发布。 */
    ApiResult<MutationResult> newDocument(const DocumentNewRequest& request);
    [[nodiscard]] const DocumentState& documentState() const;
    /** @brief 交换本次提交守卫并返回原值；调用作用域负责恢复，不影响后续 GUI 调用。 */
    BeforeCommitGuard exchangeBeforeCommitGuard(BeforeCommitGuard guard);
    [[nodiscard]] std::optional<ApiError> checkBeforeCommit() const;
    /** @brief 观察等装配服务复用同一应用线程/身份/版本/Busy 校验。 */
    [[nodiscard]] std::optional<ApiError>
    validateContext(const DocumentHandle& document, bool mutation, bool snapshot,
                    std::optional<std::uint64_t> documentRevision = std::nullopt,
                    std::optional<std::uint64_t> historyRevision = std::nullopt) const;
    [[nodiscard]] ApiResult<MeshSummaryResult> meshSummary(const MeshTargetRequest& request) const;
    [[nodiscard]] ApiResult<MeshSourcePageResult> readSourcePage(const MeshSourcePageRequest& request) const;
    ApiResult<MeshCreateResult> createMesh(const MeshCreateRequest& request);
    ApiResult<MeshExtrudeResult> extrudeRegion(const MeshExtrudeRequest& request);
    ApiResult<MeshInsetResult> insetFace(const MeshInsetRequest& request);
    /** @brief 显式一次性建模事务；不读取 GUI 选区、模式或变换偏好。 */
    ApiResult<MeshCommandResult> makeEditable(const MeshMakeEditableRequest& request);
    ApiResult<MeshTransformComponentsResult>
    transformComponents(const MeshTransformComponentsRequest& request);
    ApiResult<MeshBevelEdgeResult> bevelEdge(const MeshBevelEdgeRequest& request);
    ApiResult<MeshLoopCutResult> loopCut(const MeshLoopCutRequest& request);
    ApiResult<MeshDeleteComponentsResult>
    deleteComponents(const MeshDeleteComponentsRequest& request);
    ApiResult<MeshFillFaceResult> fillFace(const MeshFillFaceRequest& request);
    /** @brief 参数设置与应用走共享历史；真实源与固定求值链分别返回。 */
    ApiResult<ModifierCommandResult> setMirror(const ModifierSetMirrorRequest& request);
    ApiResult<ModifierCommandResult> setSubdivision(const ModifierSetSubdivisionRequest& request);
    ApiResult<ModifierCommandResult> applyModifier(const ModifierApplyRequest& request);

  private:
    friend class ApiJsonCodec;
    [[nodiscard]] QStringList busyReasons(bool forMutation) const;
    [[nodiscard]] std::optional<ApiError> checkThread() const;
    [[nodiscard]] std::optional<ApiError>
    checkContext(const DocumentHandle& document, bool mutation, bool snapshot,
                 std::optional<std::uint64_t> documentRevision = std::nullopt,
                 std::optional<std::uint64_t> historyRevision = std::nullopt) const;
    [[nodiscard]] ApiError error(ErrorCode code, const QString& message, const QString& field = {},
                                 Recovery recovery = Recovery::None) const;
    [[nodiscard]] ApiResult<EntitySnapshot> snapshotEntity(const core::SceneNode& node) const;
    ApiResult<MutationResult> changeHistory(const HistoryMutationRequest& request, bool forward);
    [[nodiscard]] std::optional<ApiError>
    validateFileMutation(const MutationRequest& request) const;
    [[nodiscard]] std::optional<ApiError> authorizeReadPath(const QString& path,
                                                            FileAccess access) const;
    [[nodiscard]] std::optional<ApiError> authorizeWriteTarget(const QString& path, bool overwrite,
                                                               FileAccess access) const;
    ApiResult<MutationResult> saveToPath(const QString& path, bool overwrite, FileAccess access);
    SceneViewModel& model_;
    std::function<QStringList()> busyProvider_;
    assets::FileReadPolicy readPolicy_, writePolicy_, replacePolicy_;
    bool observationAvailable_ = false;
};
} // namespace mini3d::editor::api
