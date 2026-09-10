/*
 * 模块名: GpuMesh
 * 功能概述: 管理一个索引 Mesh 的 VAO、VBO、EBO 和绘制数量。
 * 对外接口: mini3d::renderer_gl::GpuMesh
 * 依赖关系: OpenGL 4.1 Core、MeshData
 * 输入输出: 输入 CPU MeshData，输出可通过 glDrawElements 绘制的 GPU Mesh。
 * 异常与错误: 空数据上传返回 false；无有效 Context 的清理会写入警告日志。
 * 维护说明: upload、draw、destroy 和析构清理必须发生在所属 Context 线程。
 */

#pragma once

#include "MeshData.h"

class QOpenGLFunctions_4_1_Core;

namespace mini3d::renderer_gl {

/**
 * @brief 独占一个 OpenGL VAO/VBO/EBO 组合。
 *
 * 对象不可复制；调用方必须在当前 Context 中上传、绘制和释放。
 */
class GpuMesh final {
  public:
    GpuMesh() = default;
    ~GpuMesh();

    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;

    /**
     * @brief 上传交错顶点与 32 位索引。
     * @param functions 当前 Context 的 OpenGL 4.1 Core 函数表。
     * @param meshData 非空 CPU Mesh 数据。
     * @return 成功创建 GPU 对象时返回 true。
     */
    [[nodiscard]] bool upload(QOpenGLFunctions_4_1_Core& functions, const MeshData& meshData);

    /** @brief 使用 GL_TRIANGLES 绘制全部索引。 */
    void draw() const;

    /** @brief 在当前 Context 中删除 VAO、VBO 和 EBO。 */
    void destroy();

    /** @brief 返回三个 GPU 对象和索引数量是否有效。 */
    [[nodiscard]] bool isValid() const noexcept;

  private:
    QOpenGLFunctions_4_1_Core* functions_ = nullptr;
    unsigned int vertexArray_ = 0;
    unsigned int vertexBuffer_ = 0;
    unsigned int indexBuffer_ = 0;
    int indexCount_ = 0;
};

} // namespace mini3d::renderer_gl
