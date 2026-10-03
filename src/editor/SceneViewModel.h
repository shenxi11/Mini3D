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
#include "ComponentSelection.h"
#include "SelectionModel.h"
#include "assets/AssetManager.h"
#include "core/Ray.h"
#include "core/SceneSerializer.h"
#include "core/ViewportVisibility.h"
#include "core/modeling/BevelEdge.h"
#include "core/modeling/ExtrudeRegion.h"
#include "core/modeling/InsetFace.h"
#include "core/modeling/LoopCut.h"
#include "operations/HistoryService.h"

#include <QString>
#include <QUndoStack>
#include <memory>
#include <optional>
namespace mini3d::editor {
enum class TransformPivot { Median, Active, Cursor };
enum class SnapMode { Increment, Vertex };
/** @brief 编辑意图入口，拥有 Scene 和选择状态；向视图发出结构/属性通知。 */
class SceneViewModel final : public QObject {
    Q_OBJECT
  public:
    explicit SceneViewModel(QObject* parent = nullptr);
    ~SceneViewModel() override;
    /** @brief 共享只读 Scene，Viewport 可安全持有；不暴露可变节点。 */
    [[nodiscard]] std::shared_ptr<const core::Scene> scene() const;
    [[nodiscard]] SelectionModel* selection();
    /** @brief 单对象编辑上下文；不等同于工作区，不将模式或选择变化压入历史。 */
    [[nodiscard]] bool isEditMode() const;
    [[nodiscard]] core::EntityId editedEntity() const;
    [[nodiscard]] QString editModeDisabledReason() const;
    bool setEditMode(bool enabled);
    [[nodiscard]] const ComponentSelection& componentSelection() const;
    [[nodiscard]] quint64 componentSelectionRevision() const;
    void setSelectionDomain(SelectionDomain domain);
    void selectComponent(ComponentId id, SelectionOperation operation);
    void selectComponents(const std::set<ComponentId>& ids, SelectionOperation operation);
    void selectAllComponents();
    void clearComponentSelection();
    /** @brief 规则四边源边的Loop/Ring选择，不修改几何或历史，隐藏结果被过滤。 */
    bool selectEdgePath(core::modeling::EdgeKey seed, bool ring,
                        SelectionOperation operation = SelectionOperation::Replace);
    /** @brief 选择种子所属源连通片；缺省以活动组件为种子，投影到当前点/边/面域。 */
    bool selectConnected(std::optional<ComponentId> seed = std::nullopt);
    /** @brief 视口会话掩码；隐藏/恢复/隔离不修改工程、不入历史。 */
    [[nodiscard]] const core::ViewportVisibility& viewportVisibility() const;
    [[nodiscard]] bool isComponentVisible(ComponentId id) const;
    bool hideSelection();
    bool revealHidden();
    bool toggleLocalView();
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
    /** @brief 可见的所选相机、最近预览相机、最小 ID 相机，依序选择；没有则返回 0。 */
    [[nodiscard]] core::EntityId previewCameraCandidate() const;
    /** @brief 切换只读预览；无可见相机时报告原因，不创建相机或修改文档。 */
    bool toggleCameraPreview();
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
    /** @brief 当前顶端挤出的世界位移参数；上下文不可用时为空，不等同于 redo。 */
    [[nodiscard]] std::optional<glm::dvec3> lastOperationWorldOffset() const;
    [[nodiscard]] std::optional<double> lastOperationInsetThickness() const;
    [[nodiscard]] std::optional<double> lastOperationBevelWidth() const;
    [[nodiscard]] QString lastOperationDisabledReason() const;
    /** @brief 从原 before 重算同一操作，失败保留几何、历史与保存点。 */
    bool adjustLastOperation(const glm::dvec3& worldOffset);
    /** @brief 替换末端内插的局部厚度；不对挤出或其他类型接受厚度参数。 */
    bool adjustLastInset(double localThickness);
    /** @brief 从倒角原始网格及边键重算宽度，失败保持几何、选区、历史和保存点。 */
    bool adjustLastBevel(double localWidth);
    /** @brief 用末端挤出/内插/倒角的参数在当前选区新执行一次，区别于F9替换。 */
    bool repeatLastOperation();
    [[nodiscard]] QString repeatLastOperationDisabledReason() const;
    /** @brief 开始一次可取消变换；预览不入栈，提交最多产生一条命令。 */
    void beginTransformEdit(core::EntityId id);
    void previewTransform(const core::Transform& transform);
    void finishTransformEdit(bool commit);
    void cancelTransformEdit();
    /** @brief 组件事务独立候选，不改 scene() 真源；确认只安装一次，失败保持可修正状态。 */
    bool beginComponentTransform(const QString& label = QStringLiteral("变换组件"));
    /** @brief 验证选中面域并启动安全挤出；复用组件候选/确认/取消与唯一历史。 */
    bool beginExtrudeRegion();
    [[nodiscard]] QString extrudeRegionDisabledReason() const;
    /** @brief 当前挤出的世界法线/边界信息；非挤出事务返回空。 */
    [[nodiscard]] std::optional<core::modeling::ExtrudeRegionInfo> componentExtrusion() const;
    /** @brief 单个共面凸面的内插事务；厚度为对象局部单位。 */
    bool beginInsetFace();
    [[nodiscard]] QString insetFaceDisabledReason() const;
    [[nodiscard]] std::optional<core::modeling::InsetFaceInfo> componentInset() const;
    bool previewInsetFace(double localThickness);
    /** @brief 单个外凸源边的单段倒角，两个三价端面必须完整可见。 */
    bool beginBevelEdge();
    [[nodiscard]] QString bevelEdgeDisabledReason() const;
    [[nodiscard]] std::optional<core::modeling::BevelEdgeInfo> componentBevel() const;
    bool previewBevelEdge(double localWidth);
    /** @brief 环切不要求预选组件；鼠标源边决定目标带，确认前仍保持原选区。 */
    bool beginLoopCut();
    bool previewLoopCut(std::optional<core::modeling::EdgeKey> seed, double slide);
    /** @brief 按指定域投影当前选择并删除组件/关联面，一次提交可完整撤销。 */
    bool deleteComponents(SelectionDomain domain);
    [[nodiscard]] QString deleteComponentsDisabledReason(SelectionDomain domain) const;
    /** @brief 点/边选择补一个共面边界面；成功后选择新面，不提供 F9 参数。 */
    bool fillFace();
    [[nodiscard]] QString fillFaceDisabledReason() const;
    /** @brief 覆盖层/N侧栏读取候选选区；不作为输入或冻结上下文的选择权威。 */
    [[nodiscard]] const ComponentSelection& displayedComponentSelection() const;
    bool previewComponentTransform(const glm::dmat4& worldDelta);
    bool finishComponentTransform(bool commit);
    [[nodiscard]] bool hasComponentTransform() const;
    [[nodiscard]] std::optional<glm::dvec3> selectedComponentCenter() const;
    [[nodiscard]] std::shared_ptr<const core::EditableMeshContent> componentPreview() const;
    /** @brief 显示层读当前候选；保存与算法 before 必须使用 scene() 中的已确认内容。 */
    [[nodiscard]] std::shared_ptr<const core::EditableMeshContent>
    displayedEditableMesh(core::EntityId id) const;
    /** @brief 复制/删除选中子树，形成一条可撤销记录；无选择时不操作。 */
    void duplicateSelected();
    void deleteSelected();
    bool setSurface(core::EntityId id, const core::SurfaceStyle& surface);
    bool setLighting(const core::Lighting& lighting);
    /** @brief 文档新建/打开前，视图负责询问是否保存；读取失败不改当前状态。 */
    void newScene();
    bool openScene(const QString& path);
    bool saveScene(const QString& path);
    /** @brief 导出所选已确认几何；evaluated 选择修改器结果，原子写盘且不改文档/历史。 */
    bool exportObj(const QString& path, bool evaluated);
    [[nodiscard]] QString objExportDisabledReason() const;
    /** @brief 旧版本首次保存必须另选路径，避免覆盖唯一原件。 */
    [[nodiscard]] bool requiresSaveAs() const;
    /** @brief 当前仅支持原生 Cube；创建独立真源，一次历史，可撤销。 */
    bool makeEditable(core::EntityId id);
    /** @brief 离线验证后安装网格快照；无变化零历史，失败不改当前内容。 */
    bool replaceEditableMesh(core::EntityId id, const core::modeling::EditableMesh& source,
                             const QString& label = QStringLiteral("编辑网格"));
    [[nodiscard]] std::optional<core::modeling::MirrorOptions> mirrorOptions(core::EntityId id) const;
    /** @brief 单 Mirror 参数/添加/删除一次入栈；非法求值保持历史和源不变。 */
    bool setMirrorOptions(core::EntityId id,
                          std::optional<core::modeling::MirrorOptions> options);
    /** @brief 求值结果一次烘焙为源；Undo 恢复参数、源和组件选区。 */
    bool applyMirror(core::EntityId id);
    [[nodiscard]] std::optional<core::modeling::SubdivisionOptions>
    subdivisionOptions(core::EntityId id) const;
    /** @brief 固定链末端细分参数单步历史，失败不修改源或求值内容。 */
    bool setSubdivisionOptions(core::EntityId id,
                               std::optional<core::modeling::SubdivisionOptions> options);
    /** @brief 将最终求值链烘焙为源并删除两算子，Undo恢复源/选区/参数。 */
    bool applySubdivision(core::EntityId id);
    /** @brief 集合仅组织单对象归属，建/删/名称/显隐/成员均使用同一撤销栈。 */
    core::CollectionId createCollection(const QString& name);
    bool removeCollection(core::CollectionId id);
    bool renameCollection(core::CollectionId id, const QString& name);
    bool setCollectionVisible(core::CollectionId id, bool visible);
    bool assignEntityToCollection(core::EntityId entity, core::CollectionId collection);
    [[nodiscard]] QString filePath() const;
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] const core::CameraState& editorCamera() const;
    void setEditorCamera(const core::CameraState& camera);
    [[nodiscard]] const core::Cursor3D& cursor3D() const;
    /** @brief 不进入撤销栈、不标脏；拒绝相机预览和非有限坐标。 */
    bool setCursorPosition(const glm::vec3& position);
    void setCursorVisible(bool visible);
    [[nodiscard]] std::optional<glm::vec3> cursorSelectionCenter() const;
    bool moveCursorToSelection();
    /** @brief 窗口级枢轴偏好，独立于轴空间；切换取消预览，不标脏、不进入历史。 */
    [[nodiscard]] TransformPivot transformPivot() const;
    /** @brief 窗口级吸附类型；不写工程、不入历史；切换取消未确认变换。 */
    [[nodiscard]] SnapMode snapMode() const;
    void setSnapMode(SnapMode mode);
    /** @brief 比例编辑是窗口级偏好；仅组件GRS使用，不写工程/历史，不取消当前组件事务。 */
    [[nodiscard]] bool isProportionalEditingEnabled() const;
    [[nodiscard]] bool isProportionalConnected() const;
    [[nodiscard]] double proportionalRadius() const;
    void setProportionalEditingEnabled(bool enabled);
    void setProportionalConnected(bool connected);
    bool setProportionalRadius(double worldRadius);
    void setTransformPivot(TransformPivot pivot);
    [[nodiscard]] QString transformPivotName() const;
    /** @brief 当前已确认选区的世界枢轴；空选区返回空，活动项无效时回退质心。 */
    [[nodiscard]] std::optional<glm::dvec3> transformPivotPosition() const;
    /** @brief 保持选区内偏移，把对象原点/组件质心移到游标，一次可撤销操作。 */
    bool moveSelectionToCursor();
    [[nodiscard]] QString selectionToCursorDisabledReason() const;
  signals:
    void structureAboutToChange();
    void structureChanged();
    void entityChanged(core::EntityId id);
    void sceneChanged();
    void transformEditFinished();
    void componentTransformFinished();
    void componentPreviewChanged();
    void lastOperationChanged();
    void documentChanged();
    void documentReset();
    void cursorChanged();
    void transformPivotChanged();
    void snapModeChanged();
    void proportionalEditingChanged();
    void viewportVisibilityChanged();
    void previewCameraChanged(core::EntityId id);
    void editModeChanged(bool enabled);
    void componentSelectionChanged();
    void operationFailed(const QString& message);
    void operationCompleted(const QString& message);

  private:
    friend class TransformEntityCommand;
    friend class SubtreeCommand;
    void applyTransform(core::EntityId id, const core::Transform& transform);
    bool rejectObjectEdit();
    void reconcileEditContext();
    void notifyComponentSelection();
    bool filterHiddenSelection();
    void notifyViewportVisibility();
    bool commitEditableMesh(core::EntityId id, const core::modeling::EditableMesh& source,
                            const QString& label);
    bool changeCollections(const std::vector<core::SceneCollection>& after, const QString& label);
    void pushGeometryEdit(core::EntityId id, const core::Scene::GeometrySnapshot& before,
                          const core::Scene::GeometrySnapshot& after, const QString& label,
                          const std::optional<ComponentSelection>& beforeSelection,
                          const std::optional<ComponentSelection>& afterSelection);
    bool isComponentTransformContextValid() const;
    bool beginComponentEdit(const QString& label, bool requiresSelection);
    bool publishComponentPreview(const std::optional<core::modeling::EditableMesh>& mesh,
                                 const std::string& error,
                                 std::optional<ComponentSelection> selection = std::nullopt);
    std::shared_ptr<core::Scene> scene_;
    SelectionModel selection_;
    ComponentSelection componentSelection_;
    core::ViewportVisibility viewportVisibility_;
    core::EntityId editedEntity_ = core::kInvalidEntity;
    core::MeshId editedMesh_ = 0;
    quint64 componentSelectionRevision_ = 0;
    std::shared_ptr<assets::AssetManager> assets_ = std::make_shared<assets::AssetManager>();
    struct TransformEdit {
        core::EntityId id;
        core::Transform before;
    };
    std::optional<TransformEdit> transformEdit_;
    struct ComponentTransform {
        core::EntityId entity;
        core::MeshId mesh;
        std::uint64_t baseRevision;
        glm::dmat4 world;
        core::Scene::GeometrySnapshot before, candidate;
        ComponentSelection selection;
        std::set<core::modeling::VertexId> vertices;
        QString label;
        bool valid = true;
        std::optional<core::modeling::ExtrudeRegionInfo> extrusion;
        glm::dvec3 extrusionWorldOffset{0};
        std::optional<core::modeling::InsetFaceInfo> inset;
        double insetThickness = 0;
        std::optional<core::modeling::BevelEdgeInfo> bevel;
        double bevelWidth = 0;
        bool loopCut = false;
        std::optional<ComponentSelection> previewSelection;
        std::optional<core::modeling::MirrorClipSession> mirrorClip;
    };
    void pushModelingHistory(const ComponentTransform& edit);
    std::optional<ComponentTransform> componentTransform_;
    QString filePath_;
    QString legacySourcePath_;
    core::CameraState editorCamera_, savedCamera_;
    core::Cursor3D cursor_;
    TransformPivot transformPivot_ = TransformPivot::Median;
    SnapMode snapMode_ = SnapMode::Increment;
    bool proportionalEditingEnabled_ = false;
    bool proportionalConnected_ = true;
    double proportionalRadius_ = 1;
    core::EntityId previewCamera_ = core::kInvalidEntity;
    core::EntityId lastPreviewCamera_ = core::kInvalidEntity;
    QUndoStack history_;
    HistoryService historyService_{history_};
};
} // namespace mini3d::editor
