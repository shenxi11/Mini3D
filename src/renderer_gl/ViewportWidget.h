/*
 * 模块名: ViewportWidget
 * 功能概述: 承载 OpenGL Context，把帧生命周期和相机输入意图交给 Renderer。
 * 对外接口: mini3d::renderer_gl::ViewportWidget、defaultSurfaceFormat
 * 依赖关系: Qt 6 OpenGL、Qt 6 OpenGLWidgets
 * 输入输出: 输入 Qt 的绘制、尺寸、鼠标和滚轮事件，输出可导航的三维视口。
 * 异常与错误: OpenGL 函数表或调试日志器初始化失败时写入 Qt 日志。
 * 维护说明: 所有 OpenGL 调用必须发生在 QOpenGLWidget 的有效 Context 回调内。
 */

#pragma once

#include "ComponentOverlayRenderer.h"
#include "EditorCamera.h"
#include "GizmoController.h"
#include "Renderer.h"
#include "ViewportShading.h"
#include "assets/AssetManager.h"
#include "core/Ray.h"
#include "core/Scene.h"
#include "core/SceneSerializer.h"
#include "core/ViewportVisibility.h"

#include <QOpenGLFunctions_4_1_Core>
#include <QOpenGLWidget>
#include <QImage>
#include <QPointF>
#include <QStringList>
#include <QSurfaceFormat>
#include <memory>
#include <functional>
#include <optional>

class QOpenGLDebugLogger;
class QOpenGLDebugMessage;
class QMouseEvent;
class QWheelEvent;

namespace mini3d::renderer_gl {

class ViewNavigationWidget;

/** @brief 装配层提供的实际文档版本；Renderer 不依赖编辑 API。 */
struct FrameDocumentStamp {
    QString instanceId, documentId;
    std::uint64_t documentRevision = 0, historyRevision = 0;
    bool operator==(const FrameDocumentStamp&) const = default;
};
/** @brief 有限的临时可见性摘要，不复制源网格或隐藏元素身份集合。 */
struct ViewportVisibilitySummary {
    std::vector<core::EntityId> hiddenEntityIds, isolatedEntityIds;
    core::EntityId editedEntityId = 0;
    std::size_t hiddenVertexCount = 0, hiddenEdgeCount = 0, hiddenFaceCount = 0;
};
/** @brief 实际有效观察状态；保存相机和瞬时投影在此明确分开。 */
struct ViewportState {
    FrameDocumentStamp document;
    std::uint64_t viewportRevision = 0;
    RenderView view;
    ViewportShading shading = ViewportShading::Material;
    bool overlays = true, xRay = false;
    QSize logicalSize, pixelSize;
    double devicePixelRatio = 1;
    ViewportVisibilitySummary visibility;
};
/** @brief 一次真实 paint 的输入身份及其所用资源状态。 */
struct RenderedFrame {
    ViewportState state;
    std::uint64_t frameId = 0, contextGeneration = 0;
    RenderStatus resources;
};
struct StampedFramebuffer {
    QImage image;
    RenderedFrame frame;
};
/** @brief 经过协调层全量验证的会话显示更新。 */
struct ViewUpdate {
    std::optional<core::CameraState> camera;
    std::optional<EditorView> preset;
    std::optional<bool> orthographic;
    std::optional<ViewportShading> shading;
    std::optional<bool> overlays, xRay;
};

/**
 * @brief Mini3D Studio 的 OpenGL 三维视口。
 *
 * 当前负责创建 4.1 Core Context、接入 Debug Logger，并向 Renderer 转发帧生命周期。
 */
class ViewportWidget final : public QOpenGLWidget {
    Q_OBJECT
  public:
    /**
     * @brief 创建由 Qt 管理生命周期的 OpenGL 视口。
     * @param parent 可选父控件；非空时由 Qt 父子对象机制负责顶层所有权。
     */
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /** @brief 持有只读共享场景，更新请求不执行 GPU 操作。 */
    void setScene(std::shared_ptr<const core::Scene> scene);
    /** @brief 更新临时可见性，保持 GL Context 与持久 Scene 不变。 */
    void setViewportVisibility(const core::ViewportVisibility& visibility);
    /** @brief 共享只读 CPU 资源库，GPU 缓存由当前 Renderer 独立拥有。 */
    void setAssets(std::shared_ptr<const assets::AssetManager> assets);
    /** @brief 接收唯一选择模型的 ID 镜像，仅请求高亮重绘。 */
    void setSelectedEntity(core::EntityId id);
    /** @brief 编辑上下文禁止对象手柄；保留原工具偏好供返回对象模式时恢复。 */
    void setEditMode(bool enabled);
    /** @brief 接收 CPU 选区显示数据；本方法不执行 GL 上传。 */
    void setComponentOverlay(ComponentOverlay overlay);
    /** @brief 独立 CPU 显示候选；空值恢复持久网格，GPU 替换仍仅发生在 paintGL。 */
    void setEditablePreview(core::EntityId entity,
                            std::shared_ptr<const core::EditableMeshContent> content);
    /** @brief 视口会话显示状态；与选择、材质透明度、文档保存状态独立。 */
    void setXRayEnabled(bool enabled);
    [[nodiscard]] bool isXRayEnabled() const;
    void setOverlayVisible(bool visible);
    [[nodiscard]] bool isOverlayVisible() const;
    /** @brief 会话级网格显示模式；GL Context 重建仍保留，不影响场景或历史。 */
    [[nodiscard]] ViewportShading shadingMode() const;
    void setShadingMode(ViewportShading mode);
    /** @brief 只读游标镜像；显示不参与深度或场景节点拾取。 */
    void setCursor3D(const core::Cursor3D& cursor);
    /** @brief 接收吸附目标世界坐标；空值清除。仅显示，不参与选择或历史。 */
    void setSnapTarget(std::optional<glm::vec3> position);
    [[nodiscard]] std::optional<glm::vec3> snapTarget() const;
    /** @brief 用冻结驱动点及世界半径显示真实影响圆；空中心清除，不修改几何或相机。 */
    void setProportionalInfluence(std::vector<glm::vec3> centers, double worldRadius);
    [[nodiscard]] double proportionalInfluenceRadius() const;
    [[nodiscard]] const std::vector<glm::vec3>& proportionalInfluenceCenters() const;
    /** @brief 对象手柄使用显式世界枢轴；空值沿用所选对象原点。更改时取消拖动。 */
    void setTransformPivot(std::optional<glm::vec3> pivot);
    /** @brief 下一次左键放置，Esc/右键/失焦取消；不改变当前对象工具偏好。 */
    void setCursorPlacementEnabled(bool enabled);
    [[nodiscard]] bool isCursorPlacementEnabled() const;
    void setCursorShortcutEnabled(bool enabled);
    /** @brief 聚焦当前可见几何；无选择或空容器时返回 false。 */
    bool focusSelection();
    /** @brief 框选全部可见对象，空场景/相机预览时拒绝；不改对象选择。 */
    bool focusAll();
    /** @brief 对显式可见实体/子树框景；不改变选区或取消当前交互。 */
    bool focusEntities(const std::vector<core::EntityId>& ids,
                       const std::function<bool()>& beforeCommit = {});
    [[nodiscard]] bool hasEntity(core::EntityId id) const;
    /** @brief 原子安装已验证的显示候选；返回是否改变，持久相机走 cameraChanged。 */
    bool applyViewUpdate(const ViewUpdate& update,
                         const std::function<bool()>& beforeCommit = {});
    /** @brief 每次查询/真实绘制同步读取装配层的文档版本。 */
    void setFrameDocumentProvider(std::function<FrameDocumentStamp()> provider);
    [[nodiscard]] std::optional<ViewportState> observationState();
    [[nodiscard]] bool isObservationAvailable() const;
    [[nodiscard]] const std::optional<RenderedFrame>& lastRenderedFrame() const;
    [[nodiscard]] std::uint64_t contextGeneration() const;
    /** @brief 仅应用线程有效 Context 使用；返回抓取之后的真实 paint stamp。 */
    [[nodiscard]] std::optional<StampedFramebuffer> grabStampedFramebuffer();
    void setMoveToolEnabled(bool enabled);
    /** @brief 工具切换先取消旧手势；None 关闭手柄。 */
    void setTransformTool(GizmoTool tool);
    [[nodiscard]] GizmoTool transformTool() const;
    void setTransformSpace(GizmoSpace space);
    [[nodiscard]] GizmoSpace transformSpace() const;
    void setSnapEnabled(bool enabled);
    void setCameraView(EditorView view);
    void setOrthographic(bool enabled);
    [[nodiscard]] bool isOrthographic() const;
    [[nodiscard]] bool isPreviewingCamera() const;
    /** @brief 导航控件动作前取消编辑会话；预览或对象手柄拖动时拒绝。 */
    bool beginViewNavigation();
    /** @brief 导航按下至释放/取消的真实活动状态；即时导航须在同一事件内结束。 */
    [[nodiscard]] bool isNavigationActive() const;
    void endViewNavigation();
    void orbitViewNavigation(QPointF delta);
    void panViewNavigation(QPointF delta);
    void zoomViewNavigation(float steps);
    void requestCameraPreviewToggle();
    [[nodiscard]] std::optional<core::Transform> viewTransform() const;
    /** @brief 拷贝实际观察相机（含正交/预设），供模态会话冻结；不暴露 Renderer 或 GL。 */
    [[nodiscard]] std::optional<EditorCamera> editorCameraSnapshot() const;
    /** @brief ViewModel 完成或取消事务后，释放本地鼠标状态。 */
    void resetMoveInteraction();
    /** @brief 文档打开/新建时恢复观察相机，不产生导航通知。 */
    void setEditorCamera(const core::CameraState& camera);
    /** @brief 切换到实体只读预览，不改写编辑器相机；0 返回编辑视图。 */
    void setPreviewCamera(core::EntityId id);

    /**
     * @brief 返回应用启动前应设置的 OpenGL SurfaceFormat。
     * @return OpenGL 4.1 Core、24 位深度和 8 位模板格式；Debug 构建额外请求调试 Context。
     */
    [[nodiscard]] static QSurfaceFormat defaultSurfaceFormat();

  signals:
    /** @brief 左键完成点击时发送世界射线，交由 ViewModel 修改选择。 */
    void pickRequested(const core::Ray& ray);
    void componentPickRequested(QPointF position, bool extend);
    void cursorPlacementRequested(QPointF position);
    void cursorPlacementChanged(bool enabled);
    void moveStarted(core::EntityId id);
    void movePreviewed(const core::Transform& transform);
    void moveFinished(bool commit);
    void cameraChanged(const core::CameraState& camera);
    void previewExitRequested();
    void viewModeChanged();
    void navigationStarted();
    void navigationActivityChanged(bool active);
    void cameraPreviewToggleRequested();
    void xRayChanged(bool enabled);
    void overlayVisibilityChanged(bool visible);
    void shadingModeChanged();
    void filesDropped(const QStringList& paths);
    void interactionRejected(const QString& message);
    void viewportChanged();
    void framePainted();
    void observationUnavailable();

  protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  private:
    friend class ViewNavigationWidget;
    void setNavigationActive(bool active);
    void initializeRenderer();
    void initializeDebugLogger();
    void handleOpenGLMessage(const QOpenGLDebugMessage& message);
    void releaseOpenGLResources();
    void finishMove(bool commit);
    void paintCursor();
    void paintSnapTarget();
    void paintProportionalInfluence();
    void markViewportChanged();
    void synchronizeObservationMetrics();
    void synchronizeSceneVisualState();

    struct VisualNode {
        core::EntityId id = 0, parent = 0;
        std::vector<core::EntityId> children;
        core::Transform transform;
        core::SurfaceStyle surface;
        core::PrimitiveKind primitive = core::PrimitiveKind::Empty;
        bool visible = true;
        std::pair<core::AssetId, core::AssetId> importedMesh{0, 0};
        std::optional<core::CameraComponent> camera;
        std::optional<core::LightComponent> light;
        core::MeshId meshId = 0;
        std::shared_ptr<const core::EditableMeshContent> meshContent;
        std::uint64_t evaluationRevision = 0;
        bool operator==(const VisualNode& other) const {
            return id == other.id && parent == other.parent && children == other.children &&
                   transform.position == other.transform.position &&
                   transform.rotation == other.transform.rotation &&
                   transform.scale == other.transform.scale && surface == other.surface &&
                   primitive == other.primitive && visible == other.visible &&
                   importedMesh == other.importedMesh && camera == other.camera &&
                   light == other.light && meshId == other.meshId &&
                   meshContent == other.meshContent && evaluationRevision == other.evaluationRevision;
        }
    };
    std::vector<VisualNode> visualNodes_;
    core::Lighting visualLighting_;
    std::function<FrameDocumentStamp()> frameDocumentProvider_;
    FrameDocumentStamp observedDocument_;
    std::uint64_t viewportRevision_ = 1, frameId_ = 0, contextGeneration_ = 0;
    QSize observedLogicalSize_, observedPixelSize_;
    double observedDevicePixelRatio_ = 0;
    std::optional<RenderedFrame> lastRenderedFrame_;
    std::optional<EditorCamera> pendingRenderCamera_;

    QOpenGLDebugLogger* debugLogger_ = nullptr;
    ViewNavigationWidget* viewNavigation_ = nullptr;
    // 函数包装器与实际 Context 同生共死，不能跨重建复用 Qt 的后端引用。
    std::unique_ptr<QOpenGLFunctions_4_1_Core> functions_;
    std::unique_ptr<Renderer> renderer_;
    std::shared_ptr<const core::Scene> scene_ = std::make_shared<core::Scene>();
    std::shared_ptr<const assets::AssetManager> assets_ = std::make_shared<assets::AssetManager>();
    QPointF lastMousePosition_;
    QPointF leftPressPosition_;
    core::EntityId selectedEntity_ = core::kInvalidEntity;
    core::EntityId previewCamera_ = core::kInvalidEntity;
    bool leftClickPending_ = false;
    bool editMode_ = false;
    bool extendClick_ = false;
    ComponentOverlay componentOverlay_;
    core::EntityId previewEntity_ = core::kInvalidEntity;
    core::EditableMeshRecord editablePreview_;
    core::ViewportVisibility visibility_;
    bool xRayEnabled_ = false;
    bool overlayVisible_ = true;
    ViewportShading shadingMode_ = ViewportShading::Material;
    core::Cursor3D cursor3D_;
    std::optional<glm::vec3> transformPivot_;
    ComponentOverlayRenderer cursorRenderer_;
    ComponentOverlayRenderer snapRenderer_;
    std::optional<glm::vec3> snapTarget_;
    std::vector<glm::vec3> proportionalCenters_;
    double proportionalRadius_ = 0;
    bool cursorPlacementEnabled_ = false;
    bool cursorShortcutEnabled_ = true;
    QCursor previousCursor_;
    bool hadCursor_ = false;
    bool cameraDragActive_ = false;
    bool navigationActive_ = false;
    bool functionsInitialized_ = false;
    GizmoTool transformTool_ = GizmoTool::None;
    GizmoSpace transformSpace_ = GizmoSpace::World;
    bool snapEnabled_ = false;
    int hoveredAxis_ = -1;
    GizmoController gizmoController_;
    std::optional<core::CameraState> pendingCamera_;
};

} // namespace mini3d::renderer_gl
