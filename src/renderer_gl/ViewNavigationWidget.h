/*
 * 模块名: ViewNavigationWidget
 * 功能概述: 用实际相机方向绘制可点击、拖动的三轴和视图导航按钮。
 * 对外接口: ViewNavigationWidget；轴点和按钮坐标与绘制、命中共用。
 * 依赖关系: Qt Widgets、ViewportWidget、EditorCamera
 * 输入输出: 输入局部 Qt 事件，输出会话相机导航，不参与场景拾取。
 * 维护说明: 坐标均为逻辑像素；圆形遮罩让控件外的透明区域保留视口输入。
 */
#pragma once

#include "EditorCamera.h"

#include <QColor>
#include <QWidget>
#include <array>
#include <optional>

namespace mini3d::renderer_gl {
class ViewportWidget;

class ViewNavigationWidget final : public QWidget {
    Q_OBJECT
  public:
    enum class Control { Zoom, Pan, Camera, Projection };
    explicit ViewNavigationWidget(ViewportWidget& viewport);
    [[nodiscard]] std::optional<QPointF> axisPosition(EditorView view) const;
    [[nodiscard]] QPoint controlCenter(Control control) const;
    [[nodiscard]] bool isDragging() const;
    [[nodiscard]] bool hasPendingGesture() const;
    void synchronize();
    void cancelInteraction();
    void enableMouseCaptureRouting();

  protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    struct AxisDot {
        EditorView view;
        QPointF position;
        QColor color;
        QString label;
        float depth;
        bool positive;
    };
    enum class Gesture { None, Orbit, Zoom, Pan, Camera, Projection };
    [[nodiscard]] std::array<AxisDot, 6> axisDots() const;
    [[nodiscard]] std::optional<EditorView> axisAt(QPointF position) const;
    [[nodiscard]] std::optional<Control> controlAt(QPointF position) const;
    void updateHover(QPointF position);
    void finishInteraction();

    ViewportWidget* viewport_;
    Gesture gesture_ = Gesture::None;
    std::optional<EditorView> pressedAxis_;
    std::optional<EditorView> hoveredAxis_;
    std::optional<Control> hoveredControl_;
    QPointF pressPosition_;
    QPointF lastPosition_;
    bool dragging_ = false;
    bool hoverOrbit_ = false;
};
} // namespace mini3d::renderer_gl
