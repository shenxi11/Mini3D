/*
 * 模块名: EvaluatedSceneState
 * 功能概述: 从作者值直接计算完整双精度场景状态并提供 checked float 适配。
 * 对外接口: evaluateSceneState、adaptSceneStateToPose、不可变内容/几何快照。
 * 依赖关系: Animation、SceneNode、MeshData、GLM；无 Qt/OpenGL。
 * 输入输出: 整份作者和几何快照到单一来源的完整有效状态。
 * 异常与错误: 任一节点/绑定/必需资源失败不返回部分结果；分配异常传播。
 * 维护说明: 无轨保留基础 quaternion；全 affine 不拆 TRS；取样不分配稳定 ID。
 */
#pragma once

#include "EvaluatedPose.h"
#include "MeshData.h"
#include "SceneNode.h"

#include <map>

namespace mini3d::core {
struct EditableMeshContent;
struct SceneNodeAuthoring {
    EntityId entity = kInvalidEntity;
    bool visible = false;
    SurfaceStyle legacySurface;
    std::optional<CameraComponent> camera;
    std::optional<LightComponent> legacyLight;
    std::optional<SunComponent> sun;
    std::vector<MaterialSlotBinding> materialSlots;
};
struct SceneAuthoringSnapshot {
    DocumentUuid documentUuid;
    std::map<EntityId, SceneNodeAuthoring> nodes;
    std::shared_ptr<const SceneMaterialData> materials;
    Lighting legacyLighting;
};
/** @brief 原拓扑/实际 CPU 内容一次冻结；外部旧 runtime 资源不冒充此完整快照。 */
struct SceneGeometryEntry {
    EntityId entity = kInvalidEntity;
    PrimitiveKind primitive = PrimitiveKind::Empty;
    MeshId editableMesh = 0;
    std::uint64_t contentVersion = 0;
    bool hasGeometry = false;
    Aabb localBounds;
    std::shared_ptr<const MeshData> mesh;
    std::shared_ptr<const EditableMeshContent> editableContent;
};
struct SceneGeometrySnapshot {
    std::map<EntityId, SceneGeometryEntry> entries;
};
/** @brief 身份在所属 state.source 中；以下字段构成该来源内的无新增变形 stage。 */
struct GeometryStageIdentity {
    EntityId entity = kInvalidEntity;
    PrimitiveKind primitive = PrimitiveKind::Empty;
    MeshId mesh = 0;
    std::uint64_t contentVersion = 0;
    glm::dmat4 worldAffine{1.0};
    bool operator==(const GeometryStageIdentity&) const = default;
};
struct EvaluatedGeometryStage {
    GeometryStageIdentity identity;
    SceneGeometryEntry geometry;
};
enum class EffectiveRotationSource { BaseQuaternion, LegacyTrack, ScalarCurve };
struct EvaluatedSceneNode {
    EntityId entity = kInvalidEntity;
    EntityId parent = kInvalidEntity;
    glm::dvec3 localPosition{0.0};
    glm::dvec3 localScale{1.0};
    glm::dquat localRotation{1.0, 0.0, 0.0, 0.0};
    std::optional<glm::dvec3> rotationEulerXYZDegrees;
    EffectiveRotationSource rotationSource = EffectiveRotationSource::BaseQuaternion;
    glm::dmat4 localAffine{1.0};
    glm::dmat4 worldAffine{1.0};
    glm::dmat4 worldInverse{1.0};
    glm::dmat3 worldNormal{1.0};
    glm::dquat rigidWorldRotation{1.0, 0.0, 0.0, 0.0};
    bool visible = false;
    std::optional<CameraComponent> camera;
    std::optional<LightComponent> legacyLight;
    SurfaceStyle legacySurface;
    std::vector<MaterialSlotBinding> materialSlots;
};
struct EvaluatedMaterial {
    MaterialDefinition author;
    PbrMaterialParameters effective;
};
struct EvaluatedSun {
    EntityId entity = kInvalidEntity;
    SunComponent author;
    SunComponent effective;
    // BRDF 的 toward-light L；光实际沿局部 -Z 传播，方向为此值的相反向。
    glm::dvec3 directionToLight{0, 0, 1};
};
enum class PropertyValueSource { Base, LegacyTrack, ScalarCurve };
struct PropertyEvaluationTrace {
    PropertyAddress address;
    PropertyValueSource valueSource = PropertyValueSource::Base;
    std::optional<CurveId> curve;
    double sampledValue = 0;
    double effectiveValue = 0;
};
struct EvaluatedSceneContent {
    std::vector<EvaluatedSceneNode> nodes;
    std::unordered_map<EntityId, std::size_t> indices;
    std::map<MaterialId, EvaluatedMaterial> materials;
    std::vector<EvaluatedSun> visibleSuns;
    Lighting legacyLighting;
    std::map<EntityId, EvaluatedGeometryStage> geometryStages;
    std::vector<PropertyEvaluationTrace> propertySources;
    std::shared_ptr<const SceneAuthoringSnapshot> authors;
    std::shared_ptr<const SceneGeometrySnapshot> geometry;
};
/** @brief 复制小来源头、共享只读内容；安装后通过 const InstalledPose 发布。 */
struct EvaluatedSceneState {
    SourceIdentity source;
    std::shared_ptr<const EvaluatedSceneContent> content;
    [[nodiscard]] const EvaluatedSceneNode* find(EntityId entity) const;
};
enum class EvaluatedSceneError {
    None, InvalidSource, IncompleteAuthors, IncompleteGeometry, InvalidMaterial,
    InvalidSun, InvalidProperty, MissingResource, UnsupportedResources, InvalidNumerics
};
struct EvaluatedSceneValidation {
    EvaluatedSceneError error = EvaluatedSceneError::None;
    EntityId entity = kInvalidEntity;
    AnimationValidation numerics;
    PropertyValidation property;
    [[nodiscard]] bool isValid() const { return error == EvaluatedSceneError::None; }
};
struct EvaluatedSceneStateResult {
    std::optional<EvaluatedSceneState> state;
    EvaluatedSceneValidation validation;
};
[[nodiscard]] EvaluatedSceneStateResult evaluateSceneState(
    std::span<const AnimationPoseInput> inputs, const SceneAnimation& animation,
    std::shared_ptr<const SceneAuthoringSnapshot> authors,
    std::shared_ptr<const SceneGeometrySnapshot> geometry, const SourceIdentity& source);
/** @brief 从已计算 double 内容转换，不改变旧 evaluateAnimationPose 的数值和签名。 */
[[nodiscard]] AnimationPoseResult adaptSceneStateToPose(const EvaluatedSceneState& state);
} // namespace mini3d::core
