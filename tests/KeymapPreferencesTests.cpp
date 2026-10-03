/*
 * 模块名: KeymapPreferencesTests
 * 功能概述: 验证新默认、可见键位选择、独立偏好和模态切换取消。
 * 对外接口: Catch2 [keymap-preferences] 用例
 * 依赖关系: Qt Test、临时 INI、MainWindow/KeymapRouter
 * 输入输出: 菜单和按键到互斥动作、工具设置文字与用户偏好。
 * 异常与错误: 键位串用、状态不一致、工程被偏好污染或未确认变换残留即失败。
 * 维护说明: 临时目录保存测试偏好，不改当前用户的默认 QSettings 路径。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
void pointAt(renderer_gl::ViewportWidget* viewport) {
    const auto position = viewport->rect().center();
    const auto global = viewport->mapToGlobal(position);
    REQUIRE(viewport->window()->childAt(viewport->window()->mapFromGlobal(global)) == viewport);
    QMouseEvent move(QEvent::MouseMove, position, global, Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
}
} // namespace

TEST_CASE("Keymap preferences default to Blender and persist only an independent stable value",
          "[keymap-preferences]") {
    editor::MainWindow window;
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* label = window.findChild<QLabel*>(QStringLiteral("ToolSettings"));
    model->newScene();
    REQUIRE(router->keymap() == editor::EditorKeymap::Blender);
    REQUIRE(label->text().contains(QStringLiteral("Blender 风格")));
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("keymap.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("unrelated"), 17);
    auto* legacy = window.findChild<QAction*>(QStringLiteral("LegacyKeymap"));
    auto* blender = window.findChild<QAction*>(QStringLiteral("BlenderKeymap"));
    legacy->trigger();
    REQUIRE(router->keymap() == editor::EditorKeymap::Legacy);
    REQUIRE(legacy->isChecked());
    REQUIRE_FALSE(blender->isChecked());
    REQUIRE(label->text().contains(QStringLiteral("Legacy Mini3D")));
    router->savePreferences(settings);
    settings.sync();
    REQUIRE(settings.status() == QSettings::NoError);
    REQUIRE(settings.value(QStringLiteral("workbench/v2/keymap")) == "legacy");
    editor::MainWindow restored;
    auto* restoredRouter = restored.findChild<editor::KeymapRouter*>();
    QSettings reader(settings.fileName(), QSettings::IniFormat);
    restoredRouter->restorePreferences(reader);
    REQUIRE(restoredRouter->keymap() == editor::EditorKeymap::Legacy);
    REQUIRE(restored.findChild<QAction*>(QStringLiteral("LegacyKeymap"))->isChecked());
    settings.setValue(QStringLiteral("workbench/v2/keymap"), QStringLiteral("unknown-profile"));
    router->restorePreferences(settings);
    REQUIRE(router->keymap() == editor::EditorKeymap::Blender);
    REQUIRE(blender->isChecked());
    REQUIRE(settings.value(QStringLiteral("workbench/v2/keymap")) == "unknown-profile");
    REQUIRE(settings.value(QStringLiteral("unrelated")) == 17);
    REQUIRE(model->undoStack()->count() == 0);
    REQUIRE_FALSE(model->isModified());
}

TEST_CASE("Default W selects and R rotates while explicit Legacy retains R scale",
          "[keymap-preferences]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* modal = window.findChild<editor::ObjectTransformSession*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* label = window.findChild<QLabel*>(QStringLiteral("ToolSettings"));
    auto* scale = window.findChild<QAction*>(QStringLiteral("ScaleTool"));
    model->newScene();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    const auto history = model->undoStack()->count();
    viewport->setFocus();
    pointAt(viewport);
    REQUIRE(router->keymap() == editor::EditorKeymap::Blender);
    window.findChild<QAction*>(QStringLiteral("MoveTool"))->trigger();
    QTest::keyClick(viewport, Qt::Key_W);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::None);
    REQUIRE(label->text().contains(QStringLiteral("当前工具：选择")));
    QSignalSpy scaled(scale, &QAction::triggered);
    QTest::keyClick(viewport, Qt::Key_R);
    REQUIRE(modal->isActive());
    REQUIRE(scaled.isEmpty());
    QTest::keyClicks(viewport, "y45");
    REQUIRE(model->scene()->find(id)->transform.rotation != glm::quat(1, 0, 0, 0));
    window.findChild<QAction*>(QStringLiteral("LegacyKeymap"))->trigger();
    REQUIRE_FALSE(modal->isActive());
    REQUIRE(model->scene()->find(id)->transform.rotation == glm::quat(1, 0, 0, 0));
    REQUIRE(model->undoStack()->count() == history);
    REQUIRE(window.findChild<QAction*>(QStringLiteral("TransformRotate"))->shortcuts().isEmpty());
    QTest::keyClick(viewport, Qt::Key_R);
    REQUIRE(scaled.count() == 1);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Scale);
    REQUIRE(label->text().contains(QStringLiteral("当前工具：缩放")));
    REQUIRE(label->text().contains(QStringLiteral("Legacy Mini3D")));
    window.findChild<QAction*>(QStringLiteral("BlenderKeymap"))->trigger();
    REQUIRE(scale->shortcuts().isEmpty());
    REQUIRE(window.findChild<QAction*>(QStringLiteral("TransformRotate"))->shortcut() ==
            QKeySequence(Qt::Key_R));
    QTest::keyClick(viewport, Qt::Key_W);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::None);
    REQUIRE(label->text().contains(QStringLiteral("Blender 风格")));
    const auto capture = qEnvironmentVariable("MINI3D_TEST_KEYMAP_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture));
    }
    window.hide();
}
