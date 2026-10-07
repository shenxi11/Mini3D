/*
 * 模块名: AnimationApiTests
 * 功能概述: 验证十四个动画 API 的严格边界、正式查询、原子定义事务与双 CAS 控制。
 * 对外接口: Catch2 [animation-api] 定向测试，无独立 main 或监听入口。
 * 依赖关系: SceneViewModel、EditorApiService、ApiJsonCodec、Qt Test。
 * 输入输出: 内存场景与显式请求到 wire、历史、完整身份和故障原子性断言。
 * 异常与错误: 拒绝不能发布候选；已获准停止后的故障必须返回成功及结构化诊断。
 * 维护说明: 不创建 GL 上下文，不访问用户作品；时钟检查使用真实 Qt 事件循环。
 */
#include "editor/SceneViewModel.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <limits>
#include <new>
#include <set>
#include <utility>
#include <vector>

using namespace mini3d;
namespace {
namespace api = editor::api;
using Channel = core::AnimationChannel;
using Interpolation = core::AnimationInterpolation;
using Mode = editor::AnimationMode;

struct AnimationApiFixture {
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    AnimationApiFixture() {
        model.newScene();
    }
    template <typename Request> Request query() const {
        Request request;
        request.document = service.documentState().document;
        return request;
    }
    template <typename Request> Request mutation() const {
        auto request = query<Request>();
        request.expectedDocumentRevision = service.documentState().documentRevision;
        return request;
    }
    template <typename Request> Request control() const {
        auto request = mutation<Request>();
        request.expectedSessionRevision = model.animationSessionRevision();
        return request;
    }
    QJsonObject wireQuery() const {
        return {{"document", api::ApiJsonCodec::encodeState(service.documentState())["document"]}};
    }
    QJsonObject wireMutation() const {
        auto result = wireQuery();
        result.insert("expectedDocumentRevision",
                      QString::number(service.documentState().documentRevision));
        return result;
    }
    QJsonObject wireControl() const {
        auto result = wireMutation();
        result.insert("expectedSessionRevision", QString::number(model.animationSessionRevision()));
        return result;
    }
};

struct AnimationSnapshot {
    api::DocumentState document;
    core::SceneAnimation animation;
    api::AnimationControllerState controller;
    std::shared_ptr<const renderer_gl::InstalledPose> pose;
    int count = 0;
    int index = 0;
    int cleanIndex = 0;
    bool modified = false;
    explicit AnimationSnapshot(const AnimationApiFixture& fixture)
        : document(fixture.service.documentState()), animation(fixture.model.scene()->animation()),
          controller(fixture.model.animationControllerState()),
          pose(fixture.model.installedAnimationPose()), count(fixture.model.undoStack()->count()),
          index(fixture.model.undoStack()->index()),
          cleanIndex(fixture.model.undoStack()->cleanIndex()),
          modified(fixture.model.isModified()) {
    }
    void requireUnchanged(const AnimationApiFixture& fixture) const {
        REQUIRE(fixture.service.documentState().document == document.document);
        REQUIRE(fixture.service.documentState().documentRevision == document.documentRevision);
        REQUIRE(fixture.service.documentState().historyRevision == document.historyRevision);
        REQUIRE(fixture.model.scene()->animation() == animation);
        REQUIRE(fixture.model.animationControllerState().mode == controller.mode);
        REQUIRE(fixture.model.animationFrame() == controller.frame);
        REQUIRE(fixture.model.animationSessionRevision() == controller.sessionRevision);
        REQUIRE(fixture.model.animationEvaluationId() == controller.evaluationId);
        REQUIRE(fixture.model.isAnimationLoopEnabled() == controller.loop);
        REQUIRE(fixture.model.installedAnimationPose() == pose);
        REQUIRE(fixture.model.undoStack()->count() == count);
        REQUIRE(fixture.model.undoStack()->index() == index);
        REQUIRE(fixture.model.undoStack()->cleanIndex() == cleanIndex);
        REQUIRE(fixture.model.isModified() == modified);
    }
};

api::AnimationUpsertItem key(core::EntityId entity, Channel channel, std::uint32_t frame,
                             const glm::dvec3& value,
                             Interpolation interpolation = Interpolation::Linear) {
    api::AnimationUpsertItem item;
    item.entityId = entity;
    item.channel = channel;
    item.frame = frame;
    item.value = value;
    item.interpolation = interpolation;
    return item;
}
core::SceneAnimation translation(core::EntityId entity) {
    core::SceneAnimation animation;
    animation.settings.endFrame = 49;
    animation.tracks[{entity, Channel::Position}] = {
        {{1, {0, 0, 0}, Interpolation::Linear}, {49, {48, 0, 0}, Interpolation::Linear}}};
    return animation;
}
QJsonObject wireTarget(core::EntityId entity, const QString& channel, int frame) {
    return {{"entityId", QString::number(entity)}, {"channel", channel}, {"frame", frame}};
}
QJsonObject wireKey(core::EntityId entity, const QString& channel, int frame,
                    const QJsonArray& value,
                    const QString& interpolation = QStringLiteral("linear")) {
    auto result = wireTarget(entity, channel, frame);
    result.insert("value", value);
    result.insert("interpolation", interpolation);
    return result;
}
QJsonObject addFields(QJsonObject params, const QJsonObject& fields) {
    for (auto iterator = fields.begin(); iterator != fields.end(); ++iterator)
        params.insert(iterator.key(), iterator.value());
    return params;
}
enum class WireKind { Query, Definition, Control };
struct WireCase {
    QString method;
    WireKind kind;
    QJsonObject params;
};
std::vector<WireCase> wireCases(const AnimationApiFixture& fixture, core::EntityId entity) {
    const auto query = fixture.wireQuery();
    const auto mutation = fixture.wireMutation();
    const auto control = fixture.wireControl();
    return {
        {"animation.getState", WireKind::Query, query},
        {"animation.listTracks", WireKind::Query, query},
        {"animation.readKeyframes", WireKind::Query,
         addFields(query, {{"entityId", QString::number(entity)}, {"channel", "position"}})},
        {"animation.sample", WireKind::Query,
         addFields(query, {{"frame", 12.5}, {"entityIds", QJsonArray{QString::number(entity)}}})},
        {"animation.setSettings", WireKind::Definition,
         addFields(mutation, {{"settings", QJsonObject{{"fps", 60}, {"startFrame", 1},
                                                        {"endFrame", 49}}}})},
        {"animation.upsertKeyframes", WireKind::Definition,
         addFields(mutation, {{"items", QJsonArray{wireKey(entity, "position", 5, {4, 0, 0})}},
                              {"onConflict", "replace"}})},
        {"animation.deleteKeyframes", WireKind::Definition,
         addFields(mutation, {{"items", QJsonArray{wireTarget(entity, "position", 5)}}})},
        {"animation.removeTrack", WireKind::Definition,
         addFields(mutation, {{"entityId", QString::number(entity)}, {"channel", "scale"}})},
        {"animation.moveKeyframe", WireKind::Definition,
         addFields(mutation, {{"entityId", QString::number(entity)}, {"channel", "position"},
                              {"fromFrame", 1}, {"toFrame", 2}, {"onConflict", "reject"}})},
        {"animation.setPreview", WireKind::Control, addFields(control, {{"enabled", true}})},
        {"animation.setFrame", WireKind::Control, addFields(control, {{"frame", 12.5}})},
        {"animation.play", WireKind::Control, control},
        {"animation.pause", WireKind::Control, control},
        {"animation.setLoop", WireKind::Control, addFields(control, {{"enabled", true}})}};
}
template <typename T> void requireFailure(const api::ApiResult<T>& result, api::ErrorCode code) {
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error);
    REQUIRE(result.error->code == code);
}
void requireWireFailure(const QJsonObject& response, const QString& code,
                        const QString& field = {}) {
    REQUIRE_FALSE(response.contains("result"));
    REQUIRE(response.contains("error"));
    const auto data = response["error"].toObject()["data"].toObject();
    REQUIRE(data["code"].toString() == code);
    if (!field.isEmpty())
        REQUIRE(data["fieldPath"].toString() == field);
}
} // namespace

TEST_CASE("All fourteen animation wire requests are strict and canonical retain business CAS",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    const auto cases = wireCases(fixture, entity);
    REQUIRE(cases.size() == 14);
    for (const auto& test : cases) {
        CAPTURE(test.method.toStdString());
        REQUIRE(api::ApiJsonCodec::decodeRequest(test.method, test.params).hasValue());
        auto unknown = test.params;
        unknown.insert("unexpected", true);
        const auto rejected = api::ApiJsonCodec::decodeRequest(test.method, unknown);
        requireFailure(rejected, api::ErrorCode::InvalidArgument);
        REQUIRE(rejected.error->protocolCode == -32602);
        auto incomplete = test.params;
        incomplete.remove("document");
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest(test.method, incomplete).hasValue());
        auto maximum = test.params;
        maximum.insert("expectedDocumentRevision", "18446744073709551615");
        REQUIRE(api::ApiJsonCodec::decodeRequest(test.method, maximum).hasValue());
        for (const QJsonValue invalid : {QJsonValue("01"), QJsonValue("-1"),
                                        QJsonValue("18446744073709551616"), QJsonValue(1)}) {
            auto params = test.params;
            params.insert("expectedDocumentRevision", invalid);
            const auto failure = api::ApiJsonCodec::decodeRequest(test.method, params);
            requireFailure(failure, api::ErrorCode::InvalidArgument);
            REQUIRE(failure.error->fieldPath == "expectedDocumentRevision");
        }
        const auto canonical = api::ApiJsonCodec::canonicalParams(test.method, test.params);
        REQUIRE(canonical.hasValue());
        REQUIRE(api::ApiJsonCodec::decodeRequest(test.method, *canonical.value).hasValue());
        if (test.kind != WireKind::Query) {
            auto missingRevision = test.params;
            missingRevision.remove("expectedDocumentRevision");
            REQUIRE_FALSE(
                api::ApiJsonCodec::decodeRequest(test.method, missingRevision).hasValue());
            auto envelope = test.params;
            envelope.insert("clientSessionId", "12345678-1234-1234-1234-123456789abc");
            envelope.insert("mutationSequence", "18446744073709551615");
            envelope.insert("timeoutMs", int(api::limits::mutationTimeoutMs));
            const auto replay = api::ApiJsonCodec::canonicalParams(test.method, envelope);
            REQUIRE(replay.hasValue());
            REQUIRE(*replay.value == *canonical.value);
            REQUIRE_FALSE(replay.value->contains("clientSessionId"));
            REQUIRE_FALSE(replay.value->contains("mutationSequence"));
            REQUIRE((*replay.value)["expectedDocumentRevision"] ==
                    test.params["expectedDocumentRevision"]);
        }
        if (test.kind == WireKind::Control) {
            REQUIRE((*canonical.value)["expectedSessionRevision"] ==
                    test.params["expectedSessionRevision"]);
            auto params = test.params;
            params.remove("expectedSessionRevision");
            const auto missing = api::ApiJsonCodec::decodeRequest(test.method, params);
            requireFailure(missing, api::ErrorCode::InvalidArgument);
            REQUIRE(missing.error->fieldPath == "expectedSessionRevision");
            params.insert("expectedSessionRevision", 1);
            REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest(test.method, params).hasValue());
            params.insert("expectedSessionRevision", "18446744073709551615");
            REQUIRE(api::ApiJsonCodec::decodeRequest(test.method, params).hasValue());
        }
    }
}

TEST_CASE("Animation wire preserves complete raw doubles and rejects malformed key fields",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    const QJsonArray raw{360.000000001, -720.1234567890123, 1.000000001};
    for (const auto& interpolation : {QStringLiteral("constant"), QStringLiteral("linear")}) {
        auto params = addFields(fixture.wireMutation(),
            {{"items", QJsonArray{wireKey(entity, "rotationEulerXYZDegrees", 1, raw,
                                          interpolation)}},
             {"onConflict", "replace"}});
        const auto decoded = api::ApiJsonCodec::decodeRequest("animation.upsertKeyframes", params);
        REQUIRE(decoded.hasValue());
        const auto& request = std::get<api::AnimationUpsertKeyframesRequest>(*decoded.value);
        REQUIRE(request.items.front().value == glm::dvec3(raw[0].toDouble(), raw[1].toDouble(),
                                                          raw[2].toDouble()));
        const auto canonical =
            api::ApiJsonCodec::canonicalParams("animation.upsertKeyframes", params);
        REQUIRE(canonical.hasValue());
        REQUIRE((*canonical.value)["items"].toArray()[0].toObject()["value"].toArray() == raw);
        REQUIRE((*canonical.value)["items"].toArray()[0].toObject()["interpolation"] ==
                interpolation);
        for (const auto& field : {QStringLiteral("entityId"), QStringLiteral("channel"),
                                  QStringLiteral("frame"), QStringLiteral("value"),
                                  QStringLiteral("interpolation")}) {
            auto item = params["items"].toArray()[0].toObject();
            item.remove(field);
            params.insert("items", QJsonArray{item});
            REQUIRE_FALSE(
                api::ApiJsonCodec::decodeRequest("animation.upsertKeyframes", params).hasValue());
            params.insert("items", QJsonArray{wireKey(entity, "rotationEulerXYZDegrees", 1, raw,
                                                       interpolation)});
        }
        params.remove("onConflict");
        REQUIRE_FALSE(
            api::ApiJsonCodec::decodeRequest("animation.upsertKeyframes", params).hasValue());
    }
    const auto valid = wireKey(entity, "position", 1, {1, 2, 3});
    for (const auto& replacement :
         std::vector<QJsonObject>{{{"frame", 1.25}}, {{"frame", 0}}, {{"frame", 100001}},
                                   {{"entityId", "0"}}, {{"entityId", "01"}},
                                   {{"channel", "rotation"}}, {{"interpolation", "bezier"}},
                                   {{"value", QJsonArray{1, 2}}},
                                   {{"value", QJsonArray{1, 2, "3"}}}, {{"extra", true}}}) {
        const auto params = addFields(fixture.wireMutation(),
            {{"items", QJsonArray{addFields(valid, replacement)}}, {"onConflict", "replace"}});
        REQUIRE_FALSE(
            api::ApiJsonCodec::decodeRequest("animation.upsertKeyframes", params).hasValue());
    }
    const auto duplicate = addFields(fixture.wireMutation(),
        {{"items", QJsonArray{valid, valid}}, {"onConflict", "replace"}});
    REQUIRE_FALSE(
        api::ApiJsonCodec::decodeRequest("animation.upsertKeyframes", duplicate).hasValue());
    auto settings = addFields(fixture.wireMutation(),
        {{"settings", QJsonObject{{"fps", 24}, {"startFrame", 1}, {"endFrame", 49},
                                   {"extra", true}}}});
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.setSettings", settings).hasValue());
    auto move = wireCases(fixture, entity)[8].params;
    move.remove("onConflict");
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.moveKeyframe", move).hasValue());
}

TEST_CASE("Animation describe exposes the four read five write five control methods and budgets",
          "[animation-api]") {
    AnimationApiFixture fixture;
    for (const bool observation : {false, true}) {
        fixture.service.setObservationAvailable(observation);
        const auto result = fixture.service.describe();
        REQUIRE(result.hasValue());
        REQUIRE(result.value->apiVersion == "0.2.0");
        REQUIRE(result.value->wireVersion == 1);
        REQUIRE(result.value->methods.size() == (observation ? 61 : 57));
        std::array<int, 3> permissions{};
        std::set<QString> names;
        for (const auto& method : result.value->methods) {
            REQUIRE(names.insert(method.name).second);
            if (!method.name.startsWith("animation."))
                continue;
            REQUIRE(method.externalEnabled);
            REQUIRE(method.externalGate.isEmpty());
            if (method.permission == "scene.read") {
                ++permissions[0];
                REQUIRE(method.kind == "query");
            } else if (method.permission == "scene.write") {
                ++permissions[1];
                REQUIRE(method.kind == "mutation");
            } else {
                REQUIRE(method.permission == "viewport.control");
                REQUIRE(method.kind == "mutation");
                ++permissions[2];
            }
        }
        REQUIRE((permissions == std::array<int, 3>{4, 5, 5}));
        for (const auto& test : wireCases(fixture, 1))
            REQUIRE(names.contains(test.method));
        const auto& limits = result.value->limits;
        REQUIRE(limits.at("animationTracks") == 3000);
        REQUIRE(limits.at("animationTotalKeyframes") == 100000);
        REQUIRE(limits.at("animationTrackKeyframes") == 10000);
        REQUIRE(limits.at("animationBatchItems") == 1024);
        REQUIRE(limits.at("animationSampleEntities") == 256);
        REQUIRE(limits.at("animationPoseNodes") == 10000);
        REQUIRE(limits.at("animationPreflightVisits") == 40000);
        REQUIRE(limits.at("candidateBytes") == 64 * 1024 * 1024);
        REQUIRE(result.value->entityPageDefault == 256);
        REQUIRE(result.value->entityPageMaximum == 2048);
    }
}

TEST_CASE("Animation invoke reaches all fourteen service and response branches",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    auto animation = translation(entity);
    animation.tracks[{entity, Channel::Scale}] = {{{1, {1, 1, 1}, Interpolation::Constant}}};
    REQUIRE(fixture.model.replaceAnimation(animation));
    for (auto test : wireCases(fixture, entity)) {
        CAPTURE(test.method.toStdString());
        if (test.kind != WireKind::Query)
            test.params.insert("expectedDocumentRevision",
                               QString::number(fixture.service.documentState().documentRevision));
        if (test.kind == WireKind::Control)
            test.params.insert("expectedSessionRevision",
                               QString::number(fixture.model.animationSessionRevision()));
        const auto response = api::ApiJsonCodec::invoke(fixture.service, test.method, test.params);
        REQUIRE_FALSE(response.contains("error"));
        REQUIRE(response.contains("result"));
        const auto value = response["result"].toObject();
        const auto state = api::ApiJsonCodec::encodeState(fixture.service.documentState());
        for (const auto& field : {"document", "documentRevision", "historyRevision"})
            REQUIRE(value[field] == state[field]);
        if (test.kind == WireKind::Control) {
            REQUIRE(value["sessionRevision"].toString() ==
                    QString::number(fixture.model.animationSessionRevision()));
            REQUIRE(value["evaluationId"].toString() ==
                    QString::number(fixture.model.animationEvaluationId()));
            REQUIRE(value["mode"].toString() == fixture.model.animationControllerState().mode);
            REQUIRE(value["status"].toString() == "committed");
        }
    }
    REQUIRE(fixture.model.animationMode() == Mode::PreviewPaused);
    REQUIRE(fixture.model.isAnimationLoopEnabled());
}

TEST_CASE("Animation pages sort numeric IDs and bind continuations to document and content CAS",
          "[animation-api]") {
    AnimationApiFixture fixture;
    std::vector<core::EntityId> entities;
    for (int index = 0; index < 10; ++index)
        entities.push_back(fixture.model.createEntity(core::PrimitiveKind::Empty));
    const auto small = entities[1];
    const auto large = entities[9];
    REQUIRE(small < large);
    REQUIRE(QString::number(small) > QString::number(large));
    core::SceneAnimation animation;
    for (const auto channel : {Channel::Position, Channel::RotationEulerXYZDegrees, Channel::Scale})
        animation.tracks[{small, channel}] = {
            {{1, {1, 2, 3}, Interpolation::Constant}, {10, {4, 5, 6}, Interpolation::Linear},
             {100, {7, 8, 9}, Interpolation::Linear}}};
    for (const auto channel : {Channel::Position, Channel::Scale})
        animation.tracks[{large, channel}] = {{{1, {1, 1, 1}, Interpolation::Constant}}};
    REQUIRE(fixture.model.replaceAnimation(animation));
    auto request = fixture.query<api::AnimationListTracksRequest>();
    request.limit = 2;
    const auto first = fixture.service.animationTracks(request);
    REQUIRE(first.hasValue());
    REQUIRE(first.value->tracks.size() == 2);
    REQUIRE(first.value->tracks[0].entityId == small);
    REQUIRE(first.value->tracks[0].channel == Channel::Position);
    REQUIRE(first.value->tracks[1].channel == Channel::RotationEulerXYZDegrees);
    REQUIRE(first.value->tracks[1].keyframeCount == 3);
    REQUIRE(first.value->nextAfterTrack);
    request.afterTrack = first.value->nextAfterTrack;
    requireFailure(fixture.service.animationTracks(request), api::ErrorCode::InvalidArgument);
    request.expectedDocumentRevision = first.value->state.documentRevision;
    const auto second = fixture.service.animationTracks(request);
    REQUIRE(second.hasValue());
    REQUIRE(second.value->tracks.size() == 2);
    REQUIRE(second.value->tracks[0].entityId == small);
    REQUIRE(second.value->tracks[0].channel == Channel::Scale);
    REQUIRE(second.value->tracks[1].entityId == large);
    REQUIRE(second.value->tracks[1].channel == Channel::Position);
    REQUIRE(second.value->nextAfterTrack);
    request.afterTrack = second.value->nextAfterTrack;
    const auto last = fixture.service.animationTracks(request);
    REQUIRE(last.hasValue());
    REQUIRE(last.value->tracks.size() == 1);
    REQUIRE(last.value->tracks[0].entityId == large);
    REQUIRE(last.value->tracks[0].channel == Channel::Scale);
    REQUIRE_FALSE(last.value->nextAfterTrack);
    REQUIRE(api::ApiJsonCodec::encode(*last.value)["nextAfterTrack"].isNull());
    auto mismatch = request;
    mismatch.entityId = small;
    requireFailure(fixture.service.animationTracks(mismatch), api::ErrorCode::InvalidArgument);
    auto stale = request;
    --*stale.expectedDocumentRevision;
    requireFailure(fixture.service.animationTracks(stale), api::ErrorCode::RevisionConflict);
    stale = request;
    stale.document.documentId = "12345678-1234-1234-1234-123456789abc";
    requireFailure(fixture.service.animationTracks(stale), api::ErrorCode::StaleDocument);
    auto filtered = fixture.query<api::AnimationListTracksRequest>();
    filtered.entityId = large;
    filtered.limit = 1;
    const auto filteredFirst = fixture.service.animationTracks(filtered);
    REQUIRE(filteredFirst.hasValue());
    REQUIRE(filteredFirst.value->tracks.size() == 1);
    REQUIRE(filteredFirst.value->tracks[0].entityId == large);
    filtered.afterTrack = filteredFirst.value->nextAfterTrack;
    filtered.expectedDocumentRevision = first.value->state.documentRevision;
    const auto filteredLast = fixture.service.animationTracks(filtered);
    REQUIRE(filteredLast.hasValue());
    REQUIRE(filteredLast.value->tracks.size() == 1);
    REQUIRE(filteredLast.value->tracks[0].channel == Channel::Scale);
    REQUIRE_FALSE(filteredLast.value->nextAfterTrack);

    auto keys = fixture.query<api::AnimationReadKeyframesRequest>();
    keys.entityId = small;
    keys.channel = Channel::RotationEulerXYZDegrees;
    keys.limit = 2;
    const auto keyFirst = fixture.service.animationKeyframes(keys);
    REQUIRE(keyFirst.hasValue());
    REQUIRE(keyFirst.value->keyframes.size() == 2);
    REQUIRE(keyFirst.value->keyframes[0].frame == 1);
    REQUIRE(keyFirst.value->keyframes[1].frame == 10);
    REQUIRE(keyFirst.value->nextAfterFrame == 10);
    keys.afterFrame = keyFirst.value->nextAfterFrame;
    requireFailure(fixture.service.animationKeyframes(keys), api::ErrorCode::InvalidArgument);
    keys.expectedDocumentRevision = keyFirst.value->state.documentRevision;
    const auto keyLast = fixture.service.animationKeyframes(keys);
    REQUIRE(keyLast.hasValue());
    REQUIRE(keyLast.value->keyframes.size() == 1);
    REQUIRE(keyLast.value->keyframes[0].frame == 100);
    REQUIRE_FALSE(keyLast.value->nextAfterFrame);
    REQUIRE(api::ApiJsonCodec::encode(*keyLast.value)["nextAfterFrame"].isNull());
    keys.afterFrame = 100;
    const auto emptyTail = fixture.service.animationKeyframes(keys);
    REQUIRE(emptyTail.hasValue());
    REQUIRE(emptyTail.value->keyframes.empty());
    REQUIRE_FALSE(emptyTail.value->nextAfterFrame);
    keys.entityId = entities[2];
    keys.afterFrame.reset();
    const auto absent = fixture.service.animationKeyframes(keys);
    REQUIRE(absent.hasValue());
    REQUIRE(absent.value->keyframes.empty());
    REQUIRE(api::ApiJsonCodec::encode(*absent.value)["nextAfterFrame"].isNull());
    keys.entityId = std::numeric_limits<core::EntityId>::max();
    requireFailure(fixture.service.animationKeyframes(keys), api::ErrorCode::NotFound);
    filtered.entityId = keys.entityId;
    filtered.afterTrack.reset();
    requireFailure(fixture.service.animationTracks(filtered), api::ErrorCode::NotFound);
}

TEST_CASE("Animation sample reads formal definitions in Base Playing and Draft without effects",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto parent = fixture.model.createEntity(core::PrimitiveKind::Empty);
    const auto child = fixture.model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(fixture.model.setParent(child, parent));
    auto parentBase = fixture.model.scene()->find(parent)->transform;
    parentBase.position = {100, 0, 0};
    REQUIRE(fixture.model.setTransform(parent, parentBase));
    auto childBase = fixture.model.scene()->find(child)->transform;
    childBase.position = {1, 2, 3};
    REQUIRE(fixture.model.setTransform(child, childBase));
    const glm::dvec3 rawEuler{360.000000001, 10, 720.1234567890123};
    core::SceneAnimation animation;
    animation.settings.endFrame = 49;
    animation.tracks[{parent, Channel::Position}] = {
        {{1, {2, 3, 4}, Interpolation::Constant}, {10, {10, 20, 30}, Interpolation::Linear},
         {20, {20, 40, 60}, Interpolation::Linear}}};
    animation.tracks[{child, Channel::RotationEulerXYZDegrees}] = {
        {{1, rawEuler, Interpolation::Constant}}};
    REQUIRE(fixture.model.replaceAnimation(animation));
    SECTION("Base ignores the base cursor and samples the formal animation") {
        REQUIRE_FALSE(fixture.model.installedAnimationPose());
    }
    SECTION("Playing formal queries do not advance or replace the installed pose") {
        REQUIRE(fixture.model.setAnimationPreview(true));
        REQUIRE(fixture.model.setAnimationFrame(5));
        REQUIRE(fixture.model.playAnimation());
    }
    SECTION("Draft values never enter the formal sample") {
        REQUIRE(fixture.model.setAnimationPreview(true));
        REQUIRE(fixture.model.setAnimationFrame(5));
        REQUIRE(fixture.model.beginAnimationDraft(child, {true, false, false}));
        REQUIRE(fixture.model.setAnimationDraftChannel(Channel::Position, {99, 98, 97}));
        REQUIRE(fixture.model.installedAnimationPose()->numerics->find(child)->local.position.x ==
                99);
    }
    int viewCalls = 0;
    renderer_gl::EditorCamera view;
    REQUIRE(view.setState(fixture.model.editorCamera()));
    fixture.model.setAnimationViewProvider([&] { ++viewCalls; return view; });
    const AnimationSnapshot before(fixture);
    QSignalSpy poseNotifications(&fixture.model, &editor::SceneViewModel::animationPoseChanged);
    QSignalSpy sessionNotifications(&fixture.model,
                                   &editor::SceneViewModel::animationSessionChanged);
    QSignalSpy sceneNotifications(&fixture.model, &editor::SceneViewModel::sceneChanged);
    auto request = fixture.query<api::AnimationSampleRequest>();
    request.frame = 10.5;
    request.entityIds = {child, parent};
    const auto sample = fixture.service.sampleAnimation(request);
    REQUIRE(sample.hasValue());
    REQUIRE(sample.value->frame == 10.5);
    REQUIRE(sample.value->entities.size() == 2);
    const auto& childValue = sample.value->entities[0];
    const auto& parentValue = sample.value->entities[1];
    REQUIRE(childValue.entityId == child);
    REQUIRE(parentValue.entityId == parent);
    REQUIRE(childValue.localTransform.position == childBase.position);
    REQUIRE(childValue.rotationSource == "animationTrack");
    REQUIRE(childValue.rotationEulerXYZDegrees == rawEuler);
    REQUIRE(parentValue.rotationSource == "baseQuaternion");
    REQUIRE_FALSE(parentValue.rotationEulerXYZDegrees);
    REQUIRE(parentValue.localTransform.position == glm::vec3(10.5F, 21, 31.5F));
    REQUIRE(childValue.worldMatrix[3] == glm::vec4(11.5F, 23, 34.5F, 1));
    const auto json = api::ApiJsonCodec::encode(*sample.value);
    const auto childJson = json["entities"].toArray()[0].toObject();
    const auto parentJson = json["entities"].toArray()[1].toObject();
    REQUIRE((childJson["rotationEulerXYZDegrees"].toArray() ==
             QJsonArray{rawEuler.x, rawEuler.y, rawEuler.z}));
    REQUIRE_FALSE(parentJson.contains("rotationEulerXYZDegrees"));
    REQUIRE((childJson["localTransform"].toObject()["translation"].toArray() ==
             QJsonArray{1, 2, 3}));
    const auto matrix = childJson["worldMatrix"].toArray();
    REQUIRE(matrix.size() == 16);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            REQUIRE(matrix[column * 4 + row].toDouble() == childValue.worldMatrix[column][row]);
    for (const auto [frame, expected] :
         std::array<std::pair<double, glm::vec3>, 2>{{{9.999, {2, 3, 4}}, {10, {10, 20, 30}}}}) {
        request.frame = frame;
        const auto boundary = fixture.service.sampleAnimation(request);
        REQUIRE(boundary.hasValue());
        REQUIRE(boundary.value->entities[1].localTransform.position == expected);
    }
    REQUIRE(fixture.service.animationState(fixture.query<api::AnimationQueryRequest>()).hasValue());
    REQUIRE(fixture.service.animationTracks(fixture.query<api::AnimationListTracksRequest>())
                .hasValue());
    auto keys = fixture.query<api::AnimationReadKeyframesRequest>();
    keys.entityId = child;
    keys.channel = Channel::RotationEulerXYZDegrees;
    const auto rawKeys = fixture.service.animationKeyframes(keys);
    REQUIRE(rawKeys.hasValue());
    REQUIRE(api::ApiJsonCodec::encode(*rawKeys.value)["keyframes"].toArray()[0]
                .toObject()["value"].toArray() == childJson["rotationEulerXYZDegrees"].toArray());
    before.requireUnchanged(fixture);
    REQUIRE(viewCalls == 0);
    REQUIRE(poseNotifications.count() == 0);
    REQUIRE(sessionNotifications.count() == 0);
    REQUIRE(sceneNotifications.count() == 0);
}

TEST_CASE("Animation formal queries honor physical Busy and real object gestures",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Cube);
    bool gesture = false;
    SECTION("Window provider rejects all four formal queries") {
        fixture.service.setBusyProvider([] { return QStringList{"navigation"}; });
    }
    SECTION("Base object transform rejects all four formal queries") {
        gesture = true;
        fixture.model.beginTransformEdit(entity);
    }
    const AnimationSnapshot before(fixture);
    for (const auto& test : wireCases(fixture, entity)) {
        if (test.kind != WireKind::Query)
            continue;
        requireWireFailure(api::ApiJsonCodec::invoke(fixture.service, test.method, test.params),
                            "BUSY");
    }
    before.requireUnchanged(fixture);
    if (gesture)
        fixture.model.cancelTransformEdit();
    else
        fixture.service.setBusyProvider({});
    auto sample = fixture.query<api::AnimationSampleRequest>();
    sample.entityIds = {entity, entity};
    requireFailure(fixture.service.sampleAnimation(sample), api::ErrorCode::InvalidArgument);
    sample.entityIds = {std::numeric_limits<core::EntityId>::max()};
    requireFailure(fixture.service.sampleAnimation(sample), api::ErrorCode::NotFound);
    sample.entityIds.assign(api::limits::animationSampleEntities + 1, entity);
    requireFailure(fixture.service.sampleAnimation(sample), api::ErrorCode::LimitExceeded);
    sample.entityIds = {entity};
    sample.frame = std::numeric_limits<double>::infinity();
    requireFailure(fixture.service.sampleAnimation(sample), api::ErrorCode::InvalidArgument);
}

TEST_CASE("Animation key batches commit 1024 targets once and reject duplicates or overflow",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    auto request = fixture.mutation<api::AnimationUpsertKeyframesRequest>();
    request.onConflict = api::AnimationConflict::Reject;
    for (std::uint32_t frame = 1; frame <= api::limits::animationBatchItems; ++frame)
        request.items.push_back(key(entity, Channel::Position, frame, {double(frame), 0, 0},
                                    Interpolation::Constant));
    const AnimationSnapshot before(fixture);
    const auto committed = fixture.service.upsertAnimationKeyframes(request);
    REQUIRE(committed.hasValue());
    REQUIRE(committed.value->status == api::ResultStatus::Committed);
    REQUIRE(committed.value->undoable);
    REQUIRE(committed.value->affectedEntityIds == std::vector<core::EntityId>{entity});
    REQUIRE(fixture.model.undoStack()->count() == before.count + 1);
    REQUIRE(fixture.model.undoStack()->index() == before.index + 1);
    REQUIRE(fixture.service.documentState().documentRevision ==
            before.document.documentRevision + 1);
    REQUIRE(fixture.service.documentState().historyRevision == before.document.historyRevision + 1);
    REQUIRE(fixture.model.scene()->animation().tracks.at({entity, Channel::Position}).keys.size() ==
            1024);
    const AnimationSnapshot installed(fixture);
    request.expectedDocumentRevision = fixture.service.documentState().documentRevision;
    request.items.push_back(key(entity, Channel::Position, 1025, {1025, 0, 0}));
    requireFailure(fixture.service.upsertAnimationKeyframes(request),
                    api::ErrorCode::LimitExceeded);
    installed.requireUnchanged(fixture);
    request.items = {key(entity, Channel::Position, 1, {10, 0, 0}),
                     key(entity, Channel::Position, 1, {20, 0, 0})};
    request.onConflict = api::AnimationConflict::Replace;
    requireFailure(fixture.service.upsertAnimationKeyframes(request),
                    api::ErrorCode::InvalidArgument);
    installed.requireUnchanged(fixture);
    request.items[1] =
        key(std::numeric_limits<core::EntityId>::max(), Channel::Position, 2, {0, 0, 0});
    requireFailure(fixture.service.upsertAnimationKeyframes(request), api::ErrorCode::NotFound);
    installed.requireUnchanged(fixture);
    auto deletion = fixture.mutation<api::AnimationDeleteKeyframesRequest>();
    deletion.items = {{entity, Channel::Position, 1}, {entity, Channel::Position, 1}};
    requireFailure(fixture.service.deleteAnimationKeyframes(deletion),
                    api::ErrorCode::InvalidArgument);
    installed.requireUnchanged(fixture);
}

TEST_CASE("Animation C++ upsert and move require an explicit conflict policy", "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    REQUIRE(fixture.model.replaceAnimation(translation(entity)));
    const AnimationSnapshot before(fixture);
    auto insertion = fixture.mutation<api::AnimationUpsertKeyframesRequest>();
    insertion.items = {key(entity, Channel::Position, 5, {4, 0, 0})};
    const auto unspecifiedInsert = fixture.service.upsertAnimationKeyframes(insertion);
    requireFailure(unspecifiedInsert, api::ErrorCode::InvalidArgument);
    REQUIRE(unspecifiedInsert.error->fieldPath == "onConflict");
    auto move = fixture.mutation<api::AnimationMoveKeyframeRequest>();
    move.entityId = entity;
    move.fromFrame = 1;
    move.toFrame = 5;
    const auto unspecifiedMove = fixture.service.moveAnimationKeyframe(move);
    requireFailure(unspecifiedMove, api::ErrorCode::InvalidArgument);
    REQUIRE(unspecifiedMove.error->fieldPath == "onConflict");
    before.requireUnchanged(fixture);
    insertion.onConflict = api::AnimationConflict::Reject;
    insertion.items[0].frame = 1;
    requireFailure(fixture.service.upsertAnimationKeyframes(insertion),
                    api::ErrorCode::InvalidArgument);
    before.requireUnchanged(fixture);
    insertion.onConflict = api::AnimationConflict::Replace;
    REQUIRE(fixture.service.upsertAnimationKeyframes(insertion).hasValue());
    const auto& replacedKeys =
        fixture.model.scene()->animation().tracks.at({entity, Channel::Position}).keys;
    REQUIRE(replacedKeys[0].value.x == 4);
}

TEST_CASE("Animation move replace preserves the source raw value and leaves one destination key",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    core::SceneAnimation animation;
    const glm::dvec3 raw{360.000000001, 720.1234567890123, 1.000000001};
    animation.tracks[{entity, Channel::RotationEulerXYZDegrees}] = {
        {{1, raw, Interpolation::Constant}, {5, {4, 5, 6}, Interpolation::Linear},
         {9, {7, 8, 9}, Interpolation::Linear}}};
    REQUIRE(fixture.model.replaceAnimation(animation));
    auto request = fixture.mutation<api::AnimationMoveKeyframeRequest>();
    request.entityId = entity;
    request.channel = Channel::RotationEulerXYZDegrees;
    request.fromFrame = 1;
    request.toFrame = 5;
    request.onConflict = api::AnimationConflict::Reject;
    const AnimationSnapshot before(fixture);
    requireFailure(fixture.service.moveAnimationKeyframe(request), api::ErrorCode::InvalidArgument);
    before.requireUnchanged(fixture);
    request.onConflict = api::AnimationConflict::Replace;
    const auto result = fixture.service.moveAnimationKeyframe(request);
    REQUIRE(result.hasValue());
    REQUIRE(result.value->status == api::ResultStatus::Committed);
    const auto& keys = fixture.model.scene()->animation().tracks.at({entity, request.channel}).keys;
    REQUIRE(keys.size() == 2);
    REQUIRE(keys[0].frame == 5);
    REQUIRE(keys[0].value == raw);
    REQUIRE(keys[0].interpolation == Interpolation::Constant);
    REQUIRE(keys[1].frame == 9);
    REQUIRE(fixture.model.undoStack()->index() == before.index + 1);
    fixture.model.undo();
    REQUIRE(fixture.model.scene()->animation() == animation);
    fixture.model.redo();
    REQUIRE(fixture.model.scene()->animation().tracks.at({entity, request.channel}).keys[0].value ==
            raw);
}

TEST_CASE("Animation delete and move validate the final scale adjacency atomically",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    core::SceneAnimation animation;
    animation.tracks[{entity, Channel::Scale}] = {
        {{1, {1, 1, 1}, Interpolation::Linear}, {5, {2, 2, 2}, Interpolation::Constant},
         {9, {-1, -1, -1}, Interpolation::Linear}}};
    REQUIRE(fixture.model.replaceAnimation(animation));
    const AnimationSnapshot before(fixture);
    SECTION("Deleting a middle key cannot introduce a zero crossing") {
        auto request = fixture.mutation<api::AnimationDeleteKeyframesRequest>();
        request.items = {{entity, Channel::Scale, 5}};
        requireFailure(fixture.service.deleteAnimationKeyframes(request),
                        api::ErrorCode::InvalidArgument);
        before.requireUnchanged(fixture);
    }
    SECTION("Moving a middle key cannot introduce a zero crossing") {
        auto request = fixture.mutation<api::AnimationMoveKeyframeRequest>();
        request.entityId = entity;
        request.channel = Channel::Scale;
        request.fromFrame = 5;
        request.toFrame = 10;
        request.onConflict = api::AnimationConflict::Replace;
        requireFailure(fixture.service.moveAnimationKeyframe(request),
                        api::ErrorCode::InvalidArgument);
        before.requireUnchanged(fixture);
    }
    SECTION("A batch deleting both adjacent keys is judged by its final candidate") {
        auto request = fixture.mutation<api::AnimationDeleteKeyframesRequest>();
        request.items = {{entity, Channel::Scale, 5}, {entity, Channel::Scale, 9}};
        const auto result = fixture.service.deleteAnimationKeyframes(request);
        REQUIRE(result.hasValue());
        REQUIRE(result.value->status == api::ResultStatus::Committed);
        const auto& keys =
            fixture.model.scene()->animation().tracks.at({entity, Channel::Scale}).keys;
        REQUIRE(keys.size() == 1);
        REQUIRE(fixture.model.undoStack()->index() == before.index + 1);
    }
}

TEST_CASE("All definition no changes preserve redo clean point and still execute the final guard",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    REQUIRE(fixture.model.replaceAnimation(translation(entity)));
    const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
    auto settings = fixture.mutation<api::AnimationSetSettingsRequest>();
    settings.settings = fixture.model.scene()->animation().settings;
    settings.settings.fps = 60;
    REQUIRE(fixture.service.setAnimationSettings(settings).hasValue());
    fixture.model.undo();
    REQUIRE(fixture.model.undoStack()->canRedo());
    REQUIRE(fixture.model.undoStack()->isClean());
    const AnimationSnapshot before(fixture);
    const auto* redo = fixture.model.undoStack()->command(before.index);
    const auto mutation = fixture.wireMutation();
    const std::vector<std::pair<QString, QJsonObject>> requests{
        {"animation.setSettings", addFields(mutation,
            {{"settings", QJsonObject{{"fps", 24}, {"startFrame", 1}, {"endFrame", 49}}}})},
        {"animation.upsertKeyframes", addFields(mutation,
            {{"items", QJsonArray{wireKey(entity, "position", 1, {0, 0, 0})}},
             {"onConflict", "replace"}})},
        {"animation.deleteKeyframes", addFields(mutation,
            {{"items", QJsonArray{wireTarget(entity, "position", 25)}}})},
        {"animation.removeTrack", addFields(mutation,
            {{"entityId", QString::number(entity)}, {"channel", "scale"}})},
        {"animation.moveKeyframe", addFields(mutation,
            {{"entityId", QString::number(entity)}, {"channel", "position"},
             {"fromFrame", 1}, {"toFrame", 1}, {"onConflict", "reject"}})}};
    int calls = 0;
    for (const auto& [method, params] : requests) {
        CAPTURE(method.toStdString());
        const auto previousCalls = calls;
        const auto result = api::ApiJsonCodec::invoke(fixture.service, method, params,
            api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
                ++calls;
                return std::nullopt;
            });
        REQUIRE(result.contains("result"));
        REQUIRE(result["result"].toObject()["status"] == "no_change");
        REQUIRE(calls == previousCalls + 1);
        before.requireUnchanged(fixture);
        REQUIRE(fixture.model.undoStack()->command(before.index) == redo);
        const api::ApiError rejected{api::ErrorCode::DeadlineExceeded, QStringLiteral("到期拒绝"),
                                     "deadline", api::Recovery::QueryResult, before.document,
                                     -32071};
        const auto failure = api::ApiJsonCodec::invoke(fixture.service, method, params,
            api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
                ++calls;
                return rejected;
            });
        REQUIRE(failure["error"].toObject() == api::ApiJsonCodec::encodeError(rejected));
        REQUIRE(calls == previousCalls + 2);
        before.requireUnchanged(fixture);
    }
    auto stale = requests[1].second;
    stale.insert("expectedDocumentRevision", QString::number(before.document.documentRevision - 1));
    requireWireFailure(api::ApiJsonCodec::invoke(fixture.service, requests[1].first, stale),
                        "REVISION_CONFLICT", "expectedDocumentRevision");
    before.requireUnchanged(fixture);
    fixture.model.redo();
    REQUIRE(fixture.model.scene()->animation().settings.fps == 60);
    REQUIRE(fixture.model.undoStack()->index() == before.index + 1);
}

TEST_CASE("Animation control no changes honor both CAS physical Busy and the original final guard",
          "[animation-api]") {
    for (const auto& method : {QStringLiteral("animation.setPreview"),
                               QStringLiteral("animation.setFrame"),
                               QStringLiteral("animation.play"), QStringLiteral("animation.pause"),
                               QStringLiteral("animation.setLoop")}) {
        DYNAMIC_SECTION(method.toStdString()) {
            AnimationApiFixture fixture;
            const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
            REQUIRE(fixture.model.replaceAnimation(translation(entity)));
            REQUIRE(fixture.model.setAnimationPreview(true));
            if (method == "animation.play")
                REQUIRE(fixture.model.playAnimation());
            auto params = fixture.wireControl();
            if (method == "animation.setPreview")
                params.insert("enabled", true);
            else if (method == "animation.setLoop")
                params.insert("enabled", false);
            else if (method == "animation.setFrame")
                params.insert("frame", fixture.model.animationFrame());
            const AnimationSnapshot before(fixture);
            int calls = 0;
            const auto result = api::ApiJsonCodec::invoke(fixture.service, method, params,
                api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
                    ++calls;
                    return std::nullopt;
                });
            REQUIRE(result.contains("result"));
            REQUIRE(result["result"].toObject()["status"] == "no_change");
            REQUIRE(calls == 1);
            before.requireUnchanged(fixture);
            auto stale = params;
            stale.insert("expectedDocumentRevision",
                         QString::number(before.document.documentRevision + 1));
            requireWireFailure(api::ApiJsonCodec::invoke(fixture.service, method, stale),
                                "REVISION_CONFLICT", "expectedDocumentRevision");
            before.requireUnchanged(fixture);
            stale = params;
            stale.insert("expectedSessionRevision",
                         QString::number(before.controller.sessionRevision + 1));
            requireWireFailure(api::ApiJsonCodec::invoke(fixture.service, method, stale),
                                "REVISION_CONFLICT", "expectedSessionRevision");
            before.requireUnchanged(fixture);
            fixture.service.setExternalBusy("modal", true);
            requireWireFailure(api::ApiJsonCodec::invoke(fixture.service, method, params), "BUSY");
            fixture.service.setExternalBusy("modal", false);
            before.requireUnchanged(fixture);
            const api::ApiError rejected{api::ErrorCode::DeadlineExceeded, QStringLiteral("最终守卫拒绝"),
                                         "deadline", api::Recovery::QueryResult, before.document,
                                         -32071};
            const auto failure = api::ApiJsonCodec::invoke(fixture.service, method, params,
                api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
                    ++calls;
                    return rejected;
                });
            REQUIRE(failure["error"].toObject() == api::ApiJsonCodec::encodeError(rejected));
            REQUIRE(calls == 2);
            before.requireUnchanged(fixture);
        }
    }
}

TEST_CASE("Base EditMode permits pause preview disable and loop while preview enable needs Object",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(entity);
    REQUIRE(fixture.model.setEditMode(true));
    const AnimationSnapshot before(fixture);
    const auto state = fixture.service.animationState(fixture.query<api::AnimationQueryRequest>());
    REQUIRE(state.hasValue());
    REQUIRE(state.value->mode == "base");
    REQUIRE(state.value->evaluationId == 0);
    REQUIRE(api::ApiJsonCodec::encode(*state.value)["evaluationId"] == "0");
    auto preview = fixture.control<api::AnimationSetPreviewRequest>();
    preview.enabled = false;
    const auto disabled = fixture.service.setAnimationPreview(preview);
    REQUIRE(disabled.hasValue());
    REQUIRE(disabled.value->status == api::ResultStatus::NoChange);
    const auto paused =
        fixture.service.pauseAnimation(fixture.control<api::AnimationPauseRequest>());
    REQUIRE(paused.hasValue());
    REQUIRE(paused.value->status == api::ResultStatus::NoChange);
    auto loop = fixture.control<api::AnimationSetLoopRequest>();
    const auto sameLoop = fixture.service.setAnimationLoop(loop);
    REQUIRE(sameLoop.hasValue());
    REQUIRE(sameLoop.value->status == api::ResultStatus::NoChange);
    before.requireUnchanged(fixture);
    loop.enabled = true;
    const auto changed = fixture.service.setAnimationLoop(loop);
    REQUIRE(changed.hasValue());
    REQUIRE(changed.value->status == api::ResultStatus::Committed);
    REQUIRE(changed.value->mode == "base");
    REQUIRE(changed.value->sessionRevision == before.controller.sessionRevision + 1);
    REQUIRE(changed.value->evaluationId == before.controller.evaluationId);
    REQUIRE(fixture.service.documentState().documentRevision == before.document.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == before.document.historyRevision);
    REQUIRE(fixture.model.undoStack()->index() == before.index);
    const AnimationSnapshot afterLoop(fixture);
    preview = fixture.control<api::AnimationSetPreviewRequest>();
    preview.enabled = true;
    requireFailure(fixture.service.setAnimationPreview(preview), api::ErrorCode::Busy);
    requireFailure(fixture.service.playAnimation(fixture.control<api::AnimationPlayRequest>()),
                    api::ErrorCode::PreviewDisabled);
    requireFailure(
        fixture.service.setAnimationFrame(fixture.control<api::AnimationSetFrameRequest>()),
                    api::ErrorCode::PreviewDisabled);
    afterLoop.requireUnchanged(fixture);
    REQUIRE(fixture.model.setEditMode(false));
    fixture.model.beginTransformEdit(entity);
    preview = fixture.control<api::AnimationSetPreviewRequest>();
    preview.enabled = false;
    requireFailure(fixture.service.setAnimationPreview(preview), api::ErrorCode::Busy);
    requireFailure(fixture.service.pauseAnimation(fixture.control<api::AnimationPauseRequest>()),
                    api::ErrorCode::Busy);
    loop = fixture.control<api::AnimationSetLoopRequest>();
    loop.enabled = true;
    requireFailure(fixture.service.setAnimationLoop(loop), api::ErrorCode::Busy);
    fixture.model.cancelTransformEdit();
}

TEST_CASE("Animation controls reject mode illegal actions before any formal mutation",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(fixture.model.replaceAnimation(translation(entity)));
    REQUIRE(fixture.model.setAnimationPreview(true));
    bool draft = false;
    SECTION("Playing") {
        REQUIRE(fixture.model.playAnimation());
    }
    SECTION("Pose Draft") {
        draft = true;
        REQUIRE(fixture.model.beginAnimationDraft(entity, {true, false, false}));
    }
    const AnimationSnapshot before(fixture);
    auto preview = fixture.control<api::AnimationSetPreviewRequest>();
    preview.enabled = true;
    requireFailure(fixture.service.setAnimationPreview(preview), api::ErrorCode::Busy);
    requireFailure(
        fixture.service.setAnimationFrame(fixture.control<api::AnimationSetFrameRequest>()),
                    api::ErrorCode::Busy);
    requireFailure(
        fixture.service.setAnimationLoop(fixture.control<api::AnimationSetLoopRequest>()),
                    api::ErrorCode::Busy);
    if (draft) {
        requireFailure(fixture.service.playAnimation(fixture.control<api::AnimationPlayRequest>()),
                        api::ErrorCode::Busy);
        requireFailure(
            fixture.service.pauseAnimation(fixture.control<api::AnimationPauseRequest>()),
                        api::ErrorCode::Busy);
    }
    auto insertion = fixture.mutation<api::AnimationUpsertKeyframesRequest>();
    insertion.onConflict = api::AnimationConflict::Reject;
    insertion.items = {key(entity, Channel::Scale, 1, {1, 1, 1})};
    requireFailure(fixture.service.upsertAnimationKeyframes(insertion), api::ErrorCode::Busy);
    before.requireUnchanged(fixture);
}

TEST_CASE("Animation final guard rejection is atomic for changed definitions and seek",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    REQUIRE(fixture.model.replaceAnimation(translation(entity)));
    bool seek = false;
    SECTION("Definition transaction") {}
    SECTION("Seek transaction") {
        seek = true;
        REQUIRE(fixture.model.setAnimationPreview(true));
    }
    const AnimationSnapshot before(fixture);
    const api::ApiError rejected{api::ErrorCode::Cancelled, QStringLiteral("候选准备后拒绝"),
                                 "commit", api::Recovery::None, before.document, -32072};
    const auto params = seek
        ? addFields(fixture.wireControl(), {{"frame", 25}})
        : addFields(fixture.wireMutation(),
            {{"items", QJsonArray{wireKey(entity, "position", 5, {4, 0, 0})}},
             {"onConflict", "reject"}});
    const auto method = seek ? "animation.setFrame" : "animation.upsertKeyframes";
    int calls = 0;
    const auto response = api::ApiJsonCodec::invoke(fixture.service, method, params,
        api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
            ++calls;
            before.requireUnchanged(fixture);
            return rejected;
        });
    REQUIRE(response["error"].toObject() == api::ApiJsonCodec::encodeError(rejected));
    REQUIRE(calls == 1);
    before.requireUnchanged(fixture);
    REQUIRE(api::ApiJsonCodec::invoke(fixture.service, method, params).contains("result"));
    REQUIRE(calls == 1);
}

TEST_CASE("Animation guard rechecks callback CAS Busy and deadline without a second external call",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    REQUIRE(fixture.model.replaceAnimation(translation(entity)));
    REQUIRE(fixture.model.setAnimationPreview(true));
    const AnimationSnapshot before(fixture);
    auto params = addFields(fixture.wireControl(), {{"frame", 25}});
    int calls = 0;
    SECTION("Callback changes only the session CAS") {
        const auto response = api::ApiJsonCodec::invoke(
            fixture.service, "animation.setFrame", params,
            api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
                ++calls;
                fixture.model.exchangeBeforeCommitGuard({});
                REQUIRE(fixture.model.setAnimationLoop(true));
                return std::nullopt;
            });
        requireWireFailure(response, "REVISION_CONFLICT", "expectedSessionRevision");
        REQUIRE(fixture.model.animationSessionRevision() == before.controller.sessionRevision + 1);
        REQUIRE(fixture.model.isAnimationLoopEnabled());
        REQUIRE(fixture.service.documentState().documentRevision ==
                before.document.documentRevision);
        REQUIRE(fixture.service.documentState().historyRevision == before.document.historyRevision);
        REQUIRE(fixture.model.installedAnimationPose() == before.pose);
    }
    SECTION("Callback changes the content CAS") {
        const auto response = api::ApiJsonCodec::invoke(
            fixture.service, "animation.setFrame", params,
            api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
                ++calls;
                fixture.model.exchangeBeforeCommitGuard({});
                REQUIRE(fixture.model.renameEntity(entity, QStringLiteral("守卫内正式改名")));
                return std::nullopt;
            });
        requireWireFailure(response, "REVISION_CONFLICT", "expectedDocumentRevision");
        REQUIRE(fixture.model.scene()->find(entity)->name == "守卫内正式改名");
        REQUIRE(fixture.service.documentState().documentRevision ==
                before.document.documentRevision + 1);
        REQUIRE(fixture.service.documentState().historyRevision ==
                before.document.historyRevision + 1);
    }
    SECTION("Callback raises physical Busy") {
        const auto response = api::ApiJsonCodec::invoke(
            fixture.service, "animation.setFrame", params,
            api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
                ++calls;
                fixture.service.setExternalBusy("navigation", true);
                return std::nullopt;
            });
        requireWireFailure(response, "BUSY");
        before.requireUnchanged(fixture);
        fixture.service.setExternalBusy("navigation", false);
    }
    SECTION("Callback exhausts the deadline of an exact no change") {
        params.insert("frame", before.controller.frame);
        params.insert("timeoutMs", 50);
        const auto response = api::ApiJsonCodec::invoke(
            fixture.service, "animation.setFrame", params,
            api::FileAccess::External, [&]() -> std::optional<api::ApiError> {
                ++calls;
                QTest::qSleep(70);
                return std::nullopt;
            });
        requireWireFailure(response, "DEADLINE_EXCEEDED");
        before.requireUnchanged(fixture);
    }
    REQUIRE(calls == 1);
    REQUIRE(fixture.model.animationFrame() == before.controller.frame);
    REQUIRE(fixture.model.animationMode() == Mode::PreviewPaused);
}

TEST_CASE("Animation entity camera preview permits play and ticks retain the session CAS for pause",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto camera = fixture.model.createCamera();
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(camera);
    REQUIRE(fixture.model.replaceAnimation(translation(entity)));
    const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
    REQUIRE(fixture.model.setPreviewCamera(camera));
    auto preview = fixture.control<api::AnimationSetPreviewRequest>();
    preview.enabled = true;
    REQUIRE(fixture.service.setAnimationPreview(preview).hasValue());
    REQUIRE(fixture.service.playAnimation(fixture.control<api::AnimationPlayRequest>()).hasValue());
    const AnimationSnapshot playing(fixture);
    const auto pause = fixture.control<api::AnimationPauseRequest>();
    QTest::qWait(55);
    REQUIRE(fixture.model.animationMode() == Mode::Playing);
    REQUIRE(fixture.model.animationEvaluationId() > playing.controller.evaluationId);
    REQUIRE(fixture.model.animationFrame() > playing.controller.frame);
    REQUIRE(fixture.model.animationSessionRevision() == pause.expectedSessionRevision);
    REQUIRE(fixture.model.previewCamera() == camera);
    const auto stopped = fixture.service.pauseAnimation(pause);
    REQUIRE(stopped.hasValue());
    REQUIRE(stopped.value->status == api::ResultStatus::Committed);
    REQUIRE(stopped.value->mode == "preview_paused");
    REQUIRE_FALSE(stopped.value->diagnostic);
    REQUIRE(stopped.value->sessionRevision == playing.controller.sessionRevision + 1);
    REQUIRE(stopped.value->evaluationId == fixture.model.animationEvaluationId());
    REQUIRE(fixture.model.previewCamera() == camera);
    REQUIRE(fixture.service.documentState().documentRevision == playing.document.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == playing.document.historyRevision);
    REQUIRE(fixture.model.undoStack()->index() == playing.index);
    REQUIRE(fixture.model.undoStack()->isClean());
    REQUIRE_FALSE(fixture.model.isModified());
    const AnimationSnapshot paused(fixture);
    QTest::qWait(30);
    paused.requireUnchanged(fixture);
}

TEST_CASE("Animation pause reports admitted stop diagnostics and admission rejection keeps Playing",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Cube);
    REQUIRE(fixture.model.replaceAnimation(translation(entity)));
    REQUIRE(fixture.model.setAnimationPreview(true));
    REQUIRE(fixture.model.playAnimation());
    renderer_gl::EditorCamera view;
    REQUIRE(view.setState(fixture.model.editorCamera()));
    const AnimationSnapshot before(fixture);
    int calls = 0;
    SECTION("Provider failure after admission and stop is a successful stop with diagnostic") {
        bool sawStoppedMode = false;
        fixture.model.setAnimationViewProvider([&] {
            if (fixture.model.animationMode() != Mode::Playing) {
                sawStoppedMode = true;
                throw std::bad_alloc{};
            }
            return view;
        });
        const auto response = api::ApiJsonCodec::invoke(fixture.service, "animation.pause",
            fixture.wireControl(), api::FileAccess::External,
            [&]() -> std::optional<api::ApiError> { ++calls; return std::nullopt; });
        REQUIRE(response.contains("result"));
        REQUIRE_FALSE(response.contains("error"));
        const auto result = response["result"].toObject();
        REQUIRE(result["status"] == "committed");
        REQUIRE(result["mode"] == "base");
        REQUIRE(result["diagnostic"].toObject()["code"] == "LIMIT_EXCEEDED");
        REQUIRE_FALSE(result["diagnostic"].toObject().contains("data"));
        REQUIRE(sawStoppedMode);
        REQUIRE(fixture.model.animationMode() == Mode::Base);
        REQUIRE_FALSE(fixture.model.installedAnimationPose());
        REQUIRE(fixture.model.animationSessionRevision() > before.controller.sessionRevision);
        const AnimationSnapshot stopped(fixture);
        QTest::qWait(30);
        stopped.requireUnchanged(fixture);
    }
    SECTION("Provider failure cannot bypass a denied final guard or stop the clock") {
        fixture.model.setAnimationViewProvider([]() -> renderer_gl::EditorCamera {
            throw std::bad_alloc{};
        });
        const api::ApiError rejected{api::ErrorCode::Cancelled, QStringLiteral("暂停未获准"),
                                     "commit", api::Recovery::None, before.document, -32072};
        const auto response = api::ApiJsonCodec::invoke(fixture.service, "animation.pause",
            fixture.wireControl(), api::FileAccess::External,
            [&]() -> std::optional<api::ApiError> { ++calls; return rejected; });
        REQUIRE(response["error"].toObject() == api::ApiJsonCodec::encodeError(rejected));
        before.requireUnchanged(fixture);
        fixture.model.setAnimationViewProvider({});
        QTest::qWait(35);
        REQUIRE(fixture.model.animationMode() == Mode::Playing);
        REQUIRE(fixture.model.animationFrame() > before.controller.frame);
        REQUIRE(fixture.model.animationSessionRevision() == before.controller.sessionRevision);
    }
    REQUIRE(calls == 1);
    REQUIRE(fixture.service.documentState().documentRevision == before.document.documentRevision);
    REQUIRE(fixture.service.documentState().historyRevision == before.document.historyRevision);
    REQUIRE(fixture.model.undoStack()->index() == before.index);
}

TEST_CASE("Animation preparation provider reentry identifies the changed session or content CAS",
          "[animation-api]") {
    for (const auto& method : {QStringLiteral("animation.setFrame"),
                               QStringLiteral("animation.setPreview"),
                               QStringLiteral("animation.play")}) {
        DYNAMIC_SECTION(method.toStdString()) {
            AnimationApiFixture fixture;
            const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
            REQUIRE(fixture.model.replaceAnimation(translation(entity)));
            REQUIRE(fixture.model.setAnimationPreview(true));
            renderer_gl::EditorCamera view;
            REQUIRE(view.setState(fixture.model.editorCamera()));
            const AnimationSnapshot before(fixture);
            bool rename = false;
            SECTION("Nested seek changes session CAS") {}
            SECTION("Nested rename changes content CAS") {
                rename = true;
            }
            int calls = 0;
            bool triggered = false;
            fixture.model.setAnimationViewProvider([&] {
                if (++calls == 3) {
                    triggered = true;
                    if (rename)
                        REQUIRE(fixture.model.renameEntity(entity, QStringLiteral("准备回调改名")));
                    else
                        REQUIRE(fixture.model.setAnimationFrame(25));
                }
                return view;
            });
            auto params = fixture.wireControl();
            if (method == "animation.setFrame")
                params.insert("frame", 13);
            else if (method == "animation.setPreview")
                params.insert("enabled", false);
            const auto response = api::ApiJsonCodec::invoke(fixture.service, method, params);
            REQUIRE(triggered);
            requireWireFailure(response, "REVISION_CONFLICT",
                                rename ? "expectedDocumentRevision" : "expectedSessionRevision");
            REQUIRE(fixture.model.animationMode() == Mode::PreviewPaused);
            REQUIRE(fixture.model.animationFrame() == (rename ? before.controller.frame : 25));
            REQUIRE(fixture.model.scene()->animation() == before.animation);
            REQUIRE(fixture.model.animationSessionRevision() ==
                    before.controller.sessionRevision + (rename ? 0 : 1));
            REQUIRE(fixture.service.documentState().documentRevision ==
                    before.document.documentRevision + (rename ? 1 : 0));
            REQUIRE(fixture.service.documentState().historyRevision ==
                    before.document.historyRevision + (rename ? 1 : 0));
            if (rename)
                REQUIRE(fixture.model.scene()->find(entity)->name == "准备回调改名");
        }
    }
}

TEST_CASE("Animation query Busy provider runs once and newly raised Busy blocks every query",
          "[animation-api]") {
    for (const auto& method : {QStringLiteral("animation.getState"),
                               QStringLiteral("animation.listTracks"),
                               QStringLiteral("animation.readKeyframes"),
                               QStringLiteral("animation.sample")}) {
        DYNAMIC_SECTION(method.toStdString()) {
            AnimationApiFixture fixture;
            const auto entity = fixture.model.createEntity(core::PrimitiveKind::Cube);
            REQUIRE(fixture.model.replaceAnimation(translation(entity)));
            const auto before = fixture.service.documentState();
            const auto index = fixture.model.undoStack()->index();
            const auto base = fixture.model.scene()->find(entity)->transform;
            int calls = 0;
            fixture.service.setBusyProvider([&] {
                ++calls;
                if (calls == 1) {
                    if (method == "animation.sample") {
                        fixture.model.beginTransformEdit(entity);
                        auto preview = base;
                        preview.position.x = 321;
                        fixture.model.previewTransform(preview);
                    }
                    fixture.model.setExternalBusy("test", true);
                }
                return QStringList{};
            });
            auto params = fixture.wireQuery();
            if (method == "animation.readKeyframes") {
                params.insert("entityId", QString::number(entity));
                params.insert("channel", "position");
            } else if (method == "animation.sample") {
                params.insert("frame", 12.5);
                params.insert("entityIds", QJsonArray{QString::number(entity)});
            }
            const auto response = api::ApiJsonCodec::invoke(fixture.service, method, params);
            requireWireFailure(response, "BUSY");
            REQUIRE(calls == 1);
            REQUIRE(fixture.service.documentState().document == before.document);
            REQUIRE(fixture.service.documentState().documentRevision == before.documentRevision);
            REQUIRE(fixture.service.documentState().historyRevision == before.historyRevision);
            REQUIRE(fixture.model.undoStack()->index() == index);
            fixture.service.setBusyProvider({});
            fixture.model.setExternalBusy("test", false);
            fixture.model.cancelTransformEdit();
            if (method == "animation.sample") {
                REQUIRE(fixture.model.scene()->find(entity)->transform.position == base.position);
                const auto formal = api::ApiJsonCodec::invoke(fixture.service, method, params);
                REQUIRE(formal.contains("result"));
                REQUIRE(formal["result"].toObject()["entities"].toArray()[0].toObject()
                            ["localTransform"].toObject()["translation"].toArray()[0] == 11.5);
            }
        }
    }
}

TEST_CASE("Animation definition preparation reentry reports Busy or the changed content CAS",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    REQUIRE(fixture.model.replaceAnimation(translation(entity)));
    renderer_gl::EditorCamera view;
    REQUIRE(view.setState(fixture.model.editorCamera()));
    const AnimationSnapshot before(fixture);
    bool rename = false;
    SECTION("Provider raises physical Busy midway through definition preflight") {}
    SECTION("Provider renames the target midway through definition preflight") {
        rename = true;
    }
    int calls = 0;
    bool triggered = false;
    fixture.model.setAnimationViewProvider([&] {
        if (++calls == 4) {
            triggered = true;
            if (rename)
                REQUIRE(fixture.model.renameEntity(entity, QStringLiteral("定义准备回调改名")));
            else
                fixture.model.setExternalBusy("mid_definition", true);
        }
        return view;
    });
    auto request = fixture.mutation<api::AnimationSetSettingsRequest>();
    request.settings = before.animation.settings;
    request.settings.fps = 60;
    const auto result = fixture.service.setAnimationSettings(request);
    REQUIRE(triggered);
    requireFailure(result, rename ? api::ErrorCode::RevisionConflict : api::ErrorCode::Busy);
    REQUIRE(result.error->recovery == (rename ? api::Recovery::Refetch : api::Recovery::Wait));
    REQUIRE(fixture.model.scene()->animation() == before.animation);
    REQUIRE(fixture.model.animationMode() == Mode::Base);
    REQUIRE(fixture.model.animationSessionRevision() == before.controller.sessionRevision);
    if (rename) {
        REQUIRE(result.error->fieldPath == "expectedDocumentRevision");
        REQUIRE(fixture.model.scene()->find(entity)->name == "定义准备回调改名");
        REQUIRE(fixture.service.documentState().documentRevision ==
                before.document.documentRevision + 1);
        REQUIRE(fixture.service.documentState().historyRevision ==
                before.document.historyRevision + 1);
        REQUIRE(fixture.model.undoStack()->index() == before.index + 1);
    } else {
        before.requireUnchanged(fixture);
        fixture.model.setExternalBusy("mid_definition", false);
    }
}

TEST_CASE("Animation wire cursors limits and subframes are explicit and strict",
          "[animation-api]") {
    AnimationApiFixture fixture;
    const auto entity = fixture.model.createEntity(core::PrimitiveKind::Empty);
    const QJsonObject cursor{{"entityId", QString::number(entity)}, {"channel", "position"}};
    const auto list = addFields(fixture.wireMutation(), {{"afterTrack", cursor}, {"limit", 1}});
    REQUIRE(api::ApiJsonCodec::decodeRequest("animation.listTracks", list).hasValue());
    const auto read = addFields(fixture.wireMutation(),
        {{"entityId", QString::number(entity)}, {"channel", "position"}, {"afterFrame", 1}});
    REQUIRE(api::ApiJsonCodec::decodeRequest("animation.readKeyframes", read).hasValue());
    for (const auto& [method, params] :
         std::array<std::pair<QString, QJsonObject>, 2>{{{"animation.listTracks", list},
                                                        {"animation.readKeyframes", read}}}) {
        auto missingCas = params;
        missingCas.remove("expectedDocumentRevision");
        const auto failure = api::ApiJsonCodec::decodeRequest(method, missingCas);
        requireFailure(failure, api::ErrorCode::InvalidArgument);
        REQUIRE(failure.error->fieldPath == "expectedDocumentRevision");
        for (const int limit : {0, 2049}) {
            auto invalid = params;
            invalid.insert("limit", limit);
            REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest(method, invalid).hasValue());
        }
    }
    for (const auto& replacement :
         std::vector<QJsonObject>{{{"entityId", 1}}, {{"entityId", "01"}}, {{"channel", "other"}},
                                   {{"extra", true}}}) {
        auto invalid = list;
        invalid.insert("afterTrack", addFields(cursor, replacement));
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.listTracks", invalid).hasValue());
    }
    auto mismatch = list;
    mismatch.insert("entityId", QString::number(entity + 1));
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.listTracks", mismatch).hasValue());
    auto fractionalKey = read;
    fractionalKey.insert("afterFrame", 1.25);
    REQUIRE_FALSE(
        api::ApiJsonCodec::decodeRequest("animation.readKeyframes", fractionalKey).hasValue());
    auto sample = addFields(fixture.wireQuery(),
        {{"frame", 1.25}, {"entityIds", QJsonArray{QString::number(entity)}}});
    REQUIRE(api::ApiJsonCodec::decodeRequest("animation.sample", sample).hasValue());
    for (const double frame : {0.0, 100000.25, std::numeric_limits<double>::infinity()}) {
        sample.insert("frame", frame);
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.sample", sample).hasValue());
    }
    sample.insert("frame", 100000);
    REQUIRE(api::ApiJsonCodec::decodeRequest("animation.sample", sample).hasValue());
    sample.insert("entityIds", QJsonArray{QString::number(entity), QString::number(entity)});
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.sample", sample).hasValue());
}
