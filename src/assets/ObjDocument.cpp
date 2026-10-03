/*
 * 模块名: ObjDocument
 * 功能概述: 在同目录临时文件完成写入后提交 OBJ，不改变工程保存点。
 * 对外接口: ObjDocument::write
 * 依赖关系: Qt QSaveFile。
 * 输入输出: UTF-8 OBJ 文本到用户指定文件。
 * 异常与错误: 文件操作失败返回错误，保留原目标。
 * 维护说明: 不使用直接写回退，不执行自动命名或隐式导出。
 */
#include "ObjDocument.h"

#include <QSaveFile>

namespace mini3d::assets {
bool ObjDocument::write(const QString& path, const std::string& text, QString& error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const auto bytes = QByteArray::fromStdString(text);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = QStringLiteral("OBJ 导出失败：%1").arg(file.errorString());
        return false;
    }
    error.clear();
    return true;
}
} // namespace mini3d::assets
