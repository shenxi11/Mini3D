/*
 * 模块名: ObjExporter
 * 功能概述: 将源多边形或静态三角网格编码为世界坐标 OBJ 文本，不修改输入。
 * 对外接口: ObjExportResult、encodeObj 的 EditableMesh/MeshData 重载。
 * 依赖关系: EditableMesh、Core MeshData、GLM、C++ 标准字符串。
 * 输入输出: 只读网格、世界仿射矩阵和 UTF-8 名称到完整 OBJ 文本或错误。
 * 异常与错误: 非法网格、非有限数或不可逆矩阵返回失败；分配异常向上传播。
 * 维护说明: 右手 Y-up、单位不转换，变换已烘焙；无文件 IO、材质、场景或历史依赖。
 */
#pragma once

#include "EditableMesh.h"
#include "core/MeshData.h"

#include <glm/mat4x4.hpp>
#include <optional>
#include <string>

namespace mini3d::core::modeling {
/** @brief 成功时 text 含完整 UTF-8 OBJ；失败时 text 为空且 error 给出原因。 */
struct ObjExportResult {
    std::optional<std::string> text;
    std::string error;
};

/** @brief 保留源顶点与面环，逐角 UV/法线独立导出，缺硬法线由 deriveMesh 计算。
 * world 必须有限、可逆且仿射；负缩放反转完整角序。name 中的 CR/LF 被移除。
 * 输入始终只读；成功返回完整文本，失败不返回部分内容。
 */
[[nodiscard]] ObjExportResult encodeObj(const EditableMesh& source, const glm::dmat4& world,
                                        const std::string& name);

/** @brief 保留静态网格的真实三角形和拆分顶点，不按位置焊接或恢复猜测的面环。
 * world、name 和失败行为与 EditableMesh 重载相同；不导出材质、纹理或颜色。
 */
[[nodiscard]] ObjExportResult encodeObj(const MeshData& source, const glm::dmat4& world,
                                        const std::string& name);
} // namespace mini3d::core::modeling
