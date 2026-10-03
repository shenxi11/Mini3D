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
#include <functional>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>
#include <unordered_set>
#include <utility>
namespace mini3d::core {
namespace {
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
    if (root->parent != kInvalidEntity) {
        const auto& siblings = find(root->parent)->children;
        snapshot.siblingIndex_ = static_cast<std::size_t>(
            std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
    }
    std::function<void(EntityId)> capture = [&](EntityId current) {
        const auto* node = find(current);
        snapshot.nodes_.push_back(*node);
        for (const auto& collection : collections_) {
            if (collection.members.contains(current))
                snapshot.collectionMemberships_.emplace(current, collection.id);
        }
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
    for (const auto& node : snapshot.nodes_) {
        entities_.emplace(node.id, node);
        nextId_ = std::max(nextId_, node.id + 1);
    }
    if (root.parent != kInvalidEntity) {
        auto& siblings = entities_.at(root.parent).children;
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(snapshot.siblingIndex_),
                        root.id);
    }
    collections_ = std::move(restoredCollections);
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
        if (source.editableMesh != 0) {
            node.editableMesh = nextMeshId_++;
            const auto revision = nextMeshRevision_++;
            editableMeshes_.emplace(
                node.editableMesh,
                EditableMeshRecord{editableMeshes_.at(source.editableMesh).content, revision,
                                   revision, revision});
        }
        copies.emplace(source.id, copyId);
    }
    for (auto& collection : collections_) {
        for (const auto& [original, collectionId] : snapshot.collectionMemberships_) {
            if (collection.id == collectionId)
                collection.members.insert(copies.at(original));
        }
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
    for (auto& collection : collections_)
        collection.members.erase(id);
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
                         const std::vector<SceneCollection>& collections) {
    Scene candidate;
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
    geometrySnapshotOrigin_ = std::move(candidate.geometrySnapshotOrigin_);
    return true;
}
} // namespace mini3d::core
