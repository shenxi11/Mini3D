/*
 * 模块名: SubtreeCommand
 * 功能概述: 回放子树结构和选择，复制重做复用首次生成的实体 ID。
 * 对外接口: SubtreeCommand
 * 依赖关系: SceneViewModel、Scene
 * 输入输出: undo/redo 到结构通知和选中状态。
 * 异常与错误: 内存快照回放失败用断言报告历史契约违反。
 * 维护说明: ViewModel 生命周期长于命令；不递归入栈。
 */
#include "SubtreeCommand.h"

#include "SceneViewModel.h"
namespace mini3d::editor {
SubtreeCommand::SubtreeCommand(SceneViewModel& model, core::EntityId id, Kind kind,
                               core::EntityId previousSelection)
    : QUndoCommand(kind == Kind::Duplicate ? QStringLiteral("复制")
                   : kind == Kind::Delete  ? QStringLiteral("删除")
                                           : QStringLiteral("创建 / 导入")),
      model_(model), original_(id), kind_(kind), previousSelection_(previousSelection) {
    if (kind_ != Kind::Duplicate) {
        snapshot_ = model_.scene_->snapshotSubtree(id);
    }
}
void SubtreeCommand::undo() {
    apply(false);
}
void SubtreeCommand::redo() {
    if (kind_ == Kind::Created && firstRedo_) {
        firstRedo_ = false;
        return;
    }
    apply(true);
}
void SubtreeCommand::apply(bool forward) {
    emit model_.structureAboutToChange();
    bool changed = true;
    if (kind_ == Kind::Duplicate && snapshot_.rootId() == core::kInvalidEntity) {
        const auto copy = model_.scene_->duplicateSubtree(original_);
        if (copy != core::kInvalidEntity) {
            model_.scene_->renameEntity(copy, model_.scene_->find(original_)->name + " 副本");
        }
        snapshot_ = model_.scene_->snapshotSubtree(copy);
        changed = copy != core::kInvalidEntity;
    } else if (forward == (kind_ != Kind::Delete)) {
        changed = model_.scene_->restoreSubtree(snapshot_);
    } else {
        changed = model_.scene_->removeEntity(snapshot_.rootId());
    }
    Q_ASSERT(changed);
    emit model_.structureChanged();
    model_.selection_.setSelectedEntity(
        forward ? (kind_ != Kind::Delete ? snapshot_.rootId() : core::kInvalidEntity)
                : (kind_ == Kind::Created ? previousSelection_ : original_));
    emit model_.sceneChanged();
}
} // namespace mini3d::editor
