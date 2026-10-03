/*
 * 模块名: SubdivisionInspector
 * 功能概述: 展示固定链末端细分卡片，发送启用、级别、应用与删除意图。
 * 对外接口: SubdivisionInspector；依赖关系: Qt Widgets、SceneViewModel。
 * 输入输出: 表单到唯一历史操作，场景状态到控件。
 * 异常与错误: 求值失败恢复实际选项，由ViewModel报告原因。
 * 维护说明: 不拥有拓扑或历史，只有1–2级，不支持修改器重排。
 */
#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QPushButton;
namespace mini3d::editor {
class SceneViewModel;
/** @brief 固定Mirror→Subdivision链末端的参数视图。 */
class SubdivisionInspector final : public QWidget {
    Q_OBJECT
  public:
    explicit SubdivisionInspector(SceneViewModel& model, QWidget* parent = nullptr);

  private:
    void refresh();
    void submit();
    SceneViewModel& model_;
    QPushButton *add_, *apply_, *remove_;
    QCheckBox* enabled_;
    QComboBox* levels_;
};
} // namespace mini3d::editor
