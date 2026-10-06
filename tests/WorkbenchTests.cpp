/*
 * 模块名: WorkbenchTests
 * 功能概述: 验证工作台区域布局、真实视口生命周期和菜单/工具条业务等价。
 * 对外接口: Catch2 [workbench] 用例
 * 依赖关系: Qt Test、MainWindow、ViewportWidget
 * 输入输出: 真实窗口输入到布局、选择、历史和帧缓冲断言。
 * 异常与错误: 面板穿透、Context 重建或旧业务退化即测试失败。
 * 维护说明: 截图来自当前 C++ 窗口，不读取桌面或使用 HTML 原型。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/workbench/WorkbenchShell.h"
#include "editor/workbench/WorkspaceManager.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QOpenGLContext>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QWindow>
#include <QWheelEvent>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;

namespace {
QSize resizeWithinScreen(editor::MainWindow& window, const QSize& requestedSize) {
    const auto available = window.screen()->availableGeometry();
    const auto margins = window.windowHandle()->frameMargins();
    const QSize targetSize(std::min(requestedSize.width(),
                                   available.width() - margins.left() - margins.right()),
                           std::min(requestedSize.height(),
                                   available.height() - margins.top() - margins.bottom()));
    REQUIRE(targetSize.width() >= window.minimumWidth());
    REQUIRE(targetSize.height() >= window.minimumHeight());
    window.resize(targetSize);
    window.move(available.topLeft());
    QTest::qWait(40);
    return targetSize;
}

double logViewportArea(editor::MainWindow& window, renderer_gl::ViewportWidget& viewport,
                       const QSize& requestedSize, const QSize& targetSize, const QString& phase) {
    const auto top = window.menuBar()->geometry().bottom() + 1;
    const QRect workArea(0, top, window.width(), window.statusBar()->geometry().top() - top);
    const QRect viewportArea(viewport.mapTo(&window, QPoint()), viewport.size());
    REQUIRE(workArea.contains(viewportArea));
    const double ratio = static_cast<double>(viewportArea.width()) * viewportArea.height() /
                         (static_cast<double>(workArea.width()) * workArea.height());
    qInfo() << "viewport-layout phase" << phase << "window" << window.size() << "work-area" << workArea
            << "actual-GL-viewport" << viewportArea << "area-ratio" << ratio * 100 << "%";
    qInfo() << "viewport-layout requested" << requestedSize << "screen-target" << targetSize
            << "geometry" << window.geometry()
            << "native-window" << window.windowHandle()->geometry() << "frame"
            << window.frameGeometry() << "frame-margins" << window.windowHandle()->frameMargins()
            << "screen" << window.screen()->geometry() << "available"
            << window.screen()->availableGeometry() << "minimum" << window.minimumSize()
            << "maximum" << window.maximumSize();
    const auto windowDpr = window.devicePixelRatioF();
    const auto viewportDpr = viewport.devicePixelRatioF();
    const auto framebuffer = viewport.grabFramebuffer();
    const auto requestedScale = qEnvironmentVariable("QT_SCALE_FACTOR");
    auto path = qEnvironmentVariable("MINI3D_TEST_BLENDER_ARTIFACT_DIR");
    if (path.isEmpty())
        path = qEnvironmentVariable("MINI3D_TEST_GRID_ARTIFACT_DIR");
    if (!path.isEmpty()) {
        const auto rectJson = [](const QRect& rect) {
            return QJsonObject{{QStringLiteral("x"), rect.x()},
                               {QStringLiteral("y"), rect.y()},
                               {QStringLiteral("width"), rect.width()},
                               {QStringLiteral("height"), rect.height()}};
        };
        const auto widgetRect = [&window](QWidget* widget) {
            return QRect(widget->mapTo(&window, QPoint()), widget->size());
        };
        auto* scene = window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
        auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
        auto* console = window.findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
        auto* consoleOutput = window.findChild<QPlainTextEdit*>(QStringLiteral("ConsoleOutput"));
        auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("ViewportToolbar"));
        const auto columnHeight = inspector->geometry().bottom() - scene->geometry().top() + 1;
        const QJsonObject metrics{
            {QStringLiteral("phase"), phase},
            {QStringLiteral("requestedSize"), rectJson(QRect(QPoint(), requestedSize))},
            {QStringLiteral("targetSize"), rectJson(QRect(QPoint(), targetSize))},
            {QStringLiteral("window"), rectJson(window.rect())},
            {QStringLiteral("geometry"), rectJson(window.geometry())},
            {QStringLiteral("nativeWindow"), rectJson(window.windowHandle()->geometry())},
            {QStringLiteral("frame"), rectJson(window.frameGeometry())},
            {QStringLiteral("screen"), rectJson(window.screen()->geometry())},
            {QStringLiteral("availableScreen"), rectJson(window.screen()->availableGeometry())},
            {QStringLiteral("minimumSize"), rectJson(QRect(QPoint(), window.minimumSize()))},
            {QStringLiteral("maximumSize"), rectJson(QRect(QPoint(), window.maximumSize()))},
            {QStringLiteral("transformSettingsVisible"),
             !window.findChild<QToolBar*>(QStringLiteral("TransformSettingsBar"))->isHidden()},
            {QStringLiteral("toolSettings"),
             rectJson(window.findChild<QLabel*>(QStringLiteral("ToolSettings"))->geometry())},
            {QStringLiteral("frameMargins"),
             QJsonObject{{QStringLiteral("left"), window.windowHandle()->frameMargins().left()},
                         {QStringLiteral("top"), window.windowHandle()->frameMargins().top()},
                         {QStringLiteral("right"), window.windowHandle()->frameMargins().right()},
                         {QStringLiteral("bottom"), window.windowHandle()->frameMargins().bottom()}}},
            {QStringLiteral("menu"), rectJson(window.menuBar()->geometry())},
            {QStringLiteral("status"), rectJson(window.statusBar()->geometry())},
            {QStringLiteral("workArea"), rectJson(workArea)},
            {QStringLiteral("viewport"), rectJson(viewportArea)},
            {QStringLiteral("scene"), rectJson(widgetRect(scene))},
            {QStringLiteral("properties"), rectJson(widgetRect(inspector))},
            {QStringLiteral("console"), rectJson(widgetRect(console))},
            {QStringLiteral("consoleBody"), rectJson(widgetRect(consoleOutput->viewport()))},
            {QStringLiteral("consoleLineSpacing"), consoleOutput->fontMetrics().lineSpacing()},
            {QStringLiteral("consoleDocumentMargin"), consoleOutput->document()->documentMargin()},
            {QStringLiteral("consoleFrameWidth"), consoleOutput->frameWidth()},
            {QStringLiteral("toolbar"), rectJson(widgetRect(toolbar))},
            {QStringLiteral("workspaceBar"),
             rectJson(widgetRect(window.findChild<QWidget*>(QStringLiteral("WorkspaceBar"))))},
            {QStringLiteral("header"),
             rectJson(widgetRect(window.findChild<QWidget*>(QStringLiteral("ViewportHeader"))))},
            {QStringLiteral("rightColumnWidthFraction"),
             static_cast<double>(inspector->width()) / window.width()},
            {QStringLiteral("outlinerHeightFraction"),
             static_cast<double>(scene->height()) / columnHeight},
            {QStringLiteral("consoleHeightFraction"),
             static_cast<double>(console->height()) / workArea.height()},
            {QStringLiteral("toolbarInsideViewport"),
             toolbar->parentWidget() == &viewport && viewport.rect().contains(toolbar->geometry())},
            {QStringLiteral("ratio"), ratio},
            {QStringLiteral("areaPercent"), ratio * 100},
            {QStringLiteral("windowDpr"), windowDpr},
            {QStringLiteral("viewportDpr"), viewportDpr},
            {QStringLiteral("requestedQtScaleFactor"), requestedScale},
            {QStringLiteral("framebufferPhysical"),
             QJsonObject{{QStringLiteral("width"), framebuffer.width()},
                         {QStringLiteral("height"), framebuffer.height()}}}};
        QDir directory(path);
        REQUIRE(directory.mkpath(QStringLiteral(".")));
        const bool reference = phase == QStringLiteral("reference-applied");
        const auto jsonPrefix = reference ? QStringLiteral("layout") : phase;
        const auto imagePrefix = reference ? QStringLiteral("blender-layout") : phase;
        QFile output(directory.filePath(QStringLiteral("%1-%2x%3.json")
                                            .arg(jsonPrefix)
                                            .arg(window.width())
                                            .arg(window.height())));
        REQUIRE(output.open(QIODevice::WriteOnly));
        const auto bytes = QJsonDocument(metrics).toJson(QJsonDocument::Indented);
        REQUIRE(output.write(bytes) == bytes.size());
        REQUIRE(output.flush());
        REQUIRE(window.grab().save(directory.filePath(QStringLiteral("%1-%2x%3.png")
                                                         .arg(imagePrefix)
                                                         .arg(window.width())
                                                         .arg(window.height()))));
    }
    REQUIRE_FALSE(framebuffer.isNull());
    REQUIRE(window.size() == targetSize);
    REQUIRE(window.windowHandle()->size() == targetSize);
    REQUIRE(window.screen()->availableGeometry().contains(window.frameGeometry()));
    REQUIRE(framebuffer.size() == QSize(qRound(viewport.width() * viewportDpr),
                                       qRound(viewport.height() * viewportDpr)));
    REQUIRE(qFuzzyCompare(windowDpr, viewportDpr));
    if (!requestedScale.isEmpty()) {
        bool validScale = false;
        const auto expectedDpr = requestedScale.toDouble(&validScale);
        REQUIRE(validScale);
        INFO("requested QT_SCALE_FACTOR = " << expectedDpr << ", actual window DPR = "
                                            << windowDpr << ", actual viewport DPR = "
                                            << viewportDpr);
        REQUIRE(qFuzzyCompare(windowDpr, expectedDpr));
        REQUIRE(qFuzzyCompare(viewportDpr, expectedDpr));
    }
    return ratio;
}

void requireReadableConsole(editor::MainWindow& window) {
    auto* console = window.findChild<QPlainTextEdit*>(QStringLiteral("ConsoleOutput"));
    REQUIRE(console);
    REQUIRE(console->isVisible());
    const auto lineHeight = console->fontMetrics().lineSpacing();
    const auto margin = console->document()->documentMargin();
    INFO("console viewport height=" << console->viewport()->height() << ", line height="
                                    << lineHeight << ", document margin=" << margin);
    REQUIRE(margin == 1);
    REQUIRE(console->viewport()->height() >= lineHeight + 2 * margin);
}

void requireReferenceRegions(editor::MainWindow& window) {
    auto* scene = window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    auto* console = window.findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
    REQUIRE(scene->isVisible());
    REQUIRE(inspector->isVisible());
    REQUIRE(console->isVisible());
    REQUIRE(window.dockWidgetArea(scene) == Qt::RightDockWidgetArea);
    REQUIRE(window.dockWidgetArea(inspector) == Qt::RightDockWidgetArea);
    REQUIRE(scene->geometry().bottom() < inspector->geometry().top());
    REQUIRE(console->geometry().right() < inspector->geometry().left());
    const auto rightWidth = static_cast<double>(inspector->width()) / window.width();
    const auto columnHeight = inspector->geometry().bottom() - scene->geometry().top() + 1;
    const auto sceneHeight = static_cast<double>(scene->height()) / columnHeight;
    const auto workHeight = window.statusBar()->geometry().top() -
                            window.menuBar()->geometry().bottom() - 1;
    const auto consoleHeight = static_cast<double>(console->height()) / workHeight;
    INFO("right width=" << rightWidth << ", scene height=" << sceneHeight
                        << ", console height=" << consoleHeight);
    REQUIRE(rightWidth >= 0.16);
    REQUIRE(rightWidth <= 0.20);
    REQUIRE(sceneHeight >= 0.15);
    REQUIRE(sceneHeight <= 0.24);
    REQUIRE(consoleHeight >= 0.05);
    REQUIRE(consoleHeight <= 0.11);
    requireReadableConsole(window);
}

void clickPanelAction(QToolButton& button, QAction& action) {
    auto* menu = button.menu();
    REQUIRE(menu->actions().contains(&action));
    QTimer::singleShot(0, menu, [menu, &action] {
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(&action).center());
    });
    QTest::mouseClick(&button, Qt::LeftButton);
    QTest::qWait(30);
}
} // namespace

TEST_CASE("Reference layout uses a narrow right column and thin output around the actual GL viewport",
          "[workbench][viewport-layout]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    window.findChild<QAction*>(QStringLiteral("RestoreDefaultViewportLayout"))->trigger();
    model->newScene();
    model->createEntity(core::PrimitiveKind::Cube);
    const auto selected = model->selection()->selectedEntity();
    const auto history = model->undoStack()->index();
    const auto dirty = model->isModified();
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    auto* context = viewport->context();
    REQUIRE(context);
    QSignalSpy destroyed(context, &QOpenGLContext::aboutToBeDestroyed);
    REQUIRE(host->isToolbarVisible());
    REQUIRE_FALSE(host->isSidebarVisible());
    for (const auto* name : {"SceneDock", "InspectorDock", "ConsoleDock"})
        REQUIRE(window.findChild<QDockWidget*>(QString::fromLatin1(name))->isVisible());
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("ViewportToolbar"));
    REQUIRE(toolbar->parentWidget() == viewport);
    REQUIRE(toolbar->width() <= 40);
    REQUIRE(host->workspaceBar()->parentWidget() == window.menuBar());
    REQUIRE(window.menuBar()->height() <= 30);
    auto* panels = window.findChild<QToolButton*>(QStringLiteral("ViewportPanelsButton"));
    REQUIRE(panels->isVisible());
    for (const QSize size : {QSize(1440, 900), QSize(1280, 800), QSize(960, 640)}) {
        const auto target = resizeWithinScreen(window, size);
        window.findChild<QAction*>(QStringLiteral("RestoreDefaultViewportLayout"))->trigger();
        QTest::qWait(40);
        requireReferenceRegions(window);
        const auto ratio = logViewportArea(window, *viewport, size, target,
                                           QStringLiteral("reference-applied"));
        INFO("actual GL viewport / main work area = " << ratio);
        REQUIRE(ratio >= 0.64);
        REQUIRE(ratio <= 0.80);
        REQUIRE(viewport->rect().contains(toolbar->geometry()));
        REQUIRE(viewport->width() >= 600);
        REQUIRE(viewport->height() >= 300);
        REQUIRE(panels->isVisible());
        REQUIRE(viewport->context() == context);
        REQUIRE(destroyed.isEmpty());
        REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
        REQUIRE(model->selection()->selectedEntity() == selected);
        REQUIRE(model->undoStack()->index() == history);
        REQUIRE(model->isModified() == dirty);
        const auto path = qEnvironmentVariable("MINI3D_TEST_GRID_ARTIFACT_DIR");
        if (!path.isEmpty()) {
            QDir directory(path);
            REQUIRE(directory.mkpath(QStringLiteral(".")));
            REQUIRE(window.grab().save(directory.filePath(
                QStringLiteral("viewport-layout-%1x%2.png")
                    .arg(window.width())
                    .arg(window.height()))));
        }
    }
    window.hide();
}

TEST_CASE("Compact transform settings keep feedback visible and restore their row through menus",
          "[workbench][viewport-layout]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(40);
    REQUIRE(window.size() == window.windowHandle()->size());
    REQUIRE(window.screen()->availableGeometry().contains(window.frameGeometry()));
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* settings = window.findChild<QToolBar*>(QStringLiteral("TransformSettingsBar"));
    auto* feedback = window.findChild<QLabel*>(QStringLiteral("ToolSettings"));
    auto* compact = window.findChild<QToolButton*>(QStringLiteral("CompactTransformSettingsButton"));
    auto* panels = window.findChild<QToolButton*>(QStringLiteral("ViewportPanelsButton"));
    auto* restore = window.findChild<QAction*>(QStringLiteral("RestoreDefaultViewportLayout"));
    auto* toggle = window.findChild<QAction*>(QStringLiteral("ToggleTransformSettingsBar"));
    auto* local = window.findChild<QAction*>(QStringLiteral("LocalTransformSpace"));
    restore->trigger();
    window.findChild<QAction*>(QStringLiteral("WorldTransformSpace"))->trigger();
    model->newScene();
    model->createEntity(core::PrimitiveKind::Cube);
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(model->saveScene(directory.filePath(QStringLiteral("compact-feedback.m3dscene"))));
    const auto selected = model->selection()->selectedEntity();
    const auto history = model->undoStack()->index();
    const auto count = model->undoStack()->count();
    const auto dirty = model->isModified();
    REQUIRE_FALSE(dirty);
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    auto* context = viewport->context();
    REQUIRE(context);
    QSignalSpy destroyed(context, &QOpenGLContext::aboutToBeDestroyed);
    const auto verifyEditing = [&] {
        REQUIRE(feedback->isVisible());
        REQUIRE(feedback->width() >= 100);
        REQUIRE(viewport->context() == context);
        REQUIRE(destroyed.isEmpty());
        REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
        REQUIRE(model->selection()->selectedEntity() == selected);
        REQUIRE(model->undoStack()->index() == history);
        REQUIRE(model->undoStack()->count() == count);
        REQUIRE(model->isModified() == dirty);
    };
    const auto capture = [&window](const QString& phase) {
        const auto path = qEnvironmentVariable("MINI3D_TEST_GRID_ARTIFACT_DIR");
        if (!path.isEmpty()) {
            QDir artifacts(path);
            REQUIRE(artifacts.mkpath(QStringLiteral(".")));
            REQUIRE(window.grab().save(artifacts.filePath(QStringLiteral("viewport-settings-%1-%2x%3.png")
                                                             .arg(phase)
                                                             .arg(window.width())
                                                             .arg(window.height()))));
        }
    };
    const auto shortTarget = resizeWithinScreen(window, QSize(960, 640));
    logViewportArea(window, *viewport, QSize(960, 640), shortTarget,
                    QStringLiteral("compact-resize-short"));
    REQUIRE(settings->isHidden());
    REQUIRE(compact->isVisible());
    REQUIRE(feedback->text().contains(QStringLiteral("当前工具")));
    REQUIRE(feedback->geometry().top() >= viewport->mapTo(host, QPoint(0, viewport->height())).y());
    const auto text = feedback->text();
    verifyEditing();
    capture(QStringLiteral("compact"));
    clickPanelAction(*compact, *local);
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::Local);
    REQUIRE(local->isChecked());
    verifyEditing();
    clickPanelAction(*compact, *toggle);
    REQUIRE(settings->isVisible());
    REQUIRE(compact->isHidden());
    REQUIRE(toggle->isChecked());
    REQUIRE(feedback->text() == text);
    REQUIRE(settings->geometry().top() <= feedback->geometry().center().y());
    REQUIRE(settings->geometry().bottom() >= feedback->geometry().center().y());
    verifyEditing();
    capture(QStringLiteral("expanded"));
    clickPanelAction(*panels, *restore);
    REQUIRE(settings->isHidden());
    REQUIRE(compact->isVisible());
    REQUIRE_FALSE(toggle->isChecked());
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::Local);
    verifyEditing();
    capture(QStringLiteral("restored"));
    const auto tallTarget = resizeWithinScreen(window, QSize(1440, 900));
    logViewportArea(window, *viewport, QSize(1440, 900), tallTarget,
                    QStringLiteral("compact-resize-tall"));
    REQUIRE(settings->isHidden());
    REQUIRE(compact->isVisible());
    REQUIRE_FALSE(toggle->isChecked());
    clickPanelAction(*compact, *toggle);
    REQUIRE(settings->isVisible());
    REQUIRE(window.findChild<QWidget*>(QStringLiteral("ViewportHeader"))->geometry().bottom() <
            settings->geometry().top());
    resizeWithinScreen(window, QSize(960, 640));
    REQUIRE(settings->isVisible());
    REQUIRE(compact->isHidden());
    REQUIRE(toggle->isChecked());
    capture(QStringLiteral("manual-setting-retained"));
    verifyEditing();
    window.hide();
}

TEST_CASE("New workspaces capture actual reference proportions and retain a resized user column",
          "[workbench][viewport-layout]") {
    editor::MainWindow window;
    window.resize(960, 640);
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(40);
    auto* manager = window.findChild<editor::WorkspaceManager*>();
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    auto* context = viewport->context();
    REQUIRE(context);
    QSignalSpy destroyed(context, &QOpenGLContext::aboutToBeDestroyed);
    const auto dirty = model->isModified();
    const auto history = model->undoStack()->index();
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    QSettings emptyPreferences(directory.filePath(QStringLiteral("new-layout.ini")),
                               QSettings::IniFormat);
    manager->restorePreferences(emptyPreferences);
    for (const auto index : {1, 2, 0}) {
        manager->setWorkspace(index);
        QTest::qWait(30);
        requireReferenceRegions(window);
        REQUIRE(host->isToolbarVisible() == (index != 2));
        REQUIRE(host->isSidebarVisible() == (index == 1));
        REQUIRE(viewport->context() == context);
        REQUIRE(destroyed.isEmpty());
    }
    resizeWithinScreen(window, QSize(1280, 800));
    window.resizeDocks({inspector}, {360}, Qt::Horizontal);
    QTest::qWait(30);
    resizeWithinScreen(window, QSize(960, 640));
    REQUIRE(inspector->width() > window.width() * 0.25);
    REQUIRE(model->isModified() == dirty);
    REQUIRE(model->undoStack()->index() == history);
    REQUIRE(viewport->context() == context);
    REQUIRE(destroyed.isEmpty());
    window.hide();
}

TEST_CASE("Viewport tool overlay owns its buttons background and wheel without scene input",
          "[workbench][viewport-layout]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    window.findChild<QAction*>(QStringLiteral("RestoreDefaultViewportLayout"))->trigger();
    QTest::qWait(40);
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("ViewportToolbar"));
    auto* move = window.findChild<QAction*>(QStringLiteral("MoveTool"));
    auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(move));
    const auto entity = model->createEntity(core::PrimitiveKind::Cube);
    const auto history = model->undoStack()->index();
    const auto dirty = model->isModified();
    REQUIRE(toolbar->parentWidget() == viewport);
    REQUIRE(viewport->rect().contains(toolbar->geometry()));
    const auto point = button->mapTo(&window, button->rect().center());
    auto* hit = window.childAt(point);
    REQUIRE(hit == button);
    QSignalSpy picked(viewport, &renderer_gl::ViewportWidget::pickRequested);
    QSignalSpy cameraChanged(viewport, &renderer_gl::ViewportWidget::cameraChanged);
    QTest::mouseClick(button, Qt::LeftButton);
    REQUIRE(move->isChecked());
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Move);
    QTest::mouseClick(toolbar, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
    const auto wheelPoint = toolbar->rect().center();
    QWheelEvent wheel(wheelPoint, toolbar->mapToGlobal(wheelPoint), QPoint(), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(toolbar, &wheel);
    REQUIRE(picked.isEmpty());
    REQUIRE(cameraChanged.isEmpty());
    REQUIRE(model->selection()->selectedEntity() == entity);
    REQUIRE(model->undoStack()->index() == history);
    REQUIRE(model->isModified() == dirty);
    window.hide();
}

TEST_CASE("Short scene region exposes working collection controls through its visible tab",
          "[workbench][viewport-layout]") {
    editor::MainWindow window;
    window.resize(960, 640);
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    window.findChild<QAction*>(QStringLiteral("RestoreDefaultViewportLayout"))->trigger();
    QTest::qWait(40);
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* pages = window.findChild<QTabWidget*>(QStringLiteral("ScenePages"));
    auto* bar = pages->findChild<QTabBar*>();
    auto* scroll = window.findChild<QScrollArea*>(QStringLiteral("CollectionScroll"));
    auto* create = window.findChild<QWidget*>(QStringLiteral("CollectionCreateButton"));
    REQUIRE(pages->currentIndex() == 0);
    REQUIRE(bar->isVisible());
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(1).center());
    REQUIRE(pages->currentIndex() == 1);
    REQUIRE(scroll->isVisible());
    scroll->ensureWidgetVisible(create);
    QTest::qWait(30);
    REQUIRE(create->isVisible());
    REQUIRE(create->isEnabled());
    const auto point = create->mapTo(&window, create->rect().center());
    REQUIRE(window.childAt(point) == create);
    const auto count = model->scene()->collections().size();
    bool dialogOpened = false;
    QTimer::singleShot(0, &window, [&dialogOpened] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (dialog) {
            dialogOpened = true;
            dialog->setTextValue(QStringLiteral("布局页签集合"));
            dialog->accept();
        }
    });
    QTest::mouseClick(create, Qt::LeftButton);
    REQUIRE(dialogOpened);
    REQUIRE(model->scene()->collections().size() == count + 1);
    REQUIRE(model->scene()->collections().back().name == "布局页签集合");
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(0).center());
    REQUIRE(window.findChild<QWidget*>(QStringLiteral("SceneTree"))->isVisible());
    window.hide();
}

TEST_CASE("Panel menu opens resizable regions and restores the viewport without changing editing",
          "[workbench][viewport-layout]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* panels = window.findChild<QToolButton*>(QStringLiteral("ViewportPanelsButton"));
    auto* scene = window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    auto* console = window.findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
    auto* split = window.findChild<QSplitter*>(QStringLiteral("ViewportSidebarSplitter"));
    auto* sidebar = window.findChild<QWidget*>(QStringLiteral("ViewportSidebar"));
    auto* restore = window.findChild<QAction*>(QStringLiteral("RestoreDefaultViewportLayout"));
    restore->trigger();
    model->newScene();
    model->createEntity(core::PrimitiveKind::Cube);
    const auto selected = model->selection()->selectedEntity();
    const auto history = model->undoStack()->index();
    const auto dirty = model->isModified();
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    auto* context = viewport->context();
    REQUIRE(context);
    QSignalSpy destroyed(context, &QOpenGLContext::aboutToBeDestroyed);
    QSignalSpy picked(viewport, &renderer_gl::ViewportWidget::pickRequested);
    for (const auto* name : {"ToggleSceneDock", "ToggleInspectorDock", "ToggleConsoleDock",
                             "ToggleViewportSidebar"}) {
        auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
        if (action->isChecked()) {
            clickPanelAction(*panels, *action);
            REQUIRE_FALSE(action->isChecked());
        }
        clickPanelAction(*panels, *action);
        REQUIRE(action->isChecked());
    }
    REQUIRE(scene->isVisible());
    REQUIRE(inspector->isVisible());
    REQUIRE(console->isVisible());
    REQUIRE(host->isSidebarVisible());
    REQUIRE(scene->geometry().bottom() < inspector->geometry().top());
    QTest::mouseClick(sidebar, Qt::LeftButton, Qt::NoModifier, sidebar->rect().center());
    REQUIRE(picked.isEmpty());
    REQUIRE_FALSE(viewport->geometry().intersects(sidebar->geometry()));
    const auto sidebarWidth = sidebar->width();
    auto* handle = split->handle(1);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
    QTest::mouseMove(handle, handle->rect().center() - QPoint(40, 0));
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
    REQUIRE(sidebar->width() > sidebarWidth);
    window.resizeDocks({inspector}, {380}, Qt::Horizontal);
    console->hide();
    for (const QSize size : {QSize(960, 640), QSize(1280, 800)}) {
        window.resize(size);
        QTest::qWait(40);
        REQUIRE(viewport->width() >= 250);
        REQUIRE(viewport->height() >= 300);
        REQUIRE(viewport->context() == context);
        REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    }
    clickPanelAction(*panels, *window.findChild<QAction*>(QStringLiteral("ToggleViewportToolbar")));
    REQUIRE_FALSE(host->isToolbarVisible());
    clickPanelAction(*panels, *restore);
    REQUIRE(host->isToolbarVisible());
    REQUIRE_FALSE(host->isSidebarVisible());
    REQUIRE(scene->isVisible());
    REQUIRE(inspector->isVisible());
    REQUIRE(console->isVisible());
    const auto target = resizeWithinScreen(window, QSize(1440, 900));
    restore->trigger();
    QTest::qWait(40);
    requireReferenceRegions(window);
    const auto ratio = logViewportArea(window, *viewport, QSize(1440, 900), target,
                                       QStringLiteral("panel-reference-restored"));
    REQUIRE(ratio >= 0.64);
    REQUIRE(ratio <= 0.80);
    REQUIRE(viewport->context() == context);
    REQUIRE(destroyed.isEmpty());
    REQUIRE(model->selection()->selectedEntity() == selected);
    REQUIRE(model->undoStack()->index() == history);
    REQUIRE(model->isModified() == dirty);
    window.hide();
}

TEST_CASE("Existing v2 layouts round trip through isolated settings and reset only the active layout",
          "[workbench][viewport-layout]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("legacy-layout.ini"));
    QSettings preferences(path, QSettings::IniFormat);
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* manager = window.findChild<editor::WorkspaceManager*>();
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* scene = window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    auto* console = window.findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
    auto* pages = window.findChild<QTabWidget*>(QStringLiteral("PropertyPages"));
    window.findChild<QAction*>(QStringLiteral("RestoreDefaultViewportLayout"))->trigger();
    manager->setWorkspace(0);
    window.addDockWidget(Qt::LeftDockWidgetArea, scene);
    scene->show();
    inspector->show();
    console->hide();
    host->setToolbarVisible(false);
    host->setSidebarVisible(true);
    pages->setCurrentIndex(1);
    manager->setWorkspace(1);
    inspector->show();
    console->show();
    host->setSidebarVisible(false);
    manager->setWorkspace(0);
    manager->savePreferences(preferences);
    preferences.sync();
    REQUIRE(preferences.status() == QSettings::NoError);
    const auto keys = preferences.allKeys();
    const auto legacyDocks = preferences.value(QStringLiteral("workbench/v2/layout/docks"));
    const auto modelingDocks = preferences.value(QStringLiteral("workbench/v2/modeling/docks"));
    REQUIRE_FALSE(legacyDocks.toByteArray().isEmpty());

    editor::MainWindow reopened;
    reopened.show();
    REQUIRE(QTest::qWaitForWindowExposed(&reopened));
    QSettings stored(path, QSettings::IniFormat);
    auto* restored = reopened.findChild<editor::WorkspaceManager*>();
    auto* restoredHost = reopened.findChild<editor::WorkbenchShell*>();
    auto* restoredScene = reopened.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* restoredInspector = reopened.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    auto* restoredConsole = reopened.findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
    auto* model = reopened.findChild<editor::SceneViewModel*>();
    auto* viewport = reopened.findChild<renderer_gl::ViewportWidget*>();
    restored->restorePreferences(stored);
    REQUIRE(restored->currentWorkspace() == 0);
    REQUIRE(restoredScene->isVisible());
    REQUIRE(restoredInspector->isVisible());
    REQUIRE(restoredConsole->isHidden());
    REQUIRE(reopened.dockWidgetArea(restoredScene) == Qt::LeftDockWidgetArea);
    REQUIRE_FALSE(restoredHost->isToolbarVisible());
    REQUIRE(restoredHost->isSidebarVisible());
    REQUIRE(reopened.findChild<QTabWidget*>(QStringLiteral("PropertyPages"))->currentIndex() == 1);
    const auto history = model->undoStack()->count();
    const auto selected = model->selection()->selectedEntity();
    const auto dirty = model->isModified();
    auto* context = viewport->context();
    REQUIRE(context);
    reopened.findChild<QAction*>(QStringLiteral("RestoreDefaultViewportLayout"))->trigger();
    REQUIRE(restored->currentWorkspace() == 0);
    REQUIRE(restoredScene->isVisible());
    REQUIRE(restoredInspector->isVisible());
    REQUIRE(restoredConsole->isVisible());
    REQUIRE(reopened.dockWidgetArea(restoredScene) == Qt::RightDockWidgetArea);
    REQUIRE(restoredHost->isToolbarVisible());
    REQUIRE_FALSE(restoredHost->isSidebarVisible());
    REQUIRE(stored.allKeys() == keys);
    REQUIRE(stored.value(QStringLiteral("workbench/v2/layout/docks")) == legacyDocks);
    REQUIRE(stored.value(QStringLiteral("workbench/v2/modeling/docks")) == modelingDocks);
    restored->setWorkspace(1);
    REQUIRE(restoredInspector->isVisible());
    REQUIRE(restoredConsole->isVisible());
    REQUIRE_FALSE(restoredHost->isSidebarVisible());
    restored->setWorkspace(0);
    REQUIRE(restoredScene->isVisible());
    REQUIRE(restoredInspector->isVisible());
    REQUIRE(restoredConsole->isVisible());
    REQUIRE(restoredHost->isToolbarVisible());
    REQUIRE_FALSE(restoredHost->isSidebarVisible());
    REQUIRE(model->undoStack()->count() == history);
    REQUIRE(model->selection()->selectedEntity() == selected);
    REQUIRE(model->isModified() == dirty);
    REQUIRE(viewport->context() == context);
    window.hide();
    reopened.hide();
}

TEST_CASE("Workbench regions preserve GL identity and keep sidebar clicks outside picking",
          "[workbench]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* scene = window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* inspector = window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    auto* console = window.findChild<QDockWidget*>(QStringLiteral("ConsoleDock"));
    scene->show();
    inspector->show();
    QTest::qWait(30);
    REQUIRE(host != nullptr);
    REQUIRE(viewport != nullptr);
    REQUIRE(window.dockWidgetArea(scene) == Qt::RightDockWidgetArea);
    REQUIRE(window.dockWidgetArea(inspector) == Qt::RightDockWidgetArea);
    REQUIRE(scene->geometry().bottom() < inspector->geometry().top());
    REQUIRE(console->isVisible());
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    auto* context = viewport->context();
    REQUIRE(context != nullptr);
    QSignalSpy destroyed(context, &QOpenGLContext::aboutToBeDestroyed);
    QSignalSpy picked(viewport, &renderer_gl::ViewportWidget::pickRequested);
    const auto width = viewport->width();
    const auto modified = model->isModified();
    const auto history = model->undoStack()->count();
    window.findChild<QAction*>(QStringLiteral("ToggleViewportSidebar"))->trigger();
    QTest::qWait(40);
    REQUIRE(host->isSidebarVisible());
    REQUIRE(viewport->width() < width);
    auto* sidebar = window.findChild<QWidget*>(QStringLiteral("ViewportSidebar"));
    QTest::mouseClick(sidebar, Qt::LeftButton, Qt::NoModifier, sidebar->rect().center());
    REQUIRE(picked.isEmpty());
    REQUIRE_FALSE(viewport->geometry().intersects(sidebar->geometry()));
    REQUIRE(viewport->grabFramebuffer().size() ==
            QSize(qRound(viewport->width() * viewport->devicePixelRatioF()),
                  qRound(viewport->height() * viewport->devicePixelRatioF())));
    window.findChild<QAction*>(QStringLiteral("ToggleViewportSidebar"))->trigger();
    window.findChild<QAction*>(QStringLiteral("ToggleViewportToolbar"))->trigger();
    QTest::qWait(40);
    REQUIRE_FALSE(host->isSidebarVisible());
    REQUIRE_FALSE(host->isToolbarVisible());
    REQUIRE(viewport->width() == width);
    REQUIRE(viewport->context() == context);
    REQUIRE(destroyed.isEmpty());
    REQUIRE(model->isModified() == modified);
    REQUIRE(model->undoStack()->count() == history);
    window.hide();
}

TEST_CASE("Compact workbench buttons execute cancel repeat and history through existing actions",
          "[workbench][ui-polish]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* modal = window.findChild<editor::ObjectTransformSession*>();
    auto* tools = window.findChild<QToolBar*>(QStringLiteral("ViewportToolbar"));
    auto* quick = window.findChild<QToolBar*>(QStringLiteral("QuickActionBar"));
    auto* summary = window.findChild<QLabel*>(QStringLiteral("SelectionSummary"));
    auto* mode = window.findChild<QToolButton*>(QStringLiteral("EditModeButton"));
    REQUIRE(quick);
    REQUIRE(summary);
    REQUIRE(mode->text() == QStringLiteral("对象模式"));
    const auto buttonFor = [&window](QToolBar* bar, const char* name) {
        auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
        REQUIRE(action);
        auto* button = qobject_cast<QToolButton*>(bar->widgetForAction(action));
        REQUIRE(button);
        REQUIRE(button->defaultAction() == action);
        return button;
    };
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    auto* move = buttonFor(tools, "MoveTool");
    QTest::mouseClick(move, Qt::LeftButton);
    REQUIRE(move->text() == QStringLiteral("移动"));
    REQUIRE(move->defaultAction()->iconText() == QStringLiteral("移动"));
    QTest::mouseClick(buttonFor(tools, "SelectTool"), Qt::LeftButton);
    const auto beforeConversion = model->undoStack()->index();
    auto* inset = buttonFor(tools, "InsetFace");
    REQUIRE_FALSE(inset->isEnabled());
    QTest::mouseClick(mode, Qt::LeftButton, Qt::NoModifier, QPoint(12, mode->height() / 2));
    REQUIRE(model->isEditMode());
    REQUIRE(model->undoStack()->index() == beforeConversion + 1);
    const auto editable = model->displayedEditableMesh(cube);
    REQUIRE(editable);
    const auto original = editable->source;
    const auto index = model->undoStack()->index();
    model->setSelectionDomain(editor::SelectionDomain::Face);
    model->selectComponent({1}, editor::SelectionOperation::Replace);
    REQUIRE(mode->text() == QStringLiteral("编辑模式"));
    REQUIRE(summary->text().contains(QStringLiteral("已选面 1 / 6")));
    REQUIRE(inset->isEnabled());
    REQUIRE(inset->text() == QStringLiteral("内插"));
    REQUIRE(inset->toolTip().contains(QStringLiteral("I")));

    QTest::mouseClick(inset, Qt::LeftButton);
    REQUIRE(modal->isActive());
    QTest::keyClicks(viewport, "0.05");
    QTest::keyClick(viewport, Qt::Key_Escape);
    REQUIRE_FALSE(modal->isActive());
    REQUIRE(model->displayedEditableMesh(cube)->source == original);
    REQUIRE(model->undoStack()->index() == index);
    QTest::mouseClick(inset, Qt::LeftButton);
    REQUIRE(modal->isActive());
    QTest::keyClicks(viewport, "0.05");
    QTest::keyClick(viewport, Qt::Key_Return);
    REQUIRE_FALSE(modal->isActive());
    REQUIRE(model->undoStack()->index() == index + 1);
    const auto first = model->displayedEditableMesh(cube)->source;
    REQUIRE(first != original);
    auto* repeat = buttonFor(quick, "RepeatLastOperation");
    REQUIRE(repeat->isEnabled());
    QTest::mouseClick(repeat, Qt::LeftButton);
    REQUIRE(model->undoStack()->index() == index + 2);
    const auto repeated = model->displayedEditableMesh(cube)->source;
    REQUIRE(repeated != first);
    auto* undo = buttonFor(quick, "Undo");
    auto* redo = buttonFor(quick, "Redo");
    REQUIRE(undo->text() == QStringLiteral("撤销"));
    REQUIRE(redo->text() == QStringLiteral("重做"));
    QTest::mouseClick(undo, Qt::LeftButton);
    REQUIRE(model->displayedEditableMesh(cube)->source == first);
    QTest::mouseClick(undo, Qt::LeftButton);
    REQUIRE(model->displayedEditableMesh(cube)->source == original);
    QTest::mouseClick(redo, Qt::LeftButton);
    QTest::mouseClick(redo, Qt::LeftButton);
    REQUIRE(model->displayedEditableMesh(cube)->source == repeated);
    REQUIRE(model->undoStack()->index() == index + 2);

    QTest::mouseClick(buttonFor(quick, "SearchOperators"), Qt::LeftButton);
    auto* popup = window.findChild<QWidget*>(QStringLiteral("OperatorSearchPopup"));
    REQUIRE(popup);
    REQUIRE(popup->isVisible());
    QTest::keyClick(popup->findChild<QLineEdit*>(), Qt::Key_Escape);
    REQUIRE_FALSE(popup->isVisible());
    REQUIRE(model->undoStack()->index() == index + 2);
    window.hide();
}

TEST_CASE("Polished workbench retains readable controls and GL context at minimum size",
          "[workbench][ui-polish]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* pages = window.findChild<QTabWidget*>(QStringLiteral("PropertyPages"));
    auto* header = window.findChild<QWidget*>(QStringLiteral("ViewportHeader"));
    auto* settings = window.findChild<QToolBar*>(QStringLiteral("TransformSettingsBar"));
    auto* quick = window.findChild<QToolBar*>(QStringLiteral("QuickActionBar"));
    auto* tools = window.findChild<QToolBar*>(QStringLiteral("ViewportToolbar"));
    REQUIRE(pages->tabPosition() == QTabWidget::North);
    if (settings->isVisible()) {
        REQUIRE(header->geometry().bottom() < settings->geometry().top());
    } else {
        auto* compact = window.findChild<QToolButton*>(QStringLiteral("CompactTransformSettingsButton"));
        REQUIRE(compact->isVisible());
        REQUIRE(header->rect().contains(compact->geometry()));
    }
    for (auto* bar :
         {quick, settings, window.findChild<QToolBar*>(QStringLiteral("ViewportToolbar"))}) {
        auto* more = bar->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
        REQUIRE(more);
        REQUIRE_FALSE(more->icon().isNull());
        REQUIRE(more->toolTip() == QStringLiteral("更多工具"));
    }
    const auto capture = [&window](const QString& name) {
        const auto path = qEnvironmentVariable("MINI3D_TEST_UI_POLISH_DIRECTORY");
        if (!path.isEmpty()) {
            QDir directory(path);
            REQUIRE(directory.mkpath(QStringLiteral(".")));
            REQUIRE(window.grab().save(directory.filePath(QStringLiteral("%1-%2x%3.png")
                                                             .arg(name)
                                                             .arg(window.width())
                                                             .arg(window.height()))));
        }
    };
    model->newScene();
    model->createEntity(core::PrimitiveKind::Cube);
    QTest::qWait(40);
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    auto* context = viewport->context();
    auto* toolSettings = window.findChild<QLabel*>(QStringLiteral("ToolSettings"));
    REQUIRE(toolSettings->isVisible());
    REQUIRE(toolSettings->width() >= toolSettings->minimumSizeHint().width());
    capture(QStringLiteral("after-object"));
    REQUIRE(model->setEditMode(true));
    model->setSelectionDomain(editor::SelectionDomain::Face);
    model->selectComponent({1}, editor::SelectionOperation::Replace);
    QTest::qWait(40);
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(model->saveScene(directory.filePath(QStringLiteral("layout.m3dscene"))));
    const auto history = model->undoStack()->index();
    REQUIRE_FALSE(model->isModified());
    capture(QStringLiteral("after-edit"));
    for (const QSize size : {QSize(960, 640), QSize(1280, 800), QSize(960, 640)}) {
        host->setSidebarVisible(true);
        window.resize(size);
        QTest::qWait(40);
        requireReadableConsole(window);
        REQUIRE(viewport->width() >= 250);
        REQUIRE(viewport->height() >= 300);
        REQUIRE(toolSettings->isVisible());
        REQUIRE(toolSettings->width() >= 100);
        if (settings->isVisible()) {
            REQUIRE(header->geometry().bottom() < settings->geometry().top());
        } else {
            auto* compact = window.findChild<QToolButton*>(QStringLiteral("CompactTransformSettingsButton"));
            REQUIRE(compact->isVisible());
            REQUIRE(header->rect().contains(compact->geometry()));
            REQUIRE(toolSettings->geometry().top() >=
                    viewport->mapTo(host, QPoint(0, viewport->height())).y());
        }
        REQUIRE(viewport->context() == context);
        REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
        REQUIRE(model->undoStack()->index() == history);
        REQUIRE_FALSE(model->isModified());
        auto* mode = window.findChild<QToolButton*>(QStringLiteral("EditModeButton"));
        REQUIRE(header->rect().contains(mode->geometry()));
        auto* search = window.findChild<QAction*>(QStringLiteral("SearchOperators"));
        REQUIRE(quick->widgetForAction(search)->isVisible());
        for (const auto* name : {"BoxSelectComponents", "ExtrudeRegion", "InsetFace",
                                 "BevelEdge", "LoopCut"}) {
            auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
            auto* button = qobject_cast<QToolButton*>(tools->widgetForAction(action));
            REQUIRE(button);
            REQUIRE_FALSE(button->icon().isNull());
            REQUIRE(button->toolButtonStyle() == Qt::ToolButtonIconOnly);
            REQUIRE(button->isVisible());
            REQUIRE(tools->rect().contains(button->geometry()));
            REQUIRE(window.childAt(button->mapTo(&window, button->rect().center())) == button);
            REQUIRE(window.screen()->availableGeometry().contains(
                QRect(button->mapToGlobal(QPoint()), button->size())));
        }
    }
    capture(QStringLiteral("after-minimum"));
    window.hide();
}

TEST_CASE("Large editable source has a persistent scale hint without changing history",
          "[workbench][performance-scale]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* status = window.findChild<QLabel*>(QStringLiteral("EditableScaleStatus"));
    REQUIRE(status);
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->setEditMode(true));
    const auto original = model->displayedEditableMesh(cube)->source;
    REQUIRE(status->text().isEmpty());
    auto large = original;
    // 该夹具仅核对源点计数提示；真实性能另用完整四边格探针测量。
    for (std::uint64_t id = 9; id <= 10001; ++id)
        large.vertices.push_back({id, {2, 2, 2}});
    REQUIRE(model->replaceEditableMesh(cube, large));
    const auto history = model->undoStack()->index();
    const auto dirty = model->isModified();
    host->setSidebarVisible(false);
    REQUIRE(status->isVisible());
    REQUIRE(status->text().contains(QStringLiteral("10001")));
    REQUIRE(status->text().contains(QStringLiteral("可能延迟")));
    REQUIRE(model->setEditMode(false));
    REQUIRE(status->text().isEmpty());
    REQUIRE(model->setEditMode(true));
    REQUIRE(status->text().contains(QStringLiteral("10001")));
    REQUIRE(model->undoStack()->index() == history);
    REQUIRE(model->isModified() == dirty);
    REQUIRE(model->replaceEditableMesh(cube, original));
    REQUIRE(status->text().isEmpty());
    window.hide();
}

TEST_CASE("Workspaces restore independent preferences without changing document or GL context",
          "[workbench][workspace]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    QSettings preferences(directory.filePath(QStringLiteral("workspace.ini")),
                          QSettings::IniFormat);
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* manager = window.findChild<editor::WorkspaceManager*>();
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* pages = window.findChild<QTabWidget*>(QStringLiteral("PropertyPages"));
    const auto history = model->undoStack()->count();
    const auto dirty = model->isModified();
    const auto selection = model->selection()->selectedEntity();
    auto* context = viewport->context();
    host->setToolbarVisible(false);
    pages->setCurrentIndex(1);
    window.findChild<QToolButton*>(QStringLiteral("DataPropertiesGroup"))->setChecked(false);
    window.findChild<QAction*>(QStringLiteral("LocalTransformSpace"))->trigger();
    host->workspaceTabs()->setCurrentIndex(1);
    QTest::qWait(30);
    REQUIRE(manager->currentWorkspace() == 1);
    REQUIRE(host->isToolbarVisible());
    REQUIRE(host->isSidebarVisible());
    REQUIRE(pages->currentIndex() == 0);
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::World);
    manager->setWorkspace(0);
    REQUIRE_FALSE(host->isToolbarVisible());
    REQUIRE(pages->currentIndex() == 1);
    REQUIRE_FALSE(
        window.findChild<QToolButton*>(QStringLiteral("DataPropertiesGroup"))->isChecked());
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::Local);
    manager->savePreferences(preferences);
    REQUIRE(preferences.status() == QSettings::NoError);
    manager->setWorkspace(2);
    REQUIRE(pages->currentIndex() == 1);
    manager->restorePreferences(preferences);
    REQUIRE(manager->currentWorkspace() == 0);
    REQUIRE_FALSE(host->isToolbarVisible());
    REQUIRE(viewport->transformSpace() == renderer_gl::GizmoSpace::Local);
    REQUIRE(model->isModified() == dirty);
    REQUIRE(model->undoStack()->count() == history);
    REQUIRE(model->selection()->selectedEntity() == selection);
    REQUIRE(window.findChild<QLabel*>(QStringLiteral("ModeLabel"))->text() ==
            QStringLiteral("对象模式"));
    REQUIRE(viewport->context() == context);
    // 新窗口从独立偏好存储重建，而不是只验证原窗口内存还保留着状态。
    editor::MainWindow reopened;
    reopened.show();
    REQUIRE(QTest::qWaitForWindowExposed(&reopened));
    auto* restored = reopened.findChild<editor::WorkspaceManager*>();
    restored->restorePreferences(preferences);
    REQUIRE_FALSE(reopened.findChild<editor::WorkbenchShell*>()->isToolbarVisible());
    REQUIRE(reopened.findChild<QTabWidget*>(QStringLiteral("PropertyPages"))->currentIndex() == 1);
    restored->setWorkspace(1);
    REQUIRE(reopened.findChild<editor::WorkbenchShell*>()->isSidebarVisible());
    REQUIRE(reopened.findChild<editor::WorkbenchShell*>()->isToolbarVisible());
    window.hide();
    reopened.hide();
}

TEST_CASE("Workbench tools reuse actions and selection data with a usable minimum viewport",
          "[workbench]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* host = window.findChild<editor::WorkbenchShell*>();
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("ViewportToolbar"));
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->renameEntity(id, QStringLiteral("工作台对象")));
    REQUIRE(window.findChild<QLabel*>(QStringLiteral("SidebarSelection"))->text() ==
            QStringLiteral("工作台对象"));
    const auto count = model->undoStack()->count();
    auto* move = window.findChild<QAction*>(QStringLiteral("MoveTool"));
    REQUIRE(toolbar->actions().contains(move));
    auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(move));
    REQUIRE(button != nullptr);
    QTest::mouseClick(button, Qt::LeftButton);
    REQUIRE(move->isChecked());
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::Move);
    window.findChild<QAction*>(QStringLiteral("SelectTool"))->trigger();
    REQUIRE_FALSE(move->isChecked());
    REQUIRE(viewport->transformTool() == renderer_gl::GizmoTool::None);
    REQUIRE(model->undoStack()->count() == count);
    host->setSidebarVisible(true);
    window.resize(960, 640);
    QTest::qWait(50);
    INFO("window=" << window.width() << ", host=" << host->width()
                   << ", viewport=" << viewport->width());
    REQUIRE(viewport->width() >= 250);
    REQUIRE(viewport->height() >= 300);
    REQUIRE_FALSE(viewport->grabFramebuffer().isNull());
    const auto capture = qEnvironmentVariable("MINI3D_TEST_WORKBENCH_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture));
    }
    window.hide();
}
