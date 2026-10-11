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
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

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
    frame_->setDecimals(17);
    frame_->setRange(1, core::kAnimationMaximumFrame);
    frame_->setToolTip(QStringLiteral("支持暂停子帧和标量录键；旧 TRS 录键和草稿要求整数帧。"));
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
    fps_->setObjectName(QStringLiteral("AnimationFps"));
    start_->setObjectName(QStringLiteral("AnimationStart"));
    end_->setObjectName(QStringLiteral("AnimationEnd"));
    for (const auto& item : {std::pair{QStringLiteral("帧率"), fps_},
                              std::pair{QStringLiteral("起始"), start_},
                              std::pair{QStringLiteral("结束"), end_}}) {
        settings->addWidget(new QLabel(item.first, this));
        settings->addWidget(item.second);
    }
    settings_ = new QPushButton(QStringLiteral("应用范围/帧率"), this);
    settings_->setObjectName(QStringLiteral("AnimationApplySettings"));
    settings->addWidget(settings_);
    fpsLabel_ = new QLabel(this);
    fpsLabel_->setObjectName(QStringLiteral("AnimationEffectiveFps"));
    settings->addWidget(fpsLabel_);
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

    auto* propertyCommands = new QHBoxLayout;
    property_ = new QComboBox(this);
    property_->setObjectName(QStringLiteral("AnimationProperty"));
    property_->addItems({QStringLiteral("位置 X"), QStringLiteral("位置 Y"), QStringLiteral("位置 Z"),
        QStringLiteral("材料 roughness"), QStringLiteral("Sun 辐照度 W/m²")});
    propertyValue_ = new QLineEdit(this);
    propertyValue_->setObjectName(QStringLiteral("AnimationPropertyValue"));
    propertyValue_->installEventFilter(this);
    propertyValue_->setMinimumWidth(100);
    propertyValue_->setMaximumWidth(260);
    propertyValue_->setToolTip(QStringLiteral("标量录键输入；不修改基础作者值。线性/常量沿用上方插值选择。"));
    propertyRecord_ = new QPushButton(QStringLiteral("录制/替换标量键"), this);
    propertyRecord_->setObjectName(QStringLiteral("AnimationRecordProperty"));
    propertyCommands->addWidget(property_);
    propertyCommands->addWidget(new QLabel(QStringLiteral("录键值"), this));
    propertyCommands->addWidget(propertyValue_);
    propertyCommands->addWidget(propertyRecord_);
    propertyCommands->addStretch();
    layout->addLayout(propertyCommands);
    propertyStatus_ = new QLabel(this);
    propertyStatus_->setObjectName(QStringLiteral("AnimationPropertyStatus"));
    propertyStatus_->setWordWrap(true);
    layout->addWidget(propertyStatus_);
    propertyKeys_ = new QTableWidget(0, 4, this);
    propertyKeys_->setObjectName(QStringLiteral("AnimationPropertyKeyTable"));
    propertyKeys_->setHorizontalHeaderLabels({QStringLiteral("精确帧"), QStringLiteral("作者键值"),
        QStringLiteral("出射插值"), QStringLiteral("键 ID")});
    propertyKeys_->setSelectionBehavior(QAbstractItemView::SelectRows);
    propertyKeys_->setSelectionMode(QAbstractItemView::SingleSelection);
    propertyKeys_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    propertyKeys_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    propertyKeys_->setMaximumHeight(120);
    layout->addWidget(propertyKeys_);
    auto* propertyPages = new QHBoxLayout;
    propertyFirst_ = new QPushButton(QStringLiteral("标量键首页"), this);
    propertyFirst_->setObjectName(QStringLiteral("AnimationPropertyFirstPage"));
    propertyNextPage_ = new QPushButton(QStringLiteral("下一页（每页256）"), this);
    propertyNextPage_->setObjectName(QStringLiteral("AnimationPropertyNextPage"));
    propertyPages->addWidget(propertyFirst_);
    propertyPages->addWidget(propertyNextPage_);
    propertyPages->addStretch();
    layout->addLayout(propertyPages);
    connect(property_, &QComboBox::currentIndexChanged, this, [this] {
        refreshPropertyKeys();
        refreshPropertyValues();
    });
    connect(propertyRecord_, &QPushButton::clicked, this, &AnimationTimeline::recordPropertyKey);
    connect(propertyValue_, &QLineEdit::editingFinished, this, [this] { (void)propertyInputValue(); });
    connect(propertyFirst_, &QPushButton::clicked, this, [this] { refreshPropertyKeys(); });
    connect(propertyNextPage_, &QPushButton::clicked, this, [this] {
        propertyAfter_ = propertyNext_;
        refreshPropertyKeys(false);
    });
    connect(propertyKeys_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const auto* item = propertyKeys_->item(row, 0);
        if (!item)
            return;
        const auto time = item->data(Qt::UserRole).toDouble();
        const auto value = item->data(Qt::UserRole + 1).toDouble();
        const auto interpolation = item->data(Qt::UserRole + 2).toInt();
        api::AnimationSetFrameRequest request;
        request.document = model_.apiDocumentState().document;
        request.expectedDocumentRevision = model_.apiDocumentState().documentRevision;
        request.expectedSessionRevision = model_.animationSessionRevision();
        request.frame = time;
        const auto result = model_.setAnimationFrameExplicit(request);
        if (!result.hasValue()) {
            propertyStatus_->setText(result.error->message);
            return;
        }
        propertyValueOriginal_ = value;
        propertyValue_->setText(propertyValue_->locale().toString(value, 'g', 17));
        interpolation_->setCurrentIndex(interpolation);
    });

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
        const auto& animation = model_.scene()->animation();
        if (animation.properties.rationalFps) {
            api::AnimationSetRationalSettingsRequest request;
            request.document = model_.apiDocumentState().document;
            request.expectedDocumentRevision = model_.apiDocumentState().documentRevision;
            request.settings.rationalFps = *animation.properties.rationalFps;
            if (request.settings.rationalFps.denominator == 1)
                request.settings.rationalFps.numerator = static_cast<std::uint32_t>(fps_->value());
            request.settings.startFrame = static_cast<std::uint32_t>(start_->value());
            request.settings.endFrame = static_cast<std::uint32_t>(end_->value());
            request.settings.loopPolicy = animation.properties.loopPolicy;
            const auto result = model_.setRationalAnimationSettingsExplicit(request);
            if (!result.hasValue())
                model_.operationFailed(result.error->message);
        } else {
            api::AnimationSetSettingsRequest request;
            request.document = model_.apiDocumentState().document;
            request.expectedDocumentRevision = model_.apiDocumentState().documentRevision;
            request.settings = {static_cast<std::uint32_t>(fps_->value()),
                static_cast<std::uint32_t>(start_->value()), static_cast<std::uint32_t>(end_->value())};
            const auto result = model_.setAnimationSettingsExplicit(request);
            if (!result.hasValue())
                model_.operationFailed(result.error->message);
        }
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
    connect(&model_, &SceneViewModel::sceneChanged, this, &AnimationTimeline::refreshDefinitions);
    connect(&model_, &SceneViewModel::documentReset, this, &AnimationTimeline::refreshDefinitions);
    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this, &AnimationTimeline::refreshDefinitions);
    refreshDefinitions();
}
std::optional<core::PropertyAddress> AnimationTimeline::selectedProperty() const {
    const auto id = model_.selection()->selectedEntity();
    const auto* node = model_.scene()->find(id);
    if (!node)
        return std::nullopt;
    core::PropertyAddress address;
    address.documentUuid = model_.scene()->documentUuid();
    address.owner = core::EntityOwner{id};
    const auto property = property_->currentIndex();
    if (property < 3) {
        address.property = core::PropertyId::Position;
        address.component = static_cast<core::PropertyComponent>(property);
    } else if (property == 3) {
        if (node->materialSlots.size() != 1)
            return std::nullopt;
        address.owner = core::MaterialOwner{node->materialSlots.front().material};
        address.property = core::PropertyId::PbrRoughness;
        address.component = core::PropertyComponent::Scalar;
    } else {
        if (!node->sun)
            return std::nullopt;
        address.property = core::PropertyId::SunIrradiance;
        address.component = core::PropertyComponent::Scalar;
    }
    return address;
}
void AnimationTimeline::recordPropertyKey() {
    const auto address = selectedProperty();
    if (!address)
        return;
    if (!frame_->validationMessage().isEmpty()) {
        propertyStatus_->setText(frame_->validationMessage());
        return;
    }
    const auto value = propertyInputValue();
    if (!value)
        return;
    api::AnimationUpsertPropertyKeyframeRequest request;
    request.document = model_.apiDocumentState().document;
    request.expectedDocumentRevision = model_.apiDocumentState().documentRevision;
    request.address = *address;
    request.time = model_.animationFrame();
    request.value = *value;
    request.outgoing = interpolation_->currentIndex() == 0
        ? core::PropertyInterpolation::Linear : core::PropertyInterpolation::Constant;
    const auto input = propertyValue_->text();
    const auto result = model_.upsertAnimationPropertyKeyframeExplicit(request);
    if (!result.hasValue()) {
        propertyValue_->setText(input);
        propertyStatus_->setText(result.error->message);
    } else {
        refreshPropertyKeys();
    }
}
bool AnimationTimeline::eventFilter(QObject* object, QEvent* event) {
    if (object == propertyValue_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        propertyValue_->setText(propertyValue_->locale().toString(propertyValueOriginal_, 'g', 17));
        return true;
    }
    return QWidget::eventFilter(object, event);
}
std::optional<double> AnimationTimeline::propertyInputValue() {
    const auto address = selectedProperty();
    if (!address)
        return std::nullopt;
    bool valid = false;
    const auto value = propertyValue_->locale().toDouble(propertyValue_->text(), &valid);
    const auto maximum = address->property == core::PropertyId::PbrRoughness
        ? 1 : std::numeric_limits<float>::max();
    const auto minimum = address->property == core::PropertyId::Position ? -maximum : 0;
    if (!valid || !std::isfinite(value) || value < minimum || value > maximum) {
        propertyStatus_->setText(QStringLiteral("当前录键数值不合法；输入未提交。"));
        return std::nullopt;
    }
    return value;
}
void AnimationTimeline::refreshPropertyKeys(bool firstPage) {
    if (firstPage) {
        propertyAfter_.reset();
        propertyRevision_ = model_.apiDocumentState().documentRevision;
    }
    propertyNext_.reset();
    propertyNextPage_->setEnabled(false);
    propertyKeys_->setRowCount(0);
    const auto address = selectedProperty();
    propertyFirst_->setEnabled(address.has_value());
    if (!address)
        return;
    api::AnimationReadPropertyKeyframesRequest request;
    request.document = model_.apiDocumentState().document;
    request.expectedDocumentRevision = propertyRevision_;
    request.address = *address;
    request.afterTime = propertyAfter_;
    request.limit = kKeysPerPage;
    const auto result = model_.readAnimationPropertyKeyframesExplicit(request);
    if (!result.hasValue()) {
        propertyStatus_->setText(result.error->message);
        return;
    }
    for (const auto& key : result.value->keys) {
        const auto row = propertyKeys_->rowCount();
        propertyKeys_->insertRow(row);
        auto* time = new QTableWidgetItem(QString::number(key.time, 'g', 17));
        time->setData(Qt::UserRole, key.time);
        time->setData(Qt::UserRole + 1, key.value);
        time->setData(Qt::UserRole + 2, key.outgoing == core::PropertyInterpolation::Linear ? 0 : 1);
        propertyKeys_->setItem(row, 0, time);
        propertyKeys_->setItem(row, 1, new QTableWidgetItem(QString::number(key.value, 'g', 17)));
        propertyKeys_->setItem(row, 2, new QTableWidgetItem(key.outgoing == core::PropertyInterpolation::Linear
            ? QStringLiteral("线性") : QStringLiteral("保持常量")));
        propertyKeys_->setItem(row, 3, new QTableWidgetItem(QString::number(key.id.value)));
    }
    propertyNext_ = result.value->nextAfterTime;
    propertyNextPage_->setEnabled(propertyNext_.has_value());
}
void AnimationTimeline::refreshPropertyValues() {
    const auto address = selectedProperty();
    const auto mode = model_.animationMode();
    const bool editable = (mode == AnimationMode::Base || mode == AnimationMode::PreviewPaused) &&
        model_.apiBusyReasons().isEmpty();
    property_->setEnabled(editable);
    propertyValue_->setEnabled(editable && address.has_value());
    propertyRecord_->setEnabled(editable && address.has_value());
    if (!address) {
        propertyStatus_->setText(QStringLiteral("选择对象的位置、单槽材料的 roughness，或 Sun 辐照度。"));
        return;
    }
    const auto* node = model_.scene()->find(model_.selection()->selectedEntity());
    double author = 0;
    if (const auto* owner = std::get_if<core::MaterialOwner>(&address->owner)) {
        const auto* material = model_.scene()->materialData().find(owner->material);
        if (!material)
            return;
        author = material->parameters.roughness;
    } else if (address->property == core::PropertyId::SunIrradiance) {
        author = node->sun->irradiance;
    } else {
        author = node->transform.position[static_cast<int>(address->component)];
    }
    std::optional<double> effective;
    const auto pose = model_.installedAnimationPose();
    const auto* state = pose && pose->sceneState ? &*pose->sceneState : nullptr;
    if (mode == AnimationMode::Base) {
        effective = author;
    } else if (state && state->content) {
        const auto& content = *state->content;
        const auto trace = std::find_if(content.propertySources.begin(), content.propertySources.end(),
            [&](const auto& value) { return value.address == *address; });
        if (trace != content.propertySources.end()) {
            effective = trace->effectiveValue;
        } else if (const auto* owner = std::get_if<core::MaterialOwner>(&address->owner)) {
            const auto material = content.materials.find(owner->material);
            if (material != content.materials.end())
                effective = material->second.effective.roughness;
        } else if (address->property == core::PropertyId::Position) {
            if (const auto* value = state->find(node->id))
                effective = value->localPosition[static_cast<int>(address->component)];
        } else {
            const auto value = content.authors->nodes.find(node->id);
            if (value != content.authors->nodes.end() && value->second.sun)
                effective = value->second.sun->irradiance;
        }
    }
    const QSignalBlocker blocker(propertyValue_);
    if (effective) {
        propertyValueOriginal_ = *effective;
        propertyValue_->setText(propertyValue_->locale().toString(*effective, 'g', 17));
    } else {
        propertyRecord_->setEnabled(false);
    }
    const auto current = mode == AnimationMode::Base ? QStringLiteral("基础模式显示作者值")
        : effective ? QStringLiteral("已安装求值 %1（帧 %2，求值 ID %3）")
            .arg(*effective, 0, 'g', 17).arg(state->source.frameTime, 0, 'g', 17)
            .arg(pose->identity.evaluationId) : QStringLiteral("当前求值不可用");
    propertyStatus_->setText(QStringLiteral("基础作者值 %1；%2。录键值独立输入，不回写基础。")
        .arg(author, 0, 'g', 17).arg(current));
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
    const auto& rationalFps = model_.scene()->animation().properties.rationalFps;
    fps_->setEnabled(editable && (!rationalFps || rationalFps->denominator == 1));
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
    refreshPropertyValues();
}
void AnimationTimeline::refreshDefinitions() {
    const auto& animation = model_.scene()->animation();
    const auto id = model_.selection()->selectedEntity();
    const auto* node = model_.scene()->find(id);
    object_->setText(node ? QStringLiteral("对象：%1").arg(QString::fromStdString(node->name))
                          : QStringLiteral("未选中对象"));
    const QSignalBlocker fpsBlock(fps_), startBlock(start_), endBlock(end_), sliderBlock(slider_), pageBlock(page_);
    const auto fps = animation.properties.rationalFps.value_or(core::RationalFps{animation.settings.fps, 1});
    fps_->setValue(fps.denominator == 1 ? fps.numerator
        : static_cast<int>(std::lround(static_cast<double>(fps.numerator) / fps.denominator)));
    fps_->setToolTip(fps.denominator == 1 ? QStringLiteral("整数帧率")
        : QStringLiteral("当前有理帧率 %1/%2；范围编辑保持该精确值。")
            .arg(fps.numerator).arg(fps.denominator));
    fpsLabel_->setText(QStringLiteral("有效 %1/%2 fps").arg(fps.numerator).arg(fps.denominator));
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
    refreshPropertyKeys();
    refreshSession();
}
} // namespace mini3d::editor
