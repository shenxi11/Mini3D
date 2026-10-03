/*
 * 模块名: MeshDerivation
 * 功能概述: 以局部归一化投影检查简单环并耳切，生成可上传的 CPU 数据及来源。
 * 对外接口: deriveMesh
 * 依赖关系: MeshValidation、GLM、标准容器与数学
 * 输入输出: 只读多边形快照到独立三角网格，不改源或创建 GPU 句柄。
 * 异常与错误: 投影自交/接触/重叠、无合法耳或索引容量超限返回失败；分配异常传播。
 * 维护说明: 非共面面按面积向量主轴投影；仅接受简单投影，不宣称任意空间多边形支持。
 */
#include "MeshDerivation.h"

#include "MeshValidation.h"

#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace mini3d::core::modeling {
namespace {
constexpr double kProjectionEpsilon = 1.0e-12;
double orient(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
bool onSegment(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& point) {
    return std::abs(orient(a, b, point)) <= kProjectionEpsilon &&
           point.x >= std::min(a.x, b.x) - kProjectionEpsilon &&
           point.x <= std::max(a.x, b.x) + kProjectionEpsilon &&
           point.y >= std::min(a.y, b.y) - kProjectionEpsilon &&
           point.y <= std::max(a.y, b.y) + kProjectionEpsilon;
}
bool segmentsIntersect(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c,
                       const glm::dvec2& d) {
    const auto opposite = [](double x, double y) {
        return (x > kProjectionEpsilon && y < -kProjectionEpsilon) ||
               (y > kProjectionEpsilon && x < -kProjectionEpsilon);
    };
    return (opposite(orient(a, b, c), orient(a, b, d)) &&
            opposite(orient(c, d, a), orient(c, d, b))) ||
           onSegment(a, b, c) || onSegment(a, b, d) || onSegment(c, d, a) || onSegment(c, d, b);
}

bool triangulate(const std::vector<glm::dvec2>& points, std::vector<std::size_t>& ring,
                 std::vector<std::array<std::size_t, 3>>& triangles) {
    triangles.clear();
    const auto count = points.size();
    double signedArea = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        const auto& a = points[i];
        const auto& b = points[next];
        signedArea += a.x * b.y - a.y * b.x;
        // 相邻边仅允许共享端点，不能沿上一段回折重叠。
        const auto& previous = points[(i + count - 1) % count];
        if (onSegment(previous, a, b) || onSegment(a, b, previous)) {
            return false;
        }
        for (std::size_t j = i + 1; j < count; ++j) {
            if (j == next || (j + 1) % count == i) {
                continue;
            }
            if (segmentsIntersect(a, b, points[j], points[(j + 1) % count])) {
                return false;
            }
        }
    }
    if (std::abs(signedArea) <= kProjectionEpsilon) {
        return false;
    }
    const double direction = signedArea > 0.0 ? 1.0 : -1.0;
    ring.resize(count);
    std::iota(ring.begin(), ring.end(), 0);
    triangles.reserve(count - 2);
    while (ring.size() > 3) {
        bool clipped = false;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const auto a = ring[(i + ring.size() - 1) % ring.size()];
            const auto b = ring[i];
            const auto c = ring[(i + 1) % ring.size()];
            if (direction * orient(points[a], points[b], points[c]) <= kProjectionEpsilon) {
                continue;
            }
            const bool containsPoint = std::any_of(ring.begin(), ring.end(), [&](auto point) {
                return point != a && point != b && point != c &&
                       direction * orient(points[a], points[b], points[point]) >=
                           -kProjectionEpsilon &&
                       direction * orient(points[b], points[c], points[point]) >=
                           -kProjectionEpsilon &&
                       direction * orient(points[c], points[a], points[point]) >=
                           -kProjectionEpsilon;
            });
            if (!containsPoint) {
                triangles.push_back({a, b, c});
                ring.erase(ring.begin() + static_cast<std::ptrdiff_t>(i));
                clipped = true;
                break;
            }
        }
        if (!clipped) {
            return false;
        }
    }
    if (direction * orient(points[ring[0]], points[ring[1]], points[ring[2]]) <=
        kProjectionEpsilon) {
        return false;
    }
    triangles.push_back({ring[0], ring[1], ring[2]});
    return true;
}

struct FaceScratch {
    std::vector<glm::dvec3> positions;
    std::vector<glm::dvec2> projected;
    std::vector<std::size_t> ring;
    std::vector<std::array<std::size_t, 3>> triangles;
};

bool deriveFace(const EditableFace& face, std::size_t base, std::size_t indexBase,
                DerivedMesh& output, FaceScratch& scratch, std::string& error) {
    if (face.corners.size() > std::numeric_limits<std::uint32_t>::max() - base) {
        error = "渲染面角数量超过32位索引容量。";
        return false;
    }
    const auto validation = validateFaceGeometry(face, scratch.positions);
    if (!validation.isValid()) {
        error = validation.message;
        return false;
    }
    const auto origin = scratch.positions.front();
    glm::dvec3 areaVector(0.0);
    double scale = 0.0;
    for (std::size_t i = 0; i < scratch.positions.size(); ++i) {
        const auto a = scratch.positions[i] - origin;
        const auto b = scratch.positions[(i + 1) % scratch.positions.size()] - origin;
        areaVector += glm::cross(a, b);
        scale = std::max(scale, glm::length(a));
    }
    const auto normal = glm::normalize(areaVector);
    int droppedAxis = 0;
    for (int axis = 1; axis < 3; ++axis) {
        if (std::abs(normal[axis]) > std::abs(normal[droppedAxis])) {
            droppedAxis = axis;
        }
    }
    scratch.projected.clear();
    scratch.projected.reserve(face.corners.size());
    for (const auto& position : scratch.positions) {
        const auto local = (position - origin) / scale;
        scratch.projected.emplace_back(local[(droppedAxis + 1) % 3], local[(droppedAxis + 2) % 3]);
    }
    if (!triangulate(scratch.projected, scratch.ring, scratch.triangles)) {
        error = "面 ID " + std::to_string(face.id) +
                " 的主轴投影不是可三角化的简单环（交叉、重叠或退化）。";
        return false;
    }
    for (std::size_t i = 0; i < face.corners.size(); ++i) {
        const auto& corner = face.corners[i];
        const auto cornerNormal =
            corner.normal ? glm::normalize(glm::dvec3(*corner.normal)) : normal;
        output.mesh.vertices[base + i] = {glm::vec3(scratch.positions[i]), glm::vec3(cornerNormal),
                                          corner.color, corner.uv};
        output.vertexSources[base + i] = {corner.vertex, corner.id};
    }
    for (std::size_t i = 0; i < scratch.triangles.size(); ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            output.mesh.indices[indexBase + i * 3 + j] =
                static_cast<std::uint32_t>(base + scratch.triangles[i][j]);
        }
        output.triangleSources[indexBase / 3 + i] = {face.id, face.material};
    }
    return true;
}
} // namespace

MeshDerivationResult deriveMesh(const EditableMesh& source) {
    const auto validation = validateEditableMesh(source);
    if (!validation.isValid()) {
        return {{}, validation.message};
    }
    std::unordered_map<VertexId, glm::dvec3> positions;
    positions.reserve(source.vertices.size());
    for (const auto& vertex : source.vertices) {
        positions.emplace(vertex.id, vertex.position);
    }
    DerivedMesh output;
    std::size_t corners = 0;
    for (const auto& face : source.faces)
        corners += face.corners.size();
    if (corners > std::numeric_limits<std::uint32_t>::max())
        return {{}, "渲染面角数量超过32位索引容量。"};
    output.mesh.vertices.resize(corners);
    output.vertexSources.resize(corners);
    output.mesh.indices.resize((corners - source.faces.size() * 2) * 3);
    output.triangleSources.resize(corners - source.faces.size() * 2);
    // 临时容器仅在这次派生中复用，逐面重新填充，保留耳切顺序与来源映射。
    FaceScratch scratch;
    std::size_t base = 0;
    std::size_t indexBase = 0;
    for (const auto& face : source.faces) {
        scratch.positions.clear();
        scratch.positions.reserve(face.corners.size());
        for (const auto& corner : face.corners) {
            scratch.positions.push_back(positions.at(corner.vertex));
        }
        std::string error;
        if (!deriveFace(face, base, indexBase, output, scratch, error)) {
            return {{}, std::move(error)};
        }
        base += face.corners.size();
        indexBase += (face.corners.size() - 2) * 3;
    }
    return {std::move(output), {}};
}

MeshDerivationResult
MeshDerivation::deriveTransformed(const EditableMesh& source, const DerivedMesh& before,
                                  const std::vector<std::size_t>& affectedFaces) {
    // 只为受影响面建立位置索引，避免稀疏拖动重建整个源网格的关联节点。
    std::size_t affectedCorners = 0;
    for (const auto index : affectedFaces) {
        affectedCorners += source.faces[index].corners.size();
    }
    std::unordered_map<VertexId, glm::dvec3> positions;
    positions.reserve(std::min(source.vertices.size(), affectedCorners));
    for (const auto index : affectedFaces) {
        for (const auto& corner : source.faces[index].corners) {
            positions.try_emplace(corner.vertex, 0.0);
        }
    }
    for (const auto& vertex : source.vertices) {
        const auto found = positions.find(vertex.id);
        if (found != positions.end()) {
            found->second = vertex.position;
        }
    }
    auto output = before;
    FaceScratch scratch;
    std::size_t base = 0;
    std::size_t indexBase = 0;
    std::size_t nextAffected = 0;
    for (std::size_t i = 0; i < source.faces.size(); ++i) {
        const auto& face = source.faces[i];
        if (nextAffected < affectedFaces.size() && affectedFaces[nextAffected] == i) {
            scratch.positions.clear();
            scratch.positions.reserve(face.corners.size());
            for (const auto& corner : face.corners) {
                scratch.positions.push_back(positions.at(corner.vertex));
            }
            std::string error;
            if (!deriveFace(face, base, indexBase, output, scratch, error)) {
                return {{}, std::move(error)};
            }
            ++nextAffected;
        }
        base += face.corners.size();
        indexBase += (face.corners.size() - 2) * 3;
    }
    return {std::move(output), {}};
}
} // namespace mini3d::core::modeling
