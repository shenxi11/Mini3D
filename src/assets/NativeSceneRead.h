/*
 * 模块名: NativeSceneRead
 * 功能概述: 按已经探测的长度有界读取同一场景文件，拒绝阶段间来源增长。
 * 对外接口: detail::readPreflightedSceneBytes，供原生加载与边界测试使用。
 * 依赖关系: QFile、QByteArray、NativeSceneProbe。
 * 输入输出: 已复位文件和探针到完整字节；失败不替换输出。
 * 异常与错误: 超限、长度变化或I/O失败返回中文诊断。
 * 维护说明: 旧格式只限制本次读取长度，不新增格式4的全局预算。
 */
#pragma once

class QFile;
class QByteArray;
class QString;
namespace mini3d::core {
struct NativeSceneProbe;
}
namespace mini3d::assets::detail {
/** @brief 文件必须已复位；至多读取预算加一字节，验证后才移动到输出。 */
bool readPreflightedSceneBytes(QFile& file, const core::NativeSceneProbe& probe, QByteArray& result,
                               QString& error);
} // namespace mini3d::assets::detail
