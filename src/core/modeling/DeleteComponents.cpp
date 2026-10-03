/*
 * 模块名: DeleteComponents
 * 功能概述: 校验选择并按关联面裁剪源网格；对外接口: 见同名头文件。
 * 依赖关系: MeshTopology、MeshDerivation；输入输出: before 到合法候选。
 * 异常与错误: 删除导致多扇等非法拓扑时整次拒绝；分配异常传播。
 * 维护说明: 未删除面角的身份与属性保持不变，不自动拆点或补洞。
 */
#include "DeleteComponents.h"

#include "MeshDerivation.h"
#include "MeshTopology.h"

namespace mini3d::core::modeling {
namespace {
DeleteComponentsResult removeFaces(const EditableMesh& before, const std::set<FaceId>& faces) {
    auto candidate = before;
    std::erase_if(candidate.faces, [&faces](const auto& face) {
        return faces.contains(face.id);
    });
    std::set<VertexId> used;
    for (const auto& face : candidate.faces)
        for (const auto& corner : face.corners)
            used.insert(corner.vertex);
    std::erase_if(candidate.vertices, [&used](const auto& vertex) {
        return !used.contains(vertex.id);
    });
    const auto derived = deriveMesh(candidate);
    if (!derived.derived)
        return {std::nullopt, "删除会产生不支持的拓扑：" + derived.error};
    return {std::move(candidate), {}};
}
} // namespace

DeleteComponentsResult deleteVertices(const EditableMesh& before,
                                      const std::set<VertexId>& vertices) {
    const auto source = deriveMesh(before);
    if (!source.derived)
        return {std::nullopt, source.error};
    if (vertices.empty())
        return {std::nullopt, "请先选择需要删除的点。"};
    for (const auto id : vertices)
        if (!before.vertex(id))
            return {std::nullopt, "待删除顶点已不存在。"};
    std::set<FaceId> faces;
    for (const auto& face : before.faces)
        for (const auto& corner : face.corners)
            if (vertices.contains(corner.vertex))
                faces.insert(face.id);
    return removeFaces(before, faces);
}

DeleteComponentsResult deleteEdges(const EditableMesh& before, const std::set<EdgeKey>& edges) {
    const auto source = buildMeshTopology(before);
    if (!source.topology)
        return {std::nullopt, source.error};
    if (edges.empty())
        return {std::nullopt, "请先选择需要删除的源边。"};
    std::set<FaceId> faces;
    for (const auto edge : edges) {
        const auto found = source.topology->edgeHalfEdges.find(edge);
        if (found == source.topology->edgeHalfEdges.end())
            return {std::nullopt, "待删除源边已不存在；三角化对角线不是源边。"};
        const auto& halfEdge = source.topology->halfEdges[found->second];
        faces.insert(halfEdge.face);
        if (halfEdge.twin)
            faces.insert(source.topology->halfEdges[*halfEdge.twin].face);
    }
    return deleteFaces(before, faces);
}

DeleteComponentsResult deleteFaces(const EditableMesh& before, const std::set<FaceId>& faces) {
    const auto source = deriveMesh(before);
    if (!source.derived)
        return {std::nullopt, source.error};
    if (faces.empty())
        return {std::nullopt, "请先选择需要删除的面。"};
    std::set<FaceId> available;
    for (const auto& face : before.faces)
        available.insert(face.id);
    for (const auto id : faces)
        if (!available.contains(id))
            return {std::nullopt, "待删除面已不存在。"};
    return removeFaces(before, faces);
}
} // namespace mini3d::core::modeling
