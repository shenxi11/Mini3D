/*
 * 模块名: CollectionEditorTests
 * 功能概述: 验证集合真实文件IO、单对象归属、父变换与删除/复制/历史联动。
 * 对外接口: Catch2 [collections-editor]；依赖关系: SceneViewModel、Qt窗口/临时目录。
 * 输入输出: 组织意图到稳定集合与已有对象树，保存重开到相同归属。
 * 异常与错误: 无效名称/目标不损坏保存点，删集合不删对象。
 * 维护说明: 集合行不代表实体，多对象变换不在本项。
 */
#include "editor/MainWindow.h"
#include "editor/SceneViewModel.h"

#include <QApplication>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>

using namespace mini3d;
TEST_CASE("Collections preserve parent transforms and round trip through actual scene IO",
          "[collections-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(cube, parent));
    core::Transform transform;
    transform.position = {3, 1, -2};
    transform.scale = {2, 1, .5};
    REQUIRE(model.setTransform(parent, transform));
    const auto world = model.scene()->worldMatrix(cube);
    const auto local = model.scene()->find(cube)->transform.localMatrix();
    const auto group = model.createCollection(QStringLiteral("外壳集合"));
    REQUIRE(group != 0);
    const auto index = model.undoStack()->index();
    REQUIRE(model.assignEntityToCollection(cube, group));
    REQUIRE(model.undoStack()->index() == index + 1);
    REQUIRE(model.assignEntityToCollection(cube, group));
    REQUIRE(model.undoStack()->index() == index + 1);
    REQUIRE(model.scene()->worldMatrix(cube) == world);
    REQUIRE(model.scene()->find(cube)->parent == parent);
    REQUIRE(model.scene()->find(cube)->transform.localMatrix() == local);
    REQUIRE(model.setCollectionVisible(group, false));
    REQUIRE_FALSE(model.scene()->isVisible(cube));
    REQUIRE(model.scene()->find(cube)->visible);
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("集合往返.m3dscene"));
    REQUIRE(model.saveScene(path));
    const auto savedIndex = model.undoStack()->index();
    REQUIRE_FALSE(model.renameCollection(group, QStringLiteral("  ")));
    REQUIRE_FALSE(model.assignEntityToCollection(cube, 999999));
    REQUIRE(model.undoStack()->index() == savedIndex);
    REQUIRE_FALSE(model.isModified());
    editor::SceneViewModel reopened;
    REQUIRE(reopened.openScene(path));
    REQUIRE(reopened.scene()->collections() == model.scene()->collections());
    REQUIRE_FALSE(reopened.scene()->isVisible(cube));
    REQUIRE(reopened.scene()->worldMatrix(cube) == world);
    REQUIRE(model.removeCollection(group));
    REQUIRE(model.scene()->find(cube));
    REQUIRE(model.scene()->isVisible(cube));
    model.undo();
    REQUIRE(model.scene()->collections().front().members.contains(cube));
    REQUIRE_FALSE(model.scene()->isVisible(cube));
    REQUIRE_FALSE(model.isModified());
}

TEST_CASE("Object duplicate delete and Undo restore collection memberships without reparenting",
          "[collections-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    const auto group = model.createCollection(QStringLiteral("对象"));
    REQUIRE(group != 0);
    REQUIRE(model.assignEntityToCollection(cube, group));
    model.duplicateSelected();
    const auto copy = model.selection()->selectedEntity();
    REQUIRE(copy != cube);
    REQUIRE(model.scene()->collections().front().members == std::set<core::EntityId>{cube, copy});
    REQUIRE(model.scene()->find(copy)->parent == model.scene()->find(cube)->parent);
    model.deleteSelected();
    REQUIRE_FALSE(model.scene()->find(copy));
    REQUIRE(model.scene()->collections().front().members == std::set<core::EntityId>{cube});
    model.undo();
    REQUIRE(model.scene()->find(copy));
    REQUIRE(model.scene()->collections().front().members.contains(copy));
    model.undo();
    REQUIRE_FALSE(model.scene()->find(copy));
    REQUIRE_FALSE(model.scene()->collections().front().members.contains(copy));
    model.redo();
    REQUIRE(model.scene()->find(copy));
    REQUIRE(model.scene()->collections().front().members.contains(copy));
    REQUIRE(model.removeCollection(group));
    REQUIRE(model.scene()->find(cube));
    REQUIRE(model.scene()->find(copy));
    model.undo();
    REQUIRE(model.scene()->collections().front().members.contains(copy));
}

TEST_CASE("Invalid collection files preserve an active component preview and save point",
          "[collections-editor]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    const auto group = model.createCollection(QStringLiteral("原集合"));
    REQUIRE(model.assignEntityToCollection(cube, group));
    REQUIRE(model.setEditMode(true));
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("原工程.m3dscene"));
    REQUIRE(model.saveScene(path));
    QFile original(path);
    REQUIRE(original.open(QIODevice::ReadOnly));
    const auto document = QJsonDocument::fromJson(original.readAll()).object();
    original.close();
    const auto collections = model.scene()->collections();
    const auto source = model.scene()->geometrySnapshot(cube)->content();
    const auto selection = model.componentSelection();
    const auto index = model.undoStack()->index();
    const auto clean = model.undoStack()->cleanIndex();
    REQUIRE(model.beginComponentTransform());
    REQUIRE(model.previewComponentTransform(glm::translate(glm::dmat4(1), glm::dvec3(1, 0, 0))));
    const auto preview = model.componentPreview();
    REQUIRE(preview);
    for (const auto members :
         {QJsonArray{999999}, QJsonArray{static_cast<qint64>(cube), static_cast<qint64>(cube)}}) {
        auto invalid = document;
        auto list = invalid["collections"].toArray();
        auto collection = list[0].toObject();
        collection["members"] = members;
        list[0] = collection;
        invalid["collections"] = list;
        const auto badPath = directory.filePath("invalid.m3dscene");
        QFile file(badPath);
        REQUIRE(file.open(QIODevice::WriteOnly));
        const auto bytes = QJsonDocument(invalid).toJson();
        REQUIRE(file.write(bytes) == bytes.size());
        file.close();
        REQUIRE_FALSE(model.openScene(badPath));
        REQUIRE(model.hasComponentTransform());
        REQUIRE(model.componentPreview() == preview);
        REQUIRE(model.scene()->geometrySnapshot(cube)->content() == source);
        REQUIRE(model.scene()->collections() == collections);
        REQUIRE(model.componentSelection() == selection);
        REQUIRE(model.undoStack()->index() == index);
        REQUIRE(model.undoStack()->cleanIndex() == clean);
        REQUIRE(model.filePath() == path);
    }
    REQUIRE(model.finishComponentTransform(false));
    REQUIRE_FALSE(model.isModified());
}

TEST_CASE("Collection member tree creates groups selects real entities and moves membership",
          "[collections-editor]") {
    editor::MainWindow window;
    auto* model = window.findChild<editor::SceneViewModel*>();
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("CollectionTree"));
    auto* create = window.findChild<QPushButton*>(QStringLiteral("CollectionCreateButton"));
    auto* assign = window.findChild<QPushButton*>(QStringLiteral("CollectionAssignButton"));
    auto* unlink = window.findChild<QPushButton*>(QStringLiteral("CollectionUnassignButton"));
    REQUIRE(model);
    REQUIRE(tree);
    REQUIRE(create);
    REQUIRE(assign);
    REQUIRE(unlink);
    model->newScene();
    const auto first = model->createEntity(core::PrimitiveKind::Cube);
    const auto second = model->createEntity(core::PrimitiveKind::Cube);
    const auto group = model->createCollection(QStringLiteral("第一组"));
    REQUIRE(model->assignEntityToCollection(first, group));
    QTimer::singleShot(0, &window, [] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (dialog) {
            dialog->setTextValue(QStringLiteral("界面集合"));
            dialog->accept();
        }
    });
    create->click();
    QApplication::processEvents();
    REQUIRE(tree->topLevelItemCount() == 2);
    const auto other = model->scene()->collections().back().id;
    REQUIRE(model->assignEntityToCollection(second, other));
    QApplication::processEvents();
    tree->topLevelItem(1)->setText(0, QStringLiteral(" 界面集合 "));
    QApplication::processEvents();
    REQUIRE(tree->topLevelItem(1)->text(0) == QStringLiteral("界面集合"));
    tree->setCurrentItem(tree->topLevelItem(1)->child(0));
    REQUIRE(model->selection()->selectedEntity() == second);
    tree->topLevelItem(1)->setCheckState(0, Qt::Unchecked);
    QApplication::processEvents();
    REQUIRE_FALSE(model->scene()->isVisible(second));
    REQUIRE(model->scene()->find(second)->visible);
    model->selection()->setSelectedEntity(first);
    QApplication::processEvents();
    REQUIRE(assign->isEnabled());
    assign->click();
    QApplication::processEvents();
    REQUIRE(model->scene()->collections().front().members.empty());
    REQUIRE(model->scene()->collections().back().members.contains(first));
    REQUIRE(model->scene()->find(first)->parent == 0);
    REQUIRE(unlink->isEnabled());
    unlink->click();
    QApplication::processEvents();
    REQUIRE_FALSE(model->scene()->collections().back().members.contains(first));
    REQUIRE(model->scene()->find(first));
}
