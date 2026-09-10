/*
 * 模块名: GizmoController
 * 功能概述: 移动、旋转、缩放手柄的拾取、局部/世界变换与吸附，不修改场景。
 * 对外接口: GizmoHandle、GizmoController
 * 依赖关系: Core Ray/Transform、GLM
 * 输入输出: 世界射线和初始变换到局部 TRS 预览。
 * 异常与错误: 近乎平行、背向平面或无效结果拒绝。
 * 维护说明: 按下时冻结轴、平面与父矩阵，拖动不累计误差。
 */
#pragma once
#include "core/Ray.h"
#include "core/Transform.h"
namespace mini3d::renderer_gl {
enum class GizmoTool { None, Move, Rotate, Scale };
enum class GizmoSpace { World, Local };
struct GizmoHandle {
    glm::vec3 origin{0};
    float length = 0;
    glm::mat3 basis{1};
    GizmoSpace space = GizmoSpace::World;
    glm::vec3 viewRight{1, 0, 0};
};
/** @brief 纯 CPU 手柄拖动状态；-1 表示未激活。 */
class GizmoController final {
  public:
    /** @brief 轴返回 0/1/2，移动平面返回 3/4/5，等比缩放返回 3；未命中返回 -1。 */
    [[nodiscard]] static int pickAxis(const core::Ray& ray, const GizmoHandle& handle,
                                      GizmoTool tool = GizmoTool::Move);
    /** @brief 建立冻结拖动平面；失败不启动拖动。 */
    bool begin(const core::Ray& ray, int axis, const GizmoHandle& handle,
               const glm::mat4& parentWorld, const core::Transform& before,
               GizmoTool tool = GizmoTool::Move);
    /** @brief 计算相对按下点的预览；失败不改写输出。 */
    bool preview(const core::Ray& ray, core::Transform& result, bool snap = false) const;
    void end();
    [[nodiscard]] int activeAxis() const;

  private:
    int axis_ = -1;
    glm::vec3 origin_{0}, normal_{0}, axisDirection_{0};
    float start_ = 0;
    float length_ = 1;
    glm::vec3 startDirection_{0};
    glm::vec3 startPoint_{0};
    glm::mat3 basis_{1};
    GizmoTool tool_ = GizmoTool::Move;
    GizmoSpace space_ = GizmoSpace::World;
    glm::mat4 parentWorld_{1};
    glm::mat4 inverseParent_{1};
    core::Transform before_;
};
} // namespace mini3d::renderer_gl
