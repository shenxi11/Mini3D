/*
 * 模块名: Aabb
 * 功能概述: 实现逐点扩展、空盒检查与八角点仿射包围计算。
 * 对外接口: Aabb
 * 依赖关系: GLM
 * 输入输出: 输入顶点与矩阵，输出轴对齐的最小/最大角。
 * 异常与错误: 不抛异常；空盒变换返回空盒。
 * 维护说明: 不把仅变换最小/最大两角误用为旋转后的包围盒。
 */
#include "Aabb.h"

#include <cmath>
#include <glm/common.hpp>
#include <glm/vec4.hpp>

namespace mini3d::core {

void Aabb::expand(const glm::vec3& point) {
    minimum = glm::min(minimum, point);
    maximum = glm::max(maximum, point);
}

bool Aabb::isValid() const noexcept {
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
            minimum[axis] > maximum[axis]) {
            return false;
        }
    }
    return true;
}

Aabb Aabb::transformed(const glm::mat4& matrix) const {
    Aabb result;
    if (!isValid()) {
        return result;
    }
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point((corner & 1) ? maximum.x : minimum.x,
                              (corner & 2) ? maximum.y : minimum.y,
                              (corner & 4) ? maximum.z : minimum.z);
        result.expand(glm::vec3(matrix * glm::vec4(point, 1.0F)));
    }
    return result;
}

} // namespace mini3d::core
