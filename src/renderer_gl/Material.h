/*
 * 模块名: Material
 * 功能概述: 定义当前不透明绘制使用的基础颜色和可选纹理引用。
 * 对外接口: Material
 * 依赖关系: GLM；GpuTexture 前向声明
 * 输入输出: 输入每个物体的外观参数，由 Renderer 写入 Shader。
 * 异常与错误: 无运行时处理；空纹理引用表示纯色或顶点颜色。
 * 维护说明: 纹理引用不拥有资源，Renderer 保证纹理在绘制期间存活。
 */
#pragma once
#include <glm/vec3.hpp>

namespace mini3d::renderer_gl {
class GpuTexture;

/** @brief 基础颜色乘以可选顶点颜色和贴图；不包含透明度或 PBR 参数。 */
struct Material {
    glm::vec3 baseColor{1.0F};
    bool useVertexColor = true;
    const GpuTexture* baseColorTexture = nullptr;
    bool doubleSided = false;
};
} // namespace mini3d::renderer_gl
