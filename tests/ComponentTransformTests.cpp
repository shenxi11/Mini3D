/*
 * 模块名: ComponentTransformTests
 * 功能概述: 验证组件事务、真实 G/R/S 输入、独立预览、历史和文件闭环。
 * 对外接口: Catch2 [component-transform] / [component-transform-ui]。
 * 依赖关系: SceneViewModel、Qt Test、真实 GL 视口、纯 Core 变换数学。
 * 输入输出: 稳定点边面选择与输入到真源/候选、帧缓冲、历史和保存断言。
 * 异常与错误: 候选泄漏或错误提交即失败；不把合成输入等同真实 IME 验收。
 * 维护说明: 独立临时目录和测试窗口，不修改用户场景或关闭用户程序。
 */
#include "core/modeling/VertexTransform.h"
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/ComponentInteraction.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <glm/ext/matrix_transform.hpp>

using namespace mini3d;
using editor::ComponentId;
using editor::SelectionDomain;
using editor::SelectionOperation;
namespace {
const core::EditableMeshRecord& record(const editor::SceneViewModel& model, core::EntityId id) {
    return *model.scene()->editableMesh(model.scene()->find(id)->editableMesh);
}
glm::dmat4 translation(double x) {
    return glm::translate(glm::dmat4(1), glm::dvec3(x, 0, 0));
}
struct ComponentWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::ObjectTransformSession* modal = window.findChild<editor::ObjectTransformSession*>();
    editor::KeymapRouter* router = window.findChild<editor::KeymapRouter*>();
    core::EntityId cube;
    QPoint pointer;
    ComponentWindow() {
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        router->setKeymap(editor::EditorKeymap::Blender);
        model->newScene();
        cube = model->createEntity(core::PrimitiveKind::Cube);
        viewport->setEditorCamera({{0, 0, 5}, {0, 0, 0}, 0, 50});
        REQUIRE(model->setEditMode(true));
        pointer = {viewport->width() * 3 / 4, viewport->height() / 2};
        pointAt(pointer);
    }
    ~ComponentWindow() {
        window.hide();
    }
    QAction* action(const char* name) {
        return window.findChild<QAction*>(QString::fromLatin1(name));
    }
    void pointAt(QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        viewport->setFocus();
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, modifiers);
        QApplication::sendEvent(viewport, &event);
    }
    void start(Qt::Key key) {
        pointAt(pointer);
        QTest::keyClick(viewport, key);
        REQUIRE(modal->isActive());
        REQUIRE(model->hasComponentTransform());
    }
};
} // namespace

TEST_CASE("Component preview is separate from scene revisions dirty history and tree publication",
          "[component-transform]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(SelectionDomain::Edge);
    model.selectComponent({1, 2}, SelectionOperation::Replace);
    const auto selection = model.componentSelection();
    const auto before = record(model, id);
    const auto transform = model.scene()->find(id)->transform;
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("组件变换.m3dscene"));
    REQUIRE(model.saveScene(path));
    const auto index = model.undoStack()->index();
    const auto selectionRevision = model.componentSelectionRevision();
    QSignalSpy sceneChanges(&model, &editor::SceneViewModel::sceneChanged);
    QSignalSpy treeChanges(&model, &editor::SceneViewModel::structureChanged);
    REQUIRE(model.beginComponentTransform());
    for (double delta : {0.2, 0.3, 0.1}) {
        REQUIRE(model.previewComponentTransform(translation(delta)));
        REQUIRE(record(model, id).content == before.content);
        REQUIRE(record(model, id).evaluationRevision == before.evaluationRevision);
        REQUIRE(record(model, id).topologyRevision == before.topologyRevision);
        REQUIRE(record(model, id).geometryRevision == before.geometryRevision);
        REQUIRE(model.componentSelectionRevision() == selectionRevision);
        REQUIRE(model.undoStack()->index() == index);
        REQUIRE_FALSE(model.isModified());
        REQUIRE(model.componentPreview() == model.displayedEditableMesh(id));
        REQUIRE(model.componentPreview()->source.vertex(1)->position ==
                glm::vec3(-0.5 + delta, -0.5, -0.5));
        REQUIRE(model.componentPreview()->source.vertex(2)->position ==
                glm::vec3(0.5 + delta, -0.5, -0.5));
        REQUIRE(model.componentPreview()->source.vertex(3)->position ==
                before.content->source.vertex(3)->position);
    }
    REQUIRE(sceneChanges.empty());
    REQUIRE(treeChanges.empty());
    const auto after = model.componentPreview();
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE_FALSE(model.hasComponentTransform());
    REQUIRE(record(model, id).content == after);
    REQUIRE(record(model, id).evaluationRevision > before.evaluationRevision);
    REQUIRE(model.undoStack()->index() == index + 1);
    REQUIRE(model.isModified());
    REQUIRE(sceneChanges.count() == 1);
    REQUIRE(treeChanges.empty());
    REQUIRE(model.scene()->find(id)->transform.localMatrix() == transform.localMatrix());
    model.selectComponent({3, 4}, SelectionOperation::Replace);
    auto revision = record(model, id).evaluationRevision;
    model.undo();
    REQUIRE(record(model, id).content == before.content);
    REQUIRE(record(model, id).evaluationRevision > revision);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE_FALSE(model.isModified());
    model.clearComponentSelection();
    revision = record(model, id).evaluationRevision;
    model.redo();
    REQUIRE(record(model, id).content == after);
    REQUIRE(record(model, id).evaluationRevision > revision);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE(model.saveScene(path));
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(record(reopened, id).content->source == after->source);
}

TEST_CASE("Invalid and cancelled component candidates keep original content and redo",
          "[component-transform]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE_FALSE(model.beginComponentTransform());
    REQUIRE(model.setEditMode(true));
    const auto original = record(model, id).content;
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(translation(1)));
    REQUIRE(model.finishComponentTransform(true));
    model.undo();
    const auto index = model.undoStack()->index();
    const auto count = model.undoStack()->count();
    const auto revision = record(model, id).evaluationRevision;
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(translation(0.5)));
    const auto valid = model.componentPreview();
    REQUIRE_FALSE(model.previewComponentTransform(glm::scale(glm::dmat4(1), glm::dvec3(0))));
    REQUIRE(model.componentPreview() == valid);
    REQUIRE_FALSE(model.finishComponentTransform(true));
    REQUIRE(model.hasComponentTransform());
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE(model.displayedEditableMesh(id) == original);
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(glm::dmat4(1)));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(record(model, id).content == original);
    REQUIRE(record(model, id).evaluationRevision == revision);
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE(model.undoStack()->canRedo());
    model.clearComponentSelection();
    REQUIRE_FALSE(model.beginComponentTransform());
}

TEST_CASE("Component transactions cancel across selection mode data and document boundaries",
          "[component-transform]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    const auto before = record(model, id).content;
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(translation(1)));
    SECTION("selection") {
        model.selectComponent({1}, SelectionOperation::Replace);
    }
    SECTION("domain") {
        model.setSelectionDomain(SelectionDomain::Face);
    }
    SECTION("mode") {
        REQUIRE(model.setEditMode(false));
    }
    SECTION("visibility") {
        REQUIRE(model.setVisible(id, false));
    }
    SECTION("other data") {
        REQUIRE(model.renameEntity(id, QStringLiteral("改名")));
    }
    SECTION("save uses confirmed content") {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("before.m3dscene"));
        REQUIRE(model.saveScene(path));
        editor::SceneViewModel reopened;
        REQUIRE(reopened.openScene(path));
        REQUIRE(record(reopened, id).content->source == before->source);
    }
    SECTION("new document") {
        model.newScene();
        REQUIRE_FALSE(model.hasComponentTransform());
        REQUIRE_FALSE(model.componentPreview());
        return;
    }
    REQUIRE_FALSE(model.hasComponentTransform());
    REQUIRE_FALSE(model.componentPreview());
    REQUIRE(record(model, id).content == before);
}

TEST_CASE("Point edge face GRS changes only source vertices under a negative nonuniform parent",
          "[component-transform-ui]") {
    const auto domain =
        GENERATE(SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face);
    const auto operation = GENERATE(Qt::Key_G, Qt::Key_R, Qt::Key_S);
    ComponentWindow f;
    REQUIRE(f.model->setEditMode(false));
    const auto parent = f.model->createEntity(core::PrimitiveKind::Empty);
    core::Transform parentTransform;
    parentTransform.scale = {-2, 1.5F, 0.8F};
    parentTransform.rotation = glm::angleAxis(glm::radians(25.0F), glm::vec3(0, 1, 0));
    REQUIRE(f.model->setTransform(parent, parentTransform));
    REQUIRE(f.model->setParent(f.cube, parent));
    f.model->selection()->setSelectedEntity(f.cube);
    REQUIRE(f.model->setEditMode(true));
    f.model->setSelectionDomain(domain);
    f.model->selectComponent(domain == SelectionDomain::Edge ? ComponentId{1, 2} : ComponentId{1},
                             SelectionOperation::Replace);
    const auto before = record(*f.model, f.cube).content;
    const auto vertices = f.model->componentSelection().selectedVertices(before->source);
    const auto world = glm::dmat4(f.model->scene()->worldMatrix(f.cube));
    const auto center = *f.model->selectedComponentCenter();
    const auto index = f.model->undoStack()->index();
    f.start(operation);
    glm::dmat4 delta(1);
    if (operation == Qt::Key_G) {
        QTest::keyClicks(f.viewport, "x.25");
        delta = translation(0.25);
    } else if (operation == Qt::Key_R) {
        QTest::keyClicks(f.viewport, "y15");
        delta = glm::translate(glm::dmat4(1), center) *
                glm::rotate(glm::dmat4(1), glm::radians(15.0), glm::dvec3(0, 1, 0)) *
                glm::translate(glm::dmat4(1), -center);
    } else {
        QTest::keyClick(f.viewport, Qt::Key_Z, Qt::ShiftModifier);
        QTest::keyClicks(f.viewport, ".8");
        delta = glm::translate(glm::dmat4(1), center) *
                glm::scale(glm::dmat4(1), glm::dvec3(0.8, 0.8, 1)) *
                glm::translate(glm::dmat4(1), -center);
    }
    REQUIRE(record(*f.model, f.cube).content == before);
    REQUIRE(f.model->undoStack()->index() == index);
    const auto preview = f.model->componentPreview();
    for (const auto& vertex : before->source.vertices) {
        const auto position = world * glm::dvec4(vertex.position, 1);
        const auto expected = vertices.contains(vertex.id) ? delta * position : position;
        const auto actual = world * glm::dvec4(preview->source.vertex(vertex.id)->position, 1);
        REQUIRE(glm::length(expected - actual) < 1.0e-5);
    }
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(record(*f.model, f.cube).content->source == preview->source);
    REQUIRE(glm::dmat4(f.model->scene()->worldMatrix(f.cube)) == world);
    const bool changed = before->source != preview->source;
    REQUIRE(f.model->undoStack()->index() == index + (changed ? 1 : 0));
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("GRS.m3dscene"));
    REQUIRE(f.model->saveScene(path));
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(record(reopened, f.cube).content->source == preview->source);
    if (changed) {
        f.model->undo();
        REQUIRE(record(*f.model, f.cube).content == before);
        f.model->redo();
        REQUIRE(record(*f.model, f.cube).content == preview);
    }
}

TEST_CASE("Component preview reaches the same GL context and sidebar then cancels exactly",
          "[component-transform-ui]") {
    ComponentWindow f;
    f.action("ToggleViewportSidebar")->setChecked(true);
    f.model->setSelectionDomain(SelectionDomain::Face);
    f.model->selectComponent({1}, SelectionOperation::Replace);
    f.viewport->setOverlayVisible(false); // 即使关闭选区绘制，实体候选也必须实际上传并改变画面。
    QTest::qWait(50);
    const auto before = record(*f.model, f.cube).content;
    const auto image = f.viewport->grabFramebuffer();
    REQUIRE_FALSE(image.isNull());
    const auto context = f.viewport->context();
    const auto index = f.model->undoStack()->index();
    f.start(Qt::Key_G);
    QTest::keyClicks(f.viewport, "z1");
    QTest::qWait(40);
    REQUIRE(record(*f.model, f.cube).content == before);
    REQUIRE(f.viewport->context() == context);
    REQUIRE(f.viewport->grabFramebuffer() != image);
    REQUIRE(f.window.findChild<QLabel*>(QStringLiteral("SidebarTransform"))
                ->text()
                .contains(QStringLiteral("1.500")));
    auto* hud = f.window.findChild<QLabel*>(QStringLiteral("ObjectTransformHud"));
    REQUIRE(hud->isVisible());
    REQUIRE(hud->text().contains(QStringLiteral("组件")));
    const auto capture = qEnvironmentVariable("MINI3D_TEST_COMPONENT_TRANSFORM_CAPTURE");
    if (!capture.isEmpty()) {
        f.viewport->setOverlayVisible(true);
        REQUIRE(f.window.grab().save(capture));
        f.viewport->setOverlayVisible(false);
    }
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    QTest::qWait(40);
    REQUIRE(f.viewport->grabFramebuffer() == image);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE_FALSE(hud->isVisible());
    REQUIRE(f.model->undoStack()->index() == index);
    REQUIRE(record(*f.model, f.cube).content == before);
    REQUIRE(f.window.findChild<QLabel*>(QStringLiteral("SidebarTransform"))
                ->text()
                .contains(QStringLiteral("0.500")));
}

TEST_CASE("Component modal invalid numbers and lifecycle exits cannot commit a stale preview",
          "[component-transform-ui]") {
    ComponentWindow f;
    const auto before = record(*f.model, f.cube).content;
    const auto index = f.model->undoStack()->index();
    f.start(Qt::Key_S);
    QTest::keyClicks(f.viewport, "2");
    QTest::keyClick(f.viewport, Qt::Key_Backspace);
    QTest::keyClicks(f.viewport, "0");
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == index);
    QTest::keyClick(f.viewport, Qt::Key_Backspace);
    QTest::keyClicks(f.viewport, "1");
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == index);
    for (auto eventType :
         {QEvent::FocusOut, QEvent::WindowDeactivate, QEvent::UngrabMouse, QEvent::Resize}) {
        f.start(Qt::Key_G);
        QTest::keyClicks(f.viewport, "x.25");
        if (eventType == QEvent::FocusOut) {
            QFocusEvent event(QEvent::FocusOut);
            QApplication::sendEvent(f.viewport, &event);
        } else if (eventType == QEvent::Resize) {
            QResizeEvent event(f.viewport->size(), f.viewport->size());
            QApplication::sendEvent(f.viewport, &event);
        } else {
            QEvent event(eventType);
            QApplication::sendEvent(f.viewport, &event);
        }
        REQUIRE_FALSE(f.modal->isActive());
        REQUIRE_FALSE(f.model->hasComponentTransform());
        REQUIRE(record(*f.model, f.cube).content == before);
        REQUIRE(f.model->undoStack()->index() == index);
    }
    f.start(Qt::Key_G);
    QTest::keyClicks(f.viewport, "x1");
    QTest::mouseClick(f.viewport, Qt::RightButton, Qt::NoModifier, f.pointer);
    REQUIRE_FALSE(f.modal->isActive());
    f.start(Qt::Key_G);
    QTest::keyClicks(f.viewport, "x1");
    QTest::keyClick(f.viewport, Qt::Key_Tab);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE_FALSE(f.model->isEditMode());
    REQUIRE(record(*f.model, f.cube).content == before);
    REQUIRE(f.model->undoStack()->index() == index);
}

TEST_CASE("Component actions share F3 Q and menus with mode-specific shortcuts and text priority",
          "[component-transform-ui]") {
    ComponentWindow f;
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    auto* favorites = f.window.findChild<editor::QuickFavorites*>();
    auto* popup = f.window.findChild<editor::OperatorSearchPopup*>();
    auto* query = f.window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    REQUIRE(f.action("TransformComponentsMove")->shortcut() == QKeySequence(QStringLiteral("G")));
    REQUIRE(f.action("TransformMove")->shortcut().isEmpty());
    REQUIRE_FALSE(f.action("TransformMove")->isEnabled());
    REQUIRE(registry->descriptor(QStringLiteral("mesh.translate"))->action ==
            f.action("TransformComponentsMove"));
    REQUIRE(favorites->addOperator(QStringLiteral("mesh.translate")));
    const auto before = record(*f.model, f.cube).content;
    const auto index = f.model->undoStack()->index();
    for (auto key : {Qt::Key_F3, Qt::Key_Q}) {
        f.pointAt(f.pointer);
        QTest::keyClick(f.viewport, key);
        REQUIRE(popup->isVisible());
        query->setText(QStringLiteral("Translate Components"));
        QTest::keyClick(query, Qt::Key_Return);
        REQUIRE_FALSE(popup->isVisible());
        REQUIRE(f.modal->isActive());
        QTest::keyClicks(f.viewport, "x.5");
        REQUIRE(f.model->componentPreview()->source != before->source);
        QTest::keyClick(f.viewport, Qt::Key_Escape);
        REQUIRE(record(*f.model, f.cube).content == before);
    }
    auto* name = f.window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    name->setFocus();
    QTest::keyClicks(name, "grs");
    REQUIRE_FALSE(f.modal->isActive());
    QInputMethodEvent preedit(QStringLiteral("zhong"), {});
    QApplication::sendEvent(name, &preedit);
    QTest::keyClick(name, Qt::Key_G);
    REQUIRE_FALSE(f.modal->isActive());
    QInputMethodEvent clear;
    QApplication::sendEvent(name, &clear);
    name->setText(QString::fromStdString(f.model->scene()->find(f.cube)->name));
    f.pointAt(f.pointer);
    f.router->setKeymap(editor::EditorKeymap::Legacy);
    REQUIRE(f.action("TransformComponentsMove")->shortcut().isEmpty());
    QTest::keyClick(f.viewport, Qt::Key_G);
    REQUIRE_FALSE(f.modal->isActive());
    f.action("TransformComponentsMove")->trigger();
    REQUIRE(f.modal->isActive());
    QTest::keyClicks(f.viewport, "x.5");
    QTest::mouseClick(f.viewport, Qt::LeftButton, Qt::NoModifier, f.pointer);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == index + 1);
    f.model->undo();
    REQUIRE(record(*f.model, f.cube).content == before);
    REQUIRE(f.model->setEditMode(false));
    f.router->setKeymap(editor::EditorKeymap::Blender);
    REQUIRE(f.action("TransformMove")->shortcut() == QKeySequence(QStringLiteral("G")));
    REQUIRE(f.action("TransformComponentsMove")->shortcut().isEmpty());
    REQUIRE_FALSE(f.action("TransformComponentsMove")->isEnabled());
}

TEST_CASE("Component input preserves local axes fine motion snap inversion and box cancellation",
          "[component-transform-ui]") {
    ComponentWindow f;
    REQUIRE(f.model->setEditMode(false));
    REQUIRE(f.model->setTransformComponent(f.cube, 1, 2, 37));
    REQUIRE(f.model->setEditMode(true));
    const auto before = record(*f.model, f.cube).content;
    const auto world = f.model->scene()->worldMatrix(f.cube);
    const auto index = f.model->undoStack()->index();
    const auto displacement = [&] {
        return glm::vec3(world * glm::vec4(f.model->componentPreview()->source.vertex(1)->position -
                                               before->source.vertex(1)->position,
                                           0));
    };
    f.start(Qt::Key_G);
    QTest::keyClicks(f.viewport, "x.5");
    REQUIRE(glm::distance(displacement(), glm::vec3(0.5F, 0, 0)) < 1.0e-5F);
    QTest::keyClick(f.viewport, Qt::Key_X);
    REQUIRE(f.modal->statusText().contains(QStringLiteral("局部")));
    REQUIRE(glm::distance(displacement(), glm::vec3(world[0]) * 0.5F) < 1.0e-5F);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    f.action("LocalTransformSpace")->trigger();
    f.start(Qt::Key_S);
    QTest::keyClicks(f.viewport, "1");
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.model->undoStack()->index() == index);
    REQUIRE(record(*f.model, f.cube).content == before);
    f.action("WorldTransformSpace")->trigger();
    f.action("SnapTransform")->setChecked(false);
    f.start(Qt::Key_G);
    QTest::keyClick(f.viewport, Qt::Key_X);
    const auto units = f.viewport->editorCameraSnapshot()->worldUnitsPerPixel({0, 0, 0});
    f.pointAt(f.pointer + QPoint(30, 0));
    const auto normal = displacement();
    QTest::keyPress(f.viewport, Qt::Key_Shift);
    REQUIRE(glm::distance(displacement(), normal) < 1.0e-5F);
    f.pointAt(f.pointer + QPoint(50, 0), Qt::ShiftModifier);
    REQUIRE(std::abs(displacement().x - (30 + 2) * units) < 1.0e-5F);
    QTest::keyRelease(f.viewport, Qt::Key_Shift);
    REQUIRE(std::abs(displacement().x - 32 * units) < 1.0e-5F);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    for (bool preference : {false, true}) {
        f.action("SnapTransform")->setChecked(preference);
        f.start(Qt::Key_G);
        QTest::keyClick(f.viewport, Qt::Key_X);
        const int pixels = qRound(0.3F / units);
        f.pointAt(f.pointer + QPoint(pixels, 0));
        const float raw = pixels * units;
        REQUIRE(std::abs(displacement().x - (preference ? 0.5F : raw)) < 1.0e-5F);
        QTest::keyPress(f.viewport, Qt::Key_Control);
        REQUIRE(std::abs(displacement().x - (preference ? raw : 0.5F)) < 1.0e-5F);
        QTest::keyRelease(f.viewport, Qt::Key_Control);
        QTest::keyClicks(f.viewport, ".37");
        REQUIRE(std::abs(displacement().x - 0.37F) < 1.0e-5F);
        QTest::keyClick(f.viewport, Qt::Key_Escape);
    }
    auto* box = f.window.findChild<editor::ComponentInteraction*>();
    f.start(Qt::Key_G);
    QTest::keyClicks(f.viewport, "x1");
    REQUIRE(box->startBoxSelection());
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE_FALSE(f.model->hasComponentTransform());
    REQUIRE(record(*f.model, f.cube).content == before);
    f.action("TransformComponentsMove")->trigger();
    REQUIRE(f.modal->isActive());
    REQUIRE_FALSE(box->isBoxSelecting());
    f.viewport->setCameraView(renderer_gl::EditorView::Top);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == index);
}
