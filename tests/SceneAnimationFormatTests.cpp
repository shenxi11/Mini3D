/*
 * 模块名: SceneAnimationFormatTests
 * 功能概述: 验证格式4连续角/宽ID、严格输入和失败不发布。
 * 对外接口: Catch2 [animation][animation-format]。
 * 依赖关系: SceneSerializer、NativeScenePreflight、nlohmann/json、Catch2。
 * 输入输出: 完整场景文档到精确动画往返及原子拒绝。
 * 异常与错误: 旧版携带动画拒绝，不通过排序或空轨规范化吞掉损坏输入。
 * 维护说明: 纯CPU；真实文件/GUI接入另有独立门禁。
 */
#include "core/NativeScenePreflight.h"
#include "core/SceneSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace mini3d::core;
namespace {
using Json = nlohmann::json;
constexpr EntityId wideEntity = (EntityId{1} << 54) + 29;
SceneDocumentData animatedDocument() {
    SceneNode node;
    node.id = wideEntity;
    node.name = "旋翼";
    node.primitive = PrimitiveKind::Cube;
    SceneDocumentData data;
    data.nodes.push_back(node);
    data.animation.tracks[{wideEntity, AnimationChannel::RotationEulerXYZDegrees}] = {
        {{1, {0, 720.000000001, 0}, AnimationInterpolation::Linear},
         {49, {0, 3599999999.9, 0}, AnimationInterpolation::Linear},
         {50, {0, 3600000000.0, 0}, AnimationInterpolation::Constant}}};
    data.animation.tracks[{wideEntity, AnimationChannel::Scale}] = {
        {{1, {-0.001, 1, 1}, AnimationInterpolation::Linear},
         {50, {-0.001, 2, 3}, AnimationInterpolation::Constant}}};
    return data;
}
} // namespace
TEST_CASE("Format four preserves exact continuous double channels and wide entity IDs",
          "[animation][animation-format]") {
    const auto data = animatedDocument();
    const auto text = SceneSerializer::encode(data);
    const auto json = Json::parse(text);
    REQUIRE(json["version"] == 4);
    REQUIRE(json["animation"]["tracks"][0]["entityId"].get<EntityId>() == wideEntity);
    SceneDocumentData loaded;
    std::string error;
    REQUIRE(SceneSerializer::decode(text, loaded, error));
    REQUIRE(loaded.sourceVersion == 4);
    REQUIRE(loaded.animation == data.animation);
    REQUIRE(SceneSerializer::encode(loaded) == text);
}
TEST_CASE("Malformed animation never replaces a previously valid full document",
          "[animation][animation-format]") {
    const auto data = animatedDocument();
    const auto good = Json::parse(SceneSerializer::encode(data));
    for (int failure = 0; failure < 13; ++failure) {
        auto bad = good;
        switch (failure) {
            case 0:
                bad.erase("animation");
                break;
            case 1:
                bad["animation"]["fps"] = 24.0;
                break;
            case 2:
                bad["animation"]["endFrame"] = 0;
                break;
            case 3:
                bad["animation"]["tracks"][0]["entityId"] = wideEntity + 1;
                break;
            case 4:
                bad["animation"]["tracks"][0]["channel"] = "quaternion";
                break;
            case 5:
                bad["animation"]["tracks"].push_back(bad["animation"]["tracks"][0]);
                break;
            case 6:
                bad["animation"]["tracks"][0]["keys"][1]["frame"] = 1;
                break;
            case 7:
                bad["animation"]["tracks"][0]["keys"][1]["frame"] = 0;
                break;
            case 8:
                bad["animation"]["tracks"][0]["keys"][0]["value"] = {0, 3600000000.1, 0};
                break;
            case 9:
                bad["animation"]["tracks"][1]["keys"][1]["value"] = {0.001, 2, 3};
                break;
            case 10:
                bad["animation"]["tracks"][1]["keys"][0]["value"] = {0.000999999999, 1, 1};
                break;
            case 11:
                bad["animation"]["tracks"][0]["keys"][0]["interpolation"] = "ease";
                break;
            case 12:
                bad["animation"]["tracks"][0]["extra"] = true;
                break;
        }
        auto output = data;
        std::string error;
        CAPTURE(failure);
        REQUIRE_FALSE(SceneSerializer::decode(bad.dump(), output, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(output.animation == data.animation);
        REQUIRE(SceneSerializer::encode(output) == SceneSerializer::encode(data));
    }
}
TEST_CASE("Empty tracks normalize only after binding and duplicate checks",
          "[animation][animation-format]") {
    auto json = Json::parse(SceneSerializer::encode(animatedDocument()));
    json["animation"]["tracks"][0]["keys"] = Json::array();
    SceneDocumentData loaded;
    std::string error;
    REQUIRE(SceneSerializer::decode(json.dump(), loaded, error));
    REQUIRE(loaded.animation.tracks.size() == 1);
    json["animation"]["tracks"].push_back(json["animation"]["tracks"][0]);
    REQUIRE_FALSE(SceneSerializer::decode(json.dump(), loaded, error));
    json["animation"]["tracks"].erase(json["animation"]["tracks"].end() - 1);
    json["animation"]["tracks"][0]["entityId"] = wideEntity + 1;
    REQUIRE_FALSE(SceneSerializer::decode(json.dump(), loaded, error));
}
TEST_CASE("Legacy formats remain static and explicitly reject animation fields",
          "[animation][animation-format]") {
    auto json = Json::parse(SceneSerializer::encode(SceneDocumentData{}));
    for (int version : {1, 2, 3}) {
        json["version"] = version;
        SceneDocumentData loaded;
        std::string error;
        REQUIRE_FALSE(SceneSerializer::decode(json.dump(), loaded, error));
        REQUIRE(error.find("VERSION_FIELD_MISMATCH") != std::string::npos);
        auto legacy = json;
        legacy.erase("animation");
        REQUIRE(SceneSerializer::decode(legacy.dump(), loaded, error));
        REQUIRE(loaded.sourceVersion == version);
        REQUIRE(loaded.animation == SceneAnimation{});
    }
}
TEST_CASE("Format four writer has the same exact byte boundary as its reader",
          "[animation][animation-format]") {
    SceneDocumentData data;
    SceneNode node;
    node.id = 1;
    node.name = "x";
    data.nodes.push_back(node);
    const auto baseBytes = SceneSerializer::encode(data).size();
    data.nodes.front().name.append(kNativeSceneMaximumBytes - baseBytes, 'x');
    const auto text = SceneSerializer::encode(data);
    REQUIRE(text.size() == kNativeSceneMaximumBytes);
    NativeSceneProbe probe;
    std::string error;
    REQUIRE(validateNativeSceneText(text, probe, error));
    data.nodes.front().name += 'x';
    REQUIRE_THROWS(SceneSerializer::encode(data));
}
