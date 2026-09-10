/*
 * 模块名: SelectionRenderer
 * 功能概述: 复用线段 Shader，以单位盒缩放和平移绘制世界包围盒。
 * 对外接口: SelectionRenderer
 * 依赖关系: OpenGL Context、GLM、Core
 * 输入输出: 世界包围范围到十二条橙色线段，不改写深度缓冲。
 * 异常与错误: 创建错误返回 false，缺失当前 Context 时保留句柄并报告。
 * 维护说明: 覆盖层不被模型遮挡；不使用宽线、额外 FBO 或全屏描边。
 */
#include "SelectionRenderer.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <array>
#include <cstddef>
#include <glm/ext/matrix_transform.hpp>

namespace mini3d::renderer_gl {
SelectionRenderer::~SelectionRenderer() {
    destroy();
}
bool SelectionRenderer::initialize(QOpenGLFunctions_4_1_Core& functions) {
    destroy();
    functions_ = &functions;
    if (!shader_.create(functions, QStringLiteral(":/mini3d/shaders/grid.vert"),
                        QStringLiteral(":/mini3d/shaders/grid.frag"))) {
        functions_ = nullptr;
        return false;
    }
    struct Vertex {
        glm::vec3 position;
        glm::vec3 color{1.0F, 0.65F, 0.05F};
    };
    std::array<Vertex, 24> vertices;
    std::size_t next = 0;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 start((corner & 1) ? 1.0F : 0.0F, (corner & 2) ? 1.0F : 0.0F,
                              (corner & 4) ? 1.0F : 0.0F);
        for (int axis = 0; axis < 3; ++axis) {
            if ((corner & (1 << axis)) == 0) {
                auto end = start;
                end[axis] = 1;
                vertices[next++].position = start;
                vertices[next++].position = end;
            }
        }
    }
    functions.glGenVertexArrays(1, &vertexArray_);
    functions.glGenBuffers(1, &vertexBuffer_);
    functions.glBindVertexArray(vertexArray_);
    functions.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    functions.glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);
    functions.glEnableVertexAttribArray(0);
    functions.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, static_cast<int>(sizeof(Vertex)),
                                    nullptr);
    functions.glEnableVertexAttribArray(1);
    functions.glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, static_cast<int>(sizeof(Vertex)),
                                    reinterpret_cast<const void*>(offsetof(Vertex, color)));
    functions.glBindVertexArray(0);
    functions.glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!isValid()) {
        destroy();
        return false;
    }
    return true;
}
void SelectionRenderer::draw(const core::Aabb& bounds, const glm::mat4& viewProjection) const {
    if (!isValid() || !bounds.isValid()) {
        return;
    }
    const auto model =
        glm::scale(glm::translate(glm::mat4(1), bounds.minimum), bounds.maximum - bounds.minimum);
    const auto depthTest = functions_->glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthMask = GL_TRUE;
    functions_->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    functions_->glDisable(GL_DEPTH_TEST);
    functions_->glDepthMask(GL_FALSE);
    shader_.bind();
    shader_.setUniformMatrix4("uViewProjection", viewProjection * model);
    functions_->glLineWidth(1.0F);
    functions_->glBindVertexArray(vertexArray_);
    functions_->glDrawArrays(GL_LINES, 0, 24);
    functions_->glBindVertexArray(0);
    shader_.release();
    functions_->glDepthMask(depthMask);
    if (depthTest) {
        functions_->glEnable(GL_DEPTH_TEST);
    }
}
void SelectionRenderer::destroy() {
    if (vertexArray_ != 0 || vertexBuffer_ != 0) {
        if (functions_ == nullptr || QOpenGLContext::currentContext() == nullptr) {
            qWarning() << "无法释放选中轮廓渲染资源：当前没有 OpenGL 上下文。";
            return;
        }
        functions_->glDeleteBuffers(1, &vertexBuffer_);
        functions_->glDeleteVertexArrays(1, &vertexArray_);
    }
    vertexArray_ = 0;
    vertexBuffer_ = 0;
    shader_.destroy();
    functions_ = nullptr;
}
bool SelectionRenderer::isValid() const noexcept {
    return functions_ != nullptr && shader_.isValid() && vertexArray_ != 0 && vertexBuffer_ != 0;
}
} // namespace mini3d::renderer_gl
