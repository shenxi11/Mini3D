/*
 * 模块名: AreaMaximizer
 * 功能概述: 保持窗口/GL 控件身份，通过隐藏其他区域实现临时最大化。
 * 对外接口: AreaMaximizer
 * 依赖关系: QMainWindow、QDockWidget、焦点与可见性
 * 输入输出: 捕获的 Dock 状态到临时布局，再恢复原布局与焦点。
 * 异常与错误: 隐藏目标不被静默打开；布局字节来自同一窗口与版本。
 * 维护说明: 最大化时禁止 Dock 关闭/浮动和其他 Dock 显隐，以免丢失还原入口。
 */
#include "AreaMaximizer.h"

#include <QAction>
#include <QApplication>
#include <QMainWindow>

namespace mini3d::editor {
namespace {
constexpr int kLayoutVersion = 2;
}
AreaMaximizer::AreaMaximizer(QMainWindow& window, QObject* parent)
    : QObject(parent), window_(window) {}

bool AreaMaximizer::isMaximized() const {
    return area_ != InputArea::None;
}
InputArea AreaMaximizer::maximizedArea() const {
    return area_;
}

bool AreaMaximizer::toggle(InputArea area) {
    if (isMaximized()) {
        restore();
        return true;
    }
    const auto name = area == InputArea::Outliner     ? QStringLiteral("SceneDock")
                      : area == InputArea::Properties ? QStringLiteral("InspectorDock")
                      : area == InputArea::Console    ? QStringLiteral("ConsoleDock")
                                                      : QString();
    auto* dock = name.isEmpty() ? nullptr : window_.findChild<QDockWidget*>(name);
    QWidget* target = area == InputArea::Viewport ? window_.centralWidget() : dock;
    if (!target || !target->isVisible()) {
        emit operationRejected(QStringLiteral("请从可见编辑区域调用最大化。"));
        return false;
    }
    savedLayout_ = window_.saveState(kLayoutVersion);
    sourceFocus_ = QApplication::focusWidget();
    centralVisible_ = !window_.centralWidget()->isHidden();
    targetDock_ = dock;
    for (auto* item : window_.findChildren<QDockWidget*>()) {
        auto* toggle = item->toggleViewAction();
        dockStates_.append(
            {item, toggle->isEnabled(), item->isFloating() ? item->geometry() : QRect{}});
        toggle->setEnabled(false);
        if (item != dock) {
            item->hide();
        }
    }
    if (dock) {
        savedFeatures_ = dock->features();
        dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
        dock->setFloating(false);
    }
    window_.centralWidget()->setVisible(area == InputArea::Viewport);
    area_ = area;
    auto* focusTarget =
        dock ? dock->widget() : target->findChild<QWidget*>(QStringLiteral("ViewportWidget"));
    focusTarget->setFocus(Qt::OtherFocusReason);
    emit maximizedAreaChanged(area_);
    return true;
}

void AreaMaximizer::restore() {
    if (!isMaximized()) {
        return;
    }
    area_ = InputArea::None;
    window_.centralWidget()->setVisible(centralVisible_);
    window_.restoreState(savedLayout_, kLayoutVersion);
    if (targetDock_) {
        targetDock_->setFeatures(savedFeatures_);
    }
    for (const auto& state : dockStates_) {
        if (state.dock) {
            state.dock->toggleViewAction()->setEnabled(state.toggleEnabled);
            if (state.dock->isFloating() && state.floatingGeometry.isValid()) {
                // restoreState 会把浮窗夹到屏幕内；临时最大化须还原原始逻辑坐标。
                state.dock->setGeometry(state.floatingGeometry);
            }
        }
    }
    dockStates_.clear();
    targetDock_.clear();
    savedLayout_.clear();
    if (sourceFocus_ && sourceFocus_->isVisible()) {
        sourceFocus_->setFocus(Qt::OtherFocusReason);
    }
    sourceFocus_.clear();
    emit maximizedAreaChanged(area_);
}
} // namespace mini3d::editor
