/*
 * 模块名: BridgeDescriptor
 * 功能概述: 以当前进程用户的保护 DACL 创建认证文件并精确回收。
 * 对外接口: BridgeDescriptor.h。
 * 依赖关系: Windows advapi32/bcrypt、Qt JSON；无非 Windows 开启分支。
 * 输入输出: 认证元数据到受限普通文件；stop 核验身份后删除原始句柄。
 * 异常与错误: 任一系统检查失败关闭权限路径，保留无法核验的文件。
 * 维护说明: 常驻句柄无 DELETE 权限；停止时用核验过的独立句柄删除具体对象。
 */
#include "BridgeDescriptor.h"

#include "WinLocalSecurity.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>
#include <iterator>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace mini3d::editor::automation {
namespace {
#ifdef Q_OS_WIN
using winsecurity::systemError;
QByteArray fileIdentity(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info))
        return {};
    QByteArray identity;
    for (const auto value : {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow})
        identity.append(reinterpret_cast<const char*>(&value), sizeof(value));
    return identity;
}
bool ordinaryParent(const QString& path, QString& error) {
    const auto local = QDir::fromNativeSeparators(path);
    if (local.size() < 4 || !local[0].isLetter() || local[1] != ':' || local[2] != '/' ||
        local.mid(2).contains(':')) {
        error = QStringLiteral("描述文件必须是本机绝对普通路径。");
        return false;
    }
    const auto drive = QDir::toNativeSeparators(local.left(3));
    const auto driveType = GetDriveTypeW(reinterpret_cast<LPCWSTR>(drive.utf16()));
    if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
        error = QStringLiteral("认证描述文件禁止网络驱动器或未知卷类型。");
        return false;
    }
    const auto directory = QFileInfo(QDir::cleanPath(local)).absolutePath();
    auto parent = directory;
    while (true) {
        const auto native = QDir::toNativeSeparators(parent);
        const auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            error = QStringLiteral("描述文件祖先目录不存在或经过链接。");
            return false;
        }
        const auto next = QFileInfo(parent).absolutePath();
        if (next == parent)
            break;
        parent = next;
    }
    const auto native = QDir::toNativeSeparators(directory);
    wchar_t volumePath[MAX_PATH + 1]{};
    DWORD filesystemFlags = 0;
    if (!GetVolumePathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), volumePath,
                           DWORD(std::size(volumePath))) ||
        !GetVolumeInformationW(volumePath, nullptr, 0, nullptr, nullptr, &filesystemFlags,
                               nullptr, 0)) {
        error = systemError(QStringLiteral("无法核验认证文件所在卷的 ACL 能力"));
        return false;
    }
    return winsecurity::localAclVolume(GetDriveTypeW(volumePath), filesystemFlags, error);
}
QByteArray randomBytes(int byteCount, QString& error) {
    QByteArray bytes(byteCount, '\0');
    const auto status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(bytes.data()),
                                       ULONG(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        error = QStringLiteral("Windows 系统随机源不可用。");
        return {};
    }
    return bytes;
}
#endif
} // namespace
BridgeDescriptor::~BridgeDescriptor() {
    QString ignored;
    removeOwned(ignored);
}
bool BridgeDescriptor::canEnable(QString& error) {
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = systemError(QStringLiteral("无法检查进程提升状态"));
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool read =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    if (!read) {
        error = systemError(QStringLiteral("无法检查进程提升状态"));
        return false;
    }
    if (elevation.TokenIsElevated) {
        error = QStringLiteral("提升权限进程禁止开启自动化桥。");
        return false;
    }
    return true;
#else
    error = QStringLiteral("当前桥只支持已批准的 Windows 同用户命名管道。");
    return false;
#endif
}
QString BridgeDescriptor::randomHex(int byteCount, QString& error) {
#ifdef Q_OS_WIN
    return QString::fromLatin1(randomBytes(byteCount, error).toHex());
#else
    Q_UNUSED(byteCount);
    error = QStringLiteral("当前平台没有已批准的认证随机源。");
    return {};
#endif
}
QString BridgeDescriptor::randomUuid(QString& error) {
#ifdef Q_OS_WIN
    auto bytes = randomBytes(16, error);
    if (bytes.isEmpty())
        return {};
    bytes[6] = char((static_cast<unsigned char>(bytes[6]) & 0x0f) | 0x40);
    bytes[8] = char((static_cast<unsigned char>(bytes[8]) & 0x3f) | 0x80);
    return QUuid::fromRfc4122(bytes).toString(QUuid::WithoutBraces).toLower();
#else
    error = QStringLiteral("当前平台没有已批准的身份随机源。");
    return {};
#endif
}
bool BridgeDescriptor::validatePath(const QString& path, QString& error) {
#ifdef Q_OS_WIN
    return ordinaryParent(path, error);
#else
    Q_UNUSED(path);
    error = QStringLiteral("当前平台没有已批准的本机 ACL 路径。");
    return false;
#endif
}
bool BridgeDescriptor::create(const QString& path, const QJsonObject& content, QString& error) {
    if (handle_) {
        error = QStringLiteral("本描述文件所有者已有活动文件。");
        return false;
    }
#ifdef Q_OS_WIN
    if (!canEnable(error) || !ordinaryParent(path, error))
        return false;
    const auto absolute = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const auto native = QDir::toNativeSeparators(absolute);
    const auto descriptor = winsecurity::currentUserDescriptor(error);
    if (!descriptor)
        return false;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                   GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_DELETE, &attributes, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    LocalFree(descriptor);
    if (handle == INVALID_HANDLE_VALUE) {
        error = systemError(QStringLiteral("无法创建新认证描述文件"));
        return false;
    }
    handle_ = handle;
    path_ = absolute;
    content_ = QJsonDocument(content).toJson(QJsonDocument::Compact);
    identity_ = fileIdentity(handle);
    DWORD written = 0;
    const bool protectedFile = winsecurity::verifyFileSecurity(handle, error);
    const bool saved = protectedFile && !identity_.isEmpty() &&
                       WriteFile(handle, content_.constData(), DWORD(content_.size()), &written,
                                 nullptr) &&
                       written == DWORD(content_.size()) && FlushFileBuffers(handle);
    if (!saved) {
        if (protectedFile)
            error = systemError(QStringLiteral("无法写入受限认证描述文件"));
        // ReOpenFile 指向自有原始对象；清理失败创建时也不删除被替换的路径目标。
        const auto cleanup = ReOpenFile(handle, DELETE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
        if (cleanup != INVALID_HANDLE_VALUE) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(cleanup, FileDispositionInfo, &disposition,
                                       sizeof(disposition));
            CloseHandle(cleanup);
        }
        CloseHandle(handle);
        handle_ = nullptr;
        path_.clear();
        content_.clear();
        identity_.clear();
        return false;
    }
    return true;
#else
    Q_UNUSED(path);
    Q_UNUSED(content);
    error = QStringLiteral("当前平台禁止创建本机桥认证描述文件。");
    return false;
#endif
}
bool BridgeDescriptor::removeOwned(QString& error) {
    if (!handle_)
        return true;
    bool removed = false;
#ifdef Q_OS_WIN
    const auto owned = static_cast<HANDLE>(handle_);
    const auto native = QDir::toNativeSeparators(path_);
    const auto current = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ | DELETE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    bool same = false;
    if (current != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        QByteArray bytes(content_.size(), '\0');
        DWORD read = 0;
        same = fileIdentity(current) == identity_ && GetFileSizeEx(current, &size) &&
               size.QuadPart == content_.size() &&
               ReadFile(current, bytes.data(), DWORD(bytes.size()), &read, nullptr) &&
               read == DWORD(bytes.size()) && bytes == content_;
    }
    if (same) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        removed = SetFileInformationByHandle(current, FileDispositionInfo, &disposition,
                                            sizeof(disposition));
        if (!removed)
            error = systemError(QStringLiteral("无法删除本桥的受限描述文件"));
    } else {
        error = current == INVALID_HANDLE_VALUE
                    ? systemError(QStringLiteral("无法取得认证文件的精确删除句柄，保留该文件"))
                    : QStringLiteral("认证描述文件身份或内容无法核验，未删除路径目标。");
    }
    if (current != INVALID_HANDLE_VALUE)
        CloseHandle(current);
    CloseHandle(owned);
#else
    error = QStringLiteral("无法核验当前平台的认证描述文件身份。");
#endif
    handle_ = nullptr;
    path_.clear();
    content_.clear();
    identity_.clear();
    return removed;
}
QString BridgeDescriptor::path() const {
    return path_;
}
} // namespace mini3d::editor::automation
