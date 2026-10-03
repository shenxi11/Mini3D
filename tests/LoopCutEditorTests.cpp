/*
 * 模块名: LoopCutEditorTests
 * 功能概述: 验证环切候选、两阶段交互、右键居中、完整取消与唯一历史。
 * 对外接口: Catch2 [loop-cut-editor]/[loop-cut-ui]；依赖关系: Qt Test、真实GL、临时文件。
 * 输入输出: 源边/百分比和窗口事件到网格、选区、历史、文件与截图断言。
 * 异常与错误: 非法输入不确认；不关闭用户程序、不写用户工程。
 * 维护说明: 单屏DPI和合成IME不代替跨屏/真实输入法验收。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/ComponentInteraction.h"
#include "editor/operations/ComponentPicker.h"
#include "editor/operations/LoopCutSession.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
using editor::LoopCutStage;
namespace {
core::EntityId enterLoop(editor::SceneViewModel& model) {
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    return entity;
}
const core::EditableMeshRecord& record(const editor::SceneViewModel& model, core::EntityId entity) {
    return *model.scene()->editableMesh(model.scene()->find(entity)->editableMesh);
}
struct LoopWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::LoopCutSession* loop = window.findChild<editor::LoopCutSession*>();
    editor::KeymapRouter* router = window.findChild<editor::KeymapRouter*>();
    core::EntityId entity;
    QPoint edgePoint;
    LoopWindow() {
        REQUIRE(loop);
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        entity = enterLoop(*model);
        router->setKeymap(editor::EditorKeymap::Blender);
        window.findChild<QAction*>(QStringLiteral("ToggleViewportSidebar"))->setChecked(true);
        viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
        QTest::qWait(30);
        const auto camera = *viewport->editorCameraSnapshot();
        const auto clip = camera.viewProjectionMatrix() * glm::vec4(.5F, 0, .5F, 1);
        edgePoint = {qRound((clip.x / clip.w + 1) * viewport->width() * .5F),
                     qRound((1 - clip.y / clip.w) * viewport->height() * .5F)};
        REQUIRE(editor::pickComponent(
                    *model->scene(), *model->assets(), entity, editor::SelectionDomain::Edge,
                    camera, {edgePoint.x(), edgePoint.y()},
                    {viewport->width(), viewport->height()}) == editor::ComponentId::edge({6, 7}));
        pointAt(edgePoint);
    }
    ~LoopWindow() {
        window.hide();
    }
    void pointAt(QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        viewport->setFocus();
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, modifiers);
        QApplication::sendEvent(viewport, &event);
    }
    void start() {
        pointAt(edgePoint);
        QTest::keyClick(viewport, Qt::Key_R, Qt::ControlModifier);
        REQUIRE(loop->stage() == LoopCutStage::Preview);
        REQUIRE(model->componentPreview()->source.faces.size() == 10);
    }
    void slide() {
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, edgePoint);
        REQUIRE(loop->stage() == LoopCutStage::Slide);
    }
};
} // namespace

TEST_CASE("Loop cut preview keeps source selection until one history installs the new ring",
          "[loop-cut-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterLoop(model);
    const auto before = record(model, entity);
    const auto selection = model.componentSelection();
    const auto revision = model.componentSelectionRevision();
    const auto count = model.undoStack()->count();
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("环切保存.m3dscene"));
    REQUIRE(model.saveScene(path));
    REQUIRE(model.beginLoopCut());
    REQUIRE_FALSE(model.finishComponentTransform(true));
    for (double value : {0., .25, -.25}) {
        REQUIRE(model.previewLoopCut(core::modeling::EdgeKey(6, 7), value));
        REQUIRE(record(model, entity).content == before.content);
        REQUIRE(record(model, entity).evaluationRevision == before.evaluationRevision);
        REQUIRE(model.componentSelection() == selection);
        REQUIRE(model.componentSelectionRevision() == revision);
        REQUIRE(model.displayedComponentSelection().domain() == editor::SelectionDomain::Edge);
        REQUIRE(model.displayedComponentSelection().selectedIds().size() == 4);
        REQUIRE(model.undoStack()->count() == count);
        REQUIRE_FALSE(model.isModified());
    }
    const auto after = model.componentPreview();
    const auto afterSelection = model.displayedComponentSelection();
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(record(model, entity).content == after);
    REQUIRE(model.componentSelection() == afterSelection);
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE_FALSE(model.lastOperationDisabledReason().isEmpty());
    model.undo();
    REQUIRE(record(model, entity).content == before.content);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE_FALSE(model.isModified());
    model.redo();
    REQUIRE(record(model, entity).content == after);
    REQUIRE(model.componentSelection() == afterSelection);
    REQUIRE(record(model, entity).evaluationRevision > before.evaluationRevision);
    REQUIRE(model.saveScene(path));
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(record(reopened, entity).content->source == after->source);
}

TEST_CASE("Loop cut rejects invalid targets and parameters and cancels even with empty original "
          "selection",
          "[loop-cut-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterLoop(model);
    model.clearComponentSelection();
    const auto before = record(model, entity);
    const auto selection = model.componentSelection();
    REQUIRE(model.beginLoopCut());
    REQUIRE(model.previewLoopCut(core::modeling::EdgeKey(6, 7), 0));
    REQUIRE_FALSE(model.previewLoopCut(core::modeling::EdgeKey(1, 7), 0));
    REQUIRE(model.componentPreview() == before.content);
    REQUIRE(model.displayedComponentSelection() == selection);
    REQUIRE_FALSE(model.finishComponentTransform(true));
    REQUIRE_FALSE(model.previewLoopCut(std::nullopt, 0));
    REQUIRE_FALSE(model.previewLoopCut(core::modeling::EdgeKey(6, 7), 1));
    REQUIRE_FALSE(model.previewComponentTransform(glm::dmat4(1)));
    REQUIRE(model.previewLoopCut(core::modeling::EdgeKey(6, 7), .5));
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE(record(model, entity).content == before.content);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE_FALSE(model.hasComponentTransform());
    model.selectAllComponents();
    REQUIRE(model.beginComponentTransform());
    REQUIRE_FALSE(model.previewLoopCut(core::modeling::EdgeKey(6, 7), 0));
    REQUIRE(model.finishComponentTransform(false));
}

TEST_CASE("Ctrl R preview slide and right click center commit exactly one loop and GPU undo",
          "[loop-cut-ui]") {
    LoopWindow f;
    const auto before = record(*f.model, f.entity);
    const auto selection = f.model->componentSelection();
    const auto count = f.model->undoStack()->count();
    const auto frame = f.viewport->grabFramebuffer();
    auto* context = f.viewport->context();
    f.start();
    REQUIRE(f.viewport->grabFramebuffer() != frame);
    REQUIRE(f.model->undoStack()->count() == count);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_LOOP_CAPTURE");
    auto* sidebar = f.window.findChild<QLabel*>(QStringLiteral("SidebarSelection"));
    REQUIRE(sidebar);
    REQUIRE(sidebar->isVisible());
    REQUIRE(sidebar->text().contains(QStringLiteral("边：4 / 20")));
    auto* hud = f.viewport->findChild<QLabel*>(QStringLiteral("LoopCutHud"));
    REQUIRE(hud);
    QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    REQUIRE(f.viewport->rect().contains(hud->geometry()));
    REQUIRE(hud->height() >= hud->heightForWidth(hud->width()));
    if (!capture.isEmpty())
        REQUIRE(f.window.grab().save(capture + QStringLiteral("-preview.png")));
    f.slide();
    REQUIRE(f.model->undoStack()->count() == count);
    REQUIRE(record(*f.model, f.entity).content == before.content);
    QTest::keyClicks(f.viewport, "35");
    REQUIRE(f.loop->statusText().contains(QStringLiteral("35%")));
    REQUIRE(f.model->componentPreview()->source ==
            *core::modeling::loopCut(before.content->source, {6, 7}, .35).mesh);
    if (!capture.isEmpty()) {
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        REQUIRE(f.window.grab().save(capture + QStringLiteral("-slide.png")));
    }
    REQUIRE(f.viewport->rect().contains(hud->geometry()));
    REQUIRE(hud->height() >= hud->heightForWidth(hud->width()));
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::NoModifier, f.edgePoint);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(f.model->undoStack()->count() == count + 1);
    REQUIRE(record(*f.model, f.entity).content->source ==
            *core::modeling::loopCut(before.content->source, {6, 7}, 0).mesh);
    REQUIRE(f.model->componentSelection().selectedIds().size() == 4);
    REQUIRE(f.model->componentSelection().domain() == editor::SelectionDomain::Edge);
    if (!capture.isEmpty()) {
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        REQUIRE(f.window.grab().save(capture + QStringLiteral("-center.png")));
    }
    f.model->undo();
    REQUIRE(record(*f.model, f.entity).content == before.content);
    REQUIRE(f.model->componentSelection() == selection);
    REQUIRE(sidebar->text().contains(QStringLiteral("面：1 / 6")));
    REQUIRE(f.viewport->grabFramebuffer() == frame);
    REQUIRE(f.viewport->context() == context);
}

TEST_CASE("Loop preview right click and slide Escape both restore the entire operation",
          "[loop-cut-ui]") {
    LoopWindow f;
    const auto before = record(*f.model, f.entity);
    const auto selection = f.model->componentSelection();
    const auto count = f.model->undoStack()->count();
    f.start();
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::NoModifier, f.edgePoint);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(record(*f.model, f.entity).content == before.content);
    f.start();
    f.slide();
    QTest::keyClicks(f.viewport, "-40");
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(record(*f.model, f.entity).content == before.content);
    REQUIRE(f.model->componentSelection() == selection);
    REQUIRE(f.model->undoStack()->count() == count);
    f.start();
    f.pointAt({f.viewport->width() - 20, 20});
    REQUIRE(f.model->componentPreview() == before.content);
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.loop->stage() == LoopCutStage::Preview);
    f.pointAt(f.edgePoint);
    f.slide();
    QTest::keyClicks(f.viewport, "100");
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.loop->stage() == LoopCutStage::Slide);
    REQUIRE(f.model->undoStack()->count() == count);
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::NoModifier, f.edgePoint);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(f.model->undoStack()->count() == count + 1);
}

TEST_CASE("Loop mouse fine input snap reversal and explicit percent stay relative to before",
          "[loop-cut-ui]") {
    LoopWindow f;
    const auto before = record(*f.model, f.entity);
    f.start();
    f.slide();
    const auto expect = [&](double value) {
        const auto expected = core::modeling::loopCut(before.content->source, {6, 7}, value);
        REQUIRE(expected.mesh);
        REQUIRE(f.model->componentPreview()->source == *expected.mesh);
    };
    f.pointAt(f.edgePoint + QPoint(50, 0));
    expect(.25);
    f.pointAt(f.edgePoint + QPoint(70, 0), Qt::ShiftModifier);
    expect(.26);
    f.pointAt(f.edgePoint + QPoint(80, 0));
    expect(.31);
    QTest::keyPress(f.viewport, Qt::Key_Control);
    expect(.3);
    QTest::keyRelease(f.viewport, Qt::Key_Control);
    expect(.31);
    QTest::keyClicks(f.viewport, "12.3");
    expect(.123);
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(record(*f.model, f.entity).content->source ==
            *core::modeling::loopCut(before.content->source, {6, 7}, .123).mesh);
}

TEST_CASE("Loop menu F3 Q and Legacy share the modal while text and stale contexts are protected",
          "[loop-cut-ui]") {
    LoopWindow f;
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    auto* favorites = f.window.findChild<editor::QuickFavorites*>();
    auto* popup = f.window.findChild<editor::OperatorSearchPopup*>();
    auto* query = popup->findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* results = popup->findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"));
    REQUIRE(query);
    REQUIRE(results);
    REQUIRE(registry->descriptor(QStringLiteral("mesh.loop_cut")));
    REQUIRE(favorites->addOperator(QStringLiteral("mesh.loop_cut")));
    for (auto key : {Qt::Key_F3, Qt::Key_Q}) {
        f.pointAt(f.edgePoint);
        QTest::keyClick(f.viewport, key);
        REQUIRE(popup->isVisible());
        if (key == Qt::Key_F3)
            QTest::keyClicks(query, "loop cut");
        REQUIRE(results->count() == 1);
        results->setCurrentRow(0);
        QTest::keyClick(results, Qt::Key_Return);
        REQUIRE(f.loop->stage() == LoopCutStage::Preview);
        f.pointAt(f.edgePoint);
        f.slide();
        QTest::keyClick(f.viewport, Qt::Key_Escape);
    }
    const auto stale =
        registry->captureContext(editor::InputArea::Viewport, editor::EditorKeymap::Blender);
    f.model->clearComponentSelection();
    REQUIRE_FALSE(registry->execute(QStringLiteral("mesh.loop_cut"), stale));
    f.router->setKeymap(editor::EditorKeymap::Legacy);
    f.pointAt(f.edgePoint);
    QTest::keyClick(f.viewport, Qt::Key_R, Qt::ControlModifier);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    f.window.findChild<QAction*>(QStringLiteral("LoopCut"))->trigger();
    REQUIRE(f.loop->stage() == LoopCutStage::Preview);
    f.loop->cancel();
    auto* name = f.window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    REQUIRE(name);
    name->setFocus();
    QTest::keyClick(name, Qt::Key_R, Qt::ControlModifier);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
}

TEST_CASE(
    "Loop context interruptions and save discard preview and do not coexist with other modal tools",
    "[loop-cut-ui]") {
    LoopWindow f;
    const auto before = record(*f.model, f.entity);
    const auto count = f.model->undoStack()->count();
    f.start();
    f.slide();
    QInputMethodEvent input(QStringLiteral("中"), {});
    QApplication::sendEvent(f.viewport, &input);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    QTest::keyClick(f.viewport, Qt::Key_R, Qt::ControlModifier);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    QInputMethodEvent endInput;
    QApplication::sendEvent(f.viewport, &endInput);
    f.start();
    QEvent deactivated(QEvent::WindowDeactivate);
    QApplication::sendEvent(&f.window, &deactivated);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    f.start();
    auto* modal = f.window.findChild<editor::ObjectTransformSession*>();
    REQUIRE(modal->start(editor::TransformOperation::Move, editor::TransformTarget::Components));
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(f.loop->start());
    REQUIRE_FALSE(modal->isActive());
    auto* box = f.window.findChild<editor::ComponentInteraction*>();
    REQUIRE(box->startBoxSelection());
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(f.loop->start());
    REQUIRE_FALSE(box->isBoxSelecting());
    f.pointAt(f.edgePoint);
    f.slide();
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("环切中途保存.m3dscene"));
    REQUIRE(f.model->saveScene(path));
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(record(*f.model, f.entity).content == before.content);
    REQUIRE(f.model->undoStack()->count() == count);
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(record(reopened, f.entity).content->source == before.content->source);
}

TEST_CASE("Loop cut parent transform stays unchanged and later component edits use the new ring",
          "[loop-cut-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    core::Transform transform;
    transform.position = {3, -2, 5};
    transform.scale = {-2, 1.5F, .5F};
    transform.rotation = glm::angleAxis(glm::radians(35.F), glm::vec3(0, 1, 0));
    REQUIRE(model.setTransform(parent, transform));
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(entity, parent));
    REQUIRE(model.setEditMode(true));
    model.clearComponentSelection();
    const auto before = record(model, entity);
    const auto world = model.scene()->worldMatrix(entity);
    REQUIRE(model.beginLoopCut());
    REQUIRE(model.previewLoopCut(core::modeling::EdgeKey(6, 7), .25));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.scene()->worldMatrix(entity) == world);
    REQUIRE(record(model, entity).content->source ==
            *core::modeling::loopCut(before.content->source, {6, 7}, .25).mesh);
    const auto ring = model.componentSelection();
    const auto afterCut = record(model, entity);
    REQUIRE(model.beginComponentTransform());
    auto translation = glm::dmat4(1);
    translation[3].y = .1;
    REQUIRE(model.previewComponentTransform(translation));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.scene()->worldMatrix(entity) == world);
    REQUIRE(model.componentSelection() == ring);
    model.undo();
    REQUIRE(record(model, entity).content == afterCut.content);
}

TEST_CASE("Loop Enter auto repeat cannot skip Slide and resizing or outside click cancels safely",
          "[loop-cut-ui]") {
    LoopWindow f;
    const auto before = record(*f.model, f.entity);
    const auto count = f.model->undoStack()->count();
    f.start();
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.loop->stage() == LoopCutStage::Slide);
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(f.viewport, &repeat);
    REQUIRE(f.loop->stage() == LoopCutStage::Slide);
    REQUIRE(f.model->undoStack()->count() == count);
    QMouseEvent outside(QEvent::MouseButtonPress, QPointF(-10, -10),
                        f.viewport->mapToGlobal(QPoint(-10, -10)), Qt::RightButton, Qt::RightButton,
                        Qt::NoModifier);
    QApplication::sendEvent(f.viewport, &outside);
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(record(*f.model, f.entity).content == before.content);
    f.start();
    f.window.resize(f.window.width() - 80, f.window.height());
    QApplication::processEvents();
    REQUIRE(f.loop->stage() == LoopCutStage::Inactive);
    REQUIRE(record(*f.model, f.entity).content == before.content);
    REQUIRE(f.model->undoStack()->count() == count);
}
