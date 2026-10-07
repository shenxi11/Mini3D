/*
 * 模块名: RayCaster
 * 功能概述: 合并可见子树包围盒并将世界射线逆变换后与局部盒相交。
 * 对外接口: RayCaster
 * 依赖关系: PrimitiveFactory、Core、CPU Assets、GLM
 * 输入输出: 只读场景与资源到世界盒/最近几何 ID。
 * 异常与错误: 缺失节点/网格不命中；空盒由 Core 相交接口拒绝。
 * 维护说明: 局部射线方向不重新归一化，保持跨非均匀缩放对象的参数可比较。
 */
#include "RayCaster.h"

#include "InstalledPose.h"
#include "PrimitiveFactory.h"

#include <cmath>
#include <functional>
#include <limits>

namespace mini3d::renderer_gl {
namespace {
bool isUsableRay(const core::Ray& ray) {
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(ray.origin[axis]) || !std::isfinite(ray.direction[axis]))
            return false;
    const auto length = glm::length(ray.direction);
    if (!std::isfinite(length) || length == 0)
        return false;
    const auto direction = glm::normalize(ray.direction);
    return std::isfinite(direction.x) && std::isfinite(direction.y) &&
           std::isfinite(direction.z) && glm::length(direction) > 0;
}
} // namespace

core::Aabb RayCaster::localBounds(const core::Scene& scene, const core::SceneNode& node,
                                  const assets::AssetManager& assets,
                                  const core::ViewportVisibility& visibility) {
    if (!visibility.isVisible(scene, node.id))
        return {};
    if (node.editableMesh == 0) {
        return localBounds(node, assets);
    }
    core::Aabb bounds;
    if (const auto* mesh = scene.editableMesh(node.editableMesh)) {
        const auto& content = *mesh->content;
        if ((content.mirrorEvaluation || content.subdivisionEvaluation) &&
            (node.id != visibility.editedEntity || !visibility.hasHiddenElements())) {
            for (const auto& vertex : content.evaluatedMesh().vertices)
                bounds.expand(vertex.position);
        } else if ((content.mirrorEvaluation || content.subdivisionEvaluation) &&
                   visibility.hasHiddenElements()) {
            const auto& data = content.displayedDerived();
            for (std::size_t i = 0; i < data.mesh.indices.size(); i += 3) {
                if (!visibility.isTriangleVisible(node.id, content, i / 3))
                    continue;
                for (int corner = 0; corner < 3; ++corner)
                    bounds.expand(data.mesh.vertices[data.mesh.indices[i + corner]].position);
            }
        } else {
            for (const auto& vertex : content.source.vertices) {
                if (node.id != visibility.editedEntity ||
                    visibility.isVertexVisible(content.source, vertex.id))
                    bounds.expand(vertex.position);
            }
        }
    }
    return bounds;
}
core::Aabb RayCaster::localBounds(const core::SceneNode& node, const assets::AssetManager& assets) {
    if (node.meshRenderer) {
        const auto* mesh = assets.mesh(node.meshRenderer->mesh);
        return mesh != nullptr ? mesh->data.bounds() : core::Aabb{};
    }
    switch (node.primitive) {
        case core::PrimitiveKind::Cube: {
            static const auto bounds = PrimitiveFactory::createCube().bounds();
            return bounds;
        }
        case core::PrimitiveKind::Sphere: {
            static const auto bounds = PrimitiveFactory::createSphere().bounds();
            return bounds;
        }
        case core::PrimitiveKind::Plane: {
            static const auto bounds = PrimitiveFactory::createPlane().bounds();
            return bounds;
        }
        case core::PrimitiveKind::Empty:
            return {};
    }
    return {};
}

core::Aabb RayCaster::worldBounds(const core::Scene& scene, const assets::AssetManager& assets,
                                  core::EntityId root, const core::ViewportVisibility& visibility,
                                  const InstalledPose* pose) {
    core::Aabb result;
    if (pose) {
        if (!pose->numerics || !pose->geometry ||
            pose->numerics->nodes.size() != pose->geometry->entries.size())
            return {};
        const auto rootEntry = pose->geometry->entries.find(root);
        if (rootEntry == pose->geometry->entries.end())
            return {};
        // 一次父先子后遍历确定子树归属，不对每个候选重走祖先链。
        std::unordered_map<core::EntityId, bool> inSubtree;
        inSubtree.reserve(pose->numerics->nodes.size());
        for (const auto& node : pose->numerics->nodes) {
            const auto found = pose->geometry->entries.find(node.entity);
            if (found == pose->geometry->entries.end())
                return {};
            const auto& entry = found->second;
            bool contained = node.entity == root;
            if (entry.parent != core::kInvalidEntity) {
                const auto parent = inSubtree.find(entry.parent);
                if (parent == inSubtree.end())
                    return {};
                contained = contained || parent->second;
            }
            inSubtree.emplace(node.entity, contained);
            if (!contained || !entry.visible || !entry.localBounds.isValid())
                continue;
            const auto bounds = entry.localBounds.transformed(node.world);
            if (!bounds.isValid())
                return {};
            result.expand(bounds.minimum);
            result.expand(bounds.maximum);
        }
        return result;
    }
    if (!scene.isVisible(root)) {
        return result;
    }
    std::function<void(core::EntityId, const glm::mat4&)> visit = [&](core::EntityId id,
                                                                      const glm::mat4& world) {
        const auto* node = scene.find(id);
        if (!node->visible) {
            return;
        }
        const auto bounds = localBounds(scene, *node, assets, visibility).transformed(world);
        if (bounds.isValid()) {
            result.expand(bounds.minimum);
            result.expand(bounds.maximum);
        }
        for (const auto child : node->children) {
            visit(child, world * scene.find(child)->transform.localMatrix());
        }
    };
    visit(root, scene.worldMatrix(root));
    return result;
}

core::Aabb RayCaster::sceneBounds(const core::Scene& scene, const assets::AssetManager& assets,
                                  const core::ViewportVisibility& visibility,
                                  const InstalledPose* pose) {
    core::Aabb result;
    if (pose) {
        if (!pose->numerics || !pose->geometry ||
            pose->numerics->nodes.size() != pose->geometry->entries.size())
            return {};
        for (const auto& node : pose->numerics->nodes) {
            const auto found = pose->geometry->entries.find(node.entity);
            if (found == pose->geometry->entries.end())
                return {};
            const auto& entry = found->second;
            if (!entry.visible)
                continue;
            if (entry.localBounds.isValid()) {
                const auto bounds = entry.localBounds.transformed(node.world);
                if (!bounds.isValid())
                    return {};
                result.expand(bounds.minimum);
                result.expand(bounds.maximum);
            } else if (!entry.hasGeometry) {
                result.expand(glm::vec3(node.world[3]));
            }
        }
        return result;
    }
    std::function<void(core::EntityId, const glm::mat4&)> visit =
        [&](core::EntityId id, const glm::mat4& parentWorld) {
            const auto* node = scene.find(id);
            if (!node->visible) {
                return;
            }
            const auto world = parentWorld * node->transform.localMatrix();
            const auto bounds = localBounds(scene, *node, assets, visibility).transformed(world);
            if (bounds.isValid()) {
                result.expand(bounds.minimum);
                result.expand(bounds.maximum);
            } else if (visibility.isVisible(scene, id) && node->editableMesh == 0) {
                result.expand(glm::vec3(world[3]));
            }
            for (const auto child : node->children) {
                visit(child, world);
            }
        };
    for (const auto root : scene.roots()) {
        visit(root, glm::mat4(1));
    }
    return result;
}

core::EntityId RayCaster::pick(const core::Scene& scene, const assets::AssetManager& assets,
                               const core::Ray& ray, const core::ViewportVisibility& visibility,
                               const InstalledPose* pose) {
    if (!isUsableRay(ray))
        return core::kInvalidEntity;
    auto selected = core::kInvalidEntity;
    float nearest = std::numeric_limits<float>::infinity();
    if (pose) {
        if (!pose->numerics || !pose->geometry ||
            pose->numerics->nodes.size() != pose->geometry->entries.size())
            return core::kInvalidEntity;
        for (const auto& node : pose->numerics->nodes) {
            const auto found = pose->geometry->entries.find(node.entity);
            if (found == pose->geometry->entries.end())
                return core::kInvalidEntity;
            const auto& entry = found->second;
            if (!entry.visible || !entry.localBounds.isValid())
                continue;
            const core::Ray localRay{
                glm::vec3(node.worldInverse * glm::vec4(ray.origin, 1)),
                glm::vec3(node.worldInverse * glm::vec4(ray.direction, 0))};
            if (!isUsableRay(localRay))
                return core::kInvalidEntity;
            float distance = 0;
            if (core::intersectRayAabb(localRay, entry.localBounds, distance) && distance < nearest) {
                nearest = distance;
                selected = node.entity;
            }
        }
        return selected;
    }
    std::function<void(core::EntityId, const glm::mat4&)> visit =
        [&](core::EntityId id, const glm::mat4& parentWorld) {
            const auto* node = scene.find(id);
            if (!node->visible) {
                return;
            }
            const auto world = parentWorld * node->transform.localMatrix();
            const auto bounds = localBounds(scene, *node, assets, visibility);
            if (bounds.isValid()) {
                const auto inverse = glm::inverse(world);
                const core::Ray localRay{glm::vec3(inverse * glm::vec4(ray.origin, 1.0F)),
                                         glm::vec3(inverse * glm::vec4(ray.direction, 0.0F))};
                float distance = 0;
                if (isUsableRay(localRay) && core::intersectRayAabb(localRay, bounds, distance) &&
                    distance < nearest) {
                    nearest = distance;
                    selected = id;
                }
            }
            for (const auto child : node->children) {
                visit(child, world);
            }
        };
    for (const auto root : scene.roots()) {
        visit(root, glm::mat4(1.0F));
    }
    return selected;
}
} // namespace mini3d::renderer_gl
