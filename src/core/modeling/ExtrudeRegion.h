/*
 * 模块名: ExtrudeRegion
 * 功能概述: 分析连通面域并离线挤出边界侧壁，不修改场景或输入网格。
 * 对外接口: analyzeExtrudeRegion、extrudeRegion。
 * 依赖关系: EditableMesh、GLM、C++ 标准容器，无 Qt/GL。
 * 输入输出: 原始快照、稳定面选择与局部位移到独立候选及中文拒绝原因。
 * 异常与错误: 非法面域/位移/侧壁/ID 溢出返回失败；分配异常向上传播。
 * 维护说明: 顶盖保留原面/面角身份与属性；新侧壁和顶点使用新 ID，不检测全局自交。
 */
#pragma once
#include "EditableMesh.h"

#include <set>
#include <string>

namespace mini3d::core::modeling {
struct ExtrudeRegionInfo {
    glm::dvec3 normal{0};
    std::size_t boundaryEdgeCount = 0;
    bool usesFallbackNormal = false;
};
struct ExtrudeRegionAnalysis {
    std::optional<ExtrudeRegionInfo> region;
    std::string error;
};
struct ExtrudeRegionResult {
    std::optional<EditableMesh> mesh;
    std::string error;
};
/** @brief 验证边连通、可定向面域及非分叉闭边界；均值法线抵消时取最小面 ID 的法线。 */
[[nodiscard]] ExtrudeRegionAnalysis analyzeExtrudeRegion(const EditableMesh& mesh,
                                                         const std::set<FaceId>& faces);
/** @brief 从 before 生成挤出候选；选中面成为新顶盖，原选择 ID 仍指向顶盖，不保留重叠底面。 */
[[nodiscard]] ExtrudeRegionResult extrudeRegion(const EditableMesh& before,
                                                const std::set<FaceId>& faces,
                                                const glm::dvec3& localOffset);
} // namespace mini3d::core::modeling
