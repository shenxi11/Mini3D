/*
 * 模块名: ComponentBoxTests
 * 功能概述: 验证真实工作台框选、输入取消、独立显示状态与单次选区发布。
 * 对外接口: Catch2 [box-session]；依赖关系: Qt Test、编辑器工作台、真实 GL。
 * 输入输出: B/鼠标/弹窗/模式意图到稳定选区、截图和保存状态断言。
 * 异常与错误: 失效上下文不得落选区；维护说明: 使用独立测试窗口，不写用户场景。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/ComponentInteraction.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QRubberBand>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <vector>

using namespace mini3d;
using editor::ComponentId;
using editor::SelectionDomain;
using editor::SelectionOperation;
namespace {
void pointAt(QWidget* widget, QPoint point) {
    widget->setFocus();
    QMouseEvent event(QEvent::MouseMove, point, widget->mapToGlobal(point), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}
struct BoxWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::ComponentInteraction* interaction = window.findChild<editor::ComponentInteraction*>();
    editor::KeymapRouter* router = window.findChild<editor::KeymapRouter*>();
    core::EntityId cube = 0;
    BoxWindow() {
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        router->setKeymap(editor::EditorKeymap::Blender);
        model->newScene();
        cube = model->createEntity(core::PrimitiveKind::Cube);
        REQUIRE(model->setEditMode(true));
        REQUIRE(viewport->focusSelection());
        viewport->setCameraView(renderer_gl::EditorView::Front);
        viewport->setOrthographic(true);
        QTest::qWait(40);
        pointAt(viewport, viewport->rect().center());
    }
    QAction* action(const char* name) {
        return window.findChild<QAction*>(QString::fromLatin1(name));
    }
    void begin(QPoint origin, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        pointAt(viewport, origin);
        QTest::keyClick(viewport, Qt::Key_B);
        REQUIRE(interaction->isBoxSelecting());
        QTest::mousePress(viewport, Qt::LeftButton, modifiers, origin);
        REQUIRE(interaction->isBoxSelecting());
    }
    void move(QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::LeftButton, modifiers);
        QApplication::sendEvent(viewport, &event);
    }
    void finish(QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        move(point, modifiers);
        QTest::mouseRelease(viewport, Qt::LeftButton, modifiers, point);
        REQUIRE_FALSE(interaction->isBoxSelecting());
    }
};
renderer_gl::ComponentOverlay referenceOverlay(const BoxWindow& ui) {
    const auto content = ui.model->displayedEditableMesh(ui.cube);
    const auto& source = content->source;
    const auto world = ui.model->scene()->worldMatrix(ui.cube);
    const auto& visibility = ui.model->viewportVisibility();
    const auto& selected = ui.model->displayedComponentSelection();
    renderer_gl::ComponentOverlay result;
    const glm::vec4 normal{0.12F, 0.12F, 0.12F, 1}, chosen{1.0F, 0.5F, 0.05F, 1},
        active{1, 0.9F, 0.65F, 1};
    const auto color = [&](ComponentId id) {
        return selected.activeId() == id             ? active
               : selected.selectedIds().contains(id) ? chosen
                                                     : normal;
    };
    const auto position = [&](core::modeling::VertexId id) {
        return glm::vec3(world * glm::vec4(source.vertex(id)->position, 1));
    };
    auto selectedEdges = selected;
    selectedEdges.setDomain(source, SelectionDomain::Edge);
    for (const auto id : editor::ComponentSelection::elements(source, SelectionDomain::Edge)) {
        if (!visibility.isEdgeVisible(source, {id.first, id.second}))
            continue;
        const auto edgeColor = selected.domain() == SelectionDomain::Edge ? color(id)
                               : selectedEdges.selectedIds().contains(id) ? chosen
                                                                          : normal;
        result.lines.push_back({position(id.first), edgeColor});
        result.lines.push_back({position(id.second), edgeColor});
    }
    if (selected.domain() == SelectionDomain::Vertex) {
        for (const auto& vertex : source.vertices)
            if (visibility.isVertexVisible(source, vertex.id))
                result.points.push_back({position(vertex.id), color({vertex.id})});
    } else if (selected.domain() == SelectionDomain::Face) {
        for (const auto& face : source.faces) {
            if (!visibility.isFaceVisible(face))
                continue;
            glm::vec3 center{0};
            for (const auto& corner : face.corners)
                center += position(corner.vertex);
            result.points.push_back(
                {center / static_cast<float>(face.corners.size()), color({face.id})});
        }
        const auto& derived = content->derived;
        for (std::size_t i = 0; i < derived.triangleSources.size(); ++i) {
            const ComponentId face{derived.triangleSources[i].face};
            if (!selected.selectedIds().contains(face) ||
                !visibility.isFaceVisible(source, face.first))
                continue;
            auto faceColor = color(face);
            faceColor.a = 0.22F;
            for (std::size_t j = 0; j < 3; ++j) {
                const auto vertex = derived.mesh.indices[i * 3 + j];
                result.triangles.push_back(
                    {glm::vec3(world * glm::vec4(derived.mesh.vertices[vertex].position, 1)),
                     faceColor});
            }
        }
    }
    return result;
}
} // namespace

TEST_CASE("B rectangle replace add remove each publishes one selection without changing history",
          "[box-session]") {
    BoxWindow ui;
    QTemporaryDir directory;
    REQUIRE(ui.model->saveScene(directory.filePath(QStringLiteral("box.m3dscene"))));
    const auto index = ui.model->undoStack()->index();
    const auto original = ui.model->componentSelection();
    const auto mesh =
        ui.model->scene()->editableMesh(ui.model->scene()->find(ui.cube)->editableMesh)->content;
    std::set<ComponentId> left, right;
    for (const auto& vertex : mesh->source.vertices) {
        if (vertex.position.z == 0.5F) {
            (vertex.position.x < 0 ? left : right).insert({vertex.id});
        }
    }
    QSignalSpy changes(ui.model, &editor::SceneViewModel::componentSelectionChanged);
    const QPoint leftTop(1, 1), leftBottom(ui.viewport->width() / 2 - 1, ui.viewport->height() - 2);
    ui.begin(leftTop);
    ui.move(leftBottom);
    REQUIRE(ui.model->componentSelection() == original);
    REQUIRE(changes.empty());
    auto* band = ui.viewport->findChild<QRubberBand*>(QStringLiteral("ComponentBoxBand"));
    REQUIRE(band->isVisible());
    REQUIRE(band->geometry() == QRect(leftTop, leftBottom));
    const auto capture = qEnvironmentVariable("MINI3D_TEST_BOX_CAPTURE");
    if (!capture.isEmpty()) {
        QTest::qWait(40);
        REQUIRE(ui.window.grab().save(capture));
    }
    ui.finish(leftBottom);
    REQUIRE(changes.count() == 1);
    REQUIRE(ui.model->componentSelection().selectedIds() == left);
    REQUIRE_FALSE(band->isVisible());
    const QPoint rightTop(ui.viewport->width() / 2 + 1, 1);
    const QPoint rightBottom(ui.viewport->width() - 2, ui.viewport->height() - 2);
    ui.begin(rightBottom, Qt::ShiftModifier);
    ui.finish(rightTop, Qt::ShiftModifier);
    REQUIRE(changes.count() == 2);
    REQUIRE(ui.model->componentSelection().selectedIds().size() == 4);
    ui.begin(leftTop, Qt::ControlModifier);
    ui.finish(leftBottom, Qt::ControlModifier);
    REQUIRE(changes.count() == 3);
    REQUIRE(ui.model->componentSelection().selectedIds() == right);
    REQUIRE(ui.model->undoStack()->index() == index);
    REQUIRE_FALSE(ui.model->isModified());
    REQUIRE(
        ui.model->scene()->editableMesh(ui.model->scene()->find(ui.cube)->editableMesh)->content ==
        mesh);
}

TEST_CASE("Box cancellation loses no selection and sidebar never commits a viewport rectangle",
          "[box-session]") {
    BoxWindow ui;
    ui.action("ToggleViewportSidebar")->setChecked(true);
    QTest::qWait(40);
    const auto before = ui.model->componentSelection();
    const auto index = ui.model->undoStack()->index();
    for (const auto eventType : {QEvent::FocusOut, QEvent::WindowDeactivate, QEvent::Hide,
                                 QEvent::Resize, QEvent::UngrabMouse}) {
        ui.begin({20, 20});
        ui.move({100, 100});
        if (eventType == QEvent::Resize) {
            QResizeEvent resize(ui.viewport->size(), ui.viewport->size());
            QApplication::sendEvent(ui.viewport, &resize);
        } else if (eventType == QEvent::FocusOut) {
            QFocusEvent focus(eventType);
            QApplication::sendEvent(ui.viewport, &focus);
        } else {
            QEvent event(eventType);
            QApplication::sendEvent(ui.viewport, &event);
        }
        REQUIRE_FALSE(ui.interaction->isBoxSelecting());
        QTest::mouseRelease(ui.viewport, Qt::LeftButton, Qt::NoModifier, {100, 100});
        REQUIRE(ui.model->componentSelection() == before);
    }
    for (const bool keyboard : {true, false}) {
        ui.begin({20, 20});
        if (keyboard)
            QTest::keyClick(ui.viewport, Qt::Key_Escape);
        else
            QTest::mouseClick(ui.viewport, Qt::RightButton, Qt::NoModifier, {40, 40});
        REQUIRE_FALSE(ui.interaction->isBoxSelecting());
        QTest::mouseRelease(ui.viewport, Qt::LeftButton, Qt::NoModifier, {100, 100});
        REQUIRE(ui.model->componentSelection() == before);
    }
    ui.begin({20, 20});
    ui.finish({ui.viewport->width() + 20, 50});
    REQUIRE(ui.model->componentSelection() == before);
    auto* sidebar = ui.window.findChild<QLabel*>(QStringLiteral("SidebarSelection"));
    REQUIRE(ui.interaction->startBoxSelection());
    QTest::mouseClick(sidebar, Qt::LeftButton);
    REQUIRE_FALSE(ui.interaction->isBoxSelecting());
    REQUIRE(ui.model->componentSelection() == before);
    REQUIRE(ui.model->undoStack()->index() == index);
}

TEST_CASE("Box snapshot invalidates on geometry domain camera document and keymap changes",
          "[box-session]") {
    BoxWindow ui;
    const auto before = ui.model->componentSelection();
    ui.begin({20, 20});
    auto mesh = ui.model->scene()
                    ->editableMesh(ui.model->scene()->find(ui.cube)->editableMesh)
                    ->content->source;
    mesh.vertices[0].position.x -= 0.1F;
    REQUIRE(ui.model->replaceEditableMesh(ui.cube, mesh));
    REQUIRE_FALSE(ui.interaction->isBoxSelecting());
    REQUIRE(ui.model->componentSelection() == before);
    ui.begin({20, 20});
    ui.viewport->setCameraView(renderer_gl::EditorView::Right);
    REQUIRE_FALSE(ui.interaction->isBoxSelecting());
    ui.begin({20, 20});
    ui.model->setSelectionDomain(SelectionDomain::Face);
    REQUIRE_FALSE(ui.interaction->isBoxSelecting());
    ui.begin({20, 20});
    ui.router->setKeymap(editor::EditorKeymap::Legacy);
    REQUIRE_FALSE(ui.interaction->isBoxSelecting());
    pointAt(ui.viewport, {30, 30});
    QTest::keyClick(ui.viewport, Qt::Key_B);
    REQUIRE_FALSE(ui.interaction->isBoxSelecting());
    ui.action("BoxSelectComponents")->trigger();
    REQUIRE(ui.interaction->isBoxSelecting());
    ui.model->newScene();
    REQUIRE_FALSE(ui.interaction->isBoxSelecting());
    REQUIRE_FALSE(ui.model->isEditMode());
    REQUIRE_FALSE(ui.action("BoxSelectComponents")->isEnabled());
}

TEST_CASE("X-Ray and Overlay are independent view states with real GPU feedback",
          "[box-session][component-display]") {
    BoxWindow ui;
    QTemporaryDir directory;
    REQUIRE(ui.model->saveScene(directory.filePath(QStringLiteral("display.m3dscene"))));
    const auto before = ui.model->componentSelection();
    const auto index = ui.model->undoStack()->index();
    const auto context = ui.viewport->context();
    ui.viewport->setOrthographic(false);
    const auto visibleImage = ui.viewport->grabFramebuffer();
    QTest::keyClick(ui.viewport, Qt::Key_Z, Qt::AltModifier);
    REQUIRE(ui.viewport->isXRayEnabled());
    REQUIRE(ui.action("ToggleXRay")->isChecked());
    const auto xRayImage = ui.viewport->grabFramebuffer();
    REQUIRE(xRayImage != visibleImage);
    QTest::keyClick(ui.viewport, Qt::Key_Z, Qt::AltModifier | Qt::ShiftModifier);
    REQUIRE_FALSE(ui.viewport->isOverlayVisible());
    REQUIRE(ui.viewport->isXRayEnabled());
    const auto cleanImage = ui.viewport->grabFramebuffer();
    REQUIRE(cleanImage != xRayImage);
    ui.viewport->setXRayEnabled(false);
    REQUIRE_FALSE(ui.action("ToggleXRay")->isChecked());
    REQUIRE(ui.viewport->grabFramebuffer() == cleanImage);
    ui.action("ToggleOverlays")->trigger();
    REQUIRE(ui.viewport->grabFramebuffer() == visibleImage);
    REQUIRE(ui.model->componentSelection() == before);
    REQUIRE_FALSE(ui.model->isModified());
    REQUIRE(ui.model->undoStack()->index() == index);
    REQUIRE(ui.viewport->context() == context);
    ui.viewport->setXRayEnabled(true);
    ui.model->clearComponentSelection();
    ui.viewport->setOverlayVisible(false);
    ui.begin({1, 1});
    ui.finish({ui.viewport->width() - 2, ui.viewport->height() - 2});
    REQUIRE(ui.model->componentSelection().selectedIds().size() == 8);
    REQUIRE(ui.model->isEditMode());
    REQUIRE_FALSE(ui.viewport->isOverlayVisible());
    // 真实单击与框选使用同一个穿透状态，不只是绘制变化。
    const auto camera = *ui.viewport->editorCameraSnapshot();
    const auto& source = ui.model->scene()
                             ->editableMesh(ui.model->scene()->find(ui.cube)->editableMesh)
                             ->content->source;
    const auto vertex =
        std::find_if(source.vertices.begin(), source.vertices.end(), [](const auto& item) {
            return item.position.z == -0.5F;
        });
    REQUIRE(vertex != source.vertices.end());
    const auto clip = camera.viewProjectionMatrix() * glm::vec4(vertex->position, 1);
    const QPoint pixel(qRound((clip.x / clip.w + 1) * 0.5F * ui.viewport->width()),
                       qRound((1 - clip.y / clip.w) * 0.5F * ui.viewport->height()));
    ui.viewport->setXRayEnabled(false);
    QTest::mouseClick(ui.viewport, Qt::LeftButton, Qt::NoModifier, pixel);
    REQUIRE(ui.model->componentSelection().selectedIds().empty());
    ui.viewport->setXRayEnabled(true);
    QTest::mouseClick(ui.viewport, Qt::LeftButton, Qt::NoModifier, pixel);
    REQUIRE(ui.model->componentSelection().selectedIds() == std::set<ComponentId>{{vertex->id}});
    REQUIRE(ui.model->undoStack()->index() == index);
}

TEST_CASE("Box F3 Q and menu use one action while text and IME retain input priority",
          "[box-session]") {
    BoxWindow ui;
    auto* registry = ui.window.findChild<editor::OperatorRegistry*>();
    auto* popup = ui.window.findChild<editor::OperatorSearchPopup*>();
    auto* favorites = ui.window.findChild<editor::QuickFavorites*>();
    auto* query = ui.window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* results = ui.window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"));
    REQUIRE(registry->descriptor(QStringLiteral("mesh.box_select"))->action ==
            ui.action("BoxSelectComponents"));
    for (const auto key : {Qt::Key_F3, Qt::Key_Q}) {
        if (key == Qt::Key_Q)
            favorites->addOperator(QStringLiteral("mesh.box_select"));
        pointAt(ui.viewport, {50, 50});
        QTest::keyClick(ui.viewport, key);
        REQUIRE(popup->isVisible());
        query->setText(QStringLiteral("Box Select"));
        REQUIRE(results->count() == 1);
        QTest::keyClick(query, Qt::Key_Return);
        QTest::qWait(20);
        REQUIRE_FALSE(popup->isVisible());
        REQUIRE(ui.interaction->isBoxSelecting());
        QTest::keyClick(ui.viewport, Qt::Key_Escape);
    }
    ui.begin({20, 20});
    QInputMethodEvent preedit(QStringLiteral("框选"), {});
    QApplication::sendEvent(ui.viewport, &preedit);
    REQUIRE_FALSE(ui.interaction->isBoxSelecting());
    QInputMethodEvent endPreedit;
    QApplication::sendEvent(ui.viewport, &endPreedit);
    auto* input = new QLineEdit(&ui.window);
    input->setGeometry(20, 20, 100, 30);
    input->show();
    input->setFocus();
    QTest::keyClicks(input, "b");
    REQUIRE(input->text() == QStringLiteral("b"));
    QTest::keyClick(input, Qt::Key_Z, Qt::AltModifier);
    REQUIRE_FALSE(ui.interaction->isBoxSelecting());
    REQUIRE_FALSE(ui.viewport->isXRayEnabled());
}

TEST_CASE("Overlay positions preserve pixels for reordered vertices with sparse 64 bit IDs",
          "[performance-overlay][component-display]") {
    BoxWindow ui;
    // 正视图前后点重叠且覆盖层不写深度，绘制顺序会改变圆点外缘；本例隔离位置索引。
    ui.viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
    ui.viewport->setOrthographic(false);
    REQUIRE(ui.viewport->focusSelection());
    QTest::qWait(30);
    const auto meshId = ui.model->scene()->find(ui.cube)->editableMesh;
    auto source = ui.model->scene()->editableMesh(meshId)->content->source;
    std::vector<std::pair<SelectionDomain, ComponentId>> selections;
    std::vector<QImage> images;
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        ui.model->clearComponentSelection();
        ui.model->setSelectionDomain(domain);
        const auto id = *editor::ComponentSelection::elements(source, domain).begin();
        ui.model->selectComponent(id, SelectionOperation::Replace);
        selections.emplace_back(domain, id);
        images.push_back(ui.viewport->grabFramebuffer());
        REQUIRE_FALSE(images.back().isNull());
    }
    constexpr std::uint64_t offset = std::uint64_t{1} << 40;
    for (auto& vertex : source.vertices)
        vertex.id += offset;
    std::reverse(source.vertices.begin(), source.vertices.end());
    for (auto& face : source.faces) {
        face.id += offset;
        for (auto& corner : face.corners)
            corner.vertex += offset;
    }
    REQUIRE(ui.model->replaceEditableMesh(ui.cube, source));
    const auto history = ui.model->undoStack()->index();
    for (std::size_t i = 0; i < selections.size(); ++i) {
        const auto [domain, oldId] = selections[i];
        ui.model->clearComponentSelection();
        ui.model->setSelectionDomain(domain);
        const ComponentId id{oldId.first + offset, oldId.second == 0 ? 0 : oldId.second + offset};
        ui.model->selectComponent(id, SelectionOperation::Replace);
        REQUIRE(ui.model->componentSelection().activeId() == id);
        const auto actual = ui.viewport->grabFramebuffer();
        INFO("domain=" << static_cast<int>(domain) << " before=" << images[i].width() << "x"
                       << images[i].height() << " after=" << actual.width() << "x"
                       << actual.height());
        const auto capture = qEnvironmentVariable("MINI3D_TEST_OVERLAY_DIAGNOSTICS");
        if (!capture.isEmpty()) {
            REQUIRE(QDir().mkpath(capture));
            REQUIRE(images[i].save(QDir(capture).filePath(QStringLiteral("%1-before.png").arg(i))));
            REQUIRE(actual.save(QDir(capture).filePath(QStringLiteral("%1-after.png").arg(i))));
        }
        REQUIRE(actual == images[i]);
        REQUIRE(ui.model->undoStack()->index() == history);
        REQUIRE(ui.model->scene()->editableMesh(meshId)->content->source == source);
    }
    ui.window.hide();
}

TEST_CASE("Overlay edge projections retain reference pixels for partial and full selections",
          "[performance-overlay][component-display]") {
    BoxWindow ui;
    ui.viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
    ui.viewport->setOrthographic(false);
    REQUIRE(ui.viewport->focusSelection());
    QTest::qWait(30);
    const auto content = ui.model->displayedEditableMesh(ui.cube);
    const auto& source = content->source;
    const auto world = ui.model->scene()->worldMatrix(ui.cube);
    const auto history = ui.model->undoStack()->index();
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        for (const bool all : {false, true}) {
            ui.model->clearComponentSelection();
            ui.model->setSelectionDomain(domain);
            if (all)
                ui.model->selectAllComponents();
            else
                ui.model->selectComponent(
                    *editor::ComponentSelection::elements(source, domain).begin(),
                    SelectionOperation::Replace);
            const auto selected = ui.model->componentSelection();
            const auto actual = ui.viewport->grabFramebuffer();
            REQUIRE_FALSE(actual.isNull());
            renderer_gl::ComponentOverlay reference;
            const glm::vec4 normal{0.12F, 0.12F, 0.12F, 1}, chosen{1.0F, 0.5F, 0.05F, 1},
                active{1, 0.9F, 0.65F, 1};
            const auto color = [&](ComponentId id) {
                return selected.activeId() == id             ? active
                       : selected.selectedIds().contains(id) ? chosen
                                                             : normal;
            };
            const auto position = [&](core::modeling::VertexId id) {
                return glm::vec3(world * glm::vec4(source.vertex(id)->position, 1));
            };
            // 独立沿原选择域投影语义构造参考，不复用优化后的边收集逻辑。
            auto selectedEdges = selected;
            selectedEdges.setDomain(source, SelectionDomain::Edge);
            for (const auto id :
                 editor::ComponentSelection::elements(source, SelectionDomain::Edge)) {
                const auto edgeColor = domain == SelectionDomain::Edge            ? color(id)
                                       : selectedEdges.selectedIds().contains(id) ? chosen
                                                                                  : normal;
                reference.lines.push_back({position(id.first), edgeColor});
                reference.lines.push_back({position(id.second), edgeColor});
            }
            if (domain == SelectionDomain::Vertex) {
                for (const auto& vertex : source.vertices)
                    reference.points.push_back({position(vertex.id), color({vertex.id})});
            } else if (domain == SelectionDomain::Face) {
                for (const auto& face : source.faces) {
                    glm::vec3 center{0};
                    for (const auto& corner : face.corners)
                        center += position(corner.vertex);
                    reference.points.push_back(
                        {center / static_cast<float>(face.corners.size()), color({face.id})});
                }
                for (std::size_t triangle = 0; triangle < content->derived.triangleSources.size();
                     ++triangle) {
                    const ComponentId id{content->derived.triangleSources[triangle].face};
                    if (!selected.selectedIds().contains(id))
                        continue;
                    auto faceColor = color(id);
                    faceColor.a = 0.22F;
                    for (std::size_t corner = 0; corner < 3; ++corner) {
                        const auto index = content->derived.mesh.indices[triangle * 3 + corner];
                        reference.triangles.push_back(
                            {glm::vec3(
                                 world *
                                 glm::vec4(content->derived.mesh.vertices[index].position, 1)),
                             faceColor});
                    }
                }
            }
            ui.viewport->setComponentOverlay(std::move(reference));
            INFO("domain=" << static_cast<int>(domain) << " all=" << all);
            REQUIRE(ui.viewport->grabFramebuffer() == actual);
            REQUIRE(ui.model->componentSelection() == selected);
            REQUIRE(ui.model->undoStack()->index() == history);
        }
    }
    ui.window.hide();
}

TEST_CASE("Overlay topology reuse follows preview hidden state layout replacement and history",
          "[performance-overlay][component-display][perf-closeout]") {
    BoxWindow ui;
    ui.viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
    ui.viewport->setOrthographic(false);
    REQUIRE(ui.viewport->focusSelection());
    const auto checkPixels = [&] {
        const auto actual = ui.viewport->grabFramebuffer();
        REQUIRE_FALSE(actual.isNull());
        ui.viewport->setComponentOverlay(referenceOverlay(ui));
        REQUIRE(ui.viewport->grabFramebuffer() == actual);
    };
    const auto initial = ui.model->displayedEditableMesh(ui.cube)->source;
    for (const auto domain :
         {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        ui.model->clearComponentSelection();
        ui.model->setSelectionDomain(domain);
        ui.model->selectComponent(*editor::ComponentSelection::elements(initial, domain).begin(),
                                  SelectionOperation::Replace);
        checkPixels();
        REQUIRE(ui.model->hideSelection());
        checkPixels();
        REQUIRE(ui.model->revealHidden());
        checkPixels();
    }
    ui.model->clearComponentSelection();
    ui.model->setSelectionDomain(SelectionDomain::Vertex);
    ui.model->selectComponent({initial.vertices.front().id}, SelectionOperation::Replace);
    const auto history = ui.model->undoStack()->index();
    REQUIRE(ui.model->beginComponentTransform());
    for (const auto offset : {0.1, 0.2, -0.1}) {
        REQUIRE(ui.model->previewComponentTransform(
            glm::translate(glm::dmat4(1), glm::dvec3(offset, 0, 0))));
        checkPixels();
    }
    REQUIRE(ui.model->finishComponentTransform(false));
    checkPixels();
    REQUIRE(ui.model->undoStack()->index() == history);
    auto reordered = initial;
    std::reverse(reordered.vertices.begin(), reordered.vertices.end());
    std::reverse(reordered.faces.begin(), reordered.faces.end());
    REQUIRE(ui.model->replaceEditableMesh(ui.cube, reordered));
    checkPixels();
    ui.model->undo();
    checkPixels();
    ui.model->redo();
    checkPixels();
    // 保持身份及容器排列，只置换顶点引用；活动面的邻边归属及中心必须更新。
    ui.model->clearComponentSelection();
    ui.model->setSelectionDomain(SelectionDomain::Face);
    ui.model->selectComponent({initial.faces.front().id}, SelectionOperation::Replace);
    checkPixels();
    auto rewired = reordered;
    for (auto& face : rewired.faces) {
        for (auto& corner : face.corners) {
            const auto position = reordered.vertex(corner.vertex)->position;
            const glm::vec3 rotated{position.z, position.y, -position.x};
            const auto target = std::find_if(reordered.vertices.begin(), reordered.vertices.end(),
                                             [&](const auto& vertex) {
                                                 return vertex.position == rotated;
                                             });
            REQUIRE(target != reordered.vertices.end());
            corner.vertex = target->id;
        }
    }
    REQUIRE(ui.model->replaceEditableMesh(ui.cube, rewired));
    checkPixels();
    ui.model->newScene();
    REQUIRE_FALSE(ui.model->isEditMode());
    ui.window.hide();
}
