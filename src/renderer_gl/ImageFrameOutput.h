/*
 * 模块名: ImageFrameOutput
 * 功能概述: 将只读母版编码 PNG/FLOAT32 EXR，并独立验实际关闭文件的完整出处与像素。
 * 对外接口: ImageFrameOutput::encode、validateFile、EncodedImage。
 * 依赖关系: ImageFrame、固定 OCIO、Qt PNG、OpenEXR；不发布文件。
 * 输入输出: 母版到编码字节；实际文件到验证结果。
 * 异常与错误: 色彩身份、不可保真 PNG、非法格式或解码矛盾拒绝。
 * 维护说明: EXR 文件对象显式 numThreads=0；PNG alpha 不执行 OCIO。
 */
#pragma once
#include "ImageFrame.h"
#include <optional>

namespace mini3d::renderer_gl {
/** @brief 本次编码字节与 PNG 期望 RGBA；不持有可写母版或发布权限。 */
struct EncodedImage {
    ImageFileFormat format = ImageFileFormat::Png;
    QByteArray bytes;
    QByteArray rgba8;
};
class ImageFrameOutput final {
  public:
    /** @brief PNG 在线性域去关联后一次 OCIO；EXR 保存五 FLOAT plane 原位值。 */
    [[nodiscard]] static std::optional<EncodedImage>
    encode(const ImageFrame& frame, ImageFileFormat format,
           const OcioDisplayTransform& color, QString& error);
    /** @brief 读取实际关闭文件，独立核头/标签/所有像素；全部读拥有者在返回前关闭。 */
    [[nodiscard]] static bool validateFile(const QString& path, const ImageFrame& frame,
                                           const EncodedImage& encoded, QString& error);
};
} // namespace mini3d::renderer_gl
