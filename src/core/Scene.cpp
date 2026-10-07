/*
 * 模块名: Scene
 * 功能概述: 第三周场景数据与变换支持。
 * 对外接口: Scene
 * 依赖关系: C++ 标准库、GLM
 * 输入输出: 输入场景操作，输出节点状态或验证结果。
 * 异常与错误: 非法操作拒绝且保留既有状态；分配失败由运行时报告。
 * 维护说明: 不依赖 Qt/OpenGL，关联关系使用稳定 ID。
 */
#include "Scene.h"

#include "modeling/VertexTransform.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>
#include <unordered_set>
#include <utility>
namespace mini3d::core {
namespace {
class BatchBudget {
  public:
    explicit BatchBudget(std::size_t maximum) : maximum_(maximum) {}
    bool add(std::size_t count, std::size_t bytes) {
        if (count > (maximum_ - used_) / bytes)
            return false;
        used_ += count * bytes;
        return true;
    }
    [[nodiscard]] std::size_t used() const {
        return used_;
    }

  private:
    std::size_t maximum_;
    std::size_t used_ = 0;
};
bool sameBatchTransform(const Transform& left, const Transform& right) {
    return left.position == right.position && left.rotation == right.rotation &&
           left.scale == right.scale;
}
bool sameBatchRenderer(const std::optional<MeshRendererComponent>& left,
                       const std::optional<MeshRendererComponent>& right) {
    return left.has_value() == right.has_value() &&
           (!left || (left->mesh == right->mesh && left->material == right->material));
}
bool sameBatchNode(const SceneNode& left, const SceneNode& right) {
    return left.id == right.id && left.name == right.name && left.parent == right.parent &&
           left.children == right.children && sameBatchTransform(left.transform, right.transform) &&
           left.visible == right.visible && left.primitive == right.primitive &&
           sameBatchRenderer(left.meshRenderer, right.meshRenderer) && left.surface == right.surface &&
           left.camera == right.camera && left.light == right.light &&
           left.editableMesh == right.editableMesh;
}
bool finiteBatchMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row]))
                return false;
        }
    }
    return true;
}
std::shared_ptr<const EditableMeshContent>
prepareContent(modeling::EditableMesh source, const std::optional<modeling::MirrorOptions>& mirror,
               const std::optional<modeling::SubdivisionOptions>& subdivision, std::string& error,
               std::optional<modeling::DerivedMesh> derived = {}) {
    if (subdivision && !subdivision->isValid()) {
        error = "细分级数仅支持 1 或 2。";
        return {};
    }
    if (!derived) {
        auto result = modeling::deriveMesh(source);
        if (!result.derived) {
            error = result.error;
            return {};
        }
        derived = std::move(result.derived);
    }
    EditableMeshContent content{
        std::move(source), std::move(*derived), mirror, {}, subdivision, {}};
    if (mirror) {
        auto evaluated = modeling::evaluateMirror(content.source, *mirror);
        if (!evaluated.evaluation) {
            error = evaluated.error;
            return {};
        }
        content.mirrorEvaluation = std::move(*evaluated.evaluation);
    }
    if (subdivision && subdivision->enabled) {
        auto evaluated =
            modeling::evaluateSubdivision(content.evaluatedMesh(), subdivision->levels);
        if (!evaluated.evaluation) {
            error = evaluated.error;
            return {};
        }
        content.subdivisionEvaluation = std::move(*evaluated.evaluation);
    }
    error.clear();
    return std::make_shared<const EditableMeshContent>(std::move(content));
}
} // namespace
bool Scene::animationBindingsExist(const SceneAnimation& animation) const {
    return std::all_of(animation.tracks.begin(), animation.tracks.end(), [this](const auto& item) {
        return find(item.first.first) != nullptr;
    });
}
std::optional<Scene::PreparedAnimation> Scene::prepareAnimation(const SceneAnimation& animation,
                                                                std::string& error) const {
    error.clear();
    std::vector<EntityId> entities;
    entities.reserve(entities_.size());
    for (const auto& [id, node] : entities_)
        entities.push_back(id);
    if (!validateSceneAnimation(animation, entities).isValid()) {
        error = "动画设置、实体绑定、关键帧或最终缩放邻接无效，或超过动画预算。";
        return std::nullopt;
    }
    PreparedAnimation prepared;
    prepared.origin_ = this;
    prepared.originToken_ = geometrySnapshotOrigin_;
    prepared.before_ = animation_;
    prepared.after_ =
        animation == *animation_ ? animation_ : std::make_shared<const SceneAnimation>(animation);
    return prepared;
}
bool Scene::canInstallPreparedAnimation(const PreparedAnimation& prepared) const {
    return prepared.origin_ == this && prepared.originToken_ == geometrySnapshotOrigin_ &&
           prepared.before_ && prepared.after_ && !prepared.installed_ &&
           animation_ == prepared.before_ && animationBindingsExist(*prepared.after_);
}
bool Scene::canRestorePreparedAnimation(const PreparedAnimation& prepared) const {
    return prepared.origin_ == this && prepared.originToken_ == geometrySnapshotOrigin_ &&
           prepared.before_ && prepared.after_ && prepared.installed_ &&
           animation_ == prepared.after_ && animationBindingsExist(*prepared.before_);
}
bool Scene::installPreparedAnimation(PreparedAnimation& prepared) {
    if (!canInstallPreparedAnimation(prepared))
        return false;
    animation_ = prepared.after_;
    prepared.installed_ = true;
    return true;
}
bool Scene::restorePreparedAnimation(PreparedAnimation& prepared) {
    if (!canRestorePreparedAnimation(prepared))
        return false;
    animation_ = prepared.before_;
    prepared.installed_ = false;
    return true;
}
std::optional<std::vector<AnimationPoseInput>>
Scene::animationPoseInputs(std::string& error) const {
    error.clear();
    if (entities_.size() > kAnimationMaximumPoseNodes) {
        error = "动画姿态超过10000节点预算。";
        return std::nullopt;
    }
    const auto rootIds = roots();
    std::vector<EntityId> pending(rootIds.rbegin(), rootIds.rend());
    std::vector<AnimationPoseInput> result;
    result.reserve(entities_.size());
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto& node = entities_.at(id);
        result.push_back({id, node.parent, node.transform});
        pending.insert(pending.end(), node.children.rbegin(), node.children.rend());
    }
    return result;
}
Scene::BatchNodeState Scene::batchNodeState(const SceneNode& node) const {
    BatchNodeState state;
    state.entity = node.id;
    state.parent = node.parent;
    state.before = node.transform;
    state.after = node.transform;
    state.primitive = node.primitive;
    state.renderer = node.meshRenderer;
    state.camera = node.camera;
    state.light = node.light;
    state.mesh = node.editableMesh;
    if (const auto* record = editableMesh(state.mesh))
        state.content = record->content;
    return state;
}
std::optional<std::vector<AnimationPoseInput>>
Scene::animationPoseInputs(const PreparedTransformBatch& prepared, bool forward,
                          std::string& error) const {
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_) {
        error = "变换历史候选不属于当前场景。";
        return std::nullopt;
    }
    auto inputs = animationPoseInputs(error);
    if (!inputs)
        return std::nullopt;
    for (auto& input : *inputs) {
        const auto found = prepared.sources_.find(input.entity);
        if (found != prepared.sources_.end() && found->second.target)
            input.base = forward ? found->second.after : found->second.before;
    }
    return inputs;
}
bool Scene::matchesBatchNodeState(const BatchNodeState& state, bool after) const {
    const auto* node = find(state.entity);
    if (!node || node->parent != state.parent ||
        !sameBatchTransform(node->transform, after ? state.after : state.before) ||
        (state.capturesChildren && node->children != state.children) ||
        node->primitive != state.primitive || !sameBatchRenderer(node->meshRenderer, state.renderer) ||
        node->camera != state.camera || node->light != state.light || node->editableMesh != state.mesh) {
        return false;
    }
    const auto* record = editableMesh(state.mesh);
    return state.mesh == 0 || (record && record->content == state.content);
}
std::optional<Scene::PreparedEntityBatch>
Scene::prepareEntityBatch(const std::vector<EntityCreateOptions>& options, std::string& error,
                          std::size_t maximumItems, std::size_t maximumCandidateBytes,
                          BatchPrepareFailure* failure) {
    error.clear();
    const auto reject = [&](BatchPrepareFailure reason, const char* message)
        -> std::optional<PreparedEntityBatch> {
        if (failure)
            *failure = reason;
        error = message;
        return std::nullopt;
    };
    if (options.empty())
        return reject(BatchPrepareFailure::InvalidArgument, "创建批次不能为空。");
    if (options.size() > maximumItems ||
        options.size() > std::numeric_limits<EntityId>::max() - nextId_) {
        return reject(BatchPrepareFailure::LimitExceeded, "创建批次数量或对象编号超过上限。");
    }
    for (const auto& input : options) {
        if (input.name.empty() || !input.transform.isValid() || !input.surface.isValid() ||
            input.camera || input.light ||
            (input.primitive != PrimitiveKind::Empty && input.primitive != PrimitiveKind::Cube &&
             input.primitive != PrimitiveKind::Sphere && input.primitive != PrimitiveKind::Plane)) {
            return reject(BatchPrepareFailure::InvalidArgument, "基础对象批次参数无效。");
        }
        if (input.parent != 0 && !find(input.parent))
            return reject(BatchPrepareFailure::NotFound, "创建批次的已有父对象不存在。");
    }
    PreparedEntityBatch result;
    result.origin_ = this;
    result.originToken_ = geometrySnapshotOrigin_;
    BatchBudget budget(maximumCandidateBytes);
    // 估算同时保留的节点、快照、表节点和桶；每次缓冲增长前先占用预算。
    if (!budget.add(1, sizeof(result)) ||
        !budget.add(options.size(), sizeof(EntityId) + sizeof(SceneNode) +
                                        sizeof(decltype(result.nodes_)::value_type) +
                                        sizeof(decltype(result.worlds_)::value_type) +
                                        sizeof(std::pair<const EntityId, SceneNode>) +
                                        5 * sizeof(void*))) {
        return reject(BatchPrepareFailure::LimitExceeded, "创建批次候选内存超过上限。");
    }
    result.entities_.reserve(options.size());
    result.expected_.reserve(options.size());
    result.nodes_.reserve(options.size());
    result.worlds_.reserve(options.size());
    std::unordered_map<EntityId, SceneNode> staging;
    staging.reserve(options.size());
    for (std::size_t index = 0; index < options.size(); ++index) {
        const auto& input = options[index];
        if (!budget.add(2, std::max<std::size_t>(32, input.name.size() + 1)))
            return reject(BatchPrepareFailure::LimitExceeded, "创建批次名称内存超过上限。");
        SceneNode node;
        node.id = nextId_ + static_cast<EntityId>(index);
        node.name = input.name;
        node.parent = input.parent;
        node.primitive = input.primitive;
        node.transform = input.transform;
        node.transform.rotation = glm::normalize(node.transform.rotation);
        node.surface = input.surface;
        node.visible = input.visible;
        auto world = node.transform.localMatrix() * glm::mat4(1.0F);
        for (const auto* ancestor = find(node.parent); ancestor;
             ancestor = find(ancestor->parent)) {
            if (!result.sources_.contains(ancestor->id)) {
                if (!budget.add(1, sizeof(decltype(result.sources_)::value_type) + 3 * sizeof(void*)))
                    return reject(BatchPrepareFailure::LimitExceeded, "创建批次父链内存超过上限。");
                result.sources_.emplace(ancestor->id, batchNodeState(*ancestor));
            }
            world = ancestor->transform.localMatrix() * world;
        }
        if (!finiteBatchMatrix(world))
            return reject(BatchPrepareFailure::UnsupportedTransform, "创建批次的世界变换不是有限值。");
        result.worlds_.emplace_back(node.id, world);
        result.entities_.push_back(node.id);
        result.expected_.push_back(node);
        staging.emplace(node.id, std::move(node));
        if (input.parent != 0) {
            if (!result.parents_.contains(input.parent)) {
                const auto& children = entities_.at(input.parent).children;
                const auto additions = static_cast<std::size_t>(std::count_if(
                    options.begin(), options.end(), [&](const auto& item) {
                        return item.parent == input.parent;
                    }));
                if (!budget.add(1, sizeof(decltype(result.parents_)::value_type) + 3 * sizeof(void*)) ||
                    !budget.add(children.size(), 2 * sizeof(EntityId)) ||
                    !budget.add(additions, 2 * sizeof(EntityId))) {
                    return reject(BatchPrepareFailure::LimitExceeded, "创建批次父 children 内存超过上限。");
                }
                auto& parent = result.parents_[input.parent];
                parent.before = children;
                parent.buffer.reserve(children.size() + additions);
                parent.buffer.insert(parent.buffer.end(), children.begin(), children.end());
                parent.appended.reserve(additions);
            }
            auto& parent = result.parents_.at(input.parent);
            parent.appended.push_back(result.entities_.back());
            parent.buffer.push_back(result.entities_.back());
        }
    }
    for (const auto id : result.entities_)
        result.nodes_.push_back(staging.extract(id));
    const auto total = entities_.size() + result.nodes_.size();
    if (static_cast<double>(total) >
        static_cast<double>(entities_.bucket_count()) * entities_.max_load_factor()) {
        if (!budget.add(total, 2 * sizeof(void*)) || !budget.add(1, sizeof(void*)))
            return reject(BatchPrepareFailure::LimitExceeded, "创建批次安装表容量内存超过上限。");
        entities_.reserve(total);
    }
    result.estimatedBytes_ = budget.used();
    // map 移入返回候选仍可能分配；仅在返回值完整构造后提交身份高水位。
    struct IdentityCommit {
        EntityId& next;
        EntityId value;
        int exceptions;
        ~IdentityCommit() {
            if (std::uncaught_exceptions() == exceptions)
                next = value;
        }
    } commit{nextId_, nextId_ + static_cast<EntityId>(options.size()), std::uncaught_exceptions()};
    return result;
}
bool Scene::canInstallPreparedEntityBatch(const PreparedEntityBatch& prepared) const {
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_ ||
        prepared.installed_ || prepared.entities_.empty() ||
        prepared.entities_.size() != prepared.nodes_.size() ||
        prepared.expected_.size() != prepared.nodes_.size() ||
        static_cast<double>(entities_.size() + prepared.nodes_.size()) >
            static_cast<double>(entities_.bucket_count()) * entities_.max_load_factor()) {
        return false;
    }
    for (std::size_t index = 0; index < prepared.nodes_.size(); ++index) {
        if (prepared.nodes_[index].empty() ||
            prepared.nodes_[index].key() != prepared.entities_[index] || find(prepared.entities_[index]))
            return false;
    }
    for (const auto& [id, source] : prepared.sources_) {
        if (!matchesBatchNodeState(source, false))
            return false;
    }
    for (const auto& [id, parent] : prepared.parents_) {
        const auto* node = find(id);
        if (!node || node->children != parent.before ||
            parent.buffer.size() != parent.before.size() + parent.appended.size() ||
            !std::equal(parent.before.begin(), parent.before.end(), parent.buffer.begin()) ||
            !std::equal(parent.appended.begin(), parent.appended.end(),
                        parent.buffer.begin() + static_cast<std::ptrdiff_t>(parent.before.size()))) {
            return false;
        }
    }
    return true;
}
bool Scene::installPreparedEntityBatch(PreparedEntityBatch& prepared) {
    if (!canInstallPreparedEntityBatch(prepared))
        return false;
    // 整组检查已经结束；节点句柄插入不再扩桶，父 children 只交换已分配缓冲。
    for (auto& node : prepared.nodes_)
        entities_.insert(std::move(node));
    for (auto& [id, parent] : prepared.parents_)
        entities_.at(id).children.swap(parent.buffer);
    prepared.installed_ = true;
    return true;
}
bool Scene::removePreparedEntityBatch(PreparedEntityBatch& prepared) {
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_ ||
        !prepared.installed_ || prepared.entities_.empty()) {
        return false;
    }
    for (const auto& [id, source] : prepared.sources_) {
        if (!matchesBatchNodeState(source, false))
            return false;
    }
    for (std::size_t index = 0; index < prepared.entities_.size(); ++index) {
        const auto* node = find(prepared.entities_[index]);
        if (!prepared.nodes_[index].empty() || !node || !sameBatchNode(*node, prepared.expected_[index]))
            return false;
        const auto track = animation_->tracks.lower_bound({node->id, AnimationChannel::Position});
        if (track != animation_->tracks.end() && track->first.first == node->id)
            return false;
        for (const auto& collection : collections_) {
            if (collection.members.contains(node->id))
                return false;
        }
    }
    for (const auto& [id, parent] : prepared.parents_) {
        const auto* node = find(id);
        if (!node || parent.buffer != parent.before ||
            node->children.size() != parent.before.size() + parent.appended.size() ||
            !std::equal(parent.before.begin(), parent.before.end(), node->children.begin()) ||
            !std::equal(parent.appended.begin(), parent.appended.end(),
                        node->children.begin() + static_cast<std::ptrdiff_t>(parent.before.size()))) {
            return false;
        }
    }
    for (auto& [id, parent] : prepared.parents_)
        entities_.at(id).children.swap(parent.buffer);
    for (std::size_t index = 0; index < prepared.entities_.size(); ++index)
        prepared.nodes_[index] = entities_.extract(prepared.entities_[index]);
    prepared.installed_ = false;
    return true;
}
std::optional<Scene::PreparedTransformBatch>
Scene::prepareTransformBatch(const std::vector<TransformBatchItem>& options, std::string& error,
                             std::size_t maximumItems, std::size_t maximumCandidateBytes,
                             BatchPrepareFailure* failure) const {
    error.clear();
    const auto reject = [&](BatchPrepareFailure reason, const char* message)
        -> std::optional<PreparedTransformBatch> {
        if (failure)
            *failure = reason;
        error = message;
        return std::nullopt;
    };
    if (options.empty())
        return reject(BatchPrepareFailure::InvalidArgument, "变换批次不能为空。");
    if (options.size() > maximumItems)
        return reject(BatchPrepareFailure::LimitExceeded, "变换批次数量超过上限。");
    for (std::size_t index = 0; index < options.size(); ++index) {
        if (!options[index].transform.isValid() ||
            std::any_of(options.begin(), options.begin() + static_cast<std::ptrdiff_t>(index),
                        [&](const auto& item) { return item.entity == options[index].entity; })) {
            return reject(BatchPrepareFailure::InvalidArgument, "变换批次包含非法 TRS 或重复目标。");
        }
        if (!find(options[index].entity))
            return reject(BatchPrepareFailure::NotFound, "变换批次目标不存在。");
    }
    PreparedTransformBatch result;
    result.origin_ = this;
    result.originToken_ = geometrySnapshotOrigin_;
    BatchBudget budget(maximumCandidateBytes);
    if (!budget.add(1, sizeof(result)) || !budget.add(options.size(), 2 * sizeof(EntityId)))
        return reject(BatchPrepareFailure::LimitExceeded, "变换批次候选内存超过上限。");
    result.entities_.reserve(options.size());
    std::vector<EntityId> pending;
    pending.reserve(options.size());
    for (const auto& input : options) {
        if (!budget.add(1, sizeof(decltype(result.sources_)::value_type) + 3 * sizeof(void*)))
            return reject(BatchPrepareFailure::LimitExceeded, "变换批次来源内存超过上限。");
        auto state = batchNodeState(*find(input.entity));
        state.target = true;
        state.after = input.transform;
        // 未变的已确认旋转保持原位模式；新旋转只在候选阶段归一化一次。
        if (state.after.rotation != state.before.rotation)
            state.after.rotation = glm::normalize(state.after.rotation);
        result.hasChanges_ |= !sameBatchTransform(state.before, state.after);
        result.sources_.emplace(input.entity, std::move(state));
        result.entities_.push_back(input.entity);
        pending.push_back(input.entity);
    }
    // 合并目标子树；每个受影响节点只捕获一次，结构变化会使整组候选失效。
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto& node = entities_.at(id);
        if (!result.sources_.contains(id)) {
            if (!budget.add(1, sizeof(decltype(result.sources_)::value_type) + 3 * sizeof(void*)))
                return reject(BatchPrepareFailure::LimitExceeded, "变换批次后代来源内存超过上限。");
            result.sources_.emplace(id, batchNodeState(node));
        }
        auto& state = result.sources_.at(id);
        if (state.capturesChildren)
            continue;
        if (!budget.add(node.children.size(), sizeof(EntityId)))
            return reject(BatchPrepareFailure::LimitExceeded, "变换批次后代结构内存超过上限。");
        state.children = node.children;
        state.capturesChildren = true;
        const auto required = pending.size() + node.children.size();
        if (required > pending.capacity()) {
            if (!budget.add(required - pending.capacity(), sizeof(EntityId)))
                return reject(BatchPrepareFailure::LimitExceeded, "变换批次遍历缓冲超过上限。");
            pending.reserve(required);
        }
        pending.insert(pending.end(), node.children.begin(), node.children.end());
    }
    for (const auto& input : options) {
        for (const auto* ancestor = find(find(input.entity)->parent); ancestor;
             ancestor = find(ancestor->parent)) {
            if (result.sources_.contains(ancestor->id))
                continue;
            if (!budget.add(1, sizeof(decltype(result.sources_)::value_type) + 3 * sizeof(void*)))
                return reject(BatchPrepareFailure::LimitExceeded, "变换批次外部父链内存超过上限。");
            result.sources_.emplace(ancestor->id, batchNodeState(*ancestor));
        }
    }
    const auto affected = static_cast<std::size_t>(std::count_if(
        result.sources_.begin(), result.sources_.end(),
        [](const auto& item) { return item.second.capturesChildren; }));
    if (!budget.add(affected, sizeof(decltype(result.worlds_)::value_type)))
        return reject(BatchPrepareFailure::LimitExceeded, "变换批次世界矩阵内存超过上限。");
    result.worlds_.reserve(affected);
    for (const auto& [id, state] : result.sources_) {
        if (!state.capturesChildren)
            continue;
        glm::mat4 world(1.0F);
        for (const auto* node = find(id); node; node = find(node->parent))
            world = result.sources_.at(node->id).after.localMatrix() * world;
        if (!finiteBatchMatrix(world))
            return reject(BatchPrepareFailure::UnsupportedTransform, "变换批次最终后代世界变换不是有限值。");
        result.worlds_.emplace_back(id, world);
    }
    result.estimatedBytes_ = budget.used();
    return result;
}
bool Scene::canInstallPreparedTransformBatch(const PreparedTransformBatch& prepared) const {
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_ ||
        prepared.installed_ || prepared.entities_.empty()) {
        return false;
    }
    return std::all_of(prepared.sources_.begin(), prepared.sources_.end(), [&](const auto& item) {
        return matchesBatchNodeState(item.second, false);
    });
}
bool Scene::installPreparedTransformBatch(PreparedTransformBatch& prepared) {
    if (!canInstallPreparedTransformBatch(prepared))
        return false;
    for (const auto id : prepared.entities_)
        entities_.at(id).transform = prepared.sources_.at(id).after;
    prepared.installed_ = true;
    return true;
}
bool Scene::restorePreparedTransformBatch(PreparedTransformBatch& prepared) {
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_ ||
        !prepared.installed_ || prepared.entities_.empty() ||
        !std::all_of(prepared.sources_.begin(), prepared.sources_.end(), [&](const auto& item) {
            return matchesBatchNodeState(item.second, true);
        })) {
        return false;
    }
    for (const auto id : prepared.entities_)
        entities_.at(id).transform = prepared.sources_.at(id).before;
    prepared.installed_ = false;
    return true;
}
std::optional<Scene::PreparedEntity> Scene::prepareEntity(const EntityCreateOptions& options,
                                                          std::string& error,
                                                          const modeling::EditableMesh* source) {
    error.clear();
    if (options.name.empty() || !options.transform.isValid() || !options.surface.isValid() ||
        (options.parent != kInvalidEntity && !find(options.parent))) {
        error = "新对象名称、父节点、变换或外观无效。";
        return std::nullopt;
    }
    if (options.primitive != PrimitiveKind::Empty && options.primitive != PrimitiveKind::Cube &&
        options.primitive != PrimitiveKind::Sphere && options.primitive != PrimitiveKind::Plane) {
        error = "不支持的基础几何类型。";
        return std::nullopt;
    }
    if ((options.camera && !options.camera->isValid()) ||
        (options.light && !options.light->isValid()) || (options.camera && options.light) ||
        ((options.camera || options.light) &&
         (source || options.primitive != PrimitiveKind::Empty))) {
        error = "相机或灯光参数无效，或设备与几何组件冲突。";
        return std::nullopt;
    }
    if (nextId_ == std::numeric_limits<EntityId>::max() ||
        (source && nextMeshId_ == std::numeric_limits<MeshId>::max())) {
        error = "新对象或网格编号已耗尽。";
        return std::nullopt;
    }
    std::shared_ptr<const EditableMeshContent> content;
    if (source) {
        if (options.primitive != PrimitiveKind::Empty) {
            error = "自定义源网格不能同时指定内置几何。";
            return std::nullopt;
        }
        for (const auto& face : source->faces) {
            if (face.material != 0) {
                error = "当前可编辑网格尚不支持外部面材质引用。";
                return std::nullopt;
            }
        }
        content = prepareContent(*source, std::nullopt, std::nullopt, error);
        if (!content)
            return std::nullopt;
    }
    // 容量和表节点在候选阶段准备；发布段只移动 node handle、共享内容及已有 ID。
    entities_.reserve(entities_.size() + 1);
    std::size_t siblingIndex = 0;
    if (options.parent != kInvalidEntity) {
        auto& children = entities_.at(options.parent).children;
        siblingIndex = children.size();
        children.reserve(children.size() + 1);
    }
    SceneNode node;
    node.id = nextId_;
    node.name = options.name;
    node.parent = options.parent;
    node.primitive = options.primitive;
    node.transform = options.transform;
    node.transform.rotation = glm::normalize(node.transform.rotation);
    node.surface = options.surface;
    node.visible = options.visible;
    node.camera = options.camera;
    node.light = options.light;
    node.editableMesh = source ? nextMeshId_ : 0;
    std::unordered_map<EntityId, SceneNode> staging;
    staging.emplace(node.id, std::move(node));
    PreparedEntity result;
    result.origin_ = this;
    result.originToken_ = geometrySnapshotOrigin_;
    result.entity_ = nextId_;
    result.parent_ = options.parent;
    result.mesh_ = source ? nextMeshId_ : 0;
    result.siblingIndex_ = siblingIndex;
    result.node_ = staging.extract(nextId_);
    result.content_ = std::move(content);
    if (source) {
        editableMeshes_.try_emplace(nextMeshId_);
        ++nextMeshId_;
    }
    ++nextId_;
    return result;
}
bool Scene::installPreparedEntity(PreparedEntity& prepared) {
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_ ||
        prepared.node_.empty() || find(prepared.entity_) ||
        (prepared.parent_ != kInvalidEntity &&
         (!find(prepared.parent_) ||
          prepared.siblingIndex_ > find(prepared.parent_)->children.size())) ||
        (prepared.mesh_ != 0 && !editableMeshes_.contains(prepared.mesh_))) {
        return false;
    }
    // 唯一历史逆序回放保留这些容量；外部不得持有候选再穿插其他结构提交。
    entities_.insert(std::move(prepared.node_));
    if (prepared.parent_ != kInvalidEntity) {
        auto& siblings = entities_.at(prepared.parent_).children;
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(prepared.siblingIndex_),
                        prepared.entity_);
    }
    if (prepared.mesh_ != 0) {
        const auto revision = nextMeshRevision_++;
        editableMeshes_.at(prepared.mesh_) = {prepared.content_, revision, revision, revision};
    }
    return true;
}
bool Scene::removePreparedEntity(PreparedEntity& prepared) {
    const auto* node = find(prepared.entity_);
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_ ||
        !prepared.node_.empty() || !node || !node->children.empty()) {
        return false;
    }
    const auto track =
        animation_->tracks.lower_bound({prepared.entity_, AnimationChannel::Position});
    if (track != animation_->tracks.end() && track->first.first == prepared.entity_)
        return false;
    if (node->parent != kInvalidEntity)
        std::erase(entities_.at(node->parent).children, prepared.entity_);
    for (auto& collection : collections_)
        collection.members.erase(prepared.entity_);
    prepared.node_ = entities_.extract(prepared.entity_);
    return true;
}
std::optional<Scene::PreparedSubtree>
Scene::prepareNewSubtree(const std::vector<SubtreeNodeOptions>& options, EntityId parent,
                         std::string& error, std::size_t maximumEntities) {
    error.clear();
    if (options.empty() || maximumEntities == 0 || options.size() > maximumEntities) {
        error = "新子树不能为空，且不得超过对象数量上限。";
        return std::nullopt;
    }
    if (parent != kInvalidEntity && !find(parent)) {
        error = "新子树的外部父节点不存在。";
        return std::nullopt;
    }
    if (options.size() > std::numeric_limits<EntityId>::max() - nextId_) {
        error = "新子树对象编号已耗尽。";
        return std::nullopt;
    }
    const auto finiteMatrix = [](const glm::mat4& matrix) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(matrix[column][row]))
                    return false;
            }
        }
        return true;
    };
    const auto parentWorld = worldMatrix(parent);
    if (!finiteMatrix(parentWorld)) {
        error = "新子树的外部父节点世界变换不是有限值。";
        return std::nullopt;
    }
    std::vector<SceneNode> nodes;
    std::vector<glm::mat4> localMatrices;
    nodes.reserve(options.size());
    localMatrices.reserve(options.size());
    for (std::size_t index = 0; index < options.size(); ++index) {
        const auto& input = options[index];
        if ((index == 0 ? input.parentIndex.has_value()
                        : !input.parentIndex || *input.parentIndex >= index) ||
            input.name.empty() || !input.transform.isValid() || !input.surface.isValid() ||
            (input.meshRenderer && input.meshRenderer->mesh == kInvalidAsset)) {
            error = "新子树的名称、前序父关系、变换、外观或网格引用无效。";
            return std::nullopt;
        }
        SceneNode node;
        node.id = nextId_ + static_cast<EntityId>(index);
        node.name = input.name;
        node.parent = index == 0 ? parent : nodes[*input.parentIndex].id;
        node.transform = input.transform;
        node.transform.rotation = glm::normalize(node.transform.rotation);
        node.surface = input.surface;
        node.visible = input.visible;
        node.meshRenderer = input.meshRenderer;
        // 保存一次归一化 TRS，严格沿运行期逆向父链组合，保留浮点结合顺序。
        const auto local = node.transform.localMatrix();
        auto world = local * glm::mat4(1.0F);
        for (auto ancestor = input.parentIndex; ancestor;
             ancestor = options[*ancestor].parentIndex) {
            world = localMatrices[*ancestor] * world;
        }
        for (const auto* ancestor = find(parent); ancestor != nullptr;
             ancestor = find(ancestor->parent)) {
            world = ancestor->transform.localMatrix() * world;
        }
        if (!finiteMatrix(world)) {
            error = "新子树组合后的世界变换不是有限值。";
            return std::nullopt;
        }
        if (index != 0)
            nodes[*input.parentIndex].children.push_back(node.id);
        nodes.push_back(std::move(node));
        localMatrices.push_back(local);
    }
    PreparedSubtree result;
    result.origin_ = this;
    result.originToken_ = geometrySnapshotOrigin_;
    result.root_ = nodes.front().id;
    result.parent_ = parent;
    result.animationBefore_ = animation_;
    result.animationAfter_ = animation_;
    result.nodes_.reserve(nodes.size());
    result.entityIds_.reserve(nodes.size());
    result.nodeIndices_.reserve(nodes.size());
    result.expected_ = nodes;
    std::unordered_map<EntityId, SceneNode> staging;
    staging.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        result.copies_.emplace(static_cast<EntityId>(index) + 1, nodes[index].id);
        staging.emplace(nodes[index].id, std::move(nodes[index]));
    }
    for (const auto& [inputIndex, id] : result.copies_) {
        result.nodeIndices_.emplace(id, result.nodes_.size());
        result.entityIds_.push_back(id);
        result.nodes_.push_back(staging.extract(id));
    }
    // 复用整组安装的预检与无失败移动段；仅预留不可见容量，不改现有父子树。
    entities_.reserve(entities_.size() + result.nodes_.size());
    if (parent != kInvalidEntity) {
        auto& children = entities_.at(parent).children;
        result.siblingIndex_ = children.size();
        children.reserve(children.size() + 1);
    }
    nextId_ += static_cast<EntityId>(options.size());
    return result;
}
std::optional<Scene::PreparedSubtree> Scene::prepareDuplicateSubtree(EntityId id,
                                                                     std::string& error,
                                                                     std::size_t maximumEntities,
                                                                     std::string rootName) {
    error.clear();
    const auto* root = find(id);
    if (!root || maximumEntities == 0) {
        error = "复制目标不存在，或子树规模上限为零。";
        return std::nullopt;
    }
    // 有界捕获源节点，避免对超限子树完成全量快照或递归遍历后才拒绝。
    SubtreeSnapshot snapshot;
    std::vector<EntityId> pending{id};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const auto& node = entities_.at(current);
        if (snapshot.nodes_.size() + pending.size() + node.children.size() + 1 > maximumEntities) {
            error = "复制子树超过对象数量上限。";
            return std::nullopt;
        }
        snapshot.nodes_.push_back(node);
        for (const auto& collection : collections_) {
            if (collection.members.contains(current))
                snapshot.collectionMemberships_.emplace(current, collection.id);
        }
        pending.insert(pending.end(), node.children.rbegin(), node.children.rend());
    }
    const auto meshCount = static_cast<std::size_t>(
        std::count_if(snapshot.nodes_.begin(), snapshot.nodes_.end(), [](const auto& node) {
            return node.editableMesh != 0;
        }));
    if (snapshot.nodes_.size() > std::numeric_limits<EntityId>::max() - nextId_ ||
        meshCount > std::numeric_limits<MeshId>::max() - nextMeshId_) {
        error = "复制对象或网格编号已耗尽。";
        return std::nullopt;
    }
    PreparedSubtree result;
    result.origin_ = this;
    result.originToken_ = geometrySnapshotOrigin_;
    result.parent_ = root->parent;
    result.animationBefore_ = animation_;
    result.animationAfter_ = animation_;
    auto entityCursor = nextId_;
    for (const auto& node : snapshot.nodes_)
        result.copies_.emplace(node.id, entityCursor++);
    result.root_ = result.copies_.at(id);
    std::size_t keyCount = 0;
    std::size_t addedKeys = 0;
    std::size_t addedTracks = 0;
    for (const auto& [trackId, track] : animation_->tracks) {
        keyCount += track.keys.size();
        if (result.copies_.contains(trackId.first)) {
            ++addedTracks;
            addedKeys += track.keys.size();
        }
    }
    if (addedTracks > kAnimationMaximumTracks - animation_->tracks.size() ||
        addedKeys > kAnimationMaximumKeys - keyCount) {
        error = "复制子树的轨道或关键帧超过动画预算。";
        return std::nullopt;
    }
    if (addedTracks != 0) {
        SceneAnimation copied = *animation_;
        for (const auto& [trackId, track] : animation_->tracks) {
            if (const auto entity = result.copies_.find(trackId.first);
                entity != result.copies_.end())
                copied.tracks.emplace(AnimationTrackId{entity->second, trackId.second}, track);
        }
        result.animationAfter_ = std::make_shared<const SceneAnimation>(std::move(copied));
    }
    result.nodes_.reserve(snapshot.nodes_.size());
    result.entityIds_.reserve(snapshot.nodes_.size());
    result.nodeIndices_.reserve(snapshot.nodes_.size());
    result.expected_.reserve(snapshot.nodes_.size());
    std::unordered_map<EntityId, SceneNode> staging;
    staging.reserve(snapshot.nodes_.size());
    auto meshCursor = nextMeshId_;
    for (const auto& source : snapshot.nodes_) {
        auto node = source;
        node.id = result.copies_.at(source.id);
        node.parent = source.id == id ? source.parent : result.copies_.at(source.parent);
        for (auto& child : node.children)
            child = result.copies_.at(child);
        if (source.id == id)
            node.name = rootName.empty() ? source.name + " Copy" : rootName;
        if (source.editableMesh != 0) {
            const auto* record = editableMesh(source.editableMesh);
            if (!record) {
                error = "复制目标引用的可编辑网格不存在。";
                return std::nullopt;
            }
            node.editableMesh = meshCursor++;
            result.meshes_.emplace(node.editableMesh, record->content);
        }
        result.expected_.push_back(node);
        staging.emplace(node.id, std::move(node));
    }
    for (const auto& source : snapshot.nodes_) {
        const auto copy = result.copies_.at(source.id);
        result.nodeIndices_.emplace(copy, result.nodes_.size());
        result.entityIds_.push_back(copy);
        result.nodes_.push_back(staging.extract(copy));
    }
    for (const auto& [source, collection] : snapshot.collectionMemberships_) {
        const auto member = result.copies_.at(source);
        std::set<EntityId> stagingMembers{member};
        result.memberships_[collection].emplace_back(member, stagingMembers.extract(member));
    }
    // 所有节点、成员和网格绑定先分配完毕；发布段不创建表节点或扩容父节点。
    entities_.reserve(entities_.size() + result.nodes_.size());
    editableMeshes_.reserve(editableMeshes_.size() + result.meshes_.size());
    if (result.parent_ != kInvalidEntity) {
        auto& children = entities_.at(result.parent_).children;
        result.siblingIndex_ = children.size();
        children.reserve(children.size() + 1);
    }
    for (const auto& [mesh, content] : result.meshes_)
        editableMeshes_.try_emplace(mesh);
    nextId_ = entityCursor;
    nextMeshId_ = meshCursor;
    result.sources_ = std::move(snapshot.nodes_);
    return result;
}
std::optional<Scene::PreparedSubtree> Scene::prepareRemoveSubtree(EntityId id, std::string& error,
                                                                  std::size_t maximumEntities) {
    error.clear();
    const auto* root = find(id);
    if (!root || maximumEntities == 0) {
        error = "删除目标不存在，或子树规模上限为零。";
        return std::nullopt;
    }
    PreparedSubtree result;
    result.origin_ = this;
    result.originToken_ = geometrySnapshotOrigin_;
    result.root_ = id;
    result.parent_ = root->parent;
    result.present_ = true;
    result.animationBefore_ = animation_;
    result.animationAfter_ = animation_;
    if (root->parent != kInvalidEntity) {
        const auto& siblings = entities_.at(root->parent).children;
        result.siblingIndex_ = static_cast<std::size_t>(
            std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
    }
    std::vector<EntityId> pending{id};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const auto& node = entities_.at(current);
        if (result.nodes_.size() + pending.size() + node.children.size() + 1 > maximumEntities) {
            error = "删除子树超过对象数量上限。";
            return std::nullopt;
        }
        result.copies_.emplace(current, current);
        result.entityIds_.push_back(current);
        result.nodeIndices_.emplace(current, result.nodes_.size());
        result.nodes_.emplace_back();
        result.expected_.push_back(node);
        if (node.editableMesh != 0)
            result.meshes_.emplace(node.editableMesh,
                                   editableMeshes_.at(node.editableMesh).content);
        for (const auto& collection : collections_) {
            if (collection.members.contains(current))
                result.memberships_[collection.id].emplace_back(current,
                                                                std::set<EntityId>::node_type{});
        }
        pending.insert(pending.end(), node.children.rbegin(), node.children.rend());
    }
    const bool removesTracks = std::any_of(
        animation_->tracks.begin(), animation_->tracks.end(), [&result](const auto& item) {
            return result.nodeIndices_.contains(item.first.first);
        });
    if (removesTracks) {
        SceneAnimation remaining = *animation_;
        std::erase_if(remaining.tracks, [&result](const auto& item) {
            return result.nodeIndices_.contains(item.first.first);
        });
        result.animationBefore_ = std::make_shared<const SceneAnimation>(std::move(remaining));
    }
    // 当前表已容纳原节点，提取不缩容；恢复重复使用该容量和原 node_handle。
    entities_.reserve(entities_.size());
    return result;
}
bool Scene::canInstallPreparedSubtree(const PreparedSubtree& prepared) const {
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_ ||
        prepared.present_ || !prepared.animationBefore_ || !prepared.animationAfter_ ||
        animation_ != prepared.animationBefore_ || prepared.copies_.empty() ||
        prepared.nodes_.size() != prepared.copies_.size() ||
        prepared.expected_.size() != prepared.nodes_.size() ||
        static_cast<double>(entities_.size() + prepared.nodes_.size()) >
            static_cast<double>(entities_.bucket_count()) * entities_.max_load_factor()) {
        return false;
    }
    if (prepared.parent_ != kInvalidEntity) {
        const auto* parent = find(prepared.parent_);
        if (!parent || prepared.siblingIndex_ > parent->children.size() ||
            parent->children.size() == parent->children.capacity()) {
            return false;
        }
    }
    for (std::size_t index = 0; index < prepared.nodes_.size(); ++index) {
        const auto& node = prepared.nodes_[index];
        if (node.empty() || find(node.key()) ||
            !sameBatchNode(node.mapped(), prepared.expected_[index]))
            return false;
    }
    for (const auto& source : prepared.sources_) {
        const auto* current = find(source.id);
        if (!current || !sameBatchNode(*current, source))
            return false;
        if (source.editableMesh != 0) {
            const auto& copy =
                prepared.expected_[prepared.nodeIndices_.at(prepared.copies_.at(source.id))];
            const auto* mesh = editableMesh(source.editableMesh);
            if (!mesh || mesh->content != prepared.meshes_.at(copy.editableMesh))
                return false;
        }
    }
    for (const auto& [mesh, content] : prepared.meshes_) {
        if (!editableMeshes_.contains(mesh))
            return false;
    }
    for (const auto& [collection, members] : prepared.memberships_) {
        const auto found =
            std::find_if(collections_.begin(), collections_.end(), [collection](const auto& item) {
                return item.id == collection;
            });
        if (found == collections_.end())
            return false;
        for (const auto& [member, node] : members) {
            if (node.empty() || node.value() != member || found->members.contains(member))
                return false;
        }
    }
    for (const auto& [id, track] : prepared.animationAfter_->tracks) {
        if (!find(id.first) && !prepared.nodeIndices_.contains(id.first))
            return false;
    }
    return true;
}
bool Scene::installPreparedSubtree(PreparedSubtree& prepared) {
    if (!canInstallPreparedSubtree(prepared))
        return false;
    // 唯一历史串行回放保证预检与移动之间无其他发布；之后没有可恢复失败分支。
    for (auto& node : prepared.nodes_)
        entities_.insert(std::move(node));
    if (prepared.parent_ != kInvalidEntity) {
        auto& children = entities_.at(prepared.parent_).children;
        children.insert(children.begin() + static_cast<std::ptrdiff_t>(prepared.siblingIndex_),
                        prepared.root_);
    }
    for (const auto& [mesh, content] : prepared.meshes_) {
        const auto revision = nextMeshRevision_++;
        editableMeshes_.at(mesh) = {content, revision, revision, revision};
    }
    for (auto& [collection, members] : prepared.memberships_) {
        auto found =
            std::find_if(collections_.begin(), collections_.end(), [collection](const auto& item) {
                return item.id == collection;
            });
        for (auto& [member, node] : members)
            found->members.insert(std::move(node));
    }
    animation_ = prepared.animationAfter_;
    prepared.present_ = true;
    return true;
}
bool Scene::canRemovePreparedSubtree(const PreparedSubtree& prepared) const {
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_ ||
        !prepared.present_ || !prepared.animationBefore_ || !prepared.animationAfter_ ||
        animation_ != prepared.animationAfter_ || prepared.copies_.empty() ||
        prepared.nodes_.size() != prepared.copies_.size() ||
        prepared.expected_.size() != prepared.nodes_.size() ||
        std::any_of(prepared.nodes_.begin(), prepared.nodes_.end(), [](const auto& node) {
            return !node.empty();
        })) {
        return false;
    }
    const auto isCopy = [&prepared](EntityId id) {
        return prepared.nodeIndices_.contains(id);
    };
    for (const auto& [source, copy] : prepared.copies_) {
        const auto* node = find(copy);
        if (!node || !sameBatchNode(*node, prepared.expected_[prepared.nodeIndices_.at(copy)]) ||
            (copy == prepared.root_ ? node->parent != prepared.parent_ : !isCopy(node->parent)) ||
            std::any_of(node->children.begin(), node->children.end(), [&isCopy](EntityId child) {
                return !isCopy(child);
            })) {
            return false;
        }
    }
    if (prepared.parent_ != kInvalidEntity) {
        const auto* parent = find(prepared.parent_);
        if (!parent || prepared.siblingIndex_ >= parent->children.size() ||
            parent->children[prepared.siblingIndex_] != prepared.root_) {
            return false;
        }
    }
    for (const auto& [id, content] : prepared.meshes_) {
        const auto* mesh = editableMesh(id);
        if (!mesh || mesh->content != content)
            return false;
    }
    for (const auto& [collection, members] : prepared.memberships_) {
        const auto found =
            std::find_if(collections_.begin(), collections_.end(), [collection](const auto& item) {
                return item.id == collection;
            });
        if (found == collections_.end())
            return false;
        for (const auto& [member, node] : members) {
            if (!node.empty() || !found->members.contains(member))
                return false;
        }
    }
    for (const auto& collection : collections_) {
        for (const auto member : collection.members) {
            if (!isCopy(member))
                continue;
            const auto found = prepared.memberships_.find(collection.id);
            if (found == prepared.memberships_.end() ||
                std::none_of(found->second.begin(), found->second.end(),
                             [member](const auto& item) {
                                 return item.first == member;
                             })) {
                return false;
            }
        }
    }
    for (const auto& [id, track] : prepared.animationBefore_->tracks) {
        if (!find(id.first) || isCopy(id.first))
            return false;
    }
    return true;
}
bool Scene::removePreparedSubtree(PreparedSubtree& prepared) {
    if (!canRemovePreparedSubtree(prepared))
        return false;
    if (prepared.parent_ != kInvalidEntity)
        std::erase(entities_.at(prepared.parent_).children, prepared.root_);
    for (auto& [collection, members] : prepared.memberships_) {
        auto found =
            std::find_if(collections_.begin(), collections_.end(), [collection](const auto& item) {
                return item.id == collection;
            });
        for (auto& [member, node] : members)
            node = found->members.extract(member);
    }
    for (const auto copy : prepared.entityIds_)
        prepared.nodes_[prepared.nodeIndices_.at(copy)] = entities_.extract(copy);
    animation_ = prepared.animationBefore_;
    prepared.present_ = false;
    return true;
}
std::optional<std::vector<AnimationPoseInput>>
Scene::animationPoseInputs(const PreparedSubtree& prepared, bool present,
                           std::string& error) const {
    error.clear();
    std::size_t count = entities_.size();
    if (present && !prepared.present_) {
        if (count > kAnimationMaximumPoseNodes ||
            prepared.entityIds_.size() > kAnimationMaximumPoseNodes - count) {
            error = "候选动画姿态超过10000节点预算。";
            return std::nullopt;
        }
        count += prepared.entityIds_.size();
    } else if (!present && prepared.present_) {
        if (prepared.entityIds_.size() > count) {
            error = "候选子树来源无效。";
            return std::nullopt;
        }
        count -= prepared.entityIds_.size();
    }
    if (count > kAnimationMaximumPoseNodes) {
        error = "候选动画姿态超过10000节点预算。";
        return std::nullopt;
    }
    if (prepared.present_ ? !canRemovePreparedSubtree(prepared)
                          : !canInstallPreparedSubtree(prepared)) {
        error = "候选子树的来源、绑定、状态或恢复容量已失效。";
        return std::nullopt;
    }
    if (present == prepared.present_)
        return animationPoseInputs(error);
    std::vector<EntityId> rootIds;
    for (const auto& [id, node] : entities_) {
        if (node.parent == 0 && (present || !prepared.nodeIndices_.contains(id)))
            rootIds.push_back(id);
    }
    if (present && prepared.parent_ == 0)
        rootIds.push_back(prepared.root_);
    std::sort(rootIds.begin(), rootIds.end());
    std::vector<EntityId> pending(rootIds.rbegin(), rootIds.rend());
    std::vector<AnimationPoseInput> result;
    result.reserve(count);
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto target = prepared.nodeIndices_.find(id);
        const auto& node = target != prepared.nodeIndices_.end() && !prepared.present_
                               ? prepared.nodes_[target->second].mapped()
                               : entities_.at(id);
        result.push_back({id, node.parent, node.transform});
        if (present && !prepared.present_ && id == prepared.parent_) {
            for (std::size_t reverse = node.children.size() + 1; reverse != 0; --reverse) {
                const auto index = reverse - 1;
                pending.push_back(
                    index == prepared.siblingIndex_
                        ? prepared.root_
                        : node.children[index < prepared.siblingIndex_ ? index : index - 1]);
            }
        } else {
            for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
                if (present || !prepared.nodeIndices_.contains(*child))
                    pending.push_back(*child);
            }
        }
    }
    if (result.size() != count) {
        error = "候选子树没有形成完整数值姿态输入。";
        return std::nullopt;
    }
    return result;
}
std::optional<Scene::PreparedParentChange>
Scene::prepareParentChange(EntityId child, EntityId parent, std::string& error) const {
    error.clear();
    const auto* node = find(child);
    if (!node || (parent != kInvalidEntity && !find(parent))) {
        error = "换父目标或新父节点不存在。";
        return std::nullopt;
    }
    PreparedParentChange prepared;
    prepared.origin_ = this;
    prepared.originToken_ = geometrySnapshotOrigin_;
    prepared.animation_ = animation_;
    prepared.entity_ = child;
    prepared.beforeParent_ = node->parent;
    prepared.afterParent_ = parent;
    prepared.local_ = node->transform;
    if (!prepared.hasChanges())
        return prepared;
    for (EntityId ancestor = parent; ancestor != kInvalidEntity;
         ancestor = find(ancestor)->parent) {
        if (ancestor == child) {
            error = "换父不能指向自身或形成循环。";
            return std::nullopt;
        }
    }
    std::vector<EntityId> pending{child};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto track = animation_->tracks.lower_bound({id, AnimationChannel::Position});
        if (track != animation_->tracks.end() && track->first.first == id) {
            error = "自身或后代有直接动画轨道的子树不能换父。";
            return std::nullopt;
        }
        const auto& children = entities_.at(id).children;
        prepared.subtreeChildren_.emplace_back(id, children);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    for (const auto id : {prepared.beforeParent_, prepared.afterParent_}) {
        if (id == kInvalidEntity)
            continue;
        PreparedParentChange::ParentChildren children;
        children.parent = id;
        children.before = entities_.at(id).children;
        children.after = children.before;
        if (id == prepared.beforeParent_)
            std::erase(children.after, child);
        else
            children.after.push_back(child);
        children.buffer = children.after;
        prepared.parents_.push_back(std::move(children));
    }
    return prepared;
}
bool Scene::canApplyPreparedParentChange(const PreparedParentChange& prepared, bool forward) const {
    const auto* node = find(prepared.entity_);
    if (prepared.origin_ != this || prepared.originToken_ != geometrySnapshotOrigin_ ||
        animation_ != prepared.animation_ || prepared.installed_ == forward || !node ||
        node->parent != (forward ? prepared.beforeParent_ : prepared.afterParent_) ||
        !sameBatchTransform(node->transform, prepared.local_)) {
        return false;
    }
    for (const auto& [id, children] : prepared.subtreeChildren_) {
        const auto* descendant = find(id);
        if (!descendant || descendant->children != children)
            return false;
    }
    for (const auto& state : prepared.parents_) {
        const auto* parent = find(state.parent);
        if (!parent || parent->children != (forward ? state.before : state.after) ||
            state.buffer != (forward ? state.after : state.before)) {
            return false;
        }
    }
    return true;
}
bool Scene::canInstallPreparedParentChange(const PreparedParentChange& prepared) const {
    return canApplyPreparedParentChange(prepared, true);
}
bool Scene::canRestorePreparedParentChange(const PreparedParentChange& prepared) const {
    return canApplyPreparedParentChange(prepared, false);
}
bool Scene::installPreparedParentChange(PreparedParentChange& prepared) {
    if (!canInstallPreparedParentChange(prepared))
        return false;
    for (auto& state : prepared.parents_)
        entities_.at(state.parent).children.swap(state.buffer);
    entities_.at(prepared.entity_).parent = prepared.afterParent_;
    prepared.installed_ = true;
    return true;
}
bool Scene::restorePreparedParentChange(PreparedParentChange& prepared) {
    if (!canRestorePreparedParentChange(prepared))
        return false;
    for (auto& state : prepared.parents_)
        entities_.at(state.parent).children.swap(state.buffer);
    entities_.at(prepared.entity_).parent = prepared.beforeParent_;
    prepared.installed_ = false;
    return true;
}
std::optional<std::vector<AnimationPoseInput>>
Scene::animationPoseInputs(const PreparedParentChange& prepared, bool forward,
                           std::string& error) const {
    error.clear();
    if (entities_.size() > kAnimationMaximumPoseNodes) {
        error = "候选动画姿态超过10000节点预算。";
        return std::nullopt;
    }
    if (!canApplyPreparedParentChange(prepared, !prepared.installed_)) {
        error = "候选换父的来源、局部变换或兄弟状态已失效。";
        return std::nullopt;
    }
    const auto targetParent = forward ? prepared.afterParent_ : prepared.beforeParent_;
    std::vector<EntityId> rootIds;
    for (const auto& [id, node] : entities_) {
        if ((id == prepared.entity_ ? targetParent : node.parent) == 0)
            rootIds.push_back(id);
    }
    std::sort(rootIds.begin(), rootIds.end());
    std::vector<EntityId> pending(rootIds.rbegin(), rootIds.rend());
    std::vector<AnimationPoseInput> result;
    result.reserve(entities_.size());
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto& node = entities_.at(id);
        result.push_back({id, id == prepared.entity_ ? targetParent : node.parent, node.transform});
        const auto state = std::find_if(prepared.parents_.begin(), prepared.parents_.end(),
                                        [id](const auto& item) {
                                            return item.parent == id;
                                        });
        const auto& children = state == prepared.parents_.end()
                                   ? node.children
                                   : (forward ? state->after : state->before);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    if (result.size() != entities_.size()) {
        error = "候选换父没有形成完整数值姿态输入。";
        return std::nullopt;
    }
    return result;
}
std::optional<Scene::GeometrySnapshot> Scene::geometrySnapshot(EntityId id) const {
    const auto* node = find(id);
    if (!node) {
        return std::nullopt;
    }
    GeometrySnapshot result;
    result.origin_ = this;
    result.originToken_ = geometrySnapshotOrigin_;
    result.entity_ = id;
    result.primitive_ = node->primitive;
    result.renderer_ = node->meshRenderer;
    result.mesh_ = node->editableMesh;
    if (result.mesh_ != 0) {
        result.content_ = editableMeshes_.at(result.mesh_).content;
    }
    return result;
}
std::optional<Scene::GeometrySnapshot>
Scene::prepareEditableGeometry(EntityId id, const modeling::EditableMesh& source,
                               std::string& error) {
    return prepareEditableGeometryContent(id, source, {}, error);
}
std::optional<Scene::GeometrySnapshot> Scene::prepareTransformedEditableGeometry(
    EntityId id, const modeling::EditableMesh& source, const std::set<modeling::VertexId>& selected,
    const glm::dmat4& objectToWorld, const glm::dmat4& worldDelta, std::string& error) {
    const auto* node = find(id);
    if (!node || node->camera || node->light) {
        error = "对象不存在或不是可编辑几何体";
        return std::nullopt;
    }
    auto transformed = modeling::transformVertices(source, selected, objectToWorld, worldDelta);
    if (!transformed.mesh) {
        error = transformed.error;
        return std::nullopt;
    }
    // 复用值仅来自 Scene 内部刚执行的变换；外部调用方不能提交任意派生数据。
    return prepareEditableGeometryContent(id, std::move(*transformed.mesh),
                                          std::move(transformed.derived), error);
}
std::optional<Scene::GeometrySnapshot> Scene::prepareTransformedEditableGeometry(
    EntityId id, const GeometrySnapshot& before, const std::set<modeling::VertexId>& selected,
    const glm::dmat4& objectToWorld, const glm::dmat4& worldDelta, std::string& error) {
    const auto* node = find(id);
    const auto* current = node ? editableMesh(node->editableMesh) : nullptr;
    if (!node || node->camera || node->light || !current || before.origin_ != this ||
        before.originToken_ != geometrySnapshotOrigin_ || before.entity_ != id ||
        before.mesh_ == 0 || before.mesh_ != node->editableMesh || !before.content_) {
        error = "变换前快照必须来自当前场景，并属于同一对象及其可编辑网格。";
        return std::nullopt;
    }
    std::vector<std::size_t> affectedFaces;
    bool changed = false;
    auto transformed = modeling::VertexTransform::transformSource(
        before.content_->source, selected, objectToWorld, worldDelta, affectedFaces, changed);
    if (!transformed.mesh) {
        error = transformed.error;
        return std::nullopt;
    }
    if (!changed && current->content->mirror == before.content_->mirror &&
        current->content->subdivision == before.content_->subdivision) {
        error.clear();
        return before;
    }
    auto derived = modeling::MeshDerivation::deriveTransformed(
        *transformed.mesh, before.content_->derived, affectedFaces);
    if (!derived.derived) {
        error = derived.error;
        return std::nullopt;
    }
    return prepareEditableGeometryContent(id, std::move(*transformed.mesh),
                                          std::move(derived.derived), error);
}
std::optional<Scene::GeometrySnapshot>
Scene::prepareEditableGeometryContent(EntityId id, modeling::EditableMesh source,
                                      std::optional<modeling::DerivedMesh> derived,
                                      std::string& error) {
    const auto* node = find(id);
    if (!node || node->camera || node->light) {
        error = "对象不存在或不是可编辑几何体";
        return std::nullopt;
    }
    // 材质资源重映射尚未接入；禁止生成无法保存重开的引用。
    for (const auto& face : source.faces) {
        if (face.material != 0) {
            error = "当前可编辑网格尚不支持外部面材质引用";
            return std::nullopt;
        }
    }
    const auto* current = node->editableMesh ? editableMesh(node->editableMesh) : nullptr;
    auto content = prepareContent(
        std::move(source), current ? current->content->mirror : std::nullopt,
        current ? current->content->subdivision : std::nullopt, error, std::move(derived));
    if (!content) {
        return std::nullopt;
    }
    GeometrySnapshot result;
    result.origin_ = this;
    result.originToken_ = geometrySnapshotOrigin_;
    result.entity_ = id;
    result.content_ = std::move(content);
    result.mesh_ = node->editableMesh;
    if (result.mesh_ == 0) {
        if (nextMeshId_ == std::numeric_limits<MeshId>::max()) {
            error = "可编辑网格编号已耗尽";
            return std::nullopt;
        }
        // 在候选阶段预留槽位，避免 QUndoStack 回放过程中再分配网格表节点。
        editableMeshes_.try_emplace(nextMeshId_);
        result.mesh_ = nextMeshId_++;
    }
    error.clear();
    return result;
}
std::optional<Scene::GeometrySnapshot>
Scene::prepareMirror(EntityId id, std::optional<modeling::MirrorOptions> options,
                     std::string& error) {
    const auto* node = find(id);
    const auto* current = node ? editableMesh(node->editableMesh) : nullptr;
    if (!current) {
        error = "对象尚未绑定可编辑网格";
        return std::nullopt;
    }
    auto content =
        prepareContent(current->content->source, options, current->content->subdivision, error);
    if (!content)
        return std::nullopt;
    auto result = *geometrySnapshot(id);
    result.content_ = std::move(content);
    return result;
}
std::optional<Scene::GeometrySnapshot> Scene::prepareAppliedMirror(EntityId id,
                                                                    std::string& error) {
    const auto* node = find(id);
    const auto* current = node ? editableMesh(node->editableMesh) : nullptr;
    if (!current || !current->content->mirrorEvaluation) {
        error = "当前对象没有可应用的 Mirror";
        return std::nullopt;
    }
    auto content = prepareContent(current->content->mirrorEvaluation->mesh, std::nullopt,
                                  current->content->subdivision, error);
    if (!content)
        return std::nullopt;
    auto result = *geometrySnapshot(id);
    result.content_ = std::move(content);
    return result;
}
std::optional<Scene::GeometrySnapshot>
Scene::prepareSubdivision(EntityId id, std::optional<modeling::SubdivisionOptions> options,
                          std::string& error) {
    const auto* node = find(id);
    const auto* current = node ? editableMesh(node->editableMesh) : nullptr;
    if (!current) {
        error = "对象尚未绑定可编辑网格";
        return std::nullopt;
    }
    auto content =
        prepareContent(current->content->source, current->content->mirror, options, error);
    if (!content)
        return std::nullopt;
    auto result = *geometrySnapshot(id);
    result.content_ = std::move(content);
    return result;
}
std::optional<Scene::GeometrySnapshot> Scene::prepareAppliedSubdivision(EntityId id,
                                                                        std::string& error) {
    const auto* node = find(id);
    const auto* current = node ? editableMesh(node->editableMesh) : nullptr;
    if (!current || !current->content->subdivision) {
        error = "当前对象没有可应用的 Subdivision";
        return std::nullopt;
    }
    auto content =
        prepareContent(current->content->evaluatedMesh(), std::nullopt, std::nullopt, error);
    if (!content)
        return std::nullopt;
    auto result = *geometrySnapshot(id);
    result.content_ = std::move(content);
    return result;
}
bool Scene::installGeometry(const GeometrySnapshot& snapshot) {
    const auto* current = find(snapshot.entity_);
    if (!current || current->camera || current->light) {
        return false;
    }
    if (snapshot.mesh_ != 0) {
        const auto entry = editableMeshes_.find(snapshot.mesh_);
        if (entry == editableMeshes_.end()) {
            return false;
        }
        // 历史中的不可变内容不携带旧 revision，安装不重新分配/运行算法。
        auto& record = entry->second;
        const auto revision = nextMeshRevision_++;
        record = {snapshot.content_, revision, revision, revision};
    }
    auto& node = entities_.at(snapshot.entity_);
    node.primitive = snapshot.primitive_;
    node.meshRenderer = snapshot.renderer_;
    node.editableMesh = snapshot.mesh_;
    return true;
}
const EditableMeshRecord* Scene::editableMesh(MeshId id) const {
    const auto found = editableMeshes_.find(id);
    return found == editableMeshes_.end() || !found->second.content ? nullptr : &found->second;
}
std::vector<EditableMeshResource> Scene::editableMeshes() const {
    std::vector<EditableMeshResource> result;
    for (const auto& node : nodes()) {
        if (node.editableMesh != 0) {
            const auto& content = *editableMeshes_.at(node.editableMesh).content;
            result.push_back(
                {node.editableMesh, content.source, content.mirror, content.subdivision});
        }
    }
    return result;
}
CollectionId Scene::createCollection(std::string name) {
    if (name.empty() || nextCollectionId_ == std::numeric_limits<CollectionId>::max())
        return 0;
    const auto id = nextCollectionId_;
    collections_.push_back({id, std::move(name)});
    ++nextCollectionId_;
    return id;
}
bool Scene::replaceCollections(const std::vector<SceneCollection>& collections) {
    std::unordered_set<CollectionId> ids;
    std::unordered_set<EntityId> members;
    auto nextId = nextCollectionId_;
    for (const auto& collection : collections) {
        if (collection.id == 0 || collection.id == std::numeric_limits<CollectionId>::max() ||
            collection.name.empty() || !ids.insert(collection.id).second)
            return false;
        for (const auto member : collection.members) {
            if (!find(member) || !members.insert(member).second)
                return false;
        }
        nextId = std::max(nextId, collection.id + 1);
    }
    auto candidate = collections;
    collections_ = std::move(candidate);
    nextCollectionId_ = nextId;
    return true;
}
bool Scene::exchangeCollectionSnapshot(std::vector<SceneCollection>& prepared) {
    auto nextId = nextCollectionId_;
    for (const auto& collection : prepared) {
        if (collection.id == 0 || collection.id == std::numeric_limits<CollectionId>::max() ||
            collection.name.empty())
            return false;
        for (const auto member : collection.members)
            if (!find(member))
                return false;
        nextId = std::max(nextId, collection.id + 1);
    }
    collections_.swap(prepared);
    nextCollectionId_ = nextId;
    return true;
}
modeling::MirrorResult Scene::evaluateMirror(EntityId id,
                                             const modeling::MirrorOptions& options) const {
    const auto* node = find(id);
    const auto* record = node ? editableMesh(node->editableMesh) : nullptr;
    if (!record)
        return {{}, "请选择已转换为可编辑网格的对象；镜像求值不自动转换静态几何。"};
    return modeling::evaluateMirror(record->content->source, options);
}
Scene::SubtreeSnapshot Scene::snapshotSubtree(EntityId id) const {
    SubtreeSnapshot snapshot;
    const auto* root = find(id);
    if (!root) {
        return snapshot;
    }
    snapshot.origin_ = this;
    snapshot.originToken_ = geometrySnapshotOrigin_;
    snapshot.animation_ = animation_;
    if (root->parent != kInvalidEntity) {
        const auto& siblings = find(root->parent)->children;
        snapshot.siblingIndex_ = static_cast<std::size_t>(
            std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
    }
    std::vector<EntityId> pending{id};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const auto* node = find(current);
        snapshot.nodes_.push_back(*node);
        for (const auto& collection : collections_) {
            if (collection.members.contains(current))
                snapshot.collectionMemberships_.emplace(current, collection.id);
        }
        pending.insert(pending.end(), node->children.rbegin(), node->children.rend());
    }
    return snapshot;
}
bool Scene::restoreSubtree(const SubtreeSnapshot& snapshot) {
    if (snapshot.nodes_.empty() || snapshot.origin_ != this ||
        snapshot.originToken_ != geometrySnapshotOrigin_ || !snapshot.animation_) {
        return false;
    }
    const auto& root = snapshot.nodes_.front();
    if (root.parent != kInvalidEntity &&
        (!find(root.parent) || snapshot.siblingIndex_ > find(root.parent)->children.size())) {
        return false;
    }
    for (const auto& node : snapshot.nodes_) {
        if (find(node.id)) {
            return false;
        }
    }
    std::unordered_set<EntityId> restoring;
    for (const auto& node : snapshot.nodes_)
        restoring.insert(node.id);
    std::size_t restoredTracks = 0;
    std::size_t restoredKeys = 0;
    std::size_t currentKeys = 0;
    for (const auto& [id, track] : animation_->tracks)
        currentKeys += track.keys.size();
    for (const auto& [id, track] : snapshot.animation_->tracks) {
        if (restoring.contains(id.first)) {
            ++restoredTracks;
            restoredKeys += track.keys.size();
        }
    }
    if (restoredTracks > kAnimationMaximumTracks - animation_->tracks.size() ||
        restoredKeys > kAnimationMaximumKeys - currentKeys) {
        return false;
    }
    auto restoredAnimation = animation_;
    if (restoredTracks != 0) {
        SceneAnimation candidate = *animation_;
        for (const auto& [id, track] : snapshot.animation_->tracks) {
            if (restoring.contains(id.first))
                candidate.tracks.emplace(id, track);
        }
        std::vector<EntityId> available;
        available.reserve(entities_.size() + snapshot.nodes_.size());
        for (const auto& [id, node] : entities_)
            available.push_back(id);
        for (const auto& node : snapshot.nodes_)
            available.push_back(node.id);
        if (!validateSceneAnimation(candidate, available).isValid())
            return false;
        restoredAnimation = candidate == *snapshot.animation_
                                ? snapshot.animation_
                                : std::make_shared<const SceneAnimation>(std::move(candidate));
    }
    auto restoredCollections = collections_;
    for (const auto& [member, collection] : snapshot.collectionMemberships_) {
        const auto found = std::find_if(restoredCollections.begin(), restoredCollections.end(),
                                        [collection](const auto& item) {
                                            return item.id == collection;
                                        });
        if (found == restoredCollections.end())
            return false;
        found->members.insert(member);
    }
    std::unordered_map<EntityId, SceneNode> staging;
    staging.reserve(snapshot.nodes_.size());
    for (const auto& node : snapshot.nodes_)
        staging.emplace(node.id, node);
    entities_.reserve(entities_.size() + snapshot.nodes_.size());
    if (root.parent != kInvalidEntity) {
        auto& siblings = entities_.at(root.parent).children;
        siblings.reserve(siblings.size() + 1);
    }
    // legacy 快照仅恢复自己的轨道；正式历史应使用共同 prepared 的无分配回放。
    for (const auto& node : snapshot.nodes_) {
        entities_.insert(staging.extract(node.id));
        nextId_ = std::max(nextId_, node.id + 1);
    }
    if (root.parent != kInvalidEntity) {
        auto& siblings = entities_.at(root.parent).children;
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(snapshot.siblingIndex_),
                        root.id);
    }
    collections_ = std::move(restoredCollections);
    animation_ = std::move(restoredAnimation);
    return true;
}
EntityId Scene::duplicateSubtree(EntityId id) {
    std::string error;
    auto prepared = prepareDuplicateSubtree(id, error, std::numeric_limits<std::size_t>::max());
    return prepared && installPreparedSubtree(*prepared) ? prepared->rootId() : kInvalidEntity;
}
EntityId Scene::createEntity(std::string name, EntityId parent, PrimitiveKind primitive) {
    if (name.empty() || (parent != kInvalidEntity && find(parent) == nullptr)) {
        return kInvalidEntity;
    }
    const EntityId id = nextId_++;
    SceneNode node;
    node.id = id;
    node.name = std::move(name);
    node.parent = parent;
    node.primitive = primitive;
    entities_.emplace(id, std::move(node));
    if (parent != kInvalidEntity) {
        entities_.at(parent).children.push_back(id);
    }
    return id;
}
const SceneNode* Scene::find(EntityId id) const {
    const auto entry = entities_.find(id);
    return entry == entities_.end() ? nullptr : &entry->second;
}
bool Scene::removeEntity(EntityId id) {
    std::string error;
    auto prepared = prepareRemoveSubtree(id, error);
    return prepared && removePreparedSubtree(*prepared);
}
bool Scene::setParent(EntityId child, EntityId parent) {
    std::string error;
    auto prepared = prepareParentChange(child, parent, error);
    return prepared && installPreparedParentChange(*prepared);
}
bool Scene::renameEntity(EntityId id, std::string name) {
    if (find(id) == nullptr || name.empty()) {
        return false;
    }
    entities_.at(id).name = std::move(name);
    return true;
}
bool Scene::exchangeEntityName(EntityId id, std::string& preparedName) {
    const auto found = entities_.find(id);
    if (found == entities_.end() || preparedName.empty())
        return false;
    found->second.name.swap(preparedName);
    return true;
}
bool Scene::setSiblingIndex(EntityId child, std::size_t index) {
    const auto* node = find(child);
    if (!node || node->parent == 0) {
        return false;
    }
    auto& siblings = entities_.at(node->parent).children;
    if (index >= siblings.size()) {
        return false;
    }
    std::erase(siblings, child);
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index), child);
    return true;
}
bool Scene::setVisible(EntityId id, bool visible) {
    if (find(id) == nullptr) {
        return false;
    }
    entities_.at(id).visible = visible;
    return true;
}
bool Scene::setTransform(EntityId id, const Transform& transform) {
    if (find(id) == nullptr || !transform.isValid()) {
        return false;
    }
    entities_.at(id).transform = transform;
    entities_.at(id).transform.rotation = glm::normalize(transform.rotation);
    return true;
}
bool Scene::installTransformSnapshot(EntityId id, const Transform& transform) {
    if (find(id) == nullptr || !transform.isValid()) {
        return false;
    }
    entities_.at(id).transform = transform;
    return true;
}
glm::mat4 Scene::worldMatrix(EntityId id) const {
    glm::mat4 world(1.0F);
    for (const SceneNode* node = find(id); node != nullptr; node = find(node->parent)) {
        world = node->transform.localMatrix() * world;
    }
    return world;
}

bool Scene::setMeshRenderer(EntityId id, MeshRendererComponent component) {
    if (find(id) == nullptr || component.mesh == kInvalidAsset || find(id)->camera ||
        find(id)->light) {
        return false;
    }
    auto& node = entities_.at(id);
    node.meshRenderer = component;
    node.primitive = PrimitiveKind::Empty;
    node.editableMesh = 0;
    return true;
}
bool Scene::isVisible(EntityId id) const {
    if (find(id) == nullptr) {
        return false;
    }
    for (const SceneNode* node = find(id); node != nullptr; node = find(node->parent)) {
        if (!node->visible) {
            return false;
        }
        for (const auto& collection : collections_) {
            if (!collection.visible && collection.members.contains(node->id))
                return false;
        }
    }
    return true;
}
bool Scene::setSurface(EntityId id, const SurfaceStyle& surface) {
    if (!find(id) || !surface.isValid()) {
        return false;
    }
    entities_.at(id).surface = surface;
    return true;
}
bool Scene::setLighting(const Lighting& lighting) {
    if (!lighting.isValid()) {
        return false;
    }
    lighting_ = lighting;
    return true;
}
bool Scene::setCamera(EntityId id, const CameraComponent& camera) {
    const auto* node = find(id);
    if (!node || !camera.isValid() || node->light || node->meshRenderer ||
        node->editableMesh != 0 || node->primitive != PrimitiveKind::Empty) {
        return false;
    }
    entities_.at(id).camera = camera;
    return true;
}
bool Scene::setLight(EntityId id, const LightComponent& light) {
    const auto* node = find(id);
    if (!node || !light.isValid() || node->camera || node->meshRenderer ||
        node->editableMesh != 0 || node->primitive != PrimitiveKind::Empty) {
        return false;
    }
    entities_.at(id).light = light;
    return true;
}
glm::quat Scene::worldRotation(EntityId id) const {
    glm::quat rotation(1, 0, 0, 0);
    for (const auto* node = find(id); node; node = find(node->parent)) {
        rotation = node->transform.rotation * rotation;
    }
    return glm::normalize(rotation);
}
std::optional<glm::mat4> Scene::cameraViewProjection(EntityId id, float aspect) const {
    const auto* node = find(id);
    if (!node || !node->camera || !isVisible(id) || !std::isfinite(aspect) || aspect <= 0) {
        return std::nullopt;
    }
    const auto& camera = *node->camera;
    const auto rotation = worldRotation(id);
    // 直接构建刚体逆变换，支持垂直观察与 roll，不受父级非均匀缩放影响。
    const auto view = glm::mat4_cast(glm::conjugate(rotation)) *
                      glm::translate(glm::mat4(1), -glm::vec3(worldMatrix(id)[3]));
    return glm::perspective(glm::radians(camera.fieldOfView), aspect, camera.nearPlane,
                            camera.farPlane) *
           view;
}
Lighting Scene::effectiveLighting() const {
    Lighting result = lighting_;
    const SceneNode* active = nullptr;
    bool hasLight = false;
    for (const auto& [id, node] : entities_) {
        if (node.light) {
            hasLight = true;
            if (isVisible(id) && (!active || id < active->id)) {
                active = &node;
            }
        }
    }
    if (active) {
        result.direction = worldRotation(active->id) * glm::vec3(0, 0, 1);
        result.color = active->light->color;
        result.intensity = active->light->intensity;
    } else if (hasLight) {
        result.intensity = 0;
    }
    return result;
}
std::vector<EntityId> Scene::roots() const {
    std::vector<EntityId> result;
    for (const auto& [id, node] : entities_) {
        if (node.parent == kInvalidEntity) {
            result.push_back(id);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}
std::vector<SceneNode> Scene::nodes() const {
    std::vector<SceneNode> result;
    for (const auto root : roots()) {
        const auto snapshot = snapshotSubtree(root);
        result.insert(result.end(), snapshot.nodes_.begin(), snapshot.nodes_.end());
    }
    return result;
}
bool Scene::replaceNodes(const std::vector<SceneNode>& nodes,
                         const std::vector<EditableMeshResource>& meshes,
                         const std::vector<SceneCollection>& collections,
                         const SceneAnimation& animation) {
    std::vector<EntityId> entityIds;
    entityIds.reserve(nodes.size());
    for (const auto& node : nodes)
        entityIds.push_back(node.id);
    if (!validateSceneAnimation(animation, entityIds).isValid())
        return false;
    Scene candidate;
    candidate.animation_ = std::make_shared<const SceneAnimation>(animation);
    candidate.nextCollectionId_ = nextCollectionId_;
    candidate.nextMeshRevision_ = nextMeshRevision_;
    candidate.nextMeshId_ = nextMeshId_;
    for (const auto& mesh : meshes) {
        if (mesh.id == 0 || mesh.id == std::numeric_limits<MeshId>::max() ||
            candidate.editableMeshes_.contains(mesh.id)) {
            return false;
        }
        for (const auto& face : mesh.source.faces) {
            if (face.material != 0) {
                return false;
            }
        }
        std::string error;
        auto content = prepareContent(mesh.source, mesh.mirror, mesh.subdivision, error);
        if (!content) {
            return false;
        }
        const auto revision = candidate.nextMeshRevision_++;
        candidate.editableMeshes_.emplace(
            mesh.id,
            EditableMeshRecord{std::move(content), revision, revision, revision});
        candidate.nextMeshId_ = std::max(candidate.nextMeshId_, mesh.id + 1);
    }
    std::unordered_set<MeshId> boundMeshes;
    for (auto node : nodes) {
        if (node.id == 0 || node.id == std::numeric_limits<EntityId>::max() || node.name.empty() ||
            !node.transform.isValid() || !node.surface.isValid() || candidate.find(node.id)) {
            return false;
        }
        if ((node.camera && !node.camera->isValid()) || (node.light && !node.light->isValid()) ||
            (node.camera && node.light) ||
            ((node.camera || node.light) && (node.meshRenderer || node.editableMesh != 0 ||
                                             node.primitive != PrimitiveKind::Empty))) {
            return false;
        }
        if (node.editableMesh != 0 &&
            (node.meshRenderer || node.primitive != PrimitiveKind::Empty ||
             !candidate.editableMeshes_.contains(node.editableMesh) ||
             !boundMeshes.insert(node.editableMesh).second)) {
            return false;
        }
        node.children.clear();
        node.transform.rotation = glm::normalize(node.transform.rotation);
        candidate.nextId_ = std::max(candidate.nextId_, node.id + 1);
        candidate.entities_.emplace(node.id, std::move(node));
    }
    for (const auto& node : nodes) {
        EntityId ancestor = node.parent;
        std::size_t depth = 0;
        while (ancestor != 0) {
            const auto* parent = candidate.find(ancestor);
            if (!parent || ancestor == node.id || ++depth > nodes.size()) {
                return false;
            }
            ancestor = parent->parent;
        }
        if (node.parent != 0) {
            candidate.entities_.at(node.parent).children.push_back(node.id);
        }
    }
    if (boundMeshes.size() != meshes.size()) {
        return false;
    }
    if (!candidate.replaceCollections(collections)) {
        return false;
    }
    editableMeshes_ = std::move(candidate.editableMeshes_);
    nextMeshId_ = candidate.nextMeshId_;
    nextMeshRevision_ = candidate.nextMeshRevision_;
    entities_ = std::move(candidate.entities_);
    nextId_ = candidate.nextId_;
    collections_ = std::move(candidate.collections_);
    nextCollectionId_ = candidate.nextCollectionId_;
    animation_ = std::move(candidate.animation_);
    geometrySnapshotOrigin_ = std::move(candidate.geometrySnapshotOrigin_);
    return true;
}
} // namespace mini3d::core
