/*
 * 模块名: Renderer
 * 功能概述: 维护编辑器相机，并依次绘制网格、世界轴和三种基础几何。
 * 对外接口: mini3d::renderer_gl::Renderer
 * 依赖关系: OpenGL 4.1 Core、EditorCamera、GridRenderer、ShaderProgram、GpuMesh
 * 输入输出: 输入 Viewport 尺寸和相机交互，输出 Cube、Sphere、Plane 与世界参考线。
 * 异常与错误: 初始化任一步失败都会释放已创建资源并返回 false。
 * 维护说明: 相机只保存 CPU 状态，GPU 资源仍必须在有效 Context 中操作。
 */

#include "Renderer.h"

#include "PrimitiveFactory.h"
#include "RayCaster.h"

#include <QDebug>
#include <QImage>
#include <QOpenGLFunctions_4_1_Core>
#include <algorithm>
#include <glm/ext/matrix_transform.hpp>

namespace mini3d::renderer_gl {
namespace {

constexpr float kClearRed = 0.055F;
constexpr float kClearGreen = 0.071F;
constexpr float kClearBlue = 0.094F;
constexpr float kClearAlpha = 1.0F;

QImage createCheckerImage() {
    constexpr int size = 128;
    constexpr int cellSize = 16;
    QImage image(size, size, QImage::Format_RGBA8888);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool lightCell = ((x / cellSize + y / cellSize) % 2) == 0;
            image.setPixelColor(x, y, lightCell ? QColor(230, 230, 230) : QColor(45, 55, 70));
        }
    }
    // 顶部左红右蓝标记用于区分上下翻转与左右镜像。
    for (int y = 0; y < cellSize; ++y) {
        for (int x = 0; x < cellSize; ++x) {
            image.setPixelColor(x, y, QColor(235, 55, 45));
            image.setPixelColor(size - 1 - x, y, QColor(45, 100, 240));
        }
    }
    return image;
}

} // namespace

Renderer::~Renderer() {
    destroy();
}

bool Renderer::initialize(QOpenGLFunctions_4_1_Core& functions) {
    destroy();
    functions_ = &functions;

    const bool shaderCreated =
        meshShader_.create(functions, QStringLiteral(":/mini3d/shaders/mesh.vert"),
                           QStringLiteral(":/mini3d/shaders/mesh.frag"));
    if (!shaderCreated) {
        functions_ = nullptr;
        return false;
    }

    QImage whiteImage(1, 1, QImage::Format_RGBA8888);
    whiteImage.fill(Qt::white);
    const bool resourcesCreated = cubeMesh_.upload(functions, PrimitiveFactory::createCube()) &&
                                  sphereMesh_.upload(functions, PrimitiveFactory::createSphere()) &&
                                  planeMesh_.upload(functions, PrimitiveFactory::createPlane()) &&
                                  gridRenderer_.initialize(functions) &&
                                  selectionRenderer_.initialize(functions) &&
                                  gizmoRenderer_.initialize(functions) &&
                                  checkerTexture_.upload(functions, createCheckerImage()) &&
                                  whiteTexture_.upload(functions, whiteImage);
    if (!resourcesCreated) {
        destroy();
        return false;
    }

    initialized_ = true;
    qInfo() << "渲染器已初始化：立方体、球体、平面、网格线、XYZ 轴、材质和棋盘格纹理。";
    return true;
}

void Renderer::resize(int width, int height) {
    camera_.setViewportSize(width, height);
    aspect_ = static_cast<float>(std::max(width, 1)) / static_cast<float>(std::max(height, 1));
}

void Renderer::orbitCamera(float deltaX, float deltaY) {
    camera_.orbit(deltaX, deltaY);
}

void Renderer::panCamera(float deltaX, float deltaY) {
    camera_.pan(deltaX, deltaY);
}

void Renderer::zoomCamera(float wheelSteps) {
    camera_.zoom(wheelSteps);
}

const EditorCamera& Renderer::camera() const {
    return camera_;
}
void Renderer::setCameraView(EditorView view) {
    camera_.setView(view);
}
void Renderer::setOrthographic(bool enabled) {
    camera_.setOrthographic(enabled);
}
bool Renderer::setCameraState(const core::CameraState& state) {
    return camera_.setState(state);
}

bool Renderer::focusEntity(const core::Scene& scene, const assets::AssetManager& assets,
                           core::EntityId id) {
    return camera_.focus(RayCaster::worldBounds(scene, assets, id));
}

void Renderer::render(const core::Scene& scene, const assets::AssetManager& assets,
                      core::EntityId selected, bool moveTool, int axis,
                      core::EntityId previewCamera, GizmoTool tool, GizmoSpace space) {
    if (!isInitialized()) {
        return;
    }

    functions_->glEnable(GL_DEPTH_TEST);
    functions_->glDepthFunc(GL_LESS);
    functions_->glDepthMask(GL_TRUE);
    functions_->glEnable(GL_CULL_FACE);
    functions_->glCullFace(GL_BACK);
    functions_->glFrontFace(GL_CCW);
    functions_->glDisable(GL_BLEND);

    functions_->glClearColor(kClearRed, kClearGreen, kClearBlue, kClearAlpha);
    functions_->glClearDepth(1.0);
    functions_->glClearStencil(0);
    functions_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    const auto preview = scene.cameraViewProjection(previewCamera, aspect_);
    const glm::mat4 viewProjection = preview.value_or(camera_.viewProjectionMatrix());
    if (!preview) {
        gridRenderer_.draw(viewProjection);
    }

    meshShader_.bind();
    meshShader_.setUniformMatrix4("uViewProjection", viewProjection);
    meshShader_.setUniformInt("uBaseColorTexture", 0);
    const auto light = scene.effectiveLighting();
    meshShader_.setUniformVector3("uLightDirection", light.direction);
    meshShader_.setUniformVector3("uLightColor", light.color * light.intensity);
    meshShader_.setUniformVector3("uAmbient", glm::vec3(light.ambient));

    for (const auto root : scene.roots()) {
        drawNode(scene, root, glm::mat4(1.0F), assets);
    }
    functions_->glFrontFace(GL_CCW);
    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    meshShader_.release();
    if (!preview) {
        selectionRenderer_.draw(RayCaster::worldBounds(scene, assets, selected), viewProjection);
        if (moveTool) {
            gizmoRenderer_.draw(gizmoHandle(scene, selected, space), viewProjection, axis, tool);
        }
    }
}
GizmoHandle Renderer::gizmoHandle(const core::Scene& scene, core::EntityId id,
                                  GizmoSpace space) const {
    if (!scene.isVisible(id)) {
        return {};
    }
    const auto origin = glm::vec3(scene.worldMatrix(id)[3]);
    return {origin, camera_.worldUnitsPerPixel(origin) * 90.0F,
            space == GizmoSpace::Local ? glm::mat3_cast(scene.worldRotation(id)) : glm::mat3(1),
            space, glm::vec3(glm::inverse(camera_.viewMatrix())[0])};
}

void Renderer::drawNode(const core::Scene& scene, core::EntityId id, const glm::mat4& parentWorld,
                        const assets::AssetManager& assets) {
    const auto* node = scene.find(id);
    if (!node->visible) {
        return;
    }
    const glm::mat4 world = parentWorld * node->transform.localMatrix();
    meshShader_.setUniformMatrix4("uModel", world);
    // 奇数次镜像翻转绕序，保持负缩放后的正面可见。
    functions_->glFrontFace(glm::determinant(glm::mat3(world)) < 0.0F ? GL_CW : GL_CCW);
    if (node->meshRenderer) {
        drawImportedMesh(*node->meshRenderer, assets, node->surface);
    }
    switch (node->primitive) {
        case core::PrimitiveKind::Cube:
            applyMaterial(cubeMaterial_, node->surface);
            cubeMesh_.draw();
            break;
        case core::PrimitiveKind::Sphere:
            applyMaterial(sphereMaterial_, node->surface);
            sphereMesh_.draw();
            break;
        case core::PrimitiveKind::Plane:
            applyMaterial(planeMaterial_, node->surface);
            planeMesh_.draw();
            break;
        case core::PrimitiveKind::Empty:
            break;
    }
    for (const auto child : node->children) {
        drawNode(scene, child, world, assets);
    }
}

void Renderer::applyMaterial(const Material& material, const core::SurfaceStyle& surface) {
    if (material.doubleSided) {
        functions_->glDisable(GL_CULL_FACE);
    } else {
        functions_->glEnable(GL_CULL_FACE);
    }
    const bool hasTexture = surface.useTexture && material.baseColorTexture != nullptr &&
                            material.baseColorTexture->isValid();
    meshShader_.setUniformVector3("uBaseColor", material.baseColor * surface.tint);
    meshShader_.setUniformInt("uUseVertexColor",
                              material.useVertexColor && surface.useVertexColor ? 1 : 0);
    meshShader_.setUniformInt("uUseTexture", hasTexture ? 1 : 0);
    functions_->glActiveTexture(GL_TEXTURE0);
    if (hasTexture) {
        material.baseColorTexture->bind();
    } else {
        whiteTexture_.bind();
    }
}

void Renderer::drawImportedMesh(core::MeshRendererComponent component,
                                const assets::AssetManager& assets,
                                const core::SurfaceStyle& surface) {
    auto [meshEntry, newMesh] = importedMeshes_.try_emplace(component.mesh);
    if (newMesh) {
        const auto* source = assets.mesh(component.mesh);
        auto mesh = std::make_unique<GpuMesh>();
        if (source != nullptr && mesh->upload(*functions_, source->data)) {
            meshEntry->second = std::move(mesh);
        } else {
            qWarning() << "导入网格上传至 GPU 失败：" << component.mesh;
        }
    }
    if (!meshEntry->second) {
        return;
    }
    Material material;
    material.useVertexColor = false;
    if (const auto* source = assets.material(component.material)) {
        material.baseColor = glm::vec3(source->baseColor);
        material.doubleSided = source->doubleSided;
        if (source->baseColorTexture != core::kInvalidAsset) {
            auto [textureEntry, newTexture] =
                importedTextures_.try_emplace(source->baseColorTexture);
            if (newTexture) {
                const auto* pixels = assets.texture(source->baseColorTexture);
                auto texture = std::make_unique<GpuTexture>();
                if (pixels != nullptr &&
                    texture->upload(*functions_, pixels->image, pixels->sampler)) {
                    textureEntry->second = std::move(texture);
                } else {
                    qWarning() << "导入纹理上传至 GPU 失败：" << source->baseColorTexture;
                }
            }
            material.baseColorTexture = textureEntry->second.get();
        }
    }
    applyMaterial(material, surface);
    meshEntry->second->draw();
}

void Renderer::clearImportedResources() {
    importedMeshes_.clear();
    importedTextures_.clear();
}

void Renderer::destroy() {
    clearImportedResources();
    selectionRenderer_.destroy();
    gizmoRenderer_.destroy();
    whiteTexture_.destroy();
    checkerTexture_.destroy();
    gridRenderer_.destroy();
    planeMesh_.destroy();
    sphereMesh_.destroy();
    cubeMesh_.destroy();
    meshShader_.destroy();
    functions_ = nullptr;
    initialized_ = false;
}

bool Renderer::isInitialized() const noexcept {
    return initialized_ && functions_ != nullptr && meshShader_.isValid() && cubeMesh_.isValid() &&
           sphereMesh_.isValid() && planeMesh_.isValid() && gridRenderer_.isValid() &&
           selectionRenderer_.isValid() && checkerTexture_.isValid() && whiteTexture_.isValid();
}

} // namespace mini3d::renderer_gl
