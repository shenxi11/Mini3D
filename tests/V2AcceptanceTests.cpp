/*
 * 模块名: V2AcceptanceTests
 * 功能概述: 用真实窗口输入串联 S01–S12 配方、原生 Cube 作品和实际 DPI 证据。
 * 对外接口: Catch2 [v2-acceptance] 及各作品标签。
 * 依赖关系: Qt Test、MainWindow、公开 ViewModel/网格/OBJ 接口。
 * 输入输出: 原生对象、真实键鼠和临时文件到源笼、历史、保存重开及实际帧断言。
 * 异常与错误: 不符即失败；缺少第二块真实屏幕时明确警告，不合成跨屏证据。
 * 维护说明: 半壳为程序化原生加工夹具，不代表 Bisect；截图仅来自实际窗口。
 */
#include "core/modeling/MeshValidation.h"
#include "core/modeling/ObjExporter.h"
#include "editor/MainWindow.h"
#include "editor/SceneTreeModel.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/LoopCutSession.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/CommitSpinBox.h"
#include "editor/workbench/LastOperationPanel.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "editor/workbench/WorkbenchShell.h"
#include "editor/workbench/WorkspaceManager.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QImage>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QWindow>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>

using namespace mini3d;
namespace {
const core::EditableMeshContent& content(const editor::SceneViewModel& model,
                                         core::EntityId entity) {
    const auto* node = model.scene()->find(entity);
    REQUIRE(node);
    const auto* mesh = model.scene()->editableMesh(node->editableMesh);
    REQUIRE(mesh);
    return *mesh->content;
}

QByteArray objBytes(const editor::SceneViewModel& model, core::EntityId entity, bool evaluated) {
    const auto& node = *model.scene()->find(entity);
    const auto& mesh = content(model, entity);
    const auto encoded =
        core::modeling::encodeObj(evaluated ? mesh.evaluatedMesh() : mesh.source,
                                  glm::dmat4(model.scene()->worldMatrix(entity)), node.name);
    INFO(encoded.error);
    REQUIRE(encoded.text);
    return QByteArray::fromStdString(*encoded.text);
}

QByteArray objHash(const editor::SceneViewModel& model, core::EntityId entity, bool evaluated) {
    return QCryptographicHash::hash(objBytes(model, entity, evaluated), QCryptographicHash::Sha256)
        .toHex();
}

core::modeling::EditableMesh nativeHalfCube(const core::modeling::EditableMesh& cube) {
    // 程序化加工原生 Cube：压到中心面并去掉中心盖；不是用户界面中的 Bisect 操作。
    auto half = cube;
    for (auto& vertex : half.vertices)
        if (vertex.position.x < 0)
            vertex.position.x = 0;
    std::erase_if(half.faces, [&half](const auto& face) {
        return std::all_of(face.corners.begin(), face.corners.end(), [&half](const auto& corner) {
            return half.vertex(corner.vertex)->position.x == 0;
        });
    });
    REQUIRE(half.vertices.size() == 8);
    REQUIRE(half.faces.size() == 5);
    REQUIRE(core::modeling::validateEditableMesh(half).boundaryEdgeCount == 4);
    return half;
}

/** @brief 独立实际窗口；键盘调用前显式送入逻辑坐标，不依赖系统鼠标位置。 */
struct AcceptanceWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::WorkbenchShell* host = window.findChild<editor::WorkbenchShell*>();
    editor::KeymapRouter* router = window.findChild<editor::KeymapRouter*>();
    editor::ObjectTransformSession* modal = window.findChild<editor::ObjectTransformSession*>();
    editor::LoopCutSession* loop = window.findChild<editor::LoopCutSession*>();
    core::EntityId entity = core::kInvalidEntity;

    AcceptanceWindow() {
        REQUIRE(model);
        REQUIRE(viewport);
        REQUIRE(host);
        REQUIRE(router);
        REQUIRE(modal);
        REQUIRE(loop);
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        router->setKeymap(editor::EditorKeymap::Blender);
        auto* workspaces = window.findChild<editor::WorkspaceManager*>();
        REQUIRE(workspaces);
        workspaces->setWorkspace(0);
        host->setToolbarVisible(true);
        host->setSidebarVisible(false);
        model->newScene();
        viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
        QTest::qWait(30);
    }
    ~AcceptanceWindow() {
        window.hide();
    }
    void pointAt(QPoint point) {
        viewport->setFocus();
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
    }
    QPoint project(glm::vec3 position) const {
        const auto camera = viewport->editorCameraSnapshot();
        REQUIRE(camera);
        const auto clip = camera->viewProjectionMatrix() * glm::vec4(position, 1);
        REQUIRE(clip.w > 0);
        const QPoint point(qRound((clip.x / clip.w + 1) * viewport->width() * .5F),
                           qRound((1 - clip.y / clip.w) * viewport->height() * .5F));
        REQUIRE(viewport->rect().contains(point));
        return point;
    }
    void clickTopFace() {
        const auto point = project({.12F, .5F, .11F});
        pointAt(point);
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point);
        REQUIRE(model->componentSelection().domain() == editor::SelectionDomain::Face);
        REQUIRE(model->componentSelection().selectedIds() == std::set<editor::ComponentId>{{5}});
        REQUIRE(model->componentSelection().activeId() == editor::ComponentId{5});
    }
    void enterNativeCube() {
        entity = model->createEntity(core::PrimitiveKind::Cube);
        REQUIRE(model->scene()->find(entity)->editableMesh == 0);
        const auto index = model->undoStack()->index();
        REQUIRE(viewport->focusSelection());
        pointAt(viewport->rect().center());
        QTest::keyClick(viewport, Qt::Key_Tab);
        REQUIRE(model->isEditMode());
        REQUIRE(model->editedEntity() == entity);
        REQUIRE(model->undoStack()->index() == index + 1);
        REQUIRE(content(*model, entity).source == core::modeling::createEditableCube());
        QTest::keyClick(viewport, Qt::Key_3);
        QTest::keyClick(viewport, Qt::Key_7, Qt::KeypadModifier);
        const auto camera = viewport->editorCameraSnapshot();
        REQUIRE(camera);
        REQUIRE(camera->view() == renderer_gl::EditorView::Top);
        QTest::qWait(30);
        clickTopFace();
    }
    void checkFrame() const {
        const auto frame = viewport->grabFramebuffer();
        REQUIRE_FALSE(frame.isNull());
        REQUIRE(frame.size() == QSize(qRound(viewport->width() * viewport->devicePixelRatioF()),
                                      qRound(viewport->height() * viewport->devicePixelRatioF())));
    }
    void reopen(const QString& path) {
        const auto mesh = model->scene()->find(entity)->editableMesh;
        const auto objects = model->scene()->nodes().size();
        REQUIRE(model->openScene(path));
        REQUIRE_FALSE(model->isEditMode());
        REQUIRE(model->scene()->find(entity));
        REQUIRE(model->scene()->find(entity)->editableMesh == mesh);
        REQUIRE(model->scene()->nodes().size() == objects);
        model->selection()->setSelectedEntity(entity);
        pointAt(viewport->rect().center());
        QTest::keyClick(viewport, Qt::Key_Tab);
        REQUIRE(model->isEditMode());
        REQUIRE(model->editedEntity() == entity);
        REQUIRE(model->undoStack()->index() == 0);
    }
};

/** @brief 默认只写临时文件；显式环境开关启用正式作品路径和真实窗口截图。 */
struct ArtifactDirectory {
    QTemporaryDir temporary;
    QString capture = qEnvironmentVariable("MINI3D_TEST_V2_ARTIFACT_DIR");
    QString root;
    ArtifactDirectory() {
        REQUIRE(temporary.isValid());
        root = capture.isEmpty() ? temporary.path() : capture;
        REQUIRE(QDir().mkpath(root));
    }
    QString path(const QString& name) const {
        return QDir(root).filePath(name);
    }
};

QJsonArray sizeEvidence(QSize size) {
    return {size.width(), size.height()};
}

QJsonArray rectEvidence(QRect rect) {
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}

QJsonObject displayEvidence(const AcceptanceWindow& fixture) {
    auto* handle = fixture.window.windowHandle();
    REQUIRE(handle);
    auto* screen = handle->screen();
    REQUIRE(screen);
    const auto frame = fixture.viewport->grabFramebuffer();
    REQUIRE_FALSE(frame.isNull());
    return {{"screen", screen->name()},
            {"resolution",
             QJsonObject{{"screenLogical", rectEvidence(screen->geometry())},
                         {"screenAvailableLogical", rectEvidence(screen->availableGeometry())},
                         {"windowLogical", sizeEvidence(fixture.window.size())},
                         {"windowFrameLogical", rectEvidence(fixture.window.frameGeometry())},
                         {"viewportLogical", sizeEvidence(fixture.viewport->size())},
                         {"viewportRectLogical", rectEvidence(fixture.viewport->geometry())},
                         {"framebufferPixels", sizeEvidence(frame.size())}}},
            {"dpr", QJsonObject{{"screen", screen->devicePixelRatio()},
                                {"window", handle->devicePixelRatio()},
                                {"viewport", fixture.viewport->devicePixelRatioF()}}}};
}

/** @brief 先保存观测值再校验绝对 DPR；请求档位不冒充实际观测或系统显示设置。 */
void recordEvidence(AcceptanceWindow& fixture, const ArtifactDirectory& directory,
                    const QString& caseId, const QString& expected, QJsonObject actual = {},
                    QStringList limitations = {}, QJsonArray observations = {}) {
    const auto display = displayEvidence(fixture);
    const auto dpr = display.value("dpr").toObject();
    const auto expectedText = qEnvironmentVariable("MINI3D_TEST_V2_EXPECTED_DPR");
    bool valid = false;
    const auto expectedDpr = expectedText.toDouble(&valid);
    REQUIRE((expectedText.isEmpty() || (valid && expectedDpr > 0)));
    const bool matches =
        expectedText.isEmpty() || (std::abs(dpr.value("window").toDouble() - expectedDpr) < .01 &&
                                   std::abs(dpr.value("viewport").toDouble() - expectedDpr) < .01);
    const auto framePixels = display.value("resolution").toObject().value("framebufferPixels");
    const auto expectedPixels =
        sizeEvidence({qRound(fixture.viewport->width() * fixture.viewport->devicePixelRatioF()),
                      qRound(fixture.viewport->height() * fixture.viewport->devicePixelRatioF())});
    const bool frameMatches = framePixels.toArray() == expectedPixels;
    const auto source = objBytes(*fixture.model, fixture.entity, false);
    const auto sourceName = caseId + QStringLiteral("-source.obj");
    QFile sourceFile(directory.path(sourceName));
    REQUIRE(sourceFile.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(sourceFile.write(source) == source.size());
    sourceFile.close();
    const auto hash = QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex();
    const auto screenshotName = caseId + QStringLiteral(".png");
    if (!directory.capture.isEmpty())
        REQUIRE(fixture.window.grab().save(directory.path(screenshotName)));
    actual.insert("functionalAssertions", "passed before evidence checkpoint");
    actual.insert("undoIndex", fixture.model->undoStack()->index());
    actual.insert("undoCount", fixture.model->undoStack()->count());
    actual.insert("modified", fixture.model->isModified());
    actual.insert("framebufferMatchesViewportDpr", frameMatches);
    actual.insert("requestedDprObserved", matches);
    limitations.append(QStringLiteral("Qt synthetic input does not verify real Chinese IME."));
    limitations.append(QStringLiteral("QT_SCALE_FACTOR tests effective Qt DPR; it does not change "
                                      "or prove native system scaling or mixed-DPI screens."));
    limitations.append(
        QStringLiteral("OBJ SHA256 identifies this Mini3D source, not a Blender run."));
    const QJsonObject report{
        {"caseId", caseId},
        {"buildSha", qEnvironmentVariable("MINI3D_TEST_V2_BUILD_SHA", "standalone-not-supplied")},
        {"worktreeDirty", qEnvironmentVariable("MINI3D_TEST_V2_WORKTREE_DIRTY", "unknown")},
        {"referenceVersion", "Blender v4.5.0 (fixed reference, not an external run)"},
        {"keymap", "Blender"},
        {"resolution", display.value("resolution")},
        {"dpr", dpr},
        {"screen", display.value("screen")},
        {"requestedQtScaleFactor", qEnvironmentVariable("QT_SCALE_FACTOR")},
        {"expectedDpr",
         expectedText.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(expectedDpr)},
        {"modelHash", QString::fromLatin1(hash)},
        {"sourceObj", sourceName},
        {"screenshot",
         directory.capture.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(screenshotName)},
        {"expected", expected},
        {"actual", actual},
        {"displayObservations", observations},
        {"limitations", QJsonArray::fromStringList(limitations)},
        {"status", matches && frameMatches ? "passed-with-limitations" : "failed-dpi"}};
    QFile reportFile(directory.path(caseId + QStringLiteral(".json")));
    REQUIRE(reportFile.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    REQUIRE(reportFile.write(json) == json.size());
    reportFile.close();
    INFO("Recorded actual DPR window=" << dpr.value("window").toDouble()
                                       << " viewport=" << dpr.value("viewport").toDouble()
                                       << " requested=" << expectedText.toStdString());
    REQUIRE(frameMatches);
    REQUIRE(matches);
}

/** @brief 切到真实修改器页并滚动到控件；不向不可见控件伪造鼠标点击。 */
void clickModifier(AcceptanceWindow& fixture, QWidget* control) {
    REQUIRE(control);
    auto* inspector = fixture.window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    REQUIRE(inspector);
    if (!inspector->isVisible()) {
        fixture.window.findChild<QAction*>(QStringLiteral("ToggleInspectorDock"))->trigger();
        QTest::qWait(30);
    }
    auto* pages = fixture.window.findChild<QTabWidget*>(QStringLiteral("PropertyPages"));
    auto* scroll =
        fixture.window.findChild<QScrollArea*>(QStringLiteral("ModifierPropertiesScroll"));
    REQUIRE(pages);
    REQUIRE(scroll);
    const auto tab = pages->indexOf(scroll);
    REQUIRE(tab >= 0);
    if (pages->currentIndex() != tab)
        QTest::mouseClick(pages->tabBar(), Qt::LeftButton, Qt::NoModifier,
                          pages->tabBar()->tabRect(tab).center());
    auto* group = fixture.window.findChild<QToolButton*>(QStringLiteral("ModifierPropertiesGroup"));
    REQUIRE(group);
    if (!group->isChecked())
        QTest::mouseClick(group, Qt::LeftButton);
    scroll->ensureWidgetVisible(control);
    QTest::qWait(30);
    REQUIRE(control->isVisible());
    REQUIRE(control->isEnabled());
    const auto point = qobject_cast<QCheckBox*>(control) ? QPoint(8, control->height() / 2)
                                                         : control->rect().center();
    const auto viewportPoint = control->mapTo(scroll->viewport(), point);
    INFO(control->objectName().toStdString());
    CAPTURE(control->width(), control->height(), viewportPoint.x(), viewportPoint.y(),
            scroll->viewport()->width(), scroll->viewport()->height());
    REQUIRE(scroll->viewport()->rect().contains(viewportPoint));
    QTest::mouseClick(control, Qt::LeftButton, Qt::NoModifier, point);
}

QByteArray exportAndCheck(editor::SceneViewModel& model, core::EntityId entity, const QString& path,
                          bool evaluated) {
    const auto index = model.undoStack()->index();
    const auto selection = model.componentSelection();
    const auto expected = objBytes(model, entity, evaluated);
    REQUIRE(model.exportObj(path, evaluated));
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto actual = file.readAll();
    REQUIRE(actual == expected);
    std::size_t vertices = 0, faces = 0;
    for (const auto& line : actual.split('\n')) {
        vertices += line.startsWith("v ");
        faces += line.startsWith("f ");
    }
    const auto& mesh = content(model, entity);
    const auto& exported = evaluated ? mesh.evaluatedMesh() : mesh.source;
    REQUIRE(vertices == exported.vertices.size());
    REQUIRE(faces == exported.faces.size());
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE_FALSE(model.isModified());
    const auto hash = QCryptographicHash::hash(actual, QCryptographicHash::Sha256).toHex();
    REQUIRE(hash == objHash(model, entity, evaluated));
    INFO("OBJ vertices=" << vertices << " faces=" << faces << " SHA256=" << hash.toStdString());
    return hash;
}

struct SavedArtifact {
    QString scenePath;
    QByteArray sourceHash, evaluatedHash;
};
SavedArtifact saveArtifact(AcceptanceWindow& fixture, const ArtifactDirectory& directory,
                           const QString& name, bool evaluated) {
    SavedArtifact saved;
    saved.scenePath = directory.path(name + QStringLiteral(".m3dscene"));
    REQUIRE(fixture.model->saveScene(saved.scenePath));
    saved.sourceHash = exportAndCheck(*fixture.model, fixture.entity,
                                      directory.path(name + QStringLiteral("-source.obj")), false);
    if (evaluated)
        saved.evaluatedHash =
            exportAndCheck(*fixture.model, fixture.entity,
                           directory.path(name + QStringLiteral("-evaluated.obj")), true);
    fixture.checkFrame();
    if (!directory.capture.isEmpty())
        REQUIRE(fixture.window.grab().save(directory.path(name + QStringLiteral(".png"))));
    return saved;
}
} // namespace

TEST_CASE("V2 S01 S02 S08 native top picking survives real workspaces panels and screen DPI",
          "[v2-acceptance][v2-native-ui][s01][s02][s08][s12]") {
    AcceptanceWindow fixture;
    ArtifactDirectory artifacts;
    const auto light = fixture.model->createDirectionalLight();
    const auto index = fixture.model->undoStack()->index();
    QTest::mouseClick(fixture.host->workspaceTabs(), Qt::LeftButton, Qt::NoModifier,
                      fixture.host->workspaceTabs()->tabRect(1).center());
    REQUIRE(fixture.window.findChild<editor::WorkspaceManager*>()->currentWorkspace() == 1);
    REQUIRE_FALSE(fixture.model->isEditMode());
    REQUIRE(fixture.model->selection()->selectedEntity() == light);
    REQUIRE(fixture.model->undoStack()->index() == index);
    fixture.enterNativeCube();
    auto* sceneDock = fixture.window.findChild<QDockWidget*>(QStringLiteral("SceneDock"));
    auto* inspector = fixture.window.findChild<QDockWidget*>(QStringLiteral("InspectorDock"));
    REQUIRE(sceneDock);
    REQUIRE(inspector);
    fixture.window.findChild<QAction*>(QStringLiteral("ToggleSceneDock"))->setChecked(true);
    fixture.window.findChild<QAction*>(QStringLiteral("ToggleInspectorDock"))->setChecked(true);
    QTest::qWait(30);
    REQUIRE(fixture.window.dockWidgetArea(sceneDock) == Qt::RightDockWidgetArea);
    REQUIRE(fixture.window.dockWidgetArea(inspector) == Qt::RightDockWidgetArea);
    REQUIRE(sceneDock->geometry().bottom() < inspector->geometry().top());
    REQUIRE(fixture.window.statusBar()->isVisible());
    REQUIRE_FALSE(fixture.window.statusBar()->currentMessage().isEmpty());
    REQUIRE(fixture.window.statusBar()->geometry().top() >
            fixture.viewport->mapTo(&fixture.window, fixture.viewport->rect().bottomLeft()).y());
    int timelines = 0;
    for (const auto* widget : fixture.window.findChildren<QWidget*>()) {
        if (widget->isVisible() &&
            (widget->objectName().contains(QStringLiteral("timeline"), Qt::CaseInsensitive) ||
             widget->windowTitle().contains(QStringLiteral("时间轴"))))
            ++timelines;
    }
    REQUIRE(timelines == 0);
    recordEvidence(fixture, artifacts, QStringLiteral("S01"),
                   QStringLiteral("Modeling workspace has real right-side regions, status bar "
                                  "and no timeline placeholder; workspace switch adds no history."),
                   {{"workspace", 1},
                    {"statusBarText", fixture.window.statusBar()->currentMessage()},
                    {"visibleTimelineWidgets", timelines}});
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(fixture.model->saveScene(directory.filePath("native-top.m3dscene")));
    const auto source = content(*fixture.model, fixture.entity).source;
    const auto hash = objHash(*fixture.model, fixture.entity, false);
    const auto history = fixture.model->undoStack()->index();
    auto* tree = fixture.window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    auto* treeModel = fixture.window.findChild<editor::SceneTreeModel*>();
    auto* selection = fixture.window.findChild<QLabel*>(QStringLiteral("SidebarSelection"));
    auto* position = fixture.window.findChild<QLabel*>(QStringLiteral("SidebarTransform"));
    REQUIRE(tree);
    REQUIRE(treeModel);
    REQUIRE(selection);
    REQUIRE(position);
    REQUIRE(treeModel->entityId(tree->currentIndex()) == fixture.entity);
    REQUIRE(tree->selectionModel()->isSelected(tree->currentIndex()));
    REQUIRE(selection->text().contains(QStringLiteral("面：1 / 6")));
    REQUIRE(selection->text().contains(QStringLiteral("活动面：5")));
    REQUIRE(position->text().contains(QStringLiteral("Y  0.500")));
    recordEvidence(fixture, artifacts, QStringLiteral("S02"),
                   QStringLiteral("Tab/3/top-view mouse click selects native face 5; tree, "
                                  "active item and N readouts agree."),
                   {{"selectedFace", 5},
                    {"selectionText", selection->text()},
                    {"transformText", position->text()}});
    QSignalSpy picked(fixture.viewport, &renderer_gl::ViewportWidget::pickRequested);
    QJsonArray panelObservations;
    for (const auto key : {Qt::Key_T, Qt::Key_N, Qt::Key_T, Qt::Key_N}) {
        const auto previous = fixture.viewport->geometry();
        const bool shown =
            key == Qt::Key_T ? fixture.host->isToolbarVisible() : fixture.host->isSidebarVisible();
        fixture.pointAt(fixture.viewport->rect().center());
        QTest::keyClick(fixture.viewport, key);
        QTest::qWait(30);
        REQUIRE((key == Qt::Key_T ? fixture.host->isToolbarVisible()
                                  : fixture.host->isSidebarVisible()) != shown);
        if (key == Qt::Key_T) {
            REQUIRE(fixture.viewport->geometry() == previous);
            auto* toolbar =
                fixture.window.findChild<QWidget*>(QStringLiteral("ViewportToolbar"));
            REQUIRE(toolbar);
            REQUIRE(toolbar->parentWidget() == fixture.viewport);
            REQUIRE(fixture.viewport->rect().contains(toolbar->geometry()));
        } else {
            REQUIRE(fixture.viewport->geometry() != previous);
        }
        fixture.checkFrame();
        fixture.clickTopFace();
        REQUIRE(content(*fixture.model, fixture.entity).source == source);
        REQUIRE(fixture.model->undoStack()->index() == history);
        REQUIRE_FALSE(fixture.model->isModified());
        if (fixture.host->isSidebarVisible()) {
            auto* sidebar = fixture.window.findChild<QWidget*>(QStringLiteral("ViewportSidebar"));
            REQUIRE(sidebar);
            REQUIRE_FALSE(fixture.viewport->geometry().intersects(sidebar->geometry()));
            const auto before = fixture.model->componentSelection();
            const auto picks = picked.count();
            QTest::mouseClick(sidebar, Qt::LeftButton, Qt::NoModifier, sidebar->rect().center());
            REQUIRE(picked.count() == picks);
            REQUIRE(fixture.model->componentSelection() == before);
        }
        auto observation = displayEvidence(fixture);
        observation.insert("toolbarVisible", fixture.host->isToolbarVisible());
        observation.insert("sidebarVisible", fixture.host->isSidebarVisible());
        panelObservations.append(observation);
    }
    recordEvidence(fixture, artifacts, QStringLiteral("S08"),
                   QStringLiteral("T toggles the in-viewport toolbar without resizing GL; N changes "
                                  "viewport geometry; top picking survives all four changes and "
                                  "sidebar clicks do not reach picking."),
                   {{"panelChanges", 4}}, {}, panelObservations);
    const auto screens = QApplication::screens();
    QJsonArray screenObservations{displayEvidence(fixture)};
    bool crossedScreens = false, mixedDpi = false;
    QStringList screenLimitations;
    if (screens.size() >= 2) {
        auto* handle = fixture.window.windowHandle();
        auto* original = handle->screen();
        auto* other = screens.front() == original ? screens[1] : screens.front();
        const auto geometry = fixture.window.geometry();
        handle->setScreen(other);
        fixture.window.move(other->availableGeometry().topLeft() + QPoint(32, 32));
        REQUIRE(QTest::qWaitFor([handle, other] {
            return handle->screen() == other;
        }));
        REQUIRE(QTest::qWaitFor([&fixture, other] {
            return qFuzzyCompare(fixture.viewport->devicePixelRatioF(), other->devicePixelRatio());
        }));
        QTest::qWait(100);
        fixture.checkFrame();
        fixture.clickTopFace();
        REQUIRE(content(*fixture.model, fixture.entity).source == source);
        REQUIRE(fixture.model->undoStack()->index() == history);
        REQUIRE_FALSE(fixture.model->isModified());
        crossedScreens = true;
        mixedDpi = !qFuzzyCompare(original->devicePixelRatio(), other->devicePixelRatio());
        screenObservations.append(displayEvidence(fixture));
        if (!artifacts.capture.isEmpty())
            REQUIRE(fixture.window.grab().save(artifacts.path("S12-second-screen.png")));
        if (!mixedDpi) {
            screenLimitations.append(QStringLiteral("Real screens have equal DPR; mixed-DPI "
                                                    "crossing remains unverified."));
            WARN("S12 crossed real screens with equal DPR; mixed-DPI crossing remains unverified.");
        }
        handle->setScreen(original);
        fixture.window.setGeometry(geometry);
        QTest::qWait(100);
    } else {
        screenLimitations.append(QStringLiteral("Fewer than two real screens; cross-screen DPI "
                                                "remains unverified."));
        WARN("S12 actual cross-screen DPI unverified: fewer than two real screens are available.");
    }
    REQUIRE(objHash(*fixture.model, fixture.entity, false) == hash);
    REQUIRE(treeModel->entityId(tree->currentIndex()) == fixture.entity);
    recordEvidence(fixture, artifacts, QStringLiteral("S12"),
                   QStringLiteral("Actual frame dimensions and window/viewport DPR match the "
                                  "requested effective DPR; real screen moves preserve picking."),
                   {{"screenCount", screens.size()},
                    {"crossedRealScreens", crossedScreens},
                    {"mixedDpiCrossingVerified", mixedDpi}},
                   screenLimitations, screenObservations);
}

TEST_CASE("V2 native shell uses I E Ctrl R and survives cancel undo redo save and source OBJ",
          "[v2-acceptance][v2-shell][s06][s07]") {
    AcceptanceWindow fixture;
    ArtifactDirectory directory;
    fixture.enterNativeCube();
    const auto initialIndex = fixture.model->undoStack()->index();
    const auto native = content(*fixture.model, fixture.entity).source;
    const auto nativeContent = fixture.model->displayedEditableMesh(fixture.entity);
    const auto nativeSelection = fixture.model->componentSelection();
    REQUIRE(fixture.model->saveScene(directory.temporary.filePath("before-extrusion.m3dscene")));
    QTest::keyClick(fixture.viewport, Qt::Key_E);
    REQUIRE(fixture.modal->isActive());
    QTest::keyClicks(fixture.viewport, ".4");
    REQUIRE(fixture.model->componentPreview()->source.faces.size() == 10);
    REQUIRE(fixture.model->displayedEditableMesh(fixture.entity) != nativeContent);
    REQUIRE(content(*fixture.model, fixture.entity).source == native);
    QTest::keyClick(fixture.viewport, Qt::Key_Escape);
    REQUIRE_FALSE(fixture.modal->isActive());
    REQUIRE_FALSE(fixture.model->hasComponentTransform());
    REQUIRE(fixture.model->displayedEditableMesh(fixture.entity) == nativeContent);
    REQUIRE(fixture.model->componentSelection() == nativeSelection);
    REQUIRE(fixture.model->undoStack()->index() == initialIndex);
    REQUIRE(fixture.model->undoStack()->count() == initialIndex);
    REQUIRE_FALSE(fixture.model->isModified());
    recordEvidence(fixture, directory, QStringLiteral("S06"),
                   QStringLiteral("E/.4 previews topology; Esc restores the exact source content "
                                  "and selection with no history or dirty change."),
                   {{"previewFaces", 10}, {"restoredFaces", 6}, {"historyDelta", 0}});
    QTest::keyClick(fixture.viewport, Qt::Key_I);
    REQUIRE(fixture.modal->isActive());
    QTest::keyClicks(fixture.viewport, ".1");
    REQUIRE(content(*fixture.model, fixture.entity).source == native);
    QTest::keyClick(fixture.viewport, Qt::Key_Return);
    REQUIRE_FALSE(fixture.modal->isActive());
    REQUIRE(fixture.model->undoStack()->index() == initialIndex + 1);
    QTest::keyClick(fixture.viewport, Qt::Key_E);
    REQUIRE(fixture.modal->isActive());
    QTest::keyClicks(fixture.viewport, "-.25");
    QTest::keyClick(fixture.viewport, Qt::Key_Return);
    REQUIRE_FALSE(fixture.modal->isActive());
    REQUIRE(fixture.model->undoStack()->index() == initialIndex + 2);
    const auto shell = content(*fixture.model, fixture.entity).source;
    REQUIRE(shell.vertices.size() == 16);
    REQUIRE(shell.faces.size() == 14);
    const auto floor = std::find_if(shell.faces.begin(), shell.faces.end(), [](const auto& face) {
        return face.id == 5;
    });
    REQUIRE(floor != shell.faces.end());
    for (const auto& corner : floor->corners)
        REQUIRE(shell.vertex(corner.vertex)->position.y == .25F);
    for (const auto& vertex : shell.vertices)
        REQUIRE(vertex.position.y >= -.5F); // 凹槽没有穿透原生 Cube 底面。
    fixture.viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
    REQUIRE(fixture.viewport->focusSelection());
    QTest::qWait(30);
    REQUIRE(fixture.model->saveScene(directory.temporary.filePath("before-loop.m3dscene")));
    const auto before = fixture.model->displayedEditableMesh(fixture.entity);
    const auto selection = fixture.model->componentSelection();
    const auto index = fixture.model->undoStack()->index();
    const auto band = core::modeling::analyzeLoopCut(shell, {6, 7});
    INFO(band.error);
    REQUIRE(band.band);
    REQUIRE(band.band->closed);
    REQUIRE(band.band->faces.size() == 4);
    const auto startLoop = [&fixture] {
        const auto point = fixture.project({.5F, 0, .5F});
        fixture.pointAt(point);
        QTest::keyClick(fixture.viewport, Qt::Key_R, Qt::ControlModifier);
        REQUIRE(fixture.loop->stage() == editor::LoopCutStage::Preview);
        REQUIRE(fixture.model->componentPreview()->source.faces.size() == 18);
        return point;
    };
    for (int cycle = 0; cycle < 20; ++cycle) {
        startLoop();
        REQUIRE(fixture.model->displayedEditableMesh(fixture.entity) != before);
        REQUIRE(content(*fixture.model, fixture.entity).source == shell);
        QTest::keyClick(fixture.viewport, Qt::Key_Escape);
        REQUIRE(fixture.loop->stage() == editor::LoopCutStage::Inactive);
        REQUIRE_FALSE(fixture.model->hasComponentTransform());
        REQUIRE(fixture.model->displayedEditableMesh(fixture.entity) == before);
        REQUIRE(fixture.model->componentSelection() == selection);
        REQUIRE(fixture.model->undoStack()->index() == index);
        REQUIRE_FALSE(fixture.model->isModified());
    }
    const auto centerPoint = startLoop();
    QTest::mouseClick(fixture.viewport, Qt::LeftButton, Qt::NoModifier, centerPoint);
    REQUIRE(fixture.loop->stage() == editor::LoopCutStage::Slide);
    QTest::keyClicks(fixture.viewport, "20");
    QTest::mouseClick(fixture.viewport, Qt::RightButton, Qt::NoModifier, centerPoint);
    REQUIRE(fixture.loop->stage() == editor::LoopCutStage::Inactive);
    REQUIRE_FALSE(fixture.model->hasComponentTransform());
    const auto centered = core::modeling::loopCut(shell, {6, 7}, 0);
    REQUIRE(centered.mesh);
    REQUIRE(content(*fixture.model, fixture.entity).source == *centered.mesh);
    REQUIRE(fixture.model->undoStack()->index() == index + 1);
    REQUIRE(fixture.model->undoStack()->count() == index + 1);
    const auto centeredHash = objHash(*fixture.model, fixture.entity, false);
    if (!directory.capture.isEmpty())
        REQUIRE(fixture.window.grab().save(directory.path("S07-centered.png")));
    QTest::keyClick(fixture.viewport, Qt::Key_Z, Qt::ControlModifier);
    REQUIRE(fixture.model->displayedEditableMesh(fixture.entity) == before);
    REQUIRE(fixture.model->componentSelection() == selection);
    REQUIRE(fixture.model->undoStack()->index() == index);
    REQUIRE_FALSE(fixture.model->isModified());
    recordEvidence(fixture, directory, QStringLiteral("S07"),
                   QStringLiteral("Ctrl+R/left click/right click commits a centered cut, ignoring "
                                  "the prior slide value, as one history entry; Undo restores "
                                  "the exact original topology, selection and clean point."),
                   {{"slideInputBeforeRightClick", 20},
                    {"confirmedSlide", 0},
                    {"historyDeltaOnCommit", 1},
                    {"undoRestoredBefore", true},
                    {"centeredSourceObjSHA256", QString::fromLatin1(centeredHash)}});
    for (int cycle = 0; cycle < 20; ++cycle) {
        const auto point = startLoop();
        QTest::mouseClick(fixture.viewport, Qt::LeftButton, Qt::NoModifier, point);
        REQUIRE(fixture.loop->stage() == editor::LoopCutStage::Slide);
        QTest::keyClicks(fixture.viewport, "20");
        QTest::keyClick(fixture.viewport, Qt::Key_Return);
        REQUIRE(fixture.loop->stage() == editor::LoopCutStage::Inactive);
        REQUIRE_FALSE(fixture.model->hasComponentTransform());
        REQUIRE(fixture.model->undoStack()->index() == index + 1);
        REQUIRE(fixture.model->undoStack()->count() == index + 1);
        QTest::keyClick(fixture.viewport, Qt::Key_Z, Qt::ControlModifier);
        REQUIRE(fixture.model->displayedEditableMesh(fixture.entity) == before);
        REQUIRE(fixture.model->componentSelection() == selection);
        REQUIRE(fixture.model->undoStack()->index() == index);
        REQUIRE_FALSE(fixture.model->isModified());
    }
    QTest::keyClick(fixture.viewport, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    REQUIRE(fixture.model->undoStack()->index() == index + 1);
    const auto finished = content(*fixture.model, fixture.entity).source;
    REQUIRE(finished.vertices.size() == 20);
    REQUIRE(finished.faces.size() == 18);
    const auto validation = core::modeling::validateEditableMesh(finished);
    REQUIRE(validation.isValid());
    REQUIRE(validation.boundaryEdgeCount == 0);
    const auto saved = saveArtifact(fixture, directory, QStringLiteral("shell"), false);
    fixture.reopen(saved.scenePath);
    REQUIRE(content(*fixture.model, fixture.entity).source == finished);
    REQUIRE(objHash(*fixture.model, fixture.entity, false) == saved.sourceHash);
    fixture.model->setSelectionDomain(editor::SelectionDomain::Face);
    fixture.model->selectComponent({5}, editor::SelectionOperation::Replace);
    REQUIRE(fixture.model->beginInsetFace());
    REQUIRE(fixture.model->previewInsetFace(.02));
    REQUIRE(fixture.model->finishComponentTransform(false));
    REQUIRE(content(*fixture.model, fixture.entity).source == finished);
    REQUIRE_FALSE(fixture.model->hasComponentTransform());
    REQUIRE_FALSE(fixture.model->isModified());
    recordEvidence(fixture, directory, QStringLiteral("v2-shell"),
                   QStringLiteral("Native I/E/Ctrl+R shell survives 20 cancel and 20 commit/undo "
                                  "cycles, redo, save, reopen and source OBJ equality."),
                   {{"sourceVertices", 20},
                    {"sourceFaces", 18},
                    {"sourceObjSHA256", QString::fromLatin1(saved.sourceHash)}},
                   {QStringLiteral("Save/reopen/export and the final inset cancellation use "
                                   "public VM APIs; they are not file-dialog mouse evidence.")});
}

TEST_CASE("V2 S03 S04 F3 inset and Q favorite share native parameters and frozen context",
          "[v2-acceptance][v2-search-favorites][s03][s04]") {
    AcceptanceWindow fixture;
    ArtifactDirectory directory;
    fixture.enterNativeCube();
    const auto native = content(*fixture.model, fixture.entity).source;
    const auto initialIndex = fixture.model->undoStack()->index();
    auto* registry = fixture.window.findChild<editor::OperatorRegistry*>();
    auto* favorites = fixture.window.findChild<editor::QuickFavorites*>();
    auto* popup = fixture.window.findChild<editor::OperatorSearchPopup*>();
    auto* query = fixture.window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* results = fixture.window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"));
    auto* description =
        fixture.window.findChild<QLabel*>(QStringLiteral("OperatorSearchDescription"));
    REQUIRE(registry);
    REQUIRE(favorites);
    REQUIRE(popup);
    REQUIRE(query);
    REQUIRE(results);
    REQUIRE(description);
    const auto id = QStringLiteral("mesh.inset_face");
    REQUIRE(registry->descriptor(id));
    REQUIRE(registry->descriptor(id)->reopenable);
    QSignalSpy executed(registry, &editor::OperatorRegistry::executed);
    const auto openSearch = [&] {
        fixture.pointAt(fixture.viewport->rect().center());
        QTest::keyClick(fixture.viewport, Qt::Key_F3);
        REQUIRE(popup->isVisible());
        REQUIRE(query->hasFocus());
        REQUIRE(fixture.router->areaForWidget(query) == editor::InputArea::None);
        query->selectAll();
        QTest::keyClicks(query, "inset");
        REQUIRE(results->count() == 1);
        REQUIRE(results->currentItem()->data(Qt::UserRole).toString() == id);
        REQUIRE(description->text().contains(QStringLiteral("视口")));
    };
    const auto originalContext = registry->captureContext(editor::InputArea::Viewport);
    openSearch();
    query->setText(QStringLiteral("内插")); // 中文查询数据夹具，不宣称真实输入法验收。
    REQUIRE(results->count() == 1);
    REQUIRE(results->currentItem()->data(Qt::UserRole).toString() == id);
    // 公共入口故意改变冻结选区，验证旧弹窗拒绝；不是鼠标选择证据。
    fixture.model->selectComponent({4}, editor::SelectionOperation::Replace);
    const auto disabledReason = registry->disabledReason(id, originalContext);
    REQUIRE_FALSE(disabledReason.isEmpty());
    QTest::keyClick(query, Qt::Key_Return);
    REQUIRE(popup->isVisible());
    REQUIRE(description->text().contains(disabledReason));
    REQUIRE_FALSE(fixture.modal->isActive());
    REQUIRE(executed.isEmpty());
    REQUIRE(fixture.model->undoStack()->index() == initialIndex);
    QTest::keyClick(query, Qt::Key_Escape);
    REQUIRE_FALSE(popup->isVisible());
    fixture.clickTopFace();
    openSearch();
    const auto toggleFavorite = [&] {
        const auto point = results->visualItemRect(results->currentItem()).center();
        QContextMenuEvent context(QContextMenuEvent::Mouse, point,
                                  results->viewport()->mapToGlobal(point));
        QApplication::sendEvent(results->viewport(), &context);
        auto* menu = popup->findChild<QMenu*>(QStringLiteral("OperatorFavoriteContextMenu"));
        REQUIRE(menu);
        auto* action = menu->findChild<QAction*>(QStringLiteral("ToggleOperatorFavorite"));
        REQUIRE(action);
        REQUIRE(menu->isVisible());
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                          menu->actionGeometry(action).center());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    if (favorites->contains(id))
        toggleFavorite();
    toggleFavorite();
    REQUIRE(favorites->contains(id));
    if (!directory.capture.isEmpty())
        REQUIRE(popup->grab().save(directory.path("S03-search.png")));
    QTest::keyClick(query, Qt::Key_Return);
    REQUIRE_FALSE(popup->isVisible());
    REQUIRE(fixture.modal->isActive());
    REQUIRE(fixture.model->componentInset());
    REQUIRE(executed.count() == 1);
    REQUIRE(executed[0][0].toString() == id);
    QTest::keyClicks(fixture.viewport, ".1");
    QTest::keyClick(fixture.viewport, Qt::Key_Return);
    REQUIRE_FALSE(fixture.modal->isActive());
    const auto searched = content(*fixture.model, fixture.entity).source;
    const auto searchedHash = objHash(*fixture.model, fixture.entity, false);
    REQUIRE(searched.faces.size() == 10);
    REQUIRE(fixture.model->undoStack()->index() == initialIndex + 1);
    REQUIRE(fixture.model->lastOperationInsetThickness() == .1);
    recordEvidence(fixture, directory, QStringLiteral("S03"),
                   QStringLiteral("F3 English/Chinese queries resolve mesh.inset_face; frozen "
                                  "selection rejects stale execution with its reason; fresh "
                                  "viewport context runs .1 inset as one history entry."),
                   {{"operatorId", id},
                    {"disabledReason", disabledReason},
                    {"staleExecutionRejected", true},
                    {"thickness", .1},
                    {"historyDelta", 1}},
                   {QStringLiteral("Chinese query and stale selection are public text/VM "
                                   "fixtures; successful F3 execution uses Qt key events.")});
    fixture.pointAt(fixture.viewport->rect().center());
    QTest::keyClick(fixture.viewport, Qt::Key_Z, Qt::ControlModifier);
    REQUIRE(content(*fixture.model, fixture.entity).source == native);
    REQUIRE(fixture.model->undoStack()->index() == initialIndex);
    REQUIRE(executed.count() == 2);
    REQUIRE(executed[1][0].toString() == QStringLiteral("history.undo"));
    QTest::keyClick(fixture.viewport, Qt::Key_Q);
    REQUIRE(popup->isVisible());
    REQUIRE_FALSE(query->isVisible());
    REQUIRE(results->hasFocus());
    int favoriteRow = -1;
    for (int row = 0; row < results->count(); ++row)
        if (results->item(row)->data(Qt::UserRole).toString() == id)
            favoriteRow = row;
    REQUIRE(favoriteRow >= 0);
    for (int row = 0; row < favoriteRow; ++row)
        QTest::keyClick(results, Qt::Key_Down);
    REQUIRE(results->currentItem()->data(Qt::UserRole).toString() == id);
    if (!directory.capture.isEmpty())
        REQUIRE(popup->grab().save(directory.path("S04-favorites.png")));
    QTest::keyClick(results, Qt::Key_Return);
    REQUIRE(fixture.modal->isActive());
    QTest::keyClicks(fixture.viewport, ".1");
    QTest::keyClick(fixture.viewport, Qt::Key_Return);
    REQUIRE_FALSE(fixture.modal->isActive());
    REQUIRE(executed.count() == 3);
    REQUIRE(executed[2][0].toString() == id);
    REQUIRE(content(*fixture.model, fixture.entity).source == searched);
    REQUIRE(objHash(*fixture.model, fixture.entity, false) == searchedHash);
    REQUIRE(fixture.model->lastOperationInsetThickness() == .1);
    REQUIRE(fixture.model->undoStack()->index() == initialIndex + 1);
    REQUIRE(fixture.model->undoStack()->count() == initialIndex + 1);
    recordEvidence(fixture, directory, QStringLiteral("S04"),
                   QStringLiteral("Q executes the inset added through the F3 context menu; the "
                                  "same .1 parameter gives identical full source and OBJ hash."),
                   {{"operatorId", id},
                    {"thickness", .1},
                    {"historyDelta", 1},
                    {"f3SourceObjSHA256", QString::fromLatin1(searchedHash)}});
}

TEST_CASE("V2 S05 S11 native inset F9 replaces one saved command and guards closing",
          "[v2-acceptance][v2-inset-f9-close][s05][s11]") {
    AcceptanceWindow fixture;
    ArtifactDirectory directory;
    fixture.enterNativeCube();
    const auto native = content(*fixture.model, fixture.entity).source;
    QTest::keyClick(fixture.viewport, Qt::Key_I);
    REQUIRE(fixture.modal->isActive());
    QTest::keyClicks(fixture.viewport, ".1");
    QTest::keyClick(fixture.viewport, Qt::Key_Return);
    REQUIRE_FALSE(fixture.modal->isActive());
    const auto beforeAdjustment = content(*fixture.model, fixture.entity).source;
    const auto selection = fixture.model->componentSelection();
    const auto index = fixture.model->undoStack()->index();
    const auto* command = fixture.model->undoStack()->command(index - 1);
    const auto savedPath = directory.path("S11-before-f9.m3dscene");
    REQUIRE(fixture.model->saveScene(savedPath));
    REQUIRE_FALSE(fixture.model->isModified());
    auto* panel = fixture.window.findChild<editor::LastOperationPanel*>();
    REQUIRE(panel);
    auto* thickness =
        panel->findChild<editor::CommitSpinBox*>(QStringLiteral("LastOperationInsetThickness"));
    REQUIRE(thickness);
    fixture.pointAt(fixture.viewport->rect().center());
    QTest::keyClick(fixture.viewport, Qt::Key_F9);
    QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    REQUIRE(thickness->isVisible());
    REQUIRE(thickness->hasFocus());
    thickness->selectAll();
    QTest::keyClicks(thickness, ".2");
    auto* line = thickness->findChild<QLineEdit*>();
    REQUIRE(line);
    QTest::keyClick(line, Qt::Key_Return);
    REQUIRE(fixture.model->lastOperationInsetThickness() == .2);
    const auto adjusted = core::modeling::insetFace(native, 5, .2);
    REQUIRE(adjusted.mesh);
    REQUIRE(content(*fixture.model, fixture.entity).source == *adjusted.mesh);
    REQUIRE(content(*fixture.model, fixture.entity).source.faces.size() == 10);
    REQUIRE(fixture.model->componentSelection() == selection);
    REQUIRE(fixture.model->undoStack()->index() == index);
    REQUIRE(fixture.model->undoStack()->count() == index);
    REQUIRE(fixture.model->undoStack()->command(index - 1) == command);
    REQUIRE(fixture.model->isModified());
    recordEvidence(fixture, directory, QStringLiteral("S05"),
                   QStringLiteral("I/.1/Enter then real F9 thickness field .2/Enter recalculates "
                                  "from the native before and replaces the same command; faces "
                                  "remain 10, selection and history count remain unchanged."),
                   {{"initialThickness", .1},
                    {"adjustedThickness", .2},
                    {"sourceFaces", 10},
                    {"additionalHistoryFromF9", 0}});
    bool prompted = false;
    bool promptCaptureSaved = directory.capture.isEmpty();
    QString promptText;
    QTimer::singleShot(0, &fixture.window, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            prompted = true;
            promptText = box->text();
            if (!directory.capture.isEmpty())
                promptCaptureSaved = box->grab().save(directory.path("S11-unsaved-prompt.png"));
            auto* cancel = box->button(QMessageBox::Cancel);
            QTest::mouseClick(cancel, Qt::LeftButton);
        }
    });
    REQUIRE_FALSE(fixture.window.close());
    REQUIRE(prompted);
    REQUIRE(promptCaptureSaved);
    REQUIRE(fixture.window.isVisible());
    REQUIRE(fixture.model->isModified());
    REQUIRE(content(*fixture.model, fixture.entity).source == *adjusted.mesh);
    editor::SceneViewModel disk;
    REQUIRE(disk.openScene(savedPath));
    REQUIRE(content(disk, fixture.entity).source == beforeAdjustment);
    REQUIRE(fixture.model->undoStack()->index() == index);
    recordEvidence(
        fixture, directory, QStringLiteral("S11"),
        QStringLiteral("Save then F9 invalidates the clean point; real close asks "
                       "about unsaved edits; Cancel keeps the adjusted model open "
                       "while the saved file still contains the earlier inset."),
        {{"unsavedPromptObserved", prompted},
         {"promptText", promptText},
         {"closeCancelled", fixture.window.isVisible()},
         {"savedSourceObjSHA256", QString::fromLatin1(objHash(disk, fixture.entity, false))}},
        {QStringLiteral("Baseline save and disk verification use public VM APIs; "
                        "F9 field and the real unsaved dialog use Qt input.")});
}

TEST_CASE("V2 S10 native Local View keypad entry and exit preserve visibility history and frame",
          "[v2-acceptance][v2-local-view][s10]") {
    AcceptanceWindow fixture;
    ArtifactDirectory directory;
    fixture.window.findChild<QAction*>(QStringLiteral("ToggleSceneDock"))->setChecked(true);
    QTest::qWait(30);
    fixture.enterNativeCube();
    QTest::keyClick(fixture.viewport, Qt::Key_Tab);
    REQUIRE_FALSE(fixture.model->isEditMode());
    const auto outside = fixture.model->createEntity(core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position.x = 2;
    REQUIRE(fixture.model->setTransform(outside, transform));
    const auto hidden = fixture.model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(fixture.model->setVisible(hidden, false));
    auto* tree = fixture.window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    auto* treeModel = fixture.window.findChild<editor::SceneTreeModel*>();
    REQUIRE(tree);
    REQUIRE(treeModel);
    const auto cubeIndex = treeModel->indexForEntity(fixture.entity);
    tree->scrollTo(cubeIndex);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualRect(cubeIndex).center());
    REQUIRE(fixture.model->selection()->selectedEntity() == fixture.entity);
    fixture.viewport->setEditorCamera({{4, 3, 8}, {.5F, 0, 0}, 0, 50});
    fixture.viewport->setOverlayVisible(false);
    fixture.model->setCursorVisible(false);
    REQUIRE(fixture.model->saveScene(directory.path("S10-before-local-view.m3dscene")));
    const auto index = fixture.model->undoStack()->index();
    const auto source = content(*fixture.model, fixture.entity).source;
    const auto frame = fixture.viewport->grabFramebuffer();
    REQUIRE_FALSE(frame.isNull());
    fixture.pointAt(fixture.viewport->rect().center());
    QTest::keyClick(fixture.viewport, Qt::Key_Slash, Qt::KeypadModifier);
    const auto& visibility = fixture.model->viewportVisibility();
    REQUIRE(visibility.localRoot == fixture.entity);
    REQUIRE(visibility.isVisible(*fixture.model->scene(), fixture.entity));
    REQUIRE_FALSE(visibility.isVisible(*fixture.model->scene(), outside));
    REQUIRE_FALSE(visibility.isVisible(*fixture.model->scene(), hidden));
    REQUIRE(fixture.model->scene()->find(outside)->visible);
    REQUIRE_FALSE(fixture.model->scene()->find(hidden)->visible);
    REQUIRE(fixture.viewport->grabFramebuffer() != frame);
    REQUIRE(fixture.model->undoStack()->index() == index);
    REQUIRE_FALSE(fixture.model->isModified());
    if (!directory.capture.isEmpty())
        REQUIRE(fixture.window.grab().save(directory.path("S10-isolated.png")));
    QTest::keyClick(fixture.viewport, Qt::Key_Slash, Qt::KeypadModifier);
    REQUIRE(visibility.localRoot == 0);
    REQUIRE(visibility.isVisible(*fixture.model->scene(), outside));
    REQUIRE_FALSE(visibility.isVisible(*fixture.model->scene(), hidden));
    REQUIRE(fixture.model->scene()->find(outside)->visible);
    REQUIRE_FALSE(fixture.model->scene()->find(hidden)->visible);
    REQUIRE(content(*fixture.model, fixture.entity).source == source);
    REQUIRE(fixture.model->undoStack()->index() == index);
    REQUIRE(fixture.model->undoStack()->count() == index);
    REQUIRE_FALSE(fixture.model->isModified());
    REQUIRE(fixture.viewport->grabFramebuffer() == frame);
    recordEvidence(fixture, directory, QStringLiteral("S10"),
                   QStringLiteral("Keypad / isolates the tree-selected native Cube and returns "
                                  "to the identical frame; persistent visible flags, source, "
                                  "history and clean point stay unchanged."),
                   {{"outsidePersistentVisible", true},
                    {"hiddenPersistentVisible", false},
                    {"restoredFrameEqualsBefore", true},
                    {"historyDelta", 0}},
                   {QStringLiteral("Outside/hidden object preparation and baseline save use "
                                   "public VM fixtures; selection and Local View use Qt input.")});
}

TEST_CASE("V2 native half shell keeps Mirror clipping source distinction and reversible apply",
          "[v2-acceptance][v2-symmetric][s09]") {
    AcceptanceWindow fixture;
    ArtifactDirectory directory;
    fixture.enterNativeCube();
    const auto half = nativeHalfCube(content(*fixture.model, fixture.entity).source);
    REQUIRE(fixture.model->replaceEditableMesh(fixture.entity, half,
                                               QStringLiteral("原生Cube半壳夹具（非Bisect）")));
    auto* add = fixture.window.findChild<QPushButton*>(QStringLiteral("MirrorAddButton"));
    auto* merge = fixture.window.findChild<QCheckBox*>(QStringLiteral("MirrorMerge"));
    auto* clipping = fixture.window.findChild<QCheckBox*>(QStringLiteral("MirrorClipping"));
    REQUIRE(add);
    REQUIRE(merge);
    REQUIRE(clipping);
    clickModifier(fixture, add);
    REQUIRE(fixture.model->mirrorOptions(fixture.entity));
    REQUIRE(merge->isChecked());
    clickModifier(fixture, merge);
    REQUIRE_FALSE(merge->isChecked());
    REQUIRE(content(*fixture.model, fixture.entity).evaluatedMesh().vertices.size() == 16);
    clickModifier(fixture, merge);
    REQUIRE(merge->isChecked());
    REQUIRE(clipping->isChecked());
    clickModifier(fixture, clipping);
    REQUIRE_FALSE(fixture.model->mirrorOptions(fixture.entity)->clipping);
    clickModifier(fixture, clipping);
    REQUIRE(clipping->isChecked());
    const auto options = fixture.model->mirrorOptions(fixture.entity);
    REQUIRE(options);
    REQUIRE(options->merge);
    REQUIRE(options->clipping);
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    REQUIRE(content(*fixture.model, fixture.entity).evaluatedMesh().vertices.size() == 12);
    REQUIRE(content(*fixture.model, fixture.entity).evaluatedMesh().faces.size() == 10);
    std::set<editor::ComponentId> seam;
    for (const auto& vertex : half.vertices)
        if (vertex.position.x == 0)
            seam.insert({vertex.id});
    fixture.pointAt(fixture.viewport->rect().center());
    QTest::keyClick(fixture.viewport, Qt::Key_1);
    fixture.model->selectComponents(seam, editor::SelectionOperation::Replace);
    const auto selection = fixture.model->componentSelection();
    const auto index = fixture.model->undoStack()->index();
    QTest::keyClick(fixture.viewport, Qt::Key_G);
    REQUIRE(fixture.modal->isActive());
    QTest::keyClick(fixture.viewport, Qt::Key_X);
    QTest::keyClicks(fixture.viewport, "-.4");
    for (const auto id : seam)
        REQUIRE(fixture.model->componentPreview()->source.vertex(id.first)->position.x == 0);
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    QTest::keyClick(fixture.viewport, Qt::Key_Escape);
    REQUIRE_FALSE(fixture.modal->isActive());
    REQUIRE(fixture.model->componentSelection() == selection);
    REQUIRE(fixture.model->undoStack()->index() == index);
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    QTest::keyClick(fixture.viewport, Qt::Key_G);
    REQUIRE(fixture.modal->isActive());
    QTest::keyClick(fixture.viewport, Qt::Key_Y);
    QTest::keyClicks(fixture.viewport, ".05");
    QTest::keyClick(fixture.viewport, Qt::Key_Return);
    REQUIRE_FALSE(fixture.modal->isActive());
    REQUIRE(fixture.model->undoStack()->index() == index + 1);
    for (const auto id : seam)
        REQUIRE(content(*fixture.model, fixture.entity).source.vertex(id.first)->position.x == 0);
    const auto moved = content(*fixture.model, fixture.entity).source;
    REQUIRE(moved != half);
    QTest::keyClick(fixture.viewport, Qt::Key_Z, Qt::ControlModifier);
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    QTest::keyClick(fixture.viewport, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    REQUIRE(content(*fixture.model, fixture.entity).source == moved);
    QTest::keyClick(fixture.viewport, Qt::Key_Z, Qt::ControlModifier);
    fixture.viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
    REQUIRE(fixture.viewport->focusSelection());
    const auto saved = saveArtifact(fixture, directory, QStringLiteral("symmetric"), true);
    const auto evaluated = content(*fixture.model, fixture.entity).evaluatedMesh();
    const auto savedIndex = fixture.model->undoStack()->index();
    REQUIRE(fixture.model->applyMirror(fixture.entity));
    REQUIRE(fixture.model->undoStack()->index() == savedIndex + 1);
    REQUIRE_FALSE(fixture.model->mirrorOptions(fixture.entity));
    REQUIRE(content(*fixture.model, fixture.entity).source == evaluated);
    fixture.model->undo();
    REQUIRE(fixture.model->mirrorOptions(fixture.entity) == options);
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    REQUIRE_FALSE(fixture.model->isModified());
    fixture.model->redo();
    REQUIRE(content(*fixture.model, fixture.entity).source == evaluated);
    fixture.model->undo();
    fixture.reopen(saved.scenePath);
    REQUIRE(fixture.model->mirrorOptions(fixture.entity) == options);
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    REQUIRE(objHash(*fixture.model, fixture.entity, false) == saved.sourceHash);
    REQUIRE(objHash(*fixture.model, fixture.entity, true) == saved.evaluatedHash);
    REQUIRE(merge->isChecked() == options->merge);
    REQUIRE(clipping->isChecked() == options->clipping);
    recordEvidence(fixture, directory, QStringLiteral("S09"),
                   QStringLiteral("Real Mirror card toggles stay synchronized; G/X/-.4 pins "
                                  "source seam, Esc restores it, G/Y/.05 is one undoable edit; "
                                  "save/reopen retains distinct source and evaluated OBJ."),
                   {{"sourceVertices", 8},
                    {"sourceFaces", 5},
                    {"evaluatedVertices", 12},
                    {"evaluatedFaces", 10},
                    {"clipping", clipping->isChecked()},
                    {"merge", merge->isChecked()},
                    {"evaluatedObjSHA256", QString::fromLatin1(saved.evaluatedHash)}},
                   {QStringLiteral("Half-shell geometry and seam selection are explicit public "
                                   "VM fixtures, not UI Bisect or mouse selection evidence."),
                    QStringLiteral("Apply/save/reopen/export use public VM APIs.")});
}

TEST_CASE("V2 native subdivision levels and Mirror chain preserve the readonly cage on reopen",
          "[v2-acceptance][v2-subdivision]") {
    AcceptanceWindow fixture;
    ArtifactDirectory directory;
    fixture.enterNativeCube();
    const auto native = content(*fixture.model, fixture.entity).source;
    auto* add = fixture.window.findChild<QPushButton*>(QStringLiteral("SubdivisionAddButton"));
    auto* levels = fixture.window.findChild<QComboBox*>(QStringLiteral("SubdivisionLevels"));
    auto* apply = fixture.window.findChild<QPushButton*>(QStringLiteral("SubdivisionApplyButton"));
    REQUIRE(add);
    REQUIRE(levels);
    REQUIRE(apply);
    clickModifier(fixture, add);
    REQUIRE(content(*fixture.model, fixture.entity).source == native);
    REQUIRE(content(*fixture.model, fixture.entity).evaluatedMesh().faces.size() == 24);
    clickModifier(fixture, levels);
    QTest::keyClick(levels, Qt::Key_Home);
    QTest::keyClick(levels, Qt::Key_Down);
    QTest::keyClick(levels, Qt::Key_Return);
    REQUIRE(fixture.model->subdivisionOptions(fixture.entity));
    REQUIRE(fixture.model->subdivisionOptions(fixture.entity)->levels == 2);
    REQUIRE(content(*fixture.model, fixture.entity).source == native);
    REQUIRE(content(*fixture.model, fixture.entity).evaluatedMesh().faces.size() == 96);
    REQUIRE(content(*fixture.model, fixture.entity).evaluatedMesh().vertices.size() == 98);
    const auto nativePath = directory.temporary.filePath("native-subdivision.m3dscene");
    REQUIRE(fixture.model->saveScene(nativePath));
    fixture.reopen(nativePath);
    REQUIRE(fixture.model->subdivisionOptions(fixture.entity));
    REQUIRE(fixture.model->subdivisionOptions(fixture.entity)->levels == 2);
    REQUIRE(content(*fixture.model, fixture.entity).source == native);
    QTest::keyClick(fixture.viewport, Qt::Key_1);
    QTest::keyClick(fixture.viewport, Qt::Key_A);
    REQUIRE(fixture.model->componentSelection().selectedIds().size() == 8);
    const auto half = nativeHalfCube(native);
    REQUIRE(fixture.model->replaceEditableMesh(fixture.entity, half,
                                               QStringLiteral("原生Cube半壳夹具（非Bisect）")));
    REQUIRE(fixture.model->setMirrorOptions(fixture.entity, core::modeling::MirrorOptions{}));
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    REQUIRE(content(*fixture.model, fixture.entity).evaluatedMesh().faces.size() == 160);
    REQUIRE(content(*fixture.model, fixture.entity).evaluatedMesh().vertices.size() == 162);
    const auto evaluated = content(*fixture.model, fixture.entity).evaluatedMesh();
    const auto readonlyVertex = std::find_if(evaluated.vertices.begin(), evaluated.vertices.end(),
                                             [&half](const auto& vertex) {
                                                 return !half.vertex(vertex.id);
                                             });
    REQUIRE(readonlyVertex != evaluated.vertices.end());
    const auto selectionIndex = fixture.model->undoStack()->index();
    fixture.model->selectComponent({readonlyVertex->id}, editor::SelectionOperation::Replace);
    REQUIRE_FALSE(fixture.model->componentSelection().selectedIds().contains({readonlyVertex->id}));
    fixture.model->selectAllComponents();
    REQUIRE(fixture.model->componentSelection().selectedIds().size() == 8);
    REQUIRE(fixture.model->undoStack()->index() == selectionIndex);
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    const auto mirror = fixture.model->mirrorOptions(fixture.entity);
    const auto subdivision = fixture.model->subdivisionOptions(fixture.entity);
    fixture.viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
    REQUIRE(fixture.viewport->focusSelection());
    const auto saved = saveArtifact(fixture, directory, QStringLiteral("subdivision"), true);
    const auto index = fixture.model->undoStack()->index();
    clickModifier(fixture, apply);
    REQUIRE(fixture.model->undoStack()->index() == index + 1);
    REQUIRE_FALSE(fixture.model->mirrorOptions(fixture.entity));
    REQUIRE_FALSE(fixture.model->subdivisionOptions(fixture.entity));
    REQUIRE(content(*fixture.model, fixture.entity).source == evaluated);
    fixture.model->undo();
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    REQUIRE(fixture.model->mirrorOptions(fixture.entity) == mirror);
    REQUIRE(fixture.model->subdivisionOptions(fixture.entity) == subdivision);
    REQUIRE_FALSE(fixture.model->isModified());
    fixture.model->redo();
    REQUIRE(content(*fixture.model, fixture.entity).source == evaluated);
    fixture.model->undo();
    fixture.reopen(saved.scenePath);
    REQUIRE(content(*fixture.model, fixture.entity).source == half);
    REQUIRE(fixture.model->mirrorOptions(fixture.entity) == mirror);
    REQUIRE(fixture.model->subdivisionOptions(fixture.entity) == subdivision);
    REQUIRE(objHash(*fixture.model, fixture.entity, false) == saved.sourceHash);
    REQUIRE(objHash(*fixture.model, fixture.entity, true) == saved.evaluatedHash);
    recordEvidence(fixture, directory, QStringLiteral("v2-subdivision"),
                   QStringLiteral("Real Subdivision card level/apply input preserves the readonly "
                                  "8-vertex source cage; Mirror chain, undo/redo and reopen retain "
                                  "160 faces/162 evaluated vertices and OBJ hashes."),
                   {{"sourceVertices", 8},
                    {"sourceFaces", 5},
                    {"evaluatedVertices", 162},
                    {"evaluatedFaces", 160},
                    {"evaluatedObjSHA256", QString::fromLatin1(saved.evaluatedHash)}},
                   {QStringLiteral("Half-shell/Mirror setup, readonly-ID rejection, save/reopen "
                                   "and OBJ export use public VM fixtures.")});
}
