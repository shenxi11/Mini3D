/*
 * 模块名: EditorCamera
 * 功能概述: 提供右手坐标系下的透视观察、Orbit、Pan、Zoom 和 Resize 数学。
 * 对外接口: mini3d::renderer_gl::EditorCamera
 * 依赖关系: GLM
 * 输入输出: 输入像素拖动、滚轮步数和 Viewport 尺寸，输出 View/Projection 矩阵。
 * 异常与错误: 无异常；Pitch、距离和最小尺寸在入口处限制到有效范围。
 * 维护说明: 类不依赖 QWidget 或 OpenGL Context，可用于无窗口数学测试。
 */

#pragma once

#include "core/Aabb.h"
#include "core/Ray.h"
#include "core/SceneSerializer.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace mini3d::renderer_gl {

enum class EditorView { Orbit, Front, Right, Top };

/** @brief 围绕目标点工作的编辑器相机；预设和正交仅用于会话查看。 */
class EditorCamera final {
  public:
    EditorCamera() = default;
    void setView(EditorView view);
    [[nodiscard]] EditorView view() const;
    void setOrthographic(bool enabled);
    [[nodiscard]] bool isOrthographic() const;

    /** @brief 更新投影使用的像素尺寸；宽高至少按 1 处理。 */
    void setViewportSize(int width, int height);

    /** @brief 根据鼠标像素位移绕目标点旋转相机。 */
    void orbit(float deltaX, float deltaY);

    /** @brief 根据鼠标像素位移沿相机平面平移目标点。 */
    void pan(float deltaX, float deltaY);

    /** @brief 根据滚轮步数以指数比例调整观察距离。 */
    void zoom(float wheelSteps);

    /** @brief 从左上原点的视口坐标生成世界单位射线；起点在近裁剪面上。 */
    [[nodiscard]] core::Ray screenRay(float x, float y) const;

    /** @brief 保留观察方向并框选世界包围盒；空盒返回 false 且不改变相机。 */
    bool focus(const core::Aabb& bounds);

    /** @brief 返回从相机位置观察目标点的右手 View 矩阵。 */
    [[nodiscard]] glm::mat4 viewMatrix() const;

    /** @brief 返回当前宽高比的透视 Projection 矩阵。 */
    [[nodiscard]] glm::mat4 projectionMatrix() const;

    /** @brief 返回 Projection × View。 */
    [[nodiscard]] glm::mat4 viewProjectionMatrix() const;

    /** @brief 返回由球坐标状态计算出的世界空间相机位置。 */
    [[nodiscard]] glm::vec3 position() const;

    /** @brief 返回 Orbit/Pan 使用的世界空间目标点。 */
    [[nodiscard]] const glm::vec3& target() const noexcept;

    /** @brief 返回相机到目标点的距离。 */
    [[nodiscard]] float distance() const noexcept;

    /** @brief 返回当前 Viewport 宽高比。 */
    [[nodiscard]] float aspectRatio() const noexcept;
    /** @brief 给定世界位置的每逻辑像素世界长度；近面后方返回 0。 */
    [[nodiscard]] float worldUnitsPerPixel(const glm::vec3& point) const;
    [[nodiscard]] core::CameraState state() const;
    /** @brief 恢复已验证的观察参数；非法输入返回 false。 */
    bool setState(const core::CameraState& state);

  private:
    [[nodiscard]] glm::vec3 orbitPosition() const;
    EditorView view_ = EditorView::Orbit;
    bool orthographic_ = false;
    static constexpr float kFieldOfViewRadians = 0.7853981634F;
    static constexpr float kNearPlane = 0.1F;
    static constexpr float kFarPlane = 100.0F;
    static constexpr float kOrbitRadiansPerPixel = 0.008F;
    static constexpr float kMinimumPitch = -1.55334306F;
    static constexpr float kMaximumPitch = 1.55334306F;
    static constexpr float kMinimumDistance = 0.6F;
    static constexpr float kMaximumDistance = 50.0F;
    static constexpr float kZoomExponentPerStep = 0.18F;

    glm::vec3 target_{0.0F, 0.5F, 0.0F};
    float yawRadians_ = 0.65F;
    float pitchRadians_ = 0.38F;
    float distance_ = 6.2F;
    float maximumDistance_ = kMaximumDistance;
    float focusRadius_ = 0.0F;
    int viewportWidth_ = 1;
    int viewportHeight_ = 1;
};

} // namespace mini3d::renderer_gl
