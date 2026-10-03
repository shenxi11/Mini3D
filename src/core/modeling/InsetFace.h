/*
 * 模块名: InsetFace
 * 功能概述: 对单个共面凸多边形作局部等距内插，不以中心缩放代替。
 * 对外接口: analyzeInsetFace、insetFace；依赖关系: EditableMesh、GLM，无Qt/GL。
 * 输入输出: before、FaceId与局部厚度到独立候选及拒绝原因。
 * 异常与错误: 非凸/非共面/超限/非法ID拒绝，分配失败向调用方传播。
 * 维护说明: 保留中心面身份、插值面角属性，不检测全局自交。
 */
#pragma once

#include "EditableMesh.h"

#include <string>

namespace mini3d::core::modeling {
struct InsetFaceInfo {
    glm::dvec3 normal{0};
    double maximumThickness = 0;
};
struct InsetFaceAnalysis {
    std::optional<InsetFaceInfo> face;
    std::string error;
};
struct InsetFaceResult {
    std::optional<EditableMesh> mesh;
    std::string error;
};
/** @brief 校验单面共面凸环，返回对象局部单位的首条边坍缩上限；上限值本身不可提交。 */
[[nodiscard]] InsetFaceAnalysis analyzeInsetFace(const EditableMesh& mesh, FaceId face);
/** @brief 从before重算正厚度内插；原FaceId/CornerId指向中心面，边框使用新ID。 */
[[nodiscard]] InsetFaceResult insetFace(const EditableMesh& before, FaceId face,
                                        double localThickness);
} // namespace mini3d::core::modeling
