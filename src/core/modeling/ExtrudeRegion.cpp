/*
 * 模块名: ExtrudeRegion
 * 功能概述: 以区域有向边界生成侧壁并校验完整候选，保持稳定 ID 与面角属性。
 * 对外接口: ExtrudeRegion.h；依赖关系: MeshTopology、MeshDerivation。
 * 输入输出: 连通面选择与绝对位移到新顶盖、侧壁和清理后的原始顶点。
 * 异常与错误: 非法输入返回空结果；不发布半成品，不捕获分配失败。
 * 维护说明: 仅清理由本次顶盖替换留下的孤立旧点，保留原有独立点与其他面。
 */
#include "ExtrudeRegion.h"

#include "MeshDerivation.h"
#include "MeshTopology.h"

#include <cmath>
#include <limits>
#include <unordered_map>

namespace mini3d::core::modeling {
namespace {
struct Boundary {
    MeshCorner from, to;
    AssetId material;
};
struct Region {
    ExtrudeRegionInfo info;
    std::set<VertexId> vertices;
    std::vector<Boundary> boundary;
};
struct RegionResult {
    std::optional<Region> region;
    std::string error;
};
RegionResult inspectRegion(const EditableMesh& mesh, const std::set<FaceId>& faces) {
    if (faces.empty())
        return {{}, "请先选择要挤出的面区域。"};
    const auto topologyResult = buildMeshTopology(mesh);
    if (!topologyResult.topology)
        return {{}, topologyResult.error};
    const auto& topology = *topologyResult.topology;
    for (auto id : faces) {
        if (!topology.faceHalfEdges.contains(id))
            return {{}, "挤出选择包含失效的面 ID。"};
    }
    std::set<FaceId> visited;
    std::vector<FaceId> pending{*faces.begin()};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!visited.insert(id).second)
            continue;
        const auto first = topology.faceHalfEdges.at(id);
        auto edge = first;
        do {
            const auto& halfEdge = topology.halfEdges[edge];
            if (halfEdge.twin) {
                const auto adjacent = topology.halfEdges[*halfEdge.twin].face;
                if (faces.contains(adjacent) && !visited.contains(adjacent))
                    pending.push_back(adjacent);
            }
            edge = halfEdge.next;
        } while (edge != first);
    }
    if (visited != faces)
        return {{}, "区域挤出要求通过共享边连通的面选择。"};
    Region region;
    std::unordered_map<VertexId, glm::dvec3> positions;
    for (const auto& vertex : mesh.vertices)
        positions.emplace(vertex.id, vertex.position);
    std::map<VertexId, int> outgoing, incoming;
    glm::dvec3 summedNormal{0}, fallback{0};
    double summedArea = 0;
    for (const auto& face : mesh.faces) {
        if (!faces.contains(face.id))
            continue;
        auto edge = topology.faceHalfEdges.at(face.id);
        const auto origin = positions.at(face.corners.front().vertex);
        glm::dvec3 area{0};
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            const auto& from = face.corners[i];
            const auto& to = face.corners[(i + 1) % face.corners.size()];
            region.vertices.insert(from.vertex);
            area +=
                glm::cross(positions.at(from.vertex) - origin, positions.at(to.vertex) - origin);
            const auto& halfEdge = topology.halfEdges[edge];
            if (!halfEdge.twin || !faces.contains(topology.halfEdges[*halfEdge.twin].face)) {
                region.boundary.push_back({from, to, face.material});
                ++outgoing[from.vertex];
                ++incoming[to.vertex];
            }
            edge = halfEdge.next;
        }
        summedNormal += area;
        summedArea += glm::length(area);
        if (face.id == *faces.begin())
            fallback = area;
    }
    if (region.boundary.empty())
        return {{}, "选中面域没有边界，不能对整个闭壳执行区域挤出。"};
    for (const auto& [vertex, count] : outgoing) {
        if (count != 1 || incoming[vertex] != 1)
            return {{}, "选中区域边界在顶点处分叉或接触，请调整面选择。"};
    }
    region.info.usesFallbackNormal = glm::length(summedNormal) <= summedArea * 1.0e-10;
    region.info.normal = glm::normalize(region.info.usesFallbackNormal ? fallback : summedNormal);
    region.info.boundaryEdgeCount = region.boundary.size();
    return {std::move(region), {}};
}
} // namespace

ExtrudeRegionAnalysis analyzeExtrudeRegion(const EditableMesh& mesh,
                                           const std::set<FaceId>& faces) {
    const auto result = inspectRegion(mesh, faces);
    if (!result.region)
        return {{}, result.error};
    return {result.region->info, {}};
}

ExtrudeRegionResult extrudeRegion(const EditableMesh& before, const std::set<FaceId>& faces,
                                  const glm::dvec3& localOffset) {
    const auto inspected = inspectRegion(before, faces);
    if (!inspected.region)
        return {{}, inspected.error};
    if (!std::isfinite(localOffset.x) || !std::isfinite(localOffset.y) ||
        !std::isfinite(localOffset.z) || localOffset == glm::dvec3(0))
        return {{}, "挤出位移必须有限且不为零；安全取消不保留零位移拓扑。"};
    const auto& region = *inspected.region;
    VertexId lastVertex = 0;
    FaceId lastFace = 0;
    CornerId lastCorner = 0;
    for (const auto& vertex : before.vertices)
        lastVertex = std::max(lastVertex, vertex.id);
    for (const auto& face : before.faces) {
        lastFace = std::max(lastFace, face.id);
        for (const auto& corner : face.corners)
            lastCorner = std::max(lastCorner, corner.id);
    }
    constexpr auto lastId = std::numeric_limits<std::uint64_t>::max();
    if (region.vertices.size() > lastId - lastVertex ||
        region.boundary.size() > lastId - lastFace ||
        region.boundary.size() > (lastId - lastCorner) / 4)
        return {{}, "网格元素 ID 空间不足，无法生成挤出拓扑。"};
    auto result = before;
    std::map<VertexId, VertexId> topVertices;
    const double distance = glm::length(localOffset);
    if (!std::isfinite(distance) || distance > std::numeric_limits<float>::max())
        return {{}, "挤出距离超出可用范围。"};
    for (const auto& vertex : before.vertices) {
        if (!region.vertices.contains(vertex.id))
            continue;
        const auto position = glm::dvec3(vertex.position) + localOffset;
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(position[axis]) ||
                std::abs(position[axis]) > std::numeric_limits<float>::max())
                return {{}, "挤出顶点坐标超出可用范围。"};
        }
        const auto next = ++lastVertex;
        topVertices.emplace(vertex.id, next);
        result.vertices.push_back({next, glm::vec3(position)});
    }
    for (auto& face : result.faces) {
        if (!faces.contains(face.id))
            continue;
        for (auto& corner : face.corners)
            corner.vertex = topVertices.at(corner.vertex);
    }
    for (const auto& boundary : region.boundary) {
        const auto length = glm::length(glm::dvec3(before.vertex(boundary.to.vertex)->position) -
                                        glm::dvec3(before.vertex(boundary.from.vertex)->position));
        if (length > std::numeric_limits<float>::max())
            return {{}, "侧壁 UV 尺度超出可用范围。"};
        const float u = static_cast<float>(length), v = static_cast<float>(distance);
        EditableFace side;
        side.id = ++lastFace;
        side.material = boundary.material;
        side.corners = {
            {++lastCorner, boundary.from.vertex, {0, 0}, {}, boundary.from.color},
            {++lastCorner, boundary.to.vertex, {u, 0}, {}, boundary.to.color},
            {++lastCorner, topVertices.at(boundary.to.vertex), {u, v}, {}, boundary.to.color},
            {++lastCorner, topVertices.at(boundary.from.vertex), {0, v}, {}, boundary.from.color}};
        result.faces.push_back(std::move(side));
    }
    std::set<VertexId> referenced;
    for (const auto& face : result.faces) {
        for (const auto& corner : face.corners)
            referenced.insert(corner.vertex);
    }
    std::erase_if(result.vertices, [&](const auto& vertex) {
        return region.vertices.contains(vertex.id) && !referenced.contains(vertex.id);
    });
    const auto validated = deriveMesh(result);
    if (!validated.derived)
        return {{}, "挤出候选无效：" + validated.error};
    return {std::move(result), {}};
}
} // namespace mini3d::core::modeling
