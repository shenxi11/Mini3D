/*
 * 模块名: BevelEdge
 * 功能概述: 对单个外凸流形边作单段等距倒角，并闭合两个三价端部。
 * 对外接口: analyzeBevelEdge、bevelEdge；依赖关系: EditableMesh，无Qt/GL。
 * 输入输出: 只读源快照、稳定边键与局部宽度到独立候选及倒角面ID。
 * 异常与错误: 凹边、复杂端角、非共面凸环、超宽或精度坍缩明确拒绝。
 * 维护说明: 不支持多边同时倒角或多段圆角，不检测全局自交。
 */
#pragma once

#include "EditableMesh.h"

#include <string>

namespace mini3d::core::modeling {
/** @brief 已支持边及局部垂距宽度上限；上限值本身不可提交。 */
struct BevelEdgeInfo {
    EdgeKey edge;
    double maximumWidth = 0;
};
/** @brief 分析不修改网格；不支持的边只返回明确原因。 */
struct BevelEdgeAnalysis {
    std::optional<BevelEdgeInfo> bevel;
    std::string error;
};
/** @brief 成功返回完整候选和新倒角面身份；失败不返回部分网格。 */
struct BevelEdgeResult {
    std::optional<EditableMesh> mesh;
    FaceId bevelFace = 0;
    std::string error;
};
/** @brief 校验两个共面凸邻面、两个独立共面凸端面及三价流形端角，返回等距上限。 */
[[nodiscard]] BevelEdgeAnalysis analyzeBevelEdge(const EditableMesh& mesh, EdgeKey edge);
/** @brief 从before离线构造单段闭合倒角；width须严格大于零且小于分析上限。 */
[[nodiscard]] BevelEdgeResult bevelEdge(const EditableMesh& before, EdgeKey edge, double width);
} // namespace mini3d::core::modeling
