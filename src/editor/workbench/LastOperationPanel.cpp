/*
 * 模块名: LastOperationPanel
 * 功能概述: 绑定上一步挤出位移或内插厚度，每次字段提交仅替换同一历史的after。
 * 对外接口: LastOperationPanel.h；依赖关系: SceneViewModel、Qt 表单与安全数值控件。
 * 输入输出: 折叠/F9/数值提交到真实模型，更新和文档重置到受保护的字段状态。
 * 异常与错误: 恢复字段存储值后保留失败输入，Esc 只恢复该字段的未提交草稿。
 * 维护说明: 不从其他字段的舍入文本拼凑参数；未编辑轴始终取 ViewModel 精确值。
 */
#include "LastOperationPanel.h"

#include "CommitSpinBox.h"
#include "editor/SceneViewModel.h"

#include <QEvent>
#include <QFocusEvent>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <limits>

namespace mini3d::editor {
LastOperationPanel::LastOperationPanel(SceneViewModel& model, QWidget& viewport)
    : QFrame(&viewport), model_(model), viewport_(viewport) {
    setObjectName(QStringLiteral("LastOperationPanel"));
    setFrameShape(QFrame::StyledPanel);
    setAttribute(Qt::WA_NoMousePropagation);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);
    toggle_ = new QToolButton(this);
    toggle_->setObjectName(QStringLiteral("LastOperationToggle"));
    toggle_->setText(QStringLiteral("调整上一步 · 区域挤出"));
    toggle_->setCheckable(true);
    toggle_->setFocusPolicy(Qt::StrongFocus);
    toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle_->setArrowType(Qt::RightArrow);
    toggle_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(toggle_);
    body_ = new QWidget(this);
    auto* bodyLayout = new QVBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    offsetFields_ = new QWidget(body_);
    auto* form = new QFormLayout(offsetFields_);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(new QLabel(QStringLiteral("全局位移（世界单位）"), offsetFields_));
    for (int axis = 0; axis < 3; ++axis) {
        auto* field = new CommitSpinBox(offsetFields_);
        fields_[axis] = field;
        field->installEventFilter(this);
        field->setObjectName(QStringLiteral("LastOperationOffset%1").arg(QChar('X' + axis)));
        field->setDecimals(6);
        field->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
        field->setSingleStep(0.1);
        field->setAccessibleName(QStringLiteral("上一步全局位移 %1").arg(QChar('X' + axis)));
        form->addRow(QString(QChar('X' + axis)), field);
        connect(&model_, &SceneViewModel::operationFailed, field, &CommitSpinBox::rejectSubmission);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, axis](double value) {
            auto offset = model_.lastOperationWorldOffset();
            if (offset) {
                (*offset)[axis] = value;
                if (model_.adjustLastOperation(*offset))
                    return;
            }
            refresh(); // 同步拒绝时先恢复存储值，CommitSpinBox 再保留失败文本与错误。
        });
    }
    bodyLayout->addWidget(offsetFields_);
    insetFields_ = new QWidget(body_);
    auto* insetForm = new QFormLayout(insetFields_);
    insetForm->setContentsMargins(0, 0, 0, 0);
    insetForm->addRow(new QLabel(QStringLiteral("内插厚度（对象局部单位）"), insetFields_));
    thickness_ = new CommitSpinBox(insetFields_);
    thickness_->setObjectName(QStringLiteral("LastOperationInsetThickness"));
    thickness_->setAccessibleName(QStringLiteral("上一步内插局部厚度"));
    thickness_->setDecimals(6);
    thickness_->setRange(0, std::numeric_limits<double>::max());
    thickness_->setSingleStep(.1);
    thickness_->installEventFilter(this);
    insetForm->addRow(QStringLiteral("厚度"), thickness_);
    connect(&model_, &SceneViewModel::operationFailed, thickness_,
            &CommitSpinBox::rejectSubmission);
    connect(thickness_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!model_.adjustLastInset(value))
            refresh();
    });
    bodyLayout->addWidget(insetFields_);
    bevelFields_ = new QWidget(body_);
    auto* bevelForm = new QFormLayout(bevelFields_);
    bevelForm->setContentsMargins(0, 0, 0, 0);
    bevelForm->addRow(new QLabel(QStringLiteral("单段宽度（对象局部单位）"), bevelFields_));
    bevelWidth_ = new CommitSpinBox(bevelFields_);
    bevelWidth_->setObjectName(QStringLiteral("LastOperationBevelWidth"));
    bevelWidth_->setAccessibleName(QStringLiteral("上一步倒角局部宽度"));
    bevelWidth_->setDecimals(6);
    bevelWidth_->setRange(0, std::numeric_limits<double>::max());
    bevelWidth_->setSingleStep(.1);
    bevelWidth_->installEventFilter(this);
    bevelForm->addRow(QStringLiteral("宽度"), bevelWidth_);
    connect(&model_, &SceneViewModel::operationFailed, bevelWidth_,
            &CommitSpinBox::rejectSubmission);
    connect(bevelWidth_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (!model_.adjustLastBevel(value))
            refresh();
    });
    bodyLayout->addWidget(bevelFields_);
    message_ = new QLabel(body_);
    message_->setObjectName(QStringLiteral("LastOperationMessage"));
    message_->setWordWrap(true);
    bodyLayout->addWidget(message_);
    layout->addWidget(body_);
    body_->hide();
    connect(toggle_, &QToolButton::toggled, this, [this](bool expanded) {
        if (!expanded) {
            // 收起只丢弃尚未提交的文本，不撤销已经确认的参数。
            for (auto* field : fields_)
                field->resetInput();
            thickness_->resetInput();
            bevelWidth_->resetInput();
        }
        body_->setVisible(expanded);
        toggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        placePanel();
    });
    connect(&model_, &SceneViewModel::lastOperationChanged, this, &LastOperationPanel::refresh);
    connect(&model_, &SceneViewModel::documentReset, this, [this] {
        refresh();
        toggle_->setChecked(false);
        hide();
    });
    viewport_.installEventFilter(this);
    hide();
    refresh();
}
void LastOperationPanel::refresh() {
    const auto offset = model_.lastOperationWorldOffset();
    const auto thickness = model_.lastOperationInsetThickness();
    const auto bevel = model_.lastOperationBevelWidth();
    const bool available = offset.has_value() || thickness.has_value() || bevel.has_value();
    if (available) {
        showingInset_ = thickness.has_value();
        showingBevel_ = bevel.has_value();
    }
    offsetFields_->setVisible(!showingInset_ && !showingBevel_);
    insetFields_->setVisible(showingInset_);
    bevelFields_->setVisible(showingBevel_);
    toggle_->setText(showingBevel_   ? QStringLiteral("调整上一步 · 单段边倒角")
                     : showingInset_ ? QStringLiteral("调整上一步 · 面内插")
                                     : QStringLiteral("调整上一步 · 区域挤出"));
    for (int axis = 0; axis < 3; ++axis) {
        auto* field = fields_[axis];
        const QSignalBlocker blocker(field);
        field->setEnabled(offset.has_value());
        field->setValue(offset ? (*offset)[axis] : 0);
        field->resetInput();
    }
    {
        const QSignalBlocker blocker(thickness_);
        thickness_->setEnabled(thickness.has_value());
        thickness_->setValue(thickness.value_or(0));
        thickness_->resetInput();
    }
    {
        const QSignalBlocker blocker(bevelWidth_);
        bevelWidth_->setEnabled(bevel.has_value());
        bevelWidth_->setValue(bevel.value_or(0));
        bevelWidth_->resetInput();
    }
    message_->setText(available ? QStringLiteral("Enter/失焦提交；Esc 恢复字段。\n"
                                                 "每次调整替换上一步，不增加历史记录。")
                                : model_.lastOperationDisabledReason());
    if (available && isHidden())
        show(); // 自动出现不抢焦点；明确 F9/点击字段才进入文本编辑。
    placePanel();
}
void LastOperationPanel::open() {
    if (!model_.lastOperationDisabledReason().isEmpty())
        return;
    refresh();
    toggle_->setChecked(true);
    raise();
    auto* field = showingBevel_ ? bevelWidth_ : showingInset_ ? thickness_ : fields_[2];
    field->setFocus(Qt::ShortcutFocusReason);
    field->selectAll();
}
void LastOperationPanel::placePanel() {
    setFixedWidth(std::max(80, std::min(320, viewport_.width() - 20)));
    layout()->activate();
    const int needed = layout()->totalHeightForWidth(width());
    setFixedHeight(needed >= 0 ? needed : sizeHint().height());
    move(10, std::max(10, viewport_.height() - height() - 10));
}
bool LastOperationPanel::event(QEvent* event) {
    const bool handled = QFrame::event(event);
    if (event->type() == QEvent::LayoutRequest)
        placePanel();
    return handled;
}
bool LastOperationPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::FocusOut && toggle_->isChecked() && toggle_->hasFocus() &&
        static_cast<QFocusEvent*>(event)->reason() == Qt::MouseFocusReason) {
        // Qt 先转移点击焦点，再发按钮 pressed/toggled；在字段的失焦提交前丢弃草稿。
        if (auto* field = qobject_cast<CommitSpinBox*>(watched))
            field->resetInput();
    }
    if (watched == &viewport_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        placePanel();
    return QFrame::eventFilter(watched, event);
}
void LastOperationPanel::wheelEvent(QWheelEvent* event) {
    event->accept(); // 空白和说明上的滚轮不传给底下的世界相机。
}
} // namespace mini3d::editor
