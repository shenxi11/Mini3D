/*
 * 模块名: AnimationTimeline
 * 功能概述: 将中文时间轴控件与唯一动画会话、正式关键帧和独立草稿绑定。
 * 对外接口: AnimationTimeline构造；依赖关系: SceneViewModel、Qt Widgets。
 * 输入输出: 预览/定位/录键/草稿意图与分页只读键表。
 * 异常与错误: 所有业务错误由ViewModel报告，字段同步使用信号阻塞。
 * 维护说明: 原始旋转按17位有效数字展示，不折叠转数；播放不遍历键表。
 */
#include "AnimationTimeline.h"

#include "SceneViewModel.h"
#include "workbench/CommitSpinBox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace mini3d::editor {
namespace {
constexpr int kKeysPerPage = 256;
QString channelName(int channel) {
    return QString::fromUtf8(channel == 0 ? "位置" : channel == 1 ? "旋转（连续度数）" : "缩放");
}
QSpinBox* integerField(QWidget* parent, int maximum) {
    auto* field = new QSpinBox(parent);
    field->setRange(1, maximum);
    field->setKeyboardTracking(false);
    return field;
}
} // namespace
AnimationTimeline::AnimationTimeline(SceneViewModel& model, QWidget* parent)
    : QWidget(parent), model_(model) {
    setObjectName(QStringLiteral("AnimationTimeline"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    auto* controls = new QHBoxLayout;
    preview_ = new QCheckBox(QStringLiteral("动画预览"), this);
    preview_->setObjectName(QStringLiteral("AnimationPreview"));
    play_ = new QPushButton(QStringLiteral("播放"), this);
    play_->setObjectName(QStringLiteral("AnimationPlay"));
    frame_ = new CommitSpinBox(this);
    frame_->setObjectName(QStringLiteral("AnimationFrame"));
    frame_->setDecimals(6);
    frame_->setRange(1, core::kAnimationMaximumFrame);
    frame_->setToolTip(QStringLiteral("支持暂停子帧；录键和草稿要求整数帧。"));
    loop_ = new QCheckBox(QStringLiteral("循环"), this);
    controls->addWidget(preview_);
    controls->addWidget(play_);
    controls->addWidget(new QLabel(QStringLiteral("当前帧"), this));
    controls->addWidget(frame_);
    controls->addWidget(loop_);
    status_ = new QLabel(this);
    controls->addWidget(status_, 1);
    layout->addLayout(controls);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setObjectName(QStringLiteral("AnimationFrameSlider"));
    layout->addWidget(slider_);

    auto* settings = new QHBoxLayout;
    fps_ = integerField(this, 120);
    start_ = integerField(this, core::kAnimationMaximumFrame);
    end_ = integerField(this, core::kAnimationMaximumFrame);
    for (const auto& item : {std::pair{QStringLiteral("帧率"), fps_},
                              std::pair{QStringLiteral("起始"), start_},
                              std::pair{QStringLiteral("结束"), end_}}) {
        settings->addWidget(new QLabel(item.first, this));
        settings->addWidget(item.second);
    }
    settings_ = new QPushButton(QStringLiteral("应用范围/帧率"), this);
    settings->addWidget(settings_);
    settings->addStretch();
    object_ = new QLabel(this);
    settings->addWidget(object_);
    layout->addLayout(settings);

    auto* commands = new QHBoxLayout;
    for (int channel = 0; channel < 3; ++channel) {
        channels_[channel] = new QCheckBox(channelName(channel), this);
        channels_[channel]->setChecked(true);
        commands->addWidget(channels_[channel]);
    }
    interpolation_ = new QComboBox(this);
    interpolation_->addItems({QStringLiteral("线性"), QStringLiteral("保持常量")});
    commands->addWidget(interpolation_);
    record_ = new QPushButton(QStringLiteral("插入/替换关键帧"), this);
    record_->setObjectName(QStringLiteral("AnimationRecordKeyframes"));
    beginDraft_ = new QPushButton(QStringLiteral("编辑当前姿态"), this);
    commands->addWidget(record_);
    commands->addWidget(beginDraft_);
    commands->addStretch();
    layout->addLayout(commands);

    draftPanel_ = new QWidget(this);
    draftPanel_->setObjectName(QStringLiteral("AnimationDraftPanel"));
    auto* draftLayout = new QGridLayout(draftPanel_);
    draftLayout->addWidget(new QLabel(QStringLiteral("独立草稿：确认后录键；基础模型不变"), this), 0, 0, 1, 4);
    for (int group = 0; group < 3; ++group) {
        draftLayout->addWidget(new QLabel(channelName(group), this), group + 1, 0);
        for (int axis = 0; axis < 3; ++axis) {
            auto* field = new CommitSpinBox(this);
            field->setObjectName(QStringLiteral("AnimationDraft%1%2").arg(group).arg(axis));
            field->setDecimals(9);
            field->setRange(group == 1 ? -core::kAnimationMaximumRotationDegrees : -3.402823466e38,
                            group == 1 ? core::kAnimationMaximumRotationDegrees : 3.402823466e38);
            field->setPrefix(QStringLiteral("%1 ").arg(QChar('X' + axis)));
            draftFields_[group][axis] = field;
            draftLayout->addWidget(field, group + 1, axis + 1);
            connect(field, &QDoubleSpinBox::valueChanged, this, [this, group, axis](double value) {
                const auto values = model_.animationDraftValues();
                if (!values)
                    return;
                auto candidate = (*values)[group];
                candidate[axis] = value;
                if (!model_.setAnimationDraftChannel(static_cast<core::AnimationChannel>(group), candidate))
                    refreshSession();
            });
        }
    }
    confirmDraft_ = new QPushButton(QStringLiteral("确认草稿并录键"), this);
    cancelDraft_ = new QPushButton(QStringLiteral("取消草稿"), this);
    draftLayout->addWidget(confirmDraft_, 4, 1, 1, 2);
    draftLayout->addWidget(cancelDraft_, 4, 3);
    layout->addWidget(draftPanel_);
    keys_ = new QTableWidget(0, 5, this);
    keys_->setObjectName(QStringLiteral("AnimationKeyTable"));
    keys_->setHorizontalHeaderLabels({QStringLiteral("通道"), QStringLiteral("帧"),
                                      QStringLiteral("XYZ 原始值"), QStringLiteral("插值"),
                                      QStringLiteral("对象 ID")});
    keys_->setSelectionBehavior(QAbstractItemView::SelectRows);
    keys_->setSelectionMode(QAbstractItemView::SingleSelection);
    keys_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    keys_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    layout->addWidget(keys_, 1);
    auto* keyCommands = new QHBoxLayout;
    page_ = integerField(this, 1);
    keyCommands->addWidget(new QLabel(QStringLiteral("键表页（每页256）"), this));
    keyCommands->addWidget(page_);
    moveTo_ = integerField(this, core::kAnimationMaximumFrame);
    keyCommands->addWidget(new QLabel(QStringLiteral("目标帧"), this));
    keyCommands->addWidget(moveTo_);
    move_ = new QPushButton(QStringLiteral("移动所选键"), this);
    delete_ = new QPushButton(QStringLiteral("删除所选键"), this);
    interpolationButton_ = new QPushButton(QStringLiteral("设置所选键插值"), this);
    keyCommands->addWidget(move_);
    keyCommands->addWidget(delete_);
    keyCommands->addWidget(interpolationButton_);
    layout->addLayout(keyCommands);
    connect(preview_, &QCheckBox::toggled, this, [this](bool enabled) {
        model_.setAnimationPreview(enabled);
        refreshSession();
    });
    connect(play_, &QPushButton::clicked, this, [this] {
        if (model_.animationMode() == renderer_gl::AnimationMode::Playing)
            model_.pauseAnimation();
        else
            model_.playAnimation();
        refreshSession();
    });
    connect(frame_, &QDoubleSpinBox::valueChanged, this, [this](double frame) {
        if (!model_.setAnimationFrame(frame))
            refreshSession();
    });
    connect(slider_, &QSlider::valueChanged, this, [this](int frame) {
        if (!model_.setAnimationFrame(frame))
            refreshSession();
    });
    connect(loop_, &QCheckBox::toggled, this, [this](bool enabled) {
        model_.setAnimationLoop(enabled);
        refreshSession();
    });
    connect(settings_, &QPushButton::clicked, this, [this] {
        auto candidate = model_.scene()->animation();
        candidate.settings = {static_cast<std::uint32_t>(fps_->value()),
                              static_cast<std::uint32_t>(start_->value()),
                              static_cast<std::uint32_t>(end_->value())};
        model_.replaceAnimation(candidate, QStringLiteral("设置动画范围与帧率"));
        refreshDefinitions();
    });
    connect(record_, &QPushButton::clicked, this, [this] {
        model_.recordAnimationKeyframes(model_.selection()->selectedEntity(), channelMask(),
            interpolation_->currentIndex() == 0 ? core::AnimationInterpolation::Linear
                                                : core::AnimationInterpolation::Constant);
    });
    connect(beginDraft_, &QPushButton::clicked, this, [this] {
        model_.beginAnimationDraft(model_.selection()->selectedEntity(), channelMask());
    });
    connect(confirmDraft_, &QPushButton::clicked, &model_, &SceneViewModel::commitAnimationDraft);
    connect(cancelDraft_, &QPushButton::clicked, &model_, &SceneViewModel::cancelAnimationDraft);
    connect(delete_, &QPushButton::clicked, this, [this] {
        if (const auto key = selectedKey())
            model_.deleteAnimationKeyframe(key->entity, key->channel, key->frame);
    });
    connect(move_, &QPushButton::clicked, this, [this] {
        if (const auto key = selectedKey())
            model_.moveAnimationKeyframe(key->entity, key->channel, key->frame, moveTo_->value());
    });
    connect(interpolationButton_, &QPushButton::clicked, this, [this] {
        if (const auto key = selectedKey())
            model_.setAnimationKeyframeInterpolation(key->entity, key->channel, key->frame,
                interpolation_->currentIndex() == 0 ? core::AnimationInterpolation::Linear
                                                    : core::AnimationInterpolation::Constant);
    });
    connect(page_, &QSpinBox::valueChanged, this, &AnimationTimeline::refreshDefinitions);
    connect(keys_, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        if (const auto key = selectedKey())
            model_.setAnimationFrame(key->frame);
    });
    connect(&model_, &SceneViewModel::animationSessionChanged, this, &AnimationTimeline::refreshSession);
    connect(&model_, &SceneViewModel::animationPoseChanged, this, &AnimationTimeline::refreshSession);
    connect(&model_, &SceneViewModel::animationChanged, this, &AnimationTimeline::refreshDefinitions);
    connect(&model_, &SceneViewModel::documentReset, this, &AnimationTimeline::refreshDefinitions);
    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this, &AnimationTimeline::refreshDefinitions);
    refreshDefinitions();
}
std::array<bool, 3> AnimationTimeline::channelMask() const {
    return {channels_[0]->isChecked(), channels_[1]->isChecked(), channels_[2]->isChecked()};
}
std::optional<AnimationTimeline::KeySelection> AnimationTimeline::selectedKey() const {
    const auto* item = keys_->item(keys_->currentRow(), 0);
    if (!item)
        return std::nullopt;
    return KeySelection{item->data(Qt::UserRole).toULongLong(),
                         static_cast<core::AnimationChannel>(item->data(Qt::UserRole + 1).toInt()),
                         item->data(Qt::UserRole + 2).toUInt()};
}
void AnimationTimeline::refreshSession() {
    using Mode = renderer_gl::AnimationMode;
    const auto mode = model_.animationMode();
    const bool editable = mode == Mode::Base || mode == Mode::PreviewPaused;
    const bool paused = mode == Mode::PreviewPaused;
    const bool draft = mode == Mode::PoseDraft;
    const QSignalBlocker previewBlock(preview_), frameBlock(frame_), loopBlock(loop_), sliderBlock(slider_);
    preview_->setChecked(mode != Mode::Base);
    preview_->setEnabled(editable);
    loop_->setChecked(model_.isAnimationLoopEnabled());
    loop_->setEnabled(editable);
    frame_->setValue(model_.animationFrame());
    frame_->setEnabled(paused);
    slider_->setValue(static_cast<int>(model_.animationFrame()));
    slider_->setEnabled(paused);
    play_->setText(mode == Mode::Playing ? QStringLiteral("暂停") : QStringLiteral("播放"));
    play_->setEnabled(paused || mode == Mode::Playing);
    const auto id = model_.selection()->selectedEntity();
    const bool integerFrame = std::floor(model_.animationFrame()) == model_.animationFrame();
    record_->setEnabled(editable && id != 0 && integerFrame);
    beginDraft_->setEnabled(paused && id != 0 && integerFrame && model_.previewCamera() == 0);
    for (auto* channel : channels_)
        channel->setEnabled(!draft && editable);
    for (auto* widget : std::array<QWidget*, 9>{fps_, start_, end_, settings_, moveTo_, move_,
                                                delete_, interpolationButton_, interpolation_})
        widget->setEnabled(editable);
    status_->setText(mode == Mode::Base ? QStringLiteral("基础模式：属性面板编辑基础模型")
        : mode == Mode::Playing ? QStringLiteral("播放中：基础模型保持不变")
        : draft ? QStringLiteral("姿态草稿：确认录键 / 取消还原")
                : QStringLiteral("暂停预览：基础变换编辑须先关闭预览"));
    draftPanel_->setVisible(draft);
    if (const auto values = model_.animationDraftValues()) {
        for (int group = 0; group < 3; ++group)
            for (int axis = 0; axis < 3; ++axis) {
                auto* field = draftFields_[group][axis];
                const QSignalBlocker blocker(field);
                field->setValue((*values)[group][axis]);
                field->setEnabled(model_.animationDraftMask()[group]);
            }
    }
}
void AnimationTimeline::refreshDefinitions() {
    const auto& animation = model_.scene()->animation();
    const auto id = model_.selection()->selectedEntity();
    const auto* node = model_.scene()->find(id);
    object_->setText(node ? QStringLiteral("对象：%1").arg(QString::fromStdString(node->name))
                          : QStringLiteral("未选中对象"));
    const QSignalBlocker fpsBlock(fps_), startBlock(start_), endBlock(end_), sliderBlock(slider_), pageBlock(page_);
    fps_->setValue(animation.settings.fps);
    start_->setValue(animation.settings.startFrame);
    end_->setValue(animation.settings.endFrame);
    slider_->setRange(animation.settings.startFrame, animation.settings.endFrame);
    std::size_t count = 0;
    for (const auto& [trackId, track] : animation.tracks)
        if (trackId.first == id)
            count += track.keys.size();
    page_->setMaximum(std::max(1, static_cast<int>((count + kKeysPerPage - 1) / kKeysPerPage)));
    keys_->setRowCount(0);
    const auto first = static_cast<std::size_t>((page_->value() - 1) * kKeysPerPage);
    std::size_t index = 0;
    for (const auto& [trackId, track] : animation.tracks) {
        if (trackId.first != id)
            continue;
        for (const auto& key : track.keys) {
            if (index < first || index >= first + kKeysPerPage) {
                ++index;
                continue;
            }
            ++index;
            const auto row = keys_->rowCount();
            keys_->insertRow(row);
            auto* item = new QTableWidgetItem(channelName(static_cast<int>(trackId.second)));
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(trackId.first));
            item->setData(Qt::UserRole + 1, static_cast<int>(trackId.second));
            item->setData(Qt::UserRole + 2, key.frame);
            keys_->setItem(row, 0, item);
            keys_->setItem(row, 1, new QTableWidgetItem(QString::number(key.frame)));
            keys_->setItem(row, 2, new QTableWidgetItem(QStringLiteral("%1, %2, %3")
                .arg(key.value.x, 0, 'g', 17).arg(key.value.y, 0, 'g', 17).arg(key.value.z, 0, 'g', 17)));
            keys_->setItem(row, 3, new QTableWidgetItem(key.interpolation == core::AnimationInterpolation::Linear
                ? QStringLiteral("线性") : QStringLiteral("保持常量")));
            keys_->setItem(row, 4, new QTableWidgetItem(QString::number(trackId.first)));
        }
    }
    refreshSession();
}
} // namespace mini3d::editor
