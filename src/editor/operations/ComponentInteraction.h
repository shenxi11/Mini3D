/*
 * 模块名: ComponentInteraction
 * 功能概述: 处理点选与可取消框选，将只读源选区适配成覆盖层。
 * 对外接口: ComponentInteraction
 * 依赖关系: SceneViewModel、ViewportWidget、ComponentPicker，无拓扑写入。
 * 输入输出: 点击/框选与状态通知到选择及真实显示数据。
 * 异常与错误: 空白普通点击取消选择，Shift 空白保持；维护说明: 不创建历史栈。
 */
#pragma once
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <memory>
#include <optional>

class QMainWindow;
class QRubberBand;
class QLabel;

namespace mini3d::renderer_gl {
class ViewportWidget;
}
namespace mini3d::editor {
class SceneViewModel;
/** @brief UI 生命周期内的输入/只读显示适配器，不直接调用 GL。 */
class ComponentInteraction final : public QObject {
    Q_OBJECT
  public:
    ComponentInteraction(QMainWindow& window, SceneViewModel& model,
                         renderer_gl::ViewportWidget& viewport, QObject* parent = nullptr);
    ~ComponentInteraction() override;
    /** @brief 冻结网格、选区和观察相机；拖动释放才一次性写选区，取消无副作用。 */
    bool startBoxSelection();
    void cancelBoxSelection();
    [[nodiscard]] bool isBoxSelecting() const;
    /** @brief L/菜单：以真实鼠标命中或活动组件扩展源连通片。 */
    void selectLinkedUnderPointer();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    struct BoxState;
    struct OverlayTopology;
    void finishBoxSelection(const QPointF& point);
    void refreshOverlay();
    QMainWindow& window_;
    SceneViewModel& model_;
    renderer_gl::ViewportWidget& viewport_;
    std::unique_ptr<BoxState> box_;
    std::unique_ptr<OverlayTopology> overlayTopology_;
    QPointer<QRubberBand> band_;
    QPointer<QLabel> hud_;
    std::optional<QPointF> pointer_;
};
} // namespace mini3d::editor
