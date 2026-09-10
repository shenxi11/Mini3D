/*
 * 模块名: GridRenderer
 * 功能概述: 生成 XZ 平面有限网格与 XYZ 彩色轴，并通过 GL_LINES 绘制。
 * 对外接口: mini3d::renderer_gl::GridRenderer
 * 依赖关系: OpenGL 4.1 Core、Qt OpenGL Context、GLM、ShaderProgram
 * 输入输出: 输入当前 Context 和 ViewProjection，输出单次线段 Draw Call。
 * 异常与错误: 无当前 Context 时保留 GPU 句柄并写入警告，避免非法删除。
 * 维护说明: 网格范围和间距是首周视口固定视觉基线，不承担场景数据职责。
 */

#include "GridRenderer.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <cstddef>
#include <glm/vec3.hpp>
#include <vector>

namespace mini3d::renderer_gl {
namespace {

struct GridVertex {
    glm::vec3 position;
    glm::vec3 color;
};

constexpr int kGridHalfExtent = 10;
constexpr float kAxisHeight = 3.0F;

void appendLine(std::vector<GridVertex>& vertices, const glm::vec3& start, const glm::vec3& end,
                const glm::vec3& color) {
    vertices.push_back(GridVertex{start, color});
    vertices.push_back(GridVertex{end, color});
}

[[nodiscard]] std::vector<GridVertex> createGridVertices() {
    const glm::vec3 gridColor(0.24F, 0.28F, 0.34F);
    const glm::vec3 xAxisColor(0.92F, 0.24F, 0.20F);
    const glm::vec3 yAxisColor(0.24F, 0.82F, 0.32F);
    const glm::vec3 zAxisColor(0.24F, 0.48F, 0.96F);
    const float extent = static_cast<float>(kGridHalfExtent);

    std::vector<GridVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(kGridHalfExtent * 8 + 6));

    for (int coordinate = -kGridHalfExtent; coordinate <= kGridHalfExtent; ++coordinate) {
        if (coordinate == 0) {
            continue;
        }

        const float offset = static_cast<float>(coordinate);
        appendLine(vertices, {-extent, 0.0F, offset}, {extent, 0.0F, offset}, gridColor);
        appendLine(vertices, {offset, 0.0F, -extent}, {offset, 0.0F, extent}, gridColor);
    }

    appendLine(vertices, {-extent, 0.0F, 0.0F}, {extent, 0.0F, 0.0F}, xAxisColor);
    appendLine(vertices, {0.0F, 0.0F, 0.0F}, {0.0F, kAxisHeight, 0.0F}, yAxisColor);
    appendLine(vertices, {0.0F, 0.0F, -extent}, {0.0F, 0.0F, extent}, zAxisColor);
    return vertices;
}

} // namespace

GridRenderer::~GridRenderer() {
    destroy();
}

bool GridRenderer::initialize(QOpenGLFunctions_4_1_Core& functions) {
    destroy();
    functions_ = &functions;

    const bool shaderCreated =
        shader_.create(functions, QStringLiteral(":/mini3d/shaders/grid.vert"),
                       QStringLiteral(":/mini3d/shaders/grid.frag"));
    if (!shaderCreated) {
        functions_ = nullptr;
        return false;
    }

    const std::vector<GridVertex> vertices = createGridVertices();
    vertexCount_ = static_cast<int>(vertices.size());
    functions_->glGenVertexArrays(1, &vertexArray_);
    functions_->glGenBuffers(1, &vertexBuffer_);
    functions_->glBindVertexArray(vertexArray_);
    functions_->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    functions_->glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(vertices.size() * sizeof(GridVertex)),
                             vertices.data(), GL_STATIC_DRAW);

    constexpr int kPositionAttribute = 0;
    constexpr int kColorAttribute = 1;
    const int vertexStride = static_cast<int>(sizeof(GridVertex));
    functions_->glEnableVertexAttribArray(kPositionAttribute);
    functions_->glVertexAttribPointer(
        kPositionAttribute, 3, GL_FLOAT, GL_FALSE, vertexStride,
        reinterpret_cast<const void*>(offsetof(GridVertex, position)));
    functions_->glEnableVertexAttribArray(kColorAttribute);
    functions_->glVertexAttribPointer(kColorAttribute, 3, GL_FLOAT, GL_FALSE, vertexStride,
                                      reinterpret_cast<const void*>(offsetof(GridVertex, color)));
    functions_->glBindVertexArray(0);
    functions_->glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (!isValid()) {
        destroy();
        return false;
    }
    return true;
}

void GridRenderer::draw(const glm::mat4& viewProjection) const {
    if (!isValid()) {
        return;
    }

    shader_.bind();
    shader_.setUniformMatrix4("uViewProjection", viewProjection);
    functions_->glLineWidth(1.0F);
    functions_->glBindVertexArray(vertexArray_);
    functions_->glDrawArrays(GL_LINES, 0, vertexCount_);
    functions_->glBindVertexArray(0);
    shader_.release();
}

void GridRenderer::destroy() {
    if (vertexArray_ == 0 && vertexBuffer_ == 0) {
        shader_.destroy();
        functions_ = nullptr;
        vertexCount_ = 0;
        return;
    }

    if (functions_ == nullptr || QOpenGLContext::currentContext() == nullptr) {
        qWarning() << "无法释放网格线渲染资源：当前没有 OpenGL 上下文。";
        return;
    }

    if (vertexBuffer_ != 0) {
        functions_->glDeleteBuffers(1, &vertexBuffer_);
        vertexBuffer_ = 0;
    }
    if (vertexArray_ != 0) {
        functions_->glDeleteVertexArrays(1, &vertexArray_);
        vertexArray_ = 0;
    }

    shader_.destroy();
    functions_ = nullptr;
    vertexCount_ = 0;
}

bool GridRenderer::isValid() const noexcept {
    return functions_ != nullptr && shader_.isValid() && vertexArray_ != 0 && vertexBuffer_ != 0 &&
           vertexCount_ > 0;
}

} // namespace mini3d::renderer_gl
