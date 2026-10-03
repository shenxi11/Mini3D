/*
 * 模块名: TopologySelection
 * 功能概述: 复用半边邻接确定 Loop/Ring 的唯一后继，并按源边遍历连通顶点。
 * 对外接口: TopologySelection.h
 * 依赖关系: MeshTopology、C++ 标准容器
 * 输入输出: 只读网格快照到稳定源元素集合，不跨断开部件或三角化对角线。
 * 异常与错误: 网格校验或源边查询失败返回原因；不发布部分无效选择。
 * 维护说明: 内点仅接受四价四边面扇，普通边界仅接受二/三价点，极点停止。
 */
#include "TopologySelection.h"

#include "MeshTopology.h"

namespace mini3d::core::modeling {
namespace {
std::optional<EdgeKey> nextLoopEdge(const MeshTopology& topology,
                                    const std::unordered_map<FaceId, std::size_t>& faceSizes,
                                    EdgeKey current, VertexId vertex) {
    std::set<EdgeKey> incident;
    for (const auto index : topology.vertexHalfEdges.at(vertex)) {
        const auto& half = topology.halfEdges[index];
        if (faceSizes.at(half.face) != 4) {
            return {};
        }
        incident.emplace(half.from, half.to);
        const auto& previous = topology.halfEdges[half.previous];
        incident.emplace(previous.from, previous.to);
    }
    const auto& half = topology.halfEdges[topology.edgeHalfEdges.at(current)];
    bool hasBoundary = false;
    std::optional<EdgeKey> boundaryNext;
    for (const auto edge : incident) {
        if (!topology.halfEdges[topology.edgeHalfEdges.at(edge)].twin) {
            hasBoundary = true;
            if (edge != current) {
                boundaryNext = edge;
            }
        }
    }
    if (hasBoundary) {
        // 合法单扇的边界恰有两条边；内边到边界停止，高价边界点不猜测方向。
        return incident.size() <= 3 && !half.twin ? boundaryNext : std::nullopt;
    }
    if (incident.size() != 4) {
        return {};
    }
    const auto sameFaceEdge = [&](const MeshHalfEdge& side) {
        const auto index = vertex == side.from ? side.previous : side.next;
        const auto& adjacent = topology.halfEdges[index];
        return EdgeKey(adjacent.from, adjacent.to);
    };
    const auto adjacentFirst = sameFaceEdge(half);
    const auto adjacentSecond = sameFaceEdge(topology.halfEdges[*half.twin]);
    std::optional<EdgeKey> next;
    for (const auto edge : incident) {
        if (edge != current && edge != adjacentFirst && edge != adjacentSecond) {
            if (next) {
                return {};
            }
            next = edge;
        }
    }
    return next;
}
} // namespace

EdgeSelectionResult selectEdgePath(const EditableMesh& mesh, EdgeKey seed, EdgeSelectionKind kind) {
    const auto built = buildMeshTopology(mesh);
    if (!built.topology) {
        return {{}, built.error};
    }
    const auto& topology = *built.topology;
    if (!topology.edgeHalfEdges.contains(seed)) {
        return {{}, "边选择需要有效源边，不能使用不存在的边或三角化对角线。"};
    }
    std::unordered_map<FaceId, std::size_t> faceSizes;
    for (const auto& face : mesh.faces) {
        faceSizes.emplace(face.id, face.corners.size());
    }
    std::set<EdgeKey> selected{seed};
    std::vector<EdgeKey> pending{seed};
    const auto append = [&](EdgeKey edge) {
        if (selected.insert(edge).second) {
            pending.push_back(edge);
        }
    };
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto edge = pending[index];
        if (kind == EdgeSelectionKind::Loop) {
            for (const auto vertex : {edge.first, edge.second}) {
                const auto next = nextLoopEdge(topology, faceSizes, edge, vertex);
                if (next) {
                    append(*next);
                }
            }
        } else {
            const auto first = topology.edgeHalfEdges.at(edge);
            std::vector<std::size_t> sides{first};
            if (topology.halfEdges[first].twin) {
                sides.push_back(*topology.halfEdges[first].twin);
            }
            for (const auto side : sides) {
                const auto& half = topology.halfEdges[side];
                if (faceSizes.at(half.face) == 4) {
                    const auto& opposite = topology.halfEdges[topology.halfEdges[half.next].next];
                    append({opposite.from, opposite.to});
                }
            }
        }
    }
    return {std::move(selected), {}};
}

std::set<VertexId> connectedVertices(const EditableMesh& mesh, VertexId seed) {
    const auto built = buildMeshTopology(mesh);
    if (!built.topology || !built.topology->vertexHalfEdges.contains(seed)) {
        return {};
    }
    const auto& topology = *built.topology;
    std::set<VertexId> selected{seed};
    std::vector<VertexId> pending{seed};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        for (const auto halfIndex : topology.vertexHalfEdges.at(pending[index])) {
            const auto& half = topology.halfEdges[halfIndex];
            for (const auto vertex : {half.to, topology.halfEdges[half.previous].from}) {
                if (selected.insert(vertex).second) {
                    pending.push_back(vertex);
                }
            }
        }
    }
    return selected;
}
} // namespace mini3d::core::modeling
