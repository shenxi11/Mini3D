/*
 * 模块名: TopologySelection
 * 功能概述: 只读选择源网格的边循环、边环和连通顶点，不依据渲染拆点或位置猜测路径。
 * 对外接口: EdgeSelectionKind、EdgeSelectionResult、selectEdgePath、connectedVertices
 * 依赖关系: EditableMesh、C++ 标准容器
 * 输入输出: 合法网格与稳定元素 ID 到确定的源边或顶点集合。
 * 异常与错误: 边选择失败返回原因且无部分结果；非法连通选择返回空集，分配异常向上传播。
 * 维护说明: 不修改网格，不接触 Scene/Qt/GL，不生成新元素身份。
 */
#pragma once

#include "EditableMesh.h"

#include <optional>
#include <set>
#include <string>

namespace mini3d::core::modeling {
/** @brief Loop 沿顶点的唯一拓扑对边继续；Ring 跨四边面的对边继续。 */
enum class EdgeSelectionKind { Loop, Ring };

/** @brief 成功集合包含种子；失败时 edges 为空且 error 给出原因。 */
struct EdgeSelectionResult {
    std::optional<std::set<EdgeKey>> edges;
    std::string error;
};

/**
 * @brief 从源边双向追踪；极点、非四边面和无法继续的边界是正常终点。
 * @param mesh 只读源网格，完整校验失败时不返回部分集合。
 * @param seed 必须是实际源边，三角化内部边无效。
 * @param kind 选择 Loop 或 Ring；Loop 在普通边界沿边界环继续，内边到边界停止。
 * @return 稳定源边集合或失败原因；不改变输入。
 */
[[nodiscard]] EdgeSelectionResult selectEdgePath(const EditableMesh& mesh, EdgeKey seed,
                                                 EdgeSelectionKind kind);

/** @brief 按源边返回 seed 所在的连通顶点；孤立点保留自己，非法网格/种子返回空集。 */
[[nodiscard]] std::set<VertexId> connectedVertices(const EditableMesh& mesh, VertexId seed);
} // namespace mini3d::core::modeling
