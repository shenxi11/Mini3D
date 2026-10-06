/*
 * 模块名: AutomationLedger
 * 功能概述: 管理连接恢复、写序号和内存结果的确定性生命周期。
 * 对外接口: AutomationLedger.h。
 * 依赖关系: Qt JSON、生成的 ApiLimits；单调时间由桥提供。
 * 输入输出: 写槽接收/终结到高水位、查询和原始结果重放。
 * 异常与错误: 不把过期会话或结果丢失解释为未执行。
 * 维护说明: 断开保留期从全部请求终结开始；重放不影响过期时间。
 */
#include "AutomationLedger.h"

#include "api/ApiLimits.h"

#include <QJsonDocument>
#include <limits>

namespace mini3d::editor::automation {
namespace {
LedgerResult rejected(const QString& error) {
    return {LedgerState::Rejected, error, {}, {}};
}
std::uint64_t nextSequence(std::uint64_t highWater) {
    return highWater == std::numeric_limits<std::uint64_t>::max() ? highWater : highWater + 1;
}
} // namespace
SessionResult AutomationLedger::createSession(const QString& sessionId,
                                               const QString& connectionId, qint64 nowMs) {
    expire(nowMs);
    if (sessions_.size() >= api::limits::sessions)
        return {{}, QStringLiteral("SESSION_LIMIT")};
    Session session;
    session.connectionId = connectionId;
    const auto [position, inserted] = sessions_.emplace(sessionId, std::move(session));
    if (!inserted)
        return {{}, QStringLiteral("SESSION_IN_USE")};
    return {position->first, {}};
}
SessionResult AutomationLedger::resumeSession(const QString& sessionId,
                                               const QString& connectionId, qint64 nowMs) {
    expire(nowMs);
    const auto found = sessions_.find(sessionId);
    if (found == sessions_.end())
        return {{}, QStringLiteral("SESSION_EXPIRED")};
    if (!found->second.connectionId.isEmpty())
        return {{}, QStringLiteral("SESSION_IN_USE")};
    found->second.connectionId = connectionId;
    found->second.disconnectedAt.reset();
    return {sessionId, {}};
}
void AutomationLedger::beginRetention(Session& session, qint64 nowMs) {
    if (session.connectionId.isEmpty() && !session.pending && session.activeRequests == 0 &&
        !session.disconnectedAt)
        session.disconnectedAt = nowMs;
}
void AutomationLedger::disconnect(const QString& connectionId, qint64 nowMs) {
    for (auto& [id, session] : sessions_) {
        if (session.connectionId == connectionId) {
            session.connectionId.clear();
            beginRetention(session, nowMs);
            return;
        }
    }
}
void AutomationLedger::expire(qint64 nowMs) {
    for (auto position = sessions_.begin(); position != sessions_.end();) {
        const auto& session = position->second;
        if (session.connectionId.isEmpty() && !session.pending && session.activeRequests == 0 &&
            session.disconnectedAt &&
            nowMs - *session.disconnectedAt >= qint64(api::limits::disconnectedSessionRetentionMs)) {
            for (const auto& [sequence, result] : session.results)
                cachedBytes_ -= result.bytes;
            position = sessions_.erase(position);
        } else {
            ++position;
        }
    }
}
void AutomationLedger::clear() {
    sessions_.clear();
    cachedBytes_ = 0;
    cacheAge_ = 0;
}
bool AutomationLedger::isBound(const QString& sessionId, const QString& connectionId) const {
    const auto found = sessions_.find(sessionId);
    return found != sessions_.end() && found->second.connectionId == connectionId &&
           !connectionId.isEmpty();
}
std::optional<SessionSnapshot> AutomationLedger::snapshot(const QString& sessionId) const {
    const auto found = sessions_.find(sessionId);
    if (found == sessions_.end())
        return {};
    return SessionSnapshot{found->second.highWater, nextSequence(found->second.highWater)};
}
bool AutomationLedger::retainRequest(const QString& sessionId) {
    const auto found = sessions_.find(sessionId);
    if (found == sessions_.end())
        return false;
    ++found->second.activeRequests;
    found->second.disconnectedAt.reset();
    return true;
}
void AutomationLedger::releaseRequest(const QString& sessionId, qint64 nowMs) {
    const auto found = sessions_.find(sessionId);
    if (found == sessions_.end() || found->second.activeRequests == 0)
        return;
    --found->second.activeRequests;
    beginRetention(found->second, nowMs);
}
LedgerResult AutomationLedger::accept(const QString& sessionId, std::uint64_t sequence,
                                     const QByteArray& digest, int timeoutMs, qint64 nowMs) {
    const auto found = sessions_.find(sessionId);
    if (found == sessions_.end())
        return rejected(QStringLiteral("SESSION_EXPIRED"));
    auto& session = found->second;
    if (sequence <= session.highWater) {
        const auto cached = session.results.find(sequence);
        if (cached == session.results.end())
            return {LedgerState::ResultExpired, QStringLiteral("RESULT_EXPIRED"), {}, {}};
        if (cached->second.digest != digest)
            return rejected(QStringLiteral("REQUEST_KEY_REUSED"));
        return {LedgerState::Completed, {}, cached->second.response,
                cached->second.committedDocumentRevision};
    }
    if (sequence != nextSequence(session.highWater))
        return rejected(QStringLiteral("SEQUENCE_CONFLICT"));
    if (session.pending) {
        if (session.pending->sequence != sequence)
            return rejected(QStringLiteral("SEQUENCE_CONFLICT"));
        if (session.pending->digest != digest)
            return rejected(QStringLiteral("REQUEST_KEY_REUSED"));
        return {LedgerState::Pending, {}, {}, {}};
    }
    session.pending = Pending{sequence, digest, nowMs + timeoutMs, false};
    session.disconnectedAt.reset();
    return {LedgerState::Accepted, {}, {}, {}};
}
bool AutomationLedger::isDeadlineExceeded(const QString& sessionId, std::uint64_t sequence,
                                           qint64 nowMs) const {
    const auto found = sessions_.find(sessionId);
    return found != sessions_.end() && found->second.pending &&
           found->second.pending->sequence == sequence &&
           nowMs >= found->second.pending->deadlineMs;
}
bool AutomationLedger::start(const QString& sessionId, std::uint64_t sequence) {
    const auto found = sessions_.find(sessionId);
    if (found == sessions_.end() || !found->second.pending ||
        found->second.pending->sequence != sequence || found->second.pending->started)
        return false;
    found->second.pending->started = true;
    return true;
}
bool AutomationLedger::complete(const QString& sessionId, std::uint64_t sequence,
                                QJsonObject response,
                                std::optional<std::uint64_t> committedDocumentRevision,
                                qint64 nowMs) {
    const auto found = sessions_.find(sessionId);
    if (found == sessions_.end() || !found->second.pending ||
        found->second.pending->sequence != sequence)
        return false;
    auto& session = found->second;
    Cached cached;
    cached.digest = session.pending->digest;
    cached.bytes = std::size_t(QJsonDocument(response).toJson(QJsonDocument::Compact).size()) +
                   std::size_t(cached.digest.size());
    cached.response = std::move(response);
    cached.committedDocumentRevision = committedDocumentRevision;
    cached.age = ++cacheAge_;
    session.highWater = sequence;
    session.pending.reset();
    cachedBytes_ += cached.bytes;
    session.results.emplace(sequence, std::move(cached));
    while (session.results.size() > api::limits::cachedResultsPerSession)
        removeOldestResult(session);
    enforceCacheBudget();
    beginRetention(session, nowMs);
    return true;
}
LedgerResult AutomationLedger::status(const QString& sessionId, std::uint64_t sequence) const {
    const auto found = sessions_.find(sessionId);
    if (found == sessions_.end())
        return rejected(QStringLiteral("SESSION_EXPIRED"));
    const auto& session = found->second;
    if (session.pending && session.pending->sequence == sequence)
        return {LedgerState::Pending, {}, {}, {}};
    const auto cached = session.results.find(sequence);
    if (cached != session.results.end())
        return {LedgerState::Completed, {}, cached->second.response,
                cached->second.committedDocumentRevision};
    if (sequence <= session.highWater)
        return {LedgerState::ResultExpired, {}, {}, {}};
    return {LedgerState::NotSeen, {}, {}, {}};
}
QString AutomationLedger::cancellationStatus(const QString& sessionId,
                                             std::uint64_t sequence) const {
    const auto found = sessions_.find(sessionId);
    if (found == sessions_.end())
        return QStringLiteral("not_seen");
    const auto& session = found->second;
    if (session.pending && session.pending->sequence == sequence)
        return session.pending->started ? QStringLiteral("cannot_cancel_started")
                                        : QStringLiteral("cancelled");
    return sequence <= session.highWater ? QStringLiteral("already_completed")
                                         : QStringLiteral("not_seen");
}
void AutomationLedger::removeOldestResult(Session& session) {
    if (session.results.empty())
        return;
    cachedBytes_ -= session.results.begin()->second.bytes;
    session.results.erase(session.results.begin());
}
void AutomationLedger::enforceCacheBudget() {
    while (cachedBytes_ > api::limits::cachedResultBytes) {
        Session* oldest = nullptr;
        std::uint64_t age = std::numeric_limits<std::uint64_t>::max();
        for (auto& [id, session] : sessions_) {
            if (!session.results.empty() && session.results.begin()->second.age < age) {
                oldest = &session;
                age = session.results.begin()->second.age;
            }
        }
        if (!oldest)
            return;
        removeOldestResult(*oldest);
    }
}
std::size_t AutomationLedger::sessionCount() const {
    return sessions_.size();
}
std::size_t AutomationLedger::cachedBytes() const {
    return cachedBytes_;
}
} // namespace mini3d::editor::automation
