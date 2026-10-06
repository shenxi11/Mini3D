/*
 * 模块名: PerformanceTrace
 * 功能概述: 测试副本使用的单线程阶段计时与有界输出缓冲。
 * 对外接口: begin、record、queued、start、complete、domainAndEncode。
 * 依赖关系: Qt Core 与单调时钟；不参与生产协议。
 * 输入输出: 请求 ID、标量时长和资源计数到独占 NDJSON 证据。
 * 异常与错误: 证据文件无法创建时由探针启动拒绝。
 * 维护说明: 只记录 ID/方法及标量，不输出参数、secret 或图像载荷。
 */
#pragma once
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>

namespace mini3d::performance {
inline qint64 nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
struct RequestTrace {
    QJsonObject fields;
    qint64 queuedNs = 0;
};
inline QHash<QString, RequestTrace> requests;
inline QString activeId, captureId;
inline double incomingJsonMs = 0;
inline QFile output;
inline QByteArray buffer;
inline void flush() {
    if (!buffer.isEmpty()) {
        output.write(buffer);
        buffer.clear();
        output.flush();
    }
}
inline void append(const QJsonObject& value) {
    if (!output.isOpen())
        return;
    buffer += QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    if (buffer.size() >= 64 * 1024)
        flush();
}
/** @brief 开始当前请求；只复制身份标量，不复制业务或认证参数。 */
inline void begin(const QString& id, const QString& method) {
    activeId = id;
    requests.insert(id, {{{"kind", "request"}, {"id", id}, {"method", method},
                          {"utf8BudgetJsonMs", incomingJsonMs}}, 0});
}
inline void record(const char* phase, double milliseconds) {
    if (requests.contains(activeId))
        requests[activeId].fields.insert(QLatin1String(phase), milliseconds);
}
inline void queued(const QString& id) {
    if (requests.contains(id))
        requests[id].queuedNs = nowNs();
}
inline void start(const QString& id) {
    activeId = id;
    if (requests.contains(id) && requests[id].queuedNs)
        record("queueWaitMs", double(nowNs() - requests[id].queuedNs) / 1e6);
}
/** @brief 输出完成状态；socket 仅开始写出，不将其误称为客户端已接收。 */
inline void complete(const QString& id, qint64 bytes, std::size_t queuedCount,
                     std::size_t cachedBytes, std::size_t sessions) {
    if (!requests.contains(id))
        return;
    auto trace = requests.take(id).fields;
    trace.insert("responseFrameBytes", double(bytes));
    trace.insert("queuedCount", double(queuedCount));
    trace.insert("ledgerCachedBytes", double(cachedBytes));
    trace.insert("sessionCount", double(sessions));
    append(trace);
    if (activeId == id)
        activeId.clear();
}
/** @brief 计量原 typed 调用及原结果编码；两者之间不改变实现或参数。 */
template <typename Domain, typename Encode>
auto domainAndEncode(Domain domain, Encode encode) {
    auto started = nowNs();
    auto result = domain();
    record("typedDomainMs", double(nowNs() - started) / 1e6);
    started = nowNs();
    auto encoded = encode(result);
    record("resultDtoEncodeMs", double(nowNs() - started) / 1e6);
    return encoded;
}
} // namespace mini3d::performance
