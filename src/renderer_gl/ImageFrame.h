/*
 * 模块名: ImageFrame
 * 功能概述: 移动持有本次真实浮点母版及完整出处，供预览比较和正式输出只读消费。
 * 对外接口: ImageFrame、ImageFrameStamp、isAllowedImageOutputDerivation。
 * 依赖关系: FrozenStateRenderer、Qt Core；不执行 GL 或文件写入。
 * 输入输出: 本次渲染像素到不可变母版与 compact UTF-8 出处。
 * 异常与错误: 来源、尺寸或非有限载荷不符返回空值；分配异常传播。
 * 维护说明: alpha 与 coverage 分开；不得给旧像素补贴本次 frozen 来源。
 */
#pragma once

#include "FrozenStateRenderer.h"

#include <QByteArray>
#include <memory>

namespace mini3d::renderer_gl {
enum class ImageFileFormat { Png, Exr };

/** @brief 本消费者独立单调帧身份及真实设备消费出处；数值矩阵保存在 pixels 中。 */
struct ImageFrameStamp {
    core::EvaluationSessionId sessionId;
    std::uint64_t frameId = 0;
    std::uint64_t contextGeneration = 0;
    std::uint64_t viewportRevision = 0;
    core::EntityId cameraEntity = 0;
    double devicePixelRatio = 1;
};

/** @brief 只允许完整 Preview 作者来源派生为同作者/资源/精确时间的独立 Frozen 输出。 */
[[nodiscard]] bool isAllowedImageOutputDerivation(const core::SourceIdentity& origin,
                                                  const core::SourceIdentity& output);

/** @brief 唯一母版拥有者；创建时移动 pixels，创建后只读，不向编码器暴露可写引用。 */
class ImageFrame final {
  public:
    /** @brief 核对实际母版与来源；Output 固定 1280×720/DPR1，失败返回 nullptr 和 error。 */
    [[nodiscard]] static std::shared_ptr<const ImageFrame>
    create(FrozenRenderPixels&& pixels, const core::SourceIdentity& origin,
           const ImageFrameStamp& stamp, QString& error);
    [[nodiscard]] const FrozenRenderPixels& pixels() const;
    [[nodiscard]] const core::SourceIdentity& originSource() const;
    [[nodiscard]] const ImageFrameStamp& stamp() const;
    /** @brief 完整 JSON：uint64 十进制字符串、binary64 位编码；实际文件语义单独声明。 */
    [[nodiscard]] QByteArray provenanceJson(ImageFileFormat format) const;

  private:
    ImageFrame(FrozenRenderPixels&& pixels, core::SourceIdentity origin, ImageFrameStamp stamp);
    FrozenRenderPixels pixels_;
    core::SourceIdentity originSource_;
    ImageFrameStamp stamp_;
};
} // namespace mini3d::renderer_gl
