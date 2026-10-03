/*
 * 模块名: LoopCutSession
 * 功能概述: 以现有源边拾取预览整带，冻结目标后从before重算滑移。
 * 对外接口: LoopCutSession.h；依赖关系: Qt事件、ViewModel、只读源边拾取。
 * 输入输出: 两阶段鼠标/键盘到单次确认，覆盖层读取候选选区高亮新切线。
 * 异常与错误: 文本/IME、失活、文档和相机变化取消；无效值保留文字并禁止提交。
 * 维护说明: 滑移0为中点，200逻辑像素为100%；不重挂GL、不持有源几何指针。
 */
#include "LoopCutSession.h"

#include "ComponentPicker.h"
#include "KeymapRouter.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QEnterEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <cmath>

namespace mini3d::editor {
LoopCutSession::LoopCutSession(QMainWindow& window, SceneViewModel& model,
                               renderer_gl::ViewportWidget& viewport, QObject* parent)
    : QObject(parent), window_(window), model_(&model), viewport_(viewport) {
    hud_ = new QLabel(&viewport_);
    hud_->setObjectName(QStringLiteral("LoopCutHud"));
    hud_->setWordWrap(true);
    hud_->setAttribute(Qt::WA_TransparentForMouseEvents);
    hud_->setStyleSheet(QStringLiteral("background: #242424; color: #eeeeee; padding: 6px;"));
    hud_->move(10, 10);
    hud_->hide();
    qApp->installEventFilter(this);
    connect(&model, &SceneViewModel::componentTransformFinished, this, &LoopCutSession::clear);
    connect(&model, &QObject::destroyed, this, &LoopCutSession::clear);
    connect(&model, &SceneViewModel::operationFailed, this, [this](const QString& error) {
        if (state_)
            state_->error = error;
    });
    connect(&viewport_, &renderer_gl::ViewportWidget::cameraChanged, this, &LoopCutSession::cancel);
    connect(&viewport_, &renderer_gl::ViewportWidget::viewModeChanged, this,
            &LoopCutSession::cancel);
    connect(&viewport_, &renderer_gl::ViewportWidget::xRayChanged, this, &LoopCutSession::cancel);
}
bool LoopCutSession::start() {
    cancel();
    if (!model_ || !model_->isEditMode() || model_->previewCamera() != 0 || !viewport_.isVisible())
        return false;
    model_->cancelTransformEdit();
    viewport_.resetMoveInteraction();
    viewport_.setFocus(Qt::OtherFocusReason);
    const auto camera = viewport_.editorCameraSnapshot();
    if (!camera || !model_->beginLoopCut())
        return false;
    state_ = State{*camera};
    state_->snap = window_.findChild<QAction*>(QStringLiteral("SnapTransform"))->isChecked();
    state_->xRay = viewport_.isXRayEnabled();
    pointer_ = pointer_.value_or(viewport_.rect().center());
    viewport_.grabMouse();
    updateTarget();
    return true;
}
LoopCutStage LoopCutSession::stage() const {
    return state_ ? state_->stage : LoopCutStage::Inactive;
}
QString LoopCutSession::statusText() const {
    return status_;
}
void LoopCutSession::clear() {
    if (!state_)
        return;
    state_.reset();
    if (QWidget::mouseGrabber() == &viewport_)
        viewport_.releaseMouse();
    hud_->hide();
    status_ = QStringLiteral("就绪");
    emit statusTextChanged(status_);
}
void LoopCutSession::cancel() {
    if (!state_)
        return;
    clear();
    if (model_)
        model_->finishComponentTransform(false);
}
void LoopCutSession::publishStatus(const QString& error) {
    if (!state_)
        return;
    if (state_->stage == LoopCutStage::Preview) {
        status_ = QStringLiteral(
            "环切 · 预览面带（单切）\n鼠标移到源边 · 左键/Enter进入滑移 · Esc/右键取消");
    } else {
        const auto value = state_->numeric.text().empty()
                               ? QString::number(state_->slide * 100, 'f', 2)
                               : QString::fromStdString(state_->numeric.text());
        status_ =
            QStringLiteral("环切 · 滑移 %1%（0为中点） · 步进%2%3\n"
                           "左键/Enter确认 · 右键居中确认 · Esc整次取消 · Shift精细 · Ctrl反转步进")
                .arg(value,
                     state_->snap != state_->control ? QStringLiteral("开") : QStringLiteral("关"),
                     state_->fine ? QStringLiteral(" · 精细") : QString());
    }
    if (!error.isEmpty())
        status_ += QStringLiteral("\n%1").arg(error);
    hud_->setFixedWidth(std::max(80, std::min(480, viewport_.width() - 20)));
    hud_->setText(status_);
    hud_->adjustSize();
    hud_->show();
    hud_->raise();
    emit statusTextChanged(status_);
}
void LoopCutSession::updateTarget() {
    const auto hit =
        viewport_.rect().contains(pointer_->toPoint())
            ? pickComponent(*model_->scene(), *model_->assets(), model_->editedEntity(),
                            SelectionDomain::Edge, state_->camera,
                            {static_cast<float>(pointer_->x()), static_cast<float>(pointer_->y())},
                            {viewport_.width(), viewport_.height()}, state_->xRay,
                            model_->viewportVisibility())
            : std::nullopt;
    const auto seed =
        hit ? std::optional(core::modeling::EdgeKey(hit->first, hit->second)) : std::nullopt;
    state_->seed = seed;
    state_->error.clear();
    const bool valid = model_->previewLoopCut(seed, 0);
    if (!state_)
        return;
    state_->valid = valid;
    publishStatus(valid ? QString() : state_->error);
}
void LoopCutSession::updateSlide() {
    const auto numeric = state_->numeric.parse(-100, 100);
    if (numeric.state != NumericInputState::Empty && numeric.state != NumericInputState::Valid) {
        state_->valid = false;
        publishStatus(QStringLiteral("请输入-100与100之间的十进制百分比，不支持算式或单位。"));
        return;
    }
    double slide = numeric.value ? *numeric.value / 100 : state_->offset / 200;
    if (!numeric.value && state_->snap != state_->control)
        slide = std::round(slide / .1) * .1;
    state_->slide = slide;
    state_->error.clear();
    const bool valid = model_->previewLoopCut(state_->seed, slide);
    if (!state_)
        return;
    state_->valid = valid;
    publishStatus(valid ? QString() : state_->error);
}
void LoopCutSession::setFine(bool enabled) {
    if (state_->fine != enabled) {
        state_->anchorOffset = state_->offset;
        state_->anchor = *pointer_;
        state_->fine = enabled;
    }
}
void LoopCutSession::confirm() {
    if (!state_->valid) {
        publishStatus(QStringLiteral("当前候选无效，请修正输入或按Esc取消。"));
        return;
    }
    if (state_->stage == LoopCutStage::Preview) {
        state_->stage = LoopCutStage::Slide;
        state_->anchor = *pointer_;
        state_->anchorOffset = state_->offset = 0;
        state_->numeric.clear();
        publishStatus();
    } else {
        model_->finishComponentTransform(true);
    }
}
void LoopCutSession::centerAndConfirm() {
    if (state_->stage == LoopCutStage::Preview) {
        cancel();
        return;
    }
    // 右键中心是明确的新参数意图，允许从非法数值恢复为居中切线后确认。
    if (model_->previewLoopCut(state_->seed, 0))
        model_->finishComponentTransform(true);
    else if (state_)
        publishStatus(state_->error);
}
bool LoopCutSession::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (watched == &viewport_ && !state_) {
        if (event->type() == QEvent::MouseMove)
            pointer_ = static_cast<QMouseEvent*>(event)->position();
        else if (event->type() == QEvent::Enter)
            pointer_ = static_cast<QEnterEvent*>(event)->position();
    }
    if (!state_ || !widget || (widget != &window_ && !window_.isAncestorOf(widget)))
        return false;
    if (event->type() == QEvent::WindowDeactivate ||
        (watched == &viewport_ &&
         (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
          event->type() == QEvent::Resize || event->type() == QEvent::UngrabMouse))) {
        cancel();
        return false;
    }
    if (!widget->isVisible())
        return false;
    if (event->type() == QEvent::InputMethod &&
        !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty()) {
        cancel();
        return false;
    }
    const bool textIntent = event->type() == QEvent::KeyPress ||
                            event->type() == QEvent::ShortcutOverride ||
                            event->type() == QEvent::InputMethod;
    if (textIntent && (KeymapRouter::isTextInput(widget) ||
                       KeymapRouter::isTextInput(QApplication::focusWidget()))) {
        cancel();
        return false;
    }
    if (watched == &viewport_ && event->type() == QEvent::MouseMove) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (state_->stage == LoopCutStage::Slide) {
            setFine(mouse->modifiers().testFlag(Qt::ShiftModifier));
            state_->control = mouse->modifiers().testFlag(Qt::ControlModifier);
            state_->offset = state_->anchorOffset +
                             (mouse->position().x() - state_->anchor.x()) * (state_->fine ? .1 : 1);
        }
        pointer_ = mouse->position();
        if (state_->stage == LoopCutStage::Preview)
            updateTarget();
        else
            updateSlide();
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (watched != &viewport_ || !viewport_.rect().contains(mouse->position().toPoint())) {
            cancel();
            return watched == &viewport_;
        }
        pointer_ = mouse->position();
        if (mouse->button() == Qt::RightButton)
            centerAndConfirm();
        else if (mouse->button() == Qt::LeftButton)
            confirm();
        return true;
    }
    if (watched == &viewport_ &&
        (event->type() == QEvent::Wheel || event->type() == QEvent::MouseButtonRelease))
        return true;
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease &&
        event->type() != QEvent::ShortcutOverride)
        return false;
    const auto* key = static_cast<QKeyEvent*>(event);
    const auto modifiers = key->modifiers();
    const bool fileCommand =
        modifiers.testFlag(Qt::ControlModifier) &&
        (key->key() == Qt::Key_S || key->key() == Qt::Key_O || key->key() == Qt::Key_N);
    if (fileCommand || modifiers.testFlag(Qt::AltModifier) ||
        modifiers.testFlag(Qt::MetaModifier) || key->key() == Qt::Key_F1 ||
        key->key() == Qt::Key_Tab) {
        cancel();
        return false;
    }
    event->accept();
    if (event->type() == QEvent::ShortcutOverride)
        return true;
    const bool pressed = event->type() == QEvent::KeyPress;
    if (key->key() == Qt::Key_Shift)
        setFine(pressed);
    else if (key->key() == Qt::Key_Control) {
        state_->control = pressed;
        if (state_->stage == LoopCutStage::Slide)
            updateSlide();
    } else if (pressed && key->key() == Qt::Key_Escape)
        cancel();
    else if (pressed && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
        if (!key->isAutoRepeat())
            confirm();
    } else if (pressed && state_->stage == LoopCutStage::Slide &&
               !modifiers.testFlag(Qt::ControlModifier)) {
        if (key->key() == Qt::Key_Backspace)
            state_->numeric.backspace();
        else
            for (const auto character : key->text())
                state_->numeric.append(character.toLatin1());
        updateSlide();
    }
    return true;
}
} // namespace mini3d::editor
