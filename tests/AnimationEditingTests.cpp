/*
 * 模块名: AnimationEditingTests
 * 功能概述: 验证登记属性单笔编辑、共享动作边界、精确历史及差量时间预检。
 * 对外接口: Catch2 [animation][animation-editing][j1-contract] 定向测试。
 * 依赖关系: 真实 SceneViewModel、AnimationEditing、完整 InstalledPose；无 GL 或 UI。
 * 输入输出: 显式属性命令到正式作者、稳定 ID、历史及完整 double 场景状态断言。
 * 异常与错误: 准备/提交拒绝不能发布部分内容、姿态、版本或 ID 水位。
 * 维护说明: Core 写入口仅初始化材质/Sun fixture；被测编辑全部经服务 commit。
 */
#include "editor/AnimationEditing.h"
#include "editor/SceneViewModel.h"
#include "renderer_gl/InstalledPose.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace mini3d;
namespace {
using Interpolation = core::PropertyInterpolation;
using Component = core::PropertyComponent;
using Property = core::PropertyId;
using Linked = editor::LinkedActionEdit;
namespace api = editor::api;

struct Snapshot {
    std::shared_ptr<const core::Scene> scene;
    core::SceneAnimation animation;
    core::SceneIdHighWatermarks ids;
    api::DocumentState document;
    int historyIndex = 0;
    int historyCount = 0;
    bool canRedo = false;
    core::FrameTime frame = 1;
    std::uint64_t session = 0;
    std::uint64_t evaluation = 0;
    editor::AnimationMode mode = editor::AnimationMode::Base;
    std::shared_ptr<const renderer_gl::InstalledPose> pose;
};

Snapshot snapshot(const editor::SceneViewModel& model) {
    return {model.scene(), model.scene()->animation(), model.scene()->idHighWatermarks(),
            model.apiDocumentState(), model.undoStack()->index(), model.undoStack()->count(),
            model.undoStack()->canRedo(), model.animationFrame(), model.animationSessionRevision(),
            model.animationEvaluationId(), model.animationMode(), model.installedAnimationPose()};
}

void requireUnchanged(const editor::SceneViewModel& model, const Snapshot& before) {
    REQUIRE(model.scene() == before.scene);
    REQUIRE(model.scene()->animation() == before.animation);
    REQUIRE(model.scene()->idHighWatermarks() == before.ids);
    REQUIRE(model.apiDocumentState().document == before.document.document);
    REQUIRE(model.apiDocumentState().documentRevision == before.document.documentRevision);
    REQUIRE(model.apiDocumentState().historyRevision == before.document.historyRevision);
    REQUIRE(model.undoStack()->index() == before.historyIndex);
    REQUIRE(model.undoStack()->count() == before.historyCount);
    REQUIRE(model.undoStack()->canRedo() == before.canRedo);
    REQUIRE(model.animationFrame() == before.frame);
    REQUIRE(model.animationSessionRevision() == before.session);
    REQUIRE(model.animationEvaluationId() == before.evaluation);
    REQUIRE(model.animationMode() == before.mode);
    REQUIRE(model.installedAnimationPose() == before.pose);
    if (before.pose && before.pose->sceneState)
        REQUIRE(model.installedAnimationPose()->sceneState->source == before.pose->sceneState->source);
}

core::PropertyAddress address(const editor::SceneViewModel& model, core::OwnerRef owner,
                              Property property, Component component) {
    return {model.scene()->documentUuid(), std::move(owner), property, component};
}

const core::ScalarCurve* curveAt(const core::SceneAnimation& animation,
                                 const core::PropertyAddress& target) {
    for (const auto& binding : animation.properties.bindings) {
        if (binding.owner != target.owner || binding.documentUuid != target.documentUuid)
            continue;
        for (const auto& action : animation.properties.actions) {
            if (action.id != binding.action)
                continue;
            for (const auto& slot : action.actionSlots) {
                if (slot.id != binding.slot)
                    continue;
                for (const auto& curve : slot.curves)
                    if (curve.property == target.property && curve.component == target.component)
                        return &curve;
            }
        }
    }
    return nullptr;
}

void putKey(editor::SceneViewModel& model, editor::AnimationEditing& service,
            const core::PropertyAddress& target, core::FrameTime time, double value,
            Interpolation outgoing = Interpolation::Linear,
            Linked linked = Linked::RejectShared) {
    const auto before = snapshot(model);
    QString error;
    REQUIRE(service.prepareKey(target, time, value, outgoing, error, linked));
    REQUIRE(error.isEmpty());
    requireUnchanged(model, before);
    const bool committed = service.commit(QStringLiteral("登记属性录键"), error);
    INFO(error.toStdString());
    REQUIRE(committed);
    REQUIRE(error.isEmpty());
    REQUIRE_FALSE(service.hasChanges());
}

const core::EvaluatedSceneState& completeState(const editor::SceneViewModel& model) {
    const auto pose = model.installedAnimationPose();
    REQUIRE(pose);
    REQUIRE(pose->coverage == renderer_gl::PoseStateCoverage::CompleteSceneJ1);
    REQUIRE(pose->sceneState);
    REQUIRE(core::isSourceIdentityValid(pose->sceneState->source));
    REQUIRE(pose->sceneState->source.contentRevision == model.apiDocumentState().documentRevision);
    REQUIRE(pose->sceneState->source.frameTime == model.animationFrame());
    REQUIRE(pose->sceneState->source.evaluationId == model.animationEvaluationId());
    return *pose->sceneState;
}

core::SceneAnimation typedAnimation(const core::DocumentUuid& document, core::EntityId entity,
                                    std::vector<core::ScalarKey> keys) {
    core::SceneAnimation animation;
    animation.properties.actions = {{core::ActionId{1}, {{core::ActionSlotId{1},
        core::PropertyOwnerKind::Entity,
        {{core::CurveId{1}, Property::Position, Component::X, std::move(keys)}}}}}};
    animation.properties.bindings = {
        {document, core::EntityOwner{entity}, core::ActionId{1}, core::ActionSlotId{1}}};
    return animation;
}

void initializeMaterialAndSun(editor::SceneViewModel& model, core::EntityId mesh,
                               core::EntityId sun) {
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
    REQUIRE(scene->setMaterialSlots(mesh, {{core::MaterialSlotId{1}, material.id}}));
    REQUIRE(scene->setSun(sun, {}));
}
} // namespace

TEST_CASE("Typed editing samples XYZ independently with exact endpoints and subframes",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    auto base = model.scene()->find(entity)->transform;
    base.position = {101, 202, 303};
    REQUIRE(model.setTransform(entity, base));
    core::SceneAnimation seed;
    seed.settings = {30, 1, 100};
    seed.properties.rationalFps = core::RationalFps{30000, 1001};
    seed.properties.loopPolicy = core::AnimationLoopPolicy::LegacyHalfOpen;
    seed.tracks[{entity, core::AnimationChannel::Scale}] = {{{1, {1, 1, 1}}, {24, {1, 1, 1}}}};
    REQUIRE(model.replaceAnimation(seed));
    editor::AnimationEditing service(model);
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    const auto y = address(model, core::EntityOwner{entity}, Property::Position, Component::Y);
    const auto z = address(model, core::EntityOwner{entity}, Property::Position, Component::Z);
    putKey(model, service, x, 1, 2, Interpolation::Constant);
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(completeState(model).find(entity)->localPosition == glm::dvec3(2, 202, 303));
    const auto xOnly = *curveAt(model.scene()->animation(), x);
    putKey(model, service, y, 1, 3);
    REQUIRE(*curveAt(model.scene()->animation(), x) == xOnly);
    REQUIRE(completeState(model).find(entity)->localPosition == glm::dvec3(2, 3, 303));
    putKey(model, service, z, 1, 4);
    putKey(model, service, x, 24, 8);
    putKey(model, service, y, 24, 26);
    putKey(model, service, z, 24, 50);
    const auto formal = model.scene()->animation();
    const auto document = model.apiDocumentState();
    const auto history = model.undoStack()->index();
    const auto session = completeState(model).source.evaluationSessionId;
    for (const auto time : {1.0, 12.5, 24.0, 12.5, 1.0}) {
        REQUIRE(model.setAnimationFrame(time));
        const auto& state = completeState(model);
        const double fraction = (time - 1) / 23;
        const glm::dvec3 expected{time == 24 ? 8 : 2, std::lerp(3.0, 26.0, fraction),
                                 std::lerp(4.0, 50.0, fraction)};
        REQUIRE(state.find(entity)->localPosition == expected);
        REQUIRE(glm::dvec3(state.find(entity)->worldAffine[3]) == expected);
        REQUIRE(state.source.evaluationSessionId == session);
        REQUIRE(model.scene()->animation() == formal);
        REQUIRE(model.apiDocumentState().documentRevision == document.documentRevision);
        REQUIRE(model.apiDocumentState().historyRevision == document.historyRevision);
        REQUIRE(model.undoStack()->index() == history);
    }
    REQUIRE(model.scene()->animation().settings == seed.settings);
    REQUIRE(model.scene()->animation().properties.rationalFps == seed.properties.rationalFps);
    REQUIRE(model.scene()->animation().properties.loopPolicy == seed.properties.loopPolicy);
    REQUIRE(model.scene()->animation().tracks == seed.tracks);
    REQUIRE(model.scene()->find(entity)->transform.position == base.position);
    REQUIRE(model.scene()->animation().properties.actions.size() == 1);
    REQUIRE(model.scene()->animation().properties.actions.front().actionSlots.size() == 1);
    REQUIRE(model.scene()->animation().properties.bindings.size() == 1);
}

TEST_CASE("Typed editing preserves binary64 values and stable IDs through exact undo redo",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Empty);
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    editor::AnimationEditing service(model);
    putKey(model, service, x, 1, 1);
    putKey(model, service, x, 24, 24);
    const auto original = model.scene()->animation();
    const auto keyId = curveAt(original, x)->keys.front().id;
    const auto ids = model.scene()->idHighWatermarks();
    const double precise = std::nextafter(1.0, 2.0);
    putKey(model, service, x, 1, precise);
    REQUIRE(curveAt(model.scene()->animation(), x)->keys.front().id == keyId);
    REQUIRE(curveAt(model.scene()->animation(), x)->keys.front().value == precise);
    REQUIRE(model.scene()->idHighWatermarks() == ids);
    putKey(model, service, x, 12.5, 0.10000000000000002, Interpolation::Constant);
    const auto edited = model.scene()->animation();
    const auto published = model.scene()->idHighWatermarks();
    REQUIRE(curveAt(edited, x)->keys.at(1).time == 12.5);
    REQUIRE(curveAt(edited, x)->keys.at(1).id.value == ids.key + 1);
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(12.5));
    REQUIRE(completeState(model).find(entity)->localPosition.x == 0.10000000000000002);
    model.undo();
    REQUIRE(curveAt(model.scene()->animation(), x)->keys.size() == 2);
    REQUIRE(curveAt(model.scene()->animation(), x)->keys.front().value == precise);
    REQUIRE(model.scene()->idHighWatermarks() == published);
    model.undo();
    REQUIRE(model.scene()->animation() == original);
    REQUIRE(model.scene()->idHighWatermarks() == published);
    model.redo();
    model.redo();
    REQUIRE(model.scene()->animation() == edited);
    REQUIRE(model.scene()->idHighWatermarks() == published);
    REQUIRE(completeState(model).find(entity)->localPosition.x == 0.10000000000000002);
}

TEST_CASE("Typed prepare is single intent and cancel does not simulate undo",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Empty);
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.setAnimationFrame(12.5));
    editor::AnimationEditing service(model);
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    const auto y = address(model, core::EntityOwner{entity}, Property::Position, Component::Y);
    const auto before = snapshot(model);
    QString error;
    REQUIRE(service.prepareKey(x, 1, 7, Interpolation::Linear, error));
    REQUIRE(service.hasChanges());
    requireUnchanged(model, before);
    REQUIRE(service.prepareKey(y, 12.5, 9, Interpolation::Constant, error));
    requireUnchanged(model, before);
    REQUIRE(service.commit(QStringLiteral("仅提交最后准备意图"), error));
    REQUIRE_FALSE(curveAt(model.scene()->animation(), x));
    REQUIRE(curveAt(model.scene()->animation(), y)->keys.front().value == 9);
    REQUIRE(model.scene()->idHighWatermarks().key == before.ids.key + 1);
    REQUIRE(model.undoStack()->index() == before.historyIndex + 1);
    const auto committed = snapshot(model);
    REQUIRE(service.prepareKey(x, 24, 8, Interpolation::Linear, error));
    requireUnchanged(model, committed);
    service.cancel();
    REQUIRE_FALSE(service.hasChanges());
    requireUnchanged(model, committed);
    REQUIRE_FALSE(service.commit(QStringLiteral("不存在候选"), error));
    REQUIRE_FALSE(error.isEmpty());
    requireUnchanged(model, committed);
}

TEST_CASE("Typed exact no change preserves redo and still runs the final guard",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Empty);
    editor::AnimationEditing service(model);
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    putKey(model, service, x, 1, 7);
    putKey(model, service, x, 24, 9);
    const auto after = model.scene()->animation();
    model.undo();
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.undoStack()->canRedo());
    const auto before = snapshot(model);
    QString error;
    REQUIRE(service.prepareKey(x, 1, 7, Interpolation::Linear, error));
    REQUIRE_FALSE(service.hasChanges());
    requireUnchanged(model, before);
    REQUIRE(service.commit(QStringLiteral("精确 no_change"), error));
    requireUnchanged(model, before);
    REQUIRE_FALSE(service.commit(QStringLiteral("成功已释放候选"), error));
    REQUIRE(service.prepareKeyRemoval(x, 77.5, error));
    REQUIRE_FALSE(service.hasChanges());
    int guardCalls = 0;
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        ++guardCalls;
        return api::ApiError{api::ErrorCode::DeadlineExceeded, QStringLiteral("拒绝 no_change"), {},
                             api::Recovery::None, model.apiDocumentState()};
    });
    REQUIRE_FALSE(service.commit(QStringLiteral("守卫仍有效"), error));
    REQUIRE(guardCalls == 1);
    REQUIRE(error == QStringLiteral("拒绝 no_change"));
    requireUnchanged(model, before);
    model.exchangeBeforeCommitGuard({});
    REQUIRE(service.commit(QStringLiteral("缺失键仍为 no_change"), error));
    requireUnchanged(model, before);
    model.redo();
    REQUIRE(model.scene()->animation() == after);
}

TEST_CASE("Typed invalid prepare preserves the previous private intent and all model state",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    const auto sun = model.createEntity(core::PrimitiveKind::Empty);
    initializeMaterialAndSun(model, entity, sun);
    REQUIRE(model.setAnimationPreview(true));
    editor::AnimationEditing service(model);
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    QString error;
    REQUIRE(service.prepareKey(x, 1, 8, Interpolation::Linear, error));
    const auto before = snapshot(model);
    for (const auto time : {0.0, 100000.5, std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        REQUIRE_FALSE(service.prepareKey(x, time, 9, Interpolation::Linear, error));
        REQUIRE_FALSE(service.prepareKeyRemoval(x, time, error));
        REQUIRE_FALSE(error.isEmpty());
        REQUIRE(service.hasChanges());
        requireUnchanged(model, before);
    }
    for (const auto value : {1.0e40, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()}) {
        REQUIRE_FALSE(service.prepareKey(x, 1, value, Interpolation::Linear, error));
        requireUnchanged(model, before);
    }
    auto wrongDocument = x;
    wrongDocument.documentUuid.bytes[0] ^= 1;
    const std::vector<core::PropertyAddress> invalidAddresses{
        wrongDocument,
        address(model, core::EntityOwner{0}, Property::Position, Component::X),
        address(model, core::EntityOwner{999999}, Property::Position, Component::X),
        address(model, core::MaterialOwner{core::MaterialId{31}}, Property::Position, Component::X),
        address(model, core::EntityOwner{entity}, Property::PbrRoughness, Component::Scalar),
        address(model, core::EntityOwner{entity}, Property::SunIrradiance, Component::Scalar),
        address(model, core::MaterialOwner{core::MaterialId{99}}, Property::PbrRoughness, Component::Scalar),
        address(model, core::EntityOwner{entity}, Property::Position, Component::Scalar),
        address(model, core::EntityOwner{entity}, Property::RotationEulerXYZDegrees, Component::X)};
    for (const auto& target : invalidAddresses) {
        REQUIRE_FALSE(service.prepareKey(target, 1, .5, Interpolation::Linear, error));
        REQUIRE_FALSE(service.prepareKeyRemoval(target, 1, error));
        REQUIRE_FALSE(error.isEmpty());
        requireUnchanged(model, before);
    }
    const auto roughness = address(model, core::MaterialOwner{core::MaterialId{31}},
                                    Property::PbrRoughness, Component::Scalar);
    const auto irradiance = address(model, core::EntityOwner{sun}, Property::SunIrradiance,
                                     Component::Scalar);
    for (const auto value : {-.1, 1.1})
        REQUIRE_FALSE(service.prepareKey(roughness, 1, value, Interpolation::Linear, error));
    REQUIRE_FALSE(service.prepareKey(irradiance, 1, -1, Interpolation::Linear, error));
    REQUIRE_FALSE(service.prepareKey(irradiance, 1, 1e40, Interpolation::Linear, error));
    REQUIRE_FALSE(service.prepareKey(x, 1, 9, Interpolation::Bezier, error));
    REQUIRE_FALSE(service.prepareKey(x, 1, 9, Interpolation::Linear, error,
                                      static_cast<Linked>(99)));
    REQUIRE(service.hasChanges());
    requireUnchanged(model, before);
    REQUIRE(service.commit(QStringLiteral("仅原成功候选可以提交"), error));
    REQUIRE(curveAt(model.scene()->animation(), x)->keys.front().value == 8);
}

TEST_CASE("Typed commit rejects stale document revisions authors and IDs without side effects",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Empty);
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    editor::AnimationEditing service(model);
    QString error;
    REQUIRE(service.prepareKey(x, 1, 7, Interpolation::Linear, error));
    SECTION("Document replacement invalidates the original handle") {
        model.newScene();
    }
    SECTION("Unrelated author revision invalidates the old version") {
        REQUIRE(model.renameEntity(entity, QStringLiteral("新的作者版本")));
    }
    SECTION("A separately committed author consumes the reserved stable identity") {
        editor::AnimationEditing other(model);
        putKey(model, other, x, 1, 11);
    }
    const auto before = snapshot(model);
    REQUIRE_FALSE(service.commit(QStringLiteral("过期候选不可提交"), error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(service.hasChanges());
    requireUnchanged(model, before);
    service.cancel();
    requireUnchanged(model, before);
}

TEST_CASE("Typed Position never silently migrates an existing legacy Position author",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Empty);
    core::SceneAnimation seed;
    seed.tracks[{entity, core::AnimationChannel::Position}] = {{{1, {7, 8, 9}}}};
    REQUIRE(model.replaceAnimation(seed));
    editor::AnimationEditing service(model);
    const auto target = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    const auto before = snapshot(model);
    QString error;
    REQUIRE_FALSE(service.prepareKey(target, 1, 11, Interpolation::Linear, error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE_FALSE(service.hasChanges());
    requireUnchanged(model, before);
}

TEST_CASE("Typed commit uses the real ViewModel guard and mode admission",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    editor::AnimationEditing service(model);
    QString error;
    REQUIRE(service.prepareKey(x, 1, 7, Interpolation::Linear, error));
    SECTION("Final guard rejects a changed candidate") {
        model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            return api::ApiError{api::ErrorCode::DeadlineExceeded, QStringLiteral("真实提交守卫拒绝"), {},
                                 api::Recovery::None, model.apiDocumentState()};
        });
    }
    SECTION("Edit mode rejects formal animation commands") {
        REQUIRE(model.setEditMode(true));
    }
    SECTION("Playing rejects formal animation commands") {
        REQUIRE(model.setAnimationPreview(true));
        REQUIRE(model.playAnimation());
    }
    SECTION("Pose draft rejects formal animation commands") {
        REQUIRE(model.setAnimationPreview(true));
        REQUIRE(model.beginAnimationDraft(entity, {true, false, false}));
    }
    const auto before = snapshot(model);
    REQUIRE_FALSE(service.commit(QStringLiteral("未获提交许可"), error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(service.hasChanges());
    requireUnchanged(model, before);
}

TEST_CASE("Typed roughness and Sun irradiance in watts per square metre stay independent",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto mesh = model.createEntity(core::PrimitiveKind::Cube);
    const auto sun = model.createEntity(core::PrimitiveKind::Empty);
    initializeMaterialAndSun(model, mesh, sun);
    const auto surface = model.scene()->find(mesh)->surface;
    const auto lighting = model.scene()->lighting();
    const auto roughness = address(model, core::MaterialOwner{core::MaterialId{31}},
                                    Property::PbrRoughness, Component::Scalar);
    const auto irradiance = address(model, core::EntityOwner{sun}, Property::SunIrradiance,
                                     Component::Scalar);
    editor::AnimationEditing service(model);
    putKey(model, service, roughness, 1, .2, Interpolation::Constant);
    putKey(model, service, roughness, 24, .8);
    putKey(model, service, irradiance, 1, 100);
    putKey(model, service, irradiance, 24, 300);
    REQUIRE(model.setAnimationPreview(true));
    const auto formal = model.scene()->animation();
    for (const auto time : {1.0, 12.5, 24.0, 1.0}) {
        REQUIRE(model.setAnimationFrame(time));
        const auto& state = completeState(model);
        REQUIRE(state.content->materials.at(core::MaterialId{31}).effective.roughness ==
                (time == 24 ? .8 : .2));
        REQUIRE(state.content->materials.at(core::MaterialId{31}).author.parameters.roughness == .7);
        REQUIRE(state.content->visibleSuns.size() == 1);
        REQUIRE(state.content->visibleSuns.front().effective.irradiance ==
                std::lerp(100.0, 300.0, (time - 1) / 23));
        REQUIRE(state.content->visibleSuns.front().author.irradiance == 1);
        REQUIRE(model.scene()->animation() == formal);
    }
    putKey(model, service, roughness, 1, .2, Interpolation::Linear);
    REQUIRE(model.setAnimationFrame(12.5));
    REQUIRE(std::abs(completeState(model).content->materials.at(core::MaterialId{31})
                         .effective.roughness - .5) <= 1e-12);
    REQUIRE(model.scene()->materialData().find(core::MaterialId{31})->parameters.roughness == .7);
    REQUIRE(model.scene()->find(sun)->sun->irradiance == 1);
    REQUIRE(model.scene()->find(mesh)->surface == surface);
    REQUIRE(model.scene()->lighting() == lighting);
}

TEST_CASE("Deleting a single owner final key removes only newly orphaned definitions",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Empty);
    core::SceneAnimation seed;
    seed.properties.actions = {{core::ActionId{8},
        {{core::ActionSlotId{8}, core::PropertyOwnerKind::Entity, {}}}}};
    REQUIRE(model.replaceAnimation(seed));
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    const auto y = address(model, core::EntityOwner{entity}, Property::Position, Component::Y);
    editor::AnimationEditing service(model);
    putKey(model, service, x, 12.5, 7);
    SECTION("Empty action and its slot binding are removed") {}
    SECTION("Other curves preserve the existing action slot and binding") {
        putKey(model, service, y, 1, 9);
    }
    const auto before = model.scene()->animation();
    const auto ids = model.scene()->idHighWatermarks();
    QString error;
    REQUIRE(service.prepareKeyRemoval(x, 12.5, error));
    REQUIRE(service.hasChanges());
    REQUIRE(service.commit(QStringLiteral("删除末键"), error));
    const auto after = model.scene()->animation();
    REQUIRE_FALSE(curveAt(after, x));
    REQUIRE(after.properties.actions.front() == seed.properties.actions.front());
    REQUIRE(model.scene()->idHighWatermarks() == ids);
    if (curveAt(before, y)) {
        REQUIRE(*curveAt(after, y) == *curveAt(before, y));
        REQUIRE(after.properties.bindings == before.properties.bindings);
        REQUIRE(after.properties.actions.size() == 2);
    } else {
        REQUIRE(after == seed);
    }
    model.undo();
    REQUIRE(model.scene()->animation() == before);
    REQUIRE(model.scene()->idHighWatermarks() == ids);
    model.redo();
    REQUIRE(model.scene()->animation() == after);
    REQUIRE(model.scene()->idHighWatermarks() == ids);
}

TEST_CASE("Shared curves require explicit linked editing and cannot lose their final key",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto first = model.createEntity(core::PrimitiveKind::Empty);
    const auto second = model.createEntity(core::PrimitiveKind::Empty);
    auto seed = typedAnimation(model.scene()->documentUuid(), first,
        {{core::KeyId{1}, 1, 1}, {core::KeyId{2}, 24, 24}});
    seed.properties.bindings.push_back({model.scene()->documentUuid(), core::EntityOwner{second},
        core::ActionId{1}, core::ActionSlotId{1}});
    REQUIRE(model.replaceAnimation(seed));
    REQUIRE(model.setAnimationPreview(true));
    const auto x = address(model, core::EntityOwner{first}, Property::Position, Component::X);
    editor::AnimationEditing service(model);
    QString error;
    const auto original = snapshot(model);
    REQUIRE_FALSE(service.prepareKey(x, 1, 7, Interpolation::Linear, error));
    REQUIRE_FALSE(service.prepareKeyRemoval(x, 24, error));
    REQUIRE_FALSE(service.hasChanges());
    requireUnchanged(model, original);
    putKey(model, service, x, 1, 7, Interpolation::Linear, Linked::EditLinked);
    const auto edited = model.scene()->animation();
    REQUIRE(completeState(model).find(first)->localPosition.x == 7);
    REQUIRE(completeState(model).find(second)->localPosition.x == 7);
    REQUIRE(edited.properties.bindings == seed.properties.bindings);
    REQUIRE(curveAt(edited, x)->keys.front().id == core::KeyId{1});
    model.undo();
    REQUIRE(model.scene()->animation() == seed);
    REQUIRE(completeState(model).find(second)->localPosition.x == 1);
    const auto redoRetained = snapshot(model);
    REQUIRE(redoRetained.canRedo);
    REQUIRE(service.prepareKey(x, 1, 1, Interpolation::Linear, error));
    REQUIRE_FALSE(service.hasChanges());
    REQUIRE(service.commit(QStringLiteral("共享同值不要求写许可"), error));
    requireUnchanged(model, redoRetained);
    REQUIRE(service.prepareKeyRemoval(x, 77.5, error));
    REQUIRE_FALSE(service.hasChanges());
    REQUIRE(service.commit(QStringLiteral("共享缺失键是 no_change"), error));
    requireUnchanged(model, redoRetained);
    model.redo();
    REQUIRE(model.scene()->animation() == edited);
    REQUIRE(service.prepareKeyRemoval(x, 24, error, Linked::EditLinked));
    REQUIRE(service.commit(QStringLiteral("显式共同删非末键"), error));
    REQUIRE(curveAt(model.scene()->animation(), x)->keys.size() == 1);
    const auto oneKey = snapshot(model);
    REQUIRE_FALSE(service.prepareKeyRemoval(x, 1, error, Linked::EditLinked));
    REQUIRE_FALSE(service.hasChanges());
    requireUnchanged(model, oneKey);
}

TEST_CASE("New curves never attach implicitly to shared or ambiguous owner slots",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto first = model.createEntity(core::PrimitiveKind::Empty);
    const auto second = model.createEntity(core::PrimitiveKind::Empty);
    auto seed = typedAnimation(model.scene()->documentUuid(), first, {{core::KeyId{1}, 1, 1}});
    SECTION("Shared existing slot stays unchanged and a later axis reuses the private slot") {
        seed.properties.bindings.push_back({model.scene()->documentUuid(), core::EntityOwner{second},
            core::ActionId{1}, core::ActionSlotId{1}});
    }
    SECTION("Two exclusive slots are ambiguous and remain unchanged") {
        seed.properties.actions.front().actionSlots.push_back({core::ActionSlotId{2},
            core::PropertyOwnerKind::Entity, {{core::CurveId{2}, Property::Position,
                Component::Y, {{core::KeyId{2}, 1, 2}}}}});
        seed.properties.bindings.push_back({model.scene()->documentUuid(), core::EntityOwner{first},
            core::ActionId{1}, core::ActionSlotId{2}});
    }
    REQUIRE(model.replaceAnimation(seed));
    editor::AnimationEditing service(model);
    const auto z = address(model, core::EntityOwner{first}, Property::Position, Component::Z);
    putKey(model, service, z, 1, 5);
    REQUIRE(model.scene()->animation().properties.actions.size() == 2);
    REQUIRE(model.scene()->animation().properties.actions.front() == seed.properties.actions.front());
    REQUIRE(model.scene()->animation().properties.bindings.size() == seed.properties.bindings.size() + 1);
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(completeState(model).find(first)->localPosition.z == 5);
    REQUIRE(completeState(model).find(second)->localPosition.z == 0);
    if (seed.properties.bindings.back().owner == core::OwnerRef{core::EntityOwner{second}}) {
        const auto y = address(model, core::EntityOwner{first}, Property::Position, Component::Y);
        putKey(model, service, y, 1, 9);
        REQUIRE(model.scene()->animation().properties.actions.size() == 2);
        REQUIRE(model.scene()->animation().properties.actions.back().actionSlots.front().curves.size() == 2);
        REQUIRE(completeState(model).find(second)->localPosition.y == 0);
    }
}

TEST_CASE("Typed stable identity exhaustion never publishes a wrapped or consumed ID",
          "[animation][animation-editing][j1-contract]") {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    for (const auto category : {0, 1, 2, 3}) {
        DYNAMIC_SECTION("Exhausted ID category " << category) {
            editor::SceneViewModel model;
            model.newScene();
            const auto first = model.createEntity(core::PrimitiveKind::Empty);
            const auto second = model.createEntity(core::PrimitiveKind::Empty);
            auto seed = typedAnimation(model.scene()->documentUuid(), first,
                {{core::KeyId{category == 0 ? maximum : 1}, 1, 1}});
            auto& action = seed.properties.actions.front();
            if (category == 1)
                action.actionSlots.front().curves.front().id = core::CurveId{maximum};
            if (category == 2)
                action.id = core::ActionId{maximum};
            if (category == 3)
                action.actionSlots.front().id = core::ActionSlotId{maximum};
            seed.properties.bindings.front().action = action.id;
            seed.properties.bindings.front().slot = action.actionSlots.front().id;
            REQUIRE(model.replaceAnimation(seed));
            editor::AnimationEditing service(model);
            const auto existing = address(model, core::EntityOwner{first}, Property::Position, Component::X);
            putKey(model, service, existing, 1, 2);
            const auto before = snapshot(model);
            auto target = existing;
            double time = 24;
            if (category == 1)
                target.component = Component::Y;
            if (category >= 2)
                target.owner = core::EntityOwner{second};
            QString error;
            REQUIRE_FALSE(service.prepareKey(target, time, 3, Interpolation::Linear, error));
            REQUIRE(error.contains(QStringLiteral("耗尽")));
            REQUIRE_FALSE(service.hasChanges());
            requireUnchanged(model, before);
        }
    }
}

TEST_CASE("Typed distant checked float failure rejects real commit before any publication",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto parent = model.createEntity(core::PrimitiveKind::Empty);
    const auto child = model.createEntity(core::PrimitiveKind::Empty);
    auto transform = model.scene()->find(parent)->transform;
    // 基线须能被旧 float 姿态消费；只让新增远端键的 double 世界位移溢出 float。
    transform.scale = glm::vec3(1e10F);
    REQUIRE(model.setTransform(parent, transform));
    REQUIRE(model.setParent(child, parent));
    const auto x = address(model, core::EntityOwner{child}, Property::Position, Component::X);
    editor::AnimationEditing service(model);
    putKey(model, service, x, 1, 0);
    REQUIRE(model.setAnimationPreview(true));
    REQUIRE(model.animationFrame() == 1);
    const auto before = snapshot(model);
    QString error;
    REQUIRE(service.prepareKey(x, 24, 1e30, Interpolation::Linear, error));
    requireUnchanged(model, before);
    REQUIRE_FALSE(service.commit(QStringLiteral("非当前键同样必须可消费"), error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(service.hasChanges());
    requireUnchanged(model, before);
    service.cancel();
    requireUnchanged(model, before);
}

TEST_CASE("Typed external resources remain NotReady through the actual service commit",
          "[animation][animation-editing][j1-contract]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.importGltf(QString::fromUtf8(MINI3D_SAMPLE_DIRECTORY) + "/BoxTextured.glb");
    REQUIRE(entity);
    editor::AnimationEditing service(model);
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    const auto before = snapshot(model);
    QString error;
    REQUIRE(service.prepareKey(x, 12.5, 1, Interpolation::Linear, error));
    REQUIRE_FALSE(service.commit(QStringLiteral("不能发布部分 typed 状态"), error));
    REQUIRE(error.contains(QStringLiteral("NotReady")));
    REQUIRE(service.hasChanges());
    requireUnchanged(model, before);
}

TEST_CASE("Edit time collector uses resolved typed addresses changed neighbours and deletions",
          "[animation][animation-editing][j1-contract]") {
    const auto document = core::createDocumentUuid();
    const auto before = typedAnimation(document, 1,
        {{core::KeyId{1}, 1, 1}, {core::KeyId{2}, 10, 10},
         {core::KeyId{3}, 20, 20}, {core::KeyId{4}, 30, 30}});
    auto after = before;
    auto& keys = after.properties.actions.front().actionSlots.front().curves.front().keys;
    std::set<core::FrameTime> expected;
    SECTION("Changed key also collects both adjacent endpoints") {
        keys.at(2).value = 21;
        expected = {5, 10, 20, 30};
    }
    SECTION("New subframe collects its new adjacent endpoints") {
        keys.insert(keys.begin() + 2, {core::KeyId{5}, 12.5, 12.5});
        expected = {5, 10, 12.5, 20};
    }
    SECTION("Deleted time and newly adjacent endpoints are collected") {
        keys.erase(keys.begin() + 1);
        expected = {1, 5, 10, 20};
    }
    SECTION("Removed binding collects all old bound curve times") {
        after.properties.bindings.clear();
        expected = {1, 5, 10, 20, 30};
    }
    SECTION("New linked owner collects its own resolved address times with deduplication") {
        after.properties.bindings.push_back({document, core::EntityOwner{2}, core::ActionId{1},
                                             core::ActionSlotId{1}});
        expected = {1, 5, 10, 20, 30};
    }
    SECTION("Rebinding the same curve to a new owner collects both address sides") {
        after.properties.bindings.front().owner = core::EntityOwner{2};
        expected = {1, 5, 10, 20, 30};
    }
    SECTION("Unchanged curve collects only the current time") {
        expected = {5};
    }
    REQUIRE(editor::collectAnimationEditTimes(before, after, 5, 10000) == expected);
}

TEST_CASE("Edit time collector preserves legacy rules and skips unrelated typed curve times",
          "[animation][animation-editing][j1-contract]") {
    core::SceneAnimation before;
    before.tracks[{1, core::AnimationChannel::Position}] = {
        {{1, {1, 0, 0}}, {10, {10, 0, 0}}, {20, {20, 0, 0}}, {30, {30, 0, 0}}}};
    auto after = before;
    after.tracks.begin()->second.keys.erase(after.tracks.begin()->second.keys.begin() + 1);
    const std::set<core::FrameTime> legacyExpected{1, 12.5, 20};
    REQUIRE(editor::collectAnimationEditTimes(before, after, 12.5, 10000) == legacyExpected);
    const auto document = core::createDocumentUuid();
    before = typedAnimation(document, 1, {{core::KeyId{1}, 1, 1}, {core::KeyId{2}, 24, 24}});
    core::ScalarCurve unrelated{core::CurveId{2}, Property::Position, Component::Y, {}};
    for (std::uint64_t index = 0; index < 50; ++index)
        unrelated.keys.push_back({core::KeyId{index + 3}, double(index + 100), double(index)});
    before.properties.actions.front().actionSlots.front().curves.push_back(unrelated);
    after = before;
    after.properties.actions.front().actionSlots.front().curves.front().keys.insert(
        after.properties.actions.front().actionSlots.front().curves.front().keys.begin() + 1,
        {core::KeyId{53}, 12.5, 9});
    const std::set<core::FrameTime> changedExpected{1, 12.5, 24};
    const auto times = editor::collectAnimationEditTimes(before, after, 1, 10000);
    REQUIRE(times == changedExpected);
    REQUIRE(core::validateAnimationPreflightBudget(10000, times.size()).isValid());
    after = before;
    after.properties.bindings.clear();
    before.properties.bindings.clear();
    after.properties.actions.front().actionSlots.front().curves.back().keys.back().value = 51;
    const std::set<core::FrameTime> unboundExpected{1};
    REQUIRE(editor::collectAnimationEditTimes(before, after, 1, 10000) == unboundExpected);
}

TEST_CASE("Edit time collector keeps exact 40000 node visit budget and stops at first excess",
          "[animation][animation-editing][j1-contract]") {
    const auto document = core::createDocumentUuid();
    core::SceneAnimation before;
    auto after = typedAnimation(document, 1, {{core::KeyId{1}, 1, 1}, {core::KeyId{2}, 10, 10},
                                             {core::KeyId{3}, 20, 20}, {core::KeyId{4}, 30, 30}});
    const auto exact = editor::collectAnimationEditTimes(before, after, 1, 10000);
    REQUIRE(exact.size() == 4);
    REQUIRE(core::validateAnimationPreflightBudget(10000, exact.size()).isValid());
    after.properties.actions.front().actionSlots.front().curves.front().keys.push_back(
        {core::KeyId{5}, 40, 40});
    after.properties.actions.front().actionSlots.front().curves.front().keys.push_back(
        {core::KeyId{6}, 50, 50});
    const auto excess = editor::collectAnimationEditTimes(before, after, 1, 10000);
    const std::set<core::FrameTime> excessExpected{1, 10, 20, 30, 40};
    REQUIRE(excess == excessExpected);
    REQUIRE_FALSE(core::validateAnimationPreflightBudget(10000, excess.size()).isValid());
    const auto tooManyNodes = editor::collectAnimationEditTimes(before, after, 1, 10001);
    REQUIRE(tooManyNodes.size() == 1);
    REQUIRE_FALSE(core::validateAnimationPreflightBudget(10001, tooManyNodes.size()).isValid());
}

TEST_CASE("Structured typed prepare and commit retain old candidate and exact error codes",
          "[animation][animation-editing][j1-author]") {
    editor::SceneViewModel model;
    model.newScene();
    const auto entity = model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(model.setAnimationPreview(true));
    editor::AnimationEditing service(model);
    const auto x = address(model, core::EntityOwner{entity}, Property::Position, Component::X);
    const auto before = snapshot(model);
    const auto empty = service.commitResult(QStringLiteral("尚未准备"));
    REQUIRE(empty.error);
    REQUIRE(empty.error->code == api::ErrorCode::InvalidArgument);
    const auto prepared = service.prepareKeyResult(x, 12.5, 1, Interpolation::Linear);
    REQUIRE(prepared.hasValue());
    REQUIRE(*prepared.value);
    for (int invalid = 0; invalid < 6; ++invalid) {
        CAPTURE(invalid);
        auto target = x;
        auto time = 12.5, value = 2.0;
        auto outgoing = Interpolation::Linear;
        auto code = api::ErrorCode::InvalidArgument;
        if (invalid == 0) time = 0;
        if (invalid == 1) value = std::numeric_limits<double>::infinity();
        if (invalid == 2) {
            outgoing = Interpolation::Bezier;
            code = api::ErrorCode::UnsupportedOperation;
        }
        if (invalid == 3) {
            target.property = Property::Scale;
            code = api::ErrorCode::UnsupportedOperation;
        }
        if (invalid == 4) target.component = Component::Scalar;
        if (invalid == 5) target.documentUuid = core::createDocumentUuid();
        const auto result = service.prepareKeyResult(target, time, value, outgoing);
        REQUIRE(result.error);
        REQUIRE(result.error->code == code);
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(service.hasChanges());
        requireUnchanged(model, before);
    }
    api::MutationRequest request;
    request.document = model.apiDocumentState().document;
    request.expectedDocumentRevision = model.apiDocumentState().documentRevision;
    --request.expectedDocumentRevision;
    const auto stale = service.commitResult(QStringLiteral("过期请求"), &request);
    REQUIRE(stale.error);
    REQUIRE(stale.error->code == api::ErrorCode::RevisionConflict);
    REQUIRE(service.hasChanges());
    requireUnchanged(model, before);
    ++request.expectedDocumentRevision;
    model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
        return api::ApiError{api::ErrorCode::PermissionDenied, "structured final refusal", {},
                             api::Recovery::None, model.apiDocumentState()};
    });
    const auto denied = service.commitResult(QStringLiteral("最终拒绝"), &request);
    REQUIRE(denied.error);
    REQUIRE(denied.error->code == api::ErrorCode::PermissionDenied);
    REQUIRE(service.hasChanges());
    requireUnchanged(model, before);
    model.exchangeBeforeCommitGuard({});
    const auto committed = service.commitResult(QStringLiteral("提交原候选"), &request);
    REQUIRE(committed.hasValue());
    REQUIRE(committed.value->keyIdentity);
    REQUIRE(committed.value->command.status == api::ResultStatus::Committed);
    REQUIRE(curveAt(model.scene()->animation(), x)->keys.front().value == 1);
    REQUIRE_FALSE(service.hasChanges());
}

TEST_CASE("Structured scalar budgets share legacy expansion and return limits without text parsing",
          "[animation][animation-editing][j1-author]") {
    api::DocumentState document;
    core::SceneAnimation candidate;
    auto& curves = candidate.properties.actions.emplace_back().actionSlots.emplace_back().curves;
    curves.resize(core::kAnimationMaximumScalarCurves);
    REQUIRE_FALSE(editor::validateAnimationScalarBudget(candidate, document));
    candidate.tracks[{1, core::AnimationChannel::Position}] = {{{1, {0, 0, 0}}}};
    const auto curveLimit = editor::validateAnimationScalarBudget(candidate, document);
    REQUIRE(curveLimit);
    REQUIRE(curveLimit->code == api::ErrorCode::LimitExceeded);
    candidate.properties.actions.clear();
    auto& keys = candidate.properties.actions.emplace_back().actionSlots.emplace_back().curves
                     .emplace_back().keys;
    keys.resize(core::kAnimationMaximumScalarKeys - 3);
    REQUIRE_FALSE(editor::validateAnimationScalarBudget(candidate, document));
    keys.emplace_back();
    const auto keyLimit = editor::validateAnimationScalarBudget(candidate, document);
    REQUIRE(keyLimit);
    REQUIRE(keyLimit->code == api::ErrorCode::LimitExceeded);
}
