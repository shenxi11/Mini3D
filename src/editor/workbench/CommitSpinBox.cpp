/*
 * 模块名: CommitSpinBox
 * 功能概述: 实现非破坏数值编辑与基于起点重算的 Alt 拖动候选值。
 * 对外接口: CommitSpinBox
 * 依赖关系: Qt Widgets/Locale
 * 输入输出: Enter/失焦提交合法值，Esc 取消，非法内容保留且提示。
 * 异常与错误: 失败不写模型；业务层拒绝后恢复用户输入文本而非掩盖错误。
 * 维护说明: 不使用表达式求值；程序 setValue 仍沿用现有 ViewModel 信号通道。
 */
#include "CommitSpinBox.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QStyle>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace mini3d::editor {
CommitSpinBox::CommitSpinBox(QWidget* parent) : QDoubleSpinBox(parent) {
    setKeyboardTracking(false);
    lineEdit()->installEventFilter(this);
    setToolTip(QStringLiteral("Enter 提交；Esc 恢复；Alt＋左键横向拖动微调，松开提交一次。"));
}

QString CommitSpinBox::validationMessage() const {
    return validationMessage_;
}
void CommitSpinBox::resetInput() {
    cancelInput();
}

void CommitSpinBox::rejectSubmission(const QString& reason) {
    if (submitting_) {
        submissionError_ = reason;
    }
}

QValidator::State CommitSpinBox::validate(QString& text, int& position) const {
    const auto result = QDoubleSpinBox::validate(text, position);
    // 允许暂存非法内容；提交时统一校验，不能静默丢弃按键或把非法输入改成旧值。
    return result == QValidator::Invalid ? QValidator::Intermediate : result;
}

QString CommitSpinBox::formattedValue(double value) const {
    return prefix() + textFromValue(value) + suffix();
}

void CommitSpinBox::showValidation(const QString& reason) {
    validationMessage_ = reason;
    setProperty("invalidInput", !reason.isEmpty());
    setAccessibleDescription(reason);
    style()->unpolish(this);
    style()->polish(this);
    if (!reason.isEmpty()) {
        QToolTip::showText(mapToGlobal(rect().bottomLeft()), reason, this);
    } else {
        QToolTip::hideText();
    }
    update();
}

bool CommitSpinBox::submitInput() {
    if (submitting_ || scrubbing_ || !isEnabled()) {
        return false;
    }
    const auto input = lineEdit()->text();
    bool parsed = false;
    const double candidate = locale().toDouble(cleanText(), &parsed);
    if (!parsed || !std::isfinite(candidate) || candidate < minimum() || candidate > maximum()) {
        showValidation(QStringLiteral("请输入 %1 至 %2 之间的有限数字；Esc 可恢复原值。")
                           .arg(minimum())
                           .arg(maximum()));
        return false;
    }
    submissionError_.clear();
    submitting_ = true;
    setValue(candidate);
    submitting_ = false;
    if (!submissionError_.isEmpty()) {
        // 业务失败信号可能已刷新 Inspector；最终仍保留这次输入和准确的业务原因。
        lineEdit()->setText(input);
        showValidation(submissionError_);
        return false;
    }
    lineEdit()->setText(formattedValue(value()));
    showValidation({});
    emit editingFinished();
    return true;
}

void CommitSpinBox::cancelInput() {
    if (scrubbing_) {
        scrubbing_ = false;
        lineEdit()->releaseMouse();
    }
    lineEdit()->setText(formattedValue(value()));
    showValidation({});
}

void CommitSpinBox::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        cancelInput();
        event->accept();
    } else if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
        submitInput();
        event->accept();
    } else {
        QDoubleSpinBox::keyPressEvent(event);
    }
}

void CommitSpinBox::focusOutEvent(QFocusEvent* event) {
    if (scrubbing_) {
        cancelInput();
    } else {
        submitInput();
    }
    // 不调用 SpinBox 的 interpretText，避免其在失败时自动替换用户输入。
    QWidget::focusOutEvent(event);
}

void CommitSpinBox::stepBy(int steps) {
    if (submitInput()) {
        QDoubleSpinBox::stepBy(steps);
    }
}

bool CommitSpinBox::event(QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate && scrubbing_) {
        cancelInput();
    }
    return QDoubleSpinBox::event(event);
}

bool CommitSpinBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched != lineEdit()) {
        return QDoubleSpinBox::eventFilter(watched, event);
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return ||
            key->key() == Qt::Key_Enter) {
            keyPressEvent(key);
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && mouse->modifiers().testFlag(Qt::AltModifier)) {
            cancelInput();
            setFocus(Qt::MouseFocusReason);
            scrubbing_ = true;
            scrubStartValue_ = value();
            scrubStartX_ = mouse->globalPosition().x();
            lineEdit()->grabMouse();
            return true;
        }
    }
    if (scrubbing_ && event->type() == QEvent::MouseMove) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const auto candidate = std::clamp(
            scrubStartValue_ + (mouse->globalPosition().x() - scrubStartX_) * singleStep() * 0.1,
            minimum(), maximum());
        lineEdit()->setText(formattedValue(candidate));
        return true;
    }
    if (scrubbing_ && event->type() == QEvent::MouseButtonRelease) {
        if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            scrubbing_ = false;
            lineEdit()->releaseMouse();
            submitInput();
            return true;
        }
    }
    return QDoubleSpinBox::eventFilter(watched, event);
}
} // namespace mini3d::editor
