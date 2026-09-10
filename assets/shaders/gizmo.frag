/* 模块名: gizmo_fragment_shader
 * 功能概述: 输出移动轴纯色。对外接口: main、uColor。
 * 依赖关系: GLSL 410。输入输出: 轴颜色到不透明片元。
 * 异常与错误: 编译错误由 ShaderProgram 报告。维护说明: 不采样纹理。
 */
#version 410 core
uniform vec3 uColor;
out vec4 fragmentColor;
void main() { fragmentColor = vec4(uColor, 1.0); }
