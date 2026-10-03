/*
 * 模块名: SceneViewModel
 * 功能概述: 第三周场景选择与属性编辑的 SceneViewModel 层。
 * 对外接口: SceneViewModel
 * 依赖关系: Qt Widgets/Core、mini3d_core
 * 输入输出: 输入用户意图或场景通知，输出模型状态或界面刷新。
 * 异常与错误: 通过返回值和 operationFailed 报告非法编辑。
 * 维护说明: 同步 UI 线程操作；不持有节点地址或 GPU 资源。
 */
#include "SceneViewModel.h"

#include "EditCommand.h"
#include "SubtreeCommand.h"
#include "TransformEntityCommand.h"
#include "assets/ObjDocument.h"
#include "assets/SceneDocument.h"
#include "core/modeling/DeleteComponents.h"
#include "core/modeling/FillFace.h"
#include "core/modeling/ObjExporter.h"
#include "core/modeling/ProportionalTransform.h"
#include "core/modeling/TopologySelection.h"
#include "core/modeling/VertexTransform.h"
#include "renderer_gl/EditorCamera.h"
#include "renderer_gl/PrimitiveFactory.h"
#include "renderer_gl/RayCaster.h"

#include <QDebug>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/ext/matrix_transform.hpp>
namespace mini3d::editor {
namespace {
std::set<core::modeling::FaceId> selectedFaceIds(const ComponentSelection& selection) {
    std::set<core::modeling::FaceId> result;
    for (const auto id : selection.selectedIds())
        result.insert(id.first);
    return result;
}
} // namespace
SceneViewModel::SceneViewModel(QObject* parent)
    : QObject(parent), scene_(std::make_shared<core::Scene>()), selection_(*scene_) {
    setObjectName(QStringLiteral("SceneViewModel"));
    editorCamera_ = renderer_gl::EditorCamera{}.state();
    savedCamera_ = editorCamera_;
    connect(this, &SceneViewModel::documentReset, this, [this] {
        lastPreviewCamera_ = core::kInvalidEntity;
        viewportVisibility_ = {};
        notifyViewportVisibility();
    });
    connect(&history_, &QUndoStack::cleanChanged, this, &SceneViewModel::documentChanged);
    connect(&history_, &QUndoStack::indexChanged, this, &SceneViewModel::lastOperationChanged);
    connect(this, &SceneViewModel::editModeChanged, this, &SceneViewModel::lastOperationChanged);
    connect(this, &SceneViewModel::componentPreviewChanged, this,
            &SceneViewModel::lastOperationChanged);
    connect(&selection_, &SelectionModel::selectedEntityChanged, this,
            &SceneViewModel::cancelTransformEdit);
    connect(&selection_, &SelectionModel::selectedEntityChanged, this,
            &SceneViewModel::reconcileEditContext);
    connect(&selection_, &SelectionModel::selectedEntityChanged, this, [this] {
        const auto id = selection_.selectedEntity();
        if (id && viewportVisibility_.localRoot && !viewportVisibility_.isVisible(*scene_, id)) {
            viewportVisibility_.localRoot = 0;
            notifyViewportVisibility();
        }
    });
    connect(this, &SceneViewModel::sceneChanged, this, &SceneViewModel::reconcileEditContext);
    connect(this, &SceneViewModel::sceneChanged, this, [this] {
        finishComponentTransform(false);
        if (viewportVisibility_.localRoot && !scene_->isVisible(viewportVisibility_.localRoot)) {
            viewportVisibility_.localRoot = 0;
            notifyViewportVisibility();
        }
    });
    connect(this, &SceneViewModel::sceneChanged, this, [this] {
        if (previewCamera_ != 0 && !scene_->isVisible(previewCamera_)) {
            setPreviewCamera(0);
        }
    });
    const auto group = scene_->createEntity("示例");
    const auto cube = scene_->createEntity("立方体", group, core::PrimitiveKind::Cube);
    const auto sphere = scene_->createEntity("球体", group, core::PrimitiveKind::Sphere);
    const auto plane = scene_->createEntity("平面", group, core::PrimitiveKind::Plane);
    core::Transform transform;
    transform.position = {-1.5F, 0.5F, 0};
    scene_->setTransform(cube, transform);
    transform.position = {0, 0.5F, 0};
    scene_->setTransform(sphere, transform);
    transform.position = {1.6F, 0.02F, 0};
    scene_->setTransform(plane, transform);
}
std::shared_ptr<const core::Scene> SceneViewModel::scene() const {
    return scene_;
}
SceneViewModel::~SceneViewModel() {
    // QUndoStack 析构会改变 clean 状态，不向已经进入析构的窗口发布文档通知。
    disconnect(&history_, nullptr, this, nullptr);
}
SelectionModel* SceneViewModel::selection() {
    return &selection_;
}

bool SceneViewModel::isEditMode() const {
    return editedEntity_ != core::kInvalidEntity;
}
core::EntityId SceneViewModel::editedEntity() const {
    return editedEntity_;
}
QString SceneViewModel::editModeDisabledReason() const {
    if (isEditMode()) {
        return {};
    }
    const auto id = selection_.selectedEntity();
    const auto* node = scene_->find(id);
    if (previewCamera_ != 0) {
        return QStringLiteral("相机预览为只读；请先返回编辑视图。");
    }
    if (!node || !viewportVisibility_.isVisible(*scene_, id) || node->camera || node->light) {
        return QStringLiteral("请选择可见网格；相机、灯光或隐藏对象不能进入编辑模式。");
    }
    if (node->editableMesh == 0 && node->primitive != core::PrimitiveKind::Cube) {
        return QStringLiteral("当前仅原生立方体和已有可编辑网格支持编辑模式。");
    }
    return {};
}
bool SceneViewModel::setEditMode(bool enabled) {
    if (enabled == isEditMode()) {
        return true;
    }
    if (enabled) {
        const auto reason = editModeDisabledReason();
        if (!reason.isEmpty()) {
            emit operationFailed(reason);
            return false;
        }
        // 明确采用取消规则：不把尚未确认的对象预览固化到首次网格转换中。
        cancelTransformEdit();
        const auto id = selection_.selectedEntity();
        if (!makeEditable(id)) {
            return false;
        }
        editedEntity_ = id;
        editedMesh_ = scene_->find(id)->editableMesh;
        componentSelection_ = {};
        componentSelection_.selectAll(scene_->editableMesh(editedMesh_)->content->source);
    } else {
        cancelTransformEdit();
        editedEntity_ = core::kInvalidEntity;
        editedMesh_ = 0;
        componentSelection_ = {};
    }
    viewportVisibility_.clearElements();
    viewportVisibility_.editedEntity = editedEntity_;
    notifyComponentSelection();
    emit viewportVisibilityChanged();
    emit editModeChanged(enabled);
    return true;
}
const ComponentSelection& SceneViewModel::componentSelection() const {
    return componentSelection_;
}
quint64 SceneViewModel::componentSelectionRevision() const {
    return componentSelectionRevision_;
}
void SceneViewModel::notifyComponentSelection() {
    finishComponentTransform(false);
    filterHiddenSelection();
    ++componentSelectionRevision_;
    emit componentSelectionChanged();
}
void SceneViewModel::reconcileEditContext() {
    if (!isEditMode()) {
        return;
    }
    const auto* node = scene_->find(editedEntity_);
    if (selection_.selectedEntity() != editedEntity_ || !node ||
        !scene_->isVisible(editedEntity_) || node->editableMesh != editedMesh_) {
        setEditMode(false);
        return;
    }
    const bool reconciled =
        componentSelection_.reconcile(scene_->editableMesh(editedMesh_)->content->source);
    if (filterHiddenSelection() || reconciled) {
        notifyComponentSelection();
    }
}
void SceneViewModel::setSelectionDomain(SelectionDomain domain) {
    if (isEditMode() &&
        componentSelection_.setDomain(scene_->editableMesh(editedMesh_)->content->source, domain)) {
        notifyComponentSelection();
    }
}
void SceneViewModel::selectComponent(ComponentId id, SelectionOperation operation) {
    if (!isComponentVisible(id))
        return;
    if (isEditMode() && componentSelection_.select(
                            scene_->editableMesh(editedMesh_)->content->source, id, operation)) {
        notifyComponentSelection();
    }
}
void SceneViewModel::selectAllComponents() {
    if (isEditMode() &&
        componentSelection_.selectAll(scene_->editableMesh(editedMesh_)->content->source)) {
        notifyComponentSelection();
    }
}
void SceneViewModel::selectComponents(const std::set<ComponentId>& ids,
                                      SelectionOperation operation) {
    if (isEditMode() && componentSelection_.selectMany(
                            scene_->editableMesh(editedMesh_)->content->source, ids, operation)) {
        notifyComponentSelection();
    }
}
void SceneViewModel::clearComponentSelection() {
    if (isEditMode() && componentSelection_.clear()) {
        notifyComponentSelection();
    }
}
bool SceneViewModel::selectEdgePath(core::modeling::EdgeKey seed, bool ring,
                                    SelectionOperation operation) {
    if (!isEditMode() || previewCamera_ != 0 ||
        componentSelection_.domain() != SelectionDomain::Edge) {
        emit operationFailed(QStringLiteral("Loop / Ring 需要编辑模式中的边选择。"));
        return false;
    }
    if (!isComponentVisible(ComponentId::edge(seed)))
        return false;
    const auto& mesh = scene_->editableMesh(editedMesh_)->content->source;
    const auto result = core::modeling::selectEdgePath(
        mesh, seed, ring ? core::modeling::EdgeSelectionKind::Ring
                        : core::modeling::EdgeSelectionKind::Loop);
    if (!result.edges) {
        emit operationFailed(QString::fromStdString(result.error));
        return false;
    }
    std::set<ComponentId> ids;
    for (const auto edge : *result.edges) {
        const auto id = ComponentId::edge(edge);
        if (isComponentVisible(id))
            ids.insert(id);
    }
    const auto before = componentSelection_;
    componentSelection_.selectMany(mesh, ids, operation);
    if (componentSelection_.selectedIds().contains(ComponentId::edge(seed)))
        componentSelection_.select(mesh, ComponentId::edge(seed), SelectionOperation::Add);
    if (componentSelection_ != before)
        notifyComponentSelection();
    return true;
}
bool SceneViewModel::selectConnected(std::optional<ComponentId> seed) {
    if (!isEditMode() || previewCamera_ != 0)
        return false;
    seed = seed ? seed : componentSelection_.activeId();
    if (!seed || !isComponentVisible(*seed)) {
        emit operationFailed(QStringLiteral("请将鼠标放在源组件上，或先选择一个活动组件。"));
        return false;
    }
    const auto& mesh = scene_->editableMesh(editedMesh_)->content->source;
    if (!ComponentSelection::elements(mesh, componentSelection_.domain()).contains(*seed))
        return false;
    auto vertex = seed->first;
    if (componentSelection_.domain() == SelectionDomain::Face) {
        for (const auto& face : mesh.faces)
            if (face.id == seed->first)
                vertex = face.corners.front().vertex;
    }
    const auto connected = core::modeling::connectedVertices(mesh, vertex);
    ComponentSelection projected;
    std::set<ComponentId> ids;
    for (const auto id : connected)
        ids.insert({id});
    projected.selectMany(mesh, ids, SelectionOperation::Replace);
    projected.setDomain(mesh, componentSelection_.domain());
    ids = projected.selectedIds();
    std::erase_if(ids, [this](ComponentId id) { return !isComponentVisible(id); });
    const auto before = componentSelection_;
    componentSelection_.selectMany(mesh, ids, SelectionOperation::Add);
    componentSelection_.select(mesh, *seed, SelectionOperation::Add);
    if (componentSelection_ != before)
        notifyComponentSelection();
    return true;
}
const core::ViewportVisibility& SceneViewModel::viewportVisibility() const {
    return viewportVisibility_;
}
bool SceneViewModel::isComponentVisible(ComponentId id) const {
    if (!isEditMode())
        return false;
    const auto& mesh = scene_->editableMesh(editedMesh_)->content->source;
    switch (componentSelection_.domain()) {
        case SelectionDomain::Vertex:
            return viewportVisibility_.isVertexVisible(mesh, id.first);
        case SelectionDomain::Edge:
            return viewportVisibility_.isEdgeVisible(mesh, {id.first, id.second});
        case SelectionDomain::Face:
            return viewportVisibility_.isFaceVisible(mesh, id.first);
    }
    return false;
}
bool SceneViewModel::filterHiddenSelection() {
    if (!isEditMode() || !viewportVisibility_.hasHiddenElements())
        return false;
    std::set<ComponentId> hidden;
    for (const auto id : componentSelection_.selectedIds())
        if (!isComponentVisible(id))
            hidden.insert(id);
    return componentSelection_.selectMany(scene_->editableMesh(editedMesh_)->content->source,
                                          hidden, SelectionOperation::Remove);
}
void SceneViewModel::notifyViewportVisibility() {
    cancelTransformEdit();
    notifyComponentSelection();
    emit viewportVisibilityChanged();
}
bool SceneViewModel::hideSelection() {
    if (previewCamera_ != 0)
        return false;
    cancelTransformEdit();
    if (isEditMode()) {
        if (componentSelection_.selectedIds().empty())
            return false;
        for (const auto id : componentSelection_.selectedIds()) {
            switch (componentSelection_.domain()) {
                case SelectionDomain::Vertex:
                    viewportVisibility_.vertices.insert(id.first);
                    break;
                case SelectionDomain::Edge:
                    viewportVisibility_.edges.emplace(id.first, id.second);
                    break;
                case SelectionDomain::Face:
                    viewportVisibility_.faces.insert(id.first);
                    break;
            }
        }
    } else {
        const auto id = selection_.selectedEntity();
        if (!viewportVisibility_.isVisible(*scene_, id))
            return false;
        viewportVisibility_.hiddenObjects.insert(id);
        selection_.setSelectedEntity(0);
    }
    notifyViewportVisibility();
    return true;
}
bool SceneViewModel::revealHidden() {
    if (previewCamera_ != 0)
        return false;
    if (isEditMode())
        viewportVisibility_.clearElements();
    else
        viewportVisibility_.hiddenObjects.clear();
    notifyViewportVisibility();
    return true;
}
bool SceneViewModel::toggleLocalView() {
    if (previewCamera_ != 0)
        return false;
    if (viewportVisibility_.localRoot) {
        viewportVisibility_.localRoot = 0;
    } else {
        const auto id = selection_.selectedEntity();
        if (!viewportVisibility_.isVisible(*scene_, id)) {
            emit operationFailed(QStringLiteral("请选择可见对象再进入局部视图。"));
            return false;
        }
        viewportVisibility_.localRoot = id;
    }
    notifyViewportVisibility();
    emit operationCompleted(viewportVisibility_.localRoot
                                ? QStringLiteral("已进入局部视图：仅显示所选子树。")
                                : QStringLiteral("已退出局部视图：恢复原显示范围。"));
    return true;
}
bool SceneViewModel::rejectObjectEdit() {
    if (!isEditMode()) {
        return false;
    }
    emit operationFailed(QStringLiteral("当前是编辑模式；请先返回对象模式，再执行整对象操作。"));
    return true;
}

std::shared_ptr<const assets::AssetManager> SceneViewModel::assets() const {
    return assets_;
}

core::EntityId SceneViewModel::importGltf(const QString& path) {
    if (rejectObjectEdit()) {
        return core::kInvalidEntity;
    }
    cancelTransformEdit();
    const auto imported = assets_->importGltf(path);
    if (imported.scene == nullptr) {
        qWarning().noquote() << imported.error;
        emit operationFailed(imported.error);
        return core::kInvalidEntity;
    }
    const auto previousSelection = selection_.selectedEntity();
    // 解码和层级验证均已完成，之后才通知树模型进入一次结构事务。
    emit structureAboutToChange();
    const QString baseName = QFileInfo(path).completeBaseName();
    const auto root =
        scene_->createEntity(baseName.isEmpty() ? "导入对象" : baseName.toUtf8().toStdString());
    std::function<void(std::size_t, core::EntityId)> instantiate = [&](std::size_t index,
                                                                       core::EntityId parent) {
        const auto& source = imported.scene->nodes[index];
        const auto id = scene_->createEntity(source.name, parent);
        scene_->setTransform(id, source.transform);
        for (std::size_t p = 0; p < source.meshes.size(); ++p) {
            const auto mesh = source.meshes[p];
            const auto meshNode = source.meshes.size() == 1
                                      ? id
                                      : scene_->createEntity("子网格 " + std::to_string(p), id);
            scene_->setMeshRenderer(meshNode, {mesh, assets_->mesh(mesh)->material});
        }
        for (const auto child : source.children) {
            instantiate(child, id);
        }
    };
    for (const auto child : imported.scene->roots) {
        instantiate(child, root);
    }
    emit structureChanged();
    selection_.setSelectedEntity(root);
    history_.push(
        new SubtreeCommand(*this, root, SubtreeCommand::Kind::Created, previousSelection));
    emit sceneChanged();
    QString message =
        QStringLiteral("已导入 %1%2")
            .arg(path, imported.cacheHit ? QStringLiteral("（已复用资源）") : QString{});
    for (const auto& warning : imported.scene->warnings) {
        message += "\n" + warning;
    }
    qInfo().noquote() << message;
    emit operationCompleted(message);
    return root;
}
core::EntityId SceneViewModel::createEntity(core::PrimitiveKind primitive) {
    if (rejectObjectEdit()) {
        return core::kInvalidEntity;
    }
    cancelTransformEdit();
    const auto previousSelection = selection_.selectedEntity();
    const char* name = "空对象";
    switch (primitive) {
        case core::PrimitiveKind::Cube:
            name = "立方体";
            break;
        case core::PrimitiveKind::Sphere:
            name = "球体";
            break;
        case core::PrimitiveKind::Plane:
            name = "平面";
            break;
        case core::PrimitiveKind::Empty:
            break;
    }
    emit structureAboutToChange();
    const auto id = scene_->createEntity(name, core::kInvalidEntity, primitive);
    auto transform = scene_->find(id)->transform;
    transform.position = cursor_.position;
    scene_->setTransform(id, transform);
    emit structureChanged();
    selection_.setSelectedEntity(id);
    history_.push(new SubtreeCommand(*this, id, SubtreeCommand::Kind::Created, previousSelection));
    emit sceneChanged();
    return id;
}
core::EntityId SceneViewModel::createCamera() {
    core::Transform transform;
    transform.position = editorCamera_.position;
    transform.rotation = glm::quat_cast(glm::transpose(
        glm::mat3(glm::lookAt(editorCamera_.position, editorCamera_.target, glm::vec3(0, 1, 0)))));
    return createCameraFromView(transform);
}
core::EntityId SceneViewModel::createCameraFromView(const core::Transform& transform) {
    if (rejectObjectEdit()) {
        return core::kInvalidEntity;
    }
    if (!transform.isValid()) {
        return core::kInvalidEntity;
    }
    cancelTransformEdit();
    const auto previous = selection_.selectedEntity();
    emit structureAboutToChange();
    const auto id = scene_->createEntity("相机");
    scene_->setCamera(id, {});
    scene_->setTransform(id, transform);
    emit structureChanged();
    selection_.setSelectedEntity(id);
    history_.push(new SubtreeCommand(*this, id, SubtreeCommand::Kind::Created, previous));
    emit sceneChanged();
    return id;
}
core::EntityId SceneViewModel::createDirectionalLight() {
    if (rejectObjectEdit()) {
        return core::kInvalidEntity;
    }
    cancelTransformEdit();
    const auto previous = selection_.selectedEntity();
    emit structureAboutToChange();
    const auto id = scene_->createEntity("方向光");
    const auto& lighting = scene_->lighting();
    scene_->setLight(id, {lighting.color, lighting.intensity});
    core::Transform transform;
    const auto direction = glm::normalize(lighting.direction);
    const auto up = std::abs(direction.y) > 0.99F ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    transform.rotation =
        glm::quat_cast(glm::transpose(glm::mat3(glm::lookAt(glm::vec3(0), -direction, up))));
    scene_->setTransform(id, transform);
    emit structureChanged();
    selection_.setSelectedEntity(id);
    history_.push(new SubtreeCommand(*this, id, SubtreeCommand::Kind::Created, previous));
    emit sceneChanged();
    return id;
}
bool SceneViewModel::setCamera(core::EntityId id, const core::CameraComponent& camera) {
    cancelTransformEdit();
    const auto* node = scene_->find(id);
    if (!node || !node->camera || !camera.isValid()) {
        emit operationFailed(
            QStringLiteral("相机垂直视角须在 1～179 度之间，且 0 < 近裁剪 < 远裁剪。"));
        return false;
    }
    const auto before = *node->camera;
    if (before == camera) {
        return true;
    }
    const auto apply = [this, id](const core::CameraComponent& value) {
        scene_->setCamera(id, value);
        emit entityChanged(id);
        emit sceneChanged();
    };
    history_.push(new EditCommand(
        QStringLiteral("相机"),
        [apply, before] {
            apply(before);
        },
        [apply, camera] {
            apply(camera);
        }));
    return true;
}
bool SceneViewModel::setLight(core::EntityId id, const core::LightComponent& light) {
    cancelTransformEdit();
    const auto* node = scene_->find(id);
    if (!node || !node->light || !light.isValid()) {
        emit operationFailed(QStringLiteral("光源 RGB 须在 0～1 之间，强度须在 0～10 之间。"));
        return false;
    }
    const auto before = *node->light;
    if (before == light) {
        return true;
    }
    const auto apply = [this, id](const core::LightComponent& value) {
        scene_->setLight(id, value);
        emit entityChanged(id);
        emit sceneChanged();
    };
    history_.push(new EditCommand(
        QStringLiteral("方向光"),
        [apply, before] {
            apply(before);
        },
        [apply, light] {
            apply(light);
        }));
    return true;
}
bool SceneViewModel::setPreviewCamera(core::EntityId id) {
    if (id != 0) {
        const auto* node = scene_->find(id);
        if (!node || !node->camera || !scene_->isVisible(id)) {
            return false;
        }
        setEditMode(false);
        if (viewportVisibility_.localRoot) {
            viewportVisibility_.localRoot = 0;
            notifyViewportVisibility();
        }
    }
    cancelTransformEdit();
    if (previewCamera_ != id) {
        previewCamera_ = id;
        if (id != core::kInvalidEntity) {
            lastPreviewCamera_ = id;
        }
        emit previewCameraChanged(id);
    }
    return true;
}
core::EntityId SceneViewModel::previewCamera() const {
    return previewCamera_;
}
core::EntityId SceneViewModel::previewCameraCandidate() const {
    const auto usable = [this](core::EntityId id) {
        const auto* node = scene_->find(id);
        return node && node->camera && scene_->isVisible(id);
    };
    for (auto id : {selection_.selectedEntity(), lastPreviewCamera_}) {
        if (usable(id)) {
            return id;
        }
    }
    core::EntityId result = core::kInvalidEntity;
    for (const auto& node : scene_->nodes()) {
        if (node.camera && usable(node.id) &&
            (result == core::kInvalidEntity || node.id < result)) {
            result = node.id;
        }
    }
    return result;
}

bool SceneViewModel::toggleCameraPreview() {
    if (previewCamera_ != core::kInvalidEntity) {
        return setPreviewCamera(core::kInvalidEntity);
    }
    const auto candidate = previewCameraCandidate();
    if (candidate == core::kInvalidEntity) {
        emit operationFailed(QStringLiteral("没有可见相机：请创建相机或在场景树显示已有相机。"));
        return false;
    }
    return setPreviewCamera(candidate);
}
void SceneViewModel::selectRay(const core::Ray& ray) {
    if (isEditMode()) {
        return; // 组件拾取由独立入口处理，不能将源笼点击解释成对象选择。
    }
    selection_.setSelectedEntity(
        renderer_gl::RayCaster::pick(*scene_, *assets_, ray, viewportVisibility_));
}
bool SceneViewModel::renameEntity(core::EntityId id, const QString& name) {
    cancelTransformEdit();
    const QString trimmed = name.trimmed();
    const auto* node = scene_->find(id);
    if (!node || trimmed.isEmpty()) {
        emit operationFailed(QStringLiteral("名称不能为空。"));
        return false;
    }
    const auto before = node->name;
    const auto after = trimmed.toUtf8().toStdString();
    if (before == after) {
        return true;
    }
    const auto apply = [this, id](const std::string& value) {
        scene_->renameEntity(id, value);
        emit entityChanged(id);
        emit sceneChanged();
    };
    history_.push(new EditCommand(
        QStringLiteral("重命名"),
        [apply, before] {
            apply(before);
        },
        [apply, after] {
            apply(after);
        }));
    return true;
}
bool SceneViewModel::setVisible(core::EntityId id, bool visible) {
    cancelTransformEdit();
    const auto* node = scene_->find(id);
    if (!node) {
        return false;
    }
    const bool before = node->visible;
    if (before == visible) {
        return true;
    }
    const auto apply = [this, id](bool value) {
        scene_->setVisible(id, value);
        emit entityChanged(id);
        emit sceneChanged();
    };
    history_.push(new EditCommand(
        QStringLiteral("显示/隐藏"),
        [apply, before] {
            apply(before);
        },
        [apply, visible] {
            apply(visible);
        }));
    return true;
}
bool SceneViewModel::setParent(core::EntityId id, core::EntityId parent) {
    if (rejectObjectEdit()) {
        return false;
    }
    cancelTransformEdit();
    // 先在模型通知前验证，失败时不触发无意义的树重置。
    if (scene_->find(id) == nullptr ||
        (parent != core::kInvalidEntity && scene_->find(parent) == nullptr)) {
        emit operationFailed(QStringLiteral("父对象已不存在。"));
        return false;
    }
    for (auto ancestor = parent; ancestor != core::kInvalidEntity;
         ancestor = scene_->find(ancestor)->parent) {
        if (ancestor == id) {
            emit operationFailed(QStringLiteral("不能将对象自身或其后代设为父对象。"));
            return false;
        }
    }
    if (scene_->find(id)->parent == parent) {
        return true;
    }
    const auto beforeParent = scene_->find(id)->parent;
    std::size_t beforeIndex = 0;
    if (beforeParent != 0) {
        const auto& siblings = scene_->find(beforeParent)->children;
        beforeIndex = static_cast<std::size_t>(std::find(siblings.begin(), siblings.end(), id) -
                                               siblings.begin());
    }
    const auto afterIndex = parent == 0 ? 0 : scene_->find(parent)->children.size();
    const auto apply = [this, id](core::EntityId target, std::size_t index) {
        emit structureAboutToChange();
        scene_->setParent(id, target);
        if (target != 0) {
            scene_->setSiblingIndex(id, index);
        }
        emit structureChanged();
        emit sceneChanged();
    };
    history_.push(new EditCommand(
        QStringLiteral("更换父对象"),
        [apply, beforeParent, beforeIndex] {
            apply(beforeParent, beforeIndex);
        },
        [apply, parent, afterIndex] {
            apply(parent, afterIndex);
        }));
    return true;
}
bool SceneViewModel::setTransform(core::EntityId id, const core::Transform& transform) {
    if (rejectObjectEdit()) {
        return false;
    }
    cancelTransformEdit();
    const auto* node = scene_->find(id);
    if (node == nullptr || !transform.isValid()) {
        emit operationFailed(
            QStringLiteral("变换被拒绝：请使用有限数值，且缩放绝对值不得小于 0.001。"));
        return false;
    }
    auto after = transform;
    after.rotation = glm::normalize(after.rotation);
    const auto& before = node->transform;
    if (before.position == after.position && before.rotation == after.rotation &&
        before.scale == after.scale) {
        return true;
    }
    history_.push(new TransformEntityCommand(*this, id, before, after));
    return true;
}
void SceneViewModel::applyTransform(core::EntityId id, const core::Transform& transform) {
    scene_->setTransform(id, transform);
    emit entityChanged(id);
    emit sceneChanged();
}
const QUndoStack* SceneViewModel::undoStack() const {
    return &history_;
}
void SceneViewModel::undo() {
    cancelTransformEdit();
    history_.undo();
}
void SceneViewModel::redo() {
    cancelTransformEdit();
    history_.redo();
}
bool SceneViewModel::setTransformComponent(core::EntityId id, int group, int axis, double value) {
    cancelTransformEdit();
    const auto* node = scene_->find(id);
    if (node == nullptr || group < 0 || group > 2 || axis < 0 || axis > 2 ||
        !std::isfinite(value)) {
        return false;
    }
    core::Transform transform = node->transform;
    if (group == 0) {
        transform.position[axis] = static_cast<float>(value);
    }
    if (group == 2) {
        transform.scale[axis] = static_cast<float>(value);
    }
    if (group == 1) {
        glm::vec3 euler = glm::eulerAngles(transform.rotation);
        euler[axis] = glm::radians(static_cast<float>(value));
        transform.rotation = glm::quat(euler);
    }
    return setTransform(id, transform);
}
void SceneViewModel::beginTransformEdit(core::EntityId id) {
    if (rejectObjectEdit() || !viewportVisibility_.isVisible(*scene_, id)) {
        return;
    }
    cancelTransformEdit();
    if (const auto* node = scene_->find(id)) {
        transformEdit_ = TransformEdit{id, node->transform};
    }
}
void SceneViewModel::previewTransform(const core::Transform& transform) {
    if (transformEdit_ && transform.isValid()) {
        applyTransform(transformEdit_->id, transform);
    }
}
void SceneViewModel::finishTransformEdit(bool commit) {
    if (!transformEdit_) {
        return;
    }
    const auto edit = *transformEdit_;
    const auto after = scene_->find(edit.id)->transform;
    transformEdit_.reset();
    if (!commit) {
        applyTransform(edit.id, edit.before);
    } else if (edit.before.position != after.position || edit.before.rotation != after.rotation ||
               edit.before.scale != after.scale) {
        history_.push(new TransformEntityCommand(*this, edit.id, edit.before, after));
    }
    emit transformEditFinished();
}
void SceneViewModel::cancelTransformEdit() {
    finishComponentTransform(false);
    finishTransformEdit(false);
}
bool SceneViewModel::hasComponentTransform() const {
    return componentTransform_.has_value();
}
std::shared_ptr<const core::EditableMeshContent> SceneViewModel::componentPreview() const {
    return componentTransform_ ? componentTransform_->candidate.content() : nullptr;
}
std::shared_ptr<const core::EditableMeshContent>
SceneViewModel::displayedEditableMesh(core::EntityId id) const {
    if (componentTransform_ && componentTransform_->entity == id) {
        return componentPreview();
    }
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    return record ? record->content : nullptr;
}
std::optional<glm::dvec3> SceneViewModel::selectedComponentCenter() const {
    if (!isEditMode()) {
        return std::nullopt;
    }
    const auto& source = scene_->editableMesh(editedMesh_)->content->source;
    return core::modeling::vertexSelectionCenter(source,
                                                 componentSelection_.selectedVertices(source),
                                                 glm::dmat4(scene_->worldMatrix(editedEntity_)));
}
bool SceneViewModel::beginComponentTransform(const QString& label) {
    if (!beginComponentEdit(label, true))
        return false;
    const auto mirror = componentTransform_->before.content()->mirror;
    if (mirror && mirror->enabled && mirror->clipping) {
        std::string error;
        auto clip = core::modeling::MirrorClipSession::begin(
            componentTransform_->before.content()->source, componentTransform_->vertices,
            *mirror, error);
        if (!clip) {
            finishComponentTransform(false);
            emit operationFailed(QString::fromStdString(error));
            return false;
        }
        componentTransform_->mirrorClip = std::move(*clip);
    }
    return true;
}
bool SceneViewModel::beginComponentEdit(const QString& label, bool requiresSelection) {
    cancelTransformEdit();
    if (!isEditMode() || previewCamera_ != 0 || !scene_->isVisible(editedEntity_)) {
        emit operationFailed(QStringLiteral("请先进入可见网格的编辑模式。"));
        return false;
    }
    const auto& record = *scene_->editableMesh(editedMesh_);
    const auto selected = componentSelection_.selectedVertices(record.content->source);
    if (requiresSelection && selected.empty()) {
        emit operationFailed(QStringLiteral("请先选择需要变换的点、边或面。"));
        return false;
    }
    const auto before = *scene_->geometrySnapshot(editedEntity_);
    componentTransform_ = ComponentTransform{editedEntity_,
                                             editedMesh_,
                                             record.evaluationRevision,
                                             glm::dmat4(scene_->worldMatrix(editedEntity_)),
                                             before,
                                             before,
                                             componentSelection_,
                                             selected,
                                             label,
                                             true};
    emit componentPreviewChanged();
    return true;
}
bool SceneViewModel::isComponentTransformContextValid() const {
    if (!componentTransform_) {
        return false;
    }
    const auto& edit = *componentTransform_;
    const auto* node = scene_->find(edit.entity);
    return isEditMode() && editedEntity_ == edit.entity && node &&
           node->editableMesh == edit.mesh && scene_->isVisible(edit.entity) &&
           scene_->editableMesh(edit.mesh)->evaluationRevision == edit.baseRevision &&
           glm::dmat4(scene_->worldMatrix(edit.entity)) == edit.world &&
           componentSelection_ == edit.selection;
}
QString SceneViewModel::extrudeRegionDisabledReason() const {
    if (!isEditMode() || componentSelection_.domain() != SelectionDomain::Face) {
        return QStringLiteral("请进入编辑模式并切换到面选择，再执行区域挤出。");
    }
    const auto analysis = core::modeling::analyzeExtrudeRegion(
        scene_->editableMesh(editedMesh_)->content->source, selectedFaceIds(componentSelection_));
    return QString::fromStdString(analysis.error);
}
bool SceneViewModel::beginExtrudeRegion() {
    cancelTransformEdit();
    const auto reason = extrudeRegionDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    auto info =
        *core::modeling::analyzeExtrudeRegion(scene_->editableMesh(editedMesh_)->content->source,
                                              selectedFaceIds(componentSelection_))
             .region;
    const auto normalMatrix =
        glm::transpose(glm::inverse(glm::dmat3(scene_->worldMatrix(editedEntity_))));
    info.normal = glm::normalize(normalMatrix * info.normal);
    if (!std::isfinite(info.normal.x) || !std::isfinite(info.normal.y) ||
        !std::isfinite(info.normal.z)) {
        emit operationFailed(QStringLiteral("对象世界变换无法生成有效挤出法线。"));
        return false;
    }
    if (!beginComponentTransform(QStringLiteral("区域挤出")))
        return false;
    componentTransform_->extrusion = info;
    componentTransform_->valid = false; // 初始零距离仅显示 before，不能提交重叠拓扑。
    return true;
}
std::optional<core::modeling::ExtrudeRegionInfo> SceneViewModel::componentExtrusion() const {
    return componentTransform_ ? componentTransform_->extrusion : std::nullopt;
}
QString SceneViewModel::insetFaceDisabledReason() const {
    if (!isEditMode() || componentSelection_.domain() != SelectionDomain::Face ||
        componentSelection_.selectedIds().size() != 1)
        return QStringLiteral("请进入编辑模式并只选择一个共面凸面，再执行内插。");
    const auto id = componentSelection_.selectedIds().begin()->first;
    return QString::fromStdString(
        core::modeling::analyzeInsetFace(scene_->editableMesh(editedMesh_)->content->source, id)
            .error);
}
bool SceneViewModel::beginInsetFace() {
    cancelTransformEdit();
    const auto reason = insetFaceDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    const auto info =
        *core::modeling::analyzeInsetFace(scene_->editableMesh(editedMesh_)->content->source,
                                          componentSelection_.selectedIds().begin()->first)
             .face;
    if (!beginComponentTransform(QStringLiteral("面内插")))
        return false;
    componentTransform_->inset = info;
    componentTransform_->valid = false;
    return true;
}
std::optional<core::modeling::InsetFaceInfo> SceneViewModel::componentInset() const {
    return componentTransform_ ? componentTransform_->inset : std::nullopt;
}
bool SceneViewModel::publishComponentPreview(
    const std::optional<core::modeling::EditableMesh>& mesh, const std::string& reason,
    std::optional<ComponentSelection> selection) {
    auto& edit = *componentTransform_;
    if (!mesh) {
        edit.valid = false;
        emit operationFailed(QString::fromStdString(reason));
        return false;
    }
    std::string error;
    const auto candidate = scene_->prepareEditableGeometry(edit.entity, *mesh, error);
    if (!candidate) {
        edit.valid = false;
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    edit.candidate = *candidate;
    edit.previewSelection = std::move(selection);
    edit.valid = true;
    emit componentPreviewChanged();
    return true;
}
bool SceneViewModel::previewInsetFace(double localThickness) {
    if (!isComponentTransformContextValid()) {
        finishComponentTransform(false);
        return false;
    }
    auto& edit = *componentTransform_;
    if (!edit.inset) {
        emit operationFailed(QStringLiteral("当前操作不是面内插。"));
        return false;
    }
    const auto result = core::modeling::insetFace(
        edit.before.content()->source, edit.selection.selectedIds().begin()->first, localThickness);
    edit.insetThickness = localThickness;
    return publishComponentPreview(result.mesh, result.error);
}
QString SceneViewModel::bevelEdgeDisabledReason() const {
    if (!isEditMode() || previewCamera_ != 0 ||
        !viewportVisibility_.isVisible(*scene_, editedEntity_) ||
        componentSelection_.domain() != SelectionDomain::Edge ||
        componentSelection_.selectedIds().size() != 1)
        return QStringLiteral("请进入可见网格的编辑模式并只选择一条外凸源边，再执行倒角。");
    const auto selected = *componentSelection_.selectedIds().begin();
    const core::modeling::EdgeKey edge(selected.first, selected.second);
    const auto& source = scene_->editableMesh(editedMesh_)->content->source;
    const auto analysis = core::modeling::analyzeBevelEdge(source, edge);
    if (!analysis.bevel)
        return QString::fromStdString(analysis.error);
    for (const auto& face : source.faces) {
        const auto affected =
            std::any_of(face.corners.begin(), face.corners.end(), [edge](const auto& corner) {
                return corner.vertex == edge.first || corner.vertex == edge.second;
            });
        if (affected && !viewportVisibility_.isFaceVisible(face))
            return QStringLiteral("倒角邻面或端面包含隐藏元素；请先 Alt+H 恢复显示。");
    }
    return {};
}
bool SceneViewModel::beginBevelEdge() {
    cancelTransformEdit();
    const auto reason = bevelEdgeDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    const auto selected = *componentSelection_.selectedIds().begin();
    const auto info =
        *core::modeling::analyzeBevelEdge(scene_->editableMesh(editedMesh_)->content->source,
                                          {selected.first, selected.second})
             .bevel;
    if (!beginComponentEdit(QStringLiteral("边倒角"), true))
        return false;
    componentTransform_->bevel = info;
    componentTransform_->valid = false;
    return true;
}
std::optional<core::modeling::BevelEdgeInfo> SceneViewModel::componentBevel() const {
    return componentTransform_ ? componentTransform_->bevel : std::nullopt;
}
bool SceneViewModel::previewBevelEdge(double localWidth) {
    if (!isComponentTransformContextValid()) {
        finishComponentTransform(false);
        return false;
    }
    auto& edit = *componentTransform_;
    if (!edit.bevel) {
        emit operationFailed(QStringLiteral("当前操作不是边倒角。"));
        return false;
    }
    const auto result =
        core::modeling::bevelEdge(edit.before.content()->source, edit.bevel->edge, localWidth);
    edit.bevelWidth = localWidth;
    ComponentSelection selected;
    if (result.mesh) {
        selected.setDomain(*result.mesh, SelectionDomain::Face);
        selected.select(*result.mesh, {result.bevelFace}, SelectionOperation::Replace);
    }
    return publishComponentPreview(result.mesh, result.error, std::move(selected));
}
bool SceneViewModel::beginLoopCut() {
    if (!beginComponentEdit(QStringLiteral("环切"), false))
        return false;
    componentTransform_->loopCut = true;
    componentTransform_->valid = false;
    return true;
}
QString SceneViewModel::deleteComponentsDisabledReason(SelectionDomain domain) const {
    if (!isEditMode() || previewCamera_ != 0 || !scene_->isVisible(editedEntity_))
        return QStringLiteral("请先进入可见网格的编辑模式。");
    if (!componentSelection_.hasSelectionInDomain(
            scene_->editableMesh(editedMesh_)->content->source, domain))
        return QStringLiteral("当前选择没有该域的完整组件；请先选择对应的点、边或面。");
    return {};
}
bool SceneViewModel::deleteComponents(SelectionDomain domain) {
    const auto reason = deleteComponentsDisabledReason(domain);
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    const auto label = domain == SelectionDomain::Vertex ? QStringLiteral("删除点及关联面")
                       : domain == SelectionDomain::Edge ? QStringLiteral("删除边及关联面")
                                                         : QStringLiteral("删除面");
    if (!beginComponentEdit(label, true))
        return false;
    const auto& before = componentTransform_->before.content()->source;
    auto selected = componentTransform_->selection;
    selected.setDomain(before, domain);
    core::modeling::DeleteComponentsResult result;
    if (domain == SelectionDomain::Vertex) {
        result = core::modeling::deleteVertices(before, selected.selectedVertices(before));
    } else if (domain == SelectionDomain::Edge) {
        std::set<core::modeling::EdgeKey> edges;
        for (const auto id : selected.selectedIds())
            edges.emplace(id.first, id.second);
        result = core::modeling::deleteEdges(before, edges);
    } else {
        result = core::modeling::deleteFaces(before, selectedFaceIds(selected));
    }
    auto afterSelection = componentTransform_->selection;
    if (result.mesh)
        afterSelection.reconcile(*result.mesh);
    if (!publishComponentPreview(result.mesh, result.error, afterSelection)) {
        finishComponentTransform(false);
        return false;
    }
    return finishComponentTransform(true);
}
QString SceneViewModel::fillFaceDisabledReason() const {
    if (!isEditMode() || previewCamera_ != 0 || !scene_->isVisible(editedEntity_))
        return QStringLiteral("请先进入可见网格的编辑模式。");
    if (componentSelection_.domain() == SelectionDomain::Face)
        return QStringLiteral("补面需要点选择或边选择模式，请完整选择一个边界环。");
    if (componentSelection_.selectedIds().size() < 3)
        return QStringLiteral("请至少选择三个边界点或三条边界边。");
    return {};
}
bool SceneViewModel::fillFace() {
    const auto reason = fillFaceDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    if (!beginComponentEdit(QStringLiteral("补面"), true))
        return false;
    const auto& before = componentTransform_->before.content()->source;
    const auto& selected = componentTransform_->selection;
    core::modeling::FillFaceResult result;
    if (selected.domain() == SelectionDomain::Vertex) {
        result = core::modeling::fillFaceFromVertices(before, selected.selectedVertices(before));
    } else {
        std::set<core::modeling::EdgeKey> edges;
        for (const auto id : selected.selectedIds())
            edges.emplace(id.first, id.second);
        result = core::modeling::fillFaceFromEdges(before, edges);
    }
    ComponentSelection afterSelection;
    if (result.mesh) {
        afterSelection.setDomain(*result.mesh, SelectionDomain::Face);
        afterSelection.select(*result.mesh, {result.face}, SelectionOperation::Replace);
    }
    if (!publishComponentPreview(result.mesh, result.error, afterSelection)) {
        finishComponentTransform(false);
        return false;
    }
    return finishComponentTransform(true);
}
const ComponentSelection& SceneViewModel::displayedComponentSelection() const {
    return componentTransform_ && componentTransform_->previewSelection
               ? *componentTransform_->previewSelection
               : componentSelection_;
}
bool SceneViewModel::previewLoopCut(std::optional<core::modeling::EdgeKey> seed, double slide) {
    if (!isComponentTransformContextValid()) {
        finishComponentTransform(false);
        return false;
    }
    auto& edit = *componentTransform_;
    if (!edit.loopCut) {
        emit operationFailed(QStringLiteral("当前操作不是环切。"));
        return false;
    }
    auto result = seed ? core::modeling::loopCut(edit.before.content()->source, *seed, slide)
                       : core::modeling::LoopCutResult{{}, {}, "请将鼠标移到可见源边以预览环切。"};
    if (result.mesh && viewportVisibility_.hasHiddenElements()) {
        const auto band = core::modeling::analyzeLoopCut(edit.before.content()->source, *seed);
        for (const auto face : band.band->faces) {
            if (!viewportVisibility_.isFaceVisible(edit.before.content()->source, face)) {
                result.mesh.reset();
                result.error = "环切面带包含隐藏面；请先 Alt+H 恢复显示。";
                break;
            }
        }
    }
    if (!result.mesh) {
        edit.candidate = edit.before;
        edit.previewSelection.reset();
        edit.valid = false;
        emit componentPreviewChanged();
        emit operationFailed(QString::fromStdString(result.error));
        return false;
    }
    ComponentSelection selected;
    selected.setDomain(*result.mesh, SelectionDomain::Edge);
    std::set<ComponentId> ids;
    for (const auto edge : result.cutEdges)
        ids.insert(ComponentId::edge(edge));
    selected.selectMany(*result.mesh, ids, SelectionOperation::Replace);
    return publishComponentPreview(result.mesh, result.error, std::move(selected));
}
bool SceneViewModel::previewComponentTransform(const glm::dmat4& worldDelta) {
    if (!isComponentTransformContextValid()) {
        finishComponentTransform(false);
        return false;
    }
    auto& edit = *componentTransform_;
    if (edit.inset || edit.loopCut || edit.bevel) {
        edit.valid = false;
        emit operationFailed(QStringLiteral("内插/环切/倒角只接受自身参数，不接受变换矩阵。"));
        return false;
    }
    const auto publish = [this](auto result) {
        return publishComponentPreview(result.mesh, result.error);
    };
    if (edit.extrusion) {
        if (glm::dmat3(worldDelta) != glm::dmat3(1) || worldDelta[0][3] != 0 ||
            worldDelta[1][3] != 0 || worldDelta[2][3] != 0 || worldDelta[3][3] != 1) {
            edit.valid = false;
            emit operationFailed(QStringLiteral("区域挤出只接受位移增量。"));
            return false;
        }
        const auto offset = glm::inverse(edit.world) * glm::dvec4(glm::dvec3(worldDelta[3]), 0);
        edit.extrusionWorldOffset = glm::dvec3(worldDelta[3]);
        return publish(core::modeling::extrudeRegion(
            edit.before.content()->source, selectedFaceIds(edit.selection), glm::dvec3(offset)));
    }
    if (!proportionalEditingEnabled_ && !edit.mirrorClip) {
        std::string error;
        const auto candidate = scene_->prepareTransformedEditableGeometry(
            edit.entity, edit.before, edit.vertices, edit.world, worldDelta, error);
        if (!candidate) {
            edit.valid = false;
            emit operationFailed(QString::fromStdString(error));
            return false;
        }
        edit.candidate = *candidate;
        edit.previewSelection.reset();
        edit.valid = true;
        emit componentPreviewChanged();
        return true;
    }
    auto affected = edit.vertices;
    core::modeling::VertexTransformResult transformed;
    if (proportionalEditingEnabled_) {
        auto weights = core::modeling::proportionalWeights(edit.before.content()->source,
                                                           edit.vertices, edit.world,
                                                           proportionalRadius_,
                                                           proportionalConnected_);
        if (!weights.weights)
            return publish(core::modeling::VertexTransformResult{{}, weights.error});
        std::erase_if(*weights.weights, [this, &edit](const auto& entry) {
            return !viewportVisibility_.isVertexVisible(edit.before.content()->source, entry.first);
        });
        affected.clear();
        for (const auto& [id, weight] : *weights.weights)
            if (weight > 0)
                affected.insert(id);
        transformed = core::modeling::transformWeightedVertices(
            edit.before.content()->source, *weights.weights, edit.world, worldDelta);
    } else {
        transformed = core::modeling::transformVertices(edit.before.content()->source,
                                                         edit.vertices, edit.world, worldDelta);
    }
    if (!transformed.mesh || !edit.mirrorClip)
        return publish(std::move(transformed));
    auto clip = *edit.mirrorClip;
    transformed = clip.constrain(*transformed.mesh, affected);
    if (!publish(std::move(transformed)))
        return false;
    edit.mirrorClip = std::move(clip);
    return true;
}
bool SceneViewModel::finishComponentTransform(bool commit) {
    if (!componentTransform_) {
        return false;
    }
    if (commit && !isComponentTransformContextValid()) {
        finishComponentTransform(false);
        emit operationFailed(QStringLiteral("组件变换上下文已失效，本次候选已取消。"));
        return false;
    }
    if (commit && !componentTransform_->valid) {
        emit operationFailed(QStringLiteral("当前组件候选无效，请修正数值或取消。"));
        return false;
    }
    const auto edit = std::move(*componentTransform_);
    componentTransform_.reset();
    if (commit && edit.before.content()->source != edit.candidate.content()->source) {
        if (edit.extrusion || edit.inset || edit.bevel)
            pushModelingHistory(edit);
        else
            pushGeometryEdit(edit.entity, edit.before, edit.candidate, edit.label, edit.selection,
                             edit.previewSelection.value_or(edit.selection));
    }
    emit componentPreviewChanged();
    emit componentTransformFinished();
    return true;
}
void SceneViewModel::pushModelingHistory(const ComponentTransform& edit) {
    ReopenableGeometryEdit operation;
    operation.entity = edit.entity;
    operation.label = edit.label;
    operation.before = {edit.before, edit.selection};
    operation.after = {edit.candidate, edit.previewSelection.value_or(edit.selection)};
    if (edit.bevel)
        operation.parameters = BevelOperationParameters{edit.bevelWidth};
    else if (edit.inset)
        operation.parameters = edit.insetThickness;
    else
        operation.parameters = edit.extrusionWorldOffset;
    operation.recompute = [this, entity = edit.entity, before = edit.before, world = edit.world,
                           faces = selectedFaceIds(edit.selection),
                           edge = edit.bevel ? std::optional(edit.bevel->edge) : std::nullopt](
                              const GeometryOperationParameters& parameters, std::string& error) {
        if (const auto* bevel = std::get_if<BevelOperationParameters>(&parameters)) {
            const auto* node = scene_->find(entity);
            const auto* current = node ? scene_->editableMesh(node->editableMesh) : nullptr;
            if (!current || current->content->mirror != before.content()->mirror ||
                current->content->subdivision != before.content()->subdivision) {
                error = "倒角使用的修改器参数已变化，请重新执行倒角。";
                return std::optional<core::Scene::GeometrySnapshot>{};
            }
            for (const auto& face : before.content()->source.faces) {
                const auto affected = std::any_of(
                    face.corners.begin(), face.corners.end(), [&edge](const auto& corner) {
                        return corner.vertex == edge->first || corner.vertex == edge->second;
                    });
                if (affected &&
                    !viewportVisibility_.isFaceVisible(current->content->source, face.id)) {
                    error = "倒角邻面或端面包含隐藏元素；请先恢复显示再调整宽度。";
                    return std::optional<core::Scene::GeometrySnapshot>{};
                }
            }
            const auto candidate =
                core::modeling::bevelEdge(before.content()->source, *edge, bevel->width);
            if (!candidate.mesh) {
                error = candidate.error;
                return std::optional<core::Scene::GeometrySnapshot>{};
            }
            if (!viewportVisibility_.isFaceVisible(current->content->source, candidate.bevelFace)) {
                error = "倒角结果面已隐藏；请先 Alt+H 恢复显示再调整宽度。";
                return std::optional<core::Scene::GeometrySnapshot>{};
            }
            return scene_->prepareEditableGeometry(entity, *candidate.mesh, error);
        }
        if (const auto* thickness = std::get_if<double>(&parameters)) {
            const auto candidate =
                core::modeling::insetFace(before.content()->source, *faces.begin(), *thickness);
            if (!candidate.mesh) {
                error = candidate.error;
                return std::optional<core::Scene::GeometrySnapshot>{};
            }
            return scene_->prepareEditableGeometry(entity, *candidate.mesh, error);
        }
        const auto& value = std::get<glm::dvec3>(parameters);
        const auto local = glm::inverse(world) * glm::dvec4(value, 0);
        const auto candidate =
            core::modeling::extrudeRegion(before.content()->source, faces, glm::dvec3(local));
        if (!candidate.mesh) {
            error = candidate.error;
            return std::optional<core::Scene::GeometrySnapshot>{};
        }
        return scene_->prepareEditableGeometry(entity, *candidate.mesh, error);
    };
    operation.install = [this, entity = edit.entity](GeometryHistoryState state) {
        // 分配和选区复制已在服务的准备阶段完成；此段只安装快照与移动容器，不发通知。
        const bool installed = scene_->installGeometry(state.geometry);
        Q_ASSERT(installed);
        if (isEditMode() && editedEntity_ == entity) {
            componentSelection_ = std::move(state.selection);
            ++componentSelectionRevision_;
        }
    };
    operation.notify = [this, entity = edit.entity] {
        if (isEditMode() && editedEntity_ == entity)
            emit componentSelectionChanged();
        emit entityChanged(entity);
        emit sceneChanged();
    };
    historyService_.push(std::move(operation));
}
QString SceneViewModel::lastOperationDisabledReason() const {
    if (!historyService_.canAdjust())
        return QStringLiteral("当前没有可调整的上一步。");
    if (componentTransform_ || transformEdit_)
        return QStringLiteral("请先确认或取消当前变换，再调整上一步。");
    if (!isEditMode() || editedEntity_ != historyService_.target())
        return QStringLiteral("请回到上一步对象的编辑模式，再调整参数。");
    return {};
}
QString SceneViewModel::repeatLastOperationDisabledReason() const {
    if (!historyService_.canAdjust())
        return QStringLiteral("当前历史末端不是可重复的挤出、内插或倒角。");
    if (componentTransform_ || transformEdit_)
        return QStringLiteral("请先确认或取消当前变换，再重复上一步。");
    if (!isEditMode())
        return QStringLiteral("请进入当前对象的编辑模式，再重复建模操作。");
    if (historyService_.insetThickness())
        return insetFaceDisabledReason();
    if (historyService_.bevelWidth())
        return bevelEdgeDisabledReason();
    return extrudeRegionDisabledReason();
}
bool SceneViewModel::repeatLastOperation() {
    const auto reason = repeatLastOperationDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    // 只复用参数，不调用F9冻结的before重算回调；begin始终读取当前源与选区。
    const auto inset = historyService_.insetThickness();
    const auto bevel = historyService_.bevelWidth();
    const auto extrusion = historyService_.worldOffset();
    bool valid = false;
    if (inset) {
        if (!beginInsetFace())
            return false;
        valid = previewInsetFace(*inset);
    } else if (bevel) {
        if (!beginBevelEdge())
            return false;
        valid = previewBevelEdge(*bevel);
    } else {
        if (!beginExtrudeRegion())
            return false;
        valid = previewComponentTransform(glm::translate(glm::dmat4(1), *extrusion));
    }
    if (!valid) {
        finishComponentTransform(false);
        return false;
    }
    return finishComponentTransform(true);
}
std::optional<glm::dvec3> SceneViewModel::lastOperationWorldOffset() const {
    return lastOperationDisabledReason().isEmpty() ? historyService_.worldOffset() : std::nullopt;
}
std::optional<double> SceneViewModel::lastOperationInsetThickness() const {
    return lastOperationDisabledReason().isEmpty() ? historyService_.insetThickness()
                                                   : std::nullopt;
}
std::optional<double> SceneViewModel::lastOperationBevelWidth() const {
    return lastOperationDisabledReason().isEmpty() ? historyService_.bevelWidth() : std::nullopt;
}
bool SceneViewModel::adjustLastInset(double localThickness) {
    auto error = lastOperationDisabledReason();
    if (!error.isEmpty() || !historyService_.adjustInset(localThickness, error)) {
        emit operationFailed(error);
        return false;
    }
    emit lastOperationChanged();
    return true;
}
bool SceneViewModel::adjustLastBevel(double localWidth) {
    auto error = lastOperationDisabledReason();
    if (!error.isEmpty() || !historyService_.adjustBevel(localWidth, error)) {
        emit operationFailed(error);
        return false;
    }
    emit lastOperationChanged();
    return true;
}
bool SceneViewModel::adjustLastOperation(const glm::dvec3& worldOffset) {
    auto error = lastOperationDisabledReason();
    if (!error.isEmpty() || !historyService_.adjust(worldOffset, error)) {
        emit operationFailed(error);
        return false;
    }
    emit lastOperationChanged();
    return true;
}
void SceneViewModel::duplicateSelected() {
    if (rejectObjectEdit()) {
        return;
    }
    cancelTransformEdit();
    if (const auto id = selection_.selectedEntity(); scene_->find(id)) {
        history_.push(new SubtreeCommand(*this, id, SubtreeCommand::Kind::Duplicate));
    }
}
void SceneViewModel::deleteSelected() {
    if (rejectObjectEdit()) {
        return;
    }
    cancelTransformEdit();
    if (const auto id = selection_.selectedEntity(); scene_->find(id)) {
        history_.push(new SubtreeCommand(*this, id, SubtreeCommand::Kind::Delete));
    }
}
bool SceneViewModel::setSurface(core::EntityId id, const core::SurfaceStyle& surface) {
    cancelTransformEdit();
    const auto* node = scene_->find(id);
    if (!node || !surface.isValid()) {
        return false;
    }
    const auto before = node->surface;
    if (before == surface) {
        return true;
    }
    const auto apply = [this, id](const core::SurfaceStyle& value) {
        scene_->setSurface(id, value);
        emit entityChanged(id);
        emit sceneChanged();
    };
    history_.push(new EditCommand(
        QStringLiteral("表面材质"),
        [apply, before] {
            apply(before);
        },
        [apply, surface] {
            apply(surface);
        }));
    return true;
}
bool SceneViewModel::setLighting(const core::Lighting& lighting) {
    cancelTransformEdit();
    if (!lighting.isValid()) {
        emit operationFailed(QStringLiteral("光源方向不能为零；请使用有效的颜色和强度。"));
        return false;
    }
    const auto before = scene_->lighting();
    if (before == lighting) {
        return true;
    }
    const auto apply = [this](const core::Lighting& value) {
        scene_->setLighting(value);
        emit sceneChanged();
    };
    history_.push(new EditCommand(
        QStringLiteral("光照"),
        [apply, before] {
            apply(before);
        },
        [apply, lighting] {
            apply(lighting);
        }));
    return true;
}
QString SceneViewModel::filePath() const {
    return filePath_;
}
bool SceneViewModel::requiresSaveAs() const {
    return !legacySourcePath_.isEmpty();
}
bool SceneViewModel::makeEditable(core::EntityId id) {
    const auto* node = scene_->find(id);
    if (!node || !scene_->isVisible(id) || node->camera || node->light) {
        emit operationFailed(QStringLiteral("请选择可见的几何对象；相机和灯光不能进入编辑模式。"));
        return false;
    }
    if (node->editableMesh != 0) {
        return true;
    }
    if (node->primitive != core::PrimitiveKind::Cube) {
        emit operationFailed(
            QStringLiteral("当前编辑接入仅支持原生立方体；此对象仍保持静态几何。"));
        return false;
    }
    return commitEditableMesh(id, core::modeling::createEditableCube(),
                              QStringLiteral("转为可编辑网格"));
}
bool SceneViewModel::replaceEditableMesh(core::EntityId id,
                                         const core::modeling::EditableMesh& source,
                                         const QString& label) {
    const auto* node = scene_->find(id);
    if (!node || node->editableMesh == 0) {
        emit operationFailed(QStringLiteral("对象尚未绑定可编辑网格。"));
        return false;
    }
    if (scene_->editableMesh(node->editableMesh)->content->source == source) {
        return true;
    }
    return commitEditableMesh(id, source, label);
}
std::optional<core::modeling::MirrorOptions> SceneViewModel::mirrorOptions(core::EntityId id) const {
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    return record ? record->content->mirror : std::nullopt;
}
bool SceneViewModel::setMirrorOptions(
    core::EntityId id, std::optional<core::modeling::MirrorOptions> options) {
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    if (!record) {
        emit operationFailed(QStringLiteral("请先将对象转换为可编辑网格。"));
        return false;
    }
    if (record->content->mirror == options)
        return true;
    cancelTransformEdit();
    std::string error;
    const auto before = *scene_->geometrySnapshot(id);
    const auto after = scene_->prepareMirror(id, std::move(options), error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    const auto selected = isEditMode() && editedEntity_ == id
                              ? std::optional(componentSelection_) : std::nullopt;
    pushGeometryEdit(id, before, *after, QStringLiteral("修改 Mirror"), selected, selected);
    return true;
}
bool SceneViewModel::applyMirror(core::EntityId id) {
    if (!mirrorOptions(id)) {
        emit operationFailed(QStringLiteral("当前对象没有 Mirror。"));
        return false;
    }
    cancelTransformEdit();
    std::string error;
    const auto before = *scene_->geometrySnapshot(id);
    const auto after = scene_->prepareAppliedMirror(id, error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    const auto selected = isEditMode() && editedEntity_ == id
                              ? std::optional(componentSelection_) : std::nullopt;
    auto afterSelection = selected;
    if (afterSelection)
        afterSelection->reconcile(after->content()->source);
    pushGeometryEdit(id, before, *after, QStringLiteral("应用 Mirror"), selected, afterSelection);
    return true;
}
std::optional<core::modeling::SubdivisionOptions>
SceneViewModel::subdivisionOptions(core::EntityId id) const {
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    return record ? record->content->subdivision : std::nullopt;
}
bool SceneViewModel::setSubdivisionOptions(
    core::EntityId id, std::optional<core::modeling::SubdivisionOptions> options) {
    const auto* node = scene_->find(id);
    const auto* record = node ? scene_->editableMesh(node->editableMesh) : nullptr;
    if (!record) {
        emit operationFailed(QStringLiteral("请先将对象转换为可编辑网格。"));
        return false;
    }
    if (record->content->subdivision == options)
        return true;
    cancelTransformEdit();
    std::string error;
    const auto before = *scene_->geometrySnapshot(id);
    const auto after = scene_->prepareSubdivision(id, std::move(options), error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    const auto selected =
        isEditMode() && editedEntity_ == id ? std::optional(componentSelection_) : std::nullopt;
    pushGeometryEdit(id, before, *after, QStringLiteral("修改细分"), selected, selected);
    return true;
}
bool SceneViewModel::applySubdivision(core::EntityId id) {
    if (!subdivisionOptions(id)) {
        emit operationFailed(QStringLiteral("当前对象没有细分。"));
        return false;
    }
    cancelTransformEdit();
    std::string error;
    const auto before = *scene_->geometrySnapshot(id);
    const auto after = scene_->prepareAppliedSubdivision(id, error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    const auto selected =
        isEditMode() && editedEntity_ == id ? std::optional(componentSelection_) : std::nullopt;
    auto afterSelection = selected;
    if (afterSelection)
        afterSelection->reconcile(after->content()->source);
    pushGeometryEdit(id, before, *after, QStringLiteral("应用细分链"), selected, afterSelection);
    return true;
}
bool SceneViewModel::changeCollections(const std::vector<core::SceneCollection>& after,
                                       const QString& label) {
    const auto before = scene_->collections();
    if (before == after)
        return true;
    auto candidate = *scene_;
    if (!candidate.replaceCollections(after)) {
        emit operationFailed(QStringLiteral("集合名称/身份或成员归属无效，未修改场景。"));
        return false;
    }
    cancelTransformEdit();
    const auto apply = [this](const std::vector<core::SceneCollection>& collections) {
        const bool installed = scene_->replaceCollections(collections);
        Q_ASSERT(installed);
        emit sceneChanged();
    };
    history_.push(new EditCommand(
        label,
        [apply, before] {
            apply(before);
        },
        [apply, after] {
            apply(after);
        }));
    return true;
}
core::CollectionId SceneViewModel::createCollection(const QString& name) {
    auto candidate = *scene_;
    const auto id = candidate.createCollection(name.trimmed().toStdString());
    if (!id || !changeCollections(candidate.collections(), QStringLiteral("新建集合"))) {
        if (!id)
            emit operationFailed(QStringLiteral("集合名称不能为空，或集合编号已耗尽。"));
        return 0;
    }
    return id;
}
bool SceneViewModel::removeCollection(core::CollectionId id) {
    auto after = scene_->collections();
    if (std::erase_if(after, [id](const auto& collection) {
            return collection.id == id;
        }) == 0)
        return false;
    return changeCollections(after, QStringLiteral("删除集合（保留对象）"));
}
bool SceneViewModel::renameCollection(core::CollectionId id, const QString& name) {
    auto after = scene_->collections();
    for (auto& collection : after)
        if (collection.id == id) {
            collection.name = name.trimmed().toStdString();
            return changeCollections(after, QStringLiteral("重命名集合"));
        }
    return false;
}
bool SceneViewModel::setCollectionVisible(core::CollectionId id, bool visible) {
    auto after = scene_->collections();
    for (auto& collection : after)
        if (collection.id == id) {
            collection.visible = visible;
            return changeCollections(after, QStringLiteral("集合显隐"));
        }
    return false;
}
bool SceneViewModel::assignEntityToCollection(core::EntityId entity, core::CollectionId id) {
    if (!scene_->find(entity))
        return false;
    auto after = scene_->collections();
    if (id && std::none_of(after.begin(), after.end(), [id](const auto& collection) {
            return collection.id == id;
        }))
        return false;
    for (auto& collection : after) {
        collection.members.erase(entity);
        if (collection.id == id)
            collection.members.insert(entity);
    }
    return changeCollections(after, QStringLiteral("更改集合成员"));
}
bool SceneViewModel::commitEditableMesh(core::EntityId id,
                                        const core::modeling::EditableMesh& source,
                                        const QString& label) {
    std::string error;
    const auto before = scene_->geometrySnapshot(id);
    const auto after = scene_->prepareEditableGeometry(id, source, error);
    if (!after) {
        emit operationFailed(QString::fromStdString(error));
        return false;
    }
    cancelTransformEdit();
    const auto selected =
        isEditMode() && editedEntity_ == id ? std::optional(componentSelection_) : std::nullopt;
    auto afterSelection = selected;
    if (afterSelection) {
        afterSelection->reconcile(source);
    }
    pushGeometryEdit(id, *before, *after, label, selected, afterSelection);
    return true;
}
void SceneViewModel::pushGeometryEdit(core::EntityId id,
                                      const core::Scene::GeometrySnapshot& before,
                                      const core::Scene::GeometrySnapshot& after,
                                      const QString& label,
                                      const std::optional<ComponentSelection>& beforeSelection,
                                      const std::optional<ComponentSelection>& afterSelection) {
    const auto apply = [this, id](const core::Scene::GeometrySnapshot& snapshot,
                                  const std::optional<ComponentSelection>& selection) {
        const bool installed = scene_->installGeometry(snapshot);
        Q_ASSERT(installed);
        if (selection && isEditMode() && editedEntity_ == id) {
            componentSelection_ = *selection;
            notifyComponentSelection();
        }
        emit entityChanged(id);
        emit sceneChanged();
    };
    history_.push(new EditCommand(
        label,
        [apply, before, beforeSelection] {
            apply(before, beforeSelection);
        },
        [apply, after, afterSelection] {
            apply(after, afterSelection);
        }));
}
bool SceneViewModel::isModified() const {
    return !history_.isClean() || editorCamera_ != savedCamera_;
}
const core::CameraState& SceneViewModel::editorCamera() const {
    return editorCamera_;
}
void SceneViewModel::setEditorCamera(const core::CameraState& camera) {
    if (camera.isValid() && camera != editorCamera_) {
        editorCamera_ = camera;
        emit documentChanged();
    }
}
const core::Cursor3D& SceneViewModel::cursor3D() const {
    return cursor_;
}
bool SceneViewModel::setCursorPosition(const glm::vec3& position) {
    const core::Cursor3D candidate{position, cursor_.visible};
    if (previewCamera_ != core::kInvalidEntity || !candidate.isValid()) {
        emit operationFailed(QStringLiteral("无法定位游标：请返回编辑视图并输入有限坐标。"));
        return false;
    }
    cancelTransformEdit();
    if (cursor_ != candidate) {
        cursor_ = candidate;
        emit cursorChanged();
    }
    return true;
}
void SceneViewModel::setCursorVisible(bool visible) {
    if (cursor_.visible != visible) {
        cursor_.visible = visible;
        emit cursorChanged();
    }
}
std::optional<glm::vec3> SceneViewModel::cursorSelectionCenter() const {
    if (isEditMode()) {
        const auto center = selectedComponentCenter();
        return center ? std::optional(glm::vec3(*center)) : std::nullopt;
    }
    const auto id = selection_.selectedEntity();
    return scene_->find(id) ? std::optional(glm::vec3(scene_->worldMatrix(id)[3])) : std::nullopt;
}
bool SceneViewModel::moveCursorToSelection() {
    cancelTransformEdit();
    const auto center = cursorSelectionCenter();
    if (!center) {
        emit operationFailed(QStringLiteral("请先选择对象或网格组件，再将游标移到选择。"));
        return false;
    }
    return setCursorPosition(*center);
}
TransformPivot SceneViewModel::transformPivot() const {
    return transformPivot_;
}
SnapMode SceneViewModel::snapMode() const {
    return snapMode_;
}
void SceneViewModel::setSnapMode(SnapMode mode) {
    if (snapMode_ == mode)
        return;
    cancelTransformEdit();
    snapMode_ = mode;
    emit snapModeChanged();
}
void SceneViewModel::setTransformPivot(TransformPivot pivot) {
    if (transformPivot_ == pivot)
        return;
    cancelTransformEdit();
    transformPivot_ = pivot;
    emit transformPivotChanged();
}
QString SceneViewModel::transformPivotName() const {
    switch (transformPivot_) {
        case TransformPivot::Median:
            return QStringLiteral("选择质心");
        case TransformPivot::Active:
            return QStringLiteral("活动元素");
        case TransformPivot::Cursor:
            return QStringLiteral("3D 游标");
    }
    return {};
}
std::optional<glm::dvec3> SceneViewModel::transformPivotPosition() const {
    const auto objectCenter = isEditMode() ? std::nullopt : cursorSelectionCenter();
    const auto center =
        isEditMode() ? selectedComponentCenter()
                     : (objectCenter ? std::optional(glm::dvec3(*objectCenter)) : std::nullopt);
    if (!center)
        return std::nullopt;
    if (transformPivot_ == TransformPivot::Cursor)
        return glm::dvec3(cursor_.position);
    if (isEditMode() && transformPivot_ == TransformPivot::Active) {
        const auto& source = scene_->editableMesh(editedMesh_)->content->source;
        if (const auto local = componentSelection_.activePosition(source))
            return glm::dvec3(glm::dmat4(scene_->worldMatrix(editedEntity_)) *
                              glm::dvec4(*local, 1));
    }
    return center;
}
QString SceneViewModel::selectionToCursorDisabledReason() const {
    if (previewCamera_ != 0 || !viewportVisibility_.isVisible(*scene_, selection_.selectedEntity()))
        return QStringLiteral("请在编辑视图选择可见对象或网格组件。");
    if (!cursorSelectionCenter())
        return QStringLiteral("请先选择对象或网格组件。");
    return {};
}
bool SceneViewModel::moveSelectionToCursor() {
    cancelTransformEdit();
    const auto reason = selectionToCursorDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    if (isEditMode()) {
        const auto delta = glm::dvec3(cursor_.position) - *selectedComponentCenter();
        if (!beginComponentTransform(QStringLiteral("选择到游标（保持偏移）")))
            return false;
        if (!previewComponentTransform(glm::translate(glm::dmat4(1), delta))) {
            finishComponentTransform(false);
            return false;
        }
        return finishComponentTransform(true);
    }
    const auto id = selection_.selectedEntity();
    const auto* node = scene_->find(id);
    auto transform = node->transform;
    transform.position =
        glm::vec3(glm::inverse(scene_->worldMatrix(node->parent)) * glm::vec4(cursor_.position, 1));
    return setTransform(id, transform);
}
void SceneViewModel::newScene() {
    cancelTransformEdit();
    setPreviewCamera(0);
    selection_.setSelectedEntity(0);
    emit structureAboutToChange();
    *scene_ = core::Scene{};
    assets_ = std::make_shared<assets::AssetManager>();
    history_.clear();
    filePath_.clear();
    legacySourcePath_.clear();
    editorCamera_ = renderer_gl::EditorCamera{}.state();
    cursor_ = {};
    emit cursorChanged();
    savedCamera_ = editorCamera_;
    emit structureChanged();
    emit documentReset();
    emit sceneChanged();
    emit documentChanged();
}
bool SceneViewModel::openScene(const QString& path) {
    assets::LoadedScene loaded;
    QString error;
    if (!assets::SceneDocument::read(path, loaded, error)) {
        emit operationFailed(error);
        return false;
    }
    cancelTransformEdit();
    setPreviewCamera(0);
    selection_.setSelectedEntity(0);
    emit structureAboutToChange();
    *scene_ = std::move(loaded.scene);
    assets_ = std::move(loaded.assets);
    history_.clear();
    filePath_ = QFileInfo(path).absoluteFilePath();
    legacySourcePath_ = loaded.sourceVersion < 3 ? QFileInfo(path).canonicalFilePath() : QString{};
    editorCamera_ = loaded.camera;
    cursor_ = loaded.cursor;
    emit cursorChanged();
    savedCamera_ = editorCamera_;
    emit structureChanged();
    emit documentReset();
    emit sceneChanged();
    emit documentChanged();
    emit operationCompleted(QStringLiteral("已打开 %1").arg(filePath_));
    return true;
}
bool SceneViewModel::isProportionalEditingEnabled() const {
    return proportionalEditingEnabled_;
}
bool SceneViewModel::isProportionalConnected() const {
    return proportionalConnected_;
}
double SceneViewModel::proportionalRadius() const {
    return proportionalRadius_;
}
void SceneViewModel::setProportionalEditingEnabled(bool enabled) {
    if (proportionalEditingEnabled_ == enabled)
        return;
    proportionalEditingEnabled_ = enabled;
    emit proportionalEditingChanged();
}
void SceneViewModel::setProportionalConnected(bool connected) {
    if (proportionalConnected_ == connected)
        return;
    proportionalConnected_ = connected;
    emit proportionalEditingChanged();
}
bool SceneViewModel::setProportionalRadius(double worldRadius) {
    if (!std::isfinite(worldRadius) || worldRadius <= 0) {
        emit operationFailed(QStringLiteral("比例编辑半径必须为有限正数（世界单位）。"));
        return false;
    }
    if (proportionalRadius_ != worldRadius) {
        proportionalRadius_ = worldRadius;
        emit proportionalEditingChanged();
    }
    return true;
}
QString SceneViewModel::objExportDisabledReason() const {
    if (transformEdit_ || componentTransform_)
        return QStringLiteral("请先确认或取消当前预览，再导出已确认几何。");
    const auto* node = scene_->find(selection_.selectedEntity());
    if (!node || node->camera || node->light ||
        (!node->editableMesh && !node->meshRenderer &&
         node->primitive == core::PrimitiveKind::Empty))
        return QStringLiteral("请选择包含几何的对象；相机、灯光和空对象不能导出 OBJ。");
    return {};
}
bool SceneViewModel::exportObj(const QString& path, bool evaluated) {
    const auto reason = objExportDisabledReason();
    if (!reason.isEmpty()) {
        emit operationFailed(reason);
        return false;
    }
    const auto id = selection_.selectedEntity();
    const auto& node = *scene_->find(id);
    const glm::dmat4 world(scene_->worldMatrix(id));
    core::modeling::ObjExportResult result;
    if (node.editableMesh) {
        const auto& content = *scene_->editableMesh(node.editableMesh)->content;
        const auto& mesh = evaluated ? content.evaluatedMesh() : content.source;
        result = core::modeling::encodeObj(mesh, world, node.name);
    } else if (node.meshRenderer) {
        const auto* mesh = assets_->mesh(node.meshRenderer->mesh);
        if (!mesh) {
            emit operationFailed(QStringLiteral("所选对象缺少静态网格资源，无法导出。"));
            return false;
        }
        result = core::modeling::encodeObj(mesh->data, world, node.name);
    } else {
        const auto mesh = node.primitive == core::PrimitiveKind::Cube
                              ? renderer_gl::PrimitiveFactory::createCube()
                          : node.primitive == core::PrimitiveKind::Sphere
                              ? renderer_gl::PrimitiveFactory::createSphere()
                              : renderer_gl::PrimitiveFactory::createPlane();
        result = core::modeling::encodeObj(mesh, world, node.name);
    }
    QString error;
    if (!result.text) {
        error = QString::fromStdString(result.error);
    } else if (assets::ObjDocument::write(path, *result.text, error)) {
        emit operationCompleted(QStringLiteral("已导出 OBJ（%1）：%2")
                                    .arg(evaluated ? QStringLiteral("修改器结果")
                                                   : QStringLiteral("可编辑源"),
                                         path));
        return true;
    }
    emit operationFailed(error);
    return false;
}
bool SceneViewModel::saveScene(const QString& path) {
    cancelTransformEdit();
    if (requiresSaveAs()) {
        const QFileInfo target(path);
        const auto resolved =
            target.exists() ? target.canonicalFilePath() : target.absoluteFilePath();
#ifdef Q_OS_WIN
        constexpr auto pathCase = Qt::CaseInsensitive;
#else
        constexpr auto pathCase = Qt::CaseSensitive;
#endif
        if (resolved.compare(legacySourcePath_, pathCase) == 0) {
            emit operationFailed(
                QStringLiteral("旧版工程首次升级须另存到新路径，原文件保持不变。"));
            return false;
        }
    }
    QString error;
    if (!assets::SceneDocument::write(path, *scene_, *assets_, editorCamera_, error, cursor_)) {
        emit operationFailed(error);
        return false;
    }
    filePath_ = QFileInfo(path).absoluteFilePath();
    legacySourcePath_.clear();
    savedCamera_ = editorCamera_;
    history_.setClean();
    emit documentChanged();
    emit operationCompleted(QStringLiteral("已保存 %1").arg(filePath_));
    return true;
}
} // namespace mini3d::editor
