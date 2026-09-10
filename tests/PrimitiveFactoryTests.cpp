/*
 * 模块名: PrimitiveFactoryTests
 * 功能概述: 验证 Cube、Sphere、Plane 的固定拓扑、单位法线和外侧 CCW 绕序。
 * 对外接口: Catch2 自动注册测试用例
 * 依赖关系: Catch2、GLM、PrimitiveFactory
 * 输入输出: 输入 PrimitiveFactory 结果，输出 Catch2 断言结果。
 * 异常与错误: 索引越界、退化三角形、法线异常或绕序错误时测试失败。
 * 维护说明: 测试不创建 Qt 对象、窗口、OpenGL Context 或 GPU 资源。
 */

#include "renderer_gl/PrimitiveFactory.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/geometric.hpp>

namespace mini3d::renderer_gl {
namespace {

void requireValidTriangles(const MeshData& meshData) {
    REQUIRE_FALSE(meshData.vertices.empty());
    REQUIRE_FALSE(meshData.indices.empty());
    REQUIRE(meshData.indices.size() % 3 == 0);

    for (const std::uint32_t index : meshData.indices) {
        REQUIRE(index < meshData.vertices.size());
    }

    for (std::size_t index = 0; index < meshData.indices.size(); index += 3) {
        const MeshVertex& first = meshData.vertices[meshData.indices[index]];
        const MeshVertex& second = meshData.vertices[meshData.indices[index + 1]];
        const MeshVertex& third = meshData.vertices[meshData.indices[index + 2]];
        const glm::vec3 faceNormal =
            glm::cross(second.position - first.position, third.position - first.position);
        REQUIRE(glm::length(faceNormal) > 1.0e-6F);
        REQUIRE(glm::dot(faceNormal, first.normal + second.normal + third.normal) > 0.0F);
    }

    for (const MeshVertex& vertex : meshData.vertices) {
        REQUIRE(glm::length(vertex.normal) == Catch::Approx(1.0F).margin(1.0e-5F));
    }
}

} // namespace

TEST_CASE("Cube has deterministic hard-edge topology", "[primitive][cube]") {
    const MeshData cube = PrimitiveFactory::createCube();
    REQUIRE(cube.vertices.size() == 24);
    REQUIRE(cube.indices.size() == 36);
    requireValidTriangles(cube);
}

TEST_CASE("Sphere has unit normals and no degenerate pole triangles", "[primitive][sphere]") {
    const MeshData sphere = PrimitiveFactory::createSphere();
    REQUIRE(sphere.vertices.size() == 425);
    REQUIRE(sphere.indices.size() == 2160);
    requireValidTriangles(sphere);

    for (const MeshVertex& vertex : sphere.vertices) {
        REQUIRE(glm::length(vertex.position) == Catch::Approx(0.5F).margin(1.0e-5F));
        REQUIRE(glm::dot(glm::normalize(vertex.position), vertex.normal) ==
                Catch::Approx(1.0F).margin(1.0e-5F));
    }
}

TEST_CASE("Plane faces positive Y with two indexed triangles", "[primitive][plane]") {
    const MeshData plane = PrimitiveFactory::createPlane();
    REQUIRE(plane.vertices.size() == 4);
    REQUIRE(plane.indices.size() == 6);
    requireValidTriangles(plane);

    for (const MeshVertex& vertex : plane.vertices) {
        REQUIRE(vertex.position.y == Catch::Approx(0.0F));
        REQUIRE(vertex.normal.y == Catch::Approx(1.0F));
    }
}

TEST_CASE("Primitive UVs preserve orientation and sphere seam continuity", "[primitive][uv]") {
    for (const MeshData& mesh : {PrimitiveFactory::createCube(), PrimitiveFactory::createSphere(),
                                 PrimitiveFactory::createPlane()}) {
        for (const MeshVertex& vertex : mesh.vertices) {
            REQUIRE(std::isfinite(vertex.uv.x));
            REQUIRE(std::isfinite(vertex.uv.y));
            REQUIRE(vertex.uv.x >= 0.0F);
            REQUIRE(vertex.uv.x <= 1.0F);
            REQUIRE(vertex.uv.y >= 0.0F);
            REQUIRE(vertex.uv.y <= 1.0F);
        }
        for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
            const glm::vec2 a = mesh.vertices[mesh.indices[index]].uv;
            const glm::vec2 b = mesh.vertices[mesh.indices[index + 1]].uv;
            const glm::vec2 c = mesh.vertices[mesh.indices[index + 2]].uv;
            REQUIRE(std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) > 1.0e-6F);
        }
    }
    const MeshData sphere = PrimitiveFactory::createSphere();
    for (std::size_t row = 0; row <= 16; ++row) {
        const auto& start = sphere.vertices[row * 25];
        const auto& end = sphere.vertices[row * 25 + 24];
        REQUIRE(glm::distance(start.position, end.position) < 1.0e-6F);
        REQUIRE(start.uv.x == 0.0F);
        REQUIRE(end.uv.x == 1.0F);
    }
    for (std::size_t index = 0; index < sphere.indices.size(); index += 3) {
        const float a = sphere.vertices[sphere.indices[index]].uv.x;
        const float b = sphere.vertices[sphere.indices[index + 1]].uv.x;
        const float c = sphere.vertices[sphere.indices[index + 2]].uv.x;
        REQUIRE(std::max({a, b, c}) - std::min({a, b, c}) < 0.05F);
    }
    const MeshData plane = PrimitiveFactory::createPlane();
    for (const MeshVertex& vertex : plane.vertices) {
        REQUIRE(vertex.uv.x == Catch::Approx((vertex.position.x + 0.6F) / 1.2F));
        REQUIRE(vertex.uv.y == Catch::Approx((0.6F - vertex.position.z) / 1.2F));
    }
}

} // namespace mini3d::renderer_gl
