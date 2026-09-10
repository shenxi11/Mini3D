/*
 * 模块名: Ray
 * 功能概述: 使用 slab 区间裁剪计算射线与轴对齐盒的相交。
 * 对外接口: intersectRayAabb
 * 依赖关系: Ray、Aabb、标准数学库
 * 输入输出: 输入射线与包围盒，命中时写入非负参数。
 * 异常与错误: 平行且位于 slab 外、背向或无效输入返回 false。
 * 维护说明: 精确判断零方向分量，不用 epsilon 吞掉几乎平行的有效射线。
 */
#include "Ray.h"

#include "Aabb.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mini3d::core {

bool intersectRayAabb(const Ray& ray, const Aabb& bounds, float& distance) {
    if (!bounds.isValid()) {
        return false;
    }
    bool hasDirection = false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(ray.origin[axis]) || !std::isfinite(ray.direction[axis])) {
            return false;
        }
        hasDirection = hasDirection || ray.direction[axis] != 0.0F;
    }
    if (!hasDirection) {
        return false;
    }
    double entry = 0.0;
    double exit = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (ray.direction[axis] == 0.0F) {
            if (ray.origin[axis] < bounds.minimum[axis] ||
                ray.origin[axis] > bounds.maximum[axis]) {
                return false;
            }
            continue;
        }
        double nearValue =
            (static_cast<double>(bounds.minimum[axis]) - ray.origin[axis]) / ray.direction[axis];
        double farValue =
            (static_cast<double>(bounds.maximum[axis]) - ray.origin[axis]) / ray.direction[axis];
        if (nearValue > farValue) {
            std::swap(nearValue, farValue);
        }
        entry = std::max(entry, nearValue);
        exit = std::min(exit, farValue);
        if (entry > exit) {
            return false;
        }
    }
    if (entry > std::numeric_limits<float>::max()) {
        return false;
    }
    distance = static_cast<float>(entry);
    return true;
}

} // namespace mini3d::core
