/*
 * 模块名: TransformEntityCommand
 * 功能概述: 在撤销栈回放时应用实体变换。
 * 对外接口: TransformEntityCommand
 * 依赖关系: SceneViewModel
 * 输入输出: 命令回放到场景和界面通知。
 * 异常与错误: 非法命令由创建入口阻止。
 * 维护说明: 不合并不同手势，不执行 GPU 操作。
 */
#include "TransformEntityCommand.h"

#include "SceneViewModel.h"
namespace mini3d::editor {
TransformEntityCommand::TransformEntityCommand(SceneViewModel& model, core::EntityId id,
                                               core::Transform before, core::Transform after)
    : QUndoCommand(QStringLiteral("变换")), model_(model), id_(id), before_(before), after_(after) {
}
void TransformEntityCommand::undo() {
    model_.applyTransform(id_, before_);
}
void TransformEntityCommand::redo() {
    model_.applyTransform(id_, after_);
}
} // namespace mini3d::editor
