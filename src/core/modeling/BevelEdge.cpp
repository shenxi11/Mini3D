/*
 * 模块名: BevelEdge
 * 功能概述: 求四条端部关联边与等距偏移线的交点，重写邻面和端面后补倒角四边面。
 * 对外接口: BevelEdge.h；依赖关系: MeshTopology、MeshDerivation、标准数学/容器。
 * 输入输出: 只读快照到闭合候选，沿各面原边独立插值UV/颜色并保留未影响身份。
 * 异常与错误: 候选必须通过流形、共面凸环与三角化验证；分配异常向调用方传播。
 * 维护说明: 使用局部差向量和double；有符号二面角区分外凸/凹边，受影响硬法线清除。
 */
#include "BevelEdge.h"

#include "MeshDerivation.h"
#include "MeshTopology.h"

#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>

namespace mini3d::core::modeling {
namespace {
constexpr double kGeometryEpsilon = 1.0e-10;
struct CornerCut {
    std::size_t corner = 0;
    std::size_t neighbor = 0;
    double height = 0;
};
struct SideGeometry {
    std::size_t face = 0;
    glm::dvec3 normal{0};
    glm::dvec3 inward{0};
    std::array<CornerCut, 2> cuts;
    bool forward = false;
};
struct EndGeometry {
    std::size_t face = 0;
    std::size_t corner = 0;
};
struct EdgeGeometry {
    std::array<SideGeometry, 2> sides;
    std::array<EndGeometry, 2> ends;
    double maximumWidth = std::numeric_limits<double>::infinity();
    std::size_t boundaryEdges = 0;
};
struct Inspection {
    std::optional<EdgeGeometry> edge;
    std::string error;
};

std::size_t faceIndex(const EditableMesh& mesh, FaceId id) {
    return static_cast<std::size_t>(std::find_if(mesh.faces.begin(), mesh.faces.end(),
                                                 [id](const auto& face) {
                                                     return face.id == id;
                                                 }) -
                                    mesh.faces.begin());
}
std::size_t cornerIndex(const EditableFace& face, VertexId id) {
    return static_cast<std::size_t>(std::find_if(face.corners.begin(), face.corners.end(),
                                                 [id](const auto& corner) {
                                                     return corner.vertex == id;
                                                 }) -
                                    face.corners.begin());
}

std::optional<glm::dvec3> convexNormal(const EditableMesh& mesh, const EditableFace& face,
                                       std::string& error) {
    const auto origin = glm::dvec3(mesh.vertex(face.corners.front().vertex)->position);
    std::vector<glm::dvec3> points;
    double scale = 0;
    for (const auto& corner : face.corners) {
        points.push_back(glm::dvec3(mesh.vertex(corner.vertex)->position) - origin);
        scale = std::max(scale, glm::length(points.back()));
    }
    glm::dvec3 area{0};
    const auto count = points.size();
    for (std::size_t i = 0; i < count; ++i)
        area += glm::cross(points[i], points[(i + 1) % count]);
    const auto normal = glm::normalize(area);
    for (auto& point : points) {
        point /= scale;
        if (std::abs(glm::dot(normal, point)) > 1.0e-6) {
            error = "倒角相关面 ID " + std::to_string(face.id) + " 不共面。";
            return {};
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto inward = glm::cross(normal, points[(i + 1) % count] - points[i]);
        const auto length = glm::length(inward);
        if (length <= 1.0e-12) {
            error = "倒角相关面的边过短或无法稳定投影。";
            return {};
        }
        for (const auto& point : points) {
            if (glm::dot(inward / length, point - points[i]) < -kGeometryEpsilon) {
                error = "倒角相关面 ID " + std::to_string(face.id) + " 不是凸多边形。";
                return {};
            }
        }
    }
    return normal;
}

Inspection inspectEdge(const EditableMesh& mesh, EdgeKey edge) {
    const auto derived = deriveMesh(mesh);
    if (!derived.derived)
        return {{}, derived.error};
    const auto built = buildMeshTopology(mesh);
    if (!built.topology)
        return {{}, built.error};
    const auto& topology = *built.topology;
    const auto found = topology.edgeHalfEdges.find(edge);
    if (found == topology.edgeHalfEdges.end())
        return {{}, "倒角需要一条有效源边，不使用渲染三角对角线。"};
    const auto twin = topology.halfEdges[found->second].twin;
    if (!twin)
        return {{}, "倒角不支持边界边；所选边必须邻接两个面。"};
    std::array<std::size_t, 2> halves{found->second, *twin};
    if (topology.halfEdges[halves[0]].face > topology.halfEdges[halves[1]].face)
        std::swap(halves[0], halves[1]);
    EdgeGeometry result;
    const std::array<VertexId, 2> endpoints{edge.first, edge.second};
    for (std::size_t end = 0; end < 2; ++end) {
        const auto& incident = topology.vertexHalfEdges.at(endpoints[end]);
        if (incident.size() != 3)
            return {{}, "单边倒角仅支持三价流形端角；当前端点连接了复杂角部。"};
        for (const auto index : incident) {
            const auto& half = topology.halfEdges[index];
            if (!half.twin)
                return {{}, "倒角端角接触边界，无法生成闭合端面。"};
            if (half.face != topology.halfEdges[halves[0]].face &&
                half.face != topology.halfEdges[halves[1]].face) {
                result.ends[end].face = faceIndex(mesh, half.face);
                result.ends[end].corner =
                    cornerIndex(mesh.faces[result.ends[end].face], endpoints[end]);
            }
        }
    }
    if (result.ends[0].face == result.ends[1].face)
        return {{}, "倒角两端必须各有一个独立端面，不支持共用端面的复杂角部。"};
    const auto a = glm::dvec3(mesh.vertex(edge.first)->position);
    const auto b = glm::dvec3(mesh.vertex(edge.second)->position);
    const auto axis = glm::normalize(b - a);
    std::string error;
    for (std::size_t side = 0; side < 2; ++side) {
        auto& geometry = result.sides[side];
        const auto& half = topology.halfEdges[halves[side]];
        geometry.face = faceIndex(mesh, half.face);
        const auto& face = mesh.faces[geometry.face];
        const auto normal = convexNormal(mesh, face, error);
        if (!normal)
            return {{}, error};
        geometry.normal = *normal;
        geometry.forward = half.from == edge.first;
        geometry.inward = glm::normalize(glm::cross(*normal, geometry.forward ? axis : -axis));
        for (std::size_t end = 0; end < 2; ++end) {
            auto& cut = geometry.cuts[end];
            cut.corner = cornerIndex(face, endpoints[end]);
            const auto previous = (cut.corner + face.corners.size() - 1) % face.corners.size();
            const auto next = (cut.corner + 1) % face.corners.size();
            cut.neighbor = face.corners[previous].vertex == endpoints[1 - end] ? next : previous;
            const auto direction =
                glm::dvec3(mesh.vertex(face.corners[cut.neighbor].vertex)->position) -
                glm::dvec3(mesh.vertex(endpoints[end])->position);
            cut.height = glm::dot(geometry.inward, direction);
            if (cut.height <= 0)
                return {{}, "倒角端部关联边与原边共线或回折，无法求内部偏移交点。"};
            result.maximumWidth = std::min(result.maximumWidth, cut.height);
        }
        const auto directionAt = [&](std::size_t end) {
            const auto& cut = geometry.cuts[end];
            return (glm::dvec3(mesh.vertex(face.corners[cut.neighbor].vertex)->position) -
                    glm::dvec3(mesh.vertex(endpoints[end])->position)) /
                   cut.height;
        };
        const auto speed = glm::dot(axis, directionAt(1) - directionAt(0));
        if (speed < 0)
            result.maximumWidth = std::min(result.maximumWidth, glm::length(b - a) / -speed);
    }
    // 与首邻面的有向源边一致：外凸二面角为正，不能以绝对夹角把凹边放行。
    const auto signedAngle = glm::dot(result.sides[0].forward ? axis : -axis,
                                      glm::cross(result.sides[0].normal, result.sides[1].normal));
    if (signedAngle <= kGeometryEpsilon)
        return {{}, "倒角仅支持外凸非共面边；当前为凹边、共面边或近乎回折的边。"};
    for (std::size_t end = 0; end < 2; ++end) {
        const auto& cap = mesh.faces[result.ends[end].face];
        if (!convexNormal(mesh, cap, error))
            return {{}, error};
        const auto index = result.ends[end].corner;
        const auto previous =
            cap.corners[(index + cap.corners.size() - 1) % cap.corners.size()].vertex;
        const auto next = cap.corners[(index + 1) % cap.corners.size()].vertex;
        const auto first =
            mesh.faces[result.sides[0].face].corners[result.sides[0].cuts[end].neighbor].vertex;
        const auto second =
            mesh.faces[result.sides[1].face].corners[result.sides[1].cuts[end].neighbor].vertex;
        if (!((previous == first && next == second) || (previous == second && next == first)))
            return {{}, "倒角端面没有唯一连续的三价角部，未猜测端部拓扑。"};
    }
    result.boundaryEdges = static_cast<std::size_t>(
        std::count_if(topology.halfEdges.begin(), topology.halfEdges.end(), [](const auto& half) {
            return !half.twin;
        }));
    return {std::move(result), {}};
}

MeshCorner interpolateCorner(const MeshCorner& a, const MeshCorner& b, double factor, CornerId id,
                             VertexId vertex) {
    MeshCorner corner;
    corner.id = id;
    corner.vertex = vertex;
    corner.uv = glm::dvec2(a.uv) * (1 - factor) + glm::dvec2(b.uv) * factor;
    corner.color = glm::dvec3(a.color) * (1 - factor) + glm::dvec3(b.color) * factor;
    return corner;
}
} // namespace

BevelEdgeAnalysis analyzeBevelEdge(const EditableMesh& mesh, EdgeKey edge) {
    const auto inspected = inspectEdge(mesh, edge);
    if (!inspected.edge)
        return {{}, inspected.error};
    return {BevelEdgeInfo{edge, inspected.edge->maximumWidth}, {}};
}

BevelEdgeResult bevelEdge(const EditableMesh& before, EdgeKey edge, double width) {
    if (!std::isfinite(width) || width <= 0)
        return {{}, 0, "倒角宽度必须有限且严格大于零。"};
    const auto inspected = inspectEdge(before, edge);
    if (!inspected.edge)
        return {{}, 0, inspected.error};
    const auto& geometry = *inspected.edge;
    if (width >= geometry.maximumWidth)
        return {{}, 0, "倒角宽度必须小于端部边或倒角边的坍缩上限。"};
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
    if (lastId - lastVertex < 4 || lastId - lastFace < 1 || lastId - lastCorner < 12)
        return {{}, 0, "网格元素 ID 空间不足，无法生成单边倒角。"};
    auto result = before;
    std::array<std::array<VertexId, 2>, 2> vertices{};
    std::array<std::array<MeshCorner, 2>, 2> sideCorners{};
    for (std::size_t side = 0; side < 2; ++side) {
        const auto& data = geometry.sides[side];
        const auto& source = before.faces[data.face];
        auto& target = result.faces[data.face];
        for (std::size_t end = 0; end < 2; ++end) {
            const auto& cut = data.cuts[end];
            const auto& a = source.corners[cut.corner];
            const auto& b = source.corners[cut.neighbor];
            const auto pa = glm::dvec3(before.vertex(a.vertex)->position);
            const auto pb = glm::dvec3(before.vertex(b.vertex)->position);
            const auto factor = width / cut.height;
            const auto position = glm::vec3(pa + (pb - pa) * factor);
            const auto actualWidth = glm::dot(data.inward, glm::dvec3(position) - pa);
            if (std::abs(actualWidth - width) > width * 1.0e-4)
                return {{}, 0, "倒角宽度在当前顶点精度下无法保持等距，未修改网格。"};
            vertices[side][end] = ++lastVertex;
            result.vertices.push_back({lastVertex, position});
            auto& corner = sideCorners[side][end];
            corner = interpolateCorner(a, b, factor, ++lastCorner, lastVertex);
            target.corners[cut.corner] = corner;
        }
        for (auto& corner : target.corners)
            corner.normal.reset();
    }
    for (std::size_t end = 0; end < 2; ++end) {
        const auto& data = geometry.ends[end];
        const auto& source = before.faces[data.face];
        const auto count = source.corners.size();
        std::vector<MeshCorner> corners;
        corners.reserve(count + 1);
        for (std::size_t i = 0; i < count; ++i) {
            if (i != data.corner) {
                corners.push_back(source.corners[i]);
                continue;
            }
            // 按原端面环的上一边、下一边顺序插入两个交点，闭合真实端部切口。
            for (const auto neighbor : std::array{(i + count - 1) % count, (i + 1) % count}) {
                const auto& firstSide = geometry.sides[0];
                const auto side =
                    source.corners[neighbor].vertex == before.faces[firstSide.face]
                                                           .corners[firstSide.cuts[end].neighbor]
                                                           .vertex
                        ? 0U
                        : 1U;
                const auto factor = width / geometry.sides[side].cuts[end].height;
                corners.push_back(interpolateCorner(source.corners[i], source.corners[neighbor],
                                                    factor, ++lastCorner, vertices[side][end]));
            }
        }
        for (auto& corner : corners)
            corner.normal.reset();
        result.faces[data.face].corners = std::move(corners);
    }
    EditableFace bevel;
    bevel.id = ++lastFace;
    bevel.material = before.faces[geometry.sides[0].face].material;
    bevel.corners = geometry.sides[0].forward ? std::vector{sideCorners[0][1], sideCorners[0][0],
                                                            sideCorners[1][0], sideCorners[1][1]}
                                              : std::vector{sideCorners[0][0], sideCorners[0][1],
                                                            sideCorners[1][1], sideCorners[1][0]};
    for (auto& corner : bevel.corners)
        corner.id = ++lastCorner;
    result.faces.push_back(std::move(bevel));
    std::erase_if(result.vertices, [edge](const auto& vertex) {
        return vertex.id == edge.first || vertex.id == edge.second;
    });
    const auto validation = validateEditableMesh(result);
    if (!validation.isValid())
        return {{}, 0, "倒角候选无效：" + validation.message};
    if (validation.boundaryEdgeCount != geometry.boundaryEdges)
        return {{}, 0, "倒角候选未闭合端部切口，未修改网格。"};
    for (const auto index :
         std::array{geometry.sides[0].face, geometry.sides[1].face, geometry.ends[0].face,
                    geometry.ends[1].face, result.faces.size() - 1}) {
        std::string error;
        if (!convexNormal(result, result.faces[index], error))
            return {{}, 0, "倒角宽度导致不支持的候选几何：" + error};
    }
    const auto derived = deriveMesh(result);
    if (!derived.derived)
        return {{}, 0, "倒角候选无法派生：" + derived.error};
    return {std::move(result), lastFace, {}};
}
} // namespace mini3d::core::modeling
