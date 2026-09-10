/*
 * 模块名: PrimitiveFactory
 * 功能概述: 生成 Renderer 可直接上传的固定 Cube、Sphere 和 Plane CPU Mesh 数据。
 * 对外接口: mini3d::renderer_gl::PrimitiveFactory
 * 依赖关系: MeshData、GLM
 * 输入输出: 无外部输入，输出带位置、单位法线、颜色和三角形索引的 MeshData。
 * 异常与错误: 无异常；固定拓扑通过 Catch2 无窗口测试验证索引和绕序。
 * 维护说明: 只负责基础几何生成，不创建 GPU 资源，也不承担 Scene 或 Asset 身份。
 */

#pragma once

#include "MeshData.h"

namespace mini3d::renderer_gl {

/** @brief 提供首批固定基础几何的纯 CPU 构造函数。 */
class PrimitiveFactory final {
  public:
    PrimitiveFactory() = delete;

    /** @brief 创建边长为 1、中心位于原点的硬边 Cube。 */
    [[nodiscard]] static MeshData createCube();

    /** @brief 创建直径为 1、中心位于原点且极点无退化面的 UV Sphere。 */
    [[nodiscard]] static MeshData createSphere();

    /** @brief 创建边长为 1.2、位于 XZ 平面且朝向 +Y 的 Plane。 */
    [[nodiscard]] static MeshData createPlane();
};

} // namespace mini3d::renderer_gl
