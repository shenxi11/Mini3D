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
#include <cmath>
#include <glm/matrix.hpp>

namespace mini3d::renderer_gl {
namespace {

constexpr float kClearRed = 59.0F / 255.0F;
constexpr float kClearGreen = 59.0F / 255.0F;
constexpr float kClearBlue = 59.0F / 255.0F;
constexpr float kClearAlpha = 1.0F;

bool finiteMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                return false;
    return true;
}

bool hasSameVisibility(const core::ViewportVisibility& first,
                       const core::ViewportVisibility& second) {
    return first.hiddenObjects == second.hiddenObjects && first.localRoot == second.localRoot &&
           first.editedEntity == second.editedEntity && first.vertices == second.vertices &&
           first.edges == second.edges && first.faces == second.faces;
}

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
    const bool resourcesCreated =
        cubeMesh_.upload(functions, PrimitiveFactory::createCube()) &&
        sphereMesh_.upload(functions, PrimitiveFactory::createSphere()) &&
        planeMesh_.upload(functions, PrimitiveFactory::createPlane()) &&
        gridRenderer_.initialize(functions) && selectionRenderer_.initialize(functions) &&
        componentOverlayRenderer_.initialize(functions) && gizmoRenderer_.initialize(functions) &&
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
void Renderer::setCamera(const EditorCamera& camera) {
    camera_ = camera;
}
RenderView Renderer::renderView(const core::Scene& scene, core::EntityId previewCamera,
                                const InstalledPose* pose) const {
    RenderView result;
    if (pose && (!pose->numerics || !pose->geometry || !pose->geometry->assets ||
                 pose->numerics->nodes.size() != pose->geometry->entries.size())) {
        result.valid = false;
        return result;
    }
    result.preset = camera_.view();
    result.orthographic = camera_.isOrthographic();
    result.position = camera_.position();
    result.target = camera_.target();
    result.viewMatrix = camera_.viewMatrix();
    result.projectionMatrix = camera_.projectionMatrix();
    const auto cameraView = ScenePoseView(scene, pose).cameraView(previewCamera, aspect_);
    if (cameraView) {
        result.previewCamera = previewCamera;
        result.orthographic = false;
        result.position = cameraView->position;
        result.target = cameraView->target;
        result.viewMatrix = cameraView->viewMatrix;
        result.projectionMatrix = cameraView->projectionMatrix;
        result.forward = cameraView->forward;
        result.up = cameraView->up;
        return result;
    }
    if (pose && previewCamera != core::kInvalidEntity) {
        result.valid = false;
        return result;
    }
    const auto basis = glm::inverse(result.viewMatrix);
    result.forward = -glm::normalize(glm::vec3(basis[2]));
    result.up = glm::normalize(glm::vec3(basis[1]));
    return result;
}

bool Renderer::focusEntity(const core::Scene& scene, const assets::AssetManager& assets,
                           core::EntityId id, const InstalledPose* pose) {
    return camera_.focus(RayCaster::worldBounds(scene, assets, id, visibility_, pose));
}
bool Renderer::focusEntities(const core::Scene& scene, const assets::AssetManager& assets,
                             const std::vector<core::EntityId>& ids,
                             const std::function<bool()>& beforeCommit,
                             const InstalledPose* pose) {
    core::Aabb bounds;
    for (const auto id : ids) {
        const auto entityBounds = RayCaster::worldBounds(scene, assets, id, visibility_, pose);
        if (entityBounds.isValid()) {
            bounds.expand(entityBounds.minimum);
            bounds.expand(entityBounds.maximum);
        }
    }
    auto candidate = camera_;
    if (!candidate.focus(bounds) || !candidate.state().isValid() ||
        !finiteMatrix(candidate.viewMatrix()) || !finiteMatrix(candidate.projectionMatrix()) ||
        (beforeCommit && !beforeCommit()))
        return false;
    camera_ = candidate;
    return true;
}

bool Renderer::focusScene(const core::Scene& scene, const assets::AssetManager& assets,
                          const InstalledPose* pose) {
    return camera_.focus(RayCaster::sceneBounds(scene, assets, visibility_, pose));
}
void Renderer::setViewportVisibility(const core::ViewportVisibility& visibility) {
    visibility_ = visibility;
}
void Renderer::setShadingMode(ViewportShading mode) {
    shadingMode_ = mode;
}

RenderStatus Renderer::render(const core::Scene& scene, const assets::AssetManager& assets,
                      core::EntityId selected, bool moveTool, int axis,
                      core::EntityId previewCamera, GizmoTool tool, GizmoSpace space,
                      const ComponentOverlay* components, float pointSize, bool overlays, bool xRay,
                      core::EntityId previewEntity, const core::EditableMeshRecord* editablePreview,
                      std::optional<glm::vec3> transformPivot, const InstalledPose* pose) {
    if (!isInitialized()) {
        return {false, QStringLiteral("渲染器资源未就绪。")};
    }
    const auto view = renderView(scene, previewCamera, pose);
    const auto light = ScenePoseView(scene, pose).lighting();
    if (!view.valid || !light)
        return {false, QStringLiteral("当前显示姿态或预览相机不完整。")};
    renderStatus_ = {true, {}};

    functions_->glEnable(GL_DEPTH_TEST);
    functions_->glDepthFunc(GL_LESS);
    functions_->glDepthMask(GL_TRUE);
    functions_->glEnable(GL_CULL_FACE);
    functions_->glCullFace(GL_BACK);
    functions_->glFrontFace(GL_CCW);
    functions_->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    functions_->glDisable(GL_BLEND);

    functions_->glClearColor(kClearRed, kClearGreen, kClearBlue, kClearAlpha);
    functions_->glClearDepth(1.0);
    functions_->glClearStencil(0);
    functions_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    const glm::mat4 viewProjection = view.projectionMatrix * view.viewMatrix;
    meshShader_.bind();
    meshShader_.setUniformMatrix4("uViewProjection", viewProjection);
    meshShader_.setUniformInt("uBaseColorTexture", 0);
    meshShader_.setUniformVector3("uLightDirection", light->direction);
    meshShader_.setUniformVector3("uLightColor", light->color * light->intensity);
    meshShader_.setUniformVector3("uAmbient", glm::vec3(light->ambient));

    functions_->glPolygonMode(GL_FRONT_AND_BACK,
                              shadingMode_ == ViewportShading::Wireframe ? GL_LINE : GL_FILL);
    if (pose) {
        for (const auto& node : pose->numerics->nodes)
            drawNode(scene, node.entity, glm::mat4(1), *pose->geometry->assets, previewEntity,
                     editablePreview, pose);
    } else {
        for (const auto root : scene.roots())
            drawNode(scene, root, glm::mat4(1), assets, previewEntity, editablePreview);
    }
    // 线框只影响网格 pass；覆盖层中的三角形、手柄和后续 QPainter 仍使用填充。
    functions_->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    functions_->glFrontFace(GL_CCW);
    functions_->glBindTexture(GL_TEXTURE_2D, 0);
    meshShader_.release();
    if (view.previewCamera == 0 && overlays) {
        gridRenderer_.draw(camera_);
        if (components) {
            componentOverlayRenderer_.draw(*components, viewProjection, pointSize, xRay);
        } else {
            selectionRenderer_.draw(RayCaster::worldBounds(scene, assets, selected, visibility_, pose),
                                    viewProjection);
        }
        if (moveTool && !components) {
            gizmoRenderer_.draw(gizmoHandle(scene, selected, space, transformPivot, pose),
                                viewProjection, axis, tool);
        }
    }
    const auto error = functions_->glGetError();
    if (error != GL_NO_ERROR)
        recordRenderFailure(QStringLiteral("本帧 OpenGL 绘制失败：%1").arg(error));
    return renderStatus_;
}
void Renderer::recordRenderFailure(const QString& message) {
    if (renderStatus_.ready)
        renderStatus_ = {false, message};
}
GizmoHandle Renderer::gizmoHandle(const core::Scene& scene, core::EntityId id, GizmoSpace space,
                                  std::optional<glm::vec3> pivot,
                                  const InstalledPose* pose) const {
    const ScenePoseView poseView(scene, pose);
    if (!(pose ? poseView.isVisible(id) : visibility_.isVisible(scene, id))) {
        return {};
    }
    const auto world = poseView.worldMatrix(id);
    const auto rotation = poseView.worldRotation(id);
    if (!world || !rotation)
        return {};
    const auto origin = pivot.value_or(glm::vec3((*world)[3]));
    return {origin, camera_.worldUnitsPerPixel(origin) * 90.0F,
            space == GizmoSpace::Local ? glm::mat3_cast(*rotation) : glm::mat3(1),
            space, glm::vec3(glm::inverse(camera_.viewMatrix())[0])};
}

void Renderer::drawNode(const core::Scene& scene, core::EntityId id, const glm::mat4& parentWorld,
                        const assets::AssetManager& assets, core::EntityId previewEntity,
                        const core::EditableMeshRecord* editablePreview,
                        const InstalledPose* pose) {
    const auto* node = scene.find(id);
    if (!node) {
        recordRenderFailure(QStringLiteral("显示对象不存在：%1").arg(id));
        return;
    }
    const PoseGeometryEntry* geometry = nullptr;
    const core::EvaluatedPoseNode* evaluated = nullptr;
    if (pose) {
        const auto entry = pose->geometry->entries.find(id);
        evaluated = pose->numerics->find(id);
        if (entry == pose->geometry->entries.end() || !evaluated) {
            recordRenderFailure(QStringLiteral("显示姿态缺少对象：%1").arg(id));
            return;
        }
        geometry = &entry->second;
        if (!geometry->visible)
            return;
    } else if (!node->visible) {
        return;
    }
    const glm::mat4 world =
        evaluated ? evaluated->world : parentWorld * node->transform.localMatrix();
    if (pose || visibility_.isVisible(scene, id)) {
        meshShader_.setUniformMatrix4("uModel", world);
        // 奇数次镜像翻转绕序，保持负缩放后的正面可见。
        functions_->glFrontFace(glm::determinant(glm::dmat3(world)) < 0.0 ? GL_CW : GL_CCW);
        if (node->meshRenderer) {
            drawImportedMesh(*node->meshRenderer, assets, node->surface);
        }
        if (node->editableMesh != 0) {
            if (geometry) {
                if (geometry->content) {
                    const core::EditableMeshRecord record{geometry->content, 0, 0,
                                                          geometry->evaluationRevision};
                    drawEditableMesh(id, node->editableMesh, record, node->surface,
                                     pose->geometry->visibility);
                } else {
                    recordRenderFailure(QStringLiteral("显示姿态缺少可编辑几何：%1").arg(id));
                }
            } else {
                drawEditableMesh(id, node->editableMesh,
                                 id == previewEntity && editablePreview
                                     ? *editablePreview
                                     : *scene.editableMesh(node->editableMesh),
                                 node->surface, visibility_);
            }
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
    }
    if (!pose)
        for (const auto child : node->children)
            drawNode(scene, child, world, assets, previewEntity, editablePreview);
}

void Renderer::applyMaterial(const Material& material, const core::SurfaceStyle& surface) {
    if (material.doubleSided) {
        functions_->glDisable(GL_CULL_FACE);
    } else {
        functions_->glEnable(GL_CULL_FACE);
    }
    const bool materialMode = shadingMode_ == ViewportShading::Material;
    const bool hasTexture = materialMode && surface.useTexture &&
                            material.baseColorTexture != nullptr &&
                            material.baseColorTexture->isValid();
    meshShader_.setUniformVector3("uBaseColor", materialMode ? material.baseColor * surface.tint
                                                             : glm::vec3(.72F));
    meshShader_.setUniformInt(
        "uUseVertexColor",
        materialMode && material.useVertexColor && surface.useVertexColor ? 1 : 0);
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
        recordRenderFailure(QStringLiteral("导入网格 GPU 资源不可用：%1").arg(component.mesh));
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
            if (shadingMode_ == ViewportShading::Material && surface.useTexture &&
                (!material.baseColorTexture || !material.baseColorTexture->isValid()))
                recordRenderFailure(QStringLiteral("导入纹理 GPU 资源不可用：%1")
                                        .arg(source->baseColorTexture));
        }
    } else if (component.material != 0) {
        recordRenderFailure(QStringLiteral("导入材质不可用：%1").arg(component.material));
    }
    applyMaterial(material, surface);
    meshEntry->second->draw();
}

void Renderer::drawEditableMesh(core::EntityId entity, core::MeshId id,
                                const core::EditableMeshRecord& record,
                                const core::SurfaceStyle& surface,
                                const core::ViewportVisibility& visibility) {
    auto& cached = editableMeshes_[id];
    if (cached.revision != record.evaluationRevision || cached.content != record.content ||
        !hasSameVisibility(cached.visibility, visibility)) {
        std::unique_ptr<GpuMesh> mesh;
        bool ready = true;
        auto data = record.content->displayedDerived().mesh;
        if (entity == visibility.editedEntity && visibility.hasHiddenElements()) {
            decltype(data.indices) indices;
            for (std::size_t i = 0; i < data.indices.size(); i += 3) {
                if (!visibility.isTriangleVisible(entity, *record.content, i / 3))
                    continue;
                indices.insert(indices.end(), data.indices.begin() + static_cast<std::ptrdiff_t>(i),
                               data.indices.begin() + static_cast<std::ptrdiff_t>(i + 3));
            }
            data.indices = std::move(indices);
        }
        if (!data.indices.empty()) {
            mesh = std::make_unique<GpuMesh>();
            if (!mesh->upload(*functions_, data)) {
                qWarning() << "可编辑网格上传至 GPU 失败：" << id;
                mesh.reset();
                ready = false;
            } else {
                ++editableMeshUploadCount_;
            }
        }
        cached = {record.content, record.evaluationRevision, visibility, std::move(mesh),
                  ready};
    }
    if (!cached.ready)
        recordRenderFailure(QStringLiteral("可编辑网格 GPU 资源不可用：%1").arg(id));
    if (cached.mesh) {
        applyMaterial(cubeMaterial_, surface);
        cached.mesh->draw();
    }
}

void Renderer::clearImportedResources() {
    editableMeshes_.clear();
    importedMeshes_.clear();
    importedTextures_.clear();
}

void Renderer::destroy() {
    clearImportedResources();
    selectionRenderer_.destroy();
    componentOverlayRenderer_.destroy();
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
           selectionRenderer_.isValid() && componentOverlayRenderer_.isValid() &&
           checkerTexture_.isValid() && whiteTexture_.isValid();
}

std::uint64_t Renderer::editableMeshUploadCount() const noexcept {
    return editableMeshUploadCount_;
}

} // namespace mini3d::renderer_gl
