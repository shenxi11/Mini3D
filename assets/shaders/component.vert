/*
 * 模块名: component_vertex_shader
 * 功能概述: 源网格覆盖层世界空间投影及微小深度偏移。
 * 对外接口: main；依赖关系: GLSL 410；输入输出: 世界坐标/颜色到裁剪坐标/颜色。
 * 异常与错误: ShaderProgram 报告编译错误；维护说明: 保留深度测试，不透视选择背面。
 */
#version 410 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 color;
uniform mat4 uViewProjection;
out vec4 vertexColor;
void main() {
    gl_Position = uViewProjection * vec4(position, 1.0);
    gl_Position.z -= 0.00002 * gl_Position.w;
    vertexColor = color;
}
