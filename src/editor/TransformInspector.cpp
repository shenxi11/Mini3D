/*
 * 模块名: TransformInspector
 * 功能概述: 第三周场景选择与属性编辑的 TransformInspector 层。
 * 对外接口: TransformInspector
 * 依赖关系: Qt Widgets/Core、mini3d_core
 * 输入输出: 输入用户意图或场景通知，输出模型状态或界面刷新。
 * 异常与错误: 通过返回值和 operationFailed 报告非法编辑。
 * 维护说明: 同步 UI 线程操作；不持有节点地址或 GPU 资源。
 */
#include "TransformInspector.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
namespace mini3d::editor {
TransformInspector::TransformInspector(SceneViewModel& viewModel, QWidget* parent)
    : QWidget(parent), viewModel_(viewModel) {
    setObjectName(QStringLiteral("TransformInspector"));
    auto* form = new QFormLayout(this);
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("EntityName"));
    visible_ = new QCheckBox(QStringLiteral("可见（自身）"), this);
    visible_->setObjectName(QStringLiteral("EntityVisible"));
    parent_ = new QComboBox(this);
    parent_->setObjectName(QStringLiteral("EntityParent"));
    parent_->setToolTip(QStringLiteral("更换父对象时保留局部变换，世界位置可能改变。"));
    form->addRow(QStringLiteral("名称"), name_);
    form->addRow(QStringLiteral("显示"), visible_);
    form->addRow(QStringLiteral("父对象"), parent_);
    const std::array<QString, 3> groups{QStringLiteral("Position"), QStringLiteral("Rotation"),
                                        QStringLiteral("Scale")};
    const std::array<QString, 3> labels{QStringLiteral("位置"), QStringLiteral("旋转"),
                                        QStringLiteral("缩放")};
    const std::array<QString, 3> axes{QStringLiteral("X"), QStringLiteral("Y"),
                                      QStringLiteral("Z")};
    for (int group = 0; group < 3; ++group) {
        auto* row = new QHBoxLayout;
        for (int axis = 0; axis < 3; ++axis) {
            auto* spin = new QDoubleSpinBox(this);
            spin->setObjectName(groups[group] + axes[axis]);
            spin->setDecimals(3);
            spin->setRange(-10000, 10000);
            spin->setSingleStep(group == 1 ? 1.0 : 0.1);
            spin->setKeyboardTracking(false);
            spin->setPrefix(axes[axis] + QStringLiteral(" "));
            spin->setToolTip(group == 1 ? QStringLiteral("局部欧拉角，单位：度")
                                        : QStringLiteral("局部坐标值"));
            row->addWidget(spin);
            values_[group * 3 + axis] = spin;
            connect(spin, &QDoubleSpinBox::valueChanged, this, [this, group, axis](double value) {
                viewModel_.setTransformComponent(viewModel_.selection()->selectedEntity(), group,
                                                 axis, value);
            });
        }
        form->addRow(labels[group], row);
    }
    message_ = new QLabel(this);
    message_->setWordWrap(true);
    message_->setObjectName(QStringLiteral("InspectorMessage"));
    form->addRow(message_);
    connect(name_, &QLineEdit::editingFinished, this, [this] {
        const auto id = viewModel_.selection()->selectedEntity();
        const auto* node = viewModel_.scene()->find(id);
        if (node != nullptr && QString::fromStdString(node->name) != name_->text()) {
            viewModel_.renameEntity(id, name_->text());
        }
    });
    connect(visible_, &QCheckBox::toggled, this, [this](bool checked) {
        viewModel_.setVisible(viewModel_.selection()->selectedEntity(), checked);
    });
    connect(parent_, &QComboBox::activated, this, [this](int index) {
        viewModel_.setParent(viewModel_.selection()->selectedEntity(),
                             parent_->itemData(index).toULongLong());
    });
    connect(viewModel_.selection(), &SelectionModel::selectedEntityChanged, this, [this] {
        refresh();
    });
    connect(&viewModel_, &SceneViewModel::sceneChanged, this, [this] {
        refresh();
    });
    connect(&viewModel_, &SceneViewModel::operationFailed, this, [this](const QString& text) {
        refresh();
        message_->setText(text);
    });
    refresh();
}
void TransformInspector::appendParentOptions(core::EntityId id, int depth,
                                             core::EntityId selected) {
    if (id == selected) {
        return;
    } // 自身及整棵子树不能成为候选父节点。
    const auto* node = viewModel_.scene()->find(id);
    parent_->addItem(QString(depth * 2, QChar(' ')) + QString::fromStdString(node->name) +
                         QStringLiteral(" [%1]").arg(id),
                     QVariant::fromValue<qulonglong>(id));
    for (const auto child : node->children) {
        appendParentOptions(child, depth + 1, selected);
    }
}
void TransformInspector::refresh() {
    const auto selected = viewModel_.selection()->selectedEntity();
    const auto* node = viewModel_.scene()->find(selected);
    const QSignalBlocker nameBlock(name_), visibleBlock(visible_), parentBlock(parent_);
    name_->setEnabled(node != nullptr);
    visible_->setEnabled(node != nullptr);
    parent_->setEnabled(node != nullptr);
    name_->setText(node != nullptr ? QString::fromStdString(node->name) : QString{});
    visible_->setChecked(node != nullptr && node->visible);
    parent_->clear();
    parent_->addItem(QStringLiteral("<场景根节点>"), QVariant::fromValue<qulonglong>(0));
    for (const auto root : viewModel_.scene()->roots()) {
        appendParentOptions(root, 0, selected);
    }
    parent_->setCurrentIndex(
        node != nullptr ? parent_->findData(QVariant::fromValue<qulonglong>(node->parent)) : 0);
    const core::Transform transform = node != nullptr ? node->transform : core::Transform{};
    const std::array<glm::vec3, 3> groups{
        transform.position, glm::degrees(glm::eulerAngles(transform.rotation)), transform.scale};
    for (int group = 0; group < 3; ++group) {
        for (int axis = 0; axis < 3; ++axis) {
            auto* spin = values_[group * 3 + axis];
            const QSignalBlocker blocker(spin);
            spin->setEnabled(node != nullptr);
            spin->setValue(groups[group][axis]);
        }
    }
    if (node == nullptr) {
        message_->setText(QStringLiteral("未选择对象"));
    } else {
        message_->setText(QStringLiteral("局部变换｜旋转：度｜缩放绝对值 ≥ 0.001"));
    }
}
} // namespace mini3d::editor
