/*
 * 模块名: TopologySelectionTests
 * 功能概述: 验证规则四边网格选择、边界与极点终止、连通隔离及稳定元素身份。
 * 对外接口: Catch2 [topology-selection] 纯 CPU 用例
 * 依赖关系: Core modeling、Catch2、GLM，不创建 Qt/GL。
 * 输入输出: 平面网格、四边闭环、非四边面和非法源到精确元素集合断言。
 * 异常与错误: 非法源或不存在的边不返回部分结果，所有选择保留源快照。
 * 维护说明: 预期集合按夹具拓扑独立列出，不用位置近似或算法输出构造预期。
 */
#include "core/modeling/MeshValidation.h"
#include "core/modeling/TopologySelection.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace mini3d::core::modeling;
namespace {
void appendFace(EditableMesh& mesh, const std::vector<VertexId>& vertices, CornerId& cornerId) {
    EditableFace face;
    face.id = mesh.faces.size() + 1;
    for (const auto vertex : vertices) {
        face.corners.push_back({++cornerId, vertex});
    }
    mesh.faces.push_back(std::move(face));
}

EditableMesh grid(int columns, int rows) {
    EditableMesh mesh;
    for (int y = 0; y <= rows; ++y) {
        for (int x = 0; x <= columns; ++x) {
            mesh.vertices.push_back({static_cast<VertexId>(mesh.vertices.size() + 1),
                                     {static_cast<float>(x), static_cast<float>(y), 0}});
        }
    }
    CornerId cornerId = 0;
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const auto first = static_cast<VertexId>(y * (columns + 1) + x + 1);
            const auto above = first + columns + 1;
            appendFace(mesh, {first, first + 1, above + 1, above}, cornerId);
        }
    }
    return mesh;
}

EditableMesh cylinder() {
    EditableMesh mesh;
    for (int layer = 0; layer < 3; ++layer) {
        for (const auto point :
             {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)}) {
            mesh.vertices.push_back({static_cast<VertexId>(mesh.vertices.size() + 1),
                                     {point.x, point.y, static_cast<float>(layer)}});
        }
    }
    CornerId cornerId = 0;
    for (VertexId layer = 0; layer < 2; ++layer) {
        for (VertexId side = 0; side < 4; ++side) {
            const auto first = layer * 4 + side + 1;
            const auto next = layer * 4 + (side + 1) % 4 + 1;
            appendFace(mesh, {first, next, next + 4, first + 4}, cornerId);
        }
    }
    return mesh;
}

void requireEdges(const EditableMesh& mesh, EdgeKey seed, EdgeSelectionKind kind,
                  const std::set<EdgeKey>& expected) {
    const auto result = selectEdgePath(mesh, seed, kind);
    INFO(result.error);
    REQUIRE(result.edges);
    REQUIRE(result.error.empty());
    REQUIRE(*result.edges == expected);
    REQUIRE(result.edges->contains(seed));
}
} // namespace

TEST_CASE("Quad grid loop follows vertex opposites and ring follows face opposites",
          "[topology-selection]") {
    auto mesh = grid(3, 3);
    // 不均匀几何变形不改变源边的拓扑后继。
    for (auto& vertex : mesh.vertices) {
        const auto position = vertex.position;
        vertex.position = {position.x + .3F * position.y * position.y, position.y,
                           .1F * position.x * position.y};
    }
    const auto before = mesh;
    REQUIRE(validateEditableMesh(mesh).isValid());
    requireEdges(mesh, {6, 7}, EdgeSelectionKind::Loop, {{5, 6}, {6, 7}, {7, 8}});
    requireEdges(mesh, {6, 7}, EdgeSelectionKind::Ring, {{2, 3}, {6, 7}, {10, 11}, {14, 15}});
    REQUIRE(mesh == before);
}

TEST_CASE("Boundary loop closes at ordinary corners while inward paths terminate",
          "[topology-selection]") {
    const auto mesh = grid(3, 3);
    const auto before = mesh;
    requireEdges(mesh, {1, 2}, EdgeSelectionKind::Loop,
                 {{1, 2},
                  {2, 3},
                  {3, 4},
                  {4, 8},
                  {8, 12},
                  {12, 16},
                  {15, 16},
                  {14, 15},
                  {13, 14},
                  {9, 13},
                  {5, 9},
                  {1, 5}});
    requireEdges(mesh, {1, 2}, EdgeSelectionKind::Ring, {{1, 2}, {5, 6}, {9, 10}, {13, 14}});
    requireEdges(mesh, {6, 10}, EdgeSelectionKind::Loop, {{2, 6}, {6, 10}, {10, 14}});
    REQUIRE(mesh == before);
}

TEST_CASE("Quad cylinder loop and ring close without duplicating or changing source edges",
          "[topology-selection]") {
    const auto mesh = cylinder();
    const auto before = mesh;
    REQUIRE(validateEditableMesh(mesh).isValid());
    requireEdges(mesh, {5, 6}, EdgeSelectionKind::Loop, {{5, 6}, {6, 7}, {7, 8}, {5, 8}});
    requireEdges(mesh, {1, 5}, EdgeSelectionKind::Ring, {{1, 5}, {2, 6}, {3, 7}, {4, 8}});
    requireEdges(mesh, {1, 5}, EdgeSelectionKind::Loop, {{1, 5}, {5, 9}});
    REQUIRE(mesh == before);
}

TEST_CASE("Loop stops at poles and ring stops at triangles or ngons", "[topology-selection]") {
    EditableMesh pole;
    pole.vertices.push_back({1, {0, 0, 0}});
    for (int side = 0; side < 10; ++side) {
        const double angle = side * 3.141592653589793 / 5;
        pole.vertices.push_back(
            {static_cast<VertexId>(side + 2),
             {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)), 0}});
    }
    CornerId cornerId = 0;
    for (VertexId side = 0; side < 5; ++side) {
        appendFace(pole, {1, side * 2 + 2, side * 2 + 3, (side + 1) * 2 % 10 + 2}, cornerId);
    }
    const auto poleBefore = pole;
    REQUIRE(validateEditableMesh(pole).isValid());
    requireEdges(pole, {1, 2}, EdgeSelectionKind::Loop, {{1, 2}});
    REQUIRE(pole == poleBefore);
    requireEdges(createEditableCube(), {1, 4}, EdgeSelectionKind::Loop, {{1, 4}});
    for (const bool triangle : {true, false}) {
        auto mesh = grid(3, 1);
        auto& corners = mesh.faces.back().corners;
        if (triangle) {
            corners.erase(corners.begin() + 1);
        } else {
            mesh.vertices.push_back({9, {3.5F, .5F, 0}});
            corners.insert(corners.begin() + 2, {13, 9});
        }
        const auto before = mesh;
        REQUIRE(validateEditableMesh(mesh).isValid());
        requireEdges(mesh, {1, 5}, EdgeSelectionKind::Ring, {{1, 5}, {2, 6}, {3, 7}});
        REQUIRE(mesh == before);
    }
}

TEST_CASE("Connected selection stays in its component with stable IDs and isolated vertices",
          "[topology-selection]") {
    auto mesh = grid(3, 3);
    for (auto vertex : createEditableCube().vertices) {
        vertex.id += 100;
        vertex.position += glm::vec3(10);
        mesh.vertices.push_back(vertex);
    }
    for (auto face : createEditableCube().faces) {
        face.id += 100;
        for (auto& corner : face.corners) {
            corner.id += 100;
            corner.vertex += 100;
        }
        mesh.faces.push_back(std::move(face));
    }
    mesh.vertices.push_back({999, {30, 30, 30}});
    constexpr VertexId offset = VertexId{1} << 40;
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
    const auto before = mesh;
    std::set<VertexId> gridVertices, cubeVertices;
    for (VertexId vertex = 1; vertex <= 16; ++vertex) {
        gridVertices.insert(offset + vertex);
    }
    for (VertexId vertex = 101; vertex <= 108; ++vertex) {
        cubeVertices.insert(offset + vertex);
    }
    REQUIRE(connectedVertices(mesh, offset + 6) == gridVertices);
    REQUIRE(connectedVertices(mesh, offset + 101) == cubeVertices);
    REQUIRE(connectedVertices(mesh, offset + 999) == std::set<VertexId>{offset + 999});
    REQUIRE(mesh == before);
    std::reverse(mesh.vertices.begin(), mesh.vertices.end());
    std::reverse(mesh.faces.begin(), mesh.faces.end());
    for (auto& face : mesh.faces) {
        std::rotate(face.corners.begin(), face.corners.begin() + 1, face.corners.end());
    }
    const auto reordered = mesh;
    requireEdges(mesh, {offset + 6, offset + 7}, EdgeSelectionKind::Loop,
                 {{offset + 5, offset + 6}, {offset + 6, offset + 7}, {offset + 7, offset + 8}});
    requireEdges(mesh, {offset + 6, offset + 7}, EdgeSelectionKind::Ring,
                 {{offset + 2, offset + 3},
                  {offset + 6, offset + 7},
                  {offset + 10, offset + 11},
                  {offset + 14, offset + 15}});
    REQUIRE(connectedVertices(mesh, offset + 6) == gridVertices);
    REQUIRE(mesh == reordered);
}

TEST_CASE("Invalid meshes missing seeds and triangulation diagonals return no partial selection",
          "[topology-selection]") {
    const auto mesh = grid(3, 3);
    for (const auto kind : {EdgeSelectionKind::Loop, EdgeSelectionKind::Ring}) {
        for (const auto seed : {EdgeKey(6, 11), EdgeKey(0, 6), EdgeKey(6, 999)}) {
            const auto result = selectEdgePath(mesh, seed, kind);
            REQUIRE_FALSE(result.edges);
            REQUIRE_FALSE(result.error.empty());
        }
    }
    REQUIRE(connectedVertices(mesh, 0).empty());
    REQUIRE(connectedVertices(mesh, 999).empty());
    auto invalid = mesh;
    invalid.vertices.push_back(invalid.vertices.front());
    const auto before = invalid;
    for (const auto kind : {EdgeSelectionKind::Loop, EdgeSelectionKind::Ring}) {
        const auto result = selectEdgePath(invalid, {6, 7}, kind);
        REQUIRE_FALSE(result.edges);
        REQUIRE_FALSE(result.error.empty());
    }
    REQUIRE(connectedVertices(invalid, 6).empty());
    REQUIRE(invalid == before);
}
