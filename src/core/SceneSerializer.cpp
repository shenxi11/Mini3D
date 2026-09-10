/*
 * 模块名: SceneSerializer
 * 功能概述: 严格验证场景必需字段、稳定 ID 和资源引用。
 * 对外接口: SceneSerializer
 * 依赖关系: nlohmann/json、Scene
 * 输入输出: 版本化 UTF-8 JSON 到临时文档值。
 * 异常与错误: 捕获 JSON/验证错误，失败不发布部分结果。
 * 维护说明: 不做外部 IO；写 version 2，兼容 version 1 的全局光照和观察相机。
 */
#include "SceneSerializer.h"

#include <nlohmann/json.hpp>
#include <unordered_set>
namespace mini3d::core {
namespace {
using Json = nlohmann::json;
Json vectorJson(const glm::vec3& value) {
    return Json::array({value.x, value.y, value.z});
}
float number(const Json& value) {
    if (!value.is_number()) {
        throw std::runtime_error("应为数值");
    }
    const auto result = value.get<float>();
    if (!std::isfinite(result)) {
        throw std::runtime_error("数值不能为无穷或非数值");
    }
    return result;
}
glm::vec3 vectorValue(const Json& value) {
    if (!value.is_array() || value.size() != 3) {
        throw std::runtime_error("应为包含 3 个数值的向量");
    }
    return {number(value[0]), number(value[1]), number(value[2])};
}
std::uint64_t unsignedValue(const Json& value) {
    if (!value.is_number_unsigned()) {
        throw std::runtime_error("编号或索引应为无符号整数");
    }
    return value.get<std::uint64_t>();
}
const char* primitiveName(PrimitiveKind kind) {
    switch (kind) {
        case PrimitiveKind::Cube:
            return "Cube";
        case PrimitiveKind::Sphere:
            return "Sphere";
        case PrimitiveKind::Plane:
            return "Plane";
        default:
            return "Empty";
    }
}
PrimitiveKind primitiveValue(const std::string& text) {
    if (text == "Cube") {
        return PrimitiveKind::Cube;
    }
    if (text == "Sphere") {
        return PrimitiveKind::Sphere;
    }
    if (text == "Plane") {
        return PrimitiveKind::Plane;
    }
    if (text == "Empty") {
        return PrimitiveKind::Empty;
    }
    throw std::runtime_error("未知基本几何体：" + text);
}
} // namespace
bool CameraState::isValid() const {
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(position[i]) || !std::isfinite(target[i])) {
            return false;
        }
    }
    const auto offset = position - target;
    const float distance = glm::length(offset);
    // 最大缩放处的球坐标转换可能使长度多出几个 float ULP，不能拒绝编辑器自身状态。
    const float distanceTolerance = maximumDistance * 1.0e-6F;
    return std::isfinite(distance) && distance >= 0.599F &&
           std::abs(offset.y / distance) < 0.9999F && std::isfinite(focusRadius) &&
           focusRadius >= 0 && std::isfinite(maximumDistance) && maximumDistance >= 0.599F &&
           distance - maximumDistance <= distanceTolerance;
}
std::string SceneSerializer::encode(const SceneDocumentData& data) {
    Json root{{"format", "Mini3DScene"}, {"version", 2}};
    root["editorCamera"] = {{"position", vectorJson(data.camera.position)},
                            {"target", vectorJson(data.camera.target)},
                            {"focusRadius", data.camera.focusRadius},
                            {"maximumDistance", data.camera.maximumDistance}};
    root["lighting"] = {{"direction", vectorJson(data.lighting.direction)},
                        {"color", vectorJson(data.lighting.color)},
                        {"intensity", data.lighting.intensity},
                        {"ambient", data.lighting.ambient}};
    root["assets"] = Json::array();
    for (const auto& asset : data.assets) {
        root["assets"].push_back({{"id", asset.id},
                                  {"type", "gltf"},
                                  {"path", asset.path},
                                  {"meshIndex", asset.meshIndex}});
    }
    root["entities"] = Json::array();
    for (const auto& node : data.nodes) {
        const auto& transform = node.transform;
        const auto& rotation = transform.rotation;
        Json value{{"id", node.id},
                   {"parent", node.parent},
                   {"name", node.name},
                   {"visible", node.visible},
                   {"primitive", primitiveName(node.primitive)},
                   {"transform",
                    {{"position", vectorJson(transform.position)},
                     {"rotation", Json::array({rotation.w, rotation.x, rotation.y, rotation.z})},
                     {"scale", vectorJson(transform.scale)}}},
                   {"surface",
                    {{"tint", vectorJson(node.surface.tint)},
                     {"useTexture", node.surface.useTexture},
                     {"useVertexColor", node.surface.useVertexColor}}}};
        if (node.meshRenderer) {
            value["meshAsset"] = node.meshRenderer->mesh;
        }
        if (node.camera) {
            value["camera"] = {{"fieldOfView", node.camera->fieldOfView},
                               {"nearPlane", node.camera->nearPlane},
                               {"farPlane", node.camera->farPlane}};
        }
        if (node.light) {
            value["light"] = {{"type", "directional"},
                              {"color", vectorJson(node.light->color)},
                              {"intensity", node.light->intensity}};
        }
        root["entities"].push_back(std::move(value));
    }
    return root.dump(2) + "\n";
}
bool SceneSerializer::decode(const std::string& text, SceneDocumentData& result,
                             std::string& error) {
    try {
        const auto root = Json::parse(text);
        if (root.at("format") != "Mini3DScene" || !root.at("version").is_number_integer() ||
            (root.at("version") != 1 && root.at("version") != 2)) {
            throw std::runtime_error("不支持此场景格式或版本（需要 Mini3DScene 1/2）");
        }
        SceneDocumentData data;
        const auto& camera = root.at("editorCamera");
        data.camera.position = vectorValue(camera.at("position"));
        data.camera.target = vectorValue(camera.at("target"));
        data.camera.focusRadius = number(camera.at("focusRadius"));
        data.camera.maximumDistance = number(camera.at("maximumDistance"));
        if (!data.camera.isValid()) {
            throw std::runtime_error("编辑视图相机参数无效");
        }
        const auto& light = root.at("lighting");
        data.lighting.direction = vectorValue(light.at("direction"));
        data.lighting.color = vectorValue(light.at("color"));
        data.lighting.intensity = number(light.at("intensity"));
        data.lighting.ambient = number(light.at("ambient"));
        if (!data.lighting.isValid()) {
            throw std::runtime_error("场景光照参数无效");
        }
        if (!root.at("assets").is_array() || !root.at("entities").is_array()) {
            throw std::runtime_error("assets/entities 字段必须为数组");
        }
        std::unordered_set<AssetId> resources;
        for (const auto& asset : root.at("assets")) {
            ResourceReference reference{
                unsignedValue(asset.at("id")), asset.at("path").get<std::string>(),
                static_cast<std::size_t>(unsignedValue(asset.at("meshIndex")))};
            if (asset.at("type") != "gltf" || reference.id == 0 || reference.path.empty() ||
                !resources.insert(reference.id).second) {
                throw std::runtime_error("资源引用无效或重复");
            }
            data.assets.push_back(std::move(reference));
        }
        for (const auto& value : root.at("entities")) {
            SceneNode node;
            node.id = unsignedValue(value.at("id"));
            node.parent = unsignedValue(value.at("parent"));
            node.name = value.at("name").get<std::string>();
            node.visible = value.at("visible").get<bool>();
            node.primitive = primitiveValue(value.at("primitive").get<std::string>());
            const auto& transform = value.at("transform");
            node.transform.position = vectorValue(transform.at("position"));
            node.transform.scale = vectorValue(transform.at("scale"));
            const auto& rotation = transform.at("rotation");
            if (!rotation.is_array() || rotation.size() != 4) {
                throw std::runtime_error("应为 w/x/y/z 四元数");
            }
            node.transform.rotation = {number(rotation[0]), number(rotation[1]),
                                       number(rotation[2]), number(rotation[3])};
            const auto& surface = value.at("surface");
            node.surface.tint = vectorValue(surface.at("tint"));
            node.surface.useTexture = surface.at("useTexture").get<bool>();
            node.surface.useVertexColor = surface.at("useVertexColor").get<bool>();
            if (value.contains("meshAsset")) {
                const auto mesh = unsignedValue(value.at("meshAsset"));
                if (!resources.contains(mesh) || node.primitive != PrimitiveKind::Empty) {
                    throw std::runtime_error("网格引用无效或与基本几何体冲突");
                }
                node.meshRenderer = MeshRendererComponent{mesh, kInvalidAsset};
            }
            if (root.at("version") == 2) {
                if (value.contains("camera")) {
                    const auto& component = value.at("camera");
                    node.camera = CameraComponent{number(component.at("fieldOfView")),
                                                  number(component.at("nearPlane")),
                                                  number(component.at("farPlane"))};
                }
                if (value.contains("light")) {
                    const auto& component = value.at("light");
                    if (component.at("type") != "directional") {
                        throw std::runtime_error("不支持此光源类型");
                    }
                    node.light = LightComponent{vectorValue(component.at("color")),
                                                number(component.at("intensity"))};
                }
            }
            data.nodes.push_back(std::move(node));
        }
        Scene validation;
        if (!validation.replaceNodes(data.nodes)) {
            throw std::runtime_error("对象编号、名称、变换或层级无效");
        }
        data.nodes = validation.nodes();
        result = std::move(data);
        error.clear();
        return true;
    } catch (const std::exception& failure) {
        error = failure.what();
        return false;
    }
}
} // namespace mini3d::core
