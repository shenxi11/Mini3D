/*
 * 模块名: SelectionModel
 * 功能概述: 第三周场景选择与属性编辑的 SelectionModel 层。
 * 对外接口: SelectionModel
 * 依赖关系: Qt Widgets/Core、mini3d_core
 * 输入输出: 输入用户意图或场景通知，输出模型状态或界面刷新。
 * 异常与错误: 通过返回值和 operationFailed 报告非法编辑。
 * 维护说明: 同步 UI 线程操作；不持有节点地址或 GPU 资源。
 */
#pragma once
#include "core/Scene.h"

#include <QObject>
#include <functional>
namespace mini3d::editor {
/** @brief 单选状态，只保存实体 ID；Scene 必须比本对象活得更久。 */
class SelectionModel final : public QObject {
    Q_OBJECT
  public:
    explicit SelectionModel(const core::Scene& scene, QObject* parent = nullptr);
    [[nodiscard]] core::EntityId selectedEntity() const;
    /** @brief 无效 ID 转为空选择；值未变化不发信号。 */
    void setSelectedEntity(core::EntityId id);
    /** @brief 只读准入回调先于值变化，供ViewModel冻结草稿选区；空回调允许选择。 */
    void setSelectionGuard(std::function<bool(core::EntityId)> guard);
  signals:
    void selectedEntityChanged(core::EntityId id);

  private:
    friend class SceneViewModel;
    void installSelectedEntity(core::EntityId id);
    const core::Scene& scene_;
    core::EntityId selectedEntity_{core::kInvalidEntity};
    std::function<bool(core::EntityId)> selectionGuard_;
};
} // namespace mini3d::editor
