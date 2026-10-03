/*
 * 模块名: QuickFavoritesTests
 * 功能概述: 验证有序收藏、偏好往返、F3 右键与 Q 执行的一致性和上下文拒绝。
 * 对外接口: Catch2 [quick-favorites] 用例
 * 依赖关系: Qt Test、MainWindow、QuickFavorites、OperatorSearchPopup
 * 输入输出: 稳定 ID 与真实控件事件到用户偏好和既有对象历史。
 * 异常与错误: 重复/未知 ID、文档污染、旧上下文执行或结果不一致即失败。
 * 维护说明: INI 使用临时目录；可选截图来自本次真实 Qt 弹窗，不读取系统桌面。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;

namespace {
void toggleContextFavorite(editor::OperatorSearchPopup* popup, QListWidget* results,
                           const QString& expectedLabel) {
    REQUIRE(results->currentItem() != nullptr);
    const auto point = results->visualItemRect(results->currentItem()).center();
    QContextMenuEvent context(QContextMenuEvent::Mouse, point,
                              results->viewport()->mapToGlobal(point));
    QApplication::sendEvent(results->viewport(), &context);
    auto* menu = popup->findChild<QMenu*>(QStringLiteral("OperatorFavoriteContextMenu"));
    REQUIRE(menu != nullptr);
    auto* action = menu->findChild<QAction*>(QStringLiteral("ToggleOperatorFavorite"));
    REQUIRE(action != nullptr);
    REQUIRE(action->text() == expectedLabel);
    action->trigger();
    menu->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
} // namespace

TEST_CASE("Favorites store ordered stable IDs without document or history changes",
          "[quick-favorites]") {
    editor::MainWindow window;
    auto* favorites = window.findChild<editor::QuickFavorites*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    model->newScene();
    const auto before = model->undoStack()->count();
    REQUIRE(favorites->operatorIds().isEmpty());
    REQUIRE(favorites->addOperator(QStringLiteral("object.add_cube")));
    REQUIRE(favorites->addOperator(QStringLiteral("object.add_sphere")));
    REQUIRE_FALSE(favorites->addOperator(QStringLiteral("object.add_cube")));
    REQUIRE_FALSE(favorites->addOperator(QStringLiteral("unknown.operator")));
    REQUIRE(favorites->addOperator(QStringLiteral("mesh.extrude_region")));
    REQUIRE(favorites->removeOperator(QStringLiteral("mesh.extrude_region")));
    REQUIRE(favorites->operatorIds() == QStringList{"object.add_cube", "object.add_sphere"});
    REQUIRE_FALSE(model->isModified());
    REQUIRE(model->undoStack()->count() == before);
    QTemporaryDir temporary;
    REQUIRE(temporary.isValid());
    const auto path = temporary.filePath(QStringLiteral("favorites.ini"));
    QSettings settings(path, QSettings::IniFormat);
    favorites->savePreferences(settings);
    REQUIRE(settings.status() == QSettings::NoError);
    REQUIRE(settings.value(QStringLiteral("workbench/v2/quickFavorites")).toStringList() ==
            favorites->operatorIds());
    editor::MainWindow restored;
    auto* restoredFavorites = restored.findChild<editor::QuickFavorites*>();
    QSettings reader(path, QSettings::IniFormat);
    restoredFavorites->restorePreferences(reader);
    REQUIRE(restoredFavorites->operatorIds() == favorites->operatorIds());
    REQUIRE(restoredFavorites->removeOperator(QStringLiteral("object.add_cube")));
    REQUIRE_FALSE(restoredFavorites->removeOperator(QStringLiteral("object.add_cube")));
    REQUIRE(restoredFavorites->addOperator(QStringLiteral("object.add_cube")));
    REQUIRE(restoredFavorites->operatorIds() ==
            QStringList{"object.add_sphere", "object.add_cube"});
    settings.setValue(
        QStringLiteral("workbench/v2/quickFavorites"),
        QStringList{"object.add_cube", "unknown.id", "object.add_cube", "history.undo"});
    favorites->restorePreferences(settings);
    REQUIRE(favorites->operatorIds() == QStringList{"object.add_cube", "history.undo"});
    REQUIRE_FALSE(model->isModified());
    REQUIRE(model->undoStack()->count() == before);
}

TEST_CASE("F3 context menu adds favorites and Q shares search results and one undo entry",
          "[quick-favorites]") {
    editor::MainWindow window;
    window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Legacy);
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* favorites = window.findChild<editor::QuickFavorites*>();
    auto* popup = window.findChild<editor::OperatorSearchPopup*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* query = window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* results = window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"));
    QSignalSpy added(window.findChild<QAction*>(QStringLiteral("CreateCube")), &QAction::triggered);
    model->newScene();
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_F3);
    REQUIRE(popup->isVisible());
    query->setText(QStringLiteral("Cube"));
    toggleContextFavorite(popup, results, QStringLiteral("加入快捷收藏"));
    REQUIRE(favorites->operatorIds() == QStringList{"object.add_cube"});
    REQUIRE(model->undoStack()->count() == 0);
    REQUIRE_FALSE(model->isModified());
    const auto capturePrefix = qEnvironmentVariable("MINI3D_TEST_OPERATOR_CAPTURE");
    if (!capturePrefix.isEmpty()) {
        REQUIRE(popup->grab().save(capturePrefix + QStringLiteral("-search.png")));
    }
    QTest::keyClick(query, Qt::Key_Return);
    REQUIRE_FALSE(popup->isVisible());
    const auto searched = *model->scene()->find(model->selection()->selectedEntity());
    REQUIRE(model->undoStack()->count() == 1);
    model->newScene();
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_Q);
    REQUIRE(popup->isVisible());
    REQUIRE_FALSE(query->isVisible());
    REQUIRE(QApplication::focusWidget() == results);
    REQUIRE(results->count() == 1);
    REQUIRE(results->currentItem()->data(Qt::UserRole).toString() == "object.add_cube");
    if (!capturePrefix.isEmpty()) {
        REQUIRE(popup->grab().save(capturePrefix + QStringLiteral("-favorites.png")));
    }
    QTest::keyClick(results, Qt::Key_Return);
    REQUIRE_FALSE(popup->isVisible());
    REQUIRE(added.count() == 2);
    const auto* favoriteResult = model->scene()->find(model->selection()->selectedEntity());
    REQUIRE(favoriteResult->primitive == searched.primitive);
    REQUIRE(favoriteResult->name == searched.name);
    REQUIRE(favoriteResult->transform.position == searched.transform.position);
    REQUIRE(model->undoStack()->count() == 1);
    model->undo();
    REQUIRE(model->scene()->roots().empty());
    model->redo();
    REQUIRE(model->scene()->roots().size() == 1);
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_Q);
    toggleContextFavorite(popup, results, QStringLiteral("移出快捷收藏"));
    REQUIRE(favorites->operatorIds().isEmpty());
    REQUIRE(results->count() == 0);
    REQUIRE(window.findChild<QLabel*>(QStringLiteral("OperatorSearchDescription"))
                ->text()
                .contains(QStringLiteral("暂无快捷收藏")));
    QTest::keyClick(results, Qt::Key_Escape);
    REQUIRE_FALSE(popup->isVisible());
    REQUIRE(model->undoStack()->count() == 1);
    window.hide();
}

TEST_CASE("Favorite execution rechecks disabled state and text Q stays text", "[quick-favorites]") {
    editor::MainWindow window;
    window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Legacy);
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* favorites = window.findChild<editor::QuickFavorites*>();
    auto* popup = window.findChild<editor::OperatorSearchPopup*>();
    auto* results = window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"));
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* description = window.findChild<QLabel*>(QStringLiteral("OperatorSearchDescription"));
    model->newScene();
    REQUIRE(favorites->addOperator(QStringLiteral("object.duplicate")));
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_Q);
    REQUIRE(popup->isVisible());
    REQUIRE(description->text().contains(QStringLiteral("先选择")));
    QTest::keyClick(results, Qt::Key_Return);
    REQUIRE(popup->isVisible());
    REQUIRE(model->undoStack()->count() == 0);
    model->newScene();
    QTest::keyClick(results, Qt::Key_Return);
    REQUIRE(description->text().contains(QStringLiteral("文档已切换")));
    REQUIRE(model->undoStack()->count() == 0);
    popup->reject();
    auto* text = window.findChild<QLineEdit*>(QStringLiteral("SceneSearch"));
    text->setFocus();
    QTest::keyClick(text, Qt::Key_Q);
    REQUIRE(text->text().contains(QStringLiteral("q"), Qt::CaseInsensitive));
    REQUIRE_FALSE(popup->isVisible());
    window.hide();
}
