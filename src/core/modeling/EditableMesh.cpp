/*
 * 模块名: EditableMesh
 * 功能概述: 实现稳定 ID 查询和原生立方体拓扑，不读取渲染顶点反推四边形。
 * 对外接口: EditableMesh::vertex、createEditableCube
 * 依赖关系: EditableMesh、标准数组与算法
 * 输入输出: 原点居中的共享拓扑顶点和独立面角属性。
 * 异常与错误: 缺失 ID 返回 nullptr；分配异常交由调用方事务处理。
 * 维护说明: 面环与一期 Cube 的外侧 CCW/UV 方向一致，保留 Y-up。
 */
#include "EditableMesh.h"

#include <array>

namespace mini3d::core::modeling {
const EditableVertex* EditableMesh::vertex(VertexId id) const {
    const auto found = std::find_if(vertices.begin(), vertices.end(), [id](const auto& item) {
        return item.id == id;
    });
    return found == vertices.end() ? nullptr : &*found;
}

EditableMesh createEditableCube() {
    EditableMesh mesh;
    mesh.vertices = {{1, {-0.5F, -0.5F, -0.5F}}, {2, {0.5F, -0.5F, -0.5F}},
                     {3, {0.5F, 0.5F, -0.5F}},   {4, {-0.5F, 0.5F, -0.5F}},
                     {5, {-0.5F, -0.5F, 0.5F}},  {6, {0.5F, -0.5F, 0.5F}},
                     {7, {0.5F, 0.5F, 0.5F}},    {8, {-0.5F, 0.5F, 0.5F}}};
    const std::array<std::array<VertexId, 4>, 6> loops{
        {{5, 6, 7, 8}, {2, 1, 4, 3}, {6, 2, 3, 7}, {1, 5, 8, 4}, {8, 7, 3, 4}, {1, 2, 6, 5}}};
    const std::array<glm::vec3, 6> normals{
        {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}}};
    const std::array<glm::vec3, 6> colors{{{0.22F, 0.58F, 0.95F},
                                           {0.15F, 0.38F, 0.72F},
                                           {0.95F, 0.42F, 0.18F},
                                           {0.72F, 0.24F, 0.18F},
                                           {0.98F, 0.72F, 0.18F},
                                           {0.32F, 0.24F, 0.18F}}};
    const std::array<glm::vec2, 4> uvs{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    CornerId nextCorner = 1;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        EditableFace face;
        face.id = i + 1;
        for (std::size_t j = 0; j < 4; ++j) {
            face.corners.push_back({nextCorner++, loops[i][j], uvs[j], normals[i], colors[i]});
        }
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}
} // namespace mini3d::core::modeling
