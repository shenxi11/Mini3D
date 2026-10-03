/*
 * 模块名: TopologySelectionEditorTests
 * 功能概述: 验证Loop/Ring/L输入、稳定选区、隐藏过滤与无历史副作用。
 * 对外接口: Catch2 [topology-selection-editor]。
 * 依赖关系: SceneViewModel、真实Qt视口与输入路由。
 * 输入输出: 源拓扑与点击/菜单/按键到真实组件选区。
 * 异常与错误: 无效种子保持原选择，不跨断开部件或选择隐藏元素。
 * 维护说明: 临时模型和测试窗口；不改用户工程，不把合成输入当作真实IME。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/OperatorRegistry.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
core::modeling::EditableMesh selectionGrid(bool disconnected = false) {
    core::modeling::EditableMesh mesh;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            mesh.vertices.push_back({static_cast<std::uint64_t>(mesh.vertices.size() + 1),
                                     {static_cast<float>(x), static_cast<float>(y), 0}});
    std::uint64_t cornerId = 0;
    for (std::uint64_t y = 0; y < 3; ++y) {
        for (std::uint64_t x = 0; x < 3; ++x) {
            const auto first = y * 4 + x + 1;
            core::modeling::EditableFace face;
            face.id = mesh.faces.size() + 1;
            for (const auto vertex : {first, first + 1, first + 5, first + 4})
                face.corners.push_back({++cornerId, vertex});
            mesh.faces.push_back(std::move(face));
        }
    }
    if (disconnected) {
        core::modeling::EditableFace face;
        face.id = 100;
        for (const auto position :
             {glm::vec3(5, 0, 0), glm::vec3(6, 0, 0), glm::vec3(6, 1, 0), glm::vec3(5, 1, 0)}) {
            const auto id = 101 + face.corners.size();
            mesh.vertices.push_back({id, position});
            face.corners.push_back({++cornerId, id});
        }
        mesh.faces.push_back(std::move(face));
    }
    return mesh;
}
core::EntityId installGrid(editor::SceneViewModel& model, bool disconnected = false) {
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    REQUIRE(model.replaceEditableMesh(cube, selectionGrid(disconnected)));
    model.clearComponentSelection();
    return cube;
}
std::set<editor::ComponentId> edgeIds(std::initializer_list<core::modeling::EdgeKey> edges) {
    std::set<editor::ComponentId> result;
    for (const auto edge : edges)
        result.insert(editor::ComponentId::edge(edge));
    return result;
}
} // namespace

TEST_CASE("Topology paths set source edge selections without changing history or geometry",
          "[topology-selection-editor]") {
    editor::SceneViewModel model;
    const auto cube = installGrid(model);
    model.setSelectionDomain(editor::SelectionDomain::Edge);
    const auto source =
        model.scene()->editableMesh(model.scene()->find(cube)->editableMesh)->content;
    const auto count = model.undoStack()->count();
    REQUIRE(model.selectEdgePath({6, 7}, false));
    REQUIRE(model.componentSelection().selectedIds() == edgeIds({{5, 6}, {6, 7}, {7, 8}}));
    REQUIRE(model.componentSelection().activeId() == editor::ComponentId::edge({6, 7}));
    REQUIRE(model.selectEdgePath({6, 7}, true));
    REQUIRE(model.componentSelection().selectedIds() ==
            edgeIds({{2, 3}, {6, 7}, {10, 11}, {14, 15}}));
    const auto before = model.componentSelection();
    REQUIRE_FALSE(model.selectEdgePath({6, 11}, true));
    REQUIRE(model.componentSelection() == before);
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE(model.scene()->editableMesh(model.scene()->find(cube)->editableMesh)->content ==
            source);
    model.setSelectionDomain(editor::SelectionDomain::Vertex);
    REQUIRE_FALSE(model.selectEdgePath({6, 7}, false));
}

TEST_CASE("Linked component selection projects to each domain and respects hidden source elements",
          "[topology-selection-editor]") {
    editor::SceneViewModel model;
    const auto cube = installGrid(model, true);
    const auto count = model.undoStack()->count();
    REQUIRE(model.selectConnected(editor::ComponentId{6}));
    REQUIRE(model.componentSelection().selectedIds().size() == 16);
    REQUIRE_FALSE(model.componentSelection().selectedIds().contains({101}));
    model.setSelectionDomain(editor::SelectionDomain::Edge);
    model.clearComponentSelection();
    REQUIRE(model.selectConnected(editor::ComponentId::edge({6, 7})));
    REQUIRE(model.componentSelection().selectedIds().size() == 24);
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.clearComponentSelection();
    REQUIRE(model.selectConnected(editor::ComponentId{5}));
    REQUIRE(model.componentSelection().selectedIds().size() == 9);
    REQUIRE_FALSE(model.componentSelection().selectedIds().contains({100}));
    model.setSelectionDomain(editor::SelectionDomain::Vertex);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    REQUIRE(model.hideSelection());
    REQUIRE(model.selectConnected(editor::ComponentId{6}));
    REQUIRE_FALSE(model.componentSelection().selectedIds().contains({1}));
    for (const auto id : model.componentSelection().selectedIds())
        REQUIRE(model.isComponentVisible(id));
    REQUIRE(model.scene()->editableMesh(model.scene()->find(cube)->editableMesh)->content->source ==
            selectionGrid(true));
    REQUIRE(model.undoStack()->count() == count);
}

TEST_CASE("Alt clicks L and menu registry share real topology selection and respect keymap",
          "[topology-selection-editor][topology-selection-ui]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    auto* registry = window.findChild<editor::OperatorRegistry*>();
    installGrid(*model);
    router->setKeymap(editor::EditorKeymap::Blender);
    model->setSelectionDomain(editor::SelectionDomain::Edge);
    viewport->setEditorCamera({{1.5F, 1.5F, 8}, {1.5F, 1.5F, 0}, 0, 50});
    const auto camera = viewport->editorCameraSnapshot();
    REQUIRE(camera);
    const auto clip = camera->viewProjectionMatrix() * glm::vec4(1.5F, 1, 0, 1);
    const QPoint point(qRound((clip.x / clip.w + 1) * viewport->width() * .5),
                       qRound((1 - clip.y / clip.w) * viewport->height() * .5));
    viewport->setFocus();
    QMouseEvent move(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    const auto count = model->undoStack()->count();
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::AltModifier, point);
    REQUIRE(model->componentSelection().selectedIds() == edgeIds({{5, 6}, {6, 7}, {7, 8}}));
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::AltModifier | Qt::ControlModifier, point);
    REQUIRE(model->componentSelection().selectedIds() ==
            edgeIds({{2, 3}, {6, 7}, {10, 11}, {14, 15}}));
    REQUIRE(registry->execute(
        "mesh.select_loop",
        registry->captureContext(editor::InputArea::Viewport, editor::EditorKeymap::Blender)));
    REQUIRE(model->componentSelection().selectedIds().size() == 3);
    model->clearComponentSelection();
    QApplication::sendEvent(viewport, &move);
    QTest::keyClick(viewport, Qt::Key_L);
    REQUIRE(model->componentSelection().selectedIds().size() == 24);
    router->setKeymap(editor::EditorKeymap::Legacy);
    model->clearComponentSelection();
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::AltModifier, point);
    REQUIRE(model->componentSelection().selectedIds().empty());
    REQUIRE(model->undoStack()->count() == count);
    window.hide();
}
