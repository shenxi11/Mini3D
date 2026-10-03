/*
 * 模块名: MeshDerivation
 * 功能概述: 三角化合法多边形并生成现有 MeshData 和稳定来源映射。
 * 对外接口: DerivedMesh、MeshDerivationResult、deriveMesh
 * 依赖关系: EditableMesh、Core MeshData，无 Qt/GL
 * 输入输出: 源网格到面角拆点、三角形、原始点/角/面身份及逐三角材质。
 * 异常与错误: 不支持的投影形状或三角化失败返回原因，无部分发布。
 * 维护说明: 投影耳切保留凸/凹简单多边形绕序；不是全局自交检测或场景安装入口。
 */
#pragma once

#include "EditableMesh.h"
#include "core/MeshData.h"

#include <string>

namespace mini3d::core {
class Scene;
}

namespace mini3d::core::modeling {
/** @brief 一个渲染顶点来源于一个源面角，多个角可引用同一拓扑点。 */
struct RenderVertexSource {
    VertexId vertex;
    CornerId corner;
};
/** @brief 与 indices 每组三个索引一一对应；材质默认值的含义与源面一致。 */
struct RenderTriangleSource {
    FaceId face;
    AssetId material;
};
struct DerivedMesh {
    MeshData mesh;
    std::vector<RenderVertexSource> vertexSources;
    std::vector<RenderTriangleSource> triangleSources;
};
struct MeshDerivationResult {
    std::optional<DerivedMesh> derived;
    std::string error;
};

/** @brief 离线校验/三角化，保持面角 UV/颜色/硬法线；无硬法线时计算面法线。 */
[[nodiscard]] MeshDerivationResult deriveMesh(const EditableMesh& source);
/** @brief 仅 Scene 可复用自身已验证快照；变换必须保持面环、身份及属性布局。 */
class MeshDerivation {
  private:
    friend class mini3d::core::Scene;
    [[nodiscard]] static MeshDerivationResult
    deriveTransformed(const EditableMesh& source, const DerivedMesh& before,
                      const std::vector<std::size_t>& affectedFaces);
};
} // namespace mini3d::core::modeling
