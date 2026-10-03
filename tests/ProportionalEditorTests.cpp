/*
 * 模块名: ProportionalEditorTests
 * 功能概述: 验证比例编辑候选/取消/历史、Mirror夹持及O/滚轮的真实影响圈。
 * 对外接口: Catch2 [proportional-editor]。
 * 依赖关系: SceneViewModel、ObjectTransformSession、真实Qt/OpenGL视口。
 * 输入输出: 固定before与比例输入到实际顶点、历史和画面。
 * 异常与错误: 无效半径/候选不写源，取消恢复所有受影响点。
 * 维护说明: 合成键鼠与单屏测试不替代真实IME/跨屏验收。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
#include "editor/operations/ObjectTransformSession.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <limits>
#include <memory>

using namespace mini3d;
namespace {
core::modeling::EditableMesh proportionalQuad() {
    core::modeling::EditableMesh mesh;
    mesh.vertices = {{1, {0, 0, 0}}, {2, {1, 0, 0}}, {3, {1, 1, 0}}, {4, {0, 1, 0}}};
    core::modeling::EditableFace face;
    face.id = 1;
    for (std::uint64_t id = 1; id <= 4; ++id)
        face.corners.push_back({id, id});
    mesh.faces.push_back(std::move(face));
    return mesh;
}
const core::modeling::EditableMesh& confirmed(const editor::SceneViewModel& model,
                                              core::EntityId id) {
    return model.scene()->editableMesh(model.scene()->find(id)->editableMesh)->content->source;
}
} // namespace

TEST_CASE(
    "Proportional preferences previews cancel and commit preserve before and a single history",
    "[proportional-editor]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(cube));
    REQUIRE(model.replaceEditableMesh(cube, proportionalQuad()));
    REQUIRE(model.saveScene(directory.filePath("before.m3dscene")));
    REQUIRE(model.setEditMode(true));
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    const auto before = confirmed(model, cube);
    const auto selection = model.componentSelection();
    const auto count = model.undoStack()->count();
    model.setProportionalEditingEnabled(true);
    model.setProportionalConnected(false);
    REQUIRE(model.setProportionalRadius(2));
    REQUIRE_FALSE(model.isModified());
    REQUIRE_FALSE(model.setProportionalRadius(0));
    REQUIRE_FALSE(model.setProportionalRadius(std::numeric_limits<double>::infinity()));
    REQUIRE(model.proportionalRadius() == 2);
    const auto delta = glm::translate(glm::dmat4(1), glm::dvec3(.2, 0, 0));
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(delta));
    REQUIRE(model.componentPreview()->source.vertex(1)->position.x == Catch::Approx(.2));
    REQUIRE(model.componentPreview()->source.vertex(2)->position.x == Catch::Approx(1.1));
    REQUIRE(confirmed(model, cube) == before);
    REQUIRE(model.setProportionalRadius(1));
    REQUIRE(model.previewComponentTransform(delta));
    REQUIRE(model.componentPreview()->source.vertex(2)->position.x == 1);
    REQUIRE(model.setProportionalRadius(2));
    REQUIRE(model.previewComponentTransform(delta));
    REQUIRE(model.componentPreview()->source.vertex(2)->position.x == Catch::Approx(1.1));
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE(confirmed(model, cube) == before);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(delta));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE(confirmed(model, cube).vertex(2)->position.x == Catch::Approx(1.1));
    model.undo();
    REQUIRE(confirmed(model, cube) == before);
    REQUIRE_FALSE(model.isModified());
}

TEST_CASE("Proportional influence outside the drivers obeys Mirror clipping without source leakage",
          "[proportional-editor][mirror02-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(cube));
    REQUIRE(model.replaceEditableMesh(cube, proportionalQuad()));
    REQUIRE(model.setMirrorOptions(cube, core::modeling::MirrorOptions{}));
    REQUIRE(model.setEditMode(true));
    model.selectComponent({2}, editor::SelectionOperation::Replace);
    model.setProportionalEditingEnabled(true);
    REQUIRE(model.setProportionalRadius(3));
    const auto before = confirmed(model, cube);
    const auto count = model.undoStack()->count();
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1), glm::dvec3(.2, 0, 0))));
    const auto& preview = model.componentPreview()->source;
    REQUIRE(preview.vertex(1)->position.x == 0);
    REQUIRE(preview.vertex(4)->position.x == 0);
    REQUIRE(preview.vertex(2)->position.x == Catch::Approx(1.2));
    REQUIRE(preview.vertex(3)->position.x > 1);
    REQUIRE(confirmed(model, cube) == before);
    REQUIRE_FALSE(model.previewComponentTransform(glm::scale(glm::dmat4(1), glm::dvec3(0))));
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE(confirmed(model, cube) == before);
    REQUIRE(model.undoStack()->count() == count);
}

TEST_CASE(
    "O and wheel recompute proportional GRS with a projected world influence ring and safe Esc",
    "[proportional-editor][proportional-ui]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* modal = window.findChild<editor::ObjectTransformSession*>();
    auto* router = window.findChild<editor::KeymapRouter*>();
    router->setKeymap(editor::EditorKeymap::Blender);
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->setEditMode(true));
    model->selectComponent({1}, editor::SelectionOperation::Replace);
    viewport->setEditorCamera({{0, 0, 5}, {0, 0, 0}, 0, 50});
    REQUIRE(model->setProportionalRadius(2));
    const auto before = confirmed(*model, cube);
    const auto count = model->undoStack()->count();
    const QPoint point(viewport->width() * 3 / 4, viewport->height() / 2);
    viewport->setFocus();
    QMouseEvent move(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &move);
    QTest::keyClick(viewport, Qt::Key_O);
    REQUIRE(model->isProportionalEditingEnabled());
    QTest::keyClick(viewport, Qt::Key_G);
    REQUIRE(modal->isActive());
    REQUIRE(viewport->proportionalInfluenceCenters().size() == 1);
    REQUIRE(viewport->proportionalInfluenceRadius() == 2);
    const auto withCircle = viewport->grabFramebuffer();
    const auto withWidgetCircle = viewport->grab().toImage();
    QTest::keyClick(viewport, Qt::Key_O);
    REQUIRE_FALSE(model->isProportionalEditingEnabled());
    REQUIRE(viewport->proportionalInfluenceCenters().empty());
    const auto withoutCircle = viewport->grabFramebuffer();
    const auto withoutWidgetCircle = viewport->grab().toImage();
    INFO("viewport=" << viewport->width() << 'x' << viewport->height()
                     << ", overlay=" << viewport->isOverlayVisible()
                     << ", frame=" << withCircle.width() << 'x' << withCircle.height()
                     << ", widgetChanged=" << (withWidgetCircle != withoutWidgetCircle));
    const auto capture = qEnvironmentVariable("MINI3D_TEST_PROP_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(withCircle.save(QDir(capture).filePath("with-circle-frame.png")));
        REQUIRE(withoutCircle.save(QDir(capture).filePath("without-circle-frame.png")));
        REQUIRE(withWidgetCircle.save(QDir(capture).filePath("with-circle-widget.png")));
        REQUIRE(withoutWidgetCircle.save(QDir(capture).filePath("without-circle-widget.png")));
    }
    REQUIRE(withoutCircle != withCircle);
    QTest::keyClick(viewport, Qt::Key_O);
    QTest::keyClick(viewport, Qt::Key_X);
    QTest::keyClicks(viewport, "0.2");
    const auto oldPreview = model->componentPreview()->source;
    const auto driver = oldPreview.vertex(1)->position;
    QWheelEvent wheel(point, viewport->mapToGlobal(point), {}, {0, -120}, Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(viewport, &wheel);
    REQUIRE(modal->isActive());
    REQUIRE(model->proportionalRadius() == Catch::Approx(2.4));
    REQUIRE(viewport->proportionalInfluenceRadius() == Catch::Approx(2.4));
    REQUIRE(model->componentPreview()->source.vertex(1)->position == driver);
    REQUIRE(model->componentPreview()->source != oldPreview);
    REQUIRE(confirmed(*model, cube) == before);
    REQUIRE(model->undoStack()->count() == count);
    QTest::keyClick(viewport, Qt::Key_Escape);
    REQUIRE_FALSE(modal->isActive());
    REQUIRE(viewport->proportionalInfluenceCenters().empty());
    REQUIRE(confirmed(*model, cube) == before);
    REQUIRE(model->undoStack()->count() == count);
    window.hide();
}

TEST_CASE("Destroying a window with an active proportional transform cancels before child teardown",
          "[proportional-editor][proportional-ui][modal-lifecycle]") {
    auto window = std::make_unique<editor::MainWindow>();
    window->show();
    window->activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(window.get()));
    auto* model = window->findChild<editor::SceneViewModel*>();
    auto* viewport = window->findChild<renderer_gl::ViewportWidget*>();
    auto* modal = window->findChild<editor::ObjectTransformSession*>();
    model->newScene();
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->setEditMode(true));
    model->selectComponent({1}, editor::SelectionOperation::Replace);
    model->setProportionalEditingEnabled(true);
    REQUIRE(model->setProportionalRadius(2));
    const auto before = confirmed(*model, cube);
    const auto count = model->undoStack()->count();
    viewport->setFocus();
    REQUIRE(modal->start(editor::TransformOperation::Move, editor::TransformTarget::Components));
    REQUIRE(model->previewComponentTransform(glm::translate(glm::dmat4(1), glm::dvec3(.2, 0, 0))));
    REQUIRE(model->componentPreview()->source != before);
    REQUIRE(modal->isActive());
    REQUIRE(model->hasComponentTransform());
    QSignalSpy finished(model, &editor::SceneViewModel::componentTransformFinished);
    QSignalSpy active(modal, &editor::ObjectTransformSession::activeChanged);
    bool restored = false;
    QObject::connect(model, &editor::SceneViewModel::componentTransformFinished, window.get(), [&] {
        restored = !model->hasComponentTransform() && confirmed(*model, cube) == before &&
                   model->undoStack()->count() == count && !modal->isActive() &&
                   viewport->proportionalInfluenceCenters().empty();
    });
    // 不先 hide、不发 Esc，直接覆盖异常展开与窗口退出时的真实析构路径。
    window.reset();
    REQUIRE(finished.count() == 1);
    REQUIRE(active.count() == 1);
    REQUIRE_FALSE(active.front().front().toBool());
    REQUIRE(restored);
    REQUIRE(QWidget::mouseGrabber() == nullptr);
}
