/*
 * 模块名: OperatorSearchPopup
 * 功能概述: 呈现 F3 搜索/Q 收藏和禁用原因，保留打开前的操作上下文。
 * 对外接口: OperatorSearchPopup
 * 依赖关系: Qt Widgets、OperatorRegistry、QuickFavorites
 * 输入输出: 查询文字、方向键与确认到稳定操作 ID。
 * 异常与错误: 无结果或上下文失效时保持弹窗并解释，Esc 不执行。
 * 维护说明: 只通过 Registry 发意图，不读取或修改 Scene。
 */
#pragma once

#include "editor/operations/OperatorRegistry.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;

namespace mini3d::editor {
class QuickFavorites;
enum class OperatorPopupMode { Search, Favorites };
/** @brief 非阻塞搜索弹窗；文本焦点不会替代冻结的调用区域。 */
class OperatorSearchPopup final : public QDialog {
    Q_OBJECT
  public:
    explicit OperatorSearchPopup(OperatorRegistry& registry, QuickFavorites& favorites,
                                 QWidget* parent);
    /** @brief 冻结上下文并显示；关闭恢复原控件焦点，不执行未选择的操作。 */
    void openForContext(const OperatorContext& context,
                        OperatorPopupMode mode = OperatorPopupMode::Search);
    void accept() override;
    void reject() override;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void refreshResults();
    void refreshDescription();
    void restoreSourceFocus();
    void showFavoriteContextMenu(const QPoint& position);
    OperatorRegistry& registry_;
    QuickFavorites& favorites_;
    OperatorPopupMode mode_ = OperatorPopupMode::Search;
    OperatorContext context_;
    QPointer<QWidget> sourceFocus_;
    QLineEdit* query_;
    QLabel* title_;
    QListWidget* results_;
    QLabel* description_;
    bool preediting_ = false;
};
} // namespace mini3d::editor
