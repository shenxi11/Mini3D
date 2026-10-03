/*
 * 模块名: InsetFace
 * 功能概述: 在归一化面平面中求偏移直线交点，并验证边坍缩与候选拓扑。
 * 对外接口: InsetFace.h；依赖关系: MeshDerivation、标准数学/容器。
 * 输入输出: 只读网格到中心面与等宽边框，按原三角表面插值UV/颜色/法线。
 * 异常与错误: 拒绝不支持输入，不发布半成品；不捕获分配异常。
 * 维护说明: 原边界顶点/邻面不动；使用局部原点和double降低平移消减误差。
 */
#include "InsetFace.h"

#include "MeshDerivation.h"

#include <cmath>
#include <limits>

namespace mini3d::core::modeling {
namespace {
struct FaceGeometry {
    std::size_t index;
    glm::dvec3 origin{0};
    double scale = 0;
    InsetFaceInfo info;
    std::vector<glm::dvec3> points, directions;
    DerivedMesh surface;
};
struct Inspection {
    std::optional<FaceGeometry> face;
    std::string error;
};
Inspection inspectFace(const EditableMesh& mesh, FaceId id) {
    const auto found = std::find_if(mesh.faces.begin(), mesh.faces.end(), [id](const auto& face) {
        return face.id == id;
    });
    if (found == mesh.faces.end())
        return {{}, "内插选择包含失效的面 ID。"};
    auto derived = deriveMesh(mesh);
    if (!derived.derived)
        return {{}, derived.error};
    FaceGeometry result;
    result.index = static_cast<std::size_t>(found - mesh.faces.begin());
    result.surface = std::move(*derived.derived);
    result.origin = mesh.vertex(found->corners.front().vertex)->position;
    glm::dvec3 area{0};
    const auto count = found->corners.size();
    for (std::size_t i = 0; i < count; ++i) {
        const auto a = glm::dvec3(mesh.vertex(found->corners[i].vertex)->position) - result.origin;
        const auto b = glm::dvec3(mesh.vertex(found->corners[(i + 1) % count].vertex)->position) -
                       result.origin;
        result.points.push_back(a);
        area += glm::cross(a, b);
        result.scale = std::max(result.scale, glm::length(a));
    }
    result.info.normal = glm::normalize(area);
    std::vector<glm::dvec3> inward;
    for (auto& point : result.points) {
        point /= result.scale;
        const auto height = glm::dot(point, result.info.normal);
        if (std::abs(height) > 1.0e-6)
            return {{}, "面内插仅支持单个共面凸多边形；当前面不共面。"};
        point -= height * result.info.normal;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto edge = result.points[(i + 1) % count] - result.points[i];
        const auto normal = glm::normalize(glm::cross(result.info.normal, edge));
        for (const auto& point : result.points) {
            if (glm::dot(normal, point - result.points[i]) < -1.0e-10)
                return {{}, "面内插仅支持单个共面凸多边形；当前面不是凸面。"};
        }
        inward.push_back(normal);
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto& previous = inward[(i + count - 1) % count];
        const double denominator = 1 + glm::dot(previous, inward[i]);
        if (denominator <= 1.0e-12)
            return {{}, "面内插的相邻边近乎回折，无法求稳定偏移交点。"};
        result.directions.push_back((previous + inward[i]) / denominator);
    }
    double limit = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        const auto edge = result.points[next] - result.points[i];
        const auto speed =
            glm::dot(glm::normalize(edge), result.directions[next] - result.directions[i]);
        if (speed < 0)
            limit = std::min(limit, glm::length(edge) / -speed);
    }
    result.info.maximumThickness = limit * result.scale;
    return {std::move(result), {}};
}

std::optional<MeshCorner> sampleSurface(const FaceGeometry& face, FaceId id,
                                        const glm::dvec3& point) {
    const auto& surface = face.surface;
    for (std::size_t triangle = 0; triangle < surface.triangleSources.size(); ++triangle) {
        if (surface.triangleSources[triangle].face != id)
            continue;
        const auto& a = surface.mesh.vertices[surface.mesh.indices[triangle * 3]];
        const auto& b = surface.mesh.vertices[surface.mesh.indices[triangle * 3 + 1]];
        const auto& c = surface.mesh.vertices[surface.mesh.indices[triangle * 3 + 2]];
        const auto pa = (glm::dvec3(a.position) - face.origin) / face.scale;
        const auto pb = (glm::dvec3(b.position) - face.origin) / face.scale;
        const auto pc = (glm::dvec3(c.position) - face.origin) / face.scale;
        const auto area = glm::dot(glm::cross(pb - pa, pc - pa), face.info.normal);
        const double wa = glm::dot(glm::cross(pb - point, pc - point), face.info.normal) / area;
        const double wb = glm::dot(glm::cross(pc - point, pa - point), face.info.normal) / area;
        const double wc = 1 - wa - wb;
        if (wa < -1.0e-10 || wb < -1.0e-10 || wc < -1.0e-10)
            continue;
        MeshCorner sample;
        sample.uv = glm::dvec2(a.uv) * wa + glm::dvec2(b.uv) * wb + glm::dvec2(c.uv) * wc;
        sample.color =
            glm::dvec3(a.color) * wa + glm::dvec3(b.color) * wb + glm::dvec3(c.color) * wc;
        const auto normal =
            glm::dvec3(a.normal) * wa + glm::dvec3(b.normal) * wb + glm::dvec3(c.normal) * wc;
        sample.normal = glm::length(normal) > 1.0e-12 ? glm::normalize(normal) : face.info.normal;
        return sample;
    }
    return {};
}
} // namespace

InsetFaceAnalysis analyzeInsetFace(const EditableMesh& mesh, FaceId id) {
    const auto inspected = inspectFace(mesh, id);
    if (!inspected.face)
        return {{}, inspected.error};
    return {inspected.face->info, {}};
}
InsetFaceResult insetFace(const EditableMesh& before, FaceId id, double localThickness) {
    const auto inspected = inspectFace(before, id);
    if (!inspected.face)
        return {{}, inspected.error};
    const auto& face = *inspected.face;
    if (!std::isfinite(localThickness) || localThickness <= 0 ||
        localThickness >= face.info.maximumThickness)
        return {{}, "内插厚度必须大于零且小于边坍缩上限；不支持外插。"};
    VertexId lastVertex = 0;
    FaceId lastFace = 0;
    CornerId lastCorner = 0;
    for (const auto& vertex : before.vertices)
        lastVertex = std::max(lastVertex, vertex.id);
    for (const auto& source : before.faces) {
        lastFace = std::max(lastFace, source.id);
        for (const auto& corner : source.corners)
            lastCorner = std::max(lastCorner, corner.id);
    }
    const auto& source = before.faces[face.index];
    const auto count = source.corners.size();
    constexpr auto lastId = std::numeric_limits<std::uint64_t>::max();
    if (count > lastId - lastVertex || count > lastId - lastFace ||
        count > (lastId - lastCorner) / 4)
        return {{}, "网格元素 ID 空间不足，无法生成内插拓扑。"};
    auto result = before;
    auto& center = result.faces[face.index];
    for (std::size_t i = 0; i < count; ++i) {
        const auto point = face.points[i] + face.directions[i] * (localThickness / face.scale);
        auto sample = sampleSurface(face, id, point);
        if (!sample)
            return {{}, "内插厚度导致偏移交点离开原面，未修改网格。"};
        const auto position = face.origin + point * face.scale;
        sample->id = source.corners[i].id;
        sample->vertex = ++lastVertex;
        center.corners[i] = *sample;
        result.vertices.push_back({lastVertex, glm::vec3(position)});
    }
    // push_back面可能使center引用失效，先复制中心面角，之后只用独立副本。
    const auto inner = center.corners;
    for (std::size_t i = 0; i < count; ++i) {
        const auto next = (i + 1) % count;
        EditableFace border;
        border.id = ++lastFace;
        border.material = source.material;
        border.corners = {source.corners[i], source.corners[next], inner[next], inner[i]};
        for (auto& corner : border.corners)
            corner.id = ++lastCorner;
        result.faces.push_back(std::move(border));
    }
    const auto validated = deriveMesh(result);
    if (!validated.derived)
        return {{}, "内插候选无效：" + validated.error};
    return {std::move(result), {}};
}
} // namespace mini3d::core::modeling
