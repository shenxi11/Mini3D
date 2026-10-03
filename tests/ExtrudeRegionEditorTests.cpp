/*
 * 模块名: ExtrudeRegionEditorTests
 * 功能概述: 验证区域挤出的独立候选、安全取消、真实输入、唯一历史和文件往返。
 * 对外接口: Catch2 [extrude-editor] / [extrude-ui]；依赖关系: Qt Test、真实 GL、SceneViewModel。
 * 输入输出: 面选择与 E/F3/Q/鼠标到拓扑、侧栏、GPU、历史与临时文件断言。
 * 异常与错误: 零值/退化不得确认，取消不得留拓扑；不替代全局自交/真实 IME 验收。
 * 维护说明: 独立窗口与临时文件，不关闭用户程序或写用户设置。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QFocusEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
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
glm::dmat4 offset(double z) {
    return glm::translate(glm::dmat4(1), glm::dvec3(0, 0, z));
}
glm::vec3 capPosition(const core::EditableMeshContent& content) {
    return content.source.vertex(content.source.faces[0].corners[0].vertex)->position;
}
void requireSidebarHintFits(QWidget& window) {
    QLabel* hint = nullptr;
    for (auto* label : window.findChildren<QLabel*>()) {
        if (label->text().startsWith(QStringLiteral("组件变换预览："))) {
            hint = label;
            break;
        }
    }
    REQUIRE(hint);
    INFO("before layout: width=" << hint->width() << " height=" << hint->height()
                                 << " required=" << hint->heightForWidth(hint->width()));
    // 合成按键不会让出事件循环；截图前先交付正常的换行布局请求，不添加任意等待。
    QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    INFO("after layout: width=" << hint->width() << " height=" << hint->height()
                                << " required=" << hint->heightForWidth(hint->width()));
    REQUIRE(hint->isVisible());
    REQUIRE(hint->height() >= hint->heightForWidth(hint->width()));
}
struct ExtrudeWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::ObjectTransformSession* modal = window.findChild<editor::ObjectTransformSession*>();
    core::EntityId cube;
    QPoint pointer;
    ExtrudeWindow() {
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        model->newScene();
        cube = model->createEntity(core::PrimitiveKind::Cube);
        REQUIRE(model->setEditMode(true));
        model->setSelectionDomain(SelectionDomain::Face);
        model->selectComponent({1}, SelectionOperation::Replace);
        viewport->setEditorCamera({{0, 0, 5}, {0, 0, 0}, 0, 50});
        window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Blender);
        action("SnapTransform")->setChecked(false);
        QTest::qWait(30);
        pointer = {viewport->width() * 2 / 3, viewport->height() / 2};
        pointAt(pointer);
    }
    ~ExtrudeWindow() {
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
    void start() {
        pointAt(pointer);
        QTest::keyClick(viewport, Qt::Key_E);
        REQUIRE(modal->isActive());
        REQUIRE(model->componentExtrusion());
    }
};
} // namespace

TEST_CASE("Extrude transaction derives every preview from before and commits one topology snapshot",
          "[extrude-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    REQUIRE_FALSE(model.beginExtrudeRegion());
    model.setSelectionDomain(SelectionDomain::Face);
    model.selectComponent({1}, SelectionOperation::Replace);
    const auto before = record(model, id);
    const auto selection = model.componentSelection();
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("区域挤出.m3dscene"));
    REQUIRE(model.saveScene(path));
    const auto index = model.undoStack()->index();
    QSignalSpy sceneChanges(&model, &editor::SceneViewModel::sceneChanged);
    REQUIRE(model.beginExtrudeRegion());
    REQUIRE_FALSE(model.finishComponentTransform(true));
    for (double distance : {0.25, 0.5, -0.25}) {
        REQUIRE(model.previewComponentTransform(offset(distance)));
        REQUIRE(model.componentPreview()->source.faces.size() == 10);
        REQUIRE(model.componentPreview()->source.vertices.size() == 12);
        REQUIRE(capPosition(*model.componentPreview()).z == float(0.5 + distance));
        REQUIRE(record(model, id).content == before.content);
        REQUIRE(record(model, id).evaluationRevision == before.evaluationRevision);
        REQUIRE(model.undoStack()->index() == index);
        REQUIRE_FALSE(model.isModified());
    }
    REQUIRE(sceneChanges.empty());
    const auto after = model.componentPreview();
    REQUIRE_FALSE(model.previewComponentTransform(offset(0)));
    REQUIRE(model.componentPreview() == after);
    REQUIRE_FALSE(model.finishComponentTransform(true));
    REQUIRE(model.previewComponentTransform(offset(-0.25)));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.undoStack()->index() == index + 1);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE(sceneChanges.count() == 1);
    REQUIRE(record(model, id).content->source == after->source);
    REQUIRE(model.saveScene(path));
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(record(reopened, id).content->source == after->source);
    model.clearComponentSelection();
    model.undo();
    REQUIRE(record(model, id).content == before.content);
    REQUIRE(record(model, id).evaluationRevision > before.evaluationRevision);
    REQUIRE(model.componentSelection() == selection);
    model.redo();
    REQUIRE(record(model, id).content->source == after->source);
    REQUIRE(model.beginComponentTransform());
    REQUIRE_FALSE(model.componentExtrusion());
    REQUIRE(model.previewComponentTransform(offset(0.1)));
    REQUIRE(model.componentPreview()->source.faces.size() == 10);
    REQUIRE(model.finishComponentTransform(false));
}

TEST_CASE("Extrude uses world normal under negative parent and rejects nontranslation preview",
          "[extrude-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    core::Transform transform;
    transform.scale = {-2, 1.5F, 0.5F};
    transform.rotation = glm::angleAxis(glm::radians(35.0F), glm::vec3(0, 1, 0));
    REQUIRE(model.setTransform(parent, transform));
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(id, parent));
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(SelectionDomain::Face);
    model.selectComponent({1}, SelectionOperation::Replace);
    const auto before = record(model, id).content;
    const auto world = glm::dmat4(model.scene()->worldMatrix(id));
    REQUIRE(model.beginExtrudeRegion());
    const auto direction = model.componentExtrusion()->normal;
    const auto expected =
        glm::normalize(glm::transpose(glm::inverse(glm::dmat3(world))) * glm::dvec3(0, 0, 1));
    REQUIRE(glm::length(direction - expected) < 1.0e-10);
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1), direction * 0.3)));
    REQUIRE(glm::length(glm::dvec3(world * glm::dvec4(capPosition(*model.componentPreview()) -
                                                          capPosition(*before),
                                                      0)) -
                        direction * 0.3) < 1.0e-6);
    REQUIRE_FALSE(model.previewComponentTransform(glm::scale(glm::dmat4(1), glm::dvec3(2))));
    REQUIRE_FALSE(model.finishComponentTransform(true));
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE(record(model, id).content == before);
}

TEST_CASE("E preview changes real mesh and counts then Escape restores the exact GPU frame",
          "[extrude-ui]") {
    ExtrudeWindow f;
    f.action("ToggleViewportSidebar")->setChecked(true);
    f.viewport->setOverlayVisible(false);
    QTest::qWait(30);
    const auto before = record(*f.model, f.cube).content;
    const auto selection = f.model->componentSelection();
    const auto image = f.viewport->grabFramebuffer();
    const auto context = f.viewport->context();
    const auto index = f.model->undoStack()->index();
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    QSignalSpy failures(registry, &editor::OperatorRegistry::executionFailed);
    f.start();
    REQUIRE(failures.empty());
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.modal->isActive());
    QTest::keyClicks(f.viewport, ".5");
    REQUIRE(f.model->componentPreview()->source.faces.size() == 10);
    REQUIRE(f.window.findChild<QLabel*>(QStringLiteral("InspectorMessage"))
                ->text()
                .contains(QStringLiteral("整对象变换已锁定")));
    REQUIRE(record(*f.model, f.cube).content == before);
    REQUIRE(f.viewport->context() == context);
    REQUIRE(f.viewport->grabFramebuffer() != image);
    REQUIRE(f.window.findChild<QLabel*>(QStringLiteral("SidebarSelection"))
                ->text()
                .contains(QStringLiteral("1 / 10")));
    auto* hud = f.window.findChild<QLabel*>(QStringLiteral("ObjectTransformHud"));
    REQUIRE(hud->text().contains(QStringLiteral("安全取消")));
    requireSidebarHintFits(f.window);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_EXTRUDE_CAPTURE");
    if (!capture.isEmpty()) {
        f.viewport->setOverlayVisible(true);
        REQUIRE(f.window.grab().save(capture));
        f.viewport->setOverlayVisible(false);
    }
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE_FALSE(f.model->componentPreview());
    REQUIRE(f.model->componentSelection() == selection);
    REQUIRE(f.model->undoStack()->index() == index);
    REQUIRE(f.viewport->grabFramebuffer() == image);
    REQUIRE(f.window.findChild<QLabel*>(QStringLiteral("SidebarSelection"))
                ->text()
                .contains(QStringLiteral("1 / 6")));
}

TEST_CASE(
    "Extrusion negative input confirms one history and zero or degenerate walls cannot commit",
    "[extrude-ui]") {
    ExtrudeWindow f;
    const auto before = record(*f.model, f.cube).content;
    const auto index = f.model->undoStack()->index();
    f.start();
    QTest::keyClicks(f.viewport, "0");
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.modal->isActive());
    REQUIRE(f.modal->statusText().contains(QStringLiteral("不为零")));
    QTest::keyClick(f.viewport, Qt::Key_Backspace);
    QTest::keyClicks(f.viewport, "-.25");
    REQUIRE(capPosition(*f.model->componentPreview()).z == 0.25F);
    QTest::keyClick(f.viewport, Qt::Key_X);
    REQUIRE(f.modal->statusText().contains(QStringLiteral("无效")));
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == index);
    QTest::keyClick(f.viewport, Qt::Key_Z);
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == index + 1);
    REQUIRE(record(*f.model, f.cube).content->source.faces.size() == 10);
    REQUIRE(f.model->componentSelection().selectedIds() == std::set<ComponentId>{{1}});
    f.model->undo();
    REQUIRE(record(*f.model, f.cube).content == before);
    f.model->redo();
    REQUIRE(capPosition(*record(*f.model, f.cube).content).z == 0.25F);
}

TEST_CASE("Extrusion interruptions recover topology and save only confirmed geometry",
          "[extrude-ui]") {
    const auto interruption = GENERATE(0, 1, 2, 3, 4, 5);
    ExtrudeWindow f;
    const auto before = record(*f.model, f.cube).content;
    const auto index = f.model->undoStack()->index();
    f.start();
    QTest::keyClicks(f.viewport, ".25");
    if (interruption == 0)
        QTest::mouseClick(f.viewport, Qt::RightButton, Qt::NoModifier, f.pointer);
    if (interruption == 1) {
        QFocusEvent event(QEvent::FocusOut);
        QApplication::sendEvent(f.viewport, &event);
    }
    if (interruption == 2)
        QTest::keyClick(f.viewport, Qt::Key_Tab);
    if (interruption == 3)
        f.model->selectComponent({5}, SelectionOperation::Replace);
    if (interruption == 4) {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("cancelled.m3dscene"));
        REQUIRE(f.model->saveScene(path));
        editor::SceneViewModel reopened;
        REQUIRE(reopened.openScene(path));
        REQUIRE(record(reopened, f.cube).content->source == before->source);
    }
    if (interruption == 5)
        f.model->newScene();
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE_FALSE(f.model->componentPreview());
    if (interruption != 5) {
        REQUIRE(record(*f.model, f.cube).content == before);
        REQUIRE(f.model->undoStack()->index() == index);
    }
}

TEST_CASE("Extrusion F3 Q and Legacy menu share an action and invalid regions give reasons",
          "[extrude-ui]") {
    ExtrudeWindow f;
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    auto* favorites = f.window.findChild<editor::QuickFavorites*>();
    auto* popup = f.window.findChild<editor::OperatorSearchPopup*>();
    auto* query = f.window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    REQUIRE(registry->descriptor(QStringLiteral("mesh.extrude_region"))->action ==
            f.action("ExtrudeRegion"));
    REQUIRE(favorites->addOperator(QStringLiteral("mesh.extrude_region")));
    for (auto key : {Qt::Key_F3, Qt::Key_Q}) {
        f.pointAt(f.pointer);
        QTest::keyClick(f.viewport, key);
        REQUIRE(popup->isVisible());
        query->setText(QStringLiteral("extrude"));
        QTest::keyClick(query, Qt::Key_Return);
        REQUIRE_FALSE(popup->isVisible());
        REQUIRE(f.modal->isActive());
        QTest::keyClicks(f.viewport, ".25");
        QTest::keyClick(f.viewport, Qt::Key_Escape);
    }
    f.window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Legacy);
    f.pointAt(f.pointer);
    QTest::keyClick(f.viewport, Qt::Key_E);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.action("ExtrudeRegion")->shortcut().isEmpty());
    f.action("ExtrudeRegion")->trigger();
    REQUIRE(f.modal->isActive());
    QTest::keyClicks(f.viewport, ".25");
    QTest::mouseClick(f.viewport, Qt::LeftButton, Qt::NoModifier, f.pointer);
    REQUIRE_FALSE(f.modal->isActive());
    f.model->undo();
    f.model->selectAllComponents();
    REQUIRE(registry
                ->disabledReason(QStringLiteral("mesh.extrude_region"),
                                 registry->captureContext(editor::InputArea::Viewport))
                .contains(QStringLiteral("边界")));
    f.model->selectComponents({{1}, {2}}, SelectionOperation::Replace);
    REQUIRE(registry
                ->disabledReason(QStringLiteral("mesh.extrude_region"),
                                 registry->captureContext(editor::InputArea::Viewport))
                .contains(QStringLiteral("连通")));
}

TEST_CASE("View-aligned extrusion supports mouse distance fine continuity and normal step snapping",
          "[extrude-ui]") {
    ExtrudeWindow f;
    f.start();
    const auto units = f.viewport->editorCameraSnapshot()->worldUnitsPerPixel({0, 0, 0.5F});
    f.pointAt(f.pointer + QPoint(0, -20));
    const auto first = capPosition(*f.model->componentPreview()).z;
    REQUIRE(std::abs(first - (0.5F + units * 20)) < 1.0e-5F);
    QTest::keyPress(f.viewport, Qt::Key_Shift);
    REQUIRE(capPosition(*f.model->componentPreview()).z == first);
    f.pointAt(f.pointer + QPoint(0, -40), Qt::ShiftModifier);
    REQUIRE(std::abs(capPosition(*f.model->componentPreview()).z - (0.5F + units * 22)) < 1.0e-5F);
    QTest::keyRelease(f.viewport, Qt::Key_Shift);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    f.start();
    const int pixels = qRound(0.3F / units);
    f.pointAt(f.pointer + QPoint(0, -pixels));
    QTest::keyPress(f.viewport, Qt::Key_Control);
    REQUIRE(std::abs(capPosition(*f.model->componentPreview()).z - 1.0F) < 1.0e-5F);
    QTest::keyRelease(f.viewport, Qt::Key_Control);
    REQUIRE(std::abs(capPosition(*f.model->componentPreview()).z - (0.5F + pixels * units)) <
            1.0e-5F);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
}

TEST_CASE("E extrudes a noncoplanar face region and keeps both caps selected across history",
          "[extrude-ui]") {
    ExtrudeWindow f;
    f.action("ToggleViewportSidebar")->setChecked(true);
    f.viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
    QTest::qWait(30);
    f.model->selectComponents({{1}, {5}}, SelectionOperation::Replace);
    const auto selection = f.model->componentSelection();
    const auto before = record(*f.model, f.cube).content;
    const auto index = f.model->undoStack()->index();
    f.start();
    REQUIRE_FALSE(f.model->componentExtrusion()->usesFallbackNormal);
    REQUIRE(glm::length(f.model->componentExtrusion()->normal -
                        glm::normalize(glm::dvec3(0, 1, 1))) < 1.0e-10);
    QTest::keyClicks(f.viewport, ".5");
    const auto after = f.model->componentPreview();
    REQUIRE(after->source.faces.size() == 12);
    REQUIRE(after->source.vertices.size() == 14);
    const auto displacement = capPosition(*after) - capPosition(*before);
    REQUIRE(glm::length(displacement - glm::normalize(glm::vec3(0, 1, 1)) * 0.5F) < 1.0e-6F);
    requireSidebarHintFits(f.window);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_EXTRUDE_CAPTURE");
    if (!capture.isEmpty())
        REQUIRE(f.window.grab().save(capture + QStringLiteral(".region.png")));
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->componentSelection() == selection);
    REQUIRE(f.model->undoStack()->index() == index + 1);
    REQUIRE(record(*f.model, f.cube).content->source == after->source);
    f.model->undo();
    REQUIRE(record(*f.model, f.cube).content == before);
    REQUIRE(f.model->componentSelection() == selection);
    f.model->redo();
    REQUIRE(record(*f.model, f.cube).content->source == after->source);
    f.start();
    QTest::keyClicks(f.viewport, ".25");
    REQUIRE(f.model->componentPreview()->source.faces.size() == 18);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(record(*f.model, f.cube).content->source == after->source);
    REQUIRE(f.model->undoStack()->index() == index + 1);
}

TEST_CASE("Cancelling normals are labelled and an axis can correct degenerate extrusion walls",
          "[extrude-ui]") {
    ExtrudeWindow f;
    f.model->selectComponents({{1}, {2}, {3}, {4}}, SelectionOperation::Replace);
    const auto index = f.model->undoStack()->index();
    f.start();
    REQUIRE(f.model->componentExtrusion()->usesFallbackNormal);
    REQUIRE(f.modal->statusText().contains(QStringLiteral("替代面法线")));
    QTest::keyClicks(f.viewport, ".25");
    REQUIRE(f.modal->statusText().contains(QStringLiteral("无效")));
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == index);
    QTest::keyClick(f.viewport, Qt::Key_Y);
    REQUIRE(f.model->componentPreview()->source.faces.size() == 14);
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == index + 1);
    REQUIRE(f.model->componentSelection().selectedIds() ==
            std::set<ComponentId>{{1}, {2}, {3}, {4}});
}
