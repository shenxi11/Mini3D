/*
 * 模块名: ComponentPicker
 * 功能概述: 点选和框选共用齐次裁剪与真实三角深度，面命中映射回多边形身份。
 * 对外接口: pickComponent、boxSelectComponents；依赖关系: PrimitiveFactory、GLM。
 * 输入输出: 源几何和逻辑像素查询到稳定身份；X-Ray 仅跳过遮挡。
 * 异常与错误: 隐藏或裁剪外候选拒绝；维护说明: 连续区间求可见边，不离散采样。
 */
#include "ComponentPicker.h"

#include "renderer_gl/PrimitiveFactory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace mini3d::editor {
namespace {
bool inSubtree(const core::Scene& scene, core::EntityId id, core::EntityId root) {
    while (id != 0) {
        if (id == root)
            return true;
        const auto* node = scene.find(id);
        id = node ? node->parent : 0;
    }
    return false;
}
const core::MeshData* geometry(const core::Scene& scene, const assets::AssetManager& assets,
                               const core::SceneNode& node) {
    if (node.editableMesh != 0) {
        return &scene.editableMesh(node.editableMesh)->content->displayedDerived().mesh;
    }
    if (node.meshRenderer) {
        const auto* mesh = assets.mesh(node.meshRenderer->mesh);
        return mesh ? &mesh->data : nullptr;
    }
    switch (node.primitive) {
        case core::PrimitiveKind::Cube: {
            static const auto mesh = renderer_gl::PrimitiveFactory::createCube();
            return &mesh;
        }
        case core::PrimitiveKind::Sphere: {
            static const auto mesh = renderer_gl::PrimitiveFactory::createSphere();
            return &mesh;
        }
        case core::PrimitiveKind::Plane: {
            static const auto mesh = renderer_gl::PrimitiveFactory::createPlane();
            return &mesh;
        }
        case core::PrimitiveKind::Empty:
            return nullptr;
    }
    return nullptr;
}
std::optional<float> triangleHit(const core::Ray& ray, const glm::vec3& a, const glm::vec3& b,
                                 const glm::vec3& c, bool doubleSided) {
    const auto e1 = glm::dvec3(b - a), e2 = glm::dvec3(c - a);
    const auto p = glm::cross(glm::dvec3(ray.direction), e2);
    const auto determinant = glm::dot(e1, p);
    if ((doubleSided ? std::abs(determinant) : determinant) <= 1.0e-12) {
        return std::nullopt;
    }
    const auto offset = glm::dvec3(ray.origin - a);
    const auto u = glm::dot(offset, p) / determinant;
    const auto q = glm::cross(offset, e1);
    const auto v = glm::dot(glm::dvec3(ray.direction), q) / determinant;
    const auto distance = glm::dot(e2, q) / determinant;
    if (u < -1.0e-7 || v < -1.0e-7 || u + v > 1.0 + 1.0e-7 || distance < 0) {
        return std::nullopt;
    }
    return static_cast<float>(distance);
}
core::Ray localRay(const core::Ray& ray, const glm::mat4& inverse) {
    return {glm::vec3(inverse * glm::vec4(ray.origin, 1)),
            glm::vec3(inverse * glm::vec4(ray.direction, 0))};
}
struct Projection {
    glm::dvec2 pixel;
    double depth;
};
struct Interval {
    double first = 0;
    double last = 1;
};
// 将线性不等式 a + t(b-a) >= 0 与当前区间求交；齐次、屏幕和深度裁剪共用。
bool clipInterval(double a, double b, Interval& interval) {
    if (a < 0 && b < 0) {
        return false;
    }
    if (a < 0) {
        interval.first = std::max(interval.first, a / (a - b));
    } else if (b < 0) {
        interval.last = std::min(interval.last, a / (a - b));
    }
    return interval.first <= interval.last;
}
double clipDistance(const glm::dvec4& point, int plane) {
    return point.w + (plane % 2 == 0 ? point[plane / 2] : -point[plane / 2]);
}
Projection projectClip(const glm::dvec4& clip, glm::ivec2 size) {
    const auto ndc = glm::dvec3(clip) / clip.w;
    return {{(ndc.x + 1) * 0.5 * size.x, (1 - ndc.y) * 0.5 * size.y}, ndc.z};
}
std::optional<Projection> project(glm::dvec3 point, const glm::dmat4& matrix, glm::ivec2 size) {
    const auto clip = matrix * glm::dvec4(point, 1);
    if (clip.w <= 0) {
        return std::nullopt;
    }
    for (int plane = 0; plane < 6; ++plane) {
        if (clipDistance(clip, plane) < 0) {
            return std::nullopt;
        }
    }
    return projectClip(clip, size);
}
using Segment = std::array<Projection, 2>;
std::optional<Segment> projectEdge(glm::dvec3 a, glm::dvec3 b, const glm::dmat4& matrix,
                                   glm::ivec2 size) {
    const auto first = matrix * glm::dvec4(a, 1), last = matrix * glm::dvec4(b, 1);
    Interval interval;
    for (int plane = 0; plane < 6; ++plane) {
        if (!clipInterval(clipDistance(first, plane), clipDistance(last, plane), interval)) {
            return std::nullopt;
        }
    }
    const auto p = glm::mix(first, last, interval.first);
    const auto q = glm::mix(first, last, interval.last);
    if (p.w <= 0 || q.w <= 0) {
        return std::nullopt;
    }
    return Segment{projectClip(p, size), projectClip(q, size)};
}
Projection interpolate(const Segment& segment, double t) {
    // z/w 与屏幕坐标均线性；不把屏幕参数直接用于世界坐标插值。
    return {glm::mix(segment[0].pixel, segment[1].pixel, t),
            std::lerp(segment[0].depth, segment[1].depth, t)};
}
double cross(glm::dvec2 a, glm::dvec2 b) {
    return a.x * b.y - a.y * b.x;
}
double screenDistanceSquared(glm::dvec2 first, glm::dvec2 second) {
    const auto delta = first - second;
    return glm::dot(delta, delta);
}
struct ScreenTriangle {
    std::array<Projection, 3> vertices;
    double area;
    glm::dvec3 weights(glm::dvec2 point) const {
        const auto a = cross(vertices[1].pixel - point, vertices[2].pixel - point) / area;
        const auto b = cross(vertices[2].pixel - point, vertices[0].pixel - point) / area;
        return {a, b, 1 - a - b};
    }
    double depth(glm::dvec3 weights) const {
        return glm::dot(weights,
                        glm::dvec3(vertices[0].depth, vertices[1].depth, vertices[2].depth));
    }
};
using ScreenRegion = std::array<glm::dvec2, 2>;
ScreenRegion clickRegion(glm::vec2 pixel) {
    // 包含完整 10 像素命中半径及屏距舍入余量。
    const glm::dvec2 radius{10.000001};
    return {glm::dvec2(pixel) - radius, glm::dvec2(pixel) + radius};
}
/** 同一查询快照：将真实可见场景三角形裁剪后用于所有点/线遮挡测试。 */
class PickQuery {
  public:
    PickQuery(const core::Scene& scene, const assets::AssetManager& assets,
              const renderer_gl::EditorCamera& camera, glm::ivec2 size, bool xRay,
              const core::ViewportVisibility& visibility, core::EntityId excludedRoot = 0,
              std::optional<ScreenRegion> region = {}) {
        if (xRay) {
            return;
        }
        columns_ = std::max(1, (size.x + kBucketSize - 1) / kBucketSize);
        rows_ = std::max(1, (size.y + kBucketSize - 1) / kBucketSize);
        buckets_.resize(static_cast<std::size_t>(columns_) * rows_);
        firstCell_ = region ? cell((*region)[0]) : glm::ivec2{0};
        lastCell_ = region ? cell((*region)[1]) : glm::ivec2{columns_ - 1, rows_ - 1};
        // 查询桶包含完整命中区域；容差上界由裁剪后三角的最大视口跨度给出。
        const auto padding = 2 * kTriangleTolerance * glm::dvec2(size) + glm::dvec2(1.0e-7);
        const auto minimum = glm::dvec2(firstCell_ * kBucketSize) - padding;
        const auto maximum = glm::dvec2((lastCell_ + 1) * kBucketSize) + padding;
        const glm::dvec2 minimumNdc{2 * minimum.x / size.x - 1, 1 - 2 * maximum.y / size.y};
        const glm::dvec2 maximumNdc{2 * maximum.x / size.x - 1, 1 - 2 * minimum.y / size.y};
        // 三角形经六个齐次平面裁剪最多有九个顶点，跨三角复用两个缓冲。
        std::vector<glm::dvec4> polygon, clipped;
        polygon.reserve(9);
        clipped.reserve(9);
        for (const auto& node : scene.nodes()) {
            if (!visibility.isVisible(scene, node.id) || inSubtree(scene, node.id, excludedRoot)) {
                continue;
            }
            const auto* mesh = geometry(scene, assets, node);
            if (!mesh) {
                continue;
            }
            const auto world = scene.worldMatrix(node.id);
            const auto matrix = glm::dmat4(camera.viewProjectionMatrix()) * glm::dmat4(world);
            const auto handedness = glm::determinant(glm::mat3(world)) < 0 ? -1 : 1;
            const auto* material =
                node.meshRenderer ? assets.material(node.meshRenderer->material) : nullptr;
            for (std::size_t i = 0; i < mesh->indices.size(); i += 3) {
                if (node.editableMesh &&
                    !visibility.isTriangleVisible(
                        node.id, *scene.editableMesh(node.editableMesh)->content, i / 3))
                    continue;
                polygon.clear();
                for (int corner = 0; corner < 3; ++corner) {
                    polygon.push_back(
                        matrix * glm::dvec4(mesh->vertices[mesh->indices[i + corner]].position, 1));
                }
                if (region) {
                    bool outside = false;
                    for (int axis = 0; axis < 2; ++axis) {
                        if (std::all_of(polygon.begin(), polygon.end(),
                                        [&](const auto& point) {
                                            return point[axis] - minimumNdc[axis] * point.w < 0;
                                        }) ||
                            std::all_of(polygon.begin(), polygon.end(), [&](const auto& point) {
                                return maximumNdc[axis] * point.w - point[axis] < 0;
                            })) {
                            outside = true;
                            break;
                        }
                    }
                    // 线性齐次半空间拒绝不依赖 w 正负，跨近裁剪面也保持保守。
                    if (outside) {
                        continue;
                    }
                }
                for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
                    // 全部顶点已在该齐次半空间内时，裁剪结果就是原多边形。
                    if (std::all_of(polygon.begin(), polygon.end(), [plane](const auto& point) {
                            return clipDistance(point, plane) >= 0;
                        }))
                        continue;
                    clipped.clear();
                    auto previous = polygon.back();
                    auto previousDistance = clipDistance(previous, plane);
                    for (const auto& current : polygon) {
                        const auto distance = clipDistance(current, plane);
                        if ((distance < 0) != (previousDistance < 0)) {
                            clipped.push_back(
                                glm::mix(previous, current,
                                         previousDistance / (previousDistance - distance)));
                        }
                        if (distance >= 0) {
                            clipped.push_back(current);
                        }
                        previous = current;
                        previousDistance = distance;
                    }
                    polygon.swap(clipped);
                }
                for (std::size_t corner = 1; corner + 1 < polygon.size(); ++corner) {
                    ScreenTriangle triangle{{projectClip(polygon[0], size),
                                             projectClip(polygon[corner], size),
                                             projectClip(polygon[corner + 1], size)},
                                            0};
                    triangle.area = cross(triangle.vertices[1].pixel - triangle.vertices[0].pixel,
                                          triangle.vertices[2].pixel - triangle.vertices[0].pixel);
                    if (std::abs(triangle.area) > 1.0e-12 &&
                        ((material && material->doubleSided) || triangle.area * handedness < 0)) {
                        addTriangle(triangle);
                    }
                }
            }
        }
    }
    std::vector<Interval> visibleIntervals(const Segment& segment, Interval initial = {}) const {
        std::vector<Interval> visible{initial};
        for (const auto index : candidateTriangles(segment, initial)) {
            const auto& triangle = triangles_[index];
            const auto a = triangle.weights(segment[0].pixel);
            const auto b = triangle.weights(segment[1].pixel);
            Interval hidden = initial;
            bool intersects = true;
            for (int axis = 0; axis < 3; ++axis) {
                intersects = intersects && clipInterval(a[axis] + kTriangleTolerance,
                                                        b[axis] + kTriangleTolerance, hidden);
            }
            // 与可见点共用 NDC 深度容差，避免共面源笼自遮挡。
            if (!intersects ||
                !clipInterval(segment[0].depth - triangle.depth(a) - kDepthTolerance,
                              segment[1].depth - triangle.depth(b) - kDepthTolerance, hidden)) {
                continue;
            }
            std::vector<Interval> remaining;
            for (const auto interval : visible) {
                if (hidden.last < interval.first || hidden.first > interval.last) {
                    remaining.push_back(interval);
                } else {
                    if (interval.first < hidden.first) {
                        remaining.push_back(
                            {interval.first, std::nextafter(hidden.first, interval.first)});
                    }
                    if (interval.last > hidden.last) {
                        remaining.push_back(
                            {std::nextafter(hidden.last, interval.last), interval.last});
                    }
                }
            }
            visible = std::move(remaining);
            if (visible.empty()) {
                break;
            }
        }
        return visible;
    }
    bool isVisible(const Projection& point) const {
        if (triangles_.empty())
            return true;
        // 退化线段的区间裁剪等价于这些点不等式，避免每个点分配区间容器。
        for (const auto index : bucket(cell(point.pixel))) {
            const auto& triangle = triangles_[index];
            const auto weights = triangle.weights(point.pixel);
            if (weights.x + kTriangleTolerance >= 0 && weights.y + kTriangleTolerance >= 0 &&
                weights.z + kTriangleTolerance >= 0 &&
                point.depth - triangle.depth(weights) - kDepthTolerance >= 0)
                return false;
        }
        return true;
    }

  private:
    static constexpr int kBucketSize = 16;
    static constexpr double kTriangleTolerance = 1.0e-9;
    static constexpr double kDepthTolerance = 2.0e-6;
    glm::ivec2 cell(glm::dvec2 pixel) const {
        return {std::clamp(static_cast<int>(std::floor(pixel.x / kBucketSize)), 0, columns_ - 1),
                std::clamp(static_cast<int>(std::floor(pixel.y / kBucketSize)), 0, rows_ - 1)};
    }
    const std::vector<std::size_t>& bucket(glm::ivec2 cell) const {
        return buckets_[static_cast<std::size_t>(cell.y) * columns_ + cell.x];
    }
    void addTriangle(const ScreenTriangle& triangle) {
        auto minimum = triangle.vertices[0].pixel, maximum = minimum;
        for (int corner = 1; corner < 3; ++corner) {
            minimum = glm::min(minimum, triangle.vertices[corner].pixel);
            maximum = glm::max(maximum, triangle.vertices[corner].pixel);
        }
        // 权重各 >= -epsilon 且总和为 1，容差区域最多越过 AABB 两倍 epsilon*跨度。
        // 额外留出浮点舍入余量；分桶只缩小候选，最终仍使用原始重心/深度判定。
        const auto padding = 2 * kTriangleTolerance * (maximum - minimum) + glm::dvec2(1.0e-7);
        const auto first = glm::max(cell(minimum - padding), firstCell_);
        const auto last = glm::min(cell(maximum + padding), lastCell_);
        if (first.x > last.x || first.y > last.y) {
            return;
        }
        const auto index = triangles_.size();
        triangles_.push_back(triangle);
        for (int y = first.y; y <= last.y; ++y)
            for (int x = first.x; x <= last.x; ++x)
                buckets_[static_cast<std::size_t>(y) * columns_ + x].push_back(index);
    }
    std::vector<std::size_t> candidateTriangles(const Segment& segment, Interval interval) const {
        if (triangles_.empty())
            return {};
        const auto a = interpolate(segment, interval.first).pixel;
        const auto b = interpolate(segment, interval.last).pixel;
        const auto first = glm::max(cell(glm::min(a, b)), firstCell_);
        const auto last = glm::min(cell(glm::max(a, b)), lastCell_);
        std::vector<std::size_t> result;
        for (int y = first.y; y <= last.y; ++y)
            for (int x = first.x; x <= last.x; ++x) {
                const auto& candidates = bucket({x, y});
                result.insert(result.end(), candidates.begin(), candidates.end());
            }
        // 一条长边可跨多个桶；去重后恢复原三角顺序，连续区间扣除顺序不变。
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
    int columns_ = 0;
    int rows_ = 0;
    glm::ivec2 firstCell_{0};
    glm::ivec2 lastCell_{0};
    std::vector<std::vector<std::size_t>> buckets_;
    std::vector<ScreenTriangle> triangles_;
};
std::unordered_map<core::modeling::VertexId, glm::dvec3>
positions(const core::modeling::EditableMesh& mesh) {
    std::unordered_map<core::modeling::VertexId, glm::dvec3> result;
    result.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        result.emplace(vertex.id, vertex.position);
    }
    return result;
}
} // namespace

std::optional<VertexSnapHit>
pickVertexSnap(const core::Scene& scene, const assets::AssetManager& assets,
               const renderer_gl::EditorCamera& camera, glm::vec2 pixel, glm::ivec2 viewportSize,
               core::EntityId source, bool objectMode,
               const std::set<core::modeling::VertexId>& excludedVertices,
               const core::ViewportVisibility& visibility) {
    if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || pixel.x < 0 || pixel.y < 0 ||
        pixel.x >= viewportSize.x || pixel.y >= viewportSize.y)
        return {};
    // 移动对象不遮挡其目标，避免前一帧预览位置导致目标交替命中/丢失。
    const PickQuery query(scene, assets, camera, viewportSize, false, visibility,
                          objectMode ? source : 0, clickRegion(pixel));
    std::optional<VertexSnapHit> result;
    double bestScreen = 100, bestDepth = std::numeric_limits<double>::infinity();
    for (const auto& node : scene.nodes()) {
        if (!visibility.isVisible(scene, node.id) ||
            (objectMode && inSubtree(scene, node.id, source)))
            continue;
        const auto* editable =
            node.editableMesh ? scene.editableMesh(node.editableMesh)->content.get() : nullptr;
        // 细分新点由多个源点共同驱动，不能伪装为一个源点来排除；组件自吸附排除整个求值源。
        if (node.id == source && editable && editable->subdivisionEvaluation)
            continue;
        const auto world = glm::dmat4(scene.worldMatrix(node.id));
        const auto matrix = glm::dmat4(camera.viewProjectionMatrix()) * world;
        const auto consider = [&](std::uint64_t id, glm::dvec3 position) {
            const auto* content = node.editableMesh
                                      ? scene.editableMesh(node.editableMesh)->content.get()
                                      : nullptr;
            const auto sourceId =
                content && content->mirrorEvaluation && !content->subdivisionEvaluation
                    ? content->mirrorEvaluation->vertices.at(id).source
                    : id;
            if (node.id == visibility.editedEntity && node.editableMesh &&
                !visibility.isVertexVisible(content->source, sourceId))
                return;
            if (node.id == source && excludedVertices.contains(sourceId))
                return;
            const auto projected = project(position, matrix, viewportSize);
            if (!projected)
                return;
            const auto offset = projected->pixel - glm::dvec2(pixel);
            const auto distance = glm::dot(offset, offset);
            const bool better =
                distance < bestScreen || (distance == bestScreen &&
                                          (projected->depth < bestDepth ||
                                           (projected->depth == bestDepth &&
                                            (!result || node.id < result->entity ||
                                             (node.id == result->entity && id < result->vertex)))));
            if (!better || !query.isVisible(*projected))
                return;
            result = VertexSnapHit{node.id, id, glm::vec3(world * glm::dvec4(position, 1))};
            bestScreen = distance;
            bestDepth = projected->depth;
        };
        if (node.editableMesh) {
            const auto& content = *scene.editableMesh(node.editableMesh)->content;
            const auto& mesh = content.evaluatedMesh();
            for (const auto& vertex : mesh.vertices)
                consider(vertex.id, vertex.position);
        } else if (const auto* mesh = geometry(scene, assets, node)) {
            for (std::size_t i = 0; i < mesh->vertices.size(); ++i)
                consider(i + 1, mesh->vertices[i].position);
        }
    }
    return result;
}

std::optional<ComponentId>
pickComponent(const core::Scene& scene, const assets::AssetManager& assets, core::EntityId entity,
              SelectionDomain domain, const renderer_gl::EditorCamera& camera, glm::vec2 pixel,
              glm::ivec2 viewportSize, bool xRay, const core::ViewportVisibility& visibility) {
    const auto* node = scene.find(entity);
    if (!node || !node->editableMesh || !visibility.isVisible(scene, entity) || pixel.x < 0 ||
        pixel.y < 0 || pixel.x >= viewportSize.x || pixel.y >= viewportSize.y) {
        return std::nullopt;
    }
    const PickQuery query(scene, assets, camera, viewportSize, xRay, visibility, 0,
                          domain == SelectionDomain::Face ? std::nullopt
                                                          : std::optional(clickRegion(pixel)));
    const auto& content = *scene.editableMesh(node->editableMesh)->content;
    const auto& mesh = content.source;
    const auto world = scene.worldMatrix(entity);
    const auto matrix = glm::dmat4(camera.viewProjectionMatrix()) * glm::dmat4(world);
    std::optional<ComponentId> result;
    double bestScreen = 100.0, bestDepth = std::numeric_limits<double>::infinity();
    const auto consider = [&](ComponentId id, const Projection& projected) {
        const auto screenDistance = screenDistanceSquared(glm::dvec2(pixel), projected.pixel);
        const auto depth = projected.depth;
        const bool better =
            screenDistance < bestScreen ||
            (screenDistance == bestScreen &&
             (depth < bestDepth || (depth == bestDepth && (!result || id < *result))));
        if (better) {
            result = id;
            bestScreen = screenDistance;
            bestDepth = depth;
        }
    };
    if (domain == SelectionDomain::Face) {
        const auto ray = camera.screenRay(pixel.x, pixel.y);
        const auto local = localRay(ray, glm::inverse(world));
        const auto& data = content.derived.mesh;
        for (std::size_t i = 0; i < data.indices.size(); i += 3) {
            if (entity == visibility.editedEntity && visibility.hasHiddenElements() &&
                !visibility.isFaceVisible(mesh, content.derived.triangleSources[i / 3].face))
                continue;
            const auto distance = triangleHit(local, data.vertices[data.indices[i]].position,
                                              data.vertices[data.indices[i + 1]].position,
                                              data.vertices[data.indices[i + 2]].position, xRay);
            if (distance) {
                const auto point = ray.origin + *distance * ray.direction;
                const auto projected =
                    project(point, glm::dmat4(camera.viewProjectionMatrix()), viewportSize);
                if (projected && query.isVisible(*projected)) {
                    // 同一射线的所有面屏距都为零，避免投影舍入改变前后面优先级。
                    consider({content.derived.triangleSources[i / 3].face},
                             {glm::dvec2(pixel), projected->depth});
                }
            }
        }
    } else if (domain == SelectionDomain::Vertex) {
        for (const auto& vertex : mesh.vertices) {
            if (entity == visibility.editedEntity && !visibility.isVertexVisible(mesh, vertex.id))
                continue;
            const auto projected = project(vertex.position, matrix, viewportSize);
            if (projected &&
                screenDistanceSquared(glm::dvec2(pixel), projected->pixel) <= bestScreen &&
                query.isVisible(*projected)) {
                consider({vertex.id}, *projected);
            }
        }
    } else {
        const auto points = positions(mesh);
        for (const auto id : ComponentSelection::elements(mesh, SelectionDomain::Edge)) {
            if (entity == visibility.editedEntity &&
                !visibility.isEdgeVisible(mesh, {id.first, id.second}))
                continue;
            const auto edge =
                projectEdge(points.at(id.first), points.at(id.second), matrix, viewportSize);
            if (!edge) {
                continue;
            }
            const auto delta = (*edge)[1].pixel - (*edge)[0].pixel;
            const auto lengthSquared = glm::dot(delta, delta);
            const auto t =
                lengthSquared > 0
                    ? glm::dot(glm::dvec2(pixel) - (*edge)[0].pixel, delta) / lengthSquared
                    : ((*edge)[0].depth < (*edge)[1].depth ? 0.0 : 1.0);
            // 可见部分属于完整裁剪边；整边最小屏距超限时无需求连续遮挡区间。
            const auto closest = interpolate(*edge, std::clamp(t, 0.0, 1.0));
            if (screenDistanceSquared(glm::dvec2(pixel), closest.pixel) > bestScreen + 1.0e-9)
                continue;
            for (const auto interval : query.visibleIntervals(*edge)) {
                consider(id, interpolate(*edge, std::clamp(t, interval.first, interval.last)));
            }
        }
    }
    return result;
}

CursorPlacement locateCursor(const core::Scene& scene, const assets::AssetManager& assets,
                             const renderer_gl::EditorCamera& camera, glm::vec2 pixel,
                             glm::ivec2 viewportSize, const core::ViewportVisibility& visibility) {
    if (viewportSize.x <= 0 || viewportSize.y <= 0 || !std::isfinite(pixel.x) ||
        !std::isfinite(pixel.y) || pixel.x < 0 || pixel.y < 0 || pixel.x >= viewportSize.x ||
        pixel.y >= viewportSize.y) {
        return {{}, false, QStringLiteral("请在有效的 3D 视口内定位游标。")};
    }
    const auto ray = camera.screenRay(pixel.x, pixel.y);
    const auto matrix = glm::dmat4(camera.viewProjectionMatrix());
    float closest = std::numeric_limits<float>::infinity();
    std::optional<glm::vec3> hit;
    for (const auto& node : scene.nodes()) {
        if (!visibility.isVisible(scene, node.id))
            continue;
        const auto* mesh = geometry(scene, assets, node);
        if (!mesh)
            continue;
        const auto local = localRay(ray, glm::inverse(scene.worldMatrix(node.id)));
        const auto* material =
            node.meshRenderer ? assets.material(node.meshRenderer->material) : nullptr;
        for (std::size_t i = 0; i < mesh->indices.size(); i += 3) {
            if (node.editableMesh &&
                !visibility.isTriangleVisible(
                    node.id, *scene.editableMesh(node.editableMesh)->content, i / 3))
                continue;
            const auto distance = triangleHit(local, mesh->vertices[mesh->indices[i]].position,
                                              mesh->vertices[mesh->indices[i + 1]].position,
                                              mesh->vertices[mesh->indices[i + 2]].position,
                                              material && material->doubleSided);
            if (distance && *distance < closest) {
                const auto point = ray.origin + *distance * ray.direction;
                if (core::Cursor3D{point}.isValid() && project(point, matrix, viewportSize)) {
                    closest = *distance;
                    hit = point;
                }
            }
        }
    }
    if (hit)
        return {hit, true, {}};
    if (std::abs(ray.direction.y) < 1.0e-4F) {
        return {
            {},
            false,
            QStringLiteral("视线接近平行于 XZ 地面，无法定位；请转动视角或在 N 面板输入坐标。")};
    }
    const auto distance = -ray.origin.y / ray.direction.y;
    const auto point = ray.origin + distance * ray.direction;
    if (distance < 0 || !core::Cursor3D{point}.isValid() || !project(point, matrix, viewportSize)) {
        return {
            {}, false, QStringLiteral("XZ 地面交点位于视野外；请调整视角或在 N 面板输入坐标。")};
    }
    return {point, false, {}};
}

std::set<ComponentId> boxSelectComponents(const core::Scene& scene,
                                          const assets::AssetManager& assets, core::EntityId entity,
                                          SelectionDomain domain,
                                          const renderer_gl::EditorCamera& camera, glm::vec2 first,
                                          glm::vec2 second, glm::ivec2 viewportSize, bool xRay,
                                          const core::ViewportVisibility& visibility) {
    const auto* node = scene.find(entity);
    if (!node || !node->editableMesh || !visibility.isVisible(scene, entity) ||
        viewportSize.x <= 0 || viewportSize.y <= 0) {
        return {};
    }
    const auto minimum = glm::max(glm::dvec2(glm::min(first, second)), glm::dvec2(0));
    const auto maximum = glm::min(glm::dvec2(glm::max(first, second)), glm::dvec2(viewportSize));
    if (minimum.x > maximum.x || minimum.y > maximum.y) {
        return {};
    }
    const PickQuery query(scene, assets, camera, viewportSize, xRay, visibility, 0,
                          ScreenRegion{minimum, maximum});
    const auto& mesh = scene.editableMesh(node->editableMesh)->content->source;
    const auto matrix =
        glm::dmat4(camera.viewProjectionMatrix()) * glm::dmat4(scene.worldMatrix(entity));
    std::set<ComponentId> result;
    const auto consider = [&](ComponentId id, glm::dvec3 position) {
        if (entity == visibility.editedEntity &&
            (domain == SelectionDomain::Vertex ? !visibility.isVertexVisible(mesh, id.first)
                                               : !visibility.isFaceVisible(mesh, id.first)))
            return;
        const auto point = project(position, matrix, viewportSize);
        if (point && point->pixel.x >= minimum.x && point->pixel.x <= maximum.x &&
            point->pixel.y >= minimum.y && point->pixel.y <= maximum.y && query.isVisible(*point)) {
            result.insert(id);
        }
    };
    if (domain == SelectionDomain::Vertex) {
        for (const auto& vertex : mesh.vertices) {
            consider({vertex.id}, vertex.position);
        }
    } else if (domain == SelectionDomain::Face) {
        const auto points = positions(mesh);
        for (const auto& face : mesh.faces) {
            glm::dvec3 center{0};
            for (const auto& corner : face.corners) {
                center += points.at(corner.vertex);
            }
            consider({face.id}, center / static_cast<double>(face.corners.size()));
        }
    } else {
        const auto points = positions(mesh);
        for (const auto id : ComponentSelection::elements(mesh, SelectionDomain::Edge)) {
            if (entity == visibility.editedEntity &&
                !visibility.isEdgeVisible(mesh, {id.first, id.second}))
                continue;
            const auto segment =
                projectEdge(points.at(id.first), points.at(id.second), matrix, viewportSize);
            if (!segment) {
                continue;
            }
            Interval interval;
            bool intersects = true;
            for (int axis = 0; axis < 2; ++axis) {
                intersects = intersects &&
                             clipInterval((*segment)[0].pixel[axis] - minimum[axis],
                                          (*segment)[1].pixel[axis] - minimum[axis], interval) &&
                             clipInterval(maximum[axis] - (*segment)[0].pixel[axis],
                                          maximum[axis] - (*segment)[1].pixel[axis], interval);
            }
            if (intersects && !query.visibleIntervals(*segment, interval).empty()) {
                result.insert(id);
            }
        }
    }
    return result;
}
} // namespace mini3d::editor
