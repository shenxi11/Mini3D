/*
 * 模块名: VertexTransform
 * 功能概述: 以世界仿射增量构造独立候选，校验后交给上层安装。
 * 对外接口: VertexTransform.h；依赖关系: MeshDerivation、GLM。
 * 输入输出: 稳定顶点选择到变换后源网格；对象 TRS 不改变。
 * 异常与错误: 不可逆矩阵、溢出或不可三角化候选返回中文原因。
 * 维护说明: 负缩放保留源环，法线随实际绕序变化；不检测全局自相交。
 */
#include "VertexTransform.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <limits>

namespace mini3d::core::modeling {
namespace {
bool isFiniteAffine(const glm::dmat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    const auto determinant = glm::determinant(glm::dmat3(matrix));
    return matrix[0][3] == 0 && matrix[1][3] == 0 && matrix[2][3] == 0 && matrix[3][3] == 1 &&
           std::isfinite(determinant) && determinant != 0;
}
} // namespace

std::optional<glm::dvec3> vertexSelectionCenter(const EditableMesh& mesh,
                                                const std::set<VertexId>& selected,
                                                const glm::dmat4& objectToWorld) {
    if (selected.empty() || !isFiniteAffine(objectToWorld)) {
        return std::nullopt;
    }
    glm::dvec3 center{0};
    std::size_t count = 0;
    for (const auto& vertex : mesh.vertices) {
        if (selected.contains(vertex.id)) {
            center += glm::dvec3(objectToWorld * glm::dvec4(vertex.position, 1));
            ++count;
        }
    }
    if (count != selected.size()) {
        return std::nullopt;
    }
    return center / static_cast<double>(count);
}

VertexTransformResult
VertexTransform::transformSource(const EditableMesh& before, const std::set<VertexId>& selected,
                                 const glm::dmat4& objectToWorld, const glm::dmat4& worldDelta,
                                 std::vector<std::size_t>& affectedFaces, bool& changed) {
    affectedFaces.clear();
    changed = false;
    if (!vertexSelectionCenter(before, selected, objectToWorld)) {
        return {{}, "请选择有效源顶点，并使用有限可逆的对象变换。"};
    }
    if (!isFiniteAffine(worldDelta)) {
        return {{}, "组件变换必须为有限可逆仿射矩阵；缩放不能为零。"};
    }
    auto result = before;
    const auto inverseWorld = glm::inverse(objectToWorld);
    std::set<VertexId> moved;
    for (auto& vertex : result.vertices) {
        if (!selected.contains(vertex.id)) {
            continue;
        }
        const auto position = objectToWorld * glm::dvec4(vertex.position, 1);
        // 用位移差回到局部空间：单位变换精确保留原值，避免 inverse(M)*M 的舍入漂移。
        const auto displacement = inverseWorld * (worldDelta * position - position);
        const auto changed = glm::dvec3(vertex.position) + glm::dvec3(displacement);
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(changed[axis]) ||
                std::abs(changed[axis]) > std::numeric_limits<float>::max()) {
                return {{}, "组件坐标超出可用范围。"};
            }
        }
        if (glm::vec3(changed) != vertex.position) {
            vertex.position = glm::vec3(changed);
            moved.insert(vertex.id);
        }
    }
    if (moved.empty()) {
        return {std::move(result), {}};
    }
    changed = true;
    const auto localDelta = glm::dmat3(inverseWorld * worldDelta * objectToWorld);
    const auto normalMatrix =
        glm::transpose(glm::inverse(localDelta)) * (glm::determinant(localDelta) < 0 ? -1.0 : 1.0);
    for (std::size_t i = 0; i < result.faces.size(); ++i) {
        auto& face = result.faces[i];
        const bool affected =
            std::any_of(face.corners.begin(), face.corners.end(), [&](const auto& corner) {
                return moved.contains(corner.vertex);
            });
        if (!affected) {
            continue;
        }
        affectedFaces.push_back(i);
        const bool wholeFace =
            std::all_of(face.corners.begin(), face.corners.end(), [&](const auto& corner) {
                return selected.contains(corner.vertex);
            });
        for (auto& corner : face.corners) {
            if (!wholeFace) {
                corner.normal.reset();
            } else if (corner.normal) {
                corner.normal =
                    glm::vec3(glm::normalize(normalMatrix * glm::dvec3(*corner.normal)));
            }
        }
    }
    return {std::move(result), {}};
}

VertexTransformResult transformVertices(const EditableMesh& before,
                                        const std::set<VertexId>& selected,
                                        const glm::dmat4& objectToWorld,
                                        const glm::dmat4& worldDelta) {
    std::vector<std::size_t> affectedFaces;
    bool changed = false;
    auto result = VertexTransform::transformSource(before, selected, objectToWorld, worldDelta,
                                                   affectedFaces, changed);
    if (!result.mesh || !changed) {
        return result;
    }
    auto validation = deriveMesh(*result.mesh);
    if (!validation.derived) {
        return {{}, validation.error};
    }
    result.derived = std::move(validation.derived);
    return result;
}
} // namespace mini3d::core::modeling
