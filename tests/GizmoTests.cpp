/*
 * 模块名: GizmoTests
 * 功能概述: 验证移动轴拾取、像素尺度与拖动数学。
 * 对外接口: Catch2 用例
 * 依赖关系: GizmoController、EditorCamera、Core
 * 输入输出: 射线/矩阵到轴标识和位移断言。
 * 异常与错误: 平行或缩放行为错误时测试失败。
 * 维护说明: 纯 CPU，无窗口和 OpenGL。
 */
#include "renderer_gl/EditorCamera.h"
#include "renderer_gl/GizmoController.h"

#include <catch2/catch_test_macros.hpp>
using namespace mini3d;
TEST_CASE("World gizmo axes have independent pick volumes and stable screen scale", "[gizmo]") {
    const renderer_gl::GizmoHandle handle{{0, 0, 0}, 1};
    REQUIRE(renderer_gl::GizmoController::pickAxis({{0.6F, 0, 3}, {0, 0, -1}}, handle) == 0);
    REQUIRE(renderer_gl::GizmoController::pickAxis({{0, 0.6F, 3}, {0, 0, -1}}, handle) == 1);
    REQUIRE(renderer_gl::GizmoController::pickAxis({{3, 0, 0.6F}, {-1, 0, 0}}, handle) == 2);
    REQUIRE(renderer_gl::GizmoController::pickAxis({{0, 0, 3}, {0, 0, -1}}, handle) == -1);
    REQUIRE(renderer_gl::GizmoController::pickAxis({{4, 4, 3}, {0, 0, -1}}, handle) == -1);
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(800, 600);
    const auto size = camera.worldUnitsPerPixel(camera.target());
    camera.zoom(2);
    REQUIRE(camera.worldUnitsPerPixel(camera.target()) < size);
    REQUIRE(camera.worldUnitsPerPixel(camera.position()) == 0);
}

TEST_CASE("World drag preserves a single axis through a signed nonuniform parent", "[gizmo]") {
    core::Transform parent;
    parent.position = {3, -2, 1};
    parent.rotation = glm::quat(glm::radians(glm::vec3(25, 40, 15)));
    parent.scale = {-2, 3, 0.5F};
    core::Transform before;
    before.position = {1, 2, -1};
    before.rotation = glm::quat(glm::radians(glm::vec3(15, 20, 30)));
    before.scale = {1, -2, 3};
    const auto matrix = parent.localMatrix();
    const auto origin = glm::vec3(matrix * glm::vec4(before.position, 1));
    for (int axis = 0; axis < 3; ++axis) {
        renderer_gl::GizmoController controller;
        glm::vec3 direction(0), offset(0);
        direction[(axis + 1) % 3] = -1;
        offset[axis] = 0.6F;
        core::Ray start{origin + offset - direction * 5.0F, direction};
        REQUIRE(controller.begin(start, axis, {origin, 1}, matrix, before));
        core::Transform result;
        REQUIRE(controller.preview(start, result));
        REQUIRE(glm::length(result.position - before.position) < 1.0e-5F);
        auto moved = start;
        moved.origin[axis] += 2;
        REQUIRE(controller.preview(moved, result));
        glm::vec3 expected(0);
        expected[axis] = 2;
        REQUIRE(glm::length(glm::vec3(matrix * glm::vec4(result.position, 1)) - origin - expected) <
                1.0e-4F);
        REQUIRE(result.rotation == before.rotation);
        REQUIRE(result.scale == before.scale);
        REQUIRE(controller.preview(start, result));
        REQUIRE(glm::length(result.position - before.position) < 1.0e-5F);
        controller.end();
        REQUIRE_FALSE(controller.preview(start, result));
        direction = glm::vec3(0);
        direction[axis] = -1;
        REQUIRE_FALSE(controller.begin({origin - direction * 5.0F, direction}, axis, {origin, 1},
                                       matrix, before));
    }
}

TEST_CASE("Rotation and scale gizmos preserve pivot and reject shear", "[gizmo][transform-tools]") {
    using renderer_gl::GizmoController;
    using renderer_gl::GizmoTool;
    const renderer_gl::GizmoHandle handle{{0, 0, 0}, 1};
    GizmoController controller;
    core::Transform before, result;
    const core::Ray start{{0.7071068F, 0.7071068F, 5}, {0, 0, -1}};
    REQUIRE(GizmoController::pickAxis(start, handle, GizmoTool::Rotate) == 2);
    REQUIRE(controller.begin(start, 2, handle, glm::mat4(1), before, GizmoTool::Rotate));
    REQUIRE(controller.preview({{-0.7071068F, 0.7071068F, 5}, {0, 0, -1}}, result));
    REQUIRE(glm::length(result.rotation * glm::vec3(1, 0, 0) - glm::vec3(0, 1, 0)) < 1.0e-5F);
    REQUIRE(result.position == before.position);
    REQUIRE(controller.preview(start, result));
    REQUIRE(result.rotation == before.rotation);
    before.scale = {-1, 2, 3};
    REQUIRE(controller.begin({{0.6F, 0, 5}, {0, 0, -1}}, 0, handle, glm::mat4(1), before,
                             GizmoTool::Scale));
    REQUIRE(controller.preview({{1.6F, 0, 5}, {0, 0, -1}}, result));
    REQUIRE(glm::length(result.scale - glm::vec3(-2, 2, 3)) < 1.0e-5F);
    REQUIRE(GizmoController::pickAxis({{0, 0, 5}, {0, 0, -1}}, handle, GizmoTool::Scale) == 3);
    REQUIRE(controller.begin({{0, 0, 5}, {0, 0, -1}}, 3, handle, glm::mat4(1), before,
                             GizmoTool::Scale));
    REQUIRE(controller.preview({{1, 0, 5}, {0, 0, -1}}, result));
    REQUIRE(result.scale == glm::vec3(-2, 4, 6));
    REQUIRE(controller.preview({{-3, 0, 5}, {0, 0, -1}}, result));
    REQUIRE(result.isValid());
    REQUIRE(result.scale.x < 0);
    before.rotation = glm::quat(glm::radians(glm::vec3(0, 0, 45)));
    REQUIRE(controller.begin({{0.6F, 0, 5}, {0, 0, -1}}, 0, handle, glm::mat4(1), before,
                             GizmoTool::Scale));
    const auto preserved = result.scale;
    REQUIRE_FALSE(controller.preview({{1.6F, 0, 5}, {0, 0, -1}}, result));
    REQUIRE(result.scale == preserved);
}

TEST_CASE("Local axes pick and edit in rotated signed parent frames", "[gizmo][local-tools]") {
    using namespace renderer_gl;
    core::Transform parent, before, result;
    parent.rotation = glm::quat(glm::radians(glm::vec3(0, 0, 90)));
    parent.scale = {-2, 3, 1};
    before.rotation = glm::quat(glm::radians(glm::vec3(0, 0, 30)));
    const auto rotation = parent.rotation * before.rotation;
    GizmoHandle handle{{0, 0, 0}, 1, glm::mat3_cast(rotation), GizmoSpace::Local};
    const auto direction = handle.basis[0];
    const core::Ray start{direction * 0.6F + glm::vec3(0, 0, 5), {0, 0, -1}};
    auto end = start;
    end.origin += direction;
    REQUIRE(GizmoController::pickAxis(start, handle) == 0);
    GizmoController controller;
    REQUIRE(controller.begin(start, 0, handle, parent.localMatrix(), before));
    REQUIRE(controller.preview(end, result));
    REQUIRE(glm::length(glm::vec3(parent.localMatrix() * glm::vec4(result.position, 0)) -
                        direction) < 1.0e-5F);
    REQUIRE(controller.begin(start, 0, handle, parent.localMatrix(), before, GizmoTool::Scale));
    REQUIRE(controller.preview(end, result));
    REQUIRE(glm::length(result.scale - glm::vec3(2, 1, 1)) < 1.0e-5F);
    REQUIRE(result.rotation == before.rotation);
    const core::Ray ringStart{{1, 0, 5}, {0, 0, -1}}, ringEnd{{0, 1, 5}, {0, 0, -1}};
    REQUIRE(
        controller.begin(ringStart, 2, handle, parent.localMatrix(), before, GizmoTool::Rotate));
    REQUIRE(controller.preview(ringEnd, result));
    const auto expected = before.rotation * glm::angleAxis(glm::radians(90.0F), glm::vec3(0, 0, 1));
    REQUIRE(std::abs(glm::dot(result.rotation, expected)) > 0.99999F);
}

TEST_CASE("Plane handles and snap quantize from the gesture origin", "[gizmo][snap-tools]") {
    using namespace renderer_gl;
    GizmoController controller;
    const GizmoHandle handle{{0, 0, 0}, 1};
    const core::Ray start{{0.3F, 0.3F, 5}, {0, 0, -1}};
    REQUIRE(GizmoController::pickAxis(start, handle) == 5);
    core::Transform before, result;
    REQUIRE(controller.begin(start, 5, handle, glm::mat4(1), before));
    REQUIRE(controller.preview({{1.04F, 0.56F, 5}, {0, 0, -1}}, result, true));
    REQUIRE(result.position == glm::vec3(0.5F, 0.5F, 0));
    REQUIRE(controller.preview(start, result, true));
    REQUIRE(result.position == before.position);
    REQUIRE(controller.preview({{1.04F, 0.56F, 5}, {0, 0, -1}}, result));
    REQUIRE(glm::length(result.position - glm::vec3(0.74F, 0.26F, 0)) < 1.0e-5F);
    REQUIRE_FALSE(controller.begin({{0, 0, 5}, {1, 0, 0}}, 5, handle, glm::mat4(1), before));
    REQUIRE(controller.begin({{1, 0, 5}, {0, 0, -1}}, 2, handle, glm::mat4(1), before,
                             GizmoTool::Rotate));
    const float angle = glm::radians(22.0F);
    REQUIRE(controller.preview({{std::cos(angle), std::sin(angle), 5}, {0, 0, -1}}, result, true));
    REQUIRE(std::abs(glm::dot(result.rotation,
                              glm::angleAxis(glm::radians(15.0F), glm::vec3(0, 0, 1)))) > 0.99999F);
    REQUIRE(controller.begin({{0, 0, 5}, {0, 0, -1}}, 3, handle, glm::mat4(1), before,
                             GizmoTool::Scale));
    REQUIRE(controller.preview({{0.26F, 0, 5}, {0, 0, -1}}, result, true));
    REQUIRE(glm::length(result.scale - glm::vec3(1.3F)) < 1.0e-5F);
}

TEST_CASE("Top view uniform scale grows to screen right", "[gizmo][view-scale-regression]") {
    using namespace renderer_gl;
    GizmoController controller;
    const GizmoHandle handle{{0, 0, 0}, 1};
    core::Transform before, result;
    REQUIRE(controller.begin({{0, 5, 0}, {0, -1, 0}}, 3, handle, glm::mat4(1), before,
                             GizmoTool::Scale));
    REQUIRE(controller.preview({{0.5F, 5, 0}, {0, -1, 0}}, result));
    REQUIRE(result.scale.x > before.scale.x);
}
TEST_CASE("Local axis scaling does not clamp against an unchanged tiny axis",
          "[gizmo][view-scale-regression]") {
    using namespace renderer_gl;
    GizmoController controller;
    const GizmoHandle handle{{0, 0, 0}, 1, glm::mat3(1), GizmoSpace::Local};
    core::Transform before, result;
    before.scale = {1, 0.001F, 1};
    REQUIRE(controller.begin({{0.6F, 0, 5}, {0, 0, -1}}, 0, handle, glm::mat4(1), before,
                             GizmoTool::Scale));
    REQUIRE(controller.preview({{0.1F, 0, 5}, {0, 0, -1}}, result));
    REQUIRE(result.scale.x < 0.6F);
    REQUIRE(result.scale.y == before.scale.y);
}
