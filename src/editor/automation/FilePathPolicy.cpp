/*
 * 模块名: FilePathPolicy
 * 功能概述: 校验路径段和所有既有祖先，拒绝通过路径别名扩大访问范围。
 * 对外接口: FilePathPolicy.h。
 * 依赖关系: Win32 GetFileAttributesW/GetDriveTypeW、Qt 文件路径。
 * 输入输出: 启动根与请求路径到固定授权结果。
 * 异常与错误: 失败只提供诊断，不打开目标文件或改变权限。
 * 维护说明: 必须先检查原路径祖先再做边界判断，不能先 canonical 隐藏链接。
 */
#include "FilePathPolicy.h"

#include <QDir>
#include <QRegularExpression>
#define NOMINMAX
#include <Windows.h>

namespace mini3d::editor::automation {
namespace {
bool reject(QString& error, const QString& reason) {
    error = reason;
    return false;
}
std::optional<QString> ordinaryPath(const QString& input, QString& error) {
    const auto path = QDir::fromNativeSeparators(input);
    static const QRegularExpression absolute(QStringLiteral("^[a-zA-Z]:/"));
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (!absolute.match(path).hasMatch() || path.mid(2).contains(':')) {
        reject(error, QStringLiteral("仅允许本机盘符绝对路径；拒绝 UNC、设备路径和 ADS。"));
        return std::nullopt;
    }
    for (const auto ch : path) {
        if (ch.unicode() < 32 || QStringLiteral("<>\"|?*").contains(ch)) {
            reject(error, QStringLiteral("路径包含非法字符。"));
            return std::nullopt;
        }
    }
    const auto components = path.mid(3).split('/', Qt::SkipEmptyParts);
    for (const auto& component : components) {
        if (component == "." || component == "..")
            continue;
        if (component.endsWith('.') ||
            component.endsWith(' ') || reserved.match(component).hasMatch()) {
            reject(error, QStringLiteral("路径包含设备名称或不明确的末尾字符。"));
            return std::nullopt;
        }
    }
    const auto drive = path.left(3).toStdWString();
    const auto driveType = GetDriveTypeW(drive.c_str());
    if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
        reject(error, QStringLiteral("批准路径必须位于本机普通磁盘。"));
        return std::nullopt;
    }
    return QDir::cleanPath(path);
}
bool inspectAncestors(const QString& path, bool allowMissingLeaf, bool requireDirectory,
                      QString& error, bool allowExistingFile = false) {
    QString current = path.left(3);
    const auto components = path.mid(3).split('/', Qt::SkipEmptyParts);
    // 盘符根也可能是挂载点，不跳过其属性检查。
    for (int index = -1; index < components.size(); ++index) {
        if (index >= 0)
            current = QDir(current).filePath(components[index]);
        const auto native = QDir::toNativeSeparators(current).toStdWString();
        const auto attributes = GetFileAttributesW(native.c_str());
        const bool leaf = index == components.size() - 1;
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto failure = GetLastError();
            if (leaf && allowMissingLeaf &&
                (failure == ERROR_FILE_NOT_FOUND || failure == ERROR_PATH_NOT_FOUND))
                return true;
            return reject(error, QStringLiteral("路径或现有父目录不可访问。"));
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            return reject(error, QStringLiteral("路径不得经过符号链接、联接或重解析点。"));
        const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!leaf || requireDirectory) {
            if (!directory)
                return reject(error, QStringLiteral("批准根及父路径必须是普通目录。"));
        } else if (allowMissingLeaf && !allowExistingFile) {
            return reject(error, QStringLiteral("目标已存在，禁止另存覆盖。"));
        } else if (directory || (attributes & FILE_ATTRIBUTE_DEVICE)) {
            return reject(error, QStringLiteral("目标必须是普通文件。"));
        }
    }
    return true;
}
bool underRoots(const QString& path, const QStringList& roots, QString& error) {
    for (const auto& root : roots) {
        const auto prefix = root.endsWith('/') ? root : root + '/';
        // Win32 的文件名大小写比较不等于 Unicode casefold（例如 K 与 Kelvin 符号）。
        if (path.size() >= prefix.size() &&
            CompareStringOrdinal(reinterpret_cast<LPCWCH>(path.utf16()), int(prefix.size()),
                                 reinterpret_cast<LPCWCH>(prefix.utf16()), int(prefix.size()),
                                 TRUE) == CSTR_EQUAL)
            return true;
    }
    return reject(error, QStringLiteral("目标不在启动时批准的根目录内。"));
}
} // namespace

std::optional<FilePathPolicy> FilePathPolicy::create(const QStringList& readRoots,
                                                     const QStringList& writeRoots,
                                                     QString& error) {
    FilePathPolicy result;
    const auto prepare = [&](const QStringList& inputs, QStringList& outputs) {
        for (const auto& input : inputs) {
            const auto path = ordinaryPath(input, error);
            if (!path || !inspectAncestors(QDir::fromNativeSeparators(input), false, true, error))
                return false;
            if (!outputs.contains(*path))
                outputs.push_back(*path);
        }
        return true;
    };
    if (!prepare(readRoots, result.readRoots_) || !prepare(writeRoots, result.writeRoots_))
        return std::nullopt;
    error.clear();
    return result;
}
bool FilePathPolicy::authorizeRead(const QString& input, QString& error) const {
    const auto path = ordinaryPath(input, error);
    if (!path || !underRoots(*path, readRoots_, error) ||
        !inspectAncestors(QDir::fromNativeSeparators(input), false, false, error))
        return false;
    error.clear();
    return true;
}
bool FilePathPolicy::authorizeNewFile(const QString& input, QString& error) const {
    const auto path = ordinaryPath(input, error);
    if (!path || !underRoots(*path, writeRoots_, error) ||
        !inspectAncestors(QDir::fromNativeSeparators(input), true, false, error))
        return false;
    error.clear();
    return true;
}
bool FilePathPolicy::authorizeWrite(const QString& input, QString& error) const {
    const auto path = ordinaryPath(input, error);
    if (!path || !underRoots(*path, writeRoots_, error) ||
        !inspectAncestors(QDir::fromNativeSeparators(input), true, false, error, true))
        return false;
    error.clear();
    return true;
}
assets::FileReadPolicy FilePathPolicy::readPolicy() const {
    return [policy = *this](const QString& path, QString& error) {
        return policy.authorizeRead(path, error);
    };
}
} // namespace mini3d::editor::automation
