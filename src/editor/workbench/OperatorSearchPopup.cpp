/*
 * 模块名: OperatorSearchPopup
 * 功能概述: 渲染搜索/收藏、当前区域与禁用原因，右键管理收藏并转交单次操作意图。
 * 对外接口: OperatorSearchPopup
 * 依赖关系: Qt Widgets、OperatorRegistry
 * 输入输出: 用户中英文查询到动态结果，Enter/双击执行，Esc 关闭。
 * 异常与错误: 执行前重新 poll；文档切换或目标变化时不误操作新文档。
 * 维护说明: 结果存稳定 ID，不保存 QAction 文案或容器下标作为身份。
 */
#include "OperatorSearchPopup.h"

#include "editor/operations/QuickFavorites.h"

#include <QAction>
#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QVBoxLayout>

namespace mini3d::editor {
OperatorSearchPopup::OperatorSearchPopup(OperatorRegistry& registry, QuickFavorites& favorites,
                                         QWidget* parent)
    : QDialog(parent, Qt::Popup), registry_(registry), favorites_(favorites) {
    setObjectName(QStringLiteral("OperatorSearchPopup"));
    setWindowTitle(QStringLiteral("搜索操作"));
    resize(520, 410);
    auto* layout = new QVBoxLayout(this);
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("OperatorPopupTitle"));
    query_ = new QLineEdit(this);
    query_->setObjectName(QStringLiteral("OperatorSearchQuery"));
    query_->setPlaceholderText(QStringLiteral("搜索操作：中文 / English / 快捷键"));
    query_->setAccessibleName(QStringLiteral("搜索操作"));
    query_->installEventFilter(this);
    results_ = new QListWidget(this);
    results_->setObjectName(QStringLiteral("OperatorSearchResults"));
    results_->installEventFilter(this);
    results_->setContextMenuPolicy(Qt::CustomContextMenu);
    description_ = new QLabel(this);
    description_->setObjectName(QStringLiteral("OperatorSearchDescription"));
    description_->setWordWrap(true);
    description_->setMinimumHeight(48);
    layout->addWidget(title_);
    layout->addWidget(query_);
    layout->addWidget(results_);
    layout->addWidget(description_);
    connect(query_, &QLineEdit::textChanged, this, &OperatorSearchPopup::refreshResults);
    connect(query_, &QLineEdit::returnPressed, this, &OperatorSearchPopup::accept);
    connect(results_, &QListWidget::currentRowChanged, this,
            &OperatorSearchPopup::refreshDescription);
    connect(results_, &QListWidget::itemDoubleClicked, this, &OperatorSearchPopup::accept);
    connect(results_, &QListWidget::customContextMenuRequested, this,
            &OperatorSearchPopup::showFavoriteContextMenu);
    connect(&favorites_, &QuickFavorites::favoritesChanged, this,
            &OperatorSearchPopup::refreshResults);
}

void OperatorSearchPopup::openForContext(const OperatorContext& context, OperatorPopupMode mode) {
    context_ = context;
    mode_ = mode;
    preediting_ = false;
    sourceFocus_ = QApplication::focusWidget();
    query_->clear();
    query_->setVisible(mode == OperatorPopupMode::Search);
    title_->setText(mode == OperatorPopupMode::Search ? QStringLiteral("操作搜索 · F3")
                                                      : QStringLiteral("快捷收藏 · Q"));
    setWindowTitle(title_->text());
    refreshResults();
    move(parentWidget()->mapToGlobal(parentWidget()->rect().center()) - rect().center());
    show();
    if (mode == OperatorPopupMode::Search) {
        query_->setFocus();
    } else {
        results_->setFocus();
    }
}

void OperatorSearchPopup::refreshResults() {
    results_->clear();
    QVector<OperatorMatch> matches;
    if (mode_ == OperatorPopupMode::Favorites) {
        for (const auto& id : favorites_.operatorIds()) {
            matches.append({id, registry_.disabledReason(id, context_), 0});
        }
    } else {
        matches = registry_.search(query_->text(), context_);
    }
    for (const auto& match : matches) {
        const auto* descriptor = registry_.descriptor(match.id);
        auto text =
            QStringLiteral("%1  /  %2    [%3]")
                .arg(descriptor->chineseName, descriptor->englishName, descriptor->category);
        const auto shortcut = descriptor->action->shortcut().toString(QKeySequence::NativeText);
        if (!shortcut.isEmpty()) {
            text += QStringLiteral("    %1").arg(shortcut);
        }
        if (!match.disabledReason.isEmpty()) {
            text += QStringLiteral("　（不可用）");
        }
        if (favorites_.contains(match.id)) {
            text += QStringLiteral("　★");
        }
        auto* item = new QListWidgetItem(descriptor->action->icon(), text, results_);
        item->setData(Qt::UserRole, match.id);
        item->setToolTip(match.disabledReason.isEmpty() ? descriptor->chineseName
                                                        : match.disabledReason);
        if (!match.disabledReason.isEmpty()) {
            item->setForeground(QColor(QStringLiteral("#aaaaaa")));
        }
    }
    results_->setCurrentRow(results_->count() ? 0 : -1);
    refreshDescription();
}

void OperatorSearchPopup::refreshDescription() {
    const auto* item = results_->currentItem();
    const auto reason =
        item ? registry_.disabledReason(item->data(Qt::UserRole).toString(), context_)
        : mode_ == OperatorPopupMode::Favorites
            ? QStringLiteral("暂无快捷收藏；在 F3 结果上右键加入收藏。")
            : QStringLiteral("没有匹配操作；尚未实现的功能不列入结果。");
    description_->setText(
        QStringLiteral("调用区域：%1 · %2\n%3")
            .arg(OperatorRegistry::areaName(context_.area),
                 context_.editMode ? QStringLiteral("编辑模式") : QStringLiteral("对象模式"),
                 reason.isEmpty() ? QStringLiteral("Enter 执行 · Esc 取消 · 右键管理收藏")
                                  : reason));
}

void OperatorSearchPopup::showFavoriteContextMenu(const QPoint& position) {
    auto* item = results_->itemAt(position);
    if (!item) {
        return;
    }
    results_->setCurrentItem(item);
    const auto id = item->data(Qt::UserRole).toString();
    const bool removing = favorites_.contains(id);
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("OperatorFavoriteContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto* action =
        menu->addAction(removing ? QStringLiteral("移出快捷收藏") : QStringLiteral("加入快捷收藏"));
    action->setObjectName(QStringLiteral("ToggleOperatorFavorite"));
    connect(action, &QAction::triggered, this, [this, id, removing] {
        if (removing) {
            favorites_.removeOperator(id);
        } else {
            favorites_.addOperator(id);
        }
    });
    menu->popup(results_->viewport()->mapToGlobal(position));
}

void OperatorSearchPopup::accept() {
    const auto* item = results_->currentItem();
    if (!item) {
        return;
    }
    const auto id = item->data(Qt::UserRole).toString();
    if (!registry_.disabledReason(id, context_).isEmpty()) {
        refreshDescription();
        return;
    }
    QDialog::accept();
    restoreSourceFocus();
    // 模态操作必须在搜索窗口关闭后启动，不能让弹窗的失活事件取消新会话。
    registry_.execute(id, context_);
}

void OperatorSearchPopup::reject() {
    QDialog::reject();
    restoreSourceFocus();
}

void OperatorSearchPopup::restoreSourceFocus() {
    if (sourceFocus_ && sourceFocus_->isVisible()) {
        sourceFocus_->setFocus();
    }
}

bool OperatorSearchPopup::eventFilter(QObject* watched, QEvent* event) {
    if (watched == results_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            accept();
            return true;
        }
    }
    if (watched == query_ && event->type() == QEvent::InputMethod) {
        preediting_ = !static_cast<QInputMethodEvent*>(event)->preeditString().isEmpty();
    }
    if (watched == query_ && event->type() == QEvent::KeyPress && !preediting_) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up) {
            const int next = results_->currentRow() + (key->key() == Qt::Key_Down ? 1 : -1);
            if (next >= 0 && next < results_->count()) {
                results_->setCurrentRow(next);
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}
} // namespace mini3d::editor
