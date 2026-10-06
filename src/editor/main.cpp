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
#include "automation/LocalAutomationBridge.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QOpenGLWidget>
#include <QPixmap>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTimer>
#include <QUrl>

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

            // 只读核对真实 F1 动作的落点，不在烟测时启动用户浏览器。
            const auto* help = mainWindow.findChild<QAction*>(QStringLiteral("OpenUserGuide"));
            const auto guide = help ? help->data().toUrl() : QUrl{};
            if (!guide.isLocalFile() || !QFileInfo::exists(guide.toLocalFile())) {
                qCritical() << "验证离线帮助文件缺失。";
                QCoreApplication::exit(6);
                return;
            }
            qInfo().noquote() << "MINI3D_VALIDATION_HELP"
                              << QDir::toNativeSeparators(guide.toLocalFile());

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

    // 显式验收实例隔离布局/键位偏好，普通启动仍使用既有用户设置。
    const auto validationSettings = qEnvironmentVariable("MINI3D_VALIDATION_SETTINGS");
    if (!validationSettings.isEmpty()) {
        const QFileInfo directory(validationSettings);
        if (!directory.isAbsolute() || !directory.isDir()) {
            qCritical() << "验收偏好目录必须是已存在的绝对目录。";
            return 12;
        }
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           directory.absoluteFilePath());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                           directory.absoluteFilePath());
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Mini3D Studio 本机建模工作台"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption automationOption("automation", QStringLiteral("显式开启同用户本机自动化桥"));
    const QCommandLineOption descriptorOption("automation-descriptor", QStringLiteral("新建受限实例描述文件；父目录须已存在"), "path");
    const QCommandLineOption readRootOption("read-root", QStringLiteral("批准的现有读取根，可重复指定"), "directory");
    const QCommandLineOption writeRootOption("write-root", QStringLiteral("批准的新文件写入根，可重复指定"), "directory");
    parser.addOptions({automationOption, descriptorOption, readRootOption, writeRootOption});
    parser.process(application);
    const bool automationEnabled = parser.isSet(automationOption);
    if ((!automationEnabled && (parser.isSet(descriptorOption) || parser.isSet(readRootOption) || parser.isSet(writeRootOption))) ||
        (automationEnabled && parser.value(descriptorOption).isEmpty())) {
        qCritical() << "自动化必须显式开启并指定新的实例描述文件，读写根不能在关闭状态提供。";
        return 10;
    }

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
    if (automationEnabled) {
        mini3d::editor::automation::LocalAutomationBridge::Options options;
        options.enabled = true;
        options.descriptorPath = parser.value(descriptorOption);
        options.readRoots = parser.values(readRootOption);
        options.writeRoots = parser.values(writeRootOption);
        options.permissions = {"scene.read", "scene.write", "viewport.observe", "viewport.control"};
        if (!options.readRoots.isEmpty())
            options.permissions.append("file.read");
        if (!options.writeRoots.isEmpty())
            options.permissions.append("file.write");
        QString error;
        if (!mainWindow.automationBridge().start(options, error)) {
            qCritical().noquote() << "本机自动化桥启动失败：" << error;
            return 11;
        }
    }
    scheduleValidationCapture(mainWindow);

    return application.exec();
}
