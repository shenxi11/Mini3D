/*
 * 模块名: GeometryMathTests
 * 功能概述: 验证网格包围盒、仿射变换和 Ray/AABB 边界情况。
 * 对外接口: Catch2 测试
 * 依赖关系: core、PrimitiveFactory、GLM、Catch2
 * 输入输出: 输入已知几何与射线，断言角点和命中参数。
 * 异常与错误: 数学结果不符时测试失败。
 * 维护说明: 无 Qt/OpenGL 依赖；薄平面、平行和负方向属于必要用例。
 */
#include "core/Aabb.h"
#include "core/Ray.h"
#include "renderer_gl/PrimitiveFactory.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <limits>

using mini3d::core::Aabb;
using mini3d::core::intersectRayAabb;
using mini3d::core::Ray;
using mini3d::renderer_gl::PrimitiveFactory;

TEST_CASE("Mesh bounds are exact for primitives and empty meshes", "[aabb]") {
    REQUIRE_FALSE(mini3d::renderer_gl::MeshData{}.bounds().isValid());
    for (const auto& mesh : {PrimitiveFactory::createCube(), PrimitiveFactory::createSphere()}) {
        const Aabb bounds = mesh.bounds();
        REQUIRE(bounds.isValid());
        for (int axis = 0; axis < 3; ++axis) {
            REQUIRE(bounds.minimum[axis] == Catch::Approx(-0.5F));
            REQUIRE(bounds.maximum[axis] == Catch::Approx(0.5F));
        }
    }
    const Aabb plane = PrimitiveFactory::createPlane().bounds();
    REQUIRE(plane.isValid());
    REQUIRE(plane.minimum.y == 0.0F);
    REQUIRE(plane.maximum.y == 0.0F);
    REQUIRE(plane.minimum.x == Catch::Approx(-0.6F));
    REQUIRE(plane.maximum.z == Catch::Approx(0.6F));
}

TEST_CASE("Aabb handles translation rotation and negative nonuniform scale", "[aabb]") {
    Aabb box;
    box.expand({-1.0F, -2.0F, -3.0F});
    box.expand({1.0F, 2.0F, 3.0F});
    const glm::mat4 translation = glm::translate(glm::mat4(1.0F), {5.0F, 6.0F, 7.0F});
    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0F), glm::radians(90.0F), {0, 0, 1});
    const glm::mat4 scale = glm::scale(glm::mat4(1.0F), {-2.0F, 3.0F, 0.5F});
    const Aabb world = box.transformed(translation * rotation * scale);
    REQUIRE(glm::distance(world.minimum, glm::vec3(-1, 4, 5.5F)) < 1.0e-5F);
    REQUIRE(glm::distance(world.maximum, glm::vec3(11, 8, 8.5F)) < 1.0e-5F);
    REQUIRE_FALSE(Aabb{}.transformed(translation).isValid());
}

TEST_CASE("Ray hits both directions on each axis at the nearest distance", "[ray]") {
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    for (int axis = 0; axis < 3; ++axis) {
        for (float sign : {-1.0F, 1.0F}) {
            Ray ray{{0, 0, 0}, {0, 0, 0}};
            ray.origin[axis] = sign * 3.0F;
            ray.direction[axis] = -sign;
            float distance = -7.0F;
            REQUIRE(intersectRayAabb(ray, box, distance));
            REQUIRE(distance == Catch::Approx(2.0F));
        }
    }
}

TEST_CASE("Ray miss leaves output unchanged for parallel and backward rays", "[ray]") {
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    for (const Ray& ray :
         {Ray{{2, 0, 3}, {0, 0, -1}}, Ray{{0, 0, 3}, {0, 0, 1}}, Ray{{3, 3, 0}, {-1, 0, 0}}}) {
        float distance = 42.0F;
        REQUIRE_FALSE(intersectRayAabb(ray, box, distance));
        REQUIRE(distance == 42.0F);
    }
}

TEST_CASE("Ray starts inside or on boundary and accepts corner grazing", "[ray]") {
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    float distance = -1.0F;
    REQUIRE(intersectRayAabb({{0, 0, 0}, {1, 0, 0}}, box, distance));
    REQUIRE(distance == 0.0F);
    REQUIRE(intersectRayAabb({{1, 0, 0}, {1, 0, 0}}, box, distance));
    REQUIRE(distance == 0.0F);
    REQUIRE(intersectRayAabb({{1, 1, 3}, {0, 0, -1}}, box, distance));
    REQUIRE(distance == Catch::Approx(2.0F));
    REQUIRE(intersectRayAabb({{3, 3, 3}, glm::normalize(glm::vec3(-1))}, box, distance));
    REQUIRE(distance == Catch::Approx(glm::length(glm::vec3(2))).margin(1.0e-5F));
}

TEST_CASE("Ray can hit zero-thickness plane bounds and almost parallel slabs", "[ray]") {
    float distance = -1.0F;
    const Aabb plane = PrimitiveFactory::createPlane().bounds();
    REQUIRE(intersectRayAabb({{0, 2, 0}, {0, -1, 0}}, plane, distance));
    REQUIRE(distance == Catch::Approx(2.0F));
    const Aabb longBox{{0, -1, -1}, {1, 1, 3.0e8F}};
    REQUIRE(intersectRayAabb({{-1, 0, 0}, {1.0e-8F, 0, 1}}, longBox, distance));
    REQUIRE(distance == Catch::Approx(1.0e8F));
}

TEST_CASE("Invalid bounds and invalid rays are rejected without modifying output", "[ray]") {
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    float distance = 42.0F;
    REQUIRE_FALSE(intersectRayAabb(Ray{}, Aabb{}, distance));
    REQUIRE_FALSE(intersectRayAabb(Ray{}, {{1, 0, 0}, {-1, 1, 1}}, distance));
    REQUIRE_FALSE(intersectRayAabb({{0, 0, 0}, {0, 0, 0}}, box, distance));
    REQUIRE_FALSE(intersectRayAabb({{std::numeric_limits<float>::infinity(), 0, 0}, {0, 0, -1}},
                                   box, distance));
    REQUIRE_FALSE(intersectRayAabb({{0, 0, 0}, {std::numeric_limits<float>::quiet_NaN(), 0, -1}},
                                   box, distance));
    REQUIRE(distance == 42.0F);
}
