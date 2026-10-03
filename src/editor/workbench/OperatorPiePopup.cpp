/*
 * 模块名: OperatorPiePopup
 * 功能概述: 绘制环形扇区并用同一形状命中鼠标，提供数字键与取消入口。
 * 对外接口: OperatorPiePopup
 * 依赖关系: Qt Widgets、QPainter、OperatorRegistry
 * 输入输出: 鼠标/键盘与屏幕逻辑坐标到操作 ID、中文禁用说明和 Registry 执行。
 * 异常与错误: 执行前重新校验；关闭后才交付意图，避免弹窗失活取消新模态操作。
 * 维护说明: 无定时器、持久配置或额外动作框架；显示与命中共享环形扇区路径。
 */
#include "OperatorPiePopup.h"

#include <QApplication>
#include <QColor>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QScreen>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace mini3d::editor {
namespace {
constexpr int kMargin = 12;
constexpr int kDescriptionHeight = 72;
constexpr qreal kInnerRatio = .32;
} // namespace

OperatorPiePopup::OperatorPiePopup(OperatorRegistry& registry, OperatorContext context,
                                   const QString& title, const QStringList& operatorIds,
                                   QWidget* parent)
    : QDialog(parent, Qt::Popup), registry_(registry), context_(std::move(context)), title_(title),
      operatorIds_(operatorIds) {
    setObjectName(QStringLiteral("OperatorPiePopup"));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title_);
    setAccessibleName(title_);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    resize(480, 564);
    auto colors = palette();
    colors.setColor(QPalette::Window, QColor(QStringLiteral("#1d2430")));
    colors.setColor(QPalette::WindowText, QColor(QStringLiteral("#dce5ef")));
    setPalette(colors);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kMargin, kMargin, kMargin, kMargin);
    layout->setSpacing(kMargin);
    layout->addStretch();
    description_ = new QLabel(this);
    description_->setObjectName(QStringLiteral("OperatorPieDescription"));
    description_->setWordWrap(true);
    description_->setFixedHeight(kDescriptionHeight);
    layout->addWidget(description_);
    updateDescription();
}

void OperatorPiePopup::openAt(const QPoint& globalPosition) {
    sourceFocus_ = QApplication::focusWidget();
    hoveredIndex_ = -1;
    pressedIndex_ = -1;
    auto* screen = QApplication::screenAt(globalPosition);
    if (!screen)
        screen = QApplication::primaryScreen();
    QPoint position = globalPosition - pieBounds().center().toPoint();
    if (screen) {
        const auto available = screen->availableGeometry();
        resize(QSize(480, 564).boundedTo(available.size()));
        position = globalPosition - pieBounds().center().toPoint();
        position.setX(std::clamp(position.x(), available.left(), available.right() - width() + 1));
        position.setY(std::clamp(position.y(), available.top(), available.bottom() - height() + 1));
    }
    move(position);
    updateDescription();
    show();
    setFocus();
}

QRectF OperatorPiePopup::pieBounds() const {
    const auto diameter =
        std::min(width() - 2 * kMargin, height() - kDescriptionHeight - 3 * kMargin);
    return {(width() - diameter) / 2.0, kMargin, static_cast<qreal>(diameter),
            static_cast<qreal>(diameter)};
}

QPainterPath OperatorPiePopup::sectorPath(int index) const {
    const auto outer = pieBounds();
    const auto center = outer.center();
    const auto radius = outer.width() * kInnerRatio / 2;
    const QRectF inner(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius);
    const qreal sweep = 360.0 / operatorIds_.size();
    const qreal start = 90.0 - index * sweep - sweep / 2;
    QPainterPath path;
    path.arcMoveTo(outer, start);
    path.arcTo(outer, start, sweep);
    path.arcTo(inner, start + sweep, -sweep);
    path.closeSubpath();
    return path;
}

int OperatorPiePopup::indexAt(const QPointF& position) const {
    for (int index = 0; index < operatorIds_.size(); ++index) {
        if (sectorPath(index).contains(position))
            return index;
    }
    return -1;
}

void OperatorPiePopup::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().window());
    const auto bounds = pieBounds();
    const auto center = bounds.center();
    auto itemFont = font();
    itemFont.setPointSizeF(11);
    painter.setFont(itemFont);
    for (int index = 0; index < operatorIds_.size(); ++index) {
        const auto& id = operatorIds_[index];
        const auto* descriptor = registry_.descriptor(id);
        const auto name = descriptor ? descriptor->chineseName : QStringLiteral("未登记操作");
        const auto reason = registry_.disabledReason(id, context_);
        const bool disabled = !reason.isEmpty();
        const bool hovered = index == hoveredIndex_;
        painter.setPen(QPen(QColor(hovered ? QStringLiteral("#70c7ff") : QStringLiteral("#4c5b6f")),
                            hovered ? 2.0 : 1.0));
        painter.setBrush(QColor(hovered ? QStringLiteral("#304862") : QStringLiteral("#283343")));
        painter.drawPath(sectorPath(index));
        const auto angle = (90.0 - index * 360.0 / operatorIds_.size()) * std::numbers::pi / 180.0;
        const qreal labelRadius = bounds.width() * .34;
        const QPointF labelPosition(center.x() + std::cos(angle) * labelRadius,
                                    center.y() - std::sin(angle) * labelRadius);
        const qreal labelWidth = std::min(132.0, bounds.width() * .26);
        painter.setPen(QColor(disabled ? QStringLiteral("#939aa5") : QStringLiteral("#eef5fc")));
        const auto text = QStringLiteral("%1\n%2%3")
                              .arg(index + 1)
                              .arg(name, disabled ? QStringLiteral("\n不可用") : QString());
        painter.drawText(
            QRectF(labelPosition.x() - labelWidth / 2, labelPosition.y() - 42, labelWidth, 84),
            Qt::AlignCenter | Qt::TextWordWrap, text);
    }
    const auto radius = bounds.width() * kInnerRatio / 2;
    const QRectF inner(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius);
    painter.setPen(QColor(QStringLiteral("#4c5b6f")));
    painter.setBrush(palette().window());
    painter.drawEllipse(inner);
    painter.setPen(palette().windowText().color());
    itemFont.setBold(true);
    painter.setFont(itemFont);
    painter.drawText(inner.adjusted(6, 6, -6, -6), Qt::AlignCenter | Qt::TextWordWrap,
                     title_ + QStringLiteral("\n中心取消"));
}

void OperatorPiePopup::updateDescription() {
    if (hoveredIndex_ < 0) {
        description_->setText(
            QStringLiteral("鼠标选扇区 / 1–%1 执行 · Esc 或中心取消\n调用区域：%2")
                .arg(operatorIds_.size())
                .arg(OperatorRegistry::areaName(context_.area)));
        setToolTip(QString());
    } else {
        const auto& id = operatorIds_[hoveredIndex_];
        const auto* descriptor = registry_.descriptor(id);
        const auto reason = registry_.disabledReason(id, context_);
        description_->setText(
            QStringLiteral("%1 · %2\n%3")
                .arg(hoveredIndex_ + 1)
                .arg(descriptor ? descriptor->chineseName : QStringLiteral("未登记操作"))
                .arg(reason.isEmpty() ? QStringLiteral("点击或数字键执行，Esc 取消。") : reason));
        setToolTip(description_->text());
    }
    update();
}

void OperatorPiePopup::executeIndex(int index) {
    const auto id = operatorIds_[index];
    hoveredIndex_ = index;
    if (!registry_.disabledReason(id, context_).isEmpty()) {
        updateDescription();
        return;
    }
    const auto context = context_;
    auto* registry = &registry_;
    const auto sourceFocus = sourceFocus_;
    QDialog::accept();
    if (sourceFocus && sourceFocus->isVisible())
        sourceFocus->setFocus();
    registry->execute(id, context);
}

void OperatorPiePopup::reject() {
    const auto sourceFocus = sourceFocus_;
    QDialog::reject();
    if (sourceFocus && sourceFocus->isVisible())
        sourceFocus->setFocus();
}

void OperatorPiePopup::mouseMoveEvent(QMouseEvent* event) {
    hoveredIndex_ = indexAt(event->position());
    updateDescription();
    event->accept();
}

void OperatorPiePopup::mousePressEvent(QMouseEvent* event) {
    pressedIndex_ = indexAt(event->position());
    if (event->button() != Qt::LeftButton || pressedIndex_ < 0) {
        reject();
    } else {
        hoveredIndex_ = pressedIndex_;
        updateDescription();
    }
    event->accept();
}

void OperatorPiePopup::mouseReleaseEvent(QMouseEvent* event) {
    const auto index = indexAt(event->position());
    if (event->button() == Qt::LeftButton && index >= 0 && index == pressedIndex_) {
        executeIndex(index);
    } else {
        reject();
    }
    event->accept();
}

void OperatorPiePopup::keyPressEvent(QKeyEvent* event) {
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        reject();
        event->accept();
        return;
    }
    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9 &&
        (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier)) {
        const auto index = event->key() - Qt::Key_1;
        if (index < operatorIds_.size())
            executeIndex(index);
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}
} // namespace mini3d::editor
