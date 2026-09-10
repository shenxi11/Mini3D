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

#include "PrimitiveFactory.h"

#include <functional>
#include <limits>

namespace mini3d::renderer_gl {
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
                                  core::EntityId root) {
    core::Aabb result;
    if (!scene.isVisible(root)) {
        return result;
    }
    std::function<void(core::EntityId, const glm::mat4&)> visit = [&](core::EntityId id,
                                                                      const glm::mat4& world) {
        const auto* node = scene.find(id);
        if (!node->visible) {
            return;
        }
        const auto bounds = localBounds(*node, assets).transformed(world);
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

core::EntityId RayCaster::pick(const core::Scene& scene, const assets::AssetManager& assets,
                               const core::Ray& ray) {
    auto selected = core::kInvalidEntity;
    float nearest = std::numeric_limits<float>::infinity();
    std::function<void(core::EntityId, const glm::mat4&)> visit =
        [&](core::EntityId id, const glm::mat4& parentWorld) {
            const auto* node = scene.find(id);
            if (!node->visible) {
                return;
            }
            const auto world = parentWorld * node->transform.localMatrix();
            const auto bounds = localBounds(*node, assets);
            if (bounds.isValid()) {
                const auto inverse = glm::inverse(world);
                const core::Ray localRay{glm::vec3(inverse * glm::vec4(ray.origin, 1.0F)),
                                         glm::vec3(inverse * glm::vec4(ray.direction, 0.0F))};
                float distance = 0;
                if (core::intersectRayAabb(localRay, bounds, distance) && distance < nearest) {
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
