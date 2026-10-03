/*
 * 模块名: ExtrudeRegionTests
 * 功能概述: 验证连通面域挤出的拓扑、属性、方向、ID 和拒绝边界。
 * 对外接口: Catch2 [extrude-region]；依赖关系: 纯 Core，无 Qt/GL。
 * 输入输出: Cube / 网格面域与位移到不可变 before、合法顶盖/侧壁候选。
 * 异常与错误: 零值、无效侧壁、闭壳、非连通、边界分叉和 ID 溢出必须失败。
 * 维护说明: 不将局部拓扑校验当作全局自交验证。
 */
#include "core/modeling/ExtrudeRegion.h"
#include "core/modeling/MeshDerivation.h"
#include "core/modeling/MeshValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace mini3d::core::modeling;
namespace {
const EditableFace& face(const EditableMesh& mesh, FaceId id) {
    const auto found = std::find_if(mesh.faces.begin(), mesh.faces.end(), [id](const auto& item) {
        return item.id == id;
    });
    REQUIRE(found != mesh.faces.end());
    return *found;
}
EditableMesh grid(int count) {
    EditableMesh result;
    VertexId vertex = 0;
    CornerId corner = 0;
    for (int y = 0; y <= count; ++y) {
        for (int x = 0; x <= count; ++x)
            result.vertices.push_back({++vertex, {x, y, 0}});
    }
    for (int y = 0; y < count; ++y) {
        for (int x = 0; x < count; ++x) {
            const auto a = static_cast<VertexId>(y * (count + 1) + x + 1);
            const auto b = a + 1, d = a + count + 1, c = d + 1;
            EditableFace cell;
            cell.id = result.faces.size() + 1;
            for (const auto id : {a, b, c, d})
                cell.corners.push_back({++corner, id});
            result.faces.push_back(std::move(cell));
        }
    }
    return result;
}
} // namespace

TEST_CASE("Cube single face positive negative extrusion preserves caps and builds oriented sides",
          "[extrude-region]") {
    auto before = createEditableCube();
    before.faces[0].material = 19;
    const auto saved = before;
    const auto analysis = analyzeExtrudeRegion(before, {1});
    REQUIRE(analysis.region);
    REQUIRE(analysis.region->boundaryEdgeCount == 4);
    REQUIRE(analysis.region->normal == glm::dvec3(0, 0, 1));
    for (double offset : {0.25, -0.25}) {
        const auto result = extrudeRegion(before, {1}, {0, 0, offset});
        INFO(result.error);
        REQUIRE(result.mesh);
        REQUIRE(before == saved);
        REQUIRE(result.mesh->vertices.size() == 12);
        REQUIRE(result.mesh->faces.size() == 10);
        const auto validation = validateEditableMesh(*result.mesh);
        REQUIRE(validation.isValid());
        REQUIRE(validation.edgeCount == 20);
        REQUIRE(validation.boundaryEdgeCount == 0);
        const auto& cap = face(*result.mesh, 1);
        REQUIRE(cap.material == 19);
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& original = face(before, 1).corners[i];
            const auto& top = cap.corners[i];
            REQUIRE(top.id == original.id);
            REQUIRE(top.vertex > 8);
            REQUIRE(top.uv == original.uv);
            REQUIRE(top.color == original.color);
            REQUIRE(top.normal == original.normal);
            REQUIRE(result.mesh->vertex(top.vertex)->position ==
                    before.vertex(original.vertex)->position + glm::vec3(0, 0, offset));
        }
        for (FaceId id = 2; id <= 6; ++id)
            REQUIRE(face(*result.mesh, id) == face(before, id));
        for (const auto& side : result.mesh->faces) {
            if (side.id <= 6)
                continue;
            REQUIRE(side.material == 19);
            REQUIRE(side.corners.size() == 4);
            REQUIRE(side.corners[0].uv == glm::vec2(0, 0));
            REQUIRE(side.corners[2].uv == glm::vec2(1, std::abs(offset)));
            for (const auto& corner : side.corners) {
                REQUIRE(corner.id > 24);
                REQUIRE_FALSE(corner.normal);
                REQUIRE(corner.color == before.faces[0].corners[0].color);
            }
        }
        REQUIRE(deriveMesh(*result.mesh).derived);
    }
}

TEST_CASE("Noncoplanar connected region shares cap vertices and omits internal side walls",
          "[extrude-region]") {
    const auto before = createEditableCube();
    const auto analysis = analyzeExtrudeRegion(before, {1, 5});
    REQUIRE(analysis.region);
    REQUIRE(analysis.region->boundaryEdgeCount == 6);
    REQUIRE(glm::length(analysis.region->normal - glm::normalize(glm::dvec3(0, 1, 1))) < 1.0e-12);
    const auto first = extrudeRegion(before, {1, 5}, {0, 0.2, 0.2});
    const auto second = extrudeRegion(before, {1, 5}, {0, 0.4, 0.4});
    INFO(first.error);
    INFO(second.error);
    REQUIRE(first.mesh);
    REQUIRE(second.mesh);
    REQUIRE(first.mesh->vertices.size() == 14);
    REQUIRE(first.mesh->faces.size() == 12);
    REQUIRE(second.mesh->faces.size() == first.mesh->faces.size());
    REQUIRE(validateEditableMesh(*first.mesh).boundaryEdgeCount == 0);
    std::set<VertexId> shared;
    for (const auto& a : face(*first.mesh, 1).corners) {
        for (const auto& b : face(*first.mesh, 5).corners)
            if (a.vertex == b.vertex)
                shared.insert(a.vertex);
    }
    REQUIRE(shared.size() == 2);
    REQUIRE(extrudeRegion(before, {1, 5}, {0, 0.2, 0.2}).mesh == first.mesh);
}

TEST_CASE("Multi-face sheet removes only newly orphaned old interior points and has no bottom caps",
          "[extrude-region]") {
    auto before = grid(2);
    before.vertices.push_back({99, {7, 8, 9}});
    const auto result = extrudeRegion(before, {1, 2, 3, 4}, {0, 0, 0.5});
    INFO(result.error);
    REQUIRE(result.mesh);
    REQUIRE(result.mesh->faces.size() == 12);
    REQUIRE(result.mesh->vertices.size() == 18);
    REQUIRE_FALSE(result.mesh->vertex(5));
    REQUIRE(result.mesh->vertex(99));
    REQUIRE(result.mesh->vertex(99)->position == glm::vec3(7, 8, 9));
    const auto validation = validateEditableMesh(*result.mesh);
    REQUIRE(validation.isValid());
    REQUIRE(validation.boundaryEdgeCount == 8);
    for (auto id : {FaceId{1}, FaceId{2}, FaceId{3}, FaceId{4}}) {
        for (const auto& corner : face(*result.mesh, id).corners)
            REQUIRE(result.mesh->vertex(corner.vertex)->position.z == 0.5F);
    }
}

TEST_CASE("Region supports multiple boundary loops and deterministic normal fallback",
          "[extrude-region]") {
    const auto before = grid(3);
    const std::set<FaceId> ring{1, 2, 3, 4, 6, 7, 8, 9};
    const auto analysis = analyzeExtrudeRegion(before, ring);
    REQUIRE(analysis.region);
    REQUIRE(analysis.region->boundaryEdgeCount == 16);
    const auto result = extrudeRegion(before, ring, {0, 0, 0.5});
    INFO(result.error);
    REQUIRE(result.mesh);
    REQUIRE(result.mesh->faces.size() == 25);
    REQUIRE(face(*result.mesh, 5) == face(before, 5));
    REQUIRE(validateEditableMesh(*result.mesh).isValid());
    auto cube = createEditableCube();
    const auto sideBelt = analyzeExtrudeRegion(cube, {1, 2, 3, 4});
    REQUIRE(sideBelt.region);
    REQUIRE(sideBelt.region->usesFallbackNormal);
    REQUIRE(sideBelt.region->normal == glm::dvec3(0, 0, 1));
    std::reverse(cube.faces.begin(), cube.faces.end());
    REQUIRE(analyzeExtrudeRegion(cube, {1, 2, 3, 4}).region->normal == sideBelt.region->normal);
}

TEST_CASE("Invalid region or offset is rejected without modifying the original mesh",
          "[extrude-region]") {
    const auto before = createEditableCube();
    REQUIRE_FALSE(analyzeExtrudeRegion(before, {}).region);
    REQUIRE_FALSE(analyzeExtrudeRegion(before, {999}).region);
    REQUIRE_FALSE(analyzeExtrudeRegion(before, {1, 2}).region);
    REQUIRE_FALSE(analyzeExtrudeRegion(before, {1, 2, 3, 4, 5, 6}).region);
    REQUIRE_FALSE(analyzeExtrudeRegion(grid(3), {2, 3, 4, 6, 7, 8, 9}).region);
    REQUIRE_FALSE(extrudeRegion(before, {1}, {0, 0, 0}).mesh);
    REQUIRE_FALSE(extrudeRegion(before, {1}, {0.25, 0, 0}).mesh);
    REQUIRE_FALSE(
        extrudeRegion(before, {1}, {0, 0, std::numeric_limits<double>::quiet_NaN()}).mesh);
    REQUIRE_FALSE(extrudeRegion(before, {1}, {0, 0, std::numeric_limits<double>::max()}).mesh);
    REQUIRE_FALSE(extrudeRegion(before, {1}, {0, 0, 1.0e-300}).mesh);
    REQUIRE(before == createEditableCube());
    auto wrong = before;
    wrong.faces[1].corners[0].vertex = 999;
    REQUIRE_FALSE(analyzeExtrudeRegion(wrong, {1}).region);
}

TEST_CASE("Extrusion refuses exhausted 64 bit identity spaces instead of reusing IDs",
          "[extrude-region]") {
    for (int kind = 0; kind < 3; ++kind) {
        auto before = createEditableCube();
        const auto last = std::numeric_limits<std::uint64_t>::max();
        if (kind == 0)
            before.vertices.push_back({last, {9, 9, 9}});
        else if (kind == 1)
            before.faces[1].id = last;
        else
            before.faces[1].corners[0].id = last;
        const auto result = extrudeRegion(before, {1}, {0, 0, 0.5});
        REQUIRE_FALSE(result.mesh);
        REQUIRE(result.error.find("ID") != std::string::npos);
    }
}

TEST_CASE("Region sides inherit the adjacent face material and each boundary endpoint color",
          "[extrude-region]") {
    auto before = createEditableCube();
    for (auto index : {0, 4}) {
        auto& selected = before.faces[index];
        selected.material = static_cast<mini3d::core::AssetId>(19 + index);
        for (std::size_t i = 0; i < selected.corners.size(); ++i) {
            selected.corners[i].color = {0.1F * i, 0.1F * index, 0.75F};
            selected.corners[i].uv = {0.25F * i, 0.125F * index};
        }
    }
    const auto saved = before;
    const auto result = extrudeRegion(before, {1, 5}, {0, 0.2, 0.2});
    INFO(result.error);
    REQUIRE(result.mesh);
    REQUIRE(before == saved);
    for (const auto id : {FaceId{1}, FaceId{5}}) {
        const auto& original = face(before, id);
        const auto& cap = face(*result.mesh, id);
        REQUIRE(cap.material == original.material);
        for (std::size_t i = 0; i < cap.corners.size(); ++i) {
            REQUIRE(cap.corners[i].id == original.corners[i].id);
            REQUIRE(cap.corners[i].color == original.corners[i].color);
            REQUIRE(cap.corners[i].uv == original.corners[i].uv);
            REQUIRE(cap.corners[i].normal == original.corners[i].normal);
        }
    }
    for (const auto& side : result.mesh->faces) {
        if (side.id <= 6)
            continue;
        int matchedBoundary = 0;
        for (const auto id : {FaceId{1}, FaceId{5}}) {
            const auto& original = face(before, id);
            for (std::size_t i = 0; i < original.corners.size(); ++i) {
                const auto& from = original.corners[i];
                const auto& to = original.corners[(i + 1) % original.corners.size()];
                if (side.corners[0].vertex != from.vertex || side.corners[1].vertex != to.vertex)
                    continue;
                ++matchedBoundary;
                REQUIRE(side.material == original.material);
                REQUIRE(side.corners[0].color == from.color);
                REQUIRE(side.corners[1].color == to.color);
                REQUIRE(side.corners[2].color == to.color);
                REQUIRE(side.corners[3].color == from.color);
            }
        }
        REQUIRE(matchedBoundary == 1);
    }
}
