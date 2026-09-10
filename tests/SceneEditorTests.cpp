/*
 * 模块名: SceneEditorTests
 * 功能概述: 验证树模型契约、Inspector 无回写循环及真实视口的场景联动。
 * 对外接口: Catch2 测试、main
 * 依赖关系: Qt Test/Widgets、编辑器 UI、Catch2
 * 输入输出: 模拟控件操作，断言实体状态与 GPU 帧缓冲变化。
 * 异常与错误: 模型契约违规或 Context 不可用时测试失败。
 * 维护说明: 需要 Windows 图形环境；不读取桌面屏幕。
 */
#include "editor/MainWindow.h"
#include "editor/SceneTreeModel.h"
#include "editor/TransformInspector.h"
#include "renderer_gl/EditorCamera.h"
#include "renderer_gl/RayCaster.h"
#include "renderer_gl/ViewportWidget.h"

#include <QAbstractItemModelTester>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
using namespace mini3d;
TEST_CASE("Scene tree maps stable IDs and emits valid model notifications", "[editor]") {
    editor::SceneViewModel viewModel;
    editor::SceneTreeModel model(viewModel);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    const auto root = model.index(0, 0);
    REQUIRE(model.rowCount() == 1);
    REQUIRE(model.rowCount(root) == 3);
    const auto cubeIndex = model.index(0, 0, root);
    const auto cube = model.entityId(cubeIndex);
    REQUIRE(model.parent(cubeIndex) == root);
    REQUIRE(model.data(cubeIndex).toString() == QStringLiteral("立方体"));
    REQUIRE(model.setData(cubeIndex, QStringLiteral("方块"), Qt::EditRole));
    REQUIRE(model.data(cubeIndex).toString() == QStringLiteral("方块"));
    REQUIRE(model.setData(cubeIndex, Qt::Unchecked, Qt::CheckStateRole));
    REQUIRE_FALSE(viewModel.scene()->isVisible(cube));
    viewModel.selection()->setSelectedEntity(cube);
    const auto group = viewModel.createEntity(core::PrimitiveKind::Empty);
    REQUIRE(model.rowCount() == 2);
    REQUIRE(viewModel.selection()->selectedEntity() == group);
    viewModel.selection()->setSelectedEntity(cube);
    REQUIRE(viewModel.setParent(cube, group));
    REQUIRE(model.parent(model.indexForEntity(cube)) == model.indexForEntity(group));
    REQUIRE(viewModel.selection()->selectedEntity() == cube);
    REQUIRE_FALSE(viewModel.setParent(group, cube));
    REQUIRE(model.rowCount(model.indexForEntity(group)) == 1);
    viewModel.selection()->setSelectedEntity(999);
    REQUIRE(viewModel.selection()->selectedEntity() == core::kInvalidEntity);
}
TEST_CASE("Inspector edits synchronize without feedback and reject singular scale", "[editor]") {
    editor::SceneViewModel viewModel;
    editor::TransformInspector inspector(viewModel);
    const auto group = viewModel.scene()->roots().front();
    const auto cube = viewModel.scene()->find(group)->children.front();
    int changes = 0;
    QObject::connect(&viewModel, &editor::SceneViewModel::sceneChanged, [&changes] {
        ++changes;
    });
    viewModel.selection()->setSelectedEntity(cube);
    inspector.refresh();
    REQUIRE(changes == 0);
    auto* x = inspector.findChild<QDoubleSpinBox*>(QStringLiteral("PositionX"));
    auto* scale = inspector.findChild<QDoubleSpinBox*>(QStringLiteral("ScaleX"));
    auto* rotation = inspector.findChild<QDoubleSpinBox*>(QStringLiteral("RotationZ"));
    REQUIRE(x != nullptr);
    REQUIRE(scale != nullptr);
    REQUIRE(rotation != nullptr);
    REQUIRE(x->value() == -1.5);
    x->setValue(2.0);
    REQUIRE(viewModel.scene()->find(cube)->transform.position.x == 2.0F);
    REQUIRE(changes == 1);
    scale->setValue(0);
    REQUIRE(viewModel.scene()->find(cube)->transform.scale.x == 1.0F);
    REQUIRE(scale->value() == 1.0);
    REQUIRE(changes == 1);
    auto* error = inspector.findChild<QLabel*>(QStringLiteral("InspectorMessage"));
    REQUIRE(error != nullptr);
    REQUIRE(error->text().contains(QStringLiteral("变换被拒绝")));
    rotation->setValue(90);
    const auto direction = viewModel.scene()->find(cube)->transform.rotation * glm::vec3(1, 0, 0);
    REQUIRE(glm::length(direction - glm::vec3(0, 1, 0)) < 1.0e-5F);
    core::Transform changed = viewModel.scene()->find(cube)->transform;
    changed.position.x = -3;
    REQUIRE(viewModel.setTransform(cube, changed));
    REQUIRE(x->value() == -3.0);
    const int beforeRefresh = changes;
    inspector.refresh();
    REQUIRE(changes == beforeRefresh);
    auto* parents = inspector.findChild<QComboBox*>(QStringLiteral("EntityParent"));
    REQUIRE(parents != nullptr);
    REQUIRE(parents->findData(QVariant::fromValue<qulonglong>(cube)) == -1);
    viewModel.selection()->setSelectedEntity(core::kInvalidEntity);
    REQUIRE_FALSE(x->isEnabled());
}
TEST_CASE("Editor tree selection and parent edits update the actual framebuffer", "[editor][gpu]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    auto* viewModel = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<QOpenGLWidget*>(QStringLiteral("ViewportWidget"));
    REQUIRE(tree != nullptr);
    REQUIRE(viewModel != nullptr);
    REQUIRE(viewport != nullptr);
    auto* model = qobject_cast<editor::SceneTreeModel*>(tree->model());
    REQUIRE(model != nullptr);
    const auto root = model->index(0, 0);
    const auto cubeIndex = model->index(0, 0, root);
    tree->expandAll();
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualRect(cubeIndex).center());
    const auto cube = model->entityId(cubeIndex);
    REQUIRE(viewModel->selection()->selectedEntity() == cube);
    auto* name = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    REQUIRE(name != nullptr);
    REQUIRE(name->text() == QStringLiteral("立方体"));
    name->selectAll();
    QTest::keyClicks(name, "EditedCube");
    QTest::keyClick(name, Qt::Key_Return);
    REQUIRE(viewModel->scene()->find(cube)->name == "EditedCube");
    const QImage before = viewport->grabFramebuffer();
    REQUIRE_FALSE(before.isNull());
    const auto group = model->entityId(root);
    viewModel->selection()->setSelectedEntity(group);
    REQUIRE(tree->currentIndex() == model->indexForEntity(group));
    auto* position = window.findChild<QDoubleSpinBox*>(QStringLiteral("PositionY"));
    REQUIRE(position != nullptr);
    position->setValue(1.5);
    REQUIRE(viewModel->scene()->worldMatrix(cube)[3].y == 2.0F);
    const QImage moved = viewport->grabFramebuffer();
    REQUIRE(moved != before);
    REQUIRE(viewModel->setVisible(group, false));
    const QImage hidden = viewport->grabFramebuffer();
    REQUIRE(hidden != moved);
    REQUIRE(viewModel->setVisible(group, true));
    REQUIRE(viewModel->setTransformComponent(group, 0, 1, 0));
    viewModel->selection()->setSelectedEntity(cube);
    REQUIRE(viewport->grabFramebuffer() == before);
    // 负缩放后的绕序应仍绘制封闭 Cube。
    REQUIRE(viewModel->setTransformComponent(cube, 2, 0, -1));
    REQUIRE(viewport->grabFramebuffer() != hidden);
    auto* create = window.findChild<QAction*>(QStringLiteral("CreateEmpty"));
    REQUIRE(create != nullptr);
    create->trigger();
    const auto newParent = viewModel->selection()->selectedEntity();
    REQUIRE(viewModel->scene()->find(newParent)->primitive == core::PrimitiveKind::Empty);
    viewModel->selection()->setSelectedEntity(cube);
    auto* parentBox = window.findChild<QComboBox*>(QStringLiteral("EntityParent"));
    REQUIRE(parentBox != nullptr);
    const int parentRow = parentBox->findData(QVariant::fromValue<qulonglong>(newParent));
    REQUIRE(parentRow >= 0);
    parentBox->setCurrentIndex(parentRow);
    REQUIRE(QMetaObject::invokeMethod(parentBox, "activated", Qt::DirectConnection,
                                      Q_ARG(int, parentRow)));
    REQUIRE(viewModel->scene()->find(cube)->parent == newParent);
    REQUIRE(tree->currentIndex() == model->indexForEntity(cube));
    REQUIRE(model->parent(tree->currentIndex()) == model->indexForEntity(newParent));
    const QString capture = qEnvironmentVariable("MINI3D_TEST_EDITOR_CAPTURE");
    if (!capture.isEmpty()) {
        QTest::qWait(100);
        REQUIRE(window.grab().save(capture));
    }
    window.hide();
}
TEST_CASE("File import creates editable GLB instances with shared resources and real pixels",
          "[editor][gpu][import]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* viewModel = window.findChild<editor::SceneViewModel*>();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    auto* viewport = window.findChild<QOpenGLWidget*>(QStringLiteral("ViewportWidget"));
    REQUIRE(viewModel != nullptr);
    REQUIRE(tree != nullptr);
    REQUIRE(viewport != nullptr);
    auto* model = qobject_cast<editor::SceneTreeModel*>(tree->model());
    QAbstractItemModelTester modelTester(model,
                                         QAbstractItemModelTester::FailureReportingMode::Fatal);
    REQUIRE(viewModel->setVisible(viewModel->scene()->roots().front(), false));
    const auto emptyFrame = viewport->grabFramebuffer();
    const QString directory = QString::fromUtf8(MINI3D_SAMPLE_DIRECTORY);
    auto* importAction = window.findChild<QAction*>(QStringLiteral("ImportGltf"));
    REQUIRE(importAction != nullptr);
    bool picked = false;
    QTimer picker;
    QString dialogState;
    QObject::connect(&picker, &QTimer::timeout, &window, [&] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (dialog != nullptr && !picked) {
            auto* fileName = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            if (fileName == nullptr || buttons == nullptr) {
                dialogState = QStringLiteral("Missing fileNameEdit/button box");
                return;
            }
            const auto path = directory + QStringLiteral("/BoxTextured.glb");
            fileName->setFocus();
            fileName->selectAll();
            QTest::keyClicks(fileName, path);
            dialogState = dialog->selectedFiles().join(QStringLiteral(";"));
            auto* open = buttons->button(QDialogButtonBox::Open);
            if (open == nullptr || !open->isEnabled()) {
                return;
            }
            picked = true;
            QTest::mouseClick(open, Qt::LeftButton);
        }
    });
    // 看门狗只保护测试的模态对话框，不影响正常编辑器行为。
    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, &window, [] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dialog->reject();
        }
    });
    watchdog.start(5000);
    picker.start(100);
    importAction->trigger();
    picker.stop();
    watchdog.stop();
    INFO(dialogState.toStdString());
    REQUIRE(picked);
    const auto first = viewModel->selection()->selectedEntity();
    REQUIRE(first != core::kInvalidEntity);
    REQUIRE(viewModel->scene()->find(first)->name == "BoxTextured");
    REQUIRE(model->entityId(tree->currentIndex()) == first);
    REQUIRE(viewModel->assets()->meshCount() == 1);
    const auto texturedFrame = viewport->grabFramebuffer();
    REQUIRE_FALSE(texturedFrame.isNull());
    REQUIRE(texturedFrame != emptyFrame);
    const auto second = viewModel->importGltf(directory + "/BoxTextured.glb");
    REQUIRE(second != first);
    REQUIRE(viewModel->assets()->meshCount() == 1);
    REQUIRE(viewModel->setVisible(second, false));
    viewModel->selection()->setSelectedEntity(first);
    REQUIRE(viewport->grabFramebuffer() == texturedFrame);
    const auto rootsBeforeFailure = viewModel->scene()->roots();
    REQUIRE(viewModel->importGltf(directory + "/missing.glb") == core::kInvalidEntity);
    REQUIRE(window.statusBar()->currentMessage().contains(QStringLiteral("missing.glb")));
    REQUIRE(viewModel->scene()->roots() == rootsBeforeFailure);
    REQUIRE(viewModel->selection()->selectedEntity() == first);
    viewModel->selection()->setSelectedEntity(first);
    auto* x = window.findChild<QDoubleSpinBox*>(QStringLiteral("PositionX"));
    REQUIRE(x != nullptr);
    x->setValue(-1.5);
    REQUIRE(viewModel->scene()->find(first)->transform.position.x == -1.5F);
    REQUIRE(viewport->grabFramebuffer() != texturedFrame);
    const auto duck = viewModel->importGltf(directory + "/Duck.glb");
    REQUIRE(duck != core::kInvalidEntity);
    REQUIRE(viewModel->setTransformComponent(duck, 0, 0, 0.5));
    REQUIRE(viewModel->assets()->meshCount() == 2);
    const auto duckFrame = viewport->grabFramebuffer();
    REQUIRE(duckFrame != emptyFrame);
    const auto box = viewModel->importGltf(directory + "/Box.glb");
    REQUIRE(box != core::kInvalidEntity);
    REQUIRE(window.statusBar()->currentMessage().startsWith(QStringLiteral("已导入 ")));
    REQUIRE_FALSE(window.statusBar()->currentMessage().contains(QStringLiteral("missing.glb")));
    REQUIRE(viewModel->setTransformComponent(box, 0, 0, 1.5));
    REQUIRE(viewModel->setTransformComponent(box, 0, 2, -1.0));
    REQUIRE(viewModel->assets()->meshCount() == 3);
    REQUIRE(viewport->grabFramebuffer() != duckFrame);
    viewModel->selection()->setSelectedEntity(duck);
    tree->expandAll();
    const auto capture = qEnvironmentVariable("MINI3D_TEST_IMPORT_CAPTURE");
    if (!capture.isEmpty()) {
        QTest::qWait(100);
        REQUIRE(window.grab().save(capture));
    }
    window.hide();
}

TEST_CASE("Viewport clicks synchronize selection highlight and inspector without dragging objects",
          "[editor][gpu][picking]") {
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* viewModel = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    auto* name = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    REQUIRE(viewModel != nullptr);
    REQUIRE(viewport != nullptr);
    REQUIRE(tree != nullptr);
    REQUIRE(name != nullptr);
    REQUIRE(viewModel->setVisible(viewModel->scene()->roots().front(), false));
    INFO(viewport->devicePixelRatioF());
    if (qEnvironmentVariable("QT_SCALE_FACTOR") == QStringLiteral("2")) {
        REQUIRE(viewport->devicePixelRatioF() == 2.0);
    }
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(viewport->width(), viewport->height());
    const auto near = viewModel->createEntity(core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position = camera.target();
    REQUIRE(viewModel->setTransform(near, transform));
    const auto far = viewModel->createEntity(core::PrimitiveKind::Sphere);
    transform.position += glm::normalize(camera.target() - camera.position()) * 2.0F;
    REQUIRE(viewModel->setTransform(far, transform));
    viewModel->selection()->setSelectedEntity(0);
    const auto plain = viewport->grabFramebuffer();
    const QPoint center(viewport->width() / 2, viewport->height() / 2);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, center);
    REQUIRE(viewModel->selection()->selectedEntity() == near);
    auto* model = qobject_cast<editor::SceneTreeModel*>(tree->model());
    REQUIRE(model->entityId(tree->currentIndex()) == near);
    REQUIRE(name->text() == QStringLiteral("立方体"));
    const auto highlighted = viewport->grabFramebuffer();
    REQUIRE(highlighted != plain);
    QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, center);
    QTest::mouseRelease(viewport, Qt::LeftButton, Qt::NoModifier, center + QPoint(80, 20));
    REQUIRE(viewModel->selection()->selectedEntity() == near);
    REQUIRE(viewModel->scene()->find(near)->transform.position == camera.target());
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::ControlModifier, QPoint(2, 2));
    REQUIRE(viewModel->selection()->selectedEntity() == near);
    tree->expandAll();
    const auto farIndex = model->indexForEntity(far);
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualRect(farIndex).center());
    REQUIRE(viewModel->selection()->selectedEntity() == far);
    REQUIRE(name->text() == QStringLiteral("球体"));
    REQUIRE(viewport->grabFramebuffer() != highlighted);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    REQUIRE(viewModel->selection()->selectedEntity() == 0);
    REQUIRE_FALSE(tree->currentIndex().isValid());
    REQUIRE(tree->selectionModel()->selectedIndexes().isEmpty());
    REQUIRE_FALSE(name->isEnabled());
    REQUIRE(viewport->grabFramebuffer() == plain);
    REQUIRE(viewModel->setVisible(near, false));
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, center);
    REQUIRE(viewModel->selection()->selectedEntity() == far);
    const auto beforeOrbit = viewport->grabFramebuffer();
    QTest::mousePress(viewport, Qt::MiddleButton, Qt::NoModifier, center);
    const QPoint moved = center + QPoint(60, 20);
    QMouseEvent move(QEvent::MouseMove, QPointF(moved), QPointF(viewport->mapToGlobal(moved)),
                     Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    QCoreApplication::sendEvent(viewport, &move);
    QTest::mouseRelease(viewport, Qt::MiddleButton, Qt::NoModifier, moved);
    REQUIRE(viewModel->selection()->selectedEntity() == far);
    REQUIRE(viewport->grabFramebuffer() != beforeOrbit);
    // 相机旋转后再缩放窗口，实际点击仍应与新视口投影一致。
    window.resize(1100, 760);
    QTest::qWait(50);
    camera.orbit(60, 20);
    camera.setViewportSize(viewport->width(), viewport->height());
    const auto clip = camera.viewProjectionMatrix() * glm::vec4(transform.position, 1);
    const QPoint projected(static_cast<int>((clip.x / clip.w + 1) * viewport->width() * 0.5F),
                           static_cast<int>((1 - clip.y / clip.w) * viewport->height() * 0.5F));
    REQUIRE(viewport->rect().contains(projected));
    viewModel->selection()->setSelectedEntity(0);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, projected);
    REQUIRE(viewModel->selection()->selectedEntity() == far);
    window.hide();
}

TEST_CASE("Focus shortcut frames imported subtrees and does not steal property text input",
          "[editor][gpu][focus]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* viewModel = window.findChild<editor::SceneViewModel*>();
    auto* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    auto* focus = window.findChild<QAction*>(QStringLiteral("FocusSelection"));
    REQUIRE(viewModel != nullptr);
    REQUIRE(viewport != nullptr);
    REQUIRE(tree != nullptr);
    REQUIRE(focus != nullptr);
    QSignalSpy focused(focus, &QAction::triggered);
    REQUIRE(viewModel->setVisible(viewModel->scene()->roots().front(), false));
    const auto root =
        viewModel->importGltf(QString::fromUtf8(MINI3D_SAMPLE_DIRECTORY) + "/Duck.glb");
    REQUIRE(root != 0);
    REQUIRE(viewModel->setTransformComponent(root, 0, 0, 20));
    const auto before = viewport->grabFramebuffer();
    tree->setFocus();
    QTest::keyClick(tree, Qt::Key_F);
    REQUIRE(focused.count() == 1);
    const auto fitted = viewport->grabFramebuffer();
    REQUIRE(fitted != before);
    auto* name = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    REQUIRE(name != nullptr);
    name->setFocus();
    name->selectAll();
    QTest::keyClicks(name, "F");
    REQUIRE(name->text() == QStringLiteral("F"));
    REQUIRE(focused.count() == 1);
    REQUIRE(viewport->grabFramebuffer() == fitted);
    name->setText(QStringLiteral("Duck"));
    REQUIRE(viewModel->setTransformComponent(root, 0, 0, -10));
    viewport->setFocus();
    QTest::keyClick(viewport, Qt::Key_F);
    REQUIRE(focused.count() == 2);
    renderer_gl::EditorCamera expectedCamera;
    expectedCamera.setViewportSize(viewport->width(), viewport->height());
    REQUIRE(expectedCamera.focus(
        renderer_gl::RayCaster::worldBounds(*viewModel->scene(), *viewModel->assets(), root)));
    const QPoint center(viewport->width() / 2, viewport->height() / 2);
    const auto expected = renderer_gl::RayCaster::pick(
        *viewModel->scene(), *viewModel->assets(),
        expectedCamera.screenRay(static_cast<float>(center.x()), static_cast<float>(center.y())));
    REQUIRE(expected != 0);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, center);
    REQUIRE(viewModel->selection()->selectedEntity() == expected);
    REQUIRE(viewModel->scene()->find(expected)->meshRenderer.has_value());
    auto* model = qobject_cast<editor::SceneTreeModel*>(tree->model());
    REQUIRE(model->entityId(tree->currentIndex()) == expected);
    viewModel->selection()->setSelectedEntity(root);
    REQUIRE(viewModel->setVisible(root, false));
    const auto hidden = viewport->grabFramebuffer();
    REQUIRE_FALSE(viewport->focusSelection());
    REQUIRE(viewport->grabFramebuffer() == hidden);
    QTest::mouseClick(viewport, Qt::LeftButton, Qt::NoModifier, center);
    REQUIRE(viewModel->selection()->selectedEntity() == 0);
    REQUIRE_FALSE(viewport->focusSelection());
    REQUIRE(viewModel->setVisible(root, true));
    viewModel->selection()->setSelectedEntity(root);
    focus->trigger();
    REQUIRE(focused.count() == 3);
    tree->expandAll();
    const auto capture = qEnvironmentVariable("MINI3D_TEST_SELECTION_CAPTURE");
    if (!capture.isEmpty()) {
        QTest::qWait(100);
        REQUIRE(window.grab().save(capture));
    }
    window.hide();
}

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QSurfaceFormat::setDefaultFormat(renderer_gl::ViewportWidget::defaultSurfaceFormat());
    QApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE(
    "Tree drag data reparents once preserves local TRS and rejects cycles or foreign sessions",
    "[editor][tree-tools]") {
    editor::SceneViewModel vm;
    vm.newScene();
    editor::SceneTreeModel model(vm);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    const auto parent = vm.createEntity(core::PrimitiveKind::Empty);
    const auto child = vm.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(vm.setTransformComponent(parent, 0, 0, 5));
    REQUIRE(vm.setTransformComponent(child, 0, 1, 2));
    const auto before = vm.scene()->find(child)->transform.localMatrix();
    const auto count = vm.undoStack()->count();
    std::unique_ptr<QMimeData> data(model.mimeData({model.indexForEntity(child)}));
    REQUIRE(model.canDropMimeData(data.get(), Qt::MoveAction, -1, 0, model.indexForEntity(parent)));
    REQUIRE(model.dropMimeData(data.get(), Qt::MoveAction, -1, 0, model.indexForEntity(parent)));
    REQUIRE(vm.scene()->find(child)->parent == parent);
    REQUIRE(vm.scene()->find(child)->transform.localMatrix() == before);
    REQUIRE(vm.undoStack()->count() == count + 1);
    REQUIRE_FALSE(
        model.dropMimeData(data.get(), Qt::MoveAction, -1, 0, model.indexForEntity(child)));
    std::unique_ptr<QMimeData> parentData(model.mimeData({model.indexForEntity(parent)}));
    REQUIRE_FALSE(model.canDropMimeData(parentData.get(), Qt::MoveAction, -1, 0,
                                        model.indexForEntity(child)));
    REQUIRE_FALSE(model.canDropMimeData(data.get(), Qt::CopyAction, -1, 0, {}));
    REQUIRE_FALSE(model.canDropMimeData(data.get(), Qt::MoveAction, 0, 0, {}));
    vm.undo();
    REQUIRE(vm.scene()->find(child)->parent == 0);
    vm.redo();
    REQUIRE(model.dropMimeData(data.get(), Qt::MoveAction, -1, -1, {}));
    REQUIRE(vm.scene()->find(child)->parent == 0);
    editor::SceneTreeModel foreign(vm);
    REQUIRE_FALSE(
        foreign.canDropMimeData(data.get(), Qt::MoveAction, -1, 0, foreign.indexForEntity(parent)));
    vm.newScene();
    const auto newParent = vm.createEntity(core::PrimitiveKind::Empty);
    vm.createEntity(core::PrimitiveKind::Cube);
    REQUIRE_FALSE(
        model.canDropMimeData(data.get(), Qt::MoveAction, -1, 0, model.indexForEntity(newParent)));
}

TEST_CASE("Tree view drop search and Chinese context actions share selection and undo",
          "[editor][tree-tools]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* vm = window.findChild<editor::SceneViewModel*>();
    auto* tree = window.findChild<QTreeView*>(QStringLiteral("SceneTree"));
    auto* model = qobject_cast<editor::SceneTreeModel*>(tree->model());
    vm->newScene();
    const auto parent = vm->createEntity(core::PrimitiveKind::Empty);
    REQUIRE(vm->renameEntity(parent, QStringLiteral("中文分组")));
    const auto child = vm->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(vm->renameEntity(child, QStringLiteral("目标方块")));
    QTest::qWait(50);
    const auto drop = [&](core::EntityId id, const QPoint& point) {
        std::unique_ptr<QMimeData> mime(model->mimeData({model->indexForEntity(id)}));
        QDragEnterEvent enter(point, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &enter);
        REQUIRE(enter.isAccepted());
        QDragMoveEvent move(point, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &move);
        REQUIRE(move.isAccepted());
        QDropEvent event(point, Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &event);
        REQUIRE(event.isAccepted());
        QTest::qWait(30);
    };
    drop(child, tree->visualRect(model->indexForEntity(parent)).center());
    REQUIRE(vm->scene()->find(child) != nullptr);
    REQUIRE(vm->scene()->find(child)->parent == parent);
    vm->undo();
    REQUIRE(vm->scene()->find(child)->parent == 0);
    vm->redo();
    tree->expandAll();
    drop(child, QPoint(100, tree->viewport()->height() - 20));
    REQUIRE(vm->scene()->find(child)->parent == 0);
    vm->undo();
    tree->collapseAll();
    vm->selection()->setSelectedEntity(parent);
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("SceneSearch"));
    search->setText(QStringLiteral("目标"));
    search->setFocus();
    QTest::keyClick(search, Qt::Key_Return);
    REQUIRE(vm->selection()->selectedEntity() == child);
    REQUIRE(tree->isExpanded(model->indexForEntity(parent)));
    REQUIRE(tree->currentIndex() == model->indexForEntity(child));
    search->setText(QStringLiteral("不存在"));
    QTest::keyClick(search, Qt::Key_Return);
    REQUIRE(vm->selection()->selectedEntity() == child);
    REQUIRE(window.statusBar()->currentMessage().contains(QStringLiteral("没有找到")));
    const auto invokeMenu = [&](const QString& name) {
        const auto point = tree->visualRect(model->indexForEntity(child)).center();
        tree->customContextMenuRequested(point);
        QTest::qWait(20);
        auto* menu = window.findChild<QMenu*>(QStringLiteral("SceneContextMenu"));
        REQUIRE(menu != nullptr);
        auto* action = menu->findChild<QAction*>(name);
        REQUIRE(action != nullptr);
        REQUIRE(action->isEnabled());
        action->trigger();
        menu->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    invokeMenu(QStringLiteral("TreeRename"));
    auto* nameEditor = tree->findChild<QLineEdit*>();
    REQUIRE(nameEditor != nullptr);
    nameEditor->setText(QStringLiteral("目标方块改名"));
    QTest::keyClick(nameEditor, Qt::Key_Return);
    // Qt 委托通过排队调用提交回车编辑，等待提交后检查领域状态。
    QTest::qWait(30);
    REQUIRE(vm->scene()->find(child)->name == "目标方块改名");
    vm->undo();
    invokeMenu(QStringLiteral("TreeFocus"));
    invokeMenu(QStringLiteral("TreeVisibility"));
    REQUIRE_FALSE(vm->scene()->find(child)->visible);
    vm->undo();
    REQUIRE(vm->scene()->find(child)->visible);
    invokeMenu(QStringLiteral("TreeToRoot"));
    REQUIRE(vm->scene()->find(child)->parent == 0);
    vm->undo();
    tree->expandAll();
    invokeMenu(QStringLiteral("TreeDuplicate"));
    const auto copy = vm->selection()->selectedEntity();
    REQUIRE(copy != child);
    REQUIRE(vm->scene()->find(copy)->parent == parent);
    vm->undo();
    REQUIRE(vm->scene()->find(copy) == nullptr);
    tree->expandAll();
    invokeMenu(QStringLiteral("TreeDelete"));
    REQUIRE(vm->scene()->find(child) == nullptr);
    vm->undo();
    tree->expandAll();
    REQUIRE(vm->scene()->find(child) != nullptr);
    tree->customContextMenuRequested(QPoint(100, tree->viewport()->height() - 20));
    REQUIRE(window.findChild<QMenu*>(QStringLiteral("SceneContextMenu")) == nullptr);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_TREE_CAPTURE");
    if (!capture.isEmpty()) {
        REQUIRE(window.grab().save(capture));
    }
    window.hide();
}
