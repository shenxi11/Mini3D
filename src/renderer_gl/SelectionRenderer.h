/*
 * 模块名: SelectionRenderer
 * 功能概述: 用固定单位盒线段绘制选中对象的橙色世界 AABB。
 * 对外接口: initialize、draw、destroy、isValid
 * 依赖关系: ShaderProgram、OpenGL 4.1 Core、Core Aabb
 * 输入输出: 当前 Context、世界盒与相机矩阵到线框覆盖层。
 * 异常与错误: 初始化失败返回 false；空盒不绘制，无 Context 不删除。
 * 维护说明: 独占 VAO/VBO/Shader；固定资源只创建一次，绘制后恢复深度状态。
 */
#pragma once

#include "ShaderProgram.h"
#include "core/Aabb.h"

class QOpenGLFunctions_4_1_Core;
namespace mini3d::renderer_gl {
/** @brief 仅负责选中反馈，不拥有 Scene 或 SelectionModel。 */
class SelectionRenderer final {
  public:
    SelectionRenderer() = default;
    ~SelectionRenderer();
    SelectionRenderer(const SelectionRenderer&) = delete;
    SelectionRenderer& operator=(const SelectionRenderer&) = delete;
    /** @brief 在当前 Context 创建单位盒十二条边，成功返回 true。 */
    [[nodiscard]] bool initialize(QOpenGLFunctions_4_1_Core& functions);
    /** @brief 绘制可透视的橙色 AABB；空盒无副作用。 */
    void draw(const core::Aabb& bounds, const glm::mat4& viewProjection) const;
    /** @brief 在所属当前 Context 删除全部 GPU 资源。 */
    void destroy();
    /** @brief 检查固定线框资源是否已初始化。 */
    [[nodiscard]] bool isValid() const noexcept;

  private:
    QOpenGLFunctions_4_1_Core* functions_ = nullptr;
    ShaderProgram shader_;
    unsigned int vertexArray_ = 0;
    unsigned int vertexBuffer_ = 0;
};
} // namespace mini3d::renderer_gl
