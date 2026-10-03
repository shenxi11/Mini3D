/*
 * 模块名: InsetFaceTests
 * 功能概述: 验证真实等距内插、局部厚度上限、属性连续性与原子拒绝。
 * 对外接口: Catch2 [inset-face]；依赖关系: Core、Catch2，无Qt/GL。
 * 输入输出: Cube/非矩形凸环/不支持面到几何、属性、稳定ID断言。
 * 异常与错误: 非共面/非凸/自交/超限/精度退化/ID耗尽拒绝。
 * 维护说明: 等距以原边到新边的有向距离独立验证，不用中心缩放的结果作期望。
 */
#include "core/modeling/InsetFace.h"
#include "core/modeling/MeshDerivation.h"
#include "core/modeling/MeshValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace mini3d::core::modeling;
namespace {
EditableMesh polygon(const std::vector<glm::vec3>& points) {
    EditableMesh mesh;
    EditableFace face;
    face.id = 41;
    face.material = 17;
    for (const auto& point : points) {
        const auto id = static_cast<VertexId>(mesh.vertices.size() + 11);
        mesh.vertices.push_back({id, point});
        face.corners.push_back({id + 20,
                                id,
                                {point.x * .2F, point.y * .3F},
                                glm::vec3(0, 0, 1),
                                {point.x * .1F, point.y * .1F, .75F}});
    }
    mesh.faces.push_back(std::move(face));
    return mesh;
}
void requireEqualDistance(const EditableMesh& before, const EditableMesh& after, double thickness,
                          double tolerance = 1.0e-6) {
    const auto& outer = before.faces.front();
    const auto& inner = after.faces.front();
    const auto count = outer.corners.size();
    const auto origin = glm::dvec3(before.vertex(outer.corners.front().vertex)->position);
    glm::dvec3 area{0};
    for (std::size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        area +=
            glm::cross(glm::dvec3(before.vertex(outer.corners[i].vertex)->position) - origin,
                       glm::dvec3(before.vertex(outer.corners[next].vertex)->position) - origin);
    }
    const auto normal = glm::normalize(area);
    for (std::size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        const auto a = glm::dvec3(before.vertex(outer.corners[i].vertex)->position);
        const auto b = glm::dvec3(before.vertex(outer.corners[next].vertex)->position);
        const auto inward = glm::normalize(glm::cross(normal, b - a));
        for (const auto index : {i, next}) {
            const auto point = glm::dvec3(after.vertex(inner.corners[index].vertex)->position);
            REQUIRE(std::abs(glm::dot(inward, point - a) - thickness) < tolerance);
            REQUIRE(std::abs(glm::dot(normal, point - origin)) < tolerance);
        }
    }
}
} // namespace

TEST_CASE("Inset replaces one cube face by an equal width frame with stable center identity",
          "[inset-face]") {
    const auto before = createEditableCube();
    const auto analysis = analyzeInsetFace(before, 1);
    REQUIRE(analysis.face);
    REQUIRE(analysis.face->normal == glm::dvec3(0, 0, 1));
    REQUIRE(std::abs(analysis.face->maximumThickness - .5) < 1.0e-12);
    const auto result = insetFace(before, 1, .2);
    INFO(result.error);
    REQUIRE(result.mesh);
    REQUIRE(result.mesh->vertices.size() == 12);
    REQUIRE(result.mesh->faces.size() == 10);
    const auto validation = validateEditableMesh(*result.mesh);
    REQUIRE(validation.isValid());
    REQUIRE(validation.edgeCount == 20);
    REQUIRE(validation.boundaryEdgeCount == 0);
    requireEqualDistance(before, *result.mesh, .2);
    for (const auto& vertex : before.vertices)
        REQUIRE(*result.mesh->vertex(vertex.id) == vertex);
    for (std::size_t i = 1; i < before.faces.size(); ++i)
        REQUIRE(result.mesh->faces[i] == before.faces[i]);
    const auto& inner = result.mesh->faces.front();
    REQUIRE(inner.id == before.faces.front().id);
    for (std::size_t i = 0; i < inner.corners.size(); ++i) {
        const auto& corner = inner.corners[i];
        const auto& old = before.faces.front().corners[i];
        REQUIRE(corner.id == old.id);
        REQUIRE(corner.vertex > 8);
        REQUIRE(corner.normal == glm::vec3(0, 0, 1));
        REQUIRE(corner.color == old.color);
        REQUIRE(glm::length(corner.uv - (old.uv * .6F + glm::vec2(.2F))) < 1.0e-6F);
    }
    REQUIRE(before == createEditableCube());
}

TEST_CASE("Inset triangle trapezoid and pentagon are offsets not center scaled polygons",
          "[inset-face]") {
    for (const auto& points : std::vector<std::vector<glm::vec3>>{
             {{0, 0, 0}, {4, 0, 0}, {0, 3, 0}},
             {{0, 0, 0}, {4, 0, 0}, {3, 2, 0}, {0, 2, 0}},
             {{0, 0, 0}, {3, 0, 0}, {4, 1, 0}, {2, 3, 0}, {0, 2, 0}},
             {{0, 0, 0}, {1, 0, 0}, {4, 0, 0}, {4, 2, 0}, {0, 2, 0}}}) {
        const auto before = polygon(points);
        const auto result = insetFace(before, 41, .25);
        INFO(result.error);
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->faces.size() == points.size() + 1);
        REQUIRE(result.mesh->vertices.size() == points.size() * 2);
        requireEqualDistance(before, *result.mesh, .25);
        for (const auto& face : result.mesh->faces) {
            REQUIRE(face.material == 17);
            for (const auto& corner : face.corners) {
                const auto& p = result.mesh->vertex(corner.vertex)->position;
                REQUIRE(glm::length(corner.uv - glm::vec2(p.x * .2F, p.y * .3F)) < 1.0e-6F);
                REQUIRE(glm::length(corner.color - glm::vec3(p.x * .1F, p.y * .1F, .75F)) <
                        1.0e-6F);
                REQUIRE(corner.normal == glm::vec3(0, 0, 1));
            }
        }
        REQUIRE(validateEditableMesh(*result.mesh).isValid());
        REQUIRE(deriveMesh(*result.mesh).derived);
    }
    const auto triangle = polygon({{0, 0, 0}, {4, 0, 0}, {0, 3, 0}});
    REQUIRE(std::abs(analyzeInsetFace(triangle, 41).face->maximumThickness - 1) < 1.0e-12);
    REQUIRE(insetFace(triangle, 41, .99).mesh);
    REQUIRE_FALSE(insetFace(triangle, 41, 1).mesh);
}

TEST_CASE("Inset is scale translation plane and winding invariant in local units", "[inset-face]") {
    for (float scale : {1.0e-4F, 1.F, 1.0e4F}) {
        auto before = polygon({{0, 0, 0}, {4, 0, 0}, {3, 2, 0}, {0, 2, 0}});
        for (auto& vertex : before.vertices) {
            const auto p = vertex.position;
            vertex.position = (glm::vec3(p.x, p.y * .6F, p.y * .8F) + glm::vec3(5, -7, 11)) * scale;
        }
        for (auto& corner : before.faces.front().corners)
            corner.normal.reset();
        for (int winding = 0; winding < 2; ++winding) {
            const auto result = insetFace(before, 41, .2 * scale);
            INFO(result.error);
            REQUIRE(result.mesh);
            requireEqualDistance(before, *result.mesh, .2 * scale, scale * 3.0e-6);
            REQUIRE(validateEditableMesh(*result.mesh).isValid());
            std::reverse(before.faces.front().corners.begin(), before.faces.front().corners.end());
        }
    }
}

TEST_CASE("Inset refuses unsupported faces zero negative excessive and nonfinite thickness",
          "[inset-face]") {
    const auto before = createEditableCube();
    for (double value : {0., -.1, .5, .75, 1.0e-300, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
        const auto result = insetFace(before, 1, value);
        REQUIRE_FALSE(result.mesh);
        REQUIRE_FALSE(result.error.empty());
    }
    REQUIRE_FALSE(analyzeInsetFace(before, 999).face);
    for (const auto& points : std::vector<std::vector<glm::vec3>>{
             {{0, 0, 0}, {2, 0, 0}, {1, .5F, 0}, {2, 2, 0}, {0, 2, 0}},
             {{0, 0, 0}, {2, 0, 0}, {2, 2, .1F}, {0, 2, 0}},
             {{0, 0, 0}, {2, 2, 0}, {0, 2, 0}, {2, 0, 0}}}) {
        const auto mesh = polygon(points);
        REQUIRE_FALSE(analyzeInsetFace(mesh, 41).face);
        REQUIRE_FALSE(insetFace(mesh, 41, .1).mesh);
    }
    REQUIRE(before == createEditableCube());
}

TEST_CASE("Inset interpolates hard normals and shares center attributes with its frame",
          "[inset-face]") {
    auto before = polygon({{0, 0, 0}, {4, 0, 0}, {0, 3, 0}});
    before.faces.front().corners[1].normal = glm::vec3(1, 0, 1);
    before.faces.front().corners[2].normal = glm::vec3(0, 1, 1);
    const auto result = insetFace(before, 41, .25);
    REQUIRE(result.mesh);
    const auto& inner = result.mesh->faces.front();
    for (const auto& corner : inner.corners) {
        const auto position = result.mesh->vertex(corner.vertex)->position;
        const float wb = position.x / 4, wc = position.y / 3;
        const auto expected = glm::normalize(glm::vec3(0, 0, 1) * (1 - wb - wc) +
                                             glm::normalize(glm::vec3(1, 0, 1)) * wb +
                                             glm::normalize(glm::vec3(0, 1, 1)) * wc);
        REQUIRE(corner.normal);
        REQUIRE(glm::length(*corner.normal - expected) < 1.0e-6F);
        int copies = 0;
        for (std::size_t i = 1; i < result.mesh->faces.size(); ++i) {
            for (const auto& border : result.mesh->faces[i].corners) {
                if (border.vertex != corner.vertex)
                    continue;
                ++copies;
                REQUIRE(border.uv == corner.uv);
                REQUIRE(border.color == corner.color);
                REQUIRE(border.normal == corner.normal);
                REQUIRE(border.id != corner.id);
            }
        }
        REQUIRE(copies == 2);
    }
}

TEST_CASE("Inset preserves unrelated data and rejects exhausted element identities",
          "[inset-face]") {
    for (int kind = 0; kind < 3; ++kind) {
        auto before = createEditableCube();
        const auto last = std::numeric_limits<std::uint64_t>::max();
        if (kind == 0)
            before.vertices.push_back({last, {9, 9, 9}});
        else if (kind == 1)
            before.faces[1].id = last;
        else
            before.faces[1].corners[0].id = last;
        const auto result = insetFace(before, 1, .1);
        REQUIRE_FALSE(result.mesh);
        REQUIRE(result.error.find("ID") != std::string::npos);
    }
    auto before = createEditableCube();
    before.vertices.push_back({999, {7, 8, 9}});
    auto first = insetFace(before, 1, .1);
    auto second = insetFace(before, 1, .2);
    REQUIRE(first.mesh);
    REQUIRE(second.mesh);
    REQUIRE(first.mesh->vertices.size() == second.mesh->vertices.size());
    REQUIRE(first.mesh->faces.size() == second.mesh->faces.size());
    REQUIRE(second.mesh->vertex(999)->position == glm::vec3(7, 8, 9));
    std::reverse(before.vertices.begin(), before.vertices.end());
    std::reverse(before.faces.begin(), before.faces.end());
    const auto reordered = insetFace(before, 1, .2);
    REQUIRE(reordered.mesh);
    const auto& original = second.mesh->faces.front();
    const auto& changed = reordered.mesh->faces[5];
    REQUIRE(original == changed);
    for (const auto& corner : original.corners)
        REQUIRE(*second.mesh->vertex(corner.vertex) == *reordered.mesh->vertex(corner.vertex));
}
