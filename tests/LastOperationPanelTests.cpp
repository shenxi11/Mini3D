/*
 * 模块名: LastOperationPanelTests
 * 功能概述: 验证 F9/菜单/F3/Q 同面板、真实输入、保存提示与区域输入隔离。
 * 对外接口: Catch2 [last-operation-panel]；依赖关系: Qt Test、真实窗口/GL、临时文件。
 * 输入输出: 挤出和调参按键到源网格、唯一历史、字段、窗口布局与截图。
 * 异常与错误: 无效参数/过期上下文不改模型，不自动确认用户对话框。
 * 维护说明: 只自动处理本测试创建的关闭提示，不影响用户窗口或设置。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/operations/KeymapRouter.h"
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
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
struct PanelWindow {
    editor::MainWindow window;
    editor::SceneViewModel* model = window.findChild<editor::SceneViewModel*>();
    renderer_gl::ViewportWidget* viewport = window.findChild<renderer_gl::ViewportWidget*>();
    editor::LastOperationPanel* panel = window.findChild<editor::LastOperationPanel*>();
    editor::KeymapRouter* router = window.findChild<editor::KeymapRouter*>();
    QToolButton* toggle = window.findChild<QToolButton*>(QStringLiteral("LastOperationToggle"));
    core::EntityId entity;
    PanelWindow() {
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        model->newScene();
        entity = model->createEntity(core::PrimitiveKind::Cube);
        REQUIRE(model->setEditMode(true));
        model->setSelectionDomain(editor::SelectionDomain::Face);
        model->selectComponent({1}, editor::SelectionOperation::Replace);
        viewport->setEditorCamera({{4, 3, 5}, {0, 0, 0}, 0, 50});
        router->setKeymap(editor::EditorKeymap::Blender);
        QTest::qWait(30);
        pointAt(viewport);
    }
    ~PanelWindow() {
        window.hide();
    }
    QAction* action(const char* name) {
        return window.findChild<QAction*>(QString::fromLatin1(name));
    }
    editor::CommitSpinBox* field(char axis = 'Z') {
        return panel->findChild<editor::CommitSpinBox*>(
            QStringLiteral("LastOperationOffset%1").arg(QChar(axis)));
    }
    void pointAt(QWidget* widget) {
        const QPoint point(widget->width() * 2 / 3, widget->height() / 2);
        QMouseEvent event(QEvent::MouseMove, point, widget->mapToGlobal(point), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
        viewport->setFocus();
    }
    void extrude() {
        pointAt(viewport);
        QTest::keyClick(viewport, Qt::Key_E);
        REQUIRE(model->componentExtrusion());
        QTest::keyClicks(viewport, ".25");
        QTest::keyClick(viewport, Qt::Key_Return);
        REQUIRE_FALSE(model->hasComponentTransform());
        REQUIRE(panel->isVisible());
        REQUIRE(action("AdjustLastOperation")->isEnabled());
    }
    void open() {
        pointAt(viewport);
        QTest::keyClick(viewport, Qt::Key_F9);
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        REQUIRE(toggle->isChecked());
        REQUIRE(field()->hasFocus());
    }
    void type(const char* value, char axis = 'Z') {
        auto* spin = field(axis);
        spin->setFocus();
        spin->selectAll();
        QTest::keyClicks(spin, value);
    }
    void submit(char axis = 'Z') {
        QTest::keyClick(field(axis)->findChild<QLineEdit*>(), Qt::Key_Return);
    }
    const core::modeling::EditableMesh& source() const {
        return model->scene()
            ->editableMesh(model->scene()->find(entity)->editableMesh)
            ->content->source;
    }
};
} // namespace

TEST_CASE("F9 edits one saved extrusion with real geometry and an unsaved close prompt",
          "[last-operation-panel]") {
    PanelWindow f;
    REQUIRE_FALSE(f.panel->isVisible());
    REQUIRE_FALSE(f.action("AdjustLastOperation")->isEnabled());
    f.extrude();
    REQUIRE_FALSE(f.toggle->isChecked());
    REQUIRE(f.viewport->hasFocus());
    const auto index = f.model->undoStack()->index();
    const auto* command = f.model->undoStack()->command(index - 1);
    const auto context = f.viewport->context();
    QTemporaryDir directory;
    REQUIRE(f.model->saveScene(directory.filePath(QStringLiteral("面板调参.m3dscene"))));
    f.open();
    REQUIRE_FALSE(f.model->isModified());
    f.type(".75");
    REQUIRE(f.model->lastOperationWorldOffset() == glm::dvec3(0, 0, 0.25));
    f.submit();
    REQUIRE(f.model->lastOperationWorldOffset() == glm::dvec3(0, 0, 0.75));
    REQUIRE(f.source().faces.size() == 10);
    REQUIRE(f.source().vertex(f.source().faces[0].corners[0].vertex)->position.z == 1.25F);
    REQUIRE(f.model->undoStack()->index() == index);
    REQUIRE(f.model->undoStack()->count() == index);
    REQUIRE(f.model->undoStack()->command(index - 1) == command);
    REQUIRE(f.model->isModified());
    REQUIRE(f.viewport->context() == context);
    const auto capture = qEnvironmentVariable("MINI3D_TEST_HISTORY_CAPTURE");
    if (!capture.isEmpty()) {
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        REQUIRE(f.window.grab().save(capture));
    }
    bool prompted = false;
    QTimer::singleShot(0, &f.window, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            prompted = true;
            box->button(QMessageBox::Cancel)->click();
        }
    });
    REQUIRE_FALSE(f.window.close());
    REQUIRE(prompted);
    REQUIRE(f.window.isVisible());
    f.model->undo();
    REQUIRE(f.source().faces.size() == 6);
    REQUIRE_FALSE(f.field()->isEnabled());
    REQUIRE(f.panel->findChild<QLabel*>(QStringLiteral("LastOperationMessage"))
                ->text()
                .contains(QStringLiteral("没有可调整")));
    f.model->redo();
    REQUIRE(f.field()->value() == 0.75);
    REQUIRE(f.model->isModified());
}

TEST_CASE("F9 field rejection Escape collapse and document reset never commit stale drafts",
          "[last-operation-panel]") {
    QStringList events;
    PanelWindow f;
    f.extrude();
    f.open();
    const auto before = f.source();
    const auto count = f.model->undoStack()->count();
    f.type("0");
    f.submit();
    REQUIRE(f.source() == before);
    REQUIRE_FALSE(f.field()->validationMessage().isEmpty());
    REQUIRE(f.field()->cleanText() == QStringLiteral("0"));
    QTest::keyClick(f.field(), Qt::Key_Escape);
    REQUIRE(f.field()->validationMessage().isEmpty());
    REQUIRE(f.field()->value() == 0.25);
    f.type("bad");
    f.submit();
    REQUIRE(f.field()->cleanText() == QStringLiteral("bad"));
    REQUIRE(f.source() == before);
    QTest::keyClick(f.field(), Qt::Key_Escape);
    f.type(".75");
    REQUIRE(f.source() == before);
    REQUIRE(f.field()->value() == 0.25);
    QObject::connect(f.field(), &QDoubleSpinBox::valueChanged, f.panel, [&](double value) {
        events.append(QStringLiteral("value=%1").arg(value));
    });
    QObject::connect(f.field(), &QDoubleSpinBox::editingFinished, f.panel, [&] {
        events.append(QStringLiteral("editingFinished"));
    });
    QObject::connect(f.toggle, &QToolButton::pressed, f.panel, [&] {
        events.append(QStringLiteral("pressed"));
    });
    QObject::connect(f.toggle, &QToolButton::toggled, f.panel, [&](bool checked) {
        events.append(QStringLiteral("toggled=%1").arg(checked));
    });
    QTest::mouseClick(f.toggle, Qt::LeftButton);
    INFO(events.join(QStringLiteral(" -> ")).toStdString());
    REQUIRE_FALSE(f.toggle->isChecked());
    REQUIRE(f.source() == before);
    REQUIRE(f.model->undoStack()->count() == count);
    f.open();
    f.type(".75");
    auto surface = f.model->scene()->find(f.entity)->surface;
    surface.tint.x = 0.2F;
    REQUIRE(f.model->setSurface(f.entity, surface));
    REQUIRE_FALSE(f.field()->isEnabled());
    REQUIRE(f.source() == before);
    REQUIRE_FALSE(f.action("AdjustLastOperation")->isEnabled());
    f.model->newScene();
    REQUIRE_FALSE(f.panel->isVisible());
    REQUIRE(f.field()->validationMessage().isEmpty());
    REQUIRE(f.model->undoStack()->count() == 0);
}

TEST_CASE("F3 Q menu and Blender F9 share a registered parameter panel with legacy fallback",
          "[last-operation-panel]") {
    PanelWindow f;
    auto* registry = f.window.findChild<editor::OperatorRegistry*>();
    REQUIRE(registry->descriptor(QStringLiteral("mesh.extrude_region"))->reopenable);
    REQUIRE(registry->descriptor(QStringLiteral("mesh.extrude_region"))
                ->parameterSchema.value(QStringLiteral("worldOffset"))
                .toMap()
                .value(QStringLiteral("space")) == QStringLiteral("world"));
    REQUIRE_FALSE(registry
                      ->disabledReason(QStringLiteral("history.adjust_last"),
                                       registry->captureContext(editor::InputArea::Viewport))
                      .isEmpty());
    f.extrude();
    const auto count = f.model->undoStack()->count();
    auto* favorites = f.window.findChild<editor::QuickFavorites*>();
    REQUIRE(favorites->addOperator(QStringLiteral("history.adjust_last")));
    auto* popup = f.window.findChild<editor::OperatorSearchPopup*>();
    auto* query = f.window.findChild<QLineEdit*>(QStringLiteral("OperatorSearchQuery"));
    auto* results = f.window.findChild<QListWidget*>(QStringLiteral("OperatorSearchResults"));
    REQUIRE(query);
    REQUIRE(results);
    for (const auto key : {Qt::Key_F3, Qt::Key_Q}) {
        f.toggle->setChecked(false);
        f.pointAt(f.viewport);
        QTest::keyClick(f.viewport, key);
        REQUIRE(popup->isVisible());
        if (key == Qt::Key_F3) {
            query->setText(QStringLiteral("adjust last"));
            QTest::keyClick(query, Qt::Key_Return);
        } else {
            for (int row = 0; row < results->count(); ++row) {
                if (results->item(row)->data(Qt::UserRole) == QStringLiteral("history.adjust_last"))
                    results->setCurrentRow(row);
            }
            REQUIRE(results->currentItem());
            REQUIRE(results->currentItem()->data(Qt::UserRole) ==
                    QStringLiteral("history.adjust_last"));
            QTest::keyClick(results, Qt::Key_Return);
        }
        REQUIRE_FALSE(popup->isVisible());
        REQUIRE(f.toggle->isChecked());
        REQUIRE(f.field()->hasFocus());
    }
    const auto stale = registry->captureContext(editor::InputArea::Viewport);
    f.model->undo();
    f.model->redo();
    REQUIRE_FALSE(registry->execute(QStringLiteral("history.adjust_last"), stale));
    f.toggle->setChecked(false);
    f.router->setKeymap(editor::EditorKeymap::Legacy);
    f.pointAt(f.viewport);
    QTest::keyClick(f.viewport, Qt::Key_F9);
    REQUIRE_FALSE(f.toggle->isChecked());
    REQUIRE(f.action("AdjustLastOperation")->shortcut().isEmpty());
    f.action("AdjustLastOperation")->trigger();
    REQUIRE(f.toggle->isChecked());
    REQUIRE(f.model->undoStack()->count() == count);
}

TEST_CASE("Parameter panel blocks viewport mouse wheel and shortcuts while preserving text IME",
          "[last-operation-panel]") {
    PanelWindow f;
    f.extrude();
    f.open();
    auto* label = f.panel->findChild<QLabel*>(QStringLiteral("LastOperationMessage"));
    REQUIRE(f.viewport->editorCameraSnapshot());
    const auto camera = f.viewport->editorCameraSnapshot()->state();
    const auto selected = f.model->componentSelection();
    f.pointAt(label);
    REQUIRE(f.router->areaForWidget(label) == editor::InputArea::None);
    QTest::mouseClick(label, Qt::LeftButton);
    QTest::mouseClick(label, Qt::RightButton);
    const auto point = label->rect().center();
    QWheelEvent wheel(point, label->mapToGlobal(point), {}, QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(label, &wheel);
    QTest::keyClick(f.panel, Qt::Key_G);
    REQUIRE_FALSE(f.model->hasComponentTransform());
    REQUIRE(f.viewport->editorCameraSnapshot()->state() == camera);
    REQUIRE(f.model->componentSelection() == selected);
    f.toggle->setChecked(false);
    f.pointAt(f.viewport);
    auto* name = f.window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    REQUIRE(name);
    name->setFocus();
    QTest::keyClick(name, Qt::Key_F9);
    REQUIRE_FALSE(f.toggle->isChecked());
    f.pointAt(f.viewport);
    QInputMethodEvent preedit(QStringLiteral("测试"), {});
    QApplication::sendEvent(f.viewport, &preedit);
    QTest::keyClick(f.viewport, Qt::Key_F9);
    REQUIRE_FALSE(f.toggle->isChecked());
    QInputMethodEvent end;
    QApplication::sendEvent(f.viewport, &end);
    f.open();
    f.type(".9");
    QTest::keyClick(f.field(), Qt::Key_Z, Qt::ControlModifier);
    REQUIRE(f.model->lastOperationWorldOffset() == glm::dvec3(0, 0, 0.25));
    QTest::keyClick(f.field(), Qt::Key_Escape);
}

TEST_CASE("Last operation layout stays inside the real viewport across sidebar and resize",
          "[last-operation-panel]") {
    PanelWindow f;
    f.extrude();
    f.open();
    const auto context = f.viewport->context();
    for (const auto size : {QSize(1100, 760), QSize(1440, 900)}) {
        f.window.resize(size);
        f.action("ToggleViewportSidebar")->setChecked(true);
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        REQUIRE(f.viewport->rect().contains(f.panel->geometry()));
        for (auto* label : f.panel->findChildren<QLabel*>()) {
            if (label->isVisible() && label->hasHeightForWidth())
                REQUIRE(label->height() >= label->heightForWidth(label->width()));
        }
        REQUIRE(f.viewport->context() == context);
        REQUIRE(f.model->lastOperationWorldOffset() == glm::dvec3(0, 0, 0.25));
    }
}

TEST_CASE("Panel focus commits only edited axes while navigation preserves last operation",
          "[last-operation-panel]") {
    PanelWindow f;
    f.extrude();
    const glm::dvec3 precise(0.123456789, 0, 0.234567891);
    REQUIRE(f.model->adjustLastOperation(precise));
    QTemporaryDir directory;
    REQUIRE(f.model->saveScene(directory.filePath(QStringLiteral("字段精度.m3dscene"))));
    const auto index = f.model->undoStack()->index();
    const auto* command = f.model->undoStack()->command(index - 1);
    f.open();
    f.viewport->setFocus();
    REQUIRE(f.model->lastOperationWorldOffset() == precise);
    REQUIRE_FALSE(f.model->isModified());
    f.type(".75");
    f.field('X')->setFocus();
    REQUIRE(f.model->lastOperationWorldOffset() == glm::dvec3(precise.x, 0, .75));
    const auto confirmed = f.source();
    for (char axis : {'X', 'Y', 'Z'}) {
        f.open();
        f.type(".9", axis);
        QTest::mouseClick(f.toggle, Qt::LeftButton);
        REQUIRE(f.source() == confirmed);
    }
    f.pointAt(f.viewport);
    const auto camera = f.viewport->editorCameraSnapshot()->state();
    const auto point = f.viewport->rect().center();
    QWheelEvent wheel(point, f.viewport->mapToGlobal(point), {}, QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(f.viewport, &wheel);
    REQUIRE_FALSE(f.viewport->editorCameraSnapshot()->state() == camera);
    REQUIRE(f.action("AdjustLastOperation")->isEnabled());
    f.open();
    REQUIRE(f.model->beginComponentTransform());
    REQUIRE_FALSE(f.field()->isEnabled());
    REQUIRE_FALSE(f.action("AdjustLastOperation")->isEnabled());
    REQUIRE(f.model->finishComponentTransform(false));
    REQUIRE(f.field()->isEnabled());
    REQUIRE(f.model->setEditMode(false));
    REQUIRE_FALSE(f.field()->isEnabled());
    REQUIRE(f.model->setEditMode(true));
    REQUIRE(f.field()->isEnabled());
    REQUIRE(f.model->lastOperationWorldOffset() == glm::dvec3(precise.x, 0, .75));
    REQUIRE(f.model->undoStack()->index() == index);
    REQUIRE(f.model->undoStack()->command(index - 1) == command);
}
