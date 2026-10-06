/*
 * 模块名: AreaMaximizerTests
 * 功能概述: 验证区域最大化恢复、GL 身份、工作区保存保护及快捷键重绑定。
 * 对外接口: Catch2 [area-maximize] 用例
 * 依赖关系: Qt Test、MainWindow、AreaMaximizer、临时 INI
 * 输入输出: 鼠标区域/快捷键/菜单到布局、源上下文和设置断言。
 * 异常与错误: 区域错误、布局丢失、GL 重建、文本被抢或冲突重绑定即失败。
 * 维护说明: 不改用户默认设置；通过真实布局坐标派发 Qt 事件。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/workbench/AreaMaximizer.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "editor/workbench/WorkspaceManager.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
void pointAt(QWidget* region) {
    const auto global = region->mapToGlobal(region->rect().center());
    auto* hit = region->window()->childAt(region->window()->mapFromGlobal(global));
    REQUIRE(hit != nullptr);
    REQUIRE((hit == region || region->isAncestorOf(hit)));
    QMouseEvent event(QEvent::MouseMove, hit->mapFromGlobal(global), global, Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(hit, &event);
}
} // namespace

TEST_CASE("Ctrl Space maximizes the pointer viewport and restores layout without GL replacement",
          "[area-maximize]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* maximizer = window.findChild<editor::AreaMaximizer*>();
    auto* scene = window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* properties = window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    auto* console = window.findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
    auto* action = window.findChild<QAction*>(QStringLiteral("ToggleAreaMaximized"));
    scene->show();
    properties->show();
    model->newScene();
    QTest::qWait(20);
    const auto size = viewport->size();
    const auto sceneRect = scene->geometry();
    const auto propertiesRect = properties->geometry();
    auto* context = viewport->context();
    REQUIRE(context != nullptr);
    QSignalSpy destroyed(context, &QOpenGLContext::aboutToBeDestroyed);
    viewport->setFocus();
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_Space, Qt::ControlModifier);
    QTest::qWait(30);
    REQUIRE(maximizer->maximizedArea() == editor::InputArea::Viewport);
    REQUIRE(action->isChecked());
    REQUIRE(viewport->width() > size.width());
    REQUIRE_FALSE(scene->isVisible());
    REQUIRE_FALSE(properties->isVisible());
    REQUIRE_FALSE(console->isVisible());
    REQUIRE_FALSE(scene->toggleViewAction()->isEnabled());
    REQUIRE(window.centralWidget()->isAncestorOf(viewport));
    REQUIRE(viewport->context() == context);
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier, {}, true);
    QApplication::sendEvent(viewport, &repeat);
    REQUIRE(maximizer->maximizedArea() == editor::InputArea::Viewport);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_MAXIMIZE_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture));
    }
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_Space, Qt::ControlModifier);
    QTest::qWait(30);
    REQUIRE_FALSE(maximizer->isMaximized());
    REQUIRE_FALSE(action->isChecked());
    REQUIRE(scene->isVisible());
    REQUIRE(properties->isVisible());
    REQUIRE(console->isVisible());
    REQUIRE(scene->geometry() == sceneRect);
    REQUIRE(properties->geometry() == propertiesRect);
    REQUIRE(viewport->size() == size);
    REQUIRE(viewport->context() == context);
    REQUIRE(destroyed.isEmpty());
    REQUIRE_FALSE(model->isModified());
    REQUIRE(model->undoStack()->count() == 0);
    window.hide();
}

TEST_CASE("Maximize uses frozen F3 area and covers properties while preserving text priority",
          "[area-maximize]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* maximizer = window.findChild<editor::AreaMaximizer*>();
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* popup = window.findChild<editor::OperatorSearchPopup*>();
    auto* query = window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    auto* scene = window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* properties = window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    scene->show();
    properties->show();
    QTest::qWait(30);
    auto* context = viewport->context();
    REQUIRE(context != nullptr);
    QSignalSpy destroyed(context, &QOpenGLContext::aboutToBeDestroyed);
    viewport->setFocus();
    pointAt(tree->viewport());
    QTest::keyClick(viewport, Qt::Key_F3);
    REQUIRE(popup->isVisible());
    query->setText(QStringLiteral("Toggle Area Maximized"));
    QTest::keyClick(query, Qt::Key_Return);
    QTest::qWait(30);
    REQUIRE(maximizer->maximizedArea() == editor::InputArea::Outliner);
    REQUIRE(scene->isVisible());
    REQUIRE_FALSE(viewport->isVisible());
    REQUIRE(scene->width() > window.width() * 0.9);
    REQUIRE(scene->height() > window.height() * 0.85);
    REQUIRE(registry->executionArea() == editor::InputArea::None);
    REQUIRE(registry->execute(QStringLiteral("workbench.toggle_area_maximized"),
                              registry->captureContext(editor::InputArea::Outliner)));
    QTest::qWait(30);
    REQUIRE_FALSE(maximizer->isMaximized());
    viewport->setFocus();
    pointAt(properties->findChild<QTabBar*>());
    QTest::keyClick(viewport, Qt::Key_Space, Qt::ControlModifier);
    QTest::qWait(30);
    REQUIRE(maximizer->maximizedArea() == editor::InputArea::Properties);
    REQUIRE_FALSE(scene->isVisible());
    REQUIRE_FALSE(viewport->isVisible());
    REQUIRE(properties->width() > window.width() * 0.9);
    REQUIRE(properties->height() > window.height() * 0.85);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_MAXIMIZE_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture + QStringLiteral("-properties.png")));
    }
    maximizer->restore();
    model->createEntity(core::PrimitiveKind::Cube);
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    input->setFocus();
    pointAt(viewport);
    QTest::keyClick(input, Qt::Key_Space, Qt::ControlModifier);
    REQUIRE_FALSE(maximizer->isMaximized());
    REQUIRE(QApplication::focusWidget() == input);
    REQUIRE(viewport->context() == context);
    REQUIRE(destroyed.isEmpty());
    window.hide();
}

TEST_CASE(
    "Temporary maximization is restored before workspace persistence and handles floating docks",
    "[area-maximize]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* maximizer = window.findChild<editor::AreaMaximizer*>();
    auto* workspaces = window.findChild<editor::WorkspaceManager*>();
    auto* scene = window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* console = window.findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
    workspaces->setWorkspace(1);
    scene->show();
    workspaces->setWorkspace(0);
    scene->show();
    QTest::qWait(30);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("layout.ini")), QSettings::IniFormat);
    console->hide();
    REQUIRE_FALSE(maximizer->toggle(editor::InputArea::Console));
    REQUIRE(maximizer->toggle(editor::InputArea::Viewport));
    workspaces->savePreferences(settings);
    REQUIRE_FALSE(maximizer->isMaximized());
    REQUIRE(scene->isVisible());
    REQUIRE_FALSE(console->isVisible());
    REQUIRE(maximizer->toggle(editor::InputArea::Viewport));
    workspaces->setWorkspace(1);
    REQUIRE_FALSE(maximizer->isMaximized());
    REQUIRE(scene->isVisible());
    scene->setFloating(true);
    QTest::qWait(30);
    const auto floatingGeometry = scene->geometry();
    const auto features = scene->features();
    REQUIRE(maximizer->toggle(editor::InputArea::Outliner));
    REQUIRE_FALSE(scene->isFloating());
    maximizer->restore();
    QTest::qWait(30);
    REQUIRE(scene->isFloating());
    const auto restoredGeometry = scene->geometry();
    CAPTURE(floatingGeometry.x(), floatingGeometry.y(), floatingGeometry.width(),
            floatingGeometry.height(), restoredGeometry.x(), restoredGeometry.y(),
            restoredGeometry.width(), restoredGeometry.height());
    REQUIRE(scene->geometry() == floatingGeometry);
    REQUIRE(scene->features() == features);
    console->show();
    REQUIRE(maximizer->toggle(editor::InputArea::Console));
    REQUIRE(console->isVisible());
    REQUIRE_FALSE(scene->isVisible());
    maximizer->restore();
    REQUIRE(console->isVisible());
    window.hide();
}

TEST_CASE("Maximize shortcut rebind rejects conflicts persists and disables the old combination",
          "[area-maximize]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* maximizer = window.findChild<editor::AreaMaximizer*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* action = window.findChild<QAction*>(QStringLiteral("ToggleAreaMaximized"));
    QString reason;
    REQUIRE_FALSE(router->setMaximizeShortcut(QKeySequence(QStringLiteral("Ctrl+S")), &reason));
    REQUIRE_FALSE(reason.isEmpty());
    REQUIRE_FALSE(router->setMaximizeShortcut(QKeySequence(QStringLiteral("G"))));
    REQUIRE_FALSE(router->setMaximizeShortcut(QKeySequence(QStringLiteral("Esc"))));
    REQUIRE_FALSE(router->setMaximizeShortcut(QKeySequence(QStringLiteral("Ctrl+M, Ctrl+M"))));
    REQUIRE(router->setMaximizeShortcut(QKeySequence(QStringLiteral("Ctrl+M"))));
    REQUIRE(action->shortcut() == QKeySequence(QStringLiteral("Ctrl+M")));
    viewport->setFocus();
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_Space, Qt::ControlModifier);
    REQUIRE_FALSE(maximizer->isMaximized());
    QTest::keyClick(viewport, Qt::Key_M, Qt::ControlModifier);
    REQUIRE(maximizer->isMaximized());
    QTest::keyClick(viewport, Qt::Key_M, Qt::ControlModifier);
    REQUIRE_FALSE(maximizer->isMaximized());
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("shortcut.ini")), QSettings::IniFormat);
    router->savePreferences(settings);
    editor::MainWindow restored;
    auto* restoredRouter = restored.findChild<editor::KeymapRouter*>();
    restoredRouter->restorePreferences(settings);
    REQUIRE(restoredRouter->maximizeShortcut() == QKeySequence(QStringLiteral("Ctrl+M")));
    REQUIRE(router->setMaximizeShortcut({}));
    REQUIRE(action->shortcut().isEmpty());
    router->savePreferences(settings);
    restoredRouter->restorePreferences(settings);
    REQUIRE(restoredRouter->maximizeShortcut().isEmpty());
    settings.setValue(QStringLiteral("workbench/v2/maximizeShortcut"), QStringLiteral("Ctrl+S"));
    restoredRouter->restorePreferences(settings);
    REQUIRE(restoredRouter->maximizeShortcut() == QKeySequence(QStringLiteral("Ctrl+Space")));
    action->trigger();
    REQUIRE(maximizer->isMaximized());
    action->trigger();
    REQUIRE_FALSE(maximizer->isMaximized());
    router->setKeymap(editor::EditorKeymap::Legacy);
    REQUIRE(action->shortcut().isEmpty());
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_Space, Qt::ControlModifier);
    REQUIRE_FALSE(maximizer->isMaximized());
    action->trigger();
    REQUIRE(maximizer->isMaximized());
    action->trigger();
    REQUIRE_FALSE(maximizer->isMaximized());
    window.hide();
}

TEST_CASE("Shortcut dialog validates before accepting and cancellation retains the binding",
          "[area-maximize]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* configure = window.findChild<QAction*>(QStringLiteral("ConfigureAreaMaximizeShortcut"));
    bool rejectedConflict = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<QDialog*>(QStringLiteral("AreaShortcutDialog"));
        auto* edit = dialog->findChild<QKeySequenceEdit*>();
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        edit->setKeySequence(QKeySequence(QStringLiteral("Ctrl+S")));
        buttons->button(QDialogButtonBox::Ok)->click();
        rejectedConflict =
            dialog->isVisible() &&
            !dialog->findChild<QLabel*>(QStringLiteral("AreaShortcutReason"))->text().isEmpty();
        edit->setKeySequence(QKeySequence(QStringLiteral("Ctrl+M")));
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    configure->trigger();
    REQUIRE(rejectedConflict);
    REQUIRE(router->maximizeShortcut() == QKeySequence(QStringLiteral("Ctrl+M")));
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<QDialog*>(QStringLiteral("AreaShortcutDialog"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::RestoreDefaults)->click();
        dialog->reject();
    });
    configure->trigger();
    REQUIRE(router->maximizeShortcut() == QKeySequence(QStringLiteral("Ctrl+M")));
    window.hide();
}
