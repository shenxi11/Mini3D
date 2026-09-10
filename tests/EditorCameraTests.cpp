/*
 * 模块名: EditorCameraTests
 * 功能概述: 验证 Viewport 宽高比、Orbit、Pan、Zoom 限制和矩阵有限性。
 * 对外接口: Catch2 自动注册测试用例
 * 依赖关系: Catch2、GLM、EditorCamera
 * 输入输出: 输入相机交互增量，输出 Catch2 状态与矩阵断言结果。
 * 异常与错误: 相机状态未变化、越界或矩阵出现非有限值时测试失败。
 * 维护说明: 测试只运行纯 GLM 数学，不创建 Qt 窗口或 OpenGL Context。
 */

#include "renderer_gl/EditorCamera.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/geometric.hpp>

namespace mini3d::renderer_gl {
namespace {

void requireFiniteMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            REQUIRE(std::isfinite(matrix[column][row]));
        }
    }
}

} // namespace

TEST_CASE("Viewport size produces a stable aspect ratio", "[camera][resize]") {
    EditorCamera camera;
    camera.setViewportSize(1600, 900);
    REQUIRE(camera.aspectRatio() == Catch::Approx(16.0F / 9.0F));
    requireFiniteMatrix(camera.projectionMatrix());

    camera.setViewportSize(0, -20);
    REQUIRE(camera.aspectRatio() == Catch::Approx(1.0F));
    requireFiniteMatrix(camera.projectionMatrix());
}

TEST_CASE("Orbit and Pan update different camera state", "[camera][navigation]") {
    EditorCamera camera;
    camera.setViewportSize(1280, 720);
    const glm::vec3 initialPosition = camera.position();
    const glm::vec3 initialTarget = camera.target();
    const float initialDistance = camera.distance();

    camera.orbit(140.0F, -70.0F);
    REQUIRE(glm::distance(camera.position(), initialPosition) > 0.1F);
    REQUIRE(camera.distance() == Catch::Approx(initialDistance));
    REQUIRE(glm::distance(camera.target(), initialTarget) == Catch::Approx(0.0F));

    camera.pan(-90.0F, 55.0F);
    REQUIRE(glm::distance(camera.target(), initialTarget) > 0.1F);
    REQUIRE(camera.distance() == Catch::Approx(initialDistance));
    requireFiniteMatrix(camera.viewProjectionMatrix());
}

TEST_CASE("Zoom remains within editor distance limits", "[camera][zoom]") {
    EditorCamera camera;
    const float initialDistance = camera.distance();
    camera.zoom(3.0F);
    REQUIRE(camera.distance() < initialDistance);

    camera.zoom(10000.0F);
    REQUIRE(camera.distance() == Catch::Approx(0.6F));
    camera.zoom(-10000.0F);
    REQUIRE(camera.distance() == Catch::Approx(50.0F));
    requireFiniteMatrix(camera.viewProjectionMatrix());
}

TEST_CASE("Screen rays agree with projected points and top-left coordinates", "[camera][ray]") {
    EditorCamera camera;
    for (const auto size : {glm::ivec2(1600, 900), glm::ivec2(450, 900)}) {
        camera.setViewportSize(size.x, size.y);
        camera.orbit(32, -14);
        camera.pan(12, 20);
        const auto center = camera.screenRay(size.x * 0.5F, size.y * 0.5F);
        REQUIRE(glm::length(center.direction) == Catch::Approx(1.0F));
        REQUIRE(glm::distance(center.direction,
                              glm::normalize(camera.target() - camera.position())) < 1.0e-4F);
        const auto upper = camera.screenRay(0, 0);
        const auto lower = camera.screenRay(static_cast<float>(size.x), static_cast<float>(size.y));
        REQUIRE(upper.direction.y > lower.direction.y);
        const auto point = camera.target() + glm::vec3(0.2F, 0.1F, -0.1F);
        const auto clip = camera.viewProjectionMatrix() * glm::vec4(point, 1);
        const auto ray = camera.screenRay((clip.x / clip.w + 1) * size.x * 0.5F,
                                          (1 - clip.y / clip.w) * size.y * 0.5F);
        REQUIRE(glm::length(glm::cross(point - ray.origin, ray.direction)) < 1.0e-3F);
        const auto originClip = camera.viewProjectionMatrix() * glm::vec4(ray.origin, 1);
        REQUIRE(originClip.z / originClip.w == Catch::Approx(-1.0F).margin(1.0e-4F));
    }
    camera.setViewportSize(800, 450);
    const auto logical = camera.screenRay(200, 150);
    camera.setViewportSize(1600, 900);
    const auto physical = camera.screenRay(400, 300);
    REQUIRE(glm::distance(logical.direction, physical.direction) < 1.0e-6F);
}

TEST_CASE("Focus fits every corner for portrait wide and large models", "[camera][focus]") {
    EditorCamera camera;
    const auto initialDirection = glm::normalize(camera.position() - camera.target());
    for (const auto size : {glm::ivec2(1600, 900), glm::ivec2(400, 1000)}) {
        camera.setViewportSize(size.x, size.y);
        for (const float scale : {0.001F, 1.0F, 1000.0F}) {
            const core::Aabb bounds{glm::vec3(-2, -1, -3) * scale, glm::vec3(4, 2, 1) * scale};
            REQUIRE(camera.focus(bounds));
            REQUIRE(glm::distance(camera.target(), (bounds.minimum + bounds.maximum) * 0.5F) <
                    1.0e-3F);
            REQUIRE(glm::distance(initialDirection,
                                  glm::normalize(camera.position() - camera.target())) < 1.0e-5F);
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 point((corner & 1) ? bounds.maximum.x : bounds.minimum.x,
                                      (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
                                      (corner & 4) ? bounds.maximum.z : bounds.minimum.z);
                const auto clip = camera.viewProjectionMatrix() * glm::vec4(point, 1);
                REQUIRE(clip.w > 0);
                REQUIRE(std::abs(clip.x / clip.w) < 1);
                REQUIRE(std::abs(clip.y / clip.w) < 1);
                REQUIRE(std::abs(clip.z / clip.w) < 1);
            }
        }
    }
    const auto target = camera.target();
    const auto distance = camera.distance();
    REQUIRE_FALSE(camera.focus({}));
    REQUIRE(camera.target() == target);
    REQUIRE(camera.distance() == distance);
    REQUIRE(camera.focus({glm::vec3(1), glm::vec3(1)}));
    requireFiniteMatrix(camera.viewProjectionMatrix());
}

TEST_CASE("Document camera restoration preserves navigation and large focus projection",
          "[camera][document]") {
    for (const float scale : {1.0F, 1000.0F}) {
        EditorCamera camera;
        camera.setViewportSize(852, 659);
        REQUIRE(camera.focus({glm::vec3(-2) * scale, glm::vec3(3) * scale}));
        camera.orbit(42, -23);
        camera.pan(30, -12);
        camera.zoom(1);
        const auto state = camera.state();
        EditorCamera restored;
        restored.setViewportSize(852, 659);
        REQUIRE(restored.setState(state));
        REQUIRE(glm::distance(camera.position(), restored.position()) < 1.0e-5F * scale);
        REQUIRE(restored.target() == camera.target());
        REQUIRE(restored.state().focusRadius == state.focusRadius);
        REQUIRE(restored.state().maximumDistance == state.maximumDistance);
        const auto expected = camera.viewProjectionMatrix();
        const auto actual = restored.viewProjectionMatrix();
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                REQUIRE(actual[column][row] ==
                        Catch::Approx(expected[column][row]).margin(1.0e-5F * scale));
            }
        }
    }
}

TEST_CASE("Navigation limits remain valid document camera states", "[camera][document]") {
    EditorCamera camera;
    for (int turn = 0; turn < 20; ++turn) {
        camera.orbit(71, -32);
        for (const float steps : {-10000.0F, 10000.0F}) {
            camera.zoom(steps);
            INFO("turn " << turn << ", wheel " << steps);
            REQUIRE(camera.state().isValid());
            EditorCamera restored;
            REQUIRE(restored.setState(camera.state()));
            REQUIRE(restored.state().isValid());
        }
    }
    auto invalid = camera.state();
    invalid.maximumDistance = 0.5F;
    REQUIRE_FALSE(invalid.isValid());
    invalid = camera.state();
    invalid.position = invalid.target + glm::vec3(100, 0, 0);
    REQUIRE_FALSE(invalid.isValid());
}

} // namespace mini3d::renderer_gl

namespace mini3d::renderer_gl {
TEST_CASE("Preset orthographic navigation preserves rays scale and document compatibility",
          "[camera][view-tools]") {
    EditorCamera camera;
    camera.setViewportSize(800, 600);
    const auto saved = camera.state();
    for (const auto view : {EditorView::Front, EditorView::Right, EditorView::Top}) {
        camera.setView(view);
        requireFiniteMatrix(camera.viewProjectionMatrix());
        const float units = camera.worldUnitsPerPixel(camera.target());
        camera.setOrthographic(true);
        REQUIRE(camera.worldUnitsPerPixel(camera.target()) == Catch::Approx(units));
        const auto first = camera.screenRay(100, 100);
        const auto second = camera.screenRay(600, 500);
        REQUIRE(glm::distance(first.direction, second.direction) < 1.0e-6F);
        REQUIRE(glm::distance(first.origin, second.origin) > 0.1F);
        const auto point = camera.target() + glm::vec3(0.2F, 0.1F, -0.1F);
        const auto clip = camera.viewProjectionMatrix() * glm::vec4(point, 1);
        const auto ray = camera.screenRay((clip.x / clip.w + 1) * 400, (1 - clip.y / clip.w) * 300);
        REQUIRE(glm::length(glm::cross(point - ray.origin, ray.direction)) < 1.0e-4F);
        camera.pan(20, 10);
        camera.zoom(1);
        requireFiniteMatrix(camera.viewProjectionMatrix());
        REQUIRE(camera.state().isValid());
        camera.setOrthographic(false);
    }
    camera.setView(EditorView::Top);
    REQUIRE(glm::normalize(camera.target() - camera.position()) == glm::vec3(0, -1, 0));
    camera.orbit(3, 3);
    REQUIRE(camera.view() == EditorView::Orbit);
    REQUIRE(camera.state().isValid());
    camera.setView(EditorView::Top);
    camera.setOrthographic(true);
    REQUIRE(camera.setState(saved));
    REQUIRE(camera.view() == EditorView::Orbit);
    REQUIRE_FALSE(camera.isOrthographic());
}
} // namespace mini3d::renderer_gl
