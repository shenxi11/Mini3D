/*
 * 模块名: FileReadPolicy
 * 功能概述: 为受控文档和资产加载提供实际读取前的授权钩子。
 * 对外接口: FileReadPolicy。
 * 依赖关系: Qt Core、标准函数对象；不依赖编辑器或通信。
 * 输入输出: 即将读取的本机路径到准许/拒绝及错误。
 * 异常与错误: 拒绝时不得打开文件；空策略保持既有 GUI 行为。
 * 维护说明: 同步调用，不保存在资源库或后台线程中。
 */
#pragma once
#include <QString>
#include <functional>

namespace mini3d::assets {
/** @brief 机器可读的读取失败类别；诊断文字不能用于反推授权状态。 */
enum class FileReadFailure { None, PathDenied, IoError, InvalidData };
/** @brief 在实际打开顶层文件和每个外部依赖前检查；false 必须终止本次加载。 */
using FileReadPolicy = std::function<bool(const QString& path, QString& error)>;
} // namespace mini3d::assets
