/*
 * 模块名: EditorCamera
 * 功能概述: 实现球坐标 Orbit、相机平面 Pan、指数 Zoom 和透视矩阵计算。
 * 对外接口: mini3d::renderer_gl::EditorCamera
 * 依赖关系: GLM、C++ 数学库
 * 输入输出: 输入交互增量与尺寸，输出稳定且有限的相机状态和矩阵。
 * 异常与错误: 输入通过 clamp 和最小尺寸规避奇异投影与目标点重合。
 * 维护说明: 坐标系为右手、Y 轴向上；不读取 Qt 事件或 OpenGL 状态。
 */

#include "EditorCamera.h"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <numbers>

namespace mini3d::renderer_gl {

void EditorCamera::setViewportSize(int width, int height) {
    viewportWidth_ = std::max(width, 1);
    viewportHeight_ = std::max(height, 1);
}

void EditorCamera::setView(EditorView view) {
    view_ = view;
}
EditorView EditorCamera::view() const {
    return view_;
}
void EditorCamera::setOrthographic(bool enabled) {
    orthographic_ = enabled;
}
bool EditorCamera::isOrthographic() const {
    return orthographic_;
}

void EditorCamera::orbit(float deltaX, float deltaY) {
    if (view_ != EditorView::Orbit) {
        yawRadians_ = view_ == EditorView::Right ? std::numbers::pi_v<float> * 0.5F : 0;
        pitchRadians_ = view_ == EditorView::Top ? kMaximumPitch : 0;
        view_ = EditorView::Orbit;
    }
    constexpr float fullTurn = 2.0F * std::numbers::pi_v<float>;
    yawRadians_ = std::remainder(yawRadians_ - deltaX * kOrbitRadiansPerPixel, fullTurn);
    pitchRadians_ =
        std::clamp(pitchRadians_ - deltaY * kOrbitRadiansPerPixel, kMinimumPitch, kMaximumPitch);
}

void EditorCamera::pan(float deltaX, float deltaY) {
    const auto inverseView = glm::inverse(viewMatrix());
    const glm::vec3 right(inverseView[0]);
    const glm::vec3 up(inverseView[1]);
    const float visibleWorldHeight = 2.0F * distance_ * std::tan(kFieldOfViewRadians * 0.5F);
    const float worldUnitsPerPixel = visibleWorldHeight / static_cast<float>(viewportHeight_);
    target_ += (-right * deltaX + up * deltaY) * worldUnitsPerPixel;
}

void EditorCamera::zoom(float wheelSteps) {
    const float scale = std::exp(-wheelSteps * kZoomExponentPerStep);
    distance_ = std::clamp(distance_ * scale, kMinimumDistance, maximumDistance_);
}

core::Ray EditorCamera::screenRay(float x, float y) const {
    const glm::vec2 ndc(2.0F * x / static_cast<float>(viewportWidth_) - 1.0F,
                        1.0F - 2.0F * y / static_cast<float>(viewportHeight_));
    const auto inverse = glm::inverse(viewProjectionMatrix());
    const auto nearPoint = inverse * glm::vec4(ndc, -1.0F, 1.0F);
    const glm::vec3 origin = glm::vec3(nearPoint) / nearPoint.w;
    return {origin, glm::normalize(orthographic_ ? target_ - position() : origin - position())};
}

bool EditorCamera::focus(const core::Aabb& bounds) {
    if (!bounds.isValid()) {
        return false;
    }
    const glm::vec3 center = bounds.minimum * 0.5F + bounds.maximum * 0.5F;
    const float radius = glm::length(bounds.maximum * 0.5F - bounds.minimum * 0.5F);
    const float verticalHalfAngle = kFieldOfViewRadians * 0.5F;
    const float horizontalHalfAngle = std::atan(std::tan(verticalHalfAngle) * aspectRatio());
    const float distance =
        std::max(kMinimumDistance,
                 1.1F * radius / std::sin(std::min(verticalHalfAngle, horizontalHalfAngle)));
    if (!std::isfinite(distance) || !std::isfinite(distance * 4.0F)) {
        return false;
    }
    target_ = center;
    distance_ = distance;
    focusRadius_ = radius;
    maximumDistance_ = std::max(kMaximumDistance, distance * 4.0F);
    return true;
}

glm::mat4 EditorCamera::viewMatrix() const {
    return glm::lookAt(position(), target_,
                       view_ == EditorView::Top ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0));
}

glm::mat4 EditorCamera::projectionMatrix() const {
    const float farPlane = std::max(kFarPlane, distance_ + 2.0F * focusRadius_);
    if (orthographic_) {
        const float halfHeight = distance_ * std::tan(kFieldOfViewRadians * 0.5F);
        return glm::ortho(-halfHeight * aspectRatio(), halfHeight * aspectRatio(), -halfHeight,
                          halfHeight, kNearPlane, farPlane);
    }
    return glm::perspective(kFieldOfViewRadians, aspectRatio(), kNearPlane, farPlane);
}

glm::mat4 EditorCamera::viewProjectionMatrix() const {
    return projectionMatrix() * viewMatrix();
}

glm::vec3 EditorCamera::position() const {
    switch (view_) {
        case EditorView::Front:
            return target_ + glm::vec3(0, 0, distance_);
        case EditorView::Right:
            return target_ + glm::vec3(distance_, 0, 0);
        case EditorView::Top:
            return target_ + glm::vec3(0, distance_, 0);
        case EditorView::Orbit:
            return orbitPosition();
    }
    return orbitPosition();
}

glm::vec3 EditorCamera::orbitPosition() const {
    const float horizontalDistance = distance_ * std::cos(pitchRadians_);
    const glm::vec3 offset(horizontalDistance * std::sin(yawRadians_),
                           distance_ * std::sin(pitchRadians_),
                           horizontalDistance * std::cos(yawRadians_));
    return target_ + offset;
}

const glm::vec3& EditorCamera::target() const noexcept {
    return target_;
}

float EditorCamera::distance() const noexcept {
    return distance_;
}

float EditorCamera::aspectRatio() const noexcept {
    return static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_);
}
float EditorCamera::worldUnitsPerPixel(const glm::vec3& point) const {
    const float depth = -(viewMatrix() * glm::vec4(point, 1)).z;
    return depth > kNearPlane ? 2 * (orthographic_ ? distance_ : depth) *
                                    std::tan(kFieldOfViewRadians * 0.5F) / viewportHeight_
                              : 0;
}
core::CameraState EditorCamera::state() const {
    // 文件保持自由观察姿态，避免把精确顶视写入旧协议。
    return {orbitPosition(), target_, focusRadius_, maximumDistance_};
}
bool EditorCamera::setState(const core::CameraState& state) {
    if (!state.isValid()) {
        return false;
    }
    view_ = EditorView::Orbit;
    orthographic_ = false;
    const auto offset = state.position - state.target;
    distance_ = glm::length(offset);
    target_ = state.target;
    yawRadians_ = std::atan2(offset.x, offset.z);
    pitchRadians_ = std::asin(offset.y / distance_);
    focusRadius_ = state.focusRadius;
    maximumDistance_ = state.maximumDistance;
    return true;
}

} // namespace mini3d::renderer_gl
