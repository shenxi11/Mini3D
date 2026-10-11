/*
 * 模块名: EvaluatedSceneState
 * 功能概述: 独立以 double 作者值求值父链、材质和 Sun，统一失败原子性。
 * 对外接口: EvaluatedSceneState.h 声明的求值和受控适配。
 * 依赖关系: Scene CPU 内容、GLM、标准数学/容器。
 * 输入输出: 整体作者快照到不可变完整内容；消费转换到原姿态类型。
 * 异常与错误: 数值/引用/资源错误仅返回失败，不泄露部分 state。
 * 维护说明: 父先子后迭代；stage 持有原拓扑及内容，派生矩阵不回写作者。
 */
#include "EvaluatedSceneState.h"

#include "Scene.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <set>

namespace mini3d::core {
namespace {
template <class Matrix> bool finiteMatrix(const Matrix& matrix) {
    for (typename Matrix::length_type column = 0; column < matrix.length(); ++column)
        for (typename Matrix::col_type::length_type row = 0; row < matrix[column].length(); ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    return true;
}
template <class Quaternion> std::optional<Quaternion> unitQuaternion(const Quaternion& rotation) {
    for (int component = 0; component < 4; ++component)
        if (!std::isfinite(rotation[component])) return std::nullopt;
    const auto length = glm::length(rotation);
    if (!std::isfinite(length) || length <= 0) return std::nullopt;
    return rotation / length;
}
template <class Vector> bool canNormalize(const Vector& vector) {
    const auto length = glm::length(vector);
    if (!std::isfinite(length) || length <= 0) return false;
    for (typename Vector::length_type axis = 0; axis < vector.length(); ++axis)
        if (!std::isfinite(vector[axis] / length)) return false;
    return true;
}
EvaluatedSceneStateResult fail(EvaluatedSceneError error, EntityId entity = kInvalidEntity,
                              AnimationValidation numerics = {}, PropertyValidation property = {}) {
    return {std::nullopt, {error, entity, numerics, property}};
}
AnimationChannel legacyChannel(PropertyId property) {
    return property == PropertyId::Position ? AnimationChannel::Position :
        property == PropertyId::Scale ? AnimationChannel::Scale : AnimationChannel::RotationEulerXYZDegrees;
}
template <class Matrix> bool fitsFloat(const Matrix& matrix) {
    for (typename Matrix::length_type column = 0; column < matrix.length(); ++column)
        for (typename Matrix::col_type::length_type row = 0; row < matrix[column].length(); ++row)
            if (!std::isfinite(matrix[column][row]) ||
                std::abs(matrix[column][row]) > std::numeric_limits<float>::max()) return false;
    return true;
}
} // namespace
const EvaluatedSceneNode* EvaluatedSceneState::find(EntityId entity) const {
    if (!content) return nullptr;
    const auto found = content->indices.find(entity);
    return found == content->indices.end() || found->second >= content->nodes.size()
        ? nullptr : &content->nodes[found->second];
}
EvaluatedSceneStateResult evaluateSceneState(std::span<const AnimationPoseInput> inputs,
    const SceneAnimation& animation, std::shared_ptr<const SceneAuthoringSnapshot> authors,
    std::shared_ptr<const SceneGeometrySnapshot> geometry, const SourceIdentity& source) {
    if (inputs.size() > kAnimationMaximumPoseNodes)
        return fail(EvaluatedSceneError::InvalidNumerics, 0, {AnimationError::PoseNodeLimitExceeded});
    const RationalFps fps = animation.properties.rationalFps.value_or(RationalFps{animation.settings.fps, 1});
    if (!isSourceIdentityValid(source) || source.rationalFps != fps)
        return fail(EvaluatedSceneError::InvalidSource);
    if (!authors || authors->documentUuid != source.documentUuid || !authors->materials ||
        authors->nodes.size() != inputs.size() || !authors->legacyLighting.isValid())
        return fail(EvaluatedSceneError::IncompleteAuthors);
    if (!geometry || geometry->entries.size() != inputs.size()) return fail(EvaluatedSceneError::IncompleteGeometry);
    if (!source.resourceSetIdentity.empty()) return fail(EvaluatedSceneError::UnsupportedResources);
    if (!isSceneMaterialDataValid(*authors->materials)) return fail(EvaluatedSceneError::InvalidMaterial);
    std::vector<EntityId> entities;
    std::vector<PropertyOwnerCapabilities> owners;
    entities.reserve(inputs.size());
    owners.reserve(inputs.size() + authors->materials->assets.size());
    std::set<EntityId> available;
    for (const auto& input : inputs) {
        if (input.entity == 0 || !available.insert(input.entity).second)
            return fail(EvaluatedSceneError::InvalidNumerics, input.entity, {AnimationError::DuplicateEntity, input.entity});
        if (input.parent != 0 && (!available.contains(input.parent) || input.parent == input.entity))
            return fail(EvaluatedSceneError::InvalidNumerics, input.entity, {AnimationError::InvalidParentOrder, input.entity});
        if (!input.base.isValid())
            return fail(EvaluatedSceneError::InvalidNumerics, input.entity, {AnimationError::InvalidBaseTransform, input.entity});
        const auto author = authors->nodes.find(input.entity);
        if (author == authors->nodes.end() || author->second.entity != input.entity)
            return fail(EvaluatedSceneError::IncompleteAuthors, input.entity);
        const auto& node = author->second;
        if (!node.legacySurface.isValid() || (node.camera && !node.camera->isValid()) ||
            (node.legacyLight && !node.legacyLight->isValid()) ||
            (node.camera.has_value() + node.legacyLight.has_value() + node.sun.has_value() > 1))
            return fail(EvaluatedSceneError::IncompleteAuthors, input.entity);
        if (node.sun && !isSunComponentValid(*node.sun)) return fail(EvaluatedSceneError::InvalidSun, input.entity);
        const auto mesh = geometry->entries.find(input.entity);
        if (mesh == geometry->entries.end() || mesh->second.entity != input.entity ||
            (mesh->second.hasGeometry && (!mesh->second.mesh && !mesh->second.editableContent)) ||
            (mesh->second.hasGeometry && !mesh->second.localBounds.isValid()) ||
            (!mesh->second.hasGeometry && (mesh->second.mesh || mesh->second.editableContent ||
                                          mesh->second.primitive != PrimitiveKind::Empty || mesh->second.editableMesh != 0)) ||
            (mesh->second.primitive != PrimitiveKind::Empty && mesh->second.primitive != PrimitiveKind::Cube &&
             mesh->second.primitive != PrimitiveKind::Sphere && mesh->second.primitive != PrimitiveKind::Plane) ||
            ((node.camera || node.legacyLight || node.sun) && mesh->second.hasGeometry))
            return fail(EvaluatedSceneError::IncompleteGeometry, input.entity);
        entities.push_back(input.entity);
        owners.push_back({EntityOwner{input.entity}, node.sun.has_value()});
    }
    for (const auto& [id, asset] : authors->materials->assets) {
        (void)id;
        if (!asset.material->textures.empty()) return fail(EvaluatedSceneError::MissingResource);
        owners.push_back({MaterialOwner{asset.material->id}, false});
    }
    const auto legacy = validateSceneAnimation(animation, entities);
    if (!legacy.isValid()) return fail(EvaluatedSceneError::InvalidNumerics, legacy.entity, legacy);
    std::size_t legacyKeys = 0;
    for (const auto& [id, track] : animation.tracks) { (void)id; legacyKeys += track.keys.size() * 3; }
    const auto properties = validatePropertyAnimation(animation.properties, source.documentUuid, owners,
                                                       animation.tracks.size() * 3, legacyKeys);
    if (!properties.isValid()) return fail(EvaluatedSceneError::InvalidProperty, 0, {}, properties);
    const auto sampled = sampleValidatedPropertyAnimation(animation.properties, source.frameTime);
    std::map<EntityId, std::vector<const SampledProperty*>> nodeSamples;
    for (const auto& property : sampled) {
        if (const auto* entity = std::get_if<EntityOwner>(&property.address.owner)) {
            if (property.address.property != PropertyId::SunIrradiance &&
                animation.tracks.contains({entity->entity, legacyChannel(property.address.property)}))
                return fail(EvaluatedSceneError::InvalidProperty, entity->entity, {}, {PropertyError::DuplicateProperty, property.address, property.curve});
            nodeSamples[entity->entity].push_back(&property);
        }
    }
    auto content = std::make_shared<EvaluatedSceneContent>();
    content->authors = std::move(authors);
    content->geometry = std::move(geometry);
    content->legacyLighting = content->authors->legacyLighting;
    content->nodes.reserve(inputs.size());
    content->indices.reserve(inputs.size());
    std::set<MaterialSlotId> slots;
    for (const auto& [id, asset] : content->authors->materials->assets) {
        (void)id;
        content->materials.emplace(asset.material->id, EvaluatedMaterial{*asset.material, asset.material->parameters});
    }
    for (const auto& property : sampled) {
        if (const auto* material = std::get_if<MaterialOwner>(&property.address.owner))
            content->materials.at(material->material).effective.roughness = property.value;
        content->propertySources.push_back({property.address, PropertyValueSource::ScalarCurve, property.curve,
                                           property.value, property.value});
    }
    for (const auto& input : inputs) {
        const auto& author = content->authors->nodes.at(input.entity);
        EvaluatedSceneNode node;
        node.entity = input.entity;
        node.parent = input.parent;
        node.localPosition = glm::dvec3(input.base.position);
        node.localScale = glm::dvec3(input.base.scale);
        node.localRotation = glm::dquat(input.base.rotation);
        node.visible = author.visible;
        node.camera = author.camera;
        node.legacyLight = author.legacyLight;
        node.legacySurface = author.legacySurface;
        node.materialSlots = author.materialSlots;
        for (const auto& slot : node.materialSlots)
            if (!slot.id.isValid() || !slots.insert(slot.id).second || !content->materials.contains(slot.material))
                return fail(EvaluatedSceneError::InvalidMaterial, input.entity);
        for (const auto channel : {AnimationChannel::Position, AnimationChannel::Scale, AnimationChannel::RotationEulerXYZDegrees}) {
            const auto track = animation.tracks.find({input.entity, channel});
            if (track == animation.tracks.end()) continue;
            const auto value = detail::sampleValidatedAnimationTrack(track->second, source.frameTime);
            const auto checked = validateAnimationValue(channel, value);
            if (!checked.isValid()) return fail(EvaluatedSceneError::InvalidNumerics, input.entity, checked);
            if (channel == AnimationChannel::Position) node.localPosition = value;
            else if (channel == AnimationChannel::Scale) node.localScale = value;
            else {
                node.rotationEulerXYZDegrees = value;
                node.rotationSource = EffectiveRotationSource::LegacyTrack;
            }
            const auto property = channel == AnimationChannel::Position ? PropertyId::Position :
                channel == AnimationChannel::Scale ? PropertyId::Scale : PropertyId::RotationEulerXYZDegrees;
            for (int axis = 0; axis < 3; ++axis)
                content->propertySources.push_back({{source.documentUuid, EntityOwner{input.entity}, property,
                                                    static_cast<PropertyComponent>(axis)},
                                                   PropertyValueSource::LegacyTrack, {}, value[axis], value[axis]});
        }
        std::optional<SunComponent> sun = author.sun;
        if (const auto found = nodeSamples.find(input.entity); found != nodeSamples.end())
            for (const auto* property : found->second) {
                const auto axis = static_cast<int>(property->address.component);
                if (property->address.property == PropertyId::Position) node.localPosition[axis] = property->value;
                else if (property->address.property == PropertyId::Scale) node.localScale[axis] = property->value;
                else if (property->address.property == PropertyId::SunIrradiance) sun->irradiance = property->value;
                else {
                    if (!node.rotationEulerXYZDegrees) {
                        node.rotationEulerXYZDegrees = canonicalEulerXYZDegrees(node.localRotation);
                        if (!node.rotationEulerXYZDegrees) return fail(EvaluatedSceneError::InvalidNumerics, input.entity);
                    }
                    (*node.rotationEulerXYZDegrees)[axis] = property->value;
                    node.rotationSource = EffectiveRotationSource::ScalarCurve;
                }
            }
        if (node.rotationEulerXYZDegrees) {
            const auto rotation = quaternionFromEulerXYZDegrees(*node.rotationEulerXYZDegrees);
            if (!rotation) return fail(EvaluatedSceneError::InvalidNumerics, input.entity);
            node.localRotation = *rotation;
        }
        // 作者基础 quaternion 保持原字节，只有派生矩阵/设备方向使用 double 单位旋转。
        const auto localRigid = unitQuaternion(node.localRotation);
        if (!localRigid) return fail(EvaluatedSceneError::InvalidNumerics, input.entity);
        node.localAffine = glm::translate(glm::dmat4(1.0), node.localPosition) *
                           glm::mat4_cast(*localRigid) * glm::scale(glm::dmat4(1.0), node.localScale);
        const auto* parent = input.parent ? &content->nodes.at(content->indices.at(input.parent)) : nullptr;
        node.worldAffine = parent ? parent->worldAffine * node.localAffine : node.localAffine;
        node.worldInverse = glm::inverse(node.worldAffine);
        node.worldNormal = glm::transpose(glm::inverse(glm::dmat3(node.worldAffine)));
        if (!finiteMatrix(node.localAffine) || !finiteMatrix(node.worldAffine) ||
            !finiteMatrix(node.worldInverse) || !finiteMatrix(node.worldNormal))
            return fail(EvaluatedSceneError::InvalidNumerics, input.entity);
        const auto rigid = unitQuaternion(parent ? parent->rigidWorldRotation * *localRigid : *localRigid);
        if (!rigid || !canNormalize(*rigid * glm::dvec3(0, 0, -1)) || !canNormalize(*rigid * glm::dvec3(0, 1, 0)))
            return fail(EvaluatedSceneError::InvalidNumerics, input.entity);
        node.rigidWorldRotation = *rigid;
        for (int axis = 0; axis < 3; ++axis)
            if (!canNormalize(node.worldNormal[axis])) return fail(EvaluatedSceneError::InvalidNumerics, input.entity);
        if (sun && node.visible) content->visibleSuns.push_back({input.entity, *author.sun, *sun, *rigid * glm::dvec3(0, 0, 1)});
        const auto& mesh = content->geometry->entries.at(input.entity);
        content->geometryStages.emplace(input.entity, EvaluatedGeometryStage{
            {input.entity, mesh.primitive, mesh.editableMesh, mesh.contentVersion, node.worldAffine}, mesh});
        content->indices.emplace(input.entity, content->nodes.size());
        content->nodes.push_back(std::move(node));
    }
    return {EvaluatedSceneState{source, std::move(content)}, {}};
}
AnimationPoseResult adaptSceneStateToPose(const EvaluatedSceneState& state) {
    if (!state.content || !isSourceIdentityValid(state.source)) return {std::nullopt, {AnimationError::InvalidFrame}};
    EvaluatedPose pose;
    pose.frame = state.source.frameTime;
    pose.nodes.reserve(state.content->nodes.size());
    pose.indices.reserve(state.content->nodes.size());
    for (const auto& source : state.content->nodes) {
        if (!fitsFloat(source.localAffine) || !fitsFloat(source.worldAffine) || !fitsFloat(source.worldInverse) || !fitsFloat(source.worldNormal))
            return {std::nullopt, {AnimationError::InvalidWorldTransform, source.entity}};
        EvaluatedPoseNode node;
        node.entity = source.entity;
        node.local.position = glm::vec3(source.localPosition);
        node.local.scale = glm::vec3(source.localScale);
        node.local.rotation = glm::quat(source.localRotation);
        node.world = glm::mat4(source.worldAffine);
        node.worldInverse = glm::mat4(source.worldInverse);
        node.worldNormal = glm::mat3(source.worldNormal);
        const auto rigid = unitQuaternion(glm::quat(source.rigidWorldRotation));
        if (!rigid || !node.local.isValid()) return {std::nullopt, {AnimationError::InvalidRigidRotation, source.entity}};
        node.rigidWorldRotation = *rigid;
        node.rotationSource = source.rotationSource == EffectiveRotationSource::BaseQuaternion
            ? AnimationRotationSource::BaseQuaternion : AnimationRotationSource::AnimationTrack;
        node.rotationEulerXYZDegrees = source.rotationEulerXYZDegrees;
        for (int axis = 0; axis < 3; ++axis)
            if (!canNormalize(node.worldNormal[axis]) || !canNormalize(glm::vec3(node.worldInverse[axis])))
                return {std::nullopt, {AnimationError::InvalidWorldNormal, source.entity}};
        const auto& stage = state.content->geometryStages.at(source.entity);
        if (stage.geometry.mesh)
            for (const auto& vertex : stage.geometry.mesh->vertices)
                if (!canNormalize(node.worldNormal * vertex.normal))
                    return {std::nullopt, {AnimationError::InvalidWorldNormal, source.entity}};
        pose.indices.emplace(node.entity, pose.nodes.size());
        pose.nodes.push_back(std::move(node));
    }
    return {std::move(pose), {}};
}
} // namespace mini3d::core
