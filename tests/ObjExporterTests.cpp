/*
 * 模块名: ObjExporterTests
 * 功能概述: 验证源面环、镜像求值、世界变换和静态拆点的真实 OBJ 文本。
 * 对外接口: Catch2 [obj-export] 纯 CPU 用例。
 * 依赖关系: ObjExporter、Mirror、Catch2、GLM、标准字符串流。
 * 输入输出: 源快照/静态三角/非法输入到 OBJ 解析结果和只读性断言。
 * 异常与错误: 部分文本、索引错配、翻面或输入被修改即失败。
 * 维护说明: 不依赖文件 IO、Qt/GL，不将 CPU 解析验证写成 Blender 实测。
 */
#include "core/modeling/Mirror.h"
#include "core/modeling/ObjExporter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <limits>
#include <locale>
#include <sstream>

using namespace mini3d::core;
using namespace mini3d::core::modeling;
namespace {
struct ObjCorner {
    std::size_t vertex = 0;
    std::size_t uv = 0;
    std::size_t normal = 0;
};
struct ParsedObj {
    std::vector<glm::dvec3> vertices, normals;
    std::vector<glm::dvec2> uvs;
    std::vector<std::vector<ObjCorner>> faces;
};

ParsedObj parseObj(const std::string& text) {
    ParsedObj result;
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        std::string kind;
        row >> kind;
        if (kind == "v" || kind == "vn") {
            glm::dvec3 value;
            REQUIRE(static_cast<bool>(row >> value.x >> value.y >> value.z));
            (kind == "v" ? result.vertices : result.normals).push_back(value);
        } else if (kind == "vt") {
            glm::dvec2 value;
            REQUIRE(static_cast<bool>(row >> value.x >> value.y));
            result.uvs.push_back(value);
        } else if (kind == "f") {
            std::vector<ObjCorner> face;
            std::string token;
            while (row >> token) {
                std::istringstream entry(token);
                entry.imbue(std::locale::classic());
                ObjCorner corner;
                char firstSlash = 0, secondSlash = 0;
                REQUIRE(static_cast<bool>(entry >> corner.vertex >> firstSlash >> corner.uv >>
                                          secondSlash >> corner.normal));
                REQUIRE(firstSlash == '/');
                REQUIRE(secondSlash == '/');
                REQUIRE(corner.vertex > 0);
                REQUIRE(corner.vertex <= result.vertices.size());
                REQUIRE(corner.uv > 0);
                REQUIRE(corner.uv <= result.uvs.size());
                REQUIRE(corner.normal > 0);
                REQUIRE(corner.normal <= result.normals.size());
                face.push_back(corner);
            }
            REQUIRE(face.size() >= 3);
            result.faces.push_back(std::move(face));
        }
    }
    return result;
}

void requireNear(const glm::dvec3& actual, const glm::dvec3& expected) {
    for (int axis = 0; axis < 3; ++axis) {
        REQUIRE(actual[axis] == Catch::Approx(expected[axis]).margin(1.0e-6));
    }
}

EditableMesh slantedTriangle() {
    EditableMesh mesh;
    mesh.vertices = {{1001, {0, 0, 0}}, {1003, {1, 0, 1}}, {1009, {0, 1, 1}}};
    EditableFace face;
    face.id = 51;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        MeshCorner corner;
        corner.id = 201 + i;
        corner.vertex = mesh.vertices[i].id;
        corner.uv = {static_cast<float>(i), .25F};
        face.corners.push_back(corner);
    }
    face.corners[0].normal = glm::vec3(1, 2, 3);
    mesh.faces.push_back(std::move(face));
    return mesh;
}

MeshData splitTriangles() {
    MeshData mesh;
    const glm::vec3 positions[] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0},
                                   {0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (std::size_t i = 0; i < 6; ++i) {
        MeshVertex vertex;
        vertex.position = positions[i];
        vertex.normal = {0, 0, 2};
        vertex.uv = {static_cast<float>(i), .5F};
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2, 3, 4, 5};
    return mesh;
}

void requireUnchanged(const MeshData& source, const MeshData& before) {
    REQUIRE(source.indices == before.indices);
    REQUIRE(source.vertices.size() == before.vertices.size());
    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
        REQUIRE(source.vertices[i].position == before.vertices[i].position);
        REQUIRE(source.vertices[i].normal == before.vertices[i].normal);
        REQUIRE(source.vertices[i].uv == before.vertices[i].uv);
        REQUIRE(source.vertices[i].color == before.vertices[i].color);
    }
}

class DecimalComma : public std::numpunct<char> {
  protected:
    char do_decimal_point() const override {
        return ',';
    }
};
struct LocaleGuard {
    std::locale before = std::locale::global(std::locale(std::locale::classic(), new DecimalComma));
    ~LocaleGuard() {
        std::locale::global(before);
    }
};
} // namespace

TEST_CASE("OBJ editable cube retains eight positions and six source quad loops", "[obj-export]") {
    auto source = createEditableCube();
    std::reverse(source.vertices.begin(), source.vertices.end());
    for (auto& corner : source.faces[0].corners) {
        corner.normal.reset();
    }
    const auto before = source;
    const auto exported = encodeObj(source, glm::dmat4(1), "Cube");
    INFO(exported.error);
    REQUIRE(exported.text);
    REQUIRE(exported.error.empty());
    const auto obj = parseObj(*exported.text);
    REQUIRE(obj.vertices.size() == 8);
    REQUIRE(obj.faces.size() == 6);
    REQUIRE(obj.uvs.size() == 24);
    REQUIRE(obj.normals.size() == 24);
    for (std::size_t i = 0; i < source.faces.size(); ++i) {
        const auto& face = obj.faces[i];
        REQUIRE(face.size() == 4);
        for (std::size_t j = 0; j < face.size(); ++j) {
            const auto& expected = source.faces[i].corners[j];
            requireNear(obj.vertices[face[j].vertex - 1], source.vertex(expected.vertex)->position);
            REQUIRE(obj.uvs[face[j].uv - 1] == glm::dvec2(expected.uv));
            REQUIRE(glm::dot(obj.normals[face[j].normal - 1], obj.vertices[face[j].vertex - 1]) ==
                    Catch::Approx(.5));
        }
    }
    REQUIRE(source == before);
}

TEST_CASE("OBJ mirror evaluation exports evaluated loops without changing the source",
          "[obj-export]") {
    auto source = createEditableCube();
    for (auto& vertex : source.vertices) {
        if (vertex.position.x < 0) {
            vertex.position.x = 0;
        }
    }
    std::erase_if(source.faces, [&](const auto& face) {
        return std::all_of(face.corners.begin(), face.corners.end(), [&](const auto& corner) {
            return source.vertex(corner.vertex)->position.x == 0;
        });
    });
    const auto before = source;
    const auto mirrored = evaluateMirror(source, {});
    INFO(mirrored.error);
    REQUIRE(mirrored.evaluation);
    const auto sourceObj = encodeObj(source, glm::dmat4(1), "source");
    const auto evaluatedObj = encodeObj(mirrored.evaluation->mesh, glm::dmat4(1), "evaluated");
    REQUIRE(sourceObj.text);
    REQUIRE(evaluatedObj.text);
    REQUIRE(parseObj(*sourceObj.text).faces.size() == 5);
    const auto obj = parseObj(*evaluatedObj.text);
    REQUIRE(obj.vertices.size() == 12);
    REQUIRE(obj.faces.size() == 10);
    for (const auto& face : obj.faces) {
        REQUIRE(face.size() == 4);
    }
    REQUIRE(source == before);
}

TEST_CASE("OBJ bakes rotated nonuniform world scale and reverses reflected corner attributes",
          "[obj-export]") {
    const auto source = slantedTriangle();
    const auto before = source;
    bool reflected = false;
    SECTION("positive scale") {}
    SECTION("negative scale") {
        reflected = true;
    }
    auto world = glm::translate(glm::dmat4(1), glm::dvec3(4, -2, 3));
    world = glm::rotate(world, .47, glm::normalize(glm::dvec3(1, 2, 3)));
    world = glm::scale(world, glm::dvec3(reflected ? -2 : 2, 3, .5));
    const auto exported = encodeObj(source, world, "slanted");
    INFO(exported.error);
    REQUIRE(exported.text);
    const auto obj = parseObj(*exported.text);
    REQUIRE(obj.faces.size() == 1);
    const auto& face = obj.faces[0];
    const auto normalMatrix = glm::transpose(glm::inverse(glm::dmat3(world)));
    const auto geometric = glm::normalize(
        glm::cross(obj.vertices[face[1].vertex - 1] - obj.vertices[face[0].vertex - 1],
                   obj.vertices[face[2].vertex - 1] - obj.vertices[face[0].vertex - 1]));
    for (std::size_t i = 0; i < 3; ++i) {
        requireNear(obj.vertices[i],
                    glm::dvec3(world * glm::dvec4(source.vertices[i].position, 1)));
        const auto original = reflected ? 2 - i : i;
        const auto& corner = source.faces[0].corners[original];
        REQUIRE(face[i].vertex == original + 1);
        REQUIRE(obj.uvs[face[i].uv - 1] == glm::dvec2(corner.uv));
        const auto normal = obj.normals[face[i].normal - 1];
        REQUIRE(glm::length(normal) == Catch::Approx(1));
        if (corner.normal) {
            requireNear(normal, glm::normalize(normalMatrix * glm::dvec3(*corner.normal)));
        } else {
            requireNear(normal, geometric);
        }
    }
    REQUIRE(source == before);
}

TEST_CASE("OBJ static mesh keeps actual triangles and duplicate seam positions", "[obj-export]") {
    const auto source = splitTriangles();
    const auto before = source;
    auto world = glm::scale(glm::dmat4(1), glm::dvec3(-2, 3, .5));
    const auto exported = encodeObj(source, world, "static");
    INFO(exported.error);
    REQUIRE(exported.text);
    const auto obj = parseObj(*exported.text);
    REQUIRE(obj.vertices.size() == 6);
    REQUIRE(obj.faces.size() == 2);
    REQUIRE(obj.uvs.size() == 6);
    REQUIRE(obj.normals.size() == 6);
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& face = obj.faces[i];
        REQUIRE(face.size() == 3);
        for (std::size_t j = 0; j < 3; ++j) {
            const auto index = i * 3 + 2 - j;
            REQUIRE(face[j].vertex == index + 1);
            REQUIRE(face[j].uv == index + 1);
            REQUIRE(face[j].normal == index + 1);
            REQUIRE(obj.uvs[index] == glm::dvec2(source.vertices[index].uv));
            requireNear(obj.normals[index], glm::dvec3(0, 0, 1));
        }
        REQUIRE(glm::cross(obj.vertices[face[1].vertex - 1] - obj.vertices[face[0].vertex - 1],
                           obj.vertices[face[2].vertex - 1] - obj.vertices[face[0].vertex - 1])
                    .z > 0);
    }
    requireUnchanged(source, before);
}

TEST_CASE("OBJ uses classic decimal text and removes object name line breaks", "[obj-export]") {
    const LocaleGuard locale;
    const auto exported = encodeObj(createEditableCube(), glm::dmat4(1),
                                    "\xE7\xAB\x8B\xE6\x96\xB9\n"
                                    "f 9 9 9\r");
    REQUIRE(exported.text);
    REQUIRE(exported.text->find("v -0.5") != std::string::npos);
    REQUIRE(exported.text->find("\nf 9 9 9") == std::string::npos);
    REQUIRE(exported.text->find('\r') == std::string::npos);
    REQUIRE(exported.text->find("right-handed, Y-up") != std::string::npos);
    REQUIRE(exported.text->find("units unchanged; world transform baked") != std::string::npos);
    REQUIRE(parseObj(*exported.text).faces.size() == 6);
}

TEST_CASE("OBJ rejects invalid editable input and world matrices without partial text or mutation",
          "[obj-export]") {
    auto source = createEditableCube();
    auto world = glm::dmat4(1);
    SECTION("broken vertex reference") {
        source.faces[0].corners[0].vertex = 999;
    }
    SECTION("nonfinite source UV") {
        source.faces[0].corners[0].uv.x = std::numeric_limits<float>::infinity();
    }
    SECTION("degenerate source face") {
        source.vertices[0].position = source.vertices[1].position;
    }
    SECTION("singular world") {
        world[0][0] = 0;
    }
    SECTION("nonfinite world") {
        world[2][1] = std::numeric_limits<double>::quiet_NaN();
    }
    SECTION("projective world") {
        world[1][3] = .5;
    }
    SECTION("world position overflow") {
        world[0][0] = std::numeric_limits<double>::max();
        world[3][0] = std::numeric_limits<double>::max();
    }
    const auto before = source;
    const auto exported = encodeObj(source, world, "invalid");
    REQUIRE_FALSE(exported.text);
    REQUIRE_FALSE(exported.error.empty());
    REQUIRE(source == before);
}

TEST_CASE("OBJ rejects invalid static indices and attributes without mutation", "[obj-export]") {
    auto source = splitTriangles();
    SECTION("out of range index") {
        source.indices.back() = 6;
    }
    SECTION("incomplete triangle") {
        source.indices.pop_back();
    }
    SECTION("nonfinite position") {
        source.vertices[0].position.x = std::numeric_limits<float>::infinity();
    }
    SECTION("nonfinite UV") {
        source.vertices[0].uv.x = std::numeric_limits<float>::infinity();
    }
    SECTION("nonfinite normal") {
        source.vertices[0].normal.x = std::numeric_limits<float>::infinity();
    }
    SECTION("zero normal") {
        source.vertices[0].normal = glm::vec3(0);
    }
    const auto before = source;
    const auto exported = encodeObj(source, glm::dmat4(1), "invalid");
    REQUIRE_FALSE(exported.text);
    REQUIRE_FALSE(exported.error.empty());
    requireUnchanged(source, before);
}
