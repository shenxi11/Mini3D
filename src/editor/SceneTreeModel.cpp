/*
 * 模块名: SceneTreeModel
 * 功能概述: 第三周场景选择与属性编辑的 SceneTreeModel 层。
 * 对外接口: SceneTreeModel
 * 依赖关系: Qt Widgets/Core、mini3d_core
 * 输入输出: 输入用户意图或场景通知，输出模型状态或界面刷新。
 * 异常与错误: 通过返回值和 operationFailed 报告非法编辑。
 * 维护说明: 同步 UI 线程操作；不持有节点地址或 GPU 资源。
 */
#include "SceneTreeModel.h"

#include <QMimeData>
#include <QUuid>
#include <algorithm>
namespace mini3d::editor {
SceneTreeModel::SceneTreeModel(SceneViewModel& viewModel, QObject* parent)
    : QAbstractItemModel(parent), dragToken_(QUuid::createUuid().toByteArray()),
      viewModel_(viewModel) {
    connect(&viewModel_, &SceneViewModel::documentReset, this, [this] {
        dragToken_ = QUuid::createUuid().toByteArray();
    });
    connect(&viewModel_, &SceneViewModel::structureAboutToChange, this, [this] {
        resetting_ = true;
        beginResetModel();
    });
    connect(&viewModel_, &SceneViewModel::structureChanged, this, [this] {
        endResetModel();
        resetting_ = false;
    });
    connect(&viewModel_, &SceneViewModel::entityChanged, this, [this](core::EntityId id) {
        const auto changed = indexForEntity(id);
        emit dataChanged(changed, changed, {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});
    });
}
core::EntityId SceneTreeModel::entityId(const QModelIndex& index) const {
    return index.isValid() && index.model() == this ? index.internalId() : core::kInvalidEntity;
}
QModelIndex SceneTreeModel::index(int row, int column, const QModelIndex& parentIndex) const {
    if (row < 0 || column != 0 || (parentIndex.isValid() && parentIndex.column() != 0)) {
        return {};
    }
    const auto scene = viewModel_.scene();
    const auto* node = scene->find(entityId(parentIndex));
    const auto children = node != nullptr ? node->children : scene->roots();
    if (row >= static_cast<int>(children.size())) {
        return {};
    }
    return createIndex(row, column, static_cast<quintptr>(children[static_cast<std::size_t>(row)]));
}
QModelIndex SceneTreeModel::parent(const QModelIndex& child) const {
    const auto* node = viewModel_.scene()->find(entityId(child));
    return node != nullptr ? indexForEntity(node->parent) : QModelIndex{};
}
QModelIndex SceneTreeModel::indexForEntity(core::EntityId id) const {
    const auto scene = viewModel_.scene();
    const auto* node = scene->find(id);
    if (node == nullptr) {
        return {};
    }
    const auto* parentNode = scene->find(node->parent);
    const auto siblings = parentNode != nullptr ? parentNode->children : scene->roots();
    const auto found = std::find(siblings.begin(), siblings.end(), id);
    return createIndex(static_cast<int>(std::distance(siblings.begin(), found)), 0,
                       static_cast<quintptr>(id));
}
int SceneTreeModel::rowCount(const QModelIndex& parentIndex) const {
    if (parentIndex.isValid() && parentIndex.column() != 0) {
        return 0;
    }
    const auto scene = viewModel_.scene();
    const auto* node = scene->find(entityId(parentIndex));
    return static_cast<int>(node != nullptr ? node->children.size() : scene->roots().size());
}
int SceneTreeModel::columnCount(const QModelIndex&) const {
    return 1;
}
QVariant SceneTreeModel::data(const QModelIndex& index, int role) const {
    const auto* node = viewModel_.scene()->find(entityId(index));
    if (node == nullptr) {
        return {};
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        return QString::fromStdString(node->name);
    }
    if (role == Qt::CheckStateRole) {
        return node->visible ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::ToolTipRole) {
        return QStringLiteral("对象 %1").arg(node->id);
    }
    return {};
}
bool SceneTreeModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid()) {
        return false;
    }
    if (role == Qt::EditRole) {
        return viewModel_.renameEntity(entityId(index), value.toString());
    }
    if (role == Qt::CheckStateRole) {
        return viewModel_.setVisible(entityId(index), value.toInt() == Qt::Checked);
    }
    return false;
}
Qt::ItemFlags SceneTreeModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::ItemIsDropEnabled;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsUserCheckable |
           Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
}
QStringList SceneTreeModel::mimeTypes() const {
    return {QStringLiteral("application/x-mini3d-entity")};
}
Qt::DropActions SceneTreeModel::supportedDropActions() const {
    return Qt::MoveAction;
}
Qt::DropActions SceneTreeModel::supportedDragActions() const {
    return Qt::MoveAction;
}
QMimeData* SceneTreeModel::mimeData(const QModelIndexList& indexes) const {
    auto* data = new QMimeData;
    if (indexes.size() == 1 && viewModel_.scene()->find(entityId(indexes.front()))) {
        data->setData(mimeTypes().front(),
                      dragToken_ + ':' +
                          QByteArray::number(static_cast<qulonglong>(entityId(indexes.front()))));
    }
    return data;
}
core::EntityId SceneTreeModel::draggedEntity(const QMimeData* data) const {
    if (!data) {
        return 0;
    }
    const auto payload = data->data(mimeTypes().front());
    const auto prefix = dragToken_ + ':';
    if (!payload.startsWith(prefix)) {
        return 0;
    }
    return payload.mid(prefix.size()).toULongLong();
}
bool SceneTreeModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row,
                                     int column, const QModelIndex& parentIndex) const {
    // 只接受落在对象或空白区的换父，不支持兄弟行插入排序。
    if (action != Qt::MoveAction || row != -1 || column > 0 ||
        (parentIndex.isValid() && parentIndex.model() != this)) {
        return false;
    }
    const auto scene = viewModel_.scene();
    const auto id = draggedEntity(data);
    const auto* node = scene->find(id);
    const auto parentId = entityId(parentIndex);
    if (!node || (parentIndex.isValid() && !scene->find(parentId)) || node->parent == parentId) {
        return false;
    }
    for (auto ancestor = parentId; ancestor != 0; ancestor = scene->find(ancestor)->parent) {
        if (ancestor == id) {
            return false;
        }
    }
    return true;
}
bool SceneTreeModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                                  const QModelIndex& parentIndex) {
    if (!canDropMimeData(data, action, row, column, parentIndex)) {
        return false;
    }
    return viewModel_.setParent(draggedEntity(data), entityId(parentIndex));
}
bool SceneTreeModel::isResetting() const {
    return resetting_;
}
} // namespace mini3d::editor
