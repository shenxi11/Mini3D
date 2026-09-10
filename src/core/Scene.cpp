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

#include <algorithm>
#include <functional>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>
#include <utility>
namespace mini3d::core {
Scene::SubtreeSnapshot Scene::snapshotSubtree(EntityId id) const {
    SubtreeSnapshot snapshot;
    const auto* root = find(id);
    if (!root) {
        return snapshot;
    }
    if (root->parent != kInvalidEntity) {
        const auto& siblings = find(root->parent)->children;
        snapshot.siblingIndex_ = static_cast<std::size_t>(
            std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
    }
    std::function<void(EntityId)> capture = [&](EntityId current) {
        const auto* node = find(current);
        snapshot.nodes_.push_back(*node);
        for (const auto child : node->children) {
            capture(child);
        }
    };
    capture(id);
    return snapshot;
}
bool Scene::restoreSubtree(const SubtreeSnapshot& snapshot) {
    if (snapshot.nodes_.empty()) {
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
    for (const auto& node : snapshot.nodes_) {
        entities_.emplace(node.id, node);
        nextId_ = std::max(nextId_, node.id + 1);
    }
    if (root.parent != kInvalidEntity) {
        auto& siblings = entities_.at(root.parent).children;
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(snapshot.siblingIndex_),
                        root.id);
    }
    return true;
}
EntityId Scene::duplicateSubtree(EntityId id) {
    const auto snapshot = snapshotSubtree(id);
    if (snapshot.nodes_.empty()) {
        return kInvalidEntity;
    }
    std::unordered_map<EntityId, EntityId> copies;
    for (const auto& source : snapshot.nodes_) {
        const auto parent = source.id == id ? source.parent : copies.at(source.parent);
        const auto copyId = createEntity(source.id == id ? source.name + " Copy" : source.name,
                                         parent, source.primitive);
        auto& node = entities_.at(copyId);
        node.transform = source.transform;
        node.visible = source.visible;
        node.meshRenderer = source.meshRenderer;
        node.surface = source.surface;
        node.camera = source.camera;
        node.light = source.light;
        copies.emplace(source.id, copyId);
    }
    return copies.at(id);
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
    const SceneNode* node = find(id);
    if (node == nullptr) {
        return false;
    }
    const auto children = node->children;
    for (EntityId child : children) {
        removeEntity(child);
    }
    if (node->parent != kInvalidEntity) {
        std::erase(entities_.at(node->parent).children, id);
    }
    entities_.erase(id);
    return true;
}
bool Scene::setParent(EntityId child, EntityId parent) {
    if (find(child) == nullptr || (parent != kInvalidEntity && find(parent) == nullptr)) {
        return false;
    }
    for (EntityId ancestor = parent; ancestor != kInvalidEntity;
         ancestor = find(ancestor)->parent) {
        if (ancestor == child) {
            return false;
        }
    }
    SceneNode& node = entities_.at(child);
    if (node.parent == parent) {
        return true;
    }
    if (node.parent != kInvalidEntity) {
        std::erase(entities_.at(node.parent).children, child);
    }
    node.parent = parent;
    if (parent != kInvalidEntity) {
        entities_.at(parent).children.push_back(child);
    }
    return true;
}
bool Scene::renameEntity(EntityId id, std::string name) {
    if (find(id) == nullptr || name.empty()) {
        return false;
    }
    entities_.at(id).name = std::move(name);
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
        node->primitive != PrimitiveKind::Empty) {
        return false;
    }
    entities_.at(id).camera = camera;
    return true;
}
bool Scene::setLight(EntityId id, const LightComponent& light) {
    const auto* node = find(id);
    if (!node || !light.isValid() || node->camera || node->meshRenderer ||
        node->primitive != PrimitiveKind::Empty) {
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
bool Scene::replaceNodes(const std::vector<SceneNode>& nodes) {
    Scene candidate;
    for (auto node : nodes) {
        if (node.id == 0 || node.id == std::numeric_limits<EntityId>::max() || node.name.empty() ||
            !node.transform.isValid() || !node.surface.isValid() || candidate.find(node.id)) {
            return false;
        }
        if ((node.camera && !node.camera->isValid()) || (node.light && !node.light->isValid()) ||
            (node.camera && node.light) ||
            ((node.camera || node.light) &&
             (node.meshRenderer || node.primitive != PrimitiveKind::Empty))) {
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
    entities_ = std::move(candidate.entities_);
    nextId_ = candidate.nextId_;
    return true;
}
} // namespace mini3d::core
