/*
 * 模块名: editor_application
 * 功能概述: 创建 Qt 应用、启动顶层窗口，并提供显式启用的帧缓冲验证入口。
 * 对外接口: main
 * 依赖关系: Qt 6 Widgets、mini3d::editor::MainWindow、OpenGL Viewport 格式配置
 * 输入输出: 输入命令行与可选验证环境变量，输出事件循环退出码或视口 PNG。
 * 异常与错误: 验证截图失败时记录错误并以非零码退出；普通初始化错误由 Qt 报告。
 * 维护说明: 默认 SurfaceFormat 必须在 QApplication 前设置；验证分支默认不启用。
 */

#include "MainWindow.h"
#include "SceneViewModel.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QImage>
#include <QOpenGLWidget>
#include <QPixmap>
#include <QSurfaceFormat>
#include <QTimer>

namespace {

constexpr int kValidationCaptureDelayMilliseconds = 1500;

void scheduleValidationCapture(mini3d::editor::MainWindow& mainWindow) {
    const QString capturePath = qEnvironmentVariable("MINI3D_VALIDATION_CAPTURE");
    if (capturePath.isEmpty()) {
        return;
    }

    QTimer::singleShot(
        kValidationCaptureDelayMilliseconds, &mainWindow, [&mainWindow, capturePath]() {
            auto* viewport = mainWindow.findChild<QOpenGLWidget*>(QStringLiteral("ViewportWidget"));
            if (viewport == nullptr) {
                qCritical() << "验证截图未找到视口。";
                QCoreApplication::exit(2);
                return;
            }

            const QImage framebuffer = viewport->grabFramebuffer();
            if (framebuffer.isNull() || !framebuffer.save(capturePath)) {
                qCritical().noquote() << QStringLiteral("验证截图保存失败：%1").arg(capturePath);
                QCoreApplication::exit(3);
                return;
            }

            // 仅在既有截图验证模式下，额外保存完整界面以核对部署后的中文。
            const auto uiCapture = qEnvironmentVariable("MINI3D_VALIDATION_UI_CAPTURE");
            if (!uiCapture.isEmpty() && !mainWindow.grab().save(uiCapture)) {
                qCritical().noquote() << QStringLiteral("验证界面截图保存失败：%1").arg(uiCapture);
                QCoreApplication::exit(5);
                return;
            }

            qInfo().noquote() << QStringLiteral("验证帧缓冲已保存：%1（%2×%3）")
                                     .arg(capturePath)
                                     .arg(framebuffer.width())
                                     .arg(framebuffer.height());
            mainWindow.close();
        });
}

} // namespace

int main(int argc, char* argv[]) {
    QSurfaceFormat::setDefaultFormat(mini3d::renderer_gl::ViewportWidget::defaultSurfaceFormat());

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Mini3D Studio"));
    QCoreApplication::setApplicationName(QStringLiteral("Mini3D Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    mini3d::editor::MainWindow mainWindow;
    // 仅截图验收模式加载指定场景，供部署包从独立目录验证资源相对路径。
    if (!qEnvironmentVariableIsEmpty("MINI3D_VALIDATION_CAPTURE")) {
        const auto scene = qEnvironmentVariable("MINI3D_VALIDATION_SCENE");
        if (!scene.isEmpty()) {
            auto* model = mainWindow.findChild<mini3d::editor::SceneViewModel*>();
            if (!model || !model->openScene(scene)) {
                qCritical().noquote() << "验证场景打开失败：" << scene;
                return 4;
            }
        }
    }
    mainWindow.show();
    scheduleValidationCapture(mainWindow);

    return application.exec();
}
