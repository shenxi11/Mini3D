/*
 * 模块名: MeshValidation
 * 功能概述: 按显式 ID 建立临时索引并验证面环、边配对与顶点单扇连通。
 * 对外接口: validateEditableMesh
 * 依赖关系: GLM、标准关联容器与数学函数
 * 输入输出: 只读候选快照到校验结果，不发布场景或生成 GPU 数据。
 * 异常与错误: 发现首个非法项即返回中文原因；分配失败向上传播。
 * 维护说明: 面面积使用局部原点与 double 计算，避免大坐标平移导致消减。
 */
#include "MeshValidation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/geometric.hpp>
#include <unordered_map>
#include <unordered_set>

namespace mini3d::core::modeling {
namespace {
constexpr double kRelativeAreaEpsilon = 1.0e-10;
bool isFinite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
struct EdgeUse {
    VertexId from;
    std::size_t fromCorner;
    std::size_t toCorner;
    bool paired = false;
};
struct EdgeHash {
    std::size_t operator()(EdgeKey edge) const {
        const auto first = std::hash<VertexId>{}(edge.first);
        const auto second = std::hash<VertexId>{}(edge.second);
        return first ^ (second + 0x9e3779b9U + (first << 6) + (first >> 2));
    }
};
struct VertexFan {
    std::size_t components = 0;
    int boundaryEdges = 0;
};
MeshValidation failure(MeshError error, const std::string& reason, std::uint64_t id) {
    return {error, reason + "（ID " + std::to_string(id) + "）。"};
}
MeshValidation validateCornerNormal(const MeshCorner& corner) {
    if (corner.normal &&
        (!isFinite(*corner.normal) || glm::length(glm::dvec3(*corner.normal)) == 0.0)) {
        return failure(MeshError::InvalidNormal, "硬法线必须有限且非零", corner.id);
    }
    return {};
}
MeshValidation accumulateFaceEdge(FaceId face, const glm::dvec3& origin, const glm::dvec3& from,
                                  const glm::dvec3& to, glm::dvec3& areaVector,
                                  double& squaredScale) {
    if (glm::length(to - from) == 0.0) {
        return failure(MeshError::DegenerateEdge, "面边的两个端点位置重合", face);
    }
    const auto a = from - origin;
    const auto b = to - origin;
    squaredScale = std::max(squaredScale, glm::dot(a, a));
    areaVector += glm::cross(a, b);
    return {};
}
MeshValidation validateFaceArea(FaceId face, const glm::dvec3& areaVector, double squaredScale) {
    if (glm::length(areaVector) <= squaredScale * kRelativeAreaEpsilon) {
        return failure(MeshError::DegenerateFace, "面面积为零或相对尺度过小", face);
    }
    return {};
}
} // namespace

MeshValidation validateFaceGeometry(const EditableFace& face,
                                    const std::vector<glm::dvec3>& positions) {
    if (face.corners.size() < 3 || positions.size() != face.corners.size()) {
        return failure(MeshError::FaceTooSmall, "面角坐标必须对应至少三个面角", face.id);
    }
    for (std::size_t i = 0; i < positions.size(); ++i) {
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(positions[i][axis])) {
                return failure(MeshError::NonFiniteAttribute, "顶点位置必须是有限数",
                               face.corners[i].vertex);
            }
        }
        const auto normal = validateCornerNormal(face.corners[i]);
        if (!normal.isValid()) {
            return normal;
        }
    }
    glm::dvec3 areaVector(0.0);
    double squaredScale = 0.0;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto edge =
            accumulateFaceEdge(face.id, positions.front(), positions[i],
                               positions[(i + 1) % positions.size()], areaVector, squaredScale);
        if (!edge.isValid()) {
            return edge;
        }
    }
    return validateFaceArea(face.id, areaVector, squaredScale);
}

MeshValidation validateEditableMesh(const EditableMesh& mesh) {
    std::unordered_map<VertexId, glm::dvec3> positions;
    positions.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        if (vertex.id == 0) {
            return failure(MeshError::InvalidId, "顶点 ID 不能为 0", vertex.id);
        }
        if (!positions.emplace(vertex.id, vertex.position).second) {
            return failure(MeshError::DuplicateId, "顶点 ID 重复", vertex.id);
        }
        if (!isFinite(vertex.position)) {
            return failure(MeshError::NonFiniteAttribute, "顶点位置必须是有限数", vertex.id);
        }
    }
    std::unordered_set<FaceId> faceIds;
    std::unordered_set<CornerId> cornerIds;
    std::unordered_map<EdgeKey, EdgeUse, EdgeHash> edges;
    std::unordered_map<VertexId, VertexFan> fans;
    std::vector<std::size_t> parents;
    std::size_t corners = 0;
    for (const auto& face : mesh.faces) {
        corners += face.corners.size();
    }
    parents.reserve(corners);
    faceIds.reserve(mesh.faces.size());
    cornerIds.reserve(corners);
    edges.reserve(corners);
    fans.reserve(mesh.vertices.size());
    std::unordered_set<VertexId> ring;
    const auto root = [&parents](std::size_t index) {
        while (parents[index] != index) {
            parents[index] = parents[parents[index]];
            index = parents[index];
        }
        return index;
    };
    const auto join = [&parents, &root, &fans](VertexId vertex, std::size_t a, std::size_t b) {
        const auto first = root(a);
        const auto second = root(b);
        if (first != second) {
            parents[first] = second;
            --fans.at(vertex).components;
        }
    };
    for (const auto& face : mesh.faces) {
        if (face.id == 0) {
            return failure(MeshError::InvalidId, "面 ID 不能为 0", face.id);
        }
        if (!faceIds.insert(face.id).second) {
            return failure(MeshError::DuplicateId, "面 ID 重复", face.id);
        }
        if (face.corners.size() < 3) {
            return failure(MeshError::FaceTooSmall, "面至少需要三个不同顶点", face.id);
        }
        ring.clear();
        const auto cornerBegin = parents.size();
        for (const auto& corner : face.corners) {
            if (corner.id == 0) {
                return failure(MeshError::InvalidId, "面角 ID 不能为 0", corner.id);
            }
            if (!cornerIds.insert(corner.id).second) {
                return failure(MeshError::DuplicateId, "面角 ID 重复", corner.id);
            }
            if (!positions.contains(corner.vertex)) {
                return failure(MeshError::MissingVertex, "面角引用的顶点不存在", corner.vertex);
            }
            if (!ring.insert(corner.vertex).second) {
                return failure(MeshError::RepeatedFaceVertex, "面环中不能重复使用顶点", face.id);
            }
            if (!std::isfinite(corner.uv.x) || !std::isfinite(corner.uv.y) ||
                !isFinite(corner.color)) {
                return failure(MeshError::NonFiniteAttribute, "面角 UV/颜色必须是有限数",
                               corner.id);
            }
            const auto normal = validateCornerNormal(corner);
            if (!normal.isValid()) {
                return normal;
            }
            parents.push_back(parents.size());
            ++fans[corner.vertex].components;
        }
        const auto origin = positions.at(face.corners.front().vertex);
        glm::dvec3 areaVector(0.0);
        double squaredScale = 0.0;
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            const auto from = face.corners[i].vertex;
            const auto to = face.corners[(i + 1) % face.corners.size()].vertex;
            const auto geometry = accumulateFaceEdge(face.id, origin, positions.at(from),
                                                     positions.at(to), areaVector, squaredScale);
            if (!geometry.isValid()) {
                return geometry;
            }
            const auto fromCorner = cornerBegin + i;
            const auto toCorner = cornerBegin + (i + 1) % face.corners.size();
            auto [edge, inserted] =
                edges.try_emplace(EdgeKey(from, to), EdgeUse{from, fromCorner, toCorner});
            if (!inserted) {
                if (edge->second.paired) {
                    return failure(MeshError::NonManifoldEdge, "同一条边不能邻接三个面", face.id);
                }
                if (edge->second.from == from) {
                    return failure(MeshError::InconsistentWinding, "相邻面共享边方向必须相反",
                                   face.id);
                }
                edge->second.paired = true;
                // 相反方向的共享边只连接同一顶点的面角，不依赖稳定 ID 的数值或顺序。
                join(to, edge->second.fromCorner, toCorner);
                join(from, edge->second.toCorner, fromCorner);
            }
        }
        const auto geometry = validateFaceArea(face.id, areaVector, squaredScale);
        if (!geometry.isValid()) {
            return geometry;
        }
    }
    std::size_t boundaryCount = 0;
    for (const auto& [edge, use] : edges) {
        if (!use.paired) {
            ++boundaryCount;
            ++fans.at(edge.first).boundaryEdges;
            ++fans.at(edge.second).boundaryEdges;
        }
    }
    for (const auto& [vertex, fan] : fans) {
        if (fan.boundaryEdges != 0 && fan.boundaryEdges != 2) {
            return failure(MeshError::NonManifoldVertex, "顶点边界不是单一连续扇区", vertex);
        }
        // 共享边连接的面必须组成单扇；仅边数合法不能排除两个闭壳共用一个点。
        if (fan.components != 1) {
            return failure(MeshError::NonManifoldVertex, "顶点连接了互不相连的面扇", vertex);
        }
    }
    return {MeshError::None, {}, edges.size(), boundaryCount};
}
} // namespace mini3d::core::modeling
