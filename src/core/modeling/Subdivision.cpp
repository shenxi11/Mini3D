/*
 * 模块名: Subdivision
 * 功能概述: 计算面点、边点和顶点点，保持面环方向并逐级合成原始源面映射。
 * 对外接口: evaluateSubdivision
 * 依赖关系: MeshTopology、MeshValidation、MeshDerivation、GLM
 * 输入输出: 合法四边源快照到一到两级 Catmull-Clark 候选与平滑法线。
 * 异常与错误: 输入或候选不合法、ID 耗尽或平滑法线抵消时返回失败，源保持不变。
 * 维护说明: 每级只建一次 ID 位置表；角属性不按位置焊接，最终只生成一次渲染派生。
 */
#include "Subdivision.h"

#include "MeshTopology.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <unordered_map>
#include <utility>

namespace mini3d::core::modeling {
namespace {
struct SubdivisionStep {
    EditableMesh mesh;
    std::map<FaceId, FaceId> faces;
};

struct VertexNeighborhood {
    glm::dvec3 faceSum{0.0};
    glm::dvec3 midpointSum{0.0};
    std::size_t edgeCount = 0;
    std::vector<VertexId> boundaryNeighbors;
};

std::optional<SubdivisionStep> subdivideLevel(const EditableMesh& source,
                                              const std::map<FaceId, FaceId>& origins,
                                              std::string& error) {
    const auto built = buildMeshTopology(source);
    if (!built.topology) {
        error = built.error;
        return {};
    }
    if (source.faces.empty()) {
        error = "细分需要至少一个四边面。";
        return {};
    }
    const auto& topology = *built.topology;
    std::map<VertexId, glm::dvec3> positions;
    std::map<FaceId, const EditableFace*> faces;
    std::map<FaceId, glm::dvec3> facePoints;
    std::unordered_map<VertexId, VertexNeighborhood> neighborhoods;
    VertexId lastVertex = 0;
    FaceId lastFace = 0;
    CornerId lastCorner = 0;
    for (const auto& vertex : source.vertices) {
        if (topology.vertexHalfEdges.at(vertex.id).empty()) {
            error = "细分不支持孤立顶点（ID " + std::to_string(vertex.id) + "）。";
            return {};
        }
        positions.emplace(vertex.id, vertex.position);
        neighborhoods.emplace(vertex.id, VertexNeighborhood{});
        lastVertex = std::max(lastVertex, vertex.id);
    }
    for (const auto& face : source.faces) {
        if (face.corners.size() != 4) {
            error = "细分仅支持四边面（面 ID " + std::to_string(face.id) + "）。";
            return {};
        }
        faces.emplace(face.id, &face);
        lastFace = std::max(lastFace, face.id);
        for (const auto& corner : face.corners) {
            lastCorner = std::max(lastCorner, corner.id);
        }
    }
    constexpr auto maxId = std::numeric_limits<std::uint64_t>::max();
    const auto edgeCount = topology.edgeHalfEdges.size();
    const auto faceCount = source.faces.size();
    if (edgeCount > maxId - lastVertex || faceCount > maxId - lastVertex - edgeCount ||
        faceCount > (maxId - lastFace) / 4 || faceCount > (maxId - lastCorner) / 16) {
        error = "网格元素 ID 空间不足，无法生成细分。";
        return {};
    }
    for (const auto& [id, face] : faces) {
        glm::dvec3 point(0.0);
        for (const auto& corner : face->corners) {
            point += positions.at(corner.vertex);
        }
        point /= 4.0;
        facePoints.emplace(id, point);
        for (const auto& corner : face->corners) {
            neighborhoods.at(corner.vertex).faceSum += point;
        }
    }

    SubdivisionStep result;
    result.mesh.vertices.reserve(source.vertices.size() + edgeCount + faceCount);
    result.mesh.faces.reserve(faceCount * 4);
    for (const auto& [id, point] : positions) {
        result.mesh.vertices.push_back({id, glm::vec3(point)});
    }
    std::map<EdgeKey, VertexId> edgePointIds;
    for (const auto& [edge, index] : topology.edgeHalfEdges) {
        const auto& half = topology.halfEdges[index];
        const auto midpoint = (positions.at(edge.first) + positions.at(edge.second)) / 2.0;
        glm::dvec3 point = midpoint;
        if (half.twin) {
            point =
                (positions.at(edge.first) + positions.at(edge.second) + facePoints.at(half.face) +
                 facePoints.at(topology.halfEdges[*half.twin].face)) /
                4.0;
        }
        edgePointIds.emplace(edge, ++lastVertex);
        result.mesh.vertices.push_back({lastVertex, glm::vec3(point)});
        for (const auto vertex : {edge.first, edge.second}) {
            auto& neighborhood = neighborhoods.at(vertex);
            neighborhood.midpointSum += midpoint;
            ++neighborhood.edgeCount;
            if (!half.twin) {
                neighborhood.boundaryNeighbors.push_back(vertex == edge.first ? edge.second
                                                                              : edge.first);
            }
        }
    }
    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
        auto& vertex = result.mesh.vertices[i];
        const auto& neighborhood = neighborhoods.at(vertex.id);
        const auto& point = positions.at(vertex.id);
        if (!neighborhood.boundaryNeighbors.empty()) {
            if (neighborhood.boundaryNeighbors.size() != 2) {
                error = "细分边界顶点必须恰好连接两条边界边。";
                return {};
            }
            vertex.position =
                glm::vec3((6.0 * point + positions.at(neighborhood.boundaryNeighbors[0]) +
                           positions.at(neighborhood.boundaryNeighbors[1])) /
                          8.0);
        } else {
            const auto count = static_cast<double>(neighborhood.edgeCount);
            vertex.position =
                glm::vec3((neighborhood.faceSum / count + 2.0 * neighborhood.midpointSum / count +
                           (count - 3.0) * point) /
                          count);
        }
    }
    std::map<FaceId, VertexId> facePointIds;
    for (const auto& [id, point] : facePoints) {
        facePointIds.emplace(id, ++lastVertex);
        result.mesh.vertices.push_back({lastVertex, glm::vec3(point)});
    }
    // 每个源角对应一个子面，所有角身份重分配；几何共享点不影响逐面属性接缝。
    for (const auto& [id, face] : faces) {
        glm::dvec2 faceUv(0.0);
        glm::dvec3 faceColor(0.0);
        for (const auto& corner : face->corners) {
            faceUv += glm::dvec2(corner.uv) / 4.0;
            faceColor += glm::dvec3(corner.color) / 4.0;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& corner = face->corners[i];
            const auto& next = face->corners[(i + 1) % 4];
            const auto& previous = face->corners[(i + 3) % 4];
            EditableFace child;
            child.id = ++lastFace;
            child.material = face->material;
            child.corners = {
                {++lastCorner, corner.vertex, corner.uv, {}, corner.color},
                {++lastCorner,
                 edgePointIds.at(EdgeKey(corner.vertex, next.vertex)),
                 glm::vec2((glm::dvec2(corner.uv) + glm::dvec2(next.uv)) / 2.0),
                 {},
                 glm::vec3((glm::dvec3(corner.color) + glm::dvec3(next.color)) / 2.0)},
                {++lastCorner, facePointIds.at(id), glm::vec2(faceUv), {}, glm::vec3(faceColor)},
                {++lastCorner,
                 edgePointIds.at(EdgeKey(previous.vertex, corner.vertex)),
                 glm::vec2((glm::dvec2(previous.uv) + glm::dvec2(corner.uv)) / 2.0),
                 {},
                 glm::vec3((glm::dvec3(previous.color) + glm::dvec3(corner.color)) / 2.0)}};
            result.faces.emplace(child.id, origins.at(id));
            result.mesh.faces.push_back(std::move(child));
        }
    }
    return result;
}

bool setSmoothNormals(EditableMesh& mesh, std::string& error) {
    std::unordered_map<VertexId, glm::dvec3> positions;
    std::unordered_map<VertexId, glm::dvec3> normals;
    for (const auto& vertex : mesh.vertices) {
        positions.emplace(vertex.id, vertex.position);
        normals.emplace(vertex.id, glm::dvec3(0.0));
    }
    for (const auto& face : mesh.faces) {
        const auto origin = positions.at(face.corners.front().vertex);
        glm::dvec3 areaVector(0.0);
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            areaVector += glm::cross(positions.at(face.corners[i].vertex) - origin,
                                     positions.at(face.corners[(i + 1) % 4].vertex) - origin);
        }
        for (const auto& corner : face.corners) {
            normals.at(corner.vertex) += areaVector;
        }
    }
    for (auto& [id, normal] : normals) {
        const auto length = glm::length(normal);
        if (length == 0.0 || !std::isfinite(length)) {
            error = "细分求值顶点无法形成有效平滑法线（ID " + std::to_string(id) + "）。";
            return false;
        }
        normal /= length;
    }
    for (auto& face : mesh.faces) {
        for (auto& corner : face.corners) {
            corner.normal = glm::vec3(normals.at(corner.vertex));
        }
    }
    return true;
}
} // namespace

SubdivisionResult evaluateSubdivision(const EditableMesh& source, int levels) {
    if (levels != 1 && levels != 2) {
        return {{}, "细分级数仅支持 1 或 2。"};
    }
    SubdivisionEvaluation result;
    result.mesh = source;
    for (const auto& face : source.faces) {
        result.faces.emplace(face.id, face.id);
    }
    std::string error;
    for (int level = 0; level < levels; ++level) {
        auto step = subdivideLevel(result.mesh, result.faces, error);
        if (!step) {
            return {{}, error};
        }
        result.mesh = std::move(step->mesh);
        result.faces = std::move(step->faces);
    }
    const auto validation = validateEditableMesh(result.mesh);
    if (!validation.isValid()) {
        return {{}, "细分求值无效：" + validation.message};
    }
    if (!setSmoothNormals(result.mesh, error)) {
        return {{}, error};
    }
    auto derived = deriveMesh(result.mesh);
    if (!derived.derived) {
        return {{}, "细分求值无效：" + derived.error};
    }
    result.derived = std::move(*derived.derived);
    return {std::move(result), {}};
}
} // namespace mini3d::core::modeling
