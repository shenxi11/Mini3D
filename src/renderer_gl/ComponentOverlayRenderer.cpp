/*
 * 模块名: ComponentOverlayRenderer
 * 功能概述: 复用一个动态缓冲绘制真实选区并恢复绘制状态。
 * 对外接口: ComponentOverlayRenderer；依赖关系: 当前 OpenGL Context。
 * 输入输出: revision 感知的世界顶点到半透明面和不透明点线。
 * 异常与错误: 非法资源不绘制，无 Context 不删除；维护说明: 不重挂 QOpenGLWidget。
 */
#include "ComponentOverlayRenderer.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <cstddef>

namespace mini3d::renderer_gl {
ComponentOverlayRenderer::~ComponentOverlayRenderer() {
    destroy();
}
bool ComponentOverlayRenderer::initialize(QOpenGLFunctions_4_1_Core& functions) {
    destroy();
    functions_ = &functions;
    if (!shader_.create(functions, QStringLiteral(":/mini3d/shaders/component.vert"),
                        QStringLiteral(":/mini3d/shaders/component.frag"))) {
        functions_ = nullptr;
        return false;
    }
    functions.glGenVertexArrays(1, &vertexArray_);
    functions.glGenBuffers(1, &vertexBuffer_);
    functions.glBindVertexArray(vertexArray_);
    functions.glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    functions.glEnableVertexAttribArray(0);
    functions.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ComponentOverlayVertex),
                                    nullptr);
    functions.glEnableVertexAttribArray(1);
    functions.glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, sizeof(ComponentOverlayVertex),
        reinterpret_cast<const void*>(offsetof(ComponentOverlayVertex, color)));
    functions.glBindVertexArray(0);
    functions.glBindBuffer(GL_ARRAY_BUFFER, 0);
    return isValid();
}
void ComponentOverlayRenderer::draw(const ComponentOverlay& overlay,
                                    const glm::mat4& viewProjection, float pointSize, bool xRay) {
    if (!isValid()) {
        return;
    }
    functions_->glBindVertexArray(vertexArray_);
    if (revision_ != overlay.revision) {
        auto vertices = overlay.triangles;
        vertices.insert(vertices.end(), overlay.lines.begin(), overlay.lines.end());
        vertices.insert(vertices.end(), overlay.points.begin(), overlay.points.end());
        functions_->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
        functions_->glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(ComponentOverlayVertex),
                                 vertices.data(), GL_DYNAMIC_DRAW);
        functions_->glBindBuffer(GL_ARRAY_BUFFER, 0);
        revision_ = overlay.revision;
    }
    const auto depth = functions_->glIsEnabled(GL_DEPTH_TEST);
    const auto cull = functions_->glIsEnabled(GL_CULL_FACE);
    const auto blend = functions_->glIsEnabled(GL_BLEND);
    const auto programPoint = functions_->glIsEnabled(GL_PROGRAM_POINT_SIZE);
    GLboolean depthMask;
    GLint depthFunction, sourceBlend, destinationBlend;
    GLfloat oldPointSize;
    functions_->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    functions_->glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
    functions_->glGetIntegerv(GL_BLEND_SRC_RGB, &sourceBlend);
    functions_->glGetIntegerv(GL_BLEND_DST_RGB, &destinationBlend);
    functions_->glGetFloatv(GL_POINT_SIZE, &oldPointSize);
    if (xRay)
        functions_->glDisable(GL_DEPTH_TEST);
    else
        functions_->glEnable(GL_DEPTH_TEST);
    functions_->glDepthFunc(GL_LEQUAL);
    functions_->glDepthMask(GL_FALSE);
    functions_->glDisable(GL_CULL_FACE);
    functions_->glEnable(GL_BLEND);
    functions_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    functions_->glDisable(GL_PROGRAM_POINT_SIZE);
    functions_->glPointSize(pointSize);
    shader_.bind();
    shader_.setUniformMatrix4("uViewProjection", viewProjection);
    const auto faces = static_cast<int>(overlay.triangles.size());
    const auto lines = static_cast<int>(overlay.lines.size());
    functions_->glDrawArrays(GL_TRIANGLES, 0, faces);
    functions_->glDrawArrays(GL_LINES, faces, lines);
    functions_->glDrawArrays(GL_POINTS, faces + lines, static_cast<int>(overlay.points.size()));
    shader_.release();
    functions_->glBindVertexArray(0);
    functions_->glPointSize(oldPointSize);
    functions_->glBlendFunc(sourceBlend, destinationBlend);
    functions_->glDepthFunc(depthFunction);
    functions_->glDepthMask(depthMask);
    if (depth)
        functions_->glEnable(GL_DEPTH_TEST);
    else
        functions_->glDisable(GL_DEPTH_TEST);
    if (cull)
        functions_->glEnable(GL_CULL_FACE);
    if (!blend)
        functions_->glDisable(GL_BLEND);
    if (programPoint)
        functions_->glEnable(GL_PROGRAM_POINT_SIZE);
}
void ComponentOverlayRenderer::destroy() {
    if (vertexArray_ || vertexBuffer_) {
        if (!functions_ || !QOpenGLContext::currentContext()) {
            qWarning() << "无法释放组件覆盖层：当前没有 OpenGL 上下文。";
            return;
        }
        functions_->glDeleteBuffers(1, &vertexBuffer_);
        functions_->glDeleteVertexArrays(1, &vertexArray_);
    }
    vertexArray_ = vertexBuffer_ = 0;
    revision_ = 0;
    shader_.destroy();
    functions_ = nullptr;
}
bool ComponentOverlayRenderer::isValid() const {
    return functions_ && shader_.isValid() && vertexArray_ && vertexBuffer_;
}
} // namespace mini3d::renderer_gl
