/*
 * 模块名: GpuTexture
 * 功能概述: 上传 RGBA8 图像并配置重复寻址、线性过滤与 mipmap。
 * 对外接口: GpuTexture
 * 依赖关系: QImage、Qt 日志、OpenGL 4.1 Core
 * 输入输出: 输入 CPU 图像，输出纹理单元 0 可采样的 GPU 对象。
 * 异常与错误: 空数据、超过驱动尺寸或 GL 上传错误返回 false。
 * 维护说明: 上传恢复绑定与像素对齐状态；暂不执行 sRGB/Gamma 转换。
 */
#include "GpuTexture.h"

#include <QDebug>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>

namespace mini3d::renderer_gl {

GpuTexture::~GpuTexture() {
    destroy();
}

bool GpuTexture::upload(QOpenGLFunctions_4_1_Core& functions, const QImage& image,
                        const assets::TextureSampler& sampler) {
    if (image.isNull()) {
        qWarning() << "纹理上传被拒绝：图像为空。";
        return false;
    }
    int maximumSize = 0;
    functions.glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumSize);
    if (image.width() > maximumSize || image.height() > maximumSize) {
        qWarning() << "纹理尺寸超出 GL_MAX_TEXTURE_SIZE 上限。";
        return false;
    }
    const QImage pixels = image.convertToFormat(QImage::Format_RGBA8888).mirrored(false, true);
    destroy();
    functions_ = &functions;
    int previousBinding = 0;
    int previousAlignment = 0;
    functions.glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousBinding);
    functions.glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    functions.glGenTextures(1, &textureId_);
    functions.glBindTexture(GL_TEXTURE_2D, textureId_);
    functions.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    functions.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels.width(), pixels.height(), 0, GL_RGBA,
                           GL_UNSIGNED_BYTE, pixels.constBits());
    functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sampler.wrapS);
    functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sampler.wrapT);
    functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampler.minFilter);
    functions.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampler.magFilter);
    functions.glGenerateMipmap(GL_TEXTURE_2D);
    functions.glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
    functions.glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previousBinding));
    const unsigned int error = functions.glGetError();
    if (error != GL_NO_ERROR) {
        qWarning() << "纹理上传失败，OpenGL 错误码：" << error;
        destroy();
        return false;
    }
    return isValid();
}

void GpuTexture::bind() const {
    if (isValid()) {
        functions_->glActiveTexture(GL_TEXTURE0);
        functions_->glBindTexture(GL_TEXTURE_2D, textureId_);
    }
}

void GpuTexture::destroy() {
    if (textureId_ == 0) {
        functions_ = nullptr;
        return;
    }
    if (QOpenGLContext::currentContext() == nullptr) {
        qWarning() << "无法释放 GPU 纹理：当前没有 OpenGL 上下文。";
        return;
    }
    functions_->glDeleteTextures(1, &textureId_);
    textureId_ = 0;
    functions_ = nullptr;
}

bool GpuTexture::isValid() const noexcept {
    return functions_ != nullptr && textureId_ != 0;
}

} // namespace mini3d::renderer_gl
