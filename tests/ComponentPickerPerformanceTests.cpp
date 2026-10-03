/*
 * 模块名: ComponentPickerPerformanceTests
 * 功能概述: 验证屏幕分桶优化仍保留真实网格、遮挡、裁剪与稳定身份语义。
 * 对外接口: Catch2 [component-picker-performance]，无窗口或 GL。
 * 依赖关系: ComponentPicker、Scene、CPU Assets、EditorCamera。
 * 输入输出: 10k 四边格、桶边界和连续可见边夹具到精确源组件身份。
 * 异常与错误: 几何安装失败即拒绝夹具；维护说明: 不用机器相关耗时断言代替真实性能探针。
 */
#include "editor/operations/ComponentPicker.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <utility>
#include <vector>

using namespace mini3d;
using editor::ComponentId;
using editor::SelectionDomain;
namespace {
constexpr glm::ivec2 kSize{1024, 768};
constexpr std::uint64_t kVertexBase = std::uint64_t{1} << 40;
constexpr std::uint64_t kFaceBase = std::uint64_t{1} << 48;
constexpr std::uint64_t kCornerBase = std::uint64_t{1} << 52;
glm::dvec2 screen(const renderer_gl::EditorCamera& camera, glm::vec3 point) {
    const auto clip = glm::dmat4(camera.viewProjectionMatrix()) * glm::dvec4(point, 1);
    const auto ndc = glm::dvec3(clip) / clip.w;
    return {(ndc.x + 1) * 0.5 * kSize.x, (1 - ndc.y) * 0.5 * kSize.y};
}
renderer_gl::EditorCamera frontCamera(float extent = 1) {
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(kSize.x, kSize.y);
    camera.setView(renderer_gl::EditorView::Front);
    camera.setOrthographic(true);
    core::Aabb bounds;
    bounds.expand({-extent, -extent, 0});
    bounds.expand({extent, extent, 0});
    REQUIRE(camera.focus(bounds));
    return camera;
}
core::EntityId meshIn(core::Scene& scene, const core::modeling::EditableMesh& mesh) {
    const auto id = scene.createEntity("picker fixture");
    std::string error;
    const auto prepared = scene.prepareEditableGeometry(id, mesh, error);
    INFO(error);
    REQUIRE(prepared);
    REQUIRE(scene.installGeometry(*prepared));
    return id;
}
core::modeling::EditableMesh polygonMesh(const std::vector<glm::vec3>& points) {
    core::modeling::EditableMesh mesh;
    core::modeling::EditableFace face;
    face.id = kFaceBase + 1;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto id = kVertexBase + i + 1;
        mesh.vertices.push_back({id, points[i]});
        face.corners.push_back({kCornerBase + i + 1, id});
    }
    mesh.faces.push_back(std::move(face));
    return mesh;
}
core::modeling::VertexId gridId(int x, int y) {
    return kVertexBase + 13 * static_cast<std::uint64_t>(y * 100 + x) + 1;
}
glm::vec3 gridPoint(int x, int y) {
    return {-5 + 10.0F * x / 99, -5 + 10.0F * y / 99, 0};
}
core::modeling::EditableMesh gridMesh() {
    core::modeling::EditableMesh mesh;
    mesh.vertices.reserve(10000);
    mesh.faces.reserve(9801);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x)
            mesh.vertices.push_back({gridId(x, y), gridPoint(x, y)});
    for (int y = 0; y < 99; ++y)
        for (int x = 0; x < 99; ++x) {
            const auto index = static_cast<std::uint64_t>(y * 99 + x);
            core::modeling::EditableFace face;
            face.id = kFaceBase + index + 1;
            const std::array vertices{gridId(x, y), gridId(x + 1, y), gridId(x + 1, y + 1),
                                      gridId(x, y + 1)};
            for (std::size_t corner = 0; corner < vertices.size(); ++corner)
                face.corners.push_back({kCornerBase + index * 4 + corner + 1, vertices[corner]});
            mesh.faces.push_back(std::move(face));
        }
    // ID 必须独立于源点和源面的容器排列。
    std::reverse(mesh.vertices.begin(), mesh.vertices.end());
    std::reverse(mesh.faces.begin(), mesh.faces.end());
    return mesh;
}
} // namespace

TEST_CASE("Screen buckets select a real reordered 10k source grid with exact 64 bit IDs",
          "[component-picker-performance]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto source = gridMesh();
    const auto id = meshIn(scene, source);
    const auto camera = frontCamera(5);
    const auto point = glm::vec2(screen(camera, gridPoint(50, 50)));
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera, point,
                                  kSize) == ComponentId{gridId(50, 50)});
    const auto edgePoint = glm::vec2(screen(camera, (gridPoint(49, 50) + gridPoint(50, 50)) * .5F));
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera, edgePoint,
                                  kSize) == ComponentId{gridId(49, 50), gridId(50, 50)});
    const auto facePoint = glm::vec2(screen(camera, (gridPoint(49, 49) + gridPoint(50, 50)) * .5F));
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera, facePoint,
                                  kSize) == ComponentId{kFaceBase + 49 * 99 + 49 + 1});
    const auto a = glm::vec2(screen(camera, gridPoint(40, 40)));
    const auto b = glm::vec2(screen(camera, gridPoint(69, 69)));
    const auto minimum = glm::min(a, b) - glm::vec2(.25F);
    const auto maximum = glm::max(a, b) + glm::vec2(.25F);
    std::set<ComponentId> expected;
    for (int y = 40; y <= 69; ++y)
        for (int x = 40; x <= 69; ++x)
            expected.insert({gridId(x, y)});
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera, minimum,
                                        maximum, kSize) == expected);
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face})
        REQUIRE(editor::boxSelectComponents(scene, assets, id, domain, camera, minimum, maximum,
                                            kSize) ==
                editor::boxSelectComponents(scene, assets, id, domain, camera, minimum, maximum,
                                            kSize, true));
    const auto blocker =
        meshIn(scene, polygonMesh({{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}));
    REQUIRE_FALSE(
        editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera, point, kSize));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera,
                                        point - glm::vec2(.25F), point + glm::vec2(.25F), kSize)
                .empty());
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera, point, kSize,
                                  true) == ComponentId{gridId(50, 50)});
    REQUIRE(scene.setVisible(blocker, false));
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera, point,
                                  kSize) == ComponentId{gridId(50, 50)});
}

TEST_CASE("Bucket borders retain barycentric tolerance stable ties and the 10 pixel radius",
          "[component-picker-performance]") {
    const auto camera = frontCamera();
    const auto units = camera.worldUnitsPerPixel({0, 0, 0});
    // 桶界 x=512；2e-8 像素在三角重心容差内，2e-5 像素明确在容差外。
    for (const double offset : {0.0, 2.0e-8, 2.0e-5}) {
        INFO(offset);
        core::Scene scene;
        assets::AssetManager assets;
        const glm::vec3 position{-static_cast<float>(units * offset), 0, 0};
        core::modeling::EditableMesh source;
        source.vertices = {{kVertexBase + 91, position}, {kVertexBase + 7, position}};
        const auto id = meshIn(scene, source);
        const auto pixel = glm::vec2(screen(camera, position));
        if (offset > 0)
            REQUIRE(screen(camera, position).x < 512);
        REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera, pixel,
                                      kSize) == ComponentId{kVertexBase + 7});
        REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera,
                                      pixel + glm::vec2(9, 0), kSize));
        REQUIRE_FALSE(editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera,
                                            pixel + glm::vec2(11, 0), kSize));
        if (offset == 0)
            REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera,
                                          pixel + glm::vec2(10, 0), kSize));
        meshIn(scene, polygonMesh({{0, -1, .5F}, {1, -1, .5F}, {1, 1, .5F}, {0, 1, .5F}}));
        const bool visible = offset > 1.0e-6;
        REQUIRE(
            editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera, pixel, kSize)
                .has_value() == visible);
        const auto snap = editor::pickVertexSnap(scene, assets, camera, pixel, kSize, 0, false);
        REQUIRE(snap.has_value() == visible);
        if (snap)
            REQUIRE(snap->vertex == kVertexBase + 7);
        REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera,
                                            pixel - glm::vec2(1), pixel + glm::vec2(1), kSize)
                    .empty() == !visible);
        REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera, pixel,
                                      kSize, true) == ComponentId{kVertexBase + 7});
    }
}

TEST_CASE("Cross bucket edges retain a subpixel visible interval without sampling",
          "[component-picker-performance]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto camera = frontCamera();
    const auto id = meshIn(scene, polygonMesh({{-1, 0, 0}, {1, 0, 0}, {1, .8F, 0}, {-1, .8F, 0}}));
    const ComponentId edge{kVertexBase + 1, kVertexBase + 2};
    const float gap = camera.worldUnitsPerPixel({0, 0, 0}) * .1F;
    meshIn(scene, polygonMesh({{-2, -1, .5F}, {-gap, -1, .5F}, {-gap, 1, .5F}, {-2, 1, .5F}}));
    const auto right =
        meshIn(scene, polygonMesh({{gap, -1, .5F}, {2, -1, .5F}, {2, 1, .5F}, {gap, 1, .5F}}));
    const auto center = glm::vec2(screen(camera, {0, 0, 0}));
    const auto minimum = glm::vec2(screen(camera, {-1, 0, 0})) - glm::vec2(0, 1);
    const auto maximum = glm::vec2(screen(camera, {1, 0, 0})) + glm::vec2(0, 1);
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera, center,
                                  kSize) == edge);
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera, minimum,
                                        maximum, kSize)
                .contains(edge));
    core::Transform transform;
    transform.position.x = -2 * gap;
    REQUIRE(scene.setTransform(right, transform));
    REQUIRE_FALSE(
        editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera, center, kSize));
    REQUIRE_FALSE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                              minimum, maximum, kSize)
                      .contains(edge));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera, minimum,
                                        maximum, kSize, true)
                .contains(edge));
}

TEST_CASE("Clipped occluders enter screen buckets without changing the NDC depth tolerance",
          "[component-picker-performance]") {
    auto camera = frontCamera();
    camera.setOrthographic(false);
    const auto eye = camera.position().z;
    const std::array<std::array<glm::vec3, 3>, 3> blockers{
        std::array{glm::vec3{-100, -1, .5F}, glm::vec3{100, -1, .5F}, glm::vec3{0, 100, .5F}},
        std::array{glm::vec3{-2, -1, .5F}, glm::vec3{2, -1, .5F}, glm::vec3{0, 1.5F, eye + .2F}},
        std::array{glm::vec3{-1, -1, eye + .2F}, glm::vec3{1, -1, eye + .2F},
                   glm::vec3{0, 1, eye + .2F}}};
    for (std::size_t i = 0; i < blockers.size(); ++i) {
        INFO(i);
        core::Scene scene;
        assets::AssetManager assets;
        core::modeling::EditableMesh source;
        source.vertices.push_back({kVertexBase + 7, {0, 0, 0}});
        const auto id = meshIn(scene, source);
        meshIn(scene, polygonMesh(std::vector<glm::vec3>(blockers[i].begin(), blockers[i].end())));
        const auto pixel = glm::vec2(screen(camera, {0, 0, 0}));
        const bool visible = i == 2;
        REQUIRE(
            editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera, pixel, kSize)
                .has_value() == visible);
        REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera,
                                            pixel - glm::vec2(1), pixel + glm::vec2(1), kSize)
                    .empty() == !visible);
    }
    camera.setOrthographic(true);
    for (const double ndcOffset : {1.0e-6, 4.0e-6}) {
        INFO(ndcOffset);
        core::Scene scene;
        assets::AssetManager assets;
        const float z = static_cast<float>(ndcOffset / std::abs(camera.projectionMatrix()[2][2]));
        core::modeling::EditableMesh source;
        source.vertices.push_back({kVertexBase + 7, {0, 0, 0}});
        const auto id = meshIn(scene, source);
        meshIn(scene, polygonMesh({{-1, -1, z}, {1, -1, z}, {1, 1, z}, {-1, 1, z}}));
        REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera,
                                      {512, 384}, kSize)
                    .has_value() == (ndcOffset < 2.0e-6));
    }
}

TEST_CASE("Query regions retain full viewport visibility through perspective and near clipping",
          "[component-picker-performance]") {
    for (const bool orthographic : {true, false}) {
        auto camera = frontCamera();
        camera.setOrthographic(orthographic);
        const auto eye = camera.position().z;
        const std::array<std::array<glm::vec3, 3>, 4> blockers{
            std::array{glm::vec3{2, -1, .5F}, glm::vec3{3, -1, .5F}, glm::vec3{2.5F, 1, .5F}},
            std::array{glm::vec3{-100, -1, .5F}, glm::vec3{100, -1, .5F}, glm::vec3{0, 100, .5F}},
            std::array{glm::vec3{-2, -1, .5F}, glm::vec3{2, -1, .5F},
                       glm::vec3{0, 1.5F, eye + .2F}},
            std::array{glm::vec3{-1, -1, eye + .2F}, glm::vec3{1, -1, eye + .2F},
                       glm::vec3{0, 1, eye + .2F}}};
        for (std::size_t i = 0; i < blockers.size(); ++i) {
            INFO(orthographic);
            INFO(i);
            core::Scene scene;
            assets::AssetManager assets;
            core::modeling::EditableMesh source;
            source.vertices = {{kVertexBase + 91, {-.65F, 0, 0}},
                               {kVertexBase + 7, {0, 0, 0}},
                               {kVertexBase + 53, {.65F, 0, 0}}};
            const auto id = meshIn(scene, source);
            meshIn(scene,
                   polygonMesh(std::vector<glm::vec3>(blockers[i].begin(), blockers[i].end())));
            const auto full =
                editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera,
                                            {0, 0}, glm::vec2(kSize), kSize);
            for (const auto& vertex : source.vertices) {
                const auto pixel = glm::vec2(screen(camera, vertex.position));
                const bool visible = full.contains({vertex.id});
                const auto hit = editor::pickComponent(scene, assets, id, SelectionDomain::Vertex,
                                                       camera, pixel + glm::vec2(9, 0), kSize);
                REQUIRE(hit == (visible ? std::optional(ComponentId{vertex.id}) : std::nullopt));
                const auto expected =
                    visible ? std::set<ComponentId>{{vertex.id}} : std::set<ComponentId>{};
                REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex,
                                                    camera, pixel - glm::vec2(.25F),
                                                    pixel + glm::vec2(.25F), kSize) == expected);
                const auto snap =
                    editor::pickVertexSnap(scene, assets, camera, pixel, kSize, 0, false);
                REQUIRE(snap.has_value() == visible);
                if (snap) {
                    REQUIRE(snap->entity == id);
                    REQUIRE(snap->vertex == vertex.id);
                }
            }
        }
    }
}

TEST_CASE("A zero area query region retains occluder tolerance beyond its first bucket",
          "[component-picker-performance]") {
    const auto camera = frontCamera();
    const auto units = camera.worldUnitsPerPixel({0, 0, 0});
    for (const double offset : {2.0e-8, 2.0e-5}) {
        INFO(offset);
        core::Scene scene;
        assets::AssetManager assets;
        core::modeling::EditableMesh source;
        source.vertices.push_back({kVertexBase + 7, {0, 0, 0}});
        const auto id = meshIn(scene, source);
        const auto right = -static_cast<float>(units * offset);
        meshIn(scene,
               polygonMesh({{-1, -1, .5F}, {right, -1, .5F}, {right, 1, .5F}, {-1, 1, .5F}}));
        const auto full = editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex,
                                                      camera, {0, 0}, glm::vec2(kSize), kSize);
        REQUIRE(full.empty() == (offset < 1.0e-6));
        REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera,
                                            {512, 384}, {512, 384}, kSize) == full);
    }
}

TEST_CASE("A distant visible edge interval cannot become a hit inside the click region",
          "[component-picker-performance]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto camera = frontCamera();
    const auto id = meshIn(scene, polygonMesh({{-1, 0, 0}, {1, 0, 0}, {1, .8F, 0}, {-1, .8F, 0}}));
    const ComponentId edge{kVertexBase + 1, kVertexBase + 2};
    const float gap = camera.worldUnitsPerPixel({0, 0, 0}) * .1F;
    constexpr float gapStart = .8F;
    meshIn(scene,
           polygonMesh({{-2, -1, .5F}, {gapStart, -1, .5F}, {gapStart, 1, .5F}, {-2, 1, .5F}}));
    meshIn(scene,
           polygonMesh(
               {{gapStart + gap, -1, .5F}, {2, -1, .5F}, {2, 1, .5F}, {gapStart + gap, 1, .5F}}));
    const auto center = glm::vec2(screen(camera, {0, 0, 0}));
    const auto visible = glm::vec2(screen(camera, {gapStart + gap * .5F, 0, 0}));
    REQUIRE_FALSE(
        editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera, center, kSize));
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera, visible,
                                  kSize) == edge);
    REQUIRE_FALSE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                              center - glm::vec2(1), center + glm::vec2(1), kSize)
                      .contains(edge));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                        visible - glm::vec2(1), visible + glm::vec2(1), kSize)
                .contains(edge));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera, {0, 0},
                                        glm::vec2(kSize), kSize)
                .contains(edge));
}
