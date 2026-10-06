/*
 * 模块名: AutomationBridgeTests
 * 功能概述: 用真实同用户管道验证认证、恢复、序号、通知和精确停服。
 * 对外接口: Catch2 [automation-bridge]；仅要求 QCoreApplication，不创建窗口。
 * 依赖关系: Qt Core/Network/Test、LocalAutomationBridge、既有 API/ViewModel。
 * 输入输出: 独立描述文件和异步帧到业务状态及 wire 合同断言。
 * 异常与错误: 提升进程拒绝开启，跨用户部署权限不由本用例宣称完成。
 * 维护说明: 临时目录必须由调用者通过 TMPDIR 指向已验证任务目录。
 */
#include "editor/SceneViewModel.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"
#include "editor/automation/AutomationFrame.h"
#include "editor/automation/BridgeDescriptor.h"
#include "editor/automation/LocalAutomationBridge.h"
#include "editor/automation/WinLocalPipeServer.h"
#include "editor/automation/WinLocalSecurity.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif

using namespace mini3d;
namespace {
namespace automation = editor::automation;
namespace api = editor::api;
#ifdef Q_OS_WIN
using NativeHandle = std::unique_ptr<void, decltype(&CloseHandle)>;
using LocalMemory = std::unique_ptr<void, decltype(&LocalFree)>;
#endif
void ensureApplication() {
    static int argc = 1;
    static char name[] = "automation-tests";
    static char* argv[] = {name, nullptr};
    static std::unique_ptr<QCoreApplication> application;
    if (!QCoreApplication::instance())
        application = std::make_unique<QCoreApplication>(argc, argv);
}
QString temporaryPattern() {
    ensureApplication();
    const auto root = qEnvironmentVariable("TMPDIR");
    REQUIRE_FALSE(root.isEmpty());
    REQUIRE(QDir(root).exists());
    return QDir(root).filePath("mini3d-bridge-XXXXXX");
}
struct BridgeFixture {
    QTemporaryDir directory{temporaryPattern()};
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    automation::LocalAutomationBridge bridge{service};
    automation::LocalAutomationBridge::Options options;
    QJsonObject descriptor;
    BridgeFixture() {
        REQUIRE(directory.isValid());
        QString error;
        if (!automation::BridgeDescriptor::canEnable(error))
            SKIP("Current process cannot enable same-user automation; elevated/unsupported is rejected.");
        model.newScene();
        options.enabled = true;
        options.descriptorPath = QDir(directory.path()).filePath("instance.json");
        options.permissions = {"scene.read", "scene.write"};
        REQUIRE(bridge.start(options, error));
        REQUIRE(error.isEmpty());
        QFile file(bridge.descriptorPath());
        const bool descriptorOpened = file.open(QIODevice::ReadOnly);
        INFO(file.errorString().toStdString());
        REQUIRE(descriptorOpened);
        descriptor = QJsonDocument::fromJson(file.readAll()).object();
        REQUIRE(descriptor["secret"].toString().size() == 64);
        REQUIRE(descriptor["permissions"].toArray().size() == 2);
    }
    QJsonObject createParams(const QString& session, const QString& sequence = "1") const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        return {{"document", state["document"]},
                {"expectedDocumentRevision", state["documentRevision"]},
                {"primitive", "cube"}, {"name", "bridge cube"}, {"parentId", "0"},
                {"transform", QJsonObject{{"space", "local"}, {"translation", QJsonArray{0, 0, 0}},
                                           {"rotationQuaternion", QJsonArray{0, 0, 0, 1}},
                                           {"scale", QJsonArray{1, 1, 1}}}},
                {"surface", QJsonObject{{"tint", QJsonArray{1, 1, 1}}, {"useVertexColor", true},
                                         {"useTexture", true}}},
                {"clientSessionId", session}, {"mutationSequence", sequence}};
    }
};
QJsonObject rpc(const QJsonValue& id, const QString& method, const QJsonObject& params = {}) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
QString errorCode(const QJsonObject& response) {
    return response["error"].toObject()["data"].toObject()["code"].toString();
}
struct Client {
    QLocalSocket socket;
    automation::AutomationFrameDecoder decoder;
    std::vector<QJsonObject> responses;
    Client() {
        QObject::connect(&socket, &QLocalSocket::readyRead, &socket, [this] {
            REQUIRE(decoder.consume(socket.readAll(), [this](automation::AutomationFrame frame) {
                REQUIRE(frame.status == automation::FrameStatus::Json);
                responses.push_back(frame.document.object());
                return true;
            }));
        });
    }
    void connect(const QString& pipe) {
        socket.connectToServer(pipe);
        const auto connected = QTest::qWaitFor(
            [&] { return socket.state() == QLocalSocket::ConnectedState; }, 2500);
        INFO(socket.errorString().toStdString());
        REQUIRE(connected);
    }
    void send(const QJsonObject& request) {
        const auto frame = automation::encodeFrame(request);
        REQUIRE(socket.write(frame) == frame.size());
    }
    QJsonObject response(const QJsonValue& id) {
        auto find = [&] {
            return std::find_if(responses.begin(), responses.end(),
                                [&](const auto& response) { return response["id"] == id; });
        };
        REQUIRE(QTest::qWaitFor([&] { return find() != responses.end(); }, 2500));
        return *find();
    }
    QString hello(BridgeFixture& fixture, const QString& mode = "new", const QString& session = {}) {
        QJsonObject params{{"mode", mode}, {"instanceId", fixture.descriptor["instanceId"]},
                            {"bridgeId", fixture.descriptor["bridgeId"]},
                            {"secret", fixture.descriptor["secret"]}, {"apiVersion", "0.1.0"},
                            {"wireVersion", 1}};
        if (mode == "resume")
            params.insert("clientSessionId", session);
        send(rpc("hello", "bridge.hello", params));
        return response("hello")["result"].toObject()["clientSessionId"].toString();
    }
};
} // namespace

TEST_CASE("automation is disabled by default and rejects unsafe enablement", "[automation-bridge]") {
    ensureApplication();
    editor::SceneViewModel model;
    api::EditorApiService service(model);
    automation::LocalAutomationBridge bridge(service);
    automation::LocalAutomationBridge::Options options;
    QString error;
    REQUIRE(bridge.start(options, error));
    REQUIRE_FALSE(bridge.isRunning());
    options.enabled = true;
    options.descriptorPath = "";
    REQUIRE_FALSE(bridge.start(options, error));
    REQUIRE_FALSE(bridge.isRunning());
    REQUIRE_FALSE(error.isEmpty());
}
TEST_CASE("authenticated new and resume cannot replace a live session", "[automation-bridge]") {
    BridgeFixture fixture;
    Client first, second;
    first.connect(fixture.bridge.pipeName());
    first.send(rpc("unauthenticated", "document.current"));
    REQUIRE(errorCode(first.response("unauthenticated")) == "AUTH_FAILED");
    REQUIRE_FALSE(first.response("unauthenticated")["error"].toObject()["data"].toObject()
                      .contains("document"));
    const auto session = first.hello(fixture);
    REQUIRE_FALSE(session.isEmpty());
    second.connect(fixture.bridge.pipeName());
    REQUIRE(second.hello(fixture, "resume", session).isEmpty());
    REQUIRE(errorCode(second.response("hello")) == "SESSION_IN_USE");
    first.send(rpc("still-live", "document.current"));
    REQUIRE(first.response("still-live").contains("result"));
    first.socket.abort();
    REQUIRE(QTest::qWaitFor([&] { return fixture.bridge.connectionCount() == 1; }, 2500));
    second.responses.clear();
    REQUIRE(second.hello(fixture, "resume", session) == session);
}
TEST_CASE("mutation replay retains original result after independent document changes", "[automation-bridge]") {
    BridgeFixture fixture;
    Client client;
    client.connect(fixture.bridge.pipeName());
    const auto session = client.hello(fixture);
    const auto params = fixture.createParams(session);
    client.send(rpc("create", "entity.create", params));
    const auto created = client.response("create");
    REQUIRE(created["result"].toObject()["status"] == "committed");
    REQUIRE(created["bridge"].toObject()["highWater"] == "1");
    const auto originalRevision = created["result"].toObject()["documentRevision"];
    api::EntityCreateRequest independent;
    independent.document = fixture.service.documentState().document;
    independent.expectedDocumentRevision = fixture.service.documentState().documentRevision;
    independent.primitive = core::PrimitiveKind::Empty;
    independent.name = "independent GUI equivalent";
    REQUIRE(fixture.service.createEntity(independent).hasValue());
    auto retry = params;
    retry.insert("timeoutMs", int(api::limits::mutationTimeoutMs));
    auto document = retry["document"].toObject();
    document.insert("documentId", document["documentId"].toString().toUpper());
    retry.insert("document", document);
    client.send(rpc("replay", "entity.create", retry));
    const auto replay = client.response("replay");
    REQUIRE(replay["result"] == created["result"]);
    REQUIRE(replay["bridge"].toObject()["replayed"].toBool());
    REQUIRE(replay["bridge"].toObject()["committedDocumentRevision"] == originalRevision);
    REQUIRE(replay["bridge"].toObject()["currentState"].toObject()["documentRevision"] !=
            originalRevision);
    client.send(rpc("status", "bridge.requestStatus",
                     {{"clientSessionId", session}, {"mutationSequence", "1"}}));
    REQUIRE(client.response("status")["result"].toObject()["response"].toObject()["result"] ==
            created["result"]);
    retry.insert("name", "reused with changed body");
    client.send(rpc("different", "entity.create", retry));
    REQUIRE(errorCode(client.response("different")) == "REQUEST_KEY_REUSED");
}
TEST_CASE("domain mutation errors and their ledger replays never acquire a null result",
          "[automation-bridge][automation-error-envelope]") {
    BridgeFixture fixture;
    Client client;
    client.connect(fixture.bridge.pipeName());
    const auto session = client.hello(fixture);
    auto params = fixture.createParams(session);
    params.insert("parentId", "999");
    const auto before = fixture.service.documentState();
    client.send(rpc("bad-parent", "entity.create", params));
    const auto failed = client.response("bad-parent");
    REQUIRE(errorCode(failed) == "NOT_FOUND");
    CHECK_FALSE(failed.contains("result"));
    REQUIRE(failed["bridge"].toObject()["executionState"] == "completed");
    REQUIRE(failed["bridge"].toObject()["highWater"] == "1");
    REQUIRE(fixture.service.documentState().documentRevision == before.documentRevision);
    REQUIRE(fixture.model.undoStack()->count() == 0);
    REQUIRE(fixture.model.scene()->nodes().empty());
    client.send(rpc("replay-error", "entity.create", params));
    const auto replay = client.response("replay-error");
    REQUIRE(errorCode(replay) == "NOT_FOUND");
    CHECK_FALSE(replay.contains("result"));
    REQUIRE(replay["bridge"].toObject()["replayed"].toBool());
    client.send(rpc("error-status", "bridge.requestStatus",
                    {{"clientSessionId", session}, {"mutationSequence", "1"}}));
    const auto status = client.response("error-status")["result"].toObject();
    REQUIRE(status["state"] == "completed");
    CHECK_FALSE(status["response"].toObject().contains("result"));
    REQUIRE(errorCode(status["response"].toObject()) == "NOT_FOUND");
    client.send(rpc("valid-next", "entity.create", fixture.createParams(session, "2")));
    REQUIRE(client.response("valid-next")["result"].toObject()["status"] == "committed");
    REQUIRE(fixture.model.undoStack()->count() == 1);
}
TEST_CASE("legal notifications are ignored while malformed and batch frames get standard errors", "[automation-bridge]") {
    BridgeFixture fixture;
    Client client;
    client.connect(fixture.bridge.pipeName());
    const auto session = client.hello(fixture);
    const auto before = fixture.service.documentState();
    auto notification = rpc("discard", "entity.create", fixture.createParams(session));
    notification.remove("id");
    client.send(notification);
    client.send(rpc("barrier", "document.current"));
    REQUIRE(client.response("barrier").contains("result"));
    REQUIRE(fixture.service.documentState().documentRevision == before.documentRevision);
    REQUIRE(client.responses.size() == 2);
    const auto invalid = automation::encodeFrame({{"jsonrpc", "2.0"}, {"method", 7}});
    const QByteArray batch("[]");
    QByteArray batchFrame(4, '\0');
    qToLittleEndian<quint32>(quint32(batch.size()), batchFrame.data());
    batchFrame.append(batch);
    REQUIRE(client.socket.write(invalid + batchFrame) == invalid.size() + batchFrame.size());
    REQUIRE(QTest::qWaitFor([&] { return client.responses.size() == 4; }, 2500));
    REQUIRE(client.responses[2]["id"].isNull());
    REQUIRE(client.responses[2]["error"].toObject()["code"] == -32600);
    REQUIRE(client.responses[3]["error"].toObject()["code"] == -32600);
}
TEST_CASE("pending replay and queued cancellation execute at most one mutation", "[automation-bridge]") {
    BridgeFixture fixture;
    Client client;
    client.connect(fixture.bridge.pipeName());
    const auto session = client.hello(fixture);
    const auto params = fixture.createParams(session);
    const auto first = automation::encodeFrame(rpc("first", "entity.create", params));
    const auto duplicate = automation::encodeFrame(rpc("duplicate", "entity.create", params));
    const auto cancel = automation::encodeFrame(
        rpc("cancel", "bridge.cancel", {{"clientSessionId", session},
             {"target", QJsonObject{{"kind", "mutation"}, {"mutationSequence", "1"}}}}));
    const auto before = fixture.service.documentState();
    REQUIRE(client.socket.write(first + duplicate + cancel) == first.size() + duplicate.size() + cancel.size());
    REQUIRE(client.response("duplicate")["result"].toObject()["status"] == "pending");
    REQUIRE(client.response("duplicate")["bridge"].toObject()["executionState"] == "pending");
    REQUIRE(client.response("cancel")["result"].toObject()["status"] == "cancelled");
    REQUIRE(errorCode(client.response("first")) == "CANCELLED");
    REQUIRE(client.response("first")["bridge"].toObject()["highWater"] == "1");
    REQUIRE(fixture.service.documentState().documentRevision == before.documentRevision);
}
TEST_CASE("envelope rejection leaves sequence available and domain failure consumes it", "[automation-bridge]") {
    BridgeFixture fixture;
    Client client;
    client.connect(fixture.bridge.pipeName());
    const auto session = client.hello(fixture);
    auto params = fixture.createParams(session);
    auto envelope = params;
    envelope.remove("clientSessionId");
    client.send(rpc("bad-envelope", "entity.create", envelope));
    REQUIRE(errorCode(client.response("bad-envelope")) == "INVALID_ARGUMENT");
    params.insert("parentId", "18446744073709551615");
    client.send(rpc("domain", "entity.create", params));
    REQUIRE(client.response("domain").contains("error"));
    REQUIRE(client.response("domain")["bridge"].toObject()["highWater"] == "1");
    const auto next = fixture.createParams(session, "2");
    client.send(rpc("next", "entity.create", next));
    REQUIRE(client.response("next")["result"].toObject()["status"] == "committed");
}
TEST_CASE("transport failure affects one socket and service connections remain bounded", "[automation-bridge]") {
    BridgeFixture fixture;
    Client authenticated;
    authenticated.connect(fixture.bridge.pipeName());
    REQUIRE_FALSE(authenticated.hello(fixture).isEmpty());
    Client invalid;
    invalid.connect(fixture.bridge.pipeName());
    REQUIRE(invalid.socket.write(QByteArray(4, '\0')) == 4);
    REQUIRE(QTest::qWaitFor([&] { return invalid.socket.state() == QLocalSocket::UnconnectedState; }, 2500));
    authenticated.send(rpc("alive", "document.current"));
    REQUIRE(authenticated.response("alive").contains("result"));
    std::vector<std::unique_ptr<Client>> excess;
    for (std::size_t index = 0; index < api::limits::connections; ++index) {
        auto client = std::make_unique<Client>();
        client->socket.connectToServer(fixture.bridge.pipeName());
        excess.push_back(std::move(client));
    }
    authenticated.send(rpc("bounded", "document.current"));
    REQUIRE(authenticated.response("bounded").contains("result"));
    REQUIRE(fixture.bridge.connectionCount() <= api::limits::connections);
}
TEST_CASE("protected descriptor denies replacement and stop removes only owned file", "[automation-bridge]") {
    BridgeFixture fixture;
    const auto path = fixture.bridge.descriptorPath();
    QFile replacement(path);
    REQUIRE_FALSE(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
#ifdef Q_OS_WIN
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;
    const auto native = QDir::toNativeSeparators(path);
    REQUIRE(GetNamedSecurityInfoW(const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(native.utf16())),
                                 SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                 &dacl, nullptr, &descriptor) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    REQUIRE(GetSecurityDescriptorControl(descriptor, &control, &revision));
    REQUIRE((control & SE_DACL_PROTECTED) != 0);
    REQUIRE(dacl != nullptr);
    REQUIRE(dacl->AceCount == 1);
    QString securityError;
    const bool currentUserOnly =
        automation::winsecurity::verifyCurrentUserDacl(descriptor, securityError);
    LocalFree(descriptor);
    INFO(securityError.toStdString());
    REQUIRE(currentUserOnly);
#endif
    const auto sibling = QDir(fixture.directory.path()).filePath("sibling.txt");
    QFile siblingFile(sibling);
    REQUIRE(siblingFile.open(QIODevice::WriteOnly));
    REQUIRE(siblingFile.write("preserve") == 8);
    siblingFile.close();
    QString error;
    fixture.bridge.stop(&error);
    REQUIRE(error.isEmpty());
    REQUIRE_FALSE(QFileInfo::exists(path));
    REQUIRE(QFileInfo::exists(sibling));
    REQUIRE_FALSE(fixture.bridge.isRunning());
}
TEST_CASE("descriptor collision refuses to overwrite existing user data", "[automation-bridge]") {
    BridgeFixture fixture;
    fixture.bridge.stop();
    QFile existing(fixture.options.descriptorPath);
    REQUIRE(existing.open(QIODevice::WriteOnly));
    REQUIRE(existing.write("existing-user-file") == 18);
    existing.close();
    QString error;
    REQUIRE_FALSE(fixture.bridge.start(fixture.options, error));
    REQUIRE_FALSE(fixture.bridge.isRunning());
    REQUIRE(existing.open(QIODevice::ReadOnly));
    REQUIRE(existing.readAll() == "existing-user-file");
}

TEST_CASE("legacy DELETE handle reproduces ordinary descriptor reader sharing violation",
          "[automation-bridge][automation-descriptor-legacy]") {
#ifdef Q_OS_WIN
    QTemporaryDir directory(temporaryPattern());
    REQUIRE(directory.isValid());
    const auto path = QDir(directory.path()).filePath("legacy-reader.json");
    const auto native = QDir::toNativeSeparators(path);
    NativeHandle legacy(CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                   GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ,
                                   nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr),
                        &CloseHandle);
    REQUIRE(legacy.get() != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    REQUIRE(WriteFile(legacy.get(), "legacy", 6, &written, nullptr));
    REQUIRE(written == 6);
    QFile qtReader(path);
    const bool qtOpened = qtReader.open(QIODevice::ReadOnly);
    const auto ordinary = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto nativeError = GetLastError();
    NativeHandle reader(ordinary, &CloseHandle);
    qInfo("Legacy descriptor reader: QFile=%s; Windows error=%lu",
          qPrintable(qtReader.errorString()), static_cast<unsigned long>(nativeError));
    REQUIRE_FALSE(qtOpened);
    REQUIRE(reader.get() == INVALID_HANDLE_VALUE);
    REQUIRE(nativeError == ERROR_SHARING_VIOLATION);
#else
    SKIP("Windows descriptor sharing semantics are required.");
#endif
}
TEST_CASE("ordinary QFile and native readers can read a protected live descriptor",
          "[automation-bridge][automation-descriptor-reader]") {
    BridgeFixture fixture;
    QFile qtReader(fixture.bridge.descriptorPath());
    const bool opened = qtReader.open(QIODevice::ReadOnly);
    INFO(qtReader.errorString().toStdString());
    REQUIRE(opened);
    const auto expected = qtReader.readAll();
    const bool parsed = QJsonDocument::fromJson(expected).object() == fixture.descriptor;
    REQUIRE(parsed);
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(fixture.bridge.descriptorPath());
    NativeHandle reader(CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr),
                        &CloseHandle);
    const auto nativeError = reader.get() == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    INFO("Native ordinary reader error: " << nativeError);
    REQUIRE(reader.get() != INVALID_HANDLE_VALUE);
    QByteArray actual(expected.size(), '\0');
    DWORD count = 0;
    REQUIRE(ReadFile(reader.get(), actual.data(), DWORD(actual.size()), &count, nullptr));
    REQUIRE(count == DWORD(expected.size()));
    const bool sameContent = actual == expected;
    REQUIRE(sameContent);
    QString error;
    const bool currentUserOnly = automation::winsecurity::verifyFileSecurity(reader.get(), error);
    INFO(error.toStdString());
    REQUIRE(currentUserOnly);
#endif
}
TEST_CASE("descriptor rejects network and nonpersistent ACL volume metadata",
          "[automation-bridge][automation-security]") {
#ifdef Q_OS_WIN
    QString error;
    REQUIRE_FALSE(automation::winsecurity::localAclVolume(DRIVE_REMOTE, FILE_PERSISTENT_ACLS, error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE_FALSE(automation::winsecurity::localAclVolume(DRIVE_UNKNOWN, FILE_PERSISTENT_ACLS, error));
    REQUIRE_FALSE(
        automation::winsecurity::localAclVolume(DRIVE_NO_ROOT_DIR, FILE_PERSISTENT_ACLS, error));
    REQUIRE_FALSE(automation::winsecurity::localAclVolume(DRIVE_FIXED, 0, error));
    REQUIRE_FALSE(automation::winsecurity::localAclVolume(DRIVE_REMOVABLE, 0, error));
    error.clear();
    REQUIRE(automation::winsecurity::localAclVolume(DRIVE_FIXED, FILE_PERSISTENT_ACLS, error));
    REQUIRE(error.isEmpty());
#else
    SKIP("Windows volume capability metadata is required.");
#endif
}
TEST_CASE("descriptor rejects broad inherited or unprotected DACL data",
          "[automation-bridge][automation-security]") {
#ifdef Q_OS_WIN
    QString error;
    LocalMemory current(automation::winsecurity::currentUserDescriptor(error), &LocalFree);
    REQUIRE(current != nullptr);
    REQUIRE(automation::winsecurity::verifyCurrentUserDacl(current.get(), error));
    REQUIRE(SetSecurityDescriptorControl(current.get(), SE_DACL_PROTECTED, 0));
    REQUIRE_FALSE(automation::winsecurity::verifyCurrentUserDacl(current.get(), error));
    REQUIRE(SetSecurityDescriptorControl(current.get(), SE_DACL_PROTECTED, SE_DACL_PROTECTED));
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    REQUIRE(GetSecurityDescriptorDacl(current.get(), &present, &dacl, &defaulted));
    void* rawAce = nullptr;
    REQUIRE(GetAce(dacl, 0, &rawAce));
    auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(rawAce);
    ace->Header.AceFlags = INHERITED_ACE;
    REQUIRE_FALSE(automation::winsecurity::verifyCurrentUserDacl(current.get(), error));
    ace->Header.AceFlags = 0;
    ace->Mask = FILE_GENERIC_READ;
    REQUIRE_FALSE(automation::winsecurity::verifyCurrentUserDacl(current.get(), error));
    for (const auto* sddl : {L"D:P(A;;FA;;;WD)", L"D:P(A;;FA;;;WD)(A;;FA;;;SY)"}) {
        PSECURITY_DESCRIPTOR raw = nullptr;
        const bool converted = ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &raw, nullptr);
        LocalMemory broad(raw, &LocalFree);
        REQUIRE(converted);
        REQUIRE_FALSE(automation::winsecurity::verifyCurrentUserDacl(broad.get(), error));
    }
#else
    SKIP("Windows in-memory security descriptor validation is required.");
#endif
}
TEST_CASE("existing mapped network drive descriptor paths are rejected without writes",
          "[automation-bridge][automation-descriptor-platform]") {
#ifdef Q_OS_WIN
    const auto drives = GetLogicalDrives();
    REQUIRE(drives != 0);
    bool checked = false;
    for (unsigned int index = 0; index < 26; ++index) {
        if (!(drives & (DWORD(1) << index)))
            continue;
        const auto root = QString(QChar(ushort('A' + index))) + QStringLiteral(":/");
        const auto native = QDir::toNativeSeparators(root);
        if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(native.utf16())) != DRIVE_REMOTE)
            continue;
        checked = true;
        QString error;
        INFO(root.toStdString());
        REQUIRE_FALSE(automation::BridgeDescriptor::validatePath(
            root + "mini3d-descriptor-probe.json", error));
        REQUIRE_FALSE(error.isEmpty());
    }
    if (!checked)
        SKIP("No existing mapped network drive; metadata rejection is tested separately.");
#else
    SKIP("Windows existing drive inventory is required.");
#endif
}
TEST_CASE("existing local volumes without persistent ACLs reject descriptor paths",
          "[automation-bridge][automation-descriptor-platform]") {
#ifdef Q_OS_WIN
    const auto drives = GetLogicalDrives();
    REQUIRE(drives != 0);
    bool checked = false;
    for (unsigned int index = 0; index < 26; ++index) {
        if (!(drives & (DWORD(1) << index)))
            continue;
        const auto root = QString(QChar(ushort('A' + index))) + QStringLiteral(":/");
        const auto native = QDir::toNativeSeparators(root);
        const auto driveType = GetDriveTypeW(reinterpret_cast<LPCWSTR>(native.utf16()));
        if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE)
            continue;
        DWORD flags = 0;
        if (!GetVolumeInformationW(reinterpret_cast<LPCWSTR>(native.utf16()), nullptr, 0, nullptr,
                                   nullptr, &flags, nullptr, 0) || (flags & FILE_PERSISTENT_ACLS))
            continue;
        checked = true;
        QString error;
        INFO(root.toStdString());
        REQUIRE_FALSE(automation::BridgeDescriptor::validatePath(
            root + "mini3d-descriptor-probe.json", error));
        REQUIRE_FALSE(error.isEmpty());
    }
    if (!checked)
        SKIP("No accessible existing FAT/exFAT-like volume; metadata rejection is tested separately.");
#else
    SKIP("Windows existing volume inventory is required.");
#endif
}
TEST_CASE("stop preserves a replacement descriptor even with matching original bytes",
          "[automation-bridge][automation-descriptor-replacement]") {
    BridgeFixture fixture;
#ifdef Q_OS_WIN
    const auto path = fixture.bridge.descriptorPath();
    QFile original(path);
    REQUIRE(original.open(QIODevice::ReadOnly));
    const auto originalBytes = original.readAll();
    original.close();
    const auto renamed = QDir(fixture.directory.path()).filePath("owned-renamed.json");
    const auto native = QDir::toNativeSeparators(path);
    const auto nativeRenamed = QDir::toNativeSeparators(renamed);
    REQUIRE(MoveFileExW(reinterpret_cast<LPCWSTR>(native.utf16()),
                       reinterpret_cast<LPCWSTR>(nativeRenamed.utf16()), 0));
    QFile replacement(path);
    REQUIRE(replacement.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(replacement.write(originalBytes) == originalBytes.size());
    replacement.close();
    QString error;
    fixture.bridge.stop(&error);
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE_FALSE(fixture.bridge.isRunning());
    REQUIRE(QFileInfo::exists(renamed));
    REQUIRE(replacement.open(QIODevice::ReadOnly));
    const bool unchanged = replacement.readAll() == originalBytes;
    REQUIRE(unchanged);
#endif
}
TEST_CASE("native pipe mode rejects remote clients and freed connection slots can be reused",
          "[automation-bridge][automation-native-pipe]") {
    BridgeFixture fixture;
#ifdef Q_OS_WIN
    REQUIRE((automation::WinLocalPipeServer::creationMode() & PIPE_REJECT_REMOTE_CLIENTS) != 0);
    std::vector<std::unique_ptr<Client>> clients;
    for (std::size_t index = 0; index < api::limits::connections; ++index) {
        INFO("Native slot connection index: " << index);
        auto client = std::make_unique<Client>();
        client->connect(fixture.bridge.pipeName());
        clients.push_back(std::move(client));
        REQUIRE(QTest::qWaitFor(
            [&] { return fixture.bridge.connectionCount() == clients.size(); }, 2500));
    }
    const auto pipe = QStringLiteral("\\\\.\\pipe\\") + fixture.bridge.pipeName();
    NativeHandle excess(CreateFileW(reinterpret_cast<LPCWSTR>(pipe.utf16()),
                                   GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr),
                        &CloseHandle);
    const auto nativeError = GetLastError();
    REQUIRE(excess.get() == INVALID_HANDLE_VALUE);
    REQUIRE(nativeError == ERROR_PIPE_BUSY);
    clients.front()->socket.abort();
    REQUIRE(QTest::qWaitFor(
        [&] { return fixture.bridge.connectionCount() == api::limits::connections - 1; }, 2500));
    Client replacement;
    INFO("Native replacement after freeing slot");
    replacement.connect(fixture.bridge.pipeName());
    REQUIRE(QTest::qWaitFor(
        [&] { return fixture.bridge.connectionCount() == api::limits::connections; }, 2500));
    REQUIRE_FALSE(replacement.hello(fixture).isEmpty());
    replacement.send(rpc("slot-reused", "document.current"));
    REQUIRE(replacement.response("slot-reused").contains("result"));
#endif
}
TEST_CASE("stopping a pending native accept permits event-driven bridge restart",
          "[automation-bridge][automation-native-restart]") {
    BridgeFixture fixture;
    for (int iteration = 0; iteration < 3; ++iteration) {
        const auto oldPipe = fixture.bridge.pipeName();
        QString error;
        fixture.bridge.stop(&error);
        REQUIRE(error.isEmpty());
        REQUIRE_FALSE(fixture.bridge.isRunning());
        REQUIRE(QTest::qWaitFor([&] { return fixture.bridge.start(fixture.options, error); }, 2500));
        INFO(error.toStdString());
        REQUIRE(error.isEmpty());
        REQUIRE(fixture.bridge.pipeName() != oldPipe);
        Client client;
        client.connect(fixture.bridge.pipeName());
        REQUIRE(QTest::qWaitFor([&] { return fixture.bridge.connectionCount() == 1; }, 2500));
        client.socket.abort();
        REQUIRE(QTest::qWaitFor([&] { return fixture.bridge.connectionCount() == 0; }, 2500));
    }
    Client client;
    client.connect(fixture.bridge.pipeName());
    QString error;
    fixture.bridge.stop(&error);
    REQUIRE(error.isEmpty());
    REQUIRE(QTest::qWaitFor(
        [&] { return client.socket.state() == QLocalSocket::UnconnectedState; }, 2500));
}

TEST_CASE("file read permission explicitly permits import but not scene write methods",
          "[automation-bridge][automation-file-read-contract]") {
    BridgeFixture fixture;
    QString error;
    fixture.bridge.stop(&error);
    REQUIRE(error.isEmpty());
    fixture.options.permissions = {"scene.read", "file.read"};
    fixture.options.readRoots = {fixture.directory.path()};
    REQUIRE(fixture.bridge.start(fixture.options, error));
    QFile descriptor(fixture.bridge.descriptorPath());
    REQUIRE(descriptor.open(QIODevice::ReadOnly));
    fixture.descriptor = QJsonDocument::fromJson(descriptor.readAll()).object();
    descriptor.close();
    const float triangle[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const QByteArray positions(reinterpret_cast<const char*>(triangle), sizeof(triangle));
    auto source = QByteArray(
        R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data:application/octet-stream;base64,DATA","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})");
    source.replace("DATA", positions.toBase64());
    const auto path = fixture.directory.filePath("explicit-import.gltf");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(file.write(source) == source.size());
    file.close();
    Client client;
    client.connect(fixture.bridge.pipeName());
    const auto session = client.hello(fixture);
    REQUIRE_FALSE(session.isEmpty());
    auto params = fixture.createParams(session);
    params.remove("primitive");
    params.remove("surface");
    params.insert("path", path);
    params.insert("name", "explicit file-read import");
    client.send(rpc("import", "file.importGltf", params));
    const auto imported = client.response("import");
    INFO(QJsonDocument(imported).toJson(QJsonDocument::Compact).toStdString());
    REQUIRE(imported.contains("result"));
    CHECK(imported["result"].toObject()["status"] == "committed");
    CHECK(fixture.model.scene()->nodes().size() == 2);
    CHECK(fixture.model.undoStack()->count() == 1);
    const auto before = fixture.service.documentState();
    client.send(rpc("create-denied", "entity.create", fixture.createParams(session, "2")));
    CHECK(errorCode(client.response("create-denied")) == "PERMISSION_DENIED");
    client.send(rpc("new-denied", "document.new",
                    {{"document", api::ApiJsonCodec::encodeState(before)["document"]},
                     {"expectedDocumentRevision", QString::number(before.documentRevision)},
                     {"clientSessionId", session},
                     {"mutationSequence", "2"},
                     {"ifDirty", "discard"}}));
    CHECK(errorCode(client.response("new-denied")) == "PERMISSION_DENIED");
    CHECK(fixture.service.documentState().documentRevision == before.documentRevision);
    CHECK(fixture.service.documentState().historyRevision == before.historyRevision);
    CHECK(fixture.model.scene()->nodes().size() == 2);
    CHECK(fixture.model.undoStack()->count() == 1);
}
