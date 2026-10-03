/*
 * 模块名: ComponentSelectionTests
 * 功能概述: 验证单域组件选择、独立活动项与确定性投影，不使用渲染索引。
 * 对外接口: Catch2 [component-selection]；依赖关系: EditableMesh、ComponentSelection。
 * 输入输出: Cube 及重排/删除夹具到集合不变量；无 Qt/GL。
 * 异常与错误: 非法 ID 必须无副作用；维护说明: 不把状态测试当作鼠标拾取验收。
 */
#include "editor/ComponentSelection.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <iterator>

using namespace mini3d;
using editor::ComponentId;
using editor::ComponentSelection;
using editor::SelectionDomain;
using editor::SelectionOperation;

namespace {
core::modeling::EditableMesh sparseReorderedCube() {
    auto mesh = core::modeling::createEditableCube();
    constexpr std::uint64_t base = std::uint64_t{1} << 40;
    for (auto& vertex : mesh.vertices) {
        vertex.id = base + vertex.id * 13;
    }
    for (auto& face : mesh.faces) {
        face.id = base + face.id * 19;
        for (auto& corner : face.corners) {
            corner.vertex = base + corner.vertex * 13;
        }
    }
    std::reverse(mesh.vertices.begin(), mesh.vertices.end());
    std::reverse(mesh.faces.begin(), mesh.faces.end());
    return mesh;
}

void requireDomainAvailabilityMatchesProjection(const core::modeling::EditableMesh& mesh,
                                                const ComponentSelection& selection) {
    const auto before = selection;
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        INFO(static_cast<int>(domain));
        auto projected = selection;
        projected.setDomain(mesh, domain);
        REQUIRE(selection.hasSelectionInDomain(mesh, domain) == !projected.selectedIds().empty());
        REQUIRE(selection == before);
    }
}
} // namespace

TEST_CASE("Component selection uses source topology and keeps active independent",
          "[component-selection]") {
    const auto cube = core::modeling::createEditableCube();
    REQUIRE(ComponentSelection::elements(cube, SelectionDomain::Vertex).size() == 8);
    REQUIRE(ComponentSelection::elements(cube, SelectionDomain::Edge).size() == 12);
    REQUIRE(ComponentSelection::elements(cube, SelectionDomain::Face).size() == 6);
    const auto& face = cube.faces[0];
    const auto diagonal = ComponentId::edge({face.corners[0].vertex, face.corners[2].vertex});
    REQUIRE_FALSE(ComponentSelection::elements(cube, SelectionDomain::Edge).contains(diagonal));
    REQUIRE(ComponentId::edge({5, 2}) == ComponentId{2, 5});

    ComponentSelection selection;
    REQUIRE(selection.select(cube, {6}, SelectionOperation::Replace));
    REQUIRE(selection.select(cube, {2}, SelectionOperation::Add));
    REQUIRE(selection.selectedIds().size() == 2);
    REQUIRE(selection.activeId() == ComponentId{2});
    REQUIRE(selection.select(cube, {6}, SelectionOperation::Add));
    REQUIRE(selection.selectedIds().size() == 2);
    REQUIRE(selection.activeId() == ComponentId{6});
    REQUIRE(selection.select(cube, {6}, SelectionOperation::Toggle));
    REQUIRE(selection.activeId() == ComponentId{2});
    REQUIRE(selection.select(cube, {2}, SelectionOperation::Remove));
    REQUIRE(selection.selectedIds().empty());
    REQUIRE_FALSE(selection.activeId());
    REQUIRE_FALSE(selection.clear());
    REQUIRE(selection.selectAll(cube));
    REQUIRE(selection.activeId() == ComponentId{1});
    REQUIRE_FALSE(selection.selectAll(cube));
    const auto before = selection;
    REQUIRE_FALSE(selection.select(cube, {999999}, SelectionOperation::Replace));
    REQUIRE_FALSE(selection.select(cube, {1, 2}, SelectionOperation::Replace));
    REQUIRE(selection == before);
}

TEST_CASE("Domain projection expands boundaries and only promotes complete selected boundaries",
          "[component-selection]") {
    const auto cube = core::modeling::createEditableCube();
    const auto& face = cube.faces[0];
    ComponentSelection selection;
    REQUIRE(selection.setDomain(cube, SelectionDomain::Face));
    REQUIRE(selection.select(cube, {face.id}, SelectionOperation::Replace));
    REQUIRE(selection.setDomain(cube, SelectionDomain::Edge));
    REQUIRE(selection.selectedIds().size() == 4);
    REQUIRE(selection.setDomain(cube, SelectionDomain::Face));
    REQUIRE(selection.selectedIds() == std::set<ComponentId>{{face.id}});
    REQUIRE(selection.setDomain(cube, SelectionDomain::Vertex));
    REQUIRE(selection.selectedIds().size() == 4);
    REQUIRE(selection.setDomain(cube, SelectionDomain::Face));
    REQUIRE(selection.selectedIds() == std::set<ComponentId>{{face.id}});
    REQUIRE(selection.setDomain(cube, SelectionDomain::Edge));
    const auto edge = *selection.selectedIds().begin();
    REQUIRE(selection.select(cube, edge, SelectionOperation::Remove));
    REQUIRE(selection.selectedIds().size() == 3);
    REQUIRE(selection.setDomain(cube, SelectionDomain::Face));
    REQUIRE(selection.selectedIds().empty());
    REQUIRE_FALSE(selection.activeId());
    REQUIRE(selection.setDomain(cube, SelectionDomain::Vertex));
    REQUIRE(selection.select(cube, {face.corners[0].vertex}, SelectionOperation::Replace));
    REQUIRE(selection.setDomain(cube, SelectionDomain::Edge));
    REQUIRE(selection.selectedIds().empty());
    REQUIRE(selection.selectAll(cube));
    REQUIRE(selection.setDomain(cube, SelectionDomain::Face));
    REQUIRE(selection.selectedIds().size() == 6);
    REQUIRE_FALSE(selection.setDomain(cube, SelectionDomain::Face));
}

TEST_CASE("Stable component IDs survive source reorder and prune removed active elements",
          "[component-selection]") {
    auto mesh = core::modeling::createEditableCube();
    constexpr std::uint64_t offset = std::uint64_t{1} << 40;
    for (auto& vertex : mesh.vertices) {
        vertex.id += offset;
    }
    for (auto& face : mesh.faces) {
        face.id += offset;
        for (auto& corner : face.corners) {
            corner.vertex += offset;
        }
    }
    ComponentSelection selection;
    REQUIRE(selection.setDomain(mesh, SelectionDomain::Face));
    REQUIRE(selection.selectAll(mesh));
    const auto removed = mesh.faces[0].id;
    REQUIRE(selection.activeId() == ComponentId{removed});
    const auto before = selection;
    std::reverse(mesh.vertices.begin(), mesh.vertices.end());
    std::reverse(mesh.faces.begin(), mesh.faces.end());
    REQUIRE_FALSE(selection.reconcile(mesh));
    REQUIRE(selection == before);
    mesh.faces.pop_back();
    REQUIRE(selection.reconcile(mesh));
    REQUIRE(selection.selectedIds().size() == 5);
    REQUIRE_FALSE(selection.selectedIds().contains({removed}));
    REQUIRE(selection.activeId() == *selection.selectedIds().begin());
    mesh.faces.clear();
    REQUIRE(selection.reconcile(mesh));
    REQUIRE_FALSE(selection.activeId());
    REQUIRE_FALSE(selection.activePosition(mesh));
}

TEST_CASE("Active component coordinates use vertices edge midpoint and polygon center",
          "[component-selection]") {
    const auto cube = core::modeling::createEditableCube();
    ComponentSelection selection;
    REQUIRE(selection.select(cube, {1}, SelectionOperation::Replace));
    REQUIRE(selection.activePosition(cube) == cube.vertex(1)->position);
    const auto& face = cube.faces[0];
    const auto first = face.corners[0].vertex;
    const auto second = face.corners[1].vertex;
    REQUIRE(selection.setDomain(cube, SelectionDomain::Edge));
    REQUIRE(
        selection.select(cube, ComponentId::edge({first, second}), SelectionOperation::Replace));
    REQUIRE(selection.activePosition(cube) ==
            (cube.vertex(first)->position + cube.vertex(second)->position) * 0.5F);
    REQUIRE(selection.setDomain(cube, SelectionDomain::Face));
    REQUIRE(selection.select(cube, {face.id}, SelectionOperation::Replace));
    glm::vec3 center{0};
    for (const auto& corner : face.corners) {
        center += cube.vertex(corner.vertex)->position;
    }
    center /= static_cast<float>(face.corners.size());
    REQUIRE(selection.activePosition(cube) == center);
}

TEST_CASE("Batch component selection performs one deterministic set operation",
          "[component-selection]") {
    const auto cube = core::modeling::createEditableCube();
    ComponentSelection selection;
    REQUIRE(selection.selectMany(cube, {{2}, {4}, {999}}, SelectionOperation::Replace));
    REQUIRE(selection.selectedIds() == std::set<ComponentId>{{2}, {4}});
    REQUIRE(selection.activeId() == ComponentId{2});
    REQUIRE_FALSE(selection.selectMany(cube, {{2}, {4}}, SelectionOperation::Replace));
    REQUIRE(selection.selectMany(cube, {{1}, {3}}, SelectionOperation::Add));
    REQUIRE(selection.activeId() == ComponentId{2});
    REQUIRE(selection.selectMany(cube, {{2}, {4}}, SelectionOperation::Remove));
    REQUIRE(selection.selectedIds() == std::set<ComponentId>{{1}, {3}});
    REQUIRE(selection.activeId() == ComponentId{1});
    REQUIRE(selection.selectMany(cube, {{1}, {4}}, SelectionOperation::Toggle));
    REQUIRE(selection.selectedIds() == std::set<ComponentId>{{3}, {4}});
    REQUIRE(selection.activeId() == ComponentId{3});
    REQUIRE_FALSE(selection.selectMany(cube, {}, SelectionOperation::Add));
    REQUIRE(selection.selectMany(cube, {}, SelectionOperation::Replace));
    REQUIRE_FALSE(selection.activeId());
    REQUIRE(selection.setDomain(cube, SelectionDomain::Edge));
    const auto edges = ComponentSelection::elements(cube, SelectionDomain::Edge);
    REQUIRE(selection.selectMany(cube, edges, SelectionOperation::Replace));
    REQUIRE(selection.selectedIds() == edges);
}

TEST_CASE("Candidate filtering retains sparse domain identities set operations and active order",
          "[component-selection]") {
    const auto mesh = sparseReorderedCube();
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        INFO(static_cast<int>(domain));
        const auto elements = ComponentSelection::elements(mesh, domain);
        const auto first = *elements.begin();
        const auto middle = *std::next(elements.begin());
        const auto last = *elements.rbegin();
        const std::set<ComponentId> invalid{
            {0}, {std::uint64_t{1} << 50}, {first.first, std::uint64_t{1} << 50}};
        ComponentSelection selection;
        selection.setDomain(mesh, domain);
        REQUIRE(selection.select(mesh, first, SelectionOperation::Replace));
        REQUIRE_FALSE(selection.select(mesh, first, SelectionOperation::Replace));
        REQUIRE(selection.select(mesh, last, SelectionOperation::Add));
        REQUIRE(selection.activeId() == last);
        const auto before = selection;
        if (domain == SelectionDomain::Edge) {
            REQUIRE_FALSE(
                selection.select(mesh, {first.second, first.first}, SelectionOperation::Replace));
            const auto& face = mesh.faces.front();
            REQUIRE_FALSE(selection.select(
                mesh, ComponentId::edge({face.corners[0].vertex, face.corners[2].vertex}),
                SelectionOperation::Replace));
            REQUIRE(selection == before);
        }
        for (const auto operation : {SelectionOperation::Replace, SelectionOperation::Add,
                                     SelectionOperation::Toggle, SelectionOperation::Remove}) {
            for (const auto id : invalid) {
                REQUIRE_FALSE(selection.select(mesh, id, operation));
                REQUIRE(selection == before);
            }
        }
        auto input = invalid;
        input.insert(first);
        input.insert(middle);
        REQUIRE(selection.selectMany(mesh, input, SelectionOperation::Replace));
        REQUIRE(selection.selectedIds() == std::set<ComponentId>{first, middle});
        REQUIRE(selection.activeId() == first);
        REQUIRE(selection.select(mesh, middle, SelectionOperation::Add));
        REQUIRE(selection.activeId() == middle);
        REQUIRE_FALSE(selection.selectMany(mesh, input, SelectionOperation::Replace));
        REQUIRE(selection.activeId() == middle);
        input = invalid;
        input.insert(last);
        REQUIRE(selection.selectMany(mesh, input, SelectionOperation::Add));
        REQUIRE(selection.activeId() == middle);
        input = invalid;
        input.insert(first);
        REQUIRE(selection.selectMany(mesh, input, SelectionOperation::Remove));
        REQUIRE(selection.selectedIds() == std::set<ComponentId>{middle, last});
        REQUIRE(selection.activeId() == middle);
        input = invalid;
        input.insert(middle);
        input.insert(last);
        REQUIRE(selection.selectMany(mesh, input, SelectionOperation::Toggle));
        REQUIRE(selection.selectedIds().empty());
        REQUIRE_FALSE(selection.activeId());
        REQUIRE_FALSE(selection.selectMany(mesh, invalid, SelectionOperation::Replace));
        REQUIRE(selection.select(mesh, first, SelectionOperation::Replace));
        REQUIRE(selection.selectMany(mesh, invalid, SelectionOperation::Replace));
        REQUIRE(selection.selectedIds().empty());

        REQUIRE(selection.selectAll(mesh));
        auto reordered = mesh;
        std::reverse(reordered.vertices.begin(), reordered.vertices.end());
        std::reverse(reordered.faces.begin(), reordered.faces.end());
        REQUIRE_FALSE(selection.reconcile(reordered));
        REQUIRE(selection.selectedIds() == elements);
        if (domain == SelectionDomain::Vertex) {
            reordered.vertices.clear();
        } else {
            reordered.faces.clear();
        }
        REQUIRE(selection.reconcile(reordered));
        REQUIRE(selection.selectedIds().empty());
        REQUIRE_FALSE(selection.activeId());
    }
}

TEST_CASE("Every sparse vertex subset projects like the full source boundary set",
          "[component-selection]") {
    const auto mesh = sparseReorderedCube();
    const auto allEdges = ComponentSelection::elements(mesh, SelectionDomain::Edge);
    for (unsigned subset = 0; subset < (1U << mesh.vertices.size()); ++subset) {
        INFO(subset);
        std::set<ComponentId> vertices;
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
            if ((subset & (1U << i)) != 0) {
                vertices.insert({mesh.vertices[i].id});
            }
        }
        std::set<ComponentId> edges;
        for (const auto edge : allEdges) {
            if (vertices.contains({edge.first}) && vertices.contains({edge.second})) {
                edges.insert(edge);
            }
        }
        std::set<ComponentId> faces;
        for (const auto& face : mesh.faces) {
            bool complete = true;
            for (std::size_t i = 0; i < face.corners.size(); ++i) {
                complete = complete && edges.contains(ComponentId::edge(
                                           {face.corners[i].vertex,
                                            face.corners[(i + 1) % face.corners.size()].vertex}));
            }
            if (complete) {
                faces.insert({face.id});
            }
        }
        ComponentSelection selected;
        selected.selectMany(mesh, vertices, SelectionOperation::Replace);
        requireDomainAvailabilityMatchesProjection(mesh, selected);
        for (const auto domain : {SelectionDomain::Edge, SelectionDomain::Face}) {
            auto projected = selected;
            REQUIRE(projected.setDomain(mesh, domain));
            const auto& expected = domain == SelectionDomain::Edge ? edges : faces;
            REQUIRE(projected.selectedIds() == expected);
            REQUIRE(projected.activeId() ==
                    (expected.empty() ? std::nullopt : std::optional(*expected.begin())));
        }
    }
}

TEST_CASE("Read-only domain availability matches empty partial and complete selections",
          "[component-selection]") {
    const auto mesh = sparseReorderedCube();
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        INFO(static_cast<int>(domain));
        ComponentSelection selection;
        selection.setDomain(mesh, domain);
        requireDomainAvailabilityMatchesProjection(mesh, selection);
        const auto elements = ComponentSelection::elements(mesh, domain);
        REQUIRE(selection.select(mesh, *elements.begin(), SelectionOperation::Replace));
        requireDomainAvailabilityMatchesProjection(mesh, selection);
        REQUIRE(selection.selectAll(mesh));
        REQUIRE(selection.select(mesh, *elements.rbegin(), SelectionOperation::Add));
        requireDomainAvailabilityMatchesProjection(mesh, selection);
    }
}

TEST_CASE("Domain availability preserves unreconciled identities and projection behavior",
          "[component-selection]") {
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        INFO(static_cast<int>(domain));
        auto mesh = sparseReorderedCube();
        ComponentSelection selection;
        selection.setDomain(mesh, domain);
        REQUIRE(selection.selectAll(mesh));
        if (domain == SelectionDomain::Vertex) {
            mesh.vertices.clear();
        } else {
            mesh.faces.clear();
        }
        requireDomainAvailabilityMatchesProjection(mesh, selection);
        REQUIRE(selection.hasSelectionInDomain(mesh, domain));
        if (domain == SelectionDomain::Face) {
            REQUIRE_FALSE(selection.hasSelectionInDomain(mesh, SelectionDomain::Vertex));
            REQUIRE_FALSE(selection.hasSelectionInDomain(mesh, SelectionDomain::Edge));
        } else {
            REQUIRE(selection.hasSelectionInDomain(mesh, SelectionDomain::Vertex));
            REQUIRE(selection.hasSelectionInDomain(mesh, SelectionDomain::Edge));
            REQUIRE(selection.hasSelectionInDomain(mesh, SelectionDomain::Face) ==
                    (domain == SelectionDomain::Vertex));
        }
    }
}

TEST_CASE("An isolated selected vertex does not make an edge or face projection available",
          "[component-selection]") {
    auto mesh = sparseReorderedCube();
    auto isolated = mesh.vertices.front();
    isolated.id = std::uint64_t{1} << 50;
    mesh.vertices.push_back(isolated);
    ComponentSelection selection;
    REQUIRE(selection.select(mesh, {isolated.id}, SelectionOperation::Replace));
    requireDomainAvailabilityMatchesProjection(mesh, selection);
    REQUIRE(selection.hasSelectionInDomain(mesh, SelectionDomain::Vertex));
    REQUIRE_FALSE(selection.hasSelectionInDomain(mesh, SelectionDomain::Edge));
    REQUIRE_FALSE(selection.hasSelectionInDomain(mesh, SelectionDomain::Face));
}

TEST_CASE("Domain availability keeps empty boundary face projection semantics",
          "[component-selection]") {
    auto mesh = sparseReorderedCube();
    auto emptyFace = mesh.faces.front();
    emptyFace.id = std::uint64_t{1} << 50;
    emptyFace.corners.clear();
    mesh.faces.push_back(emptyFace);
    for (const auto domain : {SelectionDomain::Vertex, SelectionDomain::Edge}) {
        ComponentSelection selection;
        selection.setDomain(mesh, domain);
        REQUIRE(selection.selectedIds().empty());
        requireDomainAvailabilityMatchesProjection(mesh, selection);
        REQUIRE(selection.hasSelectionInDomain(mesh, SelectionDomain::Face));
    }
    ComponentSelection selection;
    REQUIRE(selection.setDomain(mesh, SelectionDomain::Face));
    REQUIRE(selection.selectedIds() == std::set<ComponentId>{{emptyFace.id}});
    requireDomainAvailabilityMatchesProjection(mesh, selection);
    REQUIRE_FALSE(selection.hasSelectionInDomain(mesh, SelectionDomain::Vertex));
    REQUIRE_FALSE(selection.hasSelectionInDomain(mesh, SelectionDomain::Edge));
}
