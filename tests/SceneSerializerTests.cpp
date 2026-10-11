/*
 * 模块名: SceneSerializerTests
 * 功能概述: 验证纯 CPU 场景 JSON 往返、ID/层级验证与版本错误。
 * 对外接口: Catch2 用例
 * 依赖关系: SceneSerializer、nlohmann/json、Catch2
 * 输入输出: 文档 JSON 到等价状态与原子拒绝断言。
 * 异常与错误: 非法数据通过断言要求拒绝，不发布部分状态。
 * 维护说明: 不访问磁盘、不创建 Qt 应用。
 */
#include "core/SceneSerializer.h"
#include "NativeSceneFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
using namespace mini3d::core;
TEST_CASE("Cursor auxiliary editor state round trips and legacy documents default safely",
          "[cursor-serializer]") {
    SceneDocumentData data;
    REQUIRE(data.sourceVersion == 5);
    data.cursor = {{1.25F, -2.5F, 3.75F}, false};
    auto json = nlohmann::json::parse(SceneSerializer::encode(data));
    REQUIRE(json["version"] == 5);
    REQUIRE(json["animation"] ==
            (nlohmann::json{{"fps", 24},
                            {"startFrame", 1},
                            {"endFrame", 250},
                            {"tracks", nlohmann::json::array()},
                            {"properties", {{"rationalFps", nullptr}, {"loopPolicy", "inclusiveFrameStep"},
                                {"actions", nlohmann::json::array()}, {"bindings", nlohmann::json::array()}}}}));
    REQUIRE(json["editorState"]["cursor3D"]["visible"] == false);
    SceneDocumentData loaded;
    std::string error;
    REQUIRE(SceneSerializer::decode(json.dump(), loaded, error));
    REQUIRE(loaded.sourceVersion == 5);
    REQUIRE(loaded.cursor == data.cursor);
    REQUIRE(loaded.nodes.empty());
    for (int version : {1, 2, 3}) {
        auto old = mini3d::test::legacySceneJson(json, version);
        old.erase("editorState");
        REQUIRE(SceneSerializer::decode(old.dump(), loaded, error));
        REQUIRE(loaded.cursor == Cursor3D{});
    }
    for (const auto& broken : {nlohmann::json(nullptr), nlohmann::json::array(),
                               nlohmann::json{{"position", {1, 2}}, {"visible", true}},
                               nlohmann::json{{"position", {1, 2, 3}}, {"visible", 1}},
                               nlohmann::json{{"position", {1, 2, "nan"}}, {"visible", true}},
                               nlohmann::json{{"position", {1, 2, 1.0e100}}, {"visible", true}}}) {
        auto invalid = json;
        invalid["editorState"]["cursor3D"] = broken;
        loaded.cursor = data.cursor;
        REQUIRE_FALSE(SceneSerializer::decode(invalid.dump(), loaded, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(loaded.cursor == data.cursor);
    }
    json["editorState"] = 3;
    REQUIRE_FALSE(SceneSerializer::decode(json.dump(), loaded, error));
    data.cursor.position.x = std::numeric_limits<float>::infinity();
    REQUIRE_THROWS(SceneSerializer::encode(data));
}
TEST_CASE("Scene JSON preserves hierarchy IDs transforms and appearance", "[serializer]") {
    Scene scene;
    const auto parent = scene.createEntity("中文父节点");
    const auto child = scene.createEntity("Child", parent, PrimitiveKind::Cube);
    Transform transform;
    transform.rotation = glm::quat(glm::radians(glm::vec3(20, 30, 40)));
    transform.scale = {-2, 3, 0.5F};
    REQUIRE(scene.setTransform(child, transform));
    REQUIRE(scene.setVisible(parent, false));
    SceneDocumentData data(scene);
    SceneDocumentData loaded;
    std::string error;
    const auto encoded = SceneSerializer::encode(data);
    REQUIRE(SceneSerializer::decode(encoded, loaded, error));
    REQUIRE(loaded.nodes.size() == 2);
    REQUIRE(loaded.nodes[0].name == "中文父节点");
    REQUIRE_FALSE(loaded.nodes[0].visible);
    REQUIRE(loaded.nodes[1].parent == parent);
    REQUIRE(loaded.nodes[1].transform.scale == transform.scale);
    REQUIRE(glm::length(loaded.nodes[1].transform.rotation - transform.rotation) < 1.0e-6F);
    auto unknown = nlohmann::json::parse(encoded);
    unknown["futureOptional"] = true;
    REQUIRE(SceneSerializer::decode(unknown.dump(), loaded, error));
}
TEST_CASE("Scene versions 3 to 5 preserve devices and read version 1 without adding entities",
          "[serializer][camera-light]") {
    Scene scene;
    const auto camera = scene.createEntity("相机");
    const auto light = scene.createEntity("太阳", camera);
    REQUIRE(scene.setCamera(camera, {70, 0.5F, 300}));
    REQUIRE(scene.setLight(light, {{0.2F, 0.4F, 1}, 3}));
    SceneDocumentData data(scene);
    const auto json = nlohmann::json::parse(SceneSerializer::encode(data));
    REQUIRE(json["version"] == 5);
    SceneDocumentData loaded;
    std::string error;
    for (int version : {3, 4, 5}) {
        auto document = version == 5 ? json : mini3d::test::legacySceneJson(json, version);
        REQUIRE(SceneSerializer::decode(document.dump(), loaded, error));
        REQUIRE(loaded.sourceVersion == version);
        REQUIRE(loaded.nodes[0].camera == data.nodes[0].camera);
        REQUIRE(loaded.nodes[1].light == data.nodes[1].light);
        REQUIRE(loaded.nodes[1].parent == camera);
    }
    auto legacy = mini3d::test::legacySceneJson(json, 1);
    legacy["entities"][0].erase("camera");
    legacy["entities"][1].erase("light");
    REQUIRE(SceneSerializer::decode(legacy.dump(), loaded, error));
    REQUIRE(loaded.nodes.size() == 2);
    REQUIRE_FALSE(loaded.nodes[0].camera);
    REQUIRE_FALSE(loaded.nodes[1].light);
    REQUIRE(loaded.lighting == data.lighting);
    for (int failure = 0; failure < 8; ++failure) {
        auto invalid = json;
        switch (failure) {
            case 0:
                invalid["entities"][0]["camera"]["nearPlane"] = 0;
                break;
            case 1:
                invalid["entities"][0]["camera"]["farPlane"] = 0.5;
                break;
            case 2:
                invalid["entities"][0]["camera"]["fieldOfView"] = 180;
                break;
            case 3:
                invalid["entities"][0]["camera"].erase("fieldOfView");
                break;
            case 4:
                invalid["entities"][1]["light"]["type"] = "point";
                break;
            case 5:
                invalid["entities"][1]["light"]["color"] = {2, 0, 0};
                break;
            case 6:
                invalid["entities"][0]["primitive"] = "Cube";
                break;
            case 7:
                invalid["entities"][0]["light"] = json["entities"][1]["light"];
                break;
        }
        REQUIRE_FALSE(SceneSerializer::decode(invalid.dump(), loaded, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE_FALSE(loaded.nodes[0].camera);
    }
}

TEST_CASE("Invalid scene JSON never replaces the output", "[serializer]") {
    Scene scene;
    scene.createEntity("Keep", 0, PrimitiveKind::Cube);
    SceneDocumentData data(scene);
    const auto valid = nlohmann::json::parse(SceneSerializer::encode(data));
    for (int failure = 0; failure < 10; ++failure) {
        auto invalid = valid;
        switch (failure) {
            case 0:
                invalid["version"] = 99;
                break;
            case 1:
                invalid.erase("format");
                break;
            case 2:
                invalid["entities"][0]["parent"] = "1";
                break;
            case 3:
                invalid["entities"].push_back(invalid["entities"][0]);
                break;
            case 4:
                invalid["entities"][0]["transform"]["scale"] = {1, 0, 1};
                break;
            case 5:
                invalid["entities"][0]["parent"] = "999";
                break;
            case 6:
                invalid["entities"][0]["id"] = -1;
                break;
            case 7:
                invalid["entities"][0]["meshAsset"] = "77";
                break;
            case 8:
                invalid["editorCamera"]["position"] = invalid["editorCamera"]["target"];
                break;
            case 9:
                invalid["lighting"]["direction"] = {0, 0, 0};
                break;
        }
        auto output = data;
        std::string error;
        REQUIRE_FALSE(SceneSerializer::decode(invalid.dump(), output, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(output.nodes.front().name == "Keep");
    }
    std::string error;
    REQUIRE_FALSE(SceneSerializer::decode("{broken", data, error));
}

namespace {
SceneDocumentData j1PersistenceDocument() {
    constexpr std::uint64_t wide = (std::uint64_t{1} << 54) + 37;
    SceneNode object;
    object.id = wide;
    object.name = "精确作者";
    object.primitive = PrimitiveKind::Cube;
    object.transform = {{1.25F, -2.5F, 3.75F}, {2.0F, -0.25F, 0.5F, -0.75F}, {-2, 3, 0.5F}};
    SceneNode sun;
    sun.id = wide + 1;
    sun.name = "物理Sun";
    sun.sun = SunComponent{1.0000000000000002, {0.12500000000000003, 0.375, 0.875}};
    Scene scene;
    REQUIRE(scene.replaceNodes({object, sun}));
    REQUIRE(scene.installTransformSnapshot(object.id, object.transform));
    MaterialDefinition material;
    material.id = MaterialId{wide};
    material.definitionVersion = wide + 9;
    material.parameters = {{0.12500000000000003, 0.375, 0.875}, 0.20000000000000004, 0.125, 1};
    SceneMaterialData materials;
    materials.assets.emplace(StableAssetId{wide},
        SceneAssetDefinition{StableAssetId{wide}, SceneAssetKind::Material, material});
    std::string error;
    auto preparedMaterial = scene.prepareMaterialData(materials, error);
    REQUIRE(preparedMaterial);
    REQUIRE(scene.installPreparedMaterialData(*preparedMaterial));
    REQUIRE(scene.setMaterialSlots(object.id, {{MaterialSlotId{wide}, MaterialId{wide}}}));
    SceneAnimation animation;
    animation.properties.rationalFps = RationalFps{30000, 1001};
    animation.properties.actions = {
        {ActionId{wide}, {{ActionSlotId{wide}, PropertyOwnerKind::Entity,
            {{CurveId{wide}, PropertyId::RotationEulerXYZDegrees, PropertyComponent::Y,
                {{KeyId{wide}, 1.0000000000000002, 720.0000000000001, PropertyInterpolation::Constant},
                 {KeyId{wide + 1}, 12.5, 3599999999.9, PropertyInterpolation::Linear}}}}}}},
        {ActionId{wide + 1}, {{ActionSlotId{wide + 1}, PropertyOwnerKind::Material,
            {{CurveId{wide + 1}, PropertyId::PbrRoughness, PropertyComponent::Scalar,
                {{KeyId{wide + 2}, 1, 0.20000000000000004, PropertyInterpolation::Linear},
                 {KeyId{wide + 3}, 24.75, 0.8000000000000002, PropertyInterpolation::Constant}}}}}}},
        {ActionId{wide + 2}, {{ActionSlotId{wide + 2}, PropertyOwnerKind::Entity,
            {{CurveId{wide + 2}, PropertyId::SunIrradiance, PropertyComponent::Scalar,
                {{KeyId{wide + 4}, 1, 1.0000000000000002, PropertyInterpolation::Linear},
                 {KeyId{wide + 5}, 24.75, 2.0000000000000004, PropertyInterpolation::Constant}}}}}}}};
    animation.properties.bindings = {
        {scene.documentUuid(), EntityOwner{object.id}, ActionId{wide}, ActionSlotId{wide}},
        {scene.documentUuid(), MaterialOwner{material.id}, ActionId{wide + 1}, ActionSlotId{wide + 1}},
        {scene.documentUuid(), EntityOwner{sun.id}, ActionId{wide + 2}, ActionSlotId{wide + 2}}};
    animation.tracks[{sun.id, AnimationChannel::Position}] = {{{1, {0.12500000000000003, 2, 3}}}};
    auto preparedAnimation = scene.prepareAnimation(animation, error);
    REQUIRE(preparedAnimation);
    REQUIRE(scene.installPreparedAnimation(*preparedAnimation));
    return SceneDocumentData(scene);
}
}
TEST_CASE("V5 preserves the complete author domain and exact source TRS and double keys",
          "[j1-persistence][serializer]") {
    const auto data = j1PersistenceDocument();
    const auto text = SceneSerializer::encode(data);
    const auto json = nlohmann::json::parse(text);
    REQUIRE(json["entities"][0]["id"] == std::to_string(data.nodes[0].id));
    REQUIRE(json["authorAssets"][0]["material"]["definitionVersion"].is_string());
    REQUIRE(json["animation"]["tracks"][0]["entityId"].is_string());
    REQUIRE(json["allocationState"]["nextEntityId"].is_string());
    SceneDocumentData loaded;
    std::string error;
    REQUIRE(SceneSerializer::decode(text, loaded, error));
    REQUIRE(loaded.documentUuid == data.documentUuid);
    REQUIRE(loaded.idHighWatermarks == data.idHighWatermarks);
    REQUIRE(loaded.allocationState == data.allocationState);
    REQUIRE(loaded.authorAssets == data.authorAssets);
    REQUIRE(loaded.animation == data.animation);
    REQUIRE(loaded.nodes[0].transform.position == data.nodes[0].transform.position);
    REQUIRE(loaded.nodes[0].transform.rotation == data.nodes[0].transform.rotation);
    REQUIRE(loaded.nodes[0].transform.scale == data.nodes[0].transform.scale);
    REQUIRE(loaded.nodes[0].materialSlots == data.nodes[0].materialSlots);
    REQUIRE(loaded.nodes[1].sun == data.nodes[1].sun);
    REQUIRE(SceneSerializer::encode(loaded) == text);
    Scene restored;
    REQUIRE(restored.replaceDocumentSnapshot(loaded, error));
    REQUIRE(SceneSerializer::encode(SceneDocumentData(restored)) == text);
}
TEST_CASE("V5 rejects malformed author input without replacing any output field",
          "[j1-persistence][serializer]") {
    const auto data = j1PersistenceDocument();
    const auto good = nlohmann::json::parse(SceneSerializer::encode(data));
    for (int failure = 0; failure < 19; ++failure) {
        auto bad = good;
        auto& properties = bad["animation"]["properties"];
        auto& curve = properties["actions"][0]["actionSlots"][0]["curves"][0];
        switch (failure) {
            case 0: bad["documentUuid"] = "00000000-0000-0000-0000-000000000000"; break;
            case 1: bad["documentUuid"] = "INVALID"; break;
            case 2: bad["entities"][0]["id"] = data.nodes[0].id; break;
            case 3: bad["entities"][0]["id"] = "01"; break;
            case 4: bad["idHighWatermarks"]["key"] = "18446744073709551616"; break;
            case 5: bad["idHighWatermarks"]["key"] = "0"; break;
            case 6: bad["allocationState"]["nextEntityId"] = bad["entities"][1]["id"]; break;
            case 7: bad["allocationState"]["nextMeshId"] = "0"; break;
            case 8: bad["authorAssets"].push_back(bad["authorAssets"][0]); break;
            case 9: bad["authorAssets"][0]["material"]["id"] = "99"; break;
            case 10: bad["authorAssets"][0]["material"]["parameters"]["roughness"] = 1.0000000000000002; break;
            case 11: bad["authorAssets"][0]["material"]["textures"] = {{{"role", "baseColor"}, {"texture", "99"}}}; break;
            case 12: bad["authorAssets"][0]["kind"] = "texture"; break;
            case 13: properties["rationalFps"] = {{"numerator", 60000}, {"denominator", 2002}}; break;
            case 14: curve["keys"][0]["time"] = 0.9999999999999999; break;
            case 15: curve["keys"][1]["id"] = curve["keys"][0]["id"]; break;
            case 16: properties["bindings"][0]["documentUuid"] = documentUuidString(createDocumentUuid()); break;
            case 17: bad["entities"][0]["materialSlots"][0]["material"] = "99"; break;
            case 18: properties["loopPolicy"] = "unknown"; break;
        }
        auto output = data;
        std::string error;
        CAPTURE(failure);
        REQUIRE_FALSE(SceneSerializer::decode(bad.dump(), output, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(SceneSerializer::encode(output) == good.dump(2) + "\n");
    }
}
TEST_CASE("V5 retains both loop policies with null rational fps and no actions",
          "[j1-persistence][serializer]") {
    for (const auto loop : {AnimationLoopPolicy::LegacyHalfOpen, AnimationLoopPolicy::InclusiveFrameStep}) {
        SceneDocumentData data;
        data.animation.properties.loopPolicy = loop;
        const auto text = SceneSerializer::encode(data);
        SceneDocumentData loaded;
        std::string error;
        REQUIRE(SceneSerializer::decode(text, loaded, error));
        REQUIRE_FALSE(loaded.animation.properties.rationalFps);
        REQUIRE(loaded.animation.properties.actions.empty());
        REQUIRE(loaded.animation.properties.loopPolicy == loop);
        REQUIRE(SceneSerializer::encode(loaded) == text);
    }
}
