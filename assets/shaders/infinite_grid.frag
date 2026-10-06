/*
 * 模块名: infinite_grid_fragment_shader
 * 功能概述: 求交无限 XZ 地面，按像素足迹抗锯齿并平滑切换主次网格。
 * 对外接口: main、fragmentColor、gl_FragDepth
 * 依赖关系: GLSL 410 Core
 * 输入输出: 射线到真实地面深度、网格及 X/Z 坐标轴颜色。
 * 异常与错误: 平行、背向或裁剪范围外的交点不绘制。
 * 维护说明: 远处淡出发生在裁剪面之前；不写入场景深度或生成线段。
 */
#version 410 core

in vec3 nearPoint;
in vec3 farPoint;
uniform mat4 uViewProjection;
uniform vec3 uEye;
uniform vec3 uOrigin;
uniform vec3 uGridPhase;
uniform vec3 uGridParameters; // 间距、远裁剪距离、连续尺度过渡
out vec4 fragmentColor;

float gridCoverage(vec2 point, vec2 footprint, float spacing) {
    vec2 distanceToLine = abs(fract(point / spacing + 0.5) - 0.5) * spacing;
    vec2 coverage = 1.0 - smoothstep(footprint * 0.35, footprint * 1.35, distanceToLine);
    // 亚像素密度过高时隐去该层，避免近水平观察的摩尔纹。
    float resolved = 1.0 - smoothstep(spacing * 0.2, spacing * 0.5,
                                    max(footprint.x, footprint.y));
    return max(coverage.x, coverage.y) * resolved;
}

void main() {
    vec3 direction = farPoint - nearPoint;
    float denominator = abs(direction.y) > 1e-6 ? direction.y :
                        (direction.y < 0.0 ? -1e-6 : 1e-6);
    float t = -nearPoint.y / denominator;
    vec3 point = mix(nearPoint, farPoint, clamp(t, 0.0, 1.0));
    vec2 footprint = max(fwidth(point.xz), vec2(1e-6));
    if (abs(direction.y) <= 1e-6 || t <= 0.0 || t >= 1.0) discard;

    float spacing = uGridParameters.x;
    float transition = smoothstep(0.0, 1.0, uGridParameters.z);
    vec2 phasePoint = point.xz + uGridPhase.xz;
    float fine = gridCoverage(phasePoint, footprint, spacing) * (1.0 - transition) * 0.32;
    float major = gridCoverage(phasePoint, footprint, spacing * 10.0) *
                  mix(0.65, 0.32, transition);
    float coarse = gridCoverage(phasePoint, footprint, spacing * 100.0) * transition * 0.65;
    float coverage = max(fine, max(major, coarse));
    vec3 color = vec3(0.49, 0.54, 0.62);

    vec2 axisDistance = abs(point.xz + uOrigin.xz);
    vec2 axes = 1.0 - smoothstep(footprint * 0.45, footprint * 1.45, axisDistance);
    if (max(axes.x, axes.y) > 0.0) {
        float axis = max(axes.x, axes.y);
        color = mix(color, axes.y >= axes.x ? vec3(0.92, 0.24, 0.20) :
                                             vec3(0.24, 0.48, 0.96), axis);
        coverage = max(coverage, axis * 0.9);
    }
    float fade = 1.0 - smoothstep(uGridParameters.y * 0.35, uGridParameters.y * 0.95,
                                 length(point - uEye));
    float alpha = coverage * fade;
    if (alpha < 0.003) discard;
    vec4 clip = uViewProjection * vec4(point, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    fragmentColor = vec4(color, alpha);
}
