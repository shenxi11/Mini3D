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
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
namespace mini3d::editor {
/** @brief 实例样式、无纹理 PBR、Sun 与相机组件编辑；作者值和预览值分开显示。 */
class AppearanceInspector final : public QWidget {
  public:
    explicit AppearanceInspector(SceneViewModel& model, QWidget* parent = nullptr);
    void refresh();

  protected:
    bool eventFilter(QObject* object, QEvent* event) override;

  private:
    void applySurface();
    void applyLighting();
    void applyCamera();
    void createMaterial();
    void assignMaterial();
    void applyRoughness();
    void applySun();
    void refreshJ1Values();
    [[nodiscard]] core::MaterialId selectedMaterial() const;
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
    QComboBox* materials_ = nullptr;
    QPushButton *createMaterial_ = nullptr, *assignMaterial_ = nullptr;
    QLineEdit *roughness_ = nullptr, *irradiance_ = nullptr;
    double roughnessOriginal_ = 1, irradianceOriginal_ = 1;
    QLabel *materialStatus_ = nullptr, *sunStatus_ = nullptr;
};
} // namespace mini3d::editor
