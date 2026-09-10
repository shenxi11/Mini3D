/*
 * 模块名: MeshData
 * 功能概述: 定义 Renderer 上传 GPU 前使用的最小 CPU 顶点和索引数据。
 * 对外接口: mini3d::renderer_gl::MeshVertex、mini3d::renderer_gl::MeshData
 * 依赖关系: core::Aabb、GLM、C++ 标准容器
 * 输入输出: 输入位置、法线、颜色、UV 和索引，提供 GPU 上传数组与局部包围盒。
 * 异常与错误: 无运行时错误处理；数据合法性由创建者和 GpuMesh 上传入口检查。
 * 维护说明: 本结构不保存 OpenGL 句柄，不承担 Scene 或 Asset 身份职责。
 */

#pragma once

#include "core/MeshData.h"

namespace mini3d::renderer_gl {

// 兼容既有几何工厂和 GPU 上传接口，数据定义统一位于 Core。
using MeshVertex = core::MeshVertex;
using MeshData = core::MeshData;

} // namespace mini3d::renderer_gl
