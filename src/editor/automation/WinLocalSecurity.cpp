/*
 * 模块名: WinLocalSecurity
 * 功能概述: 查询当前 Token SID 并核验实际保护 DACL 与本地卷能力。
 * 对外接口: WinLocalSecurity.h。
 * 依赖关系: Windows advapi32、Qt Core。
 * 输入输出: 原生卷/安全描述符数据到授权前置检查。
 * 异常与错误: 任一原生查询失败即拒绝，不按文件存在或进程名推测身份。
 * 维护说明: 该检查仅覆盖同用户本机边界，不扩大到远程或提升权限。
 */
#include "WinLocalSecurity.h"

#ifdef Q_OS_WIN
#include <QByteArray>
#include <aclapi.h>
#include <sddl.h>

namespace mini3d::editor::automation::winsecurity {
namespace {
QByteArray currentUserSid(QString& error) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        error = systemError(QStringLiteral("无法查询当前用户"));
        return {};
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray tokenData(qsizetype(size), '\0');
    if (size == 0 || !GetTokenInformation(token, TokenUser, tokenData.data(), size, &size)) {
        const auto code = GetLastError();
        CloseHandle(token);
        error = systemError(QStringLiteral("无法查询当前用户 SID"), code);
        return {};
    }
    const auto sid = reinterpret_cast<TOKEN_USER*>(tokenData.data())->User.Sid;
    QByteArray copy(reinterpret_cast<const char*>(sid), GetLengthSid(sid));
    CloseHandle(token);
    return copy;
}
} // namespace
QString systemError(const QString& action, DWORD code) {
    return action + QStringLiteral("（Windows error %1）。").arg(code);
}
bool localAclVolume(DWORD driveType, DWORD filesystemFlags, QString& error) {
    if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE) {
        error = QStringLiteral("认证描述文件禁止网络驱动器或未知卷类型。");
        return false;
    }
    if (!(filesystemFlags & FILE_PERSISTENT_ACLS)) {
        error = QStringLiteral("认证描述文件所在卷不支持持久文件 ACL。");
        return false;
    }
    return true;
}
PSECURITY_DESCRIPTOR currentUserDescriptor(QString& error) {
    const auto userSid = currentUserSid(error);
    if (userSid.isEmpty())
        return nullptr;
    LPWSTR sidText = nullptr;
    if (!ConvertSidToStringSidW(const_cast<char*>(userSid.constData()), &sidText)) {
        error = systemError(QStringLiteral("无法编码当前用户 SID"));
        return nullptr;
    }
    const auto sddl = QStringLiteral("D:P(A;;FA;;;%1)").arg(QString::fromWCharArray(sidText));
    LocalFree(sidText);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &descriptor, nullptr)) {
        error = systemError(QStringLiteral("无法生成受保护 DACL"));
        return nullptr;
    }
    return descriptor;
}
bool verifyCurrentUserDacl(PSECURITY_DESCRIPTOR descriptor, QString& error) {
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    if (!descriptor || !IsValidSecurityDescriptor(descriptor) ||
        !GetSecurityDescriptorControl(descriptor, &control, &revision) ||
        !(control & SE_DACL_PROTECTED) ||
        !GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) || !present ||
        !dacl || dacl->AceCount != 1) {
        error = QStringLiteral("认证对象没有仅当前用户的受保护 DACL。");
        return false;
    }
    void* rawAce = nullptr;
    if (!GetAce(dacl, 0, &rawAce)) {
        error = systemError(QStringLiteral("无法读取认证对象的实际 ACE"));
        return false;
    }
    const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(rawAce);
    const auto sid = const_cast<DWORD*>(&ace->SidStart);
    const auto userSid = currentUserSid(error);
    if (userSid.isEmpty())
        return false;
    if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || ace->Header.AceFlags != 0 ||
        ace->Mask != FILE_ALL_ACCESS || !IsValidSid(sid) ||
        !EqualSid(sid, const_cast<char*>(userSid.constData()))) {
        error = QStringLiteral("认证对象实际权限并非只授予当前用户。");
        return false;
    }
    return true;
}
bool verifyFileSecurity(HANDLE handle, QString& error) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto result = GetSecurityInfo(handle, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                         nullptr, nullptr, nullptr, nullptr, &descriptor);
    if (result != ERROR_SUCCESS) {
        error = systemError(QStringLiteral("无法读取新文件的实际 DACL"), result);
        return false;
    }
    const bool verified = verifyCurrentUserDacl(descriptor, error);
    LocalFree(descriptor);
    return verified;
}
} // namespace mini3d::editor::automation::winsecurity
#endif
