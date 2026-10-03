/*
 * 模块名: MirrorInspector
 * 功能概述: 单 Mirror 参数卡片，将用户意图交给 SceneViewModel。
 * 对外接口: MirrorInspector。
 * 依赖关系: Qt Widgets、SceneViewModel。
 * 输入输出: 当前选择与参数到可撤销修改器操作。
 * 异常与错误: 非法求值由 ViewModel 拒绝并保留原状态。
 * 维护说明: 刷新控件时阻断信号，不持有 Scene 可变数据。
 */
#pragma once

#include "SceneViewModel.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;

namespace mini3d::editor {
/** @brief 编辑已绑定可编辑网格的唯一 Mirror 修改器。 */
class MirrorInspector final : public QWidget {
  public:
    explicit MirrorInspector(SceneViewModel& model, QWidget* parent = nullptr);
    void refresh();

  private:
    void submit();
    SceneViewModel& model_;
    QPushButton* add_ = nullptr;
    QPushButton* apply_ = nullptr;
    QPushButton* remove_ = nullptr;
    QCheckBox* enabled_ = nullptr;
    QCheckBox* merge_ = nullptr;
    QCheckBox* clipping_ = nullptr;
    QComboBox* axis_ = nullptr;
    QDoubleSpinBox* threshold_ = nullptr;
};
} // namespace mini3d::editor
