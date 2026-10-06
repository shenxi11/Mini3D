/*
 * 模块名: WinLocalPipeServer
 * 功能概述: 在原生创建层拒绝远程客户端，并异步接受同用户连接。
 * 对外接口: listen、close、ensureListener、creationMode。
 * 依赖关系: Windows Named Pipe、QWinEventNotifier、QLocalSocket、当前用户 DACL。
 * 输入输出: 随机 pipe 名到原生已连通句柄，再交 Qt Socket 收发。
 * 异常与错误: 创建或接受失败关闭入口；取消完成前不释放 OVERLAPPED。
 * 维护说明: 单应用线程，一个 pending accept；无阻塞等待或新增业务线程。
 */
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>

class QLocalSocket;
namespace mini3d::editor::automation {
/** @brief 原生本机边界，Qt 接管连通句柄后继续使用有界帧与账本。 */
class WinLocalPipeServer final : public QObject {
  public:
    using Accepted = std::function<void(QLocalSocket*)>;
    using Failure = std::function<void(const QString&)>;
    WinLocalPipeServer(std::function<std::size_t()> activeCount, Accepted accepted,
                       Failure failure, QObject* parent);
    ~WinLocalPipeServer() override;
    /** @brief 每个实例实际使用的原生 mode，包含 PIPE_REJECT_REMOTE_CLIENTS。 */
    static quint32 creationMode();
    bool listen(const QString& pipeName, QString& error);
    /** @brief 取消异步 accept；尚未终结时保留有界记录并事件驱动回收。 */
    void close();
    /** @brief 活动连接释放后排队补一个监听，活动+pending 始终在连接限额内。 */
    void ensureListener();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mini3d::editor::automation
