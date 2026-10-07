/*
 * 模块名: AnimationReplay
 * 功能概述: 为唯一历史提供显式、只读的当前时间姿态准备接口。
 * 对外接口: PreparedAnimationReplay、AnimationReplay
 * 依赖关系: Core数值输入、Renderer不可变姿态、Qt字符串。
 * 输入输出: 回放方向到已分配的完整包装及待安装输入；不写正式内容。
 * 异常与错误: 不支持或准备失败由调用者退出预览，历史仍正常恢复。
 * 维护说明: 不根据命令名称或lambda推断影响；准备先于任何历史通知。
 */
#pragma once

#include "renderer_gl/InstalledPose.h"

#include <QString>
#include <optional>

namespace mini3d::editor {
/** @brief 发布只移动输入及共享包装；身份版本在正式提交后绑定。 */
struct PreparedAnimationReplay {
    std::shared_ptr<renderer_gl::InstalledPose> pose;
    std::vector<core::AnimationPoseInput> inputs;
    bool replacesInputs = false;
    renderer_gl::AnimationMode sourceMode = renderer_gl::AnimationMode::Base;
    std::uint64_t sourceSessionRevision = 0;
    core::FrameTime sourceFrame = 1;
    /** @brief 仅新建且未发布的独占候选；安装后绑定标量revision，再丢弃可写别名。 */
    std::shared_ptr<renderer_gl::PoseGeometry> geometryToBind;
};

/** @brief 命令明确提供候选，准备过程不能执行undo/redo或通知消费者。 */
class AnimationReplay {
  public:
    virtual ~AnimationReplay() = default;
    [[nodiscard]] virtual bool supportsAnimationReplay() const = 0;
    [[nodiscard]] virtual std::optional<PreparedAnimationReplay>
    prepareReplay(bool forward, QString& error) const = 0;
};
} // namespace mini3d::editor
