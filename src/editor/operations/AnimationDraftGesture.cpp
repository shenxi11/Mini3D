/*
 * 模块名: AnimationDraftGesture
 * 功能概述: 以冻结evaluated父矩阵进行草稿移动，保留原始连续Euler旋转与缩放值。
 * 对外接口: AnimationDraftGesture；依赖关系: Qt事件、动画ViewModel与只读视口。
 * 输入输出: G世界位移、R轴数值、S倍率到草稿许可通道。
 * 异常与错误: 不完整数字/自由旋转拒绝；失焦只撤销当前手势，不取消完整草稿。
 * 维护说明: 不写真源、不生成历史；确认手势后仍须确认草稿才能录键。
 */
#include "AnimationDraftGesture.h"
#include "KeymapRouter.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <cmath>
#include <limits>

namespace mini3d::editor {
namespace {
int channelFor(TransformOperation operation) {
    return operation == TransformOperation::Move ? 0 : operation == TransformOperation::Rotate ? 1 : 2;
}
} // namespace
AnimationDraftGesture::AnimationDraftGesture(QMainWindow& window, SceneViewModel& model,
                                            renderer_gl::ViewportWidget& viewport, QObject* parent)
    : QObject(parent), window_(window), model_(model), viewport_(viewport) {
    qApp->installEventFilter(this);
    connect(&model_, &SceneViewModel::animationSessionChanged, this, [this] {
        if (model_.animationMode() != renderer_gl::AnimationMode::PoseDraft && state_) {
            state_.reset();
            emit activeChanged(false);
        }
    });
}
bool AnimationDraftGesture::isActive() const { return state_.has_value(); }
bool AnimationDraftGesture::start(TransformOperation operation) {
    cancel();
    if (state_)
        return false;
    const auto values = model_.animationDraftValues();
    const auto pose = model_.installedAnimationPose();
    const auto camera = viewport_.editorCameraSnapshot();
    const auto id = model_.selection()->selectedEntity();
    const auto* node = model_.scene()->find(id);
    if (model_.animationMode() != renderer_gl::AnimationMode::PoseDraft || !values || !pose ||
        !camera || !node || !model_.animationDraftMask()[channelFor(operation)]) {
        emit model_.operationFailed(QStringLiteral("此通道未纳入姿态草稿，不能执行该变换。"));
        return false;
    }
    const auto* evaluated = pose->numerics->find(id);
    const auto* parent = node->parent ? pose->numerics->find(node->parent) : nullptr;
    if (!evaluated || (node->parent && !parent))
        return false;
    state_ = State{operation, *values, *camera,
                    parent ? glm::dmat4(parent->worldInverse) : glm::dmat4(1),
                    glm::vec3(evaluated->world[3]), pointer_};
    ++gestureGeneration_;
    viewport_.setFocus(Qt::OtherFocusReason);
    emit activeChanged(true);
    updatePreview();
    return isActive();
}
void AnimationDraftGesture::cancel() { finish(false); }
void AnimationDraftGesture::finish(bool commit) {
    if (!state_)
        return;
    if (commit && !state_->valid)
        return;
    if (!commit && model_.animationMode() == renderer_gl::AnimationMode::PoseDraft) {
        const auto generation = gestureGeneration_;
        const auto channel = channelFor(state_->operation);
        const auto before = state_->before[channel];
        const bool restored = model_.setAnimationDraftChannel(
            static_cast<core::AnimationChannel>(channel), before);
        if (!state_ || gestureGeneration_ != generation)
            return;
        if (!restored) {
            emit statusTextChanged(QStringLiteral("未能撤销当前手势；请结束阻塞后重试 Esc。完整草稿尚未确认。"));
            return;
        }
    }
    state_.reset();
    emit activeChanged(false);
}
void AnimationDraftGesture::updatePreview() {
    if (!state_)
        return;
    auto& state = *state_;
    const int channel = channelFor(state.operation);
    const auto bound = channel == 1 ? core::kAnimationMaximumRotationDegrees
                                   : static_cast<double>(std::numeric_limits<float>::max());
    const auto number = state.numeric.parse(-bound, bound);
    auto value = state.before[channel];
    QString error;
    if (number.state != NumericInputState::Empty && number.state != NumericInputState::Valid) {
        error = QStringLiteral("请输入完整有限数值；不支持算式或单位。");
    } else if (channel == 1) {
        if (state.axis < 0 || !number.value)
            error = QStringLiteral("旋转请输入 X/Y/Z + 数值（连续度数）；不支持自由鼠标旋转。");
        else
            value[state.axis] += *number.value;
    } else if (channel == 2) {
        const auto factor = number.value.value_or(1 + (pointer_.x() - state.pointerStart.x()) / 150.0);
        if (state.axis < 0)
            value *= factor;
        else
            value[state.axis] *= factor;
    } else {
        const auto delta = pointer_ - state.pointerStart;
        const auto basis = glm::dmat3(glm::inverse(state.camera.viewMatrix()));
        const auto units = static_cast<double>(state.camera.worldUnitsPerPixel(state.origin));
        glm::dvec3 worldDelta = units * (basis[0] * delta.x() - basis[1] * delta.y());
        if (state.axis >= 0) {
            glm::dvec3 direction(0);
            direction[state.axis] = 1;
            worldDelta = direction * number.value.value_or(worldDelta[state.axis]);
        } else if (number.value) {
            const auto length = glm::length(worldDelta);
            worldDelta = (length > 1e-12 ? worldDelta / length : basis[0]) * *number.value;
        }
        value += glm::dvec3(state.parentInverse * glm::dvec4(worldDelta, 0));
    }
    const auto generation = gestureGeneration_;
    const auto axisText = state.axis < 0 ? QStringLiteral("自由/整体") : QString(QChar('X' + state.axis));
    const auto numericText = QString::fromStdString(state.numeric.text());
    const bool valid = error.isEmpty() &&
                       model_.setAnimationDraftChannel(static_cast<core::AnimationChannel>(channel), value);
    // 同步提供者/通知可能取消并重新开始手势；返回后不再解引用旧 State。
    if (!state_ || gestureGeneration_ != generation)
        return;
    state_->valid = valid;
    if (!valid && error.isEmpty())
        error = QStringLiteral("候选姿态无效；草稿保持上一有效值，请修正输入。");
    emit statusTextChanged(QStringLiteral("姿态草稿 %1 · %2 · %3\nEnter/左键保留手势 · Esc/右键撤手势；随后确认草稿录键%4")
        .arg(channel == 0 ? QStringLiteral("G 世界移动") : channel == 1 ? QStringLiteral("R 连续度数") : QStringLiteral("S 原点缩放"),
             axisText, numericText, error.isEmpty() ? QString() : QStringLiteral("\n") + error));
}
bool AnimationDraftGesture::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || (widget != &window_ && !window_.isAncestorOf(widget)))
        return false;
    if (!state_ && watched == &viewport_ && event->type() == QEvent::Enter)
        pointer_ = static_cast<QEnterEvent*>(event)->position();
    if (watched == &viewport_ && event->type() == QEvent::MouseMove) {
        pointer_ = static_cast<QMouseEvent*>(event)->position();
        if (state_) {
            updatePreview();
            return true;
        }
    }
    if (state_ && (event->type() == QEvent::WindowDeactivate ||
                   (watched == &viewport_ && (event->type() == QEvent::FocusOut ||
                                             event->type() == QEvent::Hide || event->type() == QEvent::Resize)))) {
        cancel();
        return false;
    }
    if (model_.animationMode() != renderer_gl::AnimationMode::PoseDraft)
        return false;
    if (KeymapRouter::isTextInput(widget) || KeymapRouter::isTextInput(QApplication::focusWidget()))
        return false;
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            if (key->isAutoRepeat())
                return true;
            if (state_)
                cancel();
            else
                model_.cancelAnimationDraft();
            return true;
        }
        if (!state_)
            return false;
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            finish(true);
        } else if (key->key() >= Qt::Key_X && key->key() <= Qt::Key_Z && key->modifiers() == Qt::NoModifier) {
            state_->axis = key->key() - Qt::Key_X;
            updatePreview();
        } else if (key->key() == Qt::Key_Backspace) {
            state_->numeric.backspace();
            updatePreview();
        } else if (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::KeypadModifier) {
            for (const auto character : key->text())
                state_->numeric.append(character.toLatin1());
            updatePreview();
        }
        return true;
    }
    if (state_ && event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    if (state_ && watched == &viewport_ && event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton)
            finish(true);
        else if (mouse->button() == Qt::RightButton)
            cancel();
        return true;
    }
    return false;
}
} // namespace mini3d::editor
