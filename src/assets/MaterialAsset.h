/*
 * 模块名: MaterialAsset
 * 功能概述: 静态 glTF 导入与 CPU 资源管理的 MaterialAsset 数据/接口。
 * 对外接口: MaterialAsset
 * 依赖关系: C++/GLM、Qt Gui
 * 输入输出: 输入资源描述，输出稳定资源引用或导入结果。
 * 异常与错误: 无效导入通过 error 返回，不执行 GPU 操作。
 * 维护说明: 使用 AssetId 关联，避免资源层反向依赖 Renderer。
 */
#pragma once
#include "core/AssetId.h"

#include <glm/vec4.hpp>
namespace mini3d::assets {
/** @brief 保留 glTF 基础颜色因子；当前按不透明基础光照绘制，不实现 PBR。 */
struct MaterialAsset {
    glm::vec4 baseColor{1.0F};
    core::AssetId baseColorTexture{core::kInvalidAsset};
    bool doubleSided{false};
};
} // namespace mini3d::assets
