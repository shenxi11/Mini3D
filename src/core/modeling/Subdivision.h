/*
 * 模块名: Subdivision
 * 功能概述: 对可定向流形四边网格执行一到两级 Catmull-Clark，保留最终面的源面身份。
 * 对外接口: SubdivisionOptions、SubdivisionEvaluation、SubdivisionResult、evaluateSubdivision
 * 依赖关系: EditableMesh、MeshDerivation、C++ 标准容器，无 Qt/GL
 * 输入输出: 只读源网格与级数到独立细分网格、渲染派生数据和最终面到原始面的映射。
 * 异常与错误: 非四边网格、孤立点、非法级数或求值失败返回原因，不发布部分候选。
 * 维护说明: 不支持折痕；UV/颜色按面角插值保留接缝，法线按求值几何重新平滑计算。
 */
#pragma once

#include "MeshDerivation.h"

#include <map>
#include <optional>
#include <string>

namespace mini3d::core::modeling {
/** @brief 单个细分修改器参数；停用时仍保留并校验一到两级设置。 */
struct SubdivisionOptions {
    bool enabled = true;
    int levels = 1;
    [[nodiscard]] bool isValid() const {
        return levels == 1 || levels == 2;
    }
    bool operator==(const SubdivisionOptions&) const = default;
};

/** @brief 完整离线求值；faces 将每个最终求值面 ID 映射到最初输入的源面 ID。 */
struct SubdivisionEvaluation {
    EditableMesh mesh;
    DerivedMesh derived;
    std::map<FaceId, FaceId> faces;
};

/** @brief 求值成功时仅 evaluation 有值；失败仅返回 error，输入快照不受影响。 */
struct SubdivisionResult {
    std::optional<SubdivisionEvaluation> evaluation;
    std::string error;
};

/**
 * @brief 在局部空间细分 source，levels 仅接受 1 或 2。
 * @return 完整候选及源面映射，或明确错误；不修改 source，分配异常向上传播。
 */
[[nodiscard]] SubdivisionResult evaluateSubdivision(const EditableMesh& source, int levels);
} // namespace mini3d::core::modeling
