/*
 * 模块名: ObjectTransformMathTests
 * 功能概述: 验证固定快照的对象变换、旋转对象缩放与不可表示旋转的拒绝。
 * 对外接口: Catch2 [object-transform-math] 用例
 * 依赖关系: Core Transform、ObjectTransformMath、Catch2，无 Qt/GL
 * 输入输出: 明确矩阵夹具到 TRS 候选和数值断言。
 * 异常与错误: 不准确的世界位移、累乘或非法候选即失败。
 * 维护说明: 预期由独立矩阵/方向计算，不复用被测算法。
 */
#include "editor/operations/ObjectTransformMath.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>

using namespace mini3d;
using editor::ObjectTransformMath;

TEST_CASE("World translation preserves exact displacement below a mirrored scaled parent",
          "[object-transform-math]") {
    core::Transform parent;
    parent.position = {3, -2, 5};
    parent.rotation = glm::angleAxis(glm::radians(37.0F), glm::vec3(0, 1, 0));
    parent.scale = {-2, 3, 0.5F};
    core::Transform before;
    before.position = {1, 2, -1};
    const auto matrix = parent.localMatrix();
    const glm::vec3 delta{2, -4, 0.7F};
    const auto result = ObjectTransformMath::translate(before, matrix, delta);
    REQUIRE(result.has_value());
    const auto worldBefore = glm::vec3(matrix * glm::vec4(before.position, 1));
    const auto worldAfter = glm::vec3(matrix * glm::vec4(result->position, 1));
    REQUIRE(glm::distance(worldAfter - worldBefore, delta) < 1.0e-5F);
    REQUIRE(result->rotation == before.rotation);
    REQUIRE(result->scale == before.scale);
    REQUIRE(before.position == glm::vec3(1, 2, -1));
    REQUIRE_FALSE(ObjectTransformMath::translate(before, glm::mat4(0), delta));
}

TEST_CASE("Object rotation is computed from before and local rotation survives parent scale",
          "[object-transform-math]") {
    core::Transform before;
    before.position = {2, 3, 4};
    const auto first =
        ObjectTransformMath::rotate(before, glm::mat4(1), {0, 1, 0}, glm::radians(45.0F));
    const auto repeated =
        ObjectTransformMath::rotate(before, glm::mat4(1), {0, 1, 0}, glm::radians(45.0F));
    REQUIRE(first.has_value());
    REQUIRE(repeated.has_value());
    REQUIRE(first->rotation == repeated->rotation);
    REQUIRE(first->position == before.position);
    REQUIRE(glm::distance(first->rotation * glm::vec3(1, 0, 0),
                          glm::vec3(std::sqrt(0.5F), 0, -std::sqrt(0.5F))) < 1.0e-5F);
    before.rotation = glm::angleAxis(glm::radians(30.0F), glm::vec3(0, 0, 1));
    const auto parent = glm::scale(glm::mat4(1), glm::vec3(-2, 3, 1));
    const auto local =
        ObjectTransformMath::rotate(before, parent, {1, 0, 0}, glm::radians(45.0F), 1);
    REQUIRE(local.has_value());
    const auto expected = before.rotation * glm::angleAxis(glm::radians(45.0F), glm::vec3(0, 1, 0));
    REQUIRE(glm::distance(local->rotation * glm::vec3(1, 0, 0), expected * glm::vec3(1, 0, 0)) <
            1.0e-5F);
    REQUIRE(ObjectTransformMath::rotate(before, parent, {0, 1, 0}, 0)->rotation == before.rotation);
    REQUIRE_FALSE(ObjectTransformMath::rotate(before, parent, {0, 1, 0}, 0.5F));
}

TEST_CASE("Scale supports rotated plane uniform and negative factors but rejects zero",
          "[object-transform-math]") {
    core::Transform before;
    const auto plane =
        ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1), {0.5F, 0.5F, 1}, false);
    REQUIRE(plane.has_value());
    REQUIRE(plane->scale == glm::vec3(0.5F, 0.5F, 1));
    const auto mirrored =
        ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1), {-2, 1, 1}, false);
    REQUIRE(mirrored.has_value());
    REQUIRE(mirrored->scale == glm::vec3(-2, 1, 1));
    before.rotation = glm::angleAxis(glm::radians(35.0F), glm::vec3(0, 0, 1));
    const auto world =
        ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1), {2, 1, 1}, false);
    REQUIRE(world);
    const float c = std::cos(glm::radians(35.0F)), s = std::sin(glm::radians(35.0F));
    REQUIRE(glm::length(world->scale - glm::vec3(std::sqrt(4 * c * c + s * s),
                                                 std::sqrt(4 * s * s + c * c), 1)) < 1.0e-5F);
    REQUIRE(world->rotation == before.rotation);
    const auto local = ObjectTransformMath::scale(before, glm::mat4(1),
                                                  glm::mat3_cast(before.rotation), {2, 1, 1}, true);
    REQUIRE(local.has_value());
    REQUIRE(local->scale == glm::vec3(2, 1, 1));
    REQUIRE(local->rotation == before.rotation);
    REQUIRE(ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1), {-2, -2, -2}, false)
                ->scale == glm::vec3(-2));
    REQUIRE_FALSE(ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1), {0, 1, 1}, true));
    REQUIRE_FALSE(
        ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1), {0.0001F, 1, 1}, true));
    REQUIRE_FALSE(ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1),
                                             {std::numeric_limits<float>::infinity(), 1, 1}, true));
    REQUIRE(before.scale == glm::vec3(1));
}

TEST_CASE("Rotated cube numeric world scaling accepts all three axes",
          "[object-transform-math][rotated-scale]") {
    core::Transform before;
    before.position = {2, -3, 4};
    before.rotation = glm::quat(glm::radians(glm::vec3(25, 40, 15)));
    before.scale = {-1, 2, 0.5F};
    const auto snapshot = before.localMatrix();
    core::Transform parent;
    parent.rotation = glm::quat(glm::radians(glm::vec3(10, 35, -20)));
    parent.scale = {-2, 3, 0.5F};
    for (const auto& parentWorld : {glm::mat4(1), parent.localMatrix()}) {
        for (int axis = 0; axis < 3; ++axis) {
            for (float factor : {0.5F, 2.0F}) {
                INFO("axis=" << axis << ", factor=" << factor);
                glm::vec3 factors(1);
                factors[axis] = factor;
                const auto result =
                    ObjectTransformMath::scale(before, parentWorld, glm::mat3(1), factors, false);
                REQUIRE(result);
                REQUIRE(result->isValid());
                REQUIRE(result->position == before.position);
                REQUIRE(result->rotation == before.rotation);
                // 独立用原世界边向量计算拉伸后的长度比，不要求最终边方向发生剪切。
                const auto world = glm::mat3(parentWorld * snapshot);
                for (int edge = 0; edge < 3; ++edge) {
                    const auto expected = before.scale[edge] * glm::length(world[edge] * factors) /
                                          glm::length(world[edge]);
                    REQUIRE(std::abs(result->scale[edge] - expected) < 1.0e-5F);
                }
                const auto repeated =
                    ObjectTransformMath::scale(before, parentWorld, glm::mat3(1), factors, false);
                REQUIRE(repeated);
                REQUIRE(repeated->scale == result->scale);
            }
        }
        const auto unchanged =
            ObjectTransformMath::scale(before, parentWorld, glm::mat3(1), glm::vec3(1), false);
        REQUIRE(unchanged);
        REQUIRE(unchanged->scale == before.scale);
        REQUIRE(unchanged->rotation == before.rotation);
    }
    REQUIRE(before.localMatrix() == snapshot);
}

TEST_CASE("Rotated world scaling maps mirror signs and plane factors to local channels",
          "[object-transform-math][rotated-scale]") {
    core::Transform before;
    before.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3(0, 0, 1));
    before.scale = {-1, 2, 3};
    const auto mirror =
        ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1), {-2, 1, 1}, false);
    REQUIRE(mirror);
    REQUIRE(glm::distance(mirror->scale, glm::vec3(-1, -4, 3)) < 1.0e-5F);
    REQUIRE(mirror->rotation == before.rotation);
    const auto plane =
        ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1), {0.5F, 0.5F, 1}, false);
    REQUIRE(plane);
    REQUIRE(glm::distance(plane->scale, glm::vec3(-0.5F, 1, 3)) < 1.0e-5F);
    const auto oriented = ObjectTransformMath::scale(
        before, glm::mat4(1), glm::mat3_cast(before.rotation), {2, 1, 1}, false);
    REQUIRE(oriented);
    REQUIRE(glm::distance(oriented->scale, glm::vec3(-2, 2, 3)) < 1.0e-5F);
    for (float invalid : {0.0F, 0.00001F, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        REQUIRE_FALSE(
            ObjectTransformMath::scale(before, glm::mat4(1), glm::mat3(1), {invalid, 1, 1}, false));
    }
    REQUIRE_FALSE(ObjectTransformMath::scale(before, glm::mat4(0), glm::mat3(1), {2, 1, 1}, false));
}
