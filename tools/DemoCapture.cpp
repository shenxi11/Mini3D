/*
 * 模块名: DemoCapture
 * 功能概述: 在真实主窗口回放导入、外观、导航、手柄、历史与文档操作并保存关键帧。
 * 对外接口: main，参数为 BoxTextured.glb 和不存在的帧输出目录。
 * 依赖关系: Qt Test、编辑器 UI、真实 OpenGL；不修改用户场景。
 * 输入输出: 临时项目和真实窗口到 640 张 PNG，供 5 fps 演示编码。
 * 异常与错误: 业务状态、窗口或帧写入失败立即非零退出。
 * 维护说明: 自动化回放，不宣称人工实时操作录像；不随用户包部署。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/EditorCamera.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QMouseEvent>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace mini3d;
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
} // namespace
int main(int argc, char* argv[]) {
    QSurfaceFormat::setDefaultFormat(renderer_gl::ViewportWidget::defaultSurfaceFormat());
    QApplication application(argc, argv);
    try {
        require(argc == 3, "Usage: mini3d_demo BoxTextured.glb new-frame-directory");
        const auto output = QString::fromLocal8Bit(argv[2]);
        require(!QDir(output).exists() && QDir().mkpath(output), "Frame output must be new");
        QTemporaryDir project;
        require(project.isValid(), "Temporary project failed");
        const auto sample = project.filePath("model.glb");
        require(QFile::copy(QString::fromLocal8Bit(argv[1]), sample), "Sample copy failed");
        auto window = std::make_unique<editor::MainWindow>();
        window->show();
        window->activateWindow();
        require(QTest::qWaitForWindowActive(window.get()), "Window activation failed");
        auto* model = window->findChild<editor::SceneViewModel*>();
        auto* viewport = window->findChild<renderer_gl::ViewportWidget*>();
        int frame = 0;
        const auto capture = [&](const QString& title, int count) {
            window->statusBar()->showMessage(title);
            for (int i = 0; i < count; ++i) {
                QTest::qWait(20);
                require(window->grab().save(QDir(output).filePath(
                            QStringLiteral("%1.png").arg(frame++, 4, 10, QChar('0')))),
                        "Frame save failed");
            }
        };
        capture(QStringLiteral("1/8  Mini3D Studio - C++20 / Qt 6 / OpenGL - automated replay"),
                80);
        model->newScene();
        const auto root = model->importGltf(sample);
        require(root != 0, "Import failed");
        window->findChild<QTreeView*>(QStringLiteral("SceneTree"))->expandAll();
        capture(QStringLiteral("2/8  Import static GLB: shared resources and editable hierarchy"),
                80);
        core::EntityId mesh = 0;
        for (const auto& node : model->scene()->nodes()) {
            if (node.meshRenderer) {
                mesh = node.id;
                break;
            }
        }
        require(mesh != 0, "Imported geometry missing");
        model->selection()->setSelectedEntity(mesh);
        window->findChild<QDoubleSpinBox*>(QStringLiteral("Tint0"))->setValue(0.4);
        window->findChild<QDoubleSpinBox*>(QStringLiteral("LightIntensity"))->setValue(0.9);
        require(model->scene()->find(mesh)->surface.tint.x == 0.4F, "Inspector edit failed");
        capture(QStringLiteral("3/8  Per-instance tint and global directional lighting"), 80);
        viewport->setFocus();
        const auto center = viewport->rect().center();
        QTest::mousePress(viewport, Qt::MiddleButton, Qt::NoModifier, center);
        for (int i = 1; i <= 40; ++i) {
            const auto point = center + QPoint(i * 2, i / 2);
            QMouseEvent move(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                             Qt::MiddleButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &move);
            capture(QStringLiteral("4/8  Middle mouse orbits the editor camera"), 2);
        }
        QTest::mouseRelease(viewport, Qt::MiddleButton, Qt::NoModifier, center + QPoint(80, 20));
        renderer_gl::EditorCamera camera;
        require(camera.setState(model->editorCamera()), "Camera state invalid");
        camera.setViewportSize(viewport->width(), viewport->height());
        window->findChild<QAction*>(QStringLiteral("MoveTool"))->setChecked(true);
        const auto world = glm::vec3(model->scene()->worldMatrix(mesh)[3]);
        const auto clip =
            camera.viewProjectionMatrix() *
            glm::vec4(world + glm::vec3(camera.worldUnitsPerPixel(world) * 54, 0, 0), 1);
        const QPoint start(qRound((clip.x / clip.w + 1) * viewport->width() * 0.5F),
                           qRound((1 - clip.y / clip.w) * viewport->height() * 0.5F));
        const auto before = model->scene()->find(mesh)->transform.position;
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, start);
        for (int i = 1; i <= 40; ++i) {
            const auto point = start + QPoint(i, 0);
            QMouseEvent move(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewport, &move);
            capture(QStringLiteral("5/8  World X handle: drag previews one transform transaction"),
                    2);
        }
        QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, start + QPoint(40, 0));
        require(model->scene()->find(mesh)->transform.position != before, "Handle did not move");
        QTest::keyClick(viewport, Qt::Key_Z, Qt::ControlModifier);
        require(model->scene()->find(mesh)->transform.position == before, "Undo failed");
        capture(QStringLiteral("6/8  Ctrl+Z restores the pre-drag position"), 40);
        QTest::keyClick(viewport, Qt::Key_Y, Qt::ControlModifier);
        require(model->scene()->find(mesh)->transform.position != before, "Redo failed");
        capture(QStringLiteral("6/8  Ctrl+Y restores the committed drag"), 40);
        const auto file = project.filePath("demo.m3dscene");
        require(model->saveScene(file), "Save failed");
        require(window->close(), "Saved document did not close");
        window = std::make_unique<editor::MainWindow>();
        model = window->findChild<editor::SceneViewModel*>();
        require(model->openScene(file), "Reopen failed");
        window->show();
        window->activateWindow();
        require(QTest::qWaitForWindowActive(window.get()), "Reopened window inactive");
        model->selection()->setSelectedEntity(mesh);
        window->findChild<QTreeView*>(QStringLiteral("SceneTree"))->expandAll();
        capture(QStringLiteral(
                    "7/8  Saved, closed, and reopened: hierarchy, appearance and camera restored"),
                80);
        const auto count = model->scene()->nodes().size();
        require(!model->openScene(project.filePath("missing.m3dscene")), "Missing file accepted");
        require(model->scene()->nodes().size() == count, "Failure damaged document");
        capture(QStringLiteral(
                    "8/8  Invalid file preserves current scene - local candidate, not final V1"),
                80);
        require(frame == 640, "Unexpected frame count");
        std::cout << "Captured " << frame << " validated frames.\n";
        window->hide();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
