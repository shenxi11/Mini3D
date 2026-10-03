/*
 * 模块名: QuickFavorites
 * 功能概述: 管理有序操作收藏与用户偏好，不保存显示名称和工程数据。
 * 对外接口: QuickFavorites
 * 依赖关系: Qt Core、OperatorRegistry
 * 输入输出: 已注册操作 ID 的添加/移除和 QSettings 往返。
 * 异常与错误: 未注册或重复 ID 不加入；读取偏好时保持有效 ID 的原顺序。
 * 维护说明: 不拥有 QAction，不执行操作，不影响唯一历史栈或项目脏状态。
 */
#pragma once

#include <QObject>
#include <QStringList>

class QSettings;

namespace mini3d::editor {
class OperatorRegistry;

/** @brief 用户偏好模型；收藏只记录稳定 ID，具体执行交给 Registry。 */
class QuickFavorites final : public QObject {
    Q_OBJECT
  public:
    explicit QuickFavorites(const OperatorRegistry& registry, QObject* parent = nullptr);
    [[nodiscard]] const QStringList& operatorIds() const;
    [[nodiscard]] bool contains(const QString& id) const;
    /** @brief 仅添加真实注册操作到末尾；变更发 favoritesChanged，不创建业务历史。 */
    bool addOperator(const QString& id);
    bool removeOperator(const QString& id);
    /** @brief 独立用户偏好读写；调用方决定持久化时机，不写项目文件。 */
    void restorePreferences(QSettings& settings);
    void savePreferences(QSettings& settings) const;

  signals:
    void favoritesChanged();

  private:
    const OperatorRegistry& registry_;
    QStringList ids_;
};
} // namespace mini3d::editor
