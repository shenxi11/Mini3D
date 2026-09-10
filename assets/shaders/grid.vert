/*
 * 模块名: grid_vertex_shader
 * 功能概述: 将地面网格和世界轴线段变换到裁剪空间，并传递顶点颜色。
 * 对外接口: main、vertexColor
 * 依赖关系: GLSL 410 Core
 * 输入输出: 输入世界位置、颜色和 ViewProjection，输出裁剪位置与插值颜色。
 * 异常与错误: 编译错误由 ShaderProgram 输出资源路径与驱动日志。
 * 维护说明: 顶点布局必须与 GridRenderer 内部 GridVertex 属性配置一致。
 */

#version 410 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

uniform mat4 uViewProjection;

out vec3 vertexColor;

void main() {
    gl_Position = uViewProjection * vec4(position, 1.0);
    vertexColor = color;
}
