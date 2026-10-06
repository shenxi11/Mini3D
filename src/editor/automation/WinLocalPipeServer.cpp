/*
 * 模块名: WinLocalPipeServer
 * 功能概述: 以保护 DACL 和 PIPE_REJECT_REMOTE_CLIENTS 创建 overlapped 本机管道。
 * 对外接口: WinLocalPipeServer.h。
 * 依赖关系: Windows Named Pipe/CancelIoEx、Qt Win notifier/LocalSocket、ApiLimits。
 * 输入输出: 异步原生 accept 到既有 Qt 字节流；安全失败到入口关闭。
 * 异常与错误: GetOverlappedResult 非等待检查，取消 pending 直到实际终结才回收。
 * 维护说明: 原生句柄只移交一次；应用退出时不释放内核仍使用的 OVERLAPPED。
 */
#include "WinLocalPipeServer.h"

#include "WinLocalSecurity.h"
#include "api/ApiLimits.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QPointer>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <QWinEventNotifier>
#endif

namespace mini3d::editor::automation {
namespace {
#ifdef Q_OS_WIN
struct NativeAccept {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    OVERLAPPED overlapped{};
    bool pending = false;
};
/** @brief 父桥可先销毁；取消记录只在内核确认终结后 deleteLater。 */
class PendingAccept final : public QObject {
  public:
    using Callback = std::function<void(PendingAccept*, DWORD)>;
    PendingAccept(std::unique_ptr<NativeAccept> native, Callback callback, QObject* parent)
        : QObject(parent), native_(std::move(native)), callback_(std::move(callback)) {
        notifier_ = new QWinEventNotifier(native_->event, this);
        QObject::connect(notifier_, &QWinEventNotifier::activated, this, [this] {
            notifier_->setEnabled(false);
            auto status = completionStatus();
            if (status == ERROR_IO_INCOMPLETE) {
                // reset 后再次非等待核验，覆盖核验与 reset 之间完成的事件竞态。
                ResetEvent(native_->event);
                status = completionStatus();
                if (status == ERROR_IO_INCOMPLETE) {
                    notifier_->setEnabled(true);
                    return;
                }
            }
            native_->pending = false;
            if (retiring_) {
                closeHandles();
                deleteLater();
                return;
            }
            // 失败回调可能 retire() 并清空成员；局部副本保持正在执行的闭包存活。
            const auto callback = callback_;
            callback(this, status);
        });
    }
    ~PendingAccept() override {
        notifier_->setEnabled(false);
        if (native_->pending) {
            CancelIoEx(native_->pipe, &native_->overlapped);
            DWORD bytes = 0;
            if (!GetOverlappedResult(native_->pipe, &native_->overlapped, &bytes, FALSE) &&
                GetLastError() == ERROR_IO_INCOMPLETE) {
                // 仅应用退出且取消未终结会走此处；事件和记录留给进程退出回收。
                CloseHandle(native_->pipe);
                native_.release();
                return;
            }
        }
        closeHandles();
    }
    HANDLE takePipe() {
        const auto pipe = native_->pipe;
        native_->pipe = INVALID_HANDLE_VALUE;
        return pipe;
    }
    void retire() {
        retiring_ = true;
        callback_ = {};
        notifier_->setEnabled(false);
        if (native_->pending) {
            CancelIoEx(native_->pipe, &native_->overlapped);
            if (completionStatus() == ERROR_IO_INCOMPLETE) {
                setParent(QCoreApplication::instance());
                notifier_->setEnabled(true);
                return;
            }
            native_->pending = false;
        }
        closeHandles();
        deleteLater();
    }
    bool isPending() const {
        return native_->pending;
    }

  private:
    DWORD completionStatus() const {
        DWORD bytes = 0;
        if (native_->pending &&
            !GetOverlappedResult(native_->pipe, &native_->overlapped, &bytes, FALSE))
            return GetLastError();
        return ERROR_SUCCESS;
    }
    void closeHandles() {
        if (native_->pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(native_->pipe);
            native_->pipe = INVALID_HANDLE_VALUE;
        }
        if (native_->event) {
            CloseHandle(native_->event);
            native_->event = nullptr;
        }
    }
    std::unique_ptr<NativeAccept> native_;
    QWinEventNotifier* notifier_ = nullptr;
    Callback callback_;
    bool retiring_ = false;
};
#endif
} // namespace
class WinLocalPipeServer::Impl final {
  public:
    Impl(WinLocalPipeServer& owner, std::function<std::size_t()> activeCount, Accepted accepted,
         Failure failure)
        : owner_(owner), activeCount_(std::move(activeCount)), accepted_(std::move(accepted)),
          failure_(std::move(failure)) {}
    bool listen(const QString& pipeName, QString& error) {
#ifdef Q_OS_WIN
        if (listening_ || pending_ || (retiring_ && retiring_->isPending())) {
            error = QStringLiteral("先前的命名管道接受或取消尚未终结。");
            return false;
        }
        static const QRegularExpression name(QStringLiteral("^mini3d-[0-9a-f-]+$"));
        if (!name.match(pipeName).hasMatch()) {
            error = QStringLiteral("命名管道身份格式无效。");
            return false;
        }
        security_ = winsecurity::currentUserDescriptor(error);
        if (!security_ || !winsecurity::verifyCurrentUserDacl(security_, error)) {
            if (security_)
                LocalFree(security_);
            security_ = nullptr;
            return false;
        }
        pipeName_ = QStringLiteral("\\\\.\\pipe\\") + pipeName;
        listening_ = true;
        firstInstance_ = true;
        if (!createListener(error)) {
            close();
            return false;
        }
        return true;
#else
        Q_UNUSED(pipeName);
        error = QStringLiteral("当前平台没有已批准的本机命名管道入口。");
        return false;
#endif
    }
    void close() {
        listening_ = false;
#ifdef Q_OS_WIN
        if (pending_) {
            retiring_ = pending_;
            pending_->retire();
            pending_ = nullptr;
        }
        if (security_)
            LocalFree(security_);
        security_ = nullptr;
#endif
        pipeName_.clear();
    }
    void ensureListener() {
#ifdef Q_OS_WIN
        if (!listening_ || pending_ || activeCount_() >= api::limits::connections)
            return;
        // 接受或释放槽位后立即补一个异步监听实例；延后会让同线程 Qt 客户端
        // 在 PIPE_BUSY 上阻塞连接，进而阻止排队的补监听任务运行。
        QString error;
        if (!createListener(error)) {
            close();
            failure_(error);
        }
#endif
    }
#ifdef Q_OS_WIN
    bool createListener(QString& error) {
        auto native = std::make_unique<NativeAccept>();
        native->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!native->event) {
            error = winsecurity::systemError(QStringLiteral("无法创建异步命名管道事件"));
            return false;
        }
        native->overlapped.hEvent = native->event;
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security_, FALSE};
        const DWORD access = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                             (firstInstance_ ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
        native->pipe =
            CreateNamedPipeW(reinterpret_cast<LPCWSTR>(pipeName_.utf16()), access,
                             WinLocalPipeServer::creationMode(), DWORD(api::limits::connections),
                             64 * 1024, 64 * 1024, 0, &attributes);
        if (native->pipe == INVALID_HANDLE_VALUE) {
            error = winsecurity::systemError(QStringLiteral("无法创建拒绝远程客户端的命名管道"));
            CloseHandle(native->event);
            return false;
        }
        firstInstance_ = false;
        const bool connected = ConnectNamedPipe(native->pipe, &native->overlapped);
        const auto status = connected ? ERROR_SUCCESS : GetLastError();
        if (status != ERROR_SUCCESS && status != ERROR_IO_PENDING &&
            status != ERROR_PIPE_CONNECTED) {
            error = winsecurity::systemError(QStringLiteral("无法异步接受本机管道"), status);
            CloseHandle(native->pipe);
            CloseHandle(native->event);
            return false;
        }
        native->pending = status == ERROR_IO_PENDING;
        const auto event = native->event;
        const QPointer<WinLocalPipeServer> alive(&owner_);
        pending_ = new PendingAccept(
            std::move(native),
            [this, alive](PendingAccept* pending, DWORD status) {
                if (!alive || pending != pending_)
                    return;
                pending_ = nullptr;
                if (!listening_ || status != ERROR_SUCCESS) {
                    pending->retire();
                    if (listening_) {
                        const auto error = winsecurity::systemError(
                            QStringLiteral("本机命名管道接受失败"), status);
                        close();
                        failure_(error);
                    }
                    return;
                }
                const auto pipe = pending->takePipe();
                pending->deleteLater();
                auto* socket = new QLocalSocket(&owner_);
                if (!socket->setSocketDescriptor(reinterpret_cast<qintptr>(pipe),
                                                 QLocalSocket::ConnectedState,
                                                 QIODevice::ReadWrite)) {
                    // 固定 Qt Windows 实现已接管句柄；关闭只经 socket，不能再 CloseHandle。
                    socket->abort();
                    socket->deleteLater();
                    close();
                    failure_(QStringLiteral("Qt 无法接管已连通的本机管道。"));
                    return;
                }
                accepted_(socket);
                ensureListener();
            },
            &owner_);
        if (status != ERROR_IO_PENDING)
            SetEvent(event);
        return true;
    }
#endif
    WinLocalPipeServer& owner_;
    std::function<std::size_t()> activeCount_;
    Accepted accepted_;
    Failure failure_;
    QString pipeName_;
    bool listening_ = false, firstInstance_ = true;
#ifdef Q_OS_WIN
    PSECURITY_DESCRIPTOR security_ = nullptr;
    QPointer<PendingAccept> pending_, retiring_;
#endif
};
WinLocalPipeServer::WinLocalPipeServer(std::function<std::size_t()> activeCount, Accepted accepted,
                                       Failure failure, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(activeCount),
                                                    std::move(accepted), std::move(failure))) {}
WinLocalPipeServer::~WinLocalPipeServer() {
    impl_->close();
}
quint32 WinLocalPipeServer::creationMode() {
#ifdef Q_OS_WIN
    return PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
#else
    return 0;
#endif
}
bool WinLocalPipeServer::listen(const QString& pipeName, QString& error) {
    return impl_->listen(pipeName, error);
}
void WinLocalPipeServer::close() {
    impl_->close();
}
void WinLocalPipeServer::ensureListener() {
    impl_->ensureListener();
}
} // namespace mini3d::editor::automation
