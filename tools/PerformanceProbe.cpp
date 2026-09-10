/*
 * 模块名: PerformanceProbe
 * 功能概述: 测量启动、导入、文档 IO、离屏同步帧和重复资源/历史操作。
 * 对外接口: main，参数为样例路径、JSON 输出、可选稳定性秒数。
 * 依赖关系: Qt、真实 OpenGL 驱动、Windows 工作集统计、现有编辑器模块。
 * 输入输出: 固定样例和合成实例到带机器/方法信息的 JSON。
 * 异常与错误: Release 限制、GL 错误或状态不一致以非零码退出。
 * 维护说明: 不等价于屏幕 FPS；glFinish 包括 CPU 提交与 GPU 完成等待。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/PrimitiveFactory.h"
#include "renderer_gl/Renderer.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_4_1_Core>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
// Windows SDK 的 psapi.h 依赖 windows.h 先定义 WINAPI 与 DWORD。
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

#ifdef NDEBUG
using namespace mini3d;
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
double milliseconds(QElapsedTimer& timer) {
    return static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
}
QJsonObject memory() {
    PROCESS_MEMORY_COUNTERS counters{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)),
            "Working-set query failed");
    return {
        {"workingSetMiB", static_cast<double>(counters.WorkingSetSize) / 1048576.0},
        {"processPeakWorkingSetMiB", static_cast<double>(counters.PeakWorkingSetSize) / 1048576.0}};
}
} // namespace

int main(int argc, char* argv[]) {
    QElapsedTimer startup;
    startup.start();
    QSurfaceFormat::setDefaultFormat(renderer_gl::ViewportWidget::defaultSurfaceFormat());
    QApplication application(argc, argv);
    Q_INIT_RESOURCE(renderer_shaders);
    try {
        require(argc == 3 || argc == 4,
                "Usage: mini3d_performance model.glb output.json [seconds]");
        const auto sample = QString::fromLocal8Bit(argv[1]);
        const auto output = QString::fromLocal8Bit(argv[2]);
        require(!QFileInfo::exists(output), "Output exists; choose a new report path");
        bool validSeconds = true;
        const int seconds = argc == 4 ? QString::fromLatin1(argv[3]).toInt(&validSeconds) : 0;
        require(validSeconds && seconds >= 0 && seconds <= 3600, "Invalid soak duration");
        QJsonObject report{
            {"method", "Release; physical 1920x1080 FBO; glFinish each frame; no vsync"},
            {"qt", qVersion()},
            {"sampleBytes", QFileInfo(sample).size()}};
        {
            editor::MainWindow window;
            QEventLoop ready;
            bool presented = false;
            auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
            QObject::connect(viewport, &QOpenGLWidget::frameSwapped, &ready, [&] {
                presented = true;
                ready.quit();
            });
            QTimer::singleShot(10000, &ready, &QEventLoop::quit);
            window.show();
            ready.exec();
            require(presented, "Startup first frame timed out");
            report["mainToDemoFirstFrameMs"] = milliseconds(startup);
            window.hide();
        }
        QOpenGLContext context;
        context.setFormat(QSurfaceFormat::defaultFormat());
        require(context.create(), "Context creation failed");
        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        require(surface.isValid() && context.makeCurrent(&surface), "Offscreen surface failed");
        QOpenGLFunctions_4_1_Core gl;
        require(gl.initializeOpenGLFunctions(), "OpenGL 4.1 functions unavailable");
        report["gpu"] = reinterpret_cast<const char*>(gl.glGetString(GL_RENDERER));
        report["openGL"] = reinterpret_cast<const char*>(gl.glGetString(GL_VERSION));
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        QOpenGLFramebufferObject framebuffer(1920, 1080, format);
        require(framebuffer.isValid() && framebuffer.bind(), "FBO failed");
        gl.glViewport(0, 0, 1920, 1080);
        renderer_gl::Renderer renderer;
        require(renderer.initialize(gl), "Renderer initialization failed");
        renderer.resize(1920, 1080);
        editor::SceneViewModel model;
        model.newScene();
        QElapsedTimer timer;
        timer.start();
        const auto imported = model.importGltf(sample);
        require(imported != 0, "Sample import failed");
        report["importMs"] = milliseconds(timer);
        report["memoryAfterImport"] = memory();
        std::size_t triangles = 0;
        for (const auto& node : model.scene()->nodes()) {
            if (node.meshRenderer) {
                triangles += model.assets()->mesh(node.meshRenderer->mesh)->data.indices.size() / 3;
            }
        }
        report["sampleTriangles"] = static_cast<qint64>(triangles);
        require(renderer.focusEntity(*model.scene(), *model.assets(), imported), "Focus failed");
        timer.restart();
        renderer.render(*model.scene(), *model.assets());
        gl.glFinish();
        report["firstImportedFrameIncludingUploadMs"] = milliseconds(timer);
        require(!framebuffer.toImage().isNull(), "Rendered framebuffer unavailable");
        renderer.clearImportedResources();
        model.newScene();
        const auto perSphere = renderer_gl::PrimitiveFactory::createSphere().indices.size() / 3;
        QJsonArray cases;
        for (const std::size_t requested : {100000, 500000, 1000000}) {
            core::Scene scene;
            const auto group = scene.createEntity("Load");
            const auto count = (requested + perSphere - 1) / perSphere;
            const auto side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
            for (std::size_t i = 0; i < count; ++i) {
                const auto id = scene.createEntity("Sphere", group, core::PrimitiveKind::Sphere);
                core::Transform transform;
                transform.position = {1.5F * static_cast<float>(i % side), 0,
                                      1.5F * static_cast<float>(i / side)};
                require(scene.setTransform(id, transform), "Synthetic transform failed");
            }
            require(renderer.focusEntity(scene, *model.assets(), group), "Synthetic focus failed");
            std::vector<double> frames;
            for (int frame = -10; frame < 120; ++frame) {
                timer.restart();
                renderer.render(scene, *model.assets());
                gl.glFinish();
                if (frame >= 0) {
                    frames.push_back(milliseconds(timer));
                }
            }
            require(gl.glGetError() == GL_NO_ERROR, "OpenGL error during frame measurement");
            const double average =
                std::accumulate(frames.begin(), frames.end(), 0.0) / frames.size();
            const double worst = *std::max_element(frames.begin(), frames.end());
            cases.append(QJsonObject{{"triangles", static_cast<qint64>(count * perSphere)},
                                     {"geometryDraws", static_cast<qint64>(count)},
                                     {"samples", 120},
                                     {"meanMs", average},
                                     {"worstMs", worst},
                                     {"equivalentMeanFps", 1000 / average},
                                     {"equivalentMinimumFps", 1000 / worst}});
        }
        report["syntheticFrames"] = cases;
        QTemporaryDir directory;
        require(directory.isValid(), "Temporary project unavailable");
        for (int i = 0; i < 100; ++i) {
            require(model.createEntity(core::PrimitiveKind::Cube) != 0, "Create failed");
        }
        timer.restart();
        require(model.saveScene(directory.filePath("hundred.m3dscene")), "Save failed");
        report["save100EntitiesMs"] = milliseconds(timer);
        timer.restart();
        require(model.openScene(directory.filePath("hundred.m3dscene")), "Open failed");
        report["open100EntitiesMs"] = milliseconds(timer);
        report["memoryBeforeSoak"] = memory();
        // 压力循环保留 warning/error，避免重复导入的 info 输出主导耗时。
        QLoggingCategory::setFilterRules(QStringLiteral("*.info=false"));
        QElapsedTimer soak;
        soak.start();
        int cycles = 0;
        while (soak.elapsed() < static_cast<qint64>(seconds) * 1000) {
            renderer.clearImportedResources();
            model.newScene();
            const auto id = model.importGltf(sample);
            require(id != 0, "Soak import failed");
            model.deleteSelected();
            model.undo();
            require(model.scene()->find(id) != nullptr, "Soak undo lost entity");
            model.redo();
            require(model.scene()->roots().empty(), "Soak redo failed");
            model.undo();
            require(renderer.focusEntity(*model.scene(), *model.assets(), id), "Soak focus failed");
            renderer.render(*model.scene(), *model.assets());
            gl.glFinish();
            require(gl.glGetError() == GL_NO_ERROR, "OpenGL error during soak");
            ++cycles;
        }
        report["soak"] = QJsonObject{
            {"seconds", milliseconds(soak) / 1000}, {"cycles", cycles}, {"memoryAfter", memory()}};
        renderer.destroy();
        QSaveFile file(output);
        require(file.open(QIODevice::WriteOnly), "Report open failed");
        const auto bytes = QJsonDocument(report).toJson();
        require(file.write(bytes) == bytes.size() && file.commit(), "Report commit failed");
        std::cout << bytes.constData();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#else
int main() {
    std::cerr << "Run the Release build for performance measurements.\n";
    return 2;
}
#endif
