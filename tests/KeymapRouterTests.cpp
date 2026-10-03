/*
 * 模块名: KeymapRouterTests
 * 功能概述: 验证新旧互斥键位、区域上下文、文本/IME/弹窗优先级与单次分发。
 * 对外接口: Catch2 [keymap] 用例
 * 依赖关系: Qt Test、MainWindow、KeymapRouter
 * 输入输出: 主键/小键盘/输入法事件到动作计数及文档不变量。
 * 异常与错误: 旧键泄漏、双次执行、跨窗操作或文本被抢即失败。
 * 维护说明: 新键位为默认，旧版兼容测试显式选择 Legacy；区域坐标来自真实布局。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/workbench/WorkbenchShell.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QTreeView>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;

namespace {
void pointAt(QWidget* widget) {
    // 使用真实布局屏幕坐标注入 Qt 鼠标事件；不假定测试会话可访问 OS 光标。
    const auto local = widget->rect().center();
    const auto global = widget->mapToGlobal(local);
    REQUIRE(widget->window()->childAt(widget->window()->mapFromGlobal(global)) == widget);
    QMouseEvent move(QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &move);
}
} // namespace

TEST_CASE("Keymap distinguishes main digits keypad and conflicting legacy transform bindings",
          "[keymap]") {
    using editor::EditorKeymap;
    using editor::KeymapRouter;
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_1, Qt::NoModifier) ==
            "SelectVertices");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_2, Qt::NoModifier) ==
            "SelectEdges");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_3, Qt::NoModifier) ==
            "SelectFaces");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_1, Qt::KeypadModifier) ==
            "FrontView");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_3, Qt::KeypadModifier) ==
            "RightView");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_7, Qt::KeypadModifier) ==
            "TopView");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_0, Qt::KeypadModifier) ==
            "ToggleCameraPreview");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_R, Qt::NoModifier) ==
            "TransformRotate");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Legacy, Qt::Key_R, Qt::NoModifier) ==
            "ScaleTool");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Legacy, Qt::Key_0, Qt::NoModifier) ==
            "OrbitView");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_W, Qt::NoModifier) ==
            "SelectTool");
    REQUIRE(KeymapRouter::actionForKey(EditorKeymap::Blender, Qt::Key_1, Qt::ControlModifier)
                .isEmpty());
}

TEST_CASE("Legacy actions dispatch once and Blender does not fall back to old keys", "[keymap]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* router = window.findChild<editor::KeymapRouter*>();
    router->setKeymap(editor::EditorKeymap::Legacy);
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* move = window.findChild<QAction*>(QStringLiteral("MoveTool"));
    auto* front = window.findChild<QAction*>(QStringLiteral("FrontView"));
    QSignalSpy moved(move, &QAction::triggered);
    QSignalSpy viewed(front, &QAction::triggered);
    model->createEntity(core::PrimitiveKind::Cube);
    viewport->setFocus();
    const auto history = model->undoStack()->count();
    QTest::keyClick(viewport, Qt::Key_W);
    REQUIRE(moved.count() == 1);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Move);
    REQUIRE_FALSE(viewport->actions().contains(move));
    QTest::keyClick(viewport, Qt::Key_W);
    REQUIRE(moved.count() == 2);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::None);
    router->setKeymap(editor::EditorKeymap::Blender);
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_W);
    QTest::keyClick(viewport, Qt::Key_R);
    REQUIRE(window.findChild<editor::ObjectTransformSession*>()->isActive());
    QTest::keyClick(viewport, Qt::Key_Escape);
    REQUIRE(moved.count() == 2);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::None);
    QTest::keyClick(viewport, Qt::Key_1);
    REQUIRE(viewed.isEmpty());
    QTest::keyClick(viewport, Qt::Key_1, Qt::KeypadModifier);
    REQUIRE(viewed.count() == 1);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(viewport, &leave);
    QTest::keyClick(viewport, Qt::Key_1, Qt::KeypadModifier);
    REQUIRE(viewed.count() == 1);
    pointAt(viewport);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(&window, &deactivate);
    QTest::keyClick(viewport, Qt::Key_1, Qt::KeypadModifier);
    REQUIRE(viewed.count() == 1);
    pointAt(viewport);
    auto* host = window.findChild<editor::WorkbenchShell*>();
    const bool sidebar = host->isSidebarVisible();
    QTest::keyClick(viewport, Qt::Key_N);
    REQUIRE(host->isSidebarVisible() != sidebar);
    REQUIRE(model->undoStack()->count() == history);
    REQUIRE(router->dispatchArea() == editor::InputArea::None);
    router->setKeymap(editor::EditorKeymap::Legacy);
    QTest::keyClick(viewport, Qt::Key_W);
    REQUIRE(moved.count() == 3);
    window.hide();
}

TEST_CASE("Text IME and popup focus take priority and pointer movement never steals focus",
          "[keymap]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    model->createEntity(core::PrimitiveKind::Cube);
    router->setKeymap(editor::EditorKeymap::Blender);
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    input->setFocus();
    input->selectAll();
    pointAt(viewport);
    REQUIRE(QApplication::focusWidget() == input);
    QSignalSpy viewed(window.findChild<QAction*>(QStringLiteral("FrontView")), &QAction::triggered);
    QInputMethodEvent preedit(QStringLiteral("zhong"), {});
    QApplication::sendEvent(input, &preedit);
    QTest::keyClicks(input, "GRSTN123");
    QTest::keyClick(input, Qt::Key_1, Qt::KeypadModifier);
    REQUIRE(viewed.isEmpty());
    REQUIRE(input->text().contains(QStringLiteral("GRSTN123")));
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("中文"));
    QApplication::sendEvent(input, &commit);
    QTest::keyClick(input, Qt::Key_Return);
    REQUIRE(input->text().contains(QStringLiteral("中文")));
    viewport->setFocus();
    // 在非文本区域收到尚未结束的 IME 预编辑时也不能触发导航。
    QApplication::sendEvent(viewport, &preedit);
    QTest::keyClick(viewport, Qt::Key_1, Qt::KeypadModifier);
    REQUIRE(viewed.isEmpty());
    QInputMethodEvent clear;
    QApplication::sendEvent(viewport, &clear);
    QMenu popup(&window);
    popup.addAction(QStringLiteral("菜单项"));
    popup.popup(viewport->mapToGlobal(QPoint(20, 20)));
    QTest::qWait(20);
    REQUIRE(QApplication::activePopupWidget() == &popup);
    QTest::keyClick(viewport, Qt::Key_1, Qt::KeypadModifier);
    REQUIRE(viewed.isEmpty());
    popup.close();
    QDialog modal(&window);
    modal.setModal(true);
    modal.show();
    QTest::qWait(20);
    REQUIRE(QApplication::activeModalWidget() == &modal);
    QTest::keyClick(viewport, Qt::Key_1, Qt::KeypadModifier);
    REQUIRE(viewed.isEmpty());
    modal.close();
    window.hide();
}

TEST_CASE("Blender input follows the pointer area but rejects another window", "[keymap]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    router->setKeymap(editor::EditorKeymap::Blender);
    QSignalSpy viewed(window.findChild<QAction*>(QStringLiteral("FrontView")), &QAction::triggered);
    viewport->setFocus();
    pointAt(tree->viewport());
    QTest::keyClick(viewport, Qt::Key_1, Qt::KeypadModifier);
    REQUIRE(viewed.isEmpty());
    REQUIRE(QApplication::focusWidget() == viewport);
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_1, Qt::KeypadModifier);
    REQUIRE(viewed.count() == 1);
    editor::MainWindow foreign;
    REQUIRE(router->areaForWidget(foreign.findChild<renderer_gl::ViewportWidget*>()) ==
            editor::InputArea::None);
    window.hide();
}
