/*
 * 模块名: FileCompatibilityTests
 * 功能概述: 验证格式 3 完整字段、宽 ID 和 Y-up 兼容性，坏文件不替换输出。
 * 对外接口: Catch2 [file-compatibility]。
 * 依赖关系: SceneSerializer、EditableMesh、nlohmann/json、Catch2，无 Qt/GL。
 * 输入输出: 版本化场景 JSON 到完整往返与原子拒绝断言。
 * 异常与错误: 非法字段、绑定、拓扑和 Mirror 必须明确拒绝。
 * 维护说明: 面材质仅支持默认 0，不扩展材质资源或正交视图持久化。
 */
#include "core/SceneSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace mini3d::core;
namespace {
using Json = nlohmann::json;
constexpr std::uint64_t wideId = (std::uint64_t{1} << 54) + 37;

SceneDocumentData completeDocument() {
    auto mesh = modeling::createEditableCube();
    for (auto& vertex : mesh.vertices) {
        vertex.id += wideId;
    }
    for (auto& face : mesh.faces) {
        face.id += wideId;
        for (auto& corner : face.corners) {
            corner.id += wideId;
            corner.vertex += wideId;
            corner.uv = {0.125F, -0.75F};
            corner.color = {0.125F, 0.375F, 0.875F};
        }
    }
    mesh.faces.front().corners.front().normal.reset();
    modeling::MirrorOptions mirror{modeling::MirrorAxis::Z, false, false, false, 0.025};

    SceneNode parent;
    parent.id = wideId + 100;
    parent.name = "中文层级";
    parent.transform = {{2, -3, 4}, {0.5F, -0.5F, 0.5F, -0.5F}, {-2, 3, 0.25F}};
    SceneNode editable;
    editable.id = wideId + 101;
    editable.parent = parent.id;
    editable.name = "宽编号网格";
    editable.editableMesh = wideId + 200;
    editable.surface = {{0.2F, 0.4F, 0.8F}, false, true};
    editable.visible = false;
    SceneNode imported;
    imported.id = wideId + 102;
    imported.parent = parent.id;
    imported.name = "导入子网格";
    imported.meshRenderer = MeshRendererComponent{wideId + 300, kInvalidAsset};
    SceneNode camera;
    camera.id = wideId + 103;
    camera.parent = parent.id;
    camera.name = "场景相机";
    camera.camera = CameraComponent{68, 0.25F, 420};
    SceneNode light;
    light.id = wideId + 104;
    light.parent = parent.id;
    light.name = "方向灯";
    light.light = LightComponent{{0.25F, 0.5F, 0.75F}, 2.5F};

    SceneDocumentData data;
    data.editableMeshes.push_back({editable.editableMesh, std::move(mesh), mirror});
    Scene validation;
    REQUIRE(
        validation.replaceNodes({parent, editable, imported, camera, light}, data.editableMeshes));
    data.nodes = validation.nodes();
    data.assets.push_back({wideId + 300, "../资源/multiple.gltf", 7});
    data.camera = {{8, 6, 10}, {1, 2, -3}, 2.5F, 120};
    data.lighting = {{0.25F, 0.8F, -0.6F}, {0.2F, 0.5F, 0.8F}, 2.25F, 0.45F};
    data.cursor = {{1.25F, -2.5F, 3.75F}, false};
    return data;
}
} // namespace

TEST_CASE("Format 3 preserves every persisted field including wide identities",
          "[file-compatibility]") {
    const auto data = completeDocument();
    const auto encoded = Json::parse(SceneSerializer::encode(data));
    REQUIRE(encoded["version"] == 3);
    REQUIRE(encoded["editorState"]["upAxis"] == "Y");
    REQUIRE(encoded["entities"][0]["transform"]["rotation"] ==
            Json::array({0.5F, -0.5F, 0.5F, -0.5F}));
    const auto& mesh = encoded["editableMeshes"][0];
    REQUIRE(mesh["id"].get<std::uint64_t>() == wideId + 200);
    REQUIRE(mesh["vertices"][0]["id"].get<std::uint64_t>() ==
            data.editableMeshes[0].source.vertices[0].id);
    REQUIRE(mesh["faces"][0]["id"].get<std::uint64_t>() ==
            data.editableMeshes[0].source.faces[0].id);
    REQUIRE(mesh["faces"][0]["corners"][0]["id"].get<std::uint64_t>() ==
            data.editableMeshes[0].source.faces[0].corners[0].id);
    REQUIRE(mesh["faces"][0]["material"] == 0);
    REQUIRE_FALSE(mesh["faces"][0]["corners"][0].contains("normal"));
    REQUIRE(mesh["faces"][0]["corners"][1].contains("normal"));

    SceneDocumentData loaded;
    std::string error;
    REQUIRE(SceneSerializer::decode(encoded.dump(), loaded, error));
    REQUIRE(error.empty());
    REQUIRE(loaded.sourceVersion == 3);
    REQUIRE(loaded.editableMeshes[0].source == data.editableMeshes[0].source);
    REQUIRE(loaded.editableMeshes[0].mirror == data.editableMeshes[0].mirror);
    REQUIRE(loaded.assets[0].id == data.assets[0].id);
    REQUIRE(loaded.assets[0].path == "../资源/multiple.gltf");
    REQUIRE(loaded.assets[0].meshIndex == 7);
    REQUIRE(loaded.camera == data.camera);
    REQUIRE(loaded.lighting == data.lighting);
    REQUIRE(loaded.cursor == data.cursor);
    REQUIRE(Json::parse(SceneSerializer::encode(loaded)) == encoded);
}

TEST_CASE("Supported versions default to Y-up and reject an explicit different convention",
          "[file-compatibility]") {
    const auto current = Json::parse(SceneSerializer::encode(SceneDocumentData{}));
    for (int version : {1, 2, 3}) {
        CAPTURE(version);
        auto legacy = current;
        legacy["version"] = version;
        legacy.erase("editorState");
        SceneDocumentData loaded;
        std::string error;
        REQUIRE(SceneSerializer::decode(legacy.dump(), loaded, error));
        REQUIRE(loaded.sourceVersion == version);
        REQUIRE(loaded.cursor == Cursor3D{});
        legacy["editorState"] = {{"upAxis", "Y"}, {"futureOptional", true}};
        legacy["futureOptional"] = Json::object();
        REQUIRE(SceneSerializer::decode(legacy.dump(), loaded, error));

        auto sentinel = completeDocument();
        sentinel.sourceVersion = 2;
        const auto before = SceneSerializer::encode(sentinel);
        for (const auto& axis : {Json("X"), Json("Z"), Json(0), Json(nullptr)}) {
            legacy["editorState"]["upAxis"] = axis;
            REQUIRE_FALSE(SceneSerializer::decode(legacy.dump(), sentinel, error));
            REQUIRE_FALSE(error.empty());
            REQUIRE(sentinel.sourceVersion == 2);
            REQUIRE(SceneSerializer::encode(sentinel) == before);
        }
    }
}

TEST_CASE("Malformed format 3 geometry bindings and fields preserve the whole output",
          "[file-compatibility]") {
    const auto data = completeDocument();
    const auto valid = Json::parse(SceneSerializer::encode(data));
    for (int failure = 0; failure < 9; ++failure) {
        CAPTURE(failure);
        auto invalid = valid;
        switch (failure) {
            case 1:
                invalid["editableMeshes"][0]["vertices"].push_back(
                    invalid["editableMeshes"][0]["vertices"][0]);
                break;
            case 2:
                invalid["editableMeshes"][0]["faces"][0]["corners"][0]["vertex"] = 999;
                break;
            case 3:
                invalid["entities"][1]["editableMesh"] = wideId + 999;
                break;
            case 4:
                invalid["editableMeshes"][0]["faces"][0]["corners"][0]["uv"] = {1};
                break;
            case 5:
                invalid["editableMeshes"][0]["faces"][0]["corners"][0]["normal"] = "bad";
                break;
            case 6:
                invalid["editableMeshes"][0]["faces"][0]["material"] = wideId + 300;
                break;
            case 7:
                invalid["editableMeshes"][0]["modifiers"][0]["threshold"] = -0.25;
                break;
            case 8:
                invalid["entities"][1]["meshAsset"] = wideId + 300;
                break;
        }
        auto output = data;
        output.sourceVersion = 2;
        const auto before = SceneSerializer::encode(output);
        std::string error;
        REQUIRE_FALSE(
            SceneSerializer::decode(failure == 0 ? "{broken" : invalid.dump(), output, error));
        REQUIRE_FALSE(error.empty());
        REQUIRE(output.sourceVersion == 2);
        REQUIRE(SceneSerializer::encode(output) == before);
    }
}
