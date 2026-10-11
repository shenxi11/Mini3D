/*
 * 模块名: ImageDocumentTests
 * 功能概述: 检验关闭后验证、最终拒绝、私有暂存清理与 NewOnly 竞争保护。
 * 对外接口: Catch2 [image-document]，仅文件发布机制，不冒图像编码/GPU验收。
 * 依赖关系: ImageDocument、Qt Core、Win32；产物只在显式 E 盘测试根。
 * 输入输出: 测试 bytes/回调到新文件、字节保留和暂存残留断言。
 * 异常与错误: 无测试根或任一实际文件条件不符直接失败。
 * 维护说明: 不覆盖成品；每例使用独占目录，全部句柄在最后 guard 前关闭。
 */
#include "assets/ImageDocument.h"
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
QString scratchTemplate() {
    const auto root = qEnvironmentVariable("MINI3D_J1_TEST_ROOT");
    REQUIRE_FALSE(root.isEmpty());
    REQUIRE(QDir(root).exists());
    return QDir(root).filePath(QStringLiteral("image-document-XXXXXX"));
}
QByteArray readBytes(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
void createBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(file.write(bytes) == bytes.size());
    REQUIRE(file.flush());
}
QStringList files(const QString& path) {
    return QDir(path).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
}
} // namespace

TEST_CASE("Image NewOnly closes write and validation handles before the last guard", "[image-document]") {
    QTemporaryDir directory(scratchTemplate());
    REQUIRE(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("正式图像.bin"));
    const QByteArray bytes("closed encoded bytes\0with binary", 30);
    QString temporaryPath, error;
    bool validated = false, guarded = false;
    REQUIRE(assets::ImageDocument::write(path, bytes, error,
        [&](const QString& temporary, QString&) {
            temporaryPath = temporary;
            CHECK(QFileInfo(temporary).absolutePath() == directory.path());
            const auto native = QDir::toNativeSeparators(temporary).toStdWString();
            const auto exclusive = CreateFileW(native.c_str(), GENERIC_READ, 0, nullptr,
                                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            REQUIRE(exclusive != INVALID_HANDLE_VALUE);
            REQUIRE(CloseHandle(exclusive));
            validated = readBytes(temporary) == bytes;
            return validated;
        }, [&] {
            REQUIRE(validated);
            const auto native = QDir::toNativeSeparators(temporaryPath).toStdWString();
            const auto exclusive = CreateFileW(native.c_str(), GENERIC_READ, 0, nullptr,
                                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            REQUIRE(exclusive != INVALID_HANDLE_VALUE);
            REQUIRE(CloseHandle(exclusive));
            guarded = true;
            return true;
        }));
    CHECK(guarded);
    CHECK(error.isEmpty());
    CHECK(readBytes(path) == bytes);
    CHECK_FALSE(QFileInfo::exists(temporaryPath));
    CHECK(files(directory.path()) == QStringList{QStringLiteral("正式图像.bin")});
}

TEST_CASE("Image validation and final cancellation remove only the owned temporary", "[image-document]") {
    QTemporaryDir directory(scratchTemplate());
    REQUIRE(directory.isValid());
    const auto existing = directory.filePath("existing.bin");
    createBytes(existing, "user product");
    const auto target = directory.filePath("output.bin");
    QString error, temporaryPath;
    bool guarded = false;
    SECTION("Closed validator rejects") {
        CHECK_FALSE(assets::ImageDocument::write(target, "invalid bytes", error,
            [&](const QString& temporary, QString& diagnostic) {
                temporaryPath = temporary;
                REQUIRE(readBytes(temporary) == "invalid bytes");
                diagnostic = "invalid image";
                return false;
            }, [&] { guarded = true; return true; }));
        CHECK_FALSE(guarded);
    }
    SECTION("Final guard cancels after a closed file has been validated") {
        CHECK_FALSE(assets::ImageDocument::write(target, "valid bytes", error,
            [&](const QString& temporary, QString&) {
                temporaryPath = temporary;
                return readBytes(temporary) == "valid bytes";
            }, [&] { guarded = true; return false; }));
        CHECK(guarded);
    }
    CHECK_FALSE(QFileInfo::exists(target));
    CHECK_FALSE(QFileInfo::exists(temporaryPath));
    CHECK(readBytes(existing) == "user product");
    CHECK(files(directory.path()) == QStringList{"existing.bin"});
}

TEST_CASE("Image NewOnly preserves an existing or last-moment competing product", "[image-document]") {
    QTemporaryDir directory(scratchTemplate());
    REQUIRE(directory.isValid());
    const auto target = directory.filePath("output.bin");
    bool compete = false;
    SECTION("Already exists") { createBytes(target, "someone else's image"); }
    SECTION("Created after the final guard accepts") { compete = true; }
    QString error;
    bool denied = false;
    CHECK_FALSE(assets::ImageDocument::write(target, "this image", error,
        [](const QString&, QString&) { return true; }, [&] {
            if (compete) createBytes(target, "someone else's image");
            return true;
        }, &denied));
    CHECK(denied);
    CHECK(readBytes(target) == "someone else's image");
    CHECK(files(directory.path()) == QStringList{"output.bin"});
}
