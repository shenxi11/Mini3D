/*
 * 模块名: infinite_grid_vertex_shader
 * 功能概述: 从屏幕三角形逆投影近远裁剪面，支持透视和正交相机。
 * 对外接口: main、nearPoint、farPoint
 * 依赖关系: GLSL 410 Core
 * 输入输出: 相机附近的逆投影矩阵到平面求交所需射线端点。
 * 异常与错误: 编译失败由 ShaderProgram 报告。
 * 维护说明: 不读取顶点 Buffer；每帧固定三个顶点。
 */
#version 410 core

uniform mat4 uInverseViewProjection;
out vec3 nearPoint;
out vec3 farPoint;

vec3 unproject(vec2 point, float depth) {
    vec4 world = uInverseViewProjection * vec4(point, depth, 1.0);
    return world.xyz / world.w;
}

void main() {
    vec2 point = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    nearPoint = unproject(point, -1.0);
    farPoint = unproject(point, 1.0);
    gl_Position = vec4(point, 0.0, 1.0);
}
