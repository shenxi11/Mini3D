/*
 * 模块名: MeshValidation
 * 功能概述: 校验网格快照的 ID、引用、属性、退化和流形绕序，不修改输入。
 * 对外接口: MeshError、MeshValidation、validateEditableMesh
 * 依赖关系: EditableMesh、标准字符串
 * 输入输出: 候选网格到首个明确错误或有效源边/边界边统计。
 * 异常与错误: 非法输入返回失败；内存分配异常传播，原快照保持不变。
 * 维护说明: 不做全局自交检测，几何算子/三角化仍需校验各自支持的面形状。
 */
#pragma once

#include "EditableMesh.h"

#include <string>

namespace mini3d::core::modeling {
enum class MeshError {
    None,
    InvalidId,
    DuplicateId,
    MissingVertex,
    NonFiniteAttribute,
    InvalidNormal,
    FaceTooSmall,
    RepeatedFaceVertex,
    DegenerateEdge,
    DegenerateFace,
    NonManifoldEdge,
    InconsistentWinding,
    NonManifoldVertex
};

/** @brief 边统计只在 isValid 为真时有意义；空网格/孤立点允许存在。 */
struct MeshValidation {
    MeshError error = MeshError::None;
    std::string message;
    std::size_t edgeCount = 0;
    std::size_t boundaryEdgeCount = 0;
    [[nodiscard]] bool isValid() const {
        return error == MeshError::None;
    }
};

/** @brief 接受可定向二维流形（允许边界/多个独立分量）；拒绝非流形边与点接触双扇。 */
[[nodiscard]] MeshValidation validateEditableMesh(const EditableMesh& mesh);
/** @brief 校验单面的有序面角坐标、硬法线及退化；不替代身份、引用和流形校验。 */
[[nodiscard]] MeshValidation validateFaceGeometry(const EditableFace& face,
                                                  const std::vector<glm::dvec3>& positions);
} // namespace mini3d::core::modeling
