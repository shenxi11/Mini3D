/*
 * 模块名: RayCaster
 * 功能概述: 统一内置/导入几何的包围盒查询和场景射线拾取。
 * 对外接口: RayCaster::localBounds、worldBounds、pick
 * 依赖关系: Core、CPU Assets、PrimitiveFactory；不使用 OpenGL
 * 输入输出: 输入只读场景/资源和射线，输出包围盒或最近命中 EntityId。
 * 异常与错误: 空节点/缺失几何返回空盒，无命中返回无效 ID。
 * 维护说明: 使用局部 AABB 近似拾取，不进行三角形或 GPU ID 检测。
 */
#pragma once

#include "assets/AssetManager.h"
#include "core/Ray.h"
#include "core/Scene.h"

namespace mini3d::renderer_gl {
/** @brief 只读 CPU 查询；不拥有场景、选择状态或 GPU 资源。 */
class RayCaster final {
  public:
    /** @brief 返回节点自身几何局部盒；不包含子节点，不修改输入。 */
    [[nodiscard]] static core::Aabb localBounds(const core::SceneNode& node,
                                                const assets::AssetManager& assets);
    /** @brief 包围指定节点及可见后代的世界几何；祖先隐藏时返回空盒。 */
    [[nodiscard]] static core::Aabb
    worldBounds(const core::Scene& scene, const assets::AssetManager& assets, core::EntityId root);
    /** @brief 返回最近非负命中的可见几何 ID；等距保留树序靠前者，容器不参与拾取。 */
    [[nodiscard]] static core::EntityId
    pick(const core::Scene& scene, const assets::AssetManager& assets, const core::Ray& ray);
};
} // namespace mini3d::renderer_gl
