/*
 * 模块名: AnimationEditing
 * 功能概述: 为登记标量属性准备单笔私有动画候选，提交只经过既有正式历史出口。
 * 对外接口: AnimationEditing、LinkedActionEdit、collectAnimationEditTimes。
 * 依赖关系: SceneViewModel、Core 动画定义、Qt QString；不持有 UI 或 GPU 资源。
 * 输入输出: 明确属性地址与 binary64 时间到准备结果或一条共享历史。
 * 异常与错误: 非法地址、共享编辑、过期来源或提交拒绝通过 false/error 返回。
 * 维护说明: 不累计批次；取消只释放候选，时间收集沿用既有节点访问预算。
 */
#pragma once

#include "api/ApiTypes.h"

#include <QString>
#include <memory>
#include <set>

namespace mini3d::editor {
class SceneViewModel;
/** @brief 共享作者/读取的登记地址校验；写入口仅开放 J1 三类属性，不解释文本诊断。 */
[[nodiscard]] std::optional<api::ApiError> validateAnimationPropertyAddress(
    const core::Scene& scene, const core::PropertyAddress& address,
    const api::DocumentState& document, bool forWrite);
/** @brief 按 legacy XYZ 展开与 typed 总量共用冻结预算；不从 Core 文本诊断推断错误码。 */
[[nodiscard]] std::optional<api::ApiError> validateAnimationScalarBudget(
    const core::SceneAnimation& animation, const api::DocumentState& document);

/** @brief 默认拒绝共享曲线的实际变更；精确 no_change 无需 EditLinked，不自动拆分。 */
using LinkedActionEdit = api::LinkedPropertyEdit;

/** @brief 单笔命令服务；model 必须比服务存活更久，不发布预览或另建历史栈。 */
class AnimationEditing final {
  public:
    explicit AnimationEditing(SceneViewModel& model);
    ~AnimationEditing();
    /** @brief 从当前正式作者准备单笔插键/精确替换，不修改 model。
     * 成功替换原私有候选；失败保留原候选，调用者须检查返回值，不能误提交失败意图。
     * 仅支持 Position XYZ、roughness Scalar、Sun irradiance Scalar（W/m²）及 Constant/Linear。
     * 精确同值/同插值先判 no_change，不预留 ID，也不要求共享写许可。
     */
    bool prepareKey(const core::PropertyAddress& address, core::FrameTime time, double value,
                    core::PropertyInterpolation outgoing, QString& error,
                    LinkedActionEdit linked = LinkedActionEdit::RejectShared);
    /** @brief 从当前正式作者准备精确时间删除；未找到键是 no_change。
     * 成功替换原候选，失败保留原候选；共享曲线最后一键不能删除，避免改变他人绑定。
     */
    bool prepareKeyRemoval(const core::PropertyAddress& address, core::FrameTime time,
                           QString& error,
                           LinkedActionEdit linked = LinkedActionEdit::RejectShared);
    /** @brief 结构化准备结果仅报告差量，不提前暴露未发行身份；失败保留旧候选。 */
    api::ApiResult<bool> prepareKeyResult(const core::PropertyAddress& address,
        core::FrameTime time, double value, core::PropertyInterpolation outgoing,
        LinkedActionEdit linked = LinkedActionEdit::RejectShared);
    api::ApiResult<bool> prepareKeyRemovalResult(const core::PropertyAddress& address,
        core::FrameTime time, LinkedActionEdit linked = LinkedActionEdit::RejectShared);
    /** @brief 委托结构化正式提交；成功/no_change 释放候选。
     * 失败保持正式内容、历史及私有候选；精确 no_change 也经过既有提交守卫。
     */
    bool commit(const QString& label, QString& error);
    /** @brief 来源复核后复用正式提交链；成功/no_change 才返回身份并释放候选。 */
    api::ApiResult<api::AnimationPropertyKeyResult>
    commitResult(const QString& label, const api::MutationRequest* request = nullptr);
    /** @brief 只释放候选，不修改来源、ID 水位、预览或撤销历史。 */
    void cancel();
    /** @brief 返回私有候选是否与正式准备来源不同；无候选返回 false。 */
    [[nodiscard]] bool hasChanges() const;

  private:
    api::ApiResult<bool> prepareEdit(const core::PropertyAddress& address, core::FrameTime time,
        double value, core::PropertyInterpolation outgoing, bool remove, LinkedActionEdit linked);
    struct State;
    SceneViewModel& model_;
    std::unique_ptr<State> state_;
};

/** @brief 收集当前时间、legacy 原有差量时刻及解析 typed 地址的改键/邻接/删除时刻。
 * before/after 必须是已验证作者定义；不收集未变曲线全部键，不解析未绑定曲线。
 * 返回去重集合；首个预算超限时停止，调用者仍用既有 validateAnimationPreflightBudget 拒绝。
 */
[[nodiscard]] std::set<core::FrameTime>
collectAnimationEditTimes(const core::SceneAnimation& before, const core::SceneAnimation& after,
                          core::FrameTime currentFrame, std::size_t nodeCount);
} // namespace mini3d::editor
