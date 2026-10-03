/*
 * 模块名: DeleteComponents
 * 功能概述: 按点、源边或面域删除组件及关联面，清理孤立顶点。
 * 对外接口: deleteVertices/deleteEdges/deleteFaces；依赖关系: EditableMesh，无 Qt/GL。
 * 输入输出: 源快照和稳定 ID 集合到独立候选或中文拒绝原因。
 * 异常与错误: 无效选择、非法源或非流形结果原子拒绝，分配异常传播。
 * 维护说明: 不是溶解；无独立散边，不提供仅删除面且保留散边的选项。
 */
#pragma once

#include "EditableMesh.h"

#include <set>
#include <string>

namespace mini3d::core::modeling {
struct DeleteComponentsResult {
    std::optional<EditableMesh> mesh;
    std::string error;
};
/** @brief 删除指定点及关联面；清理全部无面引用点，输入不变。 */
[[nodiscard]] DeleteComponentsResult deleteVertices(const EditableMesh& before,
                                                    const std::set<VertexId>& vertices);
/** @brief 删除指定源边的相邻面；不执行溶解或重新连面，输入不变。 */
[[nodiscard]] DeleteComponentsResult deleteEdges(const EditableMesh& before,
                                                 const std::set<EdgeKey>& edges);
/** @brief 删除指定面并清理孤立点；其他面使用的边界保留，允许结果为空。 */
[[nodiscard]] DeleteComponentsResult deleteFaces(const EditableMesh& before,
                                                 const std::set<FaceId>& faces);
} // namespace mini3d::core::modeling
