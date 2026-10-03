/*
 * 模块名: OperatorPiePopup
 * 功能概述: 以径向扇区呈现已实现操作，保留调用上下文并显示禁用原因。
 * 对外接口: OperatorPiePopup
 * 依赖关系: Qt Widgets、OperatorRegistry
 * 输入输出: 标题、稳定操作 ID 与冻结上下文到鼠标或数字键选择的单次执行。
 * 异常与错误: 失效或禁用项仅解释原因；Esc、中心和外部点击不执行。
 * 维护说明: 使用逻辑坐标，关闭后由 Qt 删除；不直触 QAction，不修改 Scene。
 */
#pragma once

#include "editor/operations/OperatorRegistry.h"

#include <QDialog>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QStringList>

class QLabel;
class QPainterPath;

namespace mini3d::editor {
/** @brief 临时堆分配弹窗；执行始终经过 Registry 对冻结文档和目标的重新核验。 */
class OperatorPiePopup final : public QDialog {
    Q_OBJECT
  public:
    /** @brief 保存标题、操作排列和调用身份；parent 管理所有权，关闭后自动删除。 */
    OperatorPiePopup(OperatorRegistry& registry, OperatorContext context, const QString& title,
                     const QStringList& operatorIds, QWidget* parent = nullptr);
    /** @brief 将饼菜单中心放到全局逻辑坐标，并钳制到所在屏幕可用范围。 */
    void openAt(const QPoint& globalPosition);
    void reject() override;

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    [[nodiscard]] QRectF pieBounds() const;
    [[nodiscard]] QPainterPath sectorPath(int index) const;
    [[nodiscard]] int indexAt(const QPointF& position) const;
    void updateDescription();
    void executeIndex(int index);

    OperatorRegistry& registry_;
    OperatorContext context_;
    QString title_;
    QStringList operatorIds_;
    QPointer<QWidget> sourceFocus_;
    QLabel* description_;
    int hoveredIndex_ = -1;
    int pressedIndex_ = -1;
};
} // namespace mini3d::editor
