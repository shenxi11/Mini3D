/*
 * 模块名: ShaderProgram
 * 功能概述: 实现 GLSL 文件读取、Shader 编译、Program 链接和 OpenGL 对象清理。
 * 对外接口: mini3d::renderer_gl::ShaderProgram
 * 依赖关系: OpenGL 4.1 Core、Qt Core
 * 输入输出: 输入 UTF-8 GLSL 源码资源，输出 OpenGL Program 及可诊断日志。
 * 异常与错误: 失败时释放本轮已创建对象，并通过 Qt 日志输出源码路径和驱动信息。
 * 维护说明: 不缓存 Uniform；只承担 Program 的单一生命周期职责。
 */

#include "ShaderProgram.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QString>
#include <glm/gtc/type_ptr.hpp>

namespace mini3d::renderer_gl {
namespace {

[[nodiscard]] QByteArray readShaderSource(const QString& sourcePath) {
    QFile sourceFile(sourcePath);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        qCritical().noquote() << QStringLiteral("无法读取着色器源码“%1”：%2")
                                     .arg(sourcePath, sourceFile.errorString());
        return {};
    }

    return sourceFile.readAll();
}

} // namespace

ShaderProgram::~ShaderProgram() {
    destroy();
}

bool ShaderProgram::create(QOpenGLFunctions_4_1_Core& functions, const QString& vertexShaderPath,
                           const QString& fragmentShaderPath) {
    destroy();
    functions_ = &functions;

    const QByteArray vertexSource = readShaderSource(vertexShaderPath);
    const QByteArray fragmentSource = readShaderSource(fragmentShaderPath);
    if (vertexSource.isEmpty() || fragmentSource.isEmpty()) {
        functions_ = nullptr;
        return false;
    }

    const unsigned int vertexShader =
        compileShader(GL_VERTEX_SHADER, vertexSource, vertexShaderPath);
    if (vertexShader == 0) {
        functions_ = nullptr;
        return false;
    }

    const unsigned int fragmentShader =
        compileShader(GL_FRAGMENT_SHADER, fragmentSource, fragmentShaderPath);
    if (fragmentShader == 0) {
        functions_->glDeleteShader(vertexShader);
        functions_ = nullptr;
        return false;
    }

    programId_ = functions_->glCreateProgram();
    functions_->glAttachShader(programId_, vertexShader);
    functions_->glAttachShader(programId_, fragmentShader);
    functions_->glLinkProgram(programId_);

    int linkSucceeded = GL_FALSE;
    functions_->glGetProgramiv(programId_, GL_LINK_STATUS, &linkSucceeded);
    if (linkSucceeded != GL_TRUE) {
        qCritical().noquote() << QStringLiteral("着色器程序链接失败（“%1”、“%2”）：\n%3")
                                     .arg(vertexShaderPath, fragmentShaderPath,
                                          QString::fromUtf8(programInfoLog(programId_)));
        functions_->glDeleteProgram(programId_);
        programId_ = 0;
    }

    functions_->glDeleteShader(vertexShader);
    functions_->glDeleteShader(fragmentShader);

    if (programId_ == 0) {
        functions_ = nullptr;
        return false;
    }

    return true;
}

void ShaderProgram::destroy() {
    if (programId_ == 0) {
        functions_ = nullptr;
        return;
    }

    if (functions_ == nullptr || QOpenGLContext::currentContext() == nullptr) {
        qWarning() << "无法释放着色器程序：当前没有 OpenGL 上下文。";
        return;
    }

    functions_->glDeleteProgram(programId_);
    programId_ = 0;
    functions_ = nullptr;
}

void ShaderProgram::bind() const {
    if (isValid()) {
        functions_->glUseProgram(programId_);
    }
}

void ShaderProgram::release() const {
    if (functions_ != nullptr) {
        functions_->glUseProgram(0);
    }
}

void ShaderProgram::setUniformMatrix4(const char* name, const glm::mat4& value) const {
    if (!isValid()) {
        return;
    }

    const int location = functions_->glGetUniformLocation(programId_, name);
    if (location < 0) {
        qWarning().noquote()
            << QStringLiteral("未找到着色器 uniform 变量“%1”。").arg(QString::fromLatin1(name));
        return;
    }

    functions_->glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
}

void ShaderProgram::setUniformVector3(const char* name, const glm::vec3& value) const {
    if (isValid()) {
        functions_->glUniform3fv(functions_->glGetUniformLocation(programId_, name), 1,
                                 glm::value_ptr(value));
    }
}

void ShaderProgram::setUniformInt(const char* name, int value) const {
    if (isValid()) {
        functions_->glUniform1i(functions_->glGetUniformLocation(programId_, name), value);
    }
}

bool ShaderProgram::isValid() const noexcept {
    return functions_ != nullptr && programId_ != 0;
}

unsigned int ShaderProgram::compileShader(unsigned int shaderType, const QByteArray& source,
                                          const QString& sourcePath) const {
    const unsigned int shaderId = functions_->glCreateShader(shaderType);
    const char* sourceData = source.constData();
    const int sourceLength = static_cast<int>(source.size());
    functions_->glShaderSource(shaderId, 1, &sourceData, &sourceLength);
    functions_->glCompileShader(shaderId);

    int compileSucceeded = GL_FALSE;
    functions_->glGetShaderiv(shaderId, GL_COMPILE_STATUS, &compileSucceeded);
    if (compileSucceeded == GL_TRUE) {
        return shaderId;
    }

    qCritical().noquote() << QStringLiteral("着色器编译失败（“%1”）：\n%2")
                                 .arg(sourcePath, QString::fromUtf8(shaderInfoLog(shaderId)));
    functions_->glDeleteShader(shaderId);
    return 0;
}

QByteArray ShaderProgram::shaderInfoLog(unsigned int shaderId) const {
    int logLength = 0;
    functions_->glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 1) {
        return QByteArrayLiteral("驱动未返回着色器日志。");
    }

    QByteArray log(logLength, '\0');
    int writtenLength = 0;
    functions_->glGetShaderInfoLog(shaderId, logLength, &writtenLength, log.data());
    log.resize(writtenLength);
    return log;
}

QByteArray ShaderProgram::programInfoLog(unsigned int programId) const {
    int logLength = 0;
    functions_->glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &logLength);
    if (logLength <= 1) {
        return QByteArrayLiteral("驱动未返回程序日志。");
    }

    QByteArray log(logLength, '\0');
    int writtenLength = 0;
    functions_->glGetProgramInfoLog(programId, logLength, &writtenLength, log.data());
    log.resize(writtenLength);
    return log;
}

} // namespace mini3d::renderer_gl
