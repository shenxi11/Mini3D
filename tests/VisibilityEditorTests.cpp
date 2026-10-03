/*
 * 模块名: VisibilityEditorTests
 * 功能概述: 验证 VIS-01 的三域隐藏、局部隔离、拾取遮挡与会话生命周期。
 * 对外接口: Catch2 [visibility]；依赖关系: Qt Test、真实编辑器/GL、CPU 拾取。
 * 输入输出: 输入命令/事件，检查显示、选择、文档和历史不变量。
 * 异常与错误: 不符即失败；维护说明: 窗口串行，临时目录保存，不写用户工程。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/ComponentPicker.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "renderer_gl/RayCaster.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>

using namespace mini3d;
using editor::ComponentId;
using editor::SelectionDomain;
using editor::SelectionOperation;
namespace {
core::EntityId editableCube(editor::SceneViewModel& model) {
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    return cube;
}
struct VisibilityWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    VisibilityWindow() {
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Blender);
        model->newScene();
        viewport->setEditorCamera({{4, 3, 8}, {0, 0, 0}, 0, 50});
        pointAtViewport();
    }
    ~VisibilityWindow() {
        window.hide();
    }
    void pointAtViewport() {
        viewport->setFocus();
        const QPointF point(viewport->rect().center());
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
    }
    QAction* action(const char* name) {
        auto* result = window.findChild<QAction*>(QString::fromLatin1(name));
        REQUIRE(result);
        return result;
    }
};
} // namespace

TEST_CASE("Hidden components leave source history and save point intact in every domain",
          "[visibility]") {
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        editor::SceneViewModel model;
        const auto cube = editableCube(model);
        model.setSelectionDomain(domain);
        const auto content = model.displayedEditableMesh(cube);
        const auto all = editor::ComponentSelection::elements(content->source, domain);
        const auto hidden = *all.begin();
        model.selectComponent(hidden, SelectionOperation::Replace);
        QTemporaryDir directory;
        REQUIRE(model.saveScene(directory.filePath("before.mini3d")));
        const auto history = model.undoStack()->count();
        REQUIRE(model.hideSelection());
        REQUIRE(model.componentSelection().selectedIds().empty());
        REQUIRE_FALSE(model.componentSelection().activeId());
        REQUIRE_FALSE(model.isComponentVisible(hidden));
        model.selectComponent(hidden, SelectionOperation::Replace);
        REQUIRE(model.componentSelection().selectedIds().empty());
        model.selectAllComponents();
        REQUIRE_FALSE(model.componentSelection().selectedIds().contains(hidden));
        for (const auto id : model.componentSelection().selectedIds())
            REQUIRE(model.isComponentVisible(id));
        const auto visibleSelection = model.componentSelection();
        REQUIRE(model.displayedEditableMesh(cube) == content);
        REQUIRE_FALSE(model.isModified());
        REQUIRE(model.undoStack()->count() == history);
        REQUIRE(model.saveScene(directory.filePath("hidden.mini3d")));
        editor::SceneViewModel loaded;
        REQUIRE(loaded.openScene(directory.filePath("hidden.mini3d")));
        REQUIRE(loaded.displayedEditableMesh(cube)->source == content->source);
        REQUIRE_FALSE(loaded.viewportVisibility().hasHiddenElements());
        REQUIRE(model.revealHidden());
        REQUIRE(model.componentSelection() == visibleSelection);
        REQUIRE(model.isComponentVisible(hidden));
        REQUIRE_FALSE(model.isModified());
        model.selectAllComponents();
        REQUIRE(model.componentSelection().selectedIds() == all);
    }
}

TEST_CASE(
    "Visibility closure preserves shared boundary and never exposes hidden components through XRay",
    "[visibility]") {
    editor::SceneViewModel model;
    const auto cube = editableCube(model);
    const auto content = model.displayedEditableMesh(cube);
    model.setSelectionDomain(SelectionDomain::Face);
    const auto face = content->source.faces.front();
    model.selectComponent({face.id}, SelectionOperation::Replace);
    REQUIRE(model.hideSelection());
    const auto& mask = model.viewportVisibility();
    REQUIRE_FALSE(mask.isFaceVisible(face));
    REQUIRE(mask.isVertexVisible(content->source, face.corners.front().vertex));
    REQUIRE(mask.isEdgeVisible(content->source, {face.corners[0].vertex, face.corners[1].vertex}));
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(1000, 800);
    REQUIRE(camera.setState({{3, 3, 7}, {0, 0, 0}, 0, 50}));
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        model.setSelectionDomain(domain);
        const auto selected =
            editor::boxSelectComponents(*model.scene(), *model.assets(), cube, domain, camera,
                                        {0, 0}, {1000, 800}, {1000, 800}, true, mask);
        for (const auto id : selected)
            REQUIRE(model.isComponentVisible(id));
        if (domain == SelectionDomain::Face)
            REQUIRE(selected.size() == 5);
    }
    model.setSelectionDomain(SelectionDomain::Vertex);
    model.selectAllComponents();
    REQUIRE(model.hideSelection());
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        REQUIRE(editor::boxSelectComponents(*model.scene(), *model.assets(), cube, domain, camera,
                                            {0, 0}, {1000, 800}, {1000, 800}, true, mask)
                    .empty());
        REQUIRE_FALSE(editor::pickComponent(*model.scene(), *model.assets(), cube, domain, camera,
                                            {500, 400}, {1000, 800}, true, mask));
    }
    REQUIRE_FALSE(
        renderer_gl::RayCaster::worldBounds(*model.scene(), *model.assets(), cube, mask).isValid());
}

TEST_CASE("Hidden surfaces stop occluding cursor vertex snap and component picking",
          "[visibility]") {
    editor::SceneViewModel model;
    const auto cube = editableCube(model);
    REQUIRE(model.hideSelection());
    const auto content = model.displayedEditableMesh(cube);
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(1000, 800);
    REQUIRE(camera.setState({{0, 0, 6}, {0, 0, 0}, 0, 50}));
    const auto& mask = model.viewportVisibility();
    REQUIRE(editor::locateCursor(*model.scene(), *model.assets(), camera, {500, 400}, {1000, 800})
                .surface);
    REQUIRE_FALSE(
        editor::locateCursor(*model.scene(), *model.assets(), camera, {500, 400}, {1000, 800}, mask)
            .surface);
    for (const auto& vertex : content->source.vertices) {
        const auto clip = camera.viewProjectionMatrix() * glm::vec4(vertex.position, 1);
        const glm::vec2 pixel{(clip.x / clip.w + 1) * 500, (1 - clip.y / clip.w) * 400};
        REQUIRE_FALSE(editor::pickVertexSnap(*model.scene(), *model.assets(), camera, pixel,
                                             {1000, 800}, cube, false, {}, mask));
    }
    // 编辑对象全部隐藏后，后方另一个对象可以被表面查询命中。
    REQUIRE(model.setEditMode(false));
    const auto target = model.createEntity(core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position.z = -2;
    REQUIRE(model.setTransform(target, transform));
    model.selection()->setSelectedEntity(cube);
    REQUIRE(model.hideSelection());
    const auto hit = editor::locateCursor(*model.scene(), *model.assets(), camera, {500, 400},
                                          {1000, 800}, model.viewportVisibility());
    REQUIRE(hit.surface);
    REQUIRE(hit.position->z < -1);
    model.selectRay({{0, 0, 6}, {0, 0, -1}});
    REQUIRE(model.selection()->selectedEntity() == target);
}

TEST_CASE("Local view preserves parent transforms and persistent and temporary masks",
          "[visibility]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    core::Transform transform;
    transform.position = {4, 2, 0};
    REQUIRE(model.setTransform(parent, transform));
    const auto child = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(child, parent));
    const auto outside = model.createEntity(core::PrimitiveKind::Cube);
    const auto hidden = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setVisible(hidden, false));
    model.selection()->setSelectedEntity(outside);
    REQUIRE(model.hideSelection());
    model.selection()->setSelectedEntity(child);
    QTemporaryDir directory;
    REQUIRE(model.saveScene(directory.filePath("scene.mini3d")));
    const auto history = model.undoStack()->count();
    const auto world = model.scene()->worldMatrix(child);
    REQUIRE(model.toggleLocalView());
    const auto& mask = model.viewportVisibility();
    REQUIRE(mask.isVisible(*model.scene(), child));
    REQUIRE_FALSE(mask.isVisible(*model.scene(), parent));
    REQUIRE_FALSE(mask.isVisible(*model.scene(), outside));
    REQUIRE_FALSE(mask.isVisible(*model.scene(), hidden));
    const auto bounds = renderer_gl::RayCaster::sceneBounds(*model.scene(), *model.assets(), mask);
    const auto original =
        renderer_gl::RayCaster::worldBounds(*model.scene(), *model.assets(), child);
    REQUIRE(bounds.minimum == original.minimum);
    REQUIRE(bounds.maximum == original.maximum);
    REQUIRE(renderer_gl::RayCaster::pick(*model.scene(), *model.assets(), {{4, 2, 6}, {0, 0, -1}},
                                         mask) == child);
    REQUIRE(model.toggleLocalView());
    REQUIRE(mask.hiddenObjects.contains(outside));
    REQUIRE_FALSE(model.scene()->find(hidden)->visible);
    REQUIRE(model.scene()->worldMatrix(child) == world);
    REQUIRE(model.undoStack()->count() == history);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.revealHidden());
    REQUIRE(mask.isVisible(*model.scene(), outside));
    REQUIRE_FALSE(mask.isVisible(*model.scene(), hidden));
}

TEST_CASE(
    "Visibility lifecycle cancels previews filters undo selection and protects hidden loop bands",
    "[visibility]") {
    editor::SceneViewModel model;
    const auto cube = editableCube(model);
    model.selectAllComponents();
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1), glm::dvec3(.1, 0, 0))));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.hideSelection());
    model.undo();
    REQUIRE(model.componentSelection().selectedIds().empty());
    REQUIRE_FALSE(model.componentSelection().activeId());
    model.redo();
    REQUIRE(model.componentSelection().selectedIds().empty());
    REQUIRE(model.revealHidden());
    model.setSelectionDomain(SelectionDomain::Face);
    const auto content = model.displayedEditableMesh(cube);
    const auto face = content->source.faces.front();
    model.selectComponent({face.id}, SelectionOperation::Replace);
    REQUIRE(model.hideSelection());
    REQUIRE(model.beginLoopCut());
    REQUIRE_FALSE(model.previewLoopCut(
        core::modeling::EdgeKey(face.corners[0].vertex, face.corners[1].vertex), 0));
    REQUIRE_FALSE(model.finishComponentTransform(true));
    model.cancelTransformEdit();
    REQUIRE(model.revealHidden());
    model.setSelectionDomain(SelectionDomain::Vertex);
    model.selectAllComponents();
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.toggleLocalView());
    REQUIRE_FALSE(model.hasComponentTransform());
    REQUIRE(model.hideSelection());
    REQUIRE(model.setEditMode(false));
    REQUIRE_FALSE(model.viewportVisibility().hasHiddenElements());
    REQUIRE(model.viewportVisibility().localRoot == cube);
    model.deleteSelected();
    REQUIRE(model.viewportVisibility().localRoot == 0);
    model.undo();
    REQUIRE(model.viewportVisibility().localRoot == 0);
    REQUIRE(model.toggleLocalView());
    model.newScene();
    REQUIRE(model.viewportVisibility().localRoot == 0);
    REQUIRE(model.viewportVisibility().hiddenObjects.empty());
}

TEST_CASE("Visibility UI routes H AltH keypad slash and produces actual filtered GL frames",
          "[visibility]") {
    VisibilityWindow f;
    const auto cube = f.model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(f.model->setEditMode(true));
    f.viewport->setOverlayVisible(false);
    f.model->setCursorVisible(false);
    f.pointAtViewport();
    const auto* context = f.viewport->context();
    const auto before = f.viewport->grabFramebuffer();
    REQUIRE_FALSE(before.isNull());
    const auto history = f.model->undoStack()->count();
    QTest::keyClick(f.viewport, Qt::Key_H);
    REQUIRE(f.model->viewportVisibility().hasHiddenElements());
    const auto hidden = f.viewport->grabFramebuffer();
    REQUIRE(hidden != before);
    REQUIRE_FALSE(f.viewport->focusSelection());
    QTest::keyClick(f.viewport, Qt::Key_H, Qt::AltModifier);
    REQUIRE_FALSE(f.model->viewportVisibility().hasHiddenElements());
    REQUIRE(f.viewport->grabFramebuffer() == before);
    REQUIRE(f.model->componentSelection().selectedIds().empty());
    QTest::keyClick(f.viewport, Qt::Key_Slash);
    REQUIRE(f.model->viewportVisibility().localRoot == 0);
    QTest::keyClick(f.viewport, Qt::Key_Slash, Qt::KeypadModifier);
    REQUIRE(f.model->viewportVisibility().localRoot == cube);
    REQUIRE(f.action("ToggleLocalView")->isChecked());
    REQUIRE(f.window.findChild<QLabel*>("ViewportVisibilityStatus")
                ->text()
                .contains(QStringLiteral("局部视图")));
    QTest::keyClick(f.viewport, Qt::Key_Slash, Qt::KeypadModifier);
    REQUIRE(f.model->viewportVisibility().localRoot == 0);
    REQUIRE(f.model->undoStack()->count() == history);
    REQUIRE(f.viewport->context() == context);
    f.model->selectAllComponents();
    REQUIRE(f.model->hideSelection());
    f.viewport->setOverlayVisible(true);
    f.viewport->setXRayEnabled(true);
    const auto noComponents = f.viewport->grabFramebuffer();
    REQUIRE(f.model->setEditMode(false));
    REQUIRE(f.model->hideSelection());
    REQUIRE(f.viewport->grabFramebuffer() == noComponents);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_VIS_CAPTURE");
    if (!capture.isEmpty()) {
        QTest::qWait(30); // 等待状态栏布局，截图不使用尚未分配宽度的新提示。
        REQUIRE(f.window.grab().save(capture + QStringLiteral("-visibility.png")));
    }
}

TEST_CASE("Local view renders only selected subtree and menu Registry and text protection agree",
          "[visibility]") {
    VisibilityWindow f;
    const auto parent = f.model->createEntity(core::PrimitiveKind::Empty);
    core::Transform parentTransform;
    parentTransform.position.x = -1;
    REQUIRE(f.model->setTransform(parent, parentTransform));
    const auto first = f.model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(f.model->setParent(first, parent));
    const auto second = f.model->createEntity(core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position.x = 2;
    REQUIRE(f.model->setTransform(second, transform));
    f.model->selection()->setSelectedEntity(first);
    f.viewport->setOverlayVisible(false);
    f.model->setCursorVisible(false);
    const auto beforeSize = f.viewport->size();
    const auto beforeMatrix = f.viewport->editorCameraSnapshot()->viewProjectionMatrix();
    const auto before = f.viewport->grabFramebuffer();
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    const auto context =
        registry->captureContext(editor::InputArea::Viewport, editor::EditorKeymap::Blender);
    REQUIRE(registry->execute("view.local_view", context));
    REQUIRE(f.viewport->grabFramebuffer() != before);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_VIS_CAPTURE");
    if (!capture.isEmpty()) {
        QTest::qWait(30);
        REQUIRE(f.window.grab().save(capture + QStringLiteral("-local-view.png")));
    }
    REQUIRE_FALSE(registry->execute("view.local_view", context)); // 旧弹窗上下文失效。
    f.action("ToggleLocalView")->trigger();
    const auto restored = f.viewport->grabFramebuffer();
    INFO("before logical=" << beforeSize.width() << 'x' << beforeSize.height()
                           << " after logical=" << f.viewport->width() << 'x'
                           << f.viewport->height() << " before pixels=" << before.width() << 'x'
                           << before.height() << " after pixels=" << restored.width() << 'x'
                           << restored.height() << " camera unchanged="
                           << (beforeMatrix ==
                               f.viewport->editorCameraSnapshot()->viewProjectionMatrix()));
    if (!capture.isEmpty() && restored != before) {
        REQUIRE(before.save(capture + QStringLiteral("-before.png")));
        REQUIRE(restored.save(capture + QStringLiteral("-restored.png")));
    }
    REQUIRE(f.viewport->size() == beforeSize);
    REQUIRE(f.viewport->editorCameraSnapshot()->viewProjectionMatrix() == beforeMatrix);
    REQUIRE(restored == before);
    f.window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Legacy);
    f.pointAtViewport();
    QTest::keyClick(f.viewport, Qt::Key_H);
    REQUIRE(f.model->viewportVisibility().hiddenObjects.empty());
    f.action("HideSelection")->trigger();
    REQUIRE(f.model->viewportVisibility().hiddenObjects.contains(first));
    f.action("RevealHidden")->trigger();
    f.model->selection()->setSelectedEntity(first);
    f.window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Blender);
    QLineEdit input(&f.window);
    input.show();
    input.setFocus();
    QTest::keyClick(&input, Qt::Key_H);
    REQUIRE(input.text() == "h");
    REQUIRE(f.model->viewportVisibility().hiddenObjects.empty());
    f.model->toggleLocalView();
    f.model->selection()->setSelectedEntity(second);
    REQUIRE(f.model->viewportVisibility().localRoot == 0); // 场景树切到隔离外对象即退出。
}

TEST_CASE("Local view cancellation camera preview and reopen preserve document state",
          "[visibility]") {
    VisibilityWindow f;
    const auto camera = f.model->createCamera();
    const auto cube = f.model->createEntity(core::PrimitiveKind::Cube);
    QTemporaryDir directory;
    const auto path = directory.filePath("visibility.mini3d");
    REQUIRE(f.model->saveScene(path));
    const auto matrix = f.viewport->editorCameraSnapshot()->viewProjectionMatrix();
    const auto history = f.model->undoStack()->count();
    f.pointAtViewport();
    QTest::keyClick(f.viewport, Qt::Key_G);
    auto* modal = f.window.findChild<editor::ObjectTransformSession*>();
    REQUIRE(modal->isActive());
    f.action("ToggleLocalView")->trigger();
    REQUIRE_FALSE(modal->isActive());
    REQUIRE(f.model->viewportVisibility().localRoot == cube);
    REQUIRE(f.viewport->editorCameraSnapshot()->viewProjectionMatrix() == matrix);
    REQUIRE(f.model->setPreviewCamera(camera));
    REQUIRE(f.model->viewportVisibility().localRoot == 0);
    REQUIRE_FALSE(f.model->hideSelection());
    REQUIRE_FALSE(f.model->revealHidden());
    REQUIRE_FALSE(f.model->toggleLocalView());
    REQUIRE(f.model->setPreviewCamera(0));
    REQUIRE(f.model->undoStack()->count() == history);
    REQUIRE_FALSE(f.model->isModified());
    REQUIRE(f.model->hideSelection());
    REQUIRE(f.model->openScene(path));
    REQUIRE(f.model->viewportVisibility().hiddenObjects.empty());
    REQUIRE(f.model->viewportVisibility().localRoot == 0);
    REQUIRE(f.model->viewportVisibility().isVisible(*f.model->scene(), cube));
}
