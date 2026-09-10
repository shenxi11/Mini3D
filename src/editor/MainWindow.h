/*
 * 模块名: MainWindow
 * 功能概述: 组装 Mini3D Studio 的顶层窗口、OpenGL Viewport 与编辑器面板。
 * 对外接口: mini3d::editor::MainWindow
 * 依赖关系: Qt 6 Widgets、mini3d_renderer_gl
 * 输入输出: 接收可选父窗口，输出可停靠的编辑器界面。
 * 异常与错误: 不处理业务错误；Qt 负责子控件创建与生命周期。
 * 维护说明: 当前只承载 View 结构，不放置 Scene、Renderer 或资源业务逻辑。
 */

#pragma once

#include <QMainWindow>

class QDockWidget;
class QWidget;
class QTreeView;

namespace mini3d::editor {
class SceneViewModel;
class SceneTreeModel;

/**
 * @brief Mini3D Studio 的顶层编辑器窗口。
 *
 * 负责组织中央 OpenGL Viewport 和 Scene、Inspector、Console Dock，不持有领域状态。
 */
class MainWindow final : public QMainWindow {
  public:
    /**
     * @brief 构造编辑器窗口并创建当前里程碑的全部子控件。
     * @param parent 可选的 Qt 父窗口；为空时窗口作为应用顶层窗口。
     *
     * 子控件由 Qt 父子对象机制接管，本函数不执行文件或网络 I/O。
     */
    explicit MainWindow(QWidget* parent = nullptr);

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    [[nodiscard]] QWidget* createViewport();
    [[nodiscard]] QDockWidget* createSceneDock();
    [[nodiscard]] QDockWidget* createInspectorDock();
    [[nodiscard]] QDockWidget* createConsoleDock();
    void createMenus(QDockWidget* sceneDock, QDockWidget* inspectorDock, QDockWidget* consoleDock);
    void synchronizeTreeSelection();
    void showSceneContextMenu(const QPoint& position);
    void findNextEntity(const QString& text);
    bool confirmDiscardChanges();
    bool saveScene(bool saveAs);
    void refreshDocumentTitle();

    SceneViewModel* viewModel_ = nullptr;
    SceneTreeModel* treeModel_ = nullptr;
    QTreeView* tree_ = nullptr;
};

} // namespace mini3d::editor
