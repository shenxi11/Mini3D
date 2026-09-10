/*
 * 模块名: TransformEntityCommand
 * 功能概述: 保存实体变换前后值，实现一次编辑的一条撤销记录。
 * 对外接口: TransformEntityCommand、undo、redo
 * 依赖关系: Qt Undo、Core Transform
 * 输入输出: EntityId 和变换快照到 ViewModel 受控写入。
 * 异常与错误: 创建前由 ViewModel 验证输入和实体存在性。
 * 维护说明: 由 QUndoStack 拥有，命令不拥有 ViewModel，不保存节点指针。
 */
#pragma once
#include "core/EntityId.h"
#include "core/Transform.h"

#include <QUndoCommand>
namespace mini3d::editor {
class SceneViewModel;
/** @brief redo/undo 只应用快照，不递归创建命令。 */
class TransformEntityCommand final : public QUndoCommand {
  public:
    /** @brief 保存已验证的前后变换；ViewModel 生命周期须长于命令。 */
    TransformEntityCommand(SceneViewModel& model, core::EntityId id, core::Transform before,
                           core::Transform after);
    void undo() override;
    void redo() override;

  private:
    SceneViewModel& model_;
    core::EntityId id_;
    core::Transform before_, after_;
};
} // namespace mini3d::editor
