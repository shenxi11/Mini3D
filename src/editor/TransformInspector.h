/*
 * 模块名: TransformInspector
 * 功能概述: 第三周场景选择与属性编辑的 TransformInspector 层。
 * 对外接口: TransformInspector
 * 依赖关系: Qt Widgets/Core、mini3d_core
 * 输入输出: 输入用户意图或场景通知，输出模型状态或界面刷新。
 * 异常与错误: 通过返回值和 operationFailed 报告非法编辑。
 * 维护说明: 同步 UI 线程操作；不持有节点地址或 GPU 资源。
 */
#pragma once
#include "SceneViewModel.h"

#include <QWidget>
#include <array>
class QLineEdit;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
namespace mini3d::editor {
/** @brief 选择对象的名称/可见性/父节点/局部 TRS 视图，不直接修改 Scene。 */
class TransformInspector final : public QWidget {
    Q_OBJECT
  public:
    explicit TransformInspector(SceneViewModel& viewModel, QWidget* parent = nullptr);
    /** @brief 刷新当前选择并阻断控件通知，不产生业务回写。 */
    void refresh();

  private:
    void appendParentOptions(core::EntityId id, int depth, core::EntityId selected);
    SceneViewModel& viewModel_;
    QLineEdit* name_;
    QCheckBox* visible_;
    QComboBox* parent_;
    QLabel* message_;
    std::array<QDoubleSpinBox*, 9> values_{};
};
} // namespace mini3d::editor
