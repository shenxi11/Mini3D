/*
 * 模块名: WorkbenchTests
 * 功能概述: 验证工作台区域布局、真实视口生命周期和菜单/工具条业务等价。
 * 对外接口: Catch2 [workbench] 用例
 * 依赖关系: Qt Test、MainWindow、ViewportWidget
 * 输入输出: 真实窗口输入到布局、选择、历史和帧缓冲断言。
 * 异常与错误: 面板穿透、Context 重建或旧业务退化即测试失败。
 * 维护说明: 截图来自当前 C++ 窗口，不读取桌面或使用 HTML 原型。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/workbench/WorkbenchShell.h"
#include "editor/workbench/WorkspaceManager.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QOpenGLContext>
#include <QSettings>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;

TEST_CASE("Workbench regions preserve GL identity and keep sidebar clicks outside picking",
          "[workbench]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* scene = window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    auto* console = window.findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
    REQUIRE(host != nullptr);
    REQUIRE(viewport != nullptr);
    REQUIRE(window.dockWidgetArea(scene) == Qt::RightDockWidgetArea);
    REQUIRE(window.dockWidgetArea(inspector) == Qt::RightDockWidgetArea);
    REQUIRE(scene->geometry().bottom() < inspector->geometry().top());
    REQUIRE(console->isHidden());
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    auto* context = viewport->context();
    REQUIRE(context != nullptr);
    QSignalSpy destroyed(context, &QOpenGLContext::aboutToBeDestroyed);
    QSignalSpy picked(viewport, &renderer_gl::ViewportWidget::pickRequested);
    const auto width = viewport->width();
    const auto modified = model->isModified();
    const auto history = model->undoStack()->count();
    window.findChild<QAction*>(QStringLiteral("ToggleViewportSidebar"))->trigger();
    QTest::qWait(40);
    REQUIRE(host->isSidebarVisible());
    REQUIRE(viewport->width() < width);
    auto* sidebar = window.findChild<QWidget*>(QStringLiteral("ViewportSidebar"));
    QTest::mouseClick(sidebar, Qt::LeftButton, Qt::NoModifier, sidebar->rect().center());
    REQUIRE(picked.isEmpty());
    REQUIRE_FALSE(viewport->geometry().intersects(sidebar->geometry()));
    REQUIRE(viewport->grabFramebuffer().size() ==
            QSize(qRound(viewport->width() * viewport->devicePixelRatioF()),
                  qRound(viewport->height() * viewport->devicePixelRatioF())));
    window.findChild<QAction*>(QStringLiteral("ToggleViewportSidebar"))->trigger();
    window.findChild<QAction*>(QStringLiteral("ToggleViewportToolbar"))->trigger();
    QTest::qWait(40);
    REQUIRE_FALSE(host->isSidebarVisible());
    REQUIRE_FALSE(host->isToolbarVisible());
    REQUIRE(viewport->width() > width);
    REQUIRE(viewport->context() == context);
    REQUIRE(destroyed.isEmpty());
    REQUIRE(model->isModified() == modified);
    REQUIRE(model->undoStack()->count() == history);
    window.hide();
}

TEST_CASE("Large editable source has a persistent scale hint without changing history",
          "[workbench][performance-scale]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* status = window.findChild<QLabel*>(QStringLiteral("EditableScaleStatus"));
    REQUIRE(status);
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->setEditMode(true));
    const auto original = model->displayedEditableMesh(cube)->source;
    REQUIRE(status->text().isEmpty());
    auto large = original;
    // 该夹具仅核对源点计数提示；真实性能另用完整四边格探针测量。
    for (std::uint64_t id = 9; id <= 10001; ++id)
        large.vertices.push_back({id, {2, 2, 2}});
    REQUIRE(model->replaceEditableMesh(cube, large));
    const auto history = model->undoStack()->index();
    const auto dirty = model->isModified();
    host->setSidebarVisible(false);
    REQUIRE(status->isVisible());
    REQUIRE(status->text().contains(QStringLiteral("10001")));
    REQUIRE(status->text().contains(QStringLiteral("可能延迟")));
    REQUIRE(model->setEditMode(false));
    REQUIRE(status->text().isEmpty());
    REQUIRE(model->setEditMode(true));
    REQUIRE(status->text().contains(QStringLiteral("10001")));
    REQUIRE(model->undoStack()->index() == history);
    REQUIRE(model->isModified() == dirty);
    REQUIRE(model->replaceEditableMesh(cube, original));
    REQUIRE(status->text().isEmpty());
    window.hide();
}

TEST_CASE("Workspaces restore independent preferences without changing document or GL context",
          "[workbench][workspace]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    QSettings preferences(directory.filePath(QStringLiteral("workspace.ini")),
                          QSettings::IniFormat);
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* manager = window.findChild<editor::WorkspaceManager*>();
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* pages = window.findChild<QTabWidget*>(QStringLiteral("PropertyPages"));
    const auto history = model->undoStack()->count();
    const auto dirty = model->isModified();
    const auto selection = model->selection()->selectedEntity();
    auto* context = viewport->context();
    host->setToolbarVisible(false);
    pages->setCurrentIndex(1);
    window.findChild<QToolButton*>(QStringLiteral("DataPropertiesGroup"))->setChecked(false);
    window.findChild<QAction*>(QStringLiteral("LocalTransformSpace"))->trigger();
    host->workspaceTabs()->setCurrentIndex(1);
    QTest::qWait(30);
    REQUIRE(manager->currentWorkspace() == 1);
    REQUIRE(host->isToolbarVisible());
    REQUIRE(host->isSidebarVisible());
    REQUIRE(pages->currentIndex() == 0);
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::World);
    manager->setWorkspace(0);
    REQUIRE_FALSE(host->isToolbarVisible());
    REQUIRE(pages->currentIndex() == 1);
    REQUIRE_FALSE(
        window.findChild<QToolButton*>(QStringLiteral("DataPropertiesGroup"))->isChecked());
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::Local);
    manager->savePreferences(preferences);
    REQUIRE(preferences.status() == QSettings::NoError);
    manager->setWorkspace(2);
    REQUIRE(pages->currentIndex() == 1);
    manager->restorePreferences(preferences);
    REQUIRE(manager->currentWorkspace() == 0);
    REQUIRE_FALSE(host->isToolbarVisible());
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::Local);
    REQUIRE(model->isModified() == dirty);
    REQUIRE(model->undoStack()->count() == history);
    REQUIRE(model->selection()->selectedEntity() == selection);
    REQUIRE(window.findChild<QLabel*>(QStringLiteral("ModeLabel"))->text() ==
            QStringLiteral("对象模式"));
    REQUIRE(viewport->context() == context);
    // 新窗口从独立偏好存储重建，而不是只验证原窗口内存还保留着状态。
    editor::MainWindow reopened;
    reopened.show();
    REQUIRE(QTest::qWaitForWindowExposed(&reopened));
    auto* restored = reopened.findChild<editor::WorkspaceManager*>();
    restored->restorePreferences(preferences);
    REQUIRE_FALSE(reopened.findChild<editor::WorkbenchShell*>()->isToolbarVisible());
    REQUIRE(reopened.findChild<QTabWidget*>(QStringLiteral("PropertyPages"))->currentIndex() == 1);
    restored->setWorkspace(1);
    REQUIRE(reopened.findChild<editor::WorkbenchShell*>()->isSidebarVisible());
    REQUIRE(reopened.findChild<editor::WorkbenchShell*>()->isToolbarVisible());
    window.hide();
    reopened.hide();
}

TEST_CASE("Workbench tools reuse actions and selection data with a usable minimum viewport",
          "[workbench]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("ViewportToolbar"));
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->renameEntity(id, QStringLiteral("工作台对象")));
    REQUIRE(window.findChild<QLabel*>(QStringLiteral("SidebarSelection"))->text() ==
            QStringLiteral("工作台对象"));
    const auto count = model->undoStack()->count();
    auto* move = window.findChild<QAction*>(QStringLiteral("MoveTool"));
    REQUIRE(toolbar->actions().contains(move));
    auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(move));
    REQUIRE(button != nullptr);
    QTest::mouseClick(button, Qt::LeftButton);
    REQUIRE(move->isChecked());
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Move);
    window.findChild<QAction*>(QStringLiteral("SelectTool"))->trigger();
    REQUIRE_FALSE(move->isChecked());
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::None);
    REQUIRE(model->undoStack()->count() == count);
    host->setSidebarVisible(true);
    window.resize(960, 640);
    QTest::qWait(50);
    INFO("window=" << window.width() << ", host=" << host->width()
                   << ", viewport=" << viewport->width());
    REQUIRE(viewport->width() >= 250);
    REQUIRE(viewport->height() >= 300);
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    const auto capture = qEnvironmentVariable("MINI3D_TEST_WORKBENCH_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture));
    }
    window.hide();
}
