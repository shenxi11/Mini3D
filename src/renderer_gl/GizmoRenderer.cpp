/*
 * 模块名: GizmoRenderer
 * 功能概述: 生成移动箭头/平面、旋转环和缩放方杆，按统一坐标基绘制。
 * 对外接口: GizmoRenderer
 * 依赖关系: PrimitiveFactory、OpenGL、GLM
 * 输入输出: 手柄原点与逻辑像素对应长度到 GPU 图像。
 * 异常与错误: 创建失败返回 false，生命周期由 Renderer 收口。
 * 维护说明: 不使用宽线或 UI 绘图伪造 Gizmo，不逐帧上传几何。
 */
#include "GizmoRenderer.h"

#include "PrimitiveFactory.h"

#include <QOpenGLFunctions_4_1_Core>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
namespace mini3d::renderer_gl {
bool GizmoRenderer::initialize(QOpenGLFunctions_4_1_Core& functions) {
    functions_ = &functions;
    auto data = PrimitiveFactory::createCube();
    for (auto& vertex : data.vertices) {
        vertex.position =
            vertex.position * glm::vec3(0.04F, 0.67F, 0.04F) + glm::vec3(0, 0.485F, 0);
    }
    const auto base = static_cast<std::uint32_t>(data.vertices.size());
    for (const glm::vec3 point : {glm::vec3(-0.07F, 0.82F, -0.07F),
                                  {0.07F, 0.82F, -0.07F},
                                  {0.07F, 0.82F, 0.07F},
                                  {-0.07F, 0.82F, 0.07F},
                                  {0, 1, 0}}) {
        MeshVertex vertex;
        vertex.position = point;
        data.vertices.push_back(vertex);
    }
    for (std::uint32_t i = 0; i < 4; ++i) {
        data.indices.insert(data.indices.end(), {base + i, base + (i + 1) % 4, base + 4});
    }
    auto scaleData = PrimitiveFactory::createCube();
    for (auto& vertex : scaleData.vertices) {
        vertex.position =
            vertex.position * glm::vec3(0.04F, 0.75F, 0.04F) + glm::vec3(0, 0.525F, 0);
    }
    const auto knobBase = static_cast<std::uint32_t>(scaleData.vertices.size());
    auto knob = PrimitiveFactory::createCube();
    for (auto vertex : knob.vertices) {
        vertex.position = vertex.position * 0.14F + glm::vec3(0, 0.93F, 0);
        scaleData.vertices.push_back(vertex);
    }
    for (const auto index : knob.indices) {
        scaleData.indices.push_back(knobBase + index);
    }
    MeshData ring;
    constexpr unsigned int segments = 64, sides = 8;
    for (unsigned int i = 0; i < segments; ++i) {
        const float angle = glm::two_pi<float>() * i / segments;
        for (unsigned int j = 0; j < sides; ++j) {
            const float tube = glm::two_pi<float>() * j / sides;
            MeshVertex vertex;
            const float radius = 1.0F + 0.025F * std::cos(tube);
            vertex.position = {radius * std::cos(angle), 0.025F * std::sin(tube),
                               radius * std::sin(angle)};
            ring.vertices.push_back(vertex);
            const auto a = i * sides + j, b = ((i + 1) % segments) * sides + j;
            const auto c = ((i + 1) % segments) * sides + (j + 1) % sides,
                       d = i * sides + (j + 1) % sides;
            ring.indices.insert(ring.indices.end(), {a, b, c, a, c, d});
        }
    }
    return shader_.create(functions, QStringLiteral(":/mini3d/shaders/gizmo.vert"),
                          QStringLiteral(":/mini3d/shaders/gizmo.frag")) &&
           arrow_.upload(functions, data) && ring_.upload(functions, ring) &&
           scale_.upload(functions, scaleData) && center_.upload(functions, knob) &&
           plane_.upload(functions, PrimitiveFactory::createPlane());
}
void GizmoRenderer::draw(const GizmoHandle& handle, const glm::mat4& viewProjection,
                         int highlighted, GizmoTool tool) const {
    if (handle.length <= 0) {
        return;
    }
    const auto depth = functions_->glIsEnabled(GL_DEPTH_TEST);
    const auto cull = functions_->glIsEnabled(GL_CULL_FACE);
    GLboolean mask;
    functions_->glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
    functions_->glDisable(GL_DEPTH_TEST);
    functions_->glDisable(GL_CULL_FACE);
    functions_->glDepthMask(GL_FALSE);
    shader_.bind();
    for (int axis = 0; axis < 3; ++axis) {
        auto model = glm::translate(glm::mat4(1), handle.origin) * glm::mat4(handle.basis);
        if (axis == 0) {
            model = glm::rotate(model, -glm::half_pi<float>(), glm::vec3(0, 0, 1));
        }
        if (axis == 2) {
            model = glm::rotate(model, glm::half_pi<float>(), glm::vec3(1, 0, 0));
        }
        model = glm::scale(model, glm::vec3(handle.length));
        glm::vec3 color(0.12F);
        color[axis] = 1;
        if (highlighted == axis) {
            color = {1, 0.85F, 0.1F};
        }
        shader_.setUniformMatrix4("uMvp", viewProjection * model);
        shader_.setUniformVector3("uColor", color);
        if (tool == GizmoTool::Rotate) {
            ring_.draw();
        } else if (tool == GizmoTool::Scale) {
            scale_.draw();
        } else {
            arrow_.draw();
        }
    }
    if (tool == GizmoTool::Move) {
        for (int normal = 0; normal < 3; ++normal) {
            glm::vec3 offset(0.3F);
            offset[normal] = 0;
            auto model = glm::translate(glm::mat4(1), handle.origin) * glm::mat4(handle.basis);
            model = glm::translate(model, offset * handle.length);
            if (normal == 0) {
                model = glm::rotate(model, -glm::half_pi<float>(), glm::vec3(0, 0, 1));
            }
            if (normal == 2) {
                model = glm::rotate(model, glm::half_pi<float>(), glm::vec3(1, 0, 0));
            }
            model = glm::scale(model, glm::vec3(handle.length / 6.0F));
            glm::vec3 color(0.55F);
            color[normal] = 0.1F;
            shader_.setUniformMatrix4("uMvp", viewProjection * model);
            shader_.setUniformVector3(
                "uColor", highlighted == normal + 3 ? glm::vec3(1, 0.85F, 0.1F) : color);
            plane_.draw();
        }
    }
    if (tool == GizmoTool::Scale) {
        const auto model = glm::scale(glm::translate(glm::mat4(1), handle.origin),
                                      glm::vec3(0.2F * handle.length));
        shader_.setUniformMatrix4("uMvp", viewProjection * model);
        shader_.setUniformVector3("uColor",
                                  highlighted == 3 ? glm::vec3(1, 0.85F, 0.1F) : glm::vec3(0.9F));
        center_.draw();
    }
    shader_.release();
    functions_->glDepthMask(mask);
    if (depth) {
        functions_->glEnable(GL_DEPTH_TEST);
    }
    if (cull) {
        functions_->glEnable(GL_CULL_FACE);
    }
}
void GizmoRenderer::destroy() {
    arrow_.destroy();
    ring_.destroy();
    scale_.destroy();
    center_.destroy();
    plane_.destroy();
    shader_.destroy();
    functions_ = nullptr;
}
} // namespace mini3d::renderer_gl
