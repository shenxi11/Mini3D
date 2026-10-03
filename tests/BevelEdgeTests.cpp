/*
 * 模块名: BevelEdgeTests
 * 功能概述: 验证单段外凸边倒角的端部闭合、真实垂距、属性保留和原子拒绝。
 * 对外接口: Catch2 [bevel-edge]；依赖关系: Core、Catch2，无Qt/GL。
 * 输入输出: Cube、棱柱及不支持角部到几何、流形、稳定ID断言。
 * 异常与错误: 凹边/共面边/边界/四价角/非共面/无效宽度/ID不足必须拒绝。
 * 维护说明: 距离按源面法线独立测量，端面必须共享真实切口边，不接受开口条带。
 */
#include "core/modeling/BevelEdge.h"
#include "core/modeling/MeshDerivation.h"
#include "core/modeling/MeshTopology.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>

using namespace mini3d::core::modeling;
namespace {
const EditableFace& faceById(const EditableMesh& mesh, FaceId id) {
    const auto found = std::find_if(mesh.faces.begin(), mesh.faces.end(), [id](const auto& face) {
        return face.id == id;
    });
    REQUIRE(found != mesh.faces.end());
    return *found;
}
bool hasVertex(const EditableFace& face, VertexId id) {
    return std::any_of(face.corners.begin(), face.corners.end(), [id](const auto& corner) {
        return corner.vertex == id;
    });
}
glm::dvec3 faceNormal(const EditableMesh& mesh, const EditableFace& face) {
    const auto origin = glm::dvec3(mesh.vertex(face.corners.front().vertex)->position);
    glm::dvec3 area{0};
    for (std::size_t i = 0; i < face.corners.size(); ++i) {
        const auto next = (i + 1) % face.corners.size();
        area += glm::cross(glm::dvec3(mesh.vertex(face.corners[i].vertex)->position) - origin,
                           glm::dvec3(mesh.vertex(face.corners[next].vertex)->position) - origin);
    }
    return glm::normalize(area);
}
void requireEqualWidth(const EditableMesh& before, const EditableMesh& after, EdgeKey edge,
                       double width, double tolerance = 1.0e-6) {
    std::size_t checkedFaces = 0;
    for (const auto& face : before.faces) {
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            const auto next = (i + 1) % face.corners.size();
            if (EdgeKey(face.corners[i].vertex, face.corners[next].vertex) != edge)
                continue;
            const auto a = glm::dvec3(before.vertex(face.corners[i].vertex)->position);
            const auto b = glm::dvec3(before.vertex(face.corners[next].vertex)->position);
            const auto normal = faceNormal(before, face);
            const auto inward = glm::normalize(glm::cross(normal, b - a));
            std::size_t newCorners = 0;
            for (const auto& corner : faceById(after, face.id).corners) {
                if (before.vertex(corner.vertex))
                    continue;
                ++newCorners;
                const auto point = glm::dvec3(after.vertex(corner.vertex)->position);
                REQUIRE(std::abs(glm::dot(inward, point - a) - width) < tolerance);
                REQUIRE(std::abs(glm::dot(normal, point - a)) < tolerance);
            }
            REQUIRE(newCorners == 2);
            ++checkedFaces;
        }
    }
    REQUIRE(checkedFaces == 2);
}
EditableMesh prism(const std::vector<glm::vec2>& polygon) {
    EditableMesh mesh;
    const auto count = static_cast<VertexId>(polygon.size());
    for (VertexId layer = 0; layer < 2; ++layer) {
        for (VertexId i = 0; i < count; ++i)
            mesh.vertices.push_back(
                {layer * count + i + 1, {polygon[i].x, polygon[i].y, static_cast<float>(layer)}});
    }
    CornerId nextCorner = 0;
    const auto addFace = [&](const std::vector<VertexId>& loop) {
        EditableFace face;
        face.id = mesh.faces.size() + 1;
        for (const auto vertex : loop)
            face.corners.push_back({++nextCorner, vertex});
        mesh.faces.push_back(std::move(face));
    };
    std::vector<VertexId> bottom, top;
    for (VertexId i = 0; i < count; ++i) {
        bottom.push_back(count - i);
        top.push_back(count + i + 1);
    }
    addFace(bottom);
    addFace(top);
    for (VertexId i = 0; i < count; ++i) {
        const auto a = i + 1, b = (i + 1) % count + 1;
        addFace({a, b, b + count, a + count});
    }
    return mesh;
}
} // namespace

TEST_CASE("Every cube edge bevel closes both end faces and keeps two incident faces per edge",
          "[bevel-edge]") {
    const auto before = createEditableCube();
    const auto topology = buildMeshTopology(before);
    REQUIRE(topology.topology);
    for (const auto& [edge, half] : topology.topology->edgeHalfEdges) {
        INFO(edge.first);
        INFO(edge.second);
        const auto analysis = analyzeBevelEdge(before, edge);
        REQUIRE(analysis.bevel);
        REQUIRE(analysis.bevel->edge == edge);
        REQUIRE(std::abs(analysis.bevel->maximumWidth - 1) < 1.0e-12);
        const auto result = bevelEdge(before, edge, .2);
        INFO(result.error);
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->vertices.size() == 10);
        REQUIRE(result.mesh->faces.size() == 7);
        REQUIRE_FALSE(result.mesh->vertex(edge.first));
        REQUIRE_FALSE(result.mesh->vertex(edge.second));
        REQUIRE(faceById(*result.mesh, result.bevelFace).corners.size() == 4);
        const auto validation = validateEditableMesh(*result.mesh);
        REQUIRE(validation.isValid());
        REQUIRE(validation.edgeCount == 15);
        REQUIRE(validation.boundaryEdgeCount == 0);
        const auto changed = buildMeshTopology(*result.mesh);
        REQUIRE(changed.topology);
        for (const auto& item : changed.topology->halfEdges)
            REQUIRE(item.twin);
        for (const auto& vertex : result.mesh->vertices) {
            if (before.vertex(vertex.id))
                REQUIRE(vertex == *before.vertex(vertex.id));
            else
                REQUIRE(changed.topology->vertexHalfEdges.at(vertex.id).size() == 3);
        }
        std::size_t endFaces = 0, untouchedFaces = 0;
        for (const auto& face : before.faces) {
            const auto& after = faceById(*result.mesh, face.id);
            const auto affected = static_cast<int>(hasVertex(face, edge.first)) +
                                  static_cast<int>(hasVertex(face, edge.second));
            REQUIRE(after.material == face.material);
            if (affected == 0) {
                ++untouchedFaces;
                REQUIRE(after == face);
            } else if (affected == 1) {
                ++endFaces;
                REQUIRE(after.corners.size() == face.corners.size() + 1);
            } else {
                REQUIRE(after.corners.size() == face.corners.size());
            }
        }
        REQUIRE(endFaces == 2);
        REQUIRE(untouchedFaces == 2);
        requireEqualWidth(before, *result.mesh, edge, .2);
        REQUIRE(deriveMesh(*result.mesh).derived);
    }
    REQUIRE(before == createEditableCube());
}

TEST_CASE("Bevel width is perpendicular distance and per face attributes survive on sheared faces",
          "[bevel-edge]") {
    auto before = createEditableCube();
    for (auto& vertex : before.vertices) {
        const auto p = vertex.position;
        vertex.position = {2 * p.x + .75F * p.y + .5F * p.z, 3 * p.y, 4 * p.z};
    }
    for (auto& face : before.faces) {
        face.material = face.id + 100;
        for (auto& corner : face.corners) {
            const auto p = before.vertex(corner.vertex)->position;
            corner.uv = {p.x * .3F + static_cast<float>(face.id), p.y * .7F + p.z * .2F};
            corner.color = {p.x * .1F, p.y * .2F, p.z * .3F + static_cast<float>(face.id)};
        }
    }
    const auto original = before;
    const auto analysis = analyzeBevelEdge(before, {5, 6});
    REQUIRE(analysis.bevel);
    REQUIRE(std::abs(analysis.bevel->maximumWidth - 3) < 1.0e-12);
    const auto result = bevelEdge(before, {5, 6}, .6);
    INFO(result.error);
    REQUIRE(result.mesh);
    requireEqualWidth(before, *result.mesh, {5, 6}, .6);
    for (const auto& source : before.faces) {
        const auto& after = faceById(*result.mesh, source.id);
        const bool affected = hasVertex(source, 5) || hasVertex(source, 6);
        REQUIRE(after.material == source.material);
        for (const auto& corner : after.corners) {
            const auto p = result.mesh->vertex(corner.vertex)->position;
            const auto uv =
                glm::vec2(p.x * .3F + static_cast<float>(source.id), p.y * .7F + p.z * .2F);
            const auto color =
                glm::vec3(p.x * .1F, p.y * .2F, p.z * .3F + static_cast<float>(source.id));
            REQUIRE(glm::length(corner.uv - uv) < 1.0e-5F);
            REQUIRE(glm::length(corner.color - color) < 1.0e-5F);
            if (affected)
                REQUIRE_FALSE(corner.normal);
            if (before.vertex(corner.vertex)) {
                const auto old = std::find_if(source.corners.begin(), source.corners.end(),
                                              [&corner](const auto& item) {
                                                  return item.vertex == corner.vertex;
                                              });
                REQUIRE(old != source.corners.end());
                REQUIRE(corner.id == old->id);
                REQUIRE(corner.uv == old->uv);
                REQUIRE(corner.color == old->color);
            } else {
                REQUIRE(corner.id > 24);
            }
        }
    }
    const auto& bevel = faceById(*result.mesh, result.bevelFace);
    REQUIRE(bevel.material == 101);
    for (const auto& corner : bevel.corners) {
        REQUIRE_FALSE(corner.normal);
        bool matched = false;
        for (const auto id : {FaceId(1), FaceId(6)}) {
            for (const auto& side : faceById(*result.mesh, id).corners) {
                if (side.vertex != corner.vertex)
                    continue;
                REQUIRE(corner.uv == side.uv);
                REQUIRE(corner.color == side.color);
                REQUIRE(corner.id != side.id);
                matched = true;
            }
        }
        REQUIRE(matched);
    }
    const auto derived = deriveMesh(*result.mesh);
    REQUIRE(derived.derived);
    const auto expectedNormal = glm::normalize(glm::vec3(0, -1, 1));
    for (std::size_t i = 0; i < derived.derived->vertexSources.size(); ++i) {
        const auto id = derived.derived->vertexSources[i].corner;
        if (std::any_of(bevel.corners.begin(), bevel.corners.end(), [id](const auto& corner) {
                return corner.id == id;
            }))
            REQUIRE(glm::length(derived.derived->mesh.vertices[i].normal - expectedNormal) <
                    1.0e-6F);
    }
    REQUIRE(before == original);
}

TEST_CASE("Bevel local units are scale invariant and independent of vertex and face order",
          "[bevel-edge]") {
    for (const float scale : {1.0e-4F, 1.F, 1.0e4F}) {
        auto before = createEditableCube();
        for (auto& vertex : before.vertices) {
            const auto p = vertex.position;
            vertex.position = (glm::vec3(p.x, p.y * .6F + p.z * .8F, p.z * .6F - p.y * .8F) +
                               glm::vec3(5, -7, 11)) *
                              scale;
        }
        const auto analysis = analyzeBevelEdge(before, {6, 7});
        INFO(analysis.error);
        REQUIRE(analysis.bevel);
        REQUIRE(std::abs(analysis.bevel->maximumWidth - scale) < scale * 2.0e-6);
        const auto result = bevelEdge(before, {6, 7}, .2 * scale);
        INFO(result.error);
        REQUIRE(result.mesh);
        requireEqualWidth(before, *result.mesh, {6, 7}, .2 * scale, scale * 3.0e-6);
        REQUIRE(validateEditableMesh(*result.mesh).boundaryEdgeCount == 0);
        std::reverse(before.vertices.begin(), before.vertices.end());
        std::reverse(before.faces.begin(), before.faces.end());
        const auto reordered = bevelEdge(before, {6, 7}, .2 * scale);
        INFO(reordered.error);
        REQUIRE(reordered.mesh);
        REQUIRE(reordered.bevelFace == result.bevelFace);
        for (const auto& vertex : result.mesh->vertices)
            REQUIRE(*reordered.mesh->vertex(vertex.id) == vertex);
        for (const auto& face : result.mesh->faces)
            REQUIRE(faceById(*reordered.mesh, face.id) == face);
    }
    const auto triangular = prism({{0, 0}, {3, 0}, {0, 4}});
    const auto result = bevelEdge(triangular, {1, 4}, .5);
    INFO(result.error);
    REQUIRE(result.mesh);
    REQUIRE(result.mesh->vertices.size() == 8);
    REQUIRE(result.mesh->faces.size() == 6);
    REQUIRE(validateEditableMesh(*result.mesh).boundaryEdgeCount == 0);
    requireEqualWidth(triangular, *result.mesh, {1, 4}, .5);
}

TEST_CASE("Bevel distinguishes a true concave edge from convex and rejects coplanar faces",
          "[bevel-edge]") {
    const auto concave = prism({{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}});
    REQUIRE(deriveMesh(concave).derived);
    const auto analysis = analyzeBevelEdge(concave, {4, 10});
    REQUIRE_FALSE(analysis.bevel);
    REQUIRE(analysis.error.find("凹边") != std::string::npos);
    REQUIRE_FALSE(bevelEdge(concave, {4, 10}, .1).mesh);
    const auto coplanar = prism({{0, 0}, {1, 0}, {2, 0}, {2, 2}, {0, 2}});
    REQUIRE(deriveMesh(coplanar).derived);
    const auto flat = analyzeBevelEdge(coplanar, {2, 7});
    REQUIRE_FALSE(flat.bevel);
    REQUIRE(flat.error.find("共面边") != std::string::npos);
    REQUIRE_FALSE(bevelEdge(coplanar, {2, 7}, .1).mesh);
}

TEST_CASE("Bevel rejects boundary nonmanifold four valence and nonplanar neighborhoods",
          "[bevel-edge]") {
    auto boundary = createEditableCube();
    boundary.faces.pop_back();
    REQUIRE(deriveMesh(boundary).derived);
    REQUIRE_FALSE(analyzeBevelEdge(boundary, {5, 6}).bevel);
    REQUIRE_FALSE(bevelEdge(boundary, {5, 6}, .1).mesh);
    auto pole = createEditableCube();
    const auto cap = pole.faces[3].corners;
    pole.faces[3].corners = {cap[0], cap[1], cap[3]};
    EditableFace triangle{7, {cap[1], cap[2], cap[3]}};
    for (std::size_t i = 0; i < triangle.corners.size(); ++i)
        triangle.corners[i].id = i + 101;
    pole.faces.push_back(std::move(triangle));
    REQUIRE(deriveMesh(pole).derived);
    const auto complex = analyzeBevelEdge(pole, {5, 6});
    REQUIRE_FALSE(complex.bevel);
    REQUIRE(complex.error.find("三价") != std::string::npos);
    REQUIRE_FALSE(bevelEdge(pole, {5, 6}, .1).mesh);
    auto nonplanar = createEditableCube();
    for (auto& vertex : nonplanar.vertices)
        if (vertex.id == 8)
            vertex.position.z += .1F;
    REQUIRE(deriveMesh(nonplanar).derived);
    const auto warped = analyzeBevelEdge(nonplanar, {5, 6});
    REQUIRE_FALSE(warped.bevel);
    REQUIRE(warped.error.find("共面") != std::string::npos);
    REQUIRE_FALSE(bevelEdge(nonplanar, {5, 6}, .1).mesh);
    auto nonmanifold = createEditableCube();
    auto duplicate = nonmanifold.faces.front();
    duplicate.id = 7;
    for (std::size_t i = 0; i < duplicate.corners.size(); ++i)
        duplicate.corners[i].id = i + 101;
    nonmanifold.faces.push_back(std::move(duplicate));
    REQUIRE_FALSE(validateEditableMesh(nonmanifold).isValid());
    REQUIRE_FALSE(analyzeBevelEdge(nonmanifold, {5, 6}).bevel);
    REQUIRE_FALSE(bevelEdge(nonmanifold, {5, 6}, .1).mesh);
    REQUIRE_FALSE(analyzeBevelEdge(createEditableCube(), {1, 7}).bevel);
}

TEST_CASE("Bevel rejects invalid excessive or unrepresentable widths without changing source",
          "[bevel-edge]") {
    const auto before = createEditableCube();
    for (const auto width :
         {0., -.1, 1., 1.1, std::nextafter(1., 0.), 1.0e-300,
          std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        const auto result = bevelEdge(before, {5, 6}, width);
        REQUIRE_FALSE(result.mesh);
        REQUIRE(result.bevelFace == 0);
        REQUIRE_FALSE(result.error.empty());
        REQUIRE(before == createEditableCube());
    }
    const auto narrow = bevelEdge(before, {5, 6}, .99);
    INFO(narrow.error);
    REQUIRE(narrow.mesh);
    REQUIRE(validateEditableMesh(*narrow.mesh).boundaryEdgeCount == 0);
    const auto first = bevelEdge(before, {5, 6}, .1);
    const auto second = bevelEdge(before, {5, 6}, .2);
    REQUIRE(first.mesh);
    REQUIRE(second.mesh);
    REQUIRE(first.bevelFace == second.bevelFace);
    REQUIRE(first.mesh->vertices.size() == second.mesh->vertices.size());
    REQUIRE(first.mesh->faces.size() == second.mesh->faces.size());
    REQUIRE(before == createEditableCube());
}

TEST_CASE("Bevel handles high stable IDs and refuses each exhausted identity space",
          "[bevel-edge]") {
    constexpr auto last = std::numeric_limits<std::uint64_t>::max();
    auto before = createEditableCube();
    before.vertices.push_back({last - 4, {7, 8, 9}});
    before.faces[1].id = last - 1;
    before.faces[1].corners[0].id = last - 12;
    const auto original = before;
    const auto result = bevelEdge(before, {5, 6}, .2);
    INFO(result.error);
    REQUIRE(result.mesh);
    REQUIRE(result.bevelFace == last);
    REQUIRE(*result.mesh->vertex(last - 4) == *before.vertex(last - 4));
    REQUIRE(faceById(*result.mesh, last - 1) == faceById(before, last - 1));
    REQUIRE(validateEditableMesh(*result.mesh).isValid());
    REQUIRE(deriveMesh(*result.mesh).derived);
    REQUIRE(before == original);
    for (int kind = 0; kind < 3; ++kind) {
        auto mesh = createEditableCube();
        if (kind == 0)
            mesh.vertices.push_back({last - 3, {9, 9, 9}});
        else if (kind == 1)
            mesh.faces[1].id = last;
        else
            mesh.faces[1].corners[0].id = last - 11;
        const auto snapshot = mesh;
        const auto rejected = bevelEdge(mesh, {5, 6}, .1);
        REQUIRE_FALSE(rejected.mesh);
        REQUIRE(rejected.bevelFace == 0);
        REQUIRE(rejected.error.find("ID") != std::string::npos);
        REQUIRE(mesh == snapshot);
    }
}
