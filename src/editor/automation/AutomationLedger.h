/*
 * 模块名: AutomationLedger
 * 功能概述: 持有本次桥生命周期内的会话、单写槽、高水位和有界结果。
 * 对外接口: AutomationLedger 的会话绑定、接收、执行、终结和查询。
 * 依赖关系: Qt Core、生成的 ApiLimits；不执行场景业务或持久化事务。
 * 输入输出: 认证后的会话/序号/规范摘要到接受、重放或结构化状态。
 * 异常与错误: 会话过期、重复键、序号跳跃和淘汰结果明确拒绝重做。
 * 维护说明: 由应用线程单一所有者调用；时间由外部单调时钟传入。
 */
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <cstdint>
#include <map>
#include <optional>

namespace mini3d::editor::automation {
enum class LedgerState { Accepted, Pending, Completed, ResultExpired, NotSeen, Rejected };
struct LedgerResult {
    LedgerState state = LedgerState::Rejected;
    QString error;
    QJsonObject response;
    std::optional<std::uint64_t> committedDocumentRevision;
};
struct SessionResult {
    QString sessionId;
    QString error;
};
struct SessionSnapshot {
    std::uint64_t highWater = 0;
    std::uint64_t nextMutationSequence = 1;
};
/** @brief 进程内去重；结果淘汰仅释放载荷，高水位永不因此回退。 */
class AutomationLedger final {
  public:
    /** @brief UUID 由安全模块生成；容量满拒绝，不驱逐尚未终结请求。 */
    SessionResult createSession(const QString& sessionId, const QString& connectionId,
                                qint64 nowMs);
    SessionResult resumeSession(const QString& sessionId, const QString& connectionId,
                                qint64 nowMs);
    void disconnect(const QString& connectionId, qint64 nowMs);
    void expire(qint64 nowMs);
    void clear();
    [[nodiscard]] bool isBound(const QString& sessionId, const QString& connectionId) const;
    [[nodiscard]] std::optional<SessionSnapshot> snapshot(const QString& sessionId) const;
    /** @brief queries/capture 保留会话到终结；不触碰写序号。 */
    bool retainRequest(const QString& sessionId);
    void releaseRequest(const QString& sessionId, qint64 nowMs);
    /** @brief 仅首次入槽记录 deadline；重复入槽不会刷新其计时。 */
    LedgerResult accept(const QString& sessionId, std::uint64_t sequence,
                        const QByteArray& digest, int timeoutMs, qint64 nowMs);
    [[nodiscard]] bool isDeadlineExceeded(const QString& sessionId, std::uint64_t sequence,
                                          qint64 nowMs) const;
    bool start(const QString& sessionId, std::uint64_t sequence);
    bool complete(const QString& sessionId, std::uint64_t sequence, QJsonObject response,
                  std::optional<std::uint64_t> committedDocumentRevision, qint64 nowMs);
    [[nodiscard]] LedgerResult status(const QString& sessionId, std::uint64_t sequence) const;
    /** @brief 只判断取消资格；未开始的实际取消由服务写入 CANCELLED 终态。 */
    [[nodiscard]] QString cancellationStatus(const QString& sessionId,
                                              std::uint64_t sequence) const;
    [[nodiscard]] std::size_t sessionCount() const;
    [[nodiscard]] std::size_t cachedBytes() const;

  private:
    struct Pending {
        std::uint64_t sequence = 0;
        QByteArray digest;
        qint64 deadlineMs = 0;
        bool started = false;
    };
    struct Cached {
        QByteArray digest;
        QJsonObject response;
        std::optional<std::uint64_t> committedDocumentRevision;
        std::size_t bytes = 0;
        std::uint64_t age = 0;
    };
    struct Session {
        QString connectionId;
        std::uint64_t highWater = 0;
        std::optional<Pending> pending;
        std::map<std::uint64_t, Cached> results;
        std::size_t activeRequests = 0;
        std::optional<qint64> disconnectedAt;
    };
    void beginRetention(Session& session, qint64 nowMs);
    void removeOldestResult(Session& session);
    void enforceCacheBudget();
    std::map<QString, Session> sessions_;
    std::size_t cachedBytes_ = 0;
    std::uint64_t cacheAge_ = 0;
};
} // namespace mini3d::editor::automation
