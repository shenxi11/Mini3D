/*
 * 模块名: GizmoRenderer
 * 功能概述: 用共享箭头几何绘制世界 XYZ 移动手柄。
 * 对外接口: initialize、draw、destroy
 * 依赖关系: GpuMesh、ShaderProgram、GizmoHandle
 * 输入输出: 手柄尺寸/位置到彩色轴覆盖层。
 * 异常与错误: GPU 初始化失败返回 false。
 * 维护说明: 资源归 Renderer 拥有，必须在当前 Context 中释放。
 */
#pragma once
#include "GizmoController.h"
#include "GpuMesh.h"
#include "ShaderProgram.h"
namespace mini3d::renderer_gl {
class GizmoRenderer final {
  public:
    /** @brief 创建一次箭头网格和纯色 Shader。 */
    bool initialize(QOpenGLFunctions_4_1_Core& functions);
    /** @brief 绘制三轴，hover/active 轴用黄色；恢复深度状态。 */
    void draw(const GizmoHandle& handle, const glm::mat4& viewProjection, int highlighted,
              GizmoTool tool = GizmoTool::Move) const;
    void destroy();

  private:
    QOpenGLFunctions_4_1_Core* functions_ = nullptr;
    GpuMesh arrow_;
    GpuMesh ring_;
    GpuMesh scale_;
    GpuMesh center_;
    GpuMesh plane_;
    ShaderProgram shader_;
};
} // namespace mini3d::renderer_gl
