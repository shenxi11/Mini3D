/*
 * 模块名: mesh_vertex_shader
 * 功能概述: 将 Mesh 顶点从局部空间变换到裁剪空间，并输出世界法线和颜色。
 * 对外接口: main、worldNormal、vertexColor
 * 依赖关系: GLSL 410 Core
 * 输入输出: 输入位置、法线、颜色和矩阵 Uniform，输出裁剪位置与插值属性。
 * 异常与错误: 编译错误由 ShaderProgram 输出资源路径与驱动日志。
 * 维护说明: 顶点布局必须与 MeshVertex 和 GpuMesh 属性配置一致。
 */

#version 410 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 uv;

uniform mat4 uModel;
uniform mat4 uViewProjection;

out vec3 worldNormal;
out vec3 vertexColor;
out vec2 textureUv;

void main() {
    gl_Position = uViewProjection * uModel * vec4(position, 1.0);
    worldNormal = mat3(transpose(inverse(uModel))) * normal;
    vertexColor = color;
    textureUv = uv;
}
