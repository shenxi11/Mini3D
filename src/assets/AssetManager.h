/*
 * 模块名: AssetManager
 * 功能概述: 静态 glTF 导入与 CPU 资源管理的 AssetManager 数据/接口。
 * 对外接口: AssetManager
 * 依赖关系: C++/GLM、Qt Gui
 * 输入输出: 输入资源描述，输出稳定资源引用或导入结果。
 * 异常与错误: 无效导入通过 error 返回，不执行 GPU 操作。
 * 维护说明: 使用 AssetId 关联，避免资源层反向依赖 Renderer。
 */
#pragma once
#include "GltfImporter.h"

#include <QHash>
#include <memory>
#include <unordered_map>
namespace mini3d::assets {
/** @brief CPU 资源唯一拥有者，源文件按规范化路径缓存，结果以只读节点图共享。 */
class AssetManager {
  public:
    struct ImportResult {
        std::shared_ptr<const ImportedScene> scene;
        QString error;
        bool cacheHit{false};
    };
    /** @brief 同一路径复用首次成功导入；失败不增加资源。外部文件变更需重启重新导入。 */
    [[nodiscard]] ImportResult importGltf(const QString& path);
    /** @brief 临时只读资源指针，无此 ID 返回 nullptr；不允许调用方保存可变引用。 */
    [[nodiscard]] const MeshAsset* mesh(core::AssetId id) const;
    [[nodiscard]] const TextureAsset* texture(core::AssetId id) const;
    [[nodiscard]] const MaterialAsset* material(core::AssetId id) const;
    [[nodiscard]] std::size_t meshCount() const;
    struct MeshSource {
        QString path;
        std::size_t index;
    };
    /** @brief 获取持久化来源；仅返回已成功导入的网格来源。 */
    [[nodiscard]] const MeshSource* meshSource(core::AssetId id) const;
    /** @brief 已导入文件的 primitive 序号到当前资源 ID；无此项返回 0。 */
    [[nodiscard]] core::AssetId meshFromSource(const QString& path, std::size_t index) const;

  private:
    core::AssetId nextId_{1};
    std::unordered_map<core::AssetId, MeshAsset> meshes_;
    std::unordered_map<core::AssetId, TextureAsset> textures_;
    std::unordered_map<core::AssetId, MaterialAsset> materials_;
    QHash<QString, std::shared_ptr<const ImportedScene>> sourceCache_;
    std::unordered_map<core::AssetId, MeshSource> meshSources_;
    QHash<QString, std::vector<core::AssetId>> sourceMeshes_;
};
} // namespace mini3d::assets
