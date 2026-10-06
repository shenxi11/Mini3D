/*
 * 模块名: AssetManager
 * 功能概述: 静态 glTF 导入与 CPU 资源管理的 AssetManager 数据/接口。
 * 对外接口: AssetManager
 * 依赖关系: C++/GLM、Qt Gui
 * 输入输出: 输入资源描述，输出稳定资源引用或导入结果。
 * 异常与错误: 无效导入通过 error 返回，不执行 GPU 操作。
 * 维护说明: 使用 AssetId 关联，避免资源层反向依赖 Renderer。
 */
#include "AssetManager.h"

#include <QFileInfo>
namespace mini3d::assets {
AssetManager::ImportResult AssetManager::importGltf(const QString& path,
                                                  const FileReadPolicy& policy) {
    QString permissionError;
    if (policy && !policy(path, permissionError))
        return {nullptr, permissionError, false, FileReadFailure::PathDenied};
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty()) {
        return {nullptr, QStringLiteral("资源文件不存在：%1").arg(path), false,
                FileReadFailure::IoError};
    }
    const QString key = canonical;
    if (const auto found = sourceCache_.constFind(key); !policy && found != sourceCache_.cend()) {
        return {*found, {}, true};
    }
    auto data = GltfImporter::read(canonical, policy);
    if (!data.error.isEmpty()) {
        return {nullptr, data.error, false, data.failure};
    }
    // 受控读取即使已有缓存也先核对本次所有依赖，不重复发布资源。
    if (const auto found = sourceCache_.constFind(key); found != sourceCache_.cend()) {
        for (const auto& dependency : sourceDependencies_.value(key)) {
            if (!policy(dependency, permissionError))
                return {nullptr, permissionError, false, FileReadFailure::PathDenied};
        }
        // 文件变化后不能拿本次获准依赖替旧缓存背书；既有 GUI 仍保持首次成功缓存语义。
        if (sourceHashes_.value(key) != data.sourceHash)
            return {nullptr, QStringLiteral("源文件已变化，无法确认旧缓存依赖的授权；请重新载入资源。"),
                    false, FileReadFailure::PathDenied};
        return {*found, {}, true};
    }
    // 只有完整解码/验证成功后才分配全局 ID；纹理 -> 材质 -> 网格 -> 节点引用。
    std::vector<core::AssetId> textures{core::kInvalidAsset}, materials{core::kInvalidAsset},
        meshes{core::kInvalidAsset};
    for (auto& texture : data.textures) {
        const auto id = nextId_++;
        textures_.emplace(id, std::move(texture));
        textures.push_back(id);
    }
    for (auto& material : data.materials) {
        material.baseColorTexture = textures.at(material.baseColorTexture);
        const auto id = nextId_++;
        materials_.emplace(id, std::move(material));
        materials.push_back(id);
    }
    for (auto& mesh : data.meshes) {
        mesh.material = materials.at(mesh.material);
        const auto id = nextId_++;
        meshSources_.emplace(id, MeshSource{canonical, meshes.size() - 1});
        meshes_.emplace(id, std::move(mesh));
        meshes.push_back(id);
    }
    for (auto& node : data.scene.nodes) {
        for (auto& mesh : node.meshes) {
            mesh = meshes.at(mesh);
        }
    }
    auto scene = std::make_shared<ImportedScene>(std::move(data.scene));
    sourceCache_.insert(key, scene);
    sourceHashes_.insert(key, data.sourceHash);
    sourceDependencies_.insert(key, data.dependencyPaths);
    sourceMeshes_.insert(key, std::vector<core::AssetId>(meshes.begin() + 1, meshes.end()));
    return {scene, {}, false};
}
const MeshAsset* AssetManager::mesh(core::AssetId id) const {
    const auto found = meshes_.find(id);
    return found == meshes_.end() ? nullptr : &found->second;
}
const TextureAsset* AssetManager::texture(core::AssetId id) const {
    const auto found = textures_.find(id);
    return found == textures_.end() ? nullptr : &found->second;
}
const MaterialAsset* AssetManager::material(core::AssetId id) const {
    const auto found = materials_.find(id);
    return found == materials_.end() ? nullptr : &found->second;
}
std::size_t AssetManager::meshCount() const {
    return meshes_.size();
}
const AssetManager::MeshSource* AssetManager::meshSource(core::AssetId id) const {
    const auto found = meshSources_.find(id);
    return found == meshSources_.end() ? nullptr : &found->second;
}
core::AssetId AssetManager::meshFromSource(const QString& path, std::size_t index) const {
    const auto found = sourceMeshes_.constFind(QFileInfo(path).canonicalFilePath());
    return found != sourceMeshes_.cend() && index < found->size() ? (*found)[index]
                                                                  : core::kInvalidAsset;
}
} // namespace mini3d::assets
