/*
 * 模块名: SceneViewModel
 * 功能概述: 第三周场景选择与属性编辑的 SceneViewModel 层。
 * 对外接口: SceneViewModel
 * 依赖关系: Qt Widgets/Core、mini3d_core
 * 输入输出: 输入用户意图或场景通知，输出模型状态或界面刷新。
 * 异常与错误: 通过返回值和 operationFailed 报告非法编辑。
 * 维护说明: 同步 UI 线程操作；不持有节点地址或 GPU 资源。
 */
#pragma once
#include "SelectionModel.h"
#include "assets/AssetManager.h"
#include "core/Ray.h"
#include "core/SceneSerializer.h"

#include <QString>
#include <QUndoStack>
#include <memory>
#include <optional>
namespace mini3d::editor {
/** @brief 编辑意图入口，拥有 Scene 和选择状态；向视图发出结构/属性通知。 */
class SceneViewModel final : public QObject {
    Q_OBJECT
  public:
    explicit SceneViewModel(QObject* parent = nullptr);
    ~SceneViewModel() override;
    /** @brief 共享只读 Scene，Viewport 可安全持有；不暴露可变节点。 */
    [[nodiscard]] std::shared_ptr<const core::Scene> scene() const;
    [[nodiscard]] SelectionModel* selection();
    [[nodiscard]] std::shared_ptr<const assets::AssetManager> assets() const;
    /** @brief 同步导入并实例化默认场景；失败返回 0，保持现有场景和选择。 */
    core::EntityId importGltf(const QString& path);
    /** @brief 最近可见几何命中写入统一选择模型；点空白取消选择。 */
    void selectRay(const core::Ray& ray);
    /** @brief 在根层创建内置对象并选中，Empty 用作父节点容器。 */
    core::EntityId createEntity(core::PrimitiveKind primitive);
    /** @brief 创建相机于当前观察视角，或创建朝向兼容光源的方向灯；各形成一条历史。 */
    core::EntityId createCamera();
    /** @brief 从实际编辑视图姿态创建相机，支持精确顶视。 */
    core::EntityId createCameraFromView(const core::Transform& transform);
    core::EntityId createDirectionalLight();
    bool setCamera(core::EntityId id, const core::CameraComponent& camera);
    bool setLight(core::EntityId id, const core::LightComponent& light);
    /** @brief 只读预览不是文档编辑；0 退出，隐藏/删除/新建/打开也会退出。 */
    bool setPreviewCamera(core::EntityId id);
    [[nodiscard]] core::EntityId previewCamera() const;
    bool renameEntity(core::EntityId id, const QString& name);
    bool setVisible(core::EntityId id, bool visible);
    bool setParent(core::EntityId id, core::EntityId parent);
    /** @brief 写入完整变换；非法输入发出 operationFailed，原数据保持不变。 */
    bool setTransform(core::EntityId id, const core::Transform& transform);
    /** @brief 编辑局部轴分量，group:0位置/1欧拉角(度)/2缩放；旋转转回四元数。 */
    bool setTransformComponent(core::EntityId id, int group, int axis, double value);
    /** @brief 只读历史状态；修改必须通过本 ViewModel 的入口。 */
    [[nodiscard]] const QUndoStack* undoStack() const;
    void undo();
    void redo();
    /** @brief 开始一次可取消变换；预览不入栈，提交最多产生一条命令。 */
    void beginTransformEdit(core::EntityId id);
    void previewTransform(const core::Transform& transform);
    void finishTransformEdit(bool commit);
    void cancelTransformEdit();
    /** @brief 复制/删除选中子树，形成一条可撤销记录；无选择时不操作。 */
    void duplicateSelected();
    void deleteSelected();
    bool setSurface(core::EntityId id, const core::SurfaceStyle& surface);
    bool setLighting(const core::Lighting& lighting);
    /** @brief 文档新建/打开前，视图负责询问是否保存；读取失败不改当前状态。 */
    void newScene();
    bool openScene(const QString& path);
    bool saveScene(const QString& path);
    [[nodiscard]] QString filePath() const;
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] const core::CameraState& editorCamera() const;
    void setEditorCamera(const core::CameraState& camera);
  signals:
    void structureAboutToChange();
    void structureChanged();
    void entityChanged(core::EntityId id);
    void sceneChanged();
    void transformEditFinished();
    void documentChanged();
    void documentReset();
    void previewCameraChanged(core::EntityId id);
    void operationFailed(const QString& message);
    void operationCompleted(const QString& message);

  private:
    friend class TransformEntityCommand;
    friend class SubtreeCommand;
    void applyTransform(core::EntityId id, const core::Transform& transform);
    std::shared_ptr<core::Scene> scene_;
    SelectionModel selection_;
    std::shared_ptr<assets::AssetManager> assets_ = std::make_shared<assets::AssetManager>();
    struct TransformEdit {
        core::EntityId id;
        core::Transform before;
    };
    std::optional<TransformEdit> transformEdit_;
    QString filePath_;
    core::CameraState editorCamera_, savedCamera_;
    core::EntityId previewCamera_ = core::kInvalidEntity;
    QUndoStack history_;
};
} // namespace mini3d::editor
