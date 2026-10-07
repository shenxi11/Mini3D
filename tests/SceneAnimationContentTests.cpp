/*
 * 模块名: SceneAnimationContentTests
 * 功能概述: 验证正式动画真源、共同结构候选与精确历史回放的纯 CPU 合同。
 * 对外接口: Catch2 [scene-animation] 用例，无其他导出。
 * 依赖关系: Scene、Animation、EvaluatedPose、Catch2，无 Qt/OpenGL。
 * 输入输出: 动画及结构候选到原值、绑定、身份、完整数值输入与原子拒绝断言。
 * 异常与错误: 失效来源、非法邻接或超限候选不得发布部分节点或曲线。
 * 维护说明: 历史按逆序撤销；大场景只使用迭代数值输入，不递归复制节点树。
 */
#include "core/Scene.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace mini3d::core;
namespace {
AnimationTrack constantTrack(const glm::dvec3& value) {
    return {{{1, value, AnimationInterpolation::Constant}}};
}
AnimationTrack linearTrack(const glm::dvec3& first, const glm::dvec3& last) {
    return {
        {{1, first, AnimationInterpolation::Linear}, {49, last, AnimationInterpolation::Linear}}};
}
void installAnimation(Scene& scene, const SceneAnimation& animation) {
    std::string error;
    auto prepared = scene.prepareAnimation(animation, error);
    REQUIRE(prepared);
    REQUIRE(error.empty());
    REQUIRE(scene.installPreparedAnimation(*prepared));
}
const AnimationPoseInput* inputFor(const std::vector<AnimationPoseInput>& inputs, EntityId entity) {
    const auto found = std::find_if(inputs.begin(), inputs.end(), [entity](const auto& input) {
        return input.entity == entity;
    });
    return found == inputs.end() ? nullptr : &*found;
}
std::vector<EntityId> inputIds(const std::vector<AnimationPoseInput>& inputs) {
    std::vector<EntityId> result;
    for (const auto& input : inputs)
        result.push_back(input.entity);
    return result;
}
void requireInputsEqual(const std::vector<AnimationPoseInput>& left,
                        const std::vector<AnimationPoseInput>& right) {
    REQUIRE(left.size() == right.size());
    for (std::size_t index = 0; index < left.size(); ++index) {
        REQUIRE(left[index].entity == right[index].entity);
        REQUIRE(left[index].parent == right[index].parent);
        REQUIRE(left[index].base.position == right[index].base.position);
        REQUIRE(left[index].base.rotation == right[index].base.rotation);
        REQUIRE(left[index].base.scale == right[index].base.scale);
    }
}
} // namespace

TEST_CASE("Scene animation replay preserves exact continuous values and immutable identities",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto entity = scene.createEntity("Animated");
    const auto* before = &scene.animation();
    const auto* base = scene.find(entity);
    SceneAnimation animation;
    const glm::dvec3 raw{720.000000001, 3599999999.9, 3600000000.0};
    animation.tracks[{entity, AnimationChannel::RotationEulerXYZDegrees}] = constantTrack(raw);
    std::string error;
    auto prepared = scene.prepareAnimation(animation, error);
    REQUIRE(prepared);
    REQUIRE(prepared->hasChanges());
    REQUIRE(&prepared->before() == before);
    const auto* after = &prepared->after();
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(scene.canInstallPreparedAnimation(*prepared));
        REQUIRE(scene.installPreparedAnimation(*prepared));
        REQUIRE_FALSE(scene.installPreparedAnimation(*prepared));
        REQUIRE(&scene.animation() == after);
        REQUIRE(scene.animation()
                    .tracks.at({entity, AnimationChannel::RotationEulerXYZDegrees})
                    .keys.front()
                    .value == raw);
        REQUIRE(scene.find(entity) == base);
        REQUIRE(scene.find(entity)->transform.rotation == glm::quat(1, 0, 0, 0));
        auto unchanged = scene.prepareAnimation(animation, error);
        REQUIRE(unchanged);
        REQUIRE_FALSE(unchanged->hasChanges());
        REQUIRE(&unchanged->before() == after);
        REQUIRE(&unchanged->after() == after);
        auto rounded = animation;
        rounded.tracks.at({entity, AnimationChannel::RotationEulerXYZDegrees})
            .keys.front()
            .value.x = 720;
        auto changed = scene.prepareAnimation(rounded, error);
        REQUIRE(changed);
        REQUIRE(changed->hasChanges());
        REQUIRE(&scene.animation() == after);
        REQUIRE(scene.canRestorePreparedAnimation(*prepared));
        REQUIRE(scene.restorePreparedAnimation(*prepared));
        REQUIRE_FALSE(scene.restorePreparedAnimation(*prepared));
        REQUIRE(&scene.animation() == before);
        REQUIRE(scene.animation().tracks.empty());
    }
}

TEST_CASE("Scene rejects complete invalid animation candidates before changing formal content",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto entity = scene.createEntity("Scaled");
    SceneAnimation animation;
    animation.tracks[{entity, AnimationChannel::Scale}] = {
        {{1, {1, 1, 1}, AnimationInterpolation::Linear},
         {2, {1, 1, 1}, AnimationInterpolation::Constant},
         {3, {-1, 1, 1}, AnimationInterpolation::Linear}}};
    installAnimation(scene, animation);
    const auto* formal = &scene.animation();
    const auto* node = scene.find(entity);
    auto invalid = animation;
    SECTION("deleting a middle key creates a new forbidden Linear adjacency") {
        auto& keys = invalid.tracks.at({entity, AnimationChannel::Scale}).keys;
        keys.erase(keys.begin() + 1);
    }
    SECTION("an empty track cannot remain after deleting its last key") {
        invalid.tracks.at({entity, AnimationChannel::Scale}).keys.clear();
    }
    SECTION("a binding must refer to a live entity") {
        invalid.tracks[{entity + 100, AnimationChannel::Position}] = constantTrack({1, 2, 3});
    }
    SECTION("raw minimum scale is checked before float rounding") {
        invalid.tracks.at({entity, AnimationChannel::Scale}).keys[0].value.x = 0.00099999999;
    }
    SECTION("settings are part of the formal candidate") {
        invalid.settings.fps = 0;
    }
    std::string error;
    REQUIRE_FALSE(scene.prepareAnimation(invalid, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(&scene.animation() == formal);
    REQUIRE(scene.animation() == animation);
    REQUIRE(scene.find(entity) == node);
    REQUIRE(scene.find(entity)->transform.scale == glm::vec3(1));
}

TEST_CASE("Prepared animation rejects wrong source state and missing install bindings",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto entity = scene.createEntity("Target");
    SceneAnimation animation;
    animation.tracks[{entity, AnimationChannel::Position}] = constantTrack({1, 2, 3});
    std::string error;
    auto prepared = scene.prepareAnimation(animation, error);
    REQUIRE(prepared);
    SECTION("another scene cannot publish the definition") {
        Scene other;
        const auto* formal = &other.animation();
        REQUIRE_FALSE(other.canInstallPreparedAnimation(*prepared));
        REQUIRE_FALSE(other.installPreparedAnimation(*prepared));
        REQUIRE(&other.animation() == formal);
    }
    SECTION("replacement at the same address invalidates the origin token") {
        REQUIRE(scene.replaceNodes(scene.nodes()));
        const auto* formal = &scene.animation();
        REQUIRE_FALSE(scene.installPreparedAnimation(*prepared));
        REQUIRE(&scene.animation() == formal);
    }
    SECTION("deleting an unbound target leaves the before pointer but invalidates the binding") {
        const auto* formal = &scene.animation();
        REQUIRE(scene.removeEntity(entity));
        REQUIRE(&scene.animation() == formal);
        REQUIRE_FALSE(scene.installPreparedAnimation(*prepared));
        REQUIRE(scene.animation().tracks.empty());
    }
    SECTION("a different formal definition must first be undone") {
        SceneAnimation other;
        other.settings.fps = 30;
        auto later = scene.prepareAnimation(other, error);
        REQUIRE(later);
        REQUIRE(scene.installPreparedAnimation(*later));
        const auto* formal = &scene.animation();
        REQUIRE_FALSE(scene.installPreparedAnimation(*prepared));
        REQUIRE(&scene.animation() == formal);
        REQUIRE(scene.restorePreparedAnimation(*later));
        REQUIRE(scene.installPreparedAnimation(*prepared));
        REQUIRE(scene.animation() == animation);
    }
    SECTION("an unprepared value is not a valid command") {
        Scene::PreparedAnimation empty;
        REQUIRE_FALSE(scene.installPreparedAnimation(empty));
        REQUIRE_FALSE(scene.restorePreparedAnimation(empty));
    }
}

TEST_CASE("Prepared animation restore rejects missing before bindings without orphan tracks",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto entity = scene.createEntity("Target");
    SceneAnimation original;
    original.tracks[{entity, AnimationChannel::Position}] = constantTrack({1, 2, 3});
    installAnimation(scene, original);
    std::string error;
    auto clear = scene.prepareAnimation(SceneAnimation{}, error);
    REQUIRE(clear);
    REQUIRE(scene.installPreparedAnimation(*clear));
    const auto* after = &scene.animation();
    REQUIRE(scene.removeEntity(entity));
    REQUIRE(&scene.animation() == after);
    REQUIRE_FALSE(scene.canRestorePreparedAnimation(*clear));
    REQUIRE_FALSE(scene.restorePreparedAnimation(*clear));
    REQUIRE(&scene.animation() == after);
    REQUIRE(scene.animation().tracks.empty());
}

TEST_CASE("Scene document replacement publishes nodes and animation together or neither",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto existing = scene.createEntity("Existing", 0, PrimitiveKind::Cube);
    SceneAnimation original;
    original.tracks[{existing, AnimationChannel::Position}] = constantTrack({3, 0, 0});
    installAnimation(scene, original);
    const auto geometry = scene.geometrySnapshot(existing);
    REQUIRE(geometry);
    const auto* node = scene.find(existing);
    const auto* formal = &scene.animation();
    std::string error;
    auto oldCommand = scene.prepareAnimation(original, error);
    REQUIRE(oldCommand);
    SceneNode incoming;
    incoming.id = 42;
    incoming.name = "Incoming";
    SceneAnimation next;
    next.settings = {48, 10, 20};
    next.tracks[{incoming.id, AnimationChannel::RotationEulerXYZDegrees}] =
        constantTrack({720.000000001, 0, 0});
    auto dangling = next;
    dangling.tracks[{999, AnimationChannel::Position}] = constantTrack({0, 0, 0});
    REQUIRE_FALSE(scene.replaceNodes({incoming}, {}, {}, dangling));
    REQUIRE(scene.find(existing) == node);
    REQUIRE(&scene.animation() == formal);
    auto invalidNode = incoming;
    invalidNode.name.clear();
    REQUIRE_FALSE(scene.replaceNodes({invalidNode}, {}, {}, next));
    REQUIRE(scene.find(existing) == node);
    REQUIRE(&scene.animation() == formal);
    REQUIRE(scene.installGeometry(*geometry));
    REQUIRE(scene.replaceNodes({incoming}, {}, {}, next));
    REQUIRE(scene.find(existing) == nullptr);
    REQUIRE(scene.find(incoming.id));
    REQUIRE(scene.animation() == next);
    REQUIRE_FALSE(scene.installPreparedAnimation(*oldCommand));
    REQUIRE_FALSE(scene.installGeometry(*geometry));
    REQUIRE(scene.replaceNodes({incoming}));
    REQUIRE(scene.animation() == SceneAnimation{});
}

TEST_CASE("Animated subtree duplication maps wide unsorted identities and independent curves",
          "[scene][animation][scene-animation]") {
    Scene scene;
    SceneNode parent;
    parent.id = 9007199254741101ULL;
    parent.name = "External parent";
    parent.transform.position = {3, 0, 0};
    SceneNode root;
    root.id = 9007199254741005ULL;
    root.parent = parent.id;
    root.name = "原根";
    root.transform.position = {1, 0, 0};
    SceneNode child;
    child.id = 9007199254740993ULL;
    child.parent = root.id;
    child.name = "Child";
    child.transform.position = {0, 2, 0};
    SceneNode grandchild;
    grandchild.id = 9007199254740997ULL;
    grandchild.parent = child.id;
    grandchild.name = "Unbound descendant";
    grandchild.transform.position = {0, 0, 4};
    SceneNode tail;
    tail.id = 9007199254741011ULL;
    tail.parent = parent.id;
    tail.name = "Tail";
    SceneAnimation animation;
    animation.tracks[{parent.id, AnimationChannel::Position}] = linearTrack({3, 0, 0}, {5, 0, 0});
    animation.tracks[{root.id, AnimationChannel::RotationEulerXYZDegrees}] =
        constantTrack({720.000000001, 3599999999.9, 3600000000.0});
    animation.tracks[{child.id, AnimationChannel::Position}] = linearTrack({0, 2, 0}, {0, 6, 0});
    REQUIRE(scene.replaceNodes({parent, root, child, grandchild, tail}, {}, {}, animation));
    std::string error;
    auto prepared = scene.prepareDuplicateSubtree(root.id, error, 2048, "原根 副本");
    REQUIRE(prepared);
    const auto identities = prepared->entityIdMap();
    const auto copy = identities.at(root.id);
    const auto copyChild = identities.at(child.id);
    const auto copyGrandchild = identities.at(grandchild.id);
    REQUIRE(prepared->entityIds() == std::vector<EntityId>{copy, copyChild, copyGrandchild});
    REQUIRE(copy > parent.id);
    REQUIRE(&prepared->animationBefore() == &scene.animation());
    REQUIRE(prepared->animationAfter().tracks.size() == 5);
    const auto candidate = scene.animationPoseInputs(*prepared, true, error);
    REQUIRE(candidate);
    REQUIRE(candidate->size() == 8);
    REQUIRE(inputIds(*candidate) == std::vector<EntityId>{parent.id, root.id, child.id,
                                                          grandchild.id, tail.id, copy, copyChild,
                                                          copyGrandchild});
    REQUIRE(inputFor(*candidate, copy)->parent == parent.id);
    REQUIRE(inputFor(*candidate, copyChild)->parent == copy);
    REQUIRE(inputFor(*candidate, copyGrandchild)->parent == copyChild);
    REQUIRE(inputFor(*candidate, copyGrandchild)->base.position == grandchild.transform.position);
    REQUIRE(evaluateAnimationPose(*candidate, prepared->animationAfter(), 13.375).pose);
    REQUIRE(scene.find(copy) == nullptr);
    const SceneNode* copyAddress = nullptr;
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(scene.installPreparedSubtree(*prepared));
        if (cycle == 0)
            copyAddress = scene.find(copy);
        REQUIRE(scene.find(copy) == copyAddress);
        REQUIRE(scene.find(copy)->name == "原根 副本");
        REQUIRE(prepared->entityIdMap() == identities);
        const auto actual = scene.animationPoseInputs(error);
        REQUIRE(actual);
        requireInputsEqual(*candidate, *actual);
        const auto originalTrack =
            AnimationTrackId{root.id, AnimationChannel::RotationEulerXYZDegrees};
        const auto copiedTrack = AnimationTrackId{copy, AnimationChannel::RotationEulerXYZDegrees};
        REQUIRE(scene.animation().tracks.at(originalTrack) ==
                scene.animation().tracks.at(copiedTrack));
        REQUIRE(scene.animation().tracks.at(originalTrack).keys.data() !=
                scene.animation().tracks.at(copiedTrack).keys.data());
        auto edited = scene.animation();
        edited.tracks.at(copiedTrack).keys[0].value.x = 1080.000000001;
        auto later = scene.prepareAnimation(edited, error);
        REQUIRE(later);
        REQUIRE(scene.installPreparedAnimation(*later));
        REQUIRE(scene.animation().tracks.at(originalTrack).keys[0].value.x == 720.000000001);
        REQUIRE(scene.animation().tracks.at(copiedTrack).keys[0].value.x == 1080.000000001);
        const auto* changed = &scene.animation();
        REQUIRE_FALSE(scene.removePreparedSubtree(*prepared));
        REQUIRE(&scene.animation() == changed);
        REQUIRE(scene.find(copy) == copyAddress);
        REQUIRE(scene.restorePreparedAnimation(*later));
        REQUIRE(scene.removePreparedSubtree(*prepared));
        REQUIRE(scene.find(copy) == nullptr);
        REQUIRE(scene.animation() == animation);
        REQUIRE(scene.find(parent.id)->children == std::vector<EntityId>{root.id, tail.id});
    }
}

TEST_CASE("Duplicate source changes invalidate animated candidates before publication",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto root = scene.createEntity("Root");
    const auto child = scene.createEntity("Editable", root, PrimitiveKind::Cube);
    std::string error;
    const auto geometry =
        scene.prepareEditableGeometry(child, modeling::createEditableCube(), error);
    REQUIRE(geometry);
    REQUIRE(scene.installGeometry(*geometry));
    SceneAnimation animation;
    animation.tracks[{root, AnimationChannel::Position}] = constantTrack({1, 0, 0});
    installAnimation(scene, animation);
    auto prepared = scene.prepareDuplicateSubtree(root, error);
    REQUIRE(prepared);
    SECTION("node properties are frozen") {
        REQUIRE(scene.renameEntity(child, "Changed"));
    }
    SECTION("the source mesh content is frozen") {
        auto mesh = geometry->content()->source;
        mesh.vertices[0].position.x += 0.125F;
        const auto changed = scene.prepareEditableGeometry(child, mesh, error);
        REQUIRE(changed);
        REQUIRE(scene.installGeometry(*changed));
    }
    SECTION("formal curves are frozen") {
        auto edited = animation;
        edited.tracks.at({root, AnimationChannel::Position}).keys[0].value.x = 2;
        installAnimation(scene, edited);
    }
    const auto* formal = &scene.animation();
    REQUIRE_FALSE(scene.canInstallPreparedSubtree(*prepared));
    REQUIRE_FALSE(scene.animationPoseInputs(*prepared, true, error));
    REQUIRE_FALSE(scene.installPreparedSubtree(*prepared));
    REQUIRE(scene.find(prepared->rootId()) == nullptr);
    REQUIRE(scene.nodes().size() == 2);
    REQUIRE(&scene.animation() == formal);
}

TEST_CASE("Animated duplication checks added tracks and keys before copying or consuming IDs",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto root = scene.createEntity("Root");
    SceneAnimation animation;
    EntityId highest = root;
    SECTION("3000 existing tracks cannot be doubled") {
        for (std::size_t index = 0; index < 1000; ++index) {
            const auto child = scene.createEntity("Animated child", root);
            highest = child;
            animation.tracks[{child, AnimationChannel::Position}] = constantTrack({0, 0, 0});
            animation.tracks[{child, AnimationChannel::RotationEulerXYZDegrees}] =
                constantTrack({0, 0, 0});
            animation.tracks[{child, AnimationChannel::Scale}] = constantTrack({1, 1, 1});
        }
        REQUIRE(animation.tracks.size() == kAnimationMaximumTracks);
    }
    SECTION("100000 existing keys leave no room for the copied root track") {
        AnimationTrack track;
        track.keys.reserve(kAnimationMaximumTrackKeys);
        for (std::uint32_t frame = 1; frame <= kAnimationMaximumTrackKeys; ++frame)
            track.keys.push_back({frame, {0, 0, 0}, AnimationInterpolation::Linear});
        animation.tracks[{root, AnimationChannel::Position}] = track;
        for (int index = 1; index < 10; ++index) {
            const auto other = scene.createEntity("Other");
            highest = other;
            animation.tracks[{other, AnimationChannel::Position}] = track;
        }
    }
    installAnimation(scene, animation);
    const auto* formal = &scene.animation();
    std::string error;
    REQUIRE_FALSE(scene.prepareDuplicateSubtree(root, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(&scene.animation() == formal);
    REQUIRE(scene.createEntity("Next") == highest + 1);
}

TEST_CASE("Prepared animated deletion restores original node allocations curves meshes and order",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto left = scene.createEntity("Left", parent);
    const auto root = scene.createEntity("Root", parent);
    const auto child = scene.createEntity("Mesh", root, PrimitiveKind::Cube);
    const auto grandchild = scene.createEntity("Unbound descendant", child);
    const auto right = scene.createEntity("Right", parent);
    std::string error;
    const auto geometry =
        scene.prepareEditableGeometry(child, modeling::createEditableCube(), error);
    REQUIRE(geometry);
    REQUIRE(scene.installGeometry(*geometry));
    const auto mesh = scene.find(child)->editableMesh;
    const auto content = scene.editableMesh(mesh)->content;
    const std::vector<SceneCollection> groups{{7, "Group", true, {left, root, child, right}}};
    REQUIRE(scene.replaceCollections(groups));
    SceneAnimation animation;
    animation.tracks[{parent, AnimationChannel::Position}] = linearTrack({0, 0, 0}, {10, 0, 0});
    animation.tracks[{root, AnimationChannel::RotationEulerXYZDegrees}] =
        constantTrack({720.000000001, 3599999999.9, 3600000000.0});
    animation.tracks[{child, AnimationChannel::Scale}] = constantTrack({-1, 2, 3});
    installAnimation(scene, animation);
    const auto* formal = &scene.animation();
    const auto originalInputs = scene.animationPoseInputs(error);
    REQUIRE(originalInputs);
    auto prepared = scene.prepareRemoveSubtree(root, error);
    REQUIRE(prepared);
    REQUIRE(prepared->entityIds() == std::vector<EntityId>{root, child, grandchild});
    REQUIRE(prepared->entityIdMap().at(root) == root);
    REQUIRE(prepared->entityIdMap().at(child) == child);
    REQUIRE(&prepared->animationAfter() == formal);
    REQUIRE(prepared->animationBefore().tracks.size() == 1);
    const auto* rootAddress = scene.find(root);
    const auto* childAddress = scene.find(child);
    const auto* siblingsAddress = scene.find(parent)->children.data();
    const auto siblingsCapacity = scene.find(parent)->children.capacity();
    const auto candidate = scene.animationPoseInputs(*prepared, false, error);
    REQUIRE(candidate);
    REQUIRE(inputIds(*candidate) == std::vector<EntityId>{parent, left, right});
    REQUIRE(evaluateAnimationPose(*candidate, prepared->animationBefore(), 25).pose);
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(scene.canRemovePreparedSubtree(*prepared));
        REQUIRE(scene.removePreparedSubtree(*prepared));
        REQUIRE_FALSE(scene.removePreparedSubtree(*prepared));
        REQUIRE(scene.find(root) == nullptr);
        REQUIRE(scene.find(child) == nullptr);
        REQUIRE(scene.find(grandchild) == nullptr);
        REQUIRE(scene.find(parent)->children == std::vector<EntityId>{left, right});
        REQUIRE(scene.collections()[0].members == std::set<EntityId>{left, right});
        REQUIRE(&scene.animation() == &prepared->animationBefore());
        const auto removedInputs = scene.animationPoseInputs(error);
        REQUIRE(removedInputs);
        requireInputsEqual(*candidate, *removedInputs);
        const auto restoration = scene.animationPoseInputs(*prepared, true, error);
        REQUIRE(restoration);
        requireInputsEqual(*originalInputs, *restoration);
        REQUIRE(scene.canInstallPreparedSubtree(*prepared));
        REQUIRE(scene.installPreparedSubtree(*prepared));
        REQUIRE_FALSE(scene.installPreparedSubtree(*prepared));
        REQUIRE(scene.find(root) == rootAddress);
        REQUIRE(scene.find(child) == childAddress);
        REQUIRE(scene.find(child)->editableMesh == mesh);
        REQUIRE(scene.editableMesh(mesh)->content == content);
        REQUIRE(scene.find(parent)->children == std::vector<EntityId>{left, root, right});
        REQUIRE(scene.find(parent)->children.data() == siblingsAddress);
        REQUIRE(scene.find(parent)->children.capacity() == siblingsCapacity);
        REQUIRE(scene.collections() == groups);
        REQUIRE(&scene.animation() == formal);
        REQUIRE(scene.animation() == animation);
    }
}

TEST_CASE("Prepared deletion rejects changed content without deleting any portion of the subtree",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto left = scene.createEntity("Left", parent);
    const auto root = scene.createEntity("Root", parent);
    const auto child = scene.createEntity("Mesh", root, PrimitiveKind::Cube);
    std::string error;
    const auto geometry =
        scene.prepareEditableGeometry(child, modeling::createEditableCube(), error);
    REQUIRE(geometry);
    REQUIRE(scene.installGeometry(*geometry));
    SceneAnimation animation;
    animation.tracks[{root, AnimationChannel::Position}] = constantTrack({1, 0, 0});
    installAnimation(scene, animation);
    auto prepared = scene.prepareRemoveSubtree(root, error);
    REQUIRE(prepared);
    SECTION("a node property changed") {
        REQUIRE(scene.renameEntity(child, "Changed"));
    }
    SECTION("mesh content changed") {
        auto mesh = geometry->content()->source;
        mesh.vertices[0].position.x += 0.125F;
        const auto changed = scene.prepareEditableGeometry(child, mesh, error);
        REQUIRE(changed);
        REQUIRE(scene.installGeometry(*changed));
    }
    SECTION("formal animation changed") {
        auto changed = animation;
        changed.settings.fps = 48;
        installAnimation(scene, changed);
    }
    SECTION("frozen sibling position changed") {
        REQUIRE(scene.setSiblingIndex(root, 0));
        REQUIRE(scene.find(parent)->children == std::vector<EntityId>{root, left});
    }
    SECTION("a replacement document has identical visible content") {
        REQUIRE(scene.replaceNodes(scene.nodes(), scene.editableMeshes(), scene.collections(),
                                   scene.animation()));
    }
    const auto* formal = &scene.animation();
    const auto* rootAddress = scene.find(root);
    const auto* childAddress = scene.find(child);
    const auto siblings = scene.find(parent)->children;
    REQUIRE_FALSE(scene.canRemovePreparedSubtree(*prepared));
    REQUIRE_FALSE(scene.animationPoseInputs(*prepared, false, error));
    REQUIRE_FALSE(scene.removePreparedSubtree(*prepared));
    REQUIRE(scene.find(root) == rootAddress);
    REQUIRE(scene.find(child) == childAddress);
    REQUIRE(scene.find(parent)->children == siblings);
    REQUIRE(scene.nodes().size() == 4);
    REQUIRE(&scene.animation() == formal);
}

TEST_CASE("Direct animation on a subtree blocks real reparenting but permits same-parent no change",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto root = scene.createEntity("Root", parent);
    const auto child = scene.createEntity("Child", root);
    const auto other = scene.createEntity("Other parent");
    SceneAnimation animation;
    SECTION("the root owns a direct track") {
        animation.tracks[{root, AnimationChannel::Position}] = constantTrack({1, 2, 3});
    }
    SECTION("only a descendant owns a direct track") {
        animation.tracks[{child, AnimationChannel::RotationEulerXYZDegrees}] =
            constantTrack({0, 720, 0});
    }
    installAnimation(scene, animation);
    const auto* formal = &scene.animation();
    std::string error;
    auto unchanged = scene.prepareParentChange(root, parent, error);
    REQUIRE(unchanged);
    REQUIRE_FALSE(unchanged->hasChanges());
    REQUIRE(error.empty());
    REQUIRE(scene.animationPoseInputs(*unchanged, true, error));
    REQUIRE(scene.installPreparedParentChange(*unchanged));
    REQUIRE(scene.restorePreparedParentChange(*unchanged));
    REQUIRE_FALSE(scene.prepareParentChange(root, other, error));
    REQUIRE_FALSE(scene.setParent(root, other));
    REQUIRE_FALSE(scene.prepareParentChange(root, root, error));
    REQUIRE_FALSE(scene.prepareParentChange(root, child, error));
    REQUIRE_FALSE(scene.prepareParentChange(root, 999, error));
    REQUIRE(scene.find(root)->parent == parent);
    REQUIRE(scene.find(parent)->children == std::vector<EntityId>{root});
    REQUIRE(scene.find(other)->children.empty());
    REQUIRE(&scene.animation() == formal);
}

TEST_CASE("Unbound subtree can leave an animated ancestor with keepLocal and exact sibling replay",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto oldParent = scene.createEntity("Animated ancestor");
    const auto left = scene.createEntity("Left", oldParent);
    const auto child = scene.createEntity("Moving", oldParent);
    const auto grandchild = scene.createEntity("Unbound descendant", child);
    const auto right = scene.createEntity("Right", oldParent);
    const auto newParent = scene.createEntity("New parent");
    const auto newSibling = scene.createEntity("Existing child", newParent);
    Transform local;
    local.position = {2, 0, 0};
    REQUIRE(scene.setTransform(child, local));
    Transform grandchildLocal;
    grandchildLocal.position = {0, 3, 0};
    REQUIRE(scene.setTransform(grandchild, grandchildLocal));
    Transform parentLocal;
    parentLocal.position = {20, 0, 0};
    REQUIRE(scene.setTransform(newParent, parentLocal));
    SceneAnimation animation;
    animation.tracks[{oldParent, AnimationChannel::Position}] = linearTrack({10, 0, 0}, {14, 0, 0});
    installAnimation(scene, animation);
    const auto* formal = &scene.animation();
    std::string error;
    const auto before = scene.animationPoseInputs(error);
    REQUIRE(before);
    auto prepared = scene.prepareParentChange(child, newParent, error);
    REQUIRE(prepared);
    REQUIRE(prepared->hasChanges());
    REQUIRE(prepared->entityId() == child);
    REQUIRE(prepared->beforeParent() == oldParent);
    REQUIRE(prepared->afterParent() == newParent);
    const auto candidate = scene.animationPoseInputs(*prepared, true, error);
    REQUIRE(candidate);
    REQUIRE(inputIds(*candidate) == std::vector<EntityId>{oldParent, left, right, newParent,
                                                          newSibling, child, grandchild});
    REQUIRE(inputFor(*candidate, child)->parent == newParent);
    REQUIRE(inputFor(*candidate, grandchild)->parent == child);
    const auto movedPose = evaluateAnimationPose(*candidate, scene.animation(), 25);
    REQUIRE(movedPose.pose);
    REQUIRE(glm::vec3(movedPose.pose->find(child)->world[3]) == glm::vec3(22, 0, 0));
    REQUIRE(glm::vec3(movedPose.pose->find(grandchild)->world[3]) == glm::vec3(22, 3, 0));
    REQUIRE(scene.find(child)->parent == oldParent);
    for (int cycle = 0; cycle < 3; ++cycle) {
        REQUIRE(scene.installPreparedParentChange(*prepared));
        REQUIRE_FALSE(scene.installPreparedParentChange(*prepared));
        REQUIRE(scene.find(oldParent)->children == std::vector<EntityId>{left, right});
        REQUIRE(scene.find(newParent)->children == std::vector<EntityId>{newSibling, child});
        REQUIRE(scene.find(child)->transform.position == local.position);
        REQUIRE(scene.find(child)->transform.rotation == local.rotation);
        REQUIRE(scene.find(child)->transform.scale == local.scale);
        const auto actual = scene.animationPoseInputs(error);
        REQUIRE(actual);
        requireInputsEqual(*candidate, *actual);
        const auto restoration = scene.animationPoseInputs(*prepared, false, error);
        REQUIRE(restoration);
        requireInputsEqual(*before, *restoration);
        REQUIRE(scene.restorePreparedParentChange(*prepared));
        REQUIRE_FALSE(scene.restorePreparedParentChange(*prepared));
        REQUIRE(scene.find(child)->parent == oldParent);
        REQUIRE(scene.find(oldParent)->children == std::vector<EntityId>{left, child, right});
        REQUIRE(scene.find(newParent)->children == std::vector<EntityId>{newSibling});
        REQUIRE(&scene.animation() == formal);
    }
}

TEST_CASE("Parent candidates cover root detach and attach without omitting descendants",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto child = scene.createEntity("Moving");
    const auto grandchild = scene.createEntity("Descendant", child);
    EntityId target = parent;
    SECTION("a root is attached") {}
    SECTION("a child becomes a root") {
        REQUIRE(scene.setParent(child, parent));
        target = 0;
    }
    std::string error;
    const auto before = scene.animationPoseInputs(error);
    REQUIRE(before);
    auto prepared = scene.prepareParentChange(child, target, error);
    REQUIRE(prepared);
    const auto candidate = scene.animationPoseInputs(*prepared, true, error);
    REQUIRE(candidate);
    REQUIRE(candidate->size() == 3);
    REQUIRE(inputFor(*candidate, child)->parent == target);
    REQUIRE(inputFor(*candidate, grandchild)->parent == child);
    REQUIRE(evaluateAnimationPose(*candidate, scene.animation(), 1).pose);
    REQUIRE(scene.installPreparedParentChange(*prepared));
    const auto actual = scene.animationPoseInputs(error);
    REQUIRE(actual);
    requireInputsEqual(*candidate, *actual);
    REQUIRE(scene.restorePreparedParentChange(*prepared));
    const auto restored = scene.animationPoseInputs(error);
    REQUIRE(restored);
    requireInputsEqual(*before, *restored);
}

TEST_CASE("Parent replay rejects stale local sibling hierarchy document and formal sources",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto child = scene.createEntity("Moving", parent);
    const auto grandchild = scene.createEntity("Descendant", child);
    const auto other = scene.createEntity("New parent");
    std::string error;
    auto prepared = scene.prepareParentChange(child, other, error);
    REQUIRE(prepared);
    SECTION("local transform changed") {
        Transform changed;
        changed.position = {1, 2, 3};
        REQUIRE(scene.setTransform(child, changed));
    }
    SECTION("old parent sibling list changed") {
        REQUIRE(scene.createEntity("Later sibling", parent) != 0);
    }
    SECTION("a descendant gained another child") {
        REQUIRE(scene.createEntity("Later descendant", grandchild) != 0);
    }
    SECTION("moving the proposed parent under a descendant would make the candidate cyclic") {
        REQUIRE(scene.setParent(other, grandchild));
    }
    SECTION("the new parent was deleted") {
        REQUIRE(scene.removeEntity(other));
    }
    SECTION("formal animation changed") {
        SceneAnimation changed;
        changed.settings.fps = 48;
        installAnimation(scene, changed);
    }
    SECTION("the document was replaced") {
        REQUIRE(scene.replaceNodes(scene.nodes()));
    }
    const auto* formal = &scene.animation();
    const auto expectedParent = scene.find(child)->parent;
    const auto expectedChildren = scene.find(child)->children;
    REQUIRE_FALSE(scene.canInstallPreparedParentChange(*prepared));
    REQUIRE_FALSE(scene.animationPoseInputs(*prepared, true, error));
    REQUIRE_FALSE(scene.installPreparedParentChange(*prepared));
    REQUIRE(scene.find(child)->parent == expectedParent);
    REQUIRE(scene.find(child)->children == expectedChildren);
    REQUIRE(&scene.animation() == formal);
    Scene otherScene;
    REQUIRE_FALSE(otherScene.installPreparedParentChange(*prepared));
}

TEST_CASE("Legacy snapshot duplicate delete restore only merges the captured subtree tracks",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto parent = scene.createEntity("Parent");
    const auto root = scene.createEntity("Root", parent);
    const auto child = scene.createEntity("Child", root);
    const auto tail = scene.createEntity("Tail", parent);
    REQUIRE(scene.replaceCollections({{7, "Group", true, {root, child}}}));
    SceneAnimation animation;
    animation.tracks[{root, AnimationChannel::RotationEulerXYZDegrees}] =
        constantTrack({720.000000001, 0, 0});
    animation.tracks[{child, AnimationChannel::Position}] = constantTrack({1, 2, 3});
    installAnimation(scene, animation);
    const auto snapshot = scene.snapshotSubtree(root);
    const auto copy = scene.duplicateSubtree(root);
    REQUIRE(copy != 0);
    const auto copyChild = scene.find(copy)->children[0];
    REQUIRE(scene.find(copy)->name == "Root Copy");
    REQUIRE(scene.animation().tracks.size() == 4);
    REQUIRE(scene.removeEntity(root));
    REQUIRE(scene.animation().tracks.size() == 2);
    auto later = scene.animation();
    later.settings.fps = 48;
    later.tracks.at({copy, AnimationChannel::RotationEulerXYZDegrees}).keys[0].value.x = 1080;
    later.tracks[{tail, AnimationChannel::Position}] = constantTrack({9, 0, 0});
    installAnimation(scene, later);
    REQUIRE(scene.restoreSubtree(snapshot));
    REQUIRE_FALSE(scene.restoreSubtree(snapshot));
    REQUIRE(scene.find(parent)->children == std::vector<EntityId>{root, tail, copy});
    REQUIRE(scene.find(root)->children == std::vector<EntityId>{child});
    REQUIRE(scene.animation().settings == later.settings);
    REQUIRE(scene.animation().tracks.size() == 5);
    REQUIRE(scene.animation().tracks.at({root, AnimationChannel::RotationEulerXYZDegrees}) ==
            animation.tracks.at({root, AnimationChannel::RotationEulerXYZDegrees}));
    REQUIRE(scene.animation().tracks.at({child, AnimationChannel::Position}) ==
            animation.tracks.at({child, AnimationChannel::Position}));
    REQUIRE(scene.animation().tracks.at({copy, AnimationChannel::RotationEulerXYZDegrees}) ==
            later.tracks.at({copy, AnimationChannel::RotationEulerXYZDegrees}));
    REQUIRE(scene.animation().tracks.at({copyChild, AnimationChannel::Position}) ==
            later.tracks.at({copyChild, AnimationChannel::Position}));
    REQUIRE(scene.collections()[0].members == std::set<EntityId>{root, child, copy, copyChild});
    std::string error;
    const auto inputs = scene.animationPoseInputs(error);
    REQUIRE(inputs);
    REQUIRE(evaluateAnimationPose(*inputs, scene.animation(), 13.375).pose);
}

TEST_CASE("Legacy subtree restoration reuses the original animation snapshot when contents match",
          "[scene][animation][scene-animation]") {
    Scene scene;
    const auto root = scene.createEntity("Root");
    SceneAnimation animation;
    animation.tracks[{root, AnimationChannel::Position}] = constantTrack({1, 2, 3});
    installAnimation(scene, animation);
    const auto* formal = &scene.animation();
    const auto snapshot = scene.snapshotSubtree(root);
    REQUIRE(scene.removeEntity(root));
    REQUIRE(scene.animation().tracks.empty());
    REQUIRE(scene.restoreSubtree(snapshot));
    REQUIRE(&scene.animation() == formal);
    REQUIRE(scene.animation() == animation);
}

TEST_CASE(
    "Static legacy duplication keeps its prior scale while bounded API preparation stays bounded",
    "[scene][animation][scene-animation]") {
    Scene scene;
    const auto root = scene.createEntity("Root");
    for (int index = 1; index < 2050; ++index)
        REQUIRE(scene.createEntity("Child", root) != 0);
    std::string error;
    REQUIRE_FALSE(scene.prepareDuplicateSubtree(root, error));
    REQUIRE(scene.nodes().size() == 2050);
    const auto copy = scene.duplicateSubtree(root);
    REQUIRE(copy != 0);
    REQUIRE(scene.find(copy)->children.size() == 2049);
    REQUIRE(scene.nodes().size() == 4100);
    REQUIRE(scene.animation().tracks.empty());
    REQUIRE(scene.removeEntity(copy));
    REQUIRE(scene.nodes().size() == 2050);
}

TEST_CASE("Scene numeric inputs iterate deep hierarchies and budget the final structural candidate",
          "[scene][animation][scene-animation]") {
    Scene scene;
    EntityId parent = 0;
    for (std::size_t index = 0; index < kAnimationMaximumPoseNodes; ++index) {
        parent = scene.createEntity("Deep node", parent);
        REQUIRE(parent != 0);
    }
    std::string error;
    const auto inputs = scene.animationPoseInputs(error);
    REQUIRE(inputs);
    REQUIRE(inputs->size() == 10000);
    REQUIRE(inputs->front().parent == 0);
    REQUIRE(inputs->back().entity == parent);
    REQUIRE(inputs->back().parent == parent - 1);
    const auto pose = evaluateAnimationPose(*inputs, scene.animation(), 13.375);
    REQUIRE(pose.pose);
    REQUIRE(pose.pose->find(parent)->world == glm::mat4(1));
    auto copiedLeaf = scene.prepareDuplicateSubtree(parent, error);
    REQUIRE(copiedLeaf);
    REQUIRE_FALSE(scene.animationPoseInputs(*copiedLeaf, true, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(scene.find(copiedLeaf->rootId()) == nullptr);
    const auto extra = scene.createEntity("One over");
    REQUIRE(extra != 0);
    REQUIRE_FALSE(scene.animationPoseInputs(error));
    auto removedExtra = scene.prepareRemoveSubtree(extra, error);
    REQUIRE(removedExtra);
    const auto backInBudget = scene.animationPoseInputs(*removedExtra, false, error);
    REQUIRE(backInBudget);
    requireInputsEqual(*inputs, *backInBudget);
    auto removedChain = scene.prepareRemoveSubtree(1, error);
    REQUIRE(removedChain);
    const auto survivor = scene.animationPoseInputs(*removedChain, false, error);
    REQUIRE(survivor);
    REQUIRE(inputIds(*survivor) == std::vector<EntityId>{extra});
}

TEST_CASE("Prepared creation retraction refuses to leave orphan animation bindings",
          "[scene][animation][scene-animation]") {
    Scene scene;
    std::string error;
    Scene::EntityCreateOptions options;
    options.name = "Created";
    SECTION("single entity") {
        auto created = scene.prepareEntity(options, error);
        REQUIRE(created);
        REQUIRE(scene.installPreparedEntity(*created));
        SceneAnimation animation;
        animation.tracks[{created->entityId(), AnimationChannel::Position}] =
            constantTrack({1, 2, 3});
        auto later = scene.prepareAnimation(animation, error);
        REQUIRE(later);
        REQUIRE(scene.installPreparedAnimation(*later));
        REQUIRE_FALSE(scene.removePreparedEntity(*created));
        REQUIRE(scene.find(created->entityId()));
        REQUIRE(scene.animation() == animation);
        REQUIRE(scene.restorePreparedAnimation(*later));
        REQUIRE(scene.removePreparedEntity(*created));
    }
    SECTION("entity batch") {
        auto created = scene.prepareEntityBatch({options, options}, error, 2, 1024 * 1024);
        REQUIRE(created);
        REQUIRE(scene.installPreparedEntityBatch(*created));
        SceneAnimation animation;
        animation.tracks[{created->entityIds()[1], AnimationChannel::Position}] =
            constantTrack({1, 2, 3});
        auto later = scene.prepareAnimation(animation, error);
        REQUIRE(later);
        REQUIRE(scene.installPreparedAnimation(*later));
        REQUIRE_FALSE(scene.removePreparedEntityBatch(*created));
        REQUIRE(scene.find(created->entityIds()[0]));
        REQUIRE(scene.find(created->entityIds()[1]));
        REQUIRE(scene.animation() == animation);
        REQUIRE(scene.restorePreparedAnimation(*later));
        REQUIRE(scene.removePreparedEntityBatch(*created));
    }
    REQUIRE(scene.roots().empty());
    REQUIRE(scene.animation().tracks.empty());
}

TEST_CASE(
    "New subtree numerical candidates inherit animated ancestors without adding direct tracks",
    "[scene][animation][scene-animation]") {
    Scene scene;
    const auto parent = scene.createEntity("Animated parent");
    SceneAnimation animation;
    animation.tracks[{parent, AnimationChannel::Position}] = constantTrack({10, 0, 0});
    installAnimation(scene, animation);
    const auto* formal = &scene.animation();
    Scene::SubtreeNodeOptions root;
    root.name = "New root";
    root.transform.position = {2, 0, 0};
    Scene::SubtreeNodeOptions child;
    child.name = "New child";
    child.parentIndex = 0;
    child.transform.position = {0, 3, 0};
    std::string error;
    auto prepared = scene.prepareNewSubtree({root, child}, parent, error);
    REQUIRE(prepared);
    REQUIRE(&prepared->animationBefore() == formal);
    REQUIRE(&prepared->animationAfter() == formal);
    REQUIRE(prepared->entityIds().size() == 2);
    const auto candidate = scene.animationPoseInputs(*prepared, true, error);
    REQUIRE(candidate);
    const auto pose = evaluateAnimationPose(*candidate, prepared->animationAfter(), 1);
    REQUIRE(pose.pose);
    REQUIRE(glm::vec3(pose.pose->find(prepared->entityIds()[1])->world[3]) == glm::vec3(12, 3, 0));
    REQUIRE(scene.installPreparedSubtree(*prepared));
    REQUIRE(scene.removePreparedSubtree(*prepared));
    REQUIRE(&scene.animation() == formal);
    REQUIRE(scene.find(parent)->children.empty());
}
