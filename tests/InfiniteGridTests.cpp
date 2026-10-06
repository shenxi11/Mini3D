/*
 * 模块名: InfiniteGridTests
 * 功能概述: 在真实 OpenGL 驱动验证无限网格、深度、相机边界与 GPU 成本。
 * 对外接口: Catch2 [infinite-grid] / [infinite-grid-performance]
 * 依赖关系: Qt OpenGL、GridRenderer、Renderer
 * 输入输出: 实际 FBO 像素、深度与 GPU 时间查询。
 * 异常与错误: 驱动、渲染或断言失败不跳过。
 * 维护说明: 性能专项显式运行；不读取桌面、不写用户偏好。
 */
#include "renderer_gl/EditorCamera.h"
#include "renderer_gl/GridRenderer.h"
#include "renderer_gl/Renderer.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_4_1_Core>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

using namespace mini3d;

namespace {
struct GridContext {
    QOpenGLContext context;
    QOffscreenSurface surface;
    QOpenGLFunctions_4_1_Core gl;
    renderer_gl::GridRenderer grid;
    GridContext() {
        QSurfaceFormat format;
        format.setVersion(4, 1);
        format.setProfile(QSurfaceFormat::CoreProfile);
        context.setFormat(format);
        REQUIRE(context.create());
        surface.setFormat(context.format());
        surface.create();
        REQUIRE(surface.isValid());
        REQUIRE(context.makeCurrent(&surface));
        REQUIRE(gl.initializeOpenGLFunctions());
        REQUIRE(grid.initialize(gl));
    }
    ~GridContext() { grid.destroy(); }
    void clear(int width, int height, double depth = 1) {
        gl.glViewport(0, 0, width, height);
        gl.glEnable(GL_DEPTH_TEST);
        gl.glDepthFunc(GL_LESS);
        gl.glDepthMask(GL_TRUE);
        gl.glDisable(GL_BLEND);
        gl.glDisable(GL_CULL_FACE);
        gl.glClearColor(0, 0, 0, 1);
        gl.glClearDepth(depth);
        gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
};
int colored(const QImage& frame, const QRect& area) {
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if ((frame.pixel(x, y) & 0xffffff) != 0)
                ++count;
    return count;
}
}

TEST_CASE("Infinite floor remains visible beyond the old extent under both projections",
          "[gpu][infinite-grid]") {
    GridContext gpu;
    QOpenGLFramebufferObject frame(512, 384, QOpenGLFramebufferObject::CombinedDepthStencil);
    REQUIRE(frame.isValid());
    REQUIRE(frame.bind());
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(512, 384);
    for (const auto target : {glm::vec3(0), glm::vec3(250, 0, -400),
                              glm::vec3(100000, 0, -100000)}) {
        for (const float distance : {0.6F, 6.2F, 50.0F}) {
            CAPTURE(target.x, target.z, distance);
            REQUIRE(camera.setState({target + glm::vec3(0, 0, distance), target}));
            camera.setView(renderer_gl::EditorView::Top);
            for (const bool orthographic : {false, true}) {
                CAPTURE(orthographic);
                camera.setOrthographic(orthographic);
                gpu.clear(512, 384);
                gpu.grid.draw(camera);
                const auto image = frame.toImage();
                REQUIRE(colored(image, image.rect()) > 400);
                for (const auto corner : {QRect(0, 0, 80, 80), QRect(432, 0, 80, 80),
                                           QRect(0, 304, 80, 80), QRect(432, 304, 80, 80)})
                    REQUIRE(colored(image, corner) > 30);
                REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
            }
        }
    }
    // 自由观察、平移和缩放使用同一网格，不限制到世界原点。
    REQUIRE(camera.setState({{255, 4, -392}, {250, 0, -400}}));
    camera.setOrthographic(false);
    for (int step = 0; step < 12; ++step) {
        camera.orbit(7, -2);
        camera.pan(19, 3);
        camera.zoom(step % 2 ? -1 : 1);
        gpu.clear(512, 384);
        gpu.grid.draw(camera);
        const auto image = frame.toImage();
        REQUIRE(colored(image, QRect(0, 192, 512, 192)) > 200);
        REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
    }
}

TEST_CASE("Infinite floor honors depth and leaves scene depth unchanged near the horizon",
          "[gpu][infinite-grid]") {
    GridContext gpu;
    QOpenGLFramebufferObject frame(512, 384, QOpenGLFramebufferObject::CombinedDepthStencil);
    REQUIRE(frame.bind());
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(512, 384);
    REQUIRE(camera.setState({{250, 0, -394}, {250, 0, -400}}));
    camera.setView(renderer_gl::EditorView::Top);
    camera.setOrthographic(true);
    gpu.clear(512, 384, 0);
    gpu.grid.draw(camera);
    REQUIRE(colored(frame.toImage(), QRect(0, 0, 512, 384)) == 0);
    gpu.clear(512, 384, 1);
    gpu.grid.draw(camera);
    REQUIRE(colored(frame.toImage(), QRect(0, 0, 512, 384)) > 400);
    std::vector<float> depths(512 * 384);
    gpu.gl.glReadPixels(0, 0, 512, 384, GL_DEPTH_COMPONENT, GL_FLOAT, depths.data());
    REQUIRE(std::all_of(depths.begin(), depths.end(), [](float d) { return d == 1; }));
    REQUIRE(gpu.gl.glIsEnabled(GL_BLEND) == GL_FALSE);
    GLboolean depthMask = GL_FALSE;
    gpu.gl.glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    REQUIRE(depthMask == GL_TRUE);
    for (const float height : {0.0F, .0001F, -.0001F, .1F, -.1F}) {
        REQUIRE(camera.setState({{250, height, -390}, {250, 0, -400}}));
        for (const bool orthographic : {false, true}) {
            camera.setOrthographic(orthographic);
            gpu.clear(512, 384);
            gpu.grid.draw(camera);
            const auto image = frame.toImage();
            REQUIRE_FALSE(image.isNull());
            if (std::abs(height) == .1F) {
                REQUIRE(colored(image, image.rect()) > 30);
                const auto path = qEnvironmentVariable("MINI3D_TEST_GRID_ARTIFACT_DIR");
                if (!path.isEmpty()) {
                    REQUIRE(image.save(QDir(path).filePath(
                        QStringLiteral("horizon-%1-%2.png").arg(height > 0 ? "above" : "below")
                            .arg(orthographic ? "ortho" : "perspective"))));
                }
            }
            REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
        }
    }
    camera.setView(renderer_gl::EditorView::Front);
    camera.setOrthographic(true);
    gpu.clear(512, 384);
    gpu.grid.draw(camera);
    REQUIRE(colored(frame.toImage(), QRect(0, 0, 512, 384)) == 0);
    gpu.grid.destroy();
    REQUIRE_FALSE(gpu.grid.isValid());
    REQUIRE(gpu.grid.initialize(gpu.gl));
    REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
}

TEST_CASE("Fractional distant pans preserve the same world grid phase as local pans",
          "[gpu][infinite-grid]") {
    GridContext gpu;
    QOpenGLFramebufferObject frame(512, 384, QOpenGLFramebufferObject::CombinedDepthStencil);
    REQUIRE(frame.bind());
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(512, 384);
    for (const float fraction : {.015625F, .0234375F}) {
        const auto render = [&](float offset) {
            const glm::vec3 target(10 + fraction + offset, 0, -10 - offset);
            REQUIRE(camera.setState({target + glm::vec3(0, .6F, .015625F), target}));
            camera.setView(renderer_gl::EditorView::Top);
            camera.setOrthographic(true);
            gpu.clear(512, 384);
            gpu.grid.draw(camera);
            return frame.toImage();
        };
        const auto local = render(0);
        const auto distant = render(100000);
        double difference = 0;
        for (int y = 0; y < local.height(); ++y)
            for (int x = 0; x < local.width(); ++x)
                difference += std::abs(qRed(local.pixel(x, y)) - qRed(distant.pixel(x, y)));
        difference /= local.width() * local.height();
        CAPTURE(fraction, difference);
        REQUIRE(difference < .1);
        REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
    }
}

TEST_CASE("Geometry above the floor occludes the grid without changing material pixels",
          "[gpu][infinite-grid]") {
    GridContext gpu;
    QOpenGLFramebufferObject frame(512, 384, QOpenGLFramebufferObject::CombinedDepthStencil);
    REQUIRE(frame.bind());
    renderer_gl::Renderer renderer;
    REQUIRE(renderer.initialize(gpu.gl));
    renderer.resize(512, 384);
    core::Scene scene;
    assets::AssetManager assets;
    const auto cube = scene.createEntity("Cube", 0, core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position = {0, 1, 0};
    REQUIRE(scene.setTransform(cube, transform));
    REQUIRE(renderer.focusEntity(scene, assets, cube));
    renderer.setCameraView(renderer_gl::EditorView::Top);
    renderer.setOrthographic(true);
    const auto render = [&](bool overlays) {
        gpu.gl.glViewport(0, 0, 512, 384);
        renderer.render(scene, assets, 0, false, -1, 0, renderer_gl::GizmoTool::None,
                        renderer_gl::GizmoSpace::World, nullptr, 5, overlays);
        return frame.toImage();
    };
    const auto plain = render(false);
    const auto grid = render(true);
    REQUIRE(plain.copy(246, 182, 20, 20) == grid.copy(246, 182, 20, 20));
    REQUIRE(plain != grid);
    REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
    renderer.destroy();
}

TEST_CASE("Infinite floor GPU cost is measured at normal and scaled viewport resolutions",
          "[.][gpu][infinite-grid-performance]") {
    GridContext gpu;
    QJsonArray results;
    for (const QSize size : {QSize(1280, 720), QSize(1920, 1080), QSize(2560, 1440)}) {
        QOpenGLFramebufferObject frame(size, QOpenGLFramebufferObject::CombinedDepthStencil);
        REQUIRE(frame.bind());
        renderer_gl::EditorCamera camera;
        camera.setViewportSize(size.width(), size.height());
        gpu.clear(size.width(), size.height());
        for (int i = 0; i < 20; ++i) gpu.grid.draw(camera);
        gpu.gl.glFinish();
        std::vector<double> samples;
        for (int sample = 0; sample < 40; ++sample) {
            GLuint query = 0;
            gpu.gl.glGenQueries(1, &query);
            gpu.gl.glBeginQuery(GL_TIME_ELAPSED, query);
            gpu.grid.draw(camera);
            gpu.gl.glEndQuery(GL_TIME_ELAPSED);
            GLuint64 elapsed = 0;
            gpu.gl.glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed);
            gpu.gl.glDeleteQueries(1, &query);
            samples.push_back(elapsed / 1.0e6);
        }
        std::sort(samples.begin(), samples.end());
        results.append(QJsonObject{{"width", size.width()}, {"height", size.height()},
                                    {"gpuMedianMs", samples[20]}, {"gpuP95Ms", samples[37]},
                                    {"samples", 40}, {"drawCalls", 2}, {"vertices", 5}});
        qInfo() << "Infinite grid GPU" << size << "median/p95 ms" << samples[20] << samples[37];
        REQUIRE(gpu.gl.glGetError() == GL_NO_ERROR);
        REQUIRE(samples[37] < 16.67);
    }
    const auto directory = qEnvironmentVariable("MINI3D_TEST_GRID_ARTIFACT_DIR");
    if (!directory.isEmpty()) {
        QFile output(QDir(directory).filePath(QStringLiteral("grid-performance.json")));
        REQUIRE(output.open(QIODevice::WriteOnly));
        QJsonObject report{{"gpuRenderer", QString::fromLatin1(reinterpret_cast<const char*>(
                                gpu.gl.glGetString(GL_RENDERER)))}, {"results", results}};
        REQUIRE(output.write(QJsonDocument(report).toJson()) > 0);
    }
}
