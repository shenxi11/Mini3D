/*
 * 模块名: DocumentEditorTests
 * 功能概述: 通过真实 Qt 对话框验收保存重开、关闭保护和光照材质视图。
 * 对外接口: Catch2 用例
 * 依赖关系: Qt Test、MainWindow、临时目录
 * 输入输出: 菜单/键盘/对话框到文件、标题和 GPU 画面断言。
 * 异常与错误: 对话框类型不符立即终止，不挂起测试。
 * 维护说明: 只写临时文件；正式截图由显式环境变量启用。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <QWheelEvent>
#include <catch2/catch_test_macros.hpp>
using namespace mini3d;
namespace {
void answerFile(const QString& path, bool accept = true) {
    QTimer::singleShot(50, [path, accept] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) {
            qFatal("Expected a scene file dialog");
        }
        if (!accept) {
            dialog->reject();
            return;
        }
        dialog->setDirectory(QFileInfo(path).absolutePath());
        QTest::qWait(100);
        auto* input = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!input || !buttons) {
            qFatal("File dialog controls unavailable");
        }
        input->setFocus();
        input->selectAll();
        QTest::keyClicks(input, QFileInfo(path).fileName());
        QTimer::singleShot(3000, dialog, [dialog] {
            if (dialog->isVisible()) {
                qWarning() << "Scene dialog did not accept" << dialog->selectedFiles()
                           << QApplication::activeModalWidget();
                if (auto* nested = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    nested && nested != dialog) {
                    nested->reject();
                }
                dialog->reject();
            }
        });
        QTest::mouseClick(buttons->button(dialog->acceptMode() == QFileDialog::AcceptSave
                                              ? QDialogButtonBox::Save
                                              : QDialogButtonBox::Open),
                          Qt::LeftButton);
    });
}
void answerUnsaved(QMessageBox::StandardButton choice, bool cancelFile = false) {
    QTimer::singleShot(50, [choice, cancelFile] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!dialog) {
            qFatal("Expected an unsaved changes dialog");
        }
        if (cancelFile) {
            answerFile({}, false);
        }
        QTest::mouseClick(dialog->button(choice), Qt::LeftButton);
    });
}
} // namespace
TEST_CASE("File menu saves and reopens the rendered scene with camera and appearance",
          "[document-ui]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(QFile::copy(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/BoxTextured.glb"),
                        directory.filePath("model.glb")));
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* inspector = window.findChild<QScrollArea*>();
    REQUIRE(inspector->horizontalScrollBar()->maximum() == 0);
    if (qEnvironmentVariable("QT_SCALE_FACTOR") == QStringLiteral("2")) {
        REQUIRE(viewport->devicePixelRatioF() >= 2);
    }
    window.findChild<QAction*>(QStringLiteral("NewScene"))->trigger();
    REQUIRE(model->scene()->roots().empty());
    const auto root = model->importGltf(directory.filePath("model.glb"));
    REQUIRE(root != 0);
    core::EntityId mesh = 0;
    for (const auto& node : model->scene()->nodes()) {
        if (node.meshRenderer) {
            mesh = node.id;
            break;
        }
    }
    REQUIRE(mesh != 0);
    model->selection()->setSelectedEntity(mesh);
    const auto initial = viewport->grabFramebuffer();
    window.findChild<QDoubleSpinBox*>(QStringLiteral("Tint0"))->setValue(0.3);
    window.findChild<QDoubleSpinBox*>(QStringLiteral("LightIntensity"))->setValue(0.5);
    REQUIRE(model->scene()->find(mesh)->surface.tint.x == 0.3F);
    REQUIRE(model->scene()->lighting().intensity == 0.5F);
    REQUIRE(viewport->grabFramebuffer() != initial);
    viewport->setFocus();
    const auto point = viewport->rect().center();
    QWheelEvent wheel(point, viewport->mapToGlobal(point), {}, {0, 120}, Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(viewport, &wheel);
    model->selection()->setSelectedEntity(0);
    const auto camera = model->editorCamera();
    // 与打开场景使用同一次球坐标重建，隔离 atan2/asin 的浮点舍入对边缘光栅化的影响。
    viewport->setEditorCamera(camera);
    const auto before = viewport->grabFramebuffer();
    REQUIRE(window.isWindowModified());
    const auto path = directory.filePath("scene.m3dscene");
    answerFile(path);
    QTest::keyClick(viewport, Qt::Key_S, Qt::ControlModifier);
    REQUIRE(QFile::exists(path));
    REQUIRE_FALSE(window.isWindowModified());
    REQUIRE(QDir(directory.path()).mkdir("copies"));
    const auto alternate = directory.filePath("copies/alternate.m3dscene");
    answerFile(alternate);
    window.findChild<QAction*>(QStringLiteral("SaveSceneAs"))->trigger();
    REQUIRE(QFile::exists(alternate));
    REQUIRE(model->filePath() == alternate);
    window.findChild<QAction*>(QStringLiteral("NewScene"))->trigger();
    REQUIRE(model->scene()->roots().empty());
    answerFile(alternate);
    window.findChild<QAction*>(QStringLiteral("OpenScene"))->trigger();
    REQUIRE(model->scene()->find(mesh) != nullptr);
    REQUIRE(model->scene()->find(mesh)->surface.tint.x == 0.3F);
    REQUIRE(model->editorCamera() == camera);
    REQUIRE_FALSE(window.isWindowModified());
    const auto after = viewport->grabFramebuffer();
    INFO("Before " << before.width() << "x" << before.height() << "; after " << after.width() << "x"
                   << after.height());
    REQUIRE(after == before);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_DOCUMENT_CAPTURE");
    if (!capture.isEmpty()) {
        model->selection()->setSelectedEntity(mesh);
        window.findChild<QTreeView*>(QStringLiteral("SceneTree"))->expandAll();
        window.findChild<QAction*>(QStringLiteral("MoveTool"))->setChecked(true);
        QTest::qWait(100);
        REQUIRE(window.grab().save(capture));
    }
    REQUIRE(window.close());
}
TEST_CASE("Camera preview and directional light controls affect real framebuffer",
          "[camera-light][device-ui]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* scroll = window.findChild<QScrollArea*>();
    model->newScene();
    model->createEntity(core::PrimitiveKind::Cube);
    window.findChild<QAction*>(QStringLiteral("CreateCamera"))->trigger();
    const auto camera = model->selection()->selectedEntity();
    REQUIRE(model->scene()->find(camera)->camera.has_value());
    REQUIRE(model->scene()->find(camera)->transform.position == model->editorCamera().position);
    core::Transform pose;
    pose.position = {0, 0, 5};
    REQUIRE(model->setTransform(camera, pose));
    auto* fov = window.findChild<QDoubleSpinBox*>(QStringLiteral("CameraFov"));
    scroll->ensureWidgetVisible(fov);
    fov->setFocus();
    fov->selectAll();
    QTest::keyClicks(fov, "60");
    QTest::keyClick(fov, Qt::Key_Return);
    REQUIRE(model->scene()->find(camera)->camera->fieldOfView == 60);
    model->undo();
    REQUIRE(fov->value() == 45);
    model->redo();
    REQUIRE(fov->value() == 60);
    auto* nearClip = window.findChild<QDoubleSpinBox*>(QStringLiteral("CameraNear"));
    nearClip->setValue(2000); // 大于 far 的值必须恢复控件和模型。
    REQUIRE(nearClip->value() == 0.1);
    REQUIRE(model->scene()->find(camera)->camera->nearPlane == 0.1F);
    auto* preview = window.findChild<QPushButton*>(QStringLiteral("PreviewCamera"));
    auto* exit = window.findChild<QPushButton*>(QStringLiteral("ExitCameraPreview"));
    scroll->ensureWidgetVisible(preview);
    const auto editorImage = viewport->grabFramebuffer();
    const auto observer = model->editorCamera();
    const auto history = model->undoStack()->count();
    QSignalSpy navigation(viewport, &renderer_gl::ViewportWidget::cameraChanged);
    QSignalSpy picking(viewport, &renderer_gl::ViewportWidget::pickRequested);
    QSignalSpy moving(viewport, &renderer_gl::ViewportWidget::moveStarted);
    QTest::mouseClick(preview, Qt::LeftButton);
    REQUIRE(model->previewCamera() == camera);
    const auto previewImage = viewport->grabFramebuffer();
    REQUIRE_FALSE(previewImage.isNull());
    REQUIRE(previewImage != editorImage);
    viewport->setFocus();
    const auto center = viewport->rect().center();
    QWheelEvent wheel(center, viewport->mapToGlobal(center), {}, {0, 120}, Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(viewport, &wheel);
    QTest::mousePress(viewport, Qt::MiddleButton, Qt::NoModifier, center);
    QTest::mouseMove(viewport, center + QPoint(30, 20));
    QTest::mouseRelease(viewport, Qt::MiddleButton);
    QTest::mouseClick(viewport, Qt::LeftButton);
    REQUIRE_FALSE(viewport->focusSelection());
    REQUIRE(navigation.count() == 0);
    REQUIRE(picking.count() == 0);
    REQUIRE(moving.count() == 0);
    REQUIRE(model->editorCamera() == observer);
    REQUIRE(model->undoStack()->count() == history);
    REQUIRE(viewport->grabFramebuffer() == previewImage);
    fov->setValue(30);
    REQUIRE(viewport->grabFramebuffer() != previewImage);
    model->undo();
    REQUIRE(viewport->grabFramebuffer() == previewImage);
    auto* farClip = window.findChild<QDoubleSpinBox*>(QStringLiteral("CameraFar"));
    farClip->setValue(1);
    REQUIRE(viewport->grabFramebuffer() != previewImage);
    model->undo();
    REQUIRE(viewport->grabFramebuffer() == previewImage);
    QTest::keyClick(viewport, Qt::Key_Escape);
    REQUIRE(model->previewCamera() == 0);
    REQUIRE(viewport->grabFramebuffer() == editorImage);
    REQUIRE_FALSE(exit->isEnabled());
    window.findChild<QAction*>(QStringLiteral("CreateDirectionalLight"))->trigger();
    const auto light = model->selection()->selectedEntity();
    REQUIRE(model->scene()->find(light)->light.has_value());
    REQUIRE_FALSE(
        window.findChild<QDoubleSpinBox*>(QStringLiteral("LightDirection0"))->isEnabled());
    auto* intensity = window.findChild<QDoubleSpinBox*>(QStringLiteral("LightIntensity"));
    const auto lit = viewport->grabFramebuffer();
    intensity->setValue(0);
    REQUIRE(model->scene()->find(light)->light->intensity == 0);
    REQUIRE(viewport->grabFramebuffer() != lit);
    model->undo();
    REQUIRE(viewport->grabFramebuffer() == lit);
    window.findChild<QDoubleSpinBox*>(QStringLiteral("LightColor0"))->setValue(0);
    REQUIRE(model->scene()->find(light)->light->color.x == 0);
    REQUIRE(viewport->grabFramebuffer() != lit);
    model->undo();
    REQUIRE(model->setTransformComponent(light, 1, 1, 160));
    REQUIRE(viewport->grabFramebuffer() != lit);
    model->undo();
    REQUIRE(viewport->grabFramebuffer() == lit);
    REQUIRE(model->setVisible(light, false));
    REQUIRE(viewport->grabFramebuffer() != lit);
    model->undo();
    REQUIRE(viewport->grabFramebuffer() == lit);
    model->selection()->setSelectedEntity(camera);
    REQUIRE(model->setPreviewCamera(camera));
    QTemporaryDir directory;
    const auto path = directory.filePath("camera-light.m3dscene");
    REQUIRE(model->saveScene(path));
    const auto savedImage = viewport->grabFramebuffer();
    REQUIRE(model->openScene(path));
    REQUIRE(model->previewCamera() == 0);
    model->selection()->setSelectedEntity(camera);
    REQUIRE(model->setPreviewCamera(camera));
    REQUIRE(viewport->grabFramebuffer() == savedImage);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_DEVICE_CAPTURE");
    if (!capture.isEmpty()) {
        scroll->ensureWidgetVisible(preview);
        QTest::qWait(100);
        REQUIRE(window.grab().save(capture));
    }
    scroll->ensureWidgetVisible(exit);
    QTest::mouseClick(exit, Qt::LeftButton);
    REQUIRE(model->previewCamera() == 0);
    REQUIRE_FALSE(model->isModified());
    REQUIRE(window.close());
}

TEST_CASE("Unsaved prompts preserve edits when cancelled and only close after successful save",
          "[document-ui]") {
    QTemporaryDir directory;
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    answerUnsaved(QMessageBox::Cancel);
    window.findChild<QAction*>(QStringLiteral("NewScene"))->trigger();
    REQUIRE(model->scene()->find(id) != nullptr);
    answerUnsaved(QMessageBox::Cancel);
    REQUIRE_FALSE(window.close());
    REQUIRE(window.isVisible());
    answerUnsaved(QMessageBox::Save, true);
    REQUIRE_FALSE(window.close());
    REQUIRE(model->isModified());
    REQUIRE(model->filePath().isEmpty());
    REQUIRE(model->saveScene(directory.filePath("saved.m3dscene")));
    REQUIRE(model->setTransformComponent(id, 0, 0, 3));
    answerUnsaved(QMessageBox::Save);
    REQUIRE(window.close());
    REQUIRE_FALSE(model->isModified());
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(directory.filePath("saved.m3dscene")));
    REQUIRE(reopened.scene()->find(id)->transform.position.x == 3);
}
TEST_CASE("Failed save blocks closing and discard remains an explicit choice", "[document-ui]") {
    QTemporaryDir directory;
    REQUIRE(QDir(directory.path()).mkdir("project"));
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->saveScene(directory.filePath("project/saved.m3dscene")));
    REQUIRE(QDir(directory.path()).rename("project", "moved"));
    REQUIRE(model->setTransformComponent(id, 0, 0, 2));
    answerUnsaved(QMessageBox::Save);
    REQUIRE_FALSE(window.close());
    REQUIRE(model->isModified());
    REQUIRE(model->scene()->find(id)->transform.position.x == 2);
    answerUnsaved(QMessageBox::Discard);
    REQUIRE(window.close());
}
