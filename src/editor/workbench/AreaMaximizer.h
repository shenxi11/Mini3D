/*
 * 模块名: AreaMaximizer
 * 功能概述: 临时最大化既有编辑区域，再精确恢复 Dock 布局与焦点。
 * 对外接口: AreaMaximizer
 * 依赖关系: Qt Widgets、InputArea，不持有场景或 OpenGL 资源
 * 输入输出: 区域意图到窗口显隐；保存布局前恢复原态。
 * 异常与错误: 拒绝不存在或隐藏的目标；内部保存的布局只用于当前窗口。
 * 维护说明: 不重挂中央 QOpenGLWidget，不把最大化状态保存成工作区布局。
 */
#pragma once

#include "editor/operations/KeymapRouter.h"

#include <QByteArray>
#include <QDockWidget>
#include <QVector>

namespace mini3d::editor {
/** @brief 仅改变区域布局；不改变文档、对象历史或场景可见性。 */
class AreaMaximizer final : public QObject {
    Q_OBJECT
  public:
    explicit AreaMaximizer(QMainWindow& window, QObject* parent = nullptr);
    /** @brief 未最大化时放大指定可见区域；已最大化时忽略参数并恢复。 */
    bool toggle(InputArea area);
    void restore();
    [[nodiscard]] bool isMaximized() const;
    [[nodiscard]] InputArea maximizedArea() const;

  signals:
    void maximizedAreaChanged(InputArea area);
    void operationRejected(const QString& reason);

  private:
    QMainWindow& window_;
    QByteArray savedLayout_;
    QPointer<QWidget> sourceFocus_;
    QPointer<QDockWidget> targetDock_;
    QDockWidget::DockWidgetFeatures savedFeatures_;
    struct DockState {
        QPointer<QDockWidget> dock;
        bool toggleEnabled;
        QRect floatingGeometry;
    };
    QVector<DockState> dockStates_;
    bool centralVisible_ = true;
    InputArea area_ = InputArea::None;
};
} // namespace mini3d::editor
