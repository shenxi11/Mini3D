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
#include "editor/operations/ObjectTransformSession.h"
#include "renderer_gl/RayCaster.h"
#include "renderer_gl/ViewNavigationWidget.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFocusEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <glm/matrix.hpp>
#include <utility>
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

using Navigation = renderer_gl::ViewNavigationWidget;
using View = renderer_gl::EditorView;

void moveNavigation(Navigation* navigation, QPoint position,
                    Qt::MouseButtons buttons = Qt::NoButton) {
    QMouseEvent event(QEvent::MouseMove, position, navigation->mapToGlobal(position),
                      Qt::NoButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(navigation, &event);
}

void requireNavigationHit(Navigation* navigation, QPoint position) {
    const auto global = navigation->mapToGlobal(position);
    REQUIRE(navigation->window()->childAt(navigation->window()->mapFromGlobal(global)) ==
            navigation);
}

void clickAxis(Navigation* navigation, View view) {
    const auto point = navigation->axisPosition(view);
    REQUIRE(point.has_value());
    requireNavigationHit(navigation, point->toPoint());
    moveNavigation(navigation, point->toPoint());
    QTest::mouseClick(navigation, Qt::LeftButton, Qt::NoModifier, point->toPoint());
}

void clickControl(Navigation* navigation, Navigation::Control control) {
    const auto position = navigation->controlCenter(control);
    requireNavigationHit(navigation, position);
    moveNavigation(navigation, position);
    QTest::mouseClick(navigation, Qt::LeftButton, Qt::NoModifier, position);
}

struct NavigationFixture {
    editor::MainWindow window;
    editor::SceneViewModel* model = nullptr;
    renderer_gl::ViewportWidget* viewport = nullptr;
    Navigation* navigation = nullptr;
    core::EntityId cube = 0;
    NavigationFixture() {
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        model = window.findChild<editor::SceneViewModel*>();
        viewport = window.findChild<renderer_gl::ViewportWidget*>();
        navigation = window.findChild<Navigation*>();
        REQUIRE(model != nullptr);
        REQUIRE(viewport != nullptr);
        REQUIRE(navigation != nullptr);
        REQUIRE(viewport->editorCameraSnapshot().has_value());
        model->newScene();
        cube = model->createEntity(core::PrimitiveKind::Cube);
        REQUIRE(cube != 0);
        viewport->setFocus();
        pointAt(viewport);
    }
    ~NavigationFixture() { window.hide(); }
};

void captureNavigation(NavigationFixture& fixture, const QString& label) {
    const auto output = qEnvironmentVariable("MINI3D_TEST_BLENDER_ARTIFACT_DIR");
    if (output.isEmpty())
        return;
    REQUIRE(QDir().mkpath(output));
    QTest::qWait(30);
    REQUIRE(fixture.navigation->grab().save(output + '/' + label + "-navigation.png"));
    REQUIRE(fixture.window.grab().save(output + '/' + label + "-window.png"));
    const auto camera = *fixture.viewport->editorCameraSnapshot();
    const auto vector = [](const glm::vec3& value) {
        return QJsonArray{value.x, value.y, value.z};
    };
    QJsonArray matrix;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            matrix.append(camera.viewMatrix()[column][row]);
    QJsonArray dots;
    for (const auto view : {View::Right, View::Left, View::Top, View::Bottom, View::Front, View::Back}) {
        const auto point = *fixture.navigation->axisPosition(view);
        dots.append(QJsonObject{{"view", static_cast<int>(view)}, {"x", point.x()}, {"y", point.y()}});
    }
    const auto rect = [](const QRect& value) {
        return QJsonObject{{"x", value.x()}, {"y", value.y()}, {"width", value.width()},
                            {"height", value.height()}};
    };
    const QJsonObject state{{"label", label}, {"requestedScale", qEnvironmentVariable("QT_SCALE_FACTOR")},
                            {"window", rect(fixture.window.rect())},
                            {"viewport", rect(fixture.viewport->geometry())},
                            {"navigation", rect(fixture.navigation->geometry())},
                            {"devicePixelRatio", fixture.navigation->devicePixelRatioF()},
                            {"view", static_cast<int>(camera.view())},
                            {"orthographic", camera.isOrthographic()},
                            {"preview", fixture.viewport->isPreviewingCamera()},
                            {"position", vector(camera.position())}, {"target", vector(camera.target())},
                            {"distance", camera.distance()}, {"viewMatrixColumnMajor", matrix},
                            {"axisDots", dots}, {"savedCameraValid", camera.state().isValid()},
                            {"selectedEntity", static_cast<qint64>(fixture.model->selection()->selectedEntity())},
                            {"historyCount", fixture.model->undoStack()->count()}};
    QFile file(output + '/' + label + "-state.json");
    REQUIRE(file.open(QIODevice::WriteOnly));
    const auto data = QJsonDocument(state).toJson(QJsonDocument::Indented);
    REQUIRE(file.write(data) == data.size());
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

TEST_CASE("Navigation axis hit targets cover six directions and repeated clicks reverse an aligned axis",
          "[view-navigation]") {
    NavigationFixture fixture;
    auto* viewport = fixture.viewport;
    auto* navigation = fixture.navigation;
    REQUIRE(fixture.model->setEditMode(true));
    const auto selectionRevision = fixture.model->componentSelectionRevision();
    const auto history = fixture.model->undoStack()->count();
    const auto selected = fixture.model->selection()->selectedEntity();
    const auto camera = *viewport->editorCameraSnapshot();
    QSignalSpy picks(viewport, &renderer_gl::ViewportWidget::pickRequested);
    QSignalSpy componentPicks(viewport, &renderer_gl::ViewportWidget::componentPickRequested);
    const std::array<std::pair<View, glm::vec3>, 6> directions{
        std::pair{View::Right, glm::vec3(1, 0, 0)}, std::pair{View::Left, glm::vec3(-1, 0, 0)},
        std::pair{View::Top, glm::vec3(0, 1, 0)}, std::pair{View::Bottom, glm::vec3(0, -1, 0)},
        std::pair{View::Front, glm::vec3(0, 0, 1)}, std::pair{View::Back, glm::vec3(0, 0, -1)}};
    // 从已对齐姿态直接点重合轴点，不能靠每轮回到 Orbit 绕过反向访问。
    for (const auto& [view, direction] : directions) {
        INFO("clicked view " << static_cast<int>(view));
        clickAxis(navigation, view);
        const auto actual = *viewport->editorCameraSnapshot();
        REQUIRE(actual.view() == view);
        REQUIRE(glm::distance(glm::normalize(actual.position() - actual.target()), direction) < 1.0e-6F);
        REQUIRE(actual.target() == camera.target());
        REQUIRE(actual.distance() == camera.distance());
        REQUIRE(actual.state().isValid());
        captureNavigation(fixture, QStringLiteral("axis-%1").arg(static_cast<int>(view)));
    }
    clickAxis(navigation, View::Back);
    REQUIRE(viewport->editorCameraSnapshot()->view() == View::Front);
    clickAxis(navigation, View::Front);
    REQUIRE(viewport->editorCameraSnapshot()->view() == View::Back);
    REQUIRE(picks.isEmpty());
    REQUIRE(componentPicks.isEmpty());
    REQUIRE(fixture.model->componentSelectionRevision() == selectionRevision);
    REQUIRE(fixture.model->selection()->selectedEntity() == selected);
    REQUIRE(fixture.model->undoStack()->count() == history);

    viewport->setCameraView(View::Top);
    const auto top = *viewport->editorCameraSnapshot();
    const auto start = navigation->axisPosition(View::Top)->toPoint();
    QTest::mousePress(navigation, Qt::LeftButton, Qt::NoModifier, start);
    moveNavigation(navigation, start + QPoint(12, 12), Qt::LeftButton);
    REQUIRE(navigation->isDragging());
    const auto orbit = *viewport->editorCameraSnapshot();
    REQUIRE(orbit.view() == View::Orbit);
    REQUIRE(glm::dot(glm::normalize(orbit.position() - orbit.target()), glm::vec3(0, 1, 0)) > 0.99F);
    REQUIRE(glm::dot(glm::vec3(glm::inverse(top.viewMatrix())[1]),
                      glm::vec3(glm::inverse(orbit.viewMatrix())[1])) > 0.99F);
    moveNavigation(navigation, start + QPoint(13, 13), Qt::LeftButton);
    const auto continued = *viewport->editorCameraSnapshot();
    REQUIRE(glm::distance(continued.position(), orbit.position()) < orbit.distance() * 0.02F);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(navigation, &leave);
    REQUIRE(navigation->isDragging());
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier,
                         QPoint(navigation->width() + 10, navigation->height() + 10));
    REQUIRE_FALSE(navigation->hasPendingGesture());
    REQUIRE(QWidget::mouseGrabber() != navigation);
    moveNavigation(navigation, start + QPoint(40, 30), Qt::LeftButton);
    REQUIRE(viewport->editorCameraSnapshot()->viewMatrix() == continued.viewMatrix());
    REQUIRE(picks.isEmpty());
    REQUIRE(componentPicks.isEmpty());
    REQUIRE(fixture.model->undoStack()->count() == history);
    captureNavigation(fixture, QStringLiteral("axis-drag"));
}

TEST_CASE("Navigation controls update the real camera projection and leave transparent space pickable",
          "[view-navigation]") {
    NavigationFixture fixture;
    auto* viewport = fixture.viewport;
    auto* navigation = fixture.navigation;
    auto* projection = fixture.window.findChild<QAction*>(QStringLiteral("OrthographicView"));
    REQUIRE(projection != nullptr);
    QSignalSpy picks(viewport, &renderer_gl::ViewportWidget::pickRequested);
    QSignalSpy componentPicks(viewport, &renderer_gl::ViewportWidget::componentPickRequested);
    const auto history = fixture.model->undoStack()->count();
    const auto selected = fixture.model->selection()->selectedEntity();
    moveNavigation(navigation, navigation->controlCenter(Navigation::Control::Projection));
    const auto perspectiveIcon = navigation->grab().toImage();
    clickControl(navigation, Navigation::Control::Projection);
    REQUIRE(viewport->isOrthographic());
    REQUIRE(projection->isChecked());
    REQUIRE(navigation->toolTip().contains(QStringLiteral("透视")));
    REQUIRE(navigation->grab().toImage() != perspectiveIcon);
    captureNavigation(fixture, QStringLiteral("projection-orthographic"));
    projection->trigger();
    REQUIRE_FALSE(viewport->isOrthographic());
    REQUIRE(navigation->toolTip().contains(QStringLiteral("正交")));
    viewport->setOrthographic(true);
    REQUIRE(projection->isChecked());
    REQUIRE(navigation->toolTip().contains(QStringLiteral("透视")));
    viewport->setOrthographic(false);

    const auto beforeZoom = *viewport->editorCameraSnapshot();
    clickControl(navigation, Navigation::Control::Zoom);
    REQUIRE(viewport->editorCameraSnapshot()->distance() < beforeZoom.distance());
    const auto zoomStart = navigation->controlCenter(Navigation::Control::Zoom);
    const float clickedDistance = viewport->editorCameraSnapshot()->distance();
    QTest::mousePress(navigation, Qt::LeftButton, Qt::NoModifier, zoomStart);
    moveNavigation(navigation, zoomStart + QPoint(0, 20), Qt::LeftButton);
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier, zoomStart + QPoint(0, 20));
    REQUIRE(viewport->editorCameraSnapshot()->distance() > clickedDistance);
    const auto beforePan = *viewport->editorCameraSnapshot();
    const auto panStart = navigation->controlCenter(Navigation::Control::Pan);
    QTest::mousePress(navigation, Qt::LeftButton, Qt::NoModifier, panStart);
    moveNavigation(navigation, panStart + QPoint(15, 5), Qt::LeftButton);
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier, panStart + QPoint(15, 5));
    const auto afterPan = *viewport->editorCameraSnapshot();
    REQUIRE(glm::distance(afterPan.target(), beforePan.target()) > 0.01F);
    REQUIRE(afterPan.distance() == beforePan.distance());
    REQUIRE(picks.isEmpty());
    REQUIRE(componentPicks.isEmpty());
    REQUIRE(fixture.model->selection()->selectedEntity() == selected);
    REQUIRE(fixture.model->undoStack()->count() == history);

    fixture.window.resize(std::max(fixture.window.minimumWidth(), fixture.window.width() - 80),
                            std::max(fixture.window.minimumHeight(), fixture.window.height() - 40));
    QTest::qWait(30);
    REQUIRE(navigation->x() + navigation->width() + 8 == viewport->width());
    REQUIRE(navigation->y() == 8);
    const QPoint blank(10, 130);
    REQUIRE_FALSE(navigation->mask().contains(blank));
    const auto global = navigation->mapToGlobal(blank);
    auto* hit = fixture.window.childAt(fixture.window.mapFromGlobal(global));
    REQUIRE(hit == viewport);
    QTest::mouseClick(hit, Qt::LeftButton, Qt::NoModifier, hit->mapFromGlobal(global));
    REQUIRE(picks.count() == 1);
    REQUIRE(componentPicks.isEmpty());
    captureNavigation(fixture, QStringLiteral("navigation-resized"));
}

TEST_CASE("Navigation cancels captured modeling input and isolates camera preview and gesture cancellation",
          "[view-navigation]") {
    NavigationFixture fixture;
    auto* viewport = fixture.viewport;
    auto* navigation = fixture.navigation;
    auto* modal = fixture.window.findChild<editor::ObjectTransformSession*>();
    REQUIRE(modal != nullptr);
    QSignalSpy picks(viewport, &renderer_gl::ViewportWidget::pickRequested);
    QSignalSpy componentPicks(viewport, &renderer_gl::ViewportWidget::componentPickRequested);
    QSignalSpy starts(viewport, &renderer_gl::ViewportWidget::navigationStarted);
    const auto beforeTransform = fixture.model->scene()->find(fixture.cube)->transform;
    const auto history = fixture.model->undoStack()->count();
    viewport->setFocus();
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_G);
    REQUIRE(modal->isActive());
    QTest::keyClicks(viewport, "x2");
    REQUIRE(fixture.model->scene()->find(fixture.cube)->transform.position != beforeTransform.position);
    REQUIRE(QWidget::mouseGrabber() == viewport);
    const auto topPoint = navigation->axisPosition(View::Top)->toPoint();
    requireNavigationHit(navigation, topPoint);
    const auto global = navigation->mapToGlobal(topPoint);
    const auto capturedPoint = viewport->mapFromGlobal(global);
    // 真正抓鼠标时先送 viewport，不能直接给子控件而绕过模态确认路径。
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, capturedPoint);
    REQUIRE_FALSE(modal->isActive());
    REQUIRE_FALSE(starts.isEmpty());
    REQUIRE(fixture.model->scene()->find(fixture.cube)->transform.position == beforeTransform.position);
    REQUIRE(fixture.model->scene()->find(fixture.cube)->transform.rotation == beforeTransform.rotation);
    REQUIRE(fixture.model->scene()->find(fixture.cube)->transform.scale == beforeTransform.scale);
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier, topPoint);
    REQUIRE(viewport->editorCameraSnapshot()->view() == View::Top);
    QMouseEvent trailingRelease(QEvent::MouseButtonRelease, capturedPoint, global, Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &trailingRelease);
    REQUIRE(picks.isEmpty());
    REQUIRE(componentPicks.isEmpty());
    REQUIRE(fixture.model->selection()->selectedEntity() == fixture.cube);
    REQUIRE(fixture.model->undoStack()->count() == history);

    const auto start = navigation->axisPosition(View::Top)->toPoint();
    QTest::mousePress(navigation, Qt::LeftButton, Qt::NoModifier, start);
    moveNavigation(navigation, start + QPoint(1, 0), Qt::LeftButton);
    REQUIRE_FALSE(navigation->isDragging());
    QTest::keyClick(navigation, Qt::Key_Escape);
    REQUIRE_FALSE(navigation->hasPendingGesture());
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier, start + QPoint(1, 0));
    REQUIRE(viewport->editorCameraSnapshot()->view() == View::Top);
    QTest::mousePress(navigation, Qt::LeftButton, Qt::NoModifier, start);
    moveNavigation(navigation, start + QPoint(14, 14), Qt::LeftButton);
    REQUIRE(navigation->isDragging());
    QTest::keyClick(navigation, Qt::Key_Escape);
    const auto canceled = *viewport->editorCameraSnapshot();
    REQUIRE_FALSE(navigation->hasPendingGesture());
    REQUIRE(QWidget::mouseGrabber() != navigation);
    moveNavigation(navigation, start + QPoint(40, 40), Qt::LeftButton);
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier, start + QPoint(40, 40));
    REQUIRE(viewport->editorCameraSnapshot()->viewMatrix() == canceled.viewMatrix());
    QTest::mousePress(navigation, Qt::LeftButton, Qt::NoModifier, start);
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(navigation, &focusOut);
    REQUIRE_FALSE(navigation->hasPendingGesture());
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier, start);
    REQUIRE(viewport->editorCameraSnapshot()->viewMatrix() == canceled.viewMatrix());

    const auto cameraEntity = fixture.model->createCamera();
    fixture.model->selection()->setSelectedEntity(fixture.cube);
    const auto previewHistory = fixture.model->undoStack()->count();
    // 初显的零延时布局按真实窗口尺寸调整 Dock；处理后再冻结观察投影。
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    viewport->setCameraView(View::Bottom);
    viewport->setOrthographic(true);
    const auto editorCamera = *viewport->editorCameraSnapshot();
    const auto snapshotViewportSize = viewport->size();
    INFO("snapshot viewport " << snapshotViewportSize.width() << "x" << snapshotViewportSize.height()
                                << ", aspect " << editorCamera.aspectRatio());
    clickControl(navigation, Navigation::Control::Camera);
    REQUIRE(fixture.model->previewCamera() == cameraEntity);
    REQUIRE(viewport->isPreviewingCamera());
    QSignalSpy rejected(viewport, &renderer_gl::ViewportWidget::interactionRejected);
    clickAxis(navigation, View::Top);
    clickControl(navigation, Navigation::Control::Projection);
    clickControl(navigation, Navigation::Control::Zoom);
    REQUIRE(rejected.count() == 3);
    REQUIRE(viewport->size() == snapshotViewportSize);
    REQUIRE(viewport->editorCameraSnapshot()->viewProjectionMatrix() == editorCamera.viewProjectionMatrix());
    REQUIRE(fixture.model->previewCamera() == cameraEntity);
    REQUIRE(fixture.model->undoStack()->count() == previewHistory);
    captureNavigation(fixture, QStringLiteral("navigation-preview"));
    REQUIRE(viewport->size() == snapshotViewportSize);
    clickControl(navigation, Navigation::Control::Camera);
    REQUIRE(viewport->size() == snapshotViewportSize);
    REQUIRE(fixture.model->previewCamera() == 0);
    REQUIRE_FALSE(viewport->isPreviewingCamera());
    REQUIRE(viewport->editorCameraSnapshot()->viewProjectionMatrix() == editorCamera.viewProjectionMatrix());
    REQUIRE(picks.isEmpty());
    REQUIRE(componentPicks.isEmpty());
    REQUIRE(fixture.model->selection()->selectedEntity() == fixture.cube);
    REQUIRE(fixture.model->undoStack()->count() == previewHistory);
    captureNavigation(fixture, QStringLiteral("navigation-returned"));
}

TEST_CASE("Legacy shortcuts keep working after navigation release and Escape without stealing text focus",
          "[view-navigation][keymap]") {
    NavigationFixture fixture;
    auto* viewport = fixture.viewport;
    auto* navigation = fixture.navigation;
    auto* router = fixture.window.findChild<editor::KeymapRouter*>();
    auto* move = fixture.window.findChild<QAction*>(QStringLiteral("MoveTool"));
    auto* front = fixture.window.findChild<QAction*>(QStringLiteral("FrontView"));
    auto* input = fixture.window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    REQUIRE(router != nullptr);
    REQUIRE(move != nullptr);
    REQUIRE(front != nullptr);
    REQUIRE(input != nullptr);
    router->setKeymap(editor::EditorKeymap::Legacy);
    viewport->setTransformTool(renderer_gl::GizmoTool::None);
    QSignalSpy moved(move, &QAction::triggered);
    QSignalSpy viewed(front, &QAction::triggered);
    const auto history = fixture.model->undoStack()->count();

    clickAxis(navigation, View::Top);
    REQUIRE(QApplication::focusWidget() == viewport);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_W);
    REQUIRE(moved.count() == 1);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Move);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_1);
    REQUIRE(viewed.count() == 1);
    REQUIRE(viewport->editorCameraSnapshot()->view() == View::Front);

    clickControl(navigation, Navigation::Control::Projection);
    REQUIRE(QApplication::focusWidget() == viewport);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_1);
    REQUIRE(viewed.count() == 2);
    const auto start = navigation->axisPosition(View::Front)->toPoint();
    QTest::mousePress(navigation, Qt::LeftButton, Qt::NoModifier, start);
    moveNavigation(navigation, start + QPoint(14, 12), Qt::LeftButton);
    REQUIRE(navigation->isDragging());
    REQUIRE(QApplication::focusWidget() == navigation);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    REQUIRE_FALSE(navigation->hasPendingGesture());
    REQUIRE(QWidget::mouseGrabber() != navigation);
    REQUIRE(QApplication::focusWidget() == viewport);
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier, start + QPoint(14, 12));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_W);
    REQUIRE(moved.count() == 2);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::None);

    QTest::mousePress(navigation, Qt::LeftButton, Qt::NoModifier, start);
    REQUIRE(navigation->hasPendingGesture());
    input->setFocus();
    REQUIRE(QApplication::focusWidget() == input);
    REQUIRE_FALSE(navigation->hasPendingGesture());
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier, start);
    REQUIRE(QApplication::focusWidget() == input);
    REQUIRE(moved.count() == 2);
    REQUIRE(viewed.count() == 2);
    REQUIRE(fixture.model->undoStack()->count() == history);
}
