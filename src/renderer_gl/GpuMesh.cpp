/*
 * 模块名: GpuMesh
 * 功能概述: 上传交错顶点和索引，配置顶点属性并执行索引绘制。
 * 对外接口: mini3d::renderer_gl::GpuMesh
 * 依赖关系: OpenGL 4.1 Core、Qt OpenGL Context
 * 输入输出: 输入 MeshData，输出 GPU Buffer 状态和三角形绘制命令。
 * 异常与错误: 数据为空或索引过多时拒绝上传；Context 缺失时保留句柄并报告。
 * 维护说明: 不拥有 CPU MeshData；GPU 对象只能在创建它们的 Context 中释放。
 */

#include "GpuMesh.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <cstddef>
#include <limits>

namespace mini3d::renderer_gl {

GpuMesh::~GpuMesh() {
    destroy();
}

bool GpuMesh::upload(QOpenGLFunctions_4_1_Core& functions, const MeshData& meshData) {
    if (meshData.vertices.empty() || meshData.indices.empty()) {
        qWarning() << "网格上传被拒绝：顶点或索引数据为空。";
        return false;
    }
    if (meshData.indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        qWarning() << "网格上传被拒绝：索引数量超出 OpenGL GLsizei 上限。";
        return false;
    }

    destroy();
    functions_ = &functions;
    indexCount_ = static_cast<int>(meshData.indices.size());

    functions_->glGenVertexArrays(1, &vertexArray_);
    functions_->glGenBuffers(1, &vertexBuffer_);
    functions_->glGenBuffers(1, &indexBuffer_);

    functions_->glBindVertexArray(vertexArray_);
    functions_->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    functions_->glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(meshData.vertices.size() * sizeof(MeshVertex)),
                             meshData.vertices.data(), GL_STATIC_DRAW);
    functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
    functions_->glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(meshData.indices.size() * sizeof(std::uint32_t)),
        meshData.indices.data(), GL_STATIC_DRAW);

    constexpr int kPositionAttribute = 0;
    constexpr int kNormalAttribute = 1;
    constexpr int kColorAttribute = 2;
    constexpr int kUvAttribute = 3;
    const int vertexStride = static_cast<int>(sizeof(MeshVertex));

    functions_->glEnableVertexAttribArray(kPositionAttribute);
    functions_->glVertexAttribPointer(
        kPositionAttribute, 3, GL_FLOAT, GL_FALSE, vertexStride,
        reinterpret_cast<const void*>(offsetof(MeshVertex, position)));
    functions_->glEnableVertexAttribArray(kNormalAttribute);
    functions_->glVertexAttribPointer(kNormalAttribute, 3, GL_FLOAT, GL_FALSE, vertexStride,
                                      reinterpret_cast<const void*>(offsetof(MeshVertex, normal)));
    functions_->glEnableVertexAttribArray(kColorAttribute);
    functions_->glVertexAttribPointer(kColorAttribute, 3, GL_FLOAT, GL_FALSE, vertexStride,
                                      reinterpret_cast<const void*>(offsetof(MeshVertex, color)));

    functions_->glEnableVertexAttribArray(kUvAttribute);
    functions_->glVertexAttribPointer(kUvAttribute, 2, GL_FLOAT, GL_FALSE, vertexStride,
                                      reinterpret_cast<const void*>(offsetof(MeshVertex, uv)));

    functions_->glBindVertexArray(0);
    functions_->glBindBuffer(GL_ARRAY_BUFFER, 0);
    functions_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return true;
}

void GpuMesh::draw() const {
    if (!isValid()) {
        return;
    }

    functions_->glBindVertexArray(vertexArray_);
    functions_->glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    functions_->glBindVertexArray(0);
}

void GpuMesh::destroy() {
    if (vertexArray_ == 0 && vertexBuffer_ == 0 && indexBuffer_ == 0) {
        functions_ = nullptr;
        indexCount_ = 0;
        return;
    }

    if (functions_ == nullptr || QOpenGLContext::currentContext() == nullptr) {
        qWarning() << "无法释放 GPU 网格：当前没有 OpenGL 上下文。";
        return;
    }

    if (indexBuffer_ != 0) {
        functions_->glDeleteBuffers(1, &indexBuffer_);
        indexBuffer_ = 0;
    }
    if (vertexBuffer_ != 0) {
        functions_->glDeleteBuffers(1, &vertexBuffer_);
        vertexBuffer_ = 0;
    }
    if (vertexArray_ != 0) {
        functions_->glDeleteVertexArrays(1, &vertexArray_);
        vertexArray_ = 0;
    }

    indexCount_ = 0;
    functions_ = nullptr;
}

bool GpuMesh::isValid() const noexcept {
    return functions_ != nullptr && vertexArray_ != 0 && vertexBuffer_ != 0 && indexBuffer_ != 0 &&
           indexCount_ > 0;
}

} // namespace mini3d::renderer_gl
