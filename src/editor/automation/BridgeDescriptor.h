/*
 * 模块名: BridgeDescriptor
 * 功能概述: 创建当前用户受限的认证描述文件，管理精确删除身份。
 * 对外接口: canEnable、randomHex、randomUuid、create、removeOwned。
 * 依赖关系: Windows Token/ACL/BCrypt、Qt Core；其他平台拒绝开启。
 * 输入输出: 可信启动元数据到 CREATE_NEW 描述文件及独占原生句柄。
 * 异常与错误: 提升权限、随机源或 ACL 失败时保持服务关闭。
 * 维护说明: 不输出 secret，不继承目录宽泛权限，不删除替换后的文件。
 */
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace mini3d::editor::automation {
/** @brief 非复制的受限文件所有者；常驻句柄禁止写入，允许普通读者共享。 */
class BridgeDescriptor final {
  public:
    BridgeDescriptor() = default;
    ~BridgeDescriptor();
    BridgeDescriptor(const BridgeDescriptor&) = delete;
    BridgeDescriptor& operator=(const BridgeDescriptor&) = delete;
    /** @brief TokenElevation 查询失败也拒绝，避免以未知权限开启桥。 */
    static bool canEnable(QString& error);
    /** @brief BCrypt 系统随机源失败返回空值；不回退到可预测随机数。 */
    static QString randomHex(int byteCount, QString& error);
    static QString randomUuid(QString& error);
    /** @brief 只读核验本机普通祖先及持久 ACL 卷，不创建路径。 */
    static bool validatePath(const QString& path, QString& error);
    /** @brief 所有祖先须为普通本机目录；文件 CREATE_NEW 并保护 DACL。 */
    bool create(const QString& path, const QJsonObject& content, QString& error);
    /** @brief 核对同一路径的文件身份和原始内容，仅删除本对象所建文件。 */
    bool removeOwned(QString& error);
    [[nodiscard]] QString path() const;

  private:
    void* handle_ = nullptr;
    QString path_;
    QByteArray content_;
    QByteArray identity_;
};
} // namespace mini3d::editor::automation
