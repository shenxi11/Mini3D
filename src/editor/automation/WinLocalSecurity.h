/*
 * 模块名: WinLocalSecurity
 * 功能概述: 为认证文件和原生命名管道复用当前用户保护 DACL 检查。
 * 对外接口: currentUserDescriptor、verifyCurrentUserDacl、verifyFileSecurity、localAclVolume。
 * 依赖关系: Windows Token/ACL、Qt Core；仅 Windows 使用。
 * 输入输出: 系统身份和实际卷/文件安全数据到准许或失败原因。
 * 异常与错误: 未知身份、网络卷、不持久 ACL 或宽泛 DACL 均拒绝。
 * 维护说明: 不创建用户或调整权限；调用者负责 LocalFree 返回的描述符。
 */
#pragma once

#include <QString>
#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mini3d::editor::automation::winsecurity {
/** @brief 只允许本机固定/可移动卷且持久 ACL；用于真实系统查询结果。 */
bool localAclVolume(DWORD driveType, DWORD filesystemFlags, QString& error);
/** @brief 创建当前 SID 的保护 DACL；失败返回 nullptr，无继承 ACE。 */
PSECURITY_DESCRIPTOR currentUserDescriptor(QString& error);
/** @brief 核验保护 DACL 只有一个当前 SID 的完整权限 ACE。 */
bool verifyCurrentUserDacl(PSECURITY_DESCRIPTOR descriptor, QString& error);
/** @brief 从实际文件句柄读取安全数据，不能以 CreateFile 成功替代 ACL 验证。 */
bool verifyFileSecurity(HANDLE handle, QString& error);
QString systemError(const QString& action, DWORD code = GetLastError());
} // namespace mini3d::editor::automation::winsecurity
#endif
