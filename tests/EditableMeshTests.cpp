/*
 * 模块名: EditableMeshTests
 * 功能概述: 验证共享拓扑、稳定 ID、面角属性和网格合法性拒绝边界。
 * 对外接口: Catch2 [editable-mesh] 纯 CPU 用例
 * 依赖关系: Core modeling、Catch2、GLM
 * 输入输出: Cube/开边界/坏 ID/退化/非流形夹具到明确数值与错误断言。
 * 异常与错误: 验证失败必须保留完整候选快照，不将未检测的全局自交当作已通过。
 * 维护说明: 不创建 Qt/GL，不以 Renderer 的 24 个拆分顶点代替 Cube 的 8 个拓扑顶点。
 */
#include "core/modeling/EditableMesh.h"
#include "core/modeling/MeshValidation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>
#include <limits>
#include <set>

using namespace mini3d::core::modeling;
namespace {
EditableMesh meshFromFaces(const std::vector<glm::vec3>& positions,
                           const std::vector<std::vector<VertexId>>& loops) {
    EditableMesh mesh;
    VertexId vertexId = 1;
    FaceId faceId = 1;
    CornerId cornerId = 1;
    for (const auto& position : positions) {
        mesh.vertices.push_back({vertexId++, position});
    }
    for (const auto& loop : loops) {
        EditableFace face;
        face.id = faceId++;
        for (const auto vertex : loop) {
            MeshCorner corner;
            corner.id = cornerId++;
            corner.vertex = vertex;
            face.corners.push_back(corner);
        }
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}

void requireFailure(const EditableMesh& mesh, MeshError error) {
    const auto result = validateEditableMesh(mesh);
    INFO(result.message);
    REQUIRE_FALSE(result.isValid());
    REQUIRE(result.error == error);
    REQUIRE_FALSE(result.message.empty());
}
} // namespace

TEST_CASE("Editable cube has eight shared vertices twelve source edges and six outward quads",
          "[editable-mesh]") {
    const auto cube = createEditableCube();
    const auto before = cube;
    const auto result = validateEditableMesh(cube);
    INFO(result.message);
    REQUIRE(result.isValid());
    REQUIRE(result.edgeCount == 12);
    REQUIRE(result.boundaryEdgeCount == 0);
    REQUIRE(cube.vertices.size() == 8);
    REQUIRE(cube.faces.size() == 6);
    std::set<CornerId> corners;
    std::set<EdgeKey> edges;
    for (const auto& face : cube.faces) {
        REQUIRE(face.corners.size() == 4);
        REQUIRE(face.material == 0);
        const auto a = cube.vertex(face.corners[0].vertex)->position;
        const auto b = cube.vertex(face.corners[1].vertex)->position;
        const auto c = cube.vertex(face.corners[2].vertex)->position;
        const auto normal = glm::normalize(glm::cross(b - a, c - a));
        REQUIRE(glm::dot(normal, a) == Catch::Approx(0.5F));
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& corner = face.corners[i];
            REQUIRE(corners.insert(corner.id).second);
            REQUIRE(corner.normal.has_value());
            REQUIRE(glm::dot(normal, *corner.normal) == Catch::Approx(1.0F));
            edges.emplace(corner.vertex, face.corners[(i + 1) % 4].vertex);
        }
        REQUIRE(face.corners[0].uv == glm::vec2(0, 0));
        REQUIRE(face.corners[2].uv == glm::vec2(1, 1));
    }
    REQUIRE(corners.size() == 24);
    REQUIRE(edges.size() == 12);
    REQUIRE(EdgeKey(2, 7) == EdgeKey(7, 2));
    REQUIRE(cube.vertex(999) == nullptr);
    REQUIRE(cube == before);
}

TEST_CASE("Explicit mesh IDs and corner attributes survive reordering and remain 64 bit",
          "[editable-mesh]") {
    auto cube = createEditableCube();
    cube.faces[0].material = 987;
    cube.faces[0].corners[0].uv = {2, -1};
    const auto before = cube;
    std::reverse(cube.vertices.begin(), cube.vertices.end());
    std::reverse(cube.faces.begin(), cube.faces.end());
    for (auto& face : cube.faces) {
        std::rotate(face.corners.begin(), face.corners.begin() + 1, face.corners.end());
        const auto original =
            std::find_if(before.faces.begin(), before.faces.end(), [&face](const auto& item) {
                return item.id == face.id;
            });
        REQUIRE(original != before.faces.end());
        REQUIRE(face.material == original->material);
        for (const auto& corner : face.corners) {
            const auto found =
                std::find(original->corners.begin(), original->corners.end(), corner);
            REQUIRE(found != original->corners.end());
        }
    }
    for (const auto& vertex : before.vertices) {
        REQUIRE(*cube.vertex(vertex.id) == vertex);
    }
    REQUIRE(validateEditableMesh(cube).isValid());
    constexpr std::uint64_t offset = std::uint64_t{1} << 40;
    for (auto& vertex : cube.vertices) {
        vertex.id += offset;
    }
    for (auto& face : cube.faces) {
        face.id += offset;
        for (auto& corner : face.corners) {
            corner.id += offset;
            corner.vertex += offset;
        }
    }
    REQUIRE(validateEditableMesh(cube).isValid());
    REQUIRE(cube.vertex(offset + 1)->position == before.vertex(1)->position);
    REQUIRE(cube.vertex(1) == nullptr);
}

TEST_CASE("Mesh validation permits empty snapshots isolated vertices and manifold boundaries",
          "[editable-mesh]") {
    REQUIRE(validateEditableMesh({}).isValid());
    EditableMesh points;
    points.vertices = {{55, {1, 2, 3}}};
    REQUIRE(validateEditableMesh(points).isValid());
    auto cube = createEditableCube();
    cube.faces.erase(cube.faces.begin() + 4);
    const auto result = validateEditableMesh(cube);
    REQUIRE(result.isValid());
    REQUIRE(result.edgeCount == 12);
    REQUIRE(result.boundaryEdgeCount == 4);
    cube.faces.resize(1);
    REQUIRE(validateEditableMesh(cube).isValid());
    REQUIRE(validateEditableMesh(cube).boundaryEdgeCount == 4);
}

TEST_CASE("Mesh validation rejects broken identities references and rings without mutation",
          "[editable-mesh]") {
    auto mesh = createEditableCube();
    MeshError expected = MeshError::InvalidId;
    SECTION("zero vertex") {
        mesh.vertices[0].id = 0;
    }
    SECTION("zero face") {
        mesh.faces[0].id = 0;
    }
    SECTION("zero corner") {
        mesh.faces[0].corners[0].id = 0;
    }
    SECTION("duplicate vertex") {
        mesh.vertices[1].id = mesh.vertices[0].id;
        expected = MeshError::DuplicateId;
    }
    SECTION("duplicate face") {
        mesh.faces[1].id = mesh.faces[0].id;
        expected = MeshError::DuplicateId;
    }
    SECTION("duplicate corner across faces") {
        mesh.faces[1].corners[0].id = mesh.faces[0].corners[0].id;
        expected = MeshError::DuplicateId;
    }
    SECTION("missing vertex") {
        mesh.faces[0].corners[0].vertex = 555;
        expected = MeshError::MissingVertex;
    }
    SECTION("short ring") {
        mesh.faces[0].corners.resize(2);
        expected = MeshError::FaceTooSmall;
    }
    SECTION("repeated adjacent vertex") {
        mesh.faces[0].corners[1].vertex = mesh.faces[0].corners[0].vertex;
        expected = MeshError::RepeatedFaceVertex;
    }
    SECTION("repeated nonadjacent vertex") {
        mesh.faces[0].corners[2].vertex = mesh.faces[0].corners[0].vertex;
        expected = MeshError::RepeatedFaceVertex;
    }
    const auto before = mesh;
    requireFailure(mesh, expected);
    REQUIRE(mesh == before);
}

TEST_CASE("Mesh validation checks finite attributes and allows calculated normals",
          "[editable-mesh]") {
    auto mesh = createEditableCube();
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    SECTION("position infinity") {
        mesh.vertices[0].position.x = infinity;
        requireFailure(mesh, MeshError::NonFiniteAttribute);
    }
    SECTION("position nan") {
        mesh.vertices[0].position.z = nan;
        requireFailure(mesh, MeshError::NonFiniteAttribute);
    }
    SECTION("UV") {
        mesh.faces[0].corners[0].uv.y = nan;
        requireFailure(mesh, MeshError::NonFiniteAttribute);
    }
    SECTION("color") {
        mesh.faces[0].corners[0].color.x = infinity;
        requireFailure(mesh, MeshError::NonFiniteAttribute);
    }
    SECTION("zero normal") {
        mesh.faces[0].corners[0].normal = glm::vec3(0);
        requireFailure(mesh, MeshError::InvalidNormal);
    }
    SECTION("normal infinity") {
        mesh.faces[0].corners[0].normal = glm::vec3(0, infinity, 0);
        requireFailure(mesh, MeshError::InvalidNormal);
    }
    SECTION("no authored normal") {
        mesh.faces[0].corners[0].normal.reset();
        REQUIRE(validateEditableMesh(mesh).isValid());
    }
}

TEST_CASE("Mesh geometry rejects collapse but keeps valid small large and translated shapes",
          "[editable-mesh]") {
    requireFailure(meshFromFaces({{0, 0, 0}, {0, 0, 0}, {0, 1, 0}}, {{1, 2, 3}}),
                   MeshError::DegenerateEdge);
    requireFailure(meshFromFaces({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}, {{1, 2, 3}}),
                   MeshError::DegenerateFace);
    for (const auto scale : {1.0e-20F, 1.0F, 1.0e20F}) {
        auto cube = createEditableCube();
        for (auto& vertex : cube.vertices) {
            vertex.position *= scale;
        }
        REQUIRE(validateEditableMesh(cube).isValid());
    }
    auto cube = createEditableCube();
    for (auto& vertex : cube.vertices) {
        vertex.position += glm::vec3(100000.0F);
    }
    REQUIRE(validateEditableMesh(cube).isValid());
}

TEST_CASE("Mesh validation rejects winding nonmanifold edges and point-contact fans",
          "[editable-mesh]") {
    SECTION("reversed adjacent face") {
        auto cube = createEditableCube();
        std::reverse(cube.faces[0].corners.begin(), cube.faces[0].corners.end());
        requireFailure(cube, MeshError::InconsistentWinding);
    }
    SECTION("three faces at one edge") {
        requireFailure(meshFromFaces({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}},
                                     {{1, 2, 3}, {2, 1, 4}, {1, 2, 5}}),
                       MeshError::NonManifoldEdge);
    }
    SECTION("two boundary fans share one vertex") {
        requireFailure(meshFromFaces({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}},
                                     {{1, 2, 3}, {1, 4, 5}}),
                       MeshError::NonManifoldVertex);
    }
    SECTION("closed fans versus separate components") {
        for (const bool shareVertex : {false, true}) {
            auto first = createEditableCube();
            auto second = createEditableCube();
            for (auto vertex : second.vertices) {
                if (shareVertex && vertex.id == 1) {
                    continue;
                }
                vertex.id += 8;
                vertex.position += glm::vec3(shareVertex ? 1.0F : 3.0F);
                first.vertices.push_back(vertex);
            }
            for (auto face : second.faces) {
                face.id += 6;
                for (auto& corner : face.corners) {
                    corner.id += 24;
                    corner.vertex = shareVertex && corner.vertex == 1 ? 7 : corner.vertex + 8;
                }
                first.faces.push_back(std::move(face));
            }
            if (shareVertex) {
                requireFailure(first, MeshError::NonManifoldVertex);
            } else {
                const auto result = validateEditableMesh(first);
                REQUIRE(result.isValid());
                REQUIRE(result.edgeCount == 24);
                REQUIRE(result.boundaryEdgeCount == 0);
            }
        }
    }
}

TEST_CASE("Vertex fans retain connectivity checks after sparse ID and container reordering",
          "[editable-mesh][perf-regression]") {
    auto mesh = createEditableCube();
    auto expected = MeshError::None;
    std::size_t expectedBoundary = 0;
    SECTION("closed manifold fan") {}
    SECTION("open manifold fan") {
        mesh.faces.erase(mesh.faces.begin() + 4);
        expectedBoundary = 4;
    }
    SECTION("two closed shells share one vertex") {
        mesh = meshFromFaces(
            {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
             {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}},
            {{1, 3, 2}, {1, 2, 4}, {2, 3, 4}, {3, 1, 4},
             {1, 6, 5}, {1, 5, 7}, {5, 6, 7}, {6, 1, 7}});
        expected = MeshError::NonManifoldVertex;
    }
    SECTION("two boundary fans share one vertex") {
        mesh = meshFromFaces({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}},
                             {{1, 2, 3}, {1, 4, 5}});
        expected = MeshError::NonManifoldVertex;
    }
    SECTION("closed and boundary fans share one vertex with two boundary edges") {
        mesh = meshFromFaces(
            {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}},
            {{1, 3, 2}, {1, 2, 4}, {2, 3, 4}, {3, 1, 4}, {1, 5, 6}});
        expected = MeshError::NonManifoldVertex;
    }
    constexpr std::uint64_t prefix = std::uint64_t{1} << 60;
    for (auto& vertex : mesh.vertices) {
        vertex.id = prefix + vertex.id * 97;
    }
    for (auto& face : mesh.faces) {
        face.id = (prefix >> 1) + face.id * 101;
        for (auto& corner : face.corners) {
            corner.id = (prefix >> 2) + corner.id * 103;
            corner.vertex = prefix + corner.vertex * 97;
        }
        std::rotate(face.corners.begin(), face.corners.begin() + 1, face.corners.end());
    }
    std::reverse(mesh.vertices.begin(), mesh.vertices.end());
    std::reverse(mesh.faces.begin(), mesh.faces.end());
    const auto before = mesh;
    const auto result = validateEditableMesh(mesh);
    INFO(result.message);
    REQUIRE(result.error == expected);
    if (expected == MeshError::None) {
        REQUIRE(result.edgeCount == 12);
        REQUIRE(result.boundaryEdgeCount == expectedBoundary);
    } else {
        REQUIRE_FALSE(result.message.empty());
    }
    REQUIRE(mesh == before);
}
