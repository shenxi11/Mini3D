/*
 * 模块名: SubdivisionTests
 * 功能概述: 验证一到两级 Catmull-Clark 的几何、边界、逐面属性和原始源面映射。
 * 对外接口: Catch2 [subdivision] 纯 CPU 用例
 * 依赖关系: Core modeling、Catch2、GLM，无 Qt/GL
 * 输入输出: Cube、开放面片、多分量及非法输入到独立数值和完整来源断言。
 * 异常与错误: 非法级数、非四边网格、非流形、孤立点和 ID 耗尽必须原子拒绝。
 * 维护说明: 预期值来自标准公式；保留接缝不表示任意跨面 UV 连续编辑能力。
 */
#include "core/modeling/MeshTopology.h"
#include "core/modeling/Subdivision.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <map>
#include <set>
#include <utility>

using namespace mini3d::core::modeling;
namespace {
EditableMesh meshFromFaces(const std::vector<glm::vec3>& positions,
                           const std::vector<std::vector<VertexId>>& loops) {
    EditableMesh mesh;
    for (const auto& position : positions) {
        mesh.vertices.push_back({static_cast<VertexId>(mesh.vertices.size() + 1), position});
    }
    CornerId lastCorner = 0;
    for (const auto& loop : loops) {
        EditableFace face;
        face.id = mesh.faces.size() + 1;
        for (const auto vertex : loop) {
            face.corners.push_back({++lastCorner, vertex});
        }
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}

EditableMesh quad() {
    return meshFromFaces({{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}, {{1, 2, 3, 4}});
}

const EditableVertex& vertexNear(const EditableMesh& mesh, const glm::vec3& position) {
    const auto found = std::find_if(mesh.vertices.begin(), mesh.vertices.end(), [&](const auto& v) {
        return glm::length(v.position - position) < 1.0e-6F;
    });
    REQUIRE(found != mesh.vertices.end());
    return *found;
}

glm::vec3 faceAreaVector(const EditableMesh& mesh, const EditableFace& face) {
    const auto origin = mesh.vertex(face.corners.front().vertex)->position;
    glm::vec3 area(0.0F);
    for (std::size_t i = 0; i < face.corners.size(); ++i) {
        area += glm::cross(
            mesh.vertex(face.corners[i].vertex)->position - origin,
            mesh.vertex(face.corners[(i + 1) % face.corners.size()].vertex)->position - origin);
    }
    return area;
}

void offsetIds(EditableMesh& mesh, std::uint64_t offset) {
    for (auto& vertex : mesh.vertices) {
        vertex.id += offset;
    }
    for (auto& face : mesh.faces) {
        face.id += offset;
        for (auto& corner : face.corners) {
            corner.id += offset;
            corner.vertex += offset;
        }
    }
}

void requireRejected(const EditableMesh& source, int levels = 1) {
    const auto before = source;
    const auto result = evaluateSubdivision(source, levels);
    INFO(result.error);
    REQUIRE_FALSE(result.evaluation);
    REQUIRE_FALSE(result.error.empty());
    REQUIRE(source == before);
}
} // namespace

TEST_CASE("Cube subdivision has the expected closed quad topology at each supported level",
          "[subdivision]") {
    const auto source = createEditableCube();
    const auto before = source;
    for (const int levels : {1, 2}) {
        const auto result = evaluateSubdivision(source, levels);
        INFO(result.error);
        REQUIRE(result.evaluation);
        REQUIRE(result.error.empty());
        const auto& evaluated = *result.evaluation;
        REQUIRE(evaluated.mesh.vertices.size() == (levels == 1 ? 26 : 98));
        REQUIRE(evaluated.mesh.faces.size() == (levels == 1 ? 24 : 96));
        const auto validation = validateEditableMesh(evaluated.mesh);
        REQUIRE(validation.isValid());
        REQUIRE(validation.boundaryEdgeCount == 0);
        REQUIRE(evaluated.derived.triangleSources.size() == evaluated.mesh.faces.size() * 2);
        REQUIRE(evaluated.derived.mesh.indices.size() == evaluated.mesh.faces.size() * 6);
        REQUIRE(evaluated.derived.vertexSources.size() == evaluated.mesh.faces.size() * 4);
        for (const auto& face : evaluated.mesh.faces) {
            REQUIRE(face.corners.size() == 4);
            const auto point = evaluated.mesh.vertex(face.corners.front().vertex)->position;
            REQUIRE(glm::dot(faceAreaVector(evaluated.mesh, face), point) > 0.0F);
        }
    }
    REQUIRE(source == before);
}

TEST_CASE("Cube vertex edge and face points follow Catmull Clark and replace source hard normals",
          "[subdivision]") {
    const auto source = createEditableCube();
    const auto result = evaluateSubdivision(source, 1);
    INFO(result.error);
    REQUIRE(result.evaluation);
    const auto& evaluated = *result.evaluation;
    REQUIRE(glm::length(evaluated.mesh.vertex(1)->position - glm::vec3(-5.0F / 18.0F)) < 1.0e-6F);
    REQUIRE(vertexNear(evaluated.mesh, {0, -.375F, -.375F}).id > 8);
    REQUIRE(vertexNear(evaluated.mesh, {0, 0, .5F}).id > 8);
    const auto expectedNormal = glm::normalize(glm::vec3(-1, -1, -1));
    std::size_t sharedCorners = 0;
    for (const auto& face : evaluated.mesh.faces) {
        for (const auto& corner : face.corners) {
            REQUIRE(corner.normal);
            REQUIRE(std::abs(glm::length(*corner.normal) - 1.0F) < 1.0e-6F);
            if (corner.vertex == 1) {
                REQUIRE(glm::length(*corner.normal - expectedNormal) < 1.0e-6F);
                ++sharedCorners;
            }
        }
    }
    REQUIRE(sharedCorners == 3);
    for (std::size_t i = 0; i < evaluated.derived.vertexSources.size(); ++i) {
        if (evaluated.derived.vertexSources[i].vertex == 1) {
            REQUIRE(glm::length(evaluated.derived.mesh.vertices[i].normal - expectedNormal) <
                    1.0e-6F);
        }
    }
}

TEST_CASE("Open quad uses boundary midpoints and the standard six one one corner rule",
          "[subdivision]") {
    const auto source = quad();
    const auto before = source;
    const auto result = evaluateSubdivision(source, 1);
    INFO(result.error);
    REQUIRE(result.evaluation);
    const auto& evaluated = *result.evaluation;
    REQUIRE(evaluated.mesh.vertices.size() == 9);
    REQUIRE(evaluated.mesh.faces.size() == 4);
    REQUIRE(evaluated.mesh.vertex(1)->position == glm::vec3(.25F, .25F, 0));
    REQUIRE(evaluated.mesh.vertex(2)->position == glm::vec3(1.75F, .25F, 0));
    REQUIRE(evaluated.mesh.vertex(3)->position == glm::vec3(1.75F, 1.75F, 0));
    REQUIRE(evaluated.mesh.vertex(4)->position == glm::vec3(.25F, 1.75F, 0));
    for (const auto& point : {glm::vec3(1, 0, 0), glm::vec3(2, 1, 0), glm::vec3(1, 2, 0),
                              glm::vec3(0, 1, 0), glm::vec3(1, 1, 0)}) {
        REQUIRE(vertexNear(evaluated.mesh, point).id > 4);
    }
    const auto validation = validateEditableMesh(evaluated.mesh);
    REQUIRE(validation.isValid());
    REQUIRE(validation.boundaryEdgeCount == 8);
    const auto topology = buildMeshTopology(evaluated.mesh);
    REQUIRE(topology.topology);
    for (const auto& [id, halves] : topology.topology->vertexHalfEdges) {
        REQUIRE_FALSE(halves.empty());
        std::size_t boundary = 0;
        for (const auto& half : topology.topology->halfEdges) {
            boundary += !half.twin && (half.from == id || half.to == id);
        }
        REQUIRE((boundary == 0 || boundary == 2));
    }
    REQUIRE(source == before);
}

TEST_CASE(
    "Face varying UV color material and winding survive while shared seam geometry stays joined",
    "[subdivision]") {
    auto source = meshFromFaces({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0}},
                                {{1, 2, 5, 4}, {2, 3, 6, 5}});
    const std::array<glm::vec2, 4> uvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    for (std::size_t f = 0; f < source.faces.size(); ++f) {
        auto& face = source.faces[f];
        face.material = f == 0 ? 11 : 22;
        for (std::size_t i = 0; i < 4; ++i) {
            auto& corner = face.corners[i];
            corner.uv = uvs[i] + (f == 0 ? glm::vec2(0) : glm::vec2(10, 20));
            corner.color = {corner.uv.x, corner.uv.y, f == 0 ? .25F : .75F};
            corner.normal = glm::vec3(1, 0, 0); // 合法旧属性，也必须按新几何重算。
        }
    }
    const auto before = source;
    const auto result = evaluateSubdivision(source, 1);
    INFO(result.error);
    REQUIRE(result.evaluation);
    const auto& evaluated = *result.evaluation;
    const auto seamId = vertexNear(evaluated.mesh, {1, .5F, 0}).id;
    std::map<FaceId, glm::vec2> seamUvs;
    std::map<FaceId, std::size_t> seamUses;
    for (const auto& face : evaluated.mesh.faces) {
        const auto origin = evaluated.faces.at(face.id);
        const auto& original = source.faces[origin - 1];
        REQUIRE(face.material == original.material);
        REQUIRE(faceAreaVector(evaluated.mesh, face).z > 0);
        const auto found =
            std::find_if(original.corners.begin(), original.corners.end(), [&](const auto& c) {
                return c.vertex == face.corners.front().vertex;
            });
        REQUIRE(found != original.corners.end());
        const auto index = static_cast<std::size_t>(found - original.corners.begin());
        const auto& next = original.corners[(index + 1) % 4];
        const auto& previous = original.corners[(index + 3) % 4];
        glm::vec2 meanUv(0);
        glm::vec3 meanColor(0);
        for (const auto& corner : original.corners) {
            meanUv += corner.uv / 4.0F;
            meanColor += corner.color / 4.0F;
        }
        REQUIRE(face.corners[0].uv == found->uv);
        REQUIRE(face.corners[0].color == found->color);
        REQUIRE(face.corners[1].uv == (found->uv + next.uv) / 2.0F);
        REQUIRE(face.corners[1].color == (found->color + next.color) / 2.0F);
        REQUIRE(face.corners[2].uv == meanUv);
        REQUIRE(face.corners[2].color == meanColor);
        REQUIRE(face.corners[3].uv == (previous.uv + found->uv) / 2.0F);
        REQUIRE(face.corners[3].color == (previous.color + found->color) / 2.0F);
        for (const auto& corner : face.corners) {
            REQUIRE(corner.normal == glm::vec3(0, 0, 1));
            if (corner.vertex == seamId) {
                seamUvs[origin] = corner.uv;
                ++seamUses[origin];
            }
        }
    }
    REQUIRE(seamUvs.at(1) == glm::vec2(1, .5F));
    REQUIRE(seamUvs.at(2) == glm::vec2(10, 20.5F));
    REQUIRE(seamUses.at(1) == 2);
    REQUIRE(seamUses.at(2) == 2);
    REQUIRE(source == before);
}

TEST_CASE(
    "Two level face origins stay complete and generated identities tolerate reordered wide IDs",
    "[subdivision]") {
    auto source = createEditableCube();
    constexpr auto offset = std::uint64_t{1} << 54;
    offsetIds(source, offset);
    const auto first = evaluateSubdivision(source, 2);
    INFO(first.error);
    REQUIRE(first.evaluation);
    const auto& evaluated = *first.evaluation;
    REQUIRE(evaluated.faces.size() == evaluated.mesh.faces.size());
    std::map<FaceId, std::size_t> descendants;
    std::set<FaceId> finalFaces;
    for (const auto& face : evaluated.mesh.faces) {
        finalFaces.insert(face.id);
        ++descendants[evaluated.faces.at(face.id)];
    }
    REQUIRE(descendants.size() == 6);
    for (const auto& face : source.faces) {
        REQUIRE(descendants.at(face.id) == 16);
        REQUIRE_FALSE(finalFaces.contains(face.id));
    }
    for (const auto& triangle : evaluated.derived.triangleSources) {
        REQUIRE(finalFaces.contains(triangle.face));
        REQUIRE(descendants.contains(evaluated.faces.at(triangle.face)));
    }
    std::reverse(source.vertices.begin(), source.vertices.end());
    std::reverse(source.faces.begin(), source.faces.end());
    const auto second = evaluateSubdivision(source, 2);
    INFO(second.error);
    REQUIRE(second.evaluation);
    REQUIRE(second.evaluation->faces == evaluated.faces);
    REQUIRE(second.evaluation->mesh == evaluated.mesh);
}

TEST_CASE("Independent coincident quad components are subdivided without welding their identities",
          "[subdivision]") {
    auto source = quad();
    auto second = quad();
    offsetIds(second, 100);
    source.vertices.insert(source.vertices.end(), second.vertices.begin(), second.vertices.end());
    source.faces.insert(source.faces.end(), second.faces.begin(), second.faces.end());
    const auto before = source;
    const auto result = evaluateSubdivision(source, 1);
    INFO(result.error);
    REQUIRE(result.evaluation);
    const auto& evaluated = *result.evaluation;
    REQUIRE(evaluated.mesh.vertices.size() == 18);
    REQUIRE(evaluated.mesh.faces.size() == 8);
    REQUIRE(evaluated.mesh.vertex(1)->position == evaluated.mesh.vertex(101)->position);
    const auto validation = validateEditableMesh(evaluated.mesh);
    REQUIRE(validation.isValid());
    REQUIRE(validation.boundaryEdgeCount == 16);
    std::map<FaceId, std::set<VertexId>> usedVertices;
    for (const auto& face : evaluated.mesh.faces) {
        for (const auto& corner : face.corners) {
            usedVertices[evaluated.faces.at(face.id)].insert(corner.vertex);
        }
    }
    REQUIRE(usedVertices.at(1).size() == 9);
    REQUIRE(usedVertices.at(101).size() == 9);
    for (const auto vertex : usedVertices.at(1)) {
        REQUIRE_FALSE(usedVertices.at(101).contains(vertex));
    }
    REQUIRE(source == before);
}

TEST_CASE(
    "Unsupported levels topology and exhausted identities are rejected without partial results",
    "[subdivision]") {
    const auto cube = createEditableCube();
    for (const auto levels : {-1, 0, 3, std::numeric_limits<int>::max()}) {
        requireRejected(cube, levels);
    }
    requireRejected({});
    const auto triangle = meshFromFaces({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {{1, 2, 3}});
    requireRejected(triangle);
    const auto ngon =
        meshFromFaces({{0, 0, 0}, {2, 0, 0}, {3, 1, 0}, {2, 2, 0}, {0, 2, 0}}, {{1, 2, 3, 4, 5}});
    requireRejected(ngon);
    auto isolated = quad();
    isolated.vertices.push_back({55, {3, 3, 0}});
    requireRejected(isolated);
    const auto edgeFan = meshFromFaces(
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, -1, 0}, {1, -1, 0}, {1, 0, 1}, {0, 0, 1}},
        {{1, 2, 3, 4}, {2, 1, 5, 6}, {1, 2, 7, 8}});
    REQUIRE(validateEditableMesh(edgeFan).error == MeshError::NonManifoldEdge);
    requireRejected(edgeFan);
    const auto vertexFan = meshFromFaces(
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {-1, 0, 0}, {-1, -1, 0}, {0, -1, 0}},
        {{1, 2, 3, 4}, {1, 5, 6, 7}});
    REQUIRE(validateEditableMesh(vertexFan).error == MeshError::NonManifoldVertex);
    requireRejected(vertexFan);
    auto winding = meshFromFaces({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 1, 0}, {1, 1, 0}, {2, 1, 0}},
                                 {{1, 2, 5, 4}, {2, 3, 6, 5}});
    std::reverse(winding.faces.back().corners.begin(), winding.faces.back().corners.end());
    REQUIRE(validateEditableMesh(winding).error == MeshError::InconsistentWinding);
    requireRejected(winding);
    constexpr auto maxId = std::numeric_limits<std::uint64_t>::max();
    for (int domain = 0; domain < 3; ++domain) {
        auto exhausted = quad();
        if (domain == 0) {
            exhausted.vertices.front().id = maxId;
            exhausted.faces.front().corners.front().vertex = maxId;
        } else if (domain == 1) {
            exhausted.faces.front().id = maxId;
        } else {
            exhausted.faces.front().corners.front().id = maxId;
        }
        requireRejected(exhausted);
    }
    auto secondLevelExhausted = quad();
    secondLevelExhausted.faces.front().id = maxId - 4;
    REQUIRE(evaluateSubdivision(secondLevelExhausted, 1).evaluation);
    requireRejected(secondLevelExhausted, 2);
}
