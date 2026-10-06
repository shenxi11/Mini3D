/*
 * 模块名: AutomationLedgerTests
 * 功能概述: 用显式单调时间验证序号、恢复、期限和结果预算。
 * 对外接口: Catch2 [automation-ledger]；不要求 GUI 或事件循环。
 * 依赖关系: AutomationLedger、ApiLimits、Qt Core。
 * 输入输出: 认证后的独立会话夹具到高水位和确定性重放断言。
 * 异常与错误: 结果淘汰和会话失效均禁止重新执行旧命令。
 * 维护说明: 不 sleep、不建网络服务，不修改共享测试状态。
 */
#include "editor/automation/AutomationLedger.h"

#include "api/ApiLimits.h"

#include <catch2/catch_test_macros.hpp>

using namespace mini3d::editor::automation;
namespace {
const QString sessionId = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString secondSessionId = QStringLiteral("22222222-2222-4222-8222-222222222222");
QJsonObject completedResponse() {
    return {{"result", QJsonObject{{"status", "committed"}, {"documentRevision", "13"}}}};
}
} // namespace
TEST_CASE("active session resume rejects without replacing original connection", "[automation-ledger]") {
    AutomationLedger ledger;
    REQUIRE(ledger.createSession(sessionId, "connection-one", 0).error.isEmpty());
    REQUIRE(ledger.resumeSession(sessionId, "connection-two", 1).error == "SESSION_IN_USE");
    REQUIRE(ledger.isBound(sessionId, "connection-one"));
    REQUIRE_FALSE(ledger.isBound(sessionId, "connection-two"));
    ledger.disconnect("connection-one", 2);
    REQUIRE(ledger.resumeSession(sessionId, "connection-two", 3).sessionId == sessionId);
    REQUIRE(ledger.isBound(sessionId, "connection-two"));
    REQUIRE(ledger.resumeSession("unknown-session", "new-connection", 4).error == "SESSION_EXPIRED");
    REQUIRE(ledger.sessionCount() == 1);
}
TEST_CASE("one pending slot keeps original deadline and digest across retries", "[automation-ledger]") {
    AutomationLedger ledger;
    REQUIRE(ledger.createSession(sessionId, "connection", 0).error.isEmpty());
    REQUIRE(ledger.accept(sessionId, 1, "digest", 50, 10).state == LedgerState::Accepted);
    REQUIRE(ledger.accept(sessionId, 1, "digest", 50, 55).state == LedgerState::Pending);
    REQUIRE(ledger.accept(sessionId, 1, "different", 50, 56).error == "REQUEST_KEY_REUSED");
    REQUIRE(ledger.accept(sessionId, 2, "next", 50, 57).error == "SEQUENCE_CONFLICT");
    REQUIRE(ledger.snapshot(sessionId)->highWater == 0);
    REQUIRE(ledger.snapshot(sessionId)->nextMutationSequence == 1);
    REQUIRE_FALSE(ledger.isDeadlineExceeded(sessionId, 1, 59));
    REQUIRE(ledger.isDeadlineExceeded(sessionId, 1, 60));
    REQUIRE(ledger.cancellationStatus(sessionId, 1) == "cancelled");
    REQUIRE(ledger.start(sessionId, 1));
    REQUIRE(ledger.cancellationStatus(sessionId, 1) == "cannot_cancel_started");
    REQUIRE(ledger.complete(sessionId, 1, completedResponse(), 13, 70));
    const auto replay = ledger.accept(sessionId, 1, "digest", 50, 1000);
    REQUIRE(replay.state == LedgerState::Completed);
    REQUIRE(replay.response == completedResponse());
    REQUIRE(replay.committedDocumentRevision == 13);
    REQUIRE(ledger.snapshot(sessionId)->highWater == 1);
    REQUIRE(ledger.snapshot(sessionId)->nextMutationSequence == 2);
    REQUIRE(ledger.cancellationStatus(sessionId, 1) == "already_completed");
}
TEST_CASE("domain failure consumes sequence while an unknown session cannot", "[automation-ledger]") {
    AutomationLedger ledger;
    REQUIRE(ledger.createSession(sessionId, "connection", 0).error.isEmpty());
    REQUIRE(ledger.accept("unknown", 1, "digest", 10, 0).error == "SESSION_EXPIRED");
    REQUIRE(ledger.snapshot(sessionId)->highWater == 0);
    REQUIRE(ledger.accept(sessionId, 1, "digest", 10, 0).state == LedgerState::Accepted);
    const QJsonObject failure{{"error", QJsonObject{{"code", -32010}, {"message", "NOT_FOUND"}}}};
    REQUIRE(ledger.complete(sessionId, 1, failure, {}, 1));
    REQUIRE(ledger.status(sessionId, 1).response == failure);
    REQUIRE(ledger.accept(sessionId, 1, "different", 10, 2).error == "REQUEST_KEY_REUSED");
    REQUIRE(ledger.accept(sessionId, 2, "next", 10, 2).state == LedgerState::Accepted);
}
TEST_CASE("disconnected retention starts after pending mutation and query finish", "[automation-ledger]") {
    AutomationLedger ledger;
    using namespace mini3d::editor::api;
    const auto retention = qint64(limits::disconnectedSessionRetentionMs);
    REQUIRE(ledger.createSession(sessionId, "connection", 0).error.isEmpty());
    REQUIRE(ledger.retainRequest(sessionId));
    REQUIRE(ledger.accept(sessionId, 1, "digest", 30, 1).state == LedgerState::Accepted);
    ledger.disconnect("connection", 2);
    ledger.expire(retention + 2);
    REQUIRE(ledger.snapshot(sessionId));
    REQUIRE(ledger.complete(sessionId, 1, completedResponse(), 13, retention + 3));
    ledger.expire(retention * 2 + 3);
    REQUIRE(ledger.snapshot(sessionId));
    ledger.releaseRequest(sessionId, retention * 2 + 4);
    REQUIRE(ledger.status(sessionId, 1).state == LedgerState::Completed);
    ledger.expire(retention * 3 + 3);
    REQUIRE(ledger.snapshot(sessionId));
    ledger.expire(retention * 3 + 4);
    REQUIRE_FALSE(ledger.snapshot(sessionId));
    REQUIRE(ledger.status(sessionId, 1).error == "SESSION_EXPIRED");
    REQUIRE(ledger.resumeSession(sessionId, "connection", retention * 3 + 4).error == "SESSION_EXPIRED");
}
TEST_CASE("session capacity never evicts an in-flight disconnected session", "[automation-ledger]") {
    AutomationLedger ledger;
    using namespace mini3d::editor::api;
    for (std::size_t index = 0; index < limits::sessions; ++index) {
        const auto id = QStringLiteral("session-%1").arg(index);
        const auto connection = QStringLiteral("connection-%1").arg(index);
        REQUIRE(ledger.createSession(id, connection, 0).error.isEmpty());
        REQUIRE(ledger.accept(id, 1, "digest", 30, 0).state == LedgerState::Accepted);
        ledger.disconnect(connection, 1);
    }
    REQUIRE(ledger.createSession("another", "new", qint64(limits::disconnectedSessionRetentionMs) * 2)
                .error == "SESSION_LIMIT");
    REQUIRE(ledger.sessionCount() == limits::sessions);
}
TEST_CASE("per-session result eviction leaves highWater and explicit expiration", "[automation-ledger]") {
    AutomationLedger ledger;
    using namespace mini3d::editor::api;
    REQUIRE(ledger.createSession(sessionId, "connection", 0).error.isEmpty());
    const auto total = std::uint64_t(limits::cachedResultsPerSession + 1);
    for (std::uint64_t sequence = 1; sequence <= total; ++sequence) {
        REQUIRE(ledger.accept(sessionId, sequence, "digest", 30, 0).state == LedgerState::Accepted);
        REQUIRE(ledger.complete(sessionId, sequence, completedResponse(), 13, 1));
    }
    REQUIRE(ledger.status(sessionId, 1).state == LedgerState::ResultExpired);
    REQUIRE(ledger.accept(sessionId, 1, "digest", 30, 2).error == "RESULT_EXPIRED");
    REQUIRE(ledger.snapshot(sessionId)->highWater == total);
    REQUIRE(ledger.status(sessionId, total).state == LedgerState::Completed);
    REQUIRE(ledger.status(sessionId, total + 1).state == LedgerState::NotSeen);
}
TEST_CASE("global byte budget evicts oldest payload across sessions", "[automation-ledger]") {
    AutomationLedger ledger;
    using namespace mini3d::editor::api;
    REQUIRE(ledger.createSession(sessionId, "one", 0).error.isEmpty());
    REQUIRE(ledger.createSession(secondSessionId, "two", 0).error.isEmpty());
    const QJsonObject large{{"result", QString(int(limits::cachedResultBytes / 2 + 1024), 'x')}};
    REQUIRE(ledger.accept(sessionId, 1, "one", 30, 0).state == LedgerState::Accepted);
    REQUIRE(ledger.complete(sessionId, 1, large, {}, 1));
    REQUIRE(ledger.accept(secondSessionId, 1, "two", 30, 0).state == LedgerState::Accepted);
    REQUIRE(ledger.complete(secondSessionId, 1, large, {}, 2));
    REQUIRE(ledger.cachedBytes() <= limits::cachedResultBytes);
    REQUIRE(ledger.status(sessionId, 1).state == LedgerState::ResultExpired);
    REQUIRE(ledger.status(secondSessionId, 1).state == LedgerState::Completed);
    REQUIRE(ledger.snapshot(sessionId)->highWater == 1);
}
