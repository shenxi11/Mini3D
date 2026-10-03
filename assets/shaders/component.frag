/*
 * 模块名: component_fragment_shader
 * 功能概述: 输出选区透明度与活动/非活动颜色。
 * 对外接口: main；依赖关系: GLSL 410；输入输出: 插值颜色到 RGBA。
 * 异常与错误: ShaderProgram 报告编译错误；维护说明: 选中面混合，点线不透明。
 */
#version 410 core
in vec4 vertexColor;
out vec4 fragmentColor;
void main() {
    fragmentColor = vertexColor;
}
