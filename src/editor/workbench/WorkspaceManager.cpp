/*
 * 模块名: WorkspaceManager
 * 功能概述: 实现工作区状态快照、复原和独立偏好持久化。
 * 对外接口: WorkspaceManager
 * 依赖关系: Qt Widgets/Settings、WorkbenchShell
 * 输入输出: 工作区索引与用户偏好到既有控件动作和布局。
 * 异常与错误: 非法索引忽略，旧/损坏 Dock 字节由 QMainWindow 验证。
 * 维护说明: 不访问可变 Scene、不创建历史、不替换 QOpenGLWidget。
 */
#include "WorkspaceManager.h"

#include "WorkbenchShell.h"

#include <QAction>
#include <QMainWindow>
#include <QSettings>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>

namespace mini3d::editor {
namespace {
constexpr int kLayoutVersion = 2;
const std::array<QString, 3> kWorkspaceIds{QStringLiteral("layout"), QStringLiteral("modeling"),
                                           QStringLiteral("review")};
} // namespace

WorkspaceManager::WorkspaceManager(QMainWindow& window, WorkbenchShell& host, QObject* parent)
    : QObject(parent), window_(window), host_(host) {
    for (auto& state : layouts_) {
        state.docks = window_.saveState(kLayoutVersion);
    }
    layouts_[1].sidebar = true;
    layouts_[2].toolbar = false;
    layouts_[2].properties = 1;
    connect(host_.workspaceTabs(), &QTabBar::currentChanged, this, &WorkspaceManager::setWorkspace);
}

int WorkspaceManager::currentWorkspace() const {
    return current_;
}

void WorkspaceManager::captureCurrent() {
    emit layoutAboutToBeCaptured();
    auto& state = layouts_[current_];
    state.docks = window_.saveState(kLayoutVersion);
    state.toolbar = host_.isToolbarVisible();
    state.sidebar = host_.isSidebarVisible();
    state.properties =
        window_.findChild<QTabWidget*>(QStringLiteral("PropertyPages"))->currentIndex();
    state.objectExpanded =
        window_.findChild<QToolButton*>(QStringLiteral("ObjectPropertiesGroup"))->isChecked();
    state.dataExpanded =
        window_.findChild<QToolButton*>(QStringLiteral("DataPropertiesGroup"))->isChecked();
    state.tool.clear();
    for (const auto* name : {"MoveTool", "RotateTool", "ScaleTool"}) {
        if (window_.findChild<QAction*>(QString::fromLatin1(name))->isChecked()) {
            state.tool = QString::fromLatin1(name);
        }
    }
    state.local = window_.findChild<QAction*>(QStringLiteral("LocalTransformSpace"))->isChecked();
    state.snap = window_.findChild<QAction*>(QStringLiteral("SnapTransform"))->isChecked();
}

void WorkspaceManager::applyCurrent() {
    const auto& state = layouts_[current_];
    window_.restoreState(state.docks, kLayoutVersion);
    host_.setToolbarVisible(state.toolbar);
    host_.setSidebarVisible(state.sidebar);
    window_.findChild<QTabWidget*>(QStringLiteral("PropertyPages"))
        ->setCurrentIndex(state.properties);
    window_.findChild<QToolButton*>(QStringLiteral("ObjectPropertiesGroup"))
        ->setChecked(state.objectExpanded);
    window_.findChild<QToolButton*>(QStringLiteral("DataPropertiesGroup"))
        ->setChecked(state.dataExpanded);
    window_.findChild<QAction*>(QStringLiteral("SelectTool"))->trigger();
    if (!state.tool.isEmpty()) {
        window_.findChild<QAction*>(state.tool)->trigger();
    }
    window_
        .findChild<QAction*>(state.local ? QStringLiteral("LocalTransformSpace")
                                         : QStringLiteral("WorldTransformSpace"))
        ->trigger();
    window_.findChild<QAction*>(QStringLiteral("SnapTransform"))->setChecked(state.snap);
    const QSignalBlocker blocker(host_.workspaceTabs());
    host_.workspaceTabs()->setCurrentIndex(current_);
}

void WorkspaceManager::setWorkspace(int index) {
    if (index < 0 || index >= static_cast<int>(layouts_.size()) || index == current_) {
        return;
    }
    captureCurrent();
    current_ = index;
    applyCurrent();
    emit workspaceChanged(index);
}

void WorkspaceManager::restorePreferences(QSettings& settings) {
    emit layoutAboutToBeCaptured();
    settings.beginGroup(QStringLiteral("workbench/v2"));
    for (int i = 0; i < 3; ++i) {
        auto& state = layouts_[i];
        settings.beginGroup(kWorkspaceIds[i]);
        state.docks = settings.value(QStringLiteral("docks"), state.docks).toByteArray();
        state.toolbar = settings.value(QStringLiteral("toolbar"), state.toolbar).toBool();
        state.sidebar = settings.value(QStringLiteral("sidebar"), state.sidebar).toBool();
        const auto page = settings.value(QStringLiteral("properties"), state.properties).toInt();
        state.properties = page == 1 ? 1 : 0;
        state.objectExpanded = settings.value(QStringLiteral("objectExpanded"), true).toBool();
        state.dataExpanded = settings.value(QStringLiteral("dataExpanded"), true).toBool();
        const auto tool = settings.value(QStringLiteral("tool")).toString();
        state.tool =
            QStringList{"MoveTool", "RotateTool", "ScaleTool"}.contains(tool) ? tool : QString();
        state.local = settings.value(QStringLiteral("local"), false).toBool();
        state.snap = settings.value(QStringLiteral("snap"), false).toBool();
        settings.endGroup();
    }
    const int last = settings.value(QStringLiteral("active"), 0).toInt();
    current_ = last >= 0 && last < 3 ? last : 0;
    settings.endGroup();
    applyCurrent();
    emit workspaceChanged(current_);
}

void WorkspaceManager::savePreferences(QSettings& settings) {
    captureCurrent();
    settings.beginGroup(QStringLiteral("workbench/v2"));
    for (int i = 0; i < 3; ++i) {
        const auto& state = layouts_[i];
        settings.beginGroup(kWorkspaceIds[i]);
        settings.setValue(QStringLiteral("docks"), state.docks);
        settings.setValue(QStringLiteral("toolbar"), state.toolbar);
        settings.setValue(QStringLiteral("sidebar"), state.sidebar);
        settings.setValue(QStringLiteral("properties"), state.properties);
        settings.setValue(QStringLiteral("objectExpanded"), state.objectExpanded);
        settings.setValue(QStringLiteral("dataExpanded"), state.dataExpanded);
        settings.setValue(QStringLiteral("tool"), state.tool);
        settings.setValue(QStringLiteral("local"), state.local);
        settings.setValue(QStringLiteral("snap"), state.snap);
        settings.endGroup();
    }
    settings.setValue(QStringLiteral("active"), current_);
    settings.endGroup();
    settings.sync();
}
} // namespace mini3d::editor
