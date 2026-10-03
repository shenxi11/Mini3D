/*
 * 模块名: EditableMesh
 * 功能概述: 定义独立于渲染拆点的多边形网格快照与稳定元素身份。
 * 对外接口: EditableMesh、EdgeKey、createEditableCube
 * 依赖关系: Core AssetId、GLM、C++ 标准容器
 * 输入输出: 显式点/面/面角 ID、位置、UV、硬法线和材质引用。
 * 异常与错误: 候选允许离线构造，安装前必须通过 validateEditableMesh；分配异常向上传播。
 * 维护说明: ID 非容器下标；本层不持有 Scene/Qt/GL，不负责文档 revision 或文件协议。
 */
#pragma once

#include "core/AssetId.h"

#include <algorithm>
#include <compare>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <optional>
#include <vector>

namespace mini3d::core::modeling {
using VertexId = std::uint64_t;
using FaceId = std::uint64_t;
using CornerId = std::uint64_t;

/** @brief 拓扑顶点；0 是非法 ID，同坐标不同 ID 不会被自动焊接。 */
struct EditableVertex {
    VertexId id = 0;
    glm::vec3 position{0.0F};
    bool operator==(const EditableVertex&) const = default;
};

/** @brief 面角属性独立于共享顶点；无硬法线时由派生网格计算面法线。 */
struct MeshCorner {
    CornerId id = 0;
    VertexId vertex = 0;
    glm::vec2 uv{0.0F};
    std::optional<glm::vec3> normal;
    glm::vec3 color{1.0F};
    bool operator==(const MeshCorner&) const = default;
};

/** @brief 面的有序环从外侧观察为 CCW；材质 0 表示使用对象默认材质。 */
struct EditableFace {
    FaceId id = 0;
    std::vector<MeshCorner> corners;
    AssetId material = kInvalidAsset;
    bool operator==(const EditableFace&) const = default;
};

/** @brief 规范化无向边键，始终按端点 ID 排序；三角化对角线不属于源边。 */
struct EdgeKey {
    VertexId first;
    VertexId second;
    EdgeKey(VertexId a, VertexId b) : first(std::min(a, b)), second(std::max(a, b)) {}
    auto operator<=>(const EdgeKey&) const = default;
};

/** @brief 可复制的纯 CPU 候选快照；容器重排不会改变显式元素 ID。 */
struct EditableMesh {
    std::vector<EditableVertex> vertices;
    std::vector<EditableFace> faces;
    /** @brief 按稳定 ID 查找顶点，缺失返回 nullptr；指针仅在快照未修改时有效。 */
    [[nodiscard]] const EditableVertex* vertex(VertexId id) const;
    bool operator==(const EditableMesh&) const = default;
};

/** @brief 创建边长 1、原点居中、Y-up 的 8 点/6 四边面 Cube，带逐面硬法线与 UV。 */
[[nodiscard]] EditableMesh createEditableCube();
} // namespace mini3d::core::modeling
