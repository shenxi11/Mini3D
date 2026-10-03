/*
 * 模块名: SnapEditorTests
 * 功能概述: 验证顶点/步进吸附、约束、输入优先级和唯一历史。
 * 对外接口: Catch2 [snap-editor]；依赖关系: Qt Test、真实编辑器/GL。
 * 输入输出: 合成输入到世界位置、预览/历史及目标覆盖层。
 * 异常与错误: 不符即失败；维护说明: 窗口串行，配对释放鼠标，不写用户工程。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
struct SnapWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::ObjectTransformSession* modal = window.findChild<editor::ObjectTransformSession*>();
    core::EntityId source = 0, target = 0;
    const glm::vec3 targetPoint{2.5F, .5F, .5F};
    SnapWindow() {
        window.resize(1440, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        window.findChild<editor::KeymapRouter*>()->setKeymap(editor::EditorKeymap::Blender);
        model->newScene();
        target = model->createEntity(core::PrimitiveKind::Cube);
        core::Transform transform;
        transform.position = {2, 0, 0};
        REQUIRE(model->setTransform(target, transform));
        source = model->createEntity(core::PrimitiveKind::Cube);
        transform.position = {-2, 0, 0};
        REQUIRE(model->setTransform(source, transform));
        viewport->setEditorCamera({{5, 4, 9}, {0, 0, 0}, 0, 50});
        viewport->setFocus();
        QTest::qWait(30);
        action("SnapVertex")->trigger();
        action("SnapTransform")->setChecked(true);
    }
    ~SnapWindow() {
        window.hide();
    }
    QAction* action(const char* name) {
        auto* found = window.findChild<QAction*>(QString::fromLatin1(name));
        REQUIRE(found);
        return found;
    }
    QPointF screen(glm::vec3 point) {
        const auto clip =
            viewport->editorCameraSnapshot()->viewProjectionMatrix() * glm::vec4(point, 1);
        return {(clip.x / clip.w + 1) * viewport->width() / 2,
                (1 - clip.y / clip.w) * viewport->height() / 2};
    }
    void move(QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, modifiers);
        QApplication::sendEvent(viewport, &event);
    }
    void start() {
        viewport->setFocus();
        move(screen({-2, 0, 0}));
        QTest::keyClick(viewport, Qt::Key_G);
        REQUIRE(modal->isActive());
    }
    glm::vec3 origin() {
        return glm::vec3(model->scene()->worldMatrix(source)[3]);
    }
};
} // namespace

TEST_CASE("Vertex snapping stays stable clears feedback and uses one undoable saved move",
          "[snap-editor]") {
    SnapWindow f;
    const auto before = f.origin();
    const auto history = f.model->undoStack()->index();
    f.start();
    for (int i = 0; i < 3; ++i) {
        f.move(f.screen(f.targetPoint));
        REQUIRE(glm::distance(f.origin(), f.targetPoint) < 1e-5F);
        REQUIRE(f.viewport->snapTarget() == f.targetPoint);
        REQUIRE(f.model->undoStack()->index() == history);
    }
    const auto* context = f.viewport->context();
    const auto capture = qEnvironmentVariable("MINI3D_TEST_SNAP_CAPTURE");
    if (!capture.isEmpty())
        REQUIRE(f.window.grab().save(capture + QStringLiteral("-object.png")));
    const auto marked = f.viewport->grabFramebuffer();
    f.viewport->setSnapTarget(std::nullopt);
    const auto unmarked = f.viewport->grabFramebuffer();
    REQUIRE_FALSE(marked.isNull());
    REQUIRE(marked != unmarked);
    f.move(f.screen(f.targetPoint));
    QTest::keyPress(f.viewport, Qt::Key_Control);
    REQUIRE_FALSE(f.viewport->snapTarget());
    REQUIRE(glm::distance(f.origin(), f.targetPoint) > .01F);
    REQUIRE(f.action("SnapTransform")->isChecked());
    QTest::keyRelease(f.viewport, Qt::Key_Control);
    REQUIRE(f.viewport->snapTarget());
    f.move(f.screen(f.targetPoint) + QPointF(30, 0));
    REQUIRE_FALSE(f.viewport->snapTarget());
    REQUIRE(glm::distance(f.origin(), f.targetPoint) > .01F);
    f.move(f.screen(f.targetPoint));
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE_FALSE(f.viewport->snapTarget());
    REQUIRE(f.model->undoStack()->index() == history + 1);
    f.model->undo();
    REQUIRE(f.origin() == before);
    f.model->redo();
    REQUIRE(glm::distance(f.origin(), f.targetPoint) < 1e-5F);
    REQUIRE(f.viewport->context() == context);
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath("snap.mini3d");
    REQUIRE(f.model->saveScene(path));
    editor::SceneViewModel loaded;
    REQUIRE(loaded.openScene(path));
    REQUIRE(glm::distance(glm::vec3(loaded.scene()->worldMatrix(f.source)[3]), f.targetPoint) <
            1e-5F);
    REQUIRE(loaded.snapMode() == editor::SnapMode::Increment);
}

TEST_CASE("Snap obeys numeric priority local axis plane and Ctrl temporary inversion",
          "[snap-editor]") {
    SnapWindow f;
    const auto before = f.origin();
    const auto history = f.model->undoStack()->index();
    f.action("SnapTransform")->setChecked(false);
    f.start();
    f.move(f.screen(f.targetPoint));
    REQUIRE_FALSE(f.viewport->snapTarget());
    QTest::keyPress(f.viewport, Qt::Key_Control);
    REQUIRE(f.viewport->snapTarget());
    QTest::keyRelease(f.viewport, Qt::Key_Control);
    REQUIRE_FALSE(f.viewport->snapTarget());
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(f.origin() == before);
    f.action("SnapTransform")->setChecked(true);
    for (bool plane : {false, true}) {
        f.start();
        QTest::keyClick(f.viewport, Qt::Key_X, plane ? Qt::ShiftModifier : Qt::NoModifier);
        f.move(f.screen(f.targetPoint));
        const auto expected = plane ? glm::vec3(before.x, f.targetPoint.y, f.targetPoint.z)
                                    : glm::vec3(f.targetPoint.x, before.y, before.z);
        REQUIRE(glm::distance(f.origin(), expected) < 1e-5F);
        QTest::keyClick(f.viewport, Qt::Key_Escape);
    }
    f.start();
    QTest::keyClicks(f.viewport, "x1.23");
    f.move(f.screen(f.targetPoint));
    REQUIRE_FALSE(f.viewport->snapTarget());
    REQUIRE(glm::distance(f.origin(), before + glm::vec3(1.23F, 0, 0)) < 1e-5F);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    core::Transform transform = f.model->scene()->find(f.source)->transform;
    transform.rotation = glm::quat(glm::radians(glm::vec3(0, 0, 40)));
    REQUIRE(f.model->setTransform(f.source, transform));
    f.action("LocalTransformSpace")->trigger();
    f.start();
    QTest::keyClick(f.viewport, Qt::Key_X);
    f.move(f.screen(f.targetPoint));
    const auto axis = glm::mat3_cast(transform.rotation)[0];
    REQUIRE(glm::distance(f.origin(), before + axis * glm::dot(f.targetPoint - before, axis)) <
            1e-5F);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(f.model->undoStack()->index() == history + 1);
    f.action("SnapIncrement")->trigger();
    f.start();
    f.move(f.screen(f.targetPoint));
    const auto localDelta =
        glm::transpose(glm::mat3_cast(transform.rotation)) * (f.origin() - before);
    for (int i = 0; i < 3; ++i)
        REQUIRE(std::abs(localDelta[i] / .5F - std::round(localDelta[i] / .5F)) < 1e-5F);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
}

TEST_CASE("Component snap translates the frozen centroid and changing mode cancels previews",
          "[snap-editor]") {
    SnapWindow f;
    REQUIRE(f.model->setEditMode(true));
    f.model->selectAllComponents();
    const auto content =
        f.model->scene()->editableMesh(f.model->scene()->find(f.source)->editableMesh)->content;
    const auto selection = f.model->componentSelection();
    const auto before = *f.model->selectedComponentCenter();
    const auto history = f.model->undoStack()->index();
    f.start();
    f.move(f.screen(f.targetPoint));
    REQUIRE(f.viewport->snapTarget());
    REQUIRE(f.model->componentPreview());
    const auto preview = f.model->componentPreview();
    const auto world = glm::dmat4(f.model->scene()->worldMatrix(f.source));
    const auto delta = glm::dvec3(f.targetPoint) - before;
    for (const auto& vertex : content->source.vertices) {
        const auto actual = preview->source.vertex(vertex.id)->position;
        REQUIRE(glm::distance(glm::dvec3(world * glm::dvec4(actual - vertex.position, 0)), delta) <
                1e-5);
    }
    f.action("SnapIncrement")->trigger();
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE_FALSE(f.viewport->snapTarget());
    REQUIRE_FALSE(f.model->componentPreview());
    REQUIRE(f.model->undoStack()->index() == history);
    REQUIRE(f.model->componentSelection() == selection);
    f.action("SnapVertex")->trigger();
    f.start();
    f.move(f.screen(f.targetPoint));
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.model->undoStack()->index() == history + 1);
    REQUIRE(glm::distance(*f.model->selectedComponentCenter(), glm::dvec3(f.targetPoint)) < 1e-5);
    f.model->undo();
    REQUIRE(*f.model->selectedComponentCenter() == before);
    f.model->redo();
    REQUIRE(glm::distance(*f.model->selectedComponentCenter(), glm::dvec3(f.targetPoint)) < 1e-5);
}

TEST_CASE("Snap registry and header share non dirty window preferences", "[snap-editor]") {
    SnapWindow f;
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    const auto history = f.model->undoStack()->index();
    auto* button = f.window.findChild<QToolButton*>("SnapButton");
    REQUIRE(button);
    for (const auto* id :
         {"transform.snap_increment", "transform.snap_vertex", "transform.snap_toggle"}) {
        const auto context = registry->captureContext(editor::InputArea::Viewport);
        REQUIRE(registry->execute(QString::fromLatin1(id), context));
        REQUIRE_FALSE(registry->descriptor(QString::fromLatin1(id))->undoable);
    }
    REQUIRE(button->text().contains(QStringLiteral("顶点")));
    REQUIRE_FALSE(button->isChecked());
    REQUIRE(f.model->undoStack()->index() == history);
    f.model->newScene();
    REQUIRE(f.model->snapMode() == editor::SnapMode::Vertex);
    REQUIRE_FALSE(f.model->isModified());
    f.action("SnapIncrement")->trigger();
    f.action("SnapTransform")->trigger();
    REQUIRE_FALSE(f.model->isModified());
}

TEST_CASE("Vertex snap respects parent transforms and facing axis and cancels on focus loss",
          "[snap-editor]") {
    SnapWindow f;
    const auto parent = f.model->createEntity(core::PrimitiveKind::Empty);
    core::Transform transform;
    transform.position = {-1, .2F, 0};
    transform.rotation = glm::quat(glm::radians(glm::vec3(0, 0, 20)));
    transform.scale = {-1.5F, .8F, 2};
    REQUIRE(f.model->setTransform(parent, transform));
    REQUIRE(f.model->setParent(f.source, parent));
    f.model->selection()->setSelectedEntity(f.source);
    const auto before = f.model->scene()->find(f.source)->transform;
    const auto origin = f.origin();
    const auto history = f.model->undoStack()->index();
    f.start();
    f.move(f.screen(f.targetPoint));
    REQUIRE(glm::distance(f.origin(), f.targetPoint) < 1e-5F);
    REQUIRE(f.model->scene()->find(f.source)->transform.rotation == before.rotation);
    REQUIRE(f.model->scene()->find(f.source)->transform.scale == before.scale);
    QEvent lostFocus(QEvent::FocusOut);
    QApplication::sendEvent(f.viewport, &lostFocus);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE_FALSE(f.viewport->snapTarget());
    REQUIRE(f.model->scene()->find(f.source)->transform.localMatrix() == before.localMatrix());
    f.viewport->setCameraView(renderer_gl::EditorView::Front);
    f.start();
    QTest::keyClick(f.viewport, Qt::Key_Z);
    f.move(f.screen(f.targetPoint));
    REQUIRE(f.viewport->snapTarget());
    REQUIRE(glm::distance(f.origin(), glm::vec3(origin.x, origin.y, f.targetPoint.z)) < 1e-5F);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE(f.model->undoStack()->index() == history);
}

TEST_CASE("Same mesh snap rejects collapsed candidate without committing partial geometry",
          "[snap-editor]") {
    SnapWindow f;
    REQUIRE(f.model->setEditMode(true));
    f.model->setSelectionDomain(editor::SelectionDomain::Vertex);
    f.model->selectComponent({7}, editor::SelectionOperation::Replace);
    const auto content =
        f.model->scene()->editableMesh(f.model->scene()->find(f.source)->editableMesh)->content;
    const auto world = f.model->scene()->worldMatrix(f.source);
    const auto target = glm::vec3(world * glm::vec4(content->source.vertex(8)->position, 1));
    const auto history = f.model->undoStack()->index();
    f.start();
    f.move(f.screen(target));
    REQUIRE(f.viewport->snapTarget());
    QTest::keyClick(f.viewport, Qt::Key_Return);
    REQUIRE(f.modal->isActive());
    REQUIRE(f.model->undoStack()->index() == history);
    REQUIRE(
        f.model->scene()->editableMesh(f.model->scene()->find(f.source)->editableMesh)->content ==
        content);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE_FALSE(f.viewport->snapTarget());
    REQUIRE_FALSE(f.model->componentPreview());
}

TEST_CASE("Move handle Ctrl uses XOR with the persistent snap preference", "[snap-editor]") {
    SnapWindow f;
    f.action("MoveTool")->trigger();
    const auto before = f.origin();
    const auto units = f.viewport->editorCameraSnapshot()->worldUnitsPerPixel(before);
    const auto start = f.screen(before + glm::vec3(units * 54, 0, 0)).toPoint();
    const auto end = f.screen(before + glm::vec3(units * 87, 0, 0)).toPoint();
    glm::vec3 outcomes[4];
    for (int enabled = 0; enabled < 2; ++enabled) {
        for (int control = 0; control < 2; ++control) {
            f.action("SnapTransform")->setChecked(enabled);
            QTest::mousePress(f.viewport, Qt::LeftButton, Qt::NoModifier, start);
            const auto modifiers = control ? Qt::ControlModifier : Qt::NoModifier;
            QMouseEvent move(QEvent::MouseMove, end, f.viewport->mapToGlobal(end), Qt::NoButton,
                             Qt::LeftButton, modifiers);
            QApplication::sendEvent(f.viewport, &move);
            const auto preview = f.origin();
            QTest::mouseRelease(f.viewport, Qt::LeftButton, modifiers, end);
            REQUIRE(f.origin() == preview);
            outcomes[enabled * 2 + control] = f.origin();
            f.model->undo();
            REQUIRE(f.origin() == before);
        }
    }
    REQUIRE(glm::distance(outcomes[0], outcomes[3]) < 1e-6F);
    REQUIRE(glm::distance(outcomes[1], outcomes[2]) < 1e-6F);
    REQUIRE(glm::distance(outcomes[0], outcomes[1]) > .001F);
    REQUIRE(std::abs((outcomes[1].x - before.x) / .5F -
                     std::round((outcomes[1].x - before.x) / .5F)) < 1e-5F);
}
