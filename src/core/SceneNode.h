/*
 * 模块名: SceneNode
 * 功能概述: 第三周场景数据与变换支持。
 * 对外接口: SceneNode
 * 依赖关系: C++ 标准库、GLM
 * 输入输出: 输入场景操作，输出节点状态或验证结果。
 * 异常与错误: 非法操作拒绝且保留既有状态；分配失败由运行时报告。
 * 维护说明: 不依赖 Qt/OpenGL，关联关系使用稳定 ID。
 */
#pragma once
#include "Appearance.h"
#include "AssetId.h"
#include "EntityId.h"
#include "Transform.h"

#include <optional>
#include <string>
#include <vector>
namespace mini3d::core {
/** @brief 当前内置几何标识；不包含 GPU 句柄，外部 AssetId 在导入阶段接入。 */
enum class PrimitiveKind { Empty, Cube, Sphere, Plane };
/** @brief 透视相机；局部 -Z 朝前、+Y 朝上，角度单位为度，缩放不影响朝向。 */
struct CameraComponent {
    float fieldOfView = 45;
    float nearPlane = 0.1F;
    float farPlane = 1000;
    [[nodiscard]] bool isValid() const {
        return std::isfinite(fieldOfView) && fieldOfView >= 1 && fieldOfView <= 179 &&
               std::isfinite(nearPlane) && nearPlane >= 0.001F && std::isfinite(farPlane) &&
               farPlane > nearPlane && farPlane <= 1000000;
    }
    bool operator==(const CameraComponent&) const = default;
};
/** @brief 单方向灯；局部 -Z 为光线传播方向，颜色与强度独立于缩放/位置。 */
struct LightComponent {
    glm::vec3 color{1};
    float intensity = 0.72F;
    [[nodiscard]] bool isValid() const {
        return isValidColor(color) && std::isfinite(intensity) && intensity >= 0 && intensity <= 10;
    }
    bool operator==(const LightComponent&) const = default;
};
/** @brief Scene 按值拥有的节点，调用者通过 ID 访问，不持久保存地址。 */
struct SceneNode {
    EntityId id{kInvalidEntity};
    std::string name;
    EntityId parent{kInvalidEntity};
    std::vector<EntityId> children;
    Transform transform;
    bool visible{true};
    PrimitiveKind primitive{PrimitiveKind::Empty};
    std::optional<MeshRendererComponent> meshRenderer;
    SurfaceStyle surface;
    std::optional<CameraComponent> camera;
    std::optional<LightComponent> light;
};
} // namespace mini3d::core
