/*
 * 模块名: Aabb
 * 功能概述: 提供轴对齐包围盒累积和仿射变换后的包围计算。
 * 对外接口: Aabb::expand、isValid、transformed
 * 依赖关系: GLM、C++ 标准库
 * 输入输出: 输入有限顶点或仿射矩阵，输出局部或世界空间包围盒。
 * 异常与错误: 默认空盒无效；无效盒变换后仍为空盒。
 * 维护说明: 不依赖 Qt 或 GPU；零厚度盒有效，矩阵须为仿射矩阵。
 */
#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <limits>

namespace mini3d::core {

/** @brief 用最小与最大角描述包围盒；默认状态为空，首次 expand 后有效。 */
struct Aabb {
    glm::vec3 minimum{std::numeric_limits<float>::infinity()};
    glm::vec3 maximum{-std::numeric_limits<float>::infinity()};

    /** @brief 将有限点纳入包围盒；不改变点数据。 */
    void expand(const glm::vec3& point);
    /** @brief 判断角点有限且各轴 minimum <= maximum。 */
    [[nodiscard]] bool isValid() const noexcept;
    /** @brief 变换八个角点并重新包围，支持旋转与负/非均匀缩放。 */
    [[nodiscard]] Aabb transformed(const glm::mat4& matrix) const;
};

} // namespace mini3d::core
