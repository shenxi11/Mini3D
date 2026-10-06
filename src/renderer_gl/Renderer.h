/*
 * 模块名: Renderer
 * 功能概述: 组织当前一帧的清屏、相机、网格、世界轴和基础几何绘制。
 * 对外接口: mini3d::renderer_gl::Renderer
 * 依赖关系: OpenGL 4.1 Core、EditorCamera、GridRenderer、ShaderProgram、GpuMesh
 * 输入输出: 输入 Viewport 尺寸与相机交互，输出 Cube、Sphere、Plane 和世界参考线。
 * 异常与错误: Shader 或 GPU Mesh 初始化失败时返回 false，并保留可诊断日志。
 * 维护说明: Renderer 不修改 Scene 状态；全部 GPU 生命周期位于有效 Context 中。
 */

#pragma once

#include "ComponentOverlayRenderer.h"
#include "EditorCamera.h"
#include "GizmoRenderer.h"
#include "GpuMesh.h"
#include "GpuTexture.h"
#include "GridRenderer.h"
#include "Material.h"
#include "SelectionRenderer.h"
#include "ShaderProgram.h"
#include "ViewportShading.h"
#include "assets/AssetManager.h"
#include "core/Scene.h"
#include "core/ViewportVisibility.h"

#include <functional>
#include <memory>
#include <unordered_map>

class QOpenGLFunctions_4_1_Core;

namespace mini3d::renderer_gl {

/** @brief 本次绘制实际采用的观察矩阵；场景相机预览使用同一计算入口。 */
struct RenderView {
    EditorView preset = EditorView::Orbit;
    bool orthographic = false;
    core::EntityId previewCamera = 0;
    glm::vec3 position{0}, target{0}, forward{0, 0, -1}, up{0, 1, 0};
    glm::mat4 viewMatrix{1}, projectionMatrix{1};
};

/** @brief 只报告本帧所用资源的可用性；已知上传失败不能成为成功截图。 */
struct RenderStatus {
    bool ready = false;
    QString error;
};

/** @brief 管理当前最小渲染帧和三种基础几何 GPU 资源。 */
class Renderer final {
  public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /**
     * @brief 在当前 Context 中创建 Shader 和基础几何 GPU Mesh。
     * @param functions 当前 Context 的 OpenGL 4.1 Core 函数表。
     * @return 全部 GPU 资源可用时返回 true。
     */
    [[nodiscard]] bool initialize(QOpenGLFunctions_4_1_Core& functions);

    /** @brief 更新像素尺寸，供透视投影计算宽高比。 */
    void resize(int width, int height);

    /** @brief 根据鼠标像素位移绕观察目标旋转相机。 */
    void orbitCamera(float deltaX, float deltaY);

    /** @brief 根据鼠标像素位移沿相机平面平移观察目标。 */
    void panCamera(float deltaX, float deltaY);

    /** @brief 根据滚轮步数调整相机观察距离。 */
    void zoomCamera(float wheelSteps);

    /** @brief 只读相机用于屏幕射线生成，不暴露 GPU 状态。 */
    [[nodiscard]] const EditorCamera& camera() const;
    bool setCameraState(const core::CameraState& state);
    /** @brief 安装已验证的会话观察候选；不执行 GL 或持久化。 */
    void setCamera(const EditorCamera& camera);
    [[nodiscard]] RenderView renderView(const core::Scene& scene,
                                       core::EntityId previewCamera) const;
    void setCameraView(EditorView view);
    void setOrthographic(bool enabled);
    /** @brief 同步会话掩码，渲染/聚焦/手柄共用，不修改场景。 */
    void setViewportVisibility(const core::ViewportVisibility& visibility);
    /** @brief 仅改变网格显示策略，不改材质、场景、历史或 GL 资源。 */
    void setShadingMode(ViewportShading mode);
    /** @brief 根据可见几何聚焦实体/子树；空盒不改变相机。 */
    bool focusEntity(const core::Scene& scene, const assets::AssetManager& assets,
                     core::EntityId id);
    /** @brief 对显式实体/子树的可见几何并集框景；不读取选择。 */
    bool focusEntities(const core::Scene& scene, const assets::AssetManager& assets,
                       const std::vector<core::EntityId>& ids,
                       const std::function<bool()>& beforeCommit = {});
    /** @brief 以所有可见对象的世界范围框景，不改变观察方向或对象。 */
    bool focusScene(const core::Scene& scene, const assets::AssetManager& assets);

    /** @brief 设置完整帧状态、清屏并绘制 Grid 与三种基础几何。 */
    RenderStatus render(const core::Scene& scene, const assets::AssetManager& assets,
                core::EntityId selected = core::kInvalidEntity, bool moveTool = false,
                int axis = -1, core::EntityId previewCamera = core::kInvalidEntity,
                GizmoTool tool = GizmoTool::Move, GizmoSpace space = GizmoSpace::World,
                const ComponentOverlay* components = nullptr, float pointSize = 5.0F,
                bool overlays = true, bool xRay = false,
                core::EntityId previewEntity = core::kInvalidEntity,
                const core::EditableMeshRecord* editablePreview = nullptr,
                std::optional<glm::vec3> transformPivot = std::nullopt);
    [[nodiscard]] GizmoHandle gizmoHandle(const core::Scene& scene, core::EntityId id,
                                          GizmoSpace space = GizmoSpace::World,
                                          std::optional<glm::vec3> pivot = std::nullopt) const;
    /** @brief 在所属当前 Context 中清空导入 GPU 缓存，不影响内置演示资源。 */
    void clearImportedResources();

    /** @brief 在当前 Context 中按依赖逆序释放 GPU 资源。 */
    void destroy();

    /** @brief 返回 Shader、Grid 和三种 Mesh 是否都已初始化。 */
    [[nodiscard]] bool isInitialized() const noexcept;

  private:
    void recordRenderFailure(const QString& message);
    void applyMaterial(const Material& material, const core::SurfaceStyle& surface);
    void drawNode(const core::Scene& scene, core::EntityId id, const glm::mat4& parentWorld,
                  const assets::AssetManager& assets, core::EntityId previewEntity,
                  const core::EditableMeshRecord* editablePreview);
    void drawImportedMesh(core::MeshRendererComponent component, const assets::AssetManager& assets,
                          const core::SurfaceStyle& surface);
    void drawEditableMesh(core::EntityId entity, core::MeshId id,
                          const core::EditableMeshRecord& record,
                          const core::SurfaceStyle& surface);

    QOpenGLFunctions_4_1_Core* functions_ = nullptr;
    ShaderProgram meshShader_;
    GpuMesh cubeMesh_;
    GpuMesh sphereMesh_;
    GpuMesh planeMesh_;
    GridRenderer gridRenderer_;
    SelectionRenderer selectionRenderer_;
    ComponentOverlayRenderer componentOverlayRenderer_;
    GizmoRenderer gizmoRenderer_;
    GpuTexture checkerTexture_;
    GpuTexture whiteTexture_;
    Material cubeMaterial_;
    Material sphereMaterial_{{0.95F, 0.38F, 0.16F}, false, nullptr};
    Material planeMaterial_{{1.0F, 1.0F, 1.0F}, false, &checkerTexture_};
    EditorCamera camera_;
    core::ViewportVisibility visibility_;
    ViewportShading shadingMode_ = ViewportShading::Material;
    std::uint64_t visibilityRevision_ = 0;
    float aspect_ = 1;
    bool initialized_ = false;
    RenderStatus renderStatus_;
    std::unordered_map<core::AssetId, std::unique_ptr<GpuMesh>> importedMeshes_;
    std::unordered_map<core::AssetId, std::unique_ptr<GpuTexture>> importedTextures_;
    struct EditableGpuMesh {
        std::shared_ptr<const core::EditableMeshContent> content;
        std::uint64_t revision = 0;
        std::uint64_t visibilityRevision = 0;
        std::unique_ptr<GpuMesh> mesh;
        bool ready = true;
    };
    std::unordered_map<core::MeshId, EditableGpuMesh> editableMeshes_;
};

} // namespace mini3d::renderer_gl
