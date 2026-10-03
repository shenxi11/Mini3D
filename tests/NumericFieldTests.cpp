/*
 * 模块名: NumericFieldTests
 * 功能概述: 验证数值字段的提交、取消、非法保留和一次拖动一条历史。
 * 对外接口: Catch2 [numeric-field] 用例
 * 依赖关系: Qt Test、MainWindow、CommitSpinBox
 * 输入输出: 文本/鼠标事件到业务数据、字段错误与历史断言。
 * 异常与错误: 取消残留、非法丢失或重复历史则失败。
 * 维护说明: 测试使用真实控件，不读用户工程或修改用户偏好。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/workbench/CommitSpinBox.h"

#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTabWidget>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
void typeNumber(editor::CommitSpinBox* field, const char* text) {
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, text);
}
} // namespace

TEST_CASE("Number fields commit once cancel and preserve invalid text and domain errors",
          "[numeric-field]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    auto* field = window.findChild<editor::CommitSpinBox*>(QStringLiteral("PositionX"));
    auto* input = field->findChild<QLineEdit*>();
    const auto before = model->undoStack()->count();
    typeNumber(field, "2.5");
    REQUIRE(model->scene()->find(id)->transform.position.x == 0);
    QTest::keyClick(field, Qt::Key_Escape);
    REQUIRE(field->value() == 0);
    REQUIRE(model->undoStack()->count() == before);
    typeNumber(field, "2.5");
    QTest::keyClick(input, Qt::Key_Return);
    REQUIRE(model->scene()->find(id)->transform.position.x == 2.5F);
    REQUIRE(model->undoStack()->count() == before + 1);
    typeNumber(field, "bad");
    QTest::keyClick(field, Qt::Key_Return);
    REQUIRE(input->text().contains(QStringLiteral("bad")));
    REQUIRE_FALSE(field->validationMessage().isEmpty());
    REQUIRE(model->scene()->find(id)->transform.position.x == 2.5F);
    REQUIRE(model->undoStack()->count() == before + 1);
    QTest::keyClick(field, Qt::Key_Escape);
    REQUIRE(field->validationMessage().isEmpty());
    REQUIRE_FALSE(input->text().contains(QStringLiteral("bad")));
    for (const auto* invalid : {"10001", "nan", "1e999"}) {
        typeNumber(field, invalid);
        QTest::keyClick(input, Qt::Key_Return);
        REQUIRE_FALSE(field->validationMessage().isEmpty());
        REQUIRE(field->cleanText() == QString::fromLatin1(invalid));
        REQUIRE(model->scene()->find(id)->transform.position.x == 2.5F);
        REQUIRE(model->undoStack()->count() == before + 1);
        QTest::keyClick(input, Qt::Key_Escape);
    }
    auto* scale = window.findChild<editor::CommitSpinBox*>(QStringLiteral("ScaleX"));
    typeNumber(scale, "0");
    QTest::keyClick(scale, Qt::Key_Return);
    REQUIRE(scale->cleanText() == QStringLiteral("0"));
    REQUIRE_FALSE(scale->validationMessage().isEmpty());
    REQUIRE(model->scene()->find(id)->transform.scale.x == 1);
    REQUIRE(model->undoStack()->count() == before + 1);
    QTest::keyClick(scale, Qt::Key_Escape);
    REQUIRE(scale->value() == 1);
    model->undo();
    REQUIRE(model->scene()->find(id)->transform.position.x == 0);
    window.hide();
}

TEST_CASE("Alt number scrubbing commits one change and Escape or deactivation leaves no change",
          "[numeric-field]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    auto* field = window.findChild<editor::CommitSpinBox*>(QStringLiteral("PositionX"));
    auto* input = field->findChild<QLineEdit*>();
    const auto start = input->rect().center();
    const auto before = model->undoStack()->count();
    const auto move = [&](int pixels) {
        const auto point = start + QPoint(pixels, 0);
        QMouseEvent event(QEvent::MouseMove, point, input->mapToGlobal(point), Qt::NoButton,
                          Qt::LeftButton, Qt::AltModifier);
        QApplication::sendEvent(input, &event);
    };
    QTest::mousePress(input, Qt::LeftButton, Qt::AltModifier, start);
    move(10);
    move(50);
    REQUIRE(model->scene()->find(id)->transform.position.x == 0);
    REQUIRE(model->undoStack()->count() == before);
    QTest::mouseRelease(input, Qt::LeftButton, Qt::AltModifier, start + QPoint(50, 0));
    REQUIRE(model->scene()->find(id)->transform.position.x == 0.5F);
    REQUIRE(model->undoStack()->count() == before + 1);
    QTest::mousePress(input, Qt::LeftButton, Qt::AltModifier, start);
    move(90);
    QTest::keyClick(field, Qt::Key_Escape);
    QTest::mouseRelease(input, Qt::LeftButton, Qt::AltModifier, start);
    REQUIRE(model->scene()->find(id)->transform.position.x == 0.5F);
    REQUIRE(model->undoStack()->count() == before + 1);
    QTest::mousePress(input, Qt::LeftButton, Qt::AltModifier, start);
    move(90);
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(field, &deactivate);
    REQUIRE(QWidget::mouseGrabber() != input);
    // 失活已结束业务拖动，但 Qt Test 的合成按键仍需配对释放，避免污染后续鼠标组合。
    QTest::mouseRelease(input, Qt::LeftButton, Qt::AltModifier, start);
    REQUIRE(model->scene()->find(id)->transform.position.x == 0.5F);
    model->undo();
    REQUIRE(model->scene()->find(id)->transform.position.x == 0);
    window.hide();
}

TEST_CASE("Camera domain validation keeps the submitted clipping value without altering history",
          "[numeric-field]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    const auto id = model->createCamera();
    window.findChild<QTabWidget*>(QStringLiteral("PropertyPages"))->setCurrentIndex(1);
    auto* near = window.findChild<editor::CommitSpinBox*>(QStringLiteral("CameraNear"));
    const auto before = model->undoStack()->count();
    typeNumber(near, "2000");
    QTest::keyClick(near, Qt::Key_Return);
    REQUIRE(near->cleanText() == QStringLiteral("2000"));
    REQUIRE_FALSE(near->validationMessage().isEmpty());
    REQUIRE(model->scene()->find(id)->camera->nearPlane == 0.1F);
    REQUIRE(model->undoStack()->count() == before);
    QTest::keyClick(near, Qt::Key_Escape);
    REQUIRE(near->value() == 0.1);
    window.hide();
}
