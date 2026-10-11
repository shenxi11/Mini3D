/*
 * 模块名: InstalledPose
 * 功能概述: 线性冻结几何与层级可见性，核对真实 float 消费结果并提供统一姿态读取。
 * 对外接口: makePoseGeometry、validatePoseGeometry、ScenePoseView。
 * 依赖关系: Core、CPU Assets、RayCaster、EditorCamera、GLM、Qt Core。
 * 输入输出: 基础几何及完整数值姿态到不可变快照和刚性设备观察结果。
 * 异常与错误: 候选缺节点或消费数值溢出时拒绝，不返回部分基础回退。
 * 维护说明: 不递归、不逐节点重走父链；验证与 tick 不重新扫描几何点。
 */
#include "InstalledPose.h"

#include "RayCaster.h"
#include "PrimitiveFactory.h"

#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <unordered_set>

namespace mini3d::renderer_gl {
namespace {
bool isFinite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool isFinite(const glm::vec4& value) {
    return isFinite(glm::vec3(value)) && std::isfinite(value.w);
}

bool isFinite(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column)
        if (!isFinite(matrix[column]))
            return false;
    return true;
}

bool canNormalize(const glm::vec3& direction) {
    const auto length = glm::length(direction);
    return isFinite(direction) && std::isfinite(length) && length > 0 &&
           isFinite(glm::normalize(direction));
}

std::optional<PoseCameraView> deviceCameraView(const core::CameraComponent& camera,
                                              const glm::mat4& world,
                                              const glm::quat& rotation, float aspect) {
    if (!camera.isValid() || !std::isfinite(aspect) || aspect <= 0)
        return std::nullopt;
    PoseCameraView result;
    result.position = glm::vec3(world[3]);
    result.forward = rotation * glm::vec3(0, 0, -1);
    result.up = rotation * glm::vec3(0, 1, 0);
    result.target = result.position + result.forward;
    result.viewMatrix = glm::mat4_cast(glm::conjugate(rotation)) *
                        glm::translate(glm::mat4(1), -result.position);
    result.projectionMatrix = glm::perspective(glm::radians(camera.fieldOfView), aspect,
                                             camera.nearPlane, camera.farPlane);
    if (!isFinite(result.position) || !isFinite(result.target) ||
        !canNormalize(result.forward) || !canNormalize(result.up) ||
        !isFinite(result.viewMatrix) || !isFinite(result.projectionMatrix) ||
        !isFinite(result.projectionMatrix * result.viewMatrix))
        return std::nullopt;
    return result;
}

core::Aabb contentBounds(core::EntityId entity, const core::EditableMeshContent& content,
                        const core::ViewportVisibility& visibility) {
    core::Aabb result;
    const bool hasMask = entity == visibility.editedEntity && visibility.hasHiddenElements();
    if ((content.mirrorEvaluation || content.subdivisionEvaluation) && !hasMask) {
        for (const auto& vertex : content.evaluatedMesh().vertices)
            result.expand(vertex.position);
    } else if (content.mirrorEvaluation || content.subdivisionEvaluation) {
        const auto& mesh = content.displayedDerived().mesh;
        for (std::size_t triangle = 0; triangle < mesh.indices.size() / 3; ++triangle) {
            if (!visibility.isTriangleVisible(entity, content, triangle))
                continue;
            for (std::size_t corner = 0; corner < 3; ++corner)
                result.expand(mesh.vertices[mesh.indices[triangle * 3 + corner]].position);
        }
    } else {
        for (const auto& vertex : content.source.vertices)
            if (!hasMask || visibility.isVertexVisible(content.source, vertex.id))
                result.expand(vertex.position);
    }
    return result;
}
std::shared_ptr<const core::MeshData> primitiveMesh(core::PrimitiveKind primitive) {
    switch (primitive) {
    case core::PrimitiveKind::Cube: {
        static const auto mesh = std::make_shared<const core::MeshData>(PrimitiveFactory::createCube());
        return mesh;
    }
    case core::PrimitiveKind::Sphere: {
        static const auto mesh = std::make_shared<const core::MeshData>(PrimitiveFactory::createSphere());
        return mesh;
    }
    case core::PrimitiveKind::Plane: {
        static const auto mesh = std::make_shared<const core::MeshData>(PrimitiveFactory::createPlane());
        return mesh;
    }
    case core::PrimitiveKind::Empty: return {};
    }
    return {};
}
} // namespace

bool freezePoseGeometryEntry(const core::SceneNode& node, const assets::AssetManager& assets,
                             PoseGeometryEntry& entry) {
    entry.primitive = node.primitive;
    entry.editableMesh = node.editableMesh;
    entry.legacySurface = node.surface;
    entry.sun = node.sun;
    entry.materialSlots = node.materialSlots;
    entry.hasLegacyExternalResources = node.meshRenderer.has_value();
    if (node.editableMesh != 0) {
        if (!entry.content) return false;
        entry.cpuMesh = std::shared_ptr<const core::MeshData>(entry.content, &entry.content->displayedDerived().mesh);
    } else if (node.meshRenderer) {
        const auto* mesh = assets.mesh(node.meshRenderer->mesh);
        if (!mesh) return false;
        entry.cpuMesh = std::make_shared<const core::MeshData>(mesh->data);
        const auto materialId = node.meshRenderer->material ? node.meshRenderer->material : mesh->material;
        if (materialId) {
            const auto* material = assets.material(materialId);
            if (!material) return false;
            entry.legacyMaterial = std::make_shared<const assets::MaterialAsset>(*material);
            if (material->baseColorTexture) {
                const auto* texture = assets.texture(material->baseColorTexture);
                if (!texture) return false;
                entry.legacyTexture = std::make_shared<const assets::TextureAsset>(*texture);
            }
        }
    } else if (node.primitive != core::PrimitiveKind::Empty) {
        entry.cpuMesh = primitiveMesh(node.primitive);
        if (!entry.cpuMesh) return false;
    }
    if (entry.cpuMesh) entry.fullLocalBounds = entry.cpuMesh->bounds();
    return !entry.hasGeometry || (entry.cpuMesh && entry.fullLocalBounds.isValid());
}

bool freezePoseSceneData(PoseGeometry& geometry) {
    if (geometry.documentUuid.isNil() || !geometry.materials) return false;
    auto authors = std::make_shared<core::SceneAuthoringSnapshot>();
    auto meshes = std::make_shared<core::SceneGeometrySnapshot>();
    authors->documentUuid = geometry.documentUuid;
    authors->materials = geometry.materials;
    authors->legacyLighting = geometry.legacyLighting;
    bool completeResources = true;
    for (const auto& [entity, entry] : geometry.entries) {
        authors->nodes.emplace(entity, core::SceneNodeAuthoring{entity, entry.visible, entry.legacySurface,
            entry.camera, entry.light, entry.sun, entry.materialSlots});
        core::SceneGeometryEntry mesh;
        mesh.entity = entity;
        mesh.primitive = entry.primitive;
        mesh.editableMesh = entry.editableMesh;
        mesh.contentVersion = entry.content ? entry.evaluationRevision : (entry.hasGeometry ? 1 : 0);
        mesh.hasGeometry = entry.hasGeometry;
        mesh.mesh = entry.cpuMesh;
        mesh.editableContent = entry.content;
        mesh.localBounds = entry.fullLocalBounds;
        if (entry.hasGeometry && (!mesh.mesh || !mesh.localBounds.isValid())) return false;
        meshes->entries.emplace(entity, std::move(mesh));
        completeResources = completeResources && !entry.hasLegacyExternalResources;
    }
    geometry.authors = std::move(authors);
    geometry.sceneGeometry = std::move(meshes);
    geometry.hasCompleteResourceIdentity = completeResources;
    return true;
}

void InstalledPose::prepareSceneGeometryBinding() {
    if (!sceneState) return;
    sceneContentToBind_ = std::make_shared<core::EvaluatedSceneContent>(*sceneState->content);
    sceneGeometryToBind_ = std::make_shared<core::SceneGeometrySnapshot>(*sceneState->content->geometry);
    sceneContentToBind_->geometry = sceneGeometryToBind_;
    sceneState->content = sceneContentToBind_;
}

void InstalledPose::bindPreparedSceneGeometry(const core::Scene& scene) {
    if (!sceneContentToBind_) return;
    for (auto& [entity, entry] : sceneGeometryToBind_->entries) {
        if (!entry.editableContent) continue;
        const auto* node = scene.find(entity);
        const auto* record = node ? scene.editableMesh(node->editableMesh) : nullptr;
        Q_ASSERT(node && node->editableMesh == entry.editableMesh && record && record->content == entry.editableContent);
        entry.contentVersion = record->evaluationRevision;
        auto& stage = sceneContentToBind_->geometryStages.at(entity);
        stage.identity.contentVersion = entry.contentVersion;
        stage.geometry.contentVersion = entry.contentVersion;
    }
    sceneContentToBind_.reset();
    sceneGeometryToBind_.reset();
}

std::shared_ptr<const PoseGeometry>
makePoseGeometry(const core::Scene& scene, std::shared_ptr<const assets::AssetManager> assets,
                 const core::ViewportVisibility& visibility,
                 std::span<const core::AnimationPoseInput> inputs) {
    if (!assets || inputs.size() > core::kAnimationMaximumPoseNodes)
        return nullptr;
    auto result = std::make_shared<PoseGeometry>();
    result->assets = std::move(assets);
    result->visibility = visibility;
    result->documentUuid = scene.documentUuid();
    result->materials = scene.materialDataOwner();
    result->legacyLighting = scene.lighting();
    result->entries.reserve(inputs.size());
    std::unordered_set<core::EntityId> collectionHidden;
    for (const auto& collection : scene.collections())
        if (!collection.visible)
            collectionHidden.insert(collection.members.begin(), collection.members.end());
    struct VisibilityState {
        bool ancestorsVisible;
        bool inLocal;
    };
    std::unordered_map<core::EntityId, VisibilityState> states;
    states.reserve(inputs.size());
    for (const auto& input : inputs) {
        const auto* node = scene.find(input.entity);
        if (!node || result->entries.contains(input.entity))
            return nullptr;
        VisibilityState state{true, visibility.localRoot == 0};
        if (input.parent != core::kInvalidEntity) {
            const auto parent = states.find(input.parent);
            if (parent == states.end())
                return nullptr;
            state = parent->second;
        }
        state.ancestorsVisible = state.ancestorsVisible && node->visible &&
                                 !collectionHidden.contains(input.entity) &&
                                 !visibility.hiddenObjects.contains(input.entity);
        state.inLocal = state.inLocal || input.entity == visibility.localRoot;
        states.emplace(input.entity, state);
        PoseGeometryEntry entry;
        entry.entity = input.entity;
        entry.parent = input.parent;
        entry.visible = state.ancestorsVisible && state.inLocal;
        entry.hasGeometry = node->editableMesh != 0 || node->meshRenderer.has_value() ||
                            node->primitive != core::PrimitiveKind::Empty;
        entry.camera = node->camera;
        entry.light = node->light;
        if (node->editableMesh != 0) {
            const auto* record = scene.editableMesh(node->editableMesh);
            if (!record || !record->content)
                return nullptr;
            entry.content = record->content;
            entry.evaluationRevision = record->evaluationRevision;
            entry.localBounds = contentBounds(input.entity, *entry.content, visibility);
        } else {
            entry.localBounds = RayCaster::localBounds(*node, *result->assets);
        }
        if (!freezePoseGeometryEntry(*node, *result->assets, entry)) return nullptr;
        result->entries.emplace(input.entity, std::move(entry));
    }
    if (!freezePoseSceneData(*result)) return nullptr;
    return result;
}

bool validatePoseGeometry(const core::EvaluatedPose& numerics, const PoseGeometry& geometry,
                           const EditorCamera& editorCamera, core::EntityId previewCamera,
                           QString& error) {
    error.clear();
    const auto reject = [&](const QString& message) {
        error = message;
        return false;
    };
    if (!geometry.assets || numerics.nodes.size() != geometry.entries.size())
        return reject(QStringLiteral("显示姿态与几何快照不完整。"));
    const auto editorView = editorCamera.viewMatrix();
    const auto editorProjection = editorCamera.projectionMatrix();
    const auto editorBasis = glm::inverse(editorView);
    if (!editorCamera.state().isValid() || !isFinite(editorCamera.position()) ||
        !isFinite(editorCamera.target()) || !isFinite(editorView) ||
        !isFinite(editorProjection) || !isFinite(editorProjection * editorView) ||
        !isFinite(editorBasis) || !canNormalize(glm::vec3(editorBasis[1])) ||
        !canNormalize(glm::vec3(editorBasis[2])))
        return reject(QStringLiteral("编辑相机的实际观察矩阵不可用。"));
    bool hasPreviewCamera = previewCamera == core::kInvalidEntity;
    for (std::size_t index = 0; index < numerics.nodes.size(); ++index) {
        const auto& node = numerics.nodes[index];
        const auto found = geometry.entries.find(node.entity);
        const auto numericIndex = numerics.indices.find(node.entity);
        if (found == geometry.entries.end() || found->second.entity != node.entity ||
            numericIndex == numerics.indices.end() || numericIndex->second != index)
            return reject(QStringLiteral("显示姿态缺少对象 %1 的对应快照。").arg(node.entity));
        const auto& entry = found->second;
        if (entry.parent != core::kInvalidEntity) {
            const auto parent = numerics.indices.find(entry.parent);
            if (parent == numerics.indices.end() || parent->second >= index)
                return reject(QStringLiteral("对象 %1 的显示父级顺序无效。").arg(node.entity));
        }
        if (!isFinite(node.world) || !isFinite(node.worldInverse))
            return reject(QStringLiteral("对象 %1 的实际世界矩阵不可用。").arg(node.entity));
        if (entry.localBounds.isValid()) {
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 point((corner & 1) ? entry.localBounds.maximum.x
                                                  : entry.localBounds.minimum.x,
                                      (corner & 2) ? entry.localBounds.maximum.y
                                                  : entry.localBounds.minimum.y,
                                      (corner & 4) ? entry.localBounds.maximum.z
                                                  : entry.localBounds.minimum.z);
                if (!isFinite(node.world * glm::vec4(point, 1)))
                    return reject(QStringLiteral("对象 %1 的实际包围盒八角溢出。")
                                      .arg(node.entity));
            }
        }
        if (entry.cpuMesh) {
            const auto& bounds = entry.fullLocalBounds;
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 point((corner & 1) ? bounds.maximum.x : bounds.minimum.x,
                                      (corner & 2) ? bounds.maximum.y : bounds.minimum.y,
                                      (corner & 4) ? bounds.maximum.z : bounds.minimum.z);
                if (!isFinite(node.world * glm::vec4(point, 1)))
                    return reject(QStringLiteral("对象 %1 的完整几何八角溢出。").arg(node.entity));
            }
        }
        if (entry.camera) {
            if (!deviceCameraView(*entry.camera, node.world, node.rigidWorldRotation,
                                  editorCamera.aspectRatio()))
                return reject(QStringLiteral("相机 %1 的实际观察投影矩阵不可用。")
                                  .arg(node.entity));
            if (node.entity == previewCamera && entry.visible)
                hasPreviewCamera = true;
        }
        if ((entry.light || entry.sun) && !canNormalize(node.rigidWorldRotation * glm::vec3(0, 0, 1)))
            return reject(QStringLiteral("灯 %1 的实际光方向不可用。").arg(node.entity));
    }
    if (!hasPreviewCamera)
        return reject(QStringLiteral("预览相机不在当前完整可见姿态中。"));
    return true;
}

std::optional<PoseCameraView> evaluatedStateCameraView(
    const core::EvaluatedSceneState& state, const PoseGeometry& geometry,
    const EditorCamera& editorCamera, core::EntityId camera, float aspect, QString& error) {
    if (!state.content || !geometry.hasCompleteResourceIdentity ||
        state.content->authors != geometry.authors || state.content->geometry != geometry.sceneGeometry ||
        geometry.visibility.hasHiddenElements() ||
        state.content->geometryStages.size() != state.content->nodes.size()) {
        error = QStringLiteral("输出相机缺少同一完整 state/几何资源。");
        return std::nullopt;
    }
    for (const auto& node : state.content->nodes) {
        const auto found = geometry.entries.find(node.entity);
        const auto stage = state.content->geometryStages.find(node.entity);
        if (found == geometry.entries.end() || stage == state.content->geometryStages.end() ||
            stage->second.identity.worldAffine != node.worldAffine ||
            found->second.parent != node.parent || found->second.visible != node.visible ||
            found->second.camera != node.camera || stage->second.geometry.mesh != found->second.cpuMesh) {
            error = QStringLiteral("输出 state 的节点、设备或几何 stage 不一致。");
            return std::nullopt;
        }
    }
    auto checked = core::adaptSceneStateToPose(state);
    auto deviceView = editorCamera;
    deviceView.setViewportSize(1280, 720);
    if (!checked.pose || !validatePoseGeometry(*checked.pose, geometry, deviceView, camera, error)) {
        if (error.isEmpty()) error = QStringLiteral("输出 state 不能形成 checked float 设备姿态。");
        return std::nullopt;
    }
    const auto* node = state.find(camera);
    const auto* device = checked.pose->find(camera);
    const auto result = node && node->visible && node->camera && device
        ? deviceCameraView(*node->camera, device->world, device->rigidWorldRotation, aspect)
        : std::nullopt;
    if (!result) error = QStringLiteral("输出相机不存在、不可见或实际设备矩阵不可消费。");
    else error.clear();
    return result;
}

ScenePoseView::ScenePoseView(const core::Scene& scene, const InstalledPose* pose)
    : scene_(scene), pose_(pose) {
    if (!pose_)
        return;
    complete_ = pose_->numerics && pose_->geometry && pose_->geometry->assets &&
                pose_->numerics->nodes.size() == pose_->geometry->entries.size();
    if (!complete_ || pose_->consumerDomain == PoseConsumerDomain::LegacyTrsV1)
        return;
    complete_ = false;
    if (pose_->coverage != PoseStateCoverage::CompleteSceneJ1 || !pose_->sceneState ||
        !pose_->sceneNumerics || !pose_->sceneState->content ||
        !pose_->geometry->hasCompleteResourceIdentity || pose_->geometry->visibility.hasHiddenElements())
        return;
    const auto& state = *pose_->sceneState;
    const auto& source = state.source;
    const auto* live = std::get_if<core::LiveSourceHandle>(&source.sourceHandle);
    if (!core::isSourceIdentityValid(source) || !live || source.evaluationId == 0 ||
        source.evaluationProfile != core::EvaluationProfile::JointPreviewJ1 ||
        live->instanceId != pose_->identity.instanceId.toStdString() ||
        live->documentId != pose_->identity.documentId.toStdString() ||
        source.documentUuid != pose_->geometry->documentUuid ||
        source.contentRevision != pose_->identity.sourceRevision ||
        source.frameTime != pose_->identity.frame ||
        source.evaluationId != pose_->identity.evaluationId ||
        pose_->sceneNumerics->frame != source.frameTime ||
        ((source.sourceKind == core::SourceKind::LiveDraft) !=
         (pose_->identity.mode == AnimationMode::PoseDraft)) ||
        state.content->nodes.size() != pose_->geometry->entries.size() ||
        state.content->geometryStages.size() != state.content->nodes.size() ||
        pose_->sceneNumerics->nodes.size() != state.content->nodes.size() ||
        state.content->geometry != pose_->geometry->sceneGeometry ||
        state.content->authors != pose_->geometry->authors)
        return;
    for (const auto& node : state.content->nodes) {
        const auto found = pose_->geometry->entries.find(node.entity);
        const auto stage = state.content->geometryStages.find(node.entity);
        const auto* checked = pose_->sceneNumerics->find(node.entity);
        if (found == pose_->geometry->entries.end() || stage == state.content->geometryStages.end() ||
            state.find(node.entity) != &node || !checked)
            return;
        const auto& entry = found->second;
        const auto& identity = stage->second.identity;
        const auto& geometry = stage->second.geometry;
        if (entry.entity != node.entity || entry.parent != node.parent || entry.visible != node.visible ||
            identity.entity != node.entity || geometry.entity != node.entity ||
            identity.worldAffine != node.worldAffine || identity.contentVersion != geometry.contentVersion ||
            identity.primitive != geometry.primitive || identity.mesh != geometry.editableMesh ||
            geometry.mesh != entry.cpuMesh || geometry.editableContent != entry.content ||
            geometry.hasGeometry != entry.hasGeometry || geometry.primitive != entry.primitive ||
            geometry.editableMesh != entry.editableMesh ||
            (entry.content && geometry.contentVersion != entry.evaluationRevision) ||
            geometry.localBounds.minimum != entry.fullLocalBounds.minimum ||
            geometry.localBounds.maximum != entry.fullLocalBounds.maximum ||
            node.camera != entry.camera || node.legacyLight != entry.light ||
            node.materialSlots != entry.materialSlots ||
            checked->world != glm::mat4(node.worldAffine) ||
            checked->worldInverse != glm::mat4(node.worldInverse) ||
            checked->worldNormal != glm::mat3(node.worldNormal) ||
            checked->rigidWorldRotation !=
                glm::quat(node.rigidWorldRotation) / glm::length(glm::quat(node.rigidWorldRotation)))
            return;
    }
    complete_ = true;
}

bool ScenePoseView::isComplete() const { return complete_; }

const core::EvaluatedPose* ScenePoseView::numerics() const {
    if (!pose_ || !complete_)
        return nullptr;
    return pose_->consumerDomain == PoseConsumerDomain::JointJ1
               ? pose_->sceneNumerics.get() : pose_->numerics.get();
}

std::optional<core::Aabb> ScenePoseView::localBounds(core::EntityId entity) const {
    const auto* entry = geometryEntry(entity);
    if (!entry || !poseNode(entity))
        return std::nullopt;
    if (pose_->consumerDomain == PoseConsumerDomain::JointJ1)
        return pose_->sceneState->content->geometryStages.at(entity).geometry.localBounds;
    return entry->localBounds;
}

const PoseGeometryEntry* ScenePoseView::geometryEntry(core::EntityId entity) const {
    if (!pose_ || !complete_ || !pose_->geometry)
        return nullptr;
    const auto found = pose_->geometry->entries.find(entity);
    return found == pose_->geometry->entries.end() ? nullptr : &found->second;
}

const core::EvaluatedPoseNode* ScenePoseView::poseNode(core::EntityId entity) const {
    const auto* values = numerics();
    return values && geometryEntry(entity) ? values->find(entity) : nullptr;
}

bool ScenePoseView::isVisible(core::EntityId entity) const {
    if (!pose_)
        return scene_.isVisible(entity);
    const auto* entry = geometryEntry(entity);
    return entry && entry->visible && poseNode(entity);
}

std::optional<glm::mat4> ScenePoseView::worldMatrix(core::EntityId entity) const {
    if (!pose_)
        return scene_.find(entity) ? std::optional(scene_.worldMatrix(entity)) : std::nullopt;
    const auto* node = poseNode(entity);
    return node ? std::optional(node->world) : std::nullopt;
}

std::optional<glm::mat4> ScenePoseView::worldInverse(core::EntityId entity) const {
    if (!pose_)
        return scene_.find(entity) ? std::optional(glm::inverse(scene_.worldMatrix(entity)))
                                   : std::nullopt;
    const auto* node = poseNode(entity);
    return node ? std::optional(node->worldInverse) : std::nullopt;
}

std::optional<glm::quat> ScenePoseView::worldRotation(core::EntityId entity) const {
    if (!pose_)
        return scene_.find(entity) ? std::optional(scene_.worldRotation(entity)) : std::nullopt;
    const auto* node = poseNode(entity);
    return node ? std::optional(node->rigidWorldRotation) : std::nullopt;
}

std::optional<core::Lighting> ScenePoseView::lighting() const {
    if (!pose_)
        return scene_.effectiveLighting();
    const auto* values = numerics();
    if (!values)
        return std::nullopt;
    auto result = pose_->geometry->legacyLighting;
    const PoseGeometryEntry* active = nullptr;
    const core::EvaluatedPoseNode* activeNode = nullptr;
    bool hasLight = false;
    for (const auto& node : values->nodes) {
        const auto* entry = geometryEntry(node.entity);
        if (!entry)
            return std::nullopt;
        if (entry->light) {
            hasLight = true;
            if (entry->visible && (!active || node.entity < active->entity)) {
                active = entry;
                activeNode = &node;
            }
        }
    }
    if (active) {
        result.direction = activeNode->rigidWorldRotation * glm::vec3(0, 0, 1);
        result.color = active->light->color;
        result.intensity = active->light->intensity;
    } else if (hasLight) {
        result.intensity = 0;
    }
    return result;
}

std::optional<PoseCameraView> ScenePoseView::cameraView(core::EntityId entity, float aspect) const {
    if (!isVisible(entity))
        return std::nullopt;
    if (pose_) {
        const auto* entry = geometryEntry(entity);
        const auto* node = poseNode(entity);
        return entry && entry->camera && node
                   ? deviceCameraView(*entry->camera, node->world, node->rigidWorldRotation, aspect)
                   : std::nullopt;
    }
    const auto* node = scene_.find(entity);
    return node && node->camera
               ? deviceCameraView(*node->camera, scene_.worldMatrix(entity),
                                  scene_.worldRotation(entity), aspect)
               : std::nullopt;
}

} // namespace mini3d::renderer_gl
