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
  public:
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
