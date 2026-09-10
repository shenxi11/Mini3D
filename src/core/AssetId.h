/*
 * 模块名: AssetId
 * 功能概述: 静态 glTF 导入与 CPU 资源管理的 AssetId 数据/接口。
 * 对外接口: AssetId
 * 依赖关系: C++/GLM
 * 输入输出: 输入资源描述，输出稳定资源引用或导入结果。
 * 异常与错误: 无效导入通过 error 返回，不执行 GPU 操作。
 * 维护说明: 使用 AssetId 关联，避免资源层反向依赖 Renderer。
 */
#pragma once
#include <cstdint>
namespace mini3d::core {
/** @brief AssetManager 生命周期内唯一的 CPU/GPU 资源键；0 表示无资源。 */
using AssetId = std::uint64_t;
inline constexpr AssetId kInvalidAsset = 0;
/** @brief 场景只保存资源 ID，不拥有网格、纹理或 GPU 句柄。 */
struct MeshRendererComponent {
    AssetId mesh{kInvalidAsset};
    AssetId material{kInvalidAsset};
};
} // namespace mini3d::core
