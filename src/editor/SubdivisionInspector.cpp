/*
 * 模块名: SubdivisionInspector
 * 功能概述: 将细分卡片意图绑定到离线求值及原子历史接口。
 * 对外接口: SubdivisionInspector.h；依赖关系: SceneViewModel、Qt。
 * 输入输出: 1–2级四边网格细分选项与实际状态同步。
 * 异常与错误: 无效候选不留控件伪状态；失败通知后刷新。
 * 维护说明: 应用烘焙整个固定链，源笼选择仍由既有编辑模式提供。
 */
#include "SubdivisionInspector.h"

#include "SceneViewModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>

namespace mini3d::editor {
SubdivisionInspector::SubdivisionInspector(SceneViewModel& model, QWidget* parent)
    : QWidget(parent), model_(model) {
    auto* form = new QFormLayout(this);
    form->addRow(new QLabel(QStringLiteral("细分 · Catmull–Clark（四边网格）"), this));
    add_ = new QPushButton(QStringLiteral("添加细分"), this);
    add_->setObjectName(QStringLiteral("SubdivisionAddButton"));
    enabled_ = new QCheckBox(QStringLiteral("启用"), this);
    enabled_->setObjectName(QStringLiteral("SubdivisionEnabled"));
    levels_ = new QComboBox(this);
    levels_->setObjectName(QStringLiteral("SubdivisionLevels"));
    levels_->addItems({QStringLiteral("1 级"), QStringLiteral("2 级")});
    apply_ = new QPushButton(QStringLiteral("应用整个求值链"), this);
    apply_->setObjectName(QStringLiteral("SubdivisionApplyButton"));
    remove_ = new QPushButton(QStringLiteral("删除细分"), this);
    remove_->setObjectName(QStringLiteral("SubdivisionRemoveButton"));
    form->addRow(add_);
    form->addRow(enabled_);
    form->addRow(QStringLiteral("级别"), levels_);
    form->addRow(apply_, remove_);
    form->addRow(new QLabel(QStringLiteral("顺序固定：Mirror → 细分。仅编辑原始源笼。"), this));
    connect(add_, &QPushButton::clicked, this, [this] {
        model_.setSubdivisionOptions(model_.selection()->selectedEntity(),
                                     core::modeling::SubdivisionOptions{});
    });
    connect(apply_, &QPushButton::clicked, this, [this] {
        model_.applySubdivision(model_.selection()->selectedEntity());
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        model_.setSubdivisionOptions(model_.selection()->selectedEntity(), std::nullopt);
    });
    connect(enabled_, &QCheckBox::toggled, this, &SubdivisionInspector::submit);
    connect(levels_, &QComboBox::currentIndexChanged, this, &SubdivisionInspector::submit);
    connect(&model_, &SceneViewModel::sceneChanged, this, &SubdivisionInspector::refresh);
    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this,
            &SubdivisionInspector::refresh);
    connect(&model_, &SceneViewModel::operationFailed, this, &SubdivisionInspector::refresh);
    refresh();
}
void SubdivisionInspector::refresh() {
    const auto id = model_.selection()->selectedEntity();
    const auto* node = model_.scene()->find(id);
    const auto options = model_.subdivisionOptions(id);
    add_->setEnabled(node && node->editableMesh != 0 && !options);
    for (auto* button : {apply_, remove_})
        button->setEnabled(options.has_value());
    enabled_->setEnabled(options.has_value());
    levels_->setEnabled(options.has_value());
    if (!options)
        return;
    const QSignalBlocker enabledBlock(enabled_), levelsBlock(levels_);
    enabled_->setChecked(options->enabled);
    levels_->setCurrentIndex(options->levels - 1);
}
void SubdivisionInspector::submit() {
    const auto id = model_.selection()->selectedEntity();
    auto options = model_.subdivisionOptions(id);
    if (!options)
        return;
    options->enabled = enabled_->isChecked();
    options->levels = levels_->currentIndex() + 1;
    model_.setSubdivisionOptions(id, options);
}
} // namespace mini3d::editor
