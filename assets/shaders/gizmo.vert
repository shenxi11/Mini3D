/* 模块名: gizmo_vertex_shader
 * 功能概述: 移动轴顶点投影。对外接口: main、uMvp。
 * 依赖关系: GLSL 410。输入输出: 模型顶点到裁剪位置。
 * 异常与错误: 编译错误由 ShaderProgram 报告。维护说明: 复用 MeshVertex 位置属性。
 */
#version 410 core
layout(location = 0) in vec3 position;
uniform mat4 uMvp;
void main() { gl_Position = uMvp * vec4(position, 1.0); }
