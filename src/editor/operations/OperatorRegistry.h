/*
 * 模块名: OperatorRegistry
 * 功能概述: 为已实现操作提供稳定身份、搜索、上下文校验和现有 QAction 适配。
 * 对外接口: OperatorRegistry、OperatorContext、OperatorDescriptor、OperatorMatch
 * 依赖关系: Qt、SceneViewModel；不修改 Scene 或创建历史栈。
 * 输入输出: 冻结的区域/文档/选择与操作 ID 到既有业务意图。
 * 异常与错误: 文档重置、目标变化、区域不适用和禁用操作给出中文原因。
 * 维护说明: 静态描述表只登记已实现操作，显示语言不能改变 ID。
 */
#pragma once

#include "KeymapRouter.h"
#include "core/EntityId.h"

#include <QKeySequence>
#include <QVariantMap>
#include <QVector>
#include <functional>

namespace mini3d::editor {
class SceneViewModel;

/** @brief 弹出操作界面前冻结的调用身份；代际防止同一个 Scene 地址重置后误用。 */
struct OperatorContext {
    QPointer<SceneViewModel> model;
    quint64 documentGeneration = 0;
    InputArea area = InputArea::None;
    EditorKeymap keymap = EditorKeymap::Blender;
    core::EntityId selectedEntity = core::kInvalidEntity;
    bool editMode = false;
    std::uint64_t editableMesh = 0;
    std::uint64_t meshRevision = 0;
    quint64 selectionRevision = 0;
};

/** @brief 操作描述及现有无参数 QAction 适配；参数算子随对应任务注册。 */
struct OperatorDescriptor {
    QString id;
    QString chineseName;
    QString englishName;
    QString category;
    QString iconKey;
    QKeySequence defaultShortcut;
    QVariantMap parameterSchema;
    std::function<QString(const OperatorContext&)> poll;
    QPointer<QAction> action;
    bool undoable = false;
    bool reopenable = false;
    bool repeatable = false;
};

/** @brief 搜索显示结果；禁用项仍可被选中以阅读原因，但不能执行。 */
struct OperatorMatch {
    QString id;
    QString disabledReason;
    int rank = 0;
};

/** @brief UI 意图适配器；业务仍经同一 QAction/SceneViewModel，不持有模型快照。 */
class OperatorRegistry final : public QObject {
    Q_OBJECT
  public:
    explicit OperatorRegistry(QMainWindow& window, SceneViewModel& model,
                              QObject* parent = nullptr);
    [[nodiscard]] OperatorContext captureContext(InputArea area,
                                                 EditorKeymap keymap = EditorKeymap::Blender) const;
    [[nodiscard]] const OperatorDescriptor* descriptor(const QString& id) const;
    [[nodiscard]] QVector<OperatorMatch> search(const QString& query,
                                                const OperatorContext& context) const;
    /** @brief 空串表示可执行；每次执行前重新校验，不能信任旧搜索结果。 */
    [[nodiscard]] QString disabledReason(const QString& id, const OperatorContext& context) const;
    /** @brief 触发既有动作一次；同步业务失败返回 false，失败原因经 executionFailed 输出。 */
    bool execute(const QString& id, const OperatorContext& context);
    /** @brief 仅同步执行 QAction 时有效，供区域操作使用 F3/Q 捕获的源区域。 */
    [[nodiscard]] InputArea executionArea() const;
    [[nodiscard]] static QString areaName(InputArea area);

  signals:
    void executed(const QString& id);
    void executionFailed(const QString& reason);

  private:
    SceneViewModel& model_;
    quint64 documentGeneration_ = 1;
    InputArea executionArea_ = InputArea::None;
    QVector<OperatorDescriptor> descriptors_;
};
} // namespace mini3d::editor
