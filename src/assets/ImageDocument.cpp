/*
 * 模块名: ImageDocument
 * 功能概述: 关闭写句柄和验证读句柄后原子不替换发布本次图像。
 * 对外接口: ImageDocument.h。
 * 依赖关系: Qt 同目录 QTemporaryFile、Win32 MoveFileExW。
 * 输入输出: 编码 bytes 到新目标，失败保留已有/竞争文件字节。
 * 异常与错误: 操作失败返回 false；异常展开时 RAII 只清理本次暂存。
 * 维护说明: native 路径在最后 guard 前准备，不使用覆盖或直接写回退。
 */
#include "ImageDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryFile>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mini3d::assets {
bool ImageDocument::write(const QString& path, const QByteArray& bytes, QString& error,
                           const ClosedFileValidator& validateClosedFile,
                           const std::function<bool()>& beforeCommit, bool* overwriteDenied) {
    if (overwriteDenied) *overwriteDenied = false;
    if (path.isEmpty() || bytes.isEmpty() || !validateClosedFile || !beforeCommit) {
        error = QStringLiteral("图像发布必须提供 bytes、关闭后验证与最终提交守卫。");
        return false;
    }
    const QFileInfo target(path);
    QString temporaryPath;
    {
        QTemporaryFile file(target.absoluteDir().filePath(QStringLiteral(".mini3d-image-XXXXXX")));
        if (!file.open() || file.write(bytes) != bytes.size() || !file.flush()) {
            error = QStringLiteral("图像私有暂存写入或 flush 失败。");
            return false;
        }
        temporaryPath = file.fileName();
        file.setAutoRemove(false);
        file.close();
    }
    auto cleanup = qScopeGuard([&] { QFile::remove(temporaryPath); });
    if (!validateClosedFile(temporaryPath, error)) return false;
    const auto source = QDir::toNativeSeparators(temporaryPath).toStdWString();
    const auto destination = QDir::toNativeSeparators(target.absoluteFilePath()).toStdWString();
    if (!beforeCommit()) {
        error = QStringLiteral("图像最终提交已拒绝。");
        return false;
    }
    if (!MoveFileExW(source.c_str(), destination.c_str(), 0)) {
        const auto failure = GetLastError();
        const bool exists = failure == ERROR_ALREADY_EXISTS || failure == ERROR_FILE_EXISTS;
        if (overwriteDenied) *overwriteDenied = exists;
        error = exists ? QStringLiteral("图像目标已存在，禁止覆盖。")
                       : QStringLiteral("图像 NewOnly 发布失败：系统错误 %1。").arg(failure);
        return false;
    }
    cleanup.dismiss();
    error.clear();
    return true;
}
} // namespace mini3d::assets
