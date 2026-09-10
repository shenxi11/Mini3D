/*
 * 模块名: ViewportWidget
 * 功能概述: 初始化 OpenGL 4.1 Core，并把帧绘制和相机输入意图交给 Renderer。
 * 对外接口: mini3d::renderer_gl::ViewportWidget
 * 依赖关系: Qt 6 OpenGL、Qt 6 OpenGLWidgets
 * 输入输出: 输入 Qt 生命周期与鼠标事件，输出可 Orbit、Pan、Zoom 的三维视口。
 * 异常与错误: Context 能力不足时停止 OpenGL 调用并通过 Qt 日志报告实际格式。
 * 维护说明: 调试日志器在当前 Context 有效期间初始化和销毁。
 */

#include "ViewportWidget.h"

#include "Renderer.h"

#include <QApplication>
#include <QDebug>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLDebugLogger>
#include <QOpenGLDebugMessage>
#include <QResource>
#include <QUrl>
#include <QWheelEvent>
#include <memory>

static void initializeRendererShaderResources() {
    Q_INIT_RESOURCE(renderer_shaders);
}

namespace mini3d::renderer_gl {
namespace {

// 仅接收整批本地模型文件，不把网址或目录交给导入器。
QStringList modelPaths(const QMimeData* data) {
    if (!data || !data->hasUrls()) {
        return {};
    }
    QStringList paths;
    for (const auto& url : data->urls()) {
        if (!url.isLocalFile()) {
            return {};
        }
        const QFileInfo info(url.toLocalFile());
        const auto suffix = info.suffix().toLower();
        if (!info.isFile() ||
            (suffix != QStringLiteral("glb") && suffix != QStringLiteral("gltf"))) {
            return {};
        }
        paths.append(info.absoluteFilePath());
    }
    return paths;
}

constexpr int kOpenGLMajorVersion = 4;
constexpr int kOpenGLMinorVersion = 1;
constexpr int kDepthBufferSize = 24;
constexpr int kStencilBufferSize = 8;

[[nodiscard]] bool supportsRequiredFormat(const QSurfaceFormat& format) {
    const bool supportsVersion = format.majorVersion() > kOpenGLMajorVersion ||
                                 (format.majorVersion() == kOpenGLMajorVersion &&
                                  format.minorVersion() >= kOpenGLMinorVersion);
    return supportsVersion && format.profile() == QSurfaceFormat::CoreProfile;
}

} // namespace

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setObjectName(QStringLiteral("ViewportWidget"));
    setFormat(defaultSurfaceFormat());
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
}

ViewportWidget::~ViewportWidget() {
    if (context() != nullptr && context()->isValid()) {
        makeCurrent();
        releaseOpenGLResources();
        doneCurrent();
        return;
    }

    delete debugLogger_;
    debugLogger_ = nullptr;
}

void ViewportWidget::setScene(std::shared_ptr<const core::Scene> scene) {
    if (scene != nullptr) {
        scene_ = std::move(scene);
        update();
    }
}

void ViewportWidget::setAssets(std::shared_ptr<const assets::AssetManager> assets) {
    if (assets != nullptr && assets_ != assets) {
        // 资源库更换时废弃旧 ID 缓存；仍在所属 Context 中销毁。
        if (renderer_ != nullptr) {
            makeCurrent();
            renderer_->clearImportedResources();
            doneCurrent();
        }
        assets_ = std::move(assets);
        update();
    }
}

QSurfaceFormat ViewportWidget::defaultSurfaceFormat() {
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(kOpenGLMajorVersion, kOpenGLMinorVersion);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(kDepthBufferSize);
    format.setStencilBufferSize(kStencilBufferSize);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);

#ifndef NDEBUG
    format.setOption(QSurfaceFormat::DebugContext);
#endif

    return format;
}

void ViewportWidget::setSelectedEntity(core::EntityId id) {
    finishMove(false);
    selectedEntity_ = id;
    update();
}

bool ViewportWidget::focusSelection() {
    finishMove(false);
    if (renderer_ == nullptr || previewCamera_ != 0) {
        return false;
    }
    renderer_->resize(width(), height());
    if (!renderer_->focusEntity(*scene_, *assets_, selectedEntity_)) {
        return false;
    }
    emit cameraChanged(renderer_->camera().state());
    update();
    return true;
}
void ViewportWidget::setEditorCamera(const core::CameraState& camera) {
    finishMove(false);
    if (!camera.isValid()) {
        return;
    }
    if (renderer_) {
        renderer_->setCameraState(camera);
    } else {
        pendingCamera_ = camera;
    }
    emit viewModeChanged();
    update();
}
void ViewportWidget::setMoveToolEnabled(bool enabled) {
    setTransformTool(enabled ? GizmoTool::Move : GizmoTool::None);
}
GizmoTool ViewportWidget::transformTool() const {
    return transformTool_;
}
void ViewportWidget::setTransformTool(GizmoTool tool) {
    finishMove(false);
    transformTool_ = tool;
    hoveredAxis_ = -1;
    update();
}
void ViewportWidget::setTransformSpace(GizmoSpace space) {
    finishMove(false);
    transformSpace_ = space;
    hoveredAxis_ = -1;
    update();
}
GizmoSpace ViewportWidget::transformSpace() const {
    return transformSpace_;
}
void ViewportWidget::setSnapEnabled(bool enabled) {
    finishMove(false);
    snapEnabled_ = enabled;
}
void ViewportWidget::setCameraView(EditorView view) {
    if (!renderer_ || previewCamera_ != 0) {
        emit interactionRejected(QStringLiteral("请先返回编辑视图再切换观察方向。"));
        return;
    }
    finishMove(false);
    cameraDragActive_ = false;
    leftClickPending_ = false;
    renderer_->setCameraView(view);
    update();
}
void ViewportWidget::setOrthographic(bool enabled) {
    if (!renderer_ || previewCamera_ != 0) {
        emit interactionRejected(QStringLiteral("请先返回编辑视图再切换投影。"));
        emit viewModeChanged();
        return;
    }
    finishMove(false);
    cameraDragActive_ = false;
    leftClickPending_ = false;
    renderer_->setOrthographic(enabled);
    emit viewModeChanged();
    update();
}
bool ViewportWidget::isOrthographic() const {
    return renderer_ && renderer_->camera().isOrthographic();
}
std::optional<core::Transform> ViewportWidget::viewTransform() const {
    if (!renderer_) {
        return std::nullopt;
    }
    core::Transform transform;
    transform.position = renderer_->camera().position();
    transform.rotation =
        glm::quat_cast(glm::transpose(glm::mat3(renderer_->camera().viewMatrix())));
    return transform;
}
void ViewportWidget::setPreviewCamera(core::EntityId id) {
    finishMove(false);
    leftClickPending_ = false;
    cameraDragActive_ = false;
    hoveredAxis_ = -1;
    previewCamera_ = id;
    update();
}

void ViewportWidget::initializeGL() {
    functionsInitialized_ = initializeOpenGLFunctions();
    if (!functionsInitialized_) {
        qCritical() << "OpenGL 4.1 核心功能初始化失败。";
        return;
    }

    const QSurfaceFormat actualFormat = context()->format();
    if (!supportsRequiredFormat(actualFormat)) {
        qCritical().nospace() << "需要 OpenGL 4.1 核心模式，但已创建的上下文版本为 "
                              << actualFormat.majorVersion() << '.' << actualFormat.minorVersion()
                              << "，配置类型为 " << actualFormat.profile() << '.';
        functionsInitialized_ = false;
        return;
    }

    const auto* versionText = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const auto* rendererText = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    qInfo().noquote() << QStringLiteral("OpenGL 视口已初始化：%1 | %2")
                             .arg((versionText ? QString::fromLatin1(versionText)
                                               : QStringLiteral("未知")),
                                  (rendererText ? QString::fromLatin1(rendererText)
                                                : QStringLiteral("未知")));

    initializeDebugLogger();
    initializeRendererShaderResources();
    initializeRenderer();
}

void ViewportWidget::resizeGL(int width, int height) {
    if (!functionsInitialized_) {
        return;
    }

    glViewport(0, 0, width, height);
    if (renderer_ != nullptr) {
        renderer_->resize(this->width(), this->height());
    }
}

void ViewportWidget::paintGL() {
    if (!functionsInitialized_) {
        return;
    }

    if (renderer_ != nullptr) {
        renderer_->render(*scene_, *assets_, selectedEntity_, transformTool_ != GizmoTool::None,
                          gizmoController_.activeAxis() >= 0 ? gizmoController_.activeAxis()
                                                             : hoveredAxis_,
                          previewCamera_, transformTool_, transformSpace_);
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    if (previewCamera_ != 0) {
        event->accept();
        return;
    }
    if (gizmoController_.activeAxis() >= 0) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (transformTool_ != GizmoTool::None && renderer_ && !cameraDragActive_ &&
            event->buttons() == Qt::LeftButton &&
            (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ControlModifier)) {
            const auto handle = renderer_->gizmoHandle(*scene_, selectedEntity_, transformSpace_);
            const auto ray =
                renderer_->camera().screenRay(static_cast<float>(event->position().x()),
                                              static_cast<float>(event->position().y()));
            const auto axis = GizmoController::pickAxis(ray, handle, transformTool_);
            const auto* node = scene_->find(selectedEntity_);
            if (axis >= 0 && node &&
                gizmoController_.begin(ray, axis, handle,
                                       node->parent == core::kInvalidEntity
                                           ? glm::mat4(1)
                                           : scene_->worldMatrix(node->parent),
                                       node->transform, transformTool_)) {
                leftClickPending_ = false;
                emit moveStarted(selectedEntity_);
                grabMouse();
                update();
                event->accept();
                return;
            }
        }
        leftPressPosition_ = event->position();
        leftClickPending_ = event->buttons() == Qt::LeftButton &&
                            event->modifiers() == Qt::NoModifier && !cameraDragActive_;
        event->accept();
        return;
    }
    if (event->button() != Qt::MiddleButton) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    cameraDragActive_ = true;
    leftClickPending_ = false;
    lastMousePosition_ = event->position();
    event->accept();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    if (previewCamera_ != 0) {
        event->accept();
        return;
    }
    if (gizmoController_.activeAxis() >= 0) {
        if (!event->buttons().testFlag(Qt::LeftButton)) {
            finishMove(false);
        } else {
            core::Transform preview;
            if (gizmoController_.preview(
                    renderer_->camera().screenRay(static_cast<float>(event->position().x()),
                                                  static_cast<float>(event->position().y())),
                    preview, snapEnabled_ || event->modifiers().testFlag(Qt::ControlModifier))) {
                emit movePreviewed(preview);
            } else {
                emit interactionRejected(
                    QStringLiteral("当前方向无法形成有效 "
                                   "TRS（可能产生剪切或射线平行）。可改用局部坐标或属性数值。"));
            }
        }
        event->accept();
        return;
    }
    if (transformTool_ != GizmoTool::None && renderer_ != nullptr && !cameraDragActive_) {
        const auto ray = renderer_->camera().screenRay(static_cast<float>(event->position().x()),
                                                       static_cast<float>(event->position().y()));
        hoveredAxis_ = GizmoController::pickAxis(
            ray, renderer_->gizmoHandle(*scene_, selectedEntity_, transformSpace_), transformTool_);
        update();
    }
    if (leftClickPending_ && (event->position() - leftPressPosition_).manhattanLength() >=
                                 QApplication::startDragDistance()) {
        leftClickPending_ = false;
    }
    if (!cameraDragActive_ || !event->buttons().testFlag(Qt::MiddleButton) ||
        renderer_ == nullptr) {
        QOpenGLWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF currentPosition = event->position();
    const QPointF delta = currentPosition - lastMousePosition_;
    lastMousePosition_ = currentPosition;

    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        renderer_->panCamera(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    } else {
        renderer_->orbitCamera(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    }
    emit cameraChanged(renderer_->camera().state());

    update();
    event->accept();
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (previewCamera_ != 0) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && gizmoController_.activeAxis() >= 0) {
        core::Transform preview;
        if (gizmoController_.preview(
                renderer_->camera().screenRay(static_cast<float>(event->position().x()),
                                              static_cast<float>(event->position().y())),
                preview, snapEnabled_ || event->modifiers().testFlag(Qt::ControlModifier))) {
            emit movePreviewed(preview);
        }
        finishMove(true);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const bool clicked = leftClickPending_ && !cameraDragActive_ &&
                             event->modifiers() == Qt::NoModifier &&
                             rect().contains(event->position().toPoint()) &&
                             (event->position() - leftPressPosition_).manhattanLength() <
                                 QApplication::startDragDistance();
        leftClickPending_ = false;
        if (clicked && renderer_ != nullptr) {
            renderer_->resize(width(), height());
            emit pickRequested(
                renderer_->camera().screenRay(static_cast<float>(event->position().x()),
                                              static_cast<float>(event->position().y())));
        }
        event->accept();
        return;
    }
    if (event->button() != Qt::MiddleButton) {
        QOpenGLWidget::mouseReleaseEvent(event);
        return;
    }

    cameraDragActive_ = false;
    event->accept();
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    if (gizmoController_.activeAxis() >= 0 || previewCamera_ != 0) {
        event->accept();
        return;
    }
    if (renderer_ == nullptr || event->angleDelta().y() == 0) {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    constexpr float wheelAnglePerStep = 120.0F;
    const float wheelSteps = static_cast<float>(event->angleDelta().y()) / wheelAnglePerStep;
    renderer_->zoomCamera(wheelSteps);
    emit cameraChanged(renderer_->camera().state());
    update();
    event->accept();
}

void ViewportWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (previewCamera_ == 0 && event->possibleActions().testFlag(Qt::CopyAction) &&
        !modelPaths(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
        emit interactionRejected(
            QStringLiteral("请在编辑视图拖入本地 GLB/glTF 文件；不支持目录、网址或混合文件类型。"));
    }
}
void ViewportWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (previewCamera_ == 0 && event->possibleActions().testFlag(Qt::CopyAction) &&
        !modelPaths(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}
void ViewportWidget::dropEvent(QDropEvent* event) {
    const auto paths = modelPaths(event->mimeData());
    if (previewCamera_ != 0 || !event->possibleActions().testFlag(Qt::CopyAction) ||
        paths.isEmpty()) {
        event->ignore();
        emit interactionRejected(QStringLiteral("拖入文件已不可用，或当前处于相机预览。"));
        return;
    }
    finishMove(false);
    leftClickPending_ = false;
    cameraDragActive_ = false;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit filesDropped(paths);
}
void ViewportWidget::resetMoveInteraction() {
    gizmoController_.end();
    hoveredAxis_ = -1;
    leftClickPending_ = false;
    if (mouseGrabber() == this) {
        releaseMouse();
    }
    update();
}

void ViewportWidget::finishMove(bool commit) {
    if (gizmoController_.activeAxis() < 0) {
        return;
    }
    resetMoveInteraction();
    emit moveFinished(commit);
}

bool ViewportWidget::event(QEvent* event) {
    if (previewCamera_ != 0 && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        emit previewExitRequested();
        event->accept();
        return true;
    }
    if (gizmoController_.activeAxis() >= 0) {
        if (event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            finishMove(false);
            event->accept();
            return true;
        }
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
            event->type() == QEvent::WindowDeactivate || event->type() == QEvent::UngrabMouse ||
            event->type() == QEvent::Resize) {
            finishMove(false);
        }
    }
    return QOpenGLWidget::event(event);
}

void ViewportWidget::initializeRenderer() {
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->initialize(*this)) {
        renderer_.reset();
        return;
    }

    renderer_->resize(width(), height());
    if (pendingCamera_) {
        renderer_->setCameraState(*pendingCamera_);
        pendingCamera_.reset();
    }
}

void ViewportWidget::initializeDebugLogger() {
    if (!context()->format().testOption(QSurfaceFormat::DebugContext)) {
        qInfo() << "当前构建未启用 OpenGL 调试上下文。";
        return;
    }

    debugLogger_ = new QOpenGLDebugLogger(this);
    if (!debugLogger_->initialize()) {
        qWarning() << "OpenGL 调试上下文已启用，但调试日志器初始化失败。";
        delete debugLogger_;
        debugLogger_ = nullptr;
        return;
    }

    connect(debugLogger_, &QOpenGLDebugLogger::messageLogged, this,
            &ViewportWidget::handleOpenGLMessage, Qt::DirectConnection);
    debugLogger_->startLogging(QOpenGLDebugLogger::SynchronousLogging);
    debugLogger_->enableMessages();
    qInfo() << "OpenGL 调试日志已启用。";
}

void ViewportWidget::handleOpenGLMessage(const QOpenGLDebugMessage& message) {
    const QString logText = QStringLiteral("OpenGL 驱动诊断：%1").arg(message.message());

    switch (message.severity()) {
        case QOpenGLDebugMessage::HighSeverity:
            qCritical().noquote() << logText;
            break;
        case QOpenGLDebugMessage::MediumSeverity:
            qWarning().noquote() << logText;
            break;
        case QOpenGLDebugMessage::LowSeverity:
        case QOpenGLDebugMessage::NotificationSeverity:
        case QOpenGLDebugMessage::AnySeverity:
            qDebug().noquote() << logText;
            break;
    }
}

void ViewportWidget::releaseOpenGLResources() {
    if (renderer_ != nullptr) {
        renderer_->destroy();
        renderer_.reset();
    }

    if (debugLogger_ != nullptr) {
        if (debugLogger_->isLogging()) {
            debugLogger_->stopLogging();
        }
        delete debugLogger_;
        debugLogger_ = nullptr;
    }

    functionsInitialized_ = false;
}

} // namespace mini3d::renderer_gl
