/*
 * 模块名: GpuTextureTests
 * 功能概述: 在离屏 OpenGL Context 验证纹理方向、材质切换和资源释放。
 * 对外接口: main、Catch2 测试
 * 依赖关系: Qt Gui/OpenGL、Catch2、renderer_gl
 * 输入输出: 上传四角颜色图，读回纹理和 FBO 并断言结果。
 * 异常与错误: Context 创建失败或 GPU 结果错误时测试失败，不跳过。
 * 维护说明: 需要真实 OpenGL 4.1 驱动；不依赖桌面截图或鼠标注入。
 */
#include "renderer_gl/ComponentOverlayRenderer.h"
#include "renderer_gl/GizmoRenderer.h"
#include "renderer_gl/GpuMesh.h"
#include "renderer_gl/GpuTexture.h"
#include "renderer_gl/Renderer.h"
#include "renderer_gl/SelectionRenderer.h"
#include "renderer_gl/ShaderProgram.h"

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_4_1_Core>
#include <QResource>
#include <array>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d::renderer_gl;

TEST_CASE("Component X-Ray bypasses depth only for overlay and restores GL state",
          "[gpu][component-overlay]") {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    REQUIRE(context.create());
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    REQUIRE(surface.isValid());
    REQUIRE(context.makeCurrent(&surface));
    QOpenGLFunctions_4_1_Core gl;
    REQUIRE(gl.initializeOpenGLFunctions());
    ComponentOverlayRenderer renderer;
    REQUIRE(renderer.initialize(gl));
    QOpenGLFramebufferObject framebuffer(32, 32, QOpenGLFramebufferObject::CombinedDepthStencil);
    REQUIRE(framebuffer.isValid());
    REQUIRE(framebuffer.bind());
    gl.glViewport(0, 0, 32, 32);
    ComponentOverlay overlay;
    overlay.revision = 1;
    overlay.triangles = {
        {{-1, -1, 0}, {1, 0, 0, 1}}, {{1, -1, 0}, {1, 0, 0, 1}}, {{0, 1, 0}, {1, 0, 0, 1}}};
    for (const bool depthEnabled : {false, true}) {
        for (const bool xRay : {false, true}) {
            gl.glDepthMask(GL_TRUE);
            gl.glClearColor(0, 0, 0, 1);
            gl.glClearDepth(0.25);
            gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            if (depthEnabled)
                gl.glEnable(GL_DEPTH_TEST);
            else
                gl.glDisable(GL_DEPTH_TEST);
            gl.glDepthFunc(GL_GREATER);
            gl.glDepthMask(GL_FALSE);
            gl.glEnable(GL_CULL_FACE);
            gl.glDisable(GL_BLEND);
            gl.glPointSize(3);
            renderer.draw(overlay, glm::mat4(1), 7, xRay);
            REQUIRE((gl.glIsEnabled(GL_DEPTH_TEST) == GL_TRUE) == depthEnabled);
            REQUIRE(gl.glIsEnabled(GL_CULL_FACE) == GL_TRUE);
            REQUIRE(gl.glIsEnabled(GL_BLEND) == GL_FALSE);
            GLint depthFunction = 0;
            GLboolean depthMask = GL_TRUE;
            GLfloat pointSize = 0;
            gl.glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
            gl.glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            gl.glGetFloatv(GL_POINT_SIZE, &pointSize);
            REQUIRE(depthFunction == GL_GREATER);
            REQUIRE(depthMask == GL_FALSE);
            REQUIRE(pointSize == 3);
            std::array<unsigned char, 4> color{};
            float depth = 0;
            gl.glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
            gl.glReadPixels(16, 16, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
            REQUIRE(color[0] == (xRay ? 255 : 0));
            REQUIRE(color[1] == 0);
            REQUIRE(std::abs(depth - 0.25F) < 1.0e-6F);
            REQUIRE(gl.glGetError() == GL_NO_ERROR);
        }
    }
    renderer.destroy();
    REQUIRE_FALSE(renderer.isValid());
    framebuffer.release();
}

TEST_CASE("GPU texture orientation material switching and lifetime", "[gpu]") {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    REQUIRE(context.create());
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    REQUIRE(surface.isValid());
    REQUIRE(context.makeCurrent(&surface));
    QOpenGLFunctions_4_1_Core gl;
    REQUIRE(gl.initializeOpenGLFunctions());

    QImage source(2, 2, QImage::Format_RGB888);
    source.setPixelColor(0, 0, Qt::red);
    source.setPixelColor(1, 0, Qt::blue);
    source.setPixelColor(0, 1, Qt::green);
    source.setPixelColor(1, 1, Qt::yellow);
    GpuTexture texture;
    REQUIRE_FALSE(texture.upload(gl, QImage()));
    REQUIRE_FALSE(texture.isValid());
    REQUIRE(texture.upload(gl, source));
    texture.bind();
    int textureId = 0;
    gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureId);
    REQUIRE(textureId != 0);
    std::array<unsigned char, 16> pixels{};
    gl.glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    // GL 首行为图像底边：绿、黄、红、蓝，Alpha 均为 255。
    REQUIRE(pixels == std::array<unsigned char, 16>{0, 255, 0, 255, 255, 255, 0, 255, 255, 0, 0,
                                                    255, 0, 0, 255, 255});
    int mipWidth = 0;
    gl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &mipWidth);
    REQUIRE(mipWidth == 1);
    REQUIRE_FALSE(texture.upload(gl, QImage()));
    REQUIRE(texture.isValid());

    ShaderProgram shader;
    REQUIRE(shader.create(gl, QStringLiteral(":/mini3d/shaders/mesh.vert"),
                          QStringLiteral(":/mini3d/shaders/mesh.frag")));
    MeshData quad;
    quad.vertices = {
        {{-1, -1, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0}},
        {{1, -1, 0}, {0, 1, 0}, {1, 0, 0}, {1, 0}},
        {{1, 1, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1}},
        {{-1, 1, 0}, {0, 1, 0}, {1, 0, 0}, {0, 1}},
    };
    quad.indices = {0, 1, 2, 0, 2, 3};
    GpuMesh mesh;
    REQUIRE(mesh.upload(gl, quad));
    QOpenGLFramebufferObject framebuffer(32, 32);
    REQUIRE(framebuffer.isValid());
    REQUIRE(framebuffer.bind());
    gl.glViewport(0, 0, 32, 32);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDisable(GL_BLEND);
    shader.bind();
    shader.setUniformMatrix4("uModel", glm::mat4(1.0F));
    shader.setUniformMatrix4("uViewProjection", glm::mat4(1.0F));
    shader.setUniformInt("uBaseColorTexture", 0);
    shader.setUniformVector3("uLightDirection", {0.45F, 1, 0.65F});
    shader.setUniformVector3("uLightColor", glm::vec3(0.72F));
    shader.setUniformVector3("uAmbient", glm::vec3(0.28F));
    shader.setUniformVector3("uBaseColor", glm::vec3(1.0F));
    shader.setUniformInt("uUseVertexColor", 0);
    shader.setUniformInt("uUseTexture", 1);
    texture.bind();
    mesh.draw();
    const QImage textured = framebuffer.toImage();
    const QColor topLeft = textured.pixelColor(8, 8);
    const QColor topRight = textured.pixelColor(24, 8);
    const QColor bottomLeft = textured.pixelColor(8, 24);
    REQUIRE(topLeft.red() > 180);
    REQUIRE(topLeft.green() < 20);
    REQUIRE(topRight.blue() > 180);
    REQUIRE(bottomLeft.green() > 180);

    shader.setUniformInt("uUseTexture", 0);
    shader.setUniformVector3("uBaseColor", {0.0F, 1.0F, 0.0F});
    mesh.draw();
    const QColor solid = framebuffer.toImage().pixelColor(16, 16);
    REQUIRE(solid.green() > 180);
    REQUIRE(solid.red() == 0);
    REQUIRE(solid.blue() == 0);
    shader.setUniformInt("uUseVertexColor", 1);
    shader.setUniformVector3("uBaseColor", {1.0F, 1.0F, 1.0F});
    mesh.draw();
    const QColor vertexColored = framebuffer.toImage().pixelColor(16, 16);
    REQUIRE(vertexColored.red() > 180);
    REQUIRE(vertexColored.green() == 0);
    shader.setUniformVector3("uLightDirection", {0, -1, 0});
    mesh.draw();
    REQUIRE(framebuffer.toImage().pixelColor(16, 16).red() < vertexColored.red());
    shader.setUniformVector3("uAmbient", glm::vec3(0));
    mesh.draw();
    REQUIRE(framebuffer.toImage().pixelColor(16, 16).red() == 0);
    REQUIRE(gl.glGetError() == GL_NO_ERROR);

    shader.release();
    texture.destroy();
    texture.destroy();
    REQUIRE_FALSE(texture.isValid());
    REQUIRE(gl.glIsTexture(static_cast<unsigned int>(textureId)) == GL_FALSE);
    REQUIRE(texture.upload(gl, source));
    REQUIRE(gl.glGetError() == GL_NO_ERROR);
    REQUIRE(texture.upload(gl, source, {9728, 9728, 33071, 33648}));
    texture.bind();
    int parameter = 0;
    gl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &parameter);
    REQUIRE(parameter == GL_NEAREST);
    gl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &parameter);
    REQUIRE(parameter == GL_NEAREST);
    gl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &parameter);
    REQUIRE(parameter == GL_CLAMP_TO_EDGE);
    gl.glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &parameter);
    REQUIRE(parameter == GL_MIRRORED_REPEAT);
    REQUIRE(gl.glGetError() == GL_NO_ERROR);
    // 局部 GPU 对象先析构，之后 Context 才析构。
}

TEST_CASE("Selection lines draw visible pixels and restore depth state", "[gpu][selection]") {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    REQUIRE(context.create());
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    REQUIRE(surface.isValid());
    REQUIRE(context.makeCurrent(&surface));
    QOpenGLFunctions_4_1_Core gl;
    REQUIRE(gl.initializeOpenGLFunctions());
    QOpenGLFramebufferObject framebuffer(64, 64, QOpenGLFramebufferObject::Depth);
    REQUIRE(framebuffer.isValid());
    REQUIRE(framebuffer.bind());
    gl.glViewport(0, 0, 64, 64);
    gl.glClearColor(0, 0, 0, 1);
    gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.glEnable(GL_DEPTH_TEST);
    gl.glDepthMask(GL_TRUE);
    SelectionRenderer selection;
    REQUIRE(selection.initialize(gl));
    const auto empty = framebuffer.toImage();
    selection.draw({}, glm::mat4(1));
    REQUIRE(framebuffer.toImage() == empty);
    selection.draw({glm::vec3(-0.5F), glm::vec3(0.5F)}, glm::mat4(1));
    REQUIRE(gl.glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
    GLboolean writesDepth = GL_FALSE;
    gl.glGetBooleanv(GL_DEPTH_WRITEMASK, &writesDepth);
    REQUIRE(writesDepth == GL_TRUE);
    const auto lines = framebuffer.toImage();
    int orange = 0;
    for (int y = 0; y < lines.height(); ++y) {
        for (int x = 0; x < lines.width(); ++x) {
            const auto color = lines.pixelColor(x, y);
            if (color.red() > 240 && color.green() > 150 && color.green() < 180 &&
                color.blue() < 25) {
                ++orange;
            }
        }
    }
    REQUIRE(orange >= 100);
    GizmoRenderer gizmo;
    REQUIRE(gizmo.initialize(gl));
    gizmo.draw({glm::vec3(0), 0.8F}, glm::mat4(1), 0);
    REQUIRE(framebuffer.toImage() != lines);
    REQUIRE(gl.glIsEnabled(GL_DEPTH_TEST) == GL_TRUE);
    gizmo.destroy();
    REQUIRE(gl.glGetError() == GL_NO_ERROR);
    gl.glDisable(GL_DEPTH_TEST);
    gl.glDepthMask(GL_FALSE);
    selection.draw({glm::vec3(-0.5F, 0, -0.5F), glm::vec3(0.5F, 0, 0.5F)}, glm::mat4(1));
    REQUIRE(gl.glIsEnabled(GL_DEPTH_TEST) == GL_FALSE);
    gl.glGetBooleanv(GL_DEPTH_WRITEMASK, &writesDepth);
    REQUIRE(writesDepth == GL_FALSE);
    selection.destroy();
    selection.destroy();
    REQUIRE_FALSE(selection.isValid());
    REQUIRE(selection.initialize(gl));
    REQUIRE(gl.glGetError() == GL_NO_ERROR);
}

TEST_CASE("Viewport shading changes actual mesh pixels and restores fill mode before overlays",
          "[gpu][shading-gpu]") {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QOpenGLContext context;
    context.setFormat(format);
    REQUIRE(context.create());
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    REQUIRE(context.makeCurrent(&surface));
    QOpenGLFunctions_4_1_Core gl;
    REQUIRE(gl.initializeOpenGLFunctions());
    QOpenGLFramebufferObject framebuffer(256, 256, QOpenGLFramebufferObject::CombinedDepthStencil);
    REQUIRE(framebuffer.isValid());
    REQUIRE(framebuffer.bind());
    gl.glViewport(0, 0, 256, 256);
    Renderer renderer;
    REQUIRE(renderer.initialize(gl));
    renderer.resize(256, 256);
    mini3d::core::Scene scene;
    mini3d::assets::AssetManager assets;
    const auto cube = scene.createEntity("Cube", 0, mini3d::core::PrimitiveKind::Cube);
    mini3d::core::SurfaceStyle style;
    style.tint = {.9F, .1F, .05F};
    REQUIRE(scene.setSurface(cube, style));
    REQUIRE(renderer.focusEntity(scene, assets, cube));
    const auto render = [&](ViewportShading mode) {
        renderer.setShadingMode(mode);
        gl.glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        renderer.render(scene, assets, 0, false, -1, 0, GizmoTool::None, GizmoSpace::World, nullptr,
                        5, false);
        gl.glFinish();
        std::array<GLint, 2> polygonModes{};
        gl.glGetIntegerv(GL_POLYGON_MODE, polygonModes.data());
        // Core Profile 只支持统一的 FRONT_AND_BACK 模式，不能断言兼容模式的第二槽。
        REQUIRE(polygonModes[0] == GL_FILL);
        REQUIRE(gl.glGetError() == GL_NO_ERROR);
        auto image = framebuffer.toImage();
        REQUIRE_FALSE(image.isNull());
        return image;
    };
    const auto material = render(ViewportShading::Material);
    const auto solid = render(ViewportShading::Solid);
    const auto wireframe = render(ViewportShading::Wireframe);
    REQUIRE(material != solid);
    REQUIRE(solid != wireframe);
    const auto pixels = [](const QImage& image, bool redOnly) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto color = image.pixelColor(x, y);
                if (redOnly ? color.red() > color.green() + 15 : color != QColor(59, 59, 59))
                    ++count;
            }
        return count;
    };
    REQUIRE(pixels(material, true) > 1000);
    REQUIRE(pixels(solid, true) == 0);
    REQUIRE(pixels(solid, false) > 1000);
    REQUIRE(pixels(wireframe, false) > 100);
    REQUIRE(pixels(wireframe, false) < pixels(solid, false) / 2);
    REQUIRE(scene.find(cube)->surface.tint == style.tint);
    renderer.destroy();
    REQUIRE(gl.glGetError() == GL_NO_ERROR);
}

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    Q_INIT_RESOURCE(renderer_shaders);
    return Catch::Session().run(argc, argv);
}
