/*
 * 模块名: ViewportShading
 * 功能概述: 定义独立于场景材质、历史和文件的视口会话显示模式。
 * 对外接口: ViewportShading
 * 依赖关系: 无 Qt/GL 运行期依赖
 * 输入输出: 材质、实体和线框模式到 Renderer 的网格绘制策略。
 * 异常与错误: 无；只描述显示偏好，不执行 GPU 操作。
 * 维护说明: Wireframe 显示求值网格三角化边，不代表真实源拓扑边。
 */
#pragma once

namespace mini3d::renderer_gl {
/** @brief Material 沿用材质；Solid 使用中性基色；Wireframe 绘制网格三角化线框。 */
enum class ViewportShading { Material, Solid, Wireframe };
} // namespace mini3d::renderer_gl
