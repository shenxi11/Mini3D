/*
 * 模块名: AnimationSessionTests
 * 功能概述: 验证独立动画状态、当前时间候选、历史恢复、草稿许可及导航原子性。
 * 对外接口: Catch2 [animation][animation-session] 定向测试。
 * 依赖关系: SceneViewModel、Qt Test；只使用独立内存场景。
 * 输入输出: 用户/API意图到完整身份、正式内容、历史与版本断言。
 * 异常与错误: 故障不能发布部分姿态，历史恢复失败预检时仍应正常恢复内容。
 * 维护说明: 时钟用真实Qt事件循环；不使用用户模型、不写场景文件、不启动监听。
 */
#include "editor/SceneViewModel.h"
#include "renderer_gl/RayCaster.h"

#include <QSignalSpy>
#include <QElapsedTimer>
#include <QTest>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

using namespace mini3d;
namespace {
using Mode = editor::AnimationMode;
namespace api = editor::api;
core::SceneAnimation translation(core::EntityId entity, double end = 48) {
    core::SceneAnimation animation;
    animation.settings.endFrame = 49;
    animation.tracks[{entity, core::AnimationChannel::Position}] = {
        {{1, {0, 0, 0}, core::AnimationInterpolation::Linear},
         {49, {end, 0, 0}, core::AnimationInterpolation::Linear}}};
    return animation;
}
void unchangedDocument(const editor::SceneViewModel& model, const api::DocumentState& state) {
    REQUIRE(model.apiDocumentState().document == state.document);
    REQUIRE(model.apiDocumentState().documentRevision == state.documentRevision);
    REQUIRE(model.apiDocumentState().historyRevision == state.historyRevision);
}
void validIdentity(const editor::SceneViewModel& model) {
    const auto pose = model.installedAnimationPose();
    REQUIRE(pose);
    REQUIRE(pose->identity.instanceId == model.apiDocumentState().document.instanceId);
    REQUIRE(pose->identity.documentId == model.apiDocumentState().document.documentId);
    REQUIRE(pose->identity.sourceRevision == model.apiDocumentState().documentRevision);
    REQUIRE(pose->identity.evaluationId == model.animationEvaluationId());
    REQUIRE(pose->identity.frame == model.animationFrame());
    REQUIRE(pose->identity.mode == model.animationMode());
    if (pose->coverage == renderer_gl::PoseStateCoverage::CompleteSceneJ1) {
        REQUIRE(pose->sceneState);
        const auto& source = pose->sceneState->source;
        REQUIRE(core::isSourceIdentityValid(source));
        REQUIRE(source.documentUuid == model.scene()->documentUuid());
        REQUIRE(source.contentRevision == pose->identity.sourceRevision);
        REQUIRE(source.authorContextIdentity.contentRevision == source.contentRevision);
        const core::LiveSourceHandle live{pose->identity.instanceId.toStdString(),
                                          pose->identity.documentId.toStdString()};
        REQUIRE(std::get<core::LiveSourceHandle>(source.sourceHandle) == live);
        REQUIRE(source.authorContextIdentity.revisionDomain == live);
        REQUIRE(source.frameTime == pose->identity.frame);
        REQUIRE(source.evaluationId == pose->identity.evaluationId);
        REQUIRE(pose->sceneState->content->geometry == pose->geometry->sceneGeometry);
    }
}
template <typename Request>
Request mutation(const editor::SceneViewModel& model, core::EntityId entity) {
    Request request;
    request.document = model.apiDocumentState().document;
    request.expectedDocumentRevision = model.apiDocumentState().documentRevision;
    request.entityId = entity;
    return request;
}

struct JointDisplaySession {
    editor::SceneViewModel model;
    core::EntityId cube = 0, sun = 0;
    core::MaterialId material{31};
    core::SceneAnimation animation;

    JointDisplaySession() {
        model.newScene();
        cube = model.createEntity(core::PrimitiveKind::Cube);
        sun = model.createEntity(core::PrimitiveKind::Empty);
        auto scene = std::const_pointer_cast<core::Scene>(model.scene());
        core::MaterialDefinition definition;
        definition.id = material;
        definition.parameters.roughness = .7;
        core::SceneMaterialData materials;
        materials.assets.emplace(core::StableAssetId{31}, core::SceneAssetDefinition{
            core::StableAssetId{31}, core::SceneAssetKind::Material, definition});
        std::string error;
        auto prepared = scene->prepareMaterialData(materials, error);
        REQUIRE(prepared);
        REQUIRE(scene->installPreparedMaterialData(*prepared));
        REQUIRE(scene->setMaterialSlots(cube, {{core::MaterialSlotId{51}, material}}));
        REQUIRE(scene->setSun(sun, {3, {1, 1, 1}}));
        core::Transform base;
        base.position.x = .25F;
        REQUIRE(scene->setTransform(cube, base));
        animation.properties.rationalFps = core::RationalFps{24, 1};
        core::ScalarCurve position{core::CurveId{1}, core::PropertyId::Position,
            core::PropertyComponent::X, {{core::KeyId{1}, 1, 0}, {core::KeyId{2}, 24, 2}}};
        core::ScalarCurve roughness{core::CurveId{2}, core::PropertyId::PbrRoughness,
            core::PropertyComponent::Scalar, {{core::KeyId{3}, 1, .2}, {core::KeyId{4}, 24, .8}}};
        core::ScalarCurve irradiance{core::CurveId{3}, core::PropertyId::SunIrradiance,
            core::PropertyComponent::Scalar, {{core::KeyId{5}, 1, 1}, {core::KeyId{6}, 24, 2}}};
        animation.properties.actions = {{core::ActionId{1}, {
            {core::ActionSlotId{1}, core::PropertyOwnerKind::Entity, {position}},
            {core::ActionSlotId{2}, core::PropertyOwnerKind::Material, {roughness}},
            {core::ActionSlotId{3}, core::PropertyOwnerKind::Entity, {irradiance}}}}};
        animation.properties.bindings = {
            {scene->documentUuid(), core::EntityOwner{cube}, core::ActionId{1}, core::ActionSlotId{1}},
            {scene->documentUuid(), core::MaterialOwner{material}, core::ActionId{1}, core::ActionSlotId{2}},
            {scene->documentUuid(), core::EntityOwner{sun}, core::ActionId{1}, core::ActionSlotId{3}}};
        REQUIRE(model.replaceAnimation(animation));
        model.selection()->setSelectedEntity(cube);
    }

    std::shared_ptr<const renderer_gl::InstalledPose> display() {
        QString error;
        std::shared_ptr<const renderer_gl::InstalledPose> result;
        INFO(error.toStdString());
        REQUIRE(model.prepareDisplayPose(result, error));
        REQUIRE(result);
        REQUIRE(renderer_gl::ScenePoseView(*model.scene(), result.get()).isComplete());
        return result;
    }
};
} // namespace

TEST_CASE("Animation control no change validates the final guard and preserves counters",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(entity);
    const auto state = model.apiDocumentState();
    const auto revision = model.animationSessionRevision();
    REQUIRE(model.setAnimationPreview(false));
    REQUIRE(model.pauseAnimation());
    REQUIRE(model.setAnimationLoop(false));
    REQUIRE_FALSE(model.setAnimationFrame(1));
    REQUIRE_FALSE(model.playAnimation());
    REQUIRE(model.animationSessionRevision() == revision);
    unchangedDocument(model, state);
    REQUIRE(model.setAnimationPreview(true));
    const auto pausedRevision = model.animationSessionRevision();
    const auto evaluation = model.animationEvaluationId();
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(1));
    REQUIRE(model.pauseAnimation());
    REQUIRE(model.animationSessionRevision() == pausedRevision);
    REQUIRE(model.animationEvaluationId() == evaluation);
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        return api::ApiError{api::ErrorCode::DeadlineExceeded, QStringLiteral("测试到期"), {},
                             api::Recovery::None, model.apiDocumentState()};
    });
    REQUIRE_FALSE(model.setAnimationPreview(true));
    REQUIRE_FALSE(model.pauseAnimation());
    REQUIRE_FALSE(model.setAnimationFrame(1));
    REQUIRE(model.animationEvaluationId() == evaluation);
    REQUIRE(model.animationSessionRevision() == pausedRevision);
    unchangedDocument(model, state);
}

TEST_CASE("Animation preview requires Object and no existing transform gesture",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    model.beginTransformEdit(entity);
    REQUIRE_FALSE(model.setAnimationPreview(true));
    model.cancelTransformEdit();
    REQUIRE(model.setEditMode(true));
    REQUIRE_FALSE(model.setAnimationPreview(true));
    REQUIRE(model.setEditMode(false));
    REQUIRE(model.setAnimationPreview(true));
    // 暂停进入编辑模式按合同显式关闭动画预览。
    REQUIRE(model.setEditMode(true));
    REQUIRE(model.animationMode() == Mode::Base);
    REQUIRE_FALSE(model.installedAnimationPose());
}

TEST_CASE("Animation ticks share geometry and never dirty content history or session",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto child = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setParent(child, parent));
    REQUIRE(model.replaceAnimation(translation(parent)));
    const_cast<QUndoStack*>(model.undoStack())->setClean();
    REQUIRE(model.setAnimationPreview(true));
    const auto geometry = model.installedAnimationPose()->geometry;
    const auto base = model.scene()->find(parent)->transform;
    QSignalSpy sceneNotifications(&model, &editor::SceneViewModel::sceneChanged);
    REQUIRE(model.playAnimation());
    const auto document = model.apiDocumentState();
    const auto session = model.animationSessionRevision();
    const auto evaluation = model.animationEvaluationId();
    QTest::qWait(55);
    REQUIRE(model.animationMode() == Mode::Playing);
    REQUIRE(model.animationFrame() > 1);
    REQUIRE(model.animationEvaluationId() > evaluation);
    REQUIRE(model.animationSessionRevision() == session);
    REQUIRE(model.installedAnimationPose()->geometry == geometry);
    REQUIRE(model.installedAnimationPose()->numerics->find(child)->world[3].x > 0);
    REQUIRE(model.scene()->find(parent)->transform.position == base.position);
    REQUIRE_FALSE(model.isModified());
    REQUIRE(sceneNotifications.count() == 0);
    unchangedDocument(model, document);
    REQUIRE(model.pauseAnimation());
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE(model.animationFrame() != std::floor(model.animationFrame()));
    REQUIRE(model.animationSessionRevision() == session + 1);
    validIdentity(model);
}

TEST_CASE("Animation endpoint hidden stop and loop use session changes with no content writes",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    auto animation = translation(entity);
    animation.settings = {120, 1, 2};
    animation.properties.loopPolicy = core::AnimationLoopPolicy::LegacyHalfOpen;
    REQUIRE(model.replaceAnimation(animation));
    REQUIRE(model.setAnimationPreview(true));
    const auto document = model.apiDocumentState();
    REQUIRE(model.playAnimation());
    const auto playingRevision = model.animationSessionRevision();
    QTest::qWait(45);
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE(model.animationFrame() == 2);
    REQUIRE(model.animationSessionRevision() == playingRevision + 1);
    REQUIRE(model.setAnimationLoop(true));
    REQUIRE(model.playAnimation());
    const auto loopRevision = model.animationSessionRevision();
    QTest::qWait(45);
    REQUIRE(model.animationMode() == Mode::Playing);
    REQUIRE(model.animationFrame() >= 1);
    REQUIRE(model.animationFrame() < 2);
    REQUIRE(model.animationSessionRevision() == loopRevision);
    model.suspendAnimation();
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE(model.animationSessionRevision() == loopRevision + 1);
    const auto stoppedFrame = model.animationFrame();
    QTest::qWait(30);
    REQUIRE(model.animationFrame() == stoppedFrame);
    unchangedDocument(model, document);
    animation.settings = {24, 1, 1};
    REQUIRE(model.replaceAnimation(animation));
    const auto singleRevision = model.animationSessionRevision();
    const auto singleEvaluation = model.animationEvaluationId();
    REQUIRE(model.playAnimation());
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE(model.animationSessionRevision() == singleRevision);
    REQUIRE(model.animationEvaluationId() == singleEvaluation);
}

TEST_CASE("Animation history after seek evaluates the current time before notifications",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(13));
    REQUIRE(model.replaceAnimation(translation(entity, 96)));
    REQUIRE(model.installedAnimationPose()->numerics->find(entity)->local.position.x == 24);
    REQUIRE(model.setAnimationFrame(25));
    int notifications = 0;
    QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &model, [&] {
        ++notifications;
        validIdentity(model);
        REQUIRE(model.animationFrame() == 25);
    });
    model.undo();
    REQUIRE(model.installedAnimationPose()->numerics->find(entity)->local.position.x == 24);
    model.redo();
    REQUIRE(model.installedAnimationPose()->numerics->find(entity)->local.position.x == 48);
    REQUIRE(notifications == 2);
}

TEST_CASE("Animation rename shares numerics and rejects mixed paused transform patches",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(25));
    const auto pose = model.installedAnimationPose();
    const auto session = model.animationSessionRevision();
    REQUIRE(model.renameEntity(entity, QStringLiteral("旋翼名称")));
    REQUIRE(model.installedAnimationPose()->numerics == pose->numerics);
    REQUIRE(model.installedAnimationPose()->geometry == pose->geometry);
    REQUIRE(model.animationSessionRevision() == session);
    validIdentity(model);
    auto request = mutation<api::EntityUpdateRequest>(model, entity);
    request.changes.name = QStringLiteral("不应发布");
    request.changes.transform = model.scene()->find(entity)->transform;
    const auto state = model.apiDocumentState();
    const auto name = model.scene()->find(entity)->name;
    const auto result = model.updateEntityExplicit(request);
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->code == api::ErrorCode::Busy);
    REQUIRE(model.scene()->find(entity)->name == name);
    unchangedDocument(model, state);
    model.undo();
    validIdentity(model);
    REQUIRE(model.installedAnimationPose()->numerics == pose->numerics);
}

TEST_CASE("Animation copy delete and current frame restore reuse retained geometry",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(entity));
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(13));
    const auto originalPose = model.installedAnimationPose();
    REQUIRE(originalPose->sceneState);
    const auto originalState = *originalPose->sceneState;
    const auto originalStage = originalState.content->geometryStages.at(entity).identity;
    const auto content = model.installedAnimationPose()->geometry->entries.at(entity).content;
    const auto geometryRevision = model.scene()->editableMesh(model.scene()->find(entity)->editableMesh)->evaluationRevision;
    int preparedIsolation = 0;
    QObject::connect(&model, &editor::SceneViewModel::structureAboutToChange, &model, [&] {
        ++preparedIsolation;
        REQUIRE(model.animationMode() == Mode::PreviewPaused);
        REQUIRE_FALSE(model.installedAnimationPose());
    });
    model.duplicateSelected();
    const auto copy = model.selection()->selectedEntity();
    REQUIRE(copy != entity);
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(copy).content == content);
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(copy).evaluationRevision ==
            model.scene()->editableMesh(model.scene()->find(copy)->editableMesh)->evaluationRevision);
    REQUIRE(model.installedAnimationPose()->sceneState);
    REQUIRE(model.installedAnimationPose()->sceneState->content->geometryStages.at(copy).identity.contentVersion ==
            model.installedAnimationPose()->geometry->entries.at(copy).evaluationRevision);
    REQUIRE(model.setAnimationFrame(25));
    REQUIRE(model.installedAnimationPose()->sceneState->content->geometryStages.at(copy).identity.contentVersion ==
            model.installedAnimationPose()->geometry->entries.at(copy).evaluationRevision);
    model.deleteSelected();
    REQUIRE_FALSE(model.scene()->find(copy));
    REQUIRE_FALSE(model.installedAnimationPose()->numerics->find(copy));
    model.undo();
    REQUIRE(model.scene()->find(copy));
    REQUIRE(model.installedAnimationPose()->numerics->find(copy)->local.position.x == 24);
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(copy).content == content);
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(copy).evaluationRevision ==
            model.scene()->editableMesh(model.scene()->find(copy)->editableMesh)->evaluationRevision);
    REQUIRE(model.scene()->editableMesh(model.scene()->find(entity)->editableMesh)->evaluationRevision == geometryRevision);
    REQUIRE(model.installedAnimationPose()->sceneState->content->geometryStages.at(copy).identity.contentVersion ==
            model.installedAnimationPose()->geometry->entries.at(copy).evaluationRevision);
    REQUIRE(preparedIsolation == 3);
    REQUIRE(originalPose->sceneState->content == originalState.content);
    REQUIRE(originalPose->sceneState->source == originalState.source);
    REQUIRE(originalPose->sceneState->content->geometryStages.at(entity).identity == originalStage);
    REQUIRE_FALSE(originalPose->geometry->entries.contains(copy));
    REQUIRE_FALSE(originalPose->sceneState->find(copy));
    REQUIRE_FALSE(originalPose->sceneState->content->geometryStages.contains(copy));
    REQUIRE_FALSE(originalState.content->geometry->entries.contains(copy));
    REQUIRE(originalPose->geometry->entries.at(entity).evaluationRevision == geometryRevision);
    validIdentity(model);
}

TEST_CASE("Animation unsupported legacy history clears pose before its first content notification",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(entity));
    REQUIRE(model.setAnimationPreview(true));
    const auto before = model.apiDocumentState();
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &model, [&] {
        REQUIRE(model.animationMode() == Mode::Base);
        REQUIRE_FALSE(model.installedAnimationPose());
        REQUIRE(model.apiDocumentState().documentRevision == before.documentRevision + 1);
        REQUIRE(model.apiDocumentState().historyRevision == before.historyRevision + 1);
    });
    model.undo();
    REQUIRE(model.scene()->find(entity)->editableMesh == 0);
    REQUIRE(model.animationMode() == Mode::Base);
    REQUIRE_FALSE(model.installedAnimationPose());
    REQUIRE(failures.count() == 1);
}

TEST_CASE("Animation numerical history failure restores formal content and exits preview",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Empty);
    auto extreme = model.scene()->find(entity)->transform;
    extreme.scale.x = 1.0e30F;
    REQUIRE(model.setTransform(entity, extreme));
    REQUIRE(model.setTransform(entity, core::Transform{}));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(25));
    const auto index = model.undoStack()->index();
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    model.undo();
    REQUIRE(model.undoStack()->index() == index - 1);
    REQUIRE(model.scene()->find(entity)->transform.scale.x == extreme.scale.x);
    REQUIRE(model.animationMode() == Mode::Base);
    REQUIRE_FALSE(model.installedAnimationPose());
    REQUIRE(failures.count() == 1);
}

TEST_CASE("Animation history restores undo and redo with a persistently failing view provider",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    const auto original = translation(entity);
    const auto changed = translation(entity, 96);
    REQUIRE(model.replaceAnimation(original));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(25));
    REQUIRE(model.replaceAnimation(changed));
    const auto index = model.undoStack()->index();
    int viewCalls = 0;
    int guardCalls = 0;
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        ++guardCalls;
        return std::nullopt;
    });
    const auto failingView = [&]() -> renderer_gl::EditorCamera {
        ++viewCalls;
        throw std::bad_alloc{};
    };
    auto expected = model.apiDocumentState();
    int notifications = 0;
    QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &model, [&] {
        ++notifications;
        REQUIRE(model.animationMode() == Mode::Base);
        REQUIRE_FALSE(model.installedAnimationPose());
        REQUIRE(model.apiDocumentState().documentRevision == expected.documentRevision + 1);
        REQUIRE(model.apiDocumentState().historyRevision == expected.historyRevision + 1);
    });
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    model.setAnimationViewProvider(failingView);
    REQUIRE_NOTHROW(model.undo());
    REQUIRE(model.undoStack()->index() == index - 1);
    REQUIRE(model.scene()->animation() == original);
    REQUIRE(model.animationMode() == Mode::Base);
    REQUIRE_FALSE(model.installedAnimationPose());
    REQUIRE(viewCalls == 1);
    REQUIRE(guardCalls == 1);
    REQUIRE(notifications == 1);
    REQUIRE(failures.count() == 1);
    model.setAnimationViewProvider({});
    REQUIRE(model.setAnimationPreview(true));
    guardCalls = 0;
    viewCalls = 0;
    expected = model.apiDocumentState();
    model.setAnimationViewProvider(failingView);
    REQUIRE_NOTHROW(model.redo());
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(model.scene()->animation() == changed);
    REQUIRE(model.animationMode() == Mode::Base);
    REQUIRE_FALSE(model.installedAnimationPose());
    REQUIRE(viewCalls == 1);
    REQUIRE(guardCalls == 1);
    REQUIRE(notifications == 2);
    REQUIRE(failures.count() == 2);
}

TEST_CASE("Animation history view failure cannot bypass guards or reentrant source changes",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto other = model.createEntity(core::PrimitiveKind::Cube);
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    const auto changed = translation(entity, 96);
    REQUIRE(model.replaceAnimation(changed));
    const auto before = model.apiDocumentState();
    const auto index = model.undoStack()->index();
    const auto pose = model.installedAnimationPose();
    int viewCalls = 0;
    int guardCalls = 0;
    int expectedGuardCalls = 1;
    auto expectedSelection = entity;
    bool expectedBusy = false;
    model.setAnimationViewProvider([&]() -> renderer_gl::EditorCamera {
        ++viewCalls;
        throw std::bad_alloc{};
    });
    SECTION("Explicit guard refusal still rejects history") {
        model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            ++guardCalls;
            return api::ApiError{api::ErrorCode::DeadlineExceeded, QStringLiteral("测试拒绝"), {},
                                 api::Recovery::None, model.apiDocumentState()};
        });
    }
    SECTION("An allocating guard is a controlled rejection, not permission") {
        model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            ++guardCalls;
            throw std::bad_alloc{};
        });
    }
    SECTION("Guard selection reentry invalidates the original credentials") {
        expectedSelection = other;
        model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            ++guardCalls;
            model.selection()->setSelectedEntity(other);
            return std::nullopt;
        });
    }
    SECTION("Guard interaction reentry invalidates the original credentials") {
        expectedBusy = true;
        model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            ++guardCalls;
            model.setExternalBusy(QStringLiteral("history_callback"), true);
            return std::nullopt;
        });
    }
    SECTION("Provider reentry before throwing cannot replace the original credentials") {
        expectedGuardCalls = 0;
        expectedSelection = other;
        model.setAnimationViewProvider([&]() -> renderer_gl::EditorCamera {
            ++viewCalls;
            model.selection()->setSelectedEntity(other);
            throw std::bad_alloc{};
        });
        model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            ++guardCalls;
            return std::nullopt;
        });
    }
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    REQUIRE_NOTHROW(model.undo());
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(model.scene()->animation() == changed);
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE(model.installedAnimationPose() == pose);
    REQUIRE(model.selection()->selectedEntity() == expectedSelection);
    REQUIRE(model.apiBusyReasons().contains(QStringLiteral("history_callback")) == expectedBusy);
    REQUIRE(viewCalls == 1);
    REQUIRE(guardCalls == expectedGuardCalls);
    REQUIRE(failures.count() == 1);
    unchangedDocument(model, before);
    validIdentity(model);
}

TEST_CASE("Animation history recovers a post-guard view failure without rerunning the guard",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    const auto original = translation(entity);
    REQUIRE(model.replaceAnimation(original));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.replaceAnimation(translation(entity, 96)));
    const auto before = model.apiDocumentState();
    const auto index = model.undoStack()->index();
    renderer_gl::EditorCamera view;
    REQUIRE(view.setState(model.editorCamera()));
    bool failView = false;
    int failedViewCalls = 0;
    int guardCalls = 0;
    model.setAnimationViewProvider([&] {
        if (failView) {
            ++failedViewCalls;
            throw std::bad_alloc{};
        }
        return view;
    });
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        ++guardCalls;
        failView = true;
        return std::nullopt;
    });
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    REQUIRE_NOTHROW(model.undo());
    REQUIRE(model.undoStack()->index() == index - 1);
    REQUIRE(model.scene()->animation() == original);
    REQUIRE(model.animationMode() == Mode::Base);
    REQUIRE_FALSE(model.installedAnimationPose());
    REQUIRE(model.apiDocumentState().documentRevision == before.documentRevision + 1);
    REQUIRE(model.apiDocumentState().historyRevision == before.historyRevision + 1);
    REQUIRE(guardCalls == 1);
    REQUIRE(failedViewCalls == 1);
    REQUIRE(failures.count() == 1);
}

TEST_CASE("Animation history stops reading a provider that fails midway through preparation",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto other = model.createEntity(core::PrimitiveKind::Cube);
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    const auto original = translation(entity);
    const auto changed = translation(entity, 96);
    REQUIRE(model.replaceAnimation(original));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.replaceAnimation(changed));
    const auto before = model.apiDocumentState();
    const auto index = model.undoStack()->index();
    const auto pose = model.installedAnimationPose();
    int firstFailure = 2;
    bool changeSelection = false;
    SECTION("The outer source match loses its view") {}
    SECTION("The history command preparation loses its view") {
        firstFailure = 3;
    }
    SECTION("Reentry before a nested view failure invalidates the original source") {
        firstFailure = 3;
        changeSelection = true;
    }
    renderer_gl::EditorCamera view;
    REQUIRE(view.setState(model.editorCamera()));
    int viewCalls = 0;
    int failedViewCalls = 0;
    int guardCalls = 0;
    model.setAnimationViewProvider([&] {
        if (++viewCalls >= firstFailure) {
            ++failedViewCalls;
            if (changeSelection)
                model.selection()->setSelectedEntity(other);
            throw std::bad_alloc{};
        }
        return view;
    });
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        ++guardCalls;
        return std::nullopt;
    });
    int notifications = 0;
    QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &model, [&] {
        ++notifications;
        REQUIRE(model.animationMode() == Mode::Base);
        REQUIRE_FALSE(model.installedAnimationPose());
        REQUIRE(model.apiDocumentState().documentRevision == before.documentRevision + 1);
        REQUIRE(model.apiDocumentState().historyRevision == before.historyRevision + 1);
    });
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    REQUIRE_NOTHROW(model.undo());
    REQUIRE(viewCalls == firstFailure);
    REQUIRE(failedViewCalls == 1);
    REQUIRE(failures.count() == 1);
    if (changeSelection) {
        REQUIRE(guardCalls == 0);
        REQUIRE(notifications == 0);
        REQUIRE(model.undoStack()->index() == index);
        REQUIRE(model.scene()->animation() == changed);
        REQUIRE(model.animationMode() == Mode::PreviewPaused);
        REQUIRE(model.installedAnimationPose() == pose);
        REQUIRE(model.selection()->selectedEntity() == other);
        unchangedDocument(model, before);
    } else {
        REQUIRE(guardCalls == 1);
        REQUIRE(notifications == 1);
        REQUIRE(model.undoStack()->index() == index - 1);
        REQUIRE(model.scene()->animation() == original);
        REQUIRE(model.animationMode() == Mode::Base);
        REQUIRE_FALSE(model.installedAnimationPose());
    }
}

TEST_CASE("Animation draft freezes mask selection time pivot and source and confirms one history",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto other = model.createEntity(core::PrimitiveKind::Empty);
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(25.5));
    REQUIRE_FALSE(model.beginAnimationDraft(entity, {true, false, false}));
    REQUIRE(model.setAnimationFrame(25));
    const auto base = model.scene()->find(entity)->transform;
    const auto animation = model.scene()->animation();
    const auto state = model.apiDocumentState();
    const auto index = model.undoStack()->index();
    const auto formal = model.installedAnimationPose();
    REQUIRE(model.beginAnimationDraft(entity, {true, false, false}));
    REQUIRE((model.animationDraftMask() == std::array<bool, 3>{true, false, false}));
    REQUIRE_FALSE(model.setAnimationDraftChannel(core::AnimationChannel::RotationEulerXYZDegrees, {0, 720, 0}));
    REQUIRE(model.setAnimationDraftChannel(core::AnimationChannel::Position, {123, 2, 3}));
    model.selection()->setSelectedEntity(other);
    REQUIRE(model.selection()->selectedEntity() == entity);
    REQUIRE_FALSE(model.setAnimationFrame(13));
    REQUIRE_FALSE(model.playAnimation());
    REQUIRE_FALSE(model.setEditMode(true));
    REQUIRE_FALSE(model.setAnimationPreview(false));
    const auto pivot = model.transformPivot();
    model.setTransformPivot(editor::TransformPivot::Cursor);
    REQUIRE(model.transformPivot() == pivot);
    model.newScene();
    REQUIRE(model.apiDocumentState().document == state.document);
    REQUIRE(model.scene()->animation() == animation);
    REQUIRE(model.scene()->find(entity)->transform.position == base.position);
    unchangedDocument(model, state);
    model.cancelAnimationDraft();
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE(model.installedAnimationPose()->numerics == formal->numerics);
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(model.beginAnimationDraft(entity, {true, false, false}));
    REQUIRE(model.setAnimationDraftChannel(core::AnimationChannel::Position, {123, 2, 3}));
    model.setExternalBusy(QStringLiteral("animation_gesture"), true);
    REQUIRE_FALSE(model.commitAnimationDraft());
    model.setExternalBusy(QStringLiteral("animation_gesture"), false);
    REQUIRE(model.commitAnimationDraft());
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE(model.undoStack()->index() == index + 1);
    REQUIRE(model.scene()->animation().tracks.at({entity, core::AnimationChannel::Position}).keys[1].frame == 25);
    REQUIRE(model.installedAnimationPose()->numerics->find(entity)->local.position == glm::vec3(123, 2, 3));
    REQUIRE(model.scene()->find(entity)->transform.position == base.position);
    model.undo();
    REQUIRE(model.scene()->animation() == animation);
    validIdentity(model);
}

TEST_CASE("Animation rotation draft preserves raw turns and key move conflicts are atomic",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    auto animation = translation(entity);
    animation.tracks[{entity, core::AnimationChannel::RotationEulerXYZDegrees}] = {
        {{1, {0, 0, 0}, core::AnimationInterpolation::Linear},
         {49, {0, 720.000000001, 0}, core::AnimationInterpolation::Linear}}};
    REQUIRE(model.replaceAnimation(animation));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(49));
    REQUIRE(model.beginAnimationDraft(entity, {false, true, false}));
    REQUIRE(model.animationDraftValues()->at(1).y == 720.000000001);
    REQUIRE(model.setAnimationDraftChannel(core::AnimationChannel::RotationEulerXYZDegrees, {0, 1080.000000001, 0}));
    REQUIRE(model.commitAnimationDraft());
    REQUIRE(model.scene()->animation().tracks.at({entity, core::AnimationChannel::RotationEulerXYZDegrees}).keys.back().value.y == 1080.000000001);
    const auto state = model.apiDocumentState();
    const auto index = model.undoStack()->index();
    REQUIRE_FALSE(model.moveAnimationKeyframe(entity, core::AnimationChannel::Position, 1, 49));
    unchangedDocument(model, state);
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(model.moveAnimationKeyframe(entity, core::AnimationChannel::Position, 49, 50));
    REQUIRE(model.setAnimationKeyframeInterpolation(entity, core::AnimationChannel::Position, 1, core::AnimationInterpolation::Constant));
    REQUIRE(model.deleteAnimationKeyframe(entity, core::AnimationChannel::Position, 50));
    validIdentity(model);
}

TEST_CASE("Animation visibility and collection candidates stay unchanged through the guard",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    const auto collection = model.createCollection(QStringLiteral("分组"));
    REQUIRE(model.assignEntityToCollection(entity, collection));
    REQUIRE(model.setAnimationPreview(true));
    const auto pose = model.installedAnimationPose();
    const auto before = model.apiDocumentState();
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        const auto& collections = model.scene()->collections();
        const auto found = std::find_if(collections.begin(), collections.end(),
                                        [collection](const auto& entry) { return entry.id == collection; });
        REQUIRE(found != collections.end());
        REQUIRE(found->visible);
        REQUIRE(model.installedAnimationPose() == pose);
        return api::ApiError{api::ErrorCode::DeadlineExceeded, QStringLiteral("测试拒绝"), {},
                             api::Recovery::None, model.apiDocumentState()};
    });
    REQUIRE_FALSE(model.setCollectionVisible(collection, false));
    unchangedDocument(model, before);
    REQUIRE(model.installedAnimationPose() == pose);
    model.exchangeBeforeCommitGuard({});
    REQUIRE(model.setCollectionVisible(collection, false));
    REQUIRE_FALSE(model.installedAnimationPose()->geometry->entries.at(entity).visible);
    model.undo();
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(entity).visible);
    model.redo();
    REQUIRE_FALSE(model.installedAnimationPose()->geometry->entries.at(entity).visible);
    model.undo();
    model.selection()->setSelectedEntity(entity);
    REQUIRE(model.hideSelection());
    REQUIRE_FALSE(model.installedAnimationPose()->geometry->entries.at(entity).visible);
    REQUIRE(model.revealHidden());
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(entity).visible);
    validIdentity(model);
}

TEST_CASE("Animation local view prepares copy selection and deleted root visibility before notifications",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto other = model.createEntity(core::PrimitiveKind::Cube);
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.toggleLocalView());
    REQUIRE(model.installedAnimationPose()->geometry->visibility.localRoot == entity);
    model.selection()->setSelectedEntity(other);
    REQUIRE(model.viewportVisibility().localRoot == 0);
    REQUIRE(model.installedAnimationPose()->geometry->visibility.localRoot == 0);
    model.selection()->setSelectedEntity(entity);
    REQUIRE(model.toggleLocalView());
    QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &model, [&] {
        validIdentity(model);
        REQUIRE(model.viewportVisibility().localRoot ==
                model.installedAnimationPose()->geometry->visibility.localRoot);
    });
    model.duplicateSelected();
    const auto copy = model.selection()->selectedEntity();
    REQUIRE(copy != entity);
    REQUIRE(model.viewportVisibility().localRoot == 0);
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(copy).visible);
    REQUIRE(model.toggleLocalView());
    REQUIRE(model.viewportVisibility().localRoot == copy);
    model.deleteSelected();
    REQUIRE_FALSE(model.scene()->find(copy));
    REQUIRE(model.viewportVisibility().localRoot == 0);
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(other).visible);
    model.undo();
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(copy).visible);
}

TEST_CASE("GUI camera and light edits retain preview access while explicit APIs retain Busy",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto camera = model.createCamera();
    const auto light = model.createDirectionalLight();
    REQUIRE(camera);
    REQUIRE(light);
    REQUIRE(model.setPreviewCamera(camera));
    bool paused = false;
    SECTION("Base entity camera preview retains GUI editing") {}
    SECTION("Paused animation retains frozen component and revision coherence") {
        paused = true;
        REQUIRE(model.setAnimationPreview(true));
        REQUIRE(model.setAnimationFrame(25));
    }
    const auto originalCamera = *model.scene()->find(camera)->camera;
    const auto originalLight = *model.scene()->find(light)->light;
    auto changedCamera = originalCamera;
    changedCamera.fieldOfView = 30;
    auto changedLight = originalLight;
    changedLight.intensity = 1.5F;
    const auto before = model.apiDocumentState();
    const auto index = model.undoStack()->index();
    auto cameraRequest = mutation<api::CameraUpdateRequest>(model, camera);
    cameraRequest.camera = changedCamera;
    const auto cameraResult = model.updateCameraExplicit(cameraRequest);
    REQUIRE_FALSE(cameraResult.hasValue());
    REQUIRE(cameraResult.error->code == api::ErrorCode::Busy);
    REQUIRE(cameraResult.error->message.contains(QStringLiteral("camera_preview")));
    auto lightRequest = mutation<api::LightUpdateRequest>(model, light);
    lightRequest.light = changedLight;
    const auto lightResult = model.updateLightExplicit(lightRequest);
    REQUIRE_FALSE(lightResult.hasValue());
    REQUIRE(lightResult.error->code == api::ErrorCode::Busy);
    REQUIRE(lightResult.error->message.contains(QStringLiteral("camera_preview")));
    unchangedDocument(model, before);
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(*model.scene()->find(camera)->camera == originalCamera);
    REQUIRE(*model.scene()->find(light)->light == originalLight);

    const auto formal = model.installedAnimationPose();
    auto expected = before;
    auto expectedCamera = originalCamera;
    auto expectedLight = originalLight;
    int notifications = 0;
    QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &model, [&] {
        ++notifications;
        REQUIRE(model.previewCamera() == camera);
        REQUIRE(model.apiDocumentState().documentRevision == expected.documentRevision + 1);
        REQUIRE(model.apiDocumentState().historyRevision == expected.historyRevision + 1);
        REQUIRE(*model.scene()->find(camera)->camera == expectedCamera);
        REQUIRE(*model.scene()->find(light)->light == expectedLight);
        if (paused) {
            validIdentity(model);
            const auto pose = model.installedAnimationPose();
            REQUIRE(pose->numerics == formal->numerics);
            REQUIRE(pose->geometry->entries.at(camera).camera == expectedCamera);
            REQUIRE(pose->geometry->entries.at(light).light == expectedLight);
        } else {
            REQUIRE(model.animationMode() == Mode::Base);
            REQUIRE_FALSE(model.installedAnimationPose());
        }
    });
    expectedCamera = changedCamera;
    REQUIRE(model.setCamera(camera, changedCamera));
    REQUIRE(model.undoStack()->index() == index + 1);
    expected = model.apiDocumentState();
    expectedLight = changedLight;
    REQUIRE(model.setLight(light, changedLight));
    REQUIRE(model.undoStack()->index() == index + 2);
    expected = model.apiDocumentState();
    expectedLight = originalLight;
    model.undo();
    REQUIRE(model.undoStack()->index() == index + 1);
    expected = model.apiDocumentState();
    expectedCamera = originalCamera;
    model.undo();
    REQUIRE(model.undoStack()->index() == index);
    expected = model.apiDocumentState();
    expectedCamera = changedCamera;
    model.redo();
    REQUIRE(model.undoStack()->index() == index + 1);
    expected = model.apiDocumentState();
    expectedLight = changedLight;
    model.redo();
    REQUIRE(model.undoStack()->index() == index + 2);
    REQUIRE(notifications == 6);
    if (paused) {
        REQUIRE(formal->geometry->entries.at(camera).camera == originalCamera);
        REQUIRE(formal->geometry->entries.at(light).light == originalLight);
    }
}

TEST_CASE("GUI camera and light edits remain blocked during playback and pose drafts",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto camera = model.createCamera();
    const auto light = model.createDirectionalLight();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(camera);
    REQUIRE(light);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    SECTION("Playing") {
        REQUIRE(model.setPreviewCamera(camera));
        REQUIRE(model.playAnimation());
    }
    SECTION("Pose draft") {
        REQUIRE(model.beginAnimationDraft(entity, {true, false, false}));
    }
    const auto before = model.apiDocumentState();
    const auto index = model.undoStack()->index();
    const auto pose = model.installedAnimationPose();
    const auto originalCamera = *model.scene()->find(camera)->camera;
    const auto originalLight = *model.scene()->find(light)->light;
    auto changedCamera = originalCamera;
    changedCamera.fieldOfView = 30;
    auto changedLight = originalLight;
    changedLight.intensity = 1.5F;
    REQUIRE_FALSE(model.setCamera(camera, changedCamera));
    REQUIRE_FALSE(model.setLight(light, changedLight));
    auto cameraRequest = mutation<api::CameraUpdateRequest>(model, camera);
    cameraRequest.camera = changedCamera;
    const auto cameraResult = model.updateCameraExplicit(cameraRequest);
    REQUIRE_FALSE(cameraResult.hasValue());
    REQUIRE(cameraResult.error->code == api::ErrorCode::Busy);
    auto lightRequest = mutation<api::LightUpdateRequest>(model, light);
    lightRequest.light = changedLight;
    const auto lightResult = model.updateLightExplicit(lightRequest);
    REQUIRE_FALSE(lightResult.hasValue());
    REQUIRE(lightResult.error->code == api::ErrorCode::Busy);
    REQUIRE(*model.scene()->find(camera)->camera == originalCamera);
    REQUIRE(*model.scene()->find(light)->light == originalLight);
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(model.installedAnimationPose() == pose);
    unchangedDocument(model, before);
}

TEST_CASE("GUI device edits recheck animation admission after synchronous gesture cancellation",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto camera = model.createCamera();
    const auto light = model.createDirectionalLight();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(camera);
    REQUIRE(light);
    REQUIRE(model.replaceAnimation(translation(entity)));
    model.beginTransformEdit(entity);
    REQUIRE(model.apiBusyReasons().contains(QStringLiteral("object_transform")));
    bool editLight = false;
    bool enterDraft = false;
    bool fromSceneNotification = false;
    SECTION("Camera cancellation finishes into Playing") {}
    SECTION("Camera cancellation scene notification enters a draft") {
        enterDraft = true;
        fromSceneNotification = true;
    }
    SECTION("Light cancellation scene notification enters Playing") {
        editLight = true;
        fromSceneNotification = true;
    }
    SECTION("Light cancellation finishes into a draft") {
        editLight = true;
        enterDraft = true;
    }
    const auto before = model.apiDocumentState();
    const auto index = model.undoStack()->index();
    const auto originalCamera = *model.scene()->find(camera)->camera;
    const auto originalLight = *model.scene()->find(light)->light;
    auto changedCamera = originalCamera;
    changedCamera.fieldOfView = 30;
    auto changedLight = originalLight;
    changedLight.intensity = 1.5F;
    int reentries = 0;
    QMetaObject::Connection connection;
    const auto changeMode = [&] {
        QObject::disconnect(connection);
        ++reentries;
        REQUIRE(model.setAnimationPreview(true));
        if (enterDraft)
            REQUIRE(model.beginAnimationDraft(entity, {true, false, false}));
        else
            REQUIRE(model.playAnimation());
    };
    connection = fromSceneNotification
                     ? QObject::connect(&model, &editor::SceneViewModel::sceneChanged, &model, changeMode)
                     : QObject::connect(&model, &editor::SceneViewModel::transformEditFinished,
                                        &model, changeMode);
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    if (editLight)
        REQUIRE_FALSE(model.setLight(light, changedLight));
    else
        REQUIRE_FALSE(model.setCamera(camera, changedCamera));
    REQUIRE(reentries == 1);
    REQUIRE(model.animationMode() == (enterDraft ? Mode::PoseDraft : Mode::Playing));
    REQUIRE(*model.scene()->find(camera)->camera == originalCamera);
    REQUIRE(*model.scene()->find(light)->light == originalLight);
    REQUIRE(model.undoStack()->index() == index);
    REQUIRE(failures.count() == 1);
    unchangedDocument(model, before);
    validIdentity(model);
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(camera).camera == originalCamera);
    REQUIRE(model.installedAnimationPose()->geometry->entries.at(light).light == originalLight);
}

TEST_CASE("Animation navigation guard failure is atomic and successful play navigation preserves session",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    renderer_gl::EditorCamera view;
    REQUIRE(view.setState(model.editorCamera()));
    model.setAnimationViewProvider([&] { return view; });
    auto candidate = view;
    candidate.orbit(25, 0);
    const auto before = model.apiDocumentState();
    const auto pose = model.installedAnimationPose();
    bool installed = false;
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        return api::ApiError{api::ErrorCode::DeadlineExceeded, QStringLiteral("测试拒绝"), {},
                             api::Recovery::None, model.apiDocumentState()};
    });
    REQUIRE_FALSE(model.commitEditorCamera(candidate, [&] { installed = true; view = candidate; }));
    REQUIRE_FALSE(installed);
    REQUIRE(model.installedAnimationPose() == pose);
    REQUIRE(model.editorCamera() == view.state());
    unchangedDocument(model, before);
    model.exchangeBeforeCommitGuard([]() -> std::optional<api::ApiError> {
        throw std::bad_alloc{};
    });
    REQUIRE_FALSE(model.commitEditorCamera(candidate, [&] { installed = true; view = candidate; }));
    REQUIRE_FALSE(installed);
    REQUIRE(model.installedAnimationPose() == pose);
    REQUIRE(model.editorCamera() == view.state());
    unchangedDocument(model, before);
    model.exchangeBeforeCommitGuard({});
    REQUIRE(model.playAnimation());
    const auto session = model.animationSessionRevision();
    const auto history = model.apiDocumentState().historyRevision;
    REQUIRE(model.commitEditorCamera(candidate, [&] { installed = true; view = candidate; }));
    REQUIRE(installed);
    REQUIRE(model.animationMode() == Mode::Playing);
    REQUIRE(model.animationSessionRevision() == session);
    REQUIRE(model.apiDocumentState().historyRevision == history);
    REQUIRE(model.isModified());
    validIdentity(model);
    REQUIRE(model.pauseAnimation());
}

TEST_CASE("Animation navigation rejects reentrant seek and cannot leave a stale pending candidate",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    renderer_gl::EditorCamera candidate;
    REQUIRE(candidate.setState(model.editorCamera()));
    candidate.orbit(25, 0);
    const auto camera = model.editorCamera();
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        model.exchangeBeforeCommitGuard({});
        REQUIRE(model.setAnimationFrame(25));
        return std::nullopt;
    });
    bool installed = false;
    REQUIRE_FALSE(model.commitEditorCamera(candidate, [&] { installed = true; }));
    REQUIRE_FALSE(installed);
    REQUIRE(model.editorCamera() == camera);
    REQUIRE(model.animationFrame() == 25);
    REQUIRE(model.renameEntity(entity, QStringLiteral("后续编辑")));
    REQUIRE(model.installedAnimationPose()->numerics->find(entity)->local.position.x == 24);
    validIdentity(model);
}

TEST_CASE("Animation seek rejects a guard that changes only pose visibility and selection",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    const auto before = model.apiDocumentState();
    const auto session = model.animationSessionRevision();
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        model.exchangeBeforeCommitGuard({});
        REQUIRE(model.hideSelection());
        return std::nullopt;
    });
    REQUIRE_FALSE(model.setAnimationFrame(25));
    REQUIRE(model.animationFrame() == 1);
    REQUIRE(model.animationSessionRevision() == session);
    REQUIRE(model.viewportVisibility().hiddenObjects.contains(entity));
    REQUIRE_FALSE(model.installedAnimationPose()->geometry->entries.at(entity).visible);
    unchangedDocument(model, before);
    validIdentity(model);
}

TEST_CASE("Animation draft admission rejects a guard that switches the selected entity",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto other = model.createEntity(core::PrimitiveKind::Cube);
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setAnimationPreview(true));
    const auto before = model.apiDocumentState();
    const auto pose = model.installedAnimationPose();
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        model.exchangeBeforeCommitGuard({});
        model.selection()->setSelectedEntity(other);
        return std::nullopt;
    });
    REQUIRE_FALSE(model.beginAnimationDraft(entity, {true, false, false}));
    REQUIRE(model.selection()->selectedEntity() == other);
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE_FALSE(model.animationDraftValues());
    REQUIRE(model.installedAnimationPose() == pose);
    unchangedDocument(model, before);
}

TEST_CASE("Animation view providers cannot publish stale inputs or access a cancelled draft",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    renderer_gl::EditorCamera view;
    REQUIRE(view.setState(model.editorCamera()));
    bool once = true;
    SECTION("Rename during seek leaves only the nested formal edit") {
        const auto before = model.apiDocumentState();
        model.setAnimationViewProvider([&] {
            if (std::exchange(once, false))
                REQUIRE(model.renameEntity(entity, QStringLiteral("回调改名")));
            return view;
        });
        REQUIRE_FALSE(model.setAnimationFrame(25));
        REQUIRE(model.animationFrame() == 1);
        REQUIRE(model.scene()->find(entity)->name == "回调改名");
        REQUIRE(model.apiDocumentState().documentRevision == before.documentRevision + 1);
        REQUIRE(model.apiDocumentState().historyRevision == before.historyRevision + 1);
    }
    SECTION("Seek during shared rename cannot validate one wrapper then publish another") {
        const auto before = model.apiDocumentState();
        const auto name = model.scene()->find(entity)->name;
        model.setAnimationViewProvider([&] {
            if (std::exchange(once, false))
                REQUIRE(model.setAnimationFrame(25));
            return view;
        });
        REQUIRE_FALSE(model.renameEntity(entity, QStringLiteral("不应改名")));
        REQUIRE(model.animationFrame() == 25);
        REQUIRE(model.scene()->find(entity)->name == name);
        unchangedDocument(model, before);
    }
    SECTION("Draft cancellation during channel preparation cannot dereference an empty draft") {
        REQUIRE(model.beginAnimationDraft(entity, {true, false, false}));
        const auto before = model.apiDocumentState();
        model.setAnimationViewProvider([&] {
            if (std::exchange(once, false))
                model.cancelAnimationDraft();
            return view;
        });
        REQUIRE_FALSE(model.setAnimationDraftChannel(core::AnimationChannel::Position, {123, 2, 3}));
        REQUIRE_FALSE(model.animationDraftValues());
        REQUIRE(model.animationMode() == Mode::PreviewPaused);
        REQUIRE(model.installedAnimationPose()->numerics->find(entity)->local.position.x == 0);
        unchangedDocument(model, before);
    }
    validIdentity(model);
}

TEST_CASE("Animation pause view allocation failure leaves a stopped consumable state",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(entity)));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.playAnimation());
    const auto before = model.apiDocumentState();
    const auto session = model.animationSessionRevision();
    renderer_gl::EditorCamera view;
    REQUIRE(view.setState(model.editorCamera()));
    bool once = true;
    model.setAnimationViewProvider([&] {
        if (std::exchange(once, false))
            throw std::bad_alloc{};
        return view;
    });
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    REQUIRE(model.pauseAnimation());
    REQUIRE(model.animationMode() == Mode::Base);
    REQUIRE_FALSE(model.installedAnimationPose());
    REQUIRE(model.animationSessionRevision() == session + 1);
    REQUIRE(failures.count() == 1);
    const auto frame = model.animationFrame();
    QTest::qWait(30);
    REQUIRE(model.animationFrame() == frame);
    unchangedDocument(model, before);
}

TEST_CASE("Document publication clears Edit before new-source consumer notifications",
          "[animation][animation-session][document-publication]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setEditMode(true));
    model.selectAllComponents();
    REQUIRE_FALSE(model.componentSelection().selectedIds().empty());
    const auto previous = model.apiDocumentState().document;
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    int selectionNotifications = 0;
    int modeNotifications = 0;
    int newSourceNotifications = 0;
    const auto checkObject = [&] {
        CHECK_FALSE(model.isEditMode());
        CHECK(model.editedEntity() == core::kInvalidEntity);
        CHECK(model.componentSelection().selectedIds().empty());
        CHECK_FALSE(model.componentSelection().activeId());
        CHECK(model.viewportVisibility().editedEntity == core::kInvalidEntity);
        CHECK_FALSE(model.viewportVisibility().hasHiddenElements());
        CHECK_FALSE(model.selectedComponentCenter());
        CHECK_FALSE(model.cursorSelectionCenter());
    };
    QObject::connect(model.selection(), &editor::SelectionModel::selectedEntityChanged,
                     &model, [&](core::EntityId id) {
        CHECK(id == core::kInvalidEntity);
        CHECK(model.apiDocumentState().document == previous);
        checkObject();
        ++selectionNotifications;
    });
    QObject::connect(&model, &editor::SceneViewModel::editModeChanged, &model, [&](bool enabled) {
        CHECK_FALSE(enabled);
        CHECK(model.apiDocumentState().document != previous);
        checkObject();
        ++modeNotifications;
    });
    QObject::connect(&model, &editor::SceneViewModel::componentSelectionChanged, &model, [&] {
        CHECK(model.apiDocumentState().document != previous);
        checkObject();
        ++newSourceNotifications;
    });
    model.newScene();
    REQUIRE(selectionNotifications == 1);
    REQUIRE(modeNotifications == 1);
    REQUIRE(newSourceNotifications > 0);
    REQUIRE(failures.isEmpty());
    REQUIRE_FALSE(model.scene()->find(cube));
}

TEST_CASE("Animation document reset clears previous pose even when revisions equal",
          "[animation][animation-session]") {
    editor::SceneViewModel model;
    model.newScene();
    REQUIRE(model.setAnimationPreview(true));
    const auto previous = model.installedAnimationPose();
    REQUIRE(previous->identity.sourceRevision == 1);
    const auto document = model.apiDocumentState().document;
    model.newScene();
    REQUIRE(model.apiDocumentState().documentRevision == 1);
    REQUIRE(model.apiDocumentState().document != document);
    REQUIRE(model.animationMode() == Mode::Base);
    REQUIRE_FALSE(model.installedAnimationPose());
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.installedAnimationPose()->identity.documentId != previous->identity.documentId);
    validIdentity(model);
}

TEST_CASE("J1 session installs exact time material Sun and one immutable source",
          "[animation][animation-session][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    const auto sun = model.createEntity(core::PrimitiveKind::Empty);
    auto scene = std::const_pointer_cast<core::Scene>(model.scene());
    core::MaterialDefinition material;
    material.id = core::MaterialId{31};
    material.parameters.roughness = .7;
    core::SceneMaterialData materials;
    materials.assets.emplace(core::StableAssetId{31}, core::SceneAssetDefinition{
        core::StableAssetId{31}, core::SceneAssetKind::Material, material});
    std::string error;
    auto prepared = scene->prepareMaterialData(materials, error);
    REQUIRE(prepared);
    REQUIRE(scene->installPreparedMaterialData(*prepared));
    REQUIRE(scene->setMaterialSlots(cube, {{core::MaterialSlotId{1}, material.id}}));
    REQUIRE(scene->setSun(sun, {}));
    core::SceneAnimation animation;
    animation.tracks[{cube, core::AnimationChannel::Position}] =
        {{{1, {0, 0, 0}}, {24, {2, 0, 0}}}};
    animation.properties.rationalFps = core::RationalFps{24000, 1001};
    core::ScalarCurve roughness{core::CurveId{1}, core::PropertyId::PbrRoughness,
        core::PropertyComponent::Scalar, {{core::KeyId{1}, 1, .2}, {core::KeyId{2}, 24, .8}}};
    core::ScalarCurve irradiance{core::CurveId{2}, core::PropertyId::SunIrradiance,
        core::PropertyComponent::Scalar, {{core::KeyId{3}, 1, 1}, {core::KeyId{4}, 24, 2}}};
    animation.properties.actions = {{core::ActionId{1}, {
        {core::ActionSlotId{1}, core::PropertyOwnerKind::Material, {roughness}},
        {core::ActionSlotId{2}, core::PropertyOwnerKind::Entity, {irradiance}}}}};
    animation.properties.bindings = {
        {scene->documentUuid(), core::MaterialOwner{material.id}, core::ActionId{1}, core::ActionSlotId{1}},
        {scene->documentUuid(), core::EntityOwner{sun}, core::ActionId{1}, core::ActionSlotId{2}}};
    REQUIRE(model.replaceAnimation(animation));
    REQUIRE(model.setAnimationPreview(true));
    const auto formal = model.installedAnimationPose();
    REQUIRE(formal->coverage == renderer_gl::PoseStateCoverage::CompleteSceneJ1);
    const auto session = formal->sceneState->source.evaluationSessionId;
    const auto document = model.apiDocumentState();
    const auto history = model.undoStack()->index();
    for (const auto frame : {24.0, 1.0, 12.5, 1.0}) {
        REQUIRE(model.setAnimationFrame(frame));
        validIdentity(model);
        const auto pose = model.installedAnimationPose();
        const auto& state = *pose->sceneState;
        const auto fraction = (frame - 1) / 23;
        REQUIRE(std::abs(state.find(cube)->worldAffine[3].x - 2 * fraction) <= 1e-12);
        REQUIRE(std::abs(state.content->materials.at(material.id).effective.roughness -
                         std::lerp(.2, .8, fraction)) <= 1e-12);
        REQUIRE(state.content->visibleSuns.size() == 1);
        REQUIRE(std::abs(state.content->visibleSuns.front().effective.irradiance - (1 + fraction)) <= 1e-12);
        REQUIRE(state.content->visibleSuns.front().directionToLight == glm::dvec3(0, 0, 1));
        REQUIRE(state.content->materials.at(material.id).author.parameters.roughness == .7);
        REQUIRE(state.find(cube)->materialSlots == scene->find(cube)->materialSlots);
        REQUIRE(state.source.evaluationSessionId == session);
        REQUIRE((state.source.rationalFps == core::RationalFps{24000, 1001}));
        REQUIRE(state.source.resourceSetIdentity.empty());
        REQUIRE(pose->geometry == formal->geometry);
        REQUIRE(state.content->authors == formal->sceneState->content->authors);
        REQUIRE(state.content->geometryStages.at(cube).geometry.mesh == pose->geometry->entries.at(cube).cpuMesh);
        unchangedDocument(model, document);
        REQUIRE(model.undoStack()->index() == history);
    }
    REQUIRE(model.installedAnimationPose()->sceneState->find(cube)->worldAffine ==
            formal->sceneState->find(cube)->worldAffine);
    const auto previous = model.installedAnimationPose();
    auto invalid = animation;
    invalid.properties.actions.front().actionSlots.front().curves.front().keys.front().outgoing =
        core::PropertyInterpolation::Bezier;
    REQUIRE_FALSE(model.replaceAnimation(invalid));
    REQUIRE(model.installedAnimationPose() == previous);
    REQUIRE(scene->animation() == animation);
    unchangedDocument(model, document);
    REQUIRE(model.undoStack()->index() == history);
}

TEST_CASE("J1 surface and global lighting history publishes the real candidate authors",
          "[animation][animation-session][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setAnimationPreview(true));
    const auto formal = model.installedAnimationPose();
    const auto beforeSurface = model.scene()->find(cube)->surface;
    auto surface = beforeSurface;
    surface.tint = {0.25F, 0.5F, 0.75F};
    const auto beforeLighting = model.scene()->lighting();
    auto lighting = beforeLighting;
    lighting.color = {0.2F, 0.4F, 0.6F};
    lighting.intensity = 2;
    const auto state = model.apiDocumentState();
    const auto history = model.undoStack()->index();
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        REQUIRE(model.installedAnimationPose() == formal);
        REQUIRE(model.scene()->find(cube)->surface == beforeSurface);
        REQUIRE(model.scene()->lighting() == beforeLighting);
        return api::ApiError{api::ErrorCode::DeadlineExceeded, QStringLiteral("拒绝候选"), {},
                             api::Recovery::None, model.apiDocumentState()};
    });
    REQUIRE_FALSE(model.setSurface(cube, surface));
    REQUIRE_FALSE(model.setLighting(lighting));
    REQUIRE(model.installedAnimationPose() == formal);
    unchangedDocument(model, state);
    REQUIRE(model.undoStack()->index() == history);
    model.exchangeBeforeCommitGuard({});
    REQUIRE(model.setSurface(cube, surface));
    validIdentity(model);
    REQUIRE(model.installedAnimationPose()->sceneState->find(cube)->legacySurface == surface);
    REQUIRE(formal->sceneState->find(cube)->legacySurface == beforeSurface);
    const auto surfaced = model.installedAnimationPose();
    REQUIRE(model.setLighting(lighting));
    validIdentity(model);
    REQUIRE(model.installedAnimationPose()->numerics == surfaced->numerics);
    REQUIRE(model.installedAnimationPose()->sceneState->content->legacyLighting == lighting);
    REQUIRE(renderer_gl::ScenePoseView(*model.scene(), model.installedAnimationPose().get()).lighting() == lighting);
    REQUIRE(surfaced->sceneState->content->legacyLighting == beforeLighting);
    model.undo();
    validIdentity(model);
    REQUIRE(model.installedAnimationPose()->numerics == surfaced->numerics);
    REQUIRE(model.installedAnimationPose()->sceneState->content->legacyLighting == beforeLighting);
    REQUIRE(model.installedAnimationPose()->sceneState->find(cube)->legacySurface == surface);
    model.undo();
    validIdentity(model);
    REQUIRE(model.installedAnimationPose()->sceneState->find(cube)->legacySurface == beforeSurface);
    model.redo();
    model.redo();
    validIdentity(model);
    REQUIRE(model.installedAnimationPose()->sceneState->find(cube)->legacySurface == surface);
    REQUIRE(model.installedAnimationPose()->sceneState->content->legacyLighting == lighting);
    REQUIRE(model.undoStack()->index() == history + 2);
}

TEST_CASE("J1 copy transform cancellation and replay keep raw authors and fixed slot IDs",
          "[animation][animation-session][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.makeEditable(cube));
    auto scene = std::const_pointer_cast<core::Scene>(model.scene());
    auto transform = scene->find(cube)->transform;
    transform.rotation = glm::quat(2, 0, 0, 0);
    REQUIRE(scene->installTransformSnapshot(cube, transform));
    core::MaterialDefinition material;
    material.id = core::MaterialId{31};
    core::SceneMaterialData materials;
    materials.assets.emplace(core::StableAssetId{31}, core::SceneAssetDefinition{
        core::StableAssetId{31}, core::SceneAssetKind::Material, material});
    std::string error;
    auto prepared = scene->prepareMaterialData(materials, error);
    REQUIRE(prepared);
    REQUIRE(scene->installPreparedMaterialData(*prepared));
    REQUIRE(scene->setMaterialSlots(cube, {{core::MaterialSlotId{1}, material.id}}));
    core::SceneAnimation animation;
    animation.tracks[{cube, core::AnimationChannel::RotationEulerXYZDegrees}] =
        {{{1, {0, 0, 0}}, {24, {0, 720.000000001, 0}}}};
    REQUIRE(model.replaceAnimation(animation));
    const auto uuid = scene->documentUuid();
    const auto history = model.undoStack()->index();
    model.duplicateSelected();
    const auto copy = model.selection()->selectedEntity();
    REQUIRE(copy != cube);
    REQUIRE(scene->find(copy));
    const auto copied = *scene->find(copy);
    const auto copiedAnimation = scene->animation();
    const auto water = scene->idHighWatermarks();
    REQUIRE(copied.transform.rotation == transform.rotation);
    REQUIRE(copied.materialSlots.size() == 1);
    REQUIRE(copied.materialSlots.front().id != scene->find(cube)->materialSlots.front().id);
    REQUIRE(copied.materialSlots.front().material == material.id);
    REQUIRE(copiedAnimation.tracks.at({copy, core::AnimationChannel::RotationEulerXYZDegrees}).keys.back().value.y ==
            720.000000001);
    const auto document = model.apiDocumentState();
    model.beginTransformEdit(copy);
    auto candidate = copied.transform;
    candidate.position += glm::vec3(7, 2, 3);
    model.previewTransform(candidate);
    REQUIRE(scene->find(copy)->transform.position == candidate.position);
    model.cancelTransformEdit();
    REQUIRE(scene->find(copy)->transform.position == copied.transform.position);
    REQUIRE(scene->find(copy)->transform.rotation == copied.transform.rotation);
    REQUIRE(scene->find(copy)->transform.scale == copied.transform.scale);
    REQUIRE(scene->animation() == copiedAnimation);
    unchangedDocument(model, document);
    REQUIRE(model.undoStack()->index() == history + 1);
    model.undo();
    REQUIRE_FALSE(scene->find(copy));
    REQUIRE(scene->find(cube)->transform.rotation == transform.rotation);
    REQUIRE(scene->animation() == animation);
    REQUIRE(scene->idHighWatermarks() == water);
    model.redo();
    REQUIRE(scene->find(copy));
    REQUIRE(scene->find(copy)->materialSlots == copied.materialSlots);
    REQUIRE(scene->find(copy)->editableMesh == copied.editableMesh);
    REQUIRE(scene->find(copy)->transform.rotation == copied.transform.rotation);
    REQUIRE(scene->animation() == copiedAnimation);
    REQUIRE(scene->documentUuid() == uuid);
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(24));
    validIdentity(model);
    const auto pose = model.installedAnimationPose();
    REQUIRE(pose->sceneState->find(copy)->rotationEulerXYZDegrees->y == 720.000000001);
    REQUIRE(pose->sceneState->find(copy)->materialSlots == copied.materialSlots);
    REQUIRE(pose->sceneState->content->geometryStages.at(copy).identity.contentVersion ==
            scene->editableMesh(copied.editableMesh)->evaluationRevision);
}

TEST_CASE("J1 failed draft edits preserve generation and cancel shares exact formal content",
          "[animation][animation-session][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.replaceAnimation(translation(cube)));
    REQUIRE(model.setAnimationPreview(true));
    const auto formal = model.installedAnimationPose();
    REQUIRE(model.beginAnimationDraft(cube, {true, false, false}));
    validIdentity(model);
    const auto draft = model.installedAnimationPose();
    REQUIRE(draft->sceneState->source.sourceKind == core::SourceKind::LiveDraft);
    const auto identity = *draft->sceneState->source.draftIdentity;
    const auto document = model.apiDocumentState();
    const auto history = model.undoStack()->index();
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        return api::ApiError{api::ErrorCode::DeadlineExceeded, QStringLiteral("拒绝草稿"), {},
                             api::Recovery::None, model.apiDocumentState()};
    });
    REQUIRE_FALSE(model.setAnimationDraftChannel(core::AnimationChannel::Position, {7, 2, 3}));
    REQUIRE(model.installedAnimationPose() == draft);
    REQUIRE(model.installedAnimationPose()->sceneState->source.draftIdentity == identity);
    REQUIRE(model.animationDraftValues()->at(0) == glm::dvec3(0));
    model.exchangeBeforeCommitGuard({});
    REQUIRE(model.setAnimationDraftChannel(core::AnimationChannel::Position, {7, 2, 3}));
    validIdentity(model);
    REQUIRE(model.installedAnimationPose()->sceneState->source.draftIdentity->generation == identity.generation + 1);
    REQUIRE(model.installedAnimationPose()->sceneState->find(cube)->localPosition == glm::dvec3(7, 2, 3));
    model.cancelAnimationDraft();
    validIdentity(model);
    REQUIRE(model.installedAnimationPose()->sceneState->source.sourceKind == core::SourceKind::LiveFormal);
    REQUIRE_FALSE(model.installedAnimationPose()->sceneState->source.draftIdentity);
    REQUIRE(model.installedAnimationPose()->numerics == formal->numerics);
    REQUIRE(model.installedAnimationPose()->sceneState->content == formal->sceneState->content);
    unchangedDocument(model, document);
    REQUIRE(model.undoStack()->index() == history);
}

TEST_CASE("J1 external legacy resources stay explicit and cannot publish partial typed state",
          "[animation][animation-session][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto root = model.importGltf(QString::fromUtf8(MINI3D_SAMPLE_DIRECTORY) + "/BoxTextured.glb");
    REQUIRE(root);
    REQUIRE(model.setAnimationPreview(true));
    const auto legacy = model.installedAnimationPose();
    REQUIRE(legacy->coverage == renderer_gl::PoseStateCoverage::LegacyTrsV1);
    REQUIRE_FALSE(legacy->sceneState);
    REQUIRE_FALSE(legacy->geometry->hasCompleteResourceIdentity);
    bool hasDecodedOwners = false;
    for (const auto& [entity, entry] : legacy->geometry->entries) {
        (void)entity;
        if (!entry.hasLegacyExternalResources) continue;
        REQUIRE(entry.cpuMesh);
        REQUIRE(entry.legacyMaterial);
        REQUIRE(entry.legacyTexture);
        hasDecodedOwners = true;
    }
    REQUIRE(hasDecodedOwners);
    REQUIRE(model.setAnimationFrame(12.5));
    REQUIRE(model.installedAnimationPose()->geometry == legacy->geometry);
    REQUIRE(model.setAnimationPreview(false));
    core::SceneAnimation typed;
    typed.properties.rationalFps = core::RationalFps{24, 1};
    const core::ScalarCurve typedX{core::CurveId{1}, core::PropertyId::Position,
        core::PropertyComponent::X, {{core::KeyId{1}, 1, 0}}};
    typed.properties.actions = {{core::ActionId{1}, {
        {core::ActionSlotId{1}, core::PropertyOwnerKind::Entity, {typedX}}}}};
    typed.properties.bindings = {{model.scene()->documentUuid(), core::EntityOwner{root},
                                 core::ActionId{1}, core::ActionSlotId{1}}};
    const auto animation = model.scene()->animation();
    const auto document = model.apiDocumentState();
    const auto history = model.undoStack()->index();
    const auto historyCount = model.undoStack()->count();
    const auto frame = model.animationFrame();
    const auto session = model.animationSessionRevision();
    const auto evaluation = model.animationEvaluationId();
    const auto ids = model.scene()->idHighWatermarks();
    QSignalSpy failures(&model, &editor::SceneViewModel::operationFailed);
    REQUIRE_FALSE(model.replaceAnimation(typed));
    REQUIRE(model.animationMode() == Mode::Base);
    REQUIRE_FALSE(model.installedAnimationPose());
    REQUIRE(failures.count() == 1);
    REQUIRE(failures.last().at(0).toString().contains(QStringLiteral("NotReady")));
    unchangedDocument(model, document);
    REQUIRE(model.undoStack()->index() == history);
    REQUIRE(model.undoStack()->count() == historyCount);
    REQUIRE(model.animationFrame() == frame);
    REQUIRE(model.animationSessionRevision() == session);
    REQUIRE(model.animationEvaluationId() == evaluation);
    REQUIRE(model.scene()->idHighWatermarks() == ids);
    REQUIRE(model.scene()->animation() == animation);
}

TEST_CASE("Legacy Base and complete rational fps poses retain the original float consumer",
          "[animation][animation-session][j1-consumer]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto cube = model.createEntity(core::PrimitiveKind::Cube);
    auto animation = translation(cube);
    animation.properties.rationalFps = core::RationalFps{24, 1};
    REQUIRE(model.replaceAnimation(animation));
    QString error;
    std::shared_ptr<const renderer_gl::InstalledPose> display;
    REQUIRE(model.prepareDisplayPose(display, error));
    REQUIRE_FALSE(display);
    REQUIRE(model.animationEvaluationId() == 0);
    model.beginTransformEdit(cube);
    core::Transform temporary;
    temporary.position.x = 4;
    model.previewTransform(temporary);
    REQUIRE(model.prepareDisplayPose(display, error));
    REQUIRE_FALSE(display);
    REQUIRE(model.animationEvaluationId() == 0);
    model.cancelTransformEdit();
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(12.5));
    const auto pose = model.installedAnimationPose();
    REQUIRE(pose);
    REQUIRE(pose->coverage == renderer_gl::PoseStateCoverage::CompleteSceneJ1);
    REQUIRE(pose->consumerDomain == renderer_gl::PoseConsumerDomain::LegacyTrsV1);
    const renderer_gl::ScenePoseView view(*model.scene(), pose.get());
    REQUIRE(view.numerics() == pose->numerics.get());
    REQUIRE(view.worldMatrix(cube) == pose->numerics->find(cube)->world);
}

TEST_CASE("Legacy Base display does not acquire the animation node budget",
          "[animation][animation-session][j1-consumer]") {
    editor::SceneViewModel model;
    model.newScene();
    std::vector<core::SceneNode> nodes(core::kAnimationMaximumPoseNodes + 1);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        nodes[index].id = index + 1;
        nodes[index].name = "Legacy node";
    }
    auto scene = std::const_pointer_cast<core::Scene>(model.scene());
    REQUIRE(scene->replaceNodes(nodes));
    std::string diagnostic;
    REQUIRE_FALSE(scene->animationPoseInputs(diagnostic));
    QString error;
    std::shared_ptr<const renderer_gl::InstalledPose> display;
    REQUIRE(model.prepareDisplayPose(display, error));
    REQUIRE_FALSE(display);
    REQUIRE(error.isEmpty());
    REQUIRE(model.animationEvaluationId() == 0);
}

TEST_CASE("Joint Base freezes author defaults without sampling keys and shares its publication",
          "[animation][animation-session][j1-consumer]") {
    JointDisplaySession fixture;
    auto& model = fixture.model;
    const auto document = model.apiDocumentState();
    const auto history = model.undoStack()->index();
    const auto base = fixture.display();
    REQUIRE_FALSE(model.installedAnimationPose());
    REQUIRE(base->identity.mode == Mode::Base);
    REQUIRE(base->sceneState->find(fixture.cube)->localPosition.x == .25);
    REQUIRE(base->sceneState->content->materials.at(fixture.material).effective.roughness == .7);
    REQUIRE(base->sceneState->content->visibleSuns.front().effective.irradiance == 3);
    REQUIRE(base->sceneState->source.algorithmBuild.ends_with("/base-only-1"));
    REQUIRE(base->sceneState->source.evaluationProfile == core::EvaluationProfile::JointPreviewJ1);
    for (int read = 0; read < 6; ++read) {
        REQUIRE(fixture.display() == base);
        REQUIRE(model.animationEvaluationId() == base->identity.evaluationId);
    }
    unchangedDocument(model, document);
    REQUIRE(model.undoStack()->index() == history);
    REQUIRE(model.setAnimationPreview(true));
    const auto animatedAtOne = model.installedAnimationPose();
    REQUIRE_FALSE(core::isSameWorkSource(base->sceneState->source, animatedAtOne->sceneState->source));
    REQUIRE(model.setAnimationFrame(12.5));
    const auto animated = model.installedAnimationPose();
    REQUIRE(animated->sceneNumerics->find(fixture.cube)->local.position.x == 1);
    REQUIRE(animated->numerics->find(fixture.cube)->local.position.x == .25F);
    model.selectRay({{1, 0, 4}, {0, 0, -1}});
    REQUIRE(model.selection()->selectedEntity() == fixture.cube);
    REQUIRE(model.cursorSelectionCenter() == glm::vec3(1, 0, 0));
    REQUIRE(model.setAnimationPreview(false));
    const auto atMidpoint = fixture.display();
    REQUIRE(atMidpoint->identity.frame == 12.5);
    REQUIRE(atMidpoint->sceneState->find(fixture.cube)->localPosition.x == .25);
    REQUIRE(atMidpoint->sceneState->content->materials.at(fixture.material).effective.roughness == .7);
    REQUIRE(atMidpoint->sceneState->content->visibleSuns.front().effective.irradiance == 3);
    REQUIRE_FALSE(core::isSameWorkSource(atMidpoint->sceneState->source, animated->sceneState->source));
    REQUIRE(fixture.display() == atMidpoint);
    unchangedDocument(model, document);
}

TEST_CASE("Joint Base view visibility and formal revision each publish one new cache entry",
          "[animation][animation-session][j1-consumer]") {
    JointDisplaySession fixture;
    renderer_gl::EditorCamera camera;
    camera.setViewportSize(640, 480);
    fixture.model.setAnimationViewProvider([&] { return camera; });
    const auto previous = fixture.display();
    SECTION("Actual view changed") { camera.orbit(7, 0); }
    SECTION("Whole visibility changed") { REQUIRE(fixture.model.hideSelection()); }
    SECTION("Formal author revision changed") {
        core::Transform transform;
        transform.position.x = .75F;
        REQUIRE(fixture.model.setTransform(fixture.cube, transform));
    }
    SECTION("Rational fps changed") {
        auto changed = fixture.animation;
        changed.properties.rationalFps = core::RationalFps{30, 1};
        REQUIRE(fixture.model.replaceAnimation(changed));
    }
    REQUIRE_FALSE(fixture.model.cursorSelectionCenter());
    REQUIRE(fixture.model.animationEvaluationId() == previous->identity.evaluationId);
    const auto current = fixture.display();
    REQUIRE(current != previous);
    REQUIRE(current->identity.evaluationId == previous->identity.evaluationId + 1);
    REQUIRE(fixture.display() == current);
    REQUIRE(fixture.model.animationEvaluationId() == current->identity.evaluationId);
}

TEST_CASE("Joint Base rejects gestures busy invalid views and provider reentry without publication",
          "[animation][animation-session][j1-consumer]") {
    JointDisplaySession fixture;
    const auto previous = fixture.display();
    const auto document = fixture.model.apiDocumentState();
    const auto history = fixture.model.undoStack()->index();
    SECTION("Object gesture contains uncommitted live transform") {
        fixture.model.beginTransformEdit(fixture.cube);
        core::Transform temporary;
        temporary.position.x = 8;
        fixture.model.previewTransform(temporary);
    }
    SECTION("External busy") { fixture.model.setExternalBusy(QStringLiteral("j1_test"), true); }
    SECTION("Invalid provider view") {
        fixture.model.setAnimationViewProvider([] { throw std::bad_alloc{}; return renderer_gl::EditorCamera{}; });
    }
    SECTION("Provider reenters display") {
        fixture.model.setAnimationViewProvider([&] {
            std::shared_ptr<const renderer_gl::InstalledPose> inner;
            QString innerError;
            REQUIRE_FALSE(fixture.model.prepareDisplayPose(inner, innerError));
            REQUIRE_FALSE(inner);
            return renderer_gl::EditorCamera{};
        });
    }
    QString error;
    std::shared_ptr<const renderer_gl::InstalledPose> rejected;
    REQUIRE_FALSE(fixture.model.prepareDisplayPose(rejected, error));
    REQUIRE_FALSE(rejected);
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(fixture.model.animationEvaluationId() == previous->identity.evaluationId);
    unchangedDocument(fixture.model, document);
    REQUIRE(fixture.model.undoStack()->index() == history);
    fixture.model.setAnimationViewProvider({});
    fixture.model.setExternalBusy(QStringLiteral("j1_test"), false);
    fixture.model.cancelTransformEdit();
    REQUIRE(fixture.display() == previous);
}

TEST_CASE("J1 real clock uses 30000 over 1001 override instead of legacy fps one",
          "[animation][animation-session][j1-author][j1-clock]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    auto animation = translation(entity);
    animation.settings = {1, 1, 100000};
    animation.properties.rationalFps = core::RationalFps{30000, 1001};
    REQUIRE(model.replaceAnimation(animation));
    REQUIRE(model.setAnimationPreview(true));
    const auto document = model.apiDocumentState();
    const auto history = model.undoStack()->index();
    QElapsedTimer bounds;
    bounds.start();
    REQUIRE(model.playAnimation());
    const auto latestStart = bounds.nsecsElapsed();
    QTest::qWait(85);
    const auto earliestSample = bounds.nsecsElapsed();
    REQUIRE(model.pauseAnimation());
    const auto latestSample = bounds.nsecsElapsed();
    const auto rate = 30000.0 / 1001.0;
    const auto lower = (earliestSample - latestStart) / 1.0e9 * rate;
    const auto upper = latestSample / 1.0e9 * rate;
    REQUIRE(model.animationFrame() >= 1 + lower - 1e-9);
    REQUIRE(model.animationFrame() <= 1 + upper + 1e-9);
    REQUIRE(model.animationFrame() > 2);
    REQUIRE(model.installedAnimationPose()->sceneState->source.rationalFps ==
            *animation.properties.rationalFps);
    unchangedDocument(model, document);
    REQUIRE(model.undoStack()->index() == history);
    validIdentity(model);
}

TEST_CASE("J1 inclusive real clock resumes end holds a full frame then wraps",
          "[animation][animation-session][j1-author][j1-clock]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    auto animation = translation(entity);
    animation.settings = {120, 1, 2};
    animation.properties.rationalFps = core::RationalFps{1, 1};
    animation.properties.loopPolicy = core::AnimationLoopPolicy::InclusiveFrameStep;
    REQUIRE(model.replaceAnimation(animation));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationLoop(true));
    REQUIRE(model.setAnimationFrame(2));
    const auto document = model.apiDocumentState();
    REQUIRE(model.playAnimation());
    REQUIRE(model.animationFrame() == 2);
    QTest::qWait(60);
    REQUIRE(model.animationMode() == Mode::Playing);
    REQUIRE(model.animationFrame() == 2);
    QTest::qWait(1050);
    REQUIRE(model.pauseAnimation());
    REQUIRE(model.animationFrame() > 1);
    REQUIRE(model.animationFrame() < 2);
    unchangedDocument(model, document);
    validIdentity(model);
    REQUIRE(model.setAnimationLoop(false));
    REQUIRE(model.setAnimationFrame(2));
    REQUIRE(model.playAnimation());
    REQUIRE(model.animationFrame() == 2);
    QTest::qWait(45);
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE(model.animationFrame() == 2);
    unchangedDocument(model, document);
}

TEST_CASE("J1 inclusive real clock keeps end 100000 and single frame while legacy restarts",
          "[animation][animation-session][j1-author][j1-clock]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    auto animation = translation(entity);
    animation.settings = {1, 1, 100000};
    animation.properties.loopPolicy = core::AnimationLoopPolicy::InclusiveFrameStep;
    REQUIRE(model.replaceAnimation(animation));
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationLoop(true));
    REQUIRE(model.setAnimationFrame(100000));
    const auto document = model.apiDocumentState();
    REQUIRE(model.playAnimation());
    REQUIRE(model.animationFrame() == 100000);
    QTest::qWait(45);
    REQUIRE(model.animationFrame() == 100000);
    REQUIRE(model.pauseAnimation());
    unchangedDocument(model, document);
    animation.settings = {1, 100000, 100000};
    REQUIRE(model.replaceAnimation(animation));
    const auto singleDocument = model.apiDocumentState();
    const auto session = model.animationSessionRevision(), evaluation = model.animationEvaluationId();
    REQUIRE(model.playAnimation());
    QTest::qWait(30);
    REQUIRE(model.animationMode() == Mode::PreviewPaused);
    REQUIRE(model.animationFrame() == 100000);
    REQUIRE(model.animationSessionRevision() == session);
    REQUIRE(model.animationEvaluationId() == evaluation);
    unchangedDocument(model, singleDocument);
    animation.settings = {1, 1, 2};
    animation.properties.loopPolicy = core::AnimationLoopPolicy::LegacyHalfOpen;
    REQUIRE(model.replaceAnimation(animation));
    REQUIRE(model.setAnimationFrame(2));
    const auto legacyDocument = model.apiDocumentState();
    REQUIRE(model.playAnimation());
    REQUIRE(model.animationFrame() == 1);
    QTest::qWait(30);
    REQUIRE(model.pauseAnimation());
    REQUIRE(model.animationFrame() >= 1);
    REQUIRE(model.animationFrame() < 2);
    unchangedDocument(model, legacyDocument);
    validIdentity(model);
}
