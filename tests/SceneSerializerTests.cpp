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

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
using namespace mini3d::core;
TEST_CASE("Scene JSON preserves hierarchy IDs transforms and appearance", "[serializer]") {
    Scene scene;
    const auto parent = scene.createEntity("中文父节点");
    const auto child = scene.createEntity("Child", parent, PrimitiveKind::Cube);
    Transform transform;
    transform.rotation = glm::quat(glm::radians(glm::vec3(20, 30, 40)));
    transform.scale = {-2, 3, 0.5F};
    REQUIRE(scene.setTransform(child, transform));
    REQUIRE(scene.setVisible(parent, false));
    SceneDocumentData data;
    data.nodes = scene.nodes();
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
TEST_CASE("Scene version 2 preserves devices and reads version 1 without adding entities",
          "[serializer][camera-light]") {
    Scene scene;
    const auto camera = scene.createEntity("相机");
    const auto light = scene.createEntity("太阳", camera);
    REQUIRE(scene.setCamera(camera, {70, 0.5F, 300}));
    REQUIRE(scene.setLight(light, {{0.2F, 0.4F, 1}, 3}));
    SceneDocumentData data;
    data.nodes = scene.nodes();
    const auto json = nlohmann::json::parse(SceneSerializer::encode(data));
    REQUIRE(json["version"] == 2);
    SceneDocumentData loaded;
    std::string error;
    REQUIRE(SceneSerializer::decode(json.dump(), loaded, error));
    REQUIRE(loaded.nodes[0].camera == data.nodes[0].camera);
    REQUIRE(loaded.nodes[1].light == data.nodes[1].light);
    REQUIRE(loaded.nodes[1].parent == camera);
    auto legacy = json;
    legacy["version"] = 1;
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
    SceneDocumentData data;
    data.nodes = scene.nodes();
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
                invalid["entities"][0]["parent"] = 1;
                break;
            case 3:
                invalid["entities"].push_back(invalid["entities"][0]);
                break;
            case 4:
                invalid["entities"][0]["transform"]["scale"] = {1, 0, 1};
                break;
            case 5:
                invalid["entities"][0]["parent"] = 999;
                break;
            case 6:
                invalid["entities"][0]["id"] = -1;
                break;
            case 7:
                invalid["entities"][0]["meshAsset"] = 77;
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
