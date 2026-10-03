/*
 * 模块名: Mirror
 * 功能概述: 保留源身份和角属性，反射坐标/法线并反转面环；夹持不等同于焊接。
 * 对外接口: Mirror.h；依赖关系: MeshTopology、MeshDerivation、标准数学。
 * 输入输出: 源网格及参数到独立合法候选与镜像来源，局部夹持到接受后的捕获状态。
 * 异常与错误: 首错返回，不修改输入；维护说明: 不检测全局自相交，不自动切半或删盖。
 */
#include "Mirror.h"

#include "MeshTopology.h"

#include <cmath>
#include <limits>

namespace mini3d::core::modeling {
namespace {
void resetMovedNormals(EditableMesh& mesh, const std::set<VertexId>& moved) {
    for (auto& face : mesh.faces) {
        if (std::any_of(face.corners.begin(), face.corners.end(), [&](const auto& corner) {
                return moved.contains(corner.vertex);
            })) {
            for (auto& corner : face.corners)
                corner.normal.reset();
        }
    }
}
bool sameTopology(const EditableMesh& before, const EditableMesh& candidate) {
    if (before.vertices.size() != candidate.vertices.size() ||
        before.faces.size() != candidate.faces.size())
        return false;
    std::set<VertexId> vertices;
    for (const auto& vertex : candidate.vertices)
        if (!vertices.insert(vertex.id).second || !before.vertex(vertex.id))
            return false;
    std::map<FaceId, const EditableFace*> faces;
    for (const auto& face : before.faces)
        faces.emplace(face.id, &face);
    for (const auto& face : candidate.faces) {
        const auto it = faces.find(face.id);
        if (it == faces.end() || it->second->corners.size() != face.corners.size() ||
            it->second->material != face.material)
            return false;
        for (std::size_t i = 0; i < face.corners.size(); ++i) {
            const auto& a = it->second->corners[i];
            const auto& b = face.corners[i];
            if (a.id != b.id || a.vertex != b.vertex || a.uv != b.uv || a.color != b.color)
                return false;
        }
        faces.erase(it);
    }
    return faces.empty();
}
} // namespace

bool MirrorOptions::isValid() const {
    return (axis == MirrorAxis::X || axis == MirrorAxis::Y || axis == MirrorAxis::Z) &&
           std::isfinite(threshold) && threshold >= 0;
}

MirrorResult evaluateMirror(const EditableMesh& source, const MirrorOptions& options) {
    if (!options.isValid())
        return {{}, "镜像轴必须为 X/Y/Z，阈值必须是有限非负数。"};
    const auto originalDerived = deriveMesh(source);
    if (!originalDerived.derived)
        return {{}, originalDerived.error};
    const auto built = buildMeshTopology(source);
    if (!built.topology)
        return {{}, built.error};
    MirrorEvaluation result;
    result.mesh = source;
    std::map<VertexId, const EditableVertex*> vertices;
    std::map<FaceId, const EditableFace*> faces;
    VertexId lastVertex = 0;
    FaceId lastFace = 0;
    CornerId lastCorner = 0;
    for (const auto& vertex : source.vertices) {
        vertices.emplace(vertex.id, &vertex);
        result.vertices.emplace(vertex.id, MirrorElementSource{vertex.id, false});
        lastVertex = std::max(lastVertex, vertex.id);
    }
    for (const auto& face : source.faces) {
        faces.emplace(face.id, &face);
        result.faces.emplace(face.id, MirrorElementSource{face.id, false});
        lastFace = std::max(lastFace, face.id);
        for (const auto& corner : face.corners) {
            result.corners.emplace(corner.id, MirrorElementSource{corner.id, false});
            lastCorner = std::max(lastCorner, corner.id);
        }
    }
    if (options.enabled) {
        const int axis = static_cast<int>(options.axis);
        std::set<VertexId> seam;
        if (options.merge) {
            for (const auto& half : built.topology->halfEdges) {
                if (!half.twin &&
                    std::abs(vertices.at(half.from)->position[axis]) <= options.threshold &&
                    std::abs(vertices.at(half.to)->position[axis]) <= options.threshold) {
                    seam.insert(half.from);
                    seam.insert(half.to);
                }
            }
        }
        std::set<FaceId> planeFaces;
        std::size_t extraCorners = 0;
        for (const auto& [id, face] : faces) {
            if (std::all_of(face->corners.begin(), face->corners.end(), [&](const auto& corner) {
                    return seam.contains(corner.vertex);
                }))
                planeFaces.insert(id);
            else
                extraCorners += face->corners.size();
        }
        constexpr auto maxId = std::numeric_limits<std::uint64_t>::max();
        if (vertices.size() - seam.size() > maxId - lastVertex ||
            faces.size() - planeFaces.size() > maxId - lastFace ||
            extraCorners > maxId - lastCorner)
            return {{}, "网格元素 ID 空间不足，无法生成镜像。"};
        std::map<VertexId, VertexId> reflected;
        std::set<VertexId> projected;
        for (auto& vertex : result.mesh.vertices) {
            if (seam.contains(vertex.id) && vertex.position[axis] != 0) {
                vertex.position[axis] = 0;
                projected.insert(vertex.id);
            }
        }
        resetMovedNormals(result.mesh, projected);
        for (const auto& [id, vertex] : vertices) {
            if (seam.contains(id)) {
                reflected.emplace(id, id);
                continue;
            }
            auto copy = *vertex;
            copy.id = ++lastVertex;
            copy.position[axis] = -copy.position[axis];
            reflected.emplace(id, copy.id);
            result.vertices.emplace(copy.id, MirrorElementSource{id, true});
            result.mesh.vertices.push_back(copy);
        }
        // 新面/角按源 ID 分配；源容器重排不会改变来源身份。
        std::map<CornerId, CornerId> reflectedCorners;
        for (const auto& [id, face] : faces)
            if (!planeFaces.contains(id))
                for (const auto& corner : face->corners)
                    reflectedCorners.emplace(corner.id, 0);
        for (auto& [id, reflectedId] : reflectedCorners)
            reflectedId = ++lastCorner;
        for (const auto& [id, face] : faces) {
            if (planeFaces.contains(id))
                continue;
            auto copy = *face;
            copy.id = ++lastFace;
            result.faces.emplace(copy.id, MirrorElementSource{id, true});
            const bool reshaped =
                std::any_of(copy.corners.begin(), copy.corners.end(), [&](const auto& corner) {
                    return projected.contains(corner.vertex);
                });
            std::reverse(copy.corners.begin(), copy.corners.end());
            for (auto& corner : copy.corners) {
                const auto sourceId = corner.id;
                corner.id = reflectedCorners.at(sourceId);
                corner.vertex = reflected.at(corner.vertex);
                if (reshaped)
                    corner.normal.reset();
                else if (corner.normal)
                    (*corner.normal)[axis] = -(*corner.normal)[axis];
                result.corners.emplace(corner.id, MirrorElementSource{sourceId, true});
            }
            result.mesh.faces.push_back(std::move(copy));
        }
    }
    auto derived = deriveMesh(result.mesh);
    if (!derived.derived)
        return {{}, "镜像求值无效：" + derived.error};
    result.derived = std::move(*derived.derived);
    return {std::move(result), {}};
}

std::optional<MirrorClipSession> MirrorClipSession::begin(const EditableMesh& before,
                                                          const std::set<VertexId>& selected,
                                                          const MirrorOptions& options,
                                                          std::string& error) {
    if (!options.isValid()) {
        error = "镜像夹持需要有效的局部轴与有限非负阈值。";
        return {};
    }
    const auto derived = deriveMesh(before);
    if (!derived.derived) {
        error = derived.error;
        return {};
    }
    for (const auto id : selected) {
        if (!before.vertex(id)) {
            error = "镜像夹持选区包含失效源顶点。";
            return {};
        }
    }
    MirrorClipSession session;
    session.before_ = before;
    session.options_ = options;
    session.selected_ = selected;
    if (options.enabled && options.clipping) {
        const auto built = buildMeshTopology(before);
        for (const auto& half : built.topology->halfEdges) {
            if (!half.twin) {
                for (const auto id : {half.from, half.to}) {
                    session.boundary_.insert(id);
                    if (selected.contains(id) &&
                        std::abs(before.vertex(id)->position[static_cast<int>(options.axis)]) <=
                        options.threshold)
                        session.constrained_.insert(id);
                }
            }
        }
    }
    error.clear();
    return session;
}

VertexTransformResult MirrorClipSession::constrain(const EditableMesh& candidate) {
    return constrain(candidate, selected_);
}
VertexTransformResult MirrorClipSession::constrain(const EditableMesh& candidate,
                                                   const std::set<VertexId>& affected) {
    for (const auto id : affected)
        if (!before_.vertex(id))
            return {{}, "镜像夹持的受影响顶点不存在。"};
    if (!sameTopology(before_, candidate))
        return {{}, "夹持候选的源身份、面环或角属性已变化，请重新开始变换。"};
    auto mesh = candidate;
    auto constrained = constrained_;
    std::set<VertexId> moved;
    const int axis = static_cast<int>(options_.axis);
    for (auto& vertex : mesh.vertices) {
        const auto original = before_.vertex(vertex.id)->position;
        for (int component = 0; component < 3; ++component)
            if (!std::isfinite(vertex.position[component]))
                return {{}, "镜像夹持候选包含非有限坐标。"};
        if (!affected.contains(vertex.id) && vertex.position != original)
            return {{}, "镜像夹持不能修改冻结选区之外的顶点。"};
        if (!affected.contains(vertex.id) || !boundary_.contains(vertex.id))
            continue;
        const double value = vertex.position[axis];
        const bool crossed = (original[axis] < 0 && value > 0) || (original[axis] > 0 && value < 0);
        if (constrained.contains(vertex.id) || crossed ||
            std::abs(original[axis]) <= options_.threshold || std::abs(value) <= options_.threshold) {
            constrained.insert(vertex.id);
            if (value != 0) {
                vertex.position[axis] = 0;
                moved.insert(vertex.id);
            }
        }
    }
    resetMovedNormals(mesh, moved);
    const auto derived = deriveMesh(mesh);
    if (!derived.derived)
        return {{}, "中心夹持后的网格无效：" + derived.error};
    constrained_ = std::move(constrained);
    return {std::move(mesh), {}};
}
const std::set<VertexId>& MirrorClipSession::constrainedVertices() const {
    return constrained_;
}
} // namespace mini3d::core::modeling
