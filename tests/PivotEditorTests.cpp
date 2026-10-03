/*
 * 模块名: PivotEditorTests
 * 功能概述: 验证三种枢轴、方向空间、定位历史与真实窗口交互。
 * 对外接口: Catch2 [pivot-editor]/[pivot-ui]；依赖关系: Qt Test、编辑器、GL。
 * 输入输出: 独立矩阵期望与合成输入到几何、历史、帧缓冲和可选截图。
 * 异常与错误: 位置、生命周期或历史不一致即失败；维护说明: 不改用户场景/偏好。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "editor/workbench/WorkbenchShell.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>

using namespace mini3d;
using editor::SelectionDomain;
using editor::SelectionOperation;
using editor::TransformPivot;
namespace {
auto mesh(const editor::SceneViewModel& model, core::EntityId id) {
    return model.scene()->editableMesh(model.scene()->find(id)->editableMesh)->content;
}
struct PivotWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::ObjectTransformSession* modal = window.findChild<editor::ObjectTransformSession*>();
    core::EntityId cube;
    PivotWindow() {
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Blender);
        model->newScene();
        cube = model->createEntity(core::PrimitiveKind::Cube);
        viewport->setEditorCamera({{4, 3, 7}, {0, 0, 0}, 0, 50});
        pointAt();
    }
    ~PivotWindow() {
        window.hide();
    }
    QAction* action(const char* name) {
        auto* result = window.findChild<QAction*>(QString::fromLatin1(name));
        REQUIRE(result);
        return result;
    }
    void pointAt() {
        viewport->setFocus();
        const auto point = viewport->rect().center() + QPoint(90, 0);
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
    }
    void start(Qt::Key key, const char* input) {
        pointAt();
        QTest::keyClick(viewport, key);
        REQUIRE(modal->isActive());
        QTest::keyClicks(viewport, input);
    }
    void capture(const QString& suffix) {
        const auto prefix = qEnvironmentVariable("MINI3D_TEST_PIVOT_CAPTURE");
        if (!prefix.isEmpty()) {
            QTest::qWait(60);
            REQUIRE(window.grab().save(prefix + suffix + QStringLiteral(".png")));
        }
    }
};
} // namespace

TEST_CASE("Pivot resolves unique median and active point edge face under parent transforms",
          "[pivot-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    REQUIRE_FALSE(model.transformPivotPosition());
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(id, parent));
    core::Transform transform;
    transform.position = {2, 1, -1};
    transform.rotation = glm::quat(glm::radians(glm::vec3(17, 31, 12)));
    transform.scale = {-2, 3, .5F};
    REQUIRE(model.setTransform(parent, transform));
    REQUIRE(model.setEditMode(true));
    const auto source = mesh(model, id)->source;
    const glm::dmat4 world(model.scene()->worldMatrix(id));
    for (auto domain : {SelectionDomain::Vertex, SelectionDomain::Edge, SelectionDomain::Face}) {
        model.setSelectionDomain(domain);
        model.clearComponentSelection();
        const editor::ComponentId first{1, domain == SelectionDomain::Edge ? 2U : 0U};
        const editor::ComponentId last{2, domain == SelectionDomain::Edge ? 3U : 0U};
        model.selectComponent(first, SelectionOperation::Replace);
        model.selectComponent(last, SelectionOperation::Add);
        glm::dvec3 localActive;
        std::set<core::modeling::VertexId> vertices;
        if (domain == SelectionDomain::Face) {
            for (int face = 0; face < 2; ++face)
                for (const auto& corner : source.faces[face].corners)
                    vertices.insert(corner.vertex);
            localActive = {0, 0, -.5}; // Cube 第二面是 -Z。
        } else if (domain == SelectionDomain::Edge) {
            vertices = {1, 2, 3};
            localActive =
                (glm::dvec3(source.vertex(2)->position) + glm::dvec3(source.vertex(3)->position)) /
                2.0;
        } else {
            vertices = {1, 2};
            localActive = source.vertex(2)->position;
        }
        glm::dvec3 median(0);
        for (const auto vertex : vertices)
            median += glm::dvec3(world * glm::dvec4(source.vertex(vertex)->position, 1));
        median /= static_cast<double>(vertices.size());
        const auto history = model.undoStack()->index();
        model.setTransformPivot(TransformPivot::Median);
        REQUIRE(glm::distance(*model.transformPivotPosition(), median) < 1e-6);
        model.setTransformPivot(TransformPivot::Active);
        REQUIRE(glm::distance(*model.transformPivotPosition(),
                              glm::dvec3(world * glm::dvec4(localActive, 1))) < 1e-6);
        model.selectComponent(last, SelectionOperation::Remove);
        REQUIRE(model.componentSelection().activeId() == first);
        REQUIRE(model.transformPivotPosition());
        model.setCursorPosition({3, 2, 1});
        model.setTransformPivot(TransformPivot::Cursor);
        REQUIRE(*model.transformPivotPosition() == glm::dvec3(3, 2, 1));
        REQUIRE(model.undoStack()->index() == history);
        model.clearComponentSelection();
        REQUIRE_FALSE(model.transformPivotPosition());
    }
    // 相邻两面共享顶点不能重复计权。
    model.setSelectionDomain(SelectionDomain::Face);
    model.selectComponent({1}, SelectionOperation::Replace);
    model.selectComponent({3}, SelectionOperation::Add);
    model.setTransformPivot(TransformPivot::Median);
    std::set<core::modeling::VertexId> unique;
    for (int face : {0, 2})
        for (const auto& corner : source.faces[face].corners)
            unique.insert(corner.vertex);
    REQUIRE(unique.size() == 6);
    glm::dvec3 sum(0);
    for (const auto vertex : unique)
        sum += glm::dvec3(world * glm::dvec4(source.vertex(vertex)->position, 1));
    REQUIRE(glm::distance(*model.transformPivotPosition(), sum / 6.0) < 1e-6);
}

TEST_CASE("Selection to cursor keeps offsets and one history through parent and save reload",
          "[pivot-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    REQUIRE_FALSE(model.moveSelectionToCursor());
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(id, parent));
    core::Transform transform;
    transform.position = {2, 3, -1};
    transform.rotation = glm::quat(glm::radians(glm::vec3(15, 30, 20)));
    transform.scale = {-2, 3, .5F};
    REQUIRE(model.setTransform(parent, transform));
    REQUIRE(model.setCursorPosition({1.5F, 1.2F, 2.3F}));
    const auto beforeObject = model.scene()->find(id)->transform;
    const auto history = model.undoStack()->index();
    REQUIRE(model.moveSelectionToCursor());
    REQUIRE(model.undoStack()->index() == history + 1);
    REQUIRE(glm::distance(glm::vec3(model.scene()->worldMatrix(id)[3]), model.cursor3D().position) <
            1e-5F);
    REQUIRE(model.scene()->find(id)->transform.rotation == beforeObject.rotation);
    REQUIRE(model.scene()->find(id)->transform.scale == beforeObject.scale);
    model.undo();
    REQUIRE(model.scene()->find(id)->transform.localMatrix() == beforeObject.localMatrix());
    model.redo();
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(SelectionDomain::Face);
    model.selectComponent({1}, SelectionOperation::Replace);
    const auto before = mesh(model, id);
    const auto selection = model.componentSelection();
    const auto world = glm::dmat4(model.scene()->worldMatrix(id));
    const auto delta = glm::dvec3(model.cursor3D().position) - *model.selectedComponentCenter();
    const auto selected = selection.selectedVertices(before->source);
    const auto index = model.undoStack()->index();
    model.setTransformPivot(TransformPivot::Active); // 定位以质心为准，不随 R/S 枢轴变化。
    REQUIRE(model.moveSelectionToCursor());
    REQUIRE(model.undoStack()->index() == index + 1);
    REQUIRE(glm::distance(*model.selectedComponentCenter(), glm::dvec3(model.cursor3D().position)) <
            1e-5);
    for (const auto& vertex : before->source.vertices) {
        const auto actual = mesh(model, id)->source.vertex(vertex.id)->position;
        const auto displacement = glm::dvec3(world * glm::dvec4(actual - vertex.position, 0));
        REQUIRE(glm::distance(displacement, selected.contains(vertex.id) ? delta : glm::dvec3(0)) <
                1e-5);
    }
    REQUIRE(model.componentSelection() == selection);
    const auto after = mesh(model, id)->source;
    model.undo();
    REQUIRE(mesh(model, id)->source == before->source);
    model.redo();
    REQUIRE(mesh(model, id)->source == after);
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("pivot.m3dscene"));
    REQUIRE(model.saveScene(path));
    model.setTransformPivot(TransformPivot::Cursor);
    REQUIRE_FALSE(model.isModified());
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(mesh(reopened, id)->source == after);
    REQUIRE(reopened.cursor3D() == model.cursor3D());
    REQUIRE(reopened.transformPivot() == TransformPivot::Median); // 窗口偏好不写文件。
    model.clearComponentSelection();
    REQUIRE_FALSE(model.moveSelectionToCursor());
    REQUIRE(model.undoStack()->index() == index + 1);
}

TEST_CASE("Component R S use all three frozen pivots independently of Global Local axes",
          "[pivot-ui]") {
    PivotWindow f;
    core::Transform transform;
    transform.rotation = glm::quat(glm::radians(glm::vec3(20, 35, 15)));
    transform.scale = {-1, 2, 1};
    REQUIRE(f.model->setTransform(f.cube, transform));
    REQUIRE(f.model->setEditMode(true));
    f.model->setSelectionDomain(SelectionDomain::Face);
    f.model->selectAllComponents();
    f.model->selectComponent({3}, SelectionOperation::Add);
    f.model->setCursorPosition({1.2F, .7F, 0});
    const auto before = mesh(*f.model, f.cube);
    const glm::dmat4 world(f.model->scene()->worldMatrix(f.cube));
    const auto selected = f.model->componentSelection().selectedVertices(before->source);
    const auto index = f.model->undoStack()->index();
    for (auto pivotMode :
         {TransformPivot::Median, TransformPivot::Active, TransformPivot::Cursor}) {
        f.model->setTransformPivot(pivotMode);
        const auto pivot = *f.model->transformPivotPosition();
        for (bool local : {false, true}) {
            f.action(local ? "LocalTransformSpace" : "WorldTransformSpace")->trigger();
            const glm::dmat4 axes(
                local ? glm::dmat3(glm::mat3_cast(f.model->scene()->worldRotation(f.cube)))
                      : glm::dmat3(1));
            for (auto operation : {Qt::Key_R, Qt::Key_S}) {
                f.start(operation, operation == Qt::Key_R ? "z30" : "x1.2");
                INFO("pivot=" << static_cast<int>(pivotMode) << " local=" << local
                              << " operation=" << static_cast<int>(operation)
                              << " status=" << f.modal->statusText().toStdString());
                REQUIRE(f.modal->statusText().contains(f.model->transformPivotName()));
                const auto linear =
                    operation == Qt::Key_R
                        ? glm::rotate(glm::dmat4(1), glm::radians(30.0), glm::dvec3(axes[2]))
                        : axes * glm::scale(glm::dmat4(1), glm::dvec3(1.2, 1, 1)) *
                              glm::transpose(axes);
                REQUIRE(f.model->componentPreview());
                for (const auto& vertex : before->source.vertices) {
                    INFO("vertex=" << vertex.id);
                    const auto original = glm::dvec3(world * glm::dvec4(vertex.position, 1));
                    const auto expected = selected.contains(vertex.id)
                                              ? pivot + glm::dmat3(linear) * (original - pivot)
                                              : original;
                    const auto actual = glm::dvec3(
                        world *
                        glm::dvec4(f.model->componentPreview()->source.vertex(vertex.id)->position,
                                   1));
                    REQUIRE(glm::distance(actual, expected) < 1e-5);
                }
                QTest::keyClick(f.viewport, Qt::Key_Return);
                REQUIRE_FALSE(f.modal->isActive());
                REQUIRE(f.model->undoStack()->index() == index + 1);
                f.model->undo();
                REQUIRE(mesh(*f.model, f.cube)->source == before->source);
            }
        }
    }
    // 部分选区的大角度旋转会使未完整选中的面投影交叉；拒绝应保留会话，不能提交。
    f.model->selectComponent({1}, SelectionOperation::Replace);
    f.model->selectComponent({3}, SelectionOperation::Add);
    f.start(Qt::Key_R, "z30");
    REQUIRE(f.modal->statusText().contains(QStringLiteral("不是可三角化的简单环")));
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == index);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(mesh(*f.model, f.cube)->source == before->source);
    f.model->selectAllComponents();
    f.start(Qt::Key_S, "2");
    f.model->setTransformPivot(TransformPivot::Median);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(mesh(*f.model, f.cube)->source == before->source);
    REQUIRE(f.model->undoStack()->index() == index);
    f.start(Qt::Key_R, "z20");
    f.model->setCursorPosition({0, 0, 0});
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(mesh(*f.model, f.cube)->source == before->source);
}

TEST_CASE("Object numeric and real scale handle orbit the cursor with reversible history",
          "[pivot-ui]") {
    PivotWindow f;
    REQUIRE(f.model->setTransformComponent(f.cube, 0, 0, 1));
    f.action("PivotCursor")->trigger();
    const auto index = f.model->undoStack()->index();
    f.start(Qt::Key_R, "z90");
    REQUIRE(glm::distance(f.model->scene()->find(f.cube)->transform.position, glm::vec3(0, 1, 0)) <
            1e-5F);
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.model->undoStack()->index() == index + 1);
    f.model->undo();
    f.start(Qt::Key_S, "2");
    REQUIRE(f.model->scene()->find(f.cube)->transform.position == glm::vec3(2, 0, 0));
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(f.model->scene()->find(f.cube)->transform.position == glm::vec3(1, 0, 0));
    f.action("ScaleTool")->trigger();
    f.action("SnapTransform")->setChecked(false);
    const auto camera = *f.viewport->editorCameraSnapshot();
    const auto project = [&](glm::vec3 point) {
        const auto clip = camera.viewProjectionMatrix() * glm::vec4(point, 1);
        return QPoint(qRound((clip.x / clip.w + 1) * f.viewport->width() * .5),
                      qRound((1 - clip.y / clip.w) * f.viewport->height() * .5));
    };
    const auto units = camera.worldUnitsPerPixel({0, 0, 0});
    const auto start = project({54 * units, 0, 0}), end = project({99 * units, 0, 0});
    const auto image = f.viewport->grabFramebuffer();
    const auto context = f.viewport->context();
    QTest::mousePress(f.viewport, Qt::LeftButton, Qt::NoModifier, start);
    QMouseEvent move(QEvent::MouseMove, end, f.viewport->mapToGlobal(end), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(f.viewport, &move);
    const auto preview = f.model->scene()->find(f.cube)->transform;
    REQUIRE(preview.position.x > 1.4F);
    REQUIRE(std::abs(preview.position.x - preview.scale.x) < 1e-4F);
    REQUIRE(f.viewport->grabFramebuffer() != image);
    QTest::mouseRelease(f.viewport, Qt::LeftButton, Qt::NoModifier, end);
    REQUIRE(f.model->undoStack()->index() == index + 1);
    f.model->undo();
    REQUIRE(f.model->scene()->find(f.cube)->transform.position == glm::vec3(1, 0, 0));
    f.model->redo();
    REQUIRE(f.model->scene()->find(f.cube)->transform.localMatrix() == preview.localMatrix());
    REQUIRE(f.viewport->context() == context);
    f.window.findChild<editor::WorkbenchShell*>()->setSidebarVisible(true);
    f.capture(QStringLiteral("-object"));
}

TEST_CASE("Pivot F3 Q header and selection to cursor share actions without preference history",
          "[pivot-ui]") {
    PivotWindow f;
    const auto index = f.model->undoStack()->index();
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    auto* favorites = f.window.findChild<editor::QuickFavorites*>();
    REQUIRE(favorites->addOperator(QStringLiteral("transform.pivot_active")));
    for (auto key : {Qt::Key_F3, Qt::Key_Q}) {
        f.pointAt();
        QTest::keyClick(f.viewport, key);
        auto* popup = f.window.findChild<editor::OperatorSearchPopup*>();
        REQUIRE(popup->isVisible());
        auto* input = popup->findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
        input->setText(QStringLiteral("Pivot Active Element"));
        QTest::keyClick(input, Qt::Key_Return);
        REQUIRE(f.model->transformPivot() == TransformPivot::Active);
        REQUIRE(f.action("PivotActive")->isChecked());
        REQUIRE(f.window.findChild<QToolButton*>(QStringLiteral("TransformPivotButton"))
                    ->text()
                    .contains(QStringLiteral("活动元素")));
        f.action("PivotMedian")->trigger();
    }
    REQUIRE(f.model->undoStack()->index() == index);
    f.model->setCursorPosition({2, 0, 0});
    const auto context = registry->captureContext(editor::InputArea::Viewport);
    REQUIRE(registry->execute(QStringLiteral("selection.to_cursor"), context));
    REQUIRE(f.model->scene()->find(f.cube)->transform.position == glm::vec3(2, 0, 0));
    REQUIRE(f.model->undoStack()->index() == index + 1);
    REQUIRE(f.model->moveSelectionToCursor());
    REQUIRE(f.model->undoStack()->index() == index + 1);
    f.model->undo();
    REQUIRE(f.model->setEditMode(true));
    f.model->setSelectionDomain(SelectionDomain::Face);
    f.model->selectComponent({1}, SelectionOperation::Replace);
    f.model->selectComponent({3}, SelectionOperation::Add);
    f.action("PivotActive")->trigger();
    f.action("LocalTransformSpace")->trigger();
    REQUIRE(f.model->transformPivot() == TransformPivot::Active);
    REQUIRE(f.window.findChild<QToolButton*>(QStringLiteral("TransformSpaceButton"))->text() ==
            QStringLiteral("局部"));
    f.window.findChild<editor::WorkbenchShell*>()->setSidebarVisible(true);
    f.start(Qt::Key_R, "z20");
    f.capture(QStringLiteral("-components"));
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    const auto stale = registry->captureContext(editor::InputArea::Viewport);
    f.model->clearComponentSelection();
    REQUIRE_FALSE(f.action("SelectionToCursor")->isEnabled());
    REQUIRE_FALSE(registry->execute(QStringLiteral("selection.to_cursor"), stale));
}
