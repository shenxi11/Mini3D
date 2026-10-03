/*
 * 模块名: LoopCut
 * 功能概述: 以有向半边传递滑移方向，对整条面带同步分边并拆成两个四边面。
 * 对外接口: LoopCut.h；依赖关系: MeshTopology、MeshDerivation。
 * 输入输出: 只读源快照到合法候选，按各面的独立面角插值保留接缝。
 * 异常与错误: 首个不支持条件返回原因，不发布部分拓扑；分配异常向上传播。
 * 维护说明: Cube三价角点不被切线穿过；拒绝被切边相接的五价以上分叉区。
 */
#include "LoopCut.h"

#include "MeshDerivation.h"
#include "MeshTopology.h"

#include <cmath>
#include <limits>

namespace mini3d::core::modeling {
LoopCutAnalysis analyzeLoopCut(const EditableMesh& mesh, EdgeKey seed) {
    const auto derived = deriveMesh(mesh);
    if (!derived.derived)
        return {{}, derived.error};
    const auto built = buildMeshTopology(mesh);
    if (!built.topology)
        return {{}, built.error};
    const auto& topology = *built.topology;
    if (!topology.edgeHalfEdges.contains(seed))
        return {{}, "环切需要命中一条有效源边，不使用渲染三角对角线。"};
    std::map<VertexId, std::size_t> degree;
    std::map<FaceId, std::size_t> faceSizes;
    for (const auto& [edge, halfEdge] : topology.edgeHalfEdges) {
        ++degree[edge.first];
        ++degree[edge.second];
    }
    for (const auto& face : mesh.faces)
        faceSizes.emplace(face.id, face.corners.size());
    LoopCutInfo band;
    band.edgeStarts.emplace(seed, seed.first);
    std::vector<EdgeKey> pending{seed};
    std::map<FaceId, EdgeKey> visitedFaces;
    std::size_t boundaries = 0;
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto edge = pending[index];
        if (degree.at(edge.first) > 4 || degree.at(edge.second) > 4)
            return {{}, "环切面带接触五价以上极点/分叉区，当前仅支持规则四边形带。"};
        const auto first = topology.edgeHalfEdges.at(edge);
        const auto twin = topology.halfEdges[first].twin;
        if (!twin)
            ++boundaries;
        std::vector<std::size_t> sides{first};
        if (twin)
            sides.push_back(*twin);
        for (const auto side : sides) {
            const auto& half = topology.halfEdges[side];
            if (faceSizes.at(half.face) != 4)
                return {{}, "环切遇到三角面或非四边面；整条面带已拒绝，未留下T形连接。"};
            const auto& opposite = topology.halfEdges[topology.halfEdges[half.next].next];
            const EdgeKey next(opposite.from, opposite.to);
            const auto [face, inserted] = visitedFaces.emplace(half.face, edge);
            if (!inserted && face->second != edge && face->second != next)
                return {{}, "环切面带重复穿越同一面，存在交叉或极点，无法生成单条切线。"};
            band.faces.insert(half.face);
            const auto start = band.edgeStarts.at(edge) == half.from ? opposite.to : opposite.from;
            const auto [direction, added] = band.edgeStarts.emplace(next, start);
            if (!added && direction->second != start)
                return {{}, "环切面带的滑移方向无法连续闭合，当前不支持扭转面带。"};
            if (added)
                pending.push_back(next);
        }
    }
    if (boundaries != 0 && boundaries != 2)
        return {{}, "环切面带必须闭合或在两个真实边界终止。"};
    band.closed = boundaries == 0;
    return {std::move(band), {}};
}

LoopCutResult loopCut(const EditableMesh& before, EdgeKey seed, double slide) {
    if (!std::isfinite(slide) || std::abs(slide) >= 1)
        return {{}, {}, "环切滑移必须严格位于-100%与100%之间，不允许端点重叠。"};
    const auto analysis = analyzeLoopCut(before, seed);
    if (!analysis.band)
        return {{}, {}, analysis.error};
    const auto& band = *analysis.band;
    VertexId lastVertex = 0;
    FaceId lastFace = 0;
    CornerId lastCorner = 0;
    for (const auto& vertex : before.vertices)
        lastVertex = std::max(lastVertex, vertex.id);
    for (const auto& face : before.faces) {
        lastFace = std::max(lastFace, face.id);
        for (const auto& corner : face.corners)
            lastCorner = std::max(lastCorner, corner.id);
    }
    constexpr auto lastId = std::numeric_limits<std::uint64_t>::max();
    if (band.edgeStarts.size() > lastId - lastVertex || band.faces.size() > lastId - lastFace ||
        band.faces.size() > (lastId - lastCorner) / 4)
        return {{}, {}, "网格元素ID空间不足，无法生成环切。"};
    auto result = before;
    std::map<EdgeKey, VertexId> newVertices;
    const double factor = (slide + 1) * .5;
    for (const auto& [edge, start] : band.edgeStarts) {
        const auto end = edge.first == start ? edge.second : edge.first;
        const auto a = glm::dvec3(before.vertex(start)->position);
        const auto b = glm::dvec3(before.vertex(end)->position);
        newVertices.emplace(edge, ++lastVertex);
        result.vertices.push_back({lastVertex, glm::vec3(a + (b - a) * factor)});
    }
    // 各面分别插值面角，不跨UV/硬法线接缝合并属性。
    const auto surface = *deriveMesh(before).derived;
    std::map<CornerId, glm::vec3> normals;
    for (std::size_t i = 0; i < surface.vertexSources.size(); ++i)
        normals.emplace(surface.vertexSources[i].corner, surface.mesh.vertices[i].normal);
    const auto sample = [&](const MeshCorner& a, const MeshCorner& b) {
        const EdgeKey edge(a.vertex, b.vertex);
        const double t = band.edgeStarts.at(edge) == a.vertex ? factor : 1 - factor;
        MeshCorner corner;
        corner.id = ++lastCorner;
        corner.vertex = newVertices.at(edge);
        corner.uv = glm::dvec2(a.uv) * (1 - t) + glm::dvec2(b.uv) * t;
        corner.color = glm::dvec3(a.color) * (1 - t) + glm::dvec3(b.color) * t;
        const auto normal =
            glm::dvec3(normals.at(a.id)) * (1 - t) + glm::dvec3(normals.at(b.id)) * t;
        corner.normal =
            glm::length(normal) > 1.0e-12 ? glm::normalize(normal) : glm::dvec3(normals.at(a.id));
        return corner;
    };
    std::set<EdgeKey> cutEdges;
    std::map<FaceId, std::size_t> faceIndices;
    for (std::size_t i = 0; i < before.faces.size(); ++i)
        faceIndices.emplace(before.faces[i].id, i);
    for (const auto faceId : band.faces) {
        const auto faceIndex = faceIndices.at(faceId);
        const auto& face = before.faces[faceIndex];
        // 选原面0或1号边，使保留原FaceId的半面包含原来的首角。
        const std::size_t index =
            newVertices.contains({face.corners[0].vertex, face.corners[1].vertex}) ? 0 : 1;
        const auto& a = face.corners[index];
        const auto& b = face.corners[(index + 1) % 4];
        const auto& c = face.corners[(index + 2) % 4];
        const auto& d = face.corners[(index + 3) % 4];
        auto u = sample(a, b), v = sample(c, d);
        cutEdges.emplace(u.vertex, v.vertex);
        result.faces[faceIndex].corners = {a, u, v, d};
        u.id = ++lastCorner;
        v.id = ++lastCorner;
        result.faces.push_back({++lastFace, {u, b, c, v}, face.material});
    }
    const auto validated = deriveMesh(result);
    if (!validated.derived)
        return {{}, {}, "环切候选无效：" + validated.error};
    return {std::move(result), std::move(cutEdges), {}};
}
} // namespace mini3d::core::modeling
