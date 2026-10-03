/*
 * 模块名: LoopCutSession
 * 功能概述: 适配环切预览/滑移两阶段输入，不写拓扑或创建历史。
 * 对外接口: start、cancel、stage、statusText；依赖关系: Qt、ViewModel、ComponentPicker。
 * 输入输出: 真实逻辑坐标/数值到环切事务，HUD显示阶段与滑移百分比。
 * 异常与错误: 无效目标不确认；右键按阶段取消或居中确认，Esc整次取消。
 * 维护说明: 共用组件事务排他边界，保持同一个GL控件和唯一历史。
 */
#pragma once

#include "NumericInputBuffer.h"
#include "core/modeling/EditableMesh.h"
#include "renderer_gl/EditorCamera.h"

#include <QObject>
#include <QPointF>
#include <QPointer>

class QLabel;
class QMainWindow;
namespace mini3d::renderer_gl {
class ViewportWidget;
}
namespace mini3d::editor {
class SceneViewModel;
enum class LoopCutStage { Inactive, Preview, Slide };
/** @brief 原子环切输入会话；开始不要求预选组件，目标来自鼠标下的源边。 */
class LoopCutSession final : public QObject {
    Q_OBJECT
  public:
    LoopCutSession(QMainWindow& window, SceneViewModel& model,
                   renderer_gl::ViewportWidget& viewport, QObject* parent = nullptr);
    bool start();
    void cancel();
    [[nodiscard]] LoopCutStage stage() const;
    [[nodiscard]] QString statusText() const;

  signals:
    void statusTextChanged(const QString& text);

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct State {
        renderer_gl::EditorCamera camera;
        LoopCutStage stage = LoopCutStage::Preview;
        std::optional<core::modeling::EdgeKey> seed;
        QPointF anchor;
        double anchorOffset = 0, offset = 0, slide = 0;
        bool fine = false, control = false, snap = false, xRay = false, valid = false;
        NumericInputBuffer numeric;
        QString error;
    };
    void clear();
    void updateTarget();
    void updateSlide();
    void setFine(bool enabled);
    void confirm();
    void centerAndConfirm();
    void publishStatus(const QString& error = {});
    QMainWindow& window_;
    QPointer<SceneViewModel> model_;
    renderer_gl::ViewportWidget& viewport_;
    QPointer<QLabel> hud_;
    std::optional<State> state_;
    std::optional<QPointF> pointer_;
    QString status_;
};
} // namespace mini3d::editor
