/*
 * 模块名: CollectionPanel
 * 功能概述: 单层集合与成员列表，只改变组织关系，不替代对象父子树。
 * 对外接口: CollectionPanel；依赖关系: Qt Widgets、SceneViewModel。
 * 输入输出: 新建/名称/显隐/成员意图到唯一历史，成员点击到已有单对象选择。
 * 异常与错误: 失败后刷新实际状态，删除集合不删除对象。
 * 维护说明: 集合行不是EntityId，不引入批量对象变换或多对象Edit。
 */
#pragma once

#include <QWidget>
#include <cstdint>

class QTreeWidget;
class QPushButton;
namespace mini3d::editor {
class SceneViewModel;
/** @brief 集合组织视图；激活成员复用现有对象单选。 */
class CollectionPanel final : public QWidget {
    Q_OBJECT
  public:
    explicit CollectionPanel(SceneViewModel& model, QWidget* parent = nullptr);
    /** @brief 请求名称并新建集合，取消不改变场景。 */
    void createCollection();

  private:
    void refresh();
    void refreshActions();
    SceneViewModel& model_;
    QTreeWidget* tree_;
    QPushButton *remove_, *link_, *unlink_;
    std::uint64_t selected_ = 0;
};
} // namespace mini3d::editor
