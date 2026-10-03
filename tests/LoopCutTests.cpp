/*
 * 模块名: LoopCutTests
 * 功能概述: 验证闭合/开放四边形带单切、方向一致滑移、属性接缝及原子拒绝。
 * 对外接口: Catch2 [loop-cut]；依赖关系: Core、Catch2，无Qt/GL。
 * 输入输出: Cube/面带/极点夹具到拓扑、几何与身份断言。
 * 异常与错误: 非四边面、分叉、退化和ID耗尽必须拒绝且不改源。
 * 维护说明: 用独立坐标、流形和共享分点检查，不只比较算法自身输出。
 */
#include "core/modeling/ExtrudeRegion.h"
#include "core/modeling/InsetFace.h"
#include "core/modeling/LoopCut.h"
#include "core/modeling/MeshDerivation.h"
#include "core/modeling/MeshTopology.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace mini3d::core::modeling;
namespace {
EditableMesh strip() {
    EditableMesh mesh;
    for (int x = 0; x <= 3; ++x) {
        for (int y = 0; y <= 1; ++y) {
            mesh.vertices.push_back({static_cast<VertexId>(mesh.vertices.size() + 1),
                                     {static_cast<float>(x), static_cast<float>(y), 0}});
        }
    }
    CornerId cornerId = 0;
    for (VertexId x = 0; x < 3; ++x) {
        EditableFace face;
        face.id = x + 1;
        face.material = 8;
        for (auto id : {x * 2 + 1, x * 2 + 3, x * 2 + 4, x * 2 + 2}) {
            const auto p = mesh.vertex(id)->position;
            face.corners.push_back({++cornerId, id, {p.x, p.y}, glm::vec3(0, 0, 1), {p.x, p.y, 1}});
        }
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}
void requireOldVertices(const EditableMesh& before, const EditableMesh& after) {
    for (const auto& vertex : before.vertices)
        REQUIRE(*after.vertex(vertex.id) == vertex);
}
} // namespace

TEST_CASE("Cube loop cut is closed and every new edge has two incident quads", "[loop-cut]") {
    const auto before = createEditableCube();
    const auto analysis = analyzeLoopCut(before, {1, 4});
    REQUIRE(analysis.band);
    REQUIRE(analysis.band->closed);
    REQUIRE(analysis.band->faces.size() == 4);
    REQUIRE(analysis.band->edgeStarts.size() == 4);
    for (double slide : {0., -.5, .5}) {
        const auto result = loopCut(before, {1, 4}, slide);
        INFO(result.error);
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->vertices.size() == 12);
        REQUIRE(result.mesh->faces.size() == 10);
        REQUIRE(result.cutEdges.size() == 4);
        requireOldVertices(before, *result.mesh);
        const auto topology = buildMeshTopology(*result.mesh);
        REQUIRE(topology.topology);
        REQUIRE(topology.topology->edgeHalfEdges.size() == 20);
        for (const auto& vertex : result.mesh->vertices) {
            if (vertex.id > 8)
                REQUIRE(std::abs(vertex.position.y - slide * .5) < 1.0e-7);
        }
        for (const auto edge : result.cutEdges) {
            const auto index = topology.topology->edgeHalfEdges.at(edge);
            REQUIRE(topology.topology->halfEdges[index].twin);
        }
        for (const auto& face : result.mesh->faces)
            REQUIRE(face.corners.size() == 4);
        REQUIRE(result.mesh->faces[4] == before.faces[4]);
        REQUIRE(result.mesh->faces[5] == before.faces[5]);
        REQUIRE(validateEditableMesh(*result.mesh).boundaryEdgeCount == 0);
    }
    REQUIRE(before == createEditableCube());
}

TEST_CASE("Open band stops only at real boundaries with shared split vertices and interpolated "
          "attributes",
          "[loop-cut]") {
    const auto before = strip();
    const auto analysis = analyzeLoopCut(before, {3, 4});
    REQUIRE(analysis.band);
    REQUIRE_FALSE(analysis.band->closed);
    REQUIRE(analysis.band->faces.size() == 3);
    const auto result = loopCut(before, {3, 4}, -.4);
    INFO(result.error);
    REQUIRE(result.mesh);
    REQUIRE(result.mesh->vertices.size() == 12);
    REQUIRE(result.mesh->faces.size() == 6);
    REQUIRE(result.cutEdges.size() == 3);
    requireOldVertices(before, *result.mesh);
    const auto validation = validateEditableMesh(*result.mesh);
    REQUIRE(validation.isValid());
    REQUIRE(validation.edgeCount == 17);
    REQUIRE(validation.boundaryEdgeCount == 10);
    std::set<CornerId> oldIds;
    for (const auto& face : before.faces)
        for (const auto& corner : face.corners)
            oldIds.insert(corner.id);
    for (const auto& face : result.mesh->faces) {
        REQUIRE(face.material == 8);
        for (const auto& corner : face.corners) {
            oldIds.erase(corner.id);
            const auto& p = result.mesh->vertex(corner.vertex)->position;
            REQUIRE(glm::length(corner.uv - glm::vec2(p.x, p.y)) < 1.0e-6F);
            REQUIRE(glm::length(corner.color - glm::vec3(p.x, p.y, 1)) < 1.0e-6F);
            REQUIRE(corner.normal == glm::vec3(0, 0, 1));
            if (corner.vertex > 8)
                REQUIRE(std::abs(p.y - .3F) < 1.0e-6F);
        }
    }
    REQUIRE(oldIds.empty());
}

TEST_CASE(
    "Loop cut preserves per-face UV seams and hard normals after nonuniform geometric scaling",
    "[loop-cut]") {
    auto before = createEditableCube();
    for (auto& vertex : before.vertices)
        vertex.position = vertex.position * glm::vec3(3, 2, 4) + glm::vec3(10, -8, 2);
    const auto result = loopCut(before, {1, 4}, .2);
    REQUIRE(result.mesh);
    requireOldVertices(before, *result.mesh);
    std::map<VertexId, std::set<FaceId>> shared;
    for (const auto& face : result.mesh->faces) {
        for (const auto& corner : face.corners) {
            if (corner.vertex <= 8)
                continue;
            REQUIRE(std::abs(result.mesh->vertex(corner.vertex)->position.y + 7.8F) < 1.0e-6F);
            REQUIRE(corner.normal);
            REQUIRE(std::abs(glm::length(*corner.normal) - 1) < 1.0e-6F);
            shared[corner.vertex].insert(face.id);
        }
    }
    REQUIRE(shared.size() == 4);
    for (const auto& [vertex, faces] : shared)
        REQUIRE(faces.size() == 4);
    // 前/右面在共同新点保留不同的法线及UV，不按共享VertexId焊属性。
    const auto& front = result.mesh->faces[0];
    const auto& right = result.mesh->faces[2];
    bool seam = false;
    for (const auto& a : front.corners)
        for (const auto& b : right.corners)
            if (a.vertex > 8 && a.vertex == b.vertex) {
                REQUIRE(a.normal != b.normal);
                REQUIRE(a.uv != b.uv);
                seam = true;
            }
    REQUIRE(seam);
}

TEST_CASE(
    "Loop cut rejects triangles high-valence poles invalid factors and exhausted IDs atomically",
    "[loop-cut]") {
    auto before = strip();
    // 保留与第二个四边面的共享边(5,6)，仅将末端面改为合法三角面。
    before.faces.back().corners.erase(before.faces.back().corners.begin() + 1);
    REQUIRE(deriveMesh(before).derived);
    REQUIRE_FALSE(analyzeLoopCut(before, {1, 2}).band);
    REQUIRE_FALSE(loopCut(before, {1, 2}, 0).mesh);
    const auto cube = createEditableCube();
    REQUIRE_FALSE(analyzeLoopCut(cube, {1, 7}).band);
    for (const auto value : {-1., 1., 2., std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()})
        REQUIRE_FALSE(loopCut(cube, {1, 4}, value).mesh);
    for (int kind = 0; kind < 3; ++kind) {
        auto mesh = cube;
        const auto last = std::numeric_limits<std::uint64_t>::max();
        if (kind == 0)
            mesh.vertices.push_back({last, {9, 9, 9}});
        else if (kind == 1)
            mesh.faces[0].id = last;
        else
            mesh.faces[0].corners[0].id = last;
        REQUIRE_FALSE(loopCut(mesh, {1, 4}, 0).mesh);
    }
    // 五块四边面共用一个中心点：合法单扇，但中心为五价极点。
    EditableMesh pole;
    pole.vertices.push_back({1, {0, 0, 0}});
    for (int i = 0; i < 10; ++i) {
        const double angle = i * 3.141592653589793 / 5;
        pole.vertices.push_back(
            {static_cast<VertexId>(i + 2),
             {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)), 0}});
    }
    CornerId id = 0;
    for (VertexId i = 0; i < 5; ++i) {
        EditableFace face;
        face.id = i + 1;
        for (auto vertex : {VertexId(1), i * 2 + 2, i * 2 + 3, ((i + 1) * 2) % 10 + 2})
            face.corners.push_back({++id, vertex});
        pole.faces.push_back(std::move(face));
    }
    REQUIRE(deriveMesh(pole).derived);
    const auto rejected = analyzeLoopCut(pole, {1, 2});
    REQUIRE_FALSE(rejected.band);
    REQUIRE(rejected.error.find("极点") != std::string::npos);
    REQUIRE(cube == createEditableCube());
}

TEST_CASE("Loop cut works after inset extrusion and is stable across source ordering",
          "[loop-cut]") {
    const auto inset = insetFace(createEditableCube(), 1, .2);
    REQUIRE(inset.mesh);
    const auto extruded = extrudeRegion(*inset.mesh, {1}, {0, 0, -.2});
    REQUIRE(extruded.mesh);
    const auto result = loopCut(*extruded.mesh, {1, 4}, .3);
    INFO(result.error);
    REQUIRE(result.mesh);
    REQUIRE(validateEditableMesh(*result.mesh).isValid());
    REQUIRE(validateEditableMesh(*result.mesh).boundaryEdgeCount == 0);
    auto shuffled = *extruded.mesh;
    std::reverse(shuffled.vertices.begin(), shuffled.vertices.end());
    std::reverse(shuffled.faces.begin(), shuffled.faces.end());
    const auto other = loopCut(shuffled, {1, 4}, .3);
    REQUIRE(other.mesh);
    REQUIRE(other.cutEdges == result.cutEdges);
    for (const auto& vertex : result.mesh->vertices)
        REQUIRE(*other.mesh->vertex(vertex.id) == vertex);
    for (const auto& face : result.mesh->faces) {
        const auto found = std::find_if(other.mesh->faces.begin(), other.mesh->faces.end(),
                                        [&face](const auto& item) {
                                            return item.id == face.id;
                                        });
        REQUIRE(found != other.mesh->faces.end());
        REQUIRE(*found == face);
    }
}

TEST_CASE("Loop cut rejects a cut crossing a concave boundary while allowing the valid side",
          "[loop-cut]") {
    auto mesh = strip();
    mesh.faces.resize(1);
    for (auto& vertex : mesh.vertices)
        if (vertex.id == 4)
            vertex.position = {.2F, .2F, 0};
    REQUIRE(deriveMesh(mesh).derived);
    const auto valid = loopCut(mesh, {1, 3}, -.6);
    INFO(valid.error);
    REQUIRE(valid.mesh);
    REQUIRE_FALSE(loopCut(mesh, {1, 3}, .6).mesh);
    REQUIRE_FALSE(loopCut(createEditableCube(), {1, 4}, std::nextafter(1., 0.)).mesh);
    const auto topology = buildMeshTopology(createEditableCube());
    REQUIRE(topology.topology);
    for (const auto& [edge, halfEdge] : topology.topology->edgeHalfEdges) {
        REQUIRE(loopCut(createEditableCube(), edge, .25).mesh);
    }
}
