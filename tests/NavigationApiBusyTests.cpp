/*
 * 模块名: NavigationApiBusyTests
 * 功能概述: 检查真实视口导航的活动区间与结束/失焦释放。
 * 对外接口: Catch2 [api-navigation]；依赖关系: Qt Test、ViewportWidget。
 * 输入输出: 导航事件到活动状态断言；异常与错误: 活动粘住即失败。
 * 维护说明: 不模拟业务提交，不读系统光标，不使用固定睡眠。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"
#include "editor/api/EditorApiService.h"
#include "renderer_gl/ViewNavigationWidget.h"
#include "renderer_gl/ViewportWidget.h"

#include <QApplication>
#include <QFocusEvent>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d::renderer_gl;

TEST_CASE("API navigation busy ends after release cancel focus loss and hide", "[api-navigation]") {
    ViewportWidget viewport;
    viewport.resize(640, 480);
    viewport.show();
    REQUIRE(QTest::qWaitForWindowExposed(&viewport));
    REQUIRE(viewport.editorCameraSnapshot());
    QSignalSpy activity(&viewport, &ViewportWidget::navigationActivityChanged);
    const auto point = viewport.rect().center();
    QTest::mousePress(&viewport, Qt::MiddleButton, Qt::NoModifier, point);
    CHECK(viewport.isNavigationActive());
    QTest::mouseRelease(&viewport, Qt::MiddleButton, Qt::NoModifier, point);
    CHECK_FALSE(viewport.isNavigationActive());
    QTest::mousePress(&viewport, Qt::MiddleButton, Qt::NoModifier, point);
    QTest::keyClick(&viewport, Qt::Key_Escape);
    CHECK_FALSE(viewport.isNavigationActive());
    // 业务取消不释放合成按键；每次手势仍须配对 release，避免污染后续用例。
    QTest::mouseRelease(&viewport, Qt::MiddleButton, Qt::NoModifier, point);
    QTest::mousePress(&viewport, Qt::MiddleButton, Qt::NoModifier, point);
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(&viewport, &focusOut);
    CHECK_FALSE(viewport.isNavigationActive());
    QTest::mouseRelease(&viewport, Qt::MiddleButton, Qt::NoModifier, point);
    QTest::mousePress(&viewport, Qt::MiddleButton, Qt::NoModifier, point);
    viewport.hide();
    CHECK_FALSE(viewport.isNavigationActive());
    QTest::mouseRelease(&viewport, Qt::MiddleButton, Qt::NoModifier, point);
    CHECK(activity.count() >= 8);
    CHECK(QApplication::mouseButtons() == Qt::NoButton);
}

TEST_CASE("Pending navigation controls and immediate zoom have bounded busy lifetime",
          "[api-navigation]") {
    ViewportWidget viewport;
    viewport.resize(640, 480);
    viewport.show();
    REQUIRE(QTest::qWaitForWindowExposed(&viewport));
    auto* navigation = viewport.findChild<ViewNavigationWidget*>();
    REQUIRE(navigation);
    const auto point = navigation->controlCenter(ViewNavigationWidget::Control::Zoom);
    QTest::mousePress(navigation, Qt::LeftButton, Qt::NoModifier, point);
    CHECK(viewport.isNavigationActive());
    QTest::mouseRelease(navigation, Qt::LeftButton, Qt::NoModifier, point);
    CHECK_FALSE(viewport.isNavigationActive());
    REQUIRE(viewport.beginViewNavigation());
    CHECK(viewport.isNavigationActive());
    viewport.endViewNavigation();
    CHECK_FALSE(viewport.isNavigationActive());
    QWheelEvent wheel(point, navigation->mapToGlobal(point), {}, QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(navigation, &wheel);
    CHECK_FALSE(viewport.isNavigationActive());
    viewport.hide();
}

TEST_CASE("Window API busy rejects navigation without cancelling user gesture",
          "[api-navigation]") {
    mini3d::editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* viewport = window.findChild<ViewportWidget*>();
    auto* model = window.findChild<mini3d::editor::SceneViewModel*>();
    REQUIRE(viewport);
    REQUIRE(model);
    auto& service = window.apiService();
    const auto state = service.documentState();
    const auto historyCount = model->undoStack()->count();
    QTest::mousePress(viewport, Qt::MiddleButton, Qt::NoModifier, viewport->rect().center());
    auto current = service.currentDocument();
    REQUIRE(current.hasValue());
    CHECK(current.value->busyReasons.contains(QStringLiteral("navigation")));
    auto summary = service.sceneSummary({state.document});
    REQUIRE(summary.error);
    CHECK(summary.error->code == mini3d::editor::api::ErrorCode::Busy);
    CHECK(viewport->isNavigationActive());
    CHECK(model->undoStack()->count() == historyCount);
    QTest::mouseRelease(viewport, Qt::MiddleButton, Qt::NoModifier, viewport->rect().center());
    CHECK_FALSE(viewport->isNavigationActive());
    CHECK(service.sceneSummary({state.document}).hasValue());
    window.hide();
}
