/*
 * 模块名: AnimationPreparedReplayTests
 * 功能概述: 验证真实变换历史数值候选与名称交换无写真源副作用。
 * 对外接口: Catch2 [animation-prepared-replay]。
 * 依赖关系: Core Scene；输入输出: prepared before/after与原字符串缓冲。
 * 异常与错误: 跨Scene候选拒绝，空名称拒绝保持双方缓冲。
 * 维护说明: 指针互换证明所测名称swap没有另分配，不宣称全局分配失败注入。
 */
#include "core/Scene.h"
#include <catch2/catch_test_macros.hpp>

using namespace mini3d::core;
TEST_CASE("Prepared transform history supplies exact numerical overlays without installation",
          "[animation][animation-prepared-replay]") {
    Scene scene;
    const auto parent = scene.createEntity("parent");
    const auto child = scene.createEntity("child", parent, PrimitiveKind::Cube);
    Transform after;
    after.position = {2, 3, 4};
    std::string error;
    auto prepared = scene.prepareTransformBatch({{parent, after}}, error, 1024, 64 * 1024 * 1024);
    REQUIRE(prepared);
    const auto forward = scene.animationPoseInputs(*prepared, true, error);
    const auto backward = scene.animationPoseInputs(*prepared, false, error);
    REQUIRE(forward);
    REQUIRE(backward);
    REQUIRE(forward->size() == 2);
    REQUIRE(forward->at(0).entity == parent);
    REQUIRE(forward->at(0).base.position == after.position);
    REQUIRE(forward->at(1).entity == child);
    REQUIRE(forward->at(1).parent == parent);
    REQUIRE(backward->at(0).base.position == glm::vec3(0));
    REQUIRE(scene.find(parent)->transform.position == glm::vec3(0));
    REQUIRE(scene.installPreparedTransformBatch(*prepared));
    REQUIRE(scene.animationPoseInputs(*prepared, false, error)->at(0).base.position == glm::vec3(0));
    REQUIRE(scene.find(parent)->transform.position == after.position);
    Scene other;
    REQUIRE_FALSE(other.animationPoseInputs(*prepared, true, error));
}
TEST_CASE("Prepared names exchange exact allocated buffers and can replay without copying",
          "[animation][animation-prepared-replay]") {
    Scene scene;
    const auto id = scene.createEntity(std::string(80, 'a'));
    std::string prepared(90, 'b');
    const auto* originalBuffer = scene.find(id)->name.data();
    const auto* candidateBuffer = prepared.data();
    REQUIRE(scene.exchangeEntityName(id, prepared));
    REQUIRE(scene.find(id)->name == std::string(90, 'b'));
    REQUIRE(scene.find(id)->name.data() == candidateBuffer);
    REQUIRE(prepared.data() == originalBuffer);
    REQUIRE(scene.exchangeEntityName(id, prepared));
    REQUIRE(scene.find(id)->name == std::string(80, 'a'));
    REQUIRE(scene.find(id)->name.data() == originalBuffer);
    std::string empty;
    REQUIRE_FALSE(scene.exchangeEntityName(id, empty));
    REQUIRE(scene.find(id)->name == std::string(80, 'a'));
    REQUIRE(empty.empty());
}
TEST_CASE("Verified collection snapshots exchange storage without rolling back identity watermarks",
          "[animation][animation-prepared-replay]") {
    Scene scene;
    const auto member = scene.createEntity("member");
    REQUIRE(scene.replaceCollections({{2, "original", true, {member}}}));
    std::vector<SceneCollection> prepared{{90, "candidate", false, {member}}};
    Scene candidate;
    REQUIRE(candidate.createEntity("member") == member);
    REQUIRE(candidate.replaceCollections(prepared));
    const auto* before = scene.collections().data();
    const auto* after = prepared.data();
    REQUIRE(scene.exchangeCollectionSnapshot(prepared));
    REQUIRE(scene.collections().data() == after);
    REQUIRE(prepared.data() == before);
    REQUIRE(scene.collections().front().id == 90);
    REQUIRE_FALSE(scene.collections().front().visible);
    REQUIRE(scene.exchangeCollectionSnapshot(prepared));
    REQUIRE(scene.collections().data() == before);
    REQUIRE(scene.collections().front().id == 2);
    REQUIRE(scene.createCollection("next") == 91);
    std::vector<SceneCollection> invalid{{91, "bad reference", true, {member + 100}}};
    REQUIRE_FALSE(scene.exchangeCollectionSnapshot(invalid));
    REQUIRE(scene.collections().front().id == 2);
}
