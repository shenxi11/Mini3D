/*
 * 模块名: ObjDocument
 * 功能概述: 将已编码的 OBJ 文本原子写入用户指定路径。
 * 对外接口: ObjDocument::write
 * 依赖关系: Qt Core、标准字符串；不依赖视图、场景或历史。
 * 输入输出: 文件路径与完整 UTF-8 文本到成功状态或错误原因。
 * 异常与错误: 创建/写入/提交失败返回 false，不直接覆盖原文件。
 * 维护说明: GUI 默认替换；受控导出明确选择模式并在最终发布前检查许可。
 */
#pragma once
#include <QString>
#include <functional>
#include <string>

namespace mini3d::assets {
class ObjDocument final {
  public:
    /** @brief 默认兼容 GUI 替换；NewOnly 最终发布原子拒绝既有及竞争生成的目标。 */
    enum class WriteMode { ReplaceExisting, NewOnly };
    /** @brief 原子写完整 OBJ 文本；失败保留目标原字节，error 返回中文原因。 */
    static bool write(const QString& path, const std::string& text, QString& error,
                      const std::function<bool()>& beforeCommit = {},
                      WriteMode mode = WriteMode::ReplaceExisting, bool* overwriteDenied = nullptr);
};
} // namespace mini3d::assets
