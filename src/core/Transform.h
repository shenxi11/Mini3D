/*
 * 模块名: Transform
 * 功能概述: 第三周场景数据与变换支持。
 * 对外接口: Transform
 * 依赖关系: C++ 标准库、GLM
 * 输入输出: 输入场景操作，输出节点状态或验证结果。
 * 异常与错误: 非法操作拒绝且保留既有状态；分配失败由运行时报告。
 * 维护说明: 不依赖 Qt/OpenGL，关联关系使用稳定 ID。
 */
#pragma once
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
namespace mini3d::core {
/** @brief 右手 Y-up 局部变换，矩阵顺序 T × R × S，旋转使用单位四元数。 */
struct Transform {
    glm::vec3 position{0.0F};
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 scale{1.0F};
    /** @brief 返回局部到父空间的仿射矩阵。 */
    [[nodiscard]] glm::mat4 localMatrix() const;
    /** @brief 检查有限值、有效四元数与非零缩放，避免法线矩阵奇异。 */
    [[nodiscard]] bool isValid() const;
};
} // namespace mini3d::core
