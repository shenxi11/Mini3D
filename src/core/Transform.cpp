/*
 * 模块名: Transform
 * 功能概述: 第三周场景数据与变换支持。
 * 对外接口: Transform
 * 依赖关系: C++ 标准库、GLM
 * 输入输出: 输入场景操作，输出节点状态或验证结果。
 * 异常与错误: 非法操作拒绝且保留既有状态；分配失败由运行时报告。
 * 维护说明: 不依赖 Qt/OpenGL，关联关系使用稳定 ID。
 */
#include "Transform.h"

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
namespace mini3d::core {
glm::mat4 Transform::localMatrix() const {
    return glm::translate(glm::mat4(1.0F), position) * glm::mat4_cast(glm::normalize(rotation)) *
           glm::scale(glm::mat4(1.0F), scale);
}
bool Transform::isValid() const {
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(position[axis]) || !std::isfinite(scale[axis]) ||
            std::abs(scale[axis]) < 0.001F) {
            return false;
        }
    }
    for (int component = 0; component < 4; ++component) {
        if (!std::isfinite(rotation[component])) {
            return false;
        }
    }
    const float length = glm::length(rotation);
    return std::isfinite(length) && length > 0.000001F;
}
} // namespace mini3d::core
