/*
 * 模块名: FilePathPolicyTests
 * 功能概述: 验证明确读写根、中文路径、边界与危险 Windows 路径拒绝。
 * 对外接口: Catch2 [file-path-policy]。
 * 依赖关系: FilePathPolicy、Qt Core、Win32。
 * 输入输出: 隔离临时目录及路径到授权断言。
 * 异常与错误: 拒绝不能修改文件；权限不足的链接测试明确跳过而非算通过。
 * 维护说明: 仅创建本测试临时目录中的文件/链接，不触碰用户数据。
 */
#include "assets/SceneDocument.h"
#include "editor/automation/FilePathPolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#define NOMINMAX
#include <Windows.h>
using namespace mini3d;

namespace {
void writeFixture(const QString& path, const QByteArray& content = "test") {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(file.write(content) == content.size());
}
} // namespace

TEST_CASE("File roots grant only explicit ordinary local descendants", "[file-path-policy]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto root = directory.filePath("批准读写");
    const auto similar = directory.filePath("批准读写-other");
    REQUIRE(QDir().mkpath(root));
    REQUIRE(QDir().mkpath(root + "/sub"));
    REQUIRE(QDir().mkpath(similar));
    writeFixture(root + "/零件.txt");
    writeFixture(similar + "/零件.txt");
    QString error;
    const auto policy = editor::automation::FilePathPolicy::create({root}, {root}, error);
    INFO(error.toStdString());
    REQUIRE(policy.has_value());
    REQUIRE(policy->authorizeRead(root + "/零件.txt", error));
    REQUIRE(policy->authorizeRead(root + "/sub/../零件.txt", error));
    REQUIRE(policy->authorizeNewFile(root + "/sub/../normalized.m3dscene", error));
    REQUIRE_FALSE(policy->authorizeRead(root + "/../批准读写-other/零件.txt", error));
    REQUIRE(policy->authorizeNewFile(root + "/新场景.m3dscene", error));
    REQUIRE_FALSE(QFileInfo::exists(root + "/新场景.m3dscene"));
    REQUIRE_FALSE(policy->authorizeNewFile(root + "/零件.txt", error));
    REQUIRE_FALSE(policy->authorizeRead(similar + "/零件.txt", error));
    REQUIRE_FALSE(policy->authorizeNewFile(similar + "/new.txt", error));
    REQUIRE_FALSE(policy->authorizeRead(root, error));
    REQUIRE_FALSE(policy->authorizeRead(root + "/missing.txt", error));
    REQUIRE_FALSE(policy->authorizeNewFile(root + "/missing-parent/new.txt", error));
    for (const auto& bad :
         {"relative.txt", "E:relative.txt", "//server/share/a.txt", "//?/E:/a.txt", "//./NUL",
          "E:/a.txt:stream", "E:/NUL", "E:/a./x", "E:/a /x", "E:/a/../x", "E:/a/*"}) {
        REQUIRE_FALSE(policy->authorizeRead(QString::fromUtf8(bad), error));
        REQUIRE_FALSE(policy->authorizeNewFile(QString::fromUtf8(bad), error));
    }
    const auto empty = editor::automation::FilePathPolicy::create({}, {}, error);
    REQUIRE(empty.has_value());
    REQUIRE_FALSE(empty->authorizeRead(root + "/零件.txt", error));
    REQUIRE_FALSE(empty->authorizeNewFile(root + "/new.txt", error));
    REQUIRE_FALSE(editor::automation::FilePathPolicy::create({root + "/missing"}, {}, error));
}

TEST_CASE("File policy checks reparse ancestors without canonicalizing away the link",
          "[file-path-policy][path-link]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto approved = directory.filePath("approved");
    const auto outside = directory.filePath("outside");
    REQUIRE(QDir().mkpath(approved));
    REQUIRE(QDir().mkpath(outside));
    writeFixture(outside + "/private.txt");
    const auto link = approved + "/link";
    const auto nativeLink = QDir::toNativeSeparators(link).toStdWString();
    const auto nativeTarget = QDir::toNativeSeparators(outside).toStdWString();
    if (!CreateSymbolicLinkW(nativeLink.c_str(), nativeTarget.c_str(),
                             SYMBOLIC_LINK_FLAG_DIRECTORY |
                                 SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        SKIP("Current account cannot create an unprivileged symlink; reparse negative case "
             "unverified");
    QString error;
    const auto policy = editor::automation::FilePathPolicy::create({approved}, {approved}, error);
    REQUIRE(policy.has_value());
    REQUIRE_FALSE(policy->authorizeRead(link + "/private.txt", error));
    REQUIRE_FALSE(policy->authorizeNewFile(link + "/new.txt", error));
    REQUIRE_FALSE(policy->authorizeNewFile(link + "/../new.txt", error));
    REQUIRE_FALSE(editor::automation::FilePathPolicy::create({link}, {}, error));
    REQUIRE(RemoveDirectoryW(nativeLink.c_str()));
    REQUIRE(QFileInfo::exists(outside + "/private.txt"));
}

TEST_CASE("Scene top-level refusal preserves the supplied loaded document", "[file-path-policy]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("scene.m3dscene");
    writeFixture(path, "not-json");
    assets::LoadedScene loaded;
    const auto entity = loaded.scene.createEntity("unchanged");
    QString error;
    REQUIRE_FALSE(
        assets::SceneDocument::read(path, loaded, error, [](const QString&, QString& message) {
            message = "denied-before-open";
            return false;
        }));
    REQUIRE(error == "denied-before-open");
    REQUIRE(loaded.scene.find(entity) != nullptr);
}

TEST_CASE("Explicit isolated junction fixture is denied for read and new-file paths",
          "[file-path-policy][path-junction]") {
    const auto fixture = qEnvironmentVariable("MINI3D_TEST_JUNCTION_FIXTURE");
    if (fixture.isEmpty())
        SKIP("Isolated junction fixture not provided; run documented verify-assets.ps1");
    const auto approved = fixture + "/approved";
    const auto outside = fixture + "/outside";
    const auto link = approved + "/link";
    REQUIRE(QFileInfo::exists(outside + "/private.txt"));
    const auto nativeLink = QDir::toNativeSeparators(link).toStdWString();
    REQUIRE((GetFileAttributesW(nativeLink.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
    QString error;
    const auto policy = editor::automation::FilePathPolicy::create({approved}, {approved}, error);
    REQUIRE(policy.has_value());
    REQUIRE_FALSE(policy->authorizeRead(link + "/private.txt", error));
    REQUIRE_FALSE(policy->authorizeNewFile(link + "/new.txt", error));
    REQUIRE_FALSE(policy->authorizeNewFile(link + "/../new.txt", error));
    REQUIRE_FALSE(editor::automation::FilePathPolicy::create({link}, {}, error));
    REQUIRE(QFileInfo::exists(outside + "/private.txt"));
    REQUIRE_FALSE(QFileInfo::exists(outside + "/new.txt"));
}

TEST_CASE("Root checks use Windows filename comparison rather than Unicode folding",
          "[file-path-policy][path-unicode-root]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto approved = directory.filePath("K");
    const auto other = directory.filePath(QString(QChar(0x212A)));
    REQUIRE(QDir().mkpath(approved));
    REQUIRE(QDir().mkpath(other));
    writeFixture(other + "/private.txt");
    if (QFileInfo::exists(approved + "/private.txt"))
        SKIP("Filesystem considers K and Kelvin-sign directory aliases; distinct-root case absent");
    QString error;
    const auto policy = editor::automation::FilePathPolicy::create({approved}, {approved}, error);
    REQUIRE(policy.has_value());
    REQUIRE_FALSE(policy->authorizeRead(other + "/private.txt", error));
    REQUIRE_FALSE(policy->authorizeNewFile(other + "/new.txt", error));
}
