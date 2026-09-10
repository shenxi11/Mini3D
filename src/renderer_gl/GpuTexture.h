/*
 * 模块名: GpuTexture
 * 功能概述: 独占二维 RGBA 纹理，统一图像方向和采样参数。
 * 对外接口: GpuTexture::upload、bind、destroy、isValid
 * 依赖关系: Qt Gui、OpenGL 4.1 Core
 * 输入输出: 输入顶部为首行的 QImage，输出 UV 原点位于左下的纹理。
 * 异常与错误: 空图像或上传失败返回 false 并记录日志。
 * 维护说明: 所有 GPU 操作必须位于创建纹理的当前 Context；不可复制。
 */
#pragma once
#include "assets/TextureAsset.h"

class QImage;
class QOpenGLFunctions_4_1_Core;

namespace mini3d::renderer_gl {

/** @brief 管理单张不透明基础颜色纹理；析构前须使所属 Context 当前。 */
class GpuTexture final {
  public:
    GpuTexture() = default;
    ~GpuTexture();
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    /** @brief 转为 RGBA8、垂直翻转一次并生成 mipmap；空图像不替换原纹理。 */
    [[nodiscard]] bool upload(QOpenGLFunctions_4_1_Core& functions, const QImage& image,
                              const assets::TextureSampler& sampler = {});
    /** @brief 绑定到纹理单元 0，供当前材质使用。 */
    void bind() const;
    /** @brief 在当前 Context 中释放纹理；可重复调用。 */
    void destroy();
    /** @brief 返回是否持有已上传纹理。 */
    [[nodiscard]] bool isValid() const noexcept;

  private:
    QOpenGLFunctions_4_1_Core* functions_ = nullptr;
    unsigned int textureId_ = 0;
};

} // namespace mini3d::renderer_gl
