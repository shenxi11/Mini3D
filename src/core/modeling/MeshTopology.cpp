/*
 * 模块名: MeshTopology
 * 功能概述: 将每个面角映射为有向半边，按稳定端点 ID 配对相邻面。
 * 对外接口: buildMeshTopology
 * 依赖关系: MeshValidation、MeshTopology
 * 输入输出: 只读网格到下一环/上一环/对向半边及源元素索引。
 * 异常与错误: 校验失败返回原因；分配失败传播，源网格不受影响。
 * 维护说明: 不按坐标焊接，不创建可编辑的三角化内部边，不接触 Qt/GL。
 */
#include "MeshTopology.h"

namespace mini3d::core::modeling {
MeshTopologyResult buildMeshTopology(const EditableMesh& mesh) {
    const auto validation = validateEditableMesh(mesh);
    if (!validation.isValid()) {
        return {{}, validation.message};
    }
    MeshTopology topology;
    for (const auto& vertex : mesh.vertices) {
        topology.vertexHalfEdges.emplace(vertex.id, std::vector<std::size_t>{});
    }
    for (const auto& face : mesh.faces) {
        const auto first = topology.halfEdges.size();
        const auto count = face.corners.size();
        topology.faceHalfEdges.emplace(face.id, first);
        for (std::size_t i = 0; i < count; ++i) {
            const auto index = first + i;
            const auto& corner = face.corners[i];
            const auto to = face.corners[(i + 1) % count].vertex;
            topology.halfEdges.push_back({corner.vertex,
                                          to,
                                          face.id,
                                          corner.id,
                                          first + (i + 1) % count,
                                          first + (i + count - 1) % count,
                                          {}});
            topology.vertexHalfEdges.at(corner.vertex).push_back(index);
            const auto [edge, inserted] =
                topology.edgeHalfEdges.emplace(EdgeKey(corner.vertex, to), index);
            if (!inserted) {
                topology.halfEdges[index].twin = edge->second;
                topology.halfEdges[edge->second].twin = index;
            }
        }
    }
    return {std::move(topology), {}};
}
} // namespace mini3d::core::modeling
