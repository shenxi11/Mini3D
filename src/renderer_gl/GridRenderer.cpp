/*
 * 模块名: GridRenderer
 * 功能概述: 用屏幕三角形绘制无限 XZ 网格，保留独立的 Y 轴线。
 * 对外接口: mini3d::renderer_gl::GridRenderer
 * 依赖关系: OpenGL 4.1 Core、Qt OpenGL Context、GLM、ShaderProgram
 * 输入输出: 输入当前 Context 和相机，输出地面像素与轴线两个 Draw Call。
 * 异常与错误: 无当前 Context 时保留 GPU 句柄并写入警告，避免非法删除。
 * 维护说明: 网格只作观察参考，不参与场景数据、拾取或存档。
 */

#include "GridRenderer.h"

#include "EditorCamera.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <cstddef>
#include <array>
#include <cmath>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace mini3d::renderer_gl {
namespace {

struct GridVertex {
    glm::vec3 position;
    glm::vec3 color;
};

constexpr float kAxisHeight = 3.0F;

} // namespace

GridRenderer::~GridRenderer() {
    destroy();
}

bool GridRenderer::initialize(QOpenGLFunctions_4_1_Core& functions) {
    destroy();
    functions_ = &functions;

    const bool shaderCreated =
        shader_.create(functions, QStringLiteral(":/mini3d/shaders/grid.vert"),
                       QStringLiteral(":/mini3d/shaders/grid.frag")) &&
        planeShader_.create(functions, QStringLiteral(":/mini3d/shaders/infinite_grid.vert"),
                            QStringLiteral(":/mini3d/shaders/infinite_grid.frag"));
    if (!shaderCreated) {
        destroy();
        return false;
    }

    const std::array<GridVertex, 2> vertices{{{{0, 0, 0}, {.24F, .82F, .32F}},
                                           {{0, kAxisHeight, 0}, {.24F, .82F, .32F}}}};
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

void GridRenderer::draw(const EditorCamera& camera) const {
    if (!isValid()) {
        return;
    }

    // 相机附近的网格原点避免大坐标在逆投影和周期函数中丢失小数。
    const double level = std::log10(std::max(camera.worldUnitsPerPixel(camera.target()) * 40.0,
                                            1.0e-6));
    const double spacing = std::pow(10.0, std::floor(level));
    const glm::vec3 origin(std::floor(camera.target().x / spacing) * spacing, 0,
                            std::floor(camera.target().z / spacing) * spacing);
    auto relativeView = camera.viewMatrix();
    relativeView[3] = glm::vec4(glm::mat3(relativeView) *
                                  (origin - camera.position()), 1);
    const auto relativeProjection = camera.projectionMatrix() * relativeView;
    const float farPlane = std::max(100.0F, camera.distance() + 2 * camera.state().focusRadius);
    planeShader_.bind();
    planeShader_.setUniformMatrix4("uInverseViewProjection",
                                   glm::mat4(glm::inverse(glm::dmat4(relativeProjection))));
    planeShader_.setUniformMatrix4("uViewProjection", relativeProjection);
    planeShader_.setUniformVector3("uEye", camera.position() - origin);
    planeShader_.setUniformVector3("uOrigin", origin);
    planeShader_.setUniformVector3("uGridPhase", glm::vec3(
        std::fmod(static_cast<double>(origin.x), spacing * 100), 0,
        std::fmod(static_cast<double>(origin.z), spacing * 100)));
    planeShader_.setUniformVector3("uGridParameters",
                                   {static_cast<float>(spacing), farPlane,
                                    static_cast<float>(level - std::floor(level))});
    functions_->glEnable(GL_BLEND);
    functions_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    functions_->glDepthMask(GL_FALSE);
    functions_->glBindVertexArray(vertexArray_);
    functions_->glDrawArrays(GL_TRIANGLES, 0, 3);
    planeShader_.release();

    shader_.bind();
    shader_.setUniformMatrix4("uViewProjection", camera.viewProjectionMatrix());
    functions_->glLineWidth(1.0F);
    functions_->glBindVertexArray(vertexArray_);
    functions_->glDrawArrays(GL_LINES, 0, vertexCount_);
    functions_->glBindVertexArray(0);
    shader_.release();
    functions_->glDepthMask(GL_TRUE);
    functions_->glDisable(GL_BLEND);
}

void GridRenderer::destroy() {
    if (vertexArray_ == 0 && vertexBuffer_ == 0) {
        shader_.destroy();
        planeShader_.destroy();
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
    planeShader_.destroy();
    functions_ = nullptr;
    vertexCount_ = 0;
}

bool GridRenderer::isValid() const noexcept {
    return functions_ != nullptr && shader_.isValid() && planeShader_.isValid() &&
           vertexArray_ != 0 && vertexBuffer_ != 0 &&
           vertexCount_ > 0;
}

} // namespace mini3d::renderer_gl
