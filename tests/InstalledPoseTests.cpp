/*
 * 模块名: InstalledPoseTests
 * 功能概述: 验证完整动画姿态在范围、拾取、相机、灯与手柄之间共享并拒绝消费溢出。
 * 对外接口: Catch2 [installed-pose] 用例。
 * 依赖关系: Core、CPU Assets、Renderer、RayCaster、Catch2；不创建 GL Context。
 * 输入输出: 独立手算场景、冻结内容与一万层父链到可复现的消费断言。
 * 异常与错误: 缺节点、无效射线、八角/设备 VP 溢出均须拒绝。
 * 维护说明: 缓存用例直接消费旧快照以证明无真源扫描；正式入口仍须复核身份。
 */
#include "renderer_gl/InstalledPose.h"
#include "renderer_gl/RayCaster.h"
#include "renderer_gl/Renderer.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace mini3d;
using renderer_gl::InstalledPose;
using renderer_gl::RayCaster;
using renderer_gl::ScenePoseView;

namespace {
InstalledPose evaluatedScene(const core::Scene& scene,
                             std::shared_ptr<const assets::AssetManager> assets,
                             const core::SceneAnimation& animation = {}, core::FrameTime frame = 1,
                             const core::ViewportVisibility& visibility = {}) {
    std::string error;
    const auto inputs = scene.animationPoseInputs(error);
    REQUIRE(inputs);
    auto result = core::evaluateAnimationPose(*inputs, animation, frame);
    REQUIRE(result.pose);
    InstalledPose pose;
    pose.identity = {QStringLiteral("test-instance"), QStringLiteral("test-document"), 7, 3,
                     frame, renderer_gl::AnimationMode::PreviewPaused};
    pose.numerics = std::make_shared<const core::EvaluatedPose>(std::move(*result.pose));
    pose.geometry = renderer_gl::makePoseGeometry(scene, std::move(assets), visibility, *inputs);
    REQUIRE(pose.geometry);
    return pose;
}

void requireVectorNear(const glm::vec3& actual, const glm::vec3& expected,
                       float tolerance = 1.0e-4F) {
    for (int axis = 0; axis < 3; ++axis) {
        REQUIRE(std::isfinite(actual[axis]));
        REQUIRE(std::abs(actual[axis] - expected[axis]) <= tolerance);
    }
}

void requireMatrixNear(const glm::mat4& actual, const glm::mat4& expected,
                       float tolerance = 1.0e-4F) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            REQUIRE(std::abs(actual[column][row] - expected[column][row]) <= tolerance);
}

InstalledPose jointScene(const core::Scene& scene,
                         std::shared_ptr<const assets::AssetManager> assets,
                         const core::SceneAnimation& animation, core::FrameTime frame) {
    auto pose = evaluatedScene(scene, std::move(assets), animation, frame);
    std::string error;
    const auto inputs = scene.animationPoseInputs(error);
    REQUIRE(inputs);
    core::SourceIdentity source;
    source.documentUuid = scene.documentUuid();
    source.sourceHandle = core::LiveSourceHandle{"test-instance", "test-document"};
    source.contentRevision = pose.identity.sourceRevision;
    source.authorContextIdentity = {source.documentUuid, source.contentRevision,
                                    std::get<core::LiveSourceHandle>(source.sourceHandle)};
    source.frameTime = frame;
    source.evaluationSessionId = core::createEvaluationSessionId();
    source.evaluationId = pose.identity.evaluationId;
    source.evaluationProfile = core::EvaluationProfile::JointPreviewJ1;
    auto state = core::evaluateSceneState(*inputs, animation, pose.geometry->authors,
                                         pose.geometry->sceneGeometry, source);
    REQUIRE(state.state);
    auto checked = core::adaptSceneStateToPose(*state.state);
    REQUIRE(checked.pose);
    pose.sceneState = std::move(state.state);
    pose.sceneNumerics = std::make_shared<const core::EvaluatedPose>(std::move(*checked.pose));
    pose.coverage = renderer_gl::PoseStateCoverage::CompleteSceneJ1;
    pose.consumerDomain = renderer_gl::PoseConsumerDomain::JointJ1;
    REQUIRE(ScenePoseView(scene, &pose).isComplete());
    return pose;
}
} // namespace

TEST_CASE("Installed animation pose drives inherited bounds picking devices and gizmos",
          "[animation][installed-pose]") {
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto rig = scene.createEntity("Rig");
    const auto cube = scene.createEntity("Cube", rig, core::PrimitiveKind::Cube);
    const auto camera = scene.createEntity("Camera", rig);
    const auto light = scene.createEntity("Light", rig);
    REQUIRE(scene.setCamera(camera, {60, 0.2F, 200}));
    REQUIRE(scene.setLight(light, {{1, 0.5F, 0}, 2}));
    core::Transform transform;
    transform.position = {1, 2, 3};
    transform.scale = {-2, 3, 4};
    REQUIRE(scene.setTransform(rig, transform));
    transform = {};
    transform.position = {1, 0, 0};
    REQUIRE(scene.setTransform(cube, transform));
    transform.position = {0, 0, 2};
    REQUIRE(scene.setTransform(camera, transform));
    core::SceneAnimation animation;
    animation.tracks[{rig, core::AnimationChannel::Position}] =
        {{{1, {1, 2, 3}}, {49, {5, 0, 0}}}};
    animation.tracks[{rig, core::AnimationChannel::RotationEulerXYZDegrees}] =
        {{{1, {0, 0, 0}}, {49, {0, 90, 0}}}};
    const auto pose = evaluatedScene(scene, assets, animation, 49);
    const ScenePoseView view(scene, &pose);
    REQUIRE(view.worldMatrix(cube));
    requireVectorNear(glm::vec3((*view.worldMatrix(cube))[3]), {5, 0, 2});
    requireVectorNear(glm::vec3(scene.worldMatrix(cube)[3]), {-1, 2, 3});
    const auto bounds = RayCaster::worldBounds(scene, *assets, rig, {}, &pose);
    requireVectorNear(bounds.minimum, {3, -1.5F, 1});
    requireVectorNear(bounds.maximum, {7, 1.5F, 3});
    REQUIRE(RayCaster::pick(scene, *assets, {{5, 0, 10}, {0, 0, -1}}, {}, &pose) == cube);
    REQUIRE(RayCaster::pick(scene, *assets, {{5, 0, 10}, {0, 0, -1}}) == 0);
    REQUIRE(view.worldInverse(cube));
    requireVectorNear(glm::vec3(*view.worldInverse(cube) * glm::vec4(5, 0, 2, 1)), {0, 0, 0});

    renderer_gl::Renderer renderer;
    renderer.resize(800, 400);
    QString error;
    REQUIRE(renderer_gl::validatePoseGeometry(*pose.numerics, *pose.geometry, renderer.camera(),
                                              camera, error));
    const auto cameraView = view.cameraView(camera, 2);
    REQUIRE(cameraView);
    requireVectorNear(cameraView->position, {13, 0, 0});
    requireVectorNear(cameraView->forward, {-1, 0, 0});
    requireVectorNear(cameraView->up, {0, 1, 0});
    const auto rendered = renderer.renderView(scene, camera, &pose);
    REQUIRE(rendered.valid);
    REQUIRE(rendered.previewCamera == camera);
    requireVectorNear(rendered.position, cameraView->position);
    requireVectorNear(rendered.forward, cameraView->forward);
    requireMatrixNear(rendered.viewMatrix, cameraView->viewMatrix);
    requireMatrixNear(rendered.projectionMatrix, cameraView->projectionMatrix);
    REQUIRE(view.lighting());
    requireVectorNear(view.lighting()->direction, {1, 0, 0});
    REQUIRE(view.lighting()->intensity == 2);
    const auto handle = renderer.gizmoHandle(scene, cube, renderer_gl::GizmoSpace::Local,
                                             std::nullopt, &pose);
    requireVectorNear(handle.origin, {5, 0, 2});
    requireVectorNear(handle.basis[0], {0, 0, -1});
    requireVectorNear(handle.basis[1], {0, 1, 0});
    requireVectorNear(handle.basis[2], {1, 0, 0});
    REQUIRE(renderer.focusEntity(scene, *assets, cube, &pose));
    requireVectorNear(renderer.camera().target(), {5, 0, 2});
    REQUIRE(scene.find(rig)->transform.position == glm::vec3(1, 2, 3));
    REQUIRE(scene.find(rig)->transform.rotation == glm::quat(1, 0, 0, 0));
}

TEST_CASE("Installed visibility keeps collection ancestry isolation and hidden identities",
          "[animation][installed-pose][visibility]") {
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto parent = scene.createEntity("Parent");
    const auto first = scene.createEntity("First", parent, core::PrimitiveKind::Cube);
    const auto leaf = scene.createEntity("Leaf", first, core::PrimitiveKind::Cube);
    const auto second = scene.createEntity("Second", 0, core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position.x = 10;
    REQUIRE(scene.setTransform(second, transform));
    REQUIRE(scene.replaceCollections({{1, "Hidden ancestors", false, {parent}}}));
    core::ViewportVisibility visibility;
    visibility.localRoot = first;
    auto pose = evaluatedScene(scene, assets, {}, 1, visibility);
    REQUIRE_FALSE(ScenePoseView(scene, &pose).isVisible(first));
    REQUIRE_FALSE(ScenePoseView(scene, &pose).isVisible(leaf));
    REQUIRE_FALSE(ScenePoseView(scene, &pose).isVisible(second));
    REQUIRE(pose.geometry->entries.at(first).localBounds.isValid());
    REQUIRE(scene.replaceCollections({{1, "Visible ancestors", true, {parent}}}));
    pose = evaluatedScene(scene, assets, {}, 1, visibility);
    REQUIRE_FALSE(ScenePoseView(scene, &pose).isVisible(parent));
    REQUIRE(ScenePoseView(scene, &pose).isVisible(first));
    REQUIRE(ScenePoseView(scene, &pose).isVisible(leaf));
    REQUIRE_FALSE(ScenePoseView(scene, &pose).isVisible(second));
    REQUIRE(RayCaster::worldBounds(scene, *assets, first, {}, &pose).isValid());
    REQUIRE(RayCaster::worldBounds(scene, *assets, parent, {}, &pose).isValid());

    visibility.localRoot = 0;
    visibility.hiddenObjects = {first};
    const auto firstHidden = evaluatedScene(scene, assets, {}, 1, visibility);
    visibility.hiddenObjects = {second};
    const auto secondHidden = evaluatedScene(scene, assets, {}, 1, visibility);
    REQUIRE_FALSE(firstHidden.geometry->entries.at(first).visible);
    REQUIRE(firstHidden.geometry->visibility.hiddenObjects == std::set<core::EntityId>{first});
    REQUIRE_FALSE(firstHidden.geometry->entries.at(leaf).visible);
    REQUIRE(firstHidden.geometry->entries.at(second).visible);
    REQUIRE(secondHidden.geometry->entries.at(first).visible);
    REQUIRE(secondHidden.geometry->entries.at(leaf).visible);
    REQUIRE_FALSE(secondHidden.geometry->entries.at(second).visible);
    REQUIRE(secondHidden.geometry->visibility.hiddenObjects == std::set<core::EntityId>{second});
    REQUIRE(RayCaster::sceneBounds(scene, *assets, {}, &firstHidden).maximum.x == 10.5F);
    REQUIRE(RayCaster::sceneBounds(scene, *assets, {}, &secondHidden).maximum.x == 0.5F);
}

TEST_CASE("Pose ray distances remain comparable under signed nonuniform parent scales",
          "[animation][installed-pose][picking]") {
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto far = scene.createEntity("Far", 0, core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.scale = {-4, 2, 4};
    REQUIRE(scene.setTransform(far, transform));
    const auto parent = scene.createEntity("Animated parent");
    transform = {};
    transform.rotation = glm::angleAxis(glm::radians(45.0F), glm::vec3(0, 0, 1));
    transform.scale = {-1, 2, 1};
    REQUIRE(scene.setTransform(parent, transform));
    const auto near = scene.createEntity("Near", parent, core::PrimitiveKind::Cube);
    transform = {};
    transform.scale = glm::vec3(0.2F);
    REQUIRE(scene.setTransform(near, transform));
    core::SceneAnimation animation;
    animation.tracks[{parent, core::AnimationChannel::Position}] =
        {{{1, {0, 0, 0}}, {49, {0, 0, 3}}}};
    const auto pose = evaluatedScene(scene, assets, animation, 49);
    REQUIRE(RayCaster::pick(scene, *assets, {{0, 0, 5}, {0, 0, -1}}, {}, &pose) == near);
    REQUIRE(scene.setTransform(far, {}));
    REQUIRE(scene.setTransform(near, {}));
    const auto tied = evaluatedScene(scene, assets);
    REQUIRE(RayCaster::pick(scene, *assets, {{0, 0, 5}, {0, 0, -1}}, {}, &tied) == far);
}

TEST_CASE("Installed editable geometry keeps content ownership and exact element mask identities",
          "[animation][installed-pose][editable-mesh]") {
    core::Scene scene;
    auto assets = std::make_shared<assets::AssetManager>();
    const auto cube = scene.createEntity("Editable");
    auto mesh = core::modeling::createEditableCube();
    mesh.vertices.push_back({999, {10, 0, 0}});
    mesh.vertices.push_back({1000, {-20, 0, 0}});
    std::string error;
    const auto prepared = scene.prepareEditableGeometry(cube, mesh, error);
    REQUIRE(prepared);
    REQUIRE(scene.installGeometry(*prepared));
    core::ViewportVisibility visibility;
    visibility.editedEntity = cube;
    visibility.vertices = {999};
    const auto firstMask = evaluatedScene(scene, assets, {}, 1, visibility);
    visibility.vertices = {1000};
    const auto secondMask = evaluatedScene(scene, assets, {}, 1, visibility);
    REQUIRE(firstMask.geometry->entries.at(cube).content == prepared->content());
    REQUIRE(firstMask.geometry->entries.at(cube).evaluationRevision ==
            scene.editableMesh(scene.find(cube)->editableMesh)->evaluationRevision);
    REQUIRE(firstMask.geometry->visibility.vertices == std::set<core::modeling::VertexId>{999});
    REQUIRE(secondMask.geometry->visibility.vertices == std::set<core::modeling::VertexId>{1000});
    REQUIRE(firstMask.geometry->entries.at(cube).localBounds.maximum.x == 0.5F);
    REQUIRE(firstMask.geometry->entries.at(cube).localBounds.minimum.x == -20);
    REQUIRE(secondMask.geometry->entries.at(cube).localBounds.maximum.x == 10);
    REQUIRE(secondMask.geometry->entries.at(cube).localBounds.minimum.x == -0.5F);

    const auto before = evaluatedScene(scene, assets);
    for (auto& vertex : mesh.vertices)
        vertex.position.x *= 2;
    const auto changed = scene.prepareEditableGeometry(cube, mesh, error);
    REQUIRE(changed);
    REQUIRE(scene.installGeometry(*changed));
    const auto after = evaluatedScene(scene, assets);
    REQUIRE(after.geometry->entries.at(cube).content != before.geometry->entries.at(cube).content);
    REQUIRE(before.geometry->entries.at(cube).content == prepared->content());
    REQUIRE(after.geometry->entries.at(cube).evaluationRevision >
            before.geometry->entries.at(cube).evaluationRevision);
    const auto cachedBounds = RayCaster::worldBounds(scene, *assets, cube, {}, &before);
    REQUIRE(cachedBounds.minimum.x == -20);
    REQUIRE(cachedBounds.maximum.x == 10);
    REQUIRE(RayCaster::worldBounds(scene, *assets, cube).maximum.x == 20);
    const std::weak_ptr<const assets::AssetManager> owner = assets;
    assets.reset();
    REQUIRE_FALSE(owner.expired());
    REQUIRE(before.geometry->assets);
    REQUIRE(RayCaster::worldBounds(scene, *before.geometry->assets, cube, {}, &before).maximum.x ==
            10);
}

TEST_CASE("Installed light selection uses frozen visibility and animated rigid direction",
          "[animation][installed-pose][light]") {
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto rig = scene.createEntity("Rig");
    const auto first = scene.createEntity("First light", rig);
    const auto second = scene.createEntity("Second light");
    REQUIRE(scene.setLight(first, {{1, 0, 0}, 2}));
    REQUIRE(scene.setLight(second, {{0, 1, 0}, 3}));
    core::SceneAnimation animation;
    animation.tracks[{rig, core::AnimationChannel::RotationEulerXYZDegrees}] =
        {{{1, {0, 90, 0}}}};
    const auto visible = evaluatedScene(scene, assets, animation);
    requireVectorNear(ScenePoseView(scene, &visible).lighting()->direction, {1, 0, 0});
    REQUIRE(ScenePoseView(scene, &visible).lighting()->intensity == 2);
    core::ViewportVisibility visibility;
    visibility.hiddenObjects = {rig};
    const auto firstHidden = evaluatedScene(scene, assets, animation, 1, visibility);
    REQUIRE(ScenePoseView(scene, &firstHidden).lighting()->color == glm::vec3(0, 1, 0));
    REQUIRE(ScenePoseView(scene, &firstHidden).lighting()->intensity == 3);
    visibility.hiddenObjects.insert(second);
    const auto allHidden = evaluatedScene(scene, assets, animation, 1, visibility);
    REQUIRE(ScenePoseView(scene, &allHidden).lighting()->intensity == 0);
    REQUIRE(ScenePoseView(scene, &allHidden).lighting()->ambient == scene.lighting().ambient);
}

TEST_CASE("Incomplete installed poses refuse preview reads instead of using base transforms",
          "[animation][installed-pose][validation]") {
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto cube = scene.createEntity("Cube", 0, core::PrimitiveKind::Cube);
    const auto camera = scene.createEntity("Camera");
    REQUIRE(scene.setCamera(camera, {}));
    auto pose = evaluatedScene(scene, assets);
    auto incomplete = std::make_shared<renderer_gl::PoseGeometry>(*pose.geometry);
    incomplete->entries.erase(camera);
    pose.geometry = incomplete;
    const ScenePoseView view(scene, &pose);
    REQUIRE_FALSE(view.worldMatrix(camera));
    REQUIRE_FALSE(view.worldInverse(camera));
    REQUIRE_FALSE(view.worldRotation(camera));
    REQUIRE_FALSE(view.cameraView(camera, 1));
    REQUIRE_FALSE(view.lighting());
    renderer_gl::Renderer renderer;
    REQUIRE_FALSE(renderer.renderView(scene, camera, &pose).valid);
    REQUIRE(renderer.renderView(scene, camera).valid);
    REQUIRE_FALSE(RayCaster::worldBounds(scene, *assets, cube, {}, &pose).isValid());
    REQUIRE_FALSE(RayCaster::sceneBounds(scene, *assets, {}, &pose).isValid());
    REQUIRE(RayCaster::pick(scene, *assets, {{0, 0, 5}, {0, 0, -1}}, {}, &pose) == 0);
    QString error;
    REQUIRE_FALSE(renderer_gl::validatePoseGeometry(*pose.numerics, *pose.geometry,
                                                    renderer.camera(), camera, error));
    REQUIRE_FALSE(error.isEmpty());
}

TEST_CASE("Geometry validation rejects actual float corners and every device projection",
          "[animation][installed-pose][validation]") {
    const auto assets = std::make_shared<assets::AssetManager>();
    renderer_gl::EditorCamera editorCamera;
    QString error;
    SECTION("Finite world matrix can overflow an actual bounding corner") {
        core::Scene scene;
        const auto cube = scene.createEntity("Cube", 0, core::PrimitiveKind::Cube);
        core::Transform transform;
        transform.scale.x = 2;
        REQUIRE(scene.setTransform(cube, transform));
        auto pose = evaluatedScene(scene, assets);
        REQUIRE(renderer_gl::validatePoseGeometry(*pose.numerics, *pose.geometry, editorCamera,
                                                  0, error));
        auto geometry = std::make_shared<renderer_gl::PoseGeometry>(*pose.geometry);
        geometry->entries.at(cube).localBounds.minimum.x = -2.0e38F;
        geometry->entries.at(cube).localBounds.maximum.x = 2.0e38F;
        REQUIRE(geometry->entries.at(cube).localBounds.isValid());
        REQUIRE_FALSE(renderer_gl::validatePoseGeometry(*pose.numerics, *geometry, editorCamera,
                                                        0, error));
        REQUIRE(error.contains(QStringLiteral("八角")));
    }
    SECTION("Hidden camera must also have a finite actual view projection") {
        core::Scene scene;
        const auto camera = scene.createEntity("Hidden camera");
        REQUIRE(scene.setCamera(camera, {1, 0.1F, 1000}));
        core::Transform transform;
        transform.position.x = 2.0e38F;
        REQUIRE(scene.setTransform(camera, transform));
        REQUIRE(scene.setVisible(camera, false));
        const auto pose = evaluatedScene(scene, assets);
        REQUIRE_FALSE(renderer_gl::validatePoseGeometry(*pose.numerics, *pose.geometry,
                                                        editorCamera, 0, error));
        REQUIRE(error.contains(QStringLiteral("观察投影")));
    }
    SECTION("An unavailable requested preview camera is rejected") {
        core::Scene scene;
        const auto camera = scene.createEntity("Camera");
        REQUIRE(scene.setCamera(camera, {}));
        core::ViewportVisibility visibility;
        visibility.hiddenObjects.insert(camera);
        const auto pose = evaluatedScene(scene, assets, {}, 1, visibility);
        REQUIRE_FALSE(renderer_gl::validatePoseGeometry(*pose.numerics, *pose.geometry,
                                                        editorCamera, camera, error));
        REQUIRE(error.contains(QStringLiteral("预览相机")));
    }
    SECTION("A finite masked bound cannot hide overflow in the full frozen CPU geometry") {
        core::Scene scene;
        const auto cube = scene.createEntity("Cube", 0, core::PrimitiveKind::Cube);
        core::Transform transform;
        transform.scale.x = 2;
        REQUIRE(scene.setTransform(cube, transform));
        const auto pose = evaluatedScene(scene, assets);
        auto geometry = std::make_shared<renderer_gl::PoseGeometry>(*pose.geometry);
        geometry->entries.at(cube).fullLocalBounds.minimum.x = -2.0e38F;
        geometry->entries.at(cube).fullLocalBounds.maximum.x = 2.0e38F;
        REQUIRE_FALSE(renderer_gl::validatePoseGeometry(*pose.numerics, *geometry,
                                                        editorCamera, 0, error));
        REQUIRE(error.contains(QStringLiteral("完整几何八角")));
    }
    SECTION("Sun devices reject overflow in the actual rigid direction") {
        core::Scene scene;
        const auto sun = scene.createEntity("Sun");
        REQUIRE(scene.setSun(sun, {}));
        const auto pose = evaluatedScene(scene, assets);
        REQUIRE(renderer_gl::validatePoseGeometry(*pose.numerics, *pose.geometry,
                                                   editorCamera, 0, error));
        auto numerics = *pose.numerics;
        numerics.nodes.front().rigidWorldRotation = glm::quat(1, 2.0e38F, 0, 0);
        REQUIRE_FALSE(renderer_gl::validatePoseGeometry(numerics, *pose.geometry,
                                                        editorCamera, 0, error));
        REQUIRE(error.contains(QStringLiteral("光方向")));
    }
}

TEST_CASE("Pose picking rejects nonfinite and unnormalizable actual rays",
          "[animation][installed-pose][picking]") {
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto cube = scene.createEntity("Cube", 0, core::PrimitiveKind::Cube);
    const auto pose = evaluatedScene(scene, assets);
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto maximum = std::numeric_limits<float>::max();
    const auto tiny = std::numeric_limits<float>::denorm_min();
    for (const core::Ray ray : {core::Ray{{infinity, 0, 5}, {0, 0, -1}},
                                core::Ray{{0, 0, 5}, {0, 0, 0}},
                                core::Ray{{0, 0, 5}, {maximum, 0, -1}},
                                core::Ray{{0, 0, 5}, {0, 0, -tiny}},
                                core::Ray{{0, 0, 5}, {0, 0, -infinity}}}) {
        REQUIRE(RayCaster::pick(scene, *assets, ray, {}, &pose) == 0);
    }
    REQUIRE(RayCaster::pick(scene, *assets, {{0, 0, 5}, {0, 0, -1}}, {}, &pose) == cube);
    auto numerics = std::make_shared<core::EvaluatedPose>(*pose.numerics);
    numerics->nodes.front().worldInverse[2][2] = maximum;
    auto invalidRayPose = pose;
    invalidRayPose.numerics = numerics;
    REQUIRE(RayCaster::pick(scene, *assets, {{0, 0, 0}, {0, 0, -1}}, {}, &invalidRayPose) == 0);
}

TEST_CASE("Ten thousand inherited pose nodes use cached geometry through repeated ticks",
          "[animation][installed-pose][deep-hierarchy]") {
    constexpr std::size_t count = 10000;
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    core::EntityId parent = 0;
    core::EntityId root = 0;
    for (std::size_t index = 0; index < count; ++index) {
        parent = scene.createEntity("Deep node", parent,
                                    index + 1 == count ? core::PrimitiveKind::Cube
                                                       : core::PrimitiveKind::Empty);
        REQUIRE(parent != 0);
        if (index == 0)
            root = parent;
    }
    core::SceneAnimation animation;
    animation.tracks[{root, core::AnimationChannel::Position}] =
        {{{1, {0, 0, 0}}, {49, {8, 0, 0}}}};
    auto pose = evaluatedScene(scene, assets, animation, 25);
    REQUIRE(pose.numerics->nodes.size() == count);
    REQUIRE(pose.geometry->entries.size() == count);
    const auto cached = pose.geometry;
    renderer_gl::Renderer renderer;
    QString error;
    REQUIRE(renderer_gl::validatePoseGeometry(*pose.numerics, *cached, renderer.camera(), 0,
                                              error));
    requireVectorNear(RayCaster::worldBounds(scene, *assets, root, {}, &pose).minimum,
                      {3.5F, -0.5F, -0.5F});
    REQUIRE(RayCaster::pick(scene, *assets, {{4, 0, 5}, {0, 0, -1}}, {}, &pose) == parent);
    REQUIRE(renderer.focusEntity(scene, *assets, root, &pose));
    requireVectorNear(renderer.camera().target(), {4, 0, 0});
    std::string sourceError;
    const auto inputs = scene.animationPoseInputs(sourceError);
    REQUIRE(inputs);
    for (const auto frame : {49.0, 1.0, 25.5}) {
        auto result = core::evaluateAnimationPose(*inputs, animation, frame);
        REQUIRE(result.pose);
        pose.numerics = std::make_shared<const core::EvaluatedPose>(std::move(*result.pose));
        pose.identity.frame = frame;
        ++pose.identity.evaluationId;
        REQUIRE(pose.geometry == cached);
        REQUIRE(renderer_gl::validatePoseGeometry(*pose.numerics, *cached, renderer.camera(), 0,
                                                  error));
        const float x = static_cast<float>((frame - 1) / 48 * 8);
        REQUIRE(RayCaster::pick(scene, *assets, {{x, 0, 5}, {0, 0, -1}}, {}, &pose) == parent);
        requireVectorNear(RayCaster::sceneBounds(scene, *assets, {}, &pose).maximum,
                          {x + 0.5F, 0.5F, 0.5F});
        requireVectorNear(renderer.gizmoHandle(scene, parent, renderer_gl::GizmoSpace::World,
                                                std::nullopt, &pose).origin,
                          {x, 0, 0});
    }
}

TEST_CASE("J1 installed state keeps direct double values immutable CPU stages and frozen lighting",
          "[animation][installed-pose][j1-contract]") {
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto cube = scene.createEntity("Editable");
    const auto sun = scene.createEntity("Sun");
    REQUIRE(scene.setSun(sun, {}));
    std::string error;
    const auto geometry = scene.prepareEditableGeometry(cube, core::modeling::createEditableCube(), error);
    REQUIRE(geometry);
    REQUIRE(scene.installGeometry(*geometry));
    core::Transform base;
    base.rotation = glm::quat(2, 0, 0, 0);
    REQUIRE(scene.installTransformSnapshot(cube, base));
    core::SceneAnimation animation;
    core::ScalarCurve x{core::CurveId{1}, core::PropertyId::Position, core::PropertyComponent::X,
        {{core::KeyId{1}, 1, 1.000000000001}, {core::KeyId{2}, 24, 1.000000000003}}};
    animation.properties.actions = {{core::ActionId{1}, {
        {core::ActionSlotId{1}, core::PropertyOwnerKind::Entity, {x}}}}};
    animation.properties.bindings = {{scene.documentUuid(), core::EntityOwner{cube},
                                     core::ActionId{1}, core::ActionSlotId{1}}};
    auto pose = evaluatedScene(scene, assets, animation, 12.5);
    const auto legacyNumerics = pose.numerics;
    const auto inputs = scene.animationPoseInputs(error);
    REQUIRE(inputs);
    core::SourceIdentity source;
    source.documentUuid = scene.documentUuid();
    source.sourceHandle = core::LiveSourceHandle{"test-instance", "test-document"};
    source.contentRevision = pose.identity.sourceRevision;
    source.frameTime = pose.identity.frame;
    source.authorContextIdentity = {source.documentUuid, source.contentRevision,
                                   std::get<core::LiveSourceHandle>(source.sourceHandle)};
    source.evaluationSessionId = core::createEvaluationSessionId();
    source.evaluationId = pose.identity.evaluationId;
    auto result = core::evaluateSceneState(*inputs, animation, pose.geometry->authors,
                                          pose.geometry->sceneGeometry, source);
    REQUIRE(result.state);
    pose.coverage = renderer_gl::PoseStateCoverage::CompleteSceneJ1;
    pose.sceneState = std::move(result.state);
    const auto& state = *pose.sceneState;
    REQUIRE(state.find(cube)->localPosition.x == std::lerp(1.000000000001, 1.000000000003, .5));
    REQUIRE(state.find(cube)->localPosition.x != static_cast<double>(static_cast<float>(state.find(cube)->localPosition.x)));
    REQUIRE(state.find(cube)->localRotation == glm::dquat(base.rotation));
    REQUIRE_FALSE(state.find(cube)->rotationEulerXYZDegrees);
    REQUIRE(pose.numerics == legacyNumerics);
    REQUIRE(pose.numerics->find(cube)->local.position.x == 0);
    const auto checked = core::adaptSceneStateToPose(state);
    REQUIRE(checked.pose);
    renderer_gl::EditorCamera editorCamera;
    QString validationError;
    REQUIRE(renderer_gl::validatePoseGeometry(*checked.pose, *pose.geometry,
                                              editorCamera, 0, validationError));
    const auto& entry = pose.geometry->entries.at(cube);
    const auto& stage = state.content->geometryStages.at(cube);
    REQUIRE(entry.cpuMesh.get() == &entry.content->displayedDerived().mesh);
    REQUIRE(stage.geometry.mesh == entry.cpuMesh);
    REQUIRE(stage.geometry.editableContent == entry.content);
    REQUIRE(stage.identity.mesh == scene.find(cube)->editableMesh);
    REQUIRE(stage.identity.contentVersion == scene.editableMesh(entry.editableMesh)->evaluationRevision);
    REQUIRE(stage.identity.worldAffine == state.find(cube)->worldAffine);
    REQUIRE(state.content->geometry == pose.geometry->sceneGeometry);
    REQUIRE(state.content->visibleSuns.front().directionToLight == glm::dvec3(0, 0, 1));
    const auto originalLighting = scene.lighting();
    auto changedLighting = originalLighting;
    changedLighting.intensity = 3;
    REQUIRE(scene.setLighting(changedLighting));
    REQUIRE(ScenePoseView(scene, &pose).lighting() == originalLighting);
    REQUIRE(state.content->legacyLighting == originalLighting);
    auto changedMesh = core::modeling::createEditableCube();
    for (auto& vertex : changedMesh.vertices) vertex.position *= 2;
    const auto changed = scene.prepareEditableGeometry(cube, changedMesh, error);
    REQUIRE(changed);
    REQUIRE(scene.installGeometry(*changed));
    REQUIRE(scene.editableMesh(entry.editableMesh)->content != stage.geometry.editableContent);
    REQUIRE(stage.identity.contentVersion < scene.editableMesh(entry.editableMesh)->evaluationRevision);
    REQUIRE(stage.geometry.mesh->bounds().maximum.x == .5F);

    auto invalidGeometry = std::make_shared<core::SceneGeometrySnapshot>(*pose.geometry->sceneGeometry);
    auto invalidMesh = std::make_shared<core::MeshData>(*entry.cpuMesh);
    invalidMesh->vertices.front().normal = glm::vec3(0);
    invalidGeometry->entries.at(cube).mesh = std::move(invalidMesh);
    const auto invalid = core::evaluateSceneState(*inputs, animation, pose.geometry->authors,
                                                 std::move(invalidGeometry), source);
    REQUIRE(invalid.state);
    const auto rejected = core::adaptSceneStateToPose(*invalid.state);
    REQUIRE_FALSE(rejected.pose);
    REQUIRE(rejected.validation.error == core::AnimationError::InvalidWorldNormal);
}

TEST_CASE("Joint checked stages drive bounds picking camera and gizmos under inherited shear",
          "[animation][installed-pose][j1-consumer]") {
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto parent = scene.createEntity("Signed parent");
    const auto cube = scene.createEntity("Joint cube", parent, core::PrimitiveKind::Cube);
    const auto camera = scene.createEntity("Joint camera", parent);
    core::Transform transform;
    transform.scale = {-2, 3, .5F};
    transform.rotation = glm::angleAxis(glm::radians(20.0F), glm::vec3(0, 1, 0));
    REQUIRE(scene.setTransform(parent, transform));
    transform = {};
    transform.rotation = glm::angleAxis(glm::radians(45.0F), glm::vec3(0, 0, 1));
    REQUIRE(scene.setTransform(cube, transform));
    transform = {};
    transform.position = {0, 0, 6};
    REQUIRE(scene.setTransform(camera, transform));
    REQUIRE(scene.setCamera(camera, {50, .1F, 200}));
    core::ScalarCurve x{core::CurveId{1}, core::PropertyId::Position, core::PropertyComponent::X,
        {{core::KeyId{1}, 1, .5}, {core::KeyId{2}, 24, 2.5}}};
    core::SceneAnimation animation;
    animation.properties.actions = {{core::ActionId{1}, {
        {core::ActionSlotId{1}, core::PropertyOwnerKind::Entity, {x}}}}};
    animation.properties.bindings = {{scene.documentUuid(), core::EntityOwner{cube},
                                     core::ActionId{1}, core::ActionSlotId{1}}};
    auto pose = jointScene(scene, assets, animation, 12.5);
    const ScenePoseView view(scene, &pose);
    const auto& stage = pose.sceneState->content->geometryStages.at(cube);
    const auto expected = glm::mat4(stage.identity.worldAffine);
    REQUIRE(view.worldMatrix(cube) == expected);
    REQUIRE(view.worldMatrix(cube) != pose.numerics->find(cube)->world);
    REQUIRE(view.numerics() == pose.sceneNumerics.get());
    REQUIRE(view.worldInverse(cube) == glm::mat4(pose.sceneState->find(cube)->worldInverse));
    REQUIRE(glm::determinant(glm::mat3(expected)) < 0);
    REQUIRE(std::abs(glm::dot(glm::vec3(expected[0]), glm::vec3(expected[1]))) > 1);
    const auto expectedBounds = stage.geometry.localBounds.transformed(expected);
    const auto bounds = RayCaster::worldBounds(scene, *assets, cube, {}, &pose);
    REQUIRE(bounds.minimum == expectedBounds.minimum);
    REQUIRE(bounds.maximum == expectedBounds.maximum);
    const auto center = glm::vec3(expected[3]);
    REQUIRE(RayCaster::pick(scene, *assets, {center + glm::vec3(0, 0, 8), {0, 0, -1}}, {}, &pose) == cube);
    renderer_gl::Renderer renderer;
    renderer.resize(640, 480);
    requireVectorNear(renderer.gizmoHandle(scene, cube, renderer_gl::GizmoSpace::Local,
                                            std::nullopt, &pose).origin, center);
    const auto device = view.cameraView(camera, 4.0F / 3);
    REQUIRE(device);
    requireVectorNear(device->position, glm::vec3(pose.sceneState->find(camera)->worldAffine[3]));
    REQUIRE(renderer.renderView(scene, camera, &pose).viewMatrix == device->viewMatrix);

    // 同一完整状态不能偷偷改变旧 float 消费域的运算结果。
    pose.consumerDomain = renderer_gl::PoseConsumerDomain::LegacyTrsV1;
    const ScenePoseView legacy(scene, &pose);
    REQUIRE(legacy.numerics() == pose.numerics.get());
    REQUIRE(legacy.worldMatrix(cube) == pose.numerics->find(cube)->world);
    REQUIRE(legacy.worldMatrix(cube) != expected);
}

TEST_CASE("Joint incomplete identity checked values and stages reject every consumer",
          "[animation][installed-pose][j1-consumer]") {
    core::Scene scene;
    const auto assets = std::make_shared<assets::AssetManager>();
    const auto cube = scene.createEntity("Joint cube", 0, core::PrimitiveKind::Cube);
    auto pose = jointScene(scene, assets, {}, 1);
    SECTION("Missing complete state") { pose.sceneState.reset(); }
    SECTION("Missing checked numerics") { pose.sceneNumerics.reset(); }
    SECTION("Wrong installed time") { pose.identity.frame = 12.5; }
    SECTION("Wrong consumer profile") {
        pose.sceneState->source.evaluationProfile = core::EvaluationProfile::BasicAnimationJ1;
    }
    SECTION("Wrong checked matrix") {
        auto changed = std::make_shared<core::EvaluatedPose>(*pose.sceneNumerics);
        changed->nodes.front().world[3].x = 10;
        pose.sceneNumerics = std::move(changed);
    }
    SECTION("Missing geometry stage") {
        auto content = std::make_shared<core::EvaluatedSceneContent>(*pose.sceneState->content);
        content->geometryStages.erase(cube);
        pose.sceneState->content = std::move(content);
    }
    SECTION("Wrong geometry stage affine") {
        auto content = std::make_shared<core::EvaluatedSceneContent>(*pose.sceneState->content);
        content->geometryStages.at(cube).identity.worldAffine[3].x = 10;
        pose.sceneState->content = std::move(content);
    }
    const ScenePoseView view(scene, &pose);
    REQUIRE_FALSE(view.isComplete());
    REQUIRE_FALSE(view.numerics());
    REQUIRE_FALSE(view.worldMatrix(cube));
    REQUIRE_FALSE(view.worldInverse(cube));
    REQUIRE_FALSE(view.worldRotation(cube));
    REQUIRE_FALSE(view.isVisible(cube));
    REQUIRE_FALSE(RayCaster::sceneBounds(scene, *assets, {}, &pose).isValid());
    REQUIRE(RayCaster::pick(scene, *assets, {{0, 0, 4}, {0, 0, -1}}, {}, &pose) == 0);
    renderer_gl::Renderer renderer;
    REQUIRE_FALSE(renderer.renderView(scene, 0, &pose).valid);
}
