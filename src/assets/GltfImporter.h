/*
 * 模块名: GltfImporter
 * 功能概述: 静态 glTF 导入与 CPU 资源管理的 GltfImporter 数据/接口。
 * 对外接口: GltfImporter
 * 依赖关系: C++/GLM、Qt Gui
 * 输入输出: 输入资源描述，输出稳定资源引用或导入结果。
 * 异常与错误: 无效导入通过 error 返回，不执行 GPU 操作。
 * 维护说明: 使用 AssetId 关联，避免资源层反向依赖 Renderer。
 */
#pragma once
#include "MaterialAsset.h"
#include "MeshAsset.h"
#include "TextureAsset.h"
#include "core/Transform.h"

#include <QStringList>
#include <string>
#include <vector>
namespace mini3d::assets {
/** @brief glTF 节点描述，children 为当前导入数组索引，meshes 可含多个 primitive。 */
struct ImportedNode {
    std::string name;
    core::Transform transform;
    std::vector<std::size_t> children;
    std::vector<core::AssetId> meshes;
};
/** @brief 完成解析后的节点图，根节点对应默认 glTF Scene。 */
struct ImportedScene {
    std::vector<ImportedNode> nodes;
    std::vector<std::size_t> roots;
    QStringList warnings;
};
/** @brief 暂存导入结果，资源引用使用局部 1-based ID；成功后由 AssetManager 重映射。 */
struct ImportData {
    ImportedScene scene;
    std::vector<MeshAsset> meshes;
    std::vector<TextureAsset> textures;
    std::vector<MaterialAsset> materials;
    QString error;
};
/** @brief 同步只读文件解析，不修改 Scene/AssetManager，不执行 OpenGL。 */
class GltfImporter {
  public:
    /** @brief 读取静态 glTF/GLB；失败清空结果并返回包含路径与阶段的 error。 */
    [[nodiscard]] static ImportData read(const QString& path);
};
} // namespace mini3d::assets
