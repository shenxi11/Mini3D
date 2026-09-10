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

#include "EditorCamera.h"
#include "GizmoRenderer.h"
#include "GpuMesh.h"
#include "GpuTexture.h"
#include "GridRenderer.h"
#include "Material.h"
#include "SelectionRenderer.h"
#include "ShaderProgram.h"
#include "assets/AssetManager.h"
#include "core/Scene.h"

#include <memory>
#include <unordered_map>

class QOpenGLFunctions_4_1_Core;

namespace mini3d::renderer_gl {

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
    void setCameraView(EditorView view);
    void setOrthographic(bool enabled);
    /** @brief 根据可见几何聚焦实体/子树；空盒不改变相机。 */
    bool focusEntity(const core::Scene& scene, const assets::AssetManager& assets,
                     core::EntityId id);

    /** @brief 设置完整帧状态、清屏并绘制 Grid 与三种基础几何。 */
    void render(const core::Scene& scene, const assets::AssetManager& assets,
                core::EntityId selected = core::kInvalidEntity, bool moveTool = false,
                int axis = -1, core::EntityId previewCamera = core::kInvalidEntity,
                GizmoTool tool = GizmoTool::Move, GizmoSpace space = GizmoSpace::World);
    [[nodiscard]] GizmoHandle gizmoHandle(const core::Scene& scene, core::EntityId id,
                                          GizmoSpace space = GizmoSpace::World) const;
    /** @brief 在所属当前 Context 中清空导入 GPU 缓存，不影响内置演示资源。 */
    void clearImportedResources();

    /** @brief 在当前 Context 中按依赖逆序释放 GPU 资源。 */
    void destroy();

    /** @brief 返回 Shader、Grid 和三种 Mesh 是否都已初始化。 */
    [[nodiscard]] bool isInitialized() const noexcept;

  private:
    void applyMaterial(const Material& material, const core::SurfaceStyle& surface);
    void drawNode(const core::Scene& scene, core::EntityId id, const glm::mat4& parentWorld,
                  const assets::AssetManager& assets);
    void drawImportedMesh(core::MeshRendererComponent component, const assets::AssetManager& assets,
                          const core::SurfaceStyle& surface);

    QOpenGLFunctions_4_1_Core* functions_ = nullptr;
    ShaderProgram meshShader_;
    GpuMesh cubeMesh_;
    GpuMesh sphereMesh_;
    GpuMesh planeMesh_;
    GridRenderer gridRenderer_;
    SelectionRenderer selectionRenderer_;
    GizmoRenderer gizmoRenderer_;
    GpuTexture checkerTexture_;
    GpuTexture whiteTexture_;
    Material cubeMaterial_;
    Material sphereMaterial_{{0.95F, 0.38F, 0.16F}, false, nullptr};
    Material planeMaterial_{{1.0F, 1.0F, 1.0F}, false, &checkerTexture_};
    EditorCamera camera_;
    float aspect_ = 1;
    bool initialized_ = false;
    std::unordered_map<core::AssetId, std::unique_ptr<GpuMesh>> importedMeshes_;
    std::unordered_map<core::AssetId, std::unique_ptr<GpuTexture>> importedTextures_;
};

} // namespace mini3d::renderer_gl
