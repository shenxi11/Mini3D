/*
 * 模块名: ComponentOverlayRenderer
 * 功能概述: 绘制由编辑上下文提供的源网格选区，不读取或修改编辑器选择模型。
 * 对外接口: ComponentOverlay、ComponentOverlayRenderer
 * 依赖关系: ShaderProgram、GLM、OpenGL 4.1 Core。
 * 输入输出: 世界空间彩色面/线/点到深度检查覆盖层。
 * 异常与错误: 资源创建失败返回 false；维护说明: 上传/销毁仅在当前 Context。
 */
#pragma once

#include "ShaderProgram.h"

#include <cstdint>
#include <glm/vec4.hpp>
#include <vector>

namespace mini3d::renderer_gl {
struct ComponentOverlayVertex {
    glm::vec3 position;
    glm::vec4 color;
};
/** @brief 只读显示输入，不携带选择身份；revision 由 Viewport 每次接收新数据递增。 */
struct ComponentOverlay {
    std::vector<ComponentOverlayVertex> triangles, lines, points;
    std::uint64_t revision = 0;
};
/** @brief 单 Context 覆盖层资源；相机导航只更新矩阵，不重新上传选区缓冲。 */
class ComponentOverlayRenderer final {
  public:
    ComponentOverlayRenderer() = default;
    ~ComponentOverlayRenderer();
    ComponentOverlayRenderer(const ComponentOverlayRenderer&) = delete;
    ComponentOverlayRenderer& operator=(const ComponentOverlayRenderer&) = delete;
    [[nodiscard]] bool initialize(QOpenGLFunctions_4_1_Core& functions);
    void draw(const ComponentOverlay& overlay, const glm::mat4& viewProjection, float pointSize,
              bool xRay = false);
    void destroy();
    [[nodiscard]] bool isValid() const;

  private:
    QOpenGLFunctions_4_1_Core* functions_ = nullptr;
    ShaderProgram shader_;
    unsigned int vertexArray_ = 0, vertexBuffer_ = 0;
    std::uint64_t revision_ = 0;
};
} // namespace mini3d::renderer_gl
