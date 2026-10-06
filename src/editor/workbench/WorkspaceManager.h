/*
 * 模块名: WorkspaceManager
 * 功能概述: 保存三种工作区的布局和工具偏好，不修改项目或重挂 GL 视口。
 * 对外接口: WorkspaceManager
 * 依赖关系: Qt Widgets/Settings、WorkbenchShell
 * 输入输出: 工作区切换到 Dock/属性/工具状态，偏好读写到独立用户设置。
 * 异常与错误: 不可识别的偏好索引回落到默认；损坏布局交给 Qt 拒绝。
 * 维护说明: 偏好不是文档状态，不进入撤销历史。
 */
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <array>

class QMainWindow;
class QSettings;

namespace mini3d::editor {
class WorkbenchShell;

/** @brief 布局、建模、检查工作区；保存和恢复不触发场景内容编辑。 */
class WorkspaceManager final : public QObject {
    Q_OBJECT
  public:
    WorkspaceManager(QMainWindow& window, WorkbenchShell& host, QObject* parent = nullptr);
    [[nodiscard]] int currentWorkspace() const;
    void setWorkspace(int index);
    /** @brief 使用调用者提供的设置存储，测试可用隔离 INI，不污染用户偏好。 */
    void restorePreferences(QSettings& settings);
    void savePreferences(QSettings& settings);
    /** @brief 新偏好首次实际布局后，仅刷新三个工作区的默认 Dock 快照。 */
    void initializeDefaultDockLayout();

  signals:
    /** @brief 捕获或应用工作区前，还原临时区域布局。 */
    void layoutAboutToBeCaptured();
    void workspaceChanged(int index);

  private:
    struct Layout {
        QByteArray docks;
        bool toolbar = true;
        bool sidebar = false;
        int properties = 0;
        bool objectExpanded = true;
        bool dataExpanded = true;
        QString tool;
        bool local = false;
        bool snap = false;
    };
    void captureCurrent();
    void applyCurrent();
    QMainWindow& window_;
    WorkbenchShell& host_;
    std::array<Layout, 3> layouts_;
    int current_ = 0;
};
} // namespace mini3d::editor
