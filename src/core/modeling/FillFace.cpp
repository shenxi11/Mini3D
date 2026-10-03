/*
 * 模块名: FillFace
 * 功能概述: 沿邻面反向边界建立面环并校验共面与派生结果；对外接口: 见头文件。
 * 依赖关系: MeshTopology、MeshDerivation、GLM；输入输出: 源选择到独立候选。
 * 异常与错误: 非法输入不产生部分结果；维护说明: 复用派生层耳切，不检测全局相交。
 */
#include "FillFace.h"

#include "MeshDerivation.h"
#include "MeshTopology.h"

#include <glm/geometric.hpp>
#include <limits>

namespace mini3d::core::modeling {
FillFaceResult fillFaceFromVertices(const EditableMesh& before,
                                    const std::set<VertexId>& vertices) {
    if (vertices.size() < 3)
        return {std::nullopt, 0, "补面至少需要三个边界点。"};
    for (const auto id : vertices)
        if (!before.vertex(id))
            return {std::nullopt, 0, "待补面的顶点已不存在。"};
    const auto source = buildMeshTopology(before);
    if (!source.topology)
        return {std::nullopt, 0, source.error};
    std::set<EdgeKey> edges;
    std::set<VertexId> used;
    for (const auto& edge : source.topology->halfEdges) {
        if (!edge.twin && vertices.contains(edge.from) && vertices.contains(edge.to)) {
            edges.emplace(edge.from, edge.to);
            used.insert(edge.from);
            used.insert(edge.to);
        }
    }
    if (used != vertices)
        return {std::nullopt, 0, "所选点必须全部属于同一个闭合边界环，不能包含内部点或散点。"};
    return fillFaceFromEdges(before, edges);
}

FillFaceResult fillFaceFromEdges(const EditableMesh& before, const std::set<EdgeKey>& edges) {
    const auto source = buildMeshTopology(before);
    if (!source.topology)
        return {std::nullopt, 0, source.error};
    if (edges.size() < 3)
        return {std::nullopt, 0, "补面至少需要三条边界源边。"};
    std::map<VertexId, VertexId> next;
    std::set<VertexId> incoming;
    for (const auto edge : edges) {
        const auto found = source.topology->edgeHalfEdges.find(edge);
        if (found == source.topology->edgeHalfEdges.end())
            return {std::nullopt, 0, "选择包含不存在的源边。"};
        const auto& halfEdge = source.topology->halfEdges[found->second];
        if (halfEdge.twin)
            return {std::nullopt, 0, "补面只能使用边界边，不能使用已有两侧面的内部边。"};
        // 新面方向与邻面相反，不能仅根据点 ID 或点击顺序猜测绕序。
        if (!next.emplace(halfEdge.to, halfEdge.from).second ||
            !incoming.insert(halfEdge.from).second)
            return {std::nullopt, 0, "补面不支持分叉边界。"};
    }
    for (const auto& [from, to] : next) {
        if (!next.contains(to) || !incoming.contains(from))
            return {std::nullopt, 0, "所选边界未闭合，请完整选择一个环。"};
    }
    std::vector<VertexId> ring;
    const auto first = next.begin()->first;
    auto current = first;
    do {
        ring.push_back(current);
        current = next.at(current);
    } while (current != first);
    if (ring.size() != edges.size())
        return {std::nullopt, 0, "一次只能补一个边界环，不支持多环或带孔面。"};
    const std::set<VertexId> ringIds(ring.begin(), ring.end());
    for (const auto& face : before.faces) {
        std::set<VertexId> faceIds;
        for (const auto& corner : face.corners)
            faceIds.insert(corner.vertex);
        if (faceIds == ringIds)
            return {std::nullopt, 0, "该边界已经有面，不能创建重叠反向面。"};
    }
    const glm::dvec3 origin(before.vertex(first)->position);
    std::vector<glm::dvec3> points;
    double scale = 0;
    for (const auto id : ring) {
        points.push_back(glm::dvec3(before.vertex(id)->position) - origin);
        scale = std::max(scale, glm::length(points.back()));
    }
    if (scale == 0)
        return {std::nullopt, 0, "补面边界已退化。"};
    for (auto& point : points)
        point /= scale;
    glm::dvec3 normal(0);
    for (std::size_t i = 0; i < points.size(); ++i)
        normal += glm::cross(points[i], points[(i + 1) % points.size()]);
    if (glm::length(normal) <= 1.0e-12)
        return {std::nullopt, 0, "补面边界面积为零或发生自交。"};
    normal = glm::normalize(normal);
    for (const auto& point : points)
        if (std::abs(glm::dot(point, normal)) > 1.0e-6)
            return {std::nullopt, 0, "补面仅支持共面边界；请先把边界点对齐到同一平面。"};
    FaceId faceId = 0;
    CornerId cornerId = 0;
    for (const auto& face : before.faces) {
        faceId = std::max(faceId, face.id);
        for (const auto& corner : face.corners)
            cornerId = std::max(cornerId, corner.id);
    }
    if (faceId == std::numeric_limits<FaceId>::max() ||
        ring.size() > std::numeric_limits<CornerId>::max() - cornerId)
        return {std::nullopt, 0, "补面需要的新面或面角 ID 已耗尽。"};
    const auto axisU = glm::normalize(points[1]);
    const auto axisV = glm::cross(normal, axisU);
    EditableFace face;
    face.id = ++faceId;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const glm::vec2 uv(glm::dot(points[i], axisU), glm::dot(points[i], axisV));
        face.corners.push_back({++cornerId, ring[i], uv, glm::vec3(normal), glm::vec3(1)});
    }
    auto candidate = before;
    candidate.faces.push_back(std::move(face));
    const auto derived = deriveMesh(candidate);
    if (!derived.derived)
        return {std::nullopt, 0, "无法补面：" + derived.error};
    return {std::move(candidate), faceId, {}};
}
} // namespace mini3d::core::modeling
