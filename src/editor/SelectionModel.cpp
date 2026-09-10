/*
 * 模块名: SelectionModel
 * 功能概述: 第三周场景选择与属性编辑的 SelectionModel 层。
 * 对外接口: SelectionModel
 * 依赖关系: Qt Widgets/Core、mini3d_core
 * 输入输出: 输入用户意图或场景通知，输出模型状态或界面刷新。
 * 异常与错误: 通过返回值和 operationFailed 报告非法编辑。
 * 维护说明: 同步 UI 线程操作；不持有节点地址或 GPU 资源。
 */
#include "SelectionModel.h"
namespace mini3d::editor {
SelectionModel::SelectionModel(const core::Scene& scene, QObject* parent)
    : QObject(parent), scene_(scene) {}
core::EntityId SelectionModel::selectedEntity() const {
    return selectedEntity_;
}
void SelectionModel::setSelectedEntity(core::EntityId id) {
    if (scene_.find(id) == nullptr) {
        id = core::kInvalidEntity;
    }
    if (selectedEntity_ == id) {
        return;
    }
    selectedEntity_ = id;
    emit selectedEntityChanged(id);
}
} // namespace mini3d::editor
