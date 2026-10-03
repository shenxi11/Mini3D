/*
 * 模块名: ProportionalTransformTests
 * 功能概述: 验证比例编辑的 Smooth 数值、世界半径、连通片隔离和加权候选原子性。
 * 对外接口: Catch2 [proportional-transform] 纯 CPU 用例
 * 依赖关系: Core modeling、Catch2、GLM，无 Qt/GL。
 * 输入输出: 固定源网格、驱动点、半径和 G/R/S 增量到权重/几何/属性断言。
 * 异常与错误: 非法输入、溢出或退化不得发布候选，也不得修改 before。
 * 维护说明: 预览每帧重算原 before；取消与历史事务由上层验证。
 */
#include "core/modeling/ProportionalTransform.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <limits>

using namespace mini3d::core::modeling;

namespace {
EditableMesh adjacentParts() {
    EditableMesh mesh;
    mesh.vertices = {{1, {0, 0, 0}},    {2, {3, 0, 0}},    {3, {3, 1, 0}},    {4, {0, 1, 0}},
                     {5, {0, 0, 0.1F}}, {6, {3, 0, 0.1F}}, {7, {3, 1, 0.1F}}, {8, {0, 1, 0.1F}}};
    for (const VertexId start : {1, 5}) {
        EditableFace face;
        face.id = start;
        for (VertexId id = start; id < start + 4; ++id) {
            face.corners.push_back({id, id});
        }
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}

glm::dmat4 nonuniformWorld() {
    return glm::translate(glm::dmat4(1), glm::dvec3(2, -3, 4)) *
           glm::rotate(glm::dmat4(1), 0.6, glm::dvec3(0, 1, 0)) *
           glm::scale(glm::dmat4(1), glm::dvec3(2, 0.5, -3));
}
} // namespace

TEST_CASE("Proportional Smooth weights use the nearest driver and exclude the radius boundary",
          "[proportional-transform]") {
    const EditableMesh before{{{10, {0, 0, 0}},
                               {20, {0.25F, 0, 0}},
                               {30, {0.5F, 0, 0}},
                               {40, {0.75F, 0, 0}},
                               {50, {1, 0, 0}},
                               {60, {3, 0, 0}},
                               {70, {2, 0, 0}},
                               {80, {1.75F, 0, 0}}},
                              {}};
    const auto saved = before;
    const auto result = proportionalWeights(before, {10, 70}, glm::dmat4(1), 1, false);
    INFO(result.error);
    REQUIRE(result.weights);
    REQUIRE(result.weights->size() == 6);
    REQUIRE(result.weights->at(10) == 1.0);
    REQUIRE(result.weights->at(20) == 0.84375);
    REQUIRE(result.weights->at(30) == 0.5);
    REQUIRE(result.weights->at(40) == 0.15625);
    REQUIRE(result.weights->at(70) == 1.0);
    REQUIRE(result.weights->at(80) == 0.84375);
    REQUIRE_FALSE(result.weights->contains(50));
    REQUIRE_FALSE(result.weights->contains(60));
    REQUIRE(before == saved);
}

TEST_CASE("Proportional radius measures world distance under nonuniform rotated object TRS",
          "[proportional-transform]") {
    const EditableMesh before{{{1, {0, 0, 0}},
                               {2, {0.25F, 0, 0}},
                               {3, {0, 0.5F, 0}},
                               {4, {0, 0, 0.25F}},
                               {5, {0.75F, 0, 0}}},
                              {}};
    const auto result = proportionalWeights(before, {1}, nonuniformWorld(), 1, false);
    INFO(result.error);
    REQUIRE(result.weights);
    REQUIRE(result.weights->at(1) == 1.0);
    REQUIRE(result.weights->at(2) == Catch::Approx(0.5).epsilon(1.0e-12));
    REQUIRE(result.weights->at(3) == Catch::Approx(0.84375).epsilon(1.0e-12));
    REQUIRE(result.weights->at(4) == Catch::Approx(0.15625).epsilon(1.0e-12));
    REQUIRE_FALSE(result.weights->contains(5));
}

TEST_CASE("Connected proportional editing isolates adjacent parts but retains Euclidean falloff",
          "[proportional-transform]") {
    const auto before = adjacentParts();
    const auto saved = before;
    const auto free = proportionalWeights(before, {1}, glm::dmat4(1), 3.5, false);
    const auto connected = proportionalWeights(before, {1}, glm::dmat4(1), 3.5, true);
    INFO(connected.error);
    REQUIRE(free.weights);
    REQUIRE(connected.weights);
    REQUIRE(free.weights->size() == 8);
    REQUIRE(connected.weights->size() == 4);
    REQUIRE(free.weights->at(5) > 0.99);
    for (const auto id : {5, 6, 7, 8}) {
        REQUIRE_FALSE(connected.weights->contains(id));
    }
    // 到 3 的源边最短路长 4，世界直线距离 sqrt(10) 仍落在半径内。
    const auto t = std::sqrt(10.0) / 3.5;
    REQUIRE(connected.weights->at(3) == Catch::Approx(1.0 - 3.0 * t * t + 2.0 * t * t * t));
    REQUIRE(proportionalWeights(before, {1, 5}, glm::dmat4(1), 3.5, true).weights->size() == 8);
    REQUIRE(before == saved);
}

TEST_CASE("Weighted move rotate and scale interpolate world displacements and preserve attributes",
          "[proportional-transform]") {
    const auto before = createEditableCube();
    const auto saved = before;
    const auto world = nonuniformWorld();
    const auto pivot = glm::dvec3(world * glm::dvec4(before.vertex(5)->position, 1));
    const std::map<VertexId, double> weights{{5, 1}, {6, 0.5}, {7, 0.25}, {8, 0}};
    const auto aroundPivot = [&pivot](const glm::dmat4& delta) {
        return glm::translate(glm::dmat4(1), pivot) * delta * glm::translate(glm::dmat4(1), -pivot);
    };
    for (const auto& delta : {glm::translate(glm::dmat4(1), glm::dvec3(0.15, 0.1, 0.1)),
                              aroundPivot(glm::rotate(glm::dmat4(1), 0.2, glm::dvec3(0, 0, 1))),
                              aroundPivot(glm::scale(glm::dmat4(1), glm::dvec3(1.1, 0.9, 1.05)))}) {
        const auto result = transformWeightedVertices(before, weights, world, delta);
        INFO(result.error);
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->vertices.size() == before.vertices.size());
        REQUIRE(result.mesh->faces.size() == before.faces.size());
        for (const auto& vertex : before.vertices) {
            const auto position = world * glm::dvec4(vertex.position, 1);
            const auto weight = weights.contains(vertex.id) ? weights.at(vertex.id) : 0.0;
            const auto expected = position + weight * (delta * position - position);
            const auto actual = world * glm::dvec4(result.mesh->vertex(vertex.id)->position, 1);
            REQUIRE(glm::length(actual - expected) < 1.0e-6);
            if (weight == 0) {
                REQUIRE(result.mesh->vertex(vertex.id)->position == vertex.position);
            }
        }
        for (std::size_t i = 0; i < before.faces.size(); ++i) {
            const auto& original = before.faces[i];
            const auto& changed = result.mesh->faces[i];
            REQUIRE(changed.id == original.id);
            REQUIRE(changed.material == original.material);
            REQUIRE(changed.corners.size() == original.corners.size());
            const bool affected = std::any_of(
                original.corners.begin(), original.corners.end(), [&](const auto& corner) {
                    return result.mesh->vertex(corner.vertex)->position !=
                           before.vertex(corner.vertex)->position;
                });
            for (std::size_t j = 0; j < original.corners.size(); ++j) {
                REQUIRE(changed.corners[j].id == original.corners[j].id);
                REQUIRE(changed.corners[j].vertex == original.corners[j].vertex);
                REQUIRE(changed.corners[j].uv == original.corners[j].uv);
                REQUIRE(changed.corners[j].color == original.corners[j].color);
                if (affected) {
                    REQUIRE_FALSE(changed.corners[j].normal);
                } else {
                    REQUIRE(changed.corners[j].normal == original.corners[j].normal);
                }
            }
        }
        REQUIRE(before == saved);
    }
}

TEST_CASE("Proportional previews recalculate from before and identity preserves the exact snapshot",
          "[proportional-transform]") {
    const EditableMesh before{{{1, {0, 0, 0}}, {2, {0.5F, 0, 0}}, {3, {1, 0, 0}}}, {}};
    const auto saved = before;
    const auto small = proportionalWeights(before, {1}, glm::dmat4(1), 1, false);
    const auto large = proportionalWeights(before, {1}, glm::dmat4(1), 2, false);
    REQUIRE(small.weights);
    REQUIRE(large.weights);
    const auto delta = glm::translate(glm::dmat4(1), glm::dvec3(1, 0, 0));
    const auto first = transformWeightedVertices(before, *small.weights, glm::dmat4(1), delta);
    const auto second = transformWeightedVertices(before, *large.weights, glm::dmat4(1), delta);
    REQUIRE(first.mesh);
    REQUIRE(second.mesh);
    REQUIRE(first.mesh->vertex(2)->position.x == 1.0F);
    REQUIRE(second.mesh->vertex(2)->position.x == 1.34375F);
    REQUIRE(first.mesh->vertex(3)->position == before.vertex(3)->position);
    REQUIRE(second.mesh->vertex(3)->position.x == 1.5F);
    REQUIRE(transformWeightedVertices(before, *large.weights, glm::dmat4(1), delta).mesh ==
            second.mesh);
    REQUIRE(before == saved);
    const auto cube = createEditableCube();
    REQUIRE(transformWeightedVertices(cube, {{1, 1}, {2, 0.5}}, nonuniformWorld(), glm::dmat4(1))
                .mesh == cube);
    REQUIRE(transformWeightedVertices(cube, {{1, 0}}, nonuniformWorld(), delta).mesh == cube);
}

TEST_CASE("Invalid proportional inputs overflow and collapsed faces fail without partial results",
          "[proportional-transform]") {
    const auto before = createEditableCube();
    const auto saved = before;
    const auto identity = glm::dmat4(1);
    const auto rejectWeights = [&](const ProportionalWeightsResult& result) {
        REQUIRE_FALSE(result.weights);
        REQUIRE_FALSE(result.error.empty());
        REQUIRE(before == saved);
    };
    const auto rejectTransform = [&](const VertexTransformResult& result) {
        REQUIRE_FALSE(result.mesh);
        REQUIRE_FALSE(result.error.empty());
        REQUIRE(before == saved);
    };
    for (const auto radius : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        rejectWeights(proportionalWeights(before, {1}, identity, radius, false));
    }
    rejectWeights(proportionalWeights(before, {}, identity, 1, false));
    rejectWeights(proportionalWeights(before, {999}, identity, 1, true));
    auto projective = identity;
    projective[0][3] = 0.1;
    auto nonfinite = identity;
    nonfinite[3][0] = std::numeric_limits<double>::quiet_NaN();
    for (const auto& matrix : {glm::dmat4(0), projective, nonfinite}) {
        rejectWeights(proportionalWeights(before, {1}, matrix, 1, false));
        rejectTransform(transformWeightedVertices(before, {{1, 1}}, matrix, identity));
        rejectTransform(transformWeightedVertices(before, {{1, 1}}, identity, matrix));
    }
    rejectTransform(transformWeightedVertices(before, {}, identity, identity));
    rejectTransform(transformWeightedVertices(before, {{999, 1}}, identity, identity));
    for (const auto weight : {-0.1, 1.1, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        rejectTransform(transformWeightedVertices(before, {{1, weight}}, identity, identity));
    }
    const auto move = glm::translate(identity, glm::dvec3(0.1, 0, 0));
    rejectTransform(transformWeightedVertices(
        before, {{1, 1}, {2, std::numeric_limits<double>::quiet_NaN()}}, identity, move));
    rejectTransform(transformWeightedVertices(before, {{1, 1}, {999, 1}}, identity, move));
    rejectTransform(transformWeightedVertices(before, {{1, 1}}, identity,
                                              glm::scale(identity, glm::dvec3(0, 1, 1))));
    rejectTransform(transformWeightedVertices(
        before, {{1, 1}}, identity,
        glm::translate(identity, glm::dvec3(std::numeric_limits<double>::max(), 0, 0))));
    rejectTransform(transformWeightedVertices(
        before, {{1, 1}},
        glm::scale(identity, glm::dvec3(std::numeric_limits<double>::denorm_min(), 1, 1)),
        identity));
    const EditableMesh overflow{{{1, {0, 0, 0}}, {2, {2, 0, 0}}}, {}};
    const auto overflowSaved = overflow;
    rejectWeights(proportionalWeights(
        overflow, {1}, glm::scale(identity, glm::dvec3(std::numeric_limits<double>::max(), 1, 1)),
        1, false));
    REQUIRE(overflow == overflowSaved);
    const auto collapse = glm::translate(
        identity, glm::dvec3(before.vertex(2)->position - before.vertex(1)->position));
    rejectTransform(transformWeightedVertices(before, {{1, 1}}, identity, collapse));
    auto invalidSource = before;
    invalidSource.faces[0].corners[0].vertex = 999;
    const auto invalidSaved = invalidSource;
    rejectWeights(proportionalWeights(invalidSource, {1}, identity, 1, true));
    rejectTransform(transformWeightedVertices(invalidSource, {{1, 1}}, identity, identity));
    REQUIRE(invalidSource == invalidSaved);
}
