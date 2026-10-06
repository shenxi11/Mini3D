/*
 * 模块名: ControlledFileWriteTests
 * 功能概述: 验证批准写根、新文件发布竞态和 OBJ 最终守卫的字节保留。
 * 对外接口: Catch2 [controlled-file-write] 真实文件系统用例。
 * 依赖关系: FilePathPolicy、ObjDocument、Qt Core、Win32。
 * 输入输出: 隔离临时文件和授权回调到发布结果、字节及残留暂存断言。
 * 异常与错误: 越根、链接、已存在目标或最终拒绝不得改写原字节。
 * 维护说明: 运行者将 TEMP/TMP/TMPDIR 定向至任务隔离目录；不触碰用户文件。
 */
#include "assets/ObjDocument.h"
#include "editor/automation/FilePathPolicy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace mini3d;
namespace {
void writeFixture(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}
QByteArray readFixture(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
QStringList directoryFiles(const QString& path) {
    return QDir(path).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
}
} // namespace

TEST_CASE("Write authorization accepts ordinary approved targets without widening new-file access",
          "[controlled-file-write][file-path-policy]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto approved = directory.filePath("批准写入");
    const auto readOnly = directory.filePath("只读根");
    const auto sibling = directory.filePath("批准写入-other");
    REQUIRE(QDir().mkpath(approved + "/sub"));
    REQUIRE(QDir().mkpath(readOnly));
    REQUIRE(QDir().mkpath(sibling));
    const auto existing = approved + "/现有.obj";
    writeFixture(existing, "old bytes");
    writeFixture(readOnly + "/old.obj", "read only");
    writeFixture(sibling + "/old.obj", "outside");
    QString error;
    const auto policy = editor::automation::FilePathPolicy::create({readOnly}, {approved}, error);
    REQUIRE(policy);
    CHECK(policy->authorizeWrite(existing, error));
    CHECK(error.isEmpty());
    CHECK_FALSE(policy->authorizeNewFile(existing, error));
    CHECK(policy->authorizeWrite(approved + "/sub/../new.obj", error));
    CHECK_FALSE(QFileInfo::exists(approved + "/new.obj"));
    CHECK_FALSE(policy->authorizeWrite(approved, error));
    CHECK_FALSE(policy->authorizeWrite(approved + "/sub", error));
    CHECK_FALSE(policy->authorizeWrite(approved + "/missing-parent/new.obj", error));
    CHECK_FALSE(policy->authorizeWrite(readOnly + "/old.obj", error));
    CHECK_FALSE(policy->authorizeWrite(readOnly + "/new.obj", error));
    CHECK_FALSE(policy->authorizeWrite(sibling + "/old.obj", error));
    CHECK_FALSE(policy->authorizeWrite(sibling + "/new.obj", error));
    CHECK_FALSE(policy->authorizeWrite(approved + "/../批准写入-other/old.obj", error));
    for (const auto& bad :
         {QStringLiteral("relative.obj"), QStringLiteral("E:relative.obj"),
          QStringLiteral("//server/share/a.obj"), QStringLiteral("//?/E:/a.obj"),
          QStringLiteral("//./NUL"), existing + ":stream", approved + "/NUL.obj",
          approved + "/bad./new.obj", approved + "/bad /new.obj", approved + "/*"}) {
        CHECK_FALSE(policy->authorizeWrite(bad, error));
        CHECK_FALSE(error.isEmpty());
    }
    const auto noWrite = editor::automation::FilePathPolicy::create({approved}, {}, error);
    REQUIRE(noWrite);
    CHECK_FALSE(noWrite->authorizeWrite(existing, error));
    CHECK_FALSE(noWrite->authorizeWrite(approved + "/new.obj", error));
    CHECK(readFixture(existing) == "old bytes");
}

TEST_CASE("Write authorization rechecks raw link ancestors and linked targets",
          "[controlled-file-write][file-path-policy][path-link]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto approved = directory.filePath("approved");
    const auto outside = directory.filePath("outside");
    REQUIRE(QDir().mkpath(approved));
    REQUIRE(QDir().mkpath(outside));
    writeFixture(outside + "/old.obj", "outside bytes");
    const auto link = approved + "/link";
    const auto nativeLink = QDir::toNativeSeparators(link).toStdWString();
    const auto nativeTarget = QDir::toNativeSeparators(outside).toStdWString();
    if (!CreateSymbolicLinkW(nativeLink.c_str(), nativeTarget.c_str(),
                             SYMBOLIC_LINK_FLAG_DIRECTORY |
                                 SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        SKIP("Current account cannot create a symlink; linked write-root case unverified");
    QString error;
    const auto policy = editor::automation::FilePathPolicy::create({}, {approved}, error);
    REQUIRE(policy);
    CHECK_FALSE(policy->authorizeWrite(link + "/old.obj", error));
    CHECK_FALSE(policy->authorizeWrite(link + "/new.obj", error));
    CHECK_FALSE(policy->authorizeWrite(link + "/../new.obj", error));
    CHECK_FALSE(policy->authorizeWrite(link, error));
    CHECK(readFixture(outside + "/old.obj") == "outside bytes");
    CHECK_FALSE(QFileInfo::exists(outside + "/new.obj"));
    REQUIRE(RemoveDirectoryW(nativeLink.c_str()));
}

TEST_CASE("OBJ default replacement stays compatible and final refusal preserves old bytes",
          "[controlled-file-write][obj-document]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("对象.obj");
    const QByteArray before("old bytes\n");
    writeFixture(path, before);
    QString error;
    SECTION("old GUI signature replaces an existing ordinary file") {
        REQUIRE(assets::ObjDocument::write(path, "v 1 2 3\n", error));
        CHECK(error.isEmpty());
        CHECK(readFixture(path) == "v 1 2 3\n");
    }
    SECTION("complete staging precedes the final guard and false preserves the target") {
        int calls = 0;
        bool overwriteDenied = true;
        const auto guard = [&] {
            ++calls;
            CHECK(readFixture(path) == before);
            return false;
        };
        CHECK_FALSE(assets::ObjDocument::write(path, "v 1 2 3\n", error, guard,
                                               assets::ObjDocument::WriteMode::ReplaceExisting,
                                               &overwriteDenied));
        CHECK(calls == 1);
        CHECK_FALSE(error.isEmpty());
        CHECK_FALSE(overwriteDenied);
        CHECK(readFixture(path) == before);
    }
    CHECK(directoryFiles(directory.path()) == QStringList{QStringLiteral("对象.obj")});
}

TEST_CASE("OBJ NewOnly publishes a new target and rejects existing or race-created bytes",
          "[controlled-file-write][obj-document]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("new.obj");
    QString error;
    bool overwriteDenied = true;
    int calls = 0;
    SECTION("new target is published only after the final guard") {
        const auto guard = [&] {
            ++calls;
            CHECK_FALSE(QFileInfo::exists(path));
            return true;
        };
        REQUIRE(assets::ObjDocument::write(path, "v 1 2 3\n", error, guard,
                                           assets::ObjDocument::WriteMode::NewOnly,
                                           &overwriteDenied));
        CHECK(error.isEmpty());
        CHECK_FALSE(overwriteDenied);
        CHECK(readFixture(path) == "v 1 2 3\n");
    }
    SECTION("existing target is not replaced") {
        writeFixture(path, "original");
        CHECK_FALSE(assets::ObjDocument::write(
            path, "v 1 2 3\n", error,
            [&] {
                ++calls;
                return true;
            },
            assets::ObjDocument::WriteMode::NewOnly, &overwriteDenied));
        CHECK(overwriteDenied);
        CHECK_FALSE(error.isEmpty());
        CHECK(readFixture(path) == "original");
    }
    SECTION("target created at the final guard wins without being overwritten") {
        const auto guard = [&] {
            ++calls;
            CHECK_FALSE(QFileInfo::exists(path));
            writeFixture(path, "racing writer");
            return true;
        };
        CHECK_FALSE(assets::ObjDocument::write(path, "v 1 2 3\n", error, guard,
                                               assets::ObjDocument::WriteMode::NewOnly,
                                               &overwriteDenied));
        CHECK(overwriteDenied);
        CHECK_FALSE(error.isEmpty());
        CHECK(readFixture(path) == "racing writer");
    }
    SECTION("final refusal leaves no destination or temporary file") {
        CHECK_FALSE(assets::ObjDocument::write(
            path, "v 1 2 3\n", error,
            [&] {
                ++calls;
                return false;
            },
            assets::ObjDocument::WriteMode::NewOnly, &overwriteDenied));
        CHECK_FALSE(overwriteDenied);
        CHECK_FALSE(QFileInfo::exists(path));
        CHECK(directoryFiles(directory.path()).empty());
    }
    CHECK(calls == 1);
    const auto files = directoryFiles(directory.path());
    CHECK((files.empty() || files == QStringList{QStringLiteral("new.obj")}));
}

TEST_CASE("OBJ final write-root recheck prevents replacement after an ancestor becomes a link",
          "[controlled-file-write][obj-document][path-link]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto approved = directory.filePath("approved");
    const auto stagingParent = approved + "/sub";
    const auto displaced = approved + "/sub-moved";
    const auto outside = directory.filePath("outside");
    REQUIRE(QDir().mkpath(stagingParent));
    REQUIRE(QDir().mkpath(outside));
    writeFixture(stagingParent + "/old.obj", "approved original");
    writeFixture(outside + "/old.obj", "outside original");
    QString error;
    const auto policy = editor::automation::FilePathPolicy::create({}, {approved}, error);
    REQUIRE(policy);
    const auto path = stagingParent + "/old.obj";
    REQUIRE(policy->authorizeWrite(path, error));
    const auto nativeLink = QDir::toNativeSeparators(stagingParent).toStdWString();
    const auto nativeTarget = QDir::toNativeSeparators(outside).toStdWString();
    const auto probeLink = approved + "/probe-link";
    const auto nativeProbe = QDir::toNativeSeparators(probeLink).toStdWString();
    if (!CreateSymbolicLinkW(nativeProbe.c_str(), nativeTarget.c_str(),
                             SYMBOLIC_LINK_FLAG_DIRECTORY |
                                 SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        SKIP("Current account cannot create a symlink; final ancestor recheck unverified");
    REQUIRE(RemoveDirectoryW(nativeProbe.c_str()));
    bool authorized = true;
    const auto guard = [&] {
        REQUIRE(QDir().rename(stagingParent, displaced));
        REQUIRE(CreateSymbolicLinkW(nativeLink.c_str(), nativeTarget.c_str(),
                                    SYMBOLIC_LINK_FLAG_DIRECTORY |
                                        SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE));
        authorized = policy->authorizeWrite(path, error);
        return authorized;
    };
    CHECK_FALSE(assets::ObjDocument::write(path, "replacement", error, guard,
                                           assets::ObjDocument::WriteMode::ReplaceExisting));
    CHECK_FALSE(authorized);
    CHECK(readFixture(outside + "/old.obj") == "outside original");
    CHECK(readFixture(displaced + "/old.obj") == "approved original");
    REQUIRE(RemoveDirectoryW(nativeLink.c_str()));
}
