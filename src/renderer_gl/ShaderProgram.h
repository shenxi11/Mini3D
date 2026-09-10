/*
 * 模块名: ShaderProgram
 * 功能概述: 管理一组 OpenGL Shader 的编译、链接、绑定和程序对象生命周期。
 * 对外接口: mini3d::renderer_gl::ShaderProgram
 * 依赖关系: OpenGL 4.1 Core 函数表、Qt Core 文件与日志能力
 * 输入输出: 输入资源路径中的 GLSL 源码，输出可绑定的 OpenGL Program。
 * 异常与错误: 文件读取、编译或链接失败时返回 false，并输出文件名和驱动日志。
 * 维护说明: create、destroy 和析构清理必须发生在有效 OpenGL Context 中。
 */

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class QByteArray;
class QOpenGLFunctions_4_1_Core;
class QString;

namespace mini3d::renderer_gl {

/**
 * @brief 拥有一个 OpenGL Program，并报告完整 Shader 构建错误。
 *
 * 对象不可复制；调用方应在当前 Context 有效时调用 create 和 destroy。
 */
class ShaderProgram final {
  public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    /**
     * @brief 从两个 Qt 资源路径编译并链接 Program。
     * @param functions 当前 Context 对应的 OpenGL 4.1 Core 函数表。
     * @param vertexShaderPath Vertex Shader 资源路径。
     * @param fragmentShaderPath Fragment Shader 资源路径。
     * @return 两个 Shader 均编译且 Program 链接成功时返回 true。
     */
    [[nodiscard]] bool create(QOpenGLFunctions_4_1_Core& functions, const QString& vertexShaderPath,
                              const QString& fragmentShaderPath);

    /** @brief 在当前 Context 中删除所拥有的 Program。 */
    void destroy();

    /** @brief 绑定当前 Program；对象无效时不执行 OpenGL 调用。 */
    void bind() const;

    /** @brief 解除当前 Program 绑定。 */
    void release() const;

    /**
     * @brief 向当前 Program 写入一个列主序 mat4 Uniform。
     * @param name Shader 中的 Uniform 名称。
     * @param value GLM 列主序矩阵。
     */
    void setUniformMatrix4(const char* name, const glm::mat4& value) const;

    /** @brief 向已绑定 Program 写入颜色或方向向量。 */
    void setUniformVector3(const char* name, const glm::vec3& value) const;
    /** @brief 向已绑定 Program 写入整数、布尔开关或采样器单元。 */
    void setUniformInt(const char* name, int value) const;

    /** @brief 返回 Program 是否已经成功链接。 */
    [[nodiscard]] bool isValid() const noexcept;

  private:
    [[nodiscard]] unsigned int compileShader(unsigned int shaderType, const QByteArray& source,
                                             const QString& sourcePath) const;
    [[nodiscard]] QByteArray shaderInfoLog(unsigned int shaderId) const;
    [[nodiscard]] QByteArray programInfoLog(unsigned int programId) const;

    QOpenGLFunctions_4_1_Core* functions_ = nullptr;
    unsigned int programId_ = 0;
};

} // namespace mini3d::renderer_gl
