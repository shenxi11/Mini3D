#include "ViewNavigationWidget.h"

#include "ViewportWidget.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QWheelEvent>
#include <algorithm>
#include <glm/mat3x3.hpp>

namespace mini3d::renderer_gl {
namespace {
const QPointF kOrbitCenter(46, 44);
constexpr qreal kOrbitRadius = 40;
constexpr qreal kAxisLength = 29;
constexpr qreal kDotRadius = 8;
constexpr qreal kButtonRadius = 11;
constexpr std::array<ViewNavigationWidget::Control, 4> kControls{
    ViewNavigationWidget::Control::Zoom, ViewNavigationWidget::Control::Pan,
    ViewNavigationWidget::Control::Camera, ViewNavigationWidget::Control::Projection};

QString viewName(EditorView view) {
    switch (view) {
        case EditorView::Right: return QStringLiteral("右视图（+X）");
        case EditorView::Left: return QStringLiteral("左视图（-X）");
        case EditorView::Top: return QStringLiteral("顶视图（+Y）");
        case EditorView::Bottom: return QStringLiteral("底视图（-Y）");
        case EditorView::Front: return QStringLiteral("前视图（+Z）");
        case EditorView::Back: return QStringLiteral("后视图（-Z）");
        case EditorView::Orbit: return {};
    }
    return {};
}
EditorView oppositeView(EditorView view) {
    switch (view) {
        case EditorView::Right: return EditorView::Left;
        case EditorView::Left: return EditorView::Right;
        case EditorView::Top: return EditorView::Bottom;
        case EditorView::Bottom: return EditorView::Top;
        case EditorView::Front: return EditorView::Back;
        case EditorView::Back: return EditorView::Front;
        case EditorView::Orbit: return EditorView::Orbit;
    }
    return EditorView::Orbit;
}
} // namespace

ViewNavigationWidget::ViewNavigationWidget(ViewportWidget& viewport)
    : QWidget(&viewport), viewport_(&viewport) {
    setObjectName(QStringLiteral("ViewNavigationWidget"));
    setAccessibleName(QStringLiteral("视图导航"));
    setFixedSize(96, 200);
    setStyleSheet(QStringLiteral("#ViewNavigationWidget { background: transparent; }"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    QRegion input(QRect(6, 4, 80, 80), QRegion::Ellipse);
    for (const auto control : kControls) {
        const auto center = controlCenter(control);
        input += QRegion(QRect(center - QPoint(11, 11), QSize(23, 23)), QRegion::Ellipse);
    }
    setMask(input);
    connect(&viewport, &ViewportWidget::cameraChanged, this,
            [this] { synchronize(); });
    connect(&viewport, &ViewportWidget::viewModeChanged, this,
            &ViewNavigationWidget::synchronize);
}

std::array<ViewNavigationWidget::AxisDot, 6> ViewNavigationWidget::axisDots() const {
    const auto camera = viewport_->editorCameraSnapshot().value_or(EditorCamera{});
    const auto rotation = glm::mat3(camera.viewMatrix());
    std::array<AxisDot, 6> dots;
    const std::array<EditorView, 6> views{EditorView::Right, EditorView::Left, EditorView::Top,
                                         EditorView::Bottom, EditorView::Front, EditorView::Back};
    const std::array<QColor, 3> colors{QColor(246, 65, 94), QColor(135, 199, 43),
                                      QColor(50, 148, 235)};
    const std::array<QString, 3> labels{QStringLiteral("X"), QStringLiteral("Y"),
                                       QStringLiteral("Z")};
    for (int i = 0; i < 6; ++i) {
        glm::vec3 direction(0);
        direction[i / 2] = i % 2 == 0 ? 1.0F : -1.0F;
        const auto screen = rotation * direction;
        dots[i] = {views[i], kOrbitCenter + QPointF(screen.x, -screen.y) * kAxisLength,
                   colors[i / 2], labels[i / 2], screen.z, i % 2 == 0};
    }
    std::stable_sort(dots.begin(), dots.end(),
                     [](const AxisDot& a, const AxisDot& b) { return a.depth < b.depth; });
    return dots;
}

std::optional<QPointF> ViewNavigationWidget::axisPosition(EditorView view) const {
    for (const auto& dot : axisDots())
        if (dot.view == view)
            return dot.position;
    return std::nullopt;
}
QPoint ViewNavigationWidget::controlCenter(Control control) const {
    return {77, 98 + 28 * static_cast<int>(control)};
}
std::optional<EditorView> ViewNavigationWidget::axisAt(QPointF position) const {
    const auto dots = axisDots();
    for (auto dot = dots.rbegin(); dot != dots.rend(); ++dot)
        if (QLineF(position, dot->position).length() <= kDotRadius + 2)
            return dot->view;
    return std::nullopt;
}
std::optional<ViewNavigationWidget::Control>
ViewNavigationWidget::controlAt(QPointF position) const {
    for (const auto control : kControls)
        if (QLineF(position, controlCenter(control)).length() <= kButtonRadius)
            return control;
    return std::nullopt;
}
bool ViewNavigationWidget::isDragging() const { return dragging_; }
bool ViewNavigationWidget::hasPendingGesture() const { return gesture_ != Gesture::None; }

void ViewNavigationWidget::synchronize() {
    if (viewport_->isPreviewingCamera())
        cancelInteraction();
    if (hoveredControl_ == Control::Projection)
        setToolTip(viewport_->isOrthographic() ? QStringLiteral("切换为透视投影")
                                              : QStringLiteral("切换为正交投影"));
    if (hoveredControl_ == Control::Camera)
        setToolTip(viewport_->isPreviewingCamera() ? QStringLiteral("返回编辑视图")
                                                  : QStringLiteral("切换相机预览"));
    update();
}
void ViewNavigationWidget::enableMouseCaptureRouting() {
    // 初始化 GL 时，编辑器输入适配器已安装；此窄过滤器先处理抓鼠标期间的导航命中。
    qApp->installEventFilter(this);
}
bool ViewNavigationWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched != viewport_ || event->type() != QEvent::MouseButtonPress ||
        mouseGrabber() != viewport_ || !isVisible())
        return false;
    const auto* mouse = static_cast<QMouseEvent*>(event);
    const QPointF local = mapFromGlobal(mouse->globalPosition());
    if (mouse->button() != Qt::LeftButton || !mask().contains(local.toPoint()))
        return false;
    if (viewport_->beginViewNavigation()) {
        QMouseEvent redirected(QEvent::MouseButtonPress, local, mouse->globalPosition(),
                                mouse->button(), mouse->buttons(), mouse->modifiers());
        QApplication::sendEvent(this, &redirected);
    }
    event->accept();
    return true;
}
void ViewNavigationWidget::cancelInteraction() {
    gesture_ = Gesture::None;
    pressedAxis_.reset();
    dragging_ = false;
    viewport_->endViewNavigation();
    if (mouseGrabber() == this)
        releaseMouse();
    unsetCursor();
    update();
}
void ViewNavigationWidget::finishInteraction() {
    cancelInteraction();
    if (hasFocus())
        viewport_->setFocus(Qt::OtherFocusReason);
}
bool ViewNavigationWidget::event(QEvent* event) {
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate ||
        event->type() == QEvent::Hide || event->type() == QEvent::UngrabMouse) {
        cancelInteraction();
    }
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && hasPendingGesture()) {
        if (event->type() == QEvent::KeyPress)
            finishInteraction();
        event->accept();
        return true;
    }
    return QWidget::event(event);
}
void ViewNavigationWidget::updateHover(QPointF position) {
    hoverOrbit_ = QLineF(position, kOrbitCenter).length() <= kOrbitRadius;
    hoveredAxis_ = hoverOrbit_ ? axisAt(position) : std::nullopt;
    hoveredControl_ = controlAt(position);
    QString tooltip = QStringLiteral("拖动旋转视图；点击轴圆点切换观察方向");
    if (hoveredAxis_) {
        const auto camera = viewport_->editorCameraSnapshot();
        tooltip = viewName(camera && camera->view() == *hoveredAxis_
                               ? oppositeView(*hoveredAxis_) : *hoveredAxis_);
    }
    else if (hoveredControl_) {
        switch (*hoveredControl_) {
            case Control::Zoom: tooltip = QStringLiteral("拖动缩放视图；点击放大"); break;
            case Control::Pan: tooltip = QStringLiteral("拖动平移视图"); break;
            case Control::Camera:
                tooltip = viewport_->isPreviewingCamera() ? QStringLiteral("返回编辑视图")
                                                         : QStringLiteral("切换相机预览");
                break;
            case Control::Projection:
                tooltip = viewport_->isOrthographic() ? QStringLiteral("切换为透视投影")
                                                     : QStringLiteral("切换为正交投影");
                break;
        }
    }
    setToolTip(tooltip);
    update();
}
void ViewNavigationWidget::leaveEvent(QEvent* event) {
    hoverOrbit_ = false;
    hoveredAxis_.reset();
    hoveredControl_.reset();
    update();
    QWidget::leaveEvent(event);
}

void ViewNavigationWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (viewport_->isPreviewingCamera() || !viewport_->editorCameraSnapshot())
        painter.setOpacity(0.45);
    if (hoverOrbit_ || gesture_ == Gesture::Orbit) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(175, 175, 175, 60));
        painter.drawEllipse(kOrbitCenter, kOrbitRadius, kOrbitRadius);
    }
    const auto dots = axisDots();
    for (const auto& dot : dots) {
        if (dot.positive) {
            painter.setPen(QPen(dot.color, 2));
            painter.drawLine(kOrbitCenter, dot.position);
        }
    }
    auto font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);
    for (const auto& dot : dots) {
        const auto color = dot.depth < 0 ? dot.color.darker(120) : dot.color;
        painter.setPen(QPen(hoveredAxis_ == dot.view ? QColor(240, 240, 240) : color, 1.4));
        painter.setBrush(dot.positive ? color : QColor(56, 56, 56, 170));
        painter.drawEllipse(dot.position, kDotRadius, kDotRadius);
        if (dot.positive) {
            painter.setPen(QColor(25, 25, 25));
            painter.drawText(QRectF(dot.position - QPointF(8, 8), QSizeF(16, 16)),
                             Qt::AlignCenter, dot.label);
        }
    }
    for (const auto control : kControls) {
        painter.save();
        if (control == Control::Camera)
            painter.setOpacity(1);
        const auto center = controlCenter(control);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hoveredControl_ == control ? QColor(105, 105, 105, 220)
                                                   : QColor(56, 56, 56, 190));
        painter.drawEllipse(QPointF(center), kButtonRadius, kButtonRadius);
        painter.translate(center);
        painter.setPen(QPen(control == Control::Camera && viewport_->isPreviewingCamera()
                                ? QColor(239, 172, 70) : QColor(218, 218, 218), 1.4));
        painter.setBrush(Qt::NoBrush);
        switch (control) {
            case Control::Zoom:
                painter.drawEllipse(QPointF(-1.5, -1.5), 4.5, 4.5);
                painter.drawLine(QPointF(2, 2), QPointF(6, 6));
                painter.drawLine(QPointF(-4, -1.5), QPointF(1, -1.5));
                painter.drawLine(QPointF(-1.5, -4), QPointF(-1.5, 1));
                break;
            case Control::Pan: {
                QPainterPath hand;
                hand.moveTo(-5, 1); hand.lineTo(-5, -1); hand.lineTo(-2, 1);
                hand.lineTo(-2, -5); hand.lineTo(0, -5); hand.lineTo(0, -1);
                hand.lineTo(0, -6); hand.lineTo(2, -6); hand.lineTo(2, -1);
                hand.lineTo(2, -5); hand.lineTo(4, -5); hand.lineTo(4, 0);
                hand.lineTo(4, -3); hand.lineTo(6, -3); hand.lineTo(6, 2);
                hand.lineTo(3, 6); hand.lineTo(-1, 6); hand.closeSubpath();
                painter.drawPath(hand);
                break;
            }
            case Control::Camera:
                painter.drawRect(QRectF(-6, -4, 8, 8));
                painter.drawPolygon(QPolygonF{QPointF(2, -2), QPointF(6, -4),
                                               QPointF(6, 4), QPointF(2, 2)});
                break;
            case Control::Projection: {
                const qreal top = viewport_->isOrthographic() ? 6 : 3;
                painter.drawPolygon(QPolygonF{QPointF(-top, -5), QPointF(top, -5),
                                               QPointF(6, 5), QPointF(-6, 5)});
                painter.drawLine(QPointF(0, -5), QPointF(0, 5));
                painter.drawLine(QPointF(-4.5, 0), QPointF(4.5, 0));
                break;
            }
        }
        painter.restore();
    }
}

void ViewNavigationWidget::mousePressEvent(QMouseEvent* event) {
    event->accept();
    if (event->button() != Qt::LeftButton) {
        finishInteraction();
        return;
    }
    const auto control = controlAt(event->position());
    if (!control && QLineF(event->position(), kOrbitCenter).length() > kOrbitRadius)
        return;
    if (viewport_->isPreviewingCamera() && control != Control::Camera) {
        viewport_->beginViewNavigation();
        return;
    }
    cancelInteraction();
    setFocus(Qt::MouseFocusReason);
    pressPosition_ = lastPosition_ = event->position();
    pressedAxis_ = axisAt(pressPosition_);
    gesture_ = control ? static_cast<Gesture>(static_cast<int>(*control) + 2) : Gesture::Orbit;
    viewport_->setNavigationActive(true);
    grabMouse();
    updateHover(event->position());
}
void ViewNavigationWidget::mouseMoveEvent(QMouseEvent* event) {
    event->accept();
    updateHover(event->position());
    if (!hasPendingGesture())
        return;
    if (!event->buttons().testFlag(Qt::LeftButton)) {
        finishInteraction();
        return;
    }
    if (gesture_ == Gesture::Camera || gesture_ == Gesture::Projection)
        return;
    if (!dragging_) {
        if ((event->position() - pressPosition_).manhattanLength() <
            QApplication::startDragDistance())
            return;
        if (!viewport_->beginViewNavigation()) {
            finishInteraction();
            return;
        }
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
    }
    const auto delta = event->position() - lastPosition_;
    lastPosition_ = event->position();
    if (gesture_ == Gesture::Orbit)
        viewport_->orbitViewNavigation(delta);
    else if (gesture_ == Gesture::Pan)
        viewport_->panViewNavigation(delta);
    else if (gesture_ == Gesture::Zoom)
        viewport_->zoomViewNavigation(static_cast<float>(-delta.y() * 0.08));
}
void ViewNavigationWidget::mouseReleaseEvent(QMouseEvent* event) {
    event->accept();
    if (event->button() != Qt::LeftButton)
        return;
    const auto gesture = gesture_;
    const auto axis = pressedAxis_;
    const bool clicked = !dragging_ &&
                         (event->position() - pressPosition_).manhattanLength() <
                             QApplication::startDragDistance();
    finishInteraction();
    if (!clicked || gesture == Gesture::None)
        return;
    if (gesture == Gesture::Orbit && axis && axisAt(event->position()) == axis) {
        const auto camera = viewport_->editorCameraSnapshot();
        const auto view = camera && camera->view() == *axis ? oppositeView(*axis) : *axis;
        if (viewport_->beginViewNavigation())
            viewport_->setCameraView(view);
    } else if (gesture == Gesture::Camera && controlAt(event->position()) == Control::Camera) {
        viewport_->requestCameraPreviewToggle();
    } else if (gesture == Gesture::Projection &&
               controlAt(event->position()) == Control::Projection) {
        if (viewport_->beginViewNavigation())
            viewport_->setOrthographic(!viewport_->isOrthographic());
    } else if (gesture == Gesture::Zoom && controlAt(event->position()) == Control::Zoom) {
        if (viewport_->beginViewNavigation())
            viewport_->zoomViewNavigation(1);
    }
    viewport_->endViewNavigation();
}
void ViewNavigationWidget::wheelEvent(QWheelEvent* event) {
    event->accept();
    if (event->angleDelta().y() != 0 && viewport_->beginViewNavigation())
        viewport_->zoomViewNavigation(static_cast<float>(event->angleDelta().y()) / 120.0F);
    viewport_->endViewNavigation();
}
} // namespace mini3d::renderer_gl
