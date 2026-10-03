/*
 * 模块名: FillDeleteTests
 * 功能概述: 验证分域删除、边界补面、绕序和原子拒绝；对外接口: Catch2 [fill-delete]。
 * 依赖关系: Core、Catch2，无 Qt/GL；输入输出: 网格夹具到独立拓扑与属性断言。
 * 异常与错误: 非法候选不得改源；维护说明: 不用补面算法生成预期面环。
 */
#include "core/modeling/DeleteComponents.h"
#include "core/modeling/FillFace.h"
#include "core/modeling/MeshDerivation.h"
#include "core/modeling/MeshTopology.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>
#include <limits>

using namespace mini3d::core::modeling;
namespace {
EditableMesh tube(const std::vector<glm::vec3>& ring) {
    EditableMesh mesh;
    const auto count = static_cast<VertexId>(ring.size());
    for (VertexId i = 0; i < count; ++i)
        mesh.vertices.push_back({i + 1, ring[i]});
    for (VertexId i = 0; i < count; ++i)
        mesh.vertices.push_back({count + i + 1, ring[i] - glm::vec3(0, 0, 2)});
    CornerId corner = 0;
    for (VertexId i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        EditableFace face;
        face.id = i + 1;
        for (const auto id : {i + 1, count + i + 1, count + next + 1, next + 1})
            face.corners.push_back({++corner, id});
        mesh.faces.push_back(std::move(face));
    }
    REQUIRE(deriveMesh(mesh).derived);
    return mesh;
}
void requireRejected(const FillFaceResult& result) {
    REQUIRE_FALSE(result.mesh);
    REQUIRE(result.face == 0);
    REQUIRE_FALSE(result.error.empty());
}
} // namespace

TEST_CASE("Delete respects source domains and preserves remaining attributes", "[fill-delete]") {
    const auto before = createEditableCube();
    SECTION("Face keeps the hole boundary") {
        const auto result = deleteFaces(before, {1});
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->vertices == before.vertices);
        REQUIRE(result.mesh->faces.size() == 5);
        for (std::size_t i = 0; i < 5; ++i)
            REQUIRE(result.mesh->faces[i] == before.faces[i + 1]);
        REQUIRE(validateEditableMesh(*result.mesh).boundaryEdgeCount == 4);
    }
    SECTION("Edge removes both incident faces without dissolving") {
        const auto result = deleteEdges(before, {{5, 6}});
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->faces.size() == 4);
        REQUIRE(result.mesh->vertices.size() == 8);
        for (const auto& face : result.mesh->faces) {
            REQUIRE(face.id != 1);
            REQUIRE(face.id != 6);
        }
    }
    SECTION("Vertex removes its incident faces and orphan vertex") {
        const auto result = deleteVertices(before, {5});
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->faces.size() == 3);
        REQUIRE(result.mesh->vertices.size() == 7);
        REQUIRE_FALSE(result.mesh->vertex(5));
    }
    SECTION("Deleting everything leaves valid empty editable geometry") {
        const auto result = deleteFaces(before, {1, 2, 3, 4, 5, 6});
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->faces.empty());
        REQUIRE(result.mesh->vertices.empty());
        REQUIRE(deriveMesh(*result.mesh).derived->mesh.indices.empty());
    }
    SECTION("Unknown IDs and render diagonals are not silently ignored") {
        for (const auto result : {deleteVertices(before, {1, 999}), deleteEdges(before, {{5, 7}}),
                                  deleteFaces(before, {1, 999}), deleteVertices(before, {}),
                                  deleteEdges(before, {}), deleteFaces(before, {})}) {
            REQUIRE_FALSE(result.mesh);
            REQUIRE_FALSE(result.error.empty());
        }
    }
    REQUIRE(before == createEditableCube());
}

TEST_CASE("Delete cleans all orphan references and atomically rejects disconnected vertex fans",
          "[fill-delete]") {
    auto before = createEditableCube();
    before.vertices.push_back({99, {9, 9, 9}});
    const auto clean = deleteFaces(before, {1});
    REQUIRE(clean.mesh);
    REQUIRE_FALSE(clean.mesh->vertex(99));
    REQUIRE(deleteVertices(before, {99}).mesh->vertices.size() == 8);
    EditableMesh grid;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            grid.vertices.push_back({static_cast<VertexId>(grid.vertices.size() + 1),
                                     {static_cast<float>(x), static_cast<float>(y), 0}});
    CornerId corner = 0;
    for (VertexId start : {1, 2, 4, 5}) {
        EditableFace face;
        face.id = grid.faces.size() + 1;
        for (const auto id : {start, start + 1, start + 4, start + 3})
            face.corners.push_back({++corner, id});
        grid.faces.push_back(std::move(face));
    }
    REQUIRE(deriveMesh(grid).derived);
    const auto frozen = grid;
    const auto rejected = deleteFaces(grid, {1, 4});
    REQUIRE_FALSE(rejected.mesh);
    REQUIRE_FALSE(rejected.error.empty());
    REQUIRE(grid == frozen);
}

TEST_CASE("Fill closes a cube hole with opposite neighbor winding and stable old data",
          "[fill-delete]") {
    for (FaceId deleted = 1; deleted <= 6; ++deleted) {
        const auto cube = createEditableCube();
        const auto open = *deleteFaces(cube, {deleted}).mesh;
        std::set<VertexId> vertices;
        std::set<EdgeKey> edges;
        const auto& face = cube.faces[deleted - 1];
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            vertices.insert(face.corners[i].vertex);
            edges.emplace(face.corners[i].vertex,
                          face.corners[(i + 1) % face.corners.size()].vertex);
        }
        const auto result = fillFaceFromVertices(open, vertices);
        INFO(result.error);
        REQUIRE(result.mesh);
        REQUIRE(fillFaceFromEdges(open, edges).mesh == result.mesh);
        REQUIRE(result.mesh->vertices == open.vertices);
        REQUIRE(result.mesh->faces.size() == 6);
        REQUIRE(result.mesh->faces.back().id == result.face);
        for (std::size_t i = 0; i < open.faces.size(); ++i)
            REQUIRE(result.mesh->faces[i] == open.faces[i]);
        const auto topology = buildMeshTopology(*result.mesh);
        REQUIRE(topology.topology);
        for (const auto& edge : topology.topology->halfEdges)
            REQUIRE(edge.twin);
        const auto& filled = result.mesh->faces.back();
        REQUIRE(filled.material == 0);
        for (const auto& corner : filled.corners) {
            REQUIRE(corner.normal);
            REQUIRE(glm::dot(*corner.normal, *face.corners.front().normal) > .999F);
            REQUIRE(corner.color == glm::vec3(1));
        }
        REQUIRE(deriveMesh(*result.mesh).derived->mesh.indices.size() == 36);
    }
}

TEST_CASE("Fill accepts concave planar rings at different scales and input ordering",
          "[fill-delete]") {
    for (const float scale : {.001F, 1.F, 1000.F}) {
        auto before = tube({{0, 0, 0}, {3, 0, 0}, {3, 3, 0}, {1, 1, 0}, {0, 3, 0}});
        for (auto& vertex : before.vertices)
            vertex.position *= scale;
        const auto result = fillFaceFromVertices(before, {1, 2, 3, 4, 5});
        INFO(result.error);
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->faces.back().corners.size() == 5);
        REQUIRE(validateEditableMesh(*result.mesh).boundaryEdgeCount == 5);
        std::reverse(before.vertices.begin(), before.vertices.end());
        std::reverse(before.faces.begin(), before.faces.end());
        const auto reordered = fillFaceFromVertices(before, {1, 2, 3, 4, 5});
        REQUIRE(reordered.mesh);
        REQUIRE(reordered.mesh->faces.back() == result.mesh->faces.back());
    }
}

TEST_CASE("Fill rejects unsupported rings without partial output", "[fill-delete]") {
    auto before = tube({{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}});
    SECTION("Open chain, multiple rings, internal edges and unknown IDs") {
        requireRejected(fillFaceFromEdges(before, {{1, 2}, {2, 3}, {3, 4}}));
        requireRejected(fillFaceFromVertices(before, {1, 2, 3, 4, 5, 6, 7, 8}));
        requireRejected(fillFaceFromEdges(before, {{1, 2}, {2, 6}, {5, 6}, {1, 5}}));
        requireRejected(fillFaceFromVertices(before, {1, 2, 999}));
        requireRejected(fillFaceFromEdges(before, {{1, 2}, {2, 3}, {1, 999}}));
        requireRejected(fillFaceFromVertices(before, {1, 2}));
        before.vertices.push_back({99, {9, 9, 9}});
        requireRejected(fillFaceFromVertices(before, {1, 2, 3, 4, 99}));
    }
    SECTION("Non planar") {
        before.vertices[2].position.z = .1F;
        requireRejected(fillFaceFromVertices(before, {1, 2, 3, 4}));
    }
    SECTION("Crossed boundary") {
        before = tube({{0, 0, 0}, {3, 2, 0}, {0, 3, 0}, {2, 0, 0}});
        requireRejected(fillFaceFromVertices(before, {1, 2, 3, 4}));
    }
    SECTION("Face or corner ID space exhausted") {
        before.faces.back().id = std::numeric_limits<FaceId>::max();
        requireRejected(fillFaceFromVertices(before, {1, 2, 3, 4}));
        before.faces.back().id = 4;
        before.faces.back().corners.back().id = std::numeric_limits<CornerId>::max() - 2;
        requireRejected(fillFaceFromVertices(before, {1, 2, 3, 4}));
    }
    SECTION("Already filled boundary cannot be duplicated") {
        auto plane = createEditableCube();
        plane.faces.resize(1);
        requireRejected(fillFaceFromVertices(plane, {5, 6, 7, 8}));
    }
    const auto frozen = before;
    requireRejected(fillFaceFromVertices(before, {}));
    REQUIRE(before == frozen);
}
