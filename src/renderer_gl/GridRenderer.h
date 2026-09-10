/*
 * 模块名: GridRenderer
 * 功能概述: 管理有限地面网格和 XYZ 世界轴的 OpenGL 线段资源与绘制。
 * 对外接口: mini3d::renderer_gl::GridRenderer
 * 依赖关系: OpenGL 4.1 Core、GLM、ShaderProgram
 * 输入输出: 输入 ViewProjection 矩阵，输出 XZ 地面网格和 XYZ 彩色轴线。
 * 异常与错误: Shader 或 GPU Buffer 初始化失败时返回 false，并保留底层诊断日志。
 * 维护说明: initialize、draw、destroy 必须在所属 Viewport Context 线程调用。
 */

#pragma once

#include "ShaderProgram.h"

#include <glm/mat4x4.hpp>

class QOpenGLFunctions_4_1_Core;

namespace mini3d::renderer_gl {

/** @brief 独占地面网格与世界轴使用的 VAO、VBO 和 Shader。 */
class GridRenderer final {
  public:
    GridRenderer() = default;
    ~GridRenderer();

    GridRenderer(const GridRenderer&) = delete;
    GridRenderer& operator=(const GridRenderer&) = delete;

    /** @brief 在当前 Context 中创建固定网格线段和 Shader 资源。 */
    [[nodiscard]] bool initialize(QOpenGLFunctions_4_1_Core& functions);

    /** @brief 使用给定 ViewProjection 绘制地面网格和 XYZ 轴。 */
    void draw(const glm::mat4& viewProjection) const;

    /** @brief 在当前 Context 中删除 VAO、VBO 和 Shader。 */
    void destroy();

    /** @brief 返回全部线段 GPU 资源是否可用。 */
    [[nodiscard]] bool isValid() const noexcept;

  private:
    QOpenGLFunctions_4_1_Core* functions_ = nullptr;
    ShaderProgram shader_;
    unsigned int vertexArray_ = 0;
    unsigned int vertexBuffer_ = 0;
    int vertexCount_ = 0;
};

} // namespace mini3d::renderer_gl
