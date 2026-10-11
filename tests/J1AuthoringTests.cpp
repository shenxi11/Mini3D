/*
 * 模块名: J1AuthoringTests
 * 功能概述: 验证共享 J1 作者命令、唯一历史、完整独立取样与身份发行边界。
 * 对外接口: Catch2 [j1-author] Editor 用例。
 * 依赖关系: 真实 SceneViewModel Explicit 入口、V5 SceneSerializer、Qt Test；无 GL。
 * 输入输出: 完整强类型请求到作者值、身份、历史、会话和精确 V5 快照断言。
 * 异常与错误: 初始/最终拒绝不能消费身份或覆盖守卫合法编辑。
 * 维护说明: Core 写入口仅构造大身份、预算和来源变化夹具；被测动作均走 Explicit。
 */
#include "core/SceneSerializer.h"
#include "editor/SceneViewModel.h"

#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace mini3d;
namespace {
namespace api = editor::api;
using Property = core::PropertyId;
using Component = core::PropertyComponent;
using Interpolation = core::PropertyInterpolation;

std::string encodedScene(const editor::SceneViewModel& model) {
    core::SceneDocumentData data(*model.scene());
    data.camera = model.editorCamera();
    data.cursor = model.cursor3D();
    return core::SceneSerializer::encode(data);
}

struct Snapshot {
    std::shared_ptr<const core::Scene> scene;
    api::DocumentState document;
    std::string encoded;
    int count = 0, index = 0, clean = 0;
    bool modified = false, isClean = false;
    const QUndoCommand* redo = nullptr;
    core::EntityId selection = 0;
    editor::AnimationMode mode = editor::AnimationMode::Base;
    core::FrameTime frame = 1;
    bool loop = false;
    std::uint64_t session = 0, evaluation = 0;
    std::shared_ptr<const renderer_gl::InstalledPose> pose;
    QStringList busy;
};
Snapshot snapshot(editor::SceneViewModel& model) {
    const auto* stack = model.undoStack();
    return {model.scene(), model.apiDocumentState(), encodedScene(model), stack->count(),
        stack->index(), stack->cleanIndex(), model.isModified(), stack->isClean(),
        stack->canRedo() ? stack->command(stack->index()) : nullptr,
        model.selection()->selectedEntity(), model.animationMode(), model.animationFrame(),
        model.isAnimationLoopEnabled(), model.animationSessionRevision(),
        model.animationEvaluationId(), model.installedAnimationPose(), model.apiBusyReasons(false)};
}
void requireUnchanged(editor::SceneViewModel& model, const Snapshot& before) {
    REQUIRE(model.scene() == before.scene);
    REQUIRE(model.apiDocumentState().document == before.document.document);
    REQUIRE(model.apiDocumentState().documentRevision == before.document.documentRevision);
    REQUIRE(model.apiDocumentState().historyRevision == before.document.historyRevision);
    REQUIRE(encodedScene(model) == before.encoded);
    REQUIRE(model.undoStack()->count() == before.count);
    REQUIRE(model.undoStack()->index() == before.index);
    REQUIRE(model.undoStack()->cleanIndex() == before.clean);
    REQUIRE(model.undoStack()->isClean() == before.isClean);
    REQUIRE(model.isModified() == before.modified);
    REQUIRE((model.undoStack()->canRedo() ? model.undoStack()->command(model.undoStack()->index())
                                        : nullptr) == before.redo);
    REQUIRE(model.selection()->selectedEntity() == before.selection);
    REQUIRE(model.animationMode() == before.mode);
    REQUIRE(model.animationFrame() == before.frame);
    REQUIRE(model.isAnimationLoopEnabled() == before.loop);
    REQUIRE(model.animationSessionRevision() == before.session);
    REQUIRE(model.animationEvaluationId() == before.evaluation);
    REQUIRE(model.installedAnimationPose() == before.pose);
    REQUIRE(model.apiBusyReasons(false) == before.busy);
}
template <typename T>
void requireFailure(const api::ApiResult<T>& result, api::ErrorCode code) {
    INFO((result.error ? result.error->message.toStdString() : "unexpected success"));
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error);
    REQUIRE(result.error->code == code);
}
void requireOneCommit(editor::SceneViewModel& model, const Snapshot& before,
                      const api::MutationResult& command) {
    REQUIRE(command.status == api::ResultStatus::Committed);
    REQUIRE(command.undoable);
    REQUIRE_FALSE(command.selectionChanged);
    REQUIRE(model.undoStack()->index() == before.index + 1);
    REQUIRE(model.undoStack()->count() == before.index + 1);
    REQUIRE(command.state.documentRevision == before.document.documentRevision + 1);
    REQUIRE(command.state.historyRevision == before.document.historyRevision + 1);
    REQUIRE(model.selection()->selectedEntity() == before.selection);
}

struct Fixture {
    editor::SceneViewModel model;
    Fixture() { model.newScene(); }
    template <typename Request> Request mutation() const {
        Request request;
        request.document = model.apiDocumentState().document;
        request.expectedDocumentRevision = model.apiDocumentState().documentRevision;
        return request;
    }
    core::PropertyAddress address(core::OwnerRef owner, Property property,
                                   Component component) const {
        return {model.scene()->documentUuid(), std::move(owner), property, component};
    }
    core::EntityId create(const QString& name = QStringLiteral("作者立方体"),
                          core::PrimitiveKind primitive = core::PrimitiveKind::Cube) {
        auto request = mutation<api::EntityCreateRequest>();
        request.name = name;
        request.primitive = primitive;
        const auto result = model.createEntityExplicit(request);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    core::MaterialId material(double roughness = .7) {
        auto request = mutation<api::MaterialCreateRequest>();
        request.parameters.roughness = roughness;
        const auto result = model.createMaterialExplicit(request);
        REQUIRE(result.hasValue());
        return result.value->materialId;
    }
    core::EntityId sun() {
        auto request = mutation<api::SunCreateRequest>();
        request.name = QStringLiteral("作者 Sun");
        const auto result = model.createSunExplicit(request);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    api::PropertyKeyIdentity key(const core::PropertyAddress& address, double time, double value,
                                Interpolation outgoing = Interpolation::Linear) {
        auto request = mutation<api::AnimationUpsertPropertyKeyframeRequest>();
        request.address = address;
        request.time = time;
        request.value = value;
        request.outgoing = outgoing;
        const auto before = snapshot(model);
        const auto result = model.upsertAnimationPropertyKeyframeExplicit(request);
        INFO((result.error ? result.error->message.toStdString() : ""));
        REQUIRE(result.hasValue());
        REQUIRE(result.value->keyIdentity);
        requireOneCommit(model, before, result.value->command);
        return *result.value->keyIdentity;
    }
    api::AnimationReadPropertyKeyframesRequest read(const core::PropertyAddress& address) const {
        api::AnimationReadPropertyKeyframesRequest request;
        request.document = model.apiDocumentState().document;
        request.address = address;
        return request;
    }
};

struct Work : Fixture {
    core::EntityId cube = create(), light = sun();
    core::MaterialId pbr = material();
    std::array<core::PropertyAddress, 3> addresses{
        address(core::EntityOwner{cube}, Property::Position, Component::X),
        address(core::MaterialOwner{pbr}, Property::PbrRoughness, Component::Scalar),
        address(core::EntityOwner{light}, Property::SunIrradiance, Component::Scalar)};
    Work() {
        auto request = mutation<api::EntityAssignMaterialRequest>();
        request.entityId = cube;
        request.materialId = pbr;
        REQUIRE(model.assignMaterialExplicit(request).hasValue());
    }
    void seedKeys() {
        const std::array<double, 3> left{0, .2, 1}, right{2, .8, 2};
        for (std::size_t index = 0; index < addresses.size(); ++index) {
            key(addresses[index], 1, left[index]);
            key(addresses[index], 24, right[index]);
        }
    }
};

std::optional<api::ApiError> attempt(Work& fixture, int operation, int timeout = 30000) {
    const auto call = [](const auto& result) {
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error);
        return result.error;
    };
    if (operation == 0) {
        auto request = fixture.mutation<api::MaterialCreateRequest>();
        request.timeoutMs = timeout;
        return call(fixture.model.createMaterialExplicit(request));
    }
    if (operation == 1) {
        auto request = fixture.mutation<api::MaterialUpdateRequest>();
        request.materialId = fixture.pbr;
        request.parameters.roughness = .6;
        request.timeoutMs = timeout;
        return call(fixture.model.updateMaterialExplicit(request));
    }
    if (operation == 2) {
        auto request = fixture.mutation<api::EntityAssignMaterialRequest>();
        request.entityId = fixture.cube;
        request.materialId = fixture.pbr;
        request.timeoutMs = timeout;
        return call(fixture.model.assignMaterialExplicit(request));
    }
    if (operation == 3) {
        auto request = fixture.mutation<api::SunCreateRequest>();
        request.name = QStringLiteral("被拒绝的 Sun");
        request.timeoutMs = timeout;
        return call(fixture.model.createSunExplicit(request));
    }
    if (operation == 4) {
        auto request = fixture.mutation<api::SunUpdateRequest>();
        request.entityId = fixture.light;
        request.sun.irradiance = 3;
        request.timeoutMs = timeout;
        return call(fixture.model.updateSunExplicit(request));
    }
    if (operation == 5) {
        auto request = fixture.mutation<api::AnimationUpsertPropertyKeyframeRequest>();
        request.address = fixture.addresses[0];
        request.time = 12.5;
        request.value = 1;
        request.timeoutMs = timeout;
        return call(fixture.model.upsertAnimationPropertyKeyframeExplicit(request));
    }
    if (operation == 6) {
        auto request = fixture.mutation<api::AnimationDeletePropertyKeyframeRequest>();
        request.address = fixture.addresses[0];
        request.time = 24;
        request.timeoutMs = timeout;
        return call(fixture.model.deleteAnimationPropertyKeyframeExplicit(request));
    }
    auto request = fixture.mutation<api::AnimationSetRationalSettingsRequest>();
    request.settings.rationalFps = {30000, 1001};
    request.timeoutMs = timeout;
    return call(fixture.model.setRationalAnimationSettingsExplicit(request));
}
} // namespace

TEST_CASE("J1 material full parameters retain version one through unique preview history",
          "[j1-author][material]") {
    Fixture fixture;
    fixture.create();
    auto request = fixture.mutation<api::MaterialCreateRequest>();
    request.parameters = {{.123456789012345, .2, .3}, .234567890123456, .345678901234567,
                          .456789012345678};
    const auto before = snapshot(fixture.model);
    const auto created = fixture.model.createMaterialExplicit(request);
    REQUIRE(created.hasValue());
    requireOneCommit(fixture.model, before, created.value->command);
    const auto id = created.value->materialId;
    api::MaterialGetRequest get;
    get.document = fixture.model.apiDocumentState().document;
    get.materialId = id;
    const auto original = fixture.model.getMaterialExplicit(get);
    REQUIRE(original.hasValue());
    REQUIRE(original.value->material.id == id);
    REQUIRE(original.value->material.definitionVersion == 1);
    REQUIRE(original.value->material.parameters == request.parameters);
    REQUIRE(original.value->material.textures.empty());
    REQUIRE(fixture.model.setAnimationPreview(true));
    auto update = fixture.mutation<api::MaterialUpdateRequest>();
    update.materialId = id;
    update.parameters = {{.9, .8, .7}, .123456789123456, .6, .5};
    const auto beforeUpdate = snapshot(fixture.model);
    const auto updated = fixture.model.updateMaterialExplicit(update);
    REQUIRE(updated.hasValue());
    requireOneCommit(fixture.model, beforeUpdate, updated.value->command);
    const auto final = *fixture.model.scene()->materialData().find(id);
    REQUIRE(final.definitionVersion == 1);
    REQUIRE(final.parameters == update.parameters);
    REQUIRE(fixture.model.installedAnimationPose()->sceneState->content->materials.at(id).effective ==
            update.parameters);
    const auto ids = fixture.model.scene()->idHighWatermarks();
    fixture.model.undo();
    REQUIRE(*fixture.model.scene()->materialData().find(id) == original.value->material);
    REQUIRE(fixture.model.scene()->idHighWatermarks() == ids);
    fixture.model.redo();
    REQUIRE(*fixture.model.scene()->materialData().find(id) == final);
    REQUIRE(fixture.model.animationMode() == editor::AnimationMode::PreviewPaused);
}

TEST_CASE("J1 first slot undo spends identity redo restores it and single slot keeps it",
          "[j1-author][material]") {
    Fixture fixture;
    const auto entity = fixture.create();
    const auto material = fixture.material(), other = fixture.material(.3);
    auto request = fixture.mutation<api::EntityAssignMaterialRequest>();
    request.entityId = entity;
    request.materialId = material;
    const auto before = snapshot(fixture.model);
    const auto assigned = fixture.model.assignMaterialExplicit(request);
    REQUIRE(assigned.hasValue());
    requireOneCommit(fixture.model, before, assigned.value->command);
    const auto slot = assigned.value->materialSlotId;
    const auto published = encodedScene(fixture.model);
    auto scene = std::const_pointer_cast<core::Scene>(fixture.model.scene());
    for (int replay = 0; replay < 3; ++replay) {
        fixture.model.undo();
        REQUIRE(scene->find(entity)->materialSlots.empty());
        REQUIRE(scene->idHighWatermarks().materialSlot == slot.value);
        const auto undone = snapshot(fixture.model);
        REQUIRE_FALSE(scene->setMaterialSlots(entity, {{slot, material}}));
        requireUnchanged(fixture.model, undone);
        fixture.model.redo();
        REQUIRE(encodedScene(fixture.model) == published);
    }
    request = fixture.mutation<api::EntityAssignMaterialRequest>();
    request.entityId = entity;
    request.materialId = other;
    const auto changed = fixture.model.assignMaterialExplicit(request);
    REQUIRE(changed.hasValue());
    REQUIRE(changed.value->materialSlotId == slot);
    fixture.model.undo();
    REQUIRE(scene->find(entity)->materialSlots.front().material == material);
    fixture.model.redo();
    REQUIRE(scene->find(entity)->materialSlots.front().material == other);
    const auto extra = core::MaterialSlotId{slot.value + 1};
    REQUIRE(scene->setMaterialSlots(entity, {{slot, other}, {extra, material}}));
    request = fixture.mutation<api::EntityAssignMaterialRequest>();
    request.entityId = entity;
    request.materialId = material;
    const auto multi = snapshot(fixture.model);
    requireFailure(fixture.model.assignMaterialExplicit(request), api::ErrorCode::UnsupportedOperation);
    requireUnchanged(fixture.model, multi);
}

TEST_CASE("J1 Sun is created atomically and only existing Sun accepts full update",
          "[j1-author][sun]") {
    Fixture fixture;
    const auto cube = fixture.create();
    fixture.model.selection()->setSelectedEntity(cube);
    auto create = fixture.mutation<api::SunCreateRequest>();
    create.name = QStringLiteral("完整 Sun");
    create.parentId = cube;
    create.transform.position = {.25F, 1, 2};
    create.visible = false;
    create.sun = {1.23456789012345, {.3, .5, .7}};
    const auto before = snapshot(fixture.model);
    const auto result = fixture.model.createSunExplicit(create);
    REQUIRE(result.hasValue());
    requireOneCommit(fixture.model, before, *result.value);
    REQUIRE(result.value->createdEntityIds.size() == 1);
    const auto sun = result.value->createdEntityIds.front();
    REQUIRE(fixture.model.scene()->find(sun)->sun == create.sun);
    REQUIRE(fixture.model.scene()->find(sun)->parent == cube);
    REQUIRE(fixture.model.scene()->find(sun)->transform.position == create.transform.position);
    REQUIRE(fixture.model.scene()->find(sun)->transform.rotation == create.transform.rotation);
    REQUIRE(fixture.model.scene()->find(sun)->transform.scale == create.transform.scale);
    REQUIRE_FALSE(fixture.model.scene()->find(sun)->visible);
    fixture.model.undo();
    REQUIRE_FALSE(fixture.model.scene()->find(sun));
    fixture.model.redo();
    REQUIRE(fixture.model.scene()->find(sun)->sun == create.sun);
    auto update = fixture.mutation<api::SunUpdateRequest>();
    update.entityId = sun;
    update.sun = {2.34567890123456, {.8, .6, .4}};
    REQUIRE(fixture.model.setAnimationPreview(true));
    const auto beforeUpdate = snapshot(fixture.model);
    const auto updated = fixture.model.updateSunExplicit(update);
    REQUIRE(updated.hasValue());
    requireOneCommit(fixture.model, beforeUpdate, *updated.value);
    REQUIRE(fixture.model.scene()->find(sun)->sun == update.sun);
    fixture.model.undo();
    REQUIRE(fixture.model.scene()->find(sun)->sun == create.sun);
    fixture.model.redo();
    REQUIRE(fixture.model.scene()->find(sun)->sun == update.sun);
    update = fixture.mutation<api::SunUpdateRequest>();
    update.entityId = cube;
    const auto noSun = snapshot(fixture.model);
    requireFailure(fixture.model.updateSunExplicit(update), api::ErrorCode::UnsupportedOperation);
    requireUnchanged(fixture.model, noSun);
}

TEST_CASE("J1 typed keys expose published identities preserve replacement and delete identity",
          "[j1-author][animation]") {
    Work fixture;
    std::array<api::PropertyKeyIdentity, 3> ids;
    const std::array<double, 3> values{2, .8, 2};
    for (std::size_t index = 0; index < ids.size(); ++index) {
        ids[index] = fixture.key(fixture.addresses[index], 24, values[index]);
        const auto water = fixture.model.scene()->idHighWatermarks();
        const auto replacement = fixture.key(fixture.addresses[index], 24, values[index] / 2,
                                             Interpolation::Constant);
        REQUIRE(replacement == ids[index]);
        REQUIRE(fixture.model.scene()->idHighWatermarks() == water);
        auto request = fixture.mutation<api::AnimationDeletePropertyKeyframeRequest>();
        request.address = fixture.addresses[index];
        request.time = 24;
        const auto before = snapshot(fixture.model);
        const auto removed = fixture.model.deleteAnimationPropertyKeyframeExplicit(request);
        REQUIRE(removed.hasValue());
        requireOneCommit(fixture.model, before, removed.value->command);
        REQUIRE(removed.value->keyIdentity == ids[index]);
        fixture.model.undo();
        const auto page = fixture.model.readAnimationPropertyKeyframesExplicit(
            fixture.read(fixture.addresses[index]));
        REQUIRE(page.hasValue());
        REQUIRE(page.value->keys.front().id == ids[index].keyId);
        fixture.model.redo();
        request = fixture.mutation<api::AnimationDeletePropertyKeyframeRequest>();
        request.address = fixture.addresses[index];
        request.time = 24;
        const auto missing = snapshot(fixture.model);
        const auto absent = fixture.model.deleteAnimationPropertyKeyframeExplicit(request);
        REQUIRE(absent.hasValue());
        REQUIRE(absent.value->command.status == api::ResultStatus::NoChange);
        REQUIRE_FALSE(absent.value->keyIdentity);
        requireUnchanged(fixture.model, missing);
    }
}

TEST_CASE("J1 exact no change retains redo clean point full V5 and installed session",
          "[j1-author][animation]") {
    Work fixture;
    fixture.seedKeys();
    REQUIRE(fixture.model.setAnimationPreview(true));
    fixture.key(fixture.addresses[0], 12.5, 1);
    const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
    fixture.model.undo();
    REQUIRE(fixture.model.undoStack()->canRedo());
    const auto before = snapshot(fixture.model);
    auto material = fixture.mutation<api::MaterialUpdateRequest>();
    material.materialId = fixture.pbr;
    material.parameters = fixture.model.scene()->materialData().find(fixture.pbr)->parameters;
    const auto sameMaterial = fixture.model.updateMaterialExplicit(material);
    REQUIRE(sameMaterial.hasValue());
    REQUIRE(sameMaterial.value->command.status == api::ResultStatus::NoChange);
    requireUnchanged(fixture.model, before);
    auto assign = fixture.mutation<api::EntityAssignMaterialRequest>();
    assign.entityId = fixture.cube;
    assign.materialId = fixture.pbr;
    REQUIRE(fixture.model.assignMaterialExplicit(assign).value->command.status ==
            api::ResultStatus::NoChange);
    requireUnchanged(fixture.model, before);
    auto sun = fixture.mutation<api::SunUpdateRequest>();
    sun.entityId = fixture.light;
    sun.sun = *fixture.model.scene()->find(fixture.light)->sun;
    REQUIRE(fixture.model.updateSunExplicit(sun).value->status == api::ResultStatus::NoChange);
    requireUnchanged(fixture.model, before);
    auto key = fixture.mutation<api::AnimationUpsertPropertyKeyframeRequest>();
    key.address = fixture.addresses[0];
    const auto sameKey = fixture.model.upsertAnimationPropertyKeyframeExplicit(key);
    REQUIRE(sameKey.hasValue());
    REQUIRE(sameKey.value->command.status == api::ResultStatus::NoChange);
    REQUIRE(sameKey.value->keyIdentity);
    requireUnchanged(fixture.model, before);
    auto missing = fixture.mutation<api::AnimationDeletePropertyKeyframeRequest>();
    missing.address = fixture.addresses[0];
    missing.time = 99;
    REQUIRE_FALSE(fixture.model.deleteAnimationPropertyKeyframeExplicit(missing).value->keyIdentity);
    requireUnchanged(fixture.model, before);
}

TEST_CASE("J1 final cancellations permission and deadlines retain complete prepared state",
          "[j1-author][guard]") {
    for (const auto code : {api::ErrorCode::Cancelled, api::ErrorCode::PermissionDenied,
                            api::ErrorCode::DeadlineExceeded})
        for (int operation = 0; operation < 8; ++operation) {
            CAPTURE(code, operation);
            Work fixture;
            fixture.seedKeys();
            if (operation != 3)
                REQUIRE(fixture.model.setAnimationPreview(true));
            fixture.key(fixture.addresses[0], 12.5, 1);
            const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
            fixture.model.undo();
            const auto before = snapshot(fixture.model);
            int guards = 0;
            fixture.model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
                ++guards;
                return api::ApiError{code, "J1 final refusal", {}, api::Recovery::None,
                                     fixture.model.apiDocumentState()};
            });
            const auto error = attempt(fixture, operation);
            REQUIRE(error->code == code);
            REQUIRE(guards == 1);
            requireUnchanged(fixture.model, before);
        }
}

TEST_CASE("J1 initial revision busy and real elapsed deadline reject without publishing",
          "[j1-author][guard]") {
    Work fixture;
    auto request = fixture.mutation<api::MaterialCreateRequest>();
    --request.expectedDocumentRevision;
    const auto before = snapshot(fixture.model);
    requireFailure(fixture.model.createMaterialExplicit(request), api::ErrorCode::RevisionConflict);
    requireUnchanged(fixture.model, before);
    request = fixture.mutation<api::MaterialCreateRequest>();
    request.document.documentId = "stale";
    requireFailure(fixture.model.createMaterialExplicit(request), api::ErrorCode::StaleDocument);
    requireUnchanged(fixture.model, before);
    fixture.model.setExternalBusy("J1 fixture", true);
    const auto busy = snapshot(fixture.model);
    requireFailure(fixture.model.createMaterialExplicit(fixture.mutation<api::MaterialCreateRequest>()),
                   api::ErrorCode::Busy);
    requireUnchanged(fixture.model, busy);
    fixture.model.setExternalBusy("J1 fixture", false);
    for (int operation = 0; operation < 8; ++operation) {
        CAPTURE(operation);
        fixture.model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            QTest::qWait(20);
            return std::nullopt;
        });
        const auto timed = snapshot(fixture.model);
        REQUIRE(attempt(fixture, operation, 1)->code == api::ErrorCode::DeadlineExceeded);
        requireUnchanged(fixture.model, timed);
    }
}

TEST_CASE("J1 elapsed preview deadline releases pending replay before later no change",
          "[j1-author][guard][j1-preview-release]") {
    for (const auto operation : {1, 2, 4, 5}) {
        CAPTURE(operation);
        Work fixture;
        if (operation == 2)
            fixture.pbr = fixture.material(.3);
        REQUIRE(fixture.model.setAnimationPreview(true));
        const auto before = snapshot(fixture.model);
        int guards = 0;
        fixture.model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            ++guards;
            QTest::qWait(20);
            return std::nullopt;
        });
        REQUIRE(attempt(fixture, operation, 1)->code == api::ErrorCode::DeadlineExceeded);
        REQUIRE(guards == 1);
        requireUnchanged(fixture.model, before);
        fixture.model.exchangeBeforeCommitGuard({});
        REQUIRE(fixture.model.setAnimationLoop(!fixture.model.isAnimationLoopEnabled()));
        REQUIRE(fixture.model.animationSessionRevision() == before.session + 1);
        const auto advanced = snapshot(fixture.model);
        auto frame = fixture.mutation<api::AnimationSetFrameRequest>();
        frame.expectedSessionRevision = fixture.model.animationSessionRevision();
        frame.frame = fixture.model.animationFrame();
        const auto sameFrame = fixture.model.setAnimationFrameExplicit(frame);
        INFO((sameFrame.error ? sameFrame.error->message.toStdString() : ""));
        REQUIRE(sameFrame.hasValue());
        REQUIRE(sameFrame.value->status == api::ResultStatus::NoChange);
        requireUnchanged(fixture.model, advanced);
        auto loop = fixture.mutation<api::AnimationSetLoopRequest>();
        loop.expectedSessionRevision = fixture.model.animationSessionRevision();
        loop.enabled = fixture.model.isAnimationLoopEnabled();
        const auto sameLoop = fixture.model.setAnimationLoopExplicit(loop);
        INFO((sameLoop.error ? sameLoop.error->message.toStdString() : ""));
        REQUIRE(sameLoop.hasValue());
        REQUIRE(sameLoop.value->status == api::ResultStatus::NoChange);
        requireUnchanged(fixture.model, advanced);
    }
}

TEST_CASE("J1 final source CAS preserves legally issued and restored spent guard identities",
          "[j1-author][guard]") {
    for (int operation = 0; operation < 4; ++operation) {
        CAPTURE(operation);
        Work fixture;
        auto scene = std::const_pointer_cast<core::Scene>(fixture.model.scene());
        const auto other = fixture.create(QStringLiteral("守卫对象"));
        std::optional<Snapshot> afterGuard;
        fixture.model.exchangeBeforeCommitGuard([&]() -> std::optional<api::ApiError> {
            std::string error;
            if (operation == 0) {
                auto data = scene->materialData();
                core::MaterialDefinition material;
                material.id = core::MaterialId{scene->idHighWatermarks().asset + 1};
                const core::StableAssetId asset{material.id.value};
                data.assets.emplace(asset, core::SceneAssetDefinition{asset,
                    core::SceneAssetKind::Material, material});
                auto prepared = scene->prepareMaterialData(data, error);
                REQUIRE(prepared);
                REQUIRE(scene->installPreparedMaterialData(*prepared));
                REQUIRE(scene->restorePreparedMaterialData(*prepared));
            } else if (operation == 1) {
                auto prepared = scene->prepareMaterialSlots(other,
                    {{core::MaterialSlotId{scene->idHighWatermarks().materialSlot + 1}, fixture.pbr}},
                    error);
                REQUIRE(prepared);
                REQUIRE(scene->installPreparedMaterialSlots(*prepared));
                REQUIRE(scene->restorePreparedMaterialSlots(*prepared));
            } else if (operation == 2) {
                auto animation = scene->animation();
                animation.properties.actions = {{core::ActionId{1}, {{core::ActionSlotId{1},
                    core::PropertyOwnerKind::Entity, {{core::CurveId{1}, Property::Position,
                        Component::X, {{core::KeyId{1}, 12.5, 9}}}}}}}};
                animation.properties.bindings = {{scene->documentUuid(), core::EntityOwner{other},
                    core::ActionId{1}, core::ActionSlotId{1}}};
                auto prepared = scene->prepareAnimation(animation, error);
                REQUIRE(prepared);
                REQUIRE(scene->installPreparedAnimation(*prepared));
                REQUIRE(scene->restorePreparedAnimation(*prepared));
            } else {
                core::Scene::EntityCreateOptions options;
                options.name = "Guard reservation";
                REQUIRE(scene->prepareEntity(options, error));
            }
            afterGuard = snapshot(fixture.model);
            return std::nullopt;
        });
        if (operation == 0)
            requireFailure(fixture.model.createMaterialExplicit(
                fixture.mutation<api::MaterialCreateRequest>()), api::ErrorCode::RevisionConflict);
        else if (operation == 1) {
            auto request = fixture.mutation<api::EntityAssignMaterialRequest>();
            request.entityId = other;
            request.materialId = fixture.pbr;
            requireFailure(fixture.model.assignMaterialExplicit(request), api::ErrorCode::RevisionConflict);
        } else if (operation == 2) {
            auto request = fixture.mutation<api::AnimationUpsertPropertyKeyframeRequest>();
            request.address = fixture.addresses[0];
            requireFailure(fixture.model.upsertAnimationPropertyKeyframeExplicit(request),
                           api::ErrorCode::RevisionConflict);
        } else {
            auto request = fixture.mutation<api::SunCreateRequest>();
            request.name = QStringLiteral("未获发行 Sun");
            requireFailure(fixture.model.createSunExplicit(request), api::ErrorCode::RevisionConflict);
        }
        REQUIRE(afterGuard);
        requireUnchanged(fixture.model, *afterGuard);
    }
}

TEST_CASE("J1 property pagination keeps adjacent binary64 times and frozen continuation",
          "[j1-author][animation]") {
    Fixture fixture;
    const auto entity = fixture.create();
    const auto address = fixture.address(core::EntityOwner{entity}, Property::Position, Component::X);
    const auto adjacent = std::nextafter(12.5, 13.0);
    fixture.key(address, 12.5, 1);
    fixture.key(address, adjacent, 2);
    fixture.key(address, 24, 3);
    auto request = fixture.read(address);
    request.limit = 1;
    const auto before = snapshot(fixture.model);
    const auto first = fixture.model.readAnimationPropertyKeyframesExplicit(request);
    REQUIRE(first.hasValue());
    REQUIRE(first.value->keys.front().time == 12.5);
    REQUIRE(first.value->nextAfterTime == 12.5);
    REQUIRE(first.value->curveIdentity);
    request.afterTime = first.value->nextAfterTime;
    requireFailure(fixture.model.readAnimationPropertyKeyframesExplicit(request),
                   api::ErrorCode::InvalidArgument);
    request.expectedDocumentRevision = first.value->state.documentRevision;
    const auto second = fixture.model.readAnimationPropertyKeyframesExplicit(request);
    REQUIRE(second.hasValue());
    REQUIRE(second.value->keys.front().time == adjacent);
    REQUIRE(second.value->curveIdentity == first.value->curveIdentity);
    request.limit = int(api::limits::sourcePageMaximum);
    REQUIRE(fixture.model.readAnimationPropertyKeyframesExplicit(request).value->keys.size() == 2);
    request.limit += 1;
    requireFailure(fixture.model.readAnimationPropertyKeyframesExplicit(request),
                   api::ErrorCode::InvalidArgument);
    requireUnchanged(fixture.model, before);
    fixture.key(address, 25, 4);
    request.limit = 1;
    const auto changed = snapshot(fixture.model);
    requireFailure(fixture.model.readAnimationPropertyKeyframesExplicit(request),
                   api::ErrorCode::RevisionConflict);
    requireUnchanged(fixture.model, changed);
}

TEST_CASE("J1 complete independent samples are order invariant and never install a pose",
          "[j1-author][animation]") {
    Work fixture;
    fixture.seedKeys();
    REQUIRE(fixture.model.setAnimationPreview(true));
    REQUIRE(fixture.model.setAnimationFrame(12.5));
    fixture.model.selection()->setSelectedEntity(fixture.cube);
    const auto before = snapshot(fixture.model);
    const auto previewSource = before.pose->sceneState->source;
    std::optional<core::SourceIdentity> previous;
    const std::array<double, 4> times{24, 1, 12.5, 1};
    const std::array<std::array<double, 3>, 4> expected{{{2, .8, 2}, {0, .2, 1},
                                                     {1, .5, 1.5}, {0, .2, 1}}};
    for (std::size_t index = 0; index < times.size(); ++index) {
        api::AnimationSamplePropertiesRequest request;
        request.document = fixture.model.apiDocumentState().document;
        request.frame = times[index];
        request.addresses.assign(fixture.addresses.begin(), fixture.addresses.end());
        const auto result = fixture.model.sampleAnimationPropertiesExplicit(request);
        INFO((result.error ? result.error->message.toStdString() : ""));
        REQUIRE(result.hasValue());
        REQUIRE(result.value->frame == times[index]);
        REQUIRE(result.value->samples.size() == 3);
        for (std::size_t property = 0; property < 3; ++property) {
            REQUIRE(result.value->samples[property].address == fixture.addresses[property]);
            REQUIRE(std::abs(result.value->samples[property].value - expected[index][property]) <= 1e-12);
        }
        REQUIRE(core::isSourceIdentityValid(result.value->source));
        REQUIRE(result.value->source.purpose == core::EvaluationPurpose::Sample);
        REQUIRE(result.value->source.evaluationSessionId != previewSource.evaluationSessionId);
        if (previous) {
            REQUIRE(result.value->source.evaluationSessionId == previous->evaluationSessionId);
            REQUIRE(result.value->source.evaluationId == previous->evaluationId + 1);
        }
        if (times[index] == 12.5)
            REQUIRE(core::isSameWorkSource(result.value->source, previewSource));
        previous = result.value->source;
        requireUnchanged(fixture.model, before);
    }
    api::AnimationSamplePropertiesRequest invalid;
    invalid.document = fixture.model.apiDocumentState().document;
    invalid.addresses = {fixture.addresses[0], fixture.addresses[0]};
    requireFailure(fixture.model.sampleAnimationPropertiesExplicit(invalid), api::ErrorCode::InvalidArgument);
    invalid.addresses = {fixture.addresses[0]};
    const auto next = fixture.model.sampleAnimationPropertiesExplicit(invalid);
    REQUIRE(next.hasValue());
    REQUIRE(next.value->source.evaluationId == previous->evaluationId + 1);
    requireUnchanged(fixture.model, before);
}

TEST_CASE("J1 typed sample admits the shared page budget independently of legacy entity cap",
          "[j1-author][animation]") {
    Fixture fixture;
    auto scene = std::const_pointer_cast<core::Scene>(fixture.model.scene());
    api::AnimationSamplePropertiesRequest request;
    request.document = fixture.model.apiDocumentState().document;
    while (request.addresses.size() < api::limits::sourcePageMaximum) {
        const auto entity = scene->createEntity("Budget owner");
        REQUIRE(entity);
        for (const auto component : {Component::X, Component::Y, Component::Z}) {
            if (request.addresses.size() == api::limits::sourcePageMaximum)
                break;
            request.addresses.push_back(fixture.address(core::EntityOwner{entity}, Property::Position,
                                                       component));
        }
    }
    const auto before = snapshot(fixture.model);
    const auto result = fixture.model.sampleAnimationPropertiesExplicit(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->samples.size() == api::limits::sourcePageMaximum);
    REQUIRE(result.value->samples.size() > api::limits::animationSampleEntities);
    request.addresses.push_back(request.addresses.front());
    requireFailure(fixture.model.sampleAnimationPropertiesExplicit(request), api::ErrorCode::LimitExceeded);
    requireUnchanged(fixture.model, before);
}

TEST_CASE("J1 rational settings keep keys and legacy full settings explicitly replace override",
          "[j1-author][animation]") {
    Work fixture;
    fixture.seedKeys();
    const auto properties = fixture.model.scene()->animation().properties;
    auto request = fixture.mutation<api::AnimationSetRationalSettingsRequest>();
    request.settings = {{30000, 1001}, 12, 100000, core::AnimationLoopPolicy::InclusiveFrameStep};
    const auto before = snapshot(fixture.model);
    const auto result = fixture.model.setRationalAnimationSettingsExplicit(request);
    REQUIRE(result.hasValue());
    requireOneCommit(fixture.model, before, *result.value);
    REQUIRE(fixture.model.scene()->animation().properties.actions == properties.actions);
    REQUIRE(fixture.model.scene()->animation().properties.bindings == properties.bindings);
    REQUIRE(fixture.model.scene()->animation().properties.rationalFps == request.settings.rationalFps);
    request = fixture.mutation<api::AnimationSetRationalSettingsRequest>();
    request.settings = {{30000, 1001}, 12, 100000, core::AnimationLoopPolicy::InclusiveFrameStep};
    const auto unchanged = snapshot(fixture.model);
    REQUIRE(fixture.model.setRationalAnimationSettingsExplicit(request).value->status ==
            api::ResultStatus::NoChange);
    requireUnchanged(fixture.model, unchanged);
    auto old = fixture.mutation<api::AnimationSetSettingsRequest>();
    old.settings = {30, 1, 24};
    REQUIRE(fixture.model.setAnimationSettingsExplicit(old).hasValue());
    const core::RationalFps thirty{30, 1};
    REQUIRE(fixture.model.scene()->animation().properties.rationalFps == thirty);
    REQUIRE(fixture.model.scene()->animation().properties.actions == properties.actions);
    for (const auto fps : {core::RationalFps{2, 2}, core::RationalFps{120001, 1000}}) {
        request = fixture.mutation<api::AnimationSetRationalSettingsRequest>();
        request.settings.rationalFps = fps;
        const auto invalid = snapshot(fixture.model);
        requireFailure(fixture.model.setRationalAnimationSettingsExplicit(request),
                       api::ErrorCode::InvalidArgument);
        requireUnchanged(fixture.model, invalid);
    }
}

TEST_CASE("J1 authors retain identities above two to power 53 and reject exhausted allocators",
          "[j1-author][identity]") {
    constexpr std::uint64_t big = 9007199254740993ULL;
    Fixture fixture;
    auto scene = std::const_pointer_cast<core::Scene>(fixture.model.scene());
    auto data = core::SceneDocumentData(*scene);
    data.idHighWatermarks = {big, big, big, big, big, big};
    data.allocationState.nextEntityId = big;
    std::string error;
    REQUIRE(scene->replaceDocumentSnapshot(data, error));
    const auto light = fixture.sun(), entity = fixture.create();
    REQUIRE(light == big);
    REQUIRE(entity == big + 1);
    const auto material = fixture.material();
    REQUIRE(material.value == big + 1);
    auto assign = fixture.mutation<api::EntityAssignMaterialRequest>();
    assign.entityId = entity;
    assign.materialId = material;
    const auto assigned = fixture.model.assignMaterialExplicit(assign);
    REQUIRE(assigned.hasValue());
    REQUIRE(assigned.value->materialSlotId.value == big + 1);
    const auto identity = fixture.key(fixture.address(core::EntityOwner{entity}, Property::Position,
                                                     Component::X), 12.5, 1);
    REQUIRE(identity.actionId.value == big + 1);
    REQUIRE(identity.actionSlotId.value == big + 1);
    REQUIRE(identity.curveId.value == big + 1);
    REQUIRE(identity.keyId.value == big + 1);
    const auto encoded = encodedScene(fixture.model);
    REQUIRE(encoded.find('"' + std::to_string(big) + '"') != std::string::npos);
    REQUIRE(encoded.find('"' + std::to_string(big + 1) + '"') != std::string::npos);
    core::SceneDocumentData decoded;
    REQUIRE(core::SceneSerializer::decode(encoded, decoded, error));
    core::Scene reopened;
    REQUIRE(reopened.replaceDocumentSnapshot(decoded, error));
    REQUIRE(reopened.idHighWatermarks() == scene->idHighWatermarks());
    REQUIRE(reopened.find(light)->sun == scene->find(light)->sun);
    for (int exhausted = 0; exhausted < 4; ++exhausted) {
        CAPTURE(exhausted);
        Fixture failing;
        const auto cube = failing.create();
        const auto pbr = failing.material();
        auto target = std::const_pointer_cast<core::Scene>(failing.model.scene());
        auto full = core::SceneDocumentData(*target);
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        if (exhausted == 0) full.idHighWatermarks.asset = maximum;
        if (exhausted == 1) full.idHighWatermarks.materialSlot = maximum;
        if (exhausted == 2) full.idHighWatermarks.key = maximum;
        if (exhausted == 3) full.allocationState.nextEntityId = maximum;
        REQUIRE(target->replaceDocumentSnapshot(full, error));
        const auto before = snapshot(failing.model);
        if (exhausted == 0)
            requireFailure(failing.model.createMaterialExplicit(failing.mutation<api::MaterialCreateRequest>()),
                           api::ErrorCode::LimitExceeded);
        if (exhausted == 1) {
            auto request = failing.mutation<api::EntityAssignMaterialRequest>();
            request.entityId = cube;
            request.materialId = pbr;
            requireFailure(failing.model.assignMaterialExplicit(request), api::ErrorCode::LimitExceeded);
        }
        if (exhausted == 2) {
            auto request = failing.mutation<api::AnimationUpsertPropertyKeyframeRequest>();
            request.address = failing.address(core::EntityOwner{cube}, Property::Position, Component::X);
            requireFailure(failing.model.upsertAnimationPropertyKeyframeExplicit(request),
                           api::ErrorCode::LimitExceeded);
        }
        if (exhausted == 3) {
            auto request = failing.mutation<api::SunCreateRequest>();
            request.name = QStringLiteral("耗尽 Sun");
            requireFailure(failing.model.createSunExplicit(request), api::ErrorCode::LimitExceeded);
        }
        requireUnchanged(failing.model, before);
    }
}
