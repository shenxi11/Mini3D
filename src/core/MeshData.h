/*
 * 模块名: MeshData
 * 功能概述: 共享 CPU 顶点与索引布局，使 Assets 和 Renderer 单向依赖 Core。
 * 对外接口: MeshVertex、MeshData
 * 依赖关系: Aabb、GLM、标准容器
 * 输入输出: 顶点、索引与局部包围盒。
 * 异常与错误: 合法性由生成者或导入器验证。
 * 维护说明: 无 Qt/OpenGL；旧 Renderer 路径保留类型别名。
 */
#pragma once
#include "Aabb.h"

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>
namespace mini3d::core {
/** @brief 交错位置、法线、颜色和底部原点 UV。 */
struct MeshVertex {
    glm::vec3 position{0.0F};
    glm::vec3 normal{0.0F, 1.0F, 0.0F};
    glm::vec3 color{1.0F};
    glm::vec2 uv{0.0F};
};
/** @brief 单个三角形网格的纯 CPU 数据。 */
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    /** @brief 从顶点生成局部 AABB；空网格返回无效盒。 */
    [[nodiscard]] Aabb bounds() const {
        Aabb result;
        for (const auto& vertex : vertices) {
            result.expand(vertex.position);
        }
        return result;
    }
};
} // namespace mini3d::core
