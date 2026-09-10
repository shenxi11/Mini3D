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
#include "SceneNode.h"

#include <unordered_map>
namespace mini3d::core {
/** @brief 纯 CPU 场景树；所有结构变更经此接口维护双向父子关系。 */
class Scene {
  public:
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
    };
    /** @brief 保存完整子树和根在父节点中的位置；无此实体返回空快照。 */
    [[nodiscard]] SubtreeSnapshot snapshotSubtree(EntityId id) const;
    /** @brief 恢复原 ID/顺序；ID 冲突或外部父节点丢失时不写入并返回 false。 */
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
    /** @brief 节点与全部祖先可见时为 true；不存在时为 false。 */
    [[nodiscard]] bool isVisible(EntityId id) const;
    /** @brief 根节点按创建 ID 排序，提供稳定树顺序。 */
    [[nodiscard]] std::vector<EntityId> roots() const;
    /** @brief 按父先子后的顺序导出节点，保留兄弟顺序。 */
    [[nodiscard]] std::vector<SceneNode> nodes() const;
    /** @brief 文档载入时验证 ID、变换及无环父关系；失败保持当前场景不变。 */
    bool replaceNodes(const std::vector<SceneNode>& nodes);

  private:
    EntityId nextId_{1};
    Lighting lighting_;
    std::unordered_map<EntityId, SceneNode> entities_;
};
} // namespace mini3d::core
