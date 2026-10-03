/*
 * 模块名: ViewportNavigationTests
 * 功能概述: 验证全部框景与实体相机预览的输入、无副作用和返回规则。
 * 对外接口: Catch2 [viewport-navigation] 用例
 * 依赖关系: Qt Test、MainWindow、现有相机和 Registry
 * 输入输出: Home/小键盘0/菜单到真实相机矩阵、选择、历史和拒绝原因。
 * 异常与错误: 隐藏对象参与框景、预览丢失编辑视角或文本键被抢即失败。
 * 维护说明: 不读取系统光标；测试不修改用户偏好或场景格式。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/OperatorRegistry.h"
#include "renderer_gl/RayCaster.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
void pointAt(renderer_gl::ViewportWidget* viewport) {
    const auto local = viewport->rect().center();
    const auto global = viewport->mapToGlobal(local);
    REQUIRE(viewport->window()->childAt(viewport->window()->mapFromGlobal(global)) == viewport);
    QMouseEvent event(QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &event);
}
} // namespace

TEST_CASE("Home frames all visible objects without selection or object history",
          "[viewport-navigation]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    model->newScene();
    const auto left = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->setTransformComponent(left, 0, 0, -4));
    const auto right =
        model->importGltf(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/BoxTextured.glb"));
    REQUIRE(right != 0);
    REQUIRE(model->setTransformComponent(right, 0, 0, 6));
    const auto hidden = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->setTransformComponent(hidden, 0, 0, 1000));
    REQUIRE(model->setVisible(hidden, false));
    model->selection()->setSelectedEntity(0);
    const auto count = model->undoStack()->count();
    viewport->setFocus();
    pointAt(viewport);
    const auto before = *viewport->editorCameraSnapshot();
    const auto bounds = renderer_gl::RayCaster::sceneBounds(*model->scene(), *model->assets());
    QTest::keyClick(viewport, Qt::Key_Home);
    const auto after = *viewport->editorCameraSnapshot();
    REQUIRE(after.viewMatrix() != before.viewMatrix());
    REQUIRE(glm::distance(after.target(), (bounds.minimum + bounds.maximum) * 0.5F) < 1.0e-5F);
    for (int axis = 0; axis < 3; ++axis) {
        REQUIRE(glm::distance(glm::vec3(after.viewMatrix()[axis]),
                              glm::vec3(before.viewMatrix()[axis])) < 1.0e-5F);
    }
    REQUIRE(after.distance() < 100);
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 position{corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                                 corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                                 corner & 4 ? bounds.maximum.z : bounds.minimum.z};
        const auto clip = after.viewProjectionMatrix() * glm::vec4(position, 1);
        REQUIRE(clip.w > 0);
        REQUIRE(std::abs(clip.x / clip.w) <= 1);
        REQUIRE(std::abs(clip.y / clip.w) <= 1);
        REQUIRE(std::abs(clip.z / clip.w) <= 1);
    }
    REQUIRE(model->selection()->selectedEntity() == 0);
    REQUIRE(model->undoStack()->count() == count);
    model->selection()->setSelectedEntity(left);
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    input->setFocus();
    pointAt(viewport);
    QTest::keyClick(input, Qt::Key_Home);
    REQUIRE(viewport->editorCameraSnapshot()->viewMatrix() == after.viewMatrix());
    model->newScene();
    viewport->setFocus();
    pointAt(viewport);
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    QSignalSpy failure(registry, &editor::OperatorRegistry::executionFailed);
    const auto emptyCamera = viewport->editorCameraSnapshot()->viewMatrix();
    QTest::keyClick(viewport, Qt::Key_Home);
    REQUIRE(failure.count() == 1);
    REQUIRE(failure.front().front().toString().contains(QStringLiteral("没有可见对象")));
    REQUIRE(viewport->editorCameraSnapshot()->viewMatrix() == emptyCamera);
    window.hide();
}

TEST_CASE("Keypad zero uses real cameras and returns to the exact editor projection",
          "[viewport-navigation]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* action = window.findChild<QAction*>(QStringLiteral("ToggleCameraPreview"));
    QSignalSpy failed(registry, &editor::OperatorRegistry::executionFailed);
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    viewport->setFocus();
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_0, Qt::KeypadModifier);
    REQUIRE(failed.count() == 1);
    REQUIRE(failed.front().front().toString().contains(QStringLiteral("没有可见相机")));
    REQUIRE_FALSE(action->isChecked());
    const auto first = model->createCamera();
    const auto second = model->createCamera();
    REQUIRE(first != second);
    const auto history = model->undoStack()->count();
    viewport->setCameraView(renderer_gl::EditorView::Top);
    viewport->setOrthographic(true);
    const auto editorCamera = *viewport->editorCameraSnapshot();
    QTest::keyClick(viewport, Qt::Key_0);
    REQUIRE(model->previewCamera() == 0);
    QTest::keyClick(viewport, Qt::Key_0, Qt::KeypadModifier);
    REQUIRE(model->previewCamera() == second);
    REQUIRE(action->isChecked());
    REQUIRE(model->selection()->selectedEntity() == second);
    QWheelEvent wheel(viewport->rect().center(), viewport->mapToGlobal(viewport->rect().center()),
                      {}, {0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(viewport, &wheel);
    QTest::keyClick(viewport, Qt::Key_Home);
    REQUIRE(viewport->editorCameraSnapshot()->viewProjectionMatrix() ==
            editorCamera.viewProjectionMatrix());
    REQUIRE(failed.count() == 2);
    QTest::keyClick(viewport, Qt::Key_0, Qt::KeypadModifier);
    REQUIRE(model->previewCamera() == 0);
    REQUIRE_FALSE(action->isChecked());
    REQUIRE(viewport->isOrthographic());
    REQUIRE(viewport->editorCameraSnapshot()->view() == renderer_gl::EditorView::Top);
    REQUIRE(viewport->editorCameraSnapshot()->viewProjectionMatrix() ==
            editorCamera.viewProjectionMatrix());
    model->selection()->setSelectedEntity(cube);
    QTest::keyClick(viewport, Qt::Key_0, Qt::KeypadModifier);
    REQUIRE(model->previewCamera() == second);
    QTest::keyClick(viewport, Qt::Key_Escape);
    REQUIRE(model->previewCamera() == 0);
    REQUIRE(model->undoStack()->count() == history);
    REQUIRE(model->setVisible(second, false));
    QTest::keyClick(viewport, Qt::Key_0, Qt::KeypadModifier);
    REQUIRE(model->previewCamera() == first);
    REQUIRE(model->setVisible(first, false));
    REQUIRE(model->previewCamera() == 0);
    REQUIRE_FALSE(action->isChecked());
    window.hide();
}

TEST_CASE("Preview candidate state is transient and menu navigation works in Legacy",
          "[viewport-navigation]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* camera = window.findChild<QAction*>(QStringLiteral("ToggleCameraPreview"));
    auto* home = window.findChild<QAction*>(QStringLiteral("FocusAll"));
    model->newScene();
    const auto first = model->createCamera();
    const auto second = model->createCamera();
    REQUIRE(model->setPreviewCamera(second));
    REQUIRE(model->setPreviewCamera(0));
    model->selection()->setSelectedEntity(0);
    REQUIRE(model->previewCameraCandidate() == second);
    model->selection()->setSelectedEntity(second);
    model->deleteSelected();
    REQUIRE(model->previewCameraCandidate() == first);
    model->newScene();
    REQUIRE(model->previewCameraCandidate() == 0);
    REQUIRE_FALSE(model->isModified());
    router->setKeymap(editor::EditorKeymap::Legacy);
    REQUIRE(camera->shortcuts().isEmpty());
    REQUIRE(home->shortcuts().isEmpty());
    const auto added = model->createCamera();
    const auto history = model->undoStack()->count();
    camera->trigger();
    REQUIRE(model->previewCamera() == added);
    camera->trigger();
    REQUIRE(model->previewCamera() == 0);
    home->trigger();
    REQUIRE(glm::distance(viewport->editorCameraSnapshot()->target(),
                          model->scene()->find(added)->transform.position) < 1.0e-5F);
    REQUIRE(model->undoStack()->count() == history);
    window.hide();
}
