/*
 * 模块名: InsetFaceEditorTests
 * 功能概述: 验证内插独立候选、稳定选择、单一历史、F9和保存的真实业务联动。
 * 对外接口: Catch2 [inset-editor]/[inset-ui]；依赖关系: Qt Test、真实窗口/GL、临时文件。
 * 输入输出: 局部厚度与编辑上下文到网格、保存点和历史断言。
 * 异常与错误: 失败与取消不写真源，错类型参数不调用另一算子。
 * 维护说明: 不修改用户工程或偏好，文件仅写QTemporaryDir。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/ObjectTransformSession.h"
#include "editor/operations/OperatorRegistry.h"
#include "editor/operations/QuickFavorites.h"
#include "editor/workbench/CommitSpinBox.h"
#include "editor/workbench/LastOperationPanel.h"
#include "editor/workbench/OperatorSearchPopup.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <catch2/catch_test_macros.hpp>
#include <limits>

using namespace mini3d;
namespace {
core::EntityId enterInset(editor::SceneViewModel& model) {
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    return entity;
}
const core::EditableMeshRecord& meshRecord(const editor::SceneViewModel& model,
                                           core::EntityId entity) {
    return *model.scene()->editableMesh(model.scene()->find(entity)->editableMesh);
}
struct InsetWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::ObjectTransformSession* modal = window.findChild<editor::ObjectTransformSession*>();
    editor::LastOperationPanel* panel = window.findChild<editor::LastOperationPanel*>();
    editor::KeymapRouter* router = window.findChild<editor::KeymapRouter*>();
    core::EntityId entity;
    InsetWindow() {
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        entity = enterInset(*model);
        router->setKeymap(editor::EditorKeymap::Blender);
        viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
        QTest::qWait(30);
        pointAtViewport();
    }
    ~InsetWindow() {
        window.hide();
    }
    void pointAtViewport() {
        const QPoint point(viewport->width() * 2 / 3, viewport->height() / 2);
        QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
        viewport->setFocus();
    }
    void begin(const char* value = ".2") {
        pointAtViewport();
        QTest::keyClick(viewport, Qt::Key_I);
        REQUIRE(modal->isActive());
        REQUIRE(model->componentInset());
        QTest::keyClicks(viewport, value);
    }
    void confirm() {
        QTest::keyClick(viewport, Qt::Key_Return);
        REQUIRE_FALSE(modal->isActive());
    }
    editor::CommitSpinBox* thickness() {
        return panel->findChild<editor::CommitSpinBox*>(
            QStringLiteral("LastOperationInsetThickness"));
    }
    void openParameters() {
        pointAtViewport();
        QTest::keyClick(viewport, Qt::Key_F9);
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        REQUIRE(thickness()->isVisible());
        REQUIRE(thickness()->hasFocus());
    }
    void typeThickness(const char* value, bool commit = true) {
        thickness()->setFocus();
        thickness()->selectAll();
        QTest::keyClicks(thickness(), value);
        if (commit)
            QTest::keyClick(thickness()->findChild<QLineEdit*>(), Qt::Key_Return);
    }
};
} // namespace

TEST_CASE("Inset preview cancel commit adjustment and save retain one original before",
          "[inset-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterInset(model);
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("内插事务.m3dscene"));
    REQUIRE(model.saveScene(path));
    const auto before = meshRecord(model, entity);
    const auto selection = model.componentSelection();
    const auto count = model.undoStack()->count();
    REQUIRE(model.beginInsetFace());
    REQUIRE(model.componentInset());
    REQUIRE_FALSE(model.componentExtrusion());
    REQUIRE(model.previewInsetFace(.1));
    REQUIRE(model.previewInsetFace(.2));
    REQUIRE(model.componentPreview()->source.faces.size() == 10);
    REQUIRE(meshRecord(model, entity).content == before.content);
    REQUIRE(meshRecord(model, entity).evaluationRevision == before.evaluationRevision);
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE(meshRecord(model, entity).content == before.content);
    REQUIRE(model.componentSelection() == selection);
    REQUIRE(model.beginInsetFace());
    REQUIRE(model.previewInsetFace(.2));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE(model.lastOperationInsetThickness() == .2);
    REQUIRE_FALSE(model.lastOperationWorldOffset());
    REQUIRE(model.saveScene(path));
    const auto* command = model.undoStack()->command(count);
    for (double thickness : {.1, .25, .3}) {
        REQUIRE(model.adjustLastInset(thickness));
        REQUIRE(model.lastOperationInsetThickness() == thickness);
        const auto expected = core::modeling::insetFace(before.content->source, 1, thickness);
        REQUIRE(expected.mesh);
        REQUIRE(meshRecord(model, entity).content->source == *expected.mesh);
        REQUIRE(model.undoStack()->command(count) == command);
        REQUIRE(model.undoStack()->count() == count + 1);
        REQUIRE(model.componentSelection() == selection);
        REQUIRE(model.isModified());
    }
    editor::SceneViewModel oldDisk;
    REQUIRE(oldDisk.openScene(path));
    REQUIRE(oldDisk.scene()
                ->editableMesh(oldDisk.scene()->find(entity)->editableMesh)
                ->content->source ==
            *core::modeling::insetFace(before.content->source, 1, .2).mesh);
    const auto after = meshRecord(model, entity);
    model.undo();
    REQUIRE(meshRecord(model, entity).content == before.content);
    REQUIRE_FALSE(model.lastOperationInsetThickness());
    model.redo();
    REQUIRE(meshRecord(model, entity).content == after.content);
    REQUIRE(meshRecord(model, entity).evaluationRevision > after.evaluationRevision);
    REQUIRE(model.saveScene(path));
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(meshRecord(reopened, entity).content->source == after.content->source);
    REQUIRE_FALSE(reopened.lastOperationInsetThickness());
}

TEST_CASE("Inset rejects unsupported selection and preserves candidates after invalid thickness",
          "[inset-editor]") {
    editor::SceneViewModel model;
    const auto entity = enterInset(model);
    const auto before = meshRecord(model, entity);
    model.selectComponent({2}, editor::SelectionOperation::Add);
    REQUIRE_FALSE(model.insetFaceDisabledReason().isEmpty());
    REQUIRE_FALSE(model.beginInsetFace());
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    REQUIRE(model.beginInsetFace());
    REQUIRE_FALSE(model.finishComponentTransform(true));
    REQUIRE(model.previewInsetFace(.2));
    const auto preview = model.componentPreview();
    for (double thickness : {0., -.1, .5, std::numeric_limits<double>::quiet_NaN()}) {
        REQUIRE_FALSE(model.previewInsetFace(thickness));
        REQUIRE(model.componentPreview() == preview);
        REQUIRE_FALSE(model.finishComponentTransform(true));
        REQUIRE(meshRecord(model, entity).content == before.content);
    }
    REQUIRE_FALSE(model.previewComponentTransform(glm::dmat4(1)));
    REQUIRE(model.previewInsetFace(.1));
    REQUIRE(model.finishComponentTransform(true));
    QTemporaryDir directory;
    REQUIRE(model.saveScene(directory.filePath(QStringLiteral("有效内插.m3dscene"))));
    const auto after = meshRecord(model, entity);
    REQUIRE(model.adjustLastInset(.1));
    REQUIRE_FALSE(model.isModified());
    REQUIRE_FALSE(model.adjustLastInset(.5));
    REQUIRE_FALSE(model.adjustLastOperation({0, 0, .25}));
    REQUIRE(meshRecord(model, entity).content == after.content);
    REQUIRE(meshRecord(model, entity).evaluationRevision == after.evaluationRevision);
    REQUIRE(model.lastOperationInsetThickness() == .1);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(model.beginExtrudeRegion());
    REQUIRE_FALSE(model.previewInsetFace(.1));
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE(model.setEditMode(false));
    REQUIRE_FALSE(model.beginInsetFace());
}

TEST_CASE("I preview changes the GPU frame cancels exactly then F9 replaces the same inset",
          "[inset-ui]") {
    InsetWindow f;
    const auto before = meshRecord(*f.model, f.entity);
    const auto context = f.viewport->context();
    const auto frame = f.viewport->grabFramebuffer();
    const auto count = f.model->undoStack()->count();
    f.begin();
    REQUIRE(f.modal->statusText().contains(QStringLiteral("对象局部单位")));
    REQUIRE(f.model->componentPreview()->source.faces.size() == 10);
    REQUIRE(meshRecord(*f.model, f.entity).content == before.content);
    REQUIRE(f.viewport->grabFramebuffer() != frame);
    QTest::keyClick(f.viewport, Qt::Key_Escape);
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(f.viewport->grabFramebuffer() == frame);
    REQUIRE(f.model->undoStack()->count() == count);
    f.begin();
    f.confirm();
    REQUIRE(f.model->undoStack()->count() == count + 1);
    f.openParameters();
    REQUIRE_FALSE(f.panel->findChild<editor::CommitSpinBox*>(QStringLiteral("LastOperationOffsetZ"))
                      ->isVisible());
    f.typeThickness(".3");
    REQUIRE(f.model->lastOperationInsetThickness() == .3);
    REQUIRE(f.model->undoStack()->count() == count + 1);
    REQUIRE(meshRecord(*f.model, f.entity).content->source ==
            *core::modeling::insetFace(before.content->source, 1, .3).mesh);
    const auto adjusted = meshRecord(*f.model, f.entity).content;
    f.typeThickness(".5");
    REQUIRE_FALSE(f.thickness()->validationMessage().isEmpty());
    REQUIRE(f.thickness()->cleanText() == QStringLiteral(".5"));
    REQUIRE(meshRecord(*f.model, f.entity).content == adjusted);
    QTest::keyClick(f.thickness(), Qt::Key_Escape);
    f.typeThickness(".4", false);
    QTest::mouseClick(f.panel->findChild<QToolButton*>(QStringLiteral("LastOperationToggle")),
                      Qt::LeftButton);
    REQUIRE(meshRecord(*f.model, f.entity).content == adjusted);
    f.openParameters();
    REQUIRE(f.thickness()->value() == .3);
    REQUIRE(f.viewport->context() == context);
    REQUIRE(f.viewport->rect().contains(f.panel->geometry()));
    for (auto* label : f.panel->findChildren<QLabel*>()) {
        if (label->isVisible() && label->hasHeightForWidth())
            REQUIRE(label->height() >= label->heightForWidth(label->width()));
    }
    const auto capture = qEnvironmentVariable("MINI3D_TEST_INSET_CAPTURE");
    if (!capture.isEmpty())
        REQUIRE(f.window.grab().save(capture));
}

TEST_CASE("Inset F3 Q and Legacy menu share the same operation and reject stale context",
          "[inset-ui]") {
    InsetWindow f;
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    const auto* descriptor = registry->descriptor(QStringLiteral("mesh.inset_face"));
    REQUIRE(descriptor);
    REQUIRE(descriptor->reopenable);
    REQUIRE(descriptor->parameterSchema.value(QStringLiteral("thickness"))
                .toMap()
                .value(QStringLiteral("space")) == QStringLiteral("local"));
    auto* favorites = f.window.findChild<editor::QuickFavorites*>();
    REQUIRE(favorites->addOperator(QStringLiteral("mesh.inset_face")));
    auto* popup = f.window.findChild<editor::OperatorSearchPopup*>();
    auto* query = f.window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* results = f.window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"));
    REQUIRE(query);
    REQUIRE(results);
    for (const auto key : {Qt::Key_F3, Qt::Key_Q}) {
        f.pointAtViewport();
        QTest::keyClick(f.viewport, key);
        REQUIRE(popup->isVisible());
        if (key == Qt::Key_F3) {
            query->setText(QStringLiteral("inset"));
            REQUIRE(results->count() == 1);
            QTest::keyClick(query, Qt::Key_Return);
        } else {
            REQUIRE(results->count() == 1);
            QTest::keyClick(results, Qt::Key_Return);
        }
        REQUIRE(f.modal->isActive());
        QTest::keyClicks(f.viewport, ".1");
        QTest::keyClick(f.viewport, Qt::Key_Escape);
        REQUIRE_FALSE(f.modal->isActive());
    }
    const auto stale = registry->captureContext(editor::InputArea::Viewport);
    f.model->selectComponent({2}, editor::SelectionOperation::Replace);
    REQUIRE_FALSE(registry->execute(QStringLiteral("mesh.inset_face"), stale));
    f.model->selectComponent({1}, editor::SelectionOperation::Replace);
    f.router->setKeymap(editor::EditorKeymap::Legacy);
    f.pointAtViewport();
    QTest::keyClick(f.viewport, Qt::Key_I);
    REQUIRE_FALSE(f.modal->isActive());
    descriptor->action->trigger();
    REQUIRE(f.model->componentInset());
    QTest::keyClicks(f.viewport, ".1");
    f.confirm();
    REQUIRE(f.model->lastOperationInsetThickness() == .1);
}

TEST_CASE("Inset save then F9 keeps unsaved protection and switches safely to extrusion parameters",
          "[inset-ui]") {
    InsetWindow f;
    f.begin(".1");
    f.confirm();
    QTemporaryDir directory;
    REQUIRE(f.model->saveScene(directory.filePath(QStringLiteral("内插后调参.m3dscene"))));
    f.openParameters();
    f.typeThickness(".2");
    REQUIRE(f.model->isModified());
    bool prompted = false;
    QTimer::singleShot(0, &f.window, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            prompted = true;
            box->button(QMessageBox::Cancel)->click();
        }
    });
    REQUIRE_FALSE(f.window.close());
    REQUIRE(prompted);
    f.pointAtViewport();
    QTest::keyClick(f.viewport, Qt::Key_E);
    REQUIRE(f.model->componentExtrusion());
    QTest::keyClicks(f.viewport, "-.25");
    f.confirm();
    REQUIRE(meshRecord(*f.model, f.entity).content->source.faces.size() == 14);
    REQUIRE_FALSE(f.model->lastOperationInsetThickness());
    f.pointAtViewport();
    QTest::keyClick(f.viewport, Qt::Key_F9);
    REQUIRE_FALSE(f.thickness()->isVisible());
    auto* z = f.panel->findChild<editor::CommitSpinBox*>(QStringLiteral("LastOperationOffsetZ"));
    REQUIRE(z->isVisible());
    REQUIRE(z->hasFocus());
    REQUIRE(z->value() == -.25);
    f.model->undo();
    f.model->redo();
    REQUIRE(f.model->lastOperationWorldOffset() == glm::dvec3(0, 0, -.25));
}

TEST_CASE(
    "Inset thickness remains local under a negative nonuniform parent and repeated operations",
    "[inset-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    core::Transform transform;
    transform.position = {3, -2, 5};
    transform.scale = {-2, 1.5F, .5F};
    transform.rotation = glm::angleAxis(glm::radians(35.F), glm::vec3(0, 1, 0));
    REQUIRE(model.setTransform(parent, transform));
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(entity, parent));
    REQUIRE(model.setEditMode(true));
    model.setSelectionDomain(editor::SelectionDomain::Face);
    model.selectComponent({1}, editor::SelectionOperation::Replace);
    const auto before = meshRecord(model, entity);
    REQUIRE(model.beginInsetFace());
    REQUIRE(model.previewInsetFace(.1));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.adjustLastInset(.2));
    REQUIRE(meshRecord(model, entity).content->source ==
            *core::modeling::insetFace(before.content->source, 1, .2).mesh);
    const auto first = meshRecord(model, entity);
    const auto count = model.undoStack()->count();
    REQUIRE(model.beginInsetFace());
    REQUIRE(model.previewInsetFace(.05));
    REQUIRE(model.finishComponentTransform(true));
    REQUIRE(model.adjustLastInset(.1));
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE(meshRecord(model, entity).content->source ==
            *core::modeling::insetFace(first.content->source, 1, .1).mesh);
    REQUIRE(meshRecord(model, entity).content->source.faces.size() == 14);
    const auto& actual = model.scene()->find(parent)->transform;
    REQUIRE(actual.position == transform.position);
    REQUIRE(actual.rotation == transform.rotation);
    REQUIRE(actual.scale == transform.scale);
    model.undo();
    REQUIRE(meshRecord(model, entity).content == first.content);
    REQUIRE_FALSE(model.lastOperationInsetThickness());
}

TEST_CASE("Inset mouse fine input step reversal and precise numbers use one initial face",
          "[inset-ui]") {
    InsetWindow f;
    const auto before = meshRecord(*f.model, f.entity);
    const QPoint start(f.viewport->width() * 2 / 3, f.viewport->height() / 2);
    const auto move = [&](int pixels, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const auto point = start + QPoint(pixels, 0);
        QMouseEvent event(QEvent::MouseMove, point, f.viewport->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, modifiers);
        QApplication::sendEvent(f.viewport, &event);
    };
    const auto expected = [&](double thickness) {
        const auto mesh = core::modeling::insetFace(before.content->source, 1, thickness);
        REQUIRE(mesh.mesh);
        REQUIRE(f.model->componentPreview()->source == *mesh.mesh);
    };
    f.begin("");
    move(40);
    expected(.1);
    QTest::keyPress(f.viewport, Qt::Key_Shift);
    expected(.1);
    move(60, Qt::ShiftModifier);
    expected(.105);
    QTest::keyRelease(f.viewport, Qt::Key_Shift);
    expected(.105);
    QTest::keyPress(f.viewport, Qt::Key_Control);
    expected(.1);
    QTest::keyRelease(f.viewport, Qt::Key_Control);
    expected(.105);
    QTest::keyClick(f.viewport, Qt::Key_X);
    expected(.105);
    QTest::keyPress(f.viewport, Qt::Key_Control);
    QTest::keyRelease(f.viewport, Qt::Key_Control);
    QTest::keyClicks(f.viewport, ".123");
    QTest::keyPress(f.viewport, Qt::Key_Control);
    expected(.123);
    QTest::keyRelease(f.viewport, Qt::Key_Control);
    f.confirm();
    REQUIRE(f.model->lastOperationInsetThickness() == .123);
}

TEST_CASE("Inset interruptions restore source and its shortcut does not steal text or IME",
          "[inset-ui]") {
    InsetWindow f;
    const auto before = meshRecord(*f.model, f.entity);
    const auto count = f.model->undoStack()->count();
    f.begin();
    QTest::mouseClick(f.viewport, Qt::RightButton);
    REQUIRE_FALSE(f.modal->isActive());
    f.begin();
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(&f.window, &deactivate);
    REQUIRE_FALSE(f.modal->isActive());
    f.begin();
    QInputMethodEvent preedit(QStringLiteral("测试"), {});
    QApplication::sendEvent(f.viewport, &preedit);
    REQUIRE_FALSE(f.modal->isActive());
    QTest::keyClick(f.viewport, Qt::Key_I);
    REQUIRE_FALSE(f.modal->isActive());
    QInputMethodEvent end;
    QApplication::sendEvent(f.viewport, &end);
    auto* name = f.window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    REQUIRE(name);
    const auto originalName = name->text();
    name->setFocus();
    QTest::keyClicks(name, "i");
    REQUIRE_FALSE(f.modal->isActive());
    REQUIRE(name->text().contains('i'));
    name->setText(originalName);
    f.begin();
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("取消预览再保存.m3dscene"));
    REQUIRE(f.model->saveScene(path));
    REQUIRE_FALSE(f.modal->isActive());
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(meshRecord(reopened, f.entity).content->source == before.content->source);
    REQUIRE(meshRecord(*f.model, f.entity).content == before.content);
    REQUIRE(f.model->undoStack()->count() == count);
}
