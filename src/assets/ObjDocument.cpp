/*
 * 模块名: ObjDocument
 * 功能概述: 在同目录临时文件完成写入后提交 OBJ，不改变工程保存点。
 * 对外接口: ObjDocument::write
 * 依赖关系: Qt 同目录暂存文件、Win32 原子不替换发布。
 * 输入输出: UTF-8 OBJ 文本到用户指定文件。
 * 异常与错误: 文件操作失败返回错误，保留原目标。
 * 维护说明: 不使用直接写回退，不执行自动命名或隐式导出。
 */
#include "ObjDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryFile>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mini3d::assets {
namespace {
bool writeNewOnly(const QString& path, const std::string& text, QString& error,
                  const std::function<bool()>& beforeCommit, bool* overwriteDenied) {
    const QFileInfo target(path);
    QString temporaryPath;
    // 与场景另存相同：释放 QTemporaryFile 原生句柄后才执行同卷不替换发布。
    {
        QTemporaryFile file(target.absoluteDir().filePath(QStringLiteral(".mini3d-obj-XXXXXX")));
        if (!file.open() ||
            file.write(text.data(), static_cast<qint64>(text.size())) !=
                static_cast<qint64>(text.size()) ||
            !file.flush()) {
            error = QStringLiteral("OBJ 临时文件写入失败。");
            return false;
        }
        temporaryPath = file.fileName();
        file.setAutoRemove(false);
    }
    auto cleanup = qScopeGuard([&] {
        QFile::remove(temporaryPath);
    });
    const auto source = QDir::toNativeSeparators(temporaryPath).toStdWString();
    const auto destination = QDir::toNativeSeparators(target.absoluteFilePath()).toStdWString();
    if (beforeCommit && !beforeCommit()) {
        error = QStringLiteral("OBJ 文件提交已取消。");
        return false;
    }
    if (!MoveFileExW(source.c_str(), destination.c_str(), 0)) {
        const auto failure = GetLastError();
        const bool exists = failure == ERROR_ALREADY_EXISTS || failure == ERROR_FILE_EXISTS;
        if (overwriteDenied)
            *overwriteDenied = exists;
        error = exists ? QStringLiteral("OBJ 目标已存在，禁止覆盖。")
                       : QStringLiteral("OBJ 文件发布失败：系统错误 %1。").arg(failure);
        return false;
    }
    cleanup.dismiss();
    error.clear();
    return true;
}
} // namespace
bool ObjDocument::write(const QString& path, const std::string& text, QString& error,
                        const std::function<bool()>& beforeCommit, WriteMode mode,
                        bool* overwriteDenied) {
    if (overwriteDenied)
        *overwriteDenied = false;
    if (mode == WriteMode::NewOnly)
        return writeNewOnly(path, text, error, beforeCommit, overwriteDenied);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(text.data(), static_cast<qint64>(text.size())) !=
            static_cast<qint64>(text.size())) {
        error = QStringLiteral("OBJ 导出失败：%1").arg(file.errorString());
        return false;
    }
    if (beforeCommit && !beforeCommit()) {
        file.cancelWriting();
        error = QStringLiteral("OBJ 文件提交已取消。");
        return false;
    }
    if (!file.commit()) {
        error = QStringLiteral("OBJ 导出失败：%1").arg(file.errorString());
        return false;
    }
    error.clear();
    return true;
}
} // namespace mini3d::assets
