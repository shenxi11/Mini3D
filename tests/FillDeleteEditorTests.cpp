/*
 * 模块名: FillDeleteEditorTests
 * 功能概述: 验证删除/补面的历史、选区、保存及真实菜单快捷键。
 * 对外接口: Catch2 [fill-delete-editor]/[fill-delete-ui]；依赖关系: Qt Test、真实 GL。
 * 输入输出: 场景意图/窗口事件到网格、历史和可选截图；异常与错误: 拒绝不得修改真源。
 * 维护说明: 临时工程隔离，不写用户偏好、不关闭用户窗口，合成 IME 不代替实机验收。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
using editor::SelectionDomain;
using editor::SelectionOperation;
namespace {
const core::EditableMeshRecord& record(const editor::SceneViewModel& model, core::EntityId id) {
    return *model.scene()->editableMesh(model.scene()->find(id)->editableMesh);
}
core::EntityId enterCube(editor::SceneViewModel& model) {
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(SelectionDomain::Face);
    model.selectComponent({1}, SelectionOperation::Replace);
    return id;
}
void selectBoundary(editor::SceneViewModel& model, SelectionDomain domain) {
    model.setSelectionDomain(domain);
    model.clearComponentSelection();
    if (domain == SelectionDomain::Vertex) {
        for (auto id : {5, 6, 7, 8})
            model.selectComponent({static_cast<std::uint64_t>(id)}, SelectionOperation::Add);
    } else {
        for (const auto edge : {core::modeling::EdgeKey(5, 6), {6, 7}, {7, 8}, {8, 5}})
            model.selectComponent(editor::ComponentId::edge(edge), SelectionOperation::Add);
    }
}
struct FillDeleteWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::KeymapRouter* router = window.findChild<editor::KeymapRouter*>();
    core::EntityId entity;
    FillDeleteWindow() {
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        entity = enterCube(*model);
        router->setKeymap(editor::EditorKeymap::Blender);
        viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
        QTest::qWait(30);
        pointAt();
    }
    ~FillDeleteWindow() {
        window.hide();
    }
    void pointAt() {
        viewport->setFocus();
        const auto point = QPoint(viewport->width() / 2, viewport->height() / 2);
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
    }
    QMenu* openDelete(Qt::Key key) {
        pointAt();
        QTest::keyClick(viewport, key);
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        REQUIRE(menu);
        REQUIRE(menu->objectName() == QStringLiteral("ComponentDeletePopup"));
        return menu;
    }
    void capture(const QString& suffix) {
        const auto prefix = qEnvironmentVariable("MINI3D_TEST_FILL_DELETE_CAPTURE");
        if (!prefix.isEmpty()) {
            QTest::qWait(60);
            REQUIRE(window.grab().save(prefix + suffix + QStringLiteral(".png")));
        }
    }
};
} // namespace

TEST_CASE("Delete and fill restore selection snapshots and clean save points in one undo each",
          "[fill-delete-editor]") {
    for (auto domain : {SelectionDomain::Vertex, SelectionDomain::Edge}) {
        editor::SceneViewModel model;
        const auto entity = enterCube(model);
        const auto before = record(model, entity);
        const auto originalSelection = model.componentSelection();
        const auto count = model.undoStack()->count();
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("删除补面.m3dscene"));
        REQUIRE(model.saveScene(path));
        REQUIRE(model.deleteComponents(SelectionDomain::Face));
        REQUIRE(record(model, entity).content->source.faces.size() == 5);
        REQUIRE(model.componentSelection().selectedIds().empty());
        REQUIRE(model.undoStack()->count() == count + 1);
        REQUIRE(model.isModified());
        model.undo();
        REQUIRE(record(model, entity).content == before.content);
        REQUIRE(model.componentSelection() == originalSelection);
        REQUIRE_FALSE(model.isModified());
        model.redo();
        selectBoundary(model, domain);
        const auto boundary = model.componentSelection();
        const auto hole = record(model, entity);
        REQUIRE(model.saveScene(path));
        REQUIRE(model.fillFace());
        const auto filled = record(model, entity);
        REQUIRE(filled.content->source.faces.size() == 6);
        REQUIRE(model.componentSelection().domain() == SelectionDomain::Face);
        REQUIRE(model.componentSelection().selectedIds().size() == 1);
        REQUIRE(model.componentSelection().activeId()->first ==
                filled.content->source.faces.back().id);
        REQUIRE(model.undoStack()->count() == count + 2);
        REQUIRE_FALSE(model.hasComponentTransform());
        REQUIRE_FALSE(model.lastOperationDisabledReason().isEmpty());
        model.undo();
        REQUIRE(record(model, entity).content == hole.content);
        REQUIRE(model.componentSelection() == boundary);
        REQUIRE_FALSE(model.isModified());
        model.redo();
        REQUIRE(record(model, entity).content == filled.content);
        REQUIRE(record(model, entity).evaluationRevision > before.evaluationRevision);
        REQUIRE(model.saveScene(path));
        editor::SceneViewModel reopened;
        REQUIRE(reopened.openScene(path));
        REQUIRE(record(reopened, entity).content->source == filled.content->source);
        REQUIRE(model.beginExtrudeRegion());
        auto offset = glm::dmat4(1);
        offset[3].z = .25;
        REQUIRE(model.previewComponentTransform(offset));
        REQUIRE(model.finishComponentTransform(true));
        REQUIRE(record(model, entity).content->source.faces.size() == 10);
        model.undo();
        REQUIRE(record(model, entity).content == filled.content);
    }
}

TEST_CASE(
    "Delete by projected domain preserves the original selection on undo and allows empty mesh",
    "[fill-delete-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterCube(model);
    const auto selected = model.componentSelection();
    const auto before = record(model, entity);
    REQUIRE(model.deleteComponents(SelectionDomain::Vertex));
    REQUIRE(record(model, entity).content->source.faces.size() == 1);
    REQUIRE(record(model, entity).content->source.vertices.size() == 4);
    model.undo();
    REQUIRE(model.componentSelection() == selected);
    REQUIRE(record(model, entity).content == before.content);
    REQUIRE(model.deleteComponents(SelectionDomain::Edge));
    REQUIRE(record(model, entity).content->source.faces.size() == 1);
    model.undo();
    model.selectAllComponents();
    REQUIRE(model.deleteComponents(SelectionDomain::Face));
    REQUIRE(model.scene()->find(entity));
    REQUIRE(model.isEditMode());
    REQUIRE(record(model, entity).content->source.vertices.empty());
    REQUIRE(model.componentSelection().selectedIds().empty());
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("空网格.m3dscene"));
    REQUIRE(model.saveScene(path));
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(record(reopened, entity).content->source.faces.empty());
    reopened.selection()->setSelectedEntity(entity);
    REQUIRE(reopened.setEditMode(true));
    model.undo();
    REQUIRE(record(model, entity).content == before.content);
    REQUIRE(model.componentSelection().selectedIds().size() == 6);
}

TEST_CASE("Unsupported fill leaves geometry revisions history and selection unchanged",
          "[fill-delete-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterCube(model);
    const auto before = record(model, entity);
    const auto count = model.undoStack()->count();
    QSignalSpy failed(&model, &editor::SceneViewModel::operationFailed);
    REQUIRE_FALSE(model.fillFace());
    selectBoundary(model, SelectionDomain::Edge); // 闭合 Cube 的边不是边界。
    const auto selection = model.componentSelection();
    const auto revision = model.componentSelectionRevision();
    REQUIRE_FALSE(model.fillFace());
    REQUIRE(failed.count() == 2);
    REQUIRE(record(model, entity).content == before.content);
    REQUIRE(record(model, entity).evaluationRevision == before.evaluationRevision);
    REQUIRE(model.componentSelectionRevision() == revision);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE_FALSE(model.hasComponentTransform());
    REQUIRE(model.beginComponentTransform());
    auto delta = glm::dmat4(1);
    delta[3].z = .2;
    REQUIRE(model.previewComponentTransform(delta));
    REQUIRE(model.deleteComponents(SelectionDomain::Edge));
    REQUIRE_FALSE(model.hasComponentTransform());
    for (const auto& vertex : record(model, entity).content->source.vertices)
        REQUIRE(vertex == *before.content->source.vertex(vertex.id));
    REQUIRE(model.undoStack()->count() == count + 1);
}

TEST_CASE("X Delete menu and F fill operate on components with cancel undo and same GL context",
          "[fill-delete-ui]") {
    FillDeleteWindow f;
    const auto before = record(*f.model, f.entity);
    const auto count = f.model->undoStack()->count();
    auto* glContext = f.viewport->context();
    const auto beforeFrame = f.viewport->grabFramebuffer();
    auto* menu = f.openDelete(Qt::Key_X);
    REQUIRE(menu->activeAction()->objectName() == QStringLiteral("DeleteFacesChoice"));
    const auto prefix = qEnvironmentVariable("MINI3D_TEST_FILL_DELETE_CAPTURE");
    if (!prefix.isEmpty())
        REQUIRE(menu->grab().save(prefix + QStringLiteral("-menu.png")));
    QTest::keyClick(menu, Qt::Key_Escape);
    REQUIRE(record(*f.model, f.entity).content == before.content);
    REQUIRE(f.model->undoStack()->count() == count);
    menu = f.openDelete(Qt::Key_Delete);
    QTest::keyClick(menu, Qt::Key_Return);
    REQUIRE(record(*f.model, f.entity).content->source.faces.size() == 5);
    REQUIRE(f.model->scene()->find(f.entity));
    REQUIRE(f.model->undoStack()->count() == count + 1);
    const auto holeFrame = f.viewport->grabFramebuffer();
    REQUIRE(holeFrame != beforeFrame);
    f.capture(QStringLiteral("-hole"));
    selectBoundary(*f.model, SelectionDomain::Edge);
    f.capture(QStringLiteral("-boundary"));
    f.pointAt();
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier, QStringLiteral("f"), true);
    QApplication::sendEvent(f.viewport, &repeat);
    REQUIRE(f.model->undoStack()->count() == count + 1);
    QTest::keyClick(f.viewport, Qt::Key_F);
    REQUIRE(record(*f.model, f.entity).content->source.faces.size() == 6);
    REQUIRE(f.model->undoStack()->count() == count + 2);
    f.capture(QStringLiteral("-filled"));
    REQUIRE_FALSE(f.viewport->grabFramebuffer().isNull());
    REQUIRE(f.viewport->grabFramebuffer() != holeFrame);
    REQUIRE(f.viewport->context() == glContext);
    f.pointAt();
    QTest::keyClick(f.viewport, Qt::Key_Z, Qt::ControlModifier);
    REQUIRE(record(*f.model, f.entity).content->source.faces.size() == 5);
    QTest::keyClick(f.viewport, Qt::Key_Z, Qt::ControlModifier);
    REQUIRE(record(*f.model, f.entity).content == before.content);
    REQUIRE(f.model->componentSelection().domain() == SelectionDomain::Face);
    REQUIRE(f.viewport->grabFramebuffer() == beforeFrame);
}

TEST_CASE("Topology tools keep object transforms and modal X while empty geometry stays renderable",
          "[fill-delete-ui]") {
    FillDeleteWindow f;
    const auto world = f.model->scene()->worldMatrix(f.entity);
    const auto before = record(*f.model, f.entity).content;
    f.pointAt();
    QTest::keyClick(f.viewport, Qt::Key_G);
    QTest::keyClick(f.viewport, Qt::Key_X);
    REQUIRE_FALSE(QApplication::activePopupWidget());
    REQUIRE(f.model->hasComponentTransform());
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(record(*f.model, f.entity).content == before);
    REQUIRE(f.model->deleteComponents(SelectionDomain::Face));
    selectBoundary(*f.model, SelectionDomain::Edge);
    REQUIRE(f.model->fillFace());
    REQUIRE(f.model->scene()->worldMatrix(f.entity) == world);
    f.model->selectAllComponents();
    REQUIRE(f.model->deleteComponents(SelectionDomain::Face));
    REQUIRE_FALSE(f.viewport->grabFramebuffer().isNull());
    REQUIRE(f.model->isEditMode());
    REQUIRE(record(*f.model, f.entity).content->derived.mesh.indices.empty());
    REQUIRE(f.model->setEditMode(false));
    f.pointAt();
    QTest::keyClick(f.viewport, Qt::Key_Delete);
    REQUIRE_FALSE(f.model->scene()->find(f.entity));
    f.model->undo();
    REQUIRE(f.model->scene()->find(f.entity));
}

TEST_CASE("Deletion popup rejects changed selection and text IME and Legacy do not steal F or X",
          "[fill-delete-ui]") {
    FillDeleteWindow f;
    const auto before = record(*f.model, f.entity);
    auto* menu = f.openDelete(Qt::Key_X);
    auto* choice = menu->findChild<QAction*>(QStringLiteral("DeleteFacesChoice"));
    REQUIRE(choice);
    f.model->selectComponent({2}, SelectionOperation::Replace);
    choice->trigger();
    REQUIRE(record(*f.model, f.entity).content == before.content);
    menu->hide();
    f.pointAt();
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"), true);
    QApplication::sendEvent(f.viewport, &repeat);
    REQUIRE_FALSE(QApplication::activePopupWidget());
    auto* name = f.window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    REQUIRE(name);
    name->setFocus();
    QTest::keyClick(name, Qt::Key_X);
    QTest::keyClick(name, Qt::Key_Delete);
    REQUIRE_FALSE(QApplication::activePopupWidget());
    REQUIRE(record(*f.model, f.entity).content == before.content);
    f.pointAt();
    QInputMethodEvent ime(QStringLiteral("中"), {});
    QApplication::sendEvent(f.viewport, &ime);
    QTest::keyClick(f.viewport, Qt::Key_X);
    REQUIRE_FALSE(QApplication::activePopupWidget());
    QInputMethodEvent end;
    QApplication::sendEvent(f.viewport, &end);
    f.router->setKeymap(editor::EditorKeymap::Legacy);
    f.pointAt();
    QTest::keyClick(f.viewport, Qt::Key_X);
    REQUIRE_FALSE(QApplication::activePopupWidget());
    f.model->selectComponent({1}, SelectionOperation::Replace);
    REQUIRE(f.model->deleteComponents(SelectionDomain::Face));
    selectBoundary(*f.model, SelectionDomain::Edge);
    const auto hole = record(*f.model, f.entity).content;
    f.pointAt();
    QTest::keyClick(f.viewport, Qt::Key_F);
    REQUIRE(record(*f.model, f.entity).content == hole);
    f.window.findChild<QAction*>(QStringLiteral("FillFaces"))->trigger();
    REQUIRE(record(*f.model, f.entity).content->source.faces.size() == 6);
}

TEST_CASE("F3 and Q expose real fill and delete operators through the same history",
          "[fill-delete-ui]") {
    FillDeleteWindow f;
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    auto* favorites = f.window.findChild<editor::QuickFavorites*>();
    auto* popup = f.window.findChild<editor::OperatorSearchPopup*>();
    auto* query = popup->findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* results = popup->findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"));
    REQUIRE(registry->descriptor(QStringLiteral("mesh.fill_face")));
    REQUIRE(favorites->addOperator(QStringLiteral("mesh.fill_face")));
    for (const auto key : {Qt::Key_F3, Qt::Key_Q}) {
        REQUIRE(f.model->deleteComponents(SelectionDomain::Face));
        selectBoundary(*f.model, SelectionDomain::Vertex);
        const auto count = f.model->undoStack()->count();
        f.pointAt();
        QTest::keyClick(f.viewport, key);
        REQUIRE(popup->isVisible());
        if (key == Qt::Key_F3)
            QTest::keyClicks(query, "fill face");
        REQUIRE(results->count() == 1);
        results->setCurrentRow(0);
        QTest::keyClick(results, Qt::Key_Return);
        REQUIRE(record(*f.model, f.entity).content->source.faces.size() == 6);
        REQUIRE(f.model->undoStack()->count() == count + 1);
    }
    const auto context = registry->captureContext(editor::InputArea::Viewport);
    REQUIRE(registry->execute(QStringLiteral("mesh.delete_faces"), context));
    REQUIRE_FALSE(registry->execute(QStringLiteral("mesh.delete_faces"), context));
    REQUIRE(record(*f.model, f.entity).content->source.faces.size() == 5);
}
