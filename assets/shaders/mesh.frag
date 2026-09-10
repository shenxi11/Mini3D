/*
 * 模块名: mesh_fragment_shader
 * 功能概述: 使用场景方向光和环境项显示实例材质与表面朝向。
 * 对外接口: main、fragmentColor
 * 依赖关系: GLSL 410 Core
 * 输入输出: 输入世界法线和插值颜色，输出不透明 RGBA 颜色。
 * 异常与错误: 编译错误由 ShaderProgram 输出资源路径与驱动日志。
 * 维护说明: 不透明 Lambert 漫反射，不实现阴影或 PBR。
 */

#version 410 core

in vec3 worldNormal;
in vec3 vertexColor;
in vec2 textureUv;
uniform vec3 uBaseColor;
uniform bool uUseVertexColor;
uniform bool uUseTexture;
uniform sampler2D uBaseColorTexture;
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform vec3 uAmbient;
out vec4 fragmentColor;

void main() {
    vec3 surfaceNormal = normalize(worldNormal);
    if (!gl_FrontFacing) { surfaceNormal = -surfaceNormal; }
    float diffuse = max(dot(surfaceNormal, normalize(uLightDirection)), 0.0);
    vec3 baseColor = uBaseColor;
    if (uUseVertexColor) {
        baseColor *= vertexColor;
    }
    if (uUseTexture) {
        baseColor *= texture(uBaseColorTexture, textureUv).rgb;
    }
    vec3 litColor = baseColor * (uAmbient + uLightColor * diffuse);
    fragmentColor = vec4(litColor, 1.0);
}
