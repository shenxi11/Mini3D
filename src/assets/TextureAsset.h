/*
 * 模块名: TextureAsset
 * 功能概述: 静态 glTF 导入与 CPU 资源管理的 TextureAsset 数据/接口。
 * 对外接口: TextureAsset
 * 依赖关系: C++/GLM、Qt Gui
 * 输入输出: 输入资源描述，输出稳定资源引用或导入结果。
 * 异常与错误: 无效导入通过 error 返回，不执行 GPU 操作。
 * 维护说明: 使用 AssetId 关联，避免资源层反向依赖 Renderer。
 */
#pragma once
#include <QImage>
namespace mini3d::assets {
/** @brief glTF/OpenGL 共用的标准采样枚举值；此结构不执行图形 API。 */
struct TextureSampler {
    int minFilter{9987};
    int magFilter{9729};
    int wrapS{10497};
    int wrapT{10497};
};
/** @brief 顶部首行的已解码 CPU 图片；GPU 上传时统一翻转，glTF UV 已转为底部原点。 */
struct TextureAsset {
    QImage image;
    TextureSampler sampler;
};
} // namespace mini3d::assets
