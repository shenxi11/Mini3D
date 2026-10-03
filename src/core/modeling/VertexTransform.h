/*
 * 模块名: VertexTransform
 * 功能概述: 在对象世界矩阵下变换去重源顶点，保持拓扑身份及未受影响属性。
 * 对外接口: vertexSelectionCenter、transformVertices、VertexTransformResult
 * 依赖关系: EditableMesh、GLM、标准容器，无 Qt/GL。
 * 输入输出: 固定 before、稳定顶点集合和世界仿射增量到离线候选。
 * 异常与错误: 非法身份/矩阵/几何拒绝且不改变 before。
 * 维护说明: 调用方必须每帧传原 before，不把上一帧候选再变换一次。
 */
#pragma once

#include "MeshDerivation.h"

#include <glm/mat4x4.hpp>
#include <set>
#include <string>

namespace mini3d::core {
class Scene;
}

namespace mini3d::core::modeling {
struct VertexTransformResult {
    std::optional<EditableMesh> mesh;
    std::string error;
    /** @brief transformVertices 实际移动时保留本次完整派生结果；其他路径可为空。 */
    std::optional<DerivedMesh> derived = std::nullopt;
};
/** @brief 去重顶点世界质心；空集合、无效身份或矩阵返回空。 */
[[nodiscard]] std::optional<glm::dvec3> vertexSelectionCenter(const EditableMesh& mesh,
                                                              const std::set<VertexId>& selected,
                                                              const glm::dmat4& objectToWorld);
/** @brief 仅变换所选点；完整受变换面按逆转置更新硬法线，局部变形面重新求法线。 */
[[nodiscard]] VertexTransformResult transformVertices(const EditableMesh& before,
                                                      const std::set<VertexId>& selected,
                                                      const glm::dmat4& objectToWorld,
                                                      const glm::dmat4& worldDelta);
/** @brief 源变换仅供 Scene 可信快照和完整校验入口共用，不接受外部派生缓存。 */
class VertexTransform {
  private:
    friend class mini3d::core::Scene;
    friend VertexTransformResult transformVertices(const EditableMesh&, const std::set<VertexId>&,
                                                   const glm::dmat4&, const glm::dmat4&);
    [[nodiscard]] static VertexTransformResult
    transformSource(const EditableMesh& before, const std::set<VertexId>& selected,
                    const glm::dmat4& objectToWorld, const glm::dmat4& worldDelta,
                    std::vector<std::size_t>& affectedFaces, bool& changed);
};
} // namespace mini3d::core::modeling
