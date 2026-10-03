/*
 * 模块名: EditModeTests
 * 功能概述: 验证真实窗口中的模式/组件状态路由与文档、历史、对象操作互斥。
 * 对外接口: Catch2 [edit-mode]；依赖关系: Qt Test、MainWindow、SceneViewModel。
 * 输入输出: 模式/键盘/文本/场景意图到选区和历史断言、真实窗口截图。
 * 异常与错误: 失败不得改变原态；维护说明: 组件几何拾取另行验收，不伪造点击命中。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/ComponentPicker.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
using editor::ComponentId;
using editor::SelectionDomain;
using editor::SelectionOperation;
namespace {
void pointAt(QWidget* widget) {
    widget->setFocus();
    const auto point = widget->rect().center();
    QMouseEvent move(QEvent::MouseMove, point, widget->mapToGlobal(point), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &move);
}
} // namespace

TEST_CASE("Edit context owns one mesh and mode selection changes do not dirty saved content",
          "[edit-mode]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE_FALSE(model.isEditMode());
    REQUIRE(model.setEditMode(true));
    REQUIRE(model.editedEntity() == cube);
    REQUIRE(model.undoStack()->count() == 2);
    REQUIRE(model.componentSelection().selectedIds().size() == 8);
    QTemporaryDir directory;
    REQUIRE(model.saveScene(directory.filePath(QStringLiteral("mode.m3dscene"))));
    REQUIRE_FALSE(model.isModified());
    const auto index = model.undoStack()->index();
    model.setSelectionDomain(SelectionDomain::Face);
    REQUIRE(model.componentSelection().selectedIds().size() == 6);
    model.selectComponent({6}, SelectionOperation::Replace);
    model.selectComponent({2}, SelectionOperation::Add);
    REQUIRE(model.componentSelection().selectedIds().size() == 2);
    REQUIRE(model.componentSelection().activeId() == ComponentId{2});
    model.selectComponent({2}, SelectionOperation::Toggle);
    REQUIRE(model.componentSelection().activeId() == ComponentId{6});
    const auto revision = model.componentSelectionRevision();
    model.selectComponent({999}, SelectionOperation::Replace);
    REQUIRE(model.componentSelectionRevision() == revision);
    REQUIRE(model.setEditMode(false));
    REQUIRE(model.componentSelection().selectedIds().empty());
    REQUIRE(model.setEditMode(true));
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE_FALSE(model.isModified());
    model.undo();
    REQUIRE_FALSE(model.isEditMode());
    REQUIRE_FALSE(model.componentSelection().activeId());
    REQUIRE(model.scene()->find(cube)->primitive == core::PrimitiveKind::Cube);
    model.redo();
    REQUIRE_FALSE(model.isEditMode());
    REQUIRE(model.setEditMode(true));
    REQUIRE(model.undoStack()->count() == 2);
}

TEST_CASE(
    "Edit mode rejects unsupported targets and blocks object mutations at the intent boundary",
    "[edit-mode]") {
    editor::SceneViewModel model;
    model.newScene();
    for (auto kind :
         {core::PrimitiveKind::Empty, core::PrimitiveKind::Sphere, core::PrimitiveKind::Plane}) {
        model.createEntity(kind);
        const auto count = model.undoStack()->count();
        REQUIRE_FALSE(model.setEditMode(true));
        REQUIRE(model.undoStack()->count() == count);
    }
    const auto camera = model.createCamera();
    REQUIRE_FALSE(model.setEditMode(true));
    model.createDirectionalLight();
    REQUIRE_FALSE(model.setEditMode(true));
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setPreviewCamera(camera));
    REQUIRE_FALSE(model.setEditMode(true));
    REQUIRE(model.setPreviewCamera(0));
    REQUIRE(model.setEditMode(true));
    const auto count = model.undoStack()->count();
    const auto original = model.scene()->find(cube)->transform;
    auto changed = original;
    changed.position.x += 3;
    REQUIRE_FALSE(model.setTransform(cube, changed));
    REQUIRE_FALSE(model.setParent(cube, camera));
    model.beginTransformEdit(cube);
    model.previewTransform(changed);
    model.finishTransformEdit(true);
    model.duplicateSelected();
    model.deleteSelected();
    REQUIRE(model.createEntity(core::PrimitiveKind::Cube) == 0);
    REQUIRE(model.createCamera() == 0);
    REQUIRE(model.createDirectionalLight() == 0);
    REQUIRE(model.importGltf(QStringLiteral("not-read.gltf")) == 0);
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE(model.scene()->find(cube)->transform.position == original.position);
    REQUIRE(model.selection()->selectedEntity() == cube);
    REQUIRE(model.setPreviewCamera(camera));
    REQUIRE_FALSE(model.isEditMode());
}

TEST_CASE(
    "Registry invalidates mode roundtrips and changed geometry without relying on selected IDs",
    "[edit-mode]") {
    editor::MainWindow window;
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    const auto objectContext = registry->captureContext(editor::InputArea::Viewport);
    REQUIRE(model->setEditMode(true));
    REQUIRE(model->setEditMode(false));
    REQUIRE_FALSE(
        registry->disabledReason(QStringLiteral("object.delete"), objectContext).isEmpty());
    REQUIRE(model->setEditMode(true));
    const auto editContext = registry->captureContext(editor::InputArea::Viewport);
    const auto selection = model->componentSelection();
    auto mesh =
        model->scene()->editableMesh(model->scene()->find(cube)->editableMesh)->content->source;
    mesh.vertices[0].position.x -= 0.1F;
    REQUIRE(model->replaceEditableMesh(cube, mesh));
    REQUIRE(model->componentSelection() == selection);
    REQUIRE_FALSE(
        registry->disabledReason(QStringLiteral("mesh.select_all"), editContext).isEmpty());
    model->undo();
    REQUIRE_FALSE(
        registry->disabledReason(QStringLiteral("mesh.select_all"), editContext).isEmpty());
}

TEST_CASE("Selection topology visibility and document changes clear invalid edit identities",
          "[edit-mode]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(cube, parent));
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(SelectionDomain::Face);
    const auto meshId = model.scene()->find(cube)->editableMesh;
    auto mesh = model.scene()->editableMesh(meshId)->content->source;
    const auto removed = mesh.faces[0].id;
    REQUIRE(model.componentSelection().activeId() == ComponentId{removed});
    mesh.faces.erase(mesh.faces.begin());
    REQUIRE(model.replaceEditableMesh(cube, mesh));
    REQUIRE(model.isEditMode());
    REQUIRE(model.componentSelection().selectedIds().size() == 5);
    REQUIRE_FALSE(model.componentSelection().selectedIds().contains({removed}));
    REQUIRE(model.componentSelection().activeId() ==
            *model.componentSelection().selectedIds().begin());
    REQUIRE(model.setVisible(parent, false));
    REQUIRE_FALSE(model.isEditMode());
    REQUIRE(model.componentSelection().selectedIds().empty());
    REQUIRE_FALSE(model.setEditMode(true));
    model.undo();
    REQUIRE(model.setEditMode(true));
    model.selection()->setSelectedEntity(parent);
    REQUIRE_FALSE(model.isEditMode());
    model.selection()->setSelectedEntity(cube);
    REQUIRE(model.setEditMode(true));
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    REQUIRE_FALSE(model.openScene(QStringLiteral("not-existing-mode.m3dscene")));
    REQUIRE(failures.count() == 1);
    REQUIRE(model.isEditMode());
    model.newScene();
    REQUIRE_FALSE(model.isEditMode());
    REQUIRE_FALSE(model.componentSelection().activeId());
}

TEST_CASE("Edit mode keys header sidebar and frozen registry contexts reflect the same state",
          "[edit-mode]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    const bool activated = QTest::qWaitForWindowActive(&window);
    INFO("visible=" << window.isVisible() << "; active=" << window.isActiveWindow()
                    << "; Qt active window=" << QApplication::activeWindow());
    REQUIRE(activated);
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    router->setKeymap(editor::EditorKeymap::Blender);
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    window.findChild<QAction*>(QStringLiteral("MoveTool"))->trigger();
    pointAt(viewport);
    const auto objectContext = registry->captureContext(editor::InputArea::Viewport);
    QTest::keyClick(viewport, Qt::Key_Tab);
    REQUIRE(model->isEditMode());
    REQUIRE(window.findChild<QLabel*>(QStringLiteral("ModeLabel"))->text() ==
            QStringLiteral("编辑模式"));
    REQUIRE_FALSE(registry->execute(QStringLiteral("object.delete"), objectContext));
    for (const auto* name : {"MoveTool", "TransformMove", "Duplicate", "Delete", "CreateCube"}) {
        REQUIRE_FALSE(window.findChild<QAction*>(QString::fromLatin1(name))->isEnabled());
    }
    REQUIRE_FALSE(window.findChild<QDoubleSpinBox*>(QStringLiteral("PositionX"))->isEnabled());
    const auto index = model->undoStack()->index();
    QTest::keyClick(viewport, Qt::Key_3);
    REQUIRE(model->componentSelection().domain() == SelectionDomain::Face);
    REQUIRE(window.findChild<QAction*>(QStringLiteral("SelectFaces"))->isChecked());
    REQUIRE(window.findChild<QLabel*>(QStringLiteral("SidebarSelection"))
                ->text()
                .contains(QStringLiteral("6 / 6")));
    const auto context = registry->captureContext(editor::InputArea::Viewport);
    REQUIRE_FALSE(registry->disabledReason(QStringLiteral("object.translate"), context).isEmpty());
    QTest::keyClick(viewport, Qt::Key_A, Qt::AltModifier);
    REQUIRE(model->componentSelection().selectedIds().empty());
    REQUIRE_FALSE(registry->execute(QStringLiteral("mesh.select_all"), context));
    QTest::keyClick(viewport, Qt::Key_A);
    REQUIRE(model->componentSelection().selectedIds().size() == 6);
    QTest::keyClick(viewport, Qt::Key_2);
    REQUIRE(model->componentSelection().selectedIds().size() == 12);
    QTest::keyClick(viewport, Qt::Key_1);
    REQUIRE(model->componentSelection().selectedIds().size() == 8);
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"), true);
    QApplication::sendEvent(viewport, &repeat);
    REQUIRE(model->isEditMode());
    window.findChild<QTabBar*>(QStringLiteral("WorkspaceTabs"))->setCurrentIndex(1);
    REQUIRE(model->isEditMode());
    REQUIRE(model->selection()->selectedEntity() == cube);
    REQUIRE(model->undoStack()->index() == index);
    window.findChild<QAction*>(QStringLiteral("ToggleViewportSidebar"))->setChecked(true);
    QTest::qWait(40);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_EDIT_MODE_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture));
    }
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_Tab);
    REQUIRE_FALSE(model->isEditMode());
    REQUIRE(window.findChild<QAction*>(QStringLiteral("MoveTool"))->isEnabled());
    REQUIRE(window.findChild<QDoubleSpinBox*>(QStringLiteral("PositionX"))->isEnabled());
}

TEST_CASE("Tab cancels object preview and text IME and legacy keep their input priority",
          "[edit-mode]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* modal = window.findChild<editor::ObjectTransformSession*>();
    router->setKeymap(editor::EditorKeymap::Blender);
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    const auto original = model->scene()->find(cube)->transform.position;
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_G);
    QTest::keyClick(viewport, Qt::Key_X);
    QTest::keyClick(viewport, Qt::Key_2);
    REQUIRE(modal->isActive());
    REQUIRE(model->scene()->find(cube)->transform.position != original);
    QTest::keyClick(viewport, Qt::Key_Tab);
    REQUIRE_FALSE(modal->isActive());
    REQUIRE(model->isEditMode());
    REQUIRE(model->scene()->find(cube)->transform.position == original);
    REQUIRE_FALSE(modal->start(editor::TransformOperation::Move));
    model->setEditMode(false);
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("SceneSearch"));
    search->setFocus();
    QTest::keyClick(search, Qt::Key_Tab);
    REQUIRE_FALSE(model->isEditMode());
    pointAt(viewport);
    QInputMethodEvent preedit(QStringLiteral("测试"), {});
    QApplication::sendEvent(viewport, &preedit);
    QTest::keyClick(viewport, Qt::Key_Tab);
    REQUIRE_FALSE(model->isEditMode());
    QInputMethodEvent end;
    QApplication::sendEvent(viewport, &end);
    router->setKeymap(editor::EditorKeymap::Legacy);
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_Tab);
    REQUIRE_FALSE(model->isEditMode());
    window.findChild<QAction*>(QStringLiteral("ToggleEditMode"))->trigger();
    REQUIRE(model->isEditMode());
    window.findChild<QAction*>(QStringLiteral("SelectFaces"))->trigger();
    REQUIRE(model->componentSelection().domain() == SelectionDomain::Face);
    pointAt(viewport);
    QTest::keyClick(viewport, Qt::Key_1);
    REQUIRE(model->componentSelection().domain() == SelectionDomain::Face);
}

TEST_CASE("Viewport component clicks replace extend toggle and render actual source selections",
          "[edit-mode][component-click]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Blender);
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    window.findChild<QAction*>(QStringLiteral("ToggleViewportSidebar"))->setChecked(true);
    REQUIRE(viewport->focusSelection());
    REQUIRE(model->setEditMode(true));
    model->setSelectionDomain(SelectionDomain::Face);
    model->clearComponentSelection();
    const auto context = viewport->context();
    const auto before = viewport->grabFramebuffer();
    REQUIRE_FALSE(before.isNull());
    const auto source =
        model->scene()->editableMesh(model->scene()->find(cube)->editableMesh)->content->source;
    const auto camera = viewport->editorCameraSnapshot();
    REQUIRE(camera);
    const auto project = [&](glm::vec3 position) {
        const auto clip = camera->viewProjectionMatrix() * glm::vec4(position, 1);
        return QPoint(qRound((clip.x / clip.w + 1) * 0.5F * viewport->width()),
                      qRound((1 - clip.y / clip.w) * 0.5F * viewport->height()));
    };
    std::vector<std::pair<ComponentId, QPoint>> faces;
    for (const auto& face : source.faces) {
        glm::vec3 center{0};
        for (const auto& corner : face.corners)
            center += source.vertex(corner.vertex)->position;
        center /= static_cast<float>(face.corners.size());
        const auto point = project(center);
        const auto hit = editor::pickComponent(
            *model->scene(), *model->assets(), cube, SelectionDomain::Face, *camera,
            {point.x(), point.y()}, {viewport->width(), viewport->height()});
        if (hit == ComponentId{face.id})
            faces.push_back({*hit, point});
    }
    REQUIRE(faces.size() >= 2);
    const auto index = model->undoStack()->index();
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, faces[0].second);
    REQUIRE(model->componentSelection().selectedIds() == std::set<ComponentId>{faces[0].first});
    REQUIRE(model->componentSelection().activeId() == faces[0].first);
    REQUIRE(viewport->grabFramebuffer() != before);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::ShiftModifier, faces[1].second);
    REQUIRE(model->componentSelection().selectedIds().size() == 2);
    REQUIRE(model->componentSelection().activeId() == faces[1].first);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::ShiftModifier, faces[0].second);
    REQUIRE(model->componentSelection().selectedIds() == std::set<ComponentId>{faces[1].first});
    auto* sidebar = window.findChild<QLabel*>(QStringLiteral("SidebarSelection"));
    QTest::mouseClick(sidebar, Qt::LeftButton);
    REQUIRE(model->componentSelection().selectedIds() == std::set<ComponentId>{faces[1].first});
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(4, viewport->height() - 4));
    REQUIRE(model->componentSelection().selectedIds().empty());
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::ShiftModifier,
                      QPoint(4, viewport->height() - 4));
    REQUIRE(model->componentSelection().selectedIds().empty());
    REQUIRE(viewport->grabFramebuffer() == before);
    model->setSelectionDomain(SelectionDomain::Vertex);
    bool clickedVertex = false;
    for (const auto& vertex : source.vertices) {
        const auto point = project(vertex.position);
        const auto hit = editor::pickComponent(
            *model->scene(), *model->assets(), cube, SelectionDomain::Vertex, *camera,
            {point.x(), point.y()}, {viewport->width(), viewport->height()});
        if (hit != ComponentId{vertex.id})
            continue;
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point);
        REQUIRE(model->componentSelection().selectedIds() == std::set<ComponentId>{{vertex.id}});
        clickedVertex = true;
        break;
    }
    REQUIRE(clickedVertex);
    model->setSelectionDomain(SelectionDomain::Edge);
    bool clickedEdge = false;
    for (const auto edge : editor::ComponentSelection::elements(source, SelectionDomain::Edge)) {
        const auto point = project(
            (source.vertex(edge.first)->position + source.vertex(edge.second)->position) * 0.5F);
        const auto hit = editor::pickComponent(
            *model->scene(), *model->assets(), cube, SelectionDomain::Edge, *camera,
            {point.x(), point.y()}, {viewport->width(), viewport->height()});
        if (hit != edge)
            continue;
        QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, point);
        REQUIRE(model->componentSelection().selectedIds() == std::set<ComponentId>{edge});
        clickedEdge = true;
        break;
    }
    REQUIRE(clickedEdge);
    model->setSelectionDomain(SelectionDomain::Face);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, faces[0].second);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::ShiftModifier, faces[1].second);
    REQUIRE(model->undoStack()->index() == index);
    REQUIRE(model->selection()->selectedEntity() == cube);
    REQUIRE(viewport->context() == context);
    REQUIRE(
        model->scene()->editableMesh(model->scene()->find(cube)->editableMesh)->content->source ==
        source);
    // 连续 sendEvent 不会主动处理布局请求，截图前等待侧栏按三行文字重新布局。
    QTest::qWait(40);
    REQUIRE(sidebar->text().contains(QStringLiteral("2 / 6")));
    REQUIRE(sidebar->height() >= sidebar->fontMetrics().lineSpacing() * 3);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_EDIT_MODE_CAPTURE");
    if (!capture.isEmpty())
        REQUIRE(window.grab().save(capture));
}
