/*
 * 模块名: ProportionalTransform
 * 功能概述: 从固定源网格计算比例编辑权重，并以加权世界位移生成离线候选。
 * 对外接口: ProportionalWeightsResult、proportionalWeights、transformWeightedVertices
 * 依赖关系: VertexTransform、GLM、标准容器，无 Qt/GL。
 * 输入输出: before、驱动点、世界半径和仿射增量到稳定顶点权重或候选网格。
 * 异常与错误: 非法源、身份、权重、矩阵或几何返回原因，不发布部分结果。
 * 维护说明: Connected 仅限制源边连通片；调用方每帧使用原 before，并负责取消与历史。
 */
#pragma once

#include "VertexTransform.h"

#include <map>

namespace mini3d::core::modeling {
/** @brief 只读权重计算结果；失败时 weights 为空，error 给出原因。 */
struct ProportionalWeightsResult {
    std::optional<std::map<VertexId, double>> weights;
    std::string error;
};

/**
 * @brief 用 before 世界距离到最近 drivers 计算 Smooth 权重，drivers 恒为 1。
 * @param radius 有限且大于零的世界半径；边界及半径外的非驱动点不列入权重。
 * @param connected 为真时仅包含驱动所在源边连通片，距离仍为欧氏距离。
 * @return 离线权重或中文原因；空驱动、无效身份/源/世界矩阵失败，输入不改变。
 */
[[nodiscard]] ProportionalWeightsResult proportionalWeights(const EditableMesh& before,
                                                            const std::set<VertexId>& drivers,
                                                            const glm::dmat4& objectToWorld,
                                                            double radius, bool connected);

/**
 * @brief 按 weights 插值每点的 worldDelta 世界位移，再回到对象局部坐标。
 * @param weights 非空，稳定顶点 ID 必须存在，权重必须有限且位于 [0, 1]。
 * @return 保留拓扑、ID、UV、颜色的独立候选；受影响面清除硬法线并派生校验一次。
 * 非法源/矩阵、溢出或退化失败无部分结果；输入不改变，单位变换精确保留源。
 */
[[nodiscard]] VertexTransformResult
transformWeightedVertices(const EditableMesh& before, const std::map<VertexId, double>& weights,
                          const glm::dmat4& objectToWorld, const glm::dmat4& worldDelta);
} // namespace mini3d::core::modeling
