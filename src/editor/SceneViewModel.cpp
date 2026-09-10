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
#include "assets/SceneDocument.h"
#include "renderer_gl/EditorCamera.h"
#include "renderer_gl/RayCaster.h"

#include <QDebug>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <functional>
#include <glm/ext/matrix_transform.hpp>
namespace mini3d::editor {
SceneViewModel::SceneViewModel(QObject* parent)
    : QObject(parent), scene_(std::make_shared<core::Scene>()), selection_(*scene_) {
    setObjectName(QStringLiteral("SceneViewModel"));
    editorCamera_ = renderer_gl::EditorCamera{}.state();
    savedCamera_ = editorCamera_;
    connect(&history_, &QUndoStack::cleanChanged, this, &SceneViewModel::documentChanged);
    connect(&selection_, &SelectionModel::selectedEntityChanged, this,
            &SceneViewModel::cancelTransformEdit);
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

std::shared_ptr<const assets::AssetManager> SceneViewModel::assets() const {
    return assets_;
}

core::EntityId SceneViewModel::importGltf(const QString& path) {
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
    }
    cancelTransformEdit();
    if (previewCamera_ != id) {
        previewCamera_ = id;
        emit previewCameraChanged(id);
    }
    return true;
}
core::EntityId SceneViewModel::previewCamera() const {
    return previewCamera_;
}
void SceneViewModel::selectRay(const core::Ray& ray) {
    selection_.setSelectedEntity(renderer_gl::RayCaster::pick(*scene_, *assets_, ray));
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
    finishTransformEdit(false);
}
void SceneViewModel::duplicateSelected() {
    cancelTransformEdit();
    if (const auto id = selection_.selectedEntity(); scene_->find(id)) {
        history_.push(new SubtreeCommand(*this, id, SubtreeCommand::Kind::Duplicate));
    }
}
void SceneViewModel::deleteSelected() {
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
void SceneViewModel::newScene() {
    cancelTransformEdit();
    setPreviewCamera(0);
    selection_.setSelectedEntity(0);
    emit structureAboutToChange();
    *scene_ = core::Scene{};
    assets_ = std::make_shared<assets::AssetManager>();
    history_.clear();
    filePath_.clear();
    editorCamera_ = renderer_gl::EditorCamera{}.state();
    savedCamera_ = editorCamera_;
    emit structureChanged();
    emit documentReset();
    emit sceneChanged();
    emit documentChanged();
}
bool SceneViewModel::openScene(const QString& path) {
    cancelTransformEdit();
    assets::LoadedScene loaded;
    QString error;
    if (!assets::SceneDocument::read(path, loaded, error)) {
        emit operationFailed(error);
        return false;
    }
    setPreviewCamera(0);
    selection_.setSelectedEntity(0);
    emit structureAboutToChange();
    *scene_ = std::move(loaded.scene);
    assets_ = std::move(loaded.assets);
    history_.clear();
    filePath_ = QFileInfo(path).absoluteFilePath();
    editorCamera_ = loaded.camera;
    savedCamera_ = editorCamera_;
    emit structureChanged();
    emit documentReset();
    emit sceneChanged();
    emit documentChanged();
    emit operationCompleted(QStringLiteral("已打开 %1").arg(filePath_));
    return true;
}
bool SceneViewModel::saveScene(const QString& path) {
    cancelTransformEdit();
    QString error;
    if (!assets::SceneDocument::write(path, *scene_, *assets_, editorCamera_, error)) {
        emit operationFailed(error);
        return false;
    }
    filePath_ = QFileInfo(path).absoluteFilePath();
    savedCamera_ = editorCamera_;
    history_.setClean();
    emit documentChanged();
    emit operationCompleted(QStringLiteral("已保存 %1").arg(filePath_));
    return true;
}
} // namespace mini3d::editor
