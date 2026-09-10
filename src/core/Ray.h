/*
 * 模块名: Ray
 * 功能概述: 定义射线与 AABB 相交契约，为后续拾取提供纯数学基础。
 * 对外接口: Ray、intersectRayAabb
 * 依赖关系: GLM、Aabb
 * 输入输出: 输入同一空间的射线与盒，输出最近非负射线参数。
 * 异常与错误: 无效盒、非有限射线或零方向返回 false，输出保持原值。
 * 维护说明: 方向归一化时参数等于距离；盒内或边界起点命中参数为 0。
 */
#pragma once

#include <glm/vec3.hpp>

namespace mini3d::core {
struct Aabb;

/** @brief origin + t * direction；distance 语义要求调用方使用单位方向。 */
struct Ray {
    glm::vec3 origin{0.0F};
    glm::vec3 direction{0.0F, 0.0F, -1.0F};
};

/** @brief 求同一空间下最近 t >= 0；未命中不改写 distance，不抛异常。 */
[[nodiscard]] bool intersectRayAabb(const Ray& ray, const Aabb& bounds, float& distance);

} // namespace mini3d::core
