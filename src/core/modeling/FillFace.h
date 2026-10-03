/*
 * 模块名: FillFace
 * 功能概述: 从所选边界点或边界源边补一个简单共面面，不处理孔与散点连面。
 * 对外接口: fillFaceFromVertices/fillFaceFromEdges；依赖关系: EditableMesh，无 Qt/GL。
 * 输入输出: before 和无序稳定 ID 集合到候选、新面 ID 或中文拒绝原因。
 * 异常与错误: 多环、非共面、自交、已有重叠面和 ID 耗尽拒绝；分配异常传播。
 * 维护说明: 邻面绕序决定新面方向；原属性不变，新面用默认材质与平面 UV。
 */
#pragma once

#include "EditableMesh.h"

#include <set>
#include <string>

namespace mini3d::core::modeling {
struct FillFaceResult {
    std::optional<EditableMesh> mesh;
    FaceId face = 0;
    std::string error;
};
/** @brief 所选点必须恰好构成单个边界环；不按点击顺序连接散点，输入不变。 */
[[nodiscard]] FillFaceResult fillFaceFromVertices(const EditableMesh& before,
                                                  const std::set<VertexId>& vertices);
/** @brief 所选源边必须恰好构成单个边界环；成功返回新 FaceId，输入不变。 */
[[nodiscard]] FillFaceResult fillFaceFromEdges(const EditableMesh& before,
                                               const std::set<EdgeKey>& edges);
} // namespace mini3d::core::modeling
