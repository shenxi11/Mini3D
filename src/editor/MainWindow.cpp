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
#include "SceneTreeModel.h"
#include "TransformInspector.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <tuple>

namespace mini3d::editor {
namespace {

constexpr int kInitialWindowWidth = 1440;
constexpr int kInitialWindowHeight = 900;
constexpr int kMinimumWindowWidth = 960;
constexpr int kMinimumWindowHeight = 640;
constexpr int kSceneDockWidth = 260;
constexpr int kInspectorDockWidth = 320;
constexpr int kConsoleDockHeight = 180;

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

    viewModel_ = new SceneViewModel(this);
    setCentralWidget(createViewport());

    auto* sceneDock = createSceneDock();
    auto* inspectorDock = createInspectorDock();
    auto* consoleDock = createConsoleDock();

    addDockWidget(Qt::LeftDockWidgetArea, sceneDock);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);

    resizeDocks({sceneDock, inspectorDock}, {kSceneDockWidth, kInspectorDockWidth}, Qt::Horizontal);
    resizeDocks({consoleDock}, {kConsoleDockHeight}, Qt::Vertical);

    createMenus(sceneDock, inspectorDock, consoleDock);
    connect(viewModel_, &SceneViewModel::documentChanged, this, &MainWindow::refreshDocumentTitle);
    refreshDocumentTitle();
    connect(viewModel_->selection(), &SelectionModel::selectedEntityChanged, this, [this] {
        synchronizeTreeSelection();
    });
    connect(viewModel_, &SceneViewModel::sceneChanged, this, [this] {
        synchronizeTreeSelection();
        centralWidget()->update();
    });
    connect(viewModel_, &SceneViewModel::operationFailed, this, [this](const QString& message) {
        statusBar()->showMessage(message, 6000);
    });
    connect(viewModel_, &SceneViewModel::operationCompleted, this, [this](const QString& message) {
        statusBar()->showMessage(message.section('\n', 0, 0), 6000);
    });
    statusBar()->showMessage(QStringLiteral("就绪"));
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
    connect(viewport, &renderer_gl::ViewportWidget::cameraChanged, viewModel_,
            &SceneViewModel::setEditorCamera);
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
    return viewport;
}

QDockWidget* MainWindow::createSceneDock() {
    auto* dock = new QDockWidget(QStringLiteral("场景"), this);
    dock->setObjectName(QStringLiteral("SceneDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    tree_ = new QTreeView(dock);
    tree_->setObjectName(QStringLiteral("SceneTree"));
    tree_->setHeaderHidden(true);
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
    auto* content = new QWidget(dock);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* search = new QLineEdit(content);
    search->setObjectName(QStringLiteral("SceneSearch"));
    search->setPlaceholderText(QStringLiteral("搜索对象名称，回车定位下一个"));
    search->setClearButtonEnabled(true);
    auto* next = new QPushButton(QStringLiteral("查找下一个"), content);
    next->setObjectName(QStringLiteral("FindNextEntity"));
    connect(search, &QLineEdit::returnPressed, this, [this, search] {
        findNextEntity(search->text());
    });
    connect(next, &QPushButton::clicked, this, [this, search] {
        findNextEntity(search->text());
    });
    layout->addWidget(search);
    layout->addWidget(next);
    layout->addWidget(tree_);
    dock->setWidget(content);
    return dock;
}

QDockWidget* MainWindow::createInspectorDock() {
    auto* dock = new QDockWidget(QStringLiteral("属性"), this);
    dock->setObjectName(QStringLiteral("InspectorDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget(dock);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new TransformInspector(*viewModel_, content));
    layout->addWidget(new AppearanceInspector(*viewModel_, content));
    layout->addStretch();
    auto* scroll = new QScrollArea(dock);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    // 滚动容器默认会忽略内容最小宽度，必须为三轴数值行保留完整编辑空间。
    scroll->setMinimumWidth(content->minimumSizeHint().width() +
                            scroll->verticalScrollBar()->sizeHint().width() +
                            2 * scroll->frameWidth());
    dock->setWidget(scroll);

    return dock;
}

QDockWidget* MainWindow::createConsoleDock() {
    auto* dock = new QDockWidget(QStringLiteral("控制台"), this);
    dock->setObjectName(QStringLiteral("ConsoleDock"));
    dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    auto* console = new QPlainTextEdit(dock);
    console->setObjectName(QStringLiteral("ConsoleOutput"));
    console->setReadOnly(true);
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
    auto* exitAction = fileMenu->addAction(QStringLiteral("退出(&X)"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(sceneDock->toggleViewAction());
    viewMenu->addAction(inspectorDock->toggleViewAction());
    viewMenu->addAction(consoleDock->toggleViewAction());
    viewMenu->addSeparator();
    auto* focusAction = viewMenu->addAction(QStringLiteral("聚焦所选对象"));
    focusAction->setObjectName(QStringLiteral("FocusSelection"));
    focusAction->setShortcut(QKeySequence(QStringLiteral("F")));
    // 只在树/视口本身有焦点时响应，避免抢走属性或重命名输入框中的 F。
    focusAction->setShortcutContext(Qt::WidgetShortcut);
    tree_->addAction(focusAction);
    auto* viewport = qobject_cast<renderer_gl::ViewportWidget*>(centralWidget());
    viewport->addAction(focusAction);
    auto* editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
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
    auto* snap = viewMenu->addAction(QStringLiteral("步进吸附（移动 0.5 / 旋转 15° / 缩放 10%）"));
    snap->setObjectName(QStringLiteral("SnapTransform"));
    snap->setCheckable(true);
    snap->setToolTip(QStringLiteral("也可在拖动手柄时按住 Ctrl 临时吸附；步进相对拖动起点。"));
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
    connect(viewModel_, &SceneViewModel::structureChanged, menu, &QWidget::close);
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
    if (saveAs || path.isEmpty()) {
        QFileDialog dialog(this, QStringLiteral("保存场景"), path,
                           QStringLiteral("Mini3D 场景 (*.m3dscene)"));
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
void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscardChanges()) {
        event->accept();
    } else {
        event->ignore();
    }
}
} // namespace mini3d::editor
