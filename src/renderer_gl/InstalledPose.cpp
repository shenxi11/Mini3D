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
} // namespace

std::shared_ptr<const PoseGeometry>
makePoseGeometry(const core::Scene& scene, std::shared_ptr<const assets::AssetManager> assets,
                 const core::ViewportVisibility& visibility,
                 std::span<const core::AnimationPoseInput> inputs) {
    if (!assets || inputs.size() > core::kAnimationMaximumPoseNodes)
        return nullptr;
    auto result = std::make_shared<PoseGeometry>();
    result->assets = std::move(assets);
    result->visibility = visibility;
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
        result->entries.emplace(input.entity, std::move(entry));
    }
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
        if (entry.camera) {
            if (!deviceCameraView(*entry.camera, node.world, node.rigidWorldRotation,
                                  editorCamera.aspectRatio()))
                return reject(QStringLiteral("相机 %1 的实际观察投影矩阵不可用。")
                                  .arg(node.entity));
            if (node.entity == previewCamera && entry.visible)
                hasPreviewCamera = true;
        }
        if (entry.light && !canNormalize(node.rigidWorldRotation * glm::vec3(0, 0, 1)))
            return reject(QStringLiteral("灯 %1 的实际光方向不可用。").arg(node.entity));
    }
    if (!hasPreviewCamera)
        return reject(QStringLiteral("预览相机不在当前完整可见姿态中。"));
    return true;
}

ScenePoseView::ScenePoseView(const core::Scene& scene, const InstalledPose* pose)
    : scene_(scene), pose_(pose) {}

const PoseGeometryEntry* ScenePoseView::geometryEntry(core::EntityId entity) const {
    if (!pose_ || !pose_->geometry)
        return nullptr;
    const auto found = pose_->geometry->entries.find(entity);
    return found == pose_->geometry->entries.end() ? nullptr : &found->second;
}

const core::EvaluatedPoseNode* ScenePoseView::poseNode(core::EntityId entity) const {
    return pose_ && pose_->numerics && geometryEntry(entity) ? pose_->numerics->find(entity)
                                                          : nullptr;
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
    if (!pose_->numerics || !pose_->geometry ||
        pose_->numerics->nodes.size() != pose_->geometry->entries.size())
        return std::nullopt;
    auto result = scene_.lighting();
    const PoseGeometryEntry* active = nullptr;
    const core::EvaluatedPoseNode* activeNode = nullptr;
    bool hasLight = false;
    for (const auto& node : pose_->numerics->nodes) {
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
