/*
 * 模块名: SceneAnimationHistoryTests
 * 功能概述: 验证正式动画与GUI/API对象生命周期共用唯一撤销历史。
 * 对外接口: Catch2 [animation][animation-history]。
 * 依赖关系: SceneViewModel、SceneTreeModel、Qt Test；不启动监听或改用户作品。
 * 输入输出: prepared编辑到曲线、稳定ID、保存点及版本通知断言。
 * 异常与错误: 失败和no_change必须保留内容、redo与版本。
 * 维护说明: 只使用隔离内存场景，真实文件边界另有专项测试。
 */
#include "editor/SceneTreeModel.h"
#include "editor/SceneViewModel.h"

#include <QAbstractItemModelTester>
#include <catch2/catch_test_macros.hpp>

using namespace mini3d;
namespace {
namespace api = editor::api;
core::SceneAnimation spin(core::EntityId entity) {
    core::SceneAnimation animation;
    animation.tracks[{entity, core::AnimationChannel::RotationEulerXYZDegrees}] = {
        {{1, {0, 0, 0}, core::AnimationInterpolation::Linear},
         {49, {0, 720.000000001, 0}, core::AnimationInterpolation::Linear}}};
    return animation;
}
template <typename Request>
Request mutation(const editor::SceneViewModel& model, core::EntityId entity) {
    Request request;
    request.document = model.apiDocumentState().document;
    request.expectedDocumentRevision = model.apiDocumentState().documentRevision;
    request.entityId = entity;
    return request;
}
void sameState(const editor::SceneViewModel& model, const api::DocumentState& before) {
    REQUIRE(model.apiDocumentState().document == before.document);
    REQUIRE(model.apiDocumentState().documentRevision == before.documentRevision);
    REQUIRE(model.apiDocumentState().historyRevision == before.historyRevision);
}
} // namespace

TEST_CASE("Animation exact no change preserves redo and the saved point",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    const auto base = model.scene()->find(id)->transform;
    const_cast<QUndoStack*>(model.undoStack())->setClean();
    const auto count = model.undoStack()->count();
    const auto animation = spin(id);
    REQUIRE(model.replaceAnimation(animation));
    REQUIRE(model.undoStack()->count() == count + 1);
    REQUIRE(model.scene()->animation() == animation);
    REQUIRE(model.scene()->find(id)->transform.rotation == base.rotation);
    REQUIRE(model.scene()->find(id)->transform.position == base.position);
    REQUIRE(model.scene()->find(id)->transform.scale == base.scale);
    model.undo();
    REQUIRE(model.undoStack()->isClean());
    const auto* redo = model.undoStack()->command(model.undoStack()->index());
    const auto state = model.apiDocumentState();
    REQUIRE(model.replaceAnimation(model.scene()->animation()));
    REQUIRE(model.undoStack()->canRedo());
    REQUIRE(model.undoStack()->command(model.undoStack()->index()) == redo);
    REQUIRE(model.undoStack()->isClean());
    sameState(model, state);
    model.redo();
    REQUIRE(model.scene()->animation() == animation);
    auto exactEdit = animation;
    exactEdit.tracks.begin()->second.keys.back().value.y += 0.000000001;
    REQUIRE(model.replaceAnimation(exactEdit));
    REQUIRE(model.scene()->animation() == exactEdit);
    model.undo();
    REQUIRE(model.scene()->animation() == animation);
}

TEST_CASE("Animation invalid adjacency and final guard leave history intact",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(spin(id)));
    model.undo();
    const auto state = model.apiDocumentState();
    const auto count = model.undoStack()->count();
    const auto* redo = model.undoStack()->command(model.undoStack()->index());
    auto invalid = spin(id);
    invalid.tracks[{id, core::AnimationChannel::Scale}] = {
        {{1, {1, 1, 1}, core::AnimationInterpolation::Linear},
         {49, {-1, 1, 1}, core::AnimationInterpolation::Constant}}};
    REQUIRE_FALSE(model.replaceAnimation(invalid));
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        return api::ApiError{api::ErrorCode::DeadlineExceeded,
                             QStringLiteral("定向测试取消"),
                             {},
                             api::Recovery::None,
                             model.apiDocumentState()};
    });
    REQUIRE_FALSE(model.replaceAnimation(spin(id)));
    REQUIRE_FALSE(model.replaceAnimation(model.scene()->animation()));
    model.exchangeBeforeCommitGuard({});
    REQUIRE(model.scene()->animation().tracks.empty());
    REQUIRE(model.undoStack()->count() == count);
    REQUIRE(model.undoStack()->command(model.undoStack()->index()) == redo);
    sameState(model, state);
}

TEST_CASE("GUI copy and delete replay stable IDs and independent animation tracks",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    editor::SceneTreeModel tree(model);
    QAbstractItemModelTester tester(&tree, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto child = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(child, parent));
    const auto collection = model.createCollection(QStringLiteral("动画集合"));
    REQUIRE(model.assignEntityToCollection(child, collection));
    const auto animation = spin(child);
    REQUIRE(model.replaceAnimation(animation));
    model.selection()->setSelectedEntity(parent);
    model.duplicateSelected();
    const auto copy = model.selection()->selectedEntity();
    REQUIRE(copy != parent);
    REQUIRE(model.scene()->find(copy)->name == "空对象 副本");
    const auto copiedChild = model.scene()->find(copy)->children.front();
    REQUIRE(model.scene()->animation().tracks.at(
                {copiedChild, core::AnimationChannel::RotationEulerXYZDegrees}) ==
            animation.tracks.begin()->second);
    auto independent = model.scene()->animation();
    independent.tracks.at({copiedChild, core::AnimationChannel::RotationEulerXYZDegrees})
        .keys.back()
        .value.y = 1440;
    REQUIRE(model.replaceAnimation(independent));
    REQUIRE(model.scene()->animation().tracks.at(
                {child, core::AnimationChannel::RotationEulerXYZDegrees}) ==
            animation.tracks.begin()->second);
    model.undo();
    model.undo();
    REQUIRE(model.scene()->find(copy) == nullptr);
    REQUIRE(model.scene()->animation() == animation);
    model.redo();
    REQUIRE(model.scene()->find(copy)->children.front() == copiedChild);
    model.selection()->setSelectedEntity(parent);
    const auto withCopy = model.scene()->animation();
    model.deleteSelected();
    REQUIRE(model.scene()->find(parent) == nullptr);
    REQUIRE_FALSE(model.scene()->animation().tracks.contains(
        {child, core::AnimationChannel::RotationEulerXYZDegrees}));
    REQUIRE(model.scene()->animation().tracks.contains(
        {copiedChild, core::AnimationChannel::RotationEulerXYZDegrees}));
    model.undo();
    REQUIRE(model.scene()->find(parent)->children.front() == child);
    REQUIRE(model.scene()->animation() == withCopy);
    REQUIRE(model.scene()->collections().front().members.contains(child));
    REQUIRE(model.selection()->selectedEntity() == parent);
}

TEST_CASE("API copy preserves selection and deletion restores original tracks",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(spin(id)));
    const auto copied =
        model.duplicateEntityExplicit(mutation<api::EntityDuplicateRequest>(model, id));
    REQUIRE(copied.hasValue());
    const auto copy = copied.value->entityIdMap.at(id);
    REQUIRE(model.selection()->selectedEntity() == id);
    REQUIRE(model.scene()->find(copy)->name == "立方体 Copy");
    const auto copiedAnimation = model.scene()->animation();
    model.undo();
    REQUIRE(model.scene()->find(copy) == nullptr);
    model.redo();
    REQUIRE(model.scene()->animation() == copiedAnimation);
    const auto deleted = model.deleteEntityExplicit(mutation<api::EntityDeleteRequest>(model, id));
    REQUIRE(deleted.hasValue());
    REQUIRE(model.selection()->selectedEntity() == 0);
    REQUIRE(model.scene()->find(id) == nullptr);
    model.undo();
    REQUIRE(model.scene()->find(id) != nullptr);
    REQUIRE(model.scene()->animation() == copiedAnimation);
    REQUIRE(model.selection()->selectedEntity() == id);
}

TEST_CASE("Animated descendant blocks both parent entry points but same parent is no change",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto child = model.createEntity(core::PrimitiveKind::Cube);
    const auto other = model.createEntity(core::PrimitiveKind::Empty);
    REQUIRE(model.setParent(child, parent));
    REQUIRE(model.replaceAnimation(spin(child)));
    const auto state = model.apiDocumentState();
    const auto count = model.undoStack()->count();
    REQUIRE_FALSE(model.setParent(parent, other));
    auto request = mutation<api::EntitySetParentRequest>(model, parent);
    request.parentId = other;
    REQUIRE_FALSE(model.setParentExplicit(request).hasValue());
    request.parentId = 0;
    const auto unchanged = model.setParentExplicit(request);
    REQUIRE(unchanged.hasValue());
    REQUIRE(unchanged.value->status == api::ResultStatus::NoChange);
    REQUIRE(model.setParent(parent, 0));
    REQUIRE(model.undoStack()->count() == count);
    sameState(model, state);
}

TEST_CASE("Prepared history notifications observe the advanced content identity",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    auto previous = model.apiDocumentState().documentRevision;
    int notifications = 0;
    const auto connection =
        QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &model, [&] {
            REQUIRE(model.apiDocumentState().documentRevision == previous + 1);
            previous = model.apiDocumentState().documentRevision;
            ++notifications;
        });
    REQUIRE(model.replaceAnimation(spin(id)));
    model.duplicateSelected();
    model.deleteSelected();
    model.undo();
    model.undo();
    model.undo();
    REQUIRE(notifications == 6);
    QObject::disconnect(connection);
}

TEST_CASE("Animated parent overflow is rejected before either history entry point",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto child = model.createEntity(core::PrimitiveKind::Cube);
    core::Transform transform;
    transform.position = {1e21F, 0, 0};
    REQUIRE(model.setTransform(child, transform));
    core::SceneAnimation animation;
    animation.tracks[{parent, core::AnimationChannel::Scale}] = {
        {{1, {1e19, 1, 1}, core::AnimationInterpolation::Linear}}};
    REQUIRE(model.replaceAnimation(animation));
    const auto state = model.apiDocumentState();
    const auto index = model.undoStack()->index();
    REQUIRE_FALSE(model.setParent(child, parent));
    auto request = mutation<api::EntitySetParentRequest>(model, child);
    request.parentId = parent;
    const auto failed = model.setParentExplicit(request);
    REQUIRE_FALSE(failed.hasValue());
    REQUIRE(failed.error->code == api::ErrorCode::UnsupportedTransform);
    REQUIRE(model.scene()->find(child)->parent == 0);
    REQUIRE(model.scene()->animation() == animation);
    REQUIRE(model.undoStack()->index() == index);
    sameState(model, state);
}

TEST_CASE("Static GUI subtree operations do not inherit the API 2048 entity limit",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    model.newScene();
    std::vector<core::SceneNode> nodes(2050);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        nodes[index].id = index + 1;
        nodes[index].name = "静态对象";
        if (index) {
            nodes[index].parent = 1;
            nodes.front().children.push_back(index + 1);
        }
    }
    auto* scene = const_cast<core::Scene*>(model.scene().get());
    REQUIRE(scene->replaceNodes(nodes));
    model.selection()->setSelectedEntity(1);
    model.duplicateSelected();
    const auto copy = model.selection()->selectedEntity();
    REQUIRE(copy != 1);
    REQUIRE(model.scene()->nodes().size() == 4100);
    model.deleteSelected();
    REQUIRE(model.scene()->nodes().size() == 2050);
    model.undo();
    REQUIRE(model.scene()->find(copy) != nullptr);
    REQUIRE(model.scene()->nodes().size() == 4100);
}

TEST_CASE("All public prepared history notifications expose committed state and valid selection",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    editor::SceneTreeModel tree(model);
    QAbstractItemModelTester tester(&tree, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.newScene();
    const auto id = model.createEntity(core::PrimitiveKind::Cube);
    auto expected = model.apiDocumentState().documentRevision + 1;
    int callbacks = 0;
    const auto observe = [&] {
        REQUIRE(model.apiDocumentState().documentRevision == expected);
        const auto selected = model.selection()->selectedEntity();
        REQUIRE((selected == 0 || model.scene()->find(selected) != nullptr));
        ++callbacks;
    };
    QObject::connect(&model, &editor::SceneViewModel::documentChanged, &model, observe);
    QObject::connect(&model, &editor::SceneViewModel::lastOperationChanged, &model, observe);
    QObject::connect(&model, &editor::SceneViewModel::apiStateChanged, &model, observe);
    QObject::connect(&model, &editor::SceneViewModel::structureChanged, &model, observe);
    QObject::connect(&tree, &QAbstractItemModel::modelReset, &model, observe);
    REQUIRE(model.replaceAnimation(spin(id)));
    ++expected;
    model.duplicateSelected();
    ++expected;
    model.deleteSelected();
    ++expected;
    model.undo();
    ++expected;
    model.undo();
    ++expected;
    model.undo();
    REQUIRE(callbacks >= 12);
}

TEST_CASE("Animation history preflight deduplicates times and inspects only changed intervals",
          "[animation][animation-history]") {
    editor::SceneViewModel model;
    model.newScene();
    std::vector<core::SceneNode> nodes(10000);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        nodes[index].id = index + 1;
        nodes[index].name = "数值节点";
    }
    auto* scene = const_cast<core::Scene*>(model.scene().get());
    core::SceneAnimation animation;
    SECTION("Four unique frames across two changed tracks fit 40000 visits") {
        REQUIRE(scene->replaceNodes(nodes));
        for (const auto id : {core::EntityId{1}, core::EntityId{2}})
            for (std::uint32_t frame = 1; frame <= 4; ++frame)
                animation.tracks[{id, core::AnimationChannel::Position}].keys.push_back(
                    {frame, {double(frame), 0, 0}, core::AnimationInterpolation::Linear});
        REQUIRE(model.replaceAnimation(animation));
        model.undo();
        const auto state = model.apiDocumentState();
        const auto* redo = model.undoStack()->command(model.undoStack()->index());
        animation.tracks.begin()->second.keys.push_back(
            {5, {5, 0, 0}, core::AnimationInterpolation::Linear});
        REQUIRE_FALSE(model.replaceAnimation(animation));
        REQUIRE(scene->animation().tracks.empty());
        REQUIRE(model.undoStack()->command(model.undoStack()->index()) == redo);
        REQUIRE(model.undoStack()->canRedo());
        sameState(model, state);
    }
    SECTION("One changed key does not sample all one hundred unchanged old times") {
        for (std::uint32_t frame = 1; frame <= 100; ++frame)
            animation.tracks[{1, core::AnimationChannel::Position}].keys.push_back(
                {frame, {double(frame), 0, 0}, core::AnimationInterpolation::Linear});
        REQUIRE(scene->replaceNodes(nodes, {}, {}, animation));
        auto candidate = animation;
        candidate.tracks.begin()->second.keys[2].value.y = 2;
        REQUIRE(model.replaceAnimation(candidate));
        REQUIRE(scene->animation() == candidate);
        model.undo();
        REQUIRE(scene->animation() == animation);
    }
}
