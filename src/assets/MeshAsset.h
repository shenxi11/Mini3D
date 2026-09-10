/*
 * 模块名: MeshAsset
 * 功能概述: 静态 glTF 导入与 CPU 资源管理的 MeshAsset 数据/接口。
 * 对外接口: MeshAsset
 * 依赖关系: C++/GLM、Qt Gui
 * 输入输出: 输入资源描述，输出稳定资源引用或导入结果。
 * 异常与错误: 无效导入通过 error 返回，不执行 GPU 操作。
 * 维护说明: 使用 AssetId 关联，避免资源层反向依赖 Renderer。
 */
#pragma once
#include "core/AssetId.h"
#include "core/MeshData.h"
namespace mini3d::assets {
/** @brief 单个三角形 primitive 的 CPU 数据和基础材质引用。 */
struct MeshAsset {
    core::MeshData data;
    core::AssetId material{core::kInvalidAsset};
};
} // namespace mini3d::assets
