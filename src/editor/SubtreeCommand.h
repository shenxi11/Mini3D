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
#include "core/Scene.h"

#include <QUndoCommand>
namespace mini3d::editor {
class SceneViewModel;
/** @brief 同一快照用于删除恢复或复制回放；一次操作只发送一对结构通知。 */
class SubtreeCommand final : public QUndoCommand {
  public:
    enum class Kind { Duplicate, Delete, Created };
    /** @brief Created 记录已创建的子树，首次 redo 不重复创建；其余模式接收有效选中 ID。 */
    SubtreeCommand(SceneViewModel& model, core::EntityId id, Kind kind,
                   core::EntityId previousSelection = core::kInvalidEntity);
    void undo() override;
    void redo() override;

  private:
    void apply(bool forward);
    SceneViewModel& model_;
    core::EntityId original_;
    Kind kind_;
    core::EntityId previousSelection_;
    bool firstRedo_ = true;
    core::Scene::SubtreeSnapshot snapshot_;
};
} // namespace mini3d::editor
