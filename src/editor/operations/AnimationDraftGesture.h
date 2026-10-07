/*
 * 模块名: AnimationDraftGesture
 * 功能概述: 将受限GRS输入转换为独立姿态草稿，绝不调用基础变换预览。
 * 对外接口: AnimationDraftGesture::start/cancel/isActive。
 * 依赖关系: SceneViewModel、ViewportWidget、NumericInputBuffer、Qt。
 * 输入输出: 冻结父姿态/草稿值与键鼠到所选通道候选。
 * 异常与错误: 无许可/数值错误不提交；Esc先撤手势，再撤草稿。
 * 维护说明: 旋转只支持XYZ数值连续角；R/S以对象原点操作，不使用外部枢轴。
 */
#pragma once
#include "NumericInputBuffer.h"
#include "ObjectTransformMath.h"
#include "renderer_gl/EditorCamera.h"

#include <QObject>
#include <QPointF>
#include <array>
#include <cstdint>
#include <optional>

class QMainWindow;
namespace mini3d::renderer_gl { class ViewportWidget; }
namespace mini3d::editor {
class SceneViewModel;
/** @brief 手势确认只保留草稿；最终录键由时间轴确认完整草稿。 */
class AnimationDraftGesture final : public QObject {
    Q_OBJECT
  public:
    AnimationDraftGesture(QMainWindow& window, SceneViewModel& model,
                          renderer_gl::ViewportWidget& viewport, QObject* parent = nullptr);
    bool start(TransformOperation operation);
    void cancel();
    [[nodiscard]] bool isActive() const;
  signals:
    void activeChanged(bool active);
    void statusTextChanged(const QString& text);
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
  private:
    void updatePreview();
    void finish(bool commit);
    struct State {
        TransformOperation operation;
        std::array<glm::dvec3, 3> before;
        renderer_gl::EditorCamera camera;
        glm::dmat4 parentInverse{1};
        glm::vec3 origin{0};
        QPointF pointerStart;
        NumericInputBuffer numeric;
        int axis = -1;
        bool valid = false;
    };
    QMainWindow& window_;
    SceneViewModel& model_;
    renderer_gl::ViewportWidget& viewport_;
    QPointF pointer_;
    std::optional<State> state_;
    std::uint64_t gestureGeneration_ = 0;
};
} // namespace mini3d::editor
