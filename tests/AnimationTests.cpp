/*
 * 模块名: AnimationTests
 * 功能概述: 验证原生动画的原值、插值、Euler 顺序、完整父链及数值失败原子性。
 * 对外接口: Catch2 动画 CPU 测试。
 * 依赖关系: 纯 Core、GLM、Catch2；不依赖 Qt 或 OpenGL。
 * 输入输出: 手算与独立矩阵参考夹具到确定性数值断言。
 * 异常与错误: 任一合同或预算不符时测试失败，不写用户模型。
 * 维护说明: 参考旋转使用独立轴矩阵，首次逆解重构不回调生产 Euler helper。
 */
#include "core/Animation.h"
#include "core/EvaluatedPose.h"
#include "core/Scene.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace mini3d::core;

namespace {
AnimationTrack linearTrack(const glm::dvec3& first, const glm::dvec3& last,
                           std::uint32_t firstFrame = 1, std::uint32_t lastFrame = 49) {
    return {{{firstFrame, first, AnimationInterpolation::Linear},
             {lastFrame, last, AnimationInterpolation::Linear}}};
}

glm::dmat4 referenceEulerMatrix(const glm::dvec3& degrees) {
    const auto x = glm::rotate(glm::dmat4(1), glm::radians(degrees.x), glm::dvec3(1, 0, 0));
    const auto y = glm::rotate(glm::dmat4(1), glm::radians(degrees.y), glm::dvec3(0, 1, 0));
    const auto z = glm::rotate(glm::dmat4(1), glm::radians(degrees.z), glm::dvec3(0, 0, 1));
    return z * y * x;
}

void requireMatrixNear(const glm::dmat4& actual, const glm::dmat4& expected,
                       double absolute = 1.0e-4, double relative = 1.0e-5) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            INFO("column=" << column << ", row=" << row);
            REQUIRE(std::abs(actual[column][row] - expected[column][row]) <=
                    absolute + relative * std::abs(expected[column][row]));
        }
    }
}

void requireQuaternionNear(const glm::dquat& actual, const glm::dquat& expected,
                           double tolerance = 2.0e-8) {
    for (int component = 0; component < 4; ++component) {
        REQUIRE(std::isfinite(actual[component]));
        REQUIRE(std::isfinite(expected[component]));
    }
    REQUIRE(std::isfinite(glm::length(actual)));
    REQUIRE(glm::length(actual) > 0.0);
    REQUIRE(std::isfinite(glm::length(expected)));
    REQUIRE(glm::length(expected) > 0.0);
    const auto a = glm::normalize(actual);
    const auto b = glm::normalize(expected);
    double same = 0.0;
    double opposite = 0.0;
    for (int component = 0; component < 4; ++component) {
        same = std::max(same, std::abs(a[component] - b[component]));
        opposite = std::max(opposite, std::abs(a[component] + b[component]));
    }
    REQUIRE(std::min(same, opposite) <= tolerance);
}

bool matrixFinite(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}
} // namespace

TEST_CASE("Animation settings preserve keys outside the playback range", "[animation]") {
    const std::array<EntityId, 1> entities{1};
    SceneAnimation animation;
    REQUIRE(animation.settings == AnimationSettings{});
    animation.tracks[{1, AnimationChannel::Position}] =
        linearTrack({0, 0, 0}, {10, 2, -4}, 1, 100000);
    const auto keys = animation.tracks;
    REQUIRE(validateSceneAnimation(animation, entities).isValid());
    animation.settings = {120, 50, 50};
    REQUIRE(validateSceneAnimation(animation, entities).isValid());
    REQUIRE(animation.tracks == keys);
    REQUIRE(sampleAnimationTrack(AnimationChannel::Position, animation.tracks.begin()->second, 1));
    REQUIRE(sampleAnimationTrack(AnimationChannel::Position, animation.tracks.begin()->second,
                                 100000));
    for (const AnimationSettings settings :
         {AnimationSettings{0, 1, 250}, AnimationSettings{121, 1, 250},
          AnimationSettings{24, 0, 250}, AnimationSettings{24, 1, 100001},
          AnimationSettings{24, 251, 250}}) {
        animation.settings = settings;
        REQUIRE(validateSceneAnimation(animation, entities).error == AnimationError::InvalidSettings);
    }
}

TEST_CASE("Animation rejects invalid raw values before any float rounding", "[animation]") {
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    const double roundedBack = std::nextafter(maximumFloat, std::numeric_limits<double>::infinity());
    REQUIRE(roundedBack > maximumFloat);
    REQUIRE_FALSE(validateAnimationValue(AnimationChannel::Position, {roundedBack, 0, 0}).isValid());
    REQUIRE(validateAnimationValue(AnimationChannel::Position, {maximumFloat, -maximumFloat, 0})
                .isValid());
    const double roundedScale = 0.00099999999;
    REQUIRE(static_cast<float>(roundedScale) == 0.001F);
    REQUIRE(validateAnimationValue(AnimationChannel::Scale, {roundedScale, 1, 1}).error ==
            AnimationError::InvalidScale);
    REQUIRE(validateAnimationValue(AnimationChannel::Scale, {-0.001, 0.001, 1}).isValid());
    REQUIRE_FALSE(validateAnimationValue(AnimationChannel::Scale, {0, 1, 1}).isValid());
    for (const auto channel : {AnimationChannel::Position, AnimationChannel::Scale,
                               AnimationChannel::RotationEulerXYZDegrees}) {
        REQUIRE_FALSE(validateAnimationValue(channel, {std::numeric_limits<double>::infinity(), 1, 1})
                          .isValid());
        REQUIRE_FALSE(validateAnimationValue(channel, {1, std::numeric_limits<double>::quiet_NaN(), 1})
                          .isValid());
    }
    REQUIRE(validateAnimationValue(AnimationChannel::RotationEulerXYZDegrees,
                                   {3600000000.0, -3600000000.0, 720.000000001})
                .isValid());
    REQUIRE_FALSE(validateAnimationValue(AnimationChannel::RotationEulerXYZDegrees,
                                         {3600000000.1, 0, 0})
                      .isValid());
    REQUIRE_FALSE(validateAnimationValue(static_cast<AnimationChannel>(99), {1, 1, 1}).isValid());
}

TEST_CASE("Animation validates complete ordering binding and interpolation", "[animation]") {
    const std::array<EntityId, 1> entities{7};
    SceneAnimation animation;
    animation.tracks[{8, AnimationChannel::Position}] = {};
    REQUIRE(validateSceneAnimation(animation, entities).error == AnimationError::MissingEntity);
    animation.tracks.clear();
    animation.tracks[{7, AnimationChannel::Position}] = {};
    REQUIRE(validateSceneAnimation(animation, entities).error == AnimationError::EmptyTrack);
    auto& track = animation.tracks.begin()->second;
    track = linearTrack({0, 0, 0}, {1, 1, 1});
    REQUIRE(validateSceneAnimation(animation, entities).isValid());
    track.keys.back().frame = 1;
    REQUIRE(validateSceneAnimation(animation, entities).error == AnimationError::KeysNotOrdered);
    track.keys.back().frame = 0;
    REQUIRE(validateSceneAnimation(animation, entities).error == AnimationError::InvalidFrame);
    track.keys.back().frame = 100001;
    REQUIRE(validateSceneAnimation(animation, entities).error == AnimationError::InvalidFrame);
    track.keys.back().frame = 49;
    track.keys.front().interpolation = static_cast<AnimationInterpolation>(99);
    REQUIRE(validateSceneAnimation(animation, entities).error == AnimationError::InvalidInterpolation);
    REQUIRE_FALSE(sampleAnimationTrack(AnimationChannel::Position, track, 25));
    REQUIRE(validateSceneAnimation(SceneAnimation{}, std::array<EntityId, 2>{7, 7}).error ==
            AnimationError::DuplicateEntity);
    REQUIRE(validateSceneAnimation(SceneAnimation{}, std::array<EntityId, 1>{0}).error ==
            AnimationError::InvalidEntity);
}

TEST_CASE("Animation scale validation checks the final adjacency after edits", "[animation]") {
    AnimationTrack track{{{1, {1, 1, 1}, AnimationInterpolation::Linear},
                           {5, {2, 2, 2}, AnimationInterpolation::Constant},
                           {9, {-1, -1, -1}, AnimationInterpolation::Linear}}};
    REQUIRE(validateAnimationTrack(AnimationChannel::Scale, track).isValid());
    auto deleted = track;
    deleted.keys.erase(deleted.keys.begin() + 1);
    REQUIRE(validateAnimationTrack(AnimationChannel::Scale, deleted).error ==
            AnimationError::ScaleCrossesZero);
    deleted.keys.front().interpolation = AnimationInterpolation::Constant;
    REQUIRE(validateAnimationTrack(AnimationChannel::Scale, deleted).isValid());
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Scale, deleted, 8.999) == glm::dvec3(1));
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Scale, deleted, 9) == glm::dvec3(-1));
    auto moved = track;
    moved.keys[1].frame = 10;
    std::swap(moved.keys[1], moved.keys[2]);
    REQUIRE(validateAnimationTrack(AnimationChannel::Scale, moved).error ==
            AnimationError::ScaleCrossesZero);
    auto replaced = track;
    replaced.keys[1].value.x = -2;
    REQUIRE(validateAnimationTrack(AnimationChannel::Scale, replaced).error ==
            AnimationError::ScaleCrossesZero);
    auto changedInterpolation = track;
    changedInterpolation.keys[1].interpolation = AnimationInterpolation::Linear;
    REQUIRE(validateAnimationTrack(AnimationChannel::Scale, changedInterpolation).error ==
            AnimationError::ScaleCrossesZero);
}

TEST_CASE("Animation enforces track and key budgets before value validation", "[animation]") {
    SceneAnimation manyTracks;
    std::vector<EntityId> entities;
    for (EntityId entity = 1; entity <= kAnimationMaximumTracks; ++entity) {
        entities.push_back(entity);
        manyTracks.tracks[{entity, AnimationChannel::Position}].keys.push_back({1, {0, 0, 0}});
    }
    REQUIRE(validateSceneAnimation(manyTracks, entities).isValid());
    manyTracks.tracks[{3001, AnimationChannel::Position}] = {};
    REQUIRE(validateSceneAnimation(manyTracks, entities).error == AnimationError::TrackLimitExceeded);
    AnimationTrack longTrack;
    for (std::uint32_t frame = 1; frame <= kAnimationMaximumTrackKeys; ++frame) {
        longTrack.keys.push_back({frame, {0, 0, 0}});
    }
    REQUIRE(validateAnimationTrack(AnimationChannel::Position, longTrack).isValid());
    auto tooLong = longTrack;
    tooLong.keys.push_back({10001, {std::numeric_limits<double>::quiet_NaN(), 0, 0}});
    REQUIRE(validateAnimationTrack(AnimationChannel::Position, tooLong).error ==
            AnimationError::TrackKeyLimitExceeded);
    SceneAnimation manyKeys;
    entities.clear();
    for (EntityId entity = 1; entity <= 10; ++entity) {
        entities.push_back(entity);
        manyKeys.tracks[{entity, AnimationChannel::Position}] = longTrack;
    }
    REQUIRE(validateSceneAnimation(manyKeys, entities).isValid());
    manyKeys.tracks[{11, AnimationChannel::Position}].keys.push_back({1, {0, 0, 0}});
    REQUIRE(validateSceneAnimation(manyKeys, entities).error == AnimationError::TotalKeyLimitExceeded);
    REQUIRE(validateAnimationPreflightBudget(10000, 3).isValid());
    REQUIRE(validateAnimationPreflightBudget(10000, 4).isValid());
    REQUIRE(validateAnimationPreflightBudget(10000, 5).error == AnimationError::PreflightLimitExceeded);
    REQUIRE(validateAnimationPreflightBudget(10001, 1).error == AnimationError::PoseNodeLimitExceeded);
    REQUIRE(validateAnimationPreflightBudget(1, std::numeric_limits<std::size_t>::max()).error ==
            AnimationError::PreflightLimitExceeded);
}

TEST_CASE("Animation samples full XYZ linear constant exact keys and endpoint holds", "[animation]") {
    const auto track = linearTrack({0, 2, -4}, {10, 6, 8}, 5, 45);
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Position, track, 25) == glm::dvec3(5, 4, 2));
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Position, track, 1) == glm::dvec3(0, 2, -4));
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Position, track, 100000) == glm::dvec3(10, 6, 8));
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Position, track, 5) == track.keys.front().value);
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Position, track, 45) == track.keys.back().value);
    AnimationTrack mixed{{{5, {2, 3, 4}, AnimationInterpolation::Constant},
                           {10, {10, 20, 30}, AnimationInterpolation::Linear},
                           {20, {20, 40, 60}, AnimationInterpolation::Linear}}};
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Position, mixed, 9.999) == glm::dvec3(2, 3, 4));
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Position, mixed, 10) == glm::dvec3(10, 20, 30));
    REQUIRE(*sampleAnimationTrack(AnimationChannel::Position, mixed, 10.5) == glm::dvec3(10.5, 21, 31.5));
    for (const FrameTime frame : {0.0, 100001.0, std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::quiet_NaN()}) {
        REQUIRE_FALSE(sampleAnimationTrack(AnimationChannel::Position, track, frame));
    }
}

TEST_CASE("Animation lerp preserves the strict minimum scale at equal endpoints", "[animation]") {
    const std::array<AnimationPoseInput, 1> inputs{{{1, 0, {}}}};
    for (const double scale : {0.001, -0.001}) {
        for (const auto [lastFrame, frame] :
             {std::pair{2U, 1.0006}, std::pair{10001U, 7.0}, std::pair{10001U, 7.375}}) {
            const auto track = linearTrack(glm::dvec3(scale), glm::dvec3(scale), 1, lastFrame);
            const auto value = sampleAnimationTrack(AnimationChannel::Scale, track, frame);
            REQUIRE(value);
            REQUIRE(*value == glm::dvec3(scale));
            REQUIRE(validateAnimationValue(AnimationChannel::Scale, *value).isValid());
            SceneAnimation animation;
            animation.tracks[{1, AnimationChannel::Scale}] = track;
            const auto result = evaluateAnimationPose(inputs, animation, frame);
            REQUIRE(result.pose);
            REQUIRE(result.pose->find(1)->local.scale == glm::vec3(static_cast<float>(scale)));
        }
    }
}

TEST_CASE("Animation keeps continuous turns and does not choose a shortest rotation arc", "[animation]") {
    const auto twoTurns = linearTrack({0, 0, 0}, {0, 720, 0});
    for (const auto [frame, angle] :
         {std::pair{7.0, 90.0}, std::pair{13.0, 180.0}, std::pair{25.0, 360.0},
          std::pair{37.0, 540.0}, std::pair{49.0, 720.0}}) {
        REQUIRE(sampleAnimationTrack(AnimationChannel::RotationEulerXYZDegrees, twoTurns, frame)->y ==
                angle);
    }
    for (const auto [frame, z] : {std::pair{7.0, -1.0}, std::pair{19.0, 1.0},
                                  std::pair{31.0, -1.0}, std::pair{43.0, 1.0}}) {
        const auto value = sampleAnimationTrack(AnimationChannel::RotationEulerXYZDegrees, twoTurns,
                                                frame);
        const auto rotation = quaternionFromEulerXYZDegrees(*value);
        REQUIRE(rotation);
        REQUIRE(glm::distance(*rotation * glm::dvec3(1, 0, 0), glm::dvec3(0, 0, z)) < 1.0e-12);
    }
    REQUIRE(sampleAnimationTrack(AnimationChannel::RotationEulerXYZDegrees,
                                 linearTrack({0, 170, 0}, {0, -170, 0}), 25)
                ->y == 0);
    REQUIRE(sampleAnimationTrack(AnimationChannel::RotationEulerXYZDegrees,
                                 linearTrack({0, 170, 0}, {0, 190, 0}), 25)
                ->y == 180);
    AnimationTrack original{{{1, {720.000000001, 3599999999.9, 3600000000.0}}}};
    REQUIRE(*sampleAnimationTrack(AnimationChannel::RotationEulerXYZDegrees, original, 1) ==
            original.keys.front().value);
    auto different = original;
    different.keys.front().value.x = 720;
    REQUIRE_FALSE(original == different);
    REQUIRE(quaternionFromEulerXYZDegrees(original.keys.front().value));
    REQUIRE(original.keys.front().value.x == 720.000000001);
    REQUIRE(original.keys.front().value.y == 3599999999.9);
}

TEST_CASE("Animation Euler quaternion agrees with an independent Rz Ry Rx matrix", "[animation]") {
    for (const glm::dvec3 degrees : {glm::dvec3(90, 90, 0), glm::dvec3(25, -40, 70),
                                     glm::dvec3(-90, 45, -170)}) {
        const auto rotation = quaternionFromEulerXYZDegrees(degrees);
        REQUIRE(rotation);
        requireMatrixNear(glm::mat4_cast(*rotation), referenceEulerMatrix(degrees), 1.0e-12, 1.0e-12);
        REQUIRE(glm::length(*rotation) == Catch::Approx(1.0).margin(1.0e-14));
    }
    const auto ordered = quaternionFromEulerXYZDegrees({90, 90, 0});
    REQUIRE(glm::distance(*ordered * glm::dvec3(0, 1, 0), glm::dvec3(1, 0, 0)) < 1.0e-12);
    const auto large = quaternionFromEulerXYZDegrees({3600000000.0, 3599999999.9, 720.000000001});
    REQUIRE(large);
    const double retainedSmall = 3599999999.9 - 3600000000.0;
    requireMatrixNear(glm::mat4_cast(*large), referenceEulerMatrix({0, retainedSmall, 0.000000001}),
                       1.0e-10, 1.0e-10);
    REQUIRE_FALSE(quaternionFromEulerXYZDegrees({3600000000.1, 0, 0}));
}

TEST_CASE("Animation first rotation inverse handles equivalent signs and gimbal limits", "[animation]") {
    for (const glm::dvec3 degrees :
         {glm::dvec3(35, 25, -75), glm::dvec3(-180, 0, -180), glm::dvec3(30, 90, 70),
          glm::dvec3(30, -90, 70), glm::dvec3(30, 90 - 1.0e-7, 70),
          glm::dvec3(30, -90 + 1.0e-7, 70), glm::dvec3(30, 90 - 1.0e-6, 70),
          glm::dvec3(30, -90 + 1.0e-6, 70), glm::dvec3(30, 90 - 1.0e-4, 70)}) {
        const auto reference = glm::quat_cast(glm::dmat3(referenceEulerMatrix(degrees)));
        for (const double sign : {1.0, -1.0}) {
            const auto canonical = canonicalEulerXYZDegrees(reference * (3.0 * sign));
            REQUIRE(canonical);
            REQUIRE(canonical->x > -180);
            REQUIRE(canonical->x <= 180);
            REQUIRE(canonical->y >= -90);
            REQUIRE(canonical->y <= 90);
            REQUIRE(canonical->z > -180);
            REQUIRE(canonical->z <= 180);
            const auto independent = glm::quat_cast(glm::dmat3(referenceEulerMatrix(*canonical)));
            requireQuaternionNear(independent, reference);
            // 现有基础 Transform 使用 float；门禁仍在 double 域验证其实际姿态。
            const glm::dquat actualBase{glm::quat(reference * sign)};
            const auto fromBase = canonicalEulerXYZDegrees(actualBase);
            if (std::abs(degrees.y) < 89.0) {
                REQUIRE(fromBase);
            }
            if (fromBase) {
                requireQuaternionNear(
                    glm::quat_cast(glm::dmat3(referenceEulerMatrix(*fromBase))), actualBase);
            }
        }
    }
    for (const double y : {90.0, -90.0}) {
        const auto canonical = canonicalEulerXYZDegrees(
            glm::quat_cast(glm::dmat3(referenceEulerMatrix({30, y, 70}))));
        REQUIRE(canonical);
        REQUIRE(canonical->x == 0);
        REQUIRE(canonical->y == Catch::Approx(y).margin(1.0e-12));
        const glm::dquat floatBase{glm::angleAxis(glm::radians(static_cast<float>(y)),
                                                  glm::vec3(0, 1, 0))};
        const auto floatCanonical = canonicalEulerXYZDegrees(floatBase);
        REQUIRE(floatCanonical);
        REQUIRE(floatCanonical->y == Catch::Approx(y).margin(1.0e-12));
        requireQuaternionNear(
            glm::quat_cast(glm::dmat3(referenceEulerMatrix(*floatCanonical))), floatBase);
    }
    REQUIRE_FALSE(canonicalEulerXYZDegrees(glm::dquat(0, 0, 0, 0)));
    REQUIRE_FALSE(canonicalEulerXYZDegrees(
        glm::dquat(std::numeric_limits<double>::quiet_NaN(), 0, 0, 0)));
    REQUIRE_FALSE(canonicalEulerXYZDegrees(
        glm::dquat(std::numeric_limits<double>::infinity(), 0, 0, 0)));
}

TEST_CASE("Animation pose includes unbound descendants and never modifies the base Scene", "[animation][pose]") {
    Scene scene;
    const auto parent = scene.createEntity("Animated parent");
    const auto child = scene.createEntity("Unbound child", parent);
    const auto grandchild = scene.createEntity("Unbound grandchild", child);
    Transform childBase;
    childBase.position = {2, 0, 0};
    childBase.rotation = glm::angleAxis(glm::radians(35.0F), glm::vec3(0, 0, 1));
    Transform grandchildBase;
    grandchildBase.position = {0, 3, 0};
    REQUIRE(scene.setTransform(child, childBase));
    REQUIRE(scene.setTransform(grandchild, grandchildBase));
    childBase = scene.find(child)->transform;
    grandchildBase = scene.find(grandchild)->transform;
    const auto originalWorld = scene.worldMatrix(grandchild);
    const std::array<AnimationPoseInput, 3> inputs{{{parent, 0, scene.find(parent)->transform},
                                                  {child, parent, scene.find(child)->transform},
                                                  {grandchild, child, scene.find(grandchild)->transform}}};
    SceneAnimation animation;
    animation.tracks[{parent, AnimationChannel::Position}] = linearTrack({0, 0, 0}, {10, 0, 0});
    const auto originalAnimation = animation;
    const auto result = evaluateAnimationPose(inputs, animation, 25.0);
    REQUIRE(result.pose);
    REQUIRE(result.validation.isValid());
    REQUIRE(result.pose->frame == 25.0);
    REQUIRE(result.pose->nodes.size() == 3);
    REQUIRE(result.pose->indices.at(grandchild) == 2);
    REQUIRE(result.pose->find(999) == nullptr);
    REQUIRE(glm::vec3(result.pose->find(child)->world[3]) == glm::vec3(7, 0, 0));
    const auto expectedGrandchild = glm::translate(glm::dmat4(1), glm::dvec3(5, 0, 0)) *
                                    glm::dmat4(originalWorld);
    requireMatrixNear(glm::dmat4(result.pose->find(grandchild)->world), expectedGrandchild);
    for (const auto& node : result.pose->nodes) {
        REQUIRE(node.rotationSource == AnimationRotationSource::BaseQuaternion);
        REQUIRE_FALSE(node.rotationEulerXYZDegrees);
    }
    REQUIRE(result.pose->find(child)->local.rotation == childBase.rotation);
    REQUIRE(scene.find(parent)->transform.position == glm::vec3(0));
    REQUIRE(scene.find(child)->transform.position == childBase.position);
    REQUIRE(scene.find(child)->transform.rotation == childBase.rotation);
    REQUIRE(scene.find(grandchild)->transform.position == grandchildBase.position);
    REQUIRE(scene.worldMatrix(grandchild) == originalWorld);
    REQUIRE(scene.find(parent)->children == std::vector<EntityId>{child});
    REQUIRE(animation == originalAnimation);
    REQUIRE(inputs[1].base.rotation == childBase.rotation);
}

TEST_CASE("Animation pose composes negative nonuniform worlds and a separate rigid chain", "[animation][pose]") {
    Transform parent;
    parent.position = {3, -2, 1};
    parent.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3(0, 0, 1));
    parent.scale = {-2, 3, 0.5F};
    Transform child;
    child.position = {1, 2, 3};
    child.scale = {0.5F, -4, 2};
    Transform grandchild;
    grandchild.position = {-1, 0, 2};
    grandchild.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3(1, 0, 0));
    grandchild.scale = {1, 2, -1};
    const std::array<AnimationPoseInput, 3> inputs{{{1, 0, parent}, {2, 1, child}, {3, 2, grandchild}}};
    SceneAnimation animation;
    animation.tracks[{1, AnimationChannel::Position}] = linearTrack({3, -2, 1}, {7, -2, 1});
    animation.tracks[{2, AnimationChannel::RotationEulerXYZDegrees}] =
        linearTrack({0, 0, 0}, {0, 180, 0});
    const auto result = evaluateAnimationPose(inputs, animation, 25);
    REQUIRE(result.pose);
    const auto parentMatrix = glm::translate(glm::dmat4(1), glm::dvec3(5, -2, 1)) *
                              referenceEulerMatrix({0, 0, 90}) *
                              glm::scale(glm::dmat4(1), glm::dvec3(-2, 3, 0.5));
    const auto childMatrix = glm::translate(glm::dmat4(1), glm::dvec3(1, 2, 3)) *
                             referenceEulerMatrix({0, 90, 0}) *
                             glm::scale(glm::dmat4(1), glm::dvec3(0.5, -4, 2));
    const auto grandchildMatrix = glm::translate(glm::dmat4(1), glm::dvec3(-1, 0, 2)) *
                                  referenceEulerMatrix({90, 0, 0}) *
                                  glm::scale(glm::dmat4(1), glm::dvec3(1, 2, -1));
    const auto expectedWorld = parentMatrix * childMatrix * grandchildMatrix;
    const auto* node = result.pose->find(3);
    requireMatrixNear(glm::dmat4(node->world), expectedWorld);
    REQUIRE(glm::distance(glm::vec3(node->world[3]), glm::vec3(-1, -12, 2.75F)) < 1.0e-4F);
    requireMatrixNear(glm::dmat4(node->world * node->worldInverse), glm::dmat4(1));
    const auto expectedNormal = glm::transpose(glm::inverse(glm::dmat3(expectedWorld)));
    for (int column = 0; column < 3; ++column) {
        REQUIRE(glm::distance(glm::dvec3(node->worldNormal[column]), expectedNormal[column]) < 1.0e-4);
    }
    const auto expectedRigid = glm::quat_cast(glm::dmat3(referenceEulerMatrix({0, 0, 90}) *
                                                        referenceEulerMatrix({0, 90, 0}) *
                                                        referenceEulerMatrix({90, 0, 0})));
    requireQuaternionNear(glm::dquat(node->rigidWorldRotation), expectedRigid, 2.0e-6);
    REQUIRE(glm::distance(node->rigidWorldRotation * glm::vec3(0, 0, -1), glm::vec3(-1, 0, 0)) <
            1.0e-5F);
    REQUIRE(glm::length(node->rigidWorldRotation) == Catch::Approx(1.0F).margin(2.0e-6F));
    REQUIRE(result.pose->find(2)->rotationSource == AnimationRotationSource::AnimationTrack);
    REQUIRE(*result.pose->find(2)->rotationEulerXYZDegrees == glm::dvec3(0, 90, 0));
    REQUIRE_FALSE(result.pose->find(3)->rotationEulerXYZDegrees);
}

TEST_CASE("Animation pose rejects unusable float inverse and normal without partial results", "[animation][pose]") {
    SECTION("A finite local world can have a nonfinite actual float inverse") {
        Transform base;
        base.scale = glm::vec3(1.0e13F);
        REQUIRE(base.isValid());
        REQUIRE(matrixFinite(base.localMatrix()));
        REQUIRE_FALSE(matrixFinite(glm::inverse(base.localMatrix())));
        const std::array<AnimationPoseInput, 1> inputs{{{1, 0, base}}};
        const auto result = evaluateAnimationPose(inputs, SceneAnimation{}, 1);
        REQUIRE_FALSE(result.pose);
        REQUIRE(result.validation.error == AnimationError::InvalidWorldInverse);
        REQUIRE(result.validation.entity == 1);
    }
    SECTION("A finite normal column near 1e21 overflows ordinary float normalization") {
        glm::mat4 example(1);
        example[0][0] = 1.0e-21F;
        REQUIRE(matrixFinite(example));
        REQUIRE(matrixFinite(glm::inverse(example)));
        const auto normal = glm::transpose(glm::inverse(glm::mat3(example)));
        REQUIRE(std::isfinite(normal[0][0]));
        REQUIRE_FALSE(std::isfinite(glm::length(normal[0])));
        std::vector<AnimationPoseInput> inputs;
        for (EntityId entity = 1; entity <= 7; ++entity) {
            Transform base;
            base.scale = {0.001F, 1, 1};
            inputs.push_back({entity, entity - 1, base});
        }
        const auto result = evaluateAnimationPose(inputs, SceneAnimation{}, 1);
        REQUIRE_FALSE(result.pose);
        REQUIRE(result.validation.error == AnimationError::InvalidWorldNormal);
        REQUIRE(result.validation.entity == 7);
    }
    SECTION("Individually valid translations overflow only on the composed parent chain") {
        Transform base;
        base.position.x = 3.0e38F;
        REQUIRE(base.isValid());
        const std::array<AnimationPoseInput, 2> inputs{{{1, 0, base}, {2, 1, base}}};
        const auto result = evaluateAnimationPose(inputs, SceneAnimation{}, 1);
        REQUIRE_FALSE(result.pose);
        REQUIRE(result.validation.error == AnimationError::InvalidWorldTransform);
        REQUIRE(result.validation.entity == 2);
        REQUIRE(inputs[0].base.position.x == 3.0e38F);
    }
}

TEST_CASE("Animation pose rejects invalid hierarchy base time and dangling tracks", "[animation][pose]") {
    REQUIRE(evaluateAnimationPose(std::array<AnimationPoseInput, 1>{{{0, 0, {}}}}, SceneAnimation{}, 1)
                .validation.error == AnimationError::InvalidEntity);
    REQUIRE(evaluateAnimationPose(std::array<AnimationPoseInput, 2>{{{1, 0, {}}, {1, 0, {}}}},
                                  SceneAnimation{}, 1)
                .validation.error == AnimationError::DuplicateEntity);
    REQUIRE(evaluateAnimationPose(std::array<AnimationPoseInput, 2>{{{2, 1, {}}, {1, 0, {}}}},
                                  SceneAnimation{}, 1)
                .validation.error == AnimationError::InvalidParentOrder);
    Transform invalid;
    invalid.scale.x = 0;
    REQUIRE(evaluateAnimationPose(std::array<AnimationPoseInput, 1>{{{1, 0, invalid}}}, SceneAnimation{}, 1)
                .validation.error == AnimationError::InvalidBaseTransform);
    const std::array<AnimationPoseInput, 1> inputs{{{1, 0, {}}}};
    REQUIRE(evaluateAnimationPose(inputs, SceneAnimation{}, std::numeric_limits<double>::quiet_NaN())
                .validation.error == AnimationError::InvalidFrame);
    SceneAnimation dangling;
    dangling.tracks[{2, AnimationChannel::Position}] = linearTrack({0, 0, 0}, {1, 1, 1});
    const auto result = evaluateAnimationPose(inputs, dangling, 25);
    REQUIRE_FALSE(result.pose);
    REQUIRE(result.validation.error == AnimationError::MissingEntity);
}

TEST_CASE("Animation pose iterates a 10000 deep chain and rejects node 10001 first", "[animation][pose]") {
    std::vector<AnimationPoseInput> inputs;
    inputs.reserve(kAnimationMaximumPoseNodes + 1);
    for (EntityId entity = 1; entity <= kAnimationMaximumPoseNodes; ++entity) {
        inputs.push_back({entity, entity - 1, {}});
    }
    const auto result = evaluateAnimationPose(inputs, SceneAnimation{}, 13.375);
    REQUIRE(result.pose);
    REQUIRE(result.pose->frame == 13.375);
    REQUIRE(result.pose->nodes.size() == 10000);
    requireMatrixNear(glm::dmat4(result.pose->find(10000)->world), glm::dmat4(1), 0, 0);
    REQUIRE(result.pose->find(10000)->rigidWorldRotation == glm::quat(1, 0, 0, 0));
    // 第10001个输入故意非法，预算必须先于遍历、基础校验和完整 Pose 缓冲分配。
    inputs.push_back({0, 10000, {}});
    const auto tooMany = evaluateAnimationPose(inputs, SceneAnimation{}, 13.375);
    REQUIRE_FALSE(tooMany.pose);
    REQUIRE(tooMany.validation.error == AnimationError::PoseNodeLimitExceeded);
}

TEST_CASE("Animation sampling is independent of forward reverse and direct visit order", "[animation][pose]") {
    const std::array<AnimationPoseInput, 2> inputs{{{1, 0, {}}, {2, 1, {}}}};
    SceneAnimation animation;
    animation.tracks[{1, AnimationChannel::Position}] = linearTrack({0, 0, 0}, {10, 0, 0});
    animation.tracks[{2, AnimationChannel::RotationEulerXYZDegrees}] =
        linearTrack({0, 0, 0}, {0, 720, 0});
    const auto original = animation;
    const auto direct = evaluateAnimationPose(inputs, animation, 13.375);
    REQUIRE(direct.pose);
    for (const auto frames : {std::array{1.0, 25.0, 49.0}, std::array{49.0, 25.0, 1.0}}) {
        for (const double frame : frames) {
            const auto visited = evaluateAnimationPose(inputs, animation, frame);
            REQUIRE(visited.pose);
            REQUIRE(visited.pose->find(2)->rotationEulerXYZDegrees->y == 15.0 * (frame - 1.0));
        }
        const auto revisited = evaluateAnimationPose(inputs, animation, 13.375);
        REQUIRE(revisited.pose);
        for (const EntityId entity : {EntityId{1}, EntityId{2}}) {
            REQUIRE(revisited.pose->find(entity)->world == direct.pose->find(entity)->world);
            REQUIRE(revisited.pose->find(entity)->local.rotation ==
                    direct.pose->find(entity)->local.rotation);
        }
    }
    auto oneTurn = animation;
    oneTurn.tracks[{2, AnimationChannel::RotationEulerXYZDegrees}].keys.back().value.y = 360;
    auto zeroTurns = animation;
    zeroTurns.tracks[{2, AnimationChannel::RotationEulerXYZDegrees}].keys.back().value.y = 0;
    REQUIRE_FALSE(animation == oneTurn);
    REQUIRE_FALSE(oneTurn == zeroTurns);
    REQUIRE_FALSE(animation == zeroTurns);
    REQUIRE(animation == original);
}
