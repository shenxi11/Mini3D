/*
 * 模块名: HistoryService
 * 功能概述: 为唯一 QUndoStack 提供可重开的几何快照命令及顶端参数替换。
 * 对外接口: GeometryHistoryState、ReopenableGeometryEdit、HistoryService。
 * 依赖关系: Qt Undo、Core Scene、组件选择；不持有第二个历史栈或 GPU 资源。
 * 输入输出: 已验证快照和离线重算回调到同一条逻辑历史的新 after。
 * 异常与错误: 重算/准备分配失败不改模型、参数或保存点；提交段不允许分配或通知。
 * 维护说明: 安装回调只移动已准备状态，通知独立在提交后执行；仅用于已有可编辑网格。
 */
#pragma once

#include "core/Scene.h"
#include "editor/ComponentSelection.h"

#include <QUndoStack>
#include <functional>
#include <variant>

namespace mini3d::editor {
/** @brief 几何与结果选区一起回放，所有拷贝均在安装前完成。 */
struct GeometryHistoryState {
    core::Scene::GeometrySnapshot geometry;
    ComponentSelection selection;
};

/** @brief 倒角宽度使用独立类型，避免与内插厚度的double参数混用。 */
struct BevelOperationParameters {
    double width = 0;
    bool operator==(const BevelOperationParameters&) const = default;
};
/** @brief 挤出位移、内插厚度与倒角宽度有明确类型身份，不允许互换算子。 */
using GeometryOperationParameters = std::variant<glm::dvec3, double, BevelOperationParameters>;

/** @brief 已确认建模参数与离线重算契约；回调不得递归修改历史。 */
struct ReopenableGeometryEdit {
    core::EntityId entity;
    QString label;
    GeometryHistoryState before, after;
    GeometryOperationParameters parameters{glm::dvec3(0)};
    std::function<std::optional<core::Scene::GeometrySnapshot>(const GeometryOperationParameters&,
                                                               std::string&)>
        recompute;
    // 接收已准备的独立选区；开始写入后必须无异常、无分配，且不发 UI 通知。
    std::function<void(GeometryHistoryState)> install;
    std::function<void()> notify;
};

/** @brief 非拥有适配器；Qt 仍拥有全部命令，仅当前栈顶允许调整。 */
class HistoryService final {
  public:
    explicit HistoryService(QUndoStack& stack);
    void push(ReopenableGeometryEdit edit);
    [[nodiscard]] bool canAdjust() const;
    [[nodiscard]] core::EntityId target() const;
    [[nodiscard]] std::optional<glm::dvec3> worldOffset() const;
    [[nodiscard]] std::optional<double> insetThickness() const;
    [[nodiscard]] std::optional<double> bevelWidth() const;
    /** @brief 离线求值成功才替换 after；不变值不改保存点，bad_alloc 返回中文错误。 */
    bool adjust(const glm::dvec3& worldOffset, QString& error);
    /** @brief 仅调整末端内插的局部厚度；与挤出共用同一原子提交路径。 */
    bool adjustInset(double localThickness, QString& error);
    /** @brief 仅调整末端倒角的局部宽度；与其他参数共用同一原子提交路径。 */
    bool adjustBevel(double localWidth, QString& error);
    /** @brief 仅在 F9 实际替换 after 后通知一次提交；不为失败或 no_change 调用。 */
    void setReplacementCommittedCallback(std::function<void()> callback);

  private:
    class GeometryCommand;
    [[nodiscard]] const GeometryCommand* top() const;
    bool adjustParameters(const GeometryOperationParameters& parameters, QString& error);
    QUndoStack& stack_;
    std::function<void()> replacementCommitted_;
};
} // namespace mini3d::editor
