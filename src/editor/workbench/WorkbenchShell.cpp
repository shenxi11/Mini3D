/*
 * 模块名: WorkbenchShell
 * 功能概述: 以独立布局区域装配工作台，复用已有动作和只读 ViewModel。
 * 对外接口: WorkbenchShell 的布局及显隐接口
 * 依赖关系: Qt Widgets、SceneViewModel、ViewportWidget
 * 输入输出: 菜单动作和选择通知到可交互工具条、Header 与 N 侧栏。
 * 异常与错误: 不处理建模事务；业务失败保留原有消息通道。
 * 维护说明: 图标由本模块绘制，不引用 Blender 图像；不管理 GL 资源。
 */
#include "WorkbenchShell.h"

#include "CommitSpinBox.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <tuple>

namespace mini3d::editor {
namespace {
// 紧凑按钮保留原动作与实时快捷键，不改菜单中的完整名称及业务入口。
void labelActionButton(QToolButton* button, QAction* action, const QString& label) {
    action->setIconText(label);
    button->setAttribute(Qt::WA_AlwaysShowToolTips);
    const auto refresh = [button, action, label] {
        button->setText(label);
        auto hint = action->toolTip();
        if (!action->shortcuts().isEmpty())
            hint += QStringLiteral("\n快捷键：%1")
                        .arg(action->shortcut().toString(QKeySequence::NativeText));
        button->setToolTip(hint);
        button->setAccessibleName(action->text());
    };
    QObject::connect(action, &QAction::changed, button, refresh);
    refresh();
}

// 自制几何线条图标采用独立高分辨率画布，避免字符字体或第三方品牌图标依赖。
QIcon toolIcon(int tool) {
    QPixmap canvas(40, 40);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2, 2);
    painter.setPen(QPen(QColor(QStringLiteral("#e2e2e2")), 1.4));
    if (tool == 0) {
        painter.drawPolygon(QPolygonF{{4, 3}, {15, 10}, {10, 11}, {8, 16}});
    } else if (tool == 1) {
        painter.drawLine(3, 10, 17, 10);
        painter.drawLine(10, 3, 10, 17);
        painter.drawPolyline(QPolygonF{{7, 6}, {10, 3}, {13, 6}});
        painter.drawPolyline(QPolygonF{{14, 7}, {17, 10}, {14, 13}});
    } else if (tool == 2) {
        painter.drawArc(QRectF(4, 4, 12, 12), 30 * 16, 280 * 16);
        painter.drawPolyline(QPolygonF{{12, 2}, {16, 5}, {12, 6}});
    } else if (tool == 3) {
        painter.drawRect(QRectF(3, 12, 5, 5));
        painter.drawLine(8, 12, 16, 4);
        painter.drawPolyline(QPolygonF{{11, 4}, {16, 4}, {16, 9}});
    } else if (tool == 5) {
        painter.setPen(QPen(QColor(QStringLiteral("#e2e2e2")), 1.4, Qt::DashLine));
        painter.drawRect(QRectF(4, 4, 12, 12));
    } else if (tool == 6) {
        painter.drawRect(QRectF(4, 11, 12, 6));
        painter.drawLine(10, 11, 10, 3);
        painter.drawPolyline(QPolygonF{{7, 6}, {10, 3}, {13, 6}});
    } else if (tool == 7) {
        painter.drawRect(QRectF(3, 3, 14, 14));
        painter.drawRect(QRectF(7, 7, 6, 6));
    } else if (tool == 8) {
        painter.drawPolygon(QPolygonF{{3, 3}, {13, 3}, {17, 7}, {17, 17}, {3, 17}});
        painter.drawLine(10, 3, 17, 10);
    } else if (tool == 9) {
        painter.drawRect(QRectF(3, 3, 14, 14));
        painter.drawLine(10, 3, 10, 17);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#e2e2e2")));
        for (double x : {4., 10., 16.})
            painter.drawEllipse(QPointF(x, 10), 1.2, 1.2);
    }
    painter.end();
    canvas.setDevicePixelRatio(2);
    return QIcon(canvas);
}

// 当前 Qt 工作台的原生扩展按钮继续承接溢出动作，补足深色背景上的图标对比。
void markToolbarOverflow(QToolBar* toolbar) {
    auto* button = toolbar->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
    button->setIcon(toolIcon(4));
    button->setToolTip(QStringLiteral("更多工具"));
    button->setAccessibleName(QStringLiteral("更多工具"));
}
} // namespace

WorkbenchShell::WorkbenchShell(renderer_gl::ViewportWidget* viewport, SceneViewModel& model,
                               QWidget* parent)
    : QWidget(parent), model_(model), viewport_(viewport) {
    setObjectName(QStringLiteral("WorkbenchShell"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* workspaceBar = new QWidget(this);
    workspaceBar->setObjectName(QStringLiteral("WorkspaceBar"));
    workspaceTabs_ = new QTabBar(workspaceBar);
    workspaceTabs_->setObjectName(QStringLiteral("WorkspaceTabs"));
    workspaceTabs_->setExpanding(false);
    workspaceTabs_->addTab(QStringLiteral("布局"));
    workspaceTabs_->addTab(QStringLiteral("建模"));
    workspaceTabs_->addTab(QStringLiteral("检查"));
    auto* workspaceRow = new QHBoxLayout(workspaceBar);
    workspaceRow->setContentsMargins(4, 0, 4, 0);
    workspaceRow->setSpacing(4);
    workspaceRow->addWidget(workspaceTabs_);
    quickActions_ = new QToolBar(workspaceBar);
    quickActions_->setObjectName(QStringLiteral("QuickActionBar"));
    quickActions_->setMovable(false);
    quickActions_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    workspaceRow->addWidget(quickActions_);
    header_ = new QWidget(this);
    header_->setObjectName(QStringLiteral("ViewportHeader"));
    auto* headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(4, 1, 4, 1);
    headerLayout->setSpacing(4);
    modeLabel_ = new QLabel(QStringLiteral("对象模式"), header_);
    modeLabel_->setObjectName(QStringLiteral("ModeLabel"));
    headerLayout->addStretch();
    auto* convention = new QLabel(QStringLiteral("Y↑项目"), header_);
    convention->setToolTip(QStringLiteral("右手坐标系，Y 轴向上，地面为 XZ 平面。"));
    headerLayout->addWidget(convention);
    layout->addWidget(header_);

    transformSettings_ = new QToolBar(this);
    transformSettings_->setObjectName(QStringLiteral("TransformSettingsBar"));
    transformSettings_->setMovable(false);
    transformSettings_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    transformSettings_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    toolLabel_ = new QLabel(this);
    toolLabel_->setObjectName(QStringLiteral("ToolSettings"));
    toolLabel_->setMargin(5);
    toolLabel_->setMinimumWidth(100);
    toolLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    settingsRow_ = new QHBoxLayout;
    settingsRow_->setContentsMargins(0, 0, 0, 0);
    settingsRow_->setSpacing(1);
    settingsRow_->addWidget(transformSettings_);
    settingsRow_->addWidget(toolLabel_, 1);
    layout->addLayout(settingsRow_);
    compactSettings_ = new QToolButton(header_);
    compactSettings_->setObjectName(QStringLiteral("CompactTransformSettingsButton"));
    compactSettings_->setText(QStringLiteral("变换设置"));
    compactSettings_->setToolTip(
        QStringLiteral("方向空间、吸附、枢轴和比例编辑；可展开完整变换设置栏。"));
    compactSettings_->setPopupMode(QToolButton::InstantPopup);
    compactSettings_->setMenu(new QMenu(compactSettings_));
    headerLayout->insertWidget(headerLayout->count() - 1, compactSettings_);
    compactSettings_->hide();

    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(1);
    toolbar_ = new QToolBar(QStringLiteral("视口工具条"), viewport_);
    toolbar_->setObjectName(QStringLiteral("ViewportToolbar"));
    toolbar_->setOrientation(Qt::Vertical);
    toolbar_->setMovable(false);
    toolbar_->setFloatable(false);
    toolbar_->setIconSize(QSize(20, 20));
    toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar_->setFixedWidth(38);
    toolbar_->setAttribute(Qt::WA_NoMousePropagation);
    toolbar_->setFocusPolicy(Qt::NoFocus);
    toolbar_->installEventFilter(this);
    viewport_->installEventFilter(this);
    for (auto* bar : {quickActions_, transformSettings_, toolbar_})
        markToolbarOverflow(bar);
    auto* viewportSplit = new QSplitter(Qt::Horizontal, body);
    viewportSplit->setObjectName(QStringLiteral("ViewportSidebarSplitter"));
    viewportSplit->setChildrenCollapsible(false);
    viewportSplit->addWidget(viewport_);
    bodyLayout->addWidget(viewportSplit, 1);

    auto* sidebarScroll = new QScrollArea(viewportSplit);
    sidebarScroll->setWidgetResizable(true);
    sidebarScroll->setFrameShape(QFrame::NoFrame);
    sidebarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sidebar_ = sidebarScroll;
    sidebar_->setObjectName(QStringLiteral("ViewportSidebar"));
    sidebar_->setMinimumWidth(180);
    auto* sidebarContent = new QWidget(sidebar_);
    sidebarScroll->setWidget(sidebarContent);
    auto* sidebarLayout = new QVBoxLayout(sidebarContent);
    sidebarLayout->setContentsMargins(12, 10, 12, 10);
    sidebarLayout->addWidget(new QLabel(QStringLiteral("项目 · 当前选择"), sidebar_));
    selectionLabel_ = new QLabel(sidebar_);
    selectionLabel_->setObjectName(QStringLiteral("SidebarSelection"));
    selectionLabel_->setWordWrap(true);
    sidebarLayout->addWidget(selectionLabel_);
    transformLabel_ = new QLabel(sidebar_);
    transformLabel_->setObjectName(QStringLiteral("SidebarTransform"));
    transformLabel_->setWordWrap(true);
    transformLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sidebarLayout->addWidget(transformLabel_);
    selectionHint_ = new QLabel(sidebar_);
    selectionHint_->setWordWrap(true);
    sidebarLayout->addWidget(selectionHint_);
    cursorPanel_ = new QWidget(sidebarContent);
    auto* cursorLayout = new QVBoxLayout(cursorPanel_);
    cursorLayout->setContentsMargins(0, 8, 0, 0);
    cursorLayout->addWidget(new QLabel(QStringLiteral("3D 游标 · 世界坐标"), cursorPanel_));
    auto* fields = new QFormLayout;
    for (int axis = 0; axis < 3; ++axis) {
        auto* field = new CommitSpinBox(cursorPanel_);
        cursorFields_[axis] = field;
        const auto name = QString(QChar('X' + axis));
        field->setObjectName(QStringLiteral("CursorPosition") + name);
        field->setAccessibleName(QStringLiteral("3D 游标世界坐标 ") + name);
        field->setRange(-std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        field->setDecimals(4);
        field->setSingleStep(.1);
        field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        fields->addRow(name, field);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, axis, field](double value) {
            auto position = model_.cursor3D().position;
            position[axis] = static_cast<float>(value);
            if (!model_.setCursorPosition(position)) {
                field->rejectSubmission(QStringLiteral("请返回编辑视图并输入有限坐标。"));
                const QSignalBlocker blocker(field);
                field->setValue(model_.cursor3D().position[axis]);
            }
        });
    }
    cursorLayout->addLayout(fields);
    auto* cursorHint = new QLabel(
        QStringLiteral("仅辅助定位，不标记未保存；请主动保存以保留游标。"), cursorPanel_);
    cursorHint->setWordWrap(true);
    cursorLayout->addWidget(cursorHint);
    sidebarLayout->addWidget(cursorPanel_);
    sidebarLayout->addStretch();
    viewportSplit->addWidget(sidebar_);
    viewportSplit->setStretchFactor(0, 1);
    viewportSplit->setStretchFactor(1, 0);
    viewportSplit->setSizes({1000, 212});
    sidebar_->hide();
    layout->addWidget(body, 1);
    contextRow_ = new QHBoxLayout;
    contextRow_->setContentsMargins(4, 1, 4, 1);
    contextRow_->addWidget(modeLabel_);
    selectionSummary_ = new QLabel(this);
    selectionSummary_->setObjectName(QStringLiteral("SelectionSummary"));
    selectionSummary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contextRow_->addWidget(selectionSummary_, 1);
    layout->addLayout(contextRow_);

    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this,
            &WorkbenchShell::refreshSelection);
    connect(&model_, &SceneViewModel::sceneChanged, this, &WorkbenchShell::refreshSelection);
    connect(&model_, &SceneViewModel::componentSelectionChanged, this,
            &WorkbenchShell::refreshSelection);
    connect(&model_, &SceneViewModel::componentPreviewChanged, this,
            &WorkbenchShell::refreshSelection);
    refreshSelection();
    connect(&model_, &SceneViewModel::cursorChanged, this, &WorkbenchShell::refreshCursor);
    connect(&model_, &SceneViewModel::documentReset, this, &WorkbenchShell::refreshCursor);
    connect(&model_, &SceneViewModel::previewCameraChanged, this, &WorkbenchShell::refreshCursor);
    refreshCursor();
    setTransformSettingsVisible(false);
}

void WorkbenchShell::bindActions(QMainWindow& window, KeymapRouter& router) {
    auto* select = new QAction(toolIcon(0), QStringLiteral("选择工具"), this);
    select->setObjectName(QStringLiteral("SelectTool"));
    select->setCheckable(true);
    const auto refreshToolSettings = [this, &router, select] {
        const auto tool = viewport_->transformTool();
        const auto text = model_.isEditMode()                      ? QStringLiteral("组件选择")
                          : tool == renderer_gl::GizmoTool::Move   ? QStringLiteral("移动")
                          : tool == renderer_gl::GizmoTool::Rotate ? QStringLiteral("旋转")
                          : tool == renderer_gl::GizmoTool::Scale  ? QStringLiteral("缩放")
                                                                   : QStringLiteral("选择");
        toolLabel_->setText(QStringLiteral("当前工具：%1　|　键位：%2")
                                .arg(text, router.keymap() == EditorKeymap::Legacy
                                               ? QStringLiteral("Legacy Mini3D")
                                               : QStringLiteral("Blender 风格")));
        toolLabel_->setToolTip(toolLabel_->text() +
                               QStringLiteral("\nTab 切换模式；Esc 取消；F1 打开使用手册。"));
        const QSignalBlocker blocker(select);
        select->setChecked(tool == renderer_gl::GizmoTool::None);
    };
    connect(&router, &KeymapRouter::keymapChanged, this, refreshToolSettings);
    connect(&model_, &SceneViewModel::editModeChanged, this, refreshToolSettings);
    refreshToolSettings();
    toolbar_->addAction(select);
    labelActionButton(qobject_cast<QToolButton*>(toolbar_->widgetForAction(select)), select,
                      QStringLiteral("选择"));
    connect(select, &QAction::triggered, viewport_, [this, &window] {
        for (const auto* name : {"MoveTool", "RotateTool", "ScaleTool"}) {
            window.findChild<QAction*>(QString::fromLatin1(name))->setChecked(false);
        }
        viewport_->setTransformTool(renderer_gl::GizmoTool::None);
    });
    connect(select, &QAction::triggered, this, refreshToolSettings);
    int icon = 1;
    for (const auto* name : {"MoveTool", "RotateTool", "ScaleTool"}) {
        auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
        action->setIcon(toolIcon(icon++));
        toolbar_->addAction(action);
        labelActionButton(qobject_cast<QToolButton*>(toolbar_->widgetForAction(action)), action,
                          action->text().left(2));
        connect(action, &QAction::toggled, this, refreshToolSettings);
    }
    auto* headerLayout = qobject_cast<QHBoxLayout*>(header_->layout());
    for (const auto& group : {QStringLiteral("视图"), QStringLiteral("坐标系")}) {
        auto* button = new QToolButton(header_);
        button->setText(group);
        button->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(button);
        const QStringList names = group == QStringLiteral("视图")
                                      ? QStringList{"FocusSelection",   "FocusAll",
                                                    "FrontView",        "RightView",
                                                    "TopView",          "OrbitView",
                                                    "OrthographicView", "ToggleCameraPreview"}
                                      : QStringList{"WorldTransformSpace", "LocalTransformSpace"};
        for (const auto& name : names) {
            menu->addAction(window.findChild<QAction*>(name));
        }
        button->setMenu(menu);
        if (group == QStringLiteral("坐标系")) {
            button->setObjectName(QStringLiteral("TransformSpaceButton"));
            const auto refresh = [this, button] {
                button->setText(viewport_->transformSpace() == renderer_gl::GizmoSpace::Local
                                    ? QStringLiteral("局部")
                                    : QStringLiteral("全局"));
                button->setToolTip(QStringLiteral("方向空间：Global / Local，与枢轴独立。"));
            };
            for (auto* action : menu->actions())
                connect(action, &QAction::triggered, this, refresh);
            refresh();
        }
        if (group == QStringLiteral("坐标系"))
            transformSettings_->addWidget(button);
        else
            headerLayout->insertWidget(0, button);
    }
    auto* shading = new QToolButton(header_);
    shading->setObjectName(QStringLiteral("ShadingModeButton"));
    shading->setPopupMode(QToolButton::InstantPopup);
    auto* shadingMenu = new QMenu(shading);
    for (const auto* name : {"ShadingMaterial", "ShadingSolid", "ShadingWireframe", "ShadingPie"})
        shadingMenu->addAction(window.findChild<QAction*>(QString::fromLatin1(name)));
    shading->setMenu(shadingMenu);
    const auto refreshShading = [this, shading] {
        const auto mode = viewport_->shadingMode();
        shading->setText(mode == renderer_gl::ViewportShading::Material ? QStringLiteral("材质")
                         : mode == renderer_gl::ViewportShading::Solid  ? QStringLiteral("实体")
                                                                        : QStringLiteral("线框"));
        shading->setToolTip(QStringLiteral("视口着色 · Z饼菜单；线框含三角化边，不改变源拓扑。"));
    };
    connect(viewport_, &renderer_gl::ViewportWidget::shadingModeChanged, this, refreshShading);
    refreshShading();
    headerLayout->insertWidget(0, shading);
    auto* snap = new QToolButton(header_);
    auto* snapAction = window.findChild<QAction*>(QStringLiteral("SnapTransform"));
    snap->setObjectName(QStringLiteral("SnapButton"));
    snap->setText(QStringLiteral("吸附·步进"));
    snap->setToolTip(snapAction->toolTip());
    snap->setCheckable(true);
    snap->setChecked(snapAction->isChecked());
    connect(snap, &QToolButton::clicked, snapAction, &QAction::trigger);
    connect(snapAction, &QAction::toggled, snap, &QToolButton::setChecked);
    auto* snapMenu = new QMenu(snap);
    auto* snapGroup = new QActionGroup(snapMenu);
    snap->setMenu(snapMenu);
    snap->setPopupMode(QToolButton::MenuButtonPopup);
    auto* viewMenu = qobject_cast<QMenu*>(snapAction->parent());
    viewMenu->addMenu(snapMenu)->setText(QStringLiteral("吸附类型（G 移动）"));
    for (const auto& [name, label, mode] :
         {std::tuple{"SnapIncrement", "步进", SnapMode::Increment},
          std::tuple{"SnapVertex", "顶点", SnapMode::Vertex}}) {
        auto* action = snapMenu->addAction(QString::fromUtf8(label));
        action->setObjectName(QString::fromLatin1(name));
        action->setCheckable(true);
        snapGroup->addAction(action);
        connect(action, &QAction::triggered, &model_, [this, mode] {
            model_.setSnapMode(mode);
        });
        const auto refresh = [this, action, mode] {
            action->setChecked(model_.snapMode() == mode);
        };
        connect(&model_, &SceneViewModel::snapModeChanged, action, refresh);
        refresh();
    }
    connect(&model_, &SceneViewModel::snapModeChanged, snap, [this, snap] {
        snap->setText(model_.snapMode() == SnapMode::Vertex ? QStringLiteral("吸附·顶点")
                                                            : QStringLiteral("吸附·步进"));
    });
    transformSettings_->addWidget(snap);
    bindEditActions(window);
    bindCursorActions(window);
    bindPivotActions(window);
    auto* proportional = window.findChild<QAction*>(QStringLiteral("ToggleProportionalEditing"));
    transformSettings_->addAction(proportional);
    labelActionButton(qobject_cast<QToolButton*>(transformSettings_->widgetForAction(proportional)),
                      proportional, QStringLiteral("比例编辑"));
    auto* settingsMenu = compactSettings_->menu();
    for (const auto* name : {"WorldTransformSpace", "LocalTransformSpace", "SnapTransform"})
        settingsMenu->addAction(window.findChild<QAction*>(QString::fromLatin1(name)));
    settingsMenu->addMenu(snap->menu());
    settingsMenu->addMenu(findChild<QToolButton*>(QStringLiteral("TransformPivotButton"))->menu());
    settingsMenu->addAction(proportional);
    settingsMenu->addSeparator();
    settingsMenu->addAction(window.findChild<QAction*>(QStringLiteral("ToggleTransformSettingsBar")));
}

void WorkbenchShell::bindQuickActions(QMainWindow& window) {
    auto* panels = new QToolButton(header_);
    panels->setObjectName(QStringLiteral("ViewportPanelsButton"));
    panels->setText(QStringLiteral("面板"));
    panels->setToolTip(QStringLiteral("展开场景、属性、控制台、工具条和侧栏，或应用参考布局。"));
    panels->setPopupMode(QToolButton::InstantPopup);
    auto* panelMenu = new QMenu(panels);
    panelMenu->setObjectName(QStringLiteral("ViewportPanelsMenu"));
    for (const auto* name : {"ToggleSceneDock", "ToggleInspectorDock", "ToggleConsoleDock",
                             "ToggleViewportToolbar", "ToggleViewportSidebar",
                             "ToggleTransformSettingsBar"})
        panelMenu->addAction(window.findChild<QAction*>(QString::fromLatin1(name)));
    panelMenu->addSeparator();
    panelMenu->addAction(window.findChild<QAction*>(QStringLiteral("RestoreDefaultViewportLayout")));
    panels->setMenu(panelMenu);
    auto* headerLayout = qobject_cast<QHBoxLayout*>(header_->layout());
    headerLayout->insertWidget(headerLayout->count() - 1, panels);
    for (const auto& [name, label] : {std::pair{"SearchOperators", "搜索 F3"},
                                      {"QuickFavorites", "收藏"},
                                      {"Undo", "撤销"},
                                      {"Redo", "重做"},
                                      {"RepeatLastOperation", "重复"},
                                      {"AdjustLastOperation", "调整"},
                                      {"OpenUserGuide", "帮助 F1"}}) {
        auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
        quickActions_->addAction(action);
        labelActionButton(qobject_cast<QToolButton*>(quickActions_->widgetForAction(action)),
                          action, QString::fromUtf8(label));
    }
    toolbar_->addSeparator();
    auto* create = new QToolButton(toolbar_);
    create->setObjectName(QStringLiteral("CreatePrimitiveButton"));
    create->setText(QStringLiteral("＋"));
    create->setAccessibleName(QStringLiteral("创建基础体"));
    create->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(create);
    for (const auto* name : {"CreateCube", "CreateSphere", "CreatePlane", "CreateEmpty"})
        menu->addAction(window.findChild<QAction*>(QString::fromLatin1(name)));
    create->setMenu(menu);
    create->setToolTip(QStringLiteral("在 3D 游标处创建基础体；编辑模式请先返回对象模式。"));
    toolbar_->addWidget(create);
    int icon = 5;
    for (const auto& [name, label] : {std::pair{"BoxSelectComponents", "框选"},
                                      {"ExtrudeRegion", "挤出"},
                                      {"InsetFace", "内插"},
                                      {"BevelEdge", "倒角"},
                                      {"LoopCut", "环切"}}) {
        auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
        action->setIcon(toolIcon(icon++));
        toolbar_->addAction(action);
        auto* button = qobject_cast<QToolButton*>(toolbar_->widgetForAction(action));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        labelActionButton(button, action, QString::fromUtf8(label));
    }
    for (auto* button : toolbar_->findChildren<QToolButton*>())
        button->setFocusPolicy(Qt::NoFocus);
    updateToolbarGeometry();
}

void WorkbenchShell::bindPivotActions(QMainWindow& window) {
    auto* editMenu =
        qobject_cast<QMenu*>(window.findChild<QAction*>(QStringLiteral("Undo"))->parent());
    auto* menu = editMenu->addMenu(QStringLiteral("变换枢轴"));
    auto* group = new QActionGroup(this);
    auto* button = new QToolButton(header_);
    button->setObjectName(QStringLiteral("TransformPivotButton"));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu(menu);
    transformSettings_->addWidget(button);
    for (const auto& [name, label, pivot] :
         {std::tuple{"PivotMedian", "选择质心", TransformPivot::Median},
          std::tuple{"PivotActive", "活动元素", TransformPivot::Active},
          std::tuple{"PivotCursor", "3D 游标", TransformPivot::Cursor}}) {
        auto* action = menu->addAction(QString::fromUtf8(label));
        action->setObjectName(QString::fromLatin1(name));
        action->setCheckable(true);
        group->addAction(action);
        connect(action, &QAction::triggered, &model_, [this, pivot] {
            model_.setTransformPivot(pivot);
        });
        const auto refresh = [this, action, pivot] {
            action->setChecked(model_.transformPivot() == pivot);
        };
        connect(&model_, &SceneViewModel::transformPivotChanged, action, refresh);
        refresh();
    }
    const auto refresh = [this, button] {
        button->setText(QStringLiteral("枢轴·%1").arg(model_.transformPivotName()));
        button->setToolTip(
            QStringLiteral("旋转/缩放中心：%1；与方向空间独立。对象模式的质心/活动项均为对象原点。")
                .arg(model_.transformPivotName()));
        viewport_->setTransformPivot(model_.transformPivot() == TransformPivot::Cursor
                                         ? std::optional(model_.cursor3D().position)
                                         : std::nullopt);
    };
    connect(&model_, &SceneViewModel::transformPivotChanged, this, refresh);
    connect(&model_, &SceneViewModel::cursorChanged, this, refresh);
    refresh();
}

void WorkbenchShell::refreshCursor() {
    for (int axis = 0; axis < 3; ++axis) {
        const QSignalBlocker blocker(cursorFields_[axis]);
        cursorFields_[axis]->setValue(model_.cursor3D().position[axis]);
        cursorFields_[axis]->resetInput();
        cursorFields_[axis]->setEnabled(model_.previewCamera() == 0);
    }
    viewport_->setCursor3D(model_.cursor3D());
}

void WorkbenchShell::bindCursorActions(QMainWindow& window) {
    auto* editMenu =
        qobject_cast<QMenu*>(window.findChild<QAction*>(QStringLiteral("Undo"))->parent());
    auto* menu = editMenu->addMenu(QStringLiteral("3D 游标"));
    auto* place = menu->addAction(QStringLiteral("点击放置 3D 游标"));
    place->setObjectName(QStringLiteral("PlaceCursor"));
    place->setCheckable(true);
    place->setToolTip(QStringLiteral(
        "下一次左键放置；Esc / 右键取消。Blender 键位可直接 Shift＋右键；空白处使用 XZ 地面。"));
    connect(viewport_, &renderer_gl::ViewportWidget::cursorPlacementChanged, place,
            &QAction::setChecked);
    auto* origin = menu->addAction(QStringLiteral("游标到世界原点"));
    origin->setObjectName(QStringLiteral("CursorToOrigin"));
    connect(origin, &QAction::triggered, &model_, [this] {
        model_.setCursorPosition({0, 0, 0});
    });
    auto* selected = menu->addAction(QStringLiteral("游标到选择"));
    selected->setObjectName(QStringLiteral("CursorToSelection"));
    selected->setToolTip(
        QStringLiteral("对象模式取所选对象世界原点；编辑模式取去重所选顶点的世界质心。"));
    connect(selected, &QAction::triggered, &model_, &SceneViewModel::moveCursorToSelection);
    auto* toCursor = menu->addAction(QStringLiteral("选择到游标（保持偏移）"));
    toCursor->setObjectName(QStringLiteral("SelectionToCursor"));
    toCursor->setToolTip(
        QStringLiteral("将对象原点或选区质心移到游标；保持选区内部形状，一次可撤销操作。"));
    connect(toCursor, &QAction::triggered, &model_, &SceneViewModel::moveSelectionToCursor);
    auto* visible = menu->addAction(QStringLiteral("显示 3D 游标"));
    visible->setObjectName(QStringLiteral("ToggleCursorVisibility"));
    visible->setCheckable(true);
    visible->setChecked(model_.cursor3D().visible);
    connect(visible, &QAction::toggled, &model_, &SceneViewModel::setCursorVisible);
    connect(&model_, &SceneViewModel::cursorChanged, visible, [this, visible] {
        const QSignalBlocker blocker(visible);
        visible->setChecked(model_.cursor3D().visible);
    });
    auto* layout = qobject_cast<QVBoxLayout*>(cursorPanel_->layout());
    for (auto* action : {place, selected, toCursor, origin, visible}) {
        auto* button = new QToolButton(cursorPanel_);
        button->setDefaultAction(action);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        layout->insertWidget(layout->count() - 1, button);
    }
    const auto refresh = [this, place, origin, selected, toCursor] {
        const bool available = model_.previewCamera() == 0;
        place->setEnabled(available);
        origin->setEnabled(available);
        selected->setEnabled(available && model_.cursorSelectionCenter().has_value());
        toCursor->setEnabled(model_.selectionToCursorDisabledReason().isEmpty());
    };
    connect(&model_, &SceneViewModel::previewCameraChanged, this, refresh);
    connect(&model_, &SceneViewModel::componentSelectionChanged, this, refresh);
    connect(&model_, &SceneViewModel::sceneChanged, this, refresh);
    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this, refresh);
    refresh();
}

void WorkbenchShell::bindEditActions(QMainWindow& window) {
    auto* editMenu =
        qobject_cast<QMenu*>(window.findChild<QAction*>(QStringLiteral("Undo"))->parent());
    auto* modeMenu = editMenu->addMenu(QStringLiteral("模式与组件选择"));
    auto* toggle = modeMenu->addAction(QStringLiteral("进入编辑模式"));
    toggle->setObjectName(QStringLiteral("ToggleEditMode"));
    connect(toggle, &QAction::triggered, &model_, [this] {
        model_.setEditMode(!model_.isEditMode());
    });
    auto* modeButton = new QToolButton(header_);
    modeButton->setObjectName(QStringLiteral("EditModeButton"));
    modeButton->setText(QStringLiteral("对象模式"));
    modeButton->setPopupMode(QToolButton::MenuButtonPopup);
    modeButton->setMenu(modeMenu);
    connect(modeButton, &QToolButton::clicked, toggle, &QAction::trigger);
    auto* headerLayout = qobject_cast<QHBoxLayout*>(header_->layout());
    headerLayout->insertWidget(0, modeButton);
    auto* domains = new QActionGroup(this);
    for (const auto& [name, text, domain] :
         {std::tuple{"SelectVertices", "点", SelectionDomain::Vertex},
          std::tuple{"SelectEdges", "边", SelectionDomain::Edge},
          std::tuple{"SelectFaces", "面", SelectionDomain::Face}}) {
        auto* action = modeMenu->addAction(QString::fromUtf8(text));
        action->setObjectName(QString::fromLatin1(name));
        action->setCheckable(true);
        domains->addAction(action);
        connect(action, &QAction::triggered, &model_, [this, domain] {
            model_.setSelectionDomain(domain);
        });
        const auto refresh = [this, action, domain] {
            action->setEnabled(model_.isEditMode());
            action->setChecked(model_.isEditMode() &&
                               model_.componentSelection().domain() == domain);
        };
        connect(&model_, &SceneViewModel::componentSelectionChanged, action, refresh);
        refresh();
        auto* button = new QToolButton(header_);
        button->setDefaultAction(action);
        action->setToolTip(QStringLiteral("编辑模式：%1选择").arg(QString::fromUtf8(text)));
        labelActionButton(button, action, QString::fromUtf8(text));
        headerLayout->insertWidget(headerLayout->count() - 2, button);
    }
    auto* all = modeMenu->addAction(QStringLiteral("全选组件"));
    all->setObjectName(QStringLiteral("SelectAllComponents"));
    auto* clear = modeMenu->addAction(QStringLiteral("取消组件选择"));
    clear->setObjectName(QStringLiteral("ClearComponentSelection"));
    connect(all, &QAction::triggered, &model_, &SceneViewModel::selectAllComponents);
    connect(clear, &QAction::triggered, &model_, &SceneViewModel::clearComponentSelection);
    auto* box = modeMenu->addAction(QStringLiteral("框选组件…"));
    box->setObjectName(QStringLiteral("BoxSelectComponents"));
    box->setToolTip(QStringLiteral("B 后左键拖动：替换；Shift 追加，Ctrl 移除；Esc / 右键取消。"));
    auto* xRay = modeMenu->addAction(QStringLiteral("穿透选择（X-Ray）"));
    xRay->setObjectName(QStringLiteral("ToggleXRay"));
    xRay->setCheckable(true);
    xRay->setChecked(viewport_->isXRayEnabled());
    xRay->setToolTip(
        QStringLiteral("显示并选择被几何遮挡的源组件；不选择隐藏对象，不改变材质透明度。"));
    connect(xRay, &QAction::toggled, viewport_, &renderer_gl::ViewportWidget::setXRayEnabled);
    connect(viewport_, &renderer_gl::ViewportWidget::xRayChanged, xRay, &QAction::setChecked);
    auto* overlay = modeMenu->addAction(QStringLiteral("显示覆盖层"));
    overlay->setObjectName(QStringLiteral("ToggleOverlays"));
    overlay->setCheckable(true);
    overlay->setChecked(viewport_->isOverlayVisible());
    overlay->setToolTip(
        QStringLiteral("显示网格地面、选区和手柄；关闭不改变选区、穿透状态或保存内容。"));
    connect(overlay, &QAction::toggled, viewport_, &renderer_gl::ViewportWidget::setOverlayVisible);
    connect(viewport_, &renderer_gl::ViewportWidget::overlayVisibilityChanged, overlay,
            &QAction::setChecked);
    for (const auto& [action, label] : {std::pair{xRay, "穿透"}, std::pair{overlay, "覆盖层"}}) {
        auto* button = new QToolButton(header_);
        button->setDefaultAction(action);
        labelActionButton(button, action, QString::fromUtf8(label));
        headerLayout->insertWidget(headerLayout->count() - 2, button);
    }
    const auto refresh = [this, &window, modeButton, toggle, all, clear, box, xRay] {
        const bool edit = model_.isEditMode();
        modeButton->setText(edit ? QStringLiteral("编辑模式") : QStringLiteral("对象模式"));
        modeButton->setToolTip(QStringLiteral("Tab 切换对象 / 编辑模式；%1")
                                   .arg(model_.editModeDisabledReason().isEmpty()
                                            ? QStringLiteral("仅支持当前选中的可编辑网格。")
                                            : model_.editModeDisabledReason()));
        toggle->setText(edit ? QStringLiteral("返回对象模式") : QStringLiteral("进入编辑模式"));
        const auto reason = model_.editModeDisabledReason();
        toggle->setEnabled(reason.isEmpty());
        modeButton->setEnabled(toggle->isEnabled());
        toggle->setToolTip(reason.isEmpty() ? QStringLiteral("切换前取消未确认的变换预览。")
                                            : reason);
        all->setEnabled(edit);
        clear->setEnabled(edit);
        box->setEnabled(edit);
        xRay->setEnabled(edit);
        for (const auto* name :
             {"CreateCube", "CreateSphere", "CreatePlane", "CreateEmpty", "CreateCamera",
              "CreateDirectionalLight", "MoveTool", "RotateTool", "ScaleTool", "TransformMove",
              "TransformRotate", "TransformScale"}) {
            window.findChild<QAction*>(QString::fromLatin1(name))->setEnabled(!edit);
        }
        for (const auto* name : {"Duplicate", "Delete"}) {
            window.findChild<QAction*>(QString::fromLatin1(name))
                ->setEnabled(!edit && model_.selection()->selectedEntity() != core::kInvalidEntity);
        }
    };
    connect(&model_, &SceneViewModel::editModeChanged, this, refresh);
    connect(&model_, &SceneViewModel::sceneChanged, this, refresh);
    connect(&model_, &SceneViewModel::previewCameraChanged, this, refresh);
    connect(model_.selection(), &SelectionModel::selectedEntityChanged, this, refresh);
    refresh();
}

void WorkbenchShell::setToolbarVisible(bool visible) {
    if (isToolbarVisible() != visible) {
        toolbar_->setVisible(visible);
        if (visible)
            updateToolbarGeometry();
        emit toolbarVisibilityChanged(visible);
    }
}

void WorkbenchShell::setSidebarVisible(bool visible) {
    // 窄窗口优先保留视口和属性数值行；用户可在扩大窗口后重新打开侧栏。
    const bool allowed = visible && width() >= 580;
    sidebar_->setVisible(allowed);
    emit sidebarVisibilityChanged(allowed);
}

void WorkbenchShell::setTransformSettingsVisible(bool visible) {
    if (isTransformSettingsVisible() != visible) {
        transformSettings_->setVisible(visible);
        (visible ? contextRow_ : settingsRow_)->removeWidget(toolLabel_);
        toolLabel_->setMargin(visible ? 5 : 0);
        (visible ? settingsRow_ : contextRow_)->addWidget(toolLabel_, 1);
        compactSettings_->setVisible(!visible);
        emit transformSettingsVisibilityChanged(visible);
    }
}

void WorkbenchShell::restoreDefaultLayout() {
    setToolbarVisible(true);
    setSidebarVisible(false);
    setTransformSettingsVisible(false);
}

bool WorkbenchShell::isToolbarVisible() const {
    return !toolbar_->isHidden();
}

bool WorkbenchShell::isSidebarVisible() const {
    return !sidebar_->isHidden();
}

bool WorkbenchShell::isTransformSettingsVisible() const {
    return !transformSettings_->isHidden();
}

QTabBar* WorkbenchShell::workspaceTabs() const {
    return workspaceTabs_;
}

QWidget* WorkbenchShell::workspaceBar() const {
    return workspaceTabs_->parentWidget();
}

void WorkbenchShell::updateToolbarGeometry() {
    toolbar_->setGeometry(4, 6, toolbar_->width(),
                          std::min(toolbar_->sizeHint().height(), viewport_->height() - 12));
    toolbar_->raise();
}

bool WorkbenchShell::eventFilter(QObject* watched, QEvent* event) {
    if (watched == viewport_ && event->type() == QEvent::Resize)
        updateToolbarGeometry();
    if (watched == toolbar_ &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
         event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseMove ||
         event->type() == QEvent::Wheel || event->type() == QEvent::ContextMenu)) {
        event->accept();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void WorkbenchShell::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (isSidebarVisible() && event->size().width() < 580) {
        setSidebarVisible(false);
    }
}

void WorkbenchShell::refreshSelection() {
    const bool edit = model_.isEditMode();
    modeLabel_->setText(edit ? QStringLiteral("编辑模式") : QStringLiteral("对象模式"));
    selectionHint_->setText(
        edit ? QStringLiteral("活动元素的对象局部坐标。\n模式与选区不改变保存状态。")
             : QStringLiteral("此处显示所选对象的局部位置。\n数值编辑在右侧属性区。"));
    const auto* node = model_.scene()->find(model_.selection()->selectedEntity());
    selectionLabel_->setText(node ? QString::fromStdString(node->name)
                                  : QStringLiteral("未选择对象"));
    selectionSummary_->setText(
        node ? QStringLiteral("当前选择 · %1").arg(QString::fromStdString(node->name))
             : QStringLiteral("未选择对象 · 点击视口或右侧场景列表开始"));
    transformLabel_->setText(node ? QStringLiteral("局部位置\nX  %1\nY  %2\nZ  %3")
                                        .arg(node->transform.position.x, 0, 'f', 3)
                                        .arg(node->transform.position.y, 0, 'f', 3)
                                        .arg(node->transform.position.z, 0, 'f', 3)
                                  : QString());
    if (edit && node) {
        const auto& selection = model_.displayedComponentSelection();
        const auto domain = selection.domain() == SelectionDomain::Vertex ? QStringLiteral("点")
                            : selection.domain() == SelectionDomain::Edge ? QStringLiteral("边")
                                                                          : QStringLiteral("面");
        const auto active = selection.activeId();
        QString activeText = QStringLiteral("无");
        if (active) {
            activeText = QString::number(active->first);
            if (selection.domain() == SelectionDomain::Edge) {
                activeText += QStringLiteral("–%1").arg(active->second);
            }
        }
        const auto& mesh = model_.displayedEditableMesh(node->id)->source;
        // 场景源已验证身份唯一；点/面总数无需为每次预览重建完整树集合。
        const auto total = selection.domain() == SelectionDomain::Vertex ? mesh.vertices.size()
                           : selection.domain() == SelectionDomain::Face
                               ? mesh.faces.size()
                               : ComponentSelection::elements(mesh, SelectionDomain::Edge).size();
        selectionLabel_->setText(QStringLiteral("%1\n%2：%3 / %4\n活动%2：%5")
                                     .arg(QString::fromStdString(node->name), domain)
                                     .arg(selection.selectedIds().size())
                                     .arg(total)
                                     .arg(activeText));
        selectionSummary_->setText(QStringLiteral("%1 · 已选%2 %3 / %4")
                                       .arg(QString::fromStdString(node->name), domain)
                                       .arg(selection.selectedIds().size())
                                       .arg(total));
        const auto position = selection.activePosition(mesh);
        transformLabel_->setText(position
                                     ? QStringLiteral("活动元素 · 局部坐标\nX  %1\nY  %2\nZ  %3")
                                           .arg(position->x, 0, 'f', 3)
                                           .arg(position->y, 0, 'f', 3)
                                           .arg(position->z, 0, 'f', 3)
                                     : QStringLiteral("未选择组件"));
        if (model_.hasComponentTransform()) {
            selectionHint_->setText(
                QStringLiteral("组件变换预览：尚未写入文档。\n确认后可撤销；取消恢复原状。"));
        }
    }
    selectionSummary_->setToolTip(selectionSummary_->text());
}
} // namespace mini3d::editor
