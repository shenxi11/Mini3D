/*
 * 模块名: CollectionPanel
 * 功能概述: 显示独立集合身份、原对象成员链接与持久显隐状态。
 * 对外接口: CollectionPanel.h；依赖关系: SceneViewModel、Qt表单。
 * 输入输出: 用户组织意图到原子集合快照，场景通知到稳定ID列表。
 * 异常与错误: 名称取消无操作，失败不在列表保留伪状态。
 * 维护说明: 刷新阻断item信号，不通过成员链接换父或更改TRS。
 */
#include "CollectionPanel.h"

#include "SceneViewModel.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace mini3d::editor {
namespace {
constexpr int kCollectionRole = Qt::UserRole;
constexpr int kEntityRole = Qt::UserRole + 1;
} // namespace
CollectionPanel::CollectionPanel(SceneViewModel& model, QWidget* parent)
    : QWidget(parent), model_(model) {
    setObjectName(QStringLiteral("CollectionPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(QStringLiteral("集合（组织归属，不改变父子关系）"), this));
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("CollectionTree"));
    tree_->setHeaderHidden(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setMinimumHeight(90);
    layout->addWidget(tree_);
    auto* buttons = new QHBoxLayout;
    auto* create = new QPushButton(QStringLiteral("新建"), this);
    create->setObjectName(QStringLiteral("CollectionCreateButton"));
    remove_ = new QPushButton(QStringLiteral("删除"), this);
    remove_->setObjectName(QStringLiteral("CollectionRemoveButton"));
    remove_->setToolTip(QStringLiteral("删除当前集合，仅解除归属，不删除对象。"));
    link_ = new QPushButton(QStringLiteral("移入"), this);
    link_->setObjectName(QStringLiteral("CollectionAssignButton"));
    link_->setToolTip(QStringLiteral("将所选对象移入当前集合，不改变父对象或变换。"));
    unlink_ = new QPushButton(QStringLiteral("移出"), this);
    unlink_->setObjectName(QStringLiteral("CollectionUnassignButton"));
    for (auto* button : {create, remove_, link_, unlink_})
        buttons->addWidget(button);
    layout->addLayout(buttons);
    connect(create, &QPushButton::clicked, this, &CollectionPanel::createCollection);
    connect(remove_, &QPushButton::clicked, this, [this] {
        model_.removeCollection(selected_);
    });
    connect(link_, &QPushButton::clicked, this, [this] {
        model_.assignEntityToCollection(model_.selection()->selectedEntity(), selected_);
    });
    connect(unlink_, &QPushButton::clicked, this, [this] {
        model_.assignEntityToCollection(model_.selection()->selectedEntity(), 0);
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        selected_ = item ? item->data(0, kCollectionRole).toULongLong() : 0;
        const auto entity = item ? item->data(0, kEntityRole).toULongLong() : 0;
        if (entity != 0)
            model_.selection()->setSelectedEntity(entity);
        refreshActions();
    });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        if (item->data(0, kEntityRole).toULongLong() != 0)
            return;
        const auto id = item->data(0, kCollectionRole).toULongLong();
        const auto& collections = model_.scene()->collections();
        for (const auto& collection : collections) {
            if (collection.id != id)
                continue;
            if (item->text(0) != QString::fromStdString(collection.name))
                model_.renameCollection(id, item->text(0));
            else
                model_.setCollectionVisible(id, item->checkState(0) == Qt::Checked);
            break;
        }
    });
    // 等价重命名不触发场景通知，也需在item信号结束后同步规范化名称与实际显隐。
    connect(tree_, &QTreeWidget::itemChanged, this, &CollectionPanel::refresh,
            Qt::QueuedConnection);
    connect(&model_, &SceneViewModel::sceneChanged, this, &CollectionPanel::refresh,
            Qt::QueuedConnection);
    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this,
            &CollectionPanel::refresh, Qt::QueuedConnection);
    connect(&model_, &SceneViewModel::operationFailed, this, &CollectionPanel::refresh,
            Qt::QueuedConnection);
    connect(&model_, &SceneViewModel::documentReset, this, [this] {
        selected_ = 0;
        refresh();
    });
    refresh();
}
void CollectionPanel::createCollection() {
    bool accepted = false;
    const auto name =
        QInputDialog::getText(this, QStringLiteral("新建集合"), QStringLiteral("名称"),
                              QLineEdit::Normal, QStringLiteral("集合"), &accepted);
    if (!accepted)
        return;
    const auto id = model_.createCollection(name);
    if (id != 0)
        selected_ = id;
    refresh();
}
void CollectionPanel::refresh() {
    const QSignalBlocker blocker(tree_);
    tree_->clear();
    bool selectedExists = false;
    const auto entity = model_.selection()->selectedEntity();
    for (const auto& collection : model_.scene()->collections()) {
        auto* group = new QTreeWidgetItem(tree_);
        group->setText(0, QString::fromStdString(collection.name));
        group->setData(0, kCollectionRole, QVariant::fromValue<qulonglong>(collection.id));
        group->setFlags(group->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        group->setCheckState(0, collection.visible ? Qt::Checked : Qt::Unchecked);
        group->setToolTip(0, QStringLiteral("集合 %1 · %2 个成员 · 删除仅解除归属")
                                 .arg(collection.id)
                                 .arg(collection.members.size()));
        group->setExpanded(true);
        if (collection.id == selected_) {
            selectedExists = true;
            tree_->setCurrentItem(group);
        }
        for (const auto member : collection.members) {
            auto* item = new QTreeWidgetItem(group);
            item->setText(0, QString::fromStdString(model_.scene()->find(member)->name));
            item->setData(0, kCollectionRole, QVariant::fromValue<qulonglong>(collection.id));
            item->setData(0, kEntityRole, QVariant::fromValue<qulonglong>(member));
            if (collection.id == selected_ && member == entity)
                tree_->setCurrentItem(item);
        }
    }
    if (!selectedExists)
        selected_ = 0;
    refreshActions();
}
void CollectionPanel::refreshActions() {
    const auto entity = model_.selection()->selectedEntity();
    bool selectedExists = false, assigned = false;
    for (const auto& collection : model_.scene()->collections()) {
        selectedExists = selectedExists || collection.id == selected_;
        assigned = assigned || collection.members.contains(entity);
    }
    remove_->setEnabled(selectedExists);
    link_->setEnabled(selectedExists && entity != 0);
    unlink_->setEnabled(assigned);
}
} // namespace mini3d::editor
