/*
 * 模块名: SubtreeCommand
 * 功能概述: 复制/删除子树及其撤销，保持实体标识与选择一致。
 * 对外接口: SubtreeCommand
 * 依赖关系: QUndoCommand、Core Scene
 * 输入输出: 选中实体和操作种类到子树快照回放。
 * 异常与错误: 入口验证实体存在；未纳入历史的编辑由 ViewModel 清空历史。
 * 维护说明: 快照仅保存 CPU 节点及 AssetId，不拥有 GPU 资源。
 */
#pragma once
#include "AnimationReplay.h"
#include "core/Scene.h"

#include <QUndoCommand>
namespace mini3d::editor {
class SceneViewModel;
/** @brief 固定prepared子树用于恢复与回放，版本推进后才发布结构和选择。 */
class SubtreeCommand final : public QUndoCommand, public AnimationReplay {
  public:
    enum class Kind { Duplicate, Delete, Created };
    /** @brief Created 记录已创建的子树，首次 redo 不重复创建；其余模式接收有效选中 ID。 */
    SubtreeCommand(SceneViewModel& model, core::EntityId id, Kind kind,
                   core::EntityId previousSelection = core::kInvalidEntity);
    /** @brief GUI/API共同使用发布前准备的节点、曲线及映射；API不抢选区。 */
    SubtreeCommand(SceneViewModel& model, std::shared_ptr<core::Scene::PreparedSubtree> prepared,
                   Kind kind,
                   core::EntityId original, bool selectResult,
                   core::EntityId previousSelection = core::kInvalidEntity);
    void undo() override;
    void redo() override;
    [[nodiscard]] bool supportsAnimationReplay() const override;
    [[nodiscard]] std::optional<PreparedAnimationReplay>
    prepareReplay(bool forward, QString& error) const override;

  private:
    [[nodiscard]] std::optional<core::EntityId> replaySelection(bool forward) const;
    void apply(bool forward);
    SceneViewModel& model_;
    core::EntityId original_;
    Kind kind_;
    core::EntityId previousSelection_;
    bool firstRedo_ = true;
    bool selectResult_ = true;
    std::shared_ptr<core::Scene::PreparedSubtree> prepared_;
    std::vector<core::EntityId> entityIds_;
    std::shared_ptr<const renderer_gl::PoseGeometry> retainedGeometry_;
};
} // namespace mini3d::editor
