/*
 * 模块名: FilePathPolicy
 * 功能概述: 将启动时明确的本机读写根固定为受限文件访问策略。
 * 对外接口: create、authorizeRead、authorizeNewFile、authorizeWrite、readPolicy。
 * 依赖关系: Qt Core、Windows 文件属性、Assets 读取前钩子。
 * 输入输出: 本机绝对普通路径到授权结果；不扩充已批准根。
 * 异常与错误: 越界、链接、设备/UNC/ADS 和非普通文件拒绝并返回错误。
 * 维护说明: 应用线程串行；不声称防御已控制同用户权限进程的所有 TOCTOU 攻击。
 */
#pragma once
#include "assets/FileReadPolicy.h"

#include <QStringList>
#include <optional>

namespace mini3d::editor::automation {
/** @brief 不可变的显式根策略；空读/写根分别表示没有该文件权限。 */
class FilePathPolicy final {
  public:
    /** @brief 所有根先验证为本机现有普通目录；任一失败不返回半配置。 */
    static std::optional<FilePathPolicy> create(const QStringList& readRoots,
                                                const QStringList& writeRoots, QString& error);
    /** @brief 每次实际读取前重检路径与祖先，必须是读根下现有普通文件。 */
    bool authorizeRead(const QString& path, QString& error) const;
    /** @brief 仅接受写根下不存在的新文件，现有父目录须全部通过重检。 */
    bool authorizeNewFile(const QString& path, QString& error) const;
    /** @brief 接受写根下新文件或现有普通文件；覆盖意图仍由请求显式决定。 */
    bool authorizeWrite(const QString& path, QString& error) const;
    [[nodiscard]] assets::FileReadPolicy readPolicy() const;
    [[nodiscard]] const QStringList& readRoots() const {
        return readRoots_;
    }
    [[nodiscard]] const QStringList& writeRoots() const {
        return writeRoots_;
    }

  private:
    QStringList readRoots_;
    QStringList writeRoots_;
};
} // namespace mini3d::editor::automation
