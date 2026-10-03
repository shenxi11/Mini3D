/*
 * 模块名: LoopCut
 * 功能概述: 查找无分叉四边形面带，并从原快照生成单条环切及滑移候选。
 * 对外接口: analyzeLoopCut、loopCut；依赖关系: EditableMesh、标准容器，无Qt/GL。
 * 输入输出: 源边和滑移比例到独立网格及新切线边集合。
 * 异常与错误: 非四边面、高价极点、交叉面带、退化或ID耗尽拒绝。
 * 维护说明: 0为中点，范围严格(-1,1)；沿边插值，不合并端点、不留T接。
 */
#pragma once

#include "EditableMesh.h"

#include <map>
#include <set>
#include <string>

namespace mini3d::core::modeling {
/** @brief 每条被切源边的比例起点沿面带一致传递；容器顺序不作为元素身份。 */
struct LoopCutInfo {
    std::map<EdgeKey, VertexId> edgeStarts;
    std::set<FaceId> faces;
    bool closed = false;
};
struct LoopCutAnalysis {
    std::optional<LoopCutInfo> band;
    std::string error;
};
struct LoopCutResult {
    std::optional<EditableMesh> mesh;
    std::set<EdgeKey> cutEdges;
    std::string error;
};
/** @brief 从源边两侧沿四边面对边追踪；只在真边界终止，非法带整次拒绝。 */
[[nodiscard]] LoopCutAnalysis analyzeLoopCut(const EditableMesh& mesh, EdgeKey seed);
/** @brief 从before重算一条切线；保留原点和面角身份，滑移端点不允许提交。 */
[[nodiscard]] LoopCutResult loopCut(const EditableMesh& before, EdgeKey seed, double slide);
} // namespace mini3d::core::modeling
