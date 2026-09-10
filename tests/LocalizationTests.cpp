/*
 * 模块名: LocalizationTests
 * 功能概述: 验收简体中文界面、标准对话框、输入和文档兼容性。
 * 对外接口: Catch2 [localization] 用例
 * 依赖关系: Qt Test、MainWindow、临时目录、样例资源
 * 输入输出: 模拟输入与文件到中文文本、历史、布局及可选截图断言。
 * 异常与错误: 译文遗漏、控件截断或数据漂移时测试失败。
 * 维护说明: 不修改用户文件；输入法提交事件不替代真实输入法人工验收。
 */
#include "editor/ChineseUi.h"
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace mini3d;
namespace {
bool hasChinese(const QString& value) {
    return value.contains(QRegularExpression(QStringLiteral("[\\x{4e00}-\\x{9fff}]")));
}
void capture(QWidget& widget, const QString& suffix) {
    const auto prefix = qEnvironmentVariable("MINI3D_TEST_LOCALIZATION_CAPTURE");
    if (!prefix.isEmpty()) {
        REQUIRE(widget.grab().save(prefix + suffix + QStringLiteral(".png")));
    }
}
} // namespace

TEST_CASE("Chinese interface keeps identifiers and fits minimum window", "[localization]") {
    const auto previousLocale = QLocale();
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    const auto restoreLocale = qScopeGuard([previousLocale] {
        QLocale::setDefault(previousLocale);
    });
    editor::MainWindow window;
    editor::initializeChineseUi();
    REQUIRE(qApp->findChildren<QTranslator*>(QStringLiteral("Mini3DChineseTranslator")).size() ==
            1);
    REQUIRE(QLocale().language() == QLocale::English);
    REQUIRE(window.windowTitle().contains(QStringLiteral("未命名")));
    for (auto* action : window.menuBar()->actions()) {
        REQUIRE(hasChinese(action->text()));
    }
    for (const auto& name : {"SceneDock", "InspectorDock", "ConsoleDock"}) {
        const auto* dock = window.findChild<QDockWidget*>(QString::fromLatin1(name));
        REQUIRE(dock != nullptr);
        REQUIRE(hasChinese(dock->windowTitle()));
    }
    for (const auto& name : {"CreateCube", "CreateCamera", "CreateDirectionalLight", "Undo", "Redo",
                             "Duplicate", "Delete", "MoveTool", "OpenScene", "SaveScene"}) {
        const auto* action = window.findChild<QAction*>(QString::fromLatin1(name));
        REQUIRE(action != nullptr);
        REQUIRE(hasChinese(action->text()));
    }
    REQUIRE(window.findChild<QAction*>(QStringLiteral("SaveScene"))->shortcut() ==
            QKeySequence::Save);
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    model->createCamera();
    auto* scroll = window.findChild<QScrollArea*>();
    auto* preview = window.findChild<QPushButton*>(QStringLiteral("PreviewCamera"));
    scroll->ensureWidgetVisible(preview);
    QTest::qWait(100);
    capture(window, QStringLiteral("-normal"));
    window.resize(960, 640);
    QTest::qWait(100);
    REQUIRE(scroll->horizontalScrollBar()->maximum() == 0);
    for (const auto& name :
         {"PositionX", "RotationY", "ScaleZ", "CameraFov", "CameraNear", "CameraFar"}) {
        auto* spin = window.findChild<QDoubleSpinBox*>(QString::fromLatin1(name));
        REQUIRE(spin != nullptr);
        scroll->ensureWidgetVisible(spin);
        REQUIRE(spin->width() >= spin->minimumSizeHint().width());
    }
    scroll->ensureWidgetVisible(preview);
    REQUIRE(scroll->viewport()->rect().contains(
        preview->mapTo(scroll->viewport(), preview->rect().center())));
    auto* exitPreview = window.findChild<QPushButton*>(QStringLiteral("ExitCameraPreview"));
    scroll->ensureWidgetVisible(exitPreview);
    REQUIRE(scroll->viewport()->rect().contains(
        exitPreview->mapTo(scroll->viewport(), exitPreview->rect().center())));
    for (const auto* label : scroll->findChildren<QLabel*>()) {
        if (!label->text().isEmpty() && !label->wordWrap()) {
            REQUIRE(label->width() >= label->minimumSizeHint().width());
        }
    }
    capture(window, QStringLiteral("-minimum"));
    window.hide();
}

TEST_CASE("Qt dialogs and context menus use embedded Chinese with native defaults enabled",
          "[localization]") {
    const auto previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
    const auto restore = qScopeGuard([previous] {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    });
    editor::MainWindow window;
    window.show();
    REQUIRE(QTest::qWaitForWindowExposed(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    model->newScene();
    for (const auto& name : {"OpenScene", "ImportGltf", "SaveSceneAs"}) {
        bool translated = false;
        bool nonNative = false;
        QTimer::singleShot(50, &window, [&] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog) {
                qFatal("Expected a Qt file dialog");
            }
            nonNative = dialog->testOption(QFileDialog::DontUseNativeDialog);
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            translated = buttons && hasChinese(dialog->windowTitle()) &&
                         hasChinese(dialog->labelText(QFileDialog::FileName)) &&
                         hasChinese(buttons->button(QDialogButtonBox::Cancel)->text()) &&
                         hasChinese(buttons
                                        ->button(dialog->acceptMode() == QFileDialog::AcceptSave
                                                     ? QDialogButtonBox::Save
                                                     : QDialogButtonBox::Open)
                                        ->text());
            capture(*dialog, QStringLiteral("-dialog-") + QString::fromLatin1(name));
            dialog->reject();
        });
        window.findChild<QAction*>(QString::fromLatin1(name))->trigger();
        REQUIRE(nonNative);
        REQUIRE(translated);
        REQUIRE_FALSE(model->isModified());
        REQUIRE(model->filePath().isEmpty());
    }
    const auto cube = model->createEntity(core::PrimitiveKind::Cube);
    bool promptTranslated = false;
    QTimer::singleShot(50, &window, [&] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!dialog) {
            qFatal("Expected an unsaved message box");
        }
        promptTranslated = hasChinese(dialog->windowTitle()) && hasChinese(dialog->text());
        for (const auto choice : {QMessageBox::Save, QMessageBox::Discard, QMessageBox::Cancel}) {
            promptTranslated = promptTranslated && hasChinese(dialog->button(choice)->text());
        }
        capture(*dialog, QStringLiteral("-unsaved"));
        dialog->button(QMessageBox::Cancel)->click();
    });
    window.findChild<QAction*>(QStringLiteral("NewScene"))->trigger();
    REQUIRE(promptTranslated);
    REQUIRE(model->scene()->find(cube) != nullptr);
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    const std::unique_ptr<QMenu> context(input->createStandardContextMenu());
    for (const auto* action : context->actions()) {
        if (!action->isSeparator()) {
            REQUIRE(hasChinese(action->text()));
        }
    }
    window.hide();
}

TEST_CASE("Chinese input preserves user names and localized duplicate history", "[localization]") {
    editor::MainWindow window;
    window.show();
    window.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&window));
    auto* model = window.findChild<editor::SceneViewModel*>();
    const auto id = model->createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model->scene()->find(id)->name == "立方体");
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("EntityName"));
    input->setFocus();
    input->selectAll();
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("用户模型 Alpha"));
    QApplication::sendEvent(input, &commit);
    QTest::keyClicks(input, "FW");
    QTest::keyClick(input, Qt::Key_Return);
    REQUIRE(model->scene()->find(id)->name == "用户模型 AlphaFW");
    REQUIRE_FALSE(window.findChild<QAction*>(QStringLiteral("MoveTool"))->isChecked());
    REQUIRE(model->undoStack()->undoText() == QStringLiteral("重命名"));
    model->duplicateSelected();
    const auto copy = model->selection()->selectedEntity();
    REQUIRE(model->scene()->find(copy)->name == "用户模型 AlphaFW 副本");
    model->undo();
    REQUIRE(model->scene()->find(copy) == nullptr);
    model->redo();
    REQUIRE(model->scene()->find(copy)->name == "用户模型 AlphaFW 副本");
    REQUIRE(model->scene()->find(id)->name == "用户模型 AlphaFW");
    window.hide();
}

TEST_CASE("Chinese paths and errors retain document data and underlying diagnostics",
          "[localization]") {
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    REQUIRE(QDir(directory.path()).mkdir(QStringLiteral("中文项目 空格")));
    const auto folder = directory.filePath(QStringLiteral("中文项目 空格/"));
    const auto meshPath = folder + QStringLiteral("外部模型.glb");
    REQUIRE(QFile::copy(QStringLiteral(MINI3D_SAMPLE_DIRECTORY "/Box.glb"), meshPath));
    editor::SceneViewModel model;
    model.newScene();
    const auto imported = model.importGltf(meshPath);
    REQUIRE(imported != 0);
    const auto id = model.createEntity(core::PrimitiveKind::Sphere);
    REQUIRE(model.renameEntity(id, QStringLiteral("Original 原名称")));
    const auto path = folder + QStringLiteral("场景 文件.m3dscene");
    QSignalSpy completed(&model, &editor::SceneViewModel::operationCompleted);
    REQUIRE(model.saveScene(path));
    REQUIRE(completed.last().front().toString().startsWith(QStringLiteral("已保存 ")));
    REQUIRE(model.openScene(path));
    REQUIRE(model.scene()->find(id)->name == "Original 原名称");
    REQUIRE(completed.last().front().toString().startsWith(QStringLiteral("已打开 ")));
    QSignalSpy failed(&model, &editor::SceneViewModel::operationFailed);
    REQUIRE_FALSE(model.renameEntity(id, QString()));
    REQUIRE(failed.last().front().toString() == QStringLiteral("名称不能为空。"));
    REQUIRE_FALSE(model.openScene(folder + QStringLiteral("缺失.m3dscene")));
    REQUIRE(failed.last().front().toString().contains(QStringLiteral("场景文件读写失败")));
    const auto broken = folder + QStringLiteral("损坏.m3dscene");
    QFile file(broken);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write("{broken") == 7);
    file.close();
    REQUIRE_FALSE(model.openScene(broken));
    const auto error = failed.last().front().toString();
    REQUIRE(error.contains(QStringLiteral("场景解析失败")));
    REQUIRE(error.contains(QStringLiteral("json.exception.parse_error")));
    REQUIRE(model.scene()->find(id)->name == "Original 原名称");
    REQUIRE(QFile::rename(meshPath, folder + QStringLiteral("已移走.glb")));
    REQUIRE_FALSE(model.openScene(path));
    REQUIRE(failed.last().front().toString().contains(QStringLiteral("资源文件不存在")));
    REQUIRE(model.scene()->find(id)->name == "Original 原名称");
}
