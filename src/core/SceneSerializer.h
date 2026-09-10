/*
 * 模块名: SceneSerializer
 * 功能概述: 写版本 2、读版本 1/2 场景逻辑 JSON，不访问磁盘或加载 GPU 资源。
 * 对外接口: CameraState、SceneDocumentData、SceneSerializer
 * 依赖关系: Scene、标准库；实现依赖 nlohmann/json
 * 输入输出: UTF-8 JSON 与节点、相机、光照、相对资源路径。
 * 异常与错误: decode 返回错误且不改变输出；未知字段忽略、未知版本拒绝。
 * 维护说明: 不暴露第三方 JSON 类型；四元数顺序 w/x/y/z。
 */
#pragma once
#include "Scene.h"
namespace mini3d::core {
/** @brief 观察相机位置/目标和聚焦范围，非场景 Camera 实体。 */
struct CameraState {
    glm::vec3 position{5, 4, 8};
    glm::vec3 target{0};
    float focusRadius = 0;
    float maximumDistance = 50;
    [[nodiscard]] bool isValid() const;
    bool operator==(const CameraState&) const = default;
};
/** @brief 文件内网格标识映射到源 glTF 的 primitive 序号，不保存 GPU ID。 */
struct ResourceReference {
    AssetId id;
    std::string path;
    std::size_t meshIndex;
};
struct SceneDocumentData {
    std::vector<SceneNode> nodes;
    std::vector<ResourceReference> assets;
    CameraState camera;
    Lighting lighting;
};
/** @brief 纯 CPU JSON 编解码，外部资源的实际读取由文档服务处理。 */
class SceneSerializer {
  public:
    static std::string encode(const SceneDocumentData& data);
    static bool decode(const std::string& text, SceneDocumentData& result, std::string& error);
};
} // namespace mini3d::core
