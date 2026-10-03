/*
 * 模块名: VertexTransformTests
 * 功能概述: 验证组件变换的去重、坐标空间、属性与非法候选原子性。
 * 对外接口: Catch2 [vertex-transform]；依赖关系: Core、ComponentSelection，无 Qt/GL。
 * 输入输出: 固定 Cube 快照与仿射增量到几何/法线/身份断言。
 * 异常与错误: 失败不改变 before；维护说明: 不把对象 TRS 变换当成组件变形。
 */
#include "core/modeling/VertexTransform.h"
#include "editor/ComponentSelection.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>

using namespace mini3d;
using namespace core::modeling;

TEST_CASE("Component domains expand to unique vertices and centroid counts each vertex once",
          "[vertex-transform]") {
    const auto mesh = createEditableCube();
    editor::ComponentSelection selection;
    REQUIRE(selection.selectedVertices(mesh).empty());
    REQUIRE(selection.setDomain(mesh, editor::SelectionDomain::Face));
    REQUIRE(selection.select(mesh, {mesh.faces[0].id}, editor::SelectionOperation::Replace));
    REQUIRE(selection.select(mesh, {mesh.faces[2].id}, editor::SelectionOperation::Add));
    const auto ids = selection.selectedVertices(mesh);
    REQUIRE(ids.size() == 6);
    glm::dvec3 expected{0};
    for (const auto id : ids)
        expected += glm::dvec3(mesh.vertex(id)->position);
    expected /= static_cast<double>(ids.size());
    REQUIRE(vertexSelectionCenter(mesh, ids, glm::dmat4(1)) == expected);
    REQUIRE_FALSE(vertexSelectionCenter(mesh, {}, glm::dmat4(1)));
    REQUIRE_FALSE(vertexSelectionCenter(mesh, {999}, glm::dmat4(1)));
    REQUIRE(selection.setDomain(mesh, editor::SelectionDomain::Edge));
    REQUIRE(selection.selectedVertices(mesh) == ids);
}

TEST_CASE("Single vertex transform leaves object topology UV color and untouched faces intact",
          "[vertex-transform]") {
    const auto before = createEditableCube();
    const auto saved = before;
    const auto id = before.vertices.front().id;
    const glm::dvec3 delta{0.2, 0.1, -0.1};
    const auto result =
        transformVertices(before, {id}, glm::dmat4(1), glm::translate(glm::dmat4(1), delta));
    INFO(result.error);
    REQUIRE(result.mesh);
    REQUIRE(before == saved);
    REQUIRE(result.mesh->vertices.size() == before.vertices.size());
    REQUIRE(result.mesh->faces.size() == before.faces.size());
    for (const auto& vertex : before.vertices) {
        const auto expected =
            glm::dvec3(vertex.position) + (vertex.id == id ? delta : glm::dvec3(0));
        REQUIRE(glm::length(glm::dvec3(result.mesh->vertex(vertex.id)->position) - expected) <
                1.0e-7);
    }
    for (std::size_t i = 0; i < before.faces.size(); ++i) {
        const auto& original = before.faces[i];
        const auto& changed = result.mesh->faces[i];
        REQUIRE(changed.id == original.id);
        const bool affected =
            std::any_of(original.corners.begin(), original.corners.end(), [id](const auto& corner) {
                return corner.vertex == id;
            });
        for (std::size_t j = 0; j < original.corners.size(); ++j) {
            REQUIRE(changed.corners[j].id == original.corners[j].id);
            REQUIRE(changed.corners[j].vertex == original.corners[j].vertex);
            REQUIRE(changed.corners[j].uv == original.corners[j].uv);
            REQUIRE(changed.corners[j].color == original.corners[j].color);
            if (affected)
                REQUIRE_FALSE(changed.corners[j].normal);
            else
                REQUIRE(changed.corners[j].normal == original.corners[j].normal);
        }
    }
    REQUIRE(transformVertices(before, {id}, glm::dmat4(1), glm::dmat4(1)).mesh == before);
    REQUIRE(
        transformVertices(before, {id}, glm::dmat4(1), glm::translate(glm::dmat4(1), delta)).mesh ==
        result.mesh);
}

TEST_CASE(
    "Component world transforms support negative nonuniform parents without TRS decomposition",
    "[vertex-transform]") {
    const auto before = createEditableCube();
    std::set<VertexId> ids;
    for (const auto& vertex : before.vertices)
        ids.insert(vertex.id);
    const auto world = glm::translate(glm::dmat4(1), glm::dvec3(2, 3, -1)) *
                       glm::rotate(glm::dmat4(1), 0.6, glm::dvec3(0, 1, 0)) *
                       glm::scale(glm::dmat4(1), glm::dvec3(-2, 0.7, 1.5));
    const auto pivot = *vertexSelectionCenter(before, ids, world);
    for (const auto delta : {glm::rotate(glm::dmat4(1), 0.7, glm::dvec3(0, 0, 1)),
                             glm::scale(glm::dmat4(1), glm::dvec3(1.3, 0.6, 1)),
                             glm::scale(glm::dmat4(1), glm::dvec3(-1, 1, 1))}) {
        const auto aroundPivot =
            glm::translate(glm::dmat4(1), pivot) * delta * glm::translate(glm::dmat4(1), -pivot);
        const auto result = transformVertices(before, ids, world, aroundPivot);
        INFO(result.error);
        REQUIRE(result.mesh);
        for (const auto& vertex : before.vertices) {
            const auto expected = aroundPivot * world * glm::dvec4(vertex.position, 1);
            const auto actual = world * glm::dvec4(result.mesh->vertex(vertex.id)->position, 1);
            REQUIRE(glm::length(actual - expected) < 1.0e-6);
        }
        for (const auto& face : result.mesh->faces) {
            const auto a = result.mesh->vertex(face.corners[0].vertex)->position;
            const auto b = result.mesh->vertex(face.corners[1].vertex)->position;
            const auto c = result.mesh->vertex(face.corners[2].vertex)->position;
            const auto normal = glm::normalize(glm::cross(b - a, c - a));
            REQUIRE(face.corners[0].normal);
            REQUIRE(glm::dot(*face.corners[0].normal, normal) > 0.999F);
        }
    }
    REQUIRE(transformVertices(before, ids, world, glm::dmat4(1)).mesh == before);
}

TEST_CASE("Invalid component transforms and collapsed topology return no partial candidate",
          "[vertex-transform]") {
    const auto before = createEditableCube();
    const auto id = before.faces.front().corners[0].vertex;
    const auto other = before.faces.front().corners[1].vertex;
    REQUIRE_FALSE(transformVertices(before, {}, glm::dmat4(1), glm::dmat4(1)).mesh);
    REQUIRE_FALSE(transformVertices(before, {999}, glm::dmat4(1), glm::dmat4(1)).mesh);
    REQUIRE_FALSE(transformVertices(before, {id}, glm::dmat4(0), glm::dmat4(1)).mesh);
    REQUIRE_FALSE(transformVertices(before, {id}, glm::dmat4(1),
                                    glm::scale(glm::dmat4(1), glm::dvec3(0, 1, 1)))
                      .mesh);
    auto invalid = glm::dmat4(1);
    invalid[3][0] = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE(transformVertices(before, {id}, glm::dmat4(1), invalid).mesh);
    invalid[3][0] = std::numeric_limits<double>::max();
    REQUIRE_FALSE(transformVertices(before, {id}, glm::dmat4(1), invalid).mesh);
    const auto collapse = glm::translate(
        glm::dmat4(1), glm::dvec3(before.vertex(other)->position - before.vertex(id)->position));
    REQUIRE_FALSE(transformVertices(before, {id}, glm::dmat4(1), collapse).mesh);
}

TEST_CASE("Vertex transform retains only its freshly validated derived candidate",
          "[vertex-transform][perf-regression]") {
    auto before = createEditableCube();
    before.faces[0].corners[0].uv = {-2, 3};
    before.faces[0].corners[0].color = {0.2F, 0.4F, 0.8F};
    const auto saved = before;
    const std::set<VertexId> selected{before.vertices.front().id};
    const auto moved = transformVertices(before, selected, glm::dmat4(1),
                                         glm::translate(glm::dmat4(1), glm::dvec3(0.2, 0.1, 0)));
    INFO(moved.error);
    REQUIRE(moved.mesh);
    REQUIRE(moved.derived);
    const auto expected = deriveMesh(*moved.mesh);
    REQUIRE(expected.derived);
    const auto& actual = *moved.derived;
    const auto& complete = *expected.derived;
    REQUIRE(actual.mesh.indices == complete.mesh.indices);
    REQUIRE(actual.mesh.vertices.size() == complete.mesh.vertices.size());
    REQUIRE(actual.vertexSources.size() == complete.vertexSources.size());
    REQUIRE(actual.triangleSources.size() == complete.triangleSources.size());
    for (std::size_t i = 0; i < actual.mesh.vertices.size(); ++i) {
        REQUIRE(actual.mesh.vertices[i].position == complete.mesh.vertices[i].position);
        REQUIRE(actual.mesh.vertices[i].normal == complete.mesh.vertices[i].normal);
        REQUIRE(actual.mesh.vertices[i].uv == complete.mesh.vertices[i].uv);
        REQUIRE(actual.mesh.vertices[i].color == complete.mesh.vertices[i].color);
        REQUIRE(actual.vertexSources[i].vertex == complete.vertexSources[i].vertex);
        REQUIRE(actual.vertexSources[i].corner == complete.vertexSources[i].corner);
    }
    for (std::size_t i = 0; i < actual.triangleSources.size(); ++i) {
        REQUIRE(actual.triangleSources[i].face == complete.triangleSources[i].face);
        REQUIRE(actual.triangleSources[i].material == complete.triangleSources[i].material);
    }
    const auto unchanged = transformVertices(before, selected, glm::dmat4(1), glm::dmat4(1));
    REQUIRE(unchanged.mesh == before);
    REQUIRE_FALSE(unchanged.derived);
    REQUIRE(unchanged.error.empty());
    const auto failed = transformVertices(before, selected, glm::dmat4(1),
                                          glm::scale(glm::dmat4(1), glm::dvec3(0, 1, 1)));
    REQUIRE_FALSE(failed.mesh);
    REQUIRE_FALSE(failed.derived);
    REQUIRE_FALSE(failed.error.empty());
    REQUIRE(before == saved);
}

TEST_CASE("Moving an isolated source vertex still validates and retains a derived candidate",
          "[vertex-transform][perf-regression]") {
    auto source = createEditableCube();
    source.vertices.push_back({999, {10, 10, 10}});
    const auto before = source;
    const auto result = transformVertices(source, {999}, glm::dmat4(1),
                                          glm::translate(glm::dmat4(1), glm::dvec3(1, 2, 3)));
    REQUIRE(result.mesh);
    REQUIRE(result.derived);
    REQUIRE(result.mesh->vertex(999)->position == glm::vec3(11, 12, 13));
    REQUIRE(result.mesh->faces == source.faces);
    REQUIRE(result.derived->mesh.indices == deriveMesh(source).derived->mesh.indices);
    REQUIRE(source == before);
}
