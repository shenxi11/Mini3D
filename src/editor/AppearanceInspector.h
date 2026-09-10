/*
 * 模块名: AppearanceInspector
 * 功能概述: 展示选中实例样式和场景方向光参数。
 * 对外接口: AppearanceInspector
 * 依赖关系: Qt Widgets、SceneViewModel
 * 输入输出: 控件值到 ViewModel 意图，场景通知到无回写刷新。
 * 异常与错误: 非法参数由 ViewModel 拒绝并显示错误。
 * 维护说明: 不直接修改 Scene 或共享资源。
 */
#pragma once
#include "SceneViewModel.h"

#include <QWidget>
#include <array>
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
namespace mini3d::editor {
/** @brief 实例材质、方向灯及相机组件编辑，不提供 PBR 参数。 */
class AppearanceInspector final : public QWidget {
  public:
    explicit AppearanceInspector(SceneViewModel& model, QWidget* parent = nullptr);
    void refresh();

  private:
    void applySurface();
    void applyLighting();
    void applyCamera();
    SceneViewModel& model_;
    std::array<QDoubleSpinBox*, 3> tint_{}, direction_{}, color_{};
    QDoubleSpinBox* intensity_;
    QDoubleSpinBox* ambient_;
    QCheckBox* texture_;
    QCheckBox* vertexColor_;
    QLabel* lightingLabel_;
    QLabel* previewLabel_;
    std::array<QDoubleSpinBox*, 3> camera_{};
    QPushButton* preview_;
    QPushButton* exitPreview_;
};
} // namespace mini3d::editor
