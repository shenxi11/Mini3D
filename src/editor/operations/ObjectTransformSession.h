/*
 * 模块名: ObjectTransformSession
 * 功能概述: 共用对象/组件GRS与挤出/内插的模态输入，统一ViewModel确认取消事务。
 * 对外接口: ObjectTransformSession
 * 依赖关系: Qt、SceneViewModel、ViewportWidget、数值缓冲/变换数学
 * 输入输出: 鼠标/键盘意图到原 ViewModel 预览事务与中文 HUD。
 * 异常与错误: 无效候选不提交；失焦、失活、文档/选择变化恢复 before。
 * 维护说明: 不创建历史栈，不改工具条的持久工具选择，不调用 OpenGL。
 */
#pragma once

#include "ComponentPicker.h"
#include "NumericInputBuffer.h"
#include "ObjectTransformMath.h"
#include "renderer_gl/EditorCamera.h"

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <vector>

class QMainWindow;

namespace mini3d::renderer_gl {
class ViewportWidget;
}
namespace mini3d::editor {
class SceneViewModel;
enum class TransformTarget { Object, Components, ExtrudeRegion, InsetFace, BevelEdge };

/** @brief 操作意图适配器；预览和提交仅走 SceneViewModel 的已有事务。 */
class ObjectTransformSession final : public QObject {
    Q_OBJECT
  public:
    ObjectTransformSession(QMainWindow& window, SceneViewModel& model,
                           renderer_gl::ViewportWidget& viewport, QObject* parent = nullptr);
    /** @brief 冻结 before/空间/相机/选择及枢轴；目标须匹配当前模式。 */
    bool start(TransformOperation operation, TransformTarget target = TransformTarget::Object);
    void cancel();
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] QString statusText() const;

  signals:
    void activeChanged(bool active);
    void statusTextChanged(const QString& text);
    void operationRejected(const QString& reason);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct State {
        TransformOperation operation;
        TransformTarget target = TransformTarget::Object;
        core::Transform before;
        glm::mat4 parentWorld{1};
        glm::mat3 localBasis{1};
        renderer_gl::EditorCamera camera;
        glm::vec3 origin{0};
        glm::dvec3 componentCenter{0};
        QString pivotName;
        glm::vec3 extrusionNormal{0};
        double insetMaximum = 0;
        double bevelMaximum = 0;
        bool normalFallback = false;
        QString previewError;
        QPointF startPointer, anchor, anchorOffset, offset;
        NumericInputBuffer numeric;
        int axis = -1;
        bool plane = false, local = false, fine = false, control = false;
        bool snapPreference = false, previewValid = true;
        bool vertexSnap = false;
        core::EntityId sourceEntity = 0;
        std::set<core::modeling::VertexId> excludedVertices;
        std::set<core::modeling::VertexId> sourceVertices;
        std::vector<glm::vec3> proportionalCenters;
        std::optional<VertexSnapHit> snapHit;
    };
    void updatePreview();
    void updatePointer(const QPointF& position);
    void setFine(bool enabled);
    void constrain(int axis, bool plane);
    void finish(bool commit);
    void clearSession();
    void publishStatus(const QString& value, const QString& error = {});
    QMainWindow& window_;
    QPointer<SceneViewModel> model_;
    renderer_gl::ViewportWidget& viewport_;
    std::optional<QPointF> pointer_;
    std::optional<State> state_;
    QString status_;
};
} // namespace mini3d::editor
