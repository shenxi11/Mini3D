/*
 * 模块名: WorkbenchShell
 * 功能概述: 组织真实视口的标题栏、工具设置、工具条与独立侧栏，不持有建模数据。
 * 对外接口: WorkbenchShell
 * 依赖关系: Qt Widgets、SceneViewModel、既有 ViewportWidget
 * 输入输出: 复用动作与只读选择状态，输出区域布局与显隐意图。
 * 异常与错误: 业务错误仍由 ViewModel 报告；不注册未实现的操作。
 * 维护说明: 视口仅在首次装配时加入布局，不在切工作区或显隐面板时重新挂载。
 */
#pragma once

#include <QWidget>
#include <array>

class QAction;
class QLabel;
class QToolBar;
class QMainWindow;
class QTabBar;

namespace mini3d::renderer_gl {
class ViewportWidget;
}

namespace mini3d::editor {
class SceneViewModel;
class KeymapRouter;
class CommitSpinBox;

/** @brief 视口区域宿主；面板占据独立布局矩形，鼠标坐标仍由真实 GL Widget 处理。 */
class WorkbenchShell final : public QWidget {
    Q_OBJECT
  public:
    explicit WorkbenchShell(renderer_gl::ViewportWidget* viewport, SceneViewModel& model,
                            QWidget* parent = nullptr);
    /** @brief 装配既有菜单动作；必须在 MainWindow 完成菜单创建后调用一次。 */
    void bindActions(QMainWindow& window, KeymapRouter& router);
    /** @brief 只改变视图布局，不修改文档或选择。 */
    void setToolbarVisible(bool visible);
    void setSidebarVisible(bool visible);
    [[nodiscard]] bool isToolbarVisible() const;
    [[nodiscard]] bool isSidebarVisible() const;
    /** @brief 工作区标签只表达布局选择，不表达 Object/Edit 模式。 */
    [[nodiscard]] QTabBar* workspaceTabs() const;

  signals:
    void toolbarVisibilityChanged(bool visible);
    void sidebarVisibilityChanged(bool visible);

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    void bindEditActions(QMainWindow& window);
    void bindCursorActions(QMainWindow& window);
    void bindPivotActions(QMainWindow& window);
    void refreshCursor();
    void refreshSelection();
    SceneViewModel& model_;
    renderer_gl::ViewportWidget* viewport_;
    QWidget* header_;
    QToolBar* toolbar_;
    QWidget* sidebar_;
    QLabel* selectionLabel_;
    QLabel* transformLabel_;
    QLabel* toolLabel_;
    QLabel* modeLabel_;
    QLabel* selectionHint_;
    QTabBar* workspaceTabs_;
    std::array<CommitSpinBox*, 3> cursorFields_{};
    QWidget* cursorPanel_;
};
} // namespace mini3d::editor
