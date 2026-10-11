/*
 * 模块名: ImageDocument
 * 功能概述: 在同目录私有暂存中完成编码字节、关闭后验证和 NewOnly 发布。
 * 对外接口: ImageDocument::write。
 * 依赖关系: Qt Core、Win32；不依赖 renderer、场景或色彩服务。
 * 输入输出: 编码 bytes、关闭后 validator、最终 guard 到新文件或明确失败。
 * 异常与错误: 验证、取消、写入或竞争失败只清理本次私有暂存。
 * 维护说明: guard 后紧接 MoveFileExW(...,0)，无事件处理或任意 callback。
 */
#pragma once
#include <QByteArray>
#include <QString>
#include <functional>

namespace mini3d::assets {
class ImageDocument final {
  public:
    using ClosedFileValidator = std::function<bool(const QString&, QString&)>;
    /** @brief 仅新文件；validator 必须在返回前关闭读拥有者，guard 必须重查当前许可。 */
    [[nodiscard]] static bool write(const QString& path, const QByteArray& bytes, QString& error,
                                    const ClosedFileValidator& validateClosedFile,
                                    const std::function<bool()>& beforeCommit,
                                    bool* overwriteDenied = nullptr);
};
} // namespace mini3d::assets
