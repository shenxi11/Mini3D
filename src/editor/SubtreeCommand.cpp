/*
 * 模块名: SubtreeCommand
 * 功能概述: GUI/API共同回放已准备子树与动画，保持固定实体标识和历史选择策略。
 * 对外接口: SubtreeCommand
 * 依赖关系: SceneViewModel、Scene
 * 输入输出: undo/redo 到节点/成员/轨道安装和延迟通知。
 * 异常与错误: 来源检查失败断言报告历史契约违反。
 * 维护说明: 分配与映射准备在push前完成；回放不重新生成身份或求值候选。
 */
#include "SubtreeCommand.h"

#include "SceneViewModel.h"

#include <algorithm>

namespace mini3d::editor {
namespace {
QString commandLabel(SubtreeCommand::Kind kind) {
    return kind == SubtreeCommand::Kind::Duplicate ? QStringLiteral("复制")
           : kind == SubtreeCommand::Kind::Delete  ? QStringLiteral("删除")
                                                   : QStringLiteral("创建 / 导入");
}
} // namespace
SubtreeCommand::SubtreeCommand(SceneViewModel& model, core::EntityId id, Kind kind,
                               core::EntityId previousSelection)
    : QUndoCommand(commandLabel(kind)), model_(model), original_(id), kind_(kind),
      previousSelection_(previousSelection) {
    // Created已存在于Scene；首次redo只登记创建，后续使用同一准备对象恢复原ID。
    Q_ASSERT(kind == Kind::Created);
    std::string error;
    auto candidate = model_.scene_->prepareRemoveSubtree(id, error);
    Q_ASSERT(candidate);
    prepared_ = std::make_shared<core::Scene::PreparedSubtree>(std::move(*candidate));
    retainedGeometry_ = model_.captureSubtreeGeometry(*prepared_);
    entityIds_ = prepared_->entityIds();
    std::sort(entityIds_.begin(), entityIds_.end());
}
SubtreeCommand::SubtreeCommand(SceneViewModel& model,
                               std::shared_ptr<core::Scene::PreparedSubtree> prepared, Kind kind,
                               core::EntityId original, bool selectResult,
                               core::EntityId previousSelection)
    : QUndoCommand(commandLabel(kind)), model_(model), original_(original), kind_(kind),
      previousSelection_(previousSelection), selectResult_(selectResult),
      prepared_(std::move(prepared)), entityIds_(prepared_->entityIds()) {
    Q_ASSERT(kind != Kind::Created);
    retainedGeometry_ = model_.captureSubtreeGeometry(*prepared_);
    std::sort(entityIds_.begin(), entityIds_.end());
}
void SubtreeCommand::undo() {
    apply(false);
}
bool SubtreeCommand::supportsAnimationReplay() const {
    return true;
}
std::optional<PreparedAnimationReplay>
SubtreeCommand::prepareReplay(bool forward, QString& error) const {
    const auto prepared = prepared_;
    const auto geometry = retainedGeometry_;
    const auto selection = replaySelection(forward);
    return model_.prepareSubtreeReplay(*prepared, forward == (kind_ != Kind::Delete),
                                       geometry, error, selection);
}
void SubtreeCommand::redo() {
    if (kind_ == Kind::Created && firstRedo_) {
        firstRedo_ = false;
        return;
    }
    apply(true);
}
std::optional<core::EntityId> SubtreeCommand::replaySelection(bool forward) const {
    const bool present = forward == (kind_ != Kind::Delete);
    std::optional<core::EntityId> selection;
    if (selectResult_) {
        selection = forward ? (kind_ == Kind::Delete ? core::kInvalidEntity : prepared_->rootId())
                            : (kind_ == Kind::Created ? previousSelection_ : original_);
    } else if (!present && std::binary_search(entityIds_.begin(), entityIds_.end(),
                                              model_.selection_.selectedEntity())) {
        selection = core::kInvalidEntity;
    } else if (kind_ == Kind::Delete && !forward &&
               std::binary_search(entityIds_.begin(), entityIds_.end(), previousSelection_)) {
        selection = previousSelection_;
    }
    return selection;
}
void SubtreeCommand::apply(bool forward) {
    const auto selection = replaySelection(forward);
    emit model_.structureAboutToChange();
    const bool present = forward == (kind_ != Kind::Delete);
    const bool changed = present ? model_.scene_->installPreparedSubtree(*prepared_)
                                 : model_.scene_->removePreparedSubtree(*prepared_);
    Q_ASSERT(changed);
    model_.queueHistoryNotifications(true, selection);
}
} // namespace mini3d::editor
