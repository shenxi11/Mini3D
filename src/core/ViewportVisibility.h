/*
 * 模块名: ViewportVisibility
 * 功能概述: 统一临时对象隐藏、局部隔离与编辑元素隐藏查询，不修改持久 Scene。
 * 对外接口: ViewportVisibility；依赖关系: Scene、标准集合，无 Qt/GL。
 * 输入输出: 场景/源元素身份到可见性；不存在的对象不可见。
 * 异常与错误: 不负责几何校验；维护说明: 仅会话状态，不序列化、不进入历史。
 */
#pragma once

#include "Scene.h"

#include <set>

namespace mini3d::core {
/** @brief 点/边隐藏传播到关联面；面隐藏仅移除不再属于任何可见面的边/点。 */
struct ViewportVisibility {
    std::set<EntityId> hiddenObjects;
    EntityId localRoot = 0;
    EntityId editedEntity = 0;
    std::set<modeling::VertexId> vertices;
    std::set<modeling::EdgeKey> edges;
    std::set<modeling::FaceId> faces;

    /** @brief 与持久父级 visible 相交；隔离根的子树保留原父级变换。 */
    [[nodiscard]] bool isVisible(const Scene& scene, EntityId id) const {
        if (!scene.isVisible(id))
            return false;
        bool inLocal = localRoot == 0;
        for (auto current = id; current != 0; current = scene.find(current)->parent) {
            if (hiddenObjects.contains(current))
                return false;
            inLocal = inLocal || current == localRoot;
        }
        return inLocal;
    }
    [[nodiscard]] bool isFaceVisible(const modeling::EditableFace& face) const {
        if (faces.contains(face.id))
            return false;
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            const auto a = face.corners[i].vertex;
            const auto b = face.corners[(i + 1) % face.corners.size()].vertex;
            if (vertices.contains(a) || edges.contains({a, b}))
                return false;
        }
        return true;
    }
    [[nodiscard]] bool isFaceVisible(const modeling::EditableMesh& mesh,
                                     modeling::FaceId id) const {
        for (const auto& face : mesh.faces)
            if (face.id == id)
                return isFaceVisible(face);
        return false;
    }
    [[nodiscard]] bool isVertexVisible(const modeling::EditableMesh& mesh,
                                       modeling::VertexId id) const {
        if (!hasHiddenElements())
            return true;
        if (vertices.contains(id))
            return false;
        bool attached = false;
        for (const auto& face : mesh.faces) {
            for (const auto& corner : face.corners) {
                if (corner.vertex == id) {
                    attached = true;
                    if (isFaceVisible(face))
                        return true;
                    break;
                }
            }
        }
        return !attached; // 孤立点仍可编辑，除非显式隐藏。
    }
    [[nodiscard]] bool isEdgeVisible(const modeling::EditableMesh& mesh,
                                     modeling::EdgeKey edge) const {
        if (!hasHiddenElements())
            return true;
        for (const auto& face : mesh.faces) {
            if (!isFaceVisible(face))
                continue;
            for (std::size_t i = 0; i < face.corners.size(); ++i)
                if (modeling::EdgeKey(face.corners[i].vertex,
                                      face.corners[(i + 1) % face.corners.size()].vertex) == edge)
                    return true;
        }
        return false;
    }
    /** @brief 非编辑对象无元素掩码；用于渲染、游标与遮挡查询的同一三角判定。 */
    [[nodiscard]] bool isTriangleVisible(EntityId entity, const EditableMeshContent& content,
                                         std::size_t triangle) const {
        if (entity != editedEntity || !hasHiddenElements())
            return true;
        const auto face = content.displayedDerived().triangleSources[triangle].face;
        return isFaceVisible(content.source, content.sourceFace(face));
    }
    [[nodiscard]] bool hasHiddenElements() const {
        return !vertices.empty() || !edges.empty() || !faces.empty();
    }
    void clearElements() {
        vertices.clear();
        edges.clear();
        faces.clear();
    }
};
} // namespace mini3d::core
