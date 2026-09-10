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

#include "EditorCamera.h"
#include "GizmoController.h"
#include "assets/AssetManager.h"
#include "core/Ray.h"
#include "core/Scene.h"
#include "core/SceneSerializer.h"

#include <QOpenGLFunctions_4_1_Core>
#include <QOpenGLWidget>
#include <QPointF>
#include <QStringList>
#include <QSurfaceFormat>
#include <memory>
#include <optional>

class QOpenGLDebugLogger;
class QOpenGLDebugMessage;
class QMouseEvent;
class QWheelEvent;

namespace mini3d::renderer_gl {

class Renderer;

/**
 * @brief Mini3D Studio 的 OpenGL 三维视口。
 *
 * 当前负责创建 4.1 Core Context、接入 Debug Logger，并向 Renderer 转发帧生命周期。
 */
class ViewportWidget final : public QOpenGLWidget, protected QOpenGLFunctions_4_1_Core {
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
    /** @brief 共享只读 CPU 资源库，GPU 缓存由当前 Renderer 独立拥有。 */
    void setAssets(std::shared_ptr<const assets::AssetManager> assets);
    /** @brief 接收唯一选择模型的 ID 镜像，仅请求高亮重绘。 */
    void setSelectedEntity(core::EntityId id);
    /** @brief 聚焦当前可见几何；无选择或空容器时返回 false。 */
    bool focusSelection();
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
    [[nodiscard]] std::optional<core::Transform> viewTransform() const;
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
    void moveStarted(core::EntityId id);
    void movePreviewed(const core::Transform& transform);
    void moveFinished(bool commit);
    void cameraChanged(const core::CameraState& camera);
    void previewExitRequested();
    void viewModeChanged();
    void filesDropped(const QStringList& paths);
    void interactionRejected(const QString& message);

  protected:
    bool event(QEvent* event) override;
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
    void initializeRenderer();
    void initializeDebugLogger();
    void handleOpenGLMessage(const QOpenGLDebugMessage& message);
    void releaseOpenGLResources();
    void finishMove(bool commit);

    QOpenGLDebugLogger* debugLogger_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    std::shared_ptr<const core::Scene> scene_ = std::make_shared<core::Scene>();
    std::shared_ptr<const assets::AssetManager> assets_ = std::make_shared<assets::AssetManager>();
    QPointF lastMousePosition_;
    QPointF leftPressPosition_;
    core::EntityId selectedEntity_ = core::kInvalidEntity;
    core::EntityId previewCamera_ = core::kInvalidEntity;
    bool leftClickPending_ = false;
    bool cameraDragActive_ = false;
    bool functionsInitialized_ = false;
    GizmoTool transformTool_ = GizmoTool::None;
    GizmoSpace transformSpace_ = GizmoSpace::World;
    bool snapEnabled_ = false;
    int hoveredAxis_ = -1;
    GizmoController gizmoController_;
    std::optional<core::CameraState> pendingCamera_;
};

} // namespace mini3d::renderer_gl
