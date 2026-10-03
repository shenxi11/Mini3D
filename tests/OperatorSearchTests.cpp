/*
 * 模块名: OperatorSearchTests
 * 功能概述: 验证操作注册、搜索排序、冻结上下文以及菜单/键盘/搜索业务一致。
 * 对外接口: Catch2 [operator-search] 用例
 * 依赖关系: Qt Test、MainWindow、OperatorRegistry、OperatorSearchPopup
 * 输入输出: 中英文查询、F3、确认/取消到真实对象及唯一历史栈。
 * 异常与错误: 跨文档执行、目标偷换、未实现功能或重复历史均失败。
 * 维护说明: 操作对象和检查历史均通过已有 SceneViewModel；不写用户偏好。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;

TEST_CASE("Registry searches Chinese English and current shortcuts with stable IDs",
          "[operator-search]") {
    editor::MainWindow window;
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    const auto context = registry->captureContext(editor::InputArea::Viewport);
    const auto chinese = registry->search(QStringLiteral("复制"), context);
    const auto english = registry->search(QStringLiteral("duplicate"), context);
    REQUIRE(chinese.size() == 1);
    REQUIRE(english.size() == 1);
    REQUIRE(chinese.front().id == english.front().id);
    REQUIRE(chinese.front().id == "object.duplicate");
    REQUIRE_FALSE(chinese.front().disabledReason.isEmpty());
    const auto shortcuts = registry->search(QStringLiteral("Ctrl+D"), context);
    REQUIRE(shortcuts.size() == 1);
    REQUIRE(shortcuts.front().id == chinese.front().id);
    const auto extrusion = registry->search(QStringLiteral("extrude"), context);
    REQUIRE(extrusion.size() == 1);
    REQUIRE(extrusion.front().id == QStringLiteral("mesh.extrude_region"));
    REQUIRE_FALSE(extrusion.front().disabledReason.isEmpty());
    QSet<QString> ids;
    for (const auto& result : registry->search({}, context)) {
        REQUIRE_FALSE(ids.contains(result.id));
        ids.insert(result.id);
        REQUIRE(registry->descriptor(result.id)->action != nullptr);
    }
    REQUIRE(ids.size() == 85);
    for (const auto* id : {"view.shading_pie", "view.view_pie", "view.shading_material",
                           "view.shading_solid", "view.shading_wireframe", "history.repeat_last"}) {
        REQUIRE(ids.contains(QString::fromLatin1(id)));
    }
    model->createEntity(core::PrimitiveKind::Cube);
    const auto selected = registry->captureContext(editor::InputArea::Viewport);
    REQUIRE(registry->disabledReason(chinese.front().id, selected).isEmpty());
    REQUIRE(registry->descriptor(chinese.front().id)->undoable);
    REQUIRE_FALSE(registry->descriptor(chinese.front().id)->reopenable);
    const auto treeContext = registry->captureContext(editor::InputArea::Outliner);
    REQUIRE(registry->disabledReason(QStringLiteral("view.front"), treeContext)
                .contains(QStringLiteral("视口")));
    REQUIRE(registry
                ->disabledReason(QStringLiteral("view.front"),
                                 registry->captureContext(editor::InputArea::Outliner,
                                                          editor::EditorKeymap::Legacy))
                .isEmpty());
    const auto cube = model->selection()->selectedEntity();
    REQUIRE(model->setVisible(cube, false));
    REQUIRE(registry->disabledReason(QStringLiteral("view.focus_selected"), selected)
                .contains(QStringLiteral("可见几何")));
    const auto camera = model->createCamera();
    REQUIRE(model->setPreviewCamera(camera));
    REQUIRE(registry
                ->disabledReason(QStringLiteral("view.front"),
                                 registry->captureContext(editor::InputArea::Viewport))
                .contains(QStringLiteral("相机预览")));
}

TEST_CASE("Registry refuses reset documents changed selections and another window",
          "[operator-search]") {
    editor::MainWindow window;
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    model->createEntity(core::PrimitiveKind::Cube);
    auto context = registry->captureContext(editor::InputArea::Viewport);
    model->createEntity(core::PrimitiveKind::Sphere);
    const auto count = model->undoStack()->count();
    REQUIRE_FALSE(registry->execute(QStringLiteral("object.delete"), context));
    REQUIRE(model->undoStack()->count() == count);
    REQUIRE(registry->disabledReason(QStringLiteral("object.delete"), context)
                .contains(QStringLiteral("选择已变化")));
    context = registry->captureContext(editor::InputArea::Viewport);
    const auto* oldSceneAddress = model->scene().get();
    model->newScene();
    REQUIRE(model->scene().get() == oldSceneAddress);
    REQUIRE_FALSE(registry->execute(QStringLiteral("object.add_cube"), context));
    REQUIRE(model->undoStack()->count() == 0);
    REQUIRE(registry->disabledReason(QStringLiteral("object.add_cube"), context)
                .contains(QStringLiteral("文档已切换")));
    editor::MainWindow foreign;
    auto* foreignRegistry = foreign.findChild<editor::OperatorRegistry*>();
    REQUIRE_FALSE(registry->execute(QStringLiteral("object.add_cube"),
                                    foreignRegistry->captureContext(editor::InputArea::Viewport)));
    REQUIRE_FALSE(registry->execute(QStringLiteral("mesh.extrude_region"),
                                    registry->captureContext(editor::InputArea::Viewport)));
    REQUIRE(model->undoStack()->count() == 0);
}

TEST_CASE("F3 keeps caller context and matches menu operations with one history entry",
          "[operator-search]") {
    editor::MainWindow window;
    window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Legacy);
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* popup = window.findChild<editor::OperatorSearchPopup*>();
    auto* query = window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* results = window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* cubeAction = window.findChild<QAction*>(QStringLiteral("CreateCube"));
    QSignalSpy triggered(cubeAction, &QAction::triggered);
    QSignalSpy executed(registry, &editor::OperatorRegistry::executed);
    model->newScene();
    cubeAction->trigger();
    const auto menuNode = *model->scene()->find(model->selection()->selectedEntity());
    REQUIRE(model->undoStack()->count() == 1);
    model->newScene();
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_F3);
    REQUIRE(popup->isVisible());
    REQUIRE(QApplication::focusWidget() == query);
    QTest::keyClicks(query, "cube");
    REQUIRE(results->count() == 1);
    QTest::keyClick(query, Qt::Key_Return);
    REQUIRE_FALSE(popup->isVisible());
    REQUIRE(QApplication::focusWidget() == viewport);
    REQUIRE(triggered.count() == 2);
    REQUIRE(executed.count() == 1);
    REQUIRE(model->undoStack()->count() == 1);
    const auto* searchedNode = model->scene()->find(model->selection()->selectedEntity());
    REQUIRE(searchedNode->primitive == menuNode.primitive);
    REQUIRE(searchedNode->transform.position == menuNode.transform.position);
    REQUIRE(searchedNode->name == menuNode.name);
    model->undo();
    REQUIRE(model->scene()->roots().empty());
    model->redo();
    REQUIRE(model->scene()->roots().size() == 1);
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_F3);
    query->setText(QStringLiteral("球体"));
    QTest::keyClick(query, Qt::Key_Escape);
    REQUIRE_FALSE(popup->isVisible());
    REQUIRE(model->undoStack()->count() == 1);
    REQUIRE(model->scene()->roots().size() == 1);
    window.hide();
}

TEST_CASE("Search shows disabled reasons and cannot execute a stale popup", "[operator-search]") {
    editor::MainWindow window;
    window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Legacy);
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* popup = window.findChild<editor::OperatorSearchPopup*>();
    auto* query = window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* description = window.findChild<QLabel*>(QStringLiteral("OperatorSearchDescription"));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_F3);
    REQUIRE(popup->isVisible());
    REQUIRE(description->text().contains(QStringLiteral("调用区域：场景树")));
    QTest::keyClick(query, Qt::Key_Down);
    REQUIRE(window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"))->currentRow() ==
            1);
    QTest::keyClick(query, Qt::Key_Up);
    REQUIRE(window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"))->currentRow() ==
            0);
    QInputMethodEvent preedit(QStringLiteral("zhong"), {});
    QApplication::sendEvent(query, &preedit);
    QTest::keyClick(query, Qt::Key_Down);
    REQUIRE(window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"))->currentRow() ==
            0);
    QInputMethodEvent clear;
    QApplication::sendEvent(query, &clear);
    popup->reject();
    REQUIRE(QApplication::focusWidget() == tree);
    // 新配置区域限制通过冻结上下文直接验收，避免测试的系统光标权限依赖。
    router->setKeymap(editor::EditorKeymap::Blender);
    model->selection()->setSelectedEntity(core::kInvalidEntity);
    REQUIRE(model->selection()->selectedEntity() == core::kInvalidEntity);
    popup->openForContext(registry->captureContext(editor::InputArea::Outliner));
    query->setText(QStringLiteral("Front"));
    REQUIRE(description->text().contains(QStringLiteral("调用区域：场景树")));
    REQUIRE(description->text().contains(QStringLiteral("仅适用于 3D 视口")));
    QTest::keyClick(query, Qt::Key_Return);
    REQUIRE(popup->isVisible());
    query->setText(QStringLiteral("duplicate"));
    REQUIRE(description->text().contains(QStringLiteral("先选择")));
    query->setText(QStringLiteral("Cube"));
    model->newScene();
    QTest::keyClick(query, Qt::Key_Return);
    REQUIRE(popup->isVisible());
    REQUIRE(description->text().contains(QStringLiteral("文档已切换")));
    REQUIRE(model->undoStack()->count() == 0);
    REQUIRE(model->scene()->roots().empty());
    popup->reject();
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("SceneSearch"));
    REQUIRE(input != nullptr);
    input->setFocus();
    QTest::keyClick(input, Qt::Key_F3);
    REQUIRE_FALSE(popup->isVisible());
    window.hide();
}

TEST_CASE("Menu toolbar search and keymap share the registered tool action", "[operator-search]") {
    editor::MainWindow window;
    window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Legacy);
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* move = window.findChild<QAction*>(QStringLiteral("MoveTool"));
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("ViewportToolbar"));
    auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(move));
    REQUIRE(button != nullptr);
    QSignalSpy triggered(move, &QAction::triggered);
    move->trigger();
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Move);
    button->click();
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::None);
    REQUIRE(registry->execute(QStringLiteral("tool.move"),
                              registry->captureContext(editor::InputArea::Viewport)));
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Move);
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_W);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::None);
    REQUIRE(triggered.count() == 4);
    window.hide();
}
