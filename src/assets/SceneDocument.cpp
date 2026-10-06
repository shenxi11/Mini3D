/*
 * 模块名: SceneDocument
 * 功能概述: 解析场景相对资源；GUI 原子替换保存，API 另存原子拒绝覆盖。
 * 对外接口: SceneDocument
 * 依赖关系: Qt Core、SceneSerializer
 * 输入输出: UTF-8 场景文件、CPU 数据和诊断字符串。
 * 异常与错误: 缺失/损坏资源和写入错误不会覆盖当前文档或旧文件。
 * 维护说明: 不使用网络或后台线程；源 glTF 改动的热重载不在范围内。
 */
#include "SceneDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <unordered_set>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace mini3d::assets {
namespace {
bool writeNewOnly(const QString& path, const std::string& text, QString& error,
                  const std::function<bool()>& beforeCommit, bool* overwriteDenied) {
    const QFileInfo target(path);
    QString temporaryPath;
    // QTemporaryFile::close() 不释放原生句柄；必须销毁对象后再发布。
    {
        QTemporaryFile file(target.absoluteDir().filePath(QStringLiteral(".mini3d-scene-XXXXXX")));
        if (!file.open() ||
            file.write(text.data(), static_cast<qint64>(text.size())) !=
                static_cast<qint64>(text.size()) ||
            !file.flush()) {
            error = QStringLiteral("场景临时文件写入失败。");
            return false;
        }
        temporaryPath = file.fileName();
        file.setAutoRemove(false);
    }
    auto cleanup = qScopeGuard([&] {
        QFile::remove(temporaryPath);
    });
    const auto source = QDir::toNativeSeparators(temporaryPath).toStdWString();
    const auto destination = QDir::toNativeSeparators(target.absoluteFilePath()).toStdWString();
    if (beforeCommit && !beforeCommit()) {
        error = QStringLiteral("场景文件提交已取消。");
        return false;
    }
    // 同目录暂存保证同卷；不带 REPLACE_EXISTING，最终竞争也不能覆盖既有目标。
    if (!MoveFileExW(source.c_str(), destination.c_str(), 0)) {
        const auto failure = GetLastError();
        const bool exists = failure == ERROR_ALREADY_EXISTS || failure == ERROR_FILE_EXISTS;
        if (overwriteDenied)
            *overwriteDenied = exists;
        error = exists ? QStringLiteral("另存目标已存在，禁止覆盖。")
                       : QStringLiteral("场景文件发布失败：系统错误 %1。").arg(failure);
        return false;
    }
    cleanup.dismiss();
    error.clear();
    return true;
}
} // namespace
bool SceneDocument::read(const QString& path, LoadedScene& result, QString& error,
                         const FileReadPolicy& policy, FileReadFailure* failure) {
    const auto classify = [failure](FileReadFailure kind) {
        if (failure)
            *failure = kind;
    };
    classify(FileReadFailure::InvalidData);
    if (policy && !policy(path, error)) {
        classify(FileReadFailure::PathDenied);
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        classify(FileReadFailure::IoError);
        error = QStringLiteral("场景文件读写失败：%1：%2").arg(path, file.errorString());
        return false;
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        classify(FileReadFailure::IoError);
        error = QStringLiteral("场景文件读写失败：%1：%2").arg(path, file.errorString());
        return false;
    }
    core::SceneDocumentData data;
    std::string decodingError;
    if (!core::SceneSerializer::decode(bytes.toStdString(), data, decodingError)) {
        error =
            QStringLiteral("场景解析失败：%1：%2").arg(path, QString::fromStdString(decodingError));
        return false;
    }
    LoadedScene loaded;
    loaded.assets = std::make_shared<AssetManager>();
    const QDir directory = QFileInfo(path).absoluteDir();
    std::unordered_map<core::AssetId, core::MeshRendererComponent> references;
    for (const auto& source : data.assets) {
        const auto relative = QString::fromStdString(source.path);
        if (QDir::isAbsolutePath(relative)) {
            classify(policy ? FileReadFailure::PathDenied : FileReadFailure::InvalidData);
            error = QStringLiteral("场景资源必须使用相对路径：%1").arg(relative);
            return false;
        }
        const auto sourcePath = directory.absoluteFilePath(relative);
        const auto imported = loaded.assets->importGltf(sourcePath, policy);
        if (!imported.scene) {
            classify(imported.failure);
            error = imported.error;
            return false;
        }
        const auto mesh = loaded.assets->meshFromSource(sourcePath, source.meshIndex);
        if (mesh == core::kInvalidAsset) {
            error =
                QStringLiteral("%1：子网格索引 %2 已不存在").arg(sourcePath).arg(source.meshIndex);
            return false;
        }
        references.emplace(source.id,
                           core::MeshRendererComponent{mesh, loaded.assets->mesh(mesh)->material});
    }
    for (auto& node : data.nodes) {
        if (node.meshRenderer) {
            node.meshRenderer = references.at(node.meshRenderer->mesh);
        }
    }
    if (!loaded.scene.replaceNodes(data.nodes, data.editableMeshes, data.collections) ||
        !loaded.scene.setLighting(data.lighting)) {
        error = QStringLiteral("载入的场景状态无效");
        return false;
    }
    loaded.camera = data.camera;
    loaded.cursor = data.cursor;
    loaded.sourceVersion = data.sourceVersion;
    result = std::move(loaded);
    classify(FileReadFailure::None);
    error.clear();
    return true;
}
bool SceneDocument::write(const QString& path, const core::Scene& scene, const AssetManager& assets,
                          const core::CameraState& camera, QString& error,
                          const core::Cursor3D& cursor,
                          const std::function<bool()>& beforeCommit, WriteMode mode,
                          bool* overwriteDenied) {
    if (overwriteDenied)
        *overwriteDenied = false;
    if (!cursor.isValid()) {
        error = QStringLiteral("3D 游标坐标必须为有限数字");
        return false;
    }
    if (!camera.isValid()) {
        error = QStringLiteral("编辑视图相机参数无效");
        return false;
    }
    core::SceneDocumentData data;
    data.nodes = scene.nodes();
    data.editableMeshes = scene.editableMeshes();
    data.collections = scene.collections();
    data.camera = camera;
    data.cursor = cursor;
    data.lighting = scene.lighting();
    const QDir directory = QFileInfo(path).absoluteDir();
    std::unordered_set<core::AssetId> seen;
    for (const auto& node : data.nodes) {
        if (!node.meshRenderer || !seen.insert(node.meshRenderer->mesh).second) {
            continue;
        }
        const auto* source = assets.meshSource(node.meshRenderer->mesh);
        if (!source) {
            error = QStringLiteral("网格缺少可保存的来源信息");
            return false;
        }
        const auto relative = directory.relativeFilePath(source->path);
        if (QDir::isAbsolutePath(relative)) {
            error = QStringLiteral("资源位于其他磁盘；请将场景保存到资源所在磁盘：%1")
                        .arg(source->path);
            return false;
        }
        data.assets.push_back({node.meshRenderer->mesh, relative.toStdString(), source->index});
    }
    std::string text;
    try {
        text = core::SceneSerializer::encode(data);
    } catch (const std::exception& failure) {
        error = QStringLiteral("场景编码失败：%1").arg(QString::fromUtf8(failure.what()));
        return false;
    }
    if (mode == WriteMode::NewOnly)
        return writeNewOnly(path, text, error, beforeCommit, overwriteDenied);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(text.data(), static_cast<qint64>(text.size())) !=
            static_cast<qint64>(text.size())) {
        error = QStringLiteral("场景文件读写失败：%1：%2").arg(path, file.errorString());
        return false;
    }
    if (beforeCommit && !beforeCommit()) {
        file.cancelWriting();
        error = QStringLiteral("场景文件提交已取消。");
        return false;
    }
    if (!file.commit()) {
        error = QStringLiteral("场景文件读写失败：%1：%2").arg(path, file.errorString());
        return false;
    }
    error.clear();
    return true;
}
} // namespace mini3d::assets
