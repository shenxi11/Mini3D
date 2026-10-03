/*
 * 模块名: ComponentPickerTests
 * 功能概述: 验证真实投影与精确几何遮挡的源组件身份，不启动窗口或 GL。
 * 对外接口: Catch2 [component-picker]；依赖关系: Scene、CPU Assets、EditorCamera。
 * 输入输出: 单面/边/点、父变换、遮挡夹具到预期 ID。
 * 异常与错误: 隐藏始终拒绝，可见模式检查遮挡；维护说明: 覆盖裁剪、连续边段和 X-Ray。
 */
#include "editor/operations/ComponentPicker.h"
#include "renderer_gl/RayCaster.h"

#include <QFile>
#include <QTemporaryDir>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace mini3d;
using editor::ComponentId;
using editor::SelectionDomain;
namespace {
constexpr glm::ivec2 kSize{1000, 800};
glm::vec2 screen(const renderer_gl::EditorCamera& camera, glm::vec3 point) {
    const auto clip = camera.viewProjectionMatrix() * glm::vec4(point, 1);
    const auto ndc = glm::vec3(clip) / clip.w;
    return {(ndc.x + 1) * 0.5F * kSize.x, (1 - ndc.y) * 0.5F * kSize.y};
}
core::EntityId cubeIn(core::Scene& scene) {
    const auto id = scene.createEntity("cube", 0, core::PrimitiveKind::Cube);
    std::string error;
    const auto prepared =
        scene.prepareEditableGeometry(id, core::modeling::createEditableCube(), error);
    REQUIRE(prepared);
    REQUIRE(scene.installGeometry(*prepared));
    return id;
}
renderer_gl::EditorCamera frontCamera() {
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(kSize.x, kSize.y);
    camera.setView(renderer_gl::EditorView::Front);
    camera.setOrthographic(false);
    core::Aabb bounds;
    bounds.expand({-0.5F, -0.5F, -0.5F});
    bounds.expand({0.5F, 0.5F, 0.5F});
    REQUIRE(camera.focus(bounds));
    return camera;
}
core::EntityId quadIn(core::Scene& scene, const std::array<glm::vec3, 4>& positions) {
    core::modeling::EditableMesh mesh;
    core::modeling::EditableFace face;
    face.id = 1;
    for (std::uint64_t i = 0; i < positions.size(); ++i) {
        mesh.vertices.push_back({i + 1, positions[i]});
        face.corners.push_back({i + 1, i + 1});
    }
    mesh.faces.push_back(face);
    const auto id = scene.createEntity("quad");
    std::string error;
    const auto prepared = scene.prepareEditableGeometry(id, mesh, error);
    INFO(error);
    REQUIRE(prepared);
    REQUIRE(scene.installGeometry(*prepared));
    return id;
}
} // namespace

TEST_CASE("Subdivision snapping uses evaluated points and excludes its moving source",
          "[subdivision-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto id = quadIn(scene, {{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}});
    std::string error;
    const auto prepared = scene.prepareSubdivision(id, core::modeling::SubdivisionOptions{}, error);
    REQUIRE(prepared);
    REQUIRE(scene.installGeometry(*prepared));
    const auto camera = frontCamera();
    const auto hit = editor::pickVertexSnap(scene, assets, camera, screen(camera, {0, 0, 0}), kSize,
                                            0, false, {});
    REQUIRE(hit);
    REQUIRE(hit->entity == id);
    REQUIRE(hit->vertex > 4);
    REQUIRE(hit->position == glm::vec3(0));
    REQUIRE_FALSE(editor::pickVertexSnap(scene, assets, camera, screen(camera, {0, 0, 0}), kSize,
                                         id, false, {1}));
    const auto cube = cubeIn(scene);
    auto options = core::modeling::SubdivisionOptions{};
    options.levels = 2;
    const auto cubePrepared = scene.prepareSubdivision(cube, options, error);
    REQUIRE(cubePrepared);
    REQUIRE(scene.installGeometry(*cubePrepared));
    const auto& content = *scene.editableMesh(scene.find(cube)->editableMesh)->content;
    const auto expected = content.displayedDerived().mesh.bounds();
    const auto actual = renderer_gl::RayCaster::worldBounds(scene, assets, cube);
    REQUIRE(actual.minimum == expected.minimum);
    REQUIRE(actual.maximum == expected.maximum);
    REQUIRE(actual.maximum.x < .5F);
}

TEST_CASE("Vertex snap filters source vertices subtree depth radius and hidden targets",
          "[snap-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto camera = frontCamera();
    const auto cube = cubeIn(scene);
    const auto mesh = scene.editableMesh(scene.find(cube)->editableMesh)->content;
    const auto point = glm::vec3(mesh->source.vertex(5)->position);
    const auto pixel = screen(camera, point);
    auto hit = editor::pickVertexSnap(scene, assets, camera, pixel, kSize, cube, false);
    REQUIRE(hit);
    REQUIRE(hit->entity == cube);
    REQUIRE(hit->vertex == 5);
    REQUIRE(hit->position == point);
    REQUIRE_FALSE(editor::pickVertexSnap(scene, assets, camera, pixel, kSize, cube, false, {5}));
    REQUIRE_FALSE(editor::pickVertexSnap(scene, assets, camera, pixel, kSize, cube, true));
    REQUIRE(editor::pickVertexSnap(scene, assets, camera, pixel + glm::vec2(9, 0), kSize, 0, true));
    REQUIRE_FALSE(
        editor::pickVertexSnap(scene, assets, camera, pixel + glm::vec2(11, 0), kSize, 0, true));
    REQUIRE_FALSE(editor::pickVertexSnap(scene, assets, camera, {-1, 0}, kSize, 0, true));
    REQUIRE_FALSE(editor::pickVertexSnap(scene, assets, camera, {NAN, 0}, kSize, 0, true));
    // 后面同屏点不应穿透前表面；X-Ray选择偏好不进入吸附查询。
    const auto back = glm::vec3(mesh->source.vertex(1)->position);
    REQUIRE_FALSE(editor::pickVertexSnap(scene, assets, camera, screen(camera, back), kSize, cube,
                                         false, {5, 6, 7, 8}));
    const auto parent = scene.createEntity("moving parent");
    REQUIRE(scene.setParent(cube, parent));
    REQUIRE_FALSE(editor::pickVertexSnap(scene, assets, camera, pixel, kSize, parent, true));
    REQUIRE(scene.setVisible(parent, false));
    REQUIRE_FALSE(editor::pickVertexSnap(scene, assets, camera, pixel, kSize, 0, true));
}

TEST_CASE(
    "Vertex snap returns world positions and stable nearest ties for static and editable geometry",
    "[snap-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto cube = cubeIn(scene);
    const auto parent = scene.createEntity("parent");
    REQUIRE(scene.setParent(cube, parent));
    core::Transform transform;
    transform.position = {.1F, .2F, 0};
    transform.scale = {-1.1F, .8F, 1.2F};
    transform.rotation = glm::quat(glm::radians(glm::vec3(0, 0, 23)));
    REQUIRE(scene.setTransform(parent, transform));
    const auto worldPoint = glm::vec3(scene.worldMatrix(cube) * glm::vec4(-.5F, -.5F, .5F, 1));
    for (bool orthographic : {false, true}) {
        auto camera = frontCamera();
        camera.setOrthographic(orthographic);
        const auto hit = editor::pickVertexSnap(scene, assets, camera, screen(camera, worldPoint),
                                                kSize, 0, true);
        REQUIRE(hit);
        REQUIRE(hit->entity == cube);
        REQUIRE(glm::distance(hit->position, worldPoint) < 1e-6F);
    }
    core::Scene staticScene;
    const auto first = staticScene.createEntity("static cube", 0, core::PrimitiveKind::Cube);
    staticScene.createEntity("tie", 0, core::PrimitiveKind::Cube);
    const auto camera = frontCamera();
    const auto hit = editor::pickVertexSnap(staticScene, assets, camera,
                                            screen(camera, {.5F, .5F, .5F}), kSize, 0, true);
    REQUIRE(hit);
    REQUIRE(hit->entity == first);
    REQUIRE(hit->position == glm::vec3(.5F, .5F, .5F));
}

TEST_CASE("Cursor hits real nearest surfaces with transforms and visibility", "[cursor-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto camera = frontCamera();
    const auto cube = cubeIn(scene);
    const glm::vec3 point{.2F, -.1F, .5F};
    auto result = editor::locateCursor(scene, assets, camera, screen(camera, point), kSize);
    REQUIRE(result.position);
    REQUIRE(result.surface);
    REQUIRE(glm::length(*result.position - point) < 1e-5F);
    const auto parent = scene.createEntity("parent");
    REQUIRE(scene.setParent(cube, parent));
    core::Transform transform;
    transform.rotation = glm::quat(glm::radians(glm::vec3(12, 23, 34)));
    transform.scale = {-1.2F, 1.5F, .8F};
    REQUIRE(scene.setTransform(parent, transform));
    const auto world = scene.worldMatrix(cube);
    const auto expected = glm::vec3(world * glm::vec4(0, 0, .5F, 1));
    result = editor::locateCursor(scene, assets, camera, screen(camera, expected), kSize);
    REQUIRE(result.position);
    REQUIRE(result.surface);
    REQUIRE(glm::length(*result.position - expected) < 1e-4F);
    REQUIRE(scene.setVisible(parent, false));
    result = editor::locateCursor(scene, assets, camera, screen(camera, expected), kSize);
    REQUIRE_FALSE(result.surface);
    // 球体包围盒角落不是表面，不得使用 AABB 命中伪造游标位置。
    core::Scene sphereScene;
    sphereScene.createEntity("sphere", 0, core::PrimitiveKind::Sphere);
    result =
        editor::locateCursor(sphereScene, assets, camera, screen(camera, {.48F, .48F, .5F}), kSize);
    REQUIRE_FALSE(result.surface);
    result = editor::locateCursor(sphereScene, assets, camera, screen(camera, {0, 0, .5F}), kSize);
    REQUIRE(result.surface);
    REQUIRE(result.position);
    REQUIRE(std::abs(result.position->z - .5F) < .01F);
}

TEST_CASE("Cursor work plane is explicit and parallel rays cannot create distant coordinates",
          "[cursor-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    auto camera = frontCamera();
    auto result =
        editor::locateCursor(scene, assets, camera, {.5F * kSize.x, .5F * kSize.y}, kSize);
    REQUIRE_FALSE(result.position);
    REQUIRE(result.error.contains(QStringLiteral("平行")));
    camera.setView(renderer_gl::EditorView::Top);
    for (bool orthographic : {false, true}) {
        camera.setOrthographic(orthographic);
        const glm::vec3 ground{.3F, 0, -.2F};
        result = editor::locateCursor(scene, assets, camera, screen(camera, ground), kSize);
        REQUIRE(result.position);
        REQUIRE_FALSE(result.surface);
        REQUIRE(glm::length(*result.position - ground) < 1e-4F);
    }
    for (glm::vec2 invalid : {glm::vec2(-1, 10), glm::vec2(kSize), glm::vec2(INFINITY, 0)}) {
        REQUIRE_FALSE(editor::locateCursor(scene, assets, camera, invalid, kSize).position);
    }
    REQUIRE_FALSE(editor::locateCursor(scene, assets, camera, {0, 0}, {0, 0}).position);
}

TEST_CASE(
    "Component pick maps triangles to source faces and never exposes a triangulation diagonal",
    "[component-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto id = cubeIn(scene);
    const auto camera = frontCamera();
    const auto& mesh = scene.editableMesh(scene.find(id)->editableMesh)->content->source;
    core::modeling::FaceId front = 0;
    for (const auto& face : mesh.faces) {
        if (std::all_of(face.corners.begin(), face.corners.end(), [&](const auto& corner) {
                return mesh.vertex(corner.vertex)->position.z == 0.5F;
            })) {
            front = face.id;
        }
    }
    REQUIRE(front != 0);
    for (const auto point :
         {glm::vec3{0, 0, 0.5F}, glm::vec3{0.2F, -0.15F, 0.5F}, glm::vec3{-0.2F, 0.15F, 0.5F}}) {
        REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera,
                                      screen(camera, point), kSize) == ComponentId{front});
    }
    REQUIRE_FALSE(editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera,
                                        screen(camera, {0, 0, 0.5F}), kSize));
    const auto vertices = editor::ComponentSelection::elements(mesh, SelectionDomain::Edge);
    for (const auto edge : vertices) {
        const auto a = mesh.vertex(edge.first)->position;
        const auto b = mesh.vertex(edge.second)->position;
        if (a.z == 0.5F && b.z == 0.5F) {
            REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera,
                                          screen(camera, (a + b) * 0.5F), kSize) == edge);
        }
    }
    for (const auto& vertex : mesh.vertices) {
        const auto hit = editor::pickComponent(scene, assets, id, SelectionDomain::Vertex, camera,
                                               screen(camera, vertex.position), kSize);
        if (vertex.position.z == 0.5F) {
            REQUIRE(hit == ComponentId{vertex.id});
        } else {
            REQUIRE_FALSE(hit);
        }
    }
}

TEST_CASE("Component pick respects other visible geometry and world transforms",
          "[component-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto id = cubeIn(scene);
    auto camera = frontCamera();
    const auto center = screen(camera, {0, 0, 0.5F});
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera, center, kSize));
    const auto blocker = scene.createEntity("blocker", 0, core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position.z = 1.2F;
    REQUIRE(scene.setTransform(blocker, transform));
    REQUIRE_FALSE(
        editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera, center, kSize));
    REQUIRE(scene.setVisible(blocker, false));
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera, center, kSize));
    REQUIRE(scene.setVisible(id, false));
    REQUIRE_FALSE(
        editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera, center, kSize));
    REQUIRE(scene.setVisible(id, true));
    const auto parent = scene.createEntity("parent");
    REQUIRE(scene.setParent(id, parent));
    transform.position = {0.2F, 0.1F, 0};
    transform.scale = {-1.5F, 0.7F, 0.9F};
    REQUIRE(scene.setTransform(parent, transform));
    const auto worldPoint = glm::vec3(scene.worldMatrix(id) * glm::vec4(0, 0, 0.5F, 1));
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera,
                                  screen(camera, worldPoint), kSize));
    camera.setOrthographic(true);
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera,
                                  screen(camera, worldPoint), kSize));
    REQUIRE_FALSE(
        editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera, {-1, 50}, kSize));
}

TEST_CASE("Box selection shares visible depth and X-Ray never selects hidden objects",
          "[component-picker][box-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto id = cubeIn(scene);
    auto camera = frontCamera();
    for (const bool orthographic : {false, true}) {
        camera.setOrthographic(orthographic);
        REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera,
                                            {0, 0}, glm::vec2(kSize), kSize)
                    .size() == 4);
        REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Face, camera,
                                            {0, 0}, glm::vec2(kSize), kSize)
                    .size() == 1);
        for (const auto domain :
             {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
            const auto& mesh = scene.editableMesh(scene.find(id)->editableMesh)->content->source;
            REQUIRE(editor::boxSelectComponents(scene, assets, id, domain, camera, glm::vec2(kSize),
                                                {0, 0}, kSize, true) ==
                    editor::ComponentSelection::elements(mesh, domain));
        }
    }
    const auto parent = scene.createEntity("hidden parent");
    REQUIRE(scene.setParent(id, parent));
    REQUIRE(scene.setVisible(parent, false));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera, {0, 0},
                                        glm::vec2(kSize), kSize, true)
                .empty());
    REQUIRE_FALSE(editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera,
                                        {500, 400}, kSize, true));
    REQUIRE(scene.setVisible(parent, true));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera,
                                        {-100, -100}, {-1, -1}, kSize, true)
                .empty());
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Face, camera, {0, 0},
                                        {100, 100}, {0, 0}, true)
                .empty());
}

TEST_CASE("Box touching rules use points edge segments and face centers independently",
          "[component-picker][box-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto id = quadIn(scene, {{{-1, -0.5F, 0}, {1, -0.5F, 0}, {1, 0.5F, 0}, {-1, 0.5F, 0}}});
    const auto camera = frontCamera();
    const auto bottom = screen(camera, {0, -0.5F, 0});
    const auto first = bottom - glm::vec2(10, 2), second = bottom + glm::vec2(10, 2);
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera, first,
                                        second, kSize) == std::set<ComponentId>{{1, 2}});
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Vertex, camera, first,
                                        second, kSize)
                .empty());
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Face, camera, first,
                                        second, kSize)
                .empty());
    const auto center = screen(camera, {0, 0, 0});
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Face, camera,
                                        center - glm::vec2(1), center + glm::vec2(1),
                                        kSize) == std::set<ComponentId>{{1}});
}

TEST_CASE("Visible edge intervals handle partial blockers and subpixel openings without sampling",
          "[component-picker][box-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto id = quadIn(scene, {{{-1, 0, 0}, {1, 0, 0}, {1, 1, 0}, {-1, 1, 0}}});
    auto camera = frontCamera();
    camera.setOrthographic(true);
    const auto blocker = scene.createEntity("blocker", 0, core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position = {0, 0, 0.7F};
    transform.scale = {0.6F, 0.6F, 0.1F};
    REQUIRE(scene.setTransform(blocker, transform));
    const auto center = screen(camera, {0, 0, 0});
    const auto first = screen(camera, {-0.9F, 0, 0}) - glm::vec2(0, 2);
    const auto second = screen(camera, {0.9F, 0, 0}) + glm::vec2(0, 2);
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera, first,
                                        second, kSize)
                .contains({1, 2}));
    REQUIRE_FALSE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                              center - glm::vec2(2), center + glm::vec2(2), kSize)
                      .contains({1, 2}));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                        center - glm::vec2(2), center + glm::vec2(2), kSize, true)
                .contains({1, 2}));
    REQUIRE_FALSE(
        editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera, center, kSize));
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera, center, kSize,
                                  true) == ComponentId{1, 2});
    // 两个遮挡物之间保留不足一逻辑像素的开口；固定采样和只测中心都会漏选。
    transform.position.x = -0.4131F;
    transform.scale.x = 1.1736F;
    REQUIRE(scene.setTransform(blocker, transform));
    const auto right = scene.createEntity("right blocker", 0, core::PrimitiveKind::Cube);
    transform.position.x = 0.5871F;
    transform.scale.x = 0.8258F;
    REQUIRE(scene.setTransform(right, transform));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera, first,
                                        second, kSize)
                .contains({1, 2}));
    transform.scale.x += 0.004F;
    REQUIRE(scene.setTransform(right, transform));
    REQUIRE_FALSE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                              first, second, kSize)
                      .contains({1, 2}));
}

TEST_CASE("Edges crossing near far and side clipping planes retain their selectable portion",
          "[component-picker][box-picker]") {
    assets::AssetManager assets;
    const auto camera = frontCamera();
    const auto eye = camera.position().z;
    for (const auto& endpoints :
         {std::pair{glm::vec3{-0.02F, -0.01F, eye + 0.02F}, glm::vec3{0.5F, -0.01F, 0}},
          std::pair{glm::vec3{-0.5F, -0.01F, 0}, glm::vec3{0.5F, -0.01F, -150}},
          std::pair{glm::vec3{-10, -0.01F, 0}, glm::vec3{10, -0.01F, 0}}}) {
        core::Scene scene;
        const auto a = endpoints.first, b = endpoints.second;
        const auto id =
            quadIn(scene, {{a, b, b + glm::vec3(0, 0.5F, 0), a + glm::vec3(0, 0.5F, 0)}});
        const auto pixel = screen(camera, glm::mix(a, b, a.x < -2 ? 0.5F : 0.25F));
        REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                            {0, 0}, glm::vec2(kSize), kSize, true)
                    .contains({1, 2}));
        REQUIRE(pixel.x >= 0);
        REQUIRE(pixel.x < kSize.x);
        REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Edge, camera, pixel,
                                      kSize, true) == ComponentId{1, 2});
        REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                            pixel - glm::vec2(1), pixel + glm::vec2(1), kSize, true)
                    .contains({1, 2}));
    }
}

TEST_CASE("Projected depth splits intersecting edges and ignores near-clipped occluders",
          "[component-picker][box-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto camera = frontCamera();
    const auto id = quadIn(scene, {{{-1, 0, -0.5F}, {1, 0, 0.5F}, {1, 1, 0.5F}, {-1, 1, -0.5F}}});
    const auto blocker = quadIn(scene, {{{-2, -1, 0}, {2, -1, 0}, {2, 2, 0}, {-2, 2, 0}}});
    const auto hidden = screen(camera, {-0.5F, 0, -0.25F});
    const auto visible = screen(camera, {0.5F, 0, 0.25F});
    REQUIRE_FALSE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                              hidden - glm::vec2(2), hidden + glm::vec2(2), kSize)
                      .contains({1, 2}));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                        visible - glm::vec2(2), visible + glm::vec2(2), kSize)
                .contains({1, 2}));
    core::Transform transform;
    transform.position.z = camera.position().z + 0.2F;
    REQUIRE(scene.setTransform(blocker, transform));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Edge, camera,
                                        hidden - glm::vec2(2), hidden + glm::vec2(2), kSize)
                .contains({1, 2}));
}

TEST_CASE("Imported geometry participates in component occlusion without becoming editable",
          "[component-picker][box-picker]") {
    core::Scene scene;
    assets::AssetManager assets;
    const auto id = cubeIn(scene);
    const auto camera = frontCamera();
    const auto imported =
        assets.importGltf(QString::fromUtf8(MINI3D_SAMPLE_DIRECTORY) + "/BoxTextured.glb");
    REQUIRE(imported.scene);
    core::AssetId mesh = 0;
    for (const auto& node : imported.scene->nodes) {
        if (!node.meshes.empty()) {
            mesh = node.meshes.front();
            break;
        }
    }
    REQUIRE(mesh != 0);
    const auto blocker = scene.createEntity("static imported blocker");
    REQUIRE(scene.setMeshRenderer(blocker, {mesh, 0}));
    const auto bounds = assets.mesh(mesh)->data.bounds();
    core::Transform transform;
    transform.scale = glm::vec3(0.7F) / (bounds.maximum - bounds.minimum);
    transform.position =
        glm::vec3(0, 0, 1.1F) - transform.scale * (bounds.maximum + bounds.minimum) * 0.5F;
    REQUIRE(scene.setTransform(blocker, transform));
    const auto center = screen(camera, {0, 0, 0.5F});
    REQUIRE_FALSE(
        editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera, center, kSize));
    REQUIRE(editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera, center, kSize,
                                  true));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Face, camera,
                                        center - glm::vec2(2), center + glm::vec2(2), kSize)
                .empty());
    REQUIRE(scene.setVisible(blocker, false));
    REQUIRE(editor::boxSelectComponents(scene, assets, id, SelectionDomain::Face, camera,
                                        center - glm::vec2(2), center + glm::vec2(2), kSize)
                .size() == 1);
}

TEST_CASE("Imported single and double sided occluders preserve mirrored winding rules",
          "[component-picker][box-picker]") {
    using nlohmann::json;
    const auto camera = frontCamera();
    for (const bool doubleSided : {false, true}) {
        QTemporaryDir directory;
        const std::array<float, 9> vertices{-2, -2, 1, 0, 2, 1, 2, -2, 1};
        const QByteArray buffer(reinterpret_cast<const char*>(vertices.data()), sizeof(vertices));
        const json document{
            {"asset", {{"version", "2.0"}}},
            {"buffers", json::array({{{"uri", "data:application/octet-stream;base64," +
                                                  buffer.toBase64().toStdString()},
                                      {"byteLength", buffer.size()}}})},
            {"bufferViews", json::array({{{"buffer", 0}, {"byteLength", buffer.size()}}})},
            {"accessors", json::array({{{"bufferView", 0},
                                        {"componentType", 5126},
                                        {"count", 3},
                                        {"type", "VEC3"},
                                        {"min", {-2, -2, 1}},
                                        {"max", {2, 2, 1}}}})},
            {"materials", json::array({{{"doubleSided", doubleSided}}})},
            {"meshes", json::array({{{"primitives", json::array({{{"attributes", {{"POSITION", 0}}},
                                                                  {"material", 0}}})}}})},
            {"nodes", json::array({{{"mesh", 0}}})},
            {"scenes", json::array({{{"nodes", {0}}}})},
            {"scene", 0}};
        const auto path = directory.filePath(QStringLiteral("occluder.gltf"));
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        const auto bytes = document.dump();
        REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) ==
                static_cast<qint64>(bytes.size()));
        file.close();
        core::Scene scene;
        assets::AssetManager assets;
        const auto id = cubeIn(scene);
        const auto imported = assets.importGltf(path);
        INFO(imported.error.toStdString());
        REQUIRE(imported.scene);
        const auto mesh = imported.scene->nodes.front().meshes.front();
        const auto blocker = scene.createEntity("backface occluder");
        REQUIRE(scene.setMeshRenderer(blocker, {mesh, assets.mesh(mesh)->material}));
        for (const bool mirrored : {false, true}) {
            core::Transform transform;
            transform.scale.x = mirrored ? -1.0F : 1.0F;
            REQUIRE(scene.setTransform(blocker, transform));
            const auto hit = editor::pickComponent(scene, assets, id, SelectionDomain::Face, camera,
                                                   {500, 400}, kSize);
            REQUIRE(bool(hit) == !doubleSided);
            const auto box = editor::boxSelectComponents(scene, assets, id, SelectionDomain::Face,
                                                         camera, {499, 399}, {501, 401}, kSize);
            REQUIRE(box.empty() == doubleSided);
        }
    }
}
