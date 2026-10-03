/*
 * 模块名: MeshDerivationTests
 * 功能概述: 验证半边邻接、凸/凹环三角化、属性保持和稳定来源映射。
 * 对外接口: Catch2 [mesh-derivation] 纯 CPU 用例
 * 依赖关系: Core modeling、Catch2、GLM
 * 输入输出: 合法/不支持的面形状到邻接、三角几何与原子失败断言。
 * 异常与错误: 内部对角线泄露为源边、映射丢失、翻面/零面积或半成品返回即失败。
 * 维护说明: 不依赖 Qt/GL，不把简单面投影校验当作全局自交检测。
 */
#include "core/modeling/MeshDerivation.h"
#include "core/modeling/MeshTopology.h"
#include "core/modeling/MeshValidation.h"
#include "renderer_gl/PrimitiveFactory.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <numbers>
#include <set>

using namespace mini3d::core::modeling;
namespace {
EditableMesh polygon(const std::vector<glm::vec3>& points) {
    EditableMesh mesh;
    EditableFace face;
    face.id = 350;
    face.material = 77;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto id = std::uint64_t{1} << 42 | (i + 1);
        mesh.vertices.push_back({id, points[i]});
        MeshCorner corner;
        corner.id = id + 800;
        corner.vertex = id;
        corner.uv = {static_cast<float>(i), -0.25F};
        face.corners.push_back(corner);
    }
    mesh.faces.push_back(std::move(face));
    return mesh;
}

bool containsPoint(const std::vector<glm::vec3>& points, const glm::dvec3& point) {
    bool inside = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const glm::dvec3 a(points[i]);
        const glm::dvec3 b(points[j]);
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

void requireTriangulatedArea(const std::vector<glm::vec3>& points) {
    const auto source = polygon(points);
    const auto result = deriveMesh(source);
    INFO(result.error);
    REQUIRE(result.derived.has_value());
    const auto& derived = *result.derived;
    REQUIRE(derived.mesh.vertices.size() == points.size());
    REQUIRE(derived.mesh.indices.size() == 3 * (points.size() - 2));
    double twicePolygonArea = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const glm::dvec3 a(points[i]);
        const glm::dvec3 b(points[(i + 1) % points.size()]);
        twicePolygonArea += a.x * b.y - a.y * b.x;
    }
    const auto sign = twicePolygonArea > 0 ? 1 : -1;
    double twiceTriangleArea = 0.0;
    for (std::size_t i = 0; i < derived.mesh.indices.size(); i += 3) {
        const glm::dvec3 a(derived.mesh.vertices[derived.mesh.indices[i]].position);
        const glm::dvec3 b(derived.mesh.vertices[derived.mesh.indices[i + 1]].position);
        const glm::dvec3 c(derived.mesh.vertices[derived.mesh.indices[i + 2]].position);
        const auto normal = glm::cross(b - a, c - a);
        REQUIRE(sign * normal.z > 0.0);
        REQUIRE(containsPoint(points, (a + b + c) / 3.0));
        twiceTriangleArea += normal.z;
    }
    REQUIRE(twiceTriangleArea == Catch::Approx(twicePolygonArea).epsilon(1.0e-6));
}
} // namespace

TEST_CASE("Rebuilt half edges retain source identity and form paired cube rings",
          "[mesh-derivation]") {
    const auto source = createEditableCube();
    const auto before = source;
    const auto result = buildMeshTopology(source);
    REQUIRE(result.topology.has_value());
    REQUIRE(result.error.empty());
    const auto& topology = *result.topology;
    REQUIRE(topology.halfEdges.size() == 24);
    REQUIRE(topology.edgeHalfEdges.size() == 12);
    REQUIRE(topology.faceHalfEdges.size() == 6);
    REQUIRE(topology.vertexHalfEdges.size() == 8);
    for (std::size_t i = 0; i < topology.halfEdges.size(); ++i) {
        const auto& edge = topology.halfEdges[i];
        REQUIRE(edge.twin.has_value());
        const auto& twin = topology.halfEdges[*edge.twin];
        REQUIRE(twin.twin == i);
        REQUIRE(twin.from == edge.to);
        REQUIRE(twin.to == edge.from);
        REQUIRE(twin.face != edge.face);
        REQUIRE(topology.halfEdges[edge.next].previous == i);
        REQUIRE(topology.halfEdges[edge.previous].next == i);
        REQUIRE(topology.halfEdges[edge.next].from == edge.to);
        REQUIRE(topology.halfEdges[edge.next].face == edge.face);
    }
    for (const auto& face : source.faces) {
        auto index = topology.faceHalfEdges.at(face.id);
        const auto first = index;
        for (const auto& corner : face.corners) {
            REQUIRE(topology.halfEdges[index].corner == corner.id);
            REQUIRE(topology.halfEdges[index].from == corner.vertex);
            index = topology.halfEdges[index].next;
        }
        REQUIRE(index == first);
    }
    for (const auto& vertex : source.vertices) {
        REQUIRE(topology.vertexHalfEdges.at(vertex.id).size() == 3);
    }
    REQUIRE(source == before);
}

TEST_CASE("Topology exposes boundaries isolated vertices and refuses invalid input atomically",
          "[mesh-derivation]") {
    auto source = createEditableCube();
    source.faces.resize(1);
    const auto result = buildMeshTopology(source);
    REQUIRE(result.topology.has_value());
    REQUIRE(result.topology->halfEdges.size() == 4);
    REQUIRE(result.topology->edgeHalfEdges.size() == 4);
    for (const auto& edge : result.topology->halfEdges) {
        REQUIRE_FALSE(edge.twin.has_value());
    }
    REQUIRE(result.topology->vertexHalfEdges.at(1).empty());
    source.faces[0].corners[0].vertex = 999;
    const auto before = source;
    const auto failed = buildMeshTopology(source);
    REQUIRE_FALSE(failed.topology.has_value());
    REQUIRE_FALSE(failed.error.empty());
    REQUIRE(source == before);
    REQUIRE(buildMeshTopology({}).topology->halfEdges.empty());
}

TEST_CASE("Cube render splitting preserves per-corner data and triangle source maps",
          "[mesh-derivation]") {
    auto source = createEditableCube();
    source.faces[0].material = 1234;
    source.faces[0].corners[0].uv = {-4, 3};
    source.faces[0].corners[0].normal = glm::vec3(0, 0, 8);
    source.faces[0].corners[0].color = {0.2F, 0.4F, 0.8F};
    const auto before = source;
    const auto result = deriveMesh(source);
    REQUIRE(result.derived.has_value());
    const auto& derived = *result.derived;
    REQUIRE(derived.mesh.vertices.size() == 24);
    REQUIRE(derived.mesh.indices.size() == 36);
    REQUIRE(derived.vertexSources.size() == 24);
    REQUIRE(derived.triangleSources.size() == 12);
    const auto topology = buildMeshTopology(source);
    REQUIRE(topology.topology->edgeHalfEdges.size() == 12);
    std::set<EdgeKey> triangulatedEdges;
    for (std::size_t i = 0; i < derived.mesh.indices.size(); i += 3) {
        const auto mapping = derived.triangleSources[i / 3];
        const auto found =
            std::find_if(source.faces.begin(), source.faces.end(), [mapping](const auto& face) {
                return face.id == mapping.face;
            });
        REQUIRE(found != source.faces.end());
        REQUIRE(mapping.material == found->material);
        for (std::size_t j = 0; j < 3; ++j) {
            const auto index = derived.mesh.indices[i + j];
            REQUIRE(index < derived.mesh.vertices.size());
            const auto sourceId = derived.vertexSources[index];
            const auto corner = std::find_if(found->corners.begin(), found->corners.end(),
                                             [sourceId](const auto& c) {
                                                 return c.id == sourceId.corner;
                                             });
            REQUIRE(corner != found->corners.end());
            REQUIRE(corner->vertex == sourceId.vertex);
            const auto& vertex = derived.mesh.vertices[index];
            REQUIRE(vertex.position == source.vertex(sourceId.vertex)->position);
            REQUIRE(vertex.uv == corner->uv);
            REQUIRE(vertex.color == corner->color);
            REQUIRE(glm::length(vertex.normal) == Catch::Approx(1.0F));
            triangulatedEdges.emplace(
                sourceId.vertex,
                derived.vertexSources[derived.mesh.indices[i + (j + 1) % 3]].vertex);
        }
        const auto& a = derived.mesh.vertices[derived.mesh.indices[i]];
        const auto& b = derived.mesh.vertices[derived.mesh.indices[i + 1]];
        const auto& c = derived.mesh.vertices[derived.mesh.indices[i + 2]];
        REQUIRE(glm::dot(glm::cross(b.position - a.position, c.position - a.position), a.normal) >
                0);
    }
    REQUIRE(triangulatedEdges.size() == 18);
    REQUIRE(derived.mesh.bounds().minimum == glm::vec3(-0.5F));
    REQUIRE(derived.mesh.bounds().maximum == glm::vec3(0.5F));
    REQUIRE(source == before);
}

TEST_CASE("Ear clipping handles convex concave reversed and collinear boundary polygons",
          "[mesh-derivation]") {
    std::vector<glm::vec3> concave{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {1, 0.5F, 0}, {0, 2, 0}};
    requireTriangulatedArea(concave);
    std::reverse(concave.begin(), concave.end());
    requireTriangulatedArea(concave);
    requireTriangulatedArea({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}});
    for (int count = 3; count <= 20; ++count) {
        std::vector<glm::vec3> star;
        for (int i = 0; i < count; ++i) {
            const auto angle = i * 2.0F * std::numbers::pi_v<float> / count;
            const auto radius = i % 2 ? 0.6F : 1.0F;
            star.emplace_back(radius * std::cos(angle), radius * std::sin(angle), 0);
        }
        requireTriangulatedArea(star);
    }
}

TEST_CASE("Derived native cube retains legacy appearance and calculated normals on all axes",
          "[mesh-derivation]") {
    auto source = createEditableCube();
    const auto derived = deriveMesh(source);
    REQUIRE(derived.derived.has_value());
    const auto original = mini3d::renderer_gl::PrimitiveFactory::createCube();
    REQUIRE(derived.derived->mesh.vertices.size() == original.vertices.size());
    for (std::size_t i = 0; i < original.vertices.size(); ++i) {
        const auto& expected = original.vertices[i];
        const auto& actual = derived.derived->mesh.vertices[i];
        REQUIRE(actual.position == expected.position);
        REQUIRE(actual.normal == expected.normal);
        REQUIRE(actual.color == expected.color);
        REQUIRE(actual.uv == expected.uv);
    }
    for (auto& face : source.faces) {
        for (auto& corner : face.corners) {
            corner.normal.reset();
        }
    }
    const auto calculated = deriveMesh(source);
    REQUIRE(calculated.derived.has_value());
    for (const auto& vertex : calculated.derived->mesh.vertices) {
        REQUIRE(glm::dot(vertex.normal, vertex.position) == Catch::Approx(0.5F));
    }
}

TEST_CASE("Derivation preserves sparse 64 bit mapping and handles nonplanar and scaled quads",
          "[mesh-derivation]") {
    for (const auto scale : {1.0e-20F, 1.0F, 1.0e20F}) {
        auto source = polygon({{0, 0, 0}, {2, 0, 0}, {2, 2, 0.3F}, {0, 2, 0}});
        for (auto& vertex : source.vertices) {
            vertex.position *= scale;
        }
        std::reverse(source.vertices.begin(), source.vertices.end());
        const auto result = deriveMesh(source);
        INFO(result.error);
        REQUIRE(result.derived.has_value());
        REQUIRE(result.derived->mesh.indices.size() == 6);
        REQUIRE(result.derived->mesh.vertices.size() == 4);
        for (std::size_t i = 0; i < 4; ++i) {
            const auto mapping = result.derived->vertexSources[i];
            const auto& vertex = result.derived->mesh.vertices[i];
            REQUIRE(mapping.vertex > std::numeric_limits<std::uint32_t>::max());
            REQUIRE(vertex.position == source.vertex(mapping.vertex)->position);
            REQUIRE(mapping.corner == source.faces[0].corners[i].id);
            REQUIRE(glm::length(vertex.normal) == Catch::Approx(1.0F));
            REQUIRE(glm::dot(vertex.normal, glm::vec3(0, 0, 1)) > 0.9F);
        }
    }
}

TEST_CASE("Unsupported projections never publish partial triangles or change the source",
          "[mesh-derivation]") {
    std::vector<std::vector<glm::vec3>> invalid{
        {{0, 0, 0}, {3, 2, 0}, {0, 2, 0}, {2, 0, 0}},
        {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {1, 0, 0}, {0, 2, 0}},
        {{0, 0, 0}, {2, 0, 0}, {1, 0, 0}, {2, 2, 0}, {0, 2, 0}}};
    for (const auto& points : invalid) {
        auto source = polygon(points);
        REQUIRE(validateEditableMesh(source).isValid());
        // 前面放一张可三角化的面，后续失败也不能返回这一部分候选。
        auto good = createEditableCube();
        source.vertices.insert(source.vertices.end(), good.vertices.begin(), good.vertices.end());
        source.faces.insert(source.faces.begin(), good.faces[0]);
        const auto before = source;
        const auto result = deriveMesh(source);
        REQUIRE_FALSE(result.derived.has_value());
        REQUIRE_FALSE(result.error.empty());
        REQUIRE(source == before);
    }
    auto broken = createEditableCube();
    broken.faces[0].corners[0].vertex = 0;
    REQUIRE_FALSE(deriveMesh(broken).derived.has_value());
    const auto empty = deriveMesh({});
    REQUIRE(empty.derived.has_value());
    REQUIRE(empty.derived->mesh.indices.empty());
    REQUIRE_FALSE(empty.derived->mesh.bounds().isValid());
}

TEST_CASE("Mixed face sizes retain exact ear order and attributes after sparse source reordering",
          "[mesh-derivation][perf-regression]") {
    const std::vector<std::vector<glm::vec3>> polygons{
        {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {1, 0.5F, 0}, {0, 2, 0}},
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}},
        {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}},
        {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}}};
    const std::vector<std::vector<std::uint32_t>> expectedIndices{
        {1, 2, 3, 0, 1, 3, 0, 3, 4}, {0, 1, 2}, {3, 0, 1, 1, 2, 3},
        {4, 0, 1, 4, 1, 2, 2, 3, 4}, {0, 1, 2}};
    constexpr FaceId facePrefix = std::uint64_t{1} << 54;
    EditableMesh source;
    for (std::size_t i = 0; i < polygons.size(); ++i) {
        auto next = polygon(polygons[i]);
        for (auto& vertex : next.vertices) {
            vertex.id += i * 10000;
            vertex.position.x += static_cast<float>(i) * 10.0F;
        }
        auto& face = next.faces[0];
        face.id = facePrefix + i * 77;
        face.material = 500 + i;
        for (std::size_t j = 0; j < face.corners.size(); ++j) {
            auto& corner = face.corners[j];
            corner.id += i * 10000;
            corner.vertex += i * 10000;
            corner.uv = {static_cast<float>(i), static_cast<float>(j)};
            corner.color = {0.1F * static_cast<float>(i + 1), 0.2F, 0.3F};
            if (j == 0) {
                corner.normal = glm::vec3(0, 0, i == 4 ? -3 : 3);
            }
        }
        source.vertices.insert(source.vertices.end(), next.vertices.begin(), next.vertices.end());
        source.faces.push_back(std::move(face));
    }
    std::reverse(source.vertices.begin(), source.vertices.end());
    std::reverse(source.faces.begin(), source.faces.end());
    const auto before = source;
    const auto result = deriveMesh(source);
    INFO(result.error);
    REQUIRE(result.derived.has_value());
    const auto& derived = *result.derived;
    REQUIRE(derived.mesh.vertices.size() == 20);
    REQUIRE(derived.vertexSources.size() == 20);
    REQUIRE(derived.mesh.indices.size() == 30);
    REQUIRE(derived.triangleSources.size() == 10);
    std::size_t vertexOffset = 0;
    std::size_t indexOffset = 0;
    std::size_t triangleOffset = 0;
    for (const auto& face : source.faces) {
        const auto originalIndex = static_cast<std::size_t>((face.id - facePrefix) / 77);
        const auto& indices = expectedIndices[originalIndex];
        for (std::size_t i = 0; i < indices.size(); ++i) {
            REQUIRE(derived.mesh.indices[indexOffset + i] == vertexOffset + indices[i]);
        }
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            const auto& corner = face.corners[i];
            const auto& mapping = derived.vertexSources[vertexOffset + i];
            const auto& vertex = derived.mesh.vertices[vertexOffset + i];
            REQUIRE(mapping.vertex == corner.vertex);
            REQUIRE(mapping.corner == corner.id);
            REQUIRE(vertex.position == source.vertex(corner.vertex)->position);
            REQUIRE(vertex.uv == corner.uv);
            REQUIRE(vertex.color == corner.color);
            REQUIRE(vertex.normal == glm::vec3(0, 0, originalIndex == 4 ? -1 : 1));
        }
        for (std::size_t i = 0; i < indices.size() / 3; ++i) {
            const auto& mapping = derived.triangleSources[triangleOffset + i];
            REQUIRE(mapping.face == face.id);
            REQUIRE(mapping.material == face.material);
        }
        vertexOffset += face.corners.size();
        indexOffset += indices.size();
        triangleOffset += indices.size() / 3;
    }
    REQUIRE(source == before);
}

TEST_CASE("Shared face geometry preserves complete validation errors at different scales",
          "[mesh-derivation][perf-regression]") {
    for (const auto scale : {1.0e-20F, 1.0F, 1.0e20F}) {
        for (int failure = 0; failure < 4; ++failure) {
            auto source = polygon({{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}});
            for (auto& vertex : source.vertices) {
                vertex.position *= scale;
            }
            switch (failure) {
                case 0:
                    source.vertices[1].position = source.vertices[0].position;
                    break;
                case 1:
                    source.vertices[2].position.y = scale * 1.0e-12F;
                    source.vertices[3].position.y = scale * 1.0e-12F;
                    break;
                case 2:
                    source.faces[0].corners[1].normal = glm::vec3(0);
                    break;
                case 3:
                    source.vertices[0].position.x = std::numeric_limits<float>::quiet_NaN();
                    break;
            }
            std::vector<glm::dvec3> positions;
            for (const auto& corner : source.faces[0].corners) {
                positions.emplace_back(source.vertex(corner.vertex)->position);
            }
            const auto complete = validateEditableMesh(source);
            const auto face = validateFaceGeometry(source.faces[0], positions);
            INFO("Scale " << scale << ", geometry failure " << failure);
            REQUIRE_FALSE(complete.isValid());
            REQUIRE(face.error == complete.error);
            REQUIRE(face.message == complete.message);
            const auto derived = deriveMesh(source);
            REQUIRE_FALSE(derived.derived);
            REQUIRE(derived.error == complete.message);
        }
    }
}
