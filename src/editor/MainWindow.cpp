/*
 * 模块名: MainWindow
 * 功能概述: 实现编辑器主窗口、Dock 布局和中央 OpenGL Viewport 装配。
 * 对外接口: mini3d::editor::MainWindow
 * 依赖关系: Qt 6 Widgets、mini3d_renderer_gl
 * 输入输出: 输入窗口构造参数与用户显隐操作，输出可交互的桌面布局。
 * 异常与错误: 无业务异常；控件创建失败由 Qt/运行时终止处理。
 * 维护说明: 仅实现视图装配，后续业务状态通过独立 Model/ViewModel 接入。
 */

#include "MainWindow.h"

#include "AppearanceInspector.h"
#include "ChineseUi.h"
#include "CollectionPanel.h"
#include "MirrorInspector.h"
#include "SceneTreeModel.h"
#include "AnimationTimeline.h"
#include "SubdivisionInspector.h"
#include "TransformInspector.h"
#include "api/EditorApiService.h"
#include "automation/LocalAutomationBridge.h"
#include "observation/ObservationService.h"
#include "operations/ComponentInteraction.h"
#include "operations/ComponentPicker.h"
#include "operations/AnimationDraftGesture.h"
#include "operations/KeymapRouter.h"
#include "operations/LoopCutSession.h"
#include "operations/ObjectTransformSession.h"
#include "operations/OperatorRegistry.h"
#include "operations/QuickFavorites.h"
#include "renderer_gl/ViewportWidget.h"
#include "workbench/AreaMaximizer.h"
#include "workbench/LastOperationPanel.h"
#include "workbench/OperatorPiePopup.h"
#include "workbench/OperatorSearchPopup.h"
#include "workbench/WorkbenchShell.h"
#include "workbench/WorkspaceManager.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#include <tuple>

namespace mini3d::editor {
namespace {

constexpr int kInitialWindowWidth = 1440;
constexpr int kInitialWindowHeight = 900;
constexpr int kMinimumWindowWidth = 960;
constexpr int kMinimumWindowHeight = 640;
constexpr int kMinimumInspectorDockWidth = 180;

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    initializeChineseUi();
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QStringLiteral("Mini3D Studio"));
    resize(kInitialWindowWidth, kInitialWindowHeight);
    setMinimumSize(kMinimumWindowWidth, kMinimumWindowHeight);
    setDockOptions(AllowNestedDocks | AllowTabbedDocks | AnimatedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    auto workbenchFont = font();
    workbenchFont.setPixelSize(13);
    setFont(workbenchFont);

    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #202020; }"
        "QWidget { color: #dedede; background: #303030; }"
        "QDockWidget, #WorkbenchShell, #ViewportSidebar { background: #303030; }"
        "QMainWindow::separator { background: #202020; width: 3px; height: 3px; }"
        "QDockWidget::title { background: #282828; padding: 2px 6px; }"
        "#ViewportHeader, #TransformSettingsBar, #QuickActionBar, #WorkspaceBar "
        "{ background: #282828; }"
        "QMenuBar, QMenu, QStatusBar, QToolBar { background: #282828; }"
        "QMenuBar::item { padding: 2px 7px; }"
        "QMenu::item { padding: 4px 18px; }"
        "QMenu::item:selected, QMenuBar::item:selected { background: #365477; }"
        "QLineEdit, QAbstractSpinBox, QComboBox { background: #242424; border: 1px solid #444444; "
        "border-radius: 2px; padding: 1px; selection-background-color: #477eb1; }"
        "QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: #75a8e0; }"
        "QAbstractSpinBox[invalidInput=\"true\"] { border: 1px solid #d65a65; }"
        "QAbstractScrollArea { background: #303030; }"
        "QTreeView { background: #2b2b2b; alternate-background-color: #303030; }"
        "QTreeView::item { padding: 1px 2px; }"
        "QTreeView::item:selected { background: #365477; }"
        "QPushButton, QToolButton { background: #3b3b3b; border: 1px solid #484848; "
        "border-radius: 2px; padding: 2px; }"
        "QPushButton:hover, QToolButton:hover { background: #4b4b4b; border-color: #737373; }"
        "QPushButton:pressed, QToolButton:pressed { background: #25384e; }"
        "QToolButton:checked { background: #365477; border-color: #75a8e0; }"
        "#EditModeButton { font-weight: 600; }"
        "#ModeLabel { color: #cccccc; padding: 0px 4px; }"
        "#SelectionSummary, #ToolSettings { color: #bdbdbd; }"
        "QTabBar::tab { background: #282828; padding: 3px 6px; font-size: 12px; }"
        "QTabBar::tab:selected { background: #444444; border-bottom: 1px solid #888888; }"
        "QTabWidget::pane { border: 0; }"
        "#ObjectPropertiesGroup, #DataPropertiesGroup, #ModifierPropertiesGroup "
        "{ background: #3b3b3b; border-color: #484848; text-align: left; }"
        "#QuickActionBar { border: 0; spacing: 1px; }"
        "#QuickActionBar QToolButton { padding: 1px 4px; font-size: 12px; }"
        "#ViewportToolbar { background: #282828; border: 1px solid #444444; spacing: 1px; }"
        "#ViewportToolbar QToolButton { padding: 2px; min-height: 22px; }"
        "QToolTip { color: #eeeeee; background: #282828; border: 1px solid #737373; padding: 6px; }"
        "QWidget:disabled { color: #888888; }"));

    viewModel_ = new SceneViewModel(this);
    auto* viewport = qobject_cast<renderer_gl::ViewportWidget*>(createViewport());
    workbench_ = new WorkbenchShell(viewport, *viewModel_, this);
    setCentralWidget(workbench_);

    auto* sceneDock = createSceneDock();
    auto* inspectorDock = createInspectorDock();
    auto* consoleDock = createConsoleDock();

    addDockWidget(Qt::RightDockWidgetArea, sceneDock);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    splitDockWidget(sceneDock, inspectorDock, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);
    auto* animationDock = new QDockWidget(QStringLiteral("动画时间轴"), this);
    animationDock->setObjectName(QStringLiteral("AnimationDock"));
    animationDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    animationDock->setWidget(new AnimationTimeline(*viewModel_, animationDock));
    addDockWidget(Qt::BottomDockWidgetArea, animationDock);
    tabifyDockWidget(consoleDock, animationDock);
    animationDock->hide();
    animationDock->toggleViewAction()->setObjectName(QStringLiteral("ToggleAnimationDock"));

    createMenus(sceneDock, inspectorDock, consoleDock);
    if (auto* viewMenu = findChild<QMenu*>(QStringLiteral("ViewMenu")))
        viewMenu->addAction(animationDock->toggleViewAction());
    menuBar()->setCornerWidget(workbench_->workspaceBar(), Qt::TopRightCorner);
    menuBar()->setFixedHeight(26);
    applyReferenceDockSizes();
    auto* inputRouter = new KeymapRouter(*this, this);
    connect(viewModel_, &SceneViewModel::editModeChanged, inputRouter, &KeymapRouter::setEditMode);
    workbench_->bindActions(*this, *inputRouter);
    auto* components = new ComponentInteraction(*this, *viewModel_, *viewport, this);
    connect(findChild<QAction*>(QStringLiteral("BoxSelectComponents")), &QAction::triggered,
            components, &ComponentInteraction::startBoxSelection);
    connect(findChild<QAction*>(QStringLiteral("SelectLinkedComponents")), &QAction::triggered,
            components, &ComponentInteraction::selectLinkedUnderPointer);
    connect(inputRouter, &KeymapRouter::keymapChanged, components,
            &ComponentInteraction::cancelBoxSelection);
    auto* modal = new ObjectTransformSession(*this, *viewModel_, *viewport, this);
    auto* draftGesture = new AnimationDraftGesture(*this, *viewModel_, *viewport, this);
    auto* loopCut = new LoopCutSession(*this, *viewModel_, *viewport, this);
    connect(viewport, &renderer_gl::ViewportWidget::navigationStarted, this,
            [this, viewport, components, modal, loopCut] {
                components->cancelBoxSelection();
                modal->cancel();
                loopCut->cancel();
                viewModel_->cancelTransformEdit();
                viewport->setCursorPlacementEnabled(false);
            });
    connect(viewport, &renderer_gl::ViewportWidget::cameraPreviewToggleRequested,
            findChild<QAction*>(QStringLiteral("ToggleCameraPreview")), &QAction::trigger);
    auto* placeCursor = findChild<QAction*>(QStringLiteral("PlaceCursor"));
    const auto cancelCursorPlacement = [viewport] {
        viewport->setCursorPlacementEnabled(false);
    };
    connect(placeCursor, &QAction::triggered, viewport,
            [this, viewport, components, modal, loopCut, placeCursor](bool enabled) {
                if (enabled) {
                    components->cancelBoxSelection();
                    modal->cancel();
                    loopCut->cancel();
                    viewModel_->cancelTransformEdit();
                }
                viewport->setCursorPlacementEnabled(enabled);
                placeCursor->setChecked(viewport->isCursorPlacementEnabled());
                if (viewport->isCursorPlacementEnabled())
                    statusBar()->showMessage(
                        QStringLiteral("游标放置：左键确认，Esc / 右键取消；空白处落到 XZ 地面。"));
            });
    connect(viewport, &renderer_gl::ViewportWidget::cursorPlacementRequested, viewModel_,
            [this, viewport](QPointF pixel) {
                viewModel_->cancelTransformEdit();
                const auto camera = viewport->editorCameraSnapshot();
                if (!camera)
                    return;
                const auto result = locateCursor(
                    *viewModel_->scene(), *viewModel_->assets(), *camera,
                    {static_cast<float>(pixel.x()), static_cast<float>(pixel.y())},
                    {viewport->width(), viewport->height()}, viewModel_->viewportVisibility());
                if (!result.position) {
                    emit viewModel_->operationFailed(result.error);
                } else if (viewModel_->setCursorPosition(*result.position)) {
                    statusBar()->showMessage(
                        result.surface ? QStringLiteral("3D 游标已定位到模型表面。")
                                       : QStringLiteral("3D 游标已定位到 XZ 地面（Y=0）。"));
                }
            });
    connect(viewModel_, &SceneViewModel::documentReset, viewport, cancelCursorPlacement);
    connect(viewModel_, &SceneViewModel::sceneChanged, viewport, cancelCursorPlacement);
    connect(viewModel_, &SceneViewModel::viewportVisibilityChanged, viewport,
            cancelCursorPlacement);
    connect(viewModel_, &SceneViewModel::componentPreviewChanged, viewport, cancelCursorPlacement);
    connect(viewModel_, &SceneViewModel::componentSelectionChanged, viewport,
            cancelCursorPlacement);
    connect(viewModel_->selection(), &SelectionModel::selectedEntityChanged, viewport,
            cancelCursorPlacement);
    connect(viewport, &renderer_gl::ViewportWidget::cameraChanged, viewport, cancelCursorPlacement);
    connect(viewport, &renderer_gl::ViewportWidget::viewModeChanged, viewport,
            cancelCursorPlacement);
    connect(modal, &ObjectTransformSession::activeChanged, viewport,
            [cancelCursorPlacement](bool active) {
                if (active)
                    cancelCursorPlacement();
            });
    connect(inputRouter, &KeymapRouter::keymapChanged, viewport, [viewport](EditorKeymap keymap) {
        viewport->setCursorShortcutEnabled(keymap == EditorKeymap::Blender);
    });
    viewport->setCursorShortcutEnabled(inputRouter->keymap() == EditorKeymap::Blender);
    connect(findChild<QAction*>(QStringLiteral("LoopCut")), &QAction::triggered, loopCut,
            &LoopCutSession::start);
    connect(inputRouter, &KeymapRouter::keymapChanged, loopCut, &LoopCutSession::cancel);
    connect(loopCut, &LoopCutSession::statusTextChanged, this, [this](const QString& text) {
        statusBar()->showMessage(text.section('\n', 0, 0));
    });
    auto* lastOperation = new LastOperationPanel(*viewModel_, *viewport);
    apiService_ = std::make_unique<api::EditorApiService>(*viewModel_);
    apiService_->setBusyProvider([this, viewport, components, modal, loopCut, lastOperation] {
        QStringList reasons;
        if (closing_)
            reasons.append(QStringLiteral("closing"));
        if (QApplication::activeModalWidget())
            reasons.append(QStringLiteral("modal_dialog"));
        if (viewport->isNavigationActive())
            reasons.append(QStringLiteral("navigation"));
        if (viewport->isCursorPlacementEnabled())
            reasons.append(QStringLiteral("cursor_placement"));
        if (modal->isActive())
            reasons.append(QStringLiteral("transform_session"));
        if (components->isBoxSelecting())
            reasons.append(QStringLiteral("box_selection"));
        if (loopCut->stage() != LoopCutStage::Inactive)
            reasons.append(QStringLiteral("loop_cut"));
        const auto* toggle =
            lastOperation->findChild<QToolButton*>(QStringLiteral("LastOperationToggle"));
        if (lastOperation->isVisible() && toggle && toggle->isChecked())
            reasons.append(QStringLiteral("last_operation_adjustment"));
        return reasons;
    });
    observationService_ =
        std::make_unique<observation::ObservationService>(*apiService_, *viewport);
    automationBridge_ = std::make_unique<automation::LocalAutomationBridge>(
        *apiService_, observationService_.get());
    auto* adjustLast = findChild<QAction*>(QStringLiteral("AdjustLastOperation"));
    connect(adjustLast, &QAction::triggered, lastOperation, &LastOperationPanel::open);
    const auto refreshLastOperation = [this, adjustLast] {
        const auto reason = viewModel_->lastOperationDisabledReason();
        adjustLast->setEnabled(reason.isEmpty());
        adjustLast->setToolTip(reason.isEmpty() ? QStringLiteral("展开上一步参数，不重复执行操作。")
                                                : reason);
    };
    connect(viewModel_, &SceneViewModel::lastOperationChanged, adjustLast, refreshLastOperation);
    refreshLastOperation();
    auto* repeatLast = findChild<QAction*>(QStringLiteral("RepeatLastOperation"));
    const auto refreshRepeat = [this, repeatLast] {
        const auto reason = viewModel_->repeatLastOperationDisabledReason();
        repeatLast->setEnabled(reason.isEmpty());
        repeatLast->setToolTip(
            reason.isEmpty() ? QStringLiteral("使用上一步参数在当前选区新执行一次，增加一条历史。")
                             : reason);
    };
    connect(repeatLast, &QAction::triggered, viewModel_, &SceneViewModel::repeatLastOperation);
    connect(viewModel_, &SceneViewModel::lastOperationChanged, repeatLast, refreshRepeat);
    connect(viewModel_, &SceneViewModel::componentSelectionChanged, repeatLast, refreshRepeat);
    refreshRepeat();
    connect(findChild<QAction*>(QStringLiteral("ExtrudeRegion")), &QAction::triggered, modal,
            [modal] {
                modal->start(TransformOperation::Move, TransformTarget::ExtrudeRegion);
            });
    connect(findChild<QAction*>(QStringLiteral("InsetFace")), &QAction::triggered, modal, [modal] {
        modal->start(TransformOperation::Move, TransformTarget::InsetFace);
    });
    connect(findChild<QAction*>(QStringLiteral("BevelEdge")), &QAction::triggered, modal, [modal] {
        modal->start(TransformOperation::Move, TransformTarget::BevelEdge);
    });
    connect(inputRouter, &KeymapRouter::keymapChanged, modal, &ObjectTransformSession::cancel);
    auto* keymapMenu = qobject_cast<QMenu*>(findChild<QAction*>(QStringLiteral("Undo"))->parent())
                           ->addMenu(QStringLiteral("键位配置"));
    auto* keymapGroup = new QActionGroup(this);
    for (const auto keymap : {EditorKeymap::Blender, EditorKeymap::Legacy}) {
        const bool blender = keymap == EditorKeymap::Blender;
        auto* action = keymapMenu->addAction(blender ? QStringLiteral("Blender 风格（G/R/S）")
                                                     : QStringLiteral("Legacy Mini3D（W/E/R）"));
        action->setObjectName(blender ? QStringLiteral("BlenderKeymap")
                                      : QStringLiteral("LegacyKeymap"));
        action->setCheckable(true);
        action->setChecked(inputRouter->keymap() == keymap);
        keymapGroup->addAction(action);
        connect(action, &QAction::triggered, inputRouter, [inputRouter, keymap] {
            inputRouter->setKeymap(keymap);
        });
        connect(inputRouter, &KeymapRouter::keymapChanged, action,
                [action, keymap](EditorKeymap selected) {
                    action->setChecked(selected == keymap);
                });
    }
    for (const auto& [name, operation] : {std::pair{"TransformMove", TransformOperation::Move},
                                          {"TransformRotate", TransformOperation::Rotate},
                                          {"TransformScale", TransformOperation::Scale}}) {
        connect(findChild<QAction*>(QString::fromLatin1(name)), &QAction::triggered, modal,
                [this, modal, draftGesture, operation] {
                    if (viewModel_->animationMode() == renderer_gl::AnimationMode::PoseDraft)
                        draftGesture->start(operation);
                    else
                        modal->start(operation);
                });
    }
    for (const auto& [name, operation] :
         {std::pair{"TransformComponentsMove", TransformOperation::Move},
          {"TransformComponentsRotate", TransformOperation::Rotate},
          {"TransformComponentsScale", TransformOperation::Scale}}) {
        connect(findChild<QAction*>(QString::fromLatin1(name)), &QAction::triggered, modal,
                [modal, operation] {
                    modal->start(operation, TransformTarget::Components);
                });
    }
    auto* modalHud = new QLabel(viewport);
    modalHud->setObjectName(QStringLiteral("ObjectTransformHud"));
    modalHud->setWordWrap(true);
    modalHud->setAttribute(Qt::WA_TransparentForMouseEvents);
    modalHud->setStyleSheet(QStringLiteral("background: #242424; color: #eeeeee; padding: 6px;"));
    modalHud->move(50, 10);
    modalHud->hide();
    connect(modal, &ObjectTransformSession::activeChanged, modalHud, [modalHud](bool active) {
        if (!active) {
            modalHud->hide();
        }
    });
    connect(modal, &ObjectTransformSession::statusTextChanged, this,
            [this, modal, modalHud, viewport](const QString& text) {
                statusBar()->showMessage(text.section('\n', 0, 0));
                if (modal->isActive()) {
                    modalHud->setFixedWidth(std::min(430, viewport->width() - 20));
                    modalHud->setText(text);
                    modalHud->adjustSize();
                    modalHud->show();
                    modalHud->raise();
                }
            });
    connect(modal, &ObjectTransformSession::operationRejected, viewModel_,
            &SceneViewModel::operationFailed);
    connect(draftGesture, &AnimationDraftGesture::activeChanged, this,
            [this, modalHud](bool active) {
                viewModel_->setExternalBusy(QStringLiteral("animation_gesture"), active);
                if (!active)
                    modalHud->hide();
            });
    connect(draftGesture, &AnimationDraftGesture::statusTextChanged, this,
            [this, modalHud, viewport](const QString& text) {
                statusBar()->showMessage(text.section('\n', 0, 0));
                modalHud->setFixedWidth(std::max(100, std::min(500, viewport->width() - 20)));
                modalHud->setText(text);
                modalHud->adjustSize();
                modalHud->show();
                modalHud->raise();
            });
    auto* operators = new OperatorRegistry(*this, *viewModel_, this);
    for (const auto& [name, title, ids] :
         {std::tuple{
              "ShadingPie", "着色 · Z",
              QStringList{"view.shading_material", "view.shading_solid", "view.shading_wireframe"}},
          {"ViewPie", "视图 · `",
           QStringList{"view.front", "view.right", "view.top", "view.orbit", "view.orthographic",
                       "view.focus_selected", "view.focus_all"}}}) {
        connect(
            findChild<QAction*>(QString::fromLatin1(name)), &QAction::triggered, this,
            [this, operators, inputRouter, viewport, name, title, ids] {
                auto area = operators->executionArea();
                if (area == InputArea::None)
                    area = inputRouter->dispatchArea();
                const auto context = operators->captureContext(
                    area == InputArea::None ? InputArea::Viewport : area, inputRouter->keymap());
                auto* pie =
                    new OperatorPiePopup(*operators, context, QString::fromUtf8(title), ids, this);
                pie->setObjectName(QString::fromLatin1(name) + QStringLiteral("Popup"));
                const auto cursor = QCursor::pos();
                pie->openAt(viewport->rect().contains(viewport->mapFromGlobal(cursor))
                                ? cursor
                                : viewport->mapToGlobal(viewport->rect().center()));
            });
    }
    connect(findChild<QAction*>(QStringLiteral("DeleteComponentsMenu")), &QAction::triggered, this,
            [this, operators, inputRouter, viewport] {
                const auto area = operators->executionArea();
                const auto context = operators->captureContext(
                    area == InputArea::None ? InputArea::Viewport : area, inputRouter->keymap());
                auto* menu = new QMenu(this);
                menu->setObjectName(QStringLiteral("ComponentDeletePopup"));
                connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
                QAction* preferred = nullptr;
                for (const auto& [id, domain] :
                     {std::pair{"mesh.delete_vertices", SelectionDomain::Vertex},
                      {"mesh.delete_edges", SelectionDomain::Edge},
                      {"mesh.delete_faces", SelectionDomain::Face}}) {
                    const auto operatorId = QString::fromLatin1(id);
                    auto* source = operators->descriptor(operatorId)->action.data();
                    auto* choice = menu->addAction(source->text());
                    choice->setObjectName(source->objectName() + QStringLiteral("Choice"));
                    const auto reason = operators->disabledReason(operatorId, context);
                    choice->setEnabled(reason.isEmpty());
                    choice->setToolTip(reason.isEmpty() ? source->toolTip() : reason);
                    if (domain == viewModel_->componentSelection().domain())
                        preferred = choice;
                    connect(choice, &QAction::triggered, menu, [operators, operatorId, context] {
                        operators->execute(operatorId, context);
                    });
                }
                menu->addSeparator();
                menu->addAction(QStringLiteral("清理孤立点；不保留散边"))->setEnabled(false);
                menu->setToolTipsVisible(true);
                menu->popup(
                    viewport->mapToGlobal(QPoint(viewport->width() / 2, viewport->height() / 3)));
                menu->setActiveAction(preferred);
            });
    favorites_ = new QuickFavorites(*operators, this);
    auto* search = new OperatorSearchPopup(*operators, *favorites_, this);
    auto* searchAction = new QAction(QStringLiteral("搜索操作…"), this);
    searchAction->setObjectName(QStringLiteral("SearchOperators"));
    qobject_cast<QMenu*>(findChild<QAction*>(QStringLiteral("Undo"))->parent())
        ->addAction(searchAction);
    connect(searchAction, &QAction::triggered, this, [inputRouter, operators, search] {
        const auto area = inputRouter->dispatchArea();
        // 菜单替代入口明确以视口为目标；键盘入口使用打开前的区域。
        search->openForContext(operators->captureContext(
            area == InputArea::None ? InputArea::Viewport : area, inputRouter->keymap()));
    });
    auto* favoritesAction = new QAction(QStringLiteral("快捷收藏…"), this);
    favoritesAction->setObjectName(QStringLiteral("QuickFavorites"));
    qobject_cast<QMenu*>(findChild<QAction*>(QStringLiteral("Undo"))->parent())
        ->addAction(favoritesAction);
    connect(favoritesAction, &QAction::triggered, this, [inputRouter, operators, search] {
        const auto area = inputRouter->dispatchArea();
        search->openForContext(
            operators->captureContext(area == InputArea::None ? InputArea::Viewport : area,
                                      inputRouter->keymap()),
            OperatorPopupMode::Favorites);
    });
    connect(operators, &OperatorRegistry::executionFailed, this, [this](const QString& reason) {
        statusBar()->showMessage(reason, 6000);
    });
    inputRouter->setRegistry(operators);
    inputRouter->bindActions();
    workbench_->bindQuickActions(*this);
    auto* maximizer = new AreaMaximizer(*this, this);
    auto* maximizeAction = findChild<QAction*>(QStringLiteral("ToggleAreaMaximized"));
    connect(maximizeAction, &QAction::triggered, this,
            [maximizer, operators, inputRouter, maximizeAction] {
                auto area = operators->executionArea();
                if (area == InputArea::None) {
                    area = inputRouter->areaForWidget(QApplication::focusWidget());
                }
                maximizer->toggle(area == InputArea::None ? InputArea::Viewport : area);
                maximizeAction->setChecked(maximizer->isMaximized());
            });
    connect(maximizer, &AreaMaximizer::operationRejected, viewModel_,
            &SceneViewModel::operationFailed);
    connect(maximizer, &AreaMaximizer::maximizedAreaChanged, this,
            [this, maximizeAction](InputArea area) {
                maximizeAction->setChecked(area != InputArea::None);
                statusBar()->showMessage(
                    area == InputArea::None
                        ? QStringLiteral("已恢复原区域布局。")
                        : QStringLiteral("已最大化区域；再次使用快捷键或视图菜单可还原。"),
                    5000);
            });
    connect(
        findChild<QAction*>(QStringLiteral("ConfigureAreaMaximizeShortcut")), &QAction::triggered,
        this, [this, inputRouter] {
            QDialog dialog(this);
            dialog.setObjectName(QStringLiteral("AreaShortcutDialog"));
            dialog.setWindowTitle(QStringLiteral("区域最大化快捷键"));
            auto* layout = new QVBoxLayout(&dialog);
            layout->addWidget(new QLabel(
                QStringLiteral("仅用于 Blender 键位。清空可禁用；视图菜单始终可用。"), &dialog));
            auto* edit = new QKeySequenceEdit(inputRouter->maximizeShortcut(), &dialog);
            edit->setObjectName(QStringLiteral("AreaShortcutEdit"));
            edit->setMaximumSequenceLength(1);
            edit->setClearButtonEnabled(true);
            layout->addWidget(edit);
            auto* reason = new QLabel(&dialog);
            reason->setObjectName(QStringLiteral("AreaShortcutReason"));
            reason->setWordWrap(true);
            layout->addWidget(reason);
            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                                     QDialogButtonBox::RestoreDefaults,
                                                 &dialog);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, edit,
                    [edit] {
                        edit->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Space")));
                    });
            connect(buttons, &QDialogButtonBox::accepted, &dialog,
                    [&dialog, inputRouter, edit, reason] {
                        QString error;
                        if (inputRouter->setMaximizeShortcut(edit->keySequence(), &error)) {
                            dialog.accept();
                        } else {
                            reason->setText(error);
                        }
                    });
            dialog.adjustSize();
            dialog.exec();
        });
    workspaces_ = new WorkspaceManager(*this, *workbench_, this);
    connect(workspaces_, &WorkspaceManager::layoutAboutToBeCaptured, maximizer,
            &AreaMaximizer::restore);
    if (!QCoreApplication::organizationName().isEmpty()) {
        QSettings settings;
        inputRouter->restorePreferences(settings);
    }
    connect(viewModel_, &SceneViewModel::documentChanged, this, &MainWindow::refreshDocumentTitle);
    refreshDocumentTitle();
    connect(viewModel_->selection(), &SelectionModel::selectedEntityChanged, this, [this] {
        synchronizeTreeSelection();
    });
    connect(viewModel_, &SceneViewModel::sceneChanged, this, [this, viewport] {
        synchronizeTreeSelection();
        viewport->notifySceneVisualChange();
    });
    connect(viewModel_, &SceneViewModel::operationFailed, this, [this](const QString& message) {
        statusBar()->showMessage(message, 6000);
    });
    connect(viewModel_, &SceneViewModel::operationCompleted, this, [this](const QString& message) {
        statusBar()->showMessage(message.section('\n', 0, 0), 6000);
    });
    constexpr std::size_t kMeasuredEditableVertexBudget = 10000;
    auto* scaleStatus = new QLabel(QStringLiteral("大网格 · 编辑可能延迟"), this);
    scaleStatus->setObjectName(QStringLiteral("EditableScaleStatus"));
    // 保留中文行高，避免非整数 DPR 下提示出现时改变视口投影。
    scaleStatus->setMinimumHeight(scaleStatus->sizeHint().height());
    scaleStatus->setToolTip(QStringLiteral("基础编辑建议每对象不超过 1 万源点。\n"
                                           "复杂拓扑、比例编辑和修改器另有开销，详见 F1 帮助。"));
    statusBar()->addPermanentWidget(scaleStatus);
    const auto refreshScaleStatus = [this, scaleStatus] {
        const auto content = viewModel_->isEditMode()
                                 ? viewModel_->displayedEditableMesh(viewModel_->editedEntity())
                                 : nullptr;
        const auto count = content ? content->source.vertices.size() : 0;
        scaleStatus->setText(count > kMeasuredEditableVertexBudget
                                 ? QStringLiteral("大网格：%1 点 · 编辑可能延迟").arg(count)
                                 : QString{});
    };
    connect(viewModel_, &SceneViewModel::sceneChanged, this, refreshScaleStatus);
    connect(viewModel_, &SceneViewModel::editModeChanged, this, refreshScaleStatus);
    connect(viewModel_, &SceneViewModel::componentPreviewChanged, this, refreshScaleStatus);
    refreshScaleStatus();
    statusBar()->showMessage(QStringLiteral("就绪"));
}

MainWindow::~MainWindow() {
    // 状态提示与视口仍存活时清理会话，不能等到基类销毁子控件。
    // 全局动画输入过滤器引用模型；必须先销毁，避免子控件析构事件读已死模型。
    delete findChild<AnimationDraftGesture*>();
    automationBridge_.reset();
    observationService_.reset();
    if (auto* viewport = findChild<renderer_gl::ViewportWidget*>()) {
        viewport->setFrameDisplayStateProvider({});
        viewport->setCameraCommitter({});
    }
    viewModel_->setAnimationViewProvider({});
    viewModel_->suspendAnimation();
    viewModel_->cancelTransformEdit();
}
api::EditorApiService& MainWindow::apiService() {
    return *apiService_;
}
observation::ObservationService& MainWindow::observationService() {
    return *observationService_;
}
automation::LocalAutomationBridge& MainWindow::automationBridge() {
    return *automationBridge_;
}

QWidget* MainWindow::createViewport() {
    auto* viewport = new renderer_gl::ViewportWidget(this);
    viewport->setScene(viewModel_->scene());
    viewport->setAssets(viewModel_->assets());
    connect(viewport, &renderer_gl::ViewportWidget::filesDropped, viewModel_,
            [this](const QStringList& paths) {
                for (const auto& path : paths) {
                    viewModel_->importGltf(path);
                }
            });
    connect(viewport, &renderer_gl::ViewportWidget::interactionRejected, this,
            [this](const QString& message) {
                statusBar()->showMessage(message, 6000);
            });
    viewport->setFrameDisplayStateProvider([this] {
        const auto state = viewModel_->apiDocumentState();
        return renderer_gl::FrameDisplayState{
            {state.document.instanceId, state.document.documentId,
             state.documentRevision, state.historyRevision},
            {state.document.instanceId, state.document.documentId, state.documentRevision,
             viewModel_->animationEvaluationId(), viewModel_->animationFrame(),
             viewModel_->animationMode()},
            viewModel_->animationSessionRevision(), viewModel_->installedAnimationPose()};
    });
    viewport->setCameraCommitter([this](const renderer_gl::EditorCamera& camera,
                                       const std::function<void()>& install) {
        return viewModel_->commitEditorCamera(camera, install);
    });
    viewModel_->setAnimationViewProvider([viewport] {
        return viewport->editorCameraSnapshot().value_or(renderer_gl::EditorCamera{});
    });
    connect(viewModel_, &SceneViewModel::animationPoseChanged, viewport,
            &renderer_gl::ViewportWidget::notifyFrameDisplayStateChanged);
    connect(viewModel_, &SceneViewModel::apiStateChanged, viewport,
            &renderer_gl::ViewportWidget::notifyFrameDisplayStateChanged);
    connect(viewport, &renderer_gl::ViewportWidget::observationUnavailable,
            viewModel_, &SceneViewModel::suspendAnimation);
    connect(viewModel_, &SceneViewModel::animationSessionChanged, viewport, [this, viewport] {
        viewport->notifyFrameDisplayStateChanged();
        if (viewModel_->animationMode() != renderer_gl::AnimationMode::Base) {
            viewport->resetMoveInteraction();
            viewport->setCursorPlacementEnabled(false);
        }
        viewport->update();
    });
    connect(viewModel_, &SceneViewModel::previewCameraChanged, viewport,
            &renderer_gl::ViewportWidget::setPreviewCamera);
    connect(viewport, &renderer_gl::ViewportWidget::previewExitRequested, viewModel_, [this] {
        viewModel_->setPreviewCamera(0);
    });
    connect(viewModel_, &SceneViewModel::documentReset, viewport, [this, viewport] {
        viewport->setAssets(viewModel_->assets());
        viewport->setEditorCamera(viewModel_->editorCamera());
    });
    connect(viewport, &renderer_gl::ViewportWidget::pickRequested, viewModel_,
            &SceneViewModel::selectRay);
    connect(viewport, &renderer_gl::ViewportWidget::moveStarted, viewModel_,
            &SceneViewModel::beginTransformEdit);
    connect(viewport, &renderer_gl::ViewportWidget::movePreviewed, viewModel_,
            &SceneViewModel::previewTransform);
    connect(viewport, &renderer_gl::ViewportWidget::moveFinished, viewModel_,
            &SceneViewModel::finishTransformEdit);
    connect(viewModel_, &SceneViewModel::transformEditFinished, viewport,
            &renderer_gl::ViewportWidget::resetMoveInteraction);
    connect(viewModel_->selection(), &SelectionModel::selectedEntityChanged, viewport,
            &renderer_gl::ViewportWidget::setSelectedEntity);
    connect(viewModel_, &SceneViewModel::editModeChanged, viewport,
            &renderer_gl::ViewportWidget::setEditMode);
    return viewport;
}

QDockWidget* MainWindow::createSceneDock() {
    auto* dock = new QDockWidget(QStringLiteral("场景"), this);
    dock->setObjectName(QStringLiteral("SceneDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* pages = new QTabWidget(dock);
    pages->setObjectName(QStringLiteral("ScenePages"));
    pages->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    tree_ = new QTreeView(dock);
    tree_->setObjectName(QStringLiteral("SceneTree"));
    tree_->setHeaderHidden(true);
    tree_->setMinimumHeight(24);
    tree_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    // 模型的会话凭据限制同树移动，并拒绝其他文档或文件拖入。
    tree_->setDragDropMode(QAbstractItemView::DragDrop);
    tree_->setDefaultDropAction(Qt::MoveAction);
    tree_->setDropIndicatorShown(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setToolTip(
        QStringLiteral("拖到对象上更换父对象，拖到空白处移至根层；保留局部变换，不支持行间排序。"));
    connect(tree_, &QTreeView::customContextMenuRequested, this, &MainWindow::showSceneContextMenu);
    treeModel_ = new SceneTreeModel(*viewModel_, tree_);
    tree_->setModel(treeModel_);
    tree_->expandAll();
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (!treeModel_->isResetting()) {
                    viewModel_->selection()->setSelectedEntity(treeModel_->entityId(current));
                }
            });
    auto* content = new QWidget(pages);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* search = new QLineEdit(content);
    search->setObjectName(QStringLiteral("SceneSearch"));
    search->setPlaceholderText(QStringLiteral("搜索对象 · Enter"));
    search->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    search->setClearButtonEnabled(true);
    auto* next = new QPushButton(QStringLiteral("查找"), content);
    next->setToolTip(QStringLiteral("定位下一个匹配对象；也可在搜索框按 Enter。"));
    next->setAccessibleName(QStringLiteral("查找下一个对象"));
    next->setObjectName(QStringLiteral("FindNextEntity"));
    connect(search, &QLineEdit::returnPressed, this, [this, search] {
        findNextEntity(search->text());
    });
    connect(next, &QPushButton::clicked, this, [this, search] {
        findNextEntity(search->text());
    });
    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(4);
    searchRow->addWidget(search, 1);
    searchRow->addWidget(next);
    layout->addLayout(searchRow);
    layout->addWidget(tree_);
    pages->addTab(content, QStringLiteral("场景"));
    auto* collections = new QScrollArea(pages);
    collections->setObjectName(QStringLiteral("CollectionScroll"));
    collections->setWidgetResizable(true);
    collections->setFrameShape(QFrame::NoFrame);
    collections->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    collections->setWidget(new CollectionPanel(*viewModel_, collections));
    pages->addTab(collections, QStringLiteral("集合"));
    dock->setWidget(pages);
    return dock;
}

QDockWidget* MainWindow::createInspectorDock() {
    auto* dock = new QDockWidget(QStringLiteral("属性"), this);
    dock->setObjectName(QStringLiteral("InspectorDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setMinimumWidth(kMinimumInspectorDockWidth);

    auto* pages = new QTabWidget(dock);
    pages->setObjectName(QStringLiteral("PropertyPages"));
    pages->setTabPosition(QTabWidget::North);
    pages->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    const auto addPage = [pages](QWidget* inspector, const QString& title, const QString& name) {
        for (auto* form : inspector->findChildren<QFormLayout*>()) {
            form->setContentsMargins(4, 6, 4, 6);
            form->setSpacing(4);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            form->setRowWrapPolicy(QFormLayout::WrapLongRows);
            for (int row = 0; row < form->rowCount(); ++row) {
                auto* item = form->itemAt(row, QFormLayout::SpanningRole);
                if (item) {
                    if (auto* label = qobject_cast<QLabel*>(item->widget()))
                        label->setWordWrap(true);
                }
            }
        }
        for (auto* field : inspector->findChildren<QAbstractSpinBox*>())
            field->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        for (auto* field : inspector->findChildren<QLineEdit*>())
            field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        for (auto* field : inspector->findChildren<QComboBox*>())
            field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto* content = new QWidget(pages);
        auto* layout = new QVBoxLayout(content);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* group = new QToolButton(content);
        group->setObjectName(name + QStringLiteral("Group"));
        group->setText(title);
        group->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        group->setArrowType(Qt::DownArrow);
        group->setCheckable(true);
        group->setChecked(true);
        layout->addWidget(group);
        layout->addWidget(inspector);
        layout->addStretch();
        connect(group, &QToolButton::toggled, inspector, [group, inspector](bool expanded) {
            inspector->setVisible(expanded);
            group->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        });
        auto* scroll = new QScrollArea(pages);
        scroll->setObjectName(name + QStringLiteral("Scroll"));
        scroll->setWidgetResizable(true);
        scroll->setWidget(content);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setMinimumWidth(120);
        scroll->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        pages->addTab(scroll, title);
    };
    addPage(new TransformInspector(*viewModel_, pages), QStringLiteral("对象"),
            QStringLiteral("ObjectProperties"));
    addPage(new AppearanceInspector(*viewModel_, pages), QStringLiteral("数据与外观"),
            QStringLiteral("DataProperties"));
    auto* modifiers = new QWidget(pages);
    auto* modifierLayout = new QVBoxLayout(modifiers);
    modifierLayout->setContentsMargins(0, 0, 0, 0);
    modifierLayout->addWidget(new MirrorInspector(*viewModel_, modifiers));
    modifierLayout->addWidget(new SubdivisionInspector(*viewModel_, modifiers));
    addPage(modifiers, QStringLiteral("修改器"), QStringLiteral("ModifierProperties"));
    dock->setWidget(pages);

    return dock;
}

QDockWidget* MainWindow::createConsoleDock() {
    auto* dock = new QDockWidget(QStringLiteral("控制台"), this);
    dock->setObjectName(QStringLiteral("ConsoleDock"));
    dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    auto* console = new QPlainTextEdit(dock);
    console->setObjectName(QStringLiteral("ConsoleOutput"));
    console->setReadOnly(true);
    constexpr int documentMargin = 1;
    console->document()->setDocumentMargin(documentMargin);
    console->ensurePolished();
    console->setMinimumHeight(console->fontMetrics().lineSpacing() + 2 * documentMargin +
                              2 * console->frameWidth());
    console->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    console->setMaximumBlockCount(1000);
    console->setPlainText(QStringLiteral("Mini3D Studio 已初始化。"));
    connect(viewModel_, &SceneViewModel::operationCompleted, console,
            &QPlainTextEdit::appendPlainText);
    connect(viewModel_, &SceneViewModel::operationFailed, console,
            &QPlainTextEdit::appendPlainText);
    dock->setWidget(console);

    return dock;
}

void MainWindow::createMenus(QDockWidget* sceneDock, QDockWidget* inspectorDock,
                             QDockWidget* consoleDock) {
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    auto* newAction = fileMenu->addAction(QStringLiteral("新建场景"));
    newAction->setObjectName(QStringLiteral("NewScene"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, [this] {
        if (confirmDiscardChanges()) {
            viewModel_->newScene();
        }
    });
    auto* openAction = fileMenu->addAction(QStringLiteral("打开场景…"));
    openAction->setObjectName(QStringLiteral("OpenScene"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] {
        if (!confirmDiscardChanges()) {
            return;
        }
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("打开场景"), {},
                                                       QStringLiteral("Mini3D 场景 (*.m3dscene)"),
                                                       nullptr, QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) {
            viewModel_->openScene(path);
        }
    });
    auto* saveAction = fileMenu->addAction(QStringLiteral("保存场景"));
    saveAction->setObjectName(QStringLiteral("SaveScene"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this] {
        saveScene(false);
    });
    auto* saveAsAction = fileMenu->addAction(QStringLiteral("场景另存为…"));
    saveAsAction->setObjectName(QStringLiteral("SaveSceneAs"));
    saveAsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(saveAsAction, &QAction::triggered, this, [this] {
        saveScene(true);
    });
    fileMenu->addSeparator();
    auto* importAction = fileMenu->addAction(QStringLiteral("导入 glTF / GLB(&I)…"));
    importAction->setObjectName(QStringLiteral("ImportGltf"));
    importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(importAction, &QAction::triggered, this, [this] {
        const auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("导入静态 glTF 模型"), {},
            QStringLiteral("glTF 模型 (*.glb *.gltf)"), nullptr, QFileDialog::DontUseNativeDialog);
        if (!path.isEmpty()) {
            viewModel_->importGltf(path);
        }
    });
    auto* exportMenu = fileMenu->addMenu(QStringLiteral("导出所选为 OBJ"));
    const auto addObjExport = [this, exportMenu](const QString& title, const QString& name,
                                               bool evaluated) {
        auto* action = exportMenu->addAction(title);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, evaluated] {
            const auto reason = viewModel_->objExportDisabledReason();
            if (!reason.isEmpty()) {
                emit viewModel_->operationFailed(reason);
                return;
            }
            QFileDialog dialog(this, QStringLiteral("导出所选 OBJ（Y-up，单位不变）"));
            dialog.setObjectName(QStringLiteral("ExportObjDialog"));
            dialog.setOption(QFileDialog::DontUseNativeDialog);
            dialog.setAcceptMode(QFileDialog::AcceptSave);
            dialog.setFileMode(QFileDialog::AnyFile);
            dialog.setNameFilter(QStringLiteral("Wavefront OBJ (*.obj)"));
            dialog.setDefaultSuffix(QStringLiteral("obj"));
            if (dialog.exec() == QDialog::Accepted && !dialog.selectedFiles().isEmpty())
                viewModel_->exportObj(dialog.selectedFiles().front(), evaluated);
        });
    };
    addObjExport(QStringLiteral("修改器结果…"), QStringLiteral("ExportObjEvaluated"), true);
    addObjExport(QStringLiteral("可编辑源（不含修改器）…"), QStringLiteral("ExportObjSource"), false);
    auto* exitAction = fileMenu->addAction(QStringLiteral("退出(&X)"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->setObjectName(QStringLiteral("ViewMenu"));
    sceneDock->toggleViewAction()->setObjectName(QStringLiteral("ToggleSceneDock"));
    inspectorDock->toggleViewAction()->setObjectName(QStringLiteral("ToggleInspectorDock"));
    consoleDock->toggleViewAction()->setObjectName(QStringLiteral("ToggleConsoleDock"));
    viewMenu->addAction(sceneDock->toggleViewAction());
    viewMenu->addAction(inspectorDock->toggleViewAction());
    viewMenu->addAction(consoleDock->toggleViewAction());
    auto* maximizeAction = viewMenu->addAction(QStringLiteral("最大化当前区域 / 还原"));
    maximizeAction->setObjectName(QStringLiteral("ToggleAreaMaximized"));
    maximizeAction->setCheckable(true);
    auto* maximizeShortcut = viewMenu->addAction(QStringLiteral("设置区域最大化快捷键…"));
    maximizeShortcut->setObjectName(QStringLiteral("ConfigureAreaMaximizeShortcut"));
    auto* toolbarAction = viewMenu->addAction(QStringLiteral("视口工具条"));
    toolbarAction->setObjectName(QStringLiteral("ToggleViewportToolbar"));
    toolbarAction->setCheckable(true);
    toolbarAction->setChecked(workbench_->isToolbarVisible());
    connect(toolbarAction, &QAction::toggled, workbench_, &WorkbenchShell::setToolbarVisible);
    connect(workbench_, &WorkbenchShell::toolbarVisibilityChanged, toolbarAction,
            &QAction::setChecked);
    auto* sidebarAction = viewMenu->addAction(QStringLiteral("视口侧栏"));
    sidebarAction->setObjectName(QStringLiteral("ToggleViewportSidebar"));
    sidebarAction->setCheckable(true);
    sidebarAction->setChecked(workbench_->isSidebarVisible());
    connect(sidebarAction, &QAction::toggled, workbench_, &WorkbenchShell::setSidebarVisible);
    connect(workbench_, &WorkbenchShell::sidebarVisibilityChanged, sidebarAction,
            &QAction::setChecked);
    auto* settingsAction = viewMenu->addAction(QStringLiteral("变换设置栏"));
    settingsAction->setObjectName(QStringLiteral("ToggleTransformSettingsBar"));
    settingsAction->setCheckable(true);
    settingsAction->setChecked(workbench_->isTransformSettingsVisible());
    settingsAction->setToolTip(QStringLiteral("参考布局默认收起此行；变换设置仍可从视口标题栏菜单使用。"));
    connect(settingsAction, &QAction::toggled, workbench_,
            &WorkbenchShell::setTransformSettingsVisible);
    connect(workbench_, &WorkbenchShell::transformSettingsVisibilityChanged, settingsAction,
            &QAction::setChecked);
    const auto defaultLayout = saveState(2);
    auto* restoreLayout = viewMenu->addAction(QStringLiteral("应用参考布局"));
    restoreLayout->setObjectName(QStringLiteral("RestoreDefaultViewportLayout"));
    restoreLayout->setToolTip(QStringLiteral("按当前窗口应用 Blender 参考比例：右栏约 18%，下方输出区约 7%。"));
    connect(restoreLayout, &QAction::triggered, this, [this, defaultLayout] {
        findChild<AreaMaximizer*>()->restore();
        restoreState(defaultLayout, 2);
        workbench_->restoreDefaultLayout();
        applyReferenceDockSizes();
        statusBar()->showMessage(QStringLiteral("已应用参考布局；当前工作区的编辑设置保留。"),
                                 5000);
    });
    viewMenu->addSeparator();
    auto* focusAction = viewMenu->addAction(QStringLiteral("聚焦所选对象"));
    focusAction->setObjectName(QStringLiteral("FocusSelection"));
    focusAction->setShortcut(QKeySequence(QStringLiteral("F")));
    // 只在树/视口本身有焦点时响应，避免抢走属性或重命名输入框中的 F。
    focusAction->setShortcutContext(Qt::WidgetShortcut);
    tree_->addAction(focusAction);
    auto* viewport = findChild<renderer_gl::ViewportWidget*>();
    viewport->addAction(focusAction);
    auto* hideSelection = viewMenu->addAction(QStringLiteral("隐藏所选（仅视口）"));
    hideSelection->setObjectName(QStringLiteral("HideSelection"));
    connect(hideSelection, &QAction::triggered, viewModel_, &SceneViewModel::hideSelection);
    auto* revealHidden = viewMenu->addAction(QStringLiteral("恢复隐藏项（仅视口）"));
    revealHidden->setObjectName(QStringLiteral("RevealHidden"));
    connect(revealHidden, &QAction::triggered, viewModel_, &SceneViewModel::revealHidden);
    auto* localView = viewMenu->addAction(QStringLiteral("局部视图 / 返回全部"));
    localView->setObjectName(QStringLiteral("ToggleLocalView"));
    localView->setCheckable(true);
    connect(localView, &QAction::triggered, viewModel_, &SceneViewModel::toggleLocalView);
    auto* visibilityStatus = new QLabel(QStringLiteral("局部视图 · 小键盘 / 退出"), this);
    visibilityStatus->setObjectName(QStringLiteral("ViewportVisibilityStatus"));
    // 非整数 DPI 下，空文本与中文文本的 sizeHint 可能相差一像素；预留高度避免挤动视口。
    visibilityStatus->setMinimumHeight(visibilityStatus->sizeHint().height());
    statusBar()->addPermanentWidget(visibilityStatus);
    const auto syncVisibility = [this, viewport, localView, visibilityStatus] {
        const auto& visibility = viewModel_->viewportVisibility();
        viewport->setViewportVisibility(visibility);
        localView->setChecked(visibility.localRoot != 0);
        localView->setText(visibility.localRoot ? QStringLiteral("退出局部视图（当前已隔离）")
                                                : QStringLiteral("进入局部视图"));
        visibilityStatus->setText(visibility.localRoot ? QStringLiteral("局部视图 · 小键盘 / 退出")
                                  : visibility.hasHiddenElements() ||
                                          !visibility.hiddenObjects.empty()
                                      ? QStringLiteral("有临时隐藏项 · Alt+H 恢复")
                                      : QString{});
    };
    connect(viewModel_, &SceneViewModel::viewportVisibilityChanged, this, syncVisibility);
    syncVisibility();
    auto* focusAll = viewMenu->addAction(QStringLiteral("聚焦全部可见对象"));
    focusAll->setObjectName(QStringLiteral("FocusAll"));
    connect(focusAll, &QAction::triggered, this, [this, viewport] {
        viewModel_->cancelTransformEdit();
        if (!viewport->focusAll()) {
            emit viewModel_->operationFailed(
                QStringLiteral("没有可见对象可聚焦；相机预览中请先返回编辑视图。"));
        }
    });
    auto* cameraPreview = viewMenu->addAction(QStringLiteral("相机预览 / 返回编辑视图"));
    cameraPreview->setObjectName(QStringLiteral("ToggleCameraPreview"));
    cameraPreview->setCheckable(true);
    connect(cameraPreview, &QAction::triggered, viewModel_, &SceneViewModel::toggleCameraPreview);
    connect(viewModel_, &SceneViewModel::previewCameraChanged, cameraPreview,
            [cameraPreview](core::EntityId id) {
                cameraPreview->setChecked(id != 0);
            });
    connect(cameraPreview, &QAction::triggered, this, [this, cameraPreview] {
        cameraPreview->setChecked(viewModel_->previewCamera() != 0);
    });
    auto* editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    auto* createCollection = editMenu->addAction(QStringLiteral("新建集合…"));
    createCollection->setObjectName(QStringLiteral("CreateCollection"));
    connect(createCollection, &QAction::triggered, findChild<CollectionPanel*>(),
            &CollectionPanel::createCollection);
    auto* mirrorMenu = editMenu->addMenu(QStringLiteral("Mirror"));
    auto* mirrorAdd = mirrorMenu->addAction(QStringLiteral("添加 Mirror"));
    auto* mirrorApply = mirrorMenu->addAction(QStringLiteral("应用 Mirror"));
    auto* mirrorRemove = mirrorMenu->addAction(QStringLiteral("删除 Mirror"));
    mirrorAdd->setObjectName(QStringLiteral("MirrorAdd"));
    mirrorApply->setObjectName(QStringLiteral("MirrorApply"));
    mirrorRemove->setObjectName(QStringLiteral("MirrorRemove"));
    connect(mirrorAdd, &QAction::triggered, viewModel_, [this] {
        viewModel_->setMirrorOptions(viewModel_->selection()->selectedEntity(),
                                     core::modeling::MirrorOptions{});
    });
    connect(mirrorApply, &QAction::triggered, viewModel_, [this] {
        viewModel_->applyMirror(viewModel_->selection()->selectedEntity());
    });
    connect(mirrorRemove, &QAction::triggered, viewModel_, [this] {
        viewModel_->setMirrorOptions(viewModel_->selection()->selectedEntity(), std::nullopt);
    });
    const auto refreshMirror = [this, mirrorAdd, mirrorApply, mirrorRemove] {
        const auto id = viewModel_->selection()->selectedEntity();
        const auto* node = viewModel_->scene()->find(id);
        const bool editable = node && node->editableMesh != 0;
        const bool hasMirror = viewModel_->mirrorOptions(id).has_value();
        mirrorAdd->setEnabled(editable && !hasMirror);
        mirrorApply->setEnabled(hasMirror);
        mirrorRemove->setEnabled(hasMirror);
    };
    connect(viewModel_, &SceneViewModel::sceneChanged, mirrorMenu, refreshMirror);
    connect(viewModel_->selection(), &SelectionModel::selectedEntityChanged, mirrorMenu,
            refreshMirror);
    refreshMirror();
    auto* subdivisionMenu = editMenu->addMenu(QStringLiteral("细分"));
    auto* subdivAdd = subdivisionMenu->addAction(QStringLiteral("添加细分（1级）"));
    auto* subdivApply = subdivisionMenu->addAction(QStringLiteral("应用细分及前置Mirror"));
    auto* subdivRemove = subdivisionMenu->addAction(QStringLiteral("删除细分"));
    subdivAdd->setObjectName(QStringLiteral("SubdivisionAdd"));
    subdivApply->setObjectName(QStringLiteral("SubdivisionApply"));
    subdivRemove->setObjectName(QStringLiteral("SubdivisionRemove"));
    connect(subdivAdd, &QAction::triggered, viewModel_, [this] {
        viewModel_->setSubdivisionOptions(viewModel_->selection()->selectedEntity(),
                                          core::modeling::SubdivisionOptions{});
    });
    connect(subdivApply, &QAction::triggered, viewModel_, [this] {
        viewModel_->applySubdivision(viewModel_->selection()->selectedEntity());
    });
    connect(subdivRemove, &QAction::triggered, viewModel_, [this] {
        viewModel_->setSubdivisionOptions(viewModel_->selection()->selectedEntity(), std::nullopt);
    });
    const auto refreshSubdivision = [this, subdivAdd, subdivApply, subdivRemove] {
        const auto id = viewModel_->selection()->selectedEntity();
        const auto* node = viewModel_->scene()->find(id);
        const bool hasSubdivision = viewModel_->subdivisionOptions(id).has_value();
        subdivAdd->setEnabled(node && node->editableMesh != 0 && !hasSubdivision);
        subdivApply->setEnabled(hasSubdivision);
        subdivRemove->setEnabled(hasSubdivision);
    };
    connect(viewModel_, &SceneViewModel::sceneChanged, subdivisionMenu, refreshSubdivision);
    connect(viewModel_->selection(), &SelectionModel::selectedEntityChanged, subdivisionMenu,
            refreshSubdivision);
    refreshSubdivision();
    auto* adjustLast = editMenu->addAction(QStringLiteral("调整上一步…"));
    adjustLast->setObjectName(QStringLiteral("AdjustLastOperation"));
    auto* repeatLast = editMenu->addAction(QStringLiteral("重复上一步（新操作）"));
    repeatLast->setObjectName(QStringLiteral("RepeatLastOperation"));
    auto* transformMenu = editMenu->addMenu(QStringLiteral("即时变换"));
    for (const auto& [name, text] : {std::pair{"TransformMove", "移动（模态）"},
                                     {"TransformRotate", "旋转（模态）"},
                                     {"TransformScale", "缩放（模态）"}}) {
        auto* action = transformMenu->addAction(QString::fromUtf8(text));
        action->setObjectName(QString::fromLatin1(name));
    }
    auto* componentTransforms = editMenu->addMenu(QStringLiteral("组件变换"));
    auto* proportional = componentTransforms->addAction(QStringLiteral("比例编辑 Smooth（O）"));
    proportional->setObjectName(QStringLiteral("ToggleProportionalEditing"));
    proportional->setCheckable(true);
    auto* connected = componentTransforms->addAction(QStringLiteral("比例编辑：仅连通"));
    connected->setObjectName(QStringLiteral("ProportionalConnected"));
    connected->setCheckable(true);
    connect(proportional, &QAction::triggered, viewModel_,
            &SceneViewModel::setProportionalEditingEnabled);
    connect(connected, &QAction::triggered, viewModel_, &SceneViewModel::setProportionalConnected);
    const auto syncProportional = [this, proportional, connected] {
        proportional->setChecked(viewModel_->isProportionalEditingEnabled());
        connected->setChecked(viewModel_->isProportionalConnected());
        proportional->setEnabled(viewModel_->isEditMode());
        connected->setEnabled(viewModel_->isEditMode());
    };
    connect(viewModel_, &SceneViewModel::proportionalEditingChanged, proportional, syncProportional);
    connect(viewModel_, &SceneViewModel::editModeChanged, proportional, syncProportional);
    syncProportional();
    auto* topologySelection = editMenu->addMenu(QStringLiteral("组件拓扑选择"));
    for (const auto& [name, text, ring] :
         {std::tuple{"SelectEdgeLoop", "边循环 Loop（Alt+左键）", false},
          std::tuple{"SelectEdgeRing", "边环 Ring（Ctrl+Alt+左键）", true}}) {
        auto* action = topologySelection->addAction(QString::fromUtf8(text));
        action->setObjectName(QString::fromLatin1(name));
        connect(action, &QAction::triggered, viewModel_, [this, ring] {
            if (const auto active = viewModel_->componentSelection().activeId())
                viewModel_->selectEdgePath({active->first, active->second}, ring);
        });
        const auto refresh = [this, action] {
            action->setEnabled(viewModel_->isEditMode() &&
                               viewModel_->componentSelection().domain() == SelectionDomain::Edge &&
                               viewModel_->componentSelection().activeId().has_value());
        };
        connect(viewModel_, &SceneViewModel::componentSelectionChanged, action, refresh);
        refresh();
    }
    auto* linked = topologySelection->addAction(QStringLiteral("连通片（悬停 L / 活动组件）"));
    linked->setObjectName(QStringLiteral("SelectLinkedComponents"));
    const auto refreshLinked = [this, linked] { linked->setEnabled(viewModel_->isEditMode()); };
    connect(viewModel_, &SceneViewModel::editModeChanged, linked, refreshLinked);
    refreshLinked();
    auto* extrude = componentTransforms->addAction(QStringLiteral("区域挤出（安全取消）"));
    extrude->setObjectName(QStringLiteral("ExtrudeRegion"));
    const auto refreshExtrude = [this, extrude] {
        extrude->setEnabled(viewModel_->isEditMode() &&
                            viewModel_->componentSelection().domain() == SelectionDomain::Face &&
                            !viewModel_->componentSelection().selectedIds().empty());
    };
    connect(viewModel_, &SceneViewModel::componentSelectionChanged, extrude, refreshExtrude);
    refreshExtrude();
    auto* inset = componentTransforms->addAction(QStringLiteral("面内插（单个共面凸面）"));
    inset->setObjectName(QStringLiteral("InsetFace"));
    const auto refreshInset = [this, inset] {
        inset->setEnabled(viewModel_->isEditMode() &&
                          viewModel_->componentSelection().domain() == SelectionDomain::Face &&
                          viewModel_->componentSelection().selectedIds().size() == 1);
    };
    connect(viewModel_, &SceneViewModel::componentSelectionChanged, inset, refreshInset);
    refreshInset();
    auto* bevel = componentTransforms->addAction(QStringLiteral("边倒角（单条外凸边 / Ctrl+B）"));
    bevel->setObjectName(QStringLiteral("BevelEdge"));
    const auto refreshBevel = [this, bevel] {
        bevel->setEnabled(viewModel_->isEditMode() &&
                          viewModel_->componentSelection().domain() == SelectionDomain::Edge &&
                          viewModel_->componentSelection().selectedIds().size() == 1);
    };
    connect(viewModel_, &SceneViewModel::componentSelectionChanged, bevel, refreshBevel);
    refreshBevel();
    auto* loopCut = componentTransforms->addAction(QStringLiteral("环切并滑移（单切）"));
    loopCut->setObjectName(QStringLiteral("LoopCut"));
    const auto refreshLoopCut = [this, loopCut] {
        loopCut->setEnabled(viewModel_->isEditMode());
    };
    connect(viewModel_, &SceneViewModel::editModeChanged, loopCut, refreshLoopCut);
    refreshLoopCut();
    auto* fill = componentTransforms->addAction(QStringLiteral("补面（单个共面边界环）"));
    fill->setObjectName(QStringLiteral("FillFaces"));
    connect(fill, &QAction::triggered, viewModel_, &SceneViewModel::fillFace);
    auto* deleteComponents = componentTransforms->addAction(QStringLiteral("删除组件…"));
    deleteComponents->setObjectName(QStringLiteral("DeleteComponentsMenu"));
    const auto refreshTopology = [this, fill, deleteComponents] {
        fill->setEnabled(viewModel_->fillFaceDisabledReason().isEmpty());
        deleteComponents->setEnabled(viewModel_->isEditMode() &&
                                     !viewModel_->componentSelection().selectedIds().empty());
    };
    connect(viewModel_, &SceneViewModel::componentSelectionChanged, fill, refreshTopology);
    connect(viewModel_, &SceneViewModel::editModeChanged, fill, refreshTopology);
    refreshTopology();
    for (const auto& [name, text, domain] :
         {std::tuple{"DeleteVertices", "删除点及关联面", SelectionDomain::Vertex},
          {"DeleteEdges", "删除边及关联面", SelectionDomain::Edge},
          {"DeleteFaces", "删除面（保留仍被使用的边界）", SelectionDomain::Face}}) {
        auto* action = new QAction(QString::fromUtf8(text), this);
        action->setObjectName(QString::fromLatin1(name));
        action->setToolTip(
            QStringLiteral("按指定域投影当前选择，删除后清理孤立点；一次撤销可恢复。"));
        connect(action, &QAction::triggered, viewModel_, [this, domain] {
            viewModel_->deleteComponents(domain);
        });
        const auto refresh = [this, action, domain] {
            action->setEnabled(viewModel_->deleteComponentsDisabledReason(domain).isEmpty());
        };
        connect(viewModel_, &SceneViewModel::componentSelectionChanged, action, refresh);
        connect(viewModel_, &SceneViewModel::editModeChanged, action, refresh);
        refresh();
    }
    for (const auto& [name, text] : {std::pair{"TransformComponentsMove", "移动组件"},
                                     {"TransformComponentsRotate", "旋转组件"},
                                     {"TransformComponentsScale", "缩放组件"}}) {
        auto* action = componentTransforms->addAction(QString::fromUtf8(text));
        action->setObjectName(QString::fromLatin1(name));
        const auto refresh = [this, action] {
            action->setEnabled(viewModel_->isEditMode() &&
                               !viewModel_->componentSelection().selectedIds().empty());
        };
        connect(viewModel_, &SceneViewModel::componentSelectionChanged, action, refresh);
        refresh();
    }
    auto* undoAction = editMenu->addAction(QStringLiteral("撤销"));
    auto* redoAction = editMenu->addAction(QStringLiteral("重做"));
    undoAction->setObjectName(QStringLiteral("Undo"));
    redoAction->setObjectName(QStringLiteral("Redo"));
    undoAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
    redoAction->setShortcuts(
        {QKeySequence(QStringLiteral("Ctrl+Y")), QKeySequence(QStringLiteral("Ctrl+Shift+Z"))});
    for (auto* action : {undoAction, redoAction}) {
        action->setShortcutContext(Qt::WidgetShortcut);
        tree_->addAction(action);
        viewport->addAction(action);
        action->setEnabled(false);
    }
    connect(undoAction, &QAction::triggered, viewModel_, &SceneViewModel::undo);
    connect(redoAction, &QAction::triggered, viewModel_, &SceneViewModel::redo);
    editMenu->addSeparator();
    auto* duplicateAction = editMenu->addAction(QStringLiteral("复制"));
    auto* deleteAction = editMenu->addAction(QStringLiteral("删除"));
    duplicateAction->setObjectName(QStringLiteral("Duplicate"));
    deleteAction->setObjectName(QStringLiteral("Delete"));
    duplicateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    deleteAction->setShortcut(QKeySequence(QStringLiteral("Delete")));
    for (auto* action : {duplicateAction, deleteAction}) {
        action->setShortcutContext(Qt::WidgetShortcut);
        tree_->addAction(action);
        viewport->addAction(action);
        action->setEnabled(false);
        connect(viewModel_->selection(), &SelectionModel::selectedEntityChanged, action,
                [action](core::EntityId id) {
                    action->setEnabled(id != core::kInvalidEntity);
                });
    }
    connect(duplicateAction, &QAction::triggered, viewModel_, &SceneViewModel::duplicateSelected);
    connect(deleteAction, &QAction::triggered, viewModel_, &SceneViewModel::deleteSelected);
    connect(viewModel_->undoStack(), &QUndoStack::canUndoChanged, undoAction, &QAction::setEnabled);
    connect(viewModel_->undoStack(), &QUndoStack::canRedoChanged, redoAction, &QAction::setEnabled);
    connect(focusAction, &QAction::triggered, this, [this, viewport] {
        if (!viewport->focusSelection()) {
            statusBar()->showMessage(
                QStringLiteral("请选择含可见几何的对象；相机预览中请先返回编辑视图。"), 4000);
        }
    });

    auto* createMenu = menuBar()->addMenu(QStringLiteral("创建(&C)"));
    auto* moveAction = viewMenu->addAction(QStringLiteral("移动工具"));
    moveAction->setObjectName(QStringLiteral("MoveTool"));
    moveAction->setCheckable(true);
    moveAction->setShortcut(QKeySequence(QStringLiteral("W")));
    moveAction->setShortcutContext(Qt::WidgetShortcut);
    tree_->addAction(moveAction);
    viewport->addAction(moveAction);
    auto* tools = new QActionGroup(this);
    tools->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    tools->addAction(moveAction);
    connect(moveAction, &QAction::toggled, viewport, [viewport](bool checked) {
        if (checked || viewport->transformTool() == renderer_gl::GizmoTool::Move) {
            viewport->setMoveToolEnabled(checked);
        }
    });
    for (const auto& [name, text, key, tool] :
         {std::tuple{"RotateTool", "旋转工具", "E", renderer_gl::GizmoTool::Rotate},
          std::tuple{"ScaleTool", "缩放工具", "R", renderer_gl::GizmoTool::Scale}}) {
        auto* action = viewMenu->addAction(QString::fromUtf8(text));
        action->setObjectName(QString::fromLatin1(name));
        action->setCheckable(true);
        action->setShortcut(QKeySequence(QString::fromLatin1(key)));
        action->setShortcutContext(Qt::WidgetShortcut);
        tree_->addAction(action);
        viewport->addAction(action);
        tools->addAction(action);
        connect(action, &QAction::toggled, viewport, [viewport, tool](bool checked) {
            if (checked || viewport->transformTool() == tool) {
                viewport->setTransformTool(checked ? tool : renderer_gl::GizmoTool::None);
            }
        });
    }
    auto* spaceMenu = viewMenu->addMenu(QStringLiteral("变换坐标系"));
    auto* snap = viewMenu->addAction(QStringLiteral("启用吸附"));
    snap->setObjectName(QStringLiteral("SnapTransform"));
    snap->setCheckable(true);
    snap->setToolTip(QStringLiteral("Ctrl 临时反转吸附；G 按所选类型，手柄/R/S/E/I "
                                    "保留步进。移动步进 0.5、旋转 15°、缩放 10%；数值输入优先。"));
    connect(snap, &QAction::toggled, viewport, &renderer_gl::ViewportWidget::setSnapEnabled);
    auto* spaces = new QActionGroup(this);
    for (const auto& [name, text, space] :
         {std::tuple{"WorldTransformSpace", "世界坐标", renderer_gl::GizmoSpace::World},
          std::tuple{"LocalTransformSpace", "局部坐标", renderer_gl::GizmoSpace::Local}}) {
        auto* action = spaceMenu->addAction(QString::fromUtf8(text));
        action->setObjectName(QString::fromLatin1(name));
        action->setCheckable(true);
        spaces->addAction(action);
        action->setChecked(space == renderer_gl::GizmoSpace::World);
        connect(action, &QAction::triggered, viewport, [viewport, space] {
            viewport->setTransformSpace(space);
        });
    }
    auto* shadingMenu = viewMenu->addMenu(QStringLiteral("着色"));
    auto* shadingGroup = new QActionGroup(this);
    for (const auto& [name, text, mode] :
         {std::tuple{"ShadingMaterial", "材质（现有预览）", renderer_gl::ViewportShading::Material},
          {"ShadingSolid", "实体 Solid", renderer_gl::ViewportShading::Solid},
          {"ShadingWireframe", "线框 Wireframe", renderer_gl::ViewportShading::Wireframe}}) {
        auto* action = shadingMenu->addAction(QString::fromUtf8(text));
        action->setObjectName(QString::fromLatin1(name));
        action->setCheckable(true);
        shadingGroup->addAction(action);
        action->setChecked(viewport->shadingMode() == mode);
        connect(action, &QAction::triggered, viewport, [viewport, mode] {
            viewport->setShadingMode(mode);
        });
        connect(viewport, &renderer_gl::ViewportWidget::shadingModeChanged, action,
                [viewport, action, mode] {
                    action->setChecked(viewport->shadingMode() == mode);
                });
    }
    auto* shadingPie = viewMenu->addAction(QStringLiteral("着色饼菜单…"));
    shadingPie->setObjectName(QStringLiteral("ShadingPie"));
    auto* viewPie = viewMenu->addAction(QStringLiteral("视图饼菜单…"));
    viewPie->setObjectName(QStringLiteral("ViewPie"));
    auto* directions = viewMenu->addMenu(QStringLiteral("观察方向"));
    for (const auto& [name, text, key, view] :
         {std::tuple{"FrontView", "前视图（沿 -Z）", "1", renderer_gl::EditorView::Front},
          std::tuple{"RightView", "右视图（沿 -X）", "3", renderer_gl::EditorView::Right},
          std::tuple{"TopView", "顶视图（沿 -Y）", "7", renderer_gl::EditorView::Top},
          std::tuple{"OrbitView", "返回自由观察方向", "0", renderer_gl::EditorView::Orbit}}) {
        auto* action = directions->addAction(QString::fromUtf8(text));
        action->setObjectName(QString::fromLatin1(name));
        action->setShortcut(QKeySequence(QString::fromLatin1(key)));
        action->setShortcutContext(Qt::WidgetShortcut);
        tree_->addAction(action);
        viewport->addAction(action);
        connect(action, &QAction::triggered, viewport, [viewport, view] {
            viewport->setCameraView(view);
        });
    }
    auto* orthographic = viewMenu->addAction(QStringLiteral("正交投影（取消为透视）"));
    orthographic->setObjectName(QStringLiteral("OrthographicView"));
    orthographic->setCheckable(true);
    orthographic->setShortcut(QKeySequence(QStringLiteral("5")));
    orthographic->setShortcutContext(Qt::WidgetShortcut);
    tree_->addAction(orthographic);
    viewport->addAction(orthographic);
    connect(orthographic, &QAction::triggered, viewport,
            &renderer_gl::ViewportWidget::setOrthographic);
    connect(viewport, &renderer_gl::ViewportWidget::viewModeChanged, orthographic,
            [viewport, orthographic] {
                const QSignalBlocker blocker(orthographic);
                orthographic->setChecked(viewport->isOrthographic());
            });
    auto* cameraAction = createMenu->addAction(QStringLiteral("相机"));
    cameraAction->setObjectName(QStringLiteral("CreateCamera"));
    connect(cameraAction, &QAction::triggered, viewModel_, [this, viewport] {
        if (const auto transform = viewport->viewTransform()) {
            viewModel_->createCameraFromView(*transform);
        } else {
            viewModel_->createCamera();
        }
    });
    auto* lightAction = createMenu->addAction(QStringLiteral("方向光"));
    lightAction->setObjectName(QStringLiteral("CreateDirectionalLight"));
    connect(lightAction, &QAction::triggered, viewModel_, &SceneViewModel::createDirectionalLight);
    const struct {
        const char* name;
        const char* label;
        core::PrimitiveKind kind;
    } entries[] = {{"Empty", "空对象", core::PrimitiveKind::Empty},
                   {"Cube", "立方体", core::PrimitiveKind::Cube},
                   {"Sphere", "球体", core::PrimitiveKind::Sphere},
                   {"Plane", "平面", core::PrimitiveKind::Plane}};
    for (const auto& [name, label, kind] : entries) {
        auto* action = createMenu->addAction(QString::fromUtf8(label));
        action->setObjectName(QStringLiteral("Create") + QString::fromLatin1(name));
        connect(action, &QAction::triggered, this, [this, kind] {
            viewModel_->createEntity(kind);
        });
    }
    auto* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto* guide = helpMenu->addAction(QStringLiteral("使用手册与兼容性说明"));
    guide->setObjectName(QStringLiteral("OpenUserGuide"));
    guide->setShortcut(QKeySequence::HelpContents);
    guide->setShortcutContext(Qt::WindowShortcut);
    const auto packagedGuide = QDir(QApplication::applicationDirPath())
                                   .filePath(QStringLiteral("docs/Mini3D_使用手册.html"));
    const auto developmentGuide = QDir(QString::fromUtf8(MINI3D_DOCUMENTATION_DIRECTORY))
                                      .filePath(QStringLiteral("Mini3D_使用手册.html"));
    guide->setData(
        QUrl::fromLocalFile(QFileInfo::exists(packagedGuide) ? packagedGuide : developmentGuide));
    connect(guide, &QAction::triggered, this, [this, guide] {
        viewModel_->cancelTransformEdit();
        const auto url = guide->data().toUrl();
        if (!QFileInfo::exists(url.toLocalFile()) || !QDesktopServices::openUrl(url)) {
            emit viewModel_->operationFailed(
                QStringLiteral("无法打开离线使用手册，请保留程序旁的 docs 与截图目录。"));
        }
    });
}

void MainWindow::findNextEntity(const QString& text) {
    if (text.trimmed().isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请输入要查找的对象名称。"), 4000);
        return;
    }
    const auto matches =
        treeModel_->match(treeModel_->index(0, 0), Qt::DisplayRole, text.trimmed(), -1,
                          Qt::MatchContains | Qt::MatchRecursive | Qt::MatchWrap);
    if (matches.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("没有找到匹配的对象。"), 4000);
        return;
    }
    const auto current = treeModel_->indexForEntity(viewModel_->selection()->selectedEntity());
    const auto next = (matches.indexOf(current) + 1) % matches.size();
    const auto index = matches[next];
    for (auto parent = index.parent(); parent.isValid(); parent = parent.parent()) {
        tree_->expand(parent);
    }
    viewModel_->selection()->setSelectedEntity(treeModel_->entityId(index));
    tree_->scrollTo(index);
    statusBar()->showMessage(
        QStringLiteral("已定位第 %1 / %2 个匹配对象。").arg(next + 1).arg(matches.size()), 4000);
}

void MainWindow::showSceneContextMenu(const QPoint& position) {
    const auto index = tree_->indexAt(position);
    const auto id = treeModel_->entityId(index);
    const auto* node = viewModel_->scene()->find(id);
    if (!node) {
        return;
    }
    viewModel_->selection()->setSelectedEntity(id);
    auto* menu = new QMenu(tree_);
    menu->setObjectName(QStringLiteral("SceneContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    connect(viewModel_, &SceneViewModel::structureAboutToChange, menu, &QWidget::close);
    for (const auto& [name, text] :
         {std::pair{"TreeRename", "重命名"}, std::pair{"TreeDuplicate", "复制"},
          std::pair{"TreeDelete", "删除"},
          std::pair{"TreeVisibility", node->visible ? "隐藏" : "显示"},
          std::pair{"TreeToRoot", "移至根层"}, std::pair{"TreeFocus", "聚焦对象"}}) {
        auto* action = menu->addAction(QString::fromUtf8(text));
        action->setObjectName(QString::fromLatin1(name));
        if (action->objectName() == QStringLiteral("TreeToRoot")) {
            action->setEnabled(node->parent != 0);
        }
    }
    connect(menu, &QMenu::triggered, this, [this, id](QAction* action) {
        if (!viewModel_->scene()->find(id)) {
            return;
        }
        viewModel_->selection()->setSelectedEntity(id);
        const auto name = action->objectName();
        if (name == QStringLiteral("TreeRename")) {
            tree_->edit(treeModel_->indexForEntity(id));
        } else if (name == QStringLiteral("TreeDuplicate")) {
            viewModel_->duplicateSelected();
        } else if (name == QStringLiteral("TreeDelete")) {
            viewModel_->deleteSelected();
        } else if (name == QStringLiteral("TreeVisibility")) {
            viewModel_->setVisible(id, !viewModel_->scene()->find(id)->visible);
        } else if (name == QStringLiteral("TreeToRoot")) {
            viewModel_->setParent(id, 0);
        } else if (name == QStringLiteral("TreeFocus")) {
            findChild<QAction*>(QStringLiteral("FocusSelection"))->trigger();
        }
    });
    menu->popup(tree_->viewport()->mapToGlobal(position));
}

void MainWindow::synchronizeTreeSelection() {
    const QSignalBlocker blocker(tree_->selectionModel());
    const auto index = treeModel_->indexForEntity(viewModel_->selection()->selectedEntity());
    tree_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect |
                                                        QItemSelectionModel::Rows);
    if (index.isValid()) {
        tree_->scrollTo(index);
    }
}

void MainWindow::refreshDocumentTitle() {
    const auto name = viewModel_->filePath().isEmpty()
                          ? QStringLiteral("未命名")
                          : QFileInfo(viewModel_->filePath()).fileName();
    setWindowTitle(name + QStringLiteral("[*] - Mini3D Studio"));
    setWindowModified(viewModel_->isModified());
}
bool MainWindow::saveScene(bool saveAs) {
    viewModel_->cancelTransformEdit();
    if (auto* focused = QApplication::focusWidget(); focused && isAncestorOf(focused)) {
        focused->clearFocus();
    }
    auto path = viewModel_->filePath();
    const bool upgrade = viewModel_->requiresSaveAs();
    if (upgrade) {
        const QFileInfo original(path);
        path = original.absolutePath() + "/" + original.completeBaseName() + "-v4.m3dscene";
    }
    if (saveAs || path.isEmpty() || upgrade) {
        QFileDialog dialog(this,
                           upgrade ? QStringLiteral("升级场景：另存新文件，保留旧版原件")
                                   : QStringLiteral("保存场景"),
                           path, QStringLiteral("Mini3D 场景 (*.m3dscene)"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setOption(QFileDialog::DontUseNativeDialog);
        dialog.setFileMode(QFileDialog::AnyFile);
        dialog.setDefaultSuffix(QStringLiteral("m3dscene"));
        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }
        path = dialog.selectedFiles().front();
    }
    return viewModel_->saveScene(path);
}
bool MainWindow::confirmDiscardChanges() {
    viewModel_->cancelTransformEdit();
    if (auto* focused = QApplication::focusWidget(); focused && isAncestorOf(focused)) {
        focused->clearFocus();
    }
    if (!viewModel_->isModified()) {
        return true;
    }
    const auto choice = QMessageBox::warning(
        this, QStringLiteral("未保存的更改"), QStringLiteral("是否保存更改后继续？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    return choice == QMessageBox::Discard || saveScene(false);
}
void MainWindow::applyReferenceDockSizes() {
    auto* scene = findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* inspector = findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    auto* console = findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
    const auto workHeight = height() - menuBar()->height() - statusBar()->sizeHint().height();
    resizeDocks({inspector}, {std::max(kMinimumInspectorDockWidth, qRound(width() * 0.18))},
                Qt::Horizontal);
    resizeDocks({scene, inspector}, {qRound(workHeight * 0.19), qRound(workHeight * 0.81)},
                Qt::Vertical);
    resizeDocks({console}, {qRound(workHeight * 0.07)}, Qt::Vertical);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    const auto available = screen()->availableGeometry();
    const auto margins = windowHandle()->frameMargins();
    const QSize clientLimit(available.width() - margins.left() - margins.right(),
                            available.height() - margins.top() - margins.bottom());
    resize(size().boundedTo(clientLimit).expandedTo(minimumSize()));
    if (!available.contains(frameGeometry()))
        move(available.topLeft());
    if (!initialLayoutApplied_) {
        initialLayoutApplied_ = true;
        // 只在初显时选择默认或原偏好；普通 resize 和再次 show 均不覆盖用户布局。
        QTimer::singleShot(0, this, [this] {
            bool savedLayout = false;
            if (!QCoreApplication::organizationName().isEmpty()) {
                QSettings settings;
                for (const auto* workspace : {"layout", "modeling", "review"})
                    savedLayout = savedLayout || settings.contains(
                        QStringLiteral("workbench/v2/%1/docks").arg(QString::fromLatin1(workspace)));
                if (savedLayout)
                    workspaces_->restorePreferences(settings);
                favorites_->restorePreferences(settings);
            }
            if (!savedLayout) {
                applyReferenceDockSizes();
                workspaces_->initializeDefaultDockLayout();
            }
        });
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    closing_ = true;
    if (confirmDiscardChanges()) {
        if (!QCoreApplication::organizationName().isEmpty()) {
            QSettings settings;
            workspaces_->savePreferences(settings);
            favorites_->savePreferences(settings);
            findChild<KeymapRouter*>()->savePreferences(settings);
        }
        event->accept();
    } else {
        closing_ = false;
        event->ignore();
    }
}
void MainWindow::hideEvent(QHideEvent* event) {
    viewModel_->suspendAnimation();
    QMainWindow::hideEvent(event);
}
void MainWindow::changeEvent(QEvent* event) {
    if (viewModel_ && event->type() == QEvent::WindowStateChange && isMinimized())
        viewModel_->suspendAnimation();
    QMainWindow::changeEvent(event);
}
} // namespace mini3d::editor
