/*
 * 模块名: ObjExporter
 * 功能概述: 以经典数值 locale 编码 OBJ，烘焙世界位置并按逆转置变换单位法线。
 * 对外接口: encodeObj
 * 依赖关系: MeshDerivation、GLM、C++ 标准流与关联容器。
 * 输入输出: 只读源快照或静态三角数据到 1-based v/vt/vn/f 文本。
 * 异常与错误: 坏索引、非有限属性、奇异矩阵或变换溢出返回中文原因，无部分发布。
 * 维护说明: 源面环不替换为派生三角形；反射反转角序并同步全部属性索引。
 */
#include "ObjExporter.h"

#include "MeshDerivation.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>

namespace mini3d::core::modeling {
namespace {
bool isFinite(const glm::dvec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool prepareTransform(const glm::dmat4& world, glm::dmat3& normals, bool& reverseWinding) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(world[column][row])) {
                return false;
            }
        }
    }
    if (world[0][3] != 0 || world[1][3] != 0 || world[2][3] != 0 || world[3][3] != 1) {
        return false;
    }
    const auto linear = glm::dmat3(world);
    const auto determinant = glm::determinant(linear);
    if (!std::isfinite(determinant) || determinant == 0) {
        return false;
    }
    normals = glm::transpose(glm::inverse(linear));
    for (int column = 0; column < 3; ++column) {
        if (!isFinite(normals[column])) {
            return false;
        }
    }
    reverseWinding = determinant < 0;
    return true;
}

void writeHeader(std::ostringstream& output, const std::string& name) {
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "# Mini3D OBJ export\n"
              "# Coordinates: right-handed, Y-up; units unchanged; world transform baked.\n"
              "# Geometry only: no materials, textures or colors.\n"
              "o ";
    for (const auto character : name) {
        if (character != '\r' && character != '\n') {
            output << character;
        }
    }
    output << '\n';
}

bool writePosition(std::ostringstream& output, const glm::vec3& local, const glm::dmat4& world) {
    const auto position = glm::dvec3(world * glm::dvec4(local, 1));
    if (!isFinite(position)) {
        return false;
    }
    output << "v " << position.x << ' ' << position.y << ' ' << position.z << '\n';
    return true;
}

bool writeAttributes(std::ostringstream& output, const MeshVertex& vertex,
                     const glm::dmat3& normalMatrix) {
    const auto transformed = normalMatrix * glm::dvec3(vertex.normal);
    const auto length = glm::length(transformed);
    if (!std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y) || !isFinite(transformed) ||
        !std::isfinite(length) || length == 0) {
        return false;
    }
    const auto normal = transformed / length;
    output << "vt " << vertex.uv.x << ' ' << vertex.uv.y << '\n';
    output << "vn " << normal.x << ' ' << normal.y << ' ' << normal.z << '\n';
    return true;
}

void writeCorner(std::ostringstream& output, std::size_t vertex, std::size_t attribute) {
    output << ' ' << vertex + 1 << '/' << attribute + 1 << '/' << attribute + 1;
}
} // namespace

ObjExportResult encodeObj(const EditableMesh& source, const glm::dmat4& world,
                          const std::string& name) {
    glm::dmat3 normalMatrix;
    bool reverseWinding = false;
    if (!prepareTransform(world, normalMatrix, reverseWinding)) {
        return {{}, "OBJ 导出需要有限可逆的世界仿射矩阵；缩放不能为零。"};
    }
    const auto derivation = deriveMesh(source);
    if (!derivation.derived) {
        return {{}, derivation.error};
    }
    const auto& derived = *derivation.derived;
    std::ostringstream output;
    writeHeader(output, name);
    std::unordered_map<VertexId, std::size_t> vertexIndices;
    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
        const auto& vertex = source.vertices[i];
        if (!writePosition(output, vertex.position, world)) {
            return {{}, "OBJ 世界坐标包含非有限数。"};
        }
        vertexIndices.emplace(vertex.id, i);
    }
    std::unordered_map<CornerId, std::size_t> cornerIndices;
    for (std::size_t i = 0; i < derived.mesh.vertices.size(); ++i) {
        if (!writeAttributes(output, derived.mesh.vertices[i], normalMatrix)) {
            return {{}, "OBJ UV 或变换后的法线无效。"};
        }
        cornerIndices.emplace(derived.vertexSources[i].corner, i);
    }
    for (const auto& face : source.faces) {
        output << 'f';
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            const auto index = reverseWinding ? face.corners.size() - 1 - i : i;
            const auto& corner = face.corners[index];
            writeCorner(output, vertexIndices.at(corner.vertex), cornerIndices.at(corner.id));
        }
        output << '\n';
    }
    return {output.str(), {}};
}

ObjExportResult encodeObj(const MeshData& source, const glm::dmat4& world,
                          const std::string& name) {
    glm::dmat3 normalMatrix;
    bool reverseWinding = false;
    if (!prepareTransform(world, normalMatrix, reverseWinding)) {
        return {{}, "OBJ 导出需要有限可逆的世界仿射矩阵；缩放不能为零。"};
    }
    if (source.indices.size() % 3 != 0) {
        return {{}, "OBJ 静态网格索引必须组成完整三角形。"};
    }
    for (const auto index : source.indices) {
        if (index >= source.vertices.size()) {
            return {{}, "OBJ 静态网格索引超出顶点范围。"};
        }
    }
    std::ostringstream output;
    writeHeader(output, name);
    for (const auto& vertex : source.vertices) {
        if (!isFinite(glm::dvec3(vertex.position)) || !isFinite(glm::dvec3(vertex.color)) ||
            !writePosition(output, vertex.position, world)) {
            return {{}, "OBJ 静态网格属性或世界坐标包含非有限数。"};
        }
    }
    for (const auto& vertex : source.vertices) {
        if (!writeAttributes(output, vertex, normalMatrix)) {
            return {{}, "OBJ UV 或变换后的法线无效。"};
        }
    }
    for (std::size_t i = 0; i < source.indices.size(); i += 3) {
        output << 'f';
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto index = source.indices[i + (reverseWinding ? 2 - corner : corner)];
            writeCorner(output, index, index);
        }
        output << '\n';
    }
    return {output.str(), {}};
}
} // namespace mini3d::core::modeling
