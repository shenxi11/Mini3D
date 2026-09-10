/*
 * 模块名: SceneTreeModel
 * 功能概述: 第三周场景选择与属性编辑的 SceneTreeModel 层。
 * 对外接口: SceneTreeModel
 * 依赖关系: Qt Widgets/Core、mini3d_core
 * 输入输出: 输入用户意图或场景通知，输出模型状态或界面刷新。
 * 异常与错误: 通过返回值和 operationFailed 报告非法编辑。
 * 维护说明: 同步 UI 线程操作；不持有节点地址或 GPU 资源。
 */
#pragma once
#include "SceneViewModel.h"

#include <QAbstractItemModel>
namespace mini3d::editor {
/** @brief QTreeView 对 Scene 的映射，QModelIndex 仅存 EntityId。 */
class SceneTreeModel final : public QAbstractItemModel {
    Q_OBJECT
  public:
    explicit SceneTreeModel(SceneViewModel& viewModel, QObject* parent = nullptr);
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;
    /** @brief 在结构重置后用 ID 重新定位，不保存失效 QModelIndex。 */
    [[nodiscard]] QModelIndex indexForEntity(core::EntityId id) const;
    [[nodiscard]] core::EntityId entityId(const QModelIndex& index) const;
    [[nodiscard]] bool isResetting() const;

  private:
    [[nodiscard]] core::EntityId draggedEntity(const QMimeData* data) const;
    QByteArray dragToken_;
    SceneViewModel& viewModel_;
    bool resetting_ = false;
};
} // namespace mini3d::editor
