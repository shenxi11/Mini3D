/*
 * 模块名: ObjDocument
 * 功能概述: 将已编码的 OBJ 文本原子写入用户指定路径。
 * 对外接口: ObjDocument::write
 * 依赖关系: Qt Core、标准字符串；不依赖视图、场景或历史。
 * 输入输出: 文件路径与完整 UTF-8 文本到成功状态或错误原因。
 * 异常与错误: 创建/写入/提交失败返回 false，不直接覆盖原文件。
 * 维护说明: 覆盖确认由文件对话框负责；关闭 QSaveFile 直接写回退。
 */
#pragma once
#include <QString>
#include <string>

namespace mini3d::assets {
class ObjDocument final {
  public:
    /** @brief 原子写完整 OBJ 文本；失败保留目标原字节，error 返回中文原因。 */
    static bool write(const QString& path, const std::string& text, QString& error);
};
} // namespace mini3d::assets
