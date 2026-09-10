/*
 * 模块名: EditCommand
 * 功能概述: 对简单属性编辑保存前后动作，复用 Qt 撤销回放。
 * 对外接口: EditCommand
 * 依赖关系: QUndoCommand、std::function
 * 输入输出: 按值捕获的属性快照到 ViewModel 场景通知。
 * 异常与错误: 入口先验证再构造；回放不递归入栈。
 * 维护说明: 动作仅捕获 ViewModel 和值，不捕获 SceneNode 地址。
 */
#pragma once
#include <QUndoCommand>
#include <functional>
namespace mini3d::editor {
/** @brief 用于材质、光照、名称和显隐等共享生命周期的简单编辑。 */
class EditCommand final : public QUndoCommand {
  public:
    EditCommand(const QString& text, std::function<void()> undo, std::function<void()> redo)
        : QUndoCommand(text), undo_(std::move(undo)), redo_(std::move(redo)) {}
    void undo() override {
        undo_();
    }
    void redo() override {
        redo_();
    }

  private:
    std::function<void()> undo_, redo_;
};
} // namespace mini3d::editor
