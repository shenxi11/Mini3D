/*
 * 模块名: MeshApiSupport
 * 功能概述: 共享网格身份与候选资源检查，保持已确认场景和历史不变。
 * 对外接口: MeshApiSupport.h。
 * 依赖关系: Core、生成限额、C++ 容器；不依赖 JSON/Renderer。
 * 输入输出: 源及求值快照到预算或版本检查结果。
 * 异常与错误: 参数返回 INVALID_ARGUMENT；数量/内存返回 LIMIT_EXCEEDED。
 * 维护说明: 修改器展开采用保守上界，实际准备后再次按容量核算。
 */
#include "MeshApiSupport.h"

#include <cmath>
#include <limits>
#include <QRegularExpression>
#include <set>

namespace mini3d::editor::api {
namespace {
ApiError limitError(const DocumentState& state) {
    return {ErrorCode::LimitExceeded, QStringLiteral("网格源、求值或候选内存超出 API 预算。"),
            QStringLiteral("mesh"), Recovery::CorrectInput, state};
}
struct Counts {
    std::size_t vertices, faces, corners;
    bool fits() const {
        return vertices <= limits::meshVertices && faces <= limits::meshFaces &&
               corners <= limits::meshCorners;
    }
};
Counts counts(const core::modeling::EditableMesh& source) {
    return {source.vertices.size(), source.faces.size(), meshCornerCount(source)};
}
struct Budget {
    std::size_t remaining = limits::candidateBytes;
    bool add(std::size_t count, std::size_t unit) {
        if (count > remaining / unit)
            return false;
        remaining -= count * unit;
        return true;
    }
    bool source(const core::modeling::EditableMesh& mesh) {
        if (!counts(mesh).fits() ||
            !add(mesh.vertices.capacity(), sizeof(core::modeling::EditableVertex)) ||
            !add(mesh.faces.capacity(), sizeof(core::modeling::EditableFace)))
            return false;
        for (const auto& face : mesh.faces)
            if (!add(face.corners.capacity(), sizeof(core::modeling::MeshCorner)))
                return false;
        return true;
    }
    bool derived(const core::modeling::DerivedMesh& mesh) {
        return add(mesh.mesh.vertices.capacity(), sizeof(core::MeshVertex)) &&
               add(mesh.mesh.indices.capacity(), sizeof(std::uint32_t)) &&
               add(mesh.vertexSources.capacity(), sizeof(core::modeling::RenderVertexSource)) &&
               add(mesh.triangleSources.capacity(), sizeof(core::modeling::RenderTriangleSource));
    }
    bool estimated(Counts value) {
        // 源、拆点派生和临时源副本；三角形数不超过总角数。
        return value.fits() &&
               add(value.vertices, 2 * sizeof(core::modeling::EditableVertex)) &&
               add(value.faces, 2 * sizeof(core::modeling::EditableFace)) &&
               add(value.corners, 2 * sizeof(core::modeling::MeshCorner) +
                                      sizeof(core::MeshVertex) +
                                      sizeof(core::modeling::RenderVertexSource) +
                                      3 * sizeof(std::uint32_t) +
                                      sizeof(core::modeling::RenderTriangleSource));
    }
    template <typename Map> bool mapping(const Map& map) {
        // 计入树节点链接、颜色/对齐开销，不能只计算 pair 数据。
        return add(map.size(), sizeof(typename Map::value_type) + 4 * sizeof(void*));
    }
};
} // namespace
ApiError meshArgumentError(const DocumentState& state, const QString& field,
                           const QString& message) {
    return {ErrorCode::InvalidArgument, message, field, Recovery::CorrectInput, state, -32602};
}
std::optional<ApiError> checkMeshEnvelope(const MutationRequest& request,
                                         const DocumentState& state) {
    if (request.timeoutMs < 1 || std::size_t(request.timeoutMs) > limits::mutationTimeoutMaximumMs)
        return meshArgumentError(state, "timeoutMs", QStringLiteral("超时参数超出允许范围。"));
    if (request.mutationSequence && *request.mutationSequence == 0)
        return meshArgumentError(state, "mutationSequence", QStringLiteral("写序号必须非零。"));
    if (request.clientSessionId) {
        static const QRegularExpression uuid(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
        if (!uuid.match(*request.clientSessionId).hasMatch())
            return meshArgumentError(state, "clientSessionId", QStringLiteral("会话身份须为 UUID。"));
    }
    return std::nullopt;
}
bool isFiniteFloat(double value) {
    return std::isfinite(value) && std::abs(value) <= double(std::numeric_limits<float>::max());
}
std::size_t meshCornerCount(const core::modeling::EditableMesh& source) {
    std::size_t result = 0;
    for (const auto& face : source.faces)
        result += face.corners.size();
    return result;
}
ApiResult<const core::EditableMeshRecord*>
meshTarget(const core::Scene& scene, const DocumentState& state, core::EntityId entity,
           core::MeshId mesh) {
    using Result = ApiResult<const core::EditableMeshRecord*>;
    if (entity == 0 || mesh == 0)
        return Result::failure(meshArgumentError(state, entity == 0 ? "entityId" : "meshId",
                                                 QStringLiteral("目标 ID 必须非零。")));
    const auto* node = scene.find(entity);
    if (!node)
        return Result::failure({ErrorCode::NotFound, QStringLiteral("对象不存在。"), "entityId",
                                Recovery::Refetch, state});
    if (node->editableMesh == 0)
        return Result::failure({ErrorCode::UnsupportedOperation,
                                QStringLiteral("目标尚未绑定可编辑源网格。"), "meshId",
                                Recovery::CorrectInput, state});
    if (node->editableMesh != mesh)
        return Result::failure({ErrorCode::NotFound, QStringLiteral("对象不引用指定源网格。"),
                                "meshId", Recovery::Refetch, state});
    return Result::success(scene.editableMesh(mesh));
}
MeshIdentity meshIdentity(core::EntityId entity, core::MeshId mesh,
                          const core::EditableMeshRecord& record) {
    return {entity, mesh, record.topologyRevision, record.geometryRevision,
            record.evaluationRevision};
}
ModifierState modifierState(const core::EditableMeshContent& content) {
    ModifierState result;
    result.mirror = content.mirror;
    result.subdivision = content.subdivision;
    return result;
}
std::optional<ApiError> checkMeshRevisions(const MeshMutationRequest& request,
                                          const core::EditableMeshRecord& record,
                                          const DocumentState& state) {
    if (request.expectedTopologyRevision != record.topologyRevision ||
        request.expectedGeometryRevision != record.geometryRevision)
        return ApiError{ErrorCode::RevisionConflict, QStringLiteral("源网格版本已变化。"),
                        request.expectedTopologyRevision != record.topologyRevision
                            ? "expectedTopologyRevision"
                            : "expectedGeometryRevision",
                        Recovery::Refetch, state};
    return std::nullopt;
}
std::optional<ApiError>
checkMeshCandidateBudget(const core::modeling::EditableMesh& source,
                         const core::EditableMeshContent* modifiers,
                         const DocumentState& state) {
    return checkMeshCandidateBudget(source, modifiers ? modifiers->mirror : std::nullopt,
                                    modifiers ? modifiers->subdivision : std::nullopt, state);
}
std::optional<ApiError>
checkMeshCandidateBudget(const core::modeling::EditableMesh& source,
                         const std::optional<core::modeling::MirrorOptions>& mirror,
                         const std::optional<core::modeling::SubdivisionOptions>& subdivision,
                         const DocumentState& state) {
    auto value = counts(source);
    Budget budget;
    if (!budget.estimated(value))
        return limitError(state);
    if (mirror) {
        if (mirror->enabled) {
            value.vertices *= 2;
            value.faces *= 2;
            value.corners *= 2;
        }
        if (!budget.estimated(value) ||
            !budget.add(value.vertices + value.faces + value.corners,
                        sizeof(core::modeling::MirrorElementSource) +
                            sizeof(std::uint64_t) + 4 * sizeof(void*)))
            return limitError(state);
    }
    if (subdivision && subdivision->enabled) {
        std::set<core::modeling::EdgeKey> uniqueEdges;
        for (const auto& face : source.faces)
            for (std::size_t corner = 0; corner < face.corners.size(); ++corner)
                uniqueEdges.emplace(face.corners[corner].vertex,
                                    face.corners[(corner + 1) % face.corners.size()].vertex);
        auto edgeCount = uniqueEdges.size();
        if (mirror && mirror->enabled)
            edgeCount *= 2;
        for (int level = 0; level < subdivision->levels; ++level) {
            // 每条无向边只生成一个边点；面角只用于面点到边点的连边数。
            value.vertices += edgeCount + value.faces;
            edgeCount = 2 * edgeCount + value.corners;
            value.faces = value.corners;
            value.corners = value.faces * 4;
            if (!value.fits())
                return limitError(state);
        }
        if (!budget.estimated(value) ||
            !budget.add(value.faces, 2 * sizeof(std::uint64_t) + 4 * sizeof(void*)))
            return limitError(state);
    }
    return std::nullopt;
}
std::optional<ApiError> checkMeshCandidateBudget(const core::EditableMeshContent& content,
                                                const DocumentState& state,
                                                const core::modeling::EditableMesh* retainedSource) {
    Budget budget;
    if (retainedSource && !budget.source(*retainedSource))
        return limitError(state);
    if (!budget.source(content.source) || !budget.derived(content.derived))
        return limitError(state);
    if (content.mirrorEvaluation) {
        const auto& mirror = *content.mirrorEvaluation;
        if (!budget.source(mirror.mesh) || !budget.derived(mirror.derived) ||
            !budget.mapping(mirror.vertices) || !budget.mapping(mirror.faces) ||
            !budget.mapping(mirror.corners))
            return limitError(state);
    }
    if (content.subdivisionEvaluation) {
        const auto& subdivision = *content.subdivisionEvaluation;
        if (!budget.source(subdivision.mesh) || !budget.derived(subdivision.derived) ||
            !budget.mapping(subdivision.faces))
            return limitError(state);
    }
    return std::nullopt;
}
} // namespace mini3d::editor::api
