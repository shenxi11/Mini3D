/*
 * 模块名: MirrorInspector
 * 功能概述: 展示单 Mirror 参数并发送原子候选操作。
 * 对外接口: MirrorInspector::refresh。
 * 依赖关系: Qt Widgets、SceneViewModel。
 * 输入输出: 控件值到单步历史，场景通知到只读刷新。
 * 异常与错误: 求值失败由 ViewModel 报告，刷新恢复实际参数。
 * 维护说明: 一次控件提交形成一条历史，不维护第二套参数状态。
 */
#include "MirrorInspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <limits>

namespace mini3d::editor {
MirrorInspector::MirrorInspector(SceneViewModel& model, QWidget* parent)
    : QWidget(parent), model_(model) {
    auto* form = new QFormLayout(this);
    add_ = new QPushButton(QStringLiteral("添加 Mirror"), this);
    add_->setObjectName(QStringLiteral("MirrorAddButton"));
    apply_ = new QPushButton(QStringLiteral("应用"), this);
    apply_->setObjectName(QStringLiteral("MirrorApplyButton"));
    remove_ = new QPushButton(QStringLiteral("删除"), this);
    remove_->setObjectName(QStringLiteral("MirrorRemoveButton"));
    enabled_ = new QCheckBox(QStringLiteral("启用"), this);
    enabled_->setObjectName(QStringLiteral("MirrorEnabled"));
    axis_ = new QComboBox(this);
    axis_->setObjectName(QStringLiteral("MirrorAxis"));
    axis_->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
    merge_ = new QCheckBox(QStringLiteral("合并中心边界"), this);
    merge_->setObjectName(QStringLiteral("MirrorMerge"));
    clipping_ = new QCheckBox(QStringLiteral("夹持组件变换"), this);
    clipping_->setObjectName(QStringLiteral("MirrorClipping"));
    threshold_ = new QDoubleSpinBox(this);
    threshold_->setObjectName(QStringLiteral("MirrorThreshold"));
    threshold_->setRange(0, std::numeric_limits<double>::max());
    threshold_->setDecimals(6);
    threshold_->setSingleStep(0.001);
    threshold_->setKeyboardTracking(false);
    form->addRow(add_);
    form->addRow(enabled_);
    form->addRow(QStringLiteral("局部轴"), axis_);
    form->addRow(merge_);
    form->addRow(QStringLiteral("阈值"), threshold_);
    form->addRow(clipping_);
    form->addRow(apply_, remove_);
    connect(add_, &QPushButton::clicked, this, [this] {
        model_.setMirrorOptions(model_.selection()->selectedEntity(), core::modeling::MirrorOptions{});
    });
    connect(apply_, &QPushButton::clicked, this, [this] {
        model_.applyMirror(model_.selection()->selectedEntity());
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        model_.setMirrorOptions(model_.selection()->selectedEntity(), std::nullopt);
    });
    connect(enabled_, &QCheckBox::toggled, this, &MirrorInspector::submit);
    connect(axis_, &QComboBox::currentIndexChanged, this, &MirrorInspector::submit);
    connect(merge_, &QCheckBox::toggled, this, &MirrorInspector::submit);
    connect(threshold_, &QDoubleSpinBox::valueChanged, this, &MirrorInspector::submit);
    connect(clipping_, &QCheckBox::toggled, this, &MirrorInspector::submit);
    connect(&model_, &SceneViewModel::sceneChanged, this, &MirrorInspector::refresh);
    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this,
            &MirrorInspector::refresh);
    connect(&model_, &SceneViewModel::operationFailed, this, &MirrorInspector::refresh);
    refresh();
}
void MirrorInspector::refresh() {
    const auto id = model_.selection()->selectedEntity();
    const auto* node = model_.scene()->find(id);
    const bool editable = node && node->editableMesh != 0;
    const auto options = model_.mirrorOptions(id);
    add_->setEnabled(editable && !options);
    for (auto* button : {apply_, remove_})
        button->setEnabled(options.has_value());
    for (QWidget* widget : {static_cast<QWidget*>(enabled_), static_cast<QWidget*>(axis_),
                            static_cast<QWidget*>(merge_), static_cast<QWidget*>(threshold_),
                            static_cast<QWidget*>(clipping_)})
        widget->setEnabled(options.has_value());
    if (!options)
        return;
    const QSignalBlocker enabledBlock(enabled_), axisBlock(axis_), mergeBlock(merge_);
    const QSignalBlocker clippingBlock(clipping_), thresholdBlock(threshold_);
    enabled_->setChecked(options->enabled);
    axis_->setCurrentIndex(static_cast<int>(options->axis));
    merge_->setChecked(options->merge);
    clipping_->setChecked(options->clipping);
    threshold_->setValue(options->threshold);
}
void MirrorInspector::submit() {
    const auto id = model_.selection()->selectedEntity();
    auto options = model_.mirrorOptions(id);
    if (!options)
        return;
    options->enabled = enabled_->isChecked();
    options->axis = static_cast<core::modeling::MirrorAxis>(axis_->currentIndex());
    options->merge = merge_->isChecked();
    options->clipping = clipping_->isChecked();
    options->threshold = threshold_->value();
    model_.setMirrorOptions(id, options);
}
} // namespace mini3d::editor
