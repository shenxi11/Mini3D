/*
 * 模块名: NativeSceneFixture
 * 功能概述: 把无 J1 作者数据的当前文件夹具明确转换为真实 V1 到 V4 数值身份。
 * 对外接口: legacySceneJson、legacySceneText，仅供持久化兼容测试。
 * 依赖关系: SceneSerializer、nlohmann/json、标准库。
 * 输入输出: 当前 writer JSON 和目标旧版本到旧字段/数值身份夹具。
 * 异常与错误: 旧格式无法表达的作者数据明确拒绝，不伪造等价旧作品。
 * 维护说明: 仅转换列明的旧身份路径，不递归猜测字符串，不修改生产解码规则。
 */
#pragma once

#include "core/SceneSerializer.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace mini3d::test {
/** @brief 将无作者数据的 V5 JSON 转为指定旧格式；不修改调用者输入，无法表达时抛错。 */
inline nlohmann::json legacySceneJson(nlohmann::json value, int version) {
    if (version < 1 || version > 4 || value.at("version") != 5)
        throw std::runtime_error("旧版夹具只能从V5 writer转换为版本1到4。");
    const auto& properties = value.at("animation").at("properties");
    if (!value.at("authorAssets").empty() || !properties.at("rationalFps").is_null() ||
        !properties.at("actions").empty() || !properties.at("bindings").empty())
        throw std::runtime_error("旧版夹具不能表达J1作者材料或typed动画。");
    for (const auto& entity : value.at("entities"))
        if (!entity.at("materialSlots").empty() || entity.contains("sun"))
            throw std::runtime_error("旧版夹具不能表达J1材料槽或Sun。");
    const auto numericIdentity = [](nlohmann::json& identity) {
        identity = std::stoull(identity.get<std::string>());
    };
    for (auto& entity : value.at("entities")) {
        numericIdentity(entity.at("id"));
        numericIdentity(entity.at("parent"));
        if (entity.contains("meshAsset")) numericIdentity(entity.at("meshAsset"));
        if (entity.contains("editableMesh")) numericIdentity(entity.at("editableMesh"));
        entity.erase("materialSlots");
    }
    for (auto& asset : value.at("assets")) numericIdentity(asset.at("id"));
    for (auto& collection : value.at("collections")) {
        numericIdentity(collection.at("id"));
        for (auto& member : collection.at("members")) numericIdentity(member);
    }
    for (auto& mesh : value.at("editableMeshes")) {
        numericIdentity(mesh.at("id"));
        for (auto& vertex : mesh.at("vertices")) numericIdentity(vertex.at("id"));
        for (auto& face : mesh.at("faces")) {
            numericIdentity(face.at("id"));
            numericIdentity(face.at("material"));
            for (auto& corner : face.at("corners")) {
                numericIdentity(corner.at("id"));
                numericIdentity(corner.at("vertex"));
            }
        }
    }
    for (auto& track : value.at("animation").at("tracks")) numericIdentity(track.at("entityId"));
    value.at("animation").erase("properties");
    if (version < 4) value.erase("animation");
    value.erase("documentUuid");
    value.erase("idHighWatermarks");
    value.erase("allocationState");
    value.erase("authorAssets");
    value["version"] = version;
    return value;
}
/** @brief 捕获的文档经正式 writer 校验后输出旧格式文本；不读写文件，非法输入抛错。 */
inline std::string legacySceneText(const core::SceneDocumentData& data, int version) {
    return legacySceneJson(nlohmann::json::parse(core::SceneSerializer::encode(data)), version).dump(2) + "\n";
}
} // namespace mini3d::test
