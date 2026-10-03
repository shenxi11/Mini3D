/*
 * 模块名: ComponentPicker
 * 功能概述: 在真实视口投影中选择可见源点、源边或源多边形，不把渲染对角线当编辑边。
 * 对外接口: pickComponent、boxSelectComponents
 * 依赖关系: ComponentSelection、Scene、CPU Assets、EditorCamera，无 GL 调用。
 * 输入输出: 逻辑像素点击到稳定组件 ID；未命中返回空。
 * 异常与错误: 隐藏/非可编辑对象不命中；维护说明: 点选与框选共用投影和遮挡策略。
 */
#pragma once

#include "assets/AssetManager.h"
#include "core/Scene.h"
#include "core/ViewportVisibility.h"
#include "editor/ComponentSelection.h"
#include "renderer_gl/EditorCamera.h"

namespace mini3d::editor {
/** @brief 顶点吸附命中；editable 使用源顶点 ID，静态网格使用渲染顶点下标加一。 */
struct VertexSnapHit {
    core::EntityId entity = 0;
    std::uint64_t vertex = 0;
    glm::vec3 position{0};
};
/** @brief 10逻辑像素内最近可见求值顶点；对象排除移动子树，组件排除所选源顶点。
 * 开启细分的组件自吸附排除整个求值源，避免给多源驱动的新点伪造单源身份。
 * 数值/轴约束由调用者处理；不受X-Ray影响，不修改几何，未命中返回空。
 */
[[nodiscard]] std::optional<VertexSnapHit>
pickVertexSnap(const core::Scene& scene, const assets::AssetManager& assets,
               const renderer_gl::EditorCamera& camera, glm::vec2 pixel, glm::ivec2 viewportSize,
               core::EntityId source, bool objectMode,
               const std::set<core::modeling::VertexId>& excludedVertices = {},
               const core::ViewportVisibility& visibility = {});
struct CursorPlacement {
    std::optional<glm::vec3> position;
    bool surface = false;
    QString error;
};
/** @brief 最近可见真实表面优先，未命中求 XZ/Y=0 平面；近平行或裁剪外明确拒绝。 */
[[nodiscard]] CursorPlacement locateCursor(const core::Scene& scene,
                                           const assets::AssetManager& assets,
                                           const renderer_gl::EditorCamera& camera, glm::vec2 pixel,
                                           glm::ivec2 viewportSize,
                                           const core::ViewportVisibility& visibility = {});
/** @brief 点/边半径 10 逻辑像素；真实三角深度遮挡，近者优先，等距按稳定 ID。 */
[[nodiscard]] std::optional<ComponentId>
pickComponent(const core::Scene& scene, const assets::AssetManager& assets, core::EntityId entity,
              SelectionDomain domain, const renderer_gl::EditorCamera& camera, glm::vec2 pixel,
              glm::ivec2 viewportSize, bool xRay = false,
              const core::ViewportVisibility& visibility = {});
/** @brief 触碰框选：点投影、裁剪边段、面角均值中心；可见模式只接受未遮挡部分。 */
[[nodiscard]] std::set<ComponentId>
boxSelectComponents(const core::Scene& scene, const assets::AssetManager& assets,
                    core::EntityId entity, SelectionDomain domain,
                    const renderer_gl::EditorCamera& camera, glm::vec2 first, glm::vec2 second,
                    glm::ivec2 viewportSize, bool xRay = false,
                    const core::ViewportVisibility& visibility = {});
} // namespace mini3d::editor
