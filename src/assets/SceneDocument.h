/*
 * 模块名: SceneDocument
 * 功能概述: 原子写入场景文件，临时加载并重映射 CPU 资源引用。
 * 对外接口: LoadedScene、SceneDocument
 * 依赖关系: SceneSerializer、AssetManager、Qt Core 文件操作
 * 输入输出: 文件路径和编辑数据到可发布的完整文档。
 * 异常与错误: 文件或资源失败返回 false 和错误，不发布部分结果。
 * 维护说明: 不持有 GPU 对象；资源相对场景文件目录，不复制模型文件。
 */
#pragma once
#include "AssetManager.h"
#include "core/SceneSerializer.h"
namespace mini3d::assets {
struct LoadedScene {
    core::Scene scene;
    std::shared_ptr<AssetManager> assets;
    core::CameraState camera;
    int sourceVersion = 4;
    core::Cursor3D cursor;
};
/** @brief 同步文档 IO 服务；读取成功前调用者不得替换当前编辑状态。 */
class SceneDocument {
  public:
    /** @brief GUI 默认替换保存；API 另存只原子发布到不存在的新目标。 */
    enum class WriteMode { ReplaceExisting, NewOnly };
    static bool read(const QString& path, LoadedScene& result, QString& error,
                     const FileReadPolicy& policy = {}, FileReadFailure* failure = nullptr);
    static bool write(const QString& path, const core::Scene& scene, const AssetManager& assets,
                      const core::CameraState& camera, QString& error,
                      const core::Cursor3D& cursor = {},
                      const std::function<bool()>& beforeCommit = {},
                      WriteMode mode = WriteMode::ReplaceExisting,
                      bool* overwriteDenied = nullptr);
};
} // namespace mini3d::assets
