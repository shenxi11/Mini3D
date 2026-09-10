/*
 * 模块名: grid_fragment_shader
 * 功能概述: 输出地面网格与 XYZ 世界轴的顶点颜色。
 * 对外接口: main、fragmentColor
 * 依赖关系: GLSL 410 Core
 * 输入输出: 输入插值颜色，输出不透明 RGBA 颜色。
 * 异常与错误: 编译错误由 ShaderProgram 输出资源路径与驱动日志。
 * 维护说明: 首周网格保持不透明，由 Renderer 的深度状态控制遮挡关系。
 */

#version 410 core

in vec3 vertexColor;
out vec4 fragmentColor;

void main() {
    fragmentColor = vec4(vertexColor, 1.0);
}
