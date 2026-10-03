/*
 * 模块名: ObjectTransformSession
 * 功能概述: 实现对象/组件共用 G/R/S 模态、方向空间、数字输入、精细与吸附反转。
 * 对外接口: ObjectTransformSession
 * 依赖关系: Qt 输入、SceneViewModel 事务、CPU 相机/变换数学
 * 输入输出: 冻结视口中的输入到绝对预览；确认一条历史，取消恢复原状。
 * 异常与错误: 无效数值/剪切候选不提交；失活/失焦/尺寸变化取消。
 * 维护说明: Shift 仅分段重设输入锚点，不积乘几何；模态不改变持久手柄工具。
 */
#include "ObjectTransformSession.h"

#include "KeymapRouter.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QEnterEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>

namespace mini3d::editor {
namespace {
float dot(const QPointF& a, const QPointF& b) {
    return static_cast<float>(a.x() * b.x() + a.y() * b.y());
}
std::optional<glm::vec3> planePoint(const core::Ray& ray, const glm::vec3& origin,
                                    const glm::vec3& normal) {
    const float denominator = glm::dot(ray.direction, normal);
    if (std::abs(denominator) < 1.0e-5F) {
        return std::nullopt;
    }
    const float distance = glm::dot(origin - ray.origin, normal) / denominator;
    if (!std::isfinite(distance) || distance < 0) {
        return std::nullopt;
    }
    return ray.origin + distance * ray.direction;
}
} // namespace

ObjectTransformSession::ObjectTransformSession(QMainWindow& window, SceneViewModel& model,
                                               renderer_gl::ViewportWidget& viewport,
                                               QObject* parent)
    : QObject(parent), window_(window), model_(&model), viewport_(viewport) {
    qApp->installEventFilter(this);
    connect(&model, &SceneViewModel::transformEditFinished, this,
            &ObjectTransformSession::clearSession);
    connect(&model, &SceneViewModel::componentTransformFinished, this,
            &ObjectTransformSession::clearSession);
    connect(&model, &QObject::destroyed, this, &ObjectTransformSession::clearSession);
    connect(&model, &SceneViewModel::operationFailed, this, [this](const QString& reason) {
        if (state_ && state_->target != TransformTarget::Object)
            state_->previewError = reason;
    });
    connect(&viewport_, &renderer_gl::ViewportWidget::viewModeChanged, this,
            &ObjectTransformSession::cancel);
    connect(&viewport_, &renderer_gl::ViewportWidget::cameraChanged, this,
            &ObjectTransformSession::cancel);
    connect(&model, &SceneViewModel::proportionalEditingChanged, this, [this] {
        if (state_ && state_->target == TransformTarget::Components)
            updatePreview();
    });
}

bool ObjectTransformSession::start(TransformOperation operation, TransformTarget target) {
    cancel();
    if (!model_) {
        return false;
    }
    // 先撤销可能仍在进行的手柄预览，快照必须取已提交的场景状态。
    model_->cancelTransformEdit();
    viewport_.resetMoveInteraction();
    const auto id = model_->selection()->selectedEntity();
    const auto* node = model_->scene()->find(id);
    const auto camera = viewport_.editorCameraSnapshot();
    const bool components = target != TransformTarget::Object;
    if ((target == TransformTarget::ExtrudeRegion || target == TransformTarget::InsetFace ||
         target == TransformTarget::BevelEdge) &&
        operation != TransformOperation::Move) {
        emit operationRejected(QStringLiteral("区域挤出/面内插/边倒角使用距离操作。"));
        return false;
    }
    if (model_->isEditMode() != components || !node ||
        !model_->viewportVisibility().isVisible(*model_->scene(), id) || !camera ||
        model_->previewCamera() != 0) {
        emit operationRejected(QStringLiteral("请在编辑视图选择一个可见对象，再启动变换。"));
        return false;
    }
    State state;
    state.operation = operation;
    state.target = target;
    state.before = node->transform;
    state.parentWorld = model_->scene()->worldMatrix(node->parent);
    state.localBasis = glm::mat3_cast(model_->scene()->worldRotation(id));
    state.origin = glm::vec3(model_->scene()->worldMatrix(id)[3]);
    if (components) {
        const auto center = model_->selectedComponentCenter();
        if (!center) {
            emit operationRejected(QStringLiteral("请先选择需要变换的点、边或面。"));
            return false;
        }
        state.origin = glm::vec3(*center);
        state.componentCenter = *center;
    }
    if (operation != TransformOperation::Move) {
        const auto pivot = model_->transformPivotPosition();
        if (!pivot) {
            emit operationRejected(QStringLiteral("当前选择没有可用的变换枢轴。"));
            return false;
        }
        state.componentCenter = *pivot;
        state.origin = glm::vec3(*pivot);
        state.pivotName = model_->transformPivotName();
    }
    state.camera = *camera;
    if (state.camera.worldUnitsPerPixel(state.origin) <= 0) {
        emit operationRejected(QStringLiteral("对象位于视图近裁剪面后方，请先调整视角。"));
        return false;
    }
    state.local = viewport_.transformSpace() == renderer_gl::GizmoSpace::Local;
    state.snapPreference =
        window_.findChild<QAction*>(QStringLiteral("SnapTransform"))->isChecked();
    state.vertexSnap = model_->snapMode() == SnapMode::Vertex &&
                       operation == TransformOperation::Move &&
                       (target == TransformTarget::Object || target == TransformTarget::Components);
    state.sourceEntity = id;
    if (state.vertexSnap && components)
        state.excludedVertices = model_->componentSelection().selectedVertices(
            model_->scene()->editableMesh(node->editableMesh)->content->source);
    if (target == TransformTarget::Components) {
        const auto& source = model_->scene()->editableMesh(node->editableMesh)->content->source;
        const auto drivers = model_->componentSelection().selectedVertices(source);
        const glm::dmat4 world(model_->scene()->worldMatrix(id));
        for (const auto& vertex : source.vertices) {
            state.sourceVertices.insert(vertex.id);
            if (drivers.contains(vertex.id))
                state.proportionalCenters.push_back(
                    glm::vec3(world * glm::dvec4(vertex.position, 1)));
        }
    }
    state.startPointer = pointer_.value_or(viewport_.rect().center());
    state.anchor = state.startPointer;
    pointer_ = state.startPointer;
    if (target == TransformTarget::ExtrudeRegion) {
        if (!model_->beginExtrudeRegion())
            return false;
        const auto info = *model_->componentExtrusion();
        state.extrusionNormal = glm::vec3(info.normal);
        state.normalFallback = info.usesFallbackNormal;
    } else if (target == TransformTarget::InsetFace) {
        if (!model_->beginInsetFace())
            return false;
        state.insetMaximum = model_->componentInset()->maximumThickness;
    } else if (target == TransformTarget::BevelEdge) {
        if (!model_->beginBevelEdge())
            return false;
        state.bevelMaximum = model_->componentBevel()->maximumWidth;
    } else if (components) {
        const auto label = operation == TransformOperation::Move     ? QStringLiteral("移动组件")
                           : operation == TransformOperation::Rotate ? QStringLiteral("旋转组件")
                                                                     : QStringLiteral("缩放组件");
        if (!model_->beginComponentTransform(label)) {
            return false;
        }
    } else {
        model_->beginTransformEdit(id);
    }
    viewport_.setFocus(); // 明确启动操作才获取焦点，不在 Enter/MouseMove 时抢文本焦点。
    state_ = std::move(state);
    viewport_.grabMouse();
    emit activeChanged(true);
    if (target == TransformTarget::ExtrudeRegion || target == TransformTarget::InsetFace ||
        target == TransformTarget::BevelEdge) {
        state_->previewValid = false;
        publishStatus(QStringLiteral("0"),
                      target != TransformTarget::ExtrudeRegion
                          ? QStringLiteral("向右移动鼠标或输入正距离；Esc / 右键安全取消。")
                          : QStringLiteral("输入非零挤出距离；Esc / 右键安全取消。"));
    } else {
        updatePreview();
    }
    return true;
}

bool ObjectTransformSession::isActive() const {
    return state_.has_value();
}
QString ObjectTransformSession::statusText() const {
    return status_;
}

void ObjectTransformSession::clearSession() {
    if (!state_) {
        return;
    }
    state_.reset();
    viewport_.setSnapTarget(std::nullopt);
    viewport_.setProportionalInfluence({}, 0);
    if (QWidget::mouseGrabber() == &viewport_) {
        viewport_.releaseMouse();
    }
    status_ = QStringLiteral("就绪");
    emit activeChanged(false);
    emit statusTextChanged(status_);
}

void ObjectTransformSession::finish(bool commit) {
    if (!state_) {
        return;
    }
    if (commit && !state_->previewValid) {
        emit operationRejected(QStringLiteral("当前候选无效，请修正输入或按 Esc 取消。"));
        return;
    }
    const auto target = state_->target;
    if (model_ && target != TransformTarget::Object) {
        // ViewModel 校验成功才退出；无效候选保留会话，允许继续修正输入。
        model_->finishComponentTransform(commit);
        return;
    }
    clearSession();
    if (model_) {
        model_->finishTransformEdit(commit);
    }
}

void ObjectTransformSession::cancel() {
    finish(false);
}

void ObjectTransformSession::updatePointer(const QPointF& position) {
    pointer_ = position;
    state_->offset =
        state_->anchorOffset + (position - state_->anchor) * (state_->fine ? 0.1 : 1.0);
    updatePreview();
}

void ObjectTransformSession::setFine(bool enabled) {
    if (state_->fine != enabled) {
        state_->anchorOffset = state_->offset;
        state_->anchor = *pointer_;
        state_->fine = enabled;
        updatePreview();
    }
}

void ObjectTransformSession::constrain(int axis, bool plane) {
    if (state_->target == TransformTarget::BevelEdge) {
        emit operationRejected(QStringLiteral("单段边倒角只接受局部宽度，不使用轴约束。"));
        return;
    }
    if (state_->target == TransformTarget::InsetFace) {
        emit operationRejected(
            QStringLiteral("内插在面内等距偏移，仅接受局部厚度，不使用轴约束。"));
        return;
    }
    if (plane && state_->operation == TransformOperation::Rotate) {
        emit operationRejected(QStringLiteral("旋转使用单轴约束；排轴平面适用于移动与缩放。"));
        return;
    }
    if (state_->axis == axis && state_->plane == plane) {
        state_->local = !state_->local;
    }
    state_->axis = axis;
    state_->plane = plane;
    updatePreview();
}

void ObjectTransformSession::publishStatus(const QString& value, const QString& error) {
    const auto& state = *state_;
    if (state.target == TransformTarget::BevelEdge) {
        status_ = QStringLiteral("单段边倒角 | 宽度 %1（对象局部单位） | 上限 < %2\n"
                                 "向右增宽 · Enter/左键确认 · Esc/右键取消 · Shift精细%3")
                      .arg(value, QString::number(state.bevelMaximum, 'g', 6),
                           error.isEmpty() ? QString() : QStringLiteral("\n%1").arg(error));
        emit statusTextChanged(status_);
        return;
    }
    if (state.target == TransformTarget::InsetFace) {
        status_ = QStringLiteral(
                      "面内插 | 厚度 %1（对象局部单位） | 上限 < %2 | 步进%3%4\n"
                      "向右增厚 · Enter/左键确认 · Esc/右键安全取消 · Shift精细 · Ctrl反转步进%5")
                      .arg(value, QString::number(state.insetMaximum, 'g', 6),
                           state.snapPreference != state.control ? QStringLiteral("开")
                                                                 : QStringLiteral("关"),
                           state.fine ? QStringLiteral(" · 精细") : QString(),
                           error.isEmpty() ? QString() : QStringLiteral("\n%1").arg(error));
        emit statusTextChanged(status_);
        return;
    }
    auto operation = state.operation == TransformOperation::Move     ? QStringLiteral("移动")
                     : state.operation == TransformOperation::Rotate ? QStringLiteral("旋转")
                                                                     : QStringLiteral("缩放");
    if (state.target == TransformTarget::Components) {
        operation += QStringLiteral("组件");
    } else if (state.target == TransformTarget::ExtrudeRegion) {
        operation = QStringLiteral("区域挤出 · 安全取消");
    }
    if (!state.pivotName.isEmpty())
        operation += QStringLiteral(" · 枢轴：%1").arg(state.pivotName);
    if (state.target == TransformTarget::Components && model_->isProportionalEditingEnabled())
        operation += QStringLiteral(" · 比例 Smooth%1 · 半径 %2（滚轮）")
                         .arg(model_->isProportionalConnected() ? QStringLiteral("/Connected")
                                                                : QStringLiteral("/全部"),
                              QString::number(model_->proportionalRadius(), 'g', 6));
    if (state.vertexSnap) {
        operation += state.numeric.text().empty()
                         ? (state.snapHit ? QStringLiteral(" · 顶点目标 %1/%2")
                                                .arg(state.snapHit->entity)
                                                .arg(state.snapHit->vertex)
                                          : QStringLiteral(" · 顶点：无命中"))
                         : QStringLiteral(" · 数值优先");
    }
    const auto axis =
        state.axis < 0
            ? (state.target == TransformTarget::ExtrudeRegion
                   ? (state.normalFallback ? QStringLiteral("替代面法线")
                                           : QStringLiteral("区域法线"))
                   : QStringLiteral("自由"))
            : QStringLiteral("%1%2").arg(state.plane ? QStringLiteral("排除 ") : QString(),
                                         QString(QChar('X' + state.axis)));
    status_ = QStringLiteral("%1 | %2 %3 | %4 | 吸附%5%6\nEnter/左键确认 · Esc/右键取消 · "
                             "Shift精细 · Ctrl反转吸附%7")
                  .arg(operation, axis,
                       state.local ? QStringLiteral("局部") : QStringLiteral("全局"), value,
                       state.snapPreference != state.control ? QStringLiteral("开")
                                                             : QStringLiteral("关"),
                       state.fine ? QStringLiteral(" · 精细") : QString(),
                       error.isEmpty() ? QString() : QStringLiteral("\n%1").arg(error));
    emit statusTextChanged(status_);
}

void ObjectTransformSession::updatePreview() {
    if (!state_ || !model_) {
        return;
    }
    auto& state = *state_;
    viewport_.setProportionalInfluence(
        state.target == TransformTarget::Components && model_->isProportionalEditingEnabled()
            ? state.proportionalCenters
            : std::vector<glm::vec3>{},
        model_->proportionalRadius());
    state.snapHit.reset();
    viewport_.setSnapTarget(std::nullopt);
    const auto numeric =
        state.numeric.parse(-std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    const auto rawText = QString::fromStdString(state.numeric.text());
    const auto reject = [this, &state, &rawText](const QString& error) {
        state.previewValid = false;
        publishStatus(rawText, error);
    };
    if (numeric.state != NumericInputState::Empty && numeric.state != NumericInputState::Valid) {
        reject(numeric.state == NumericInputState::Incomplete ? QStringLiteral("数值尚未输入完整。")
               : numeric.state == NumericInputState::OutOfRange
                   ? QStringLiteral("数值超出可用范围。")
                   : QStringLiteral("仅支持符号和十进制小数；不支持算式或单位。"));
        return;
    }
    const bool precise = numeric.value.has_value();
    const bool snap = !precise && state.snapPreference != state.control;
    if (snap && state.vertexSnap) {
        state.snapHit =
            pickVertexSnap(*model_->scene(), *model_->assets(), state.camera,
                           {static_cast<float>(pointer_->x()), static_cast<float>(pointer_->y())},
                           {viewport_.width(), viewport_.height()}, state.sourceEntity,
                           state.target == TransformTarget::Object,
                           state.target == TransformTarget::Components &&
                                   model_->isProportionalEditingEnabled()
                               ? state.sourceVertices
                               : state.excludedVertices,
                           model_->viewportVisibility());
    }
    if (state.target == TransformTarget::InsetFace || state.target == TransformTarget::BevelEdge) {
        // 横向200逻辑像素对应当前面首条边的坍缩上限；精细只改变输入锚点，不累积几何。
        const double maximum =
            state.target == TransformTarget::BevelEdge ? state.bevelMaximum : state.insetMaximum;
        double thickness = precise ? *numeric.value : state.offset.x() * maximum / 200;
        if (snap)
            thickness = std::round(thickness / .1) * .1;
        state.previewError.clear();
        const bool valid = state.target == TransformTarget::BevelEdge
                               ? model_->previewBevelEdge(thickness)
                               : model_->previewInsetFace(thickness);
        if (state_) {
            state_->previewValid = valid;
            publishStatus(precise ? rawText : QString::number(thickness, 'f', 6),
                          valid ? QString() : state_->previewError);
        }
        return;
    }
    const auto basis = state.local ? state.localBasis : glm::mat3(1);
    const auto viewBasis = glm::mat3(glm::inverse(state.camera.viewMatrix()));
    const float units = state.camera.worldUnitsPerPixel(state.origin);
    const auto rawDelta = units * (viewBasis[0] * static_cast<float>(state.offset.x()) -
                                   viewBasis[1] * static_cast<float>(state.offset.y()));
    std::optional<core::Transform> candidate;
    glm::dmat4 worldDelta(1);
    const bool components = state.target != TransformTarget::Object;
    const bool normalExtrusion = state.target == TransformTarget::ExtrudeRegion && state.axis < 0;
    QString value;
    if (state.operation == TransformOperation::Move) {
        auto delta = rawDelta;
        if (normalExtrusion) {
            const float projectedLength =
                1.0F - std::pow(glm::dot(state.extrusionNormal, viewBasis[2]), 2.0F);
            const float distance = precise ? static_cast<float>(*numeric.value)
                                   : projectedLength > 1.0e-5F
                                       ? glm::dot(rawDelta, state.extrusionNormal) / projectedLength
                                       : -static_cast<float>(state.offset.y()) * units;
            delta = state.extrusionNormal * distance;
        } else if (state.axis >= 0 && !state.plane) {
            const auto axis = basis[state.axis];
            const float projectedLength = 1.0F - std::pow(glm::dot(axis, viewBasis[2]), 2.0F);
            if (!precise && !state.snapHit && projectedLength < 1.0e-5F) {
                reject(QStringLiteral("此轴正对视线，请输入数值或调整视角。"));
                return;
            }
            delta = axis *
                    (precise ? static_cast<float>(*numeric.value)
                             : (state.snapHit ? 0.0F : glm::dot(rawDelta, axis) / projectedLength));
        } else if (state.plane && !precise) {
            const auto end = state.startPointer + state.offset;
            const auto startPoint =
                planePoint(state.camera.screenRay(static_cast<float>(state.startPointer.x()),
                                                  static_cast<float>(state.startPointer.y())),
                           state.origin, basis[state.axis]);
            const auto endPoint = planePoint(
                state.camera.screenRay(static_cast<float>(end.x()), static_cast<float>(end.y())),
                state.origin, basis[state.axis]);
            if ((!startPoint || !endPoint) && !state.snapHit) {
                reject(QStringLiteral("视线与约束平面近平行，请改用轴数值或调整视角。"));
                return;
            }
            delta = state.snapHit ? glm::vec3(0) : *endPoint - *startPoint;
        }
        if (precise && !normalExtrusion && (state.axis < 0 || state.plane)) {
            if (state.plane) {
                delta -= basis[state.axis] * glm::dot(delta, basis[state.axis]);
            }
            // 自由数值移动沿当前鼠标方向；尚无方向时取当前空间首个可用轴。
            const auto direction = glm::length(delta) > 1.0e-6F
                                       ? glm::normalize(delta)
                                       : basis[state.plane && state.axis == 0 ? 1 : 0];
            delta = direction * static_cast<float>(*numeric.value);
        }
        if (snap && normalExtrusion) {
            delta = state.extrusionNormal *
                    (std::round(glm::dot(delta, state.extrusionNormal) / 0.5F) * 0.5F);
        } else if (snap && !state.vertexSnap) {
            auto snappedDelta = glm::transpose(basis) * delta;
            for (int axis = 0; axis < 3; ++axis) {
                snappedDelta[axis] = std::round(snappedDelta[axis] / 0.5F) * 0.5F;
            }
            delta = basis * snappedDelta;
        }
        if (state.snapHit) {
            delta = state.snapHit->position - state.origin;
            if (state.axis >= 0) {
                const auto constrained = basis[state.axis] * glm::dot(delta, basis[state.axis]);
                delta = state.plane ? delta - constrained : constrained;
            }
            viewport_.setSnapTarget(state.snapHit->position);
        }
        if (components) {
            worldDelta = glm::translate(glm::dmat4(1), glm::dvec3(delta));
        } else {
            candidate = ObjectTransformMath::translate(state.before, state.parentWorld, delta);
        }
        value = precise ? rawText
                        : QStringLiteral("Δ %1, %2, %3")
                              .arg(delta.x, 0, 'f', 3)
                              .arg(delta.y, 0, 'f', 3)
                              .arg(delta.z, 0, 'f', 3);
    } else {
        const auto clip = state.camera.viewProjectionMatrix() * glm::vec4(state.origin, 1);
        const QPointF pivot((clip.x / clip.w + 1) * viewport_.width() * 0.5,
                            (1 - clip.y / clip.w) * viewport_.height() * 0.5);
        auto reference = state.startPointer - pivot;
        if (dot(reference, reference) < 100) {
            reference = QPointF(80, 0);
        }
        const auto current = reference + state.offset;
        if (state.operation == TransformOperation::Rotate) {
            const auto worldAxis = state.axis < 0 ? viewBasis[2] : basis[state.axis];
            float angle = precise ? glm::radians(static_cast<float>(*numeric.value))
                                  : std::atan2(static_cast<float>(reference.y() * current.x() -
                                                                  reference.x() * current.y()),
                                               dot(reference, current));
            if (!precise && glm::dot(worldAxis, viewBasis[2]) < 0) {
                angle = -angle;
            }
            if (snap) {
                angle = std::round(angle / glm::radians(15.0F)) * glm::radians(15.0F);
            }
            worldDelta =
                glm::rotate(glm::dmat4(1), static_cast<double>(angle), glm::dvec3(worldAxis));
            if (!components) {
                candidate = ObjectTransformMath::rotate(state.before, state.parentWorld, worldAxis,
                                                        angle, state.local ? state.axis : -1);
            }
            value = QStringLiteral("%1°").arg(
                precise ? rawText : QString::number(glm::degrees(angle), 'f', 3));
        } else {
            float factor = precise ? static_cast<float>(*numeric.value)
                                   : 1 + dot(state.offset, reference) / dot(reference, reference);
            if (snap) {
                factor = 1 + std::round((factor - 1) / 0.1F) * 0.1F;
            }
            glm::vec3 factors(state.axis < 0 || state.plane ? factor : 1);
            if (state.axis >= 0) {
                factors[state.axis] = state.plane ? 1 : factor;
            }
            const glm::dmat4 axes{glm::dmat3(basis)};
            if (factors != glm::vec3(1)) {
                worldDelta =
                    axes * glm::scale(glm::dmat4(1), glm::dvec3(factors)) * glm::transpose(axes);
            }
            if (!components) {
                candidate = ObjectTransformMath::scale(state.before, state.parentWorld, basis,
                                                       factors, state.local);
            }
            value =
                QStringLiteral("倍率 %1").arg(precise ? rawText : QString::number(factor, 'f', 3));
        }
        if (components) {
            worldDelta = glm::translate(glm::dmat4(1), state.componentCenter) * worldDelta *
                         glm::translate(glm::dmat4(1), -state.componentCenter);
        } else if (candidate && worldDelta != glm::dmat4(1)) {
            // 对象姿态仍沿用既有 TRS 规则；原点另按同一世界操作绕冻结枢轴移动。
            const auto origin = glm::dvec3(state.parentWorld * glm::vec4(state.before.position, 1));
            const auto offset = state.componentCenter +
                                glm::dmat3(worldDelta) * (origin - state.componentCenter) - origin;
            candidate =
                ObjectTransformMath::translate(*candidate, state.parentWorld, glm::vec3(offset));
        }
    }
    if (components) {
        state.previewError.clear();
        const bool valid = model_->previewComponentTransform(worldDelta);
        if (state_) {
            state_->previewValid = valid;
            publishStatus(value, valid ? QString() : state_->previewError);
        }
        return;
    }
    if (!candidate) {
        reject(state.operation == TransformOperation::Scale
                   ? QStringLiteral("缩放倍率为零、缩放过小或结果超出范围，请修正数值。")
                   : QStringLiteral("结果超出范围或包含TRS无法表示的剪切；可切换局部空间。"));
        return;
    }
    state.previewValid = true;
    model_->previewTransform(*candidate);
    publishStatus(value);
}

bool ObjectTransformSession::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (watched == &viewport_ && !state_) {
        if (event->type() == QEvent::MouseMove) {
            pointer_ = static_cast<QMouseEvent*>(event)->position();
        } else if (event->type() == QEvent::Enter) {
            pointer_ = static_cast<QEnterEvent*>(event)->position();
        }
    }
    if (!state_ || !widget || (widget != &window_ && !window_.isAncestorOf(widget))) {
        return false;
    }
    if (event->type() == QEvent::WindowDeactivate ||
        (watched == &viewport_ &&
         (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
          event->type() == QEvent::Resize || event->type() == QEvent::UngrabMouse))) {
        cancel();
        return false;
    }
    // 弹窗确认后，原输入框仍可能收到按键释放/重绘；这不是新的输入意图。
    if (!widget->isVisible()) {
        return false;
    }
    if (event->type() == QEvent::InputMethod &&
        !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty()) {
        cancel();
        return false;
    }
    const bool textIntent = event->type() == QEvent::KeyPress ||
                            event->type() == QEvent::ShortcutOverride ||
                            event->type() == QEvent::InputMethod;
    if (textIntent && (KeymapRouter::isTextInput(QApplication::focusWidget()) ||
                       KeymapRouter::isTextInput(widget))) {
        cancel();
        return false;
    }
    if (watched == &viewport_ && event->type() == QEvent::MouseMove) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        setFine(mouse->modifiers().testFlag(Qt::ShiftModifier));
        state_->control = mouse->modifiers().testFlag(Qt::ControlModifier);
        updatePointer(mouse->position());
        return true;
    }
    if (watched == &viewport_ && event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            finish(viewport_.rect().contains(mouse->position().toPoint()));
        } else if (mouse->button() == Qt::RightButton) {
            cancel();
        }
        return true;
    }
    if (watched == &viewport_ && event->type() == QEvent::Wheel) {
        if (state_->target == TransformTarget::Components && model_->isProportionalEditingEnabled()) {
            const auto* wheel = static_cast<QWheelEvent*>(event);
            const double steps = wheel->angleDelta().y() / 120.0;
            const auto radius = model_->proportionalRadius() * std::pow(1.2, -steps);
            model_->setProportionalRadius(std::clamp(radius, 0.0001, 1000000.0));
        }
        return true;
    }
    if (watched == &viewport_ && event->type() == QEvent::MouseButtonRelease) {
        return true;
    }
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease &&
        event->type() != QEvent::ShortcutOverride) {
        return false;
    }
    const auto* key = static_cast<QKeyEvent*>(event);
    const auto modifiers = key->modifiers();
    if (key->key() == Qt::Key_Tab && modifiers == Qt::NoModifier && !key->isAutoRepeat()) {
        // 保留路由器的区域/键位判定；先取消旧会话，再由同一次 Tab 触发模式切换。
        cancel();
        return false;
    }
    const bool fileCommand =
        modifiers.testFlag(Qt::ControlModifier) &&
        (key->key() == Qt::Key_S || key->key() == Qt::Key_O || key->key() == Qt::Key_N);
    if (fileCommand || modifiers.testFlag(Qt::AltModifier) ||
        modifiers.testFlag(Qt::MetaModifier) || key->key() == Qt::Key_F1) {
        cancel();
        return false;
    }
    event->accept();
    if (event->type() == QEvent::ShortcutOverride) {
        return true;
    }
    const bool pressed = event->type() == QEvent::KeyPress;
    if (key->key() == Qt::Key_Shift) {
        setFine(pressed);
    } else if (key->key() == Qt::Key_Control) {
        state_->control = pressed;
        updatePreview();
    } else if (pressed) {
        if (key->key() == Qt::Key_Escape) {
            cancel();
        } else if (key->key() == Qt::Key_O && modifiers == Qt::NoModifier &&
                   state_->target == TransformTarget::Components) {
            if (!key->isAutoRepeat())
                model_->setProportionalEditingEnabled(!model_->isProportionalEditingEnabled());
        } else if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            finish(true);
        } else if (key->key() >= Qt::Key_X && key->key() <= Qt::Key_Z &&
                   !modifiers.testFlag(Qt::ControlModifier)) {
            if (!key->isAutoRepeat()) {
                constrain(key->key() - Qt::Key_X, modifiers.testFlag(Qt::ShiftModifier));
            }
        } else if (key->key() == Qt::Key_Backspace) {
            state_->numeric.backspace();
            updatePreview();
        } else if (!modifiers.testFlag(Qt::ControlModifier) && !key->text().isEmpty()) {
            for (const auto character : key->text()) {
                state_->numeric.append(character.toLatin1());
            }
            updatePreview();
        }
    }
    return true;
}
} // namespace mini3d::editor
