/*
 * 模块名: GizmoEditorTests
 * 功能概述: 验证真实窗口中的手柄拖动、撤销和中断恢复。
 * 对外接口: Catch2 用例
 * 依赖关系: Qt Test、编辑器、OpenGL
 * 输入输出: 鼠标键盘事件到场景、历史和帧缓冲断言。
 * 异常与错误: 交互与数据不一致时失败。
 * 维护说明: 使用逻辑坐标，允许 QT_SCALE_FACTOR=2 验证。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/EditorCamera.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QFile>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QWheelEvent>
#include <catch2/catch_test_macros.hpp>
using namespace mini3d;

TEST_CASE("Viewport moves by handle and cancels interrupted gestures", "[gizmo-ui]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    REQUIRE(model != nullptr);
    REQUIRE(viewport != nullptr);
    if (qEnvironmentVariable("QT_SCALE_FACTOR") == QStringLiteral("2")) {
        REQUIRE(viewport->devicePixelRatioF() >= 2);
    }
    REQUIRE(model->setVisible(model->scene()->roots().front(), false));
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    const auto baseline = model->undoStack()->count();
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_W);
    REQUIRE(window.findChild<QAction*>(QStringLiteral("MoveTool"))->isChecked());
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(viewport->width(), viewport->height());
    const auto project = [&](glm::vec3 point) {
        const auto clip = camera.projectionMatrix() * camera.viewMatrix() * glm::vec4(point, 1);
        const auto ndc = glm::vec3(clip) / clip.w;
        return QPoint(qRound((ndc.x + 1) * 0.5F * viewport->width()),
                      qRound((1 - ndc.y) * 0.5F * viewport->height()));
    };
    const auto start = project({camera.worldUnitsPerPixel({0, 0, 0}) * 54, 0, 0});
    const auto end = start + QPoint(60, 0);
    const auto drag = [&] {
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, end, viewport->mapToGlobal(end), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
    };
    const auto before = viewport->grabFramebuffer();
    drag();
    REQUIRE(model->scene()->find(id)->transform.position.x > 0);
    REQUIRE(model->scene()->find(id)->transform.position.y == 0);
    REQUIRE(model->scene()->find(id)->transform.position.z == 0);
    REQUIRE(model->undoStack()->count() == baseline);
    const auto previewPosition = model->scene()->find(id)->transform.position;
    QWheelEvent wheel(end, viewport->mapToGlobal(end), {}, {0, 120}, Qt::LeftButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(viewport, &wheel);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    REQUIRE(model->undoStack()->count() == baseline + 1);
    REQUIRE(model->scene()->find(id)->transform.position == previewPosition);
    REQUIRE(model->selection()->selectedEntity() == id);
    REQUIRE(viewport->grabFramebuffer() != before);
    QTest::keyClick(viewport, Qt::Key_Z, Qt::ControlModifier);
    REQUIRE(model->scene()->find(id)->transform.position == glm::vec3(0));
    REQUIRE(viewport->grabFramebuffer() == before);
    drag();
    QTest::keyClick(viewport, Qt::Key_Escape);
    REQUIRE(model->scene()->find(id)->transform.position == glm::vec3(0));
    REQUIRE(model->undoStack()->canRedo());
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    drag();
    QEvent lostFocus(QEvent::FocusOut);
    QApplication::sendEvent(viewport, &lostFocus);
    REQUIRE(model->scene()->find(id)->transform.position == glm::vec3(0));
    REQUIRE(QWidget::mouseGrabber() != viewport);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    for (auto type : {QEvent::WindowDeactivate, QEvent::UngrabMouse}) {
        drag();
        REQUIRE(model->scene()->find(id)->transform.position.x > 0);
        QEvent interruption(type);
        QApplication::sendEvent(viewport, &interruption);
        REQUIRE(model->scene()->find(id)->transform.position == glm::vec3(0));
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    }
    drag();
    QResizeEvent resize(viewport->size(), viewport->size());
    QApplication::sendEvent(viewport, &resize);
    REQUIRE(model->scene()->find(id)->transform.position == glm::vec3(0));
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    drag();
    window.findChild<QAction*>(QStringLiteral("MoveTool"))->setChecked(false);
    REQUIRE(model->scene()->find(id)->transform.position == glm::vec3(0));
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    REQUIRE(model->undoStack()->canRedo());
    QTest::keyClick(viewport, Qt::Key_Y, Qt::ControlModifier);
    REQUIRE(model->scene()->find(id)->transform.position.x > 0);
}

TEST_CASE("Duplicate and delete shortcuts spare inspector text editing", "[gizmo-ui]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_D, Qt::ControlModifier);
    const auto copy = model->selection()->selectedEntity();
    REQUIRE(copy != id);
    REQUIRE(model->scene()->find(copy)->name == "立方体 副本");
    QTest::keyClick(viewport, Qt::Key_Delete);
    REQUIRE(model->scene()->find(copy) == nullptr);
    REQUIRE_FALSE(window.findChild<QAction*>(QStringLiteral("Delete"))->isEnabled());
    QTest::keyClick(viewport, Qt::Key_Z, Qt::ControlModifier);
    REQUIRE(model->scene()->find(copy) != nullptr);
    auto* name = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    name->setFocus();
    name->selectAll();
    QTest::keyClick(name, Qt::Key_Delete);
    REQUIRE(name->text().isEmpty());
    REQUIRE(model->scene()->find(copy) != nullptr);
    QTest::keyClick(name, Qt::Key_Z, Qt::ControlModifier);
    REQUIRE(name->text() == QStringLiteral("立方体 副本"));
    REQUIRE(model->undoStack()->index() == 2);
    QTest::keyClick(name, Qt::Key_D, Qt::ControlModifier);
    REQUIRE(model->undoStack()->count() == 3);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_GIZMO_CAPTURE");
    if (!capture.isEmpty()) {
        viewport->setFocus();
        REQUIRE(model->setVisible(model->scene()->roots().front(), false));
        REQUIRE(model->setVisible(id, false));
        REQUIRE(model->setVisible(copy, false));
        const auto imported =
            model->importGltf(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/BoxTextured.glb"));
        REQUIRE(imported != core::kInvalidEntity);
        REQUIRE(model->setTransformComponent(imported, 0, 0, -1.1));
        model->duplicateSelected();
        REQUIRE(model->setTransformComponent(model->selection()->selectedEntity(), 0, 0, 1.1));
        window.findChild<QAction*>(QStringLiteral("MoveTool"))->setChecked(true);
        QTest::qWait(100);
        REQUIRE(window.grab().save(capture));
    }
}

TEST_CASE("Rotate and scale mouse tools commit once and cancel on tool change",
          "[gizmo-ui][transform-tools]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    model->newScene();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    const core::CameraState state{{0, 0, 5}, {0, 0, 0}, 0, 50};
    viewport->setEditorCamera(state);
    renderer_gl::EditorCamera camera;
    REQUIRE(camera.setState(state));
    camera.setViewportSize(viewport->width(), viewport->height());
    const auto project = [&](glm::vec3 point) {
        const auto clip = camera.viewProjectionMatrix() * glm::vec4(point, 1);
        return QPoint(qRound((clip.x / clip.w + 1) * 0.5F * viewport->width()),
                      qRound((1 - clip.y / clip.w) * 0.5F * viewport->height()));
    };
    const auto drag = [&](QPoint start, QPoint end, bool release) {
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        QMouseEvent move(QEvent::MouseMove, end, viewport->mapToGlobal(end), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &move);
        if (release) {
            QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
        }
    };
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_E);
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Rotate);
    const auto baseline = model->undoStack()->count();
    const float radius = camera.worldUnitsPerPixel({0, 0, 0}) * 90;
    const auto start = project({radius * 0.7071F, radius * 0.7071F, 0});
    const auto end = project({-radius * 0.7071F, radius * 0.7071F, 0});
    const auto initial = viewport->grabFramebuffer();
    drag(start, end, true);
    REQUIRE(model->undoStack()->count() == baseline + 1);
    REQUIRE(glm::length(model->scene()->find(id)->transform.rotation * glm::vec3(1, 0, 0) -
                        glm::vec3(0, 1, 0)) < 0.04F);
    model->undo();
    REQUIRE(model->scene()->find(id)->transform.rotation == glm::quat(1, 0, 0, 0));
    drag(start, end, false);
    QTest::keyClick(viewport, Qt::Key_Escape);
    REQUIRE(model->scene()->find(id)->transform.rotation == glm::quat(1, 0, 0, 0));
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    QTest::keyClick(viewport, Qt::Key_R);
    REQUIRE_FALSE(window.findChild<QAction*>(QStringLiteral("RotateTool"))->isChecked());
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Scale);
    REQUIRE(viewport->grabFramebuffer() != initial);
    const auto center = project({0, 0, 0});
    drag(center, center + QPoint(45, 0), true);
    const auto scale = model->scene()->find(id)->transform.scale;
    REQUIRE(scale.x > 1.4F);
    REQUIRE(scale.x == scale.y);
    REQUIRE(scale.y == scale.z);
    model->undo();
    drag(center, center + QPoint(45, 0), false);
    window.findChild<QAction*>(QStringLiteral("RotateTool"))->trigger();
    REQUIRE(model->scene()->find(id)->transform.scale == glm::vec3(1));
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, center);
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    input->setFocus();
    QTest::keyClicks(input, "ER");
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Rotate);
    const auto scaleCapture = qEnvironmentVariable("MINI3D_TEST_SCALE_CAPTURE");
    if (!scaleCapture.isEmpty()) {
        window.findChild<QAction*>(QStringLiteral("ScaleTool"))->trigger();
        REQUIRE(window.grab().save(scaleCapture));
        window.findChild<QAction*>(QStringLiteral("RotateTool"))->trigger();
    }
    const auto capture = qEnvironmentVariable("MINI3D_TEST_TOOLS_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture));
    }
    window.hide();
}

TEST_CASE("Local coordinate menu aligns handles and cancels on space change",
          "[gizmo-ui][local-tools]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    model->newScene();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->setTransformComponent(id, 1, 2, 90));
    const core::CameraState state{{0, 0, 5}, {0, 0, 0}, 0, 50};
    viewport->setEditorCamera(state);
    renderer_gl::EditorCamera camera;
    REQUIRE(camera.setState(state));
    camera.setViewportSize(viewport->width(), viewport->height());
    viewport->setFocus();
    window.findChild<QAction*>(QStringLiteral("MoveTool"))->trigger();
    const auto world = viewport->grabFramebuffer();
    window.findChild<QAction*>(QStringLiteral("LocalTransformSpace"))->trigger();
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::Local);
    REQUIRE(viewport->grabFramebuffer() != world);
    const QPoint start(viewport->width() / 2, viewport->height() / 2 - 54);
    const auto end = start - QPoint(0, 45);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, end, viewport->mapToGlobal(end), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    REQUIRE(model->scene()->find(id)->transform.position.y > 0.1F);
    REQUIRE(std::abs(model->scene()->find(id)->transform.position.x) < 1.0e-5F);
    window.findChild<QAction*>(QStringLiteral("WorldTransformSpace"))->trigger();
    REQUIRE(model->scene()->find(id)->transform.position == glm::vec3(0));
    REQUIRE(QWidget::mouseGrabber() != viewport);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::World);
    window.hide();
}

TEST_CASE("Plane dragging and menu snap produce one reversible edit", "[gizmo-ui][snap-tools]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    model->newScene();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    viewport->setEditorCamera({{0, 0, 5}, {0, 0, 0}, 0, 50});
    window.findChild<QAction*>(QStringLiteral("MoveTool"))->trigger();
    window.findChild<QAction*>(QStringLiteral("SnapTransform"))->setChecked(true);
    viewport->setFocus();
    const QPoint start(viewport->width() / 2 + 27, viewport->height() / 2 - 27);
    const auto end = start + QPoint(70, -40);
    const auto before = model->undoStack()->count();
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, end, viewport->mapToGlobal(end), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    const auto position = model->scene()->find(id)->transform.position;
    REQUIRE(position.x > 0);
    REQUIRE(position.y > 0);
    REQUIRE(position.z == 0);
    REQUIRE(std::abs(position.x * 2 - std::round(position.x * 2)) < 1.0e-5F);
    REQUIRE(std::abs(position.y * 2 - std::round(position.y * 2)) < 1.0e-5F);
    REQUIRE(model->undoStack()->count() == before + 1);
    model->undo();
    REQUIRE(model->scene()->find(id)->transform.position == glm::vec3(0));
    window.hide();
}

TEST_CASE("View shortcuts support top camera creation picking and input isolation",
          "[gizmo-ui][view-tools]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    viewport->setEditorCamera({{0, 0, 5}, {0, 0, 0}, 0, 50});
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_7);
    QTest::keyClick(viewport, Qt::Key_5);
    REQUIRE(viewport->isOrthographic());
    REQUIRE(window.findChild<QAction*>(QStringLiteral("OrthographicView"))->isChecked());
    const auto pose = viewport->viewTransform();
    REQUIRE(pose.has_value());
    REQUIRE(glm::distance(pose->rotation * glm::vec3(0, 0, -1), glm::vec3(0, -1, 0)) < 1.0e-5F);
    model->selection()->setSelectedEntity(0);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, viewport->rect().center());
    REQUIRE(model->selection()->selectedEntity() == cube);
    window.findChild<QAction*>(QStringLiteral("CreateCamera"))->trigger();
    const auto id = model->selection()->selectedEntity();
    REQUIRE(model->scene()->find(id)->camera.has_value());
    REQUIRE(glm::distance(model->scene()->find(id)->transform.position, pose->position) < 1.0e-5F);
    REQUIRE(glm::distance(model->scene()->find(id)->transform.rotation * glm::vec3(0, 0, -1),
                          glm::vec3(0, -1, 0)) < 1.0e-5F);
    REQUIRE(model->setPreviewCamera(id));
    QTest::keyClick(viewport, Qt::Key_5);
    REQUIRE(viewport->isOrthographic());
    REQUIRE(window.findChild<QAction*>(QStringLiteral("OrthographicView"))->isChecked());
    REQUIRE(model->setPreviewCamera(0));
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    input->setFocus();
    QTest::keyClicks(input, "1357");
    REQUIRE(viewport->isOrthographic());
    model->selection()->setSelectedEntity(cube);
    viewport->setFocus();
    window.findChild<QAction*>(QStringLiteral("MoveTool"))->trigger();
    const QPoint start(viewport->width() / 2 + 54, viewport->height() / 2);
    const auto end = start + QPoint(45, 0);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, end, viewport->mapToGlobal(end), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, end);
    REQUIRE(model->scene()->find(cube)->transform.position.x > 0);
    model->undo();
    REQUIRE(model->scene()->find(cube)->transform.position == glm::vec3(0));
    window.findChild<QAction*>(QStringLiteral("ScaleTool"))->trigger();
    const auto center = viewport->rect().center();
    const auto scaleEnd = center + QPoint(45, 0);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, center);
    QMouseEvent scaleMove(QEvent::MouseMove, scaleEnd, viewport->mapToGlobal(scaleEnd),
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &scaleMove);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, scaleEnd);
    REQUIRE(model->scene()->find(cube)->transform.scale.x > 1.4F);
    model->undo();
    window.findChild<QAction*>(QStringLiteral("MoveTool"))->trigger();
    const auto capture = qEnvironmentVariable("MINI3D_TEST_VIEW_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture));
    }
    model->newScene();
    REQUIRE_FALSE(viewport->isOrthographic());
    REQUIRE_FALSE(window.findChild<QAction*>(QStringLiteral("OrthographicView"))->isChecked());
    window.hide();
}

TEST_CASE("Viewport accepts local model drops and rejects mixed or unavailable inputs",
          "[gizmo-ui][drop-tools]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    model->newScene();
    const auto drop = [&](const QList<QUrl>& urls, bool expected) {
        QMimeData mime;
        mime.setUrls(urls);
        QDragEnterEvent enter(viewport->rect().center(), Qt::CopyAction, &mime, Qt::LeftButton,
                              Qt::NoModifier);
        QApplication::sendEvent(viewport, &enter);
        REQUIRE(enter.isAccepted() == expected);
        if (expected) {
            QDragMoveEvent move(viewport->rect().center(), Qt::CopyAction, &mime, Qt::LeftButton,
                                Qt::NoModifier);
            QApplication::sendEvent(viewport, &move);
            REQUIRE(move.isAccepted());
            QDropEvent event(viewport->rect().center(), Qt::CopyAction, &mime, Qt::LeftButton,
                             Qt::NoModifier);
            QApplication::sendEvent(viewport, &event);
            REQUIRE(event.isAccepted());
            REQUIRE(event.dropAction() == Qt::CopyAction);
        }
    };
    const auto box =
        QUrl::fromLocalFile(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/BoxTextured.glb"));
    drop({box}, true);
    REQUIRE(model->scene()->roots().size() == 1);
    REQUIRE(model->undoStack()->count() == 1);
    const auto first = model->selection()->selectedEntity();
    model->undo();
    REQUIRE(model->scene()->roots().empty());
    model->redo();
    REQUIRE(model->scene()->find(first) != nullptr);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto upperPath = dir.filePath(QStringLiteral("模型.GLB"));
    REQUIRE(QFile::copy(box.toLocalFile(), upperPath));
    drop({QUrl::fromLocalFile(upperPath), box}, true);
    REQUIRE(model->scene()->roots().size() == 3);
    REQUIRE(model->undoStack()->count() == 3);
    const auto before = model->selection()->selectedEntity();
    drop({QUrl(QStringLiteral("https://example.com/model.glb"))}, false);
    drop({box, QUrl::fromLocalFile(dir.path())}, false);
    drop({QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.gltf")))}, false);
    const auto brokenPath = dir.filePath(QStringLiteral("损坏.gltf"));
    QFile broken(brokenPath);
    REQUIRE(broken.open(QIODevice::WriteOnly));
    REQUIRE(broken.write("{}") == 2);
    broken.close();
    drop({QUrl::fromLocalFile(brokenPath)}, true);
    REQUIRE(model->selection()->selectedEntity() == before);
    REQUIRE(model->scene()->roots().size() == 3);
    REQUIRE(model->undoStack()->count() == 3);
    const auto camera = model->createCamera();
    REQUIRE(model->setPreviewCamera(camera));
    drop({box}, false);
    window.hide();
}
