/*
 * 模块名: ApiTypes
 * 功能概述: 定义共享编辑 API 的强类型请求、只读快照和结构化结果。
 * 对外接口: DocumentHandle、DocumentState、ApiResult 及对象/网格请求与结果值类型。
 * 依赖关系: Core Scene/数学类型、Qt Core；不依赖 JSON 或通信层。
 * 输入输出: 显式文档、对象和前置版本到可复制的业务结果。
 * 异常与错误: 使用 ErrorCode 和 Recovery 表达失败，不解析中文提示。
 * 维护说明: ID 和版本在此保持 uint64，只有边界编解码使用十进制字符串。
 */
#pragma once

#include "api/ApiLimits.h"
#include "core/Aabb.h"
#include "core/Scene.h"

#include <QStringList>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <variant>

namespace mini3d::editor::api {
/** @brief 应用进程与文档身份；成功新建/打开使旧句柄失效。 */
struct DocumentHandle {
    QString instanceId;
    QString documentId;
    bool operator==(const DocumentHandle&) const = default;
};
/** @brief 已确认内容与共享历史版本；不写入场景文件。 */
struct DocumentState {
    DocumentHandle document;
    std::uint64_t documentRevision = 1;
    std::uint64_t historyRevision = 1;
};

enum class ErrorCode {
    InvalidArgument,
    InvalidTopology,
    LimitExceeded,
    StaleDocument,
    RevisionConflict,
    NotFound,
    Busy,
    UnsupportedOperation,
    UnsupportedTransform,
    PermissionDenied,
    UnsavedChanges,
    OverwriteDenied,
    IoError,
    Internal,
    ViewportUnavailable,
    ViewChanged,
    RenderFailed,
    CaptureTimeout,
    Cancelled,
    DeadlineExceeded,
    PathDenied
};
enum class Recovery { Refetch, CorrectInput, Wait, QueryResult, None };
enum class ResultStatus { Committed, NoChange, Saved, Opened };

/** @brief 失败包含当前状态；protocolCode 仅供 JSON-RPC 边界编码。 */
struct ApiError {
    ErrorCode code = ErrorCode::Internal;
    QString message;
    QString fieldPath;
    Recovery recovery = Recovery::None;
    DocumentState state;
    int protocolCode = -32010;
};
/** @brief 候选准备完成后的提交许可；拒绝时原样传回错误，不发布候选或历史。 */
using BeforeCommitGuard = std::function<std::optional<ApiError>()>;
/** @brief 业务成功或失败的明确分支，调用者无需读取提示文本。 */
template <typename T> struct ApiResult {
    std::optional<T> value;
    std::optional<ApiError> error;
    [[nodiscard]] bool hasValue() const {
        return value.has_value();
    }
    static ApiResult success(T result) {
        return {std::move(result), std::nullopt};
    }
    static ApiResult failure(ApiError failure) {
        return {std::nullopt, std::move(failure)};
    }
};

struct EmptyRequest {};
struct DocumentRequest {
    DocumentHandle document;
};
/** @brief 直接 C++ 调用不需要网络会话；边界仍检查可选桥元数据。 */
struct MutationRequest : DocumentRequest {
    std::uint64_t expectedDocumentRevision = 0;
    std::optional<QString> clientSessionId;
    std::optional<std::uint64_t> mutationSequence;
    int timeoutMs = int(limits::mutationTimeoutMs);
};
struct EntityGetRequest : DocumentRequest {
    core::EntityId entityId = 0;
};
struct EntityListRequest : DocumentRequest {
    core::EntityId afterEntityId = 0;
    int limit = int(limits::sourcePageDefault);
    std::optional<std::uint64_t> expectedDocumentRevision;
};
/** @brief 完整新对象参数；不读取游标、选区或界面外观偏好。 */
struct EntityCreateRequest : MutationRequest {
    core::PrimitiveKind primitive = core::PrimitiveKind::Empty;
    QString name;
    core::EntityId parentId = 0;
    core::Transform transform;
    core::SurfaceStyle surface;
};
/** @brief 批内仅引用已有父节点；完整基础对象参数与单对象创建同义。 */
struct BatchEntityCreateItem {
    QString name;
    core::EntityId parentId = 0;
    core::PrimitiveKind primitive = core::PrimitiveKind::Empty;
    core::Transform transform;
    core::SurfaceStyle surface;
};
struct BatchCreateEntitiesRequest : MutationRequest {
    std::vector<BatchEntityCreateItem> items;
};
/** @brief 同一目标仅出现一次；全部 local TRS 按最终 overlay 验证。 */
struct BatchTransformItem {
    core::EntityId entityId = 0;
    core::Transform transform;
};
struct BatchSetTransformsRequest : MutationRequest {
    std::vector<BatchTransformItem> items;
};
/** @brief 一次对象属性更新；所有字段先验证，再形成唯一历史。 */
struct EntityPatch {
    std::optional<QString> name;
    std::optional<core::Transform> transform;
    std::optional<core::SurfaceStyle> surface;
    std::optional<bool> visible;
};
struct EntityUpdateRequest : MutationRequest {
    core::EntityId entityId = 0;
    EntityPatch changes;
};
/** @brief 显式对象结构目标；从不以当前选区补齐目标。 */
struct EntityMutationRequest : MutationRequest {
    core::EntityId entityId = 0;
};
struct EntityDuplicateRequest : EntityMutationRequest {};
struct EntityDeleteRequest : EntityMutationRequest {};
struct EntitySetParentRequest : EntityMutationRequest {
    core::EntityId parentId = 0;
    QString mode = QStringLiteral("keepLocal");
};
struct CollectionCreateRequest : MutationRequest {
    QString name;
    bool visible = true;
};
struct CollectionMutationRequest : MutationRequest {
    core::CollectionId collectionId = 0;
};
struct CollectionUpdateRequest : CollectionMutationRequest {
    std::optional<QString> name;
    std::optional<bool> visible;
};
struct CollectionDeleteRequest : CollectionMutationRequest {};
struct CollectionAssignRequest : EntityMutationRequest {
    core::CollectionId collectionId = 0;
};
/** @brief 完整设备创建参数；设备与空对象在同一次候选准备中发布。 */
struct DeviceCreateRequest : MutationRequest {
    QString name;
    core::EntityId parentId = 0;
    core::Transform transform;
    bool visible = true;
};
struct CameraCreateRequest : DeviceCreateRequest {
    core::CameraComponent camera;
};
struct CameraUpdateRequest : EntityMutationRequest {
    core::CameraComponent camera;
};
struct LightCreateRequest : DeviceCreateRequest {
    core::LightComponent light;
};
struct LightUpdateRequest : EntityMutationRequest {
    core::LightComponent light;
};
enum class MeshDomain { Vertices, Faces };
enum class MeshField { Position, CornerAttributes };
enum class MeshSpace { Local, World };
/** @brief 创建请求的面角属性；未提供时保持当前内核默认值。 */
struct MeshCornerInput {
    glm::vec2 uv{0};
    glm::vec3 color{1};
    std::optional<glm::vec3> normal;
};
struct MeshFaceInput {
    std::vector<std::size_t> indices;
    std::optional<std::vector<MeshCornerInput>> corners;
};
/** @brief 输入索引只属于本条请求；稳定元素身份由服务生成。 */
struct MeshCreateRequest : MutationRequest {
    QString name;
    core::EntityId parentId = 0;
    core::Transform transform;
    core::SurfaceStyle surface;
    std::vector<glm::vec3> positions;
    std::vector<MeshFaceInput> faces;
};
struct MeshTargetRequest : DocumentRequest {
    core::EntityId entityId = 0;
    core::MeshId meshId = 0;
};
/** @brief 游标是版本绑定的延续值，不承担授权；不接受渲染索引作源身份。 */
struct MeshSourceCursor : MeshTargetRequest {
    std::uint64_t documentRevision = 0;
    std::uint64_t topologyRevision = 0;
    std::uint64_t geometryRevision = 0;
    MeshDomain domain = MeshDomain::Vertices;
    std::vector<MeshField> fields;
    std::uint64_t afterId = 0;
};
struct MeshSourcePageRequest : MeshTargetRequest {
    MeshDomain domain = MeshDomain::Vertices;
    std::optional<std::vector<MeshField>> fields;
    int limit = int(limits::sourcePageDefault);
    std::optional<MeshSourceCursor> cursor;
};
struct MeshMutationRequest : MutationRequest {
    core::EntityId entityId = 0;
    core::MeshId meshId = 0;
    std::uint64_t expectedTopologyRevision = 0;
    std::uint64_t expectedGeometryRevision = 0;
};
struct MeshExtrudeRequest : MeshMutationRequest {
    std::vector<core::modeling::FaceId> faceIds;
    MeshSpace space = MeshSpace::Local;
    glm::dvec3 offset{0};
};
struct MeshInsetRequest : MeshMutationRequest {
    core::modeling::FaceId faceId = 0;
    double thickness = 0;
};
struct MeshMakeEditableRequest : EntityMutationRequest {};
enum class MeshComponentDomain { Vertices, Edges, Faces };
/** @brief 仅允许当前域的字段；显式 optional 区分缺失与错误域的空数组。 */
struct MeshComponentsRequest : MeshMutationRequest {
    MeshComponentDomain domain = MeshComponentDomain::Vertices;
    std::optional<std::vector<core::modeling::VertexId>> vertexIds;
    std::optional<std::vector<core::modeling::EdgeKey>> edges;
    std::optional<std::vector<core::modeling::FaceId>> faceIds;
};
/** @brief 显式双精度增量；绕 pivot 旋转/缩放后叠加 translation，不读交互偏好。 */
struct MeshTransformDelta {
    MeshSpace space = MeshSpace::Local;
    glm::dvec3 pivot{0}, translation{0}, scale{1};
    glm::dquat rotation{1, 0, 0, 0};
};
struct MeshTransformComponentsRequest : MeshComponentsRequest {
    MeshTransformDelta transform;
};
struct MeshBevelEdgeRequest : MeshMutationRequest {
    core::modeling::EdgeKey edge{0, 0};
    double width = 0;
};
struct MeshLoopCutRequest : MeshMutationRequest {
    core::modeling::EdgeKey seedEdge{0, 0};
    double slide = 0;
};
struct MeshDeleteComponentsRequest : MeshComponentsRequest {};
struct MeshFillFaceRequest : MeshComponentsRequest {};
enum class ModifierKind { Mirror, Subdivision };
/** @brief 完整参数或 nullopt 删除；仅影响固定链的指定修改器，不改真实源。 */
struct ModifierSetMirrorRequest : MeshMutationRequest {
    std::optional<core::modeling::MirrorOptions> options;
};
struct ModifierSetSubdivisionRequest : MeshMutationRequest {
    std::optional<core::modeling::SubdivisionOptions> options;
};
struct ModifierApplyRequest : MeshMutationRequest {
    ModifierKind modifier = ModifierKind::Mirror;
};
struct HistoryMutationRequest : MutationRequest {
    std::uint64_t expectedHistoryRevision = 0;
};
enum class IfDirty { Reject, Discard };
struct FileRequest : MutationRequest {
    QString path;
    IfDirty ifDirty = IfDirty::Reject;
};
/** @brief 只保存当前路径；覆盖意图不授予路径或替换权限。 */
struct FileSaveRequest : MutationRequest {
    bool overwrite = false;
};
struct ImportGltfRequest : MutationRequest {
    QString path;
    QString name;
    core::EntityId parentId = 0;
    core::Transform transform;
};
enum class ExportObjMode { Source, Evaluated };
struct ExportObjRequest : EntityMutationRequest {
    QString path;
    ExportObjMode mode = ExportObjMode::Source;
    bool overwrite = false;
};
struct DocumentNewRequest : MutationRequest {
    IfDirty ifDirty = IfDirty::Reject;
};
/** @brief 外部文件调用须具备对应路径策略；可信 GUI 调用保持既有行为。 */
enum class FileAccess { External, InternalTrusted };

struct CurrentDocument {
    DocumentState state;
    QString path;
    bool isModified = false;
    bool requiresSaveAs = false;
    QStringList busyReasons;
};
/** @brief 独立值快照；worldMatrix 为 GLM 列主序，不承诺 world TRS 分解。 */
struct EntitySnapshot {
    core::EntityId entityId = 0;
    core::EntityId parentId = 0;
    std::vector<core::EntityId> childIds;
    QString name;
    core::PrimitiveKind primitive = core::PrimitiveKind::Empty;
    core::MeshId meshId = 0;
    QString meshKind;
    core::Transform localTransform;
    glm::mat4 worldMatrix{1};
    core::SurfaceStyle surface;
    bool visible = true;
    bool effectiveVisible = true;
    bool viewportVisible = true;
    std::optional<core::Aabb> localBounds;
    std::optional<core::Aabb> worldBounds;
    core::CollectionId collectionId = 0;
    std::optional<core::CameraComponent> camera;
    std::optional<core::LightComponent> light;
};
struct EntityResult {
    DocumentState state;
    EntitySnapshot entity;
};
struct EntityListResult {
    DocumentState state;
    std::vector<EntitySnapshot> entities;
    std::optional<core::EntityId> nextAfterEntityId;
};
struct CollectionSnapshot {
    core::CollectionId collectionId = 0;
    QString name;
    bool visible = true;
    std::vector<core::EntityId> entityIds;
};
struct SceneSummary {
    DocumentState state;
    std::size_t entityCount = 0;
    std::size_t collectionCount = 0;
    std::vector<core::EntityId> rootIds;
    std::vector<CollectionSnapshot> collections;
};
struct HistoryState {
    DocumentState state;
    int count = 0;
    int index = 0;
    int cleanIndex = 0;
    bool isClean = true;
    bool canUndo = false;
    bool canRedo = false;
    QString undoText;
    QString redoText;
};
struct MutationResult {
    DocumentState state;
    ResultStatus status = ResultStatus::NoChange;
    std::vector<core::EntityId> createdEntityIds;
    std::vector<core::EntityId> affectedEntityIds;
    bool undoable = false;
    bool selectionChanged = false;
    QString path;
};
struct EntityDuplicateResult {
    MutationResult command;
    std::map<core::EntityId, core::EntityId> entityIdMap;
};
struct CollectionMutationResult {
    MutationResult command;
    core::CollectionId collectionId = 0;
};
/** @brief 完整前序身份仅在发布后返回；Undo/Redo 复用已准备子树。 */
struct ImportGltfResult {
    MutationResult command;
    core::EntityId rootEntityId = 0;
    QStringList warnings;
};
/** @brief saved 仅表示 OBJ 文件发布，不改变工程保存点或任何版本。 */
struct ExportObjResult {
    MutationResult command;
    core::EntityId entityId = 0;
    ExportObjMode mode = ExportObjMode::Source;
    std::size_t byteLength = 0;
};
struct MeshIdentity {
    core::EntityId entityId = 0;
    core::MeshId meshId = 0;
    std::uint64_t topologyRevision = 0;
    std::uint64_t geometryRevision = 0;
    std::uint64_t evaluationRevision = 0;
};
struct MeshCreateResult {
    MutationResult command;
    MeshIdentity mesh;
    std::vector<core::modeling::VertexId> vertexIdsByInputIndex;
    std::vector<core::modeling::FaceId> faceIdsByInputIndex;
    std::vector<std::vector<core::modeling::CornerId>> cornerIdsByFace;
};
struct MeshSourceStatistics {
    std::size_t vertexCount = 0, faceCount = 0, cornerCount = 0;
    std::size_t edgeCount = 0, boundaryEdgeCount = 0;
    std::optional<core::Aabb> bounds;
};
/** @brief 最终求值几何统计；vertexCount 不计渲染属性拆点。 */
struct MeshEvaluatedStatistics {
    std::size_t vertexCount = 0, faceCount = 0, triangleCount = 0;
    std::optional<core::Aabb> bounds;
};
/** @brief order 表示固定求值顺序；参数缺失与停用分别保留 nullopt/完整值。 */
struct ModifierState {
    std::array<ModifierKind, 2> order{ModifierKind::Mirror, ModifierKind::Subdivision};
    std::optional<core::modeling::MirrorOptions> mirror;
    std::optional<core::modeling::SubdivisionOptions> subdivision;
    bool operator==(const ModifierState&) const = default;
};
struct MeshSummaryResult {
    DocumentState state;
    MeshIdentity mesh;
    MeshSourceStatistics source;
    MeshEvaluatedStatistics evaluated;
    ModifierState modifiers;
};
struct MeshSourceVertex {
    core::modeling::VertexId vertexId = 0;
    std::optional<glm::vec3> position;
};
struct MeshSourceCorner {
    core::modeling::CornerId cornerId = 0;
    core::modeling::VertexId vertexId = 0;
    std::optional<MeshCornerInput> attributes;
};
struct MeshSourceFace {
    core::modeling::FaceId faceId = 0;
    std::vector<MeshSourceCorner> corners;
};
struct MeshSourcePageResult {
    DocumentState state;
    MeshIdentity mesh;
    MeshDomain domain = MeshDomain::Vertices;
    std::vector<MeshField> fields;
    std::vector<MeshSourceVertex> vertices;
    std::vector<MeshSourceFace> faces;
    std::optional<MeshSourceCursor> nextCursor;
};
struct MeshExtrudeResult {
    MutationResult command;
    MeshIdentity mesh;
    std::vector<core::modeling::FaceId> capFaceIds, sideFaceIds;
};
struct MeshInsetResult {
    MutationResult command;
    MeshIdentity mesh;
    std::vector<core::modeling::FaceId> innerFaceIds, rimFaceIds;
};
struct MeshCommandResult {
    MutationResult command;
    MeshIdentity mesh;
};
struct MeshTransformComponentsResult : MeshCommandResult {
    std::vector<core::modeling::VertexId> affectedVertexIds;
};
struct MeshBevelEdgeResult : MeshCommandResult {
    core::modeling::FaceId bevelFaceId = 0;
};
struct MeshLoopCutResult : MeshCommandResult {
    std::vector<core::modeling::EdgeKey> cutEdges;
};
struct MeshDeleteComponentsResult : MeshCommandResult {
    std::vector<core::modeling::VertexId> deletedVertexIds;
    std::vector<core::modeling::FaceId> deletedFaceIds;
    std::vector<core::modeling::CornerId> deletedCornerIds;
    std::vector<core::modeling::EdgeKey> deletedEdges;
};
struct MeshFillFaceResult : MeshCommandResult {
    core::modeling::FaceId faceId = 0;
};
struct ModifierCommandResult : MeshCommandResult {
    ModifierState modifiers;
    bool requeryRequired = false;
};
struct MethodDescription {
    QString name;
    QString kind;
    QString permission;
    QString externalGate;
    bool externalEnabled = true;
};
struct SystemDescription {
    DocumentState state;
    QString apiVersion = QStringLiteral("0.1.0");
    int wireVersion = 1;
    std::vector<MethodDescription> methods;
    int entityPageDefault = int(limits::sourcePageDefault);
    int entityPageMaximum = int(limits::sourcePageMaximum);
    std::map<QString, std::size_t> limits;
};

using ApiRequest = std::variant<
    EmptyRequest, DocumentRequest, EntityGetRequest, EntityListRequest, EntityCreateRequest,
    EntityUpdateRequest, HistoryMutationRequest, FileRequest, MeshCreateRequest, MeshTargetRequest,
    MeshSourcePageRequest, MeshExtrudeRequest, MeshInsetRequest, EntityDuplicateRequest,
    EntityDeleteRequest, EntitySetParentRequest, CollectionCreateRequest, CollectionUpdateRequest,
    CollectionDeleteRequest, CollectionAssignRequest, CameraCreateRequest, CameraUpdateRequest,
    LightCreateRequest, LightUpdateRequest, MeshMakeEditableRequest, MeshTransformComponentsRequest,
    MeshBevelEdgeRequest, MeshLoopCutRequest, MeshDeleteComponentsRequest, MeshFillFaceRequest,
    ModifierSetMirrorRequest, ModifierSetSubdivisionRequest, ModifierApplyRequest, FileSaveRequest,
    ImportGltfRequest, ExportObjRequest, DocumentNewRequest, BatchCreateEntitiesRequest,
    BatchSetTransformsRequest>;
} // namespace mini3d::editor::api
