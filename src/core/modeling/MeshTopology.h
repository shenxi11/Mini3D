/*
 * 模块名: MeshTopology
 * 功能概述: 从合法多边形快照重建半边、顶点关联、面环和无向源边索引。
 * 对外接口: MeshHalfEdge、MeshTopology、buildMeshTopology
 * 依赖关系: EditableMesh、标准关联容器
 * 输入输出: 稳定元素 ID 到本次构建的邻接；不持久化半边下标。
 * 异常与错误: 非法快照返回空结果与原因，分配异常传播且输入不变。
 * 维护说明: 边界半边没有 twin；这些边来自面环，不包含三角化对角线。
 */
#pragma once

#include "MeshValidation.h"

#include <map>
#include <unordered_map>

namespace mini3d::core::modeling {
/** @brief 有面一侧的有向边；next/previous/twin 仅在当前 MeshTopology 内有效。 */
struct MeshHalfEdge {
    VertexId from;
    VertexId to;
    FaceId face;
    CornerId corner;
    std::size_t next;
    std::size_t previous;
    std::optional<std::size_t> twin;
};

/** @brief 可重建邻接，不拥有或修改源网格；源快照修改后必须重建。 */
struct MeshTopology {
    std::vector<MeshHalfEdge> halfEdges;
    std::map<EdgeKey, std::size_t> edgeHalfEdges;
    std::unordered_map<VertexId, std::vector<std::size_t>> vertexHalfEdges;
    std::unordered_map<FaceId, std::size_t> faceHalfEdges;
};

struct MeshTopologyResult {
    std::optional<MeshTopology> topology;
    std::string error;
};

/** @brief 完整校验后离线建立邻接，失败不返回部分结果，也不修改候选。 */
[[nodiscard]] MeshTopologyResult buildMeshTopology(const EditableMesh& mesh);
} // namespace mini3d::core::modeling
