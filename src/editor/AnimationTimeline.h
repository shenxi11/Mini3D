/*
 * 模块名: AnimationTimeline
 * 功能概述: 中文动画控制、分页键表与分组姿态草稿视图，业务提交统一交给ViewModel。
 * 对外接口: AnimationTimeline。
 * 依赖关系: Qt Widgets、SceneViewModel、CommitSpinBox。
 * 输入输出: 用户意图到动画会话命令；正式定义及草稿到只读/受控字段。
 * 异常与错误: ViewModel拒绝时保留正式数据并刷新控件；不维护第二历史。
 * 维护说明: tick只刷新会话字段，不重建键表；普通属性仍显示基础TRS。
 */
#pragma once

#include "core/Animation.h"

#include <QWidget>
#include <array>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
namespace mini3d::editor {
class CommitSpinBox;
class SceneViewModel;
/** @brief 时间轴仅装配状态/命令，不写真源变换，也不自行播放或保存。 */
class AnimationTimeline final : public QWidget {
    Q_OBJECT
  public:
    explicit AnimationTimeline(SceneViewModel& model, QWidget* parent = nullptr);

  private:
    void refreshSession();
    void refreshDefinitions();
    [[nodiscard]] std::array<bool, 3> channelMask() const;
    struct KeySelection {
        core::EntityId entity;
        core::AnimationChannel channel;
        std::uint32_t frame;
    };
    [[nodiscard]] std::optional<KeySelection> selectedKey() const;
    SceneViewModel& model_;
    QCheckBox *preview_ = nullptr, *loop_ = nullptr;
    std::array<QCheckBox*, 3> channels_{};
    QPushButton *play_ = nullptr, *record_ = nullptr, *beginDraft_ = nullptr,
                *confirmDraft_ = nullptr, *cancelDraft_ = nullptr;
    CommitSpinBox* frame_ = nullptr;
    QSlider* slider_ = nullptr;
    QSpinBox *fps_ = nullptr, *start_ = nullptr, *end_ = nullptr, *moveTo_ = nullptr,
             *page_ = nullptr;
    QPushButton *settings_ = nullptr, *move_ = nullptr, *delete_ = nullptr,
                *interpolationButton_ = nullptr;
    QComboBox* interpolation_ = nullptr;
    QLabel *status_ = nullptr, *object_ = nullptr;
    QTableWidget* keys_ = nullptr;
    QWidget* draftPanel_ = nullptr;
    std::array<std::array<CommitSpinBox*, 3>, 3> draftFields_{};
};
} // namespace mini3d::editor
