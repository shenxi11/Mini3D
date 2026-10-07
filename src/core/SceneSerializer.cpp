/*
 * 模块名: SceneSerializer
 * 功能概述: 严格验证场景必需字段、稳定 ID 和资源引用。
 * 对外接口: SceneSerializer
 * 依赖关系: nlohmann/json、Scene
 * 输入输出: 版本化 UTF-8 JSON 到临时文档值。
 * 异常与错误: 捕获 JSON/验证错误，失败不发布部分结果。
 * 维护说明: 不做外部IO；写版本4，保留旧1/2/3字段含义和严格动画double。
 */
#include "SceneSerializer.h"
#include "NativeScenePreflight.h"

#include <nlohmann/json.hpp>
#include <initializer_list>
#include <limits>
#include <unordered_set>
namespace mini3d::core {
namespace {
using Json = nlohmann::json;
Json animationVectorJson(const glm::dvec3& value) {
    return Json::array({value.x, value.y, value.z});
}
glm::dvec3 animationVectorValue(const Json& value) {
    if (!value.is_array() || value.size() != 3)
        throw std::runtime_error("动画value必须恰好包含三个数字。");
    glm::dvec3 result;
    for (int index = 0; index < 3; ++index) {
        if (!value[index].is_number())
            throw std::runtime_error("动画value必须为double数字。");
        result[index] = value[index].get<double>();
        if (!std::isfinite(result[index]))
            throw std::runtime_error("动画value必须为有限double数字。");
    }
    return result;
}
std::uint32_t animationInteger(const Json& value) {
    if (!value.is_number_integer() || value < 0 ||
        value > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("动画设置和frame必须为合法整数。");
    return value.get<std::uint32_t>();
}
void animationFields(const Json& value, std::initializer_list<const char*> names) {
    if (!value.is_object() || value.size() != names.size())
        throw std::runtime_error("动画对象必需字段缺失或包含未知字段。");
    for (const auto* name : names)
        if (!value.contains(name))
            throw std::runtime_error("动画对象缺少必需字段。");
}
const char* animationChannelName(AnimationChannel channel) {
    switch (channel) {
        case AnimationChannel::Position: return "position";
        case AnimationChannel::RotationEulerXYZDegrees: return "rotationEulerXYZDegrees";
        case AnimationChannel::Scale: return "scale";
    }
    throw std::runtime_error("未知动画通道。");
}
AnimationChannel animationChannelValue(const Json& value) {
    if (value == "position") return AnimationChannel::Position;
    if (value == "rotationEulerXYZDegrees") return AnimationChannel::RotationEulerXYZDegrees;
    if (value == "scale") return AnimationChannel::Scale;
    throw std::runtime_error("未知动画通道。");
}
Json animationJson(const SceneAnimation& animation) {
    Json result{{"fps", animation.settings.fps}, {"startFrame", animation.settings.startFrame},
                {"endFrame", animation.settings.endFrame}, {"tracks", Json::array()}};
    for (const auto& [id, track] : animation.tracks) {
        Json keys = Json::array();
        for (const auto& key : track.keys) {
            keys.push_back({{"frame", key.frame}, {"value", animationVectorJson(key.value)},
                            {"interpolation", key.interpolation == AnimationInterpolation::Constant
                                                  ? "constant" : "linear"}});
        }
        result["tracks"].push_back({{"entityId", id.first}, {"channel", animationChannelName(id.second)},
                                    {"keys", std::move(keys)}});
    }
    return result;
}
SceneAnimation animationValue(const Json& value, const std::vector<SceneNode>& nodes) {
    animationFields(value, {"fps", "startFrame", "endFrame", "tracks"});
    SceneAnimation result;
    result.settings = {animationInteger(value.at("fps")), animationInteger(value.at("startFrame")),
                       animationInteger(value.at("endFrame"))};
    if (!value.at("tracks").is_array())
        throw std::runtime_error("动画tracks必须为数组。");
    std::unordered_set<EntityId> entities;
    std::vector<EntityId> entityIds;
    entityIds.reserve(nodes.size());
    for (const auto& node : nodes) {
        entities.insert(node.id);
        entityIds.push_back(node.id);
    }
    for (const auto& item : value.at("tracks")) {
        animationFields(item, {"entityId", "channel", "keys"});
        if (!item.at("entityId").is_number_unsigned())
            throw std::runtime_error("动画entityId必须为非零uint64编号。");
        const AnimationTrackId id{item.at("entityId").get<EntityId>(),
                                  animationChannelValue(item.at("channel"))};
        if (!entities.contains(id.first) || id.first == 0)
            throw std::runtime_error("动画轨道引用了不存在的实体。");
        if (result.tracks.contains(id))
            throw std::runtime_error("动画实体/通道绑定重复。");
        if (!item.at("keys").is_array())
            throw std::runtime_error("动画keys必须为数组。");
        AnimationTrack track;
        track.keys.reserve(item.at("keys").size());
        for (const auto& raw : item.at("keys")) {
            animationFields(raw, {"frame", "value", "interpolation"});
            const auto& interpolation = raw.at("interpolation");
            if (interpolation != "constant" && interpolation != "linear")
                throw std::runtime_error("未知动画插值。");
            track.keys.push_back({animationInteger(raw.at("frame")), animationVectorValue(raw.at("value")),
                                  interpolation == "constant" ? AnimationInterpolation::Constant
                                                              : AnimationInterpolation::Linear});
        }
        result.tracks.emplace(id, std::move(track));
    }
    // 所有引用和重复（包括空轨）已检查；只有合法空轨才规范化为无轨。
    std::erase_if(result.tracks, [](const auto& entry) { return entry.second.keys.empty(); });
    if (!validateSceneAnimation(result, entityIds).isValid())
        throw std::runtime_error("动画设置、原值、帧顺序、邻接缩放或规模无效。");
    return result;
}
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
modeling::MirrorOptions mirrorValue(const Json& modifier) {
    if (!modifier.is_object() || modifier.size() != 6 ||
        modifier.at("type").get<std::string>() != "Mirror")
        throw std::runtime_error("Mirror 修改器字段无效");
    modeling::MirrorOptions mirror;
    const auto axis = modifier.at("axis").get<std::string>();
    if (axis == "X")
        mirror.axis = modeling::MirrorAxis::X;
    else if (axis == "Y")
        mirror.axis = modeling::MirrorAxis::Y;
    else if (axis == "Z")
        mirror.axis = modeling::MirrorAxis::Z;
    else
        throw std::runtime_error("Mirror 轴必须为 X、Y 或 Z");
    if (!modifier.at("enabled").is_boolean() || !modifier.at("merge").is_boolean() ||
        !modifier.at("clipping").is_boolean())
        throw std::runtime_error("Mirror 启用、合并和夹持参数必须为布尔值");
    mirror.enabled = modifier.at("enabled").get<bool>();
    mirror.merge = modifier.at("merge").get<bool>();
    mirror.clipping = modifier.at("clipping").get<bool>();
    if (!modifier.at("threshold").is_number())
        throw std::runtime_error("Mirror 阈值必须为数值");
    mirror.threshold = modifier.at("threshold").get<double>();
    if (!mirror.isValid())
        throw std::runtime_error("Mirror 参数无效");
    return mirror;
}
modeling::SubdivisionOptions subdivisionValue(const Json& modifier) {
    if (!modifier.is_object() || modifier.size() != 3 ||
        modifier.at("type").get<std::string>() != "Subdivision" ||
        !modifier.at("enabled").is_boolean())
        throw std::runtime_error("Subdivision 修改器字段无效或启用参数不是布尔值");
    const auto& levels = modifier.at("levels");
    if (!levels.is_number_integer() || (levels != 1 && levels != 2))
        throw std::runtime_error("Subdivision 级数必须为整数 1 或 2");
    return {modifier.at("enabled").get<bool>(), levels.get<int>()};
}
Json meshJson(const EditableMeshResource& resource) {
    Json value{{"id", resource.id},
               {"vertices", Json::array()},
               {"faces", Json::array()},
               {"modifiers", Json::array()}};
    if (resource.mirror) {
        const auto& mirror = *resource.mirror;
        const char* axis = mirror.axis == modeling::MirrorAxis::X ? "X"
                           : mirror.axis == modeling::MirrorAxis::Y ? "Y" : "Z";
        value["modifiers"].push_back({{"type", "Mirror"},
                                      {"axis", axis},
                                      {"enabled", mirror.enabled},
                                      {"merge", mirror.merge},
                                      {"clipping", mirror.clipping},
                                      {"threshold", mirror.threshold}});
    }
    if (resource.subdivision) {
        value["modifiers"].push_back({{"type", "Subdivision"},
                                      {"enabled", resource.subdivision->enabled},
                                      {"levels", resource.subdivision->levels}});
    }
    for (const auto& vertex : resource.source.vertices) {
        value["vertices"].push_back({{"id", vertex.id}, {"position", vectorJson(vertex.position)}});
    }
    for (const auto& face : resource.source.faces) {
        Json polygon{{"id", face.id}, {"material", face.material}, {"corners", Json::array()}};
        for (const auto& corner : face.corners) {
            Json item{{"id", corner.id},
                      {"vertex", corner.vertex},
                      {"uv", Json::array({corner.uv.x, corner.uv.y})},
                      {"color", vectorJson(corner.color)}};
            if (corner.normal) {
                item["normal"] = vectorJson(*corner.normal);
            }
            polygon["corners"].push_back(std::move(item));
        }
        value["faces"].push_back(std::move(polygon));
    }
    return value;
}
EditableMeshResource meshValue(const Json& value) {
    EditableMeshResource resource;
    resource.id = unsignedValue(value.at("id"));
    if (value.contains("modifier") && value.contains("modifiers"))
        throw std::runtime_error("不能同时使用 modifier 与 modifiers 字段");
    if (value.contains("modifiers")) {
        const auto& modifiers = value.at("modifiers");
        if (!modifiers.is_array() || modifiers.size() > 2)
            throw std::runtime_error("modifiers 必须为至多两个元素的数组");
        for (const auto& modifier : modifiers) {
            const auto type = modifier.at("type").get<std::string>();
            if (type == "Mirror") {
                if (resource.mirror || resource.subdivision)
                    throw std::runtime_error("Mirror 不可重复，且必须位于 Subdivision 之前");
                resource.mirror = mirrorValue(modifier);
            } else if (type == "Subdivision") {
                if (resource.subdivision)
                    throw std::runtime_error("Subdivision 不可重复");
                resource.subdivision = subdivisionValue(modifier);
            } else {
                throw std::runtime_error("未知修改器：" + type);
            }
        }
    } else if (value.contains("modifier")) {
        resource.mirror = mirrorValue(value.at("modifier"));
    }
    if (!value.at("vertices").is_array() || !value.at("faces").is_array()) {
        throw std::runtime_error("网格 vertices/faces 必须为数组");
    }
    for (const auto& vertex : value.at("vertices")) {
        resource.source.vertices.push_back(
            {unsignedValue(vertex.at("id")), vectorValue(vertex.at("position"))});
    }
    for (const auto& polygon : value.at("faces")) {
        modeling::EditableFace face;
        face.id = unsignedValue(polygon.at("id"));
        face.material = unsignedValue(polygon.at("material"));
        if (!polygon.at("corners").is_array()) {
            throw std::runtime_error("面 corners 必须为数组");
        }
        for (const auto& item : polygon.at("corners")) {
            modeling::MeshCorner corner;
            corner.id = unsignedValue(item.at("id"));
            corner.vertex = unsignedValue(item.at("vertex"));
            const auto& uv = item.at("uv");
            if (!uv.is_array() || uv.size() != 2) {
                throw std::runtime_error("面角 UV 必须包含两个数值");
            }
            corner.uv = {number(uv[0]), number(uv[1])};
            corner.color = vectorValue(item.at("color"));
            if (item.contains("normal")) {
                corner.normal = vectorValue(item.at("normal"));
            }
            face.corners.push_back(std::move(corner));
        }
        resource.source.faces.push_back(std::move(face));
    }
    return resource;
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
bool Cursor3D::isValid() const {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}
std::string SceneSerializer::encode(const SceneDocumentData& data) {
    if (!data.cursor.isValid()) {
        throw std::runtime_error("3D 游标坐标必须为有限数字");
    }
    std::vector<EntityId> entityIds;
    entityIds.reserve(data.nodes.size());
    for (const auto& node : data.nodes) entityIds.push_back(node.id);
    if (!validateSceneAnimation(data.animation, entityIds).isValid())
        throw std::runtime_error("正式动画定义或实体绑定无效，不能保存。");
    Json root{{"format", "Mini3DScene"}, {"version", 4}};
    root["animation"] = animationJson(data.animation);
    root["editorState"] = {
        {"upAxis", "Y"},
        {"cursor3D",
         {{"position", vectorJson(data.cursor.position)}, {"visible", data.cursor.visible}}}};
    root["editableMeshes"] = Json::array();
    for (const auto& mesh : data.editableMeshes) {
        root["editableMeshes"].push_back(meshJson(mesh));
    }
    root["collections"] = Json::array();
    for (const auto& collection : data.collections) {
        root["collections"].push_back({{"id", collection.id},
                                       {"name", collection.name},
                                       {"visible", collection.visible},
                                       {"members", collection.members}});
    }
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
        if (node.editableMesh != 0) {
            value["editableMesh"] = node.editableMesh;
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
    auto text = root.dump(2) + "\n";
    if (text.size() > kNativeSceneMaximumBytes)
        throw std::runtime_error("格式4场景编码超过64MiB上限，未写入文件。");
    return text;
}
bool SceneSerializer::decode(const std::string& text, SceneDocumentData& result,
                             std::string& error) {
    try {
        NativeSceneProbe probe;
        if (!validateNativeSceneText(text, probe, error))
            return false;
        const auto root = Json::parse(text);
        if (root.at("format") != "Mini3DScene" || !root.at("version").is_number_integer() ||
            (root.at("version") != 1 && root.at("version") != 2 &&
             root.at("version") != 3 && root.at("version") != 4)) {
            throw std::runtime_error("不支持此场景格式或版本（需要Mini3DScene 1/2/3/4）。");
        }
        SceneDocumentData data;
        data.sourceVersion = root.at("version").get<int>();
        if (root.contains("collections")) {
            const auto& collections = root.at("collections");
            if (!collections.is_array()) {
                throw std::runtime_error("collections 必须为数组");
            }
            if (data.sourceVersion < 3 && !collections.empty()) {
                throw std::runtime_error("旧版本不能包含集合");
            }
            for (const auto& value : collections) {
                if (!value.is_object() || !value.at("name").is_string() ||
                    !value.at("visible").is_boolean() || !value.at("members").is_array()) {
                    throw std::runtime_error("集合名称、可见性或成员数组无效");
                }
                SceneCollection collection;
                collection.id = unsignedValue(value.at("id"));
                collection.name = value.at("name").get<std::string>();
                collection.visible = value.at("visible").get<bool>();
                for (const auto& member : value.at("members")) {
                    if (!collection.members.insert(unsignedValue(member)).second) {
                        throw std::runtime_error("同一集合不能重复引用成员");
                    }
                }
                data.collections.push_back(std::move(collection));
            }
        }
        if (root.contains("editorState")) {
            const auto& state = root.at("editorState");
            if (!state.is_object()) {
                throw std::runtime_error("editorState 必须为对象");
            }
            if (state.contains("upAxis") && state.at("upAxis") != "Y") {
                throw std::runtime_error("场景仅支持 Y-up 坐标约定");
            }
            if (data.sourceVersion >= 3 && state.contains("cursor3D")) {
                const auto& cursor = state.at("cursor3D");
                data.cursor.position = vectorValue(cursor.at("position"));
                data.cursor.visible = cursor.at("visible").get<bool>();
            }
        }
        if (data.sourceVersion >= 3) {
            if (!root.at("editableMeshes").is_array()) {
                throw std::runtime_error("editableMeshes 必须为数组");
            }
            for (const auto& mesh : root.at("editableMeshes")) {
                data.editableMeshes.push_back(meshValue(mesh));
            }
        } else if (root.contains("editableMeshes") && !root.at("editableMeshes").empty()) {
            throw std::runtime_error("旧版本不能包含可编辑网格");
        }
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
            if (value.contains("editableMesh")) {
                if (data.sourceVersion < 3) {
                    throw std::runtime_error("旧版本不能绑定可编辑网格");
                }
                node.editableMesh = unsignedValue(value.at("editableMesh"));
                if (node.editableMesh == 0) {
                    throw std::runtime_error("可编辑网格编号不能为零");
                }
            }
            if (data.sourceVersion >= 2) {
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
        if (data.sourceVersion == 4) {
            data.animation = animationValue(root.at("animation"), data.nodes);
        } else if (root.contains("animation")) {
            throw std::runtime_error("VERSION_FIELD_MISMATCH：旧版本不能包含animation。");
        }
        Scene validation;
        if (!validation.replaceNodes(data.nodes, data.editableMeshes, data.collections, data.animation)) {
            throw std::runtime_error("对象编号、层级、组件绑定、可编辑网格或集合引用无效");
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
