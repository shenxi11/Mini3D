/*
 * 模块名: LastOperationPanel
 * 功能概述: 在真实视口左下显示同一上一步参数，F9 与折叠标题共享界面。
 * 对外接口: LastOperationPanel::open；依赖关系: Qt Widgets、SceneViewModel、CommitSpinBox。
 * 输入输出: 挤出世界位移或内插局部厚度到调整意图，模型通知到参数和禁用原因。
 * 异常与错误: 非法输入留在字段，模型失效丢弃草稿，不自动提交或重建历史。
 * 维护说明: 只在视口上叠加普通 QWidget，不重挂 GL 控件，阻止鼠标向视口穿透。
 */
#pragma once

#include <QFrame>
#include <array>

class QLabel;
class QToolButton;
namespace mini3d::editor {
class SceneViewModel;
class CommitSpinBox;

/** @brief 可折叠的参数视图；只有用户明确打开时才获取字段焦点。 */
class LastOperationPanel final : public QFrame {
    Q_OBJECT
  public:
    explicit LastOperationPanel(SceneViewModel& model, QWidget& viewport);
    /** @brief 展开已有可调整操作，聚焦Z位移或内插厚度，不创建/重做建模操作。 */
    void open();

  protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

  private:
    void refresh();
    void placePanel();
    SceneViewModel& model_;
    QWidget& viewport_;
    QToolButton* toggle_;
    QWidget* body_;
    QWidget* offsetFields_;
    QWidget* insetFields_;
    CommitSpinBox* thickness_;
    bool showingInset_ = false;
    QWidget* bevelFields_;
    CommitSpinBox* bevelWidth_;
    bool showingBevel_ = false;
    QLabel* message_;
    std::array<CommitSpinBox*, 3> fields_{};
};
} // namespace mini3d::editor
