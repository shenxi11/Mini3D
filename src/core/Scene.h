/*
 * 模块名: Scene
 * 功能概述: 第三周场景数据与变换支持。
 * 对外接口: Scene
 * 依赖关系: C++ 标准库、GLM
 * 输入输出: 输入场景操作，输出节点状态或验证结果。
 * 异常与错误: 非法操作拒绝且保留既有状态；分配失败由运行时报告。
 * 维护说明: 不依赖 Qt/OpenGL，关联关系使用稳定 ID。
 */
#pragma once
#include "SceneCollection.h"
#include "SceneNode.h"
#include "modeling/MeshDerivation.h"
#include "modeling/Mirror.h"
#include "modeling/Subdivision.h"

#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
namespace mini3d::core {
/** @brief 持久化真源；派生数据和运行期 revision 不写文件。 */
struct EditableMeshResource {
    MeshId id;
    modeling::EditableMesh source;
    std::optional<modeling::MirrorOptions> mirror;
    std::optional<modeling::SubdivisionOptions> subdivision;
};
/** @brief 已验证的不可变 CPU 内容，历史共享快照而非重新执行算法。 */
struct EditableMeshContent {
    modeling::EditableMesh source;
    modeling::DerivedMesh derived;
    std::optional<modeling::MirrorOptions> mirror;
    std::optional<modeling::MirrorEvaluation> mirrorEvaluation;
    std::optional<modeling::SubdivisionOptions> subdivision;
    std::optional<modeling::SubdivisionEvaluation> subdivisionEvaluation;
    [[nodiscard]] const modeling::DerivedMesh& displayedDerived() const {
        if (subdivisionEvaluation)
            return subdivisionEvaluation->derived;
        return mirrorEvaluation ? mirrorEvaluation->derived : derived;
    }
    /** @brief 固定 Mirror→Subdivision 顺序的最终求值网格；不改变真实源笼。 */
    [[nodiscard]] const modeling::EditableMesh& evaluatedMesh() const {
        if (subdivisionEvaluation)
            return subdivisionEvaluation->mesh;
        return mirrorEvaluation ? mirrorEvaluation->mesh : source;
    }
    /** @brief 将最终求值面身份沿细分、镜像来源映射回真实源面。 */
    [[nodiscard]] modeling::FaceId sourceFace(modeling::FaceId evaluated) const {
        if (subdivisionEvaluation)
            evaluated = subdivisionEvaluation->faces.at(evaluated);
        return mirrorEvaluation ? mirrorEvaluation->faces.at(evaluated).source : evaluated;
    }
};
/** @brief Scene 拥有的网格状态；只读指针在下一次安装后失效。 */
struct EditableMeshRecord {
    std::shared_ptr<const EditableMeshContent> content;
    std::uint64_t topologyRevision = 0;
    std::uint64_t geometryRevision = 0;
    std::uint64_t evaluationRevision = 0;
};
/** @brief 纯 CPU 场景树；所有结构变更经此接口维护双向父子关系。 */
class Scene {
    struct BatchNodeState {
        EntityId entity = 0;
        EntityId parent = 0;
        Transform before;
        Transform after;
        std::vector<EntityId> children;
        bool capturesChildren = false;
        bool target = false;
        PrimitiveKind primitive = PrimitiveKind::Empty;
        std::optional<MeshRendererComponent> renderer;
        std::optional<CameraComponent> camera;
        std::optional<LightComponent> light;
        MeshId mesh = 0;
        std::shared_ptr<const EditableMeshContent> content;
    };

  public:
    /** @brief 完整新对象参数；候选准备之前统一验证，不依赖选择或游标。 */
    struct EntityCreateOptions {
        std::string name;
        EntityId parent = kInvalidEntity;
        PrimitiveKind primitive = PrimitiveKind::Empty;
        Transform transform;
        SurfaceStyle surface;
        bool visible = true;
        std::optional<CameraComponent> camera;
        std::optional<LightComponent> light;
    };
    /** @brief 批次准备失败的业务分类；资源上限由调用者传入，不依赖 API 配置。 */
    enum class BatchPrepareFailure { InvalidArgument, NotFound, UnsupportedTransform, LimitExceeded };
    /** @brief 整组基础对象候选；节点和父 children 缓冲在发布前统一分配。 */
    class PreparedEntityBatch {
      public:
        [[nodiscard]] const std::vector<EntityId>& entityIds() const {
            return entities_;
        }
        [[nodiscard]] const std::vector<std::pair<EntityId, glm::mat4>>& affectedWorldMatrices() const {
            return worlds_;
        }
        [[nodiscard]] std::size_t estimatedBytes() const {
            return estimatedBytes_;
        }

      private:
        friend class Scene;
        struct ParentChildren {
            std::vector<EntityId> before;
            std::vector<EntityId> buffer;
            std::vector<EntityId> appended;
        };
        const Scene* origin_ = nullptr;
        std::shared_ptr<const int> originToken_;
        bool installed_ = false;
        std::size_t estimatedBytes_ = 0;
        std::vector<EntityId> entities_;
        std::vector<SceneNode> expected_;
        std::vector<std::unordered_map<EntityId, SceneNode>::node_type> nodes_;
        std::map<EntityId, ParentChildren> parents_;
        std::map<EntityId, BatchNodeState> sources_;
        std::vector<std::pair<EntityId, glm::mat4>> worlds_;
    };
    /** @brief 完整准备基础对象整组；仅引用已有父节点，失败不发布或消耗身份。 */
    [[nodiscard]] std::optional<PreparedEntityBatch>
    prepareEntityBatch(const std::vector<EntityCreateOptions>& options, std::string& error,
                       std::size_t maximumItems, std::size_t maximumCandidateBytes,
                       BatchPrepareFailure* failure = nullptr);
    /** @brief 提交历史前检查整组来源、身份、父 children 和安装容量，不修改候选。 */
    [[nodiscard]] bool canInstallPreparedEntityBatch(const PreparedEntityBatch& prepared) const;
    /** @brief 整组预检后只移动节点并交换父 children；不处理事件或观察中间状态。 */
    bool installPreparedEntityBatch(PreparedEntityBatch& prepared);
    /** @brief 整组预检后收回同一批节点；后续属性/结构/集合命令须已逆序撤销。 */
    bool removePreparedEntityBatch(PreparedEntityBatch& prepared);
    /** @brief 完整 local TRS，目标在同一批次中只能出现一次。 */
    struct TransformBatchItem {
        EntityId entity = 0;
        Transform transform;
    };
    /** @brief 最终 TRS overlay 与精确 before/after；保存依赖的父链及几何来源。 */
    class PreparedTransformBatch {
      public:
        [[nodiscard]] const std::vector<EntityId>& entityIds() const {
            return entities_;
        }
        [[nodiscard]] bool hasChanges() const {
            return hasChanges_;
        }
        [[nodiscard]] const std::vector<std::pair<EntityId, glm::mat4>>& affectedWorldMatrices() const {
            return worlds_;
        }
        [[nodiscard]] std::size_t estimatedBytes() const {
            return estimatedBytes_;
        }

      private:
        friend class Scene;
        const Scene* origin_ = nullptr;
        std::shared_ptr<const int> originToken_;
        bool installed_ = false;
        bool hasChanges_ = false;
        std::size_t estimatedBytes_ = 0;
        std::vector<EntityId> entities_;
        std::map<EntityId, BatchNodeState> sources_;
        std::vector<std::pair<EntityId, glm::mat4>> worlds_;
    };
    /** @brief 验证最终 overlay 和全部受影响后代；不以逐项中间状态决定成功。 */
    [[nodiscard]] std::optional<PreparedTransformBatch>
    prepareTransformBatch(const std::vector<TransformBatchItem>& options, std::string& error,
                          std::size_t maximumItems, std::size_t maximumCandidateBytes,
                          BatchPrepareFailure* failure = nullptr) const;
    /** @brief 提交历史前检查全部精确 before 与父子/几何来源，不重新求矩阵。 */
    [[nodiscard]] bool canInstallPreparedTransformBatch(const PreparedTransformBatch& prepared) const;
    /** @brief 整组预检后安装精确 after；不分配或再次归一化四元数。 */
    bool installPreparedTransformBatch(PreparedTransformBatch& prepared);
    /** @brief 整组预检后恢复精确 before；不改变来源 token 或网格 revision。 */
    bool restorePreparedTransformBatch(PreparedTransformBatch& prepared);
    /** @brief 已准备但未发布的单对象；命令持有并在撤销时收回同一节点分配。 */
    class PreparedEntity {
      public:
        [[nodiscard]] EntityId entityId() const {
            return entity_;
        }
        [[nodiscard]] MeshId meshId() const {
            return mesh_;
        }
        /** @brief 只读联合候选内容，允许调用者在发布前核算源/求值预算。 */
        [[nodiscard]] std::shared_ptr<const EditableMeshContent> content() const {
            return content_;
        }

      private:
        friend class Scene;
        const Scene* origin_ = nullptr;
        std::shared_ptr<const int> originToken_;
        EntityId entity_ = 0;
        EntityId parent_ = 0;
        MeshId mesh_ = 0;
        std::size_t siblingIndex_ = 0;
        std::unordered_map<EntityId, SceneNode>::node_type node_;
        std::shared_ptr<const EditableMeshContent> content_;
    };
    /** @brief 验证属性/源网格并预分配安装资源；失败无可见对象或网格绑定。
     * source 非空时 primitive 必须为 Empty；失败可消耗内部身份但不会发布它们。
     */
    [[nodiscard]] std::optional<PreparedEntity>
    prepareEntity(const EntityCreateOptions& options, std::string& error,
                  const modeling::EditableMesh* source = nullptr);
    /** @brief 安装同场景候选，不重算几何；候选准备与安装之间不得另行改场景。 */
    bool installPreparedEntity(PreparedEntity& prepared);
    /** @brief 撤销单对象创建并收回节点；子树/集合编辑需先按唯一历史逆序撤销。 */
    bool removePreparedEntity(PreparedEntity& prepared);
    /** @brief 新子树的输入节点；唯一根的 parentIndex 为空，其余只引用较早输入。 */
    struct SubtreeNodeOptions {
        std::string name;
        std::optional<std::size_t> parentIndex;
        Transform transform;
        SurfaceStyle surface;
        bool visible = true;
        std::optional<MeshRendererComponent> meshRenderer;
    };
    /** @brief 离线准备新建或复制子树；身份映射/节点及成员均在发布前分配。 */
    class PreparedSubtree {
      public:
        [[nodiscard]] EntityId rootId() const {
            return root_;
        }
        [[nodiscard]] const std::map<EntityId, EntityId>& entityIdMap() const {
            return copies_;
        }

      private:
        friend class Scene;
        const Scene* origin_ = nullptr;
        std::shared_ptr<const int> originToken_;
        EntityId root_ = 0;
        EntityId parent_ = 0;
        std::size_t siblingIndex_ = 0;
        std::map<EntityId, EntityId> copies_;
        std::vector<std::unordered_map<EntityId, SceneNode>::node_type> nodes_;
        std::map<MeshId, std::shared_ptr<const EditableMeshContent>> meshes_;
        std::map<CollectionId, std::vector<std::pair<EntityId, std::set<EntityId>::node_type>>>
            memberships_;
    };
    /** @brief 验证并准备完整新子树；不解析资源，映射键是输入的 1-based 索引。
     * 全部属性、父关系及世界矩阵通过后才预分配；失败不发布节点或返回新身份。
     */
    [[nodiscard]] std::optional<PreparedSubtree>
    prepareNewSubtree(const std::vector<SubtreeNodeOptions>& options, EntityId parent,
                      std::string& error, std::size_t maximumEntities = 2048);
    /** @brief 只复制明确目标子树；最大规模在候选阶段检查，失败不发布任何对象。 */
    [[nodiscard]] std::optional<PreparedSubtree>
    prepareDuplicateSubtree(EntityId id, std::string& error, std::size_t maximumEntities = 2048);
    /** @brief 唯一历史串行回放；候选源Scene和外部父/集合必须仍有效。 */
    bool installPreparedSubtree(PreparedSubtree& prepared);
    /** @brief 收回完整准备子树及成员；后续子树/集合命令必须先按唯一历史逆序撤销。 */
    bool removePreparedSubtree(PreparedSubtree& prepared);
    /** @brief 只能由 Scene 生成的几何快照，安装时不重复校验/运行建模算法。 */
    class GeometrySnapshot {
      public:
        /** @brief 已验证的不可变候选内容，供预览读取；不安装到场景或推进 revision。 */
        [[nodiscard]] std::shared_ptr<const EditableMeshContent> content() const {
            return content_;
        }

      private:
        friend class Scene;
        const Scene* origin_ = nullptr;
        std::shared_ptr<const int> originToken_;
        EntityId entity_ = 0;
        PrimitiveKind primitive_ = PrimitiveKind::Empty;
        std::optional<MeshRendererComponent> renderer_;
        MeshId mesh_ = 0;
        std::shared_ptr<const EditableMeshContent> content_;
    };
    /** @brief 捕获对象当前几何；不存在时返回空值。 */
    [[nodiscard]] std::optional<GeometrySnapshot> geometrySnapshot(EntityId id) const;
    /** @brief 离线验证/派生候选，成功才分配身份；不改变绑定、内容或 revision。 */
    [[nodiscard]] std::optional<GeometrySnapshot>
    prepareEditableGeometry(EntityId id, const modeling::EditableMesh& source, std::string& error);
    /** @brief 内部执行顶点变换并复用其完整派生结果；不改变绑定、内容或 revision。 */
    [[nodiscard]] std::optional<GeometrySnapshot>
    prepareTransformedEditableGeometry(EntityId id, const modeling::EditableMesh& source,
                                       const std::set<modeling::VertexId>& selected,
                                       const glm::dmat4& objectToWorld,
                                       const glm::dmat4& worldDelta, std::string& error);
    /** @brief 复用本 Scene 同实体、同网格的可信 before；允许已安装其他预览/历史内容。
     * 仅重派生受影响源面，修改器仍完整求值；失败不安装且不推进 revision。
     */
    [[nodiscard]] std::optional<GeometrySnapshot> prepareTransformedEditableGeometry(
        EntityId id, const GeometrySnapshot& before, const std::set<modeling::VertexId>& selected,
        const glm::dmat4& objectToWorld, const glm::dmat4& worldDelta, std::string& error);
    /** @brief 验证单个 Mirror 参数并生成快照；nullopt 表示删除，不改变真源。 */
    [[nodiscard]] std::optional<GeometrySnapshot>
    prepareMirror(EntityId id, std::optional<modeling::MirrorOptions> options,
                  std::string& error);
    /** @brief 仅烘焙当前 Mirror 为真源，保留后续 Subdivision 参数；不安装。 */
    [[nodiscard]] std::optional<GeometrySnapshot> prepareAppliedMirror(EntityId id,
                                                                        std::string& error);
    /** @brief 离线准备单个 Subdivision；nullopt 表示删除，始终保留真实源笼。 */
    [[nodiscard]] std::optional<GeometrySnapshot>
    prepareSubdivision(EntityId id, std::optional<modeling::SubdivisionOptions> options,
                       std::string& error);
    /** @brief 烘焙最终求值链为真源并移除 Mirror/Subdivision；不安装。 */
    [[nodiscard]] std::optional<GeometrySnapshot> prepareAppliedSubdivision(EntityId id,
                                                                            std::string& error);
    /** @brief 安装已验证快照；三类 revision 均保守前进，Undo 不回退。 */
    bool installGeometry(const GeometrySnapshot& snapshot);
    [[nodiscard]] const EditableMeshRecord* editableMesh(MeshId id) const;
    /** @brief 对可编辑真源进行只读镜像求值，不改绑定、source或revision；不安装修改器。
     * 求值mesh可交prepareEditableGeometry生成应用候选，持久参数和UI在后续接入。
     */
    [[nodiscard]] modeling::MirrorResult
    evaluateMirror(EntityId id, const modeling::MirrorOptions& options) const;
    /** @brief 只导出当前节点引用的网格，不保存撤销保留的资源。 */
    [[nodiscard]] std::vector<EditableMeshResource> editableMeshes() const;
    /** @brief 保留输入排列的只读单层集合；删除集合不删除成员对象。 */
    [[nodiscard]] const std::vector<SceneCollection>& collections() const {
        return collections_;
    }
    /** @brief 创建空集合并追加到末尾；名称为空或 ID 耗尽返回 0，高水位不回退。 */
    CollectionId createCollection(std::string name);
    /** @brief 验证 ID、名称、成员引用及单对象单组约束，成功才替换集合。 */
    bool replaceCollections(const std::vector<SceneCollection>& collections);
    /** @brief 只由 Scene 生成的内存子树快照；不作为外部文件格式使用。 */
    class SubtreeSnapshot {
      public:
        [[nodiscard]] EntityId rootId() const {
            return nodes_.empty() ? kInvalidEntity : nodes_.front().id;
        }

      private:
        friend class Scene;
        std::vector<SceneNode> nodes_;
        std::size_t siblingIndex_ = 0;
        std::map<EntityId, CollectionId> collectionMemberships_;
    };
    /** @brief 保存完整子树和根在父节点中的位置；无此实体返回空快照。 */
    [[nodiscard]] SubtreeSnapshot snapshotSubtree(EntityId id) const;
    /** @brief 恢复原 ID/顺序和成员增量；ID 冲突、外部父节点或集合丢失时不写入。 */
    bool restoreSubtree(const SubtreeSnapshot& snapshot);
    /** @brief 深复制节点、共享 AssetId；新根与原根同父同位置，名称附加 Copy。 */
    EntityId duplicateSubtree(EntityId id);
    /** @brief 创建节点，父节点不存在或名称为空时返回 0，不消耗 ID。 */
    EntityId createEntity(std::string name, EntityId parent = kInvalidEntity,
                          PrimitiveKind primitive = PrimitiveKind::Empty);
    /** @brief 级联删除整棵子树；无此 ID 返回 false。 */
    bool removeEntity(EntityId id);
    /** @brief 换父并保留局部变换；禁止自身、循环或不存在的父节点。 */
    bool setParent(EntityId child, EntityId parent);
    /** @brief 调整非根节点兄弟位置，用于换父撤销；越界不修改。 */
    bool setSiblingIndex(EntityId child, std::size_t index);
    /** @brief 查询只读临时节点；不存在时返回 nullptr。 */
    [[nodiscard]] const SceneNode* find(EntityId id) const;
    /** @brief 名称非空时更新；失败不改变数据。 */
    bool renameEntity(EntityId id, std::string name);
    /** @brief 更新节点自身可见性，子节点仍受祖先可见性约束。 */
    bool setVisible(EntityId id, bool visible);
    /** @brief 接受有限且非奇异变换，旋转归一化后保存；失败保持原值。 */
    bool setTransform(EntityId id, const Transform& transform);
    /** @brief 精确安装已确认或离线准备的历史 TRS，避免重复归一化引起回放漂移。
     * 不用于未处理的外部输入；调用者须先验证并归一化新旋转，失败保持原值。
     */
    bool installTransformSnapshot(EntityId id, const Transform& transform);
    /** @brief 绑定导入资源引用并关闭内置几何标识；Core 不解析或拥有资源。 */
    bool setMeshRenderer(EntityId id, MeshRendererComponent component);
    bool setSurface(EntityId id, const SurfaceStyle& surface);
    /** @brief 给空节点添加或更新组件；与几何及另一类组件互斥，非法值不写入。 */
    bool setCamera(EntityId id, const CameraComponent& camera);
    bool setLight(EntityId id, const LightComponent& light);
    /** @brief 只组合祖先和自身旋转，忽略缩放（含镜像）对设备朝向的影响。 */
    [[nodiscard]] glm::quat worldRotation(EntityId id) const;
    /** @brief 可见 Camera 的透视预览矩阵；无效相机/宽高比返回空值。 */
    [[nodiscard]] std::optional<glm::mat4> cameraViewProjection(EntityId id, float aspect) const;
    /** @brief 最小 ID 的可见灯生效；有灯但全隐藏时仅环境光，无灯沿用兼容光照。 */
    [[nodiscard]] Lighting effectiveLighting() const;
    bool setLighting(const Lighting& lighting);
    [[nodiscard]] const Lighting& lighting() const {
        return lighting_;
    }
    /** @brief ParentWorld × Local；无效 ID 返回单位矩阵。 */
    [[nodiscard]] glm::mat4 worldMatrix(EntityId id) const;
    /** @brief 节点和全部祖先及其所属集合均可见时为 true；不存在时为 false。 */
    [[nodiscard]] bool isVisible(EntityId id) const;
    /** @brief 根节点按创建 ID 排序，提供稳定树顺序。 */
    [[nodiscard]] std::vector<EntityId> roots() const;
    /** @brief 按父先子后的顺序导出节点，保留兄弟顺序。 */
    [[nodiscard]] std::vector<SceneNode> nodes() const;
    /** @brief 文档载入时验证 ID、变换、无环父关系和集合；失败保持当前场景不变。 */
    bool replaceNodes(const std::vector<SceneNode>& nodes,
                      const std::vector<EditableMeshResource>& meshes = {},
                      const std::vector<SceneCollection>& collections = {});

  private:
    [[nodiscard]] BatchNodeState batchNodeState(const SceneNode& node) const;
    [[nodiscard]] bool matchesBatchNodeState(const BatchNodeState& state, bool after) const;
    [[nodiscard]] std::optional<GeometrySnapshot>
    prepareEditableGeometryContent(EntityId id, modeling::EditableMesh source,
                                   std::optional<modeling::DerivedMesh> derived,
                                   std::string& error);
    // Scene 原址被新文档替换时，旧会话快照也不能作为新的可信 before。
    std::shared_ptr<const int> geometrySnapshotOrigin_ = std::make_shared<const int>(0);
    EntityId nextId_{1};
    Lighting lighting_;
    std::unordered_map<EntityId, SceneNode> entities_;
    MeshId nextMeshId_ = 1;
    std::uint64_t nextMeshRevision_ = 1;
    std::unordered_map<MeshId, EditableMeshRecord> editableMeshes_;
    CollectionId nextCollectionId_ = 1;
    std::vector<SceneCollection> collections_;
};
} // namespace mini3d::core
