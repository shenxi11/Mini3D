/*
 * 模块名: MirrorTests
 * 功能概述: 验证单轴镜像、来源只读性、对应边界合并和有状态中心夹持。
 * 对外接口: Catch2 [mirror]；依赖关系: Core/GLM，无 Qt/GL。
 * 输入输出: 半网格/独立部件/非法候选到几何、来源、状态和 Scene 原子性断言。
 * 异常与错误: 不符即失败；维护说明: 真正界面/保存修改器参数的验收属于 MIRROR-02。
 */
#include "core/Scene.h"
#include "core/SceneSerializer.h"
#include "core/modeling/MeshTopology.h"
#include "core/modeling/Mirror.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <limits>

using namespace mini3d::core;
using namespace mini3d::core::modeling;
namespace {
EditableMesh halfCube() {
    auto mesh = createEditableCube();
    for (auto& vertex : mesh.vertices)
        if (vertex.position.x < 0)
            vertex.position.x = 0;
    std::erase_if(mesh.faces, [&](const auto& face) {
        return std::all_of(face.corners.begin(), face.corners.end(), [&](const auto& corner) {
            return mesh.vertex(corner.vertex)->position.x == 0;
        });
    });
    return mesh;
}
EditableMesh halfQuad(float start = 0) {
    EditableMesh mesh;
    mesh.vertices = {{1, {start, -1, 0}}, {2, {1, -1, 0}}, {3, {1, 1, 0}}, {4, {start, 1, 0}}};
    EditableFace face;
    face.id = 1;
    for (std::uint64_t i = 1; i <= 4; ++i) {
        MeshCorner corner;
        corner.id = i;
        corner.vertex = i;
        corner.uv = {static_cast<float>(i), .25F};
        corner.normal = glm::vec3(0, 0, 1);
        corner.color = {.2F, .4F, .6F};
        face.corners.push_back(corner);
    }
    mesh.faces.push_back(face);
    return mesh;
}
void setPosition(EditableMesh& mesh, VertexId id, glm::vec3 position) {
    for (auto& vertex : mesh.vertices)
        if (vertex.id == id)
            vertex.position = position;
}
void setLeft(EditableMesh& mesh, float x) {
    for (auto& vertex : mesh.vertices)
        if (vertex.id == 1 || vertex.id == 4)
            vertex.position.x = x;
}
void offsetIds(EditableMesh& mesh, std::uint64_t offset) {
    for (auto& vertex : mesh.vertices)
        vertex.id += offset;
    for (auto& face : mesh.faces) {
        face.id += offset;
        for (auto& corner : face.corners) {
            corner.id += offset;
            corner.vertex += offset;
        }
    }
}
const EditableFace& faceById(const EditableMesh& mesh, FaceId id) {
    const auto it = std::find_if(mesh.faces.begin(), mesh.faces.end(), [id](const auto& face) {
        return face.id == id;
    });
    REQUIRE(it != mesh.faces.end());
    return *it;
}
void requireNormalsFollowWinding(const MirrorEvaluation& evaluation) {
    const auto& data = evaluation.derived.mesh;
    for (std::size_t i = 0; i < data.indices.size(); i += 3) {
        const auto a = data.vertices[data.indices[i]].position;
        const auto b = data.vertices[data.indices[i + 1]].position;
        const auto c = data.vertices[data.indices[i + 2]].position;
        const auto normal = glm::normalize(glm::cross(b - a, c - a));
        for (std::size_t j = 0; j < 3; ++j)
            REQUIRE(glm::dot(normal, data.vertices[data.indices[i + j]].normal) > .999F);
    }
}
} // namespace

TEST_CASE("Mirror joins only matching center boundaries and preserves the source", "[mirror]") {
    const auto source = halfCube();
    REQUIRE(validateEditableMesh(source).boundaryEdgeCount == 4);
    const auto before = source;
    const auto result = evaluateMirror(source, {});
    INFO(result.error);
    REQUIRE(result.evaluation);
    const auto& evaluated = *result.evaluation;
    REQUIRE(source == before);
    REQUIRE(evaluated.mesh.vertices.size() == 12);
    REQUIRE(evaluated.mesh.faces.size() == 10);
    REQUIRE(validateEditableMesh(evaluated.mesh).isValid());
    REQUIRE(validateEditableMesh(evaluated.mesh).boundaryEdgeCount == 0);
    REQUIRE(evaluated.derived.mesh.indices.size() == 60);
    for (const auto& vertex : source.vertices) {
        REQUIRE(evaluated.mesh.vertex(vertex.id)->position == vertex.position);
        REQUIRE(evaluated.vertices.at(vertex.id) == MirrorElementSource{vertex.id, false});
    }
    for (const auto& face : source.faces)
        REQUIRE(faceById(evaluated.mesh, face.id) == face);
    std::size_t sharedMirrorCorners = 0;
    for (const auto& face : evaluated.mesh.faces) {
        if (!evaluated.faces.at(face.id).mirrored)
            continue;
        for (const auto& corner : face.corners) {
            REQUIRE(evaluated.corners.at(corner.id).mirrored);
            if (!evaluated.vertices.at(corner.vertex).mirrored) {
                REQUIRE(evaluated.mesh.vertex(corner.vertex)->position.x == 0);
                ++sharedMirrorCorners;
            }
        }
    }
    REQUIRE(sharedMirrorCorners == 8);
    requireNormalsFollowWinding(evaluated);
    MirrorOptions options;
    options.merge = false;
    const auto separate = evaluateMirror(source, options);
    REQUIRE(separate.evaluation);
    REQUIRE(separate.evaluation->mesh.vertices.size() == 16);
    REQUIRE(validateEditableMesh(separate.evaluation->mesh).boundaryEdgeCount == 8);
    options.enabled = false;
    const auto disabled = evaluateMirror(source, options);
    REQUIRE(disabled.evaluation);
    REQUIRE(disabled.evaluation->mesh == source);
    for (const auto& [id, origin] : disabled.evaluation->faces)
        REQUIRE_FALSE(origin.mirrored);
}

TEST_CASE("Mirror all local axes reflect normals winding attributes and readonly render origins",
          "[mirror]") {
    auto source = createEditableCube();
    for (auto& face : source.faces)
        face.material = 37;
    for (const auto axis : {MirrorAxis::X, MirrorAxis::Y, MirrorAxis::Z}) {
        const auto result = evaluateMirror(source, MirrorOptions{axis});
        REQUIRE(result.evaluation);
        const auto& evaluated = *result.evaluation;
        REQUIRE(evaluated.mesh.vertices.size() == 16); // 闭合网格不自动切半、不焊内部近点。
        REQUIRE(evaluated.mesh.faces.size() == 12);
        for (const auto& [id, origin] : evaluated.vertices) {
            auto expected = source.vertex(origin.source)->position;
            if (origin.mirrored)
                expected[static_cast<int>(axis)] *= -1;
            REQUIRE(evaluated.mesh.vertex(id)->position == expected);
        }
        for (const auto& face : evaluated.mesh.faces) {
            const auto origin = evaluated.faces.at(face.id);
            const auto& original = faceById(source, origin.source);
            REQUIRE(face.material == original.material);
            for (std::size_t i = 0; i < face.corners.size(); ++i) {
                const auto& corner = face.corners[i];
                const auto& expected =
                    original.corners[origin.mirrored ? face.corners.size() - 1 - i : i];
                REQUIRE(evaluated.corners.at(corner.id) ==
                        MirrorElementSource{expected.id, origin.mirrored});
                REQUIRE(corner.uv == expected.uv);
                REQUIRE(corner.color == expected.color);
                auto normal = expected.normal;
                if (normal && origin.mirrored)
                    (*normal)[static_cast<int>(axis)] *= -1;
                REQUIRE(corner.normal == normal);
            }
        }
        REQUIRE(evaluated.derived.triangleSources.size() == 24);
        std::size_t mirroredTriangles = 0;
        for (const auto& triangle : evaluated.derived.triangleSources) {
            const auto origin = evaluated.faces.at(triangle.face);
            REQUIRE(triangle.material == 37);
            REQUIRE(origin.source != 0);
            mirroredTriangles += origin.mirrored;
        }
        REQUIRE(mirroredTriangles == 12);
        for (const auto& renderSource : evaluated.derived.vertexSources) {
            const auto corner = evaluated.corners.at(renderSource.corner);
            const auto vertex = evaluated.vertices.at(renderSource.vertex);
            REQUIRE(corner.mirrored == vertex.mirrored);
        }
        requireNormalsFollowWinding(evaluated);
    }
}

TEST_CASE("Mirror threshold projection does not weld close independent parts or isolated points",
          "[mirror]") {
    auto mesh = halfQuad(.0005F);
    mesh.vertices.push_back({5, {0, 3, 0}}); // 中心孤立点不是焊接边界。
    auto other = halfQuad(.0005F);
    offsetIds(other, 10);
    for (auto& vertex : other.vertices)
        vertex.position.z += .0001F; // 比阈值还近的另一个独立部件。
    mesh.vertices.insert(mesh.vertices.end(), other.vertices.begin(), other.vertices.end());
    mesh.faces.insert(mesh.faces.end(), other.faces.begin(), other.faces.end());
    const auto result = evaluateMirror(mesh, {});
    REQUIRE(result.evaluation);
    const auto& evaluated = *result.evaluation;
    REQUIRE(evaluated.mesh.vertices.size() == 14);
    REQUIRE(evaluated.mesh.vertex(1)->position.x == 0);
    REQUIRE(evaluated.mesh.vertex(11)->position.x == 0);
    REQUIRE(evaluated.mesh.vertex(1)->position != evaluated.mesh.vertex(11)->position);
    REQUIRE(evaluated.vertices.at(1).source != evaluated.vertices.at(11).source);
    REQUIRE(evaluated.mesh.faces.size() == 4);
    REQUIRE(mesh.vertex(1)->position.x == .0005F);
    for (const auto& face : evaluated.mesh.faces)
        for (const auto& corner : face.corners)
            REQUIRE_FALSE(corner.normal); // 中心投影改变了面形，硬法线不得沿用。
    MirrorOptions exact;
    exact.threshold = 0;
    REQUIRE(evaluateMirror(mesh, exact).evaluation->mesh.vertices.size() == 18);
    auto planar = halfQuad();
    for (auto& vertex : planar.vertices) {
        vertex.position.z = vertex.position.x;
        vertex.position.x = 0;
    }
    for (auto& corner : planar.faces.front().corners)
        corner.normal.reset();
    const auto plane = evaluateMirror(planar, {});
    REQUIRE(plane.evaluation);
    REQUIRE(plane.evaluation->mesh.faces.size() == 1); // 不制造重合反向面。
    REQUIRE(plane.evaluation->mesh.vertices.size() == 4);
}

TEST_CASE("Mirror generated IDs remain stable under reordering and support wide identities",
          "[mirror]") {
    auto source = halfCube();
    offsetIds(source, std::uint64_t{1} << 54);
    const auto first = evaluateMirror(source, {});
    REQUIRE(first.evaluation);
    std::reverse(source.vertices.begin(), source.vertices.end());
    std::reverse(source.faces.begin(), source.faces.end());
    const auto second = evaluateMirror(source, {});
    REQUIRE(second.evaluation);
    REQUIRE(first.evaluation->vertices == second.evaluation->vertices);
    REQUIRE(first.evaluation->faces == second.evaluation->faces);
    REQUIRE(first.evaluation->corners == second.evaluation->corners);
    for (const auto& vertex : first.evaluation->mesh.vertices)
        REQUIRE(vertex.position == second.evaluation->mesh.vertex(vertex.id)->position);
    for (const auto& face : first.evaluation->mesh.faces)
        REQUIRE(face == faceById(second.evaluation->mesh, face.id));
}

TEST_CASE("Mirror rejects invalid options geometry exhausted IDs and collapsed seam atomically",
          "[mirror]") {
    const auto source = halfQuad(.1F);
    for (const double threshold : {-1.0, std::numeric_limits<double>::infinity(),
                                   std::numeric_limits<double>::quiet_NaN()}) {
        MirrorOptions options;
        options.threshold = threshold;
        const auto result = evaluateMirror(source, options);
        REQUIRE_FALSE(result.evaluation);
        REQUIRE_FALSE(result.error.empty());
    }
    MirrorOptions invalidAxis;
    invalidAxis.axis = static_cast<MirrorAxis>(3);
    REQUIRE_FALSE(evaluateMirror(source, invalidAxis).evaluation);
    auto invalid = source;
    invalid.vertices[1].id = invalid.vertices[0].id;
    REQUIRE_FALSE(evaluateMirror(invalid, {}).evaluation);
    MirrorOptions collapse;
    collapse.threshold = 2;
    REQUIRE_FALSE(evaluateMirror(source, collapse).evaluation);
    for (int domain = 0; domain < 3; ++domain) {
        auto exhausted = source;
        const auto last = std::numeric_limits<std::uint64_t>::max();
        if (domain == 0) {
            exhausted.vertices[0].id = last;
            exhausted.faces[0].corners[0].vertex = last;
        } else if (domain == 1) {
            exhausted.faces[0].id = last;
        } else {
            exhausted.faces[0].corners[0].id = last;
        }
        const auto before = exhausted;
        const auto result = evaluateMirror(exhausted, {});
        REQUIRE_FALSE(result.evaluation);
        REQUIRE_FALSE(result.error.empty());
        REQUIRE(exhausted == before);
    }
    const auto empty = evaluateMirror({}, {});
    REQUIRE(empty.evaluation);
    REQUIRE(empty.evaluation->mesh.vertices.empty());
    REQUIRE(empty.evaluation->derived.mesh.indices.empty());
}

TEST_CASE("Clipping captures near or crossing boundary vertices and retains accepted locks",
          "[mirror]") {
    for (const float start : {.1F, -.1F, .0005F, -.0005F}) {
        auto before = halfQuad(start);
        std::string error;
        auto session = MirrorClipSession::begin(before, {1, 4}, {}, error);
        REQUIRE(session);
        REQUIRE(error.empty());
        REQUIRE(session->constrainedVertices().size() == (std::abs(start) <= .001F ? 2 : 0));
        auto crossing = before;
        setLeft(crossing, -std::copysign(.2F, start));
        const auto result = session->constrain(crossing);
        INFO(result.error);
        REQUIRE(result.mesh);
        REQUIRE(result.mesh->vertex(1)->position.x == 0);
        REQUIRE(result.mesh->vertex(4)->position.x == 0);
        REQUIRE(session->constrainedVertices() == std::set<VertexId>{1, 4});
        auto retreat = before;
        setLeft(retreat, .4F);
        retreat.vertices[0].position.y = -1.2F;
        const auto locked = session->constrain(retreat);
        REQUIRE(locked.mesh);
        REQUIRE(locked.mesh->vertex(1)->position == glm::vec3(0, -1.2F, 0));
        REQUIRE(locked.mesh->vertices.size() == before.vertices.size());
        REQUIRE(locked.mesh->faces[0].corners.size() == before.faces[0].corners.size());
        REQUIRE(before.vertex(1)->position.x == start);
    }
}

TEST_CASE("Clipping is independent of Merge and only constrains selected boundary vertices",
          "[mirror]") {
    const auto before = halfQuad(.1F);
    auto candidate = before;
    setLeft(candidate, .0005F);
    for (const bool merge : {false, true}) {
        MirrorOptions options;
        options.merge = merge;
        std::string error;
        auto clip = MirrorClipSession::begin(before, {1, 4}, options, error);
        REQUIRE(clip);
        REQUIRE(clip->constrain(candidate).mesh->vertex(1)->position.x == 0);
        options.clipping = false;
        auto free = MirrorClipSession::begin(before, {1, 4}, options, error);
        REQUIRE(free);
        REQUIRE(free->constrain(candidate).mesh == candidate);
        REQUIRE(free->constrainedVertices().empty());
        options.clipping = true;
        options.enabled = false;
        auto disabled = MirrorClipSession::begin(before, {1, 4}, options, error);
        REQUIRE(disabled->constrain(candidate).mesh == candidate);
    }
    auto isolated = before;
    isolated.vertices.push_back({99, {0, 2, 0}});
    auto moved = isolated;
    setPosition(moved, 99, {1, 2, 0});
    std::string error;
    auto session = MirrorClipSession::begin(isolated, {99}, {}, error);
    REQUIRE(session);
    REQUIRE(session->constrain(moved).mesh == moved);
    auto closed = createEditableCube();
    auto transformed = transformVertices(closed, {1, 2, 3, 4, 5, 6, 7, 8}, glm::dmat4(1),
                                         glm::translate(glm::dmat4(1), glm::dvec3(1, 0, 0)));
    REQUIRE(transformed.mesh);
    auto noBoundary = MirrorClipSession::begin(closed, {1, 2, 3, 4, 5, 6, 7, 8}, {}, error);
    REQUIRE(noBoundary->constrain(*transformed.mesh).mesh == transformed.mesh);
    REQUIRE(noBoundary->constrainedVertices().empty());
}

TEST_CASE("Mirror and Clipping include exact threshold on each local axis", "[mirror]") {
    for (const auto axis : {MirrorAxis::X, MirrorAxis::Y, MirrorAxis::Z}) {
        MirrorOptions options{axis};
        options.threshold = .125; // 二进制可精确表示，验证包含阈值本身。
        const int component = static_cast<int>(axis);
        auto source = halfQuad(.125F);
        for (auto& vertex : source.vertices)
            std::swap(vertex.position.x, vertex.position[component]);
        for (auto& corner : source.faces.front().corners)
            corner.normal.reset();
        const auto mirrored = evaluateMirror(source, options);
        REQUIRE(mirrored.evaluation);
        REQUIRE(mirrored.evaluation->mesh.vertices.size() == 6);
        REQUIRE(mirrored.evaluation->mesh.vertex(1)->position[component] == 0);
        std::string error;
        auto clip = MirrorClipSession::begin(source, {1, 4}, options, error);
        REQUIRE(clip);
        REQUIRE(clip->constrainedVertices() == std::set<VertexId>{1, 4});
        auto moved = source;
        moved.vertices[0].position[component] = -.3F;
        moved.vertices[3].position[component] = -.3F;
        const auto clipped = clip->constrain(moved);
        REQUIRE(clipped.mesh);
        REQUIRE(clipped.mesh->vertex(1)->position[component] == 0);
        REQUIRE(clipped.mesh->vertex(4)->position[component] == 0);
        auto outside = source;
        outside.vertices[0].position[component] = .25F;
        outside.vertices[3].position[component] = .25F;
        auto crossingClip = MirrorClipSession::begin(outside, {1, 4}, options, error);
        REQUIRE(crossingClip);
        REQUIRE(crossingClip->constrainedVertices().empty());
        const auto crossing = crossingClip->constrain(moved);
        REQUIRE(crossing.mesh);
        REQUIRE(crossing.mesh->vertex(1)->position[component] == 0);
        REQUIRE(crossingClip->constrainedVertices() == std::set<VertexId>{1, 4});
        auto singleEndpoint = source;
        singleEndpoint.vertices[3].position[component] = .25F;
        const auto separate = evaluateMirror(singleEndpoint, options);
        REQUIRE(separate.evaluation);
        REQUIRE(separate.evaluation->mesh.vertices.size() == 8);
        REQUIRE(separate.evaluation->mesh.vertex(1)->position[component] == .125F);
    }
}

TEST_CASE("Clipping rejected candidates do not capture vertices or mutate previous accepted state",
          "[mirror]") {
    const auto before = halfQuad(.1F);
    std::string error;
    auto session = MirrorClipSession::begin(before, {1, 4}, {}, error);
    REQUIRE(session);
    auto bad = before;
    setPosition(bad, 1, {-.1F, 0, 0});
    setPosition(bad, 4, {-.1F, 0, 0});
    REQUIRE_FALSE(session->constrain(bad).mesh);
    REQUIRE(session->constrainedVertices().empty());
    auto retreat = before;
    setLeft(retreat, .2F);
    REQUIRE(session->constrain(retreat).mesh == retreat);
    REQUIRE(session->constrainedVertices().empty());
    bad = before;
    bad.faces[0].corners[0].uv.x += 1;
    REQUIRE_FALSE(session->constrain(bad).mesh);
    bad = before;
    bad.vertices[1].position.y -= .1F; // 冻结选区外不允许修改。
    REQUIRE_FALSE(session->constrain(bad).mesh);
    bad = before;
    bad.vertices[0].position.x = std::numeric_limits<float>::infinity();
    REQUIRE_FALSE(session->constrain(bad).mesh);
    auto captured = before;
    setLeft(captured, 0);
    REQUIRE(session->constrain(captured).mesh);
    REQUIRE(session->constrainedVertices().size() == 2);
    REQUIRE_FALSE(session->constrain(bad).mesh);
    REQUIRE(session->constrainedVertices().size() == 2);
    REQUIRE_FALSE(MirrorClipSession::begin(before, {99}, {}, error));
    REQUIRE_FALSE(error.empty());
    MirrorOptions invalid;
    invalid.threshold = -1;
    REQUIRE_FALSE(MirrorClipSession::begin(before, {}, invalid, error));
}

TEST_CASE(
    "Scene mirror evaluation stays local and source revision changes only on snapshot installation",
    "[mirror]") {
    Scene scene;
    const auto parent = scene.createEntity("parent");
    const auto object = scene.createEntity("source", parent);
    Transform transform;
    transform.position = {4, 2, -3};
    transform.scale = {-2, 3, .5F};
    transform.rotation = glm::quat(glm::vec3(.2F, .6F, -.3F));
    REQUIRE(scene.setTransform(parent, transform));
    std::string error;
    const auto source = halfCube();
    REQUIRE(scene.installGeometry(*scene.prepareEditableGeometry(object, source, error)));
    const auto before = scene.geometrySnapshot(object);
    const auto meshId = scene.find(object)->editableMesh;
    const auto content = scene.editableMesh(meshId)->content;
    const auto revision = scene.editableMesh(meshId)->evaluationRevision;
    const auto result = scene.evaluateMirror(object, {});
    REQUIRE(result.evaluation);
    MirrorOptions invalid;
    invalid.threshold = -1;
    REQUIRE_FALSE(scene.evaluateMirror(object, invalid).evaluation);
    REQUIRE(scene.editableMesh(meshId)->content == content);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision == revision);
    const auto world = scene.worldMatrix(object);
    for (const auto& [id, origin] : result.evaluation->vertices) {
        auto position = source.vertex(origin.source)->position;
        if (origin.mirrored)
            position.x = -position.x;
        REQUIRE(glm::vec3(world * glm::vec4(result.evaluation->mesh.vertex(id)->position, 1)) ==
                glm::vec3(world * glm::vec4(position, 1)));
    }
    const auto prepared = scene.prepareEditableGeometry(object, result.evaluation->mesh, error);
    REQUIRE(prepared);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision == revision);
    REQUIRE(scene.editableMesh(meshId)->content == content);
    REQUIRE(scene.installGeometry(*prepared));
    REQUIRE(scene.editableMesh(meshId)->content->source == result.evaluation->mesh);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision > revision);
    SceneDocumentData appliedData;
    appliedData.nodes = scene.nodes();
    appliedData.editableMeshes = scene.editableMeshes();
    SceneDocumentData appliedLoaded;
    REQUIRE(SceneSerializer::decode(SceneSerializer::encode(appliedData), appliedLoaded, error));
    REQUIRE(appliedLoaded.editableMeshes[0].source == result.evaluation->mesh);
    const auto installedRevision = scene.editableMesh(meshId)->evaluationRevision;
    REQUIRE(scene.installGeometry(*before));
    REQUIRE(scene.editableMesh(meshId)->content == content);
    REQUIRE(scene.editableMesh(meshId)->evaluationRevision > installedRevision);
    REQUIRE_FALSE(scene.evaluateMirror(999, {}).evaluation);
    const auto primitive = scene.createEntity("static", 0, PrimitiveKind::Cube);
    REQUIRE_FALSE(scene.evaluateMirror(primitive, {}).evaluation);
    REQUIRE(scene.find(primitive)->primitive == PrimitiveKind::Cube);
    SceneDocumentData data;
    data.nodes = scene.nodes();
    data.editableMeshes = scene.editableMeshes();
    SceneDocumentData loaded;
    REQUIRE(SceneSerializer::decode(SceneSerializer::encode(data), loaded, error));
    REQUIRE(loaded.editableMeshes[0].source == source);
}
