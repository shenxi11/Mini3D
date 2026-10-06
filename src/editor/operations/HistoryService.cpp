/*
 * 模块名: HistoryService
 * 功能概述: 在不增删 Qt 命令的情况下替换栈顶快照，避免 undo/push 中途分配失效。
 * 对外接口: HistoryService.h；依赖关系: 唯一 QUndoStack、已准备的几何状态。
 * 输入输出: 原 before 重算到 after，保持索引、历史条数和原 before。
 * 异常与错误: 准备阶段捕获 bad_alloc；安装段依赖无异常回调，完成后才通知视图。
 * 维护说明: 不使用 const_cast，命令自身以受控方法管理其可替换内容。
 */
#include "HistoryService.h"

#include <new>
#include <type_traits>

namespace mini3d::editor {
class HistoryService::GeometryCommand final : public QUndoCommand {
  public:
    explicit GeometryCommand(ReopenableGeometryEdit edit)
        : QUndoCommand(edit.label), edit_(std::move(edit)) {}
    void undo() override {
        edit_.install(edit_.before);
        edit_.notify();
    }
    void redo() override {
        edit_.install(edit_.after);
        edit_.notify();
    }
    // 栈只暴露 const 命令；逻辑命令身份不变，after 的替换由服务在栈顶验证后执行。
    mutable ReopenableGeometryEdit edit_;
};

HistoryService::HistoryService(QUndoStack& stack) : stack_(stack) {}
void HistoryService::setReplacementCommittedCallback(std::function<void()> callback) {
    replacementCommitted_ = std::move(callback);
}
void HistoryService::push(ReopenableGeometryEdit edit) {
    stack_.push(new GeometryCommand(std::move(edit)));
}
const HistoryService::GeometryCommand* HistoryService::top() const {
    if (stack_.index() == 0 || stack_.index() != stack_.count())
        return nullptr;
    return dynamic_cast<const GeometryCommand*>(stack_.command(stack_.index() - 1));
}
bool HistoryService::canAdjust() const {
    return top() != nullptr;
}
core::EntityId HistoryService::target() const {
    const auto* command = top();
    return command ? command->edit_.entity : core::kInvalidEntity;
}
std::optional<glm::dvec3> HistoryService::worldOffset() const {
    const auto* command = top();
    const auto* value = command ? std::get_if<glm::dvec3>(&command->edit_.parameters) : nullptr;
    return value ? std::optional(*value) : std::nullopt;
}
std::optional<double> HistoryService::insetThickness() const {
    const auto* command = top();
    const auto* value = command ? std::get_if<double>(&command->edit_.parameters) : nullptr;
    return value ? std::optional(*value) : std::nullopt;
}
std::optional<double> HistoryService::bevelWidth() const {
    const auto* command = top();
    const auto* value =
        command ? std::get_if<BevelOperationParameters>(&command->edit_.parameters) : nullptr;
    return value ? std::optional(value->width) : std::nullopt;
}
bool HistoryService::adjust(const glm::dvec3& worldOffset, QString& error) {
    return adjustParameters(worldOffset, error);
}
bool HistoryService::adjustInset(double localThickness, QString& error) {
    return adjustParameters(localThickness, error);
}
bool HistoryService::adjustBevel(double localWidth, QString& error) {
    return adjustParameters(BevelOperationParameters{localWidth}, error);
}
bool HistoryService::adjustParameters(const GeometryOperationParameters& parameters,
                                      QString& error) {
    error.clear();
    const auto* command = top();
    if (!command) {
        error = QStringLiteral("当前没有可调整的上一步。");
        return false;
    }
    auto& edit = command->edit_;
    if (parameters.index() != edit.parameters.index()) {
        error = QStringLiteral("参数类型与当前上一步不匹配，请重新打开参数面板。");
        return false;
    }
    if (parameters == edit.parameters)
        return true;
    std::function<void()> notify;
    try {
        std::string diagnostic;
        const auto geometry = edit.recompute(parameters, diagnostic);
        if (!geometry) {
            error = QString::fromStdString(diagnostic);
            return false;
        }
        if (geometry->content()->source == edit.after.geometry.content()->source)
            return true;
        GeometryHistoryState replacement{*geometry, edit.after.selection};
        auto installed = replacement;
        notify = edit.notify;
        static_assert(std::is_nothrow_move_assignable_v<GeometryHistoryState>);
        edit.install(std::move(installed));
        edit.after = std::move(replacement);
        static_assert(std::is_nothrow_copy_assignable_v<GeometryOperationParameters>);
        edit.parameters = parameters;
    } catch (const std::bad_alloc&) {
        error = QStringLiteral("内存不足，未修改模型或上一步参数。");
        return false;
    }
    // 仅替换恰好已保存的顶端时失效；保存于 before 的状态仍能通过 Undo 回到 clean。
    if (stack_.cleanIndex() == stack_.index())
        stack_.resetClean();
    if (replacementCommitted_)
        replacementCommitted_();
    notify();
    return true;
}
} // namespace mini3d::editor
