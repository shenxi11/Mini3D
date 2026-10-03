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
#include <QPainter>
#include <QPainterPath>
#include <QResource>
#include <QUrl>
#include <QWheelEvent>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <memory>

static void initializeRendererShaderResources() {
    Q_INIT_RESOURCE(renderer_shaders);
}

namespace mini3d::renderer_gl {
namespace {

// 固定逻辑像素的游标形状，只上传一次；绘制时仅更新屏幕投影矩阵。
ComponentOverlay cursorShape() {
    ComponentOverlay overlay;
    overlay.revision = 1;
    const auto quad = [&](glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d, glm::vec4 color) {
        for (const auto point : {a, b, c, a, c, d})
            overlay.triangles.push_back({glm::vec3(point, 0), color});
    };
    for (const float halfWidth : {1.5F, .5F}) {
        const glm::vec4 color(halfWidth > 1 ? glm::vec3(0) : glm::vec3(1), 1);
        quad({-13, -halfWidth}, {13, -halfWidth}, {13, halfWidth}, {-13, halfWidth}, color);
        quad({-halfWidth, -13}, {halfWidth, -13}, {halfWidth, 13}, {-halfWidth, 13}, color);
    }
    constexpr int segments = 64;
    for (int segment = 0; segment < segments; ++segment) {
        const float angle = segment * glm::two_pi<float>() / segments;
        const float next = (segment + 1) * glm::two_pi<float>() / segments;
        const glm::vec2 a(std::cos(angle), std::sin(angle)), b(std::cos(next), std::sin(next));
        quad(a * 7.0F, a * 9.0F, b * 9.0F, b * 7.0F,
             segment / 8 % 2 ? glm::vec4(1) : glm::vec4(.92F, .25F, .25F, 1));
    }
    return overlay;
}

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
        setEditablePreview(core::kInvalidEntity, nullptr);
        update();
    }
}
void ViewportWidget::setViewportVisibility(const core::ViewportVisibility& visibility) {
    resetMoveInteraction();
    visibility_ = visibility;
    if (renderer_)
        renderer_->setViewportVisibility(visibility_);
    update();
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

void ViewportWidget::setEditMode(bool enabled) {
    finishMove(false);
    leftClickPending_ = false;
    hoveredAxis_ = -1;
    editMode_ = enabled;
    update();
}
void ViewportWidget::setComponentOverlay(ComponentOverlay overlay) {
    overlay.revision = componentOverlay_.revision + 1;
    componentOverlay_ = std::move(overlay);
    update();
}
void ViewportWidget::setEditablePreview(core::EntityId entity,
                                        std::shared_ptr<const core::EditableMeshContent> content) {
    previewEntity_ = content ? entity : core::kInvalidEntity;
    if (editablePreview_.content != content) {
        editablePreview_.content = std::move(content);
        ++editablePreview_.evaluationRevision;
    }
    update();
}
void ViewportWidget::setXRayEnabled(bool enabled) {
    if (xRayEnabled_ != enabled) {
        xRayEnabled_ = enabled;
        emit xRayChanged(enabled);
        update();
    }
}
bool ViewportWidget::isXRayEnabled() const {
    return xRayEnabled_;
}
void ViewportWidget::setOverlayVisible(bool visible) {
    if (overlayVisible_ != visible) {
        overlayVisible_ = visible;
        emit overlayVisibilityChanged(visible);
        update();
    }
}
bool ViewportWidget::isOverlayVisible() const {
    return overlayVisible_;
}
ViewportShading ViewportWidget::shadingMode() const {
    return shadingMode_;
}
void ViewportWidget::setShadingMode(ViewportShading mode) {
    if (shadingMode_ == mode)
        return;
    shadingMode_ = mode;
    if (renderer_)
        renderer_->setShadingMode(mode);
    emit shadingModeChanged();
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
bool ViewportWidget::focusAll() {
    finishMove(false);
    if (!renderer_ || previewCamera_ != 0) {
        return false;
    }
    renderer_->resize(width(), height());
    if (!renderer_->focusScene(*scene_, *assets_)) {
        return false;
    }
    resetMoveInteraction();
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
    emit viewModeChanged();
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
    setCursorPlacementEnabled(false);
    finishMove(false);
    leftClickPending_ = false;
    cameraDragActive_ = false;
    hoveredAxis_ = -1;
    previewCamera_ = id;
    update();
}

std::optional<EditorCamera> ViewportWidget::editorCameraSnapshot() const {
    if (!renderer_) {
        return std::nullopt;
    }
    auto camera = renderer_->camera();
    camera.setViewportSize(width(), height());
    return camera;
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
        renderer_->render(
            *scene_, *assets_, selectedEntity_, !editMode_ && transformTool_ != GizmoTool::None,
            gizmoController_.activeAxis() >= 0 ? gizmoController_.activeAxis() : hoveredAxis_,
            previewCamera_, transformTool_, transformSpace_,
            editMode_ ? &componentOverlay_ : nullptr, 5.0F * devicePixelRatioF(), overlayVisible_,
            xRayEnabled_, previewEntity_, editablePreview_.content ? &editablePreview_ : nullptr,
            transformPivot_);
        paintCursor();
        paintSnapTarget();
        paintProportionalInfluence();
    }
}

void ViewportWidget::setCursor3D(const core::Cursor3D& cursor) {
    cursor3D_ = cursor;
    update();
}
void ViewportWidget::setTransformPivot(std::optional<glm::vec3> pivot) {
    if (transformPivot_ == pivot)
        return;
    finishMove(false);
    resetMoveInteraction();
    transformPivot_ = pivot;
    update();
}
bool ViewportWidget::isCursorPlacementEnabled() const {
    return cursorPlacementEnabled_;
}
void ViewportWidget::setCursorShortcutEnabled(bool enabled) {
    setCursorPlacementEnabled(false);
    cursorShortcutEnabled_ = enabled;
}
void ViewportWidget::setCursorPlacementEnabled(bool enabled) {
    enabled = enabled && previewCamera_ == 0 && renderer_ && isVisible();
    if (cursorPlacementEnabled_ == enabled)
        return;
    if (enabled) {
        finishMove(false);
        resetMoveInteraction();
        setFocus(Qt::OtherFocusReason);
        previousCursor_ = cursor();
        hadCursor_ = testAttribute(Qt::WA_SetCursor);
        setCursor(Qt::CrossCursor);
    } else if (hadCursor_) {
        setCursor(previousCursor_);
    } else {
        unsetCursor();
    }
    cursorPlacementEnabled_ = enabled;
    emit cursorPlacementChanged(enabled);
    update();
}
void ViewportWidget::paintCursor() {
    if (!overlayVisible_ || !cursor3D_.visible || previewCamera_ != 0)
        return;
    const auto clip = renderer_->camera().viewProjectionMatrix() * glm::vec4(cursor3D_.position, 1);
    if (clip.w <= 0 || !cursor3D_.isValid())
        return;
    const auto ndc = glm::vec3(clip) / clip.w;
    if (glm::any(glm::greaterThan(glm::abs(ndc), glm::vec3(1))))
        return;
    static const auto shape = cursorShape();
    glm::mat4 screen(1);
    screen[0][0] = 2.0F / width();
    screen[1][1] = 2.0F / height();
    screen[3] = glm::vec4(ndc.x, ndc.y, 0, 1);
    cursorRenderer_.draw(shape, screen, 1, true);
}

void ViewportWidget::setSnapTarget(std::optional<glm::vec3> position) {
    if (snapTarget_ == position)
        return;
    snapTarget_ = position;
    update();
}
std::optional<glm::vec3> ViewportWidget::snapTarget() const {
    return snapTarget_;
}
void ViewportWidget::setProportionalInfluence(std::vector<glm::vec3> centers, double worldRadius) {
    if (centers.empty())
        worldRadius = 0;
    if (proportionalCenters_ == centers && proportionalRadius_ == worldRadius)
        return;
    proportionalCenters_ = std::move(centers);
    proportionalRadius_ = worldRadius;
    update();
}
double ViewportWidget::proportionalInfluenceRadius() const {
    return proportionalRadius_;
}
const std::vector<glm::vec3>& ViewportWidget::proportionalInfluenceCenters() const {
    return proportionalCenters_;
}
void ViewportWidget::paintProportionalInfluence() {
    if (!overlayVisible_ || previewCamera_ != 0 || proportionalCenters_.empty())
        return;
    const auto& camera = renderer_->camera();
    const glm::dmat4 projection(camera.viewProjectionMatrix());
    const glm::dmat3 basis(glm::inverse(camera.viewMatrix()));
    // QPainter 的二维笔画不能继承模型 pass 的深度检查和背面剔除。
    const auto depth = glIsEnabled(GL_DEPTH_TEST);
    const auto cull = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(220, 235, 255), 1.5, Qt::DashLine));
    for (const auto& center : proportionalCenters_) {
        QPainterPath path;
        std::optional<QPointF> previous;
        for (int i = 0; i <= 64; ++i) {
            const auto angle = glm::two_pi<double>() * i / 64;
            const auto point = glm::dvec3(center) + proportionalRadius_ *
                                  (basis[0] * std::cos(angle) + basis[1] * std::sin(angle));
            const auto clip = projection * glm::dvec4(point, 1);
            if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || clip.w <= 0 ||
                std::abs(clip.z) > clip.w) {
                previous.reset();
                continue;
            }
            const QPointF screen((clip.x / clip.w + 1) * width() * .5,
                                  (1 - clip.y / clip.w) * height() * .5);
            if (previous)
                path.lineTo(screen);
            else
                path.moveTo(screen);
            previous = screen;
        }
        painter.drawPath(path);
    }
    painter.end();
    if (depth)
        glEnable(GL_DEPTH_TEST);
    if (cull)
        glEnable(GL_CULL_FACE);
}
void ViewportWidget::paintSnapTarget() {
    if (!snapTarget_ || !overlayVisible_ || previewCamera_ != 0)
        return;
    const auto clip = renderer_->camera().viewProjectionMatrix() * glm::vec4(*snapTarget_, 1);
    if (clip.w <= 0)
        return;
    const auto ndc = glm::vec3(clip) / clip.w;
    if (glm::any(glm::greaterThan(glm::abs(ndc), glm::vec3(1))))
        return;
    static const auto shape = [] {
        ComponentOverlay overlay;
        overlay.revision = 1;
        const glm::vec4 color(0.2F, 1.0F, 0.85F, 1);
        for (int side = 0; side < 4; ++side) {
            const glm::vec3 corners[] = {{-7, -7, 0}, {7, -7, 0}, {7, 7, 0}, {-7, 7, 0}};
            overlay.lines.push_back({corners[side], color});
            overlay.lines.push_back({corners[(side + 1) % 4], color});
        }
        return overlay;
    }();
    glm::mat4 screen(1);
    screen[0][0] = 2.0F / width();
    screen[1][1] = 2.0F / height();
    screen[3] = glm::vec4(ndc.x, ndc.y, 0, 1);
    snapRenderer_.draw(shape, screen, 1, true);
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
    if (cursorPlacementEnabled_ && event->button() == Qt::RightButton) {
        setCursorPlacementEnabled(false);
        event->accept();
        return;
    }
    if (!cameraDragActive_ &&
        ((cursorPlacementEnabled_ && event->button() == Qt::LeftButton &&
          event->modifiers() == Qt::NoModifier) ||
         (cursorShortcutEnabled_ && event->button() == Qt::RightButton &&
          event->buttons() == Qt::RightButton && event->modifiers() == Qt::ShiftModifier))) {
        setCursorPlacementEnabled(false);
        leftClickPending_ = false;
        emit cursorPlacementRequested(event->position());
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (!editMode_ && transformTool_ != GizmoTool::None && renderer_ && !cameraDragActive_ &&
            event->buttons() == Qt::LeftButton &&
            (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ControlModifier)) {
            const auto handle =
                renderer_->gizmoHandle(*scene_, selectedEntity_, transformSpace_, transformPivot_);
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
        extendClick_ = editMode_ && event->modifiers() == Qt::ShiftModifier;
        leftClickPending_ = event->buttons() == Qt::LeftButton &&
                            (event->modifiers() == Qt::NoModifier || extendClick_) &&
                            !cameraDragActive_;
        event->accept();
        return;
    }
    if (event->button() != Qt::MiddleButton) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    setCursorPlacementEnabled(false);
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
                    preview, snapEnabled_ != event->modifiers().testFlag(Qt::ControlModifier))) {
                emit movePreviewed(preview);
            } else {
                emit interactionRejected(
                    transformTool_ == GizmoTool::Scale
                        ? QStringLiteral("缩放结果超出范围、过小或视线与拖动平面近平行；请调整拖动"
                                         "或输入数值。")
                        : QStringLiteral("当前变换无法表示为有效TRS，或视线与拖动平面近平行；可调整"
                                         "视角或切换局部坐标。"));
            }
        }
        event->accept();
        return;
    }
    if (!editMode_ && transformTool_ != GizmoTool::None && renderer_ != nullptr &&
        !cameraDragActive_) {
        const auto ray = renderer_->camera().screenRay(static_cast<float>(event->position().x()),
                                                       static_cast<float>(event->position().y()));
        hoveredAxis_ = GizmoController::pickAxis(
            ray, renderer_->gizmoHandle(*scene_, selectedEntity_, transformSpace_, transformPivot_),
            transformTool_);
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
                preview, snapEnabled_ != event->modifiers().testFlag(Qt::ControlModifier))) {
            emit movePreviewed(preview);
        }
        finishMove(true);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const bool clicked =
            leftClickPending_ && !cameraDragActive_ &&
            event->modifiers() == (extendClick_ ? Qt::ShiftModifier : Qt::NoModifier) &&
            rect().contains(event->position().toPoint()) &&
            (event->position() - leftPressPosition_).manhattanLength() <
                QApplication::startDragDistance();
        leftClickPending_ = false;
        if (clicked && renderer_ != nullptr) {
            renderer_->resize(width(), height());
            if (editMode_) {
                emit componentPickRequested(event->position(), extendClick_);
            } else {
                emit pickRequested(
                    renderer_->camera().screenRay(static_cast<float>(event->position().x()),
                                                  static_cast<float>(event->position().y())));
            }
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
    setCursorPlacementEnabled(false);
    gizmoController_.end();
    cameraDragActive_ = false;
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
    if (cursorPlacementEnabled_) {
        if (event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            setCursorPlacementEnabled(false);
            event->accept();
            return true;
        }
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
            event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Resize) {
            setCursorPlacementEnabled(false);
        }
    }
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
    renderer_->setViewportVisibility(visibility_);
    renderer_->setShadingMode(shadingMode_);
    if (!renderer_->initialize(*this)) {
        renderer_.reset();
        return;
    }

    renderer_->resize(width(), height());
    if (!cursorRenderer_.initialize(*this)) {
        qWarning() << "3D 游标覆盖层初始化失败。";
    }
    if (!snapRenderer_.initialize(*this)) {
        qWarning() << "吸附目标覆盖层初始化失败。";
    }
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
    cursorRenderer_.destroy();
    snapRenderer_.destroy();
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
