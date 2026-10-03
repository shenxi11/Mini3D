/*
 * 模块名: ProportionalTransform
 * 功能概述: 用源半边连通片与世界欧氏距离计算 Smooth 权重，整批变换候选。
 * 对外接口: ProportionalTransform.h
 * 依赖关系: MeshTopology、MeshDerivation、GLM、标准数学与容器。
 * 输入输出: 只读 before 与稳定顶点权重到独立候选，不安装场景或更新对象 TRS。
 * 异常与错误: 检查有限可逆仿射、权重范围与坐标溢出，几何失败不返回候选。
 * 维护说明: 用位移差避免 inverse(M)*M 漂移；不按测地距离衰减或处理取消/历史。
 */
#include "ProportionalTransform.h"

#include "MeshDerivation.h"
#include "MeshTopology.h"

#include <cmath>
#include <glm/matrix.hpp>
#include <limits>

namespace mini3d::core::modeling {
namespace {
bool isFinite(const glm::dmat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

bool isFiniteAffine(const glm::dmat4& matrix) {
    if (!isFinite(matrix)) {
        return false;
    }
    const auto determinant = glm::determinant(glm::dmat3(matrix));
    return matrix[0][3] == 0 && matrix[1][3] == 0 && matrix[2][3] == 0 && matrix[3][3] == 1 &&
           std::isfinite(determinant) && determinant != 0;
}
} // namespace

ProportionalWeightsResult proportionalWeights(const EditableMesh& before,
                                              const std::set<VertexId>& drivers,
                                              const glm::dmat4& objectToWorld, double radius,
                                              bool connected) {
    if (drivers.empty() || !std::isfinite(radius) || radius <= 0) {
        return {{}, "请选择驱动顶点，并使用有限且大于零的比例编辑半径。"};
    }
    if (!isFiniteAffine(objectToWorld)) {
        return {{}, "比例编辑必须使用有限可逆的对象仿射变换。"};
    }
    const auto topology = buildMeshTopology(before);
    if (!topology.topology) {
        return {{}, topology.error};
    }
    std::map<VertexId, glm::dvec3> positions;
    for (const auto& vertex : before.vertices) {
        const auto position = glm::dvec3(objectToWorld * glm::dvec4(vertex.position, 1));
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z)) {
            return {{}, "比例编辑世界坐标超出可用范围。"};
        }
        positions.emplace(vertex.id, position);
    }
    for (const auto driver : drivers) {
        if (!positions.contains(driver)) {
            return {{}, "比例编辑驱动顶点不存在。"};
        }
    }
    std::set<VertexId> reachable = drivers;
    if (connected) {
        std::vector<VertexId> pending(drivers.begin(), drivers.end());
        for (std::size_t i = 0; i < pending.size(); ++i) {
            for (const auto index : topology.topology->vertexHalfEdges.at(pending[i])) {
                const auto& edge = topology.topology->halfEdges[index];
                for (const auto neighbor :
                     {edge.to, topology.topology->halfEdges[edge.previous].from}) {
                    if (reachable.insert(neighbor).second) {
                        pending.push_back(neighbor);
                    }
                }
            }
        }
    }
    std::map<VertexId, double> weights;
    for (const auto& [id, position] : positions) {
        if (drivers.contains(id)) {
            weights.emplace(id, 1.0);
            continue;
        }
        if (connected && !reachable.contains(id)) {
            continue;
        }
        double distance = radius;
        for (const auto driver : drivers) {
            const auto displacement = position - positions.at(driver);
            const auto candidate = std::hypot(displacement.x, displacement.y, displacement.z);
            if (!std::isfinite(candidate)) {
                return {{}, "比例编辑世界距离超出可用范围。"};
            }
            distance = std::min(distance, candidate);
        }
        if (distance < radius) {
            const auto t = distance / radius;
            weights.emplace(id, std::clamp(1.0 - 3.0 * t * t + 2.0 * t * t * t, 0.0, 1.0));
        }
    }
    return {std::move(weights), {}};
}

VertexTransformResult transformWeightedVertices(const EditableMesh& before,
                                                const std::map<VertexId, double>& weights,
                                                const glm::dmat4& objectToWorld,
                                                const glm::dmat4& worldDelta) {
    if (weights.empty() || !isFiniteAffine(objectToWorld) || !isFiniteAffine(worldDelta)) {
        return {{}, "比例编辑必须使用有效权重和有限可逆仿射矩阵；缩放不能为零。"};
    }
    const auto sourceValidation = validateEditableMesh(before);
    if (!sourceValidation.isValid()) {
        return {{}, sourceValidation.message};
    }
    const auto inverseWorld = glm::inverse(objectToWorld);
    if (!isFinite(inverseWorld)) {
        return {{}, "比例编辑对象逆变换超出可用范围。"};
    }
    auto result = before;
    std::set<VertexId> moved;
    std::size_t matched = 0;
    for (auto& vertex : result.vertices) {
        const auto found = weights.find(vertex.id);
        if (found == weights.end()) {
            continue;
        }
        ++matched;
        const auto weight = found->second;
        if (!std::isfinite(weight) || weight < 0 || weight > 1) {
            return {{}, "比例编辑权重必须是位于 [0, 1] 的有限数。"};
        }
        if (weight == 0) {
            continue;
        }
        const auto position = objectToWorld * glm::dvec4(vertex.position, 1);
        // 插值世界位移而非变换矩阵，单位增量的差精确为零。
        const auto displacement = inverseWorld * (weight * (worldDelta * position - position));
        const auto changed = glm::dvec3(vertex.position) + glm::dvec3(displacement);
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(changed[axis]) ||
                std::abs(changed[axis]) > std::numeric_limits<float>::max()) {
                return {{}, "比例编辑坐标超出可用范围。"};
            }
        }
        if (glm::vec3(changed) != vertex.position) {
            vertex.position = glm::vec3(changed);
            moved.insert(vertex.id);
        }
    }
    if (matched != weights.size()) {
        return {{}, "比例编辑权重引用的顶点不存在。"};
    }
    for (auto& face : result.faces) {
        if (std::any_of(face.corners.begin(), face.corners.end(), [&](const auto& corner) {
                return moved.contains(corner.vertex);
            })) {
            for (auto& corner : face.corners) {
                corner.normal.reset();
            }
        }
    }
    const auto validation = deriveMesh(result);
    if (!validation.derived) {
        return {{}, validation.error};
    }
    return {std::move(result), {}};
}
} // namespace mini3d::core::modeling
