/*
 * 模块名: LocalAutomationBridge
 * 功能概述: 事件驱动收发有界帧，在应用线程排队调用冻结 API 与真实观察。
 * 对外接口: LocalAutomationBridge.h。
 * 依赖关系: 原生本机管道/QLocalSocket、帧/账本/描述文件、既有编解码与业务。
 * 输入输出: 认证请求到领域 result/error 与独立 bridge 元数据。
 * 异常与错误: 传输错误关闭单连接；断线不回滚已接收命令或重新执行。
 * 维护说明: hello/status/cancel 无领域副作用；异步 capture 绑定会话和 RPC id。
 */
#include "LocalAutomationBridge.h"

#include "AutomationFrame.h"
#include "AutomationLedger.h"
#include "BridgeDescriptor.h"
#include "FilePathPolicy.h"
#include "WinLocalPipeServer.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"
#include "editor/observation/ObservationJsonCodec.h"
#include "editor/observation/ObservationService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <optional>

namespace mini3d::editor::automation {
namespace {
constexpr qsizetype transferChunkBytes = 64 * 1024;
bool isUuid(const QJsonValue& value) {
    static const QRegularExpression pattern(QStringLiteral(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return value.isString() && pattern.match(value.toString()).hasMatch();
}
bool validRequestId(const QJsonValue& value) {
    if (value.isString()) {
        const auto count = value.toString().toUcs4().size();
        return count >= 1 && count <= 128;
    }
    if (!value.isDouble())
        return false;
    const auto number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number &&
           std::abs(number) <= 9007199254740991.0;
}
bool objectFields(const QJsonObject& object, const QStringList& allowed,
                  const QStringList& required) {
    for (auto item = object.begin(); item != object.end(); ++item)
        if (!allowed.contains(item.key()))
            return false;
    for (const auto& key : required)
        if (!object.contains(key))
            return false;
    return true;
}
bool constantTimeEqual(const QString& left, const QString& right) {
    const auto first = left.toLatin1(), second = right.toLatin1();
    if (first.size() != second.size())
        return false;
    unsigned int different = 0;
    for (qsizetype index = 0; index < first.size(); ++index)
        different |= static_cast<unsigned char>(first[index]) ^
                     static_cast<unsigned char>(second[index]);
    return different == 0;
}
QJsonObject protocolError(int code, const QString& message) {
    return {{"error", QJsonObject{{"code", code}, {"message", message}}}};
}
QString requestKey(const QString& sessionId, const QJsonValue& requestId) {
    return sessionId + ':' +
           QString::fromUtf8(QJsonDocument(QJsonArray{requestId}).toJson(QJsonDocument::Compact));
}
} // namespace

class LocalAutomationBridge::Impl final {
  public:
    struct Connection {
        QPointer<QLocalSocket> socket;
        QTimer* authentication = nullptr;
        QTimer* slowWrite = nullptr;
        AutomationFrameDecoder decoder;
        QString id, sessionId;
        QByteArray output;
        qsizetype outputOffset = 0;
        bool drainScheduled = false, closing = false, closeAfterWrite = false;
    };
    struct Request {
        QString connectionId, sessionId, method;
        QJsonValue id;
        QJsonObject params;
        std::uint64_t sequence = 0, generation = 0;
        bool mutation = false, capture = false, started = false;
        QString captureId;
    };
    Impl(LocalAutomationBridge& owner, api::EditorApiService& service,
         observation::ObservationService* observation)
        : owner_(owner), service_(service), observation_(observation),
          server_(std::make_unique<WinLocalPipeServer>(
              [this] { return connections_.size(); },
              [this](QLocalSocket* socket) { acceptConnection(socket); },
              [this](const QString& error) {
                  qWarning("自动化桥本机入口已关闭：%s", qPrintable(error));
                  stop(nullptr);
              }, &owner)) {
        expiry_.setInterval(int(api::limits::disconnectedSessionRetentionMs));
        QObject::connect(&expiry_, &QTimer::timeout, &owner_, [this] { expireSessions(); });
    }
    bool start(const Options& options, QString& error) {
        error.clear();
        if (running_) {
            error = QStringLiteral("自动化桥已经开启。");
            return false;
        }
        if (!options.enabled)
            return true;
        if (!QCoreApplication::instance() ||
            QThread::currentThread() != QCoreApplication::instance()->thread() ||
            owner_.thread() != QThread::currentThread() ||
            (observation_ && observation_->thread() != owner_.thread())) {
            error = QStringLiteral("自动化桥必须装配并运行在应用线程。");
            return false;
        }
        if (!BridgeDescriptor::canEnable(error))
            return false;
        const QStringList allowedPermissions{"scene.read",       "scene.write", "viewport.observe",
                                             "viewport.control", "file.read",   "file.write"};
        QStringList permissions;
        for (const auto& permission : options.permissions) {
            if (!allowedPermissions.contains(permission) || permissions.contains(permission)) {
                error = QStringLiteral("启动权限包含未知或重复项。");
                return false;
            }
            permissions.append(permission);
        }
        const auto paths = FilePathPolicy::create(options.readRoots, options.writeRoots, error);
        if (!paths)
            return false;
        if ((permissions.contains("file.read") && paths->readRoots().isEmpty()) ||
            (permissions.contains("file.write") && paths->writeRoots().isEmpty())) {
            error = QStringLiteral("文件权限必须有明确的批准根目录。");
            return false;
        }
        const auto description = service_.describe();
        if (description.error) {
            error = description.error->message;
            return false;
        }
        methods_.clear();
        for (const auto& method : description.value->methods)
            methods_.emplace(method.name, method);
        if (observation_) {
            for (const auto& name : {"viewport.getState", "viewport.capture"})
                methods_.emplace(name, api::MethodDescription{name, "query", "viewport.observe", {},
                                                               true});
            for (const auto& name : {"viewport.setView", "viewport.focus"})
                methods_.emplace(name, api::MethodDescription{name, "mutation", "viewport.control",
                                                               {}, true});
        }
        bridgeId_ = BridgeDescriptor::randomUuid(error);
        const auto pipeId = BridgeDescriptor::randomUuid(error);
        secret_ = BridgeDescriptor::randomHex(32, error);
        if (bridgeId_.isEmpty() || pipeId.isEmpty() || secret_.size() != 64)
            return false;
        pipe_ = QStringLiteral("mini3d-") + pipeId;
        options_ = options;
        options_.permissions = permissions;
        options_.readRoots = paths->readRoots();
        options_.writeRoots = paths->writeRoots();
        // 原生创建每个实例时固定当前 SID DACL，并显式拒绝远程客户端。
        if (!server_->listen(pipe_, error)) {
            secret_.clear();
            return false;
        }
        const auto descriptor =
            QJsonObject{{"instanceId", service_.documentState().document.instanceId},
                        {"bridgeId", bridgeId_},
                        {"pid", double(QCoreApplication::applicationPid())},
                        {"startedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                        {"pipe", pipe_},
                        {"apiVersion", "0.2.0"},
                        {"wireVersion", 1},
                        {"secret", secret_},
                        {"permissions", QJsonArray::fromStringList(options_.permissions)},
                        {"readRoots", QJsonArray::fromStringList(options_.readRoots)},
                        {"writeRoots", QJsonArray::fromStringList(options_.writeRoots)}};
        if (!descriptor_.create(options_.descriptorPath, descriptor, error)) {
            server_->close();
            secret_.clear();
            return false;
        }
        assets::FileReadPolicy readPolicy, writePolicy, replacePolicy;
        if (options_.permissions.contains("file.read"))
            readPolicy = paths->readPolicy();
        if (options_.permissions.contains("file.write")) {
            writePolicy = [paths = *paths](const QString& path, QString& failure) {
                return paths.authorizeNewFile(path, failure);
            };
            replacePolicy = [paths = *paths](const QString& path, QString& failure) {
                return paths.authorizeWrite(path, failure);
            };
        }
        // 描述文件和 listen 全部成功之后，才开放与本次启动权限匹配的外部文件入口。
        service_.setFileAccessPolicies(std::move(readPolicy), std::move(writePolicy),
                                      std::move(replacePolicy));
        const auto effectiveDescription = service_.describe();
        for (const auto& method : effectiveDescription.value->methods)
            methods_.insert_or_assign(method.name, method);
        ++generation_;
        clock_.start();
        running_ = true;
        expiry_.start();
        return true;
    }
    void stop(QString* error) {
        if (running_)
            service_.setFileAccessPolicies({}, {});
        running_ = false;
        pumpScheduled_ = false;
        ++generation_;
        expiry_.stop();
        queue_.clear();
        const auto captures = std::move(captures_);
        captures_.clear();
        for (const auto& [key, request] : captures)
            if (observation_ && !request->captureId.isEmpty())
                observation_->cancelCapture(request->captureId);
        server_->close();
        const auto connections = connections_;
        for (const auto& [id, connection] : connections)
            closeConnection(connection);
        ledger_.clear();
        completedCaptures_.clear();
        QString removalError;
        descriptor_.removeOwned(removalError);
        if (error)
            *error = removalError;
        secret_.clear();
        bridgeId_.clear();
        pipe_.clear();
    }
    qint64 now() const {
        return clock_.isValid() ? clock_.elapsed() : 0;
    }
    void expireSessions() {
        ledger_.expire(now());
        for (auto item = completedCaptures_.begin(); item != completedCaptures_.end();) {
            if (!ledger_.snapshot(item->first))
                item = completedCaptures_.erase(item);
            else
                ++item;
        }
    }
    QJsonObject bridgeError(const QString& code, const QString& field = {},
                            bool authenticated = true) const {
        QString recovery = "none";
        if (code == "SEQUENCE_CONFLICT" || code == "SESSION_EXPIRED" || code == "RESULT_EXPIRED")
            recovery = "query_result";
        else if (code == "QUEUE_FULL" || code == "SESSION_LIMIT")
            recovery = "wait";
        else if (code == "INVALID_ARGUMENT" || code == "REQUEST_KEY_REUSED" ||
                 code == "LIMIT_EXCEEDED")
            recovery = "correct_input";
        QJsonObject data{{"code", code}, {"message", code}, {"recovery", recovery}};
        if (!field.isEmpty())
            data.insert("fieldPath", field);
        if (authenticated) {
            const auto state = api::ApiJsonCodec::encodeState(service_.documentState());
            for (auto item = state.begin(); item != state.end(); ++item)
                data.insert(item.key(), item.value());
        }
        return {{"error", QJsonObject{{"code", code == "INVALID_ARGUMENT" ? -32602 : -32010},
                                      {"message", code}, {"data", data}}}};
    }
    QJsonObject metadata(const QString& sessionId, std::uint64_t sequence, LedgerState state,
                         bool replayed, std::optional<std::uint64_t> committed = {}) const {
        const auto snapshot = ledger_.snapshot(sessionId).value_or(SessionSnapshot{});
        QJsonObject result{{"clientSessionId", sessionId},
                           {"mutationSequence", QString::number(sequence)},
                           {"executionState", state == LedgerState::Pending ? "pending" : "completed"},
                           {"highWater", QString::number(snapshot.highWater)},
                           {"nextMutationSequence", QString::number(snapshot.nextMutationSequence)},
                           {"replayed", replayed},
                           {"currentState", api::ApiJsonCodec::encodeState(service_.documentState())}};
        if (committed)
            result.insert("committedDocumentRevision", QString::number(*committed));
        return result;
    }
    void acceptConnection(QLocalSocket* socket) {
        if (!running_ || connections_.size() >= api::limits::connections) {
            socket->abort();
            socket->deleteLater();
            return;
        }
        auto connection = std::make_shared<Connection>();
        QString error;
        connection->id = BridgeDescriptor::randomUuid(error);
        if (connection->id.isEmpty()) {
            socket->abort();
            socket->deleteLater();
            return;
        }
        socket->setParent(&owner_);
        socket->setReadBufferSize(qint64(api::limits::requestBytes + 4));
        connection->socket = socket;
        connection->authentication = new QTimer(&owner_);
        connection->authentication->setSingleShot(true);
        connection->slowWrite = new QTimer(&owner_);
        connection->slowWrite->setSingleShot(true);
        connections_.emplace(connection->id, connection);
        const auto weak = std::weak_ptr(connection);
        QObject::connect(socket, &QLocalSocket::readyRead, &owner_, [this, weak] {
            if (const auto connection = weak.lock())
                scheduleDrain(connection);
        });
        QObject::connect(socket, &QLocalSocket::disconnected, &owner_, [this, weak] {
            if (const auto connection = weak.lock())
                closeConnection(connection);
        });
        QObject::connect(socket, &QLocalSocket::errorOccurred, &owner_, [this, weak] {
            if (const auto connection = weak.lock())
                closeConnection(connection);
        });
        QObject::connect(socket, &QLocalSocket::bytesWritten, &owner_, [this, weak] {
            if (const auto connection = weak.lock())
                writeOutput(connection);
        }, Qt::QueuedConnection);
        QObject::connect(connection->authentication, &QTimer::timeout, &owner_, [this, weak] {
            if (const auto connection = weak.lock())
                closeConnection(connection);
        });
        QObject::connect(connection->slowWrite, &QTimer::timeout, &owner_, [this, weak] {
            if (const auto connection = weak.lock())
                closeConnection(connection);
        });
        connection->authentication->start(int(api::limits::mutationTimeoutMs));
        scheduleDrain(connection);
    }
    void closeConnection(const std::shared_ptr<Connection>& connection) {
        if (connection->closing)
            return;
        connection->closing = true;
        connection->authentication->stop();
        connection->slowWrite->stop();
        ledger_.disconnect(connection->id, now());
        connections_.erase(connection->id);
        connection->output.clear();
        connection->decoder.reset();
        connection->authentication->deleteLater();
        connection->slowWrite->deleteLater();
        if (connection->socket) {
            connection->socket->abort();
            connection->socket->deleteLater();
        }
        server_->ensureListener();
    }
    void scheduleDrain(const std::shared_ptr<Connection>& connection) {
        if (connection->closing || connection->drainScheduled)
            return;
        connection->drainScheduled = true;
        const auto weak = std::weak_ptr(connection);
        QMetaObject::invokeMethod(&owner_, [this, weak] {
            const auto connection = weak.lock();
            if (!connection || connection->closing)
                return;
            connection->drainScheduled = false;
            const auto bytes = connection->socket->read(transferChunkBytes);
            if (!connection->decoder.consume(bytes, [this, connection](AutomationFrame frame) {
                    receiveFrame(connection, std::move(frame));
                    return !connection->closing;
                })) {
                closeConnection(connection);
                return;
            }
            if (!connection->closing && connection->socket->bytesAvailable() > 0)
                scheduleDrain(connection);
        }, Qt::QueuedConnection);
    }
    void send(const QString& connectionId, const QJsonValue& id, QJsonObject fragment) {
        const auto found = connections_.find(connectionId);
        if (found == connections_.end())
            return;
        const auto connection = found->second;
        fragment.insert("jsonrpc", "2.0");
        fragment.insert("id", id);
        auto frame = encodeFrame(fragment);
        if (frame.isEmpty()) {
            fragment = bridgeError("LIMIT_EXCEEDED");
            fragment.insert("jsonrpc", "2.0");
            fragment.insert("id", id);
            frame = encodeFrame(fragment);
        }
        const auto pending = connection->output.size() - connection->outputOffset +
                             connection->socket->bytesToWrite();
        if (pending + frame.size() > qint64(api::limits::responseBytes + 4)) {
            closeConnection(connection);
            return;
        }
        if (connection->outputOffset != 0) {
            connection->output.remove(0, connection->outputOffset);
            connection->outputOffset = 0;
        }
        connection->output.append(frame);
        if (!connection->slowWrite->isActive())
            connection->slowWrite->start(int(api::limits::mutationTimeoutMs));
        writeOutput(connection);
    }
    void writeOutput(const std::shared_ptr<Connection>& connection) {
        if (connection->closing || !connection->socket)
            return;
        if (!connection->output.isEmpty() &&
            connection->socket->bytesToWrite() < transferChunkBytes) {
            const auto count = std::min(connection->output.size() - connection->outputOffset,
                                        transferChunkBytes);
            const auto written = connection->socket->write(
                connection->output.constData() + connection->outputOffset, count);
            if (written < 0) {
                closeConnection(connection);
                return;
            }
            connection->outputOffset += written;
            if (connection->outputOffset == connection->output.size()) {
                connection->output.clear();
                connection->outputOffset = 0;
            }
        }
        if (connection->output.isEmpty() && connection->socket->bytesToWrite() == 0) {
            connection->slowWrite->stop();
            if (connection->closeAfterWrite)
                closeConnection(connection);
        }
    }
    void receiveFrame(const std::shared_ptr<Connection>& connection, AutomationFrame frame) {
        if (frame.status == FrameStatus::ParseError) {
            send(connection->id, QJsonValue::Null, protocolError(-32700, "Parse error"));
            return;
        }
        if (frame.status == FrameStatus::LimitExceeded) {
            send(connection->id, QJsonValue::Null,
                 bridgeError("LIMIT_EXCEEDED", {}, !connection->sessionId.isEmpty()));
            return;
        }
        if (frame.status != FrameStatus::Json || !frame.document.isObject()) {
            send(connection->id, QJsonValue::Null, protocolError(-32600, "Invalid Request"));
            return;
        }
        const auto object = frame.document.object();
        if (!objectFields(object, {"jsonrpc", "id", "method", "params"}, {"jsonrpc", "method"}) ||
            object["jsonrpc"] != "2.0" || !object["method"].isString() ||
            object["method"].toString().isEmpty() ||
            (object.contains("params") && !object["params"].isObject()) ||
            (object.contains("id") && !validRequestId(object["id"]))) {
            send(connection->id, QJsonValue::Null, protocolError(-32600, "Invalid Request"));
            return;
        }
        // 合法通知在认证和业务入口之前丢弃，永不执行也永不回应。
        if (!object.contains("id"))
            return;
        const auto method = object["method"].toString();
        const auto id = object["id"];
        const auto params = object["params"].toObject();
        if (method == "bridge.hello") {
            hello(connection, id, params);
            return;
        }
        if (connection->sessionId.isEmpty() ||
            !ledger_.isBound(connection->sessionId, connection->id)) {
            send(connection->id, id, bridgeError("AUTH_FAILED", {}, false));
            return;
        }
        if (method == "bridge.requestStatus") {
            requestStatus(connection, id, params);
            return;
        }
        if (method == "bridge.cancel") {
            cancel(connection, id, params);
            return;
        }
        const auto found = methods_.find(method);
        if (found == methods_.end()) {
            send(connection->id, id, protocolError(-32601, "Method not found"));
            return;
        }
        const auto& description = found->second;
        if (!options_.permissions.contains(description.permission) || !description.externalEnabled) {
            send(connection->id, id, bridgeError("PERMISSION_DENIED"));
            return;
        }
        auto request = std::make_shared<Request>();
        request->connectionId = connection->id;
        request->sessionId = connection->sessionId;
        request->method = method;
        request->id = id;
        request->params = params;
        request->generation = generation_;
        request->mutation = description.kind == "mutation" || description.kind == "file";
        request->capture = method == "viewport.capture";
        if (request->mutation) {
            acceptMutation(request);
            return;
        }
        if (request->capture && captures_.contains(requestKey(request->sessionId, request->id))) {
            send(connection->id, id, bridgeError("REQUEST_KEY_REUSED", "id"));
            return;
        }
        if (queue_.size() >= api::limits::queuedRequests) {
            send(connection->id, id, bridgeError("QUEUE_FULL"));
            return;
        }
        ledger_.retainRequest(request->sessionId);
        if (request->capture)
            captures_.emplace(requestKey(request->sessionId, request->id), request);
        queue_.push_back(std::move(request));
        schedulePump();
    }
    void hello(const std::shared_ptr<Connection>& connection, const QJsonValue& id,
               const QJsonObject& params) {
        if (!connection->sessionId.isEmpty()) {
            send(connection->id, id, bridgeError("SESSION_IN_USE", {}, false));
            return;
        }
        const QStringList required{"mode", "instanceId", "bridgeId", "secret", "apiVersion",
                                   "wireVersion"};
        auto allowed = required;
        allowed.append("clientSessionId");
        static const QRegularExpression secretPattern(QStringLiteral("^[0-9a-f]{64}$"));
        const auto mode = params["mode"].toString();
        if (!objectFields(params, allowed, required) || (mode != "new" && mode != "resume") ||
            !isUuid(params["instanceId"]) || !isUuid(params["bridgeId"]) ||
            !params["secret"].isString() ||
            !secretPattern.match(params["secret"].toString()).hasMatch() ||
            (mode == "new" && params.contains("clientSessionId")) ||
            (mode == "resume" && !isUuid(params["clientSessionId"]))) {
            send(connection->id, id, bridgeError("INVALID_ARGUMENT", "params", false));
            return;
        }
        if (params["apiVersion"] != "0.2.0" || params["wireVersion"] != 1) {
            send(connection->id, id, bridgeError("VERSION_MISMATCH", {}, false));
            return;
        }
        if (params["instanceId"].toString().toLower() !=
                service_.documentState().document.instanceId ||
            params["bridgeId"].toString().toLower() != bridgeId_ ||
            !constantTimeEqual(params["secret"].toString(), secret_)) {
            send(connection->id, id, bridgeError("AUTH_FAILED", {}, false));
            return;
        }
        SessionResult session;
        expireSessions();
        if (mode == "resume") {
            session = ledger_.resumeSession(params["clientSessionId"].toString().toLower(),
                                             connection->id, now());
        } else {
            QString error;
            const auto sessionId = BridgeDescriptor::randomUuid(error);
            if (sessionId.isEmpty()) {
                send(connection->id, id, bridgeError("AUTH_FAILED", {}, false));
                return;
            }
            session = ledger_.createSession(sessionId, connection->id, now());
        }
        if (!session.error.isEmpty()) {
            send(connection->id, id, bridgeError(session.error, {}, false));
            return;
        }
        connection->sessionId = session.sessionId;
        connection->authentication->stop();
        const auto snapshot = *ledger_.snapshot(session.sessionId);
        send(connection->id, id,
             {{"result", QJsonObject{{"instanceId", service_.documentState().document.instanceId},
                                      {"bridgeId", bridgeId_}, {"clientSessionId", session.sessionId},
                                      {"highWater", QString::number(snapshot.highWater)},
                                      {"nextMutationSequence", QString::number(snapshot.nextMutationSequence)},
                                      {"permissions", QJsonArray::fromStringList(options_.permissions)}}}});
    }
    bool matchedSession(const std::shared_ptr<Connection>& connection,
                        const QJsonObject& params) const {
        return isUuid(params["clientSessionId"]) &&
               params["clientSessionId"].toString().toLower() == connection->sessionId;
    }
    void requestStatus(const std::shared_ptr<Connection>& connection, const QJsonValue& id,
                       const QJsonObject& params) {
        if (!objectFields(params, {"clientSessionId", "mutationSequence"},
                          {"clientSessionId", "mutationSequence"}) ||
            !matchedSession(connection, params)) {
            send(connection->id, id, bridgeError("INVALID_ARGUMENT", "clientSessionId"));
            return;
        }
        const auto sequence = api::ApiJsonCodec::parseUint64(params["mutationSequence"],
                                                            "mutationSequence", false);
        if (sequence.error) {
            send(connection->id, id, {{"error", api::ApiJsonCodec::encodeError(*sequence.error)}});
            return;
        }
        const auto status = ledger_.status(connection->sessionId, *sequence.value);
        if (status.state == LedgerState::Rejected) {
            send(connection->id, id, bridgeError(status.error));
            return;
        }
        const auto snapshot = *ledger_.snapshot(connection->sessionId);
        const auto state = status.state == LedgerState::Pending ? "pending"
                           : status.state == LedgerState::Completed ? "completed"
                           : status.state == LedgerState::ResultExpired ? "result_expired"
                                                                        : "not_seen";
        QJsonObject result{{"state", state}, {"highWater", QString::number(snapshot.highWater)},
                           {"nextMutationSequence", QString::number(snapshot.nextMutationSequence)}};
        if (status.state == LedgerState::Completed) {
            result.insert("response", status.response);
            result.insert("bridge", metadata(connection->sessionId, *sequence.value,
                                               LedgerState::Completed, true,
                                               status.committedDocumentRevision));
        } else if (status.state == LedgerState::Pending) {
            result.insert("bridge", metadata(connection->sessionId, *sequence.value,
                                               LedgerState::Pending, true));
        }
        send(connection->id, id, {{"result", result}});
    }
    void acceptMutation(const std::shared_ptr<Request>& request) {
        const auto connection = connections_.at(request->connectionId);
        const auto& params = request->params;
        const auto sequence = api::ApiJsonCodec::parseUint64(params["mutationSequence"],
                                                            "mutationSequence", false);
        const auto timeout = params.contains("timeoutMs") ? params["timeoutMs"].toDouble(-1)
                                                          : double(api::limits::mutationTimeoutMs);
        if (!matchedSession(connection, params) || sequence.error ||
            (params.contains("timeoutMs") && !params["timeoutMs"].isDouble()) ||
            !std::isfinite(timeout) || std::floor(timeout) != timeout || timeout < 1 ||
            timeout > double(api::limits::mutationTimeoutMaximumMs)) {
            send(request->connectionId, request->id, bridgeError("INVALID_ARGUMENT", "params"));
            return;
        }
        request->sequence = *sequence.value;
        const auto acceptedAt = now();
        const auto normalized = request->method.startsWith("viewport.")
                                    ? observation::ObservationJsonCodec::canonicalParams(
                                          request->method, request->params)
                                    : api::ApiJsonCodec::canonicalParams(request->method,
                                                                         request->params);
        // 信封合法但领域解码失败仍须终结；无法形成 DTO 时保留确定性失败摘要。
        auto digestParams = normalized.value.value_or(request->params);
        digestParams.remove("clientSessionId");
        digestParams.remove("mutationSequence");
        digestParams.insert("timeoutMs", timeout);
        const auto admission = ledger_.accept(request->sessionId, request->sequence,
                                                canonicalDigest(request->method, digestParams),
                                                int(timeout), acceptedAt);
        if (admission.state == LedgerState::Completed || admission.state == LedgerState::Pending) {
            auto response = admission.state == LedgerState::Pending
                                ? QJsonObject{{"result", QJsonObject{{"status", "pending"}}}}
                                : admission.response;
            response.insert("bridge", metadata(request->sessionId, request->sequence,
                                                 admission.state, true,
                                                 admission.committedDocumentRevision));
            send(request->connectionId, request->id, std::move(response));
            return;
        }
        if (admission.state != LedgerState::Accepted) {
            send(request->connectionId, request->id, bridgeError(admission.error));
            return;
        }
        if (normalized.error) {
            auto failure = *normalized.error;
            failure.state = service_.documentState();
            finishMutation(request, {{"error", api::ApiJsonCodec::encodeError(failure)}});
            return;
        }
        if (queue_.size() >= api::limits::queuedRequests) {
            finishMutation(request, bridgeError("QUEUE_FULL"));
            return;
        }
        queue_.push_back(request);
        schedulePump();
    }
    void schedulePump() {
        if (!running_ || pumpScheduled_)
            return;
        pumpScheduled_ = true;
        const auto generation = generation_;
        QMetaObject::invokeMethod(&owner_, [this, generation] {
            if (generation != generation_)
                return;
            pumpScheduled_ = false;
            pump();
        }, Qt::QueuedConnection);
    }
    void pump() {
        if (!running_)
            return;
        const auto eligible = std::find_if(queue_.begin(), queue_.end(), [this](const auto& request) {
            return std::none_of(captures_.begin(), captures_.end(), [&](const auto& item) {
                return item.second->sessionId == request->sessionId && item.second->started;
            });
        });
        if (eligible == queue_.end())
            return;
        const auto request = *eligible;
        queue_.erase(eligible);
        if (request->mutation && ledger_.isDeadlineExceeded(request->sessionId, request->sequence,
                                                            now())) {
            finishMutation(request, bridgeError("DEADLINE_EXCEEDED"));
            schedulePump();
            return;
        }
        request->started = true;
        if (request->mutation && !ledger_.start(request->sessionId, request->sequence)) {
            schedulePump();
            return;
        }
        const QPointer<LocalAutomationBridge> alive(&owner_);
        const auto callback = [this, alive, request](QJsonObject response) {
            if (!alive || request->generation != generation_)
                return;
            if (request->mutation)
                finishMutation(request, std::move(response));
            else
                finishQuery(request, std::move(response));
            schedulePump();
        };
        const api::BeforeCommitGuard guard = [this, alive, request]() -> std::optional<api::ApiError> {
            if (!alive)
                return api::ApiError{api::ErrorCode::Cancelled, "CANCELLED", {}, api::Recovery::None,
                                      {}};
            if (request->generation != generation_ || !running_)
                return api::ApiError{api::ErrorCode::Cancelled, "CANCELLED", {}, api::Recovery::None,
                                      service_.documentState()};
            if (ledger_.isDeadlineExceeded(request->sessionId, request->sequence, now()))
                return api::ApiError{api::ErrorCode::DeadlineExceeded, "DEADLINE_EXCEEDED", {},
                                      api::Recovery::None, service_.documentState()};
            return {};
        };
        if (request->method.startsWith("viewport.")) {
            if (!observation_) {
                callback(bridgeError("PERMISSION_DENIED"));
                return;
            }
            request->captureId = observation_->invoke(request->method, request->params, callback,
                                                        request->mutation ? guard : api::BeforeCommitGuard{});
        } else {
            auto response = api::ApiJsonCodec::invoke(service_, request->method, request->params,
                                                       api::FileAccess::External,
                                                       request->mutation ? guard : api::BeforeCommitGuard{});
            if (request->method == "system.describe" && response.contains("result"))
                addBridgeDescription(response);
            callback(std::move(response));
        }
        schedulePump();
    }
    void addBridgeDescription(QJsonObject& response) const {
        auto result = response["result"].toObject();
        QJsonArray methods;
        for (const auto& [name, method] : methods_) {
            methods.append(QJsonObject{{"name", name}, {"kind", method.kind},
                                        {"permission", method.permission},
                                        {"externalEnabled", method.externalEnabled &&
                                                                options_.permissions.contains(method.permission)},
                                        {"externalGate", method.externalGate}});
        }
        result.insert("methods", methods);
        result.insert("privateWireProfile", QJsonObject{{"batch", false},
                                                         {"notificationsExecuted", false},
                                                         {"framing", "uint32-le/UTF-8"},
                                                         {"requestIdRequired", true}});
        response.insert("result", result);
    }
    void finishMutation(const std::shared_ptr<Request>& request, QJsonObject response) {
        if (request->generation != generation_)
            return;
        std::optional<std::uint64_t> committed;
        auto result = response.value("result").toObject();
        if (result["status"] == "committed" || result["status"] == "opened" ||
            result["status"] == "saved") {
            if (result.contains("view"))
                result = result["view"].toObject();
            const auto revision = api::ApiJsonCodec::parseUint64(result["documentRevision"]);
            if (revision.value)
                committed = *revision.value;
        }
        // 编码或输出失败不触碰领域结果；原始 result/error 先入账本。
        ledger_.complete(request->sessionId, request->sequence, response, committed, now());
        response.insert("bridge", metadata(request->sessionId, request->sequence,
                                             LedgerState::Completed, false, committed));
        send(request->connectionId, request->id, std::move(response));
    }
    void finishQuery(const std::shared_ptr<Request>& request, QJsonObject response) {
        if (request->generation != generation_)
            return;
        if (request->capture) {
            const auto key = requestKey(request->sessionId, request->id);
            if (captures_.erase(key) == 0)
                return;
            auto& completed = completedCaptures_[request->sessionId];
            completed.push_back(key);
            while (completed.size() > api::limits::cachedResultsPerSession)
                completed.pop_front();
        }
        ledger_.releaseRequest(request->sessionId, now());
        send(request->connectionId, request->id, std::move(response));
    }
    void cancel(const std::shared_ptr<Connection>& connection, const QJsonValue& id,
                const QJsonObject& params) {
        if (!objectFields(params, {"clientSessionId", "target"}, {"clientSessionId", "target"}) ||
            !matchedSession(connection, params) || !params["target"].isObject()) {
            send(connection->id, id, bridgeError("INVALID_ARGUMENT", "params"));
            return;
        }
        const auto target = params["target"].toObject();
        const auto kind = target["kind"].toString();
        QString status;
        if (kind == "mutation") {
            const auto sequence = api::ApiJsonCodec::parseUint64(target["mutationSequence"],
                                                                "target.mutationSequence", false);
            if (!objectFields(target, {"kind", "mutationSequence"}, {"kind", "mutationSequence"}) ||
                sequence.error) {
                send(connection->id, id, bridgeError("INVALID_ARGUMENT", "target"));
                return;
            }
            status = ledger_.cancellationStatus(connection->sessionId, *sequence.value);
            if (status == "cancelled") {
                const auto found = std::find_if(queue_.begin(), queue_.end(), [&](const auto& request) {
                    return request->mutation && request->sessionId == connection->sessionId &&
                           request->sequence == *sequence.value;
                });
                if (found != queue_.end()) {
                    const auto request = *found;
                    queue_.erase(found);
                    finishMutation(request, bridgeError("CANCELLED"));
                }
            }
        } else if (kind == "request") {
            if (!objectFields(target, {"kind", "requestId"}, {"kind", "requestId"}) ||
                !validRequestId(target["requestId"])) {
                send(connection->id, id, bridgeError("INVALID_ARGUMENT", "target"));
                return;
            }
            const auto key = requestKey(connection->sessionId, target["requestId"]);
            const auto found = captures_.find(key);
            if (found == captures_.end()) {
                const auto completed = completedCaptures_.find(connection->sessionId);
                status = completed != completedCaptures_.end() &&
                                 std::find(completed->second.begin(), completed->second.end(), key) !=
                                     completed->second.end()
                             ? "already_completed" : "not_seen";
            } else {
                const auto request = found->second;
                if (!request->started) {
                    const auto queued = std::find(queue_.begin(), queue_.end(), request);
                    if (queued != queue_.end())
                        queue_.erase(queued);
                    finishQuery(request, bridgeError("CANCELLED"));
                    status = "cancelled";
                } else if (observation_ && observation_->cancelCapture(request->captureId)) {
                    status = "cancelled";
                } else {
                    status = "already_completed";
                }
            }
        } else {
            send(connection->id, id, bridgeError("INVALID_ARGUMENT", "target.kind"));
            return;
        }
        send(connection->id, id, {{"result", QJsonObject{{"status", status}}}});
        schedulePump();
    }
    LocalAutomationBridge& owner_;
    api::EditorApiService& service_;
    QPointer<observation::ObservationService> observation_;
    std::unique_ptr<WinLocalPipeServer> server_;
    BridgeDescriptor descriptor_;
    AutomationLedger ledger_;
    QElapsedTimer clock_;
    QTimer expiry_;
    Options options_;
    QString bridgeId_, pipe_, secret_;
    std::map<QString, api::MethodDescription> methods_;
    std::map<QString, std::shared_ptr<Connection>> connections_;
    std::deque<std::shared_ptr<Request>> queue_;
    std::map<QString, std::shared_ptr<Request>> captures_;
    std::map<QString, std::deque<QString>> completedCaptures_;
    bool running_ = false, pumpScheduled_ = false;
    std::uint64_t generation_ = 0;
};

LocalAutomationBridge::LocalAutomationBridge(api::EditorApiService& service,
                                             observation::ObservationService* observation,
                                             QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, service, observation)) {}
LocalAutomationBridge::~LocalAutomationBridge() {
    impl_->stop(nullptr);
}
bool LocalAutomationBridge::start(const Options& options, QString& error) {
    return impl_->start(options, error);
}
void LocalAutomationBridge::stop(QString* error) {
    impl_->stop(error);
}
bool LocalAutomationBridge::isRunning() const {
    return impl_->running_;
}
QString LocalAutomationBridge::descriptorPath() const {
    return impl_->descriptor_.path();
}
QString LocalAutomationBridge::pipeName() const {
    return impl_->pipe_;
}
QString LocalAutomationBridge::bridgeId() const {
    return impl_->bridgeId_;
}
std::size_t LocalAutomationBridge::connectionCount() const {
    return impl_->connections_.size();
}
std::size_t LocalAutomationBridge::queuedCount() const {
    return impl_->queue_.size();
}
} // namespace mini3d::editor::automation
