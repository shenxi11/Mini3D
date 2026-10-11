/*
 * 模块名: J1ApiTests
 * 功能概述: 验证 J1 作者 API 的真实业务接线、规范 wire、身份和提交原子性。
 * 对外接口: Catch2 [j1-api]；旧十四方法仍由 AnimationApiTests 单独回归。
 * 依赖关系: SceneViewModel、EditorApiService、ApiJsonCodec、V5 SceneSerializer、Qt Test。
 * 输入输出: 显式请求到完整作者值、单笔历史、精确分页和无状态取样断言。
 * 异常与错误: 初始/最终 CAS、Busy、取消、撤权和真实到期拒绝不能发布候选。
 * 维护说明: 不创建 GL 或窗口；Core 快照入口仅构造真实大身份夹具。
 */
#include "core/SceneSerializer.h"
#include "editor/SceneViewModel.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"

#include <QJsonArray>
#include <QTest>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>
#include <vector>

using namespace mini3d;
namespace {
namespace api = editor::api;

QJsonObject withFields(QJsonObject value, const QJsonObject& fields) {
    for (auto field = fields.begin(); field != fields.end(); ++field)
        value.insert(field.key(), field.value());
    return value;
}
QJsonObject parameters(double roughness = .2) {
    return {{"baseColor", QJsonArray{.12345678901234567, .4, .8}},
            {"roughness", roughness}, {"metallic", .3456789012345678},
            {"coverage", .4567890123456789}};
}
QJsonObject transform() {
    return {{"space", "local"}, {"translation", QJsonArray{0, 0, 0}},
            {"rotationQuaternion", QJsonArray{0, 0, 0, 1}}, {"scale", QJsonArray{1, 1, 1}}};
}
QJsonObject rationalSettings(qint64 numerator = 30000, qint64 denominator = 1001) {
    return {{"rationalFps", QJsonObject{{"numerator", numerator}, {"denominator", denominator}}},
            {"startFrame", 1}, {"endFrame", 24}, {"loopPolicy", "inclusiveFrameStep"}};
}
QString errorCode(const QJsonObject& response) {
    REQUIRE(response.contains("error"));
    REQUIRE_FALSE(response.contains("result"));
    return response["error"].toObject()["data"].toObject()["code"].toString();
}
struct Fixture {
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    Fixture() { model.newScene(); }
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
    QJsonObject wire(bool mutation = false) const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        QJsonObject result{{"document", state["document"]}};
        if (mutation) result.insert("expectedDocumentRevision", state["documentRevision"]);
        return result;
    }
    QJsonObject invoke(const QString& method, const QJsonObject& fields, bool mutation = true,
                       api::BeforeCommitGuard guard = {}) {
        return api::ApiJsonCodec::invoke(service, method, withFields(wire(mutation), fields),
                                         api::FileAccess::External, std::move(guard));
    }
    QJsonObject call(const QString& method, const QJsonObject& fields, bool mutation = true) {
        const auto response = invoke(method, fields, mutation);
        INFO(response["error"].toObject()["message"].toString().toStdString());
        REQUIRE(response.contains("result"));
        REQUIRE_FALSE(response.contains("error"));
        return response["result"].toObject();
    }
    core::EntityId cube() {
        auto request = mutation<api::EntityCreateRequest>();
        request.name = "API cube";
        request.primitive = core::PrimitiveKind::Cube;
        const auto result = service.createEntity(request);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    QJsonObject address(const QString& kind, const QString& id, const QString& property,
                         const QString& component) const {
        const QJsonObject owner = kind == "material"
            ? QJsonObject{{"kind", kind}, {"materialId", id}}
            : QJsonObject{{"kind", kind}, {"entityId", id}};
        return {{"documentUuid", QString::fromStdString(core::documentUuidString(model.scene()->documentUuid()))},
                {"owner", owner}, {"property", property}, {"component", component}};
    }
};
struct Snapshot {
    api::DocumentState document;
    std::string encoded;
    int count, index, clean;
    bool modified;
    const QUndoCommand* redo;
    core::EntityId selection;
    core::Cursor3D cursor;
    api::AnimationControllerState controller;
    std::shared_ptr<const renderer_gl::InstalledPose> pose;
    explicit Snapshot(Fixture& fixture)
        : document(fixture.service.documentState()),
          encoded(core::SceneSerializer::encode(core::SceneDocumentData(*fixture.model.scene()))),
          count(fixture.model.undoStack()->count()), index(fixture.model.undoStack()->index()),
          clean(fixture.model.undoStack()->cleanIndex()), modified(fixture.model.isModified()),
          redo(fixture.model.undoStack()->canRedo() ? fixture.model.undoStack()->command(index) : nullptr),
          selection(fixture.model.selection()->selectedEntity()), cursor(fixture.model.cursor3D()),
          controller(fixture.model.animationControllerState()), pose(fixture.model.installedAnimationPose()) {}
    void requireUnchanged(Fixture& fixture) const {
        REQUIRE(fixture.service.documentState().document == document.document);
        REQUIRE(fixture.service.documentState().documentRevision == document.documentRevision);
        REQUIRE(fixture.service.documentState().historyRevision == document.historyRevision);
        REQUIRE(core::SceneSerializer::encode(core::SceneDocumentData(*fixture.model.scene())) == encoded);
        REQUIRE(fixture.model.undoStack()->count() == count);
        REQUIRE(fixture.model.undoStack()->index() == index);
        REQUIRE(fixture.model.undoStack()->cleanIndex() == clean);
        REQUIRE(fixture.model.isModified() == modified);
        REQUIRE((fixture.model.undoStack()->canRedo() ? fixture.model.undoStack()->command(index) : nullptr) == redo);
        REQUIRE(fixture.model.selection()->selectedEntity() == selection);
        REQUIRE(fixture.model.cursor3D() == cursor);
        REQUIRE(fixture.model.animationControllerState().mode == controller.mode);
        REQUIRE(fixture.model.animationFrame() == controller.frame);
        REQUIRE(fixture.model.isAnimationLoopEnabled() == controller.loop);
        REQUIRE(fixture.model.animationSessionRevision() == controller.sessionRevision);
        REQUIRE(fixture.model.animationEvaluationId() == controller.evaluationId);
        REQUIRE(fixture.model.installedAnimationPose() == pose);
    }
};
struct WireCase {
    QString method;
    bool mutation;
    QJsonObject fields;
};
std::vector<WireCase> wireCases(const QJsonObject& address) {
    return {
        {"material.create", true, {{"parameters", parameters()}}},
        {"material.get", false, {{"materialId", "1"}}},
        {"material.update", true, {{"materialId", "1"}, {"parameters", parameters(.6)}}},
        {"entity.assignMaterial", true, {{"entityId", "1"}, {"materialId", "1"}}},
        {"light.createSun", true, {{"name", "J1 Sun"}, {"parentId", "0"}, {"transform", transform()},
                                   {"sun", QJsonObject{{"irradiance", 1.5}, {"color", QJsonArray{1, .7, .4}}}}}},
        {"light.updateSun", true, {{"entityId", "2"},
                                   {"sun", QJsonObject{{"irradiance", 3}, {"color", QJsonArray{1, 1, 1}}}}}},
        {"animation.upsertPropertyKeyframe", true, {{"address", address}, {"time", 12.5},
                                                    {"value", 1.2345678901234567}, {"outgoing", "linear"}}},
        {"animation.deletePropertyKeyframe", true, {{"address", address}, {"time", 1}}},
        {"animation.readPropertyKeyframes", false, {{"address", address}}},
        {"animation.sampleProperties", false, {{"frame", 12.5}, {"addresses", QJsonArray{address}}}},
        {"animation.setRationalSettings", true, {{"settings", rationalSettings()}}}
    };
}
struct Work : Fixture {
    core::EntityId entity = cube();
    QString material = call("material.create", {{"parameters", parameters()}})["materialId"].toString();
    QString sun = call("light.createSun", {{"name", "J1 Sun"}, {"parentId", "0"}, {"transform", transform()},
        {"sun", QJsonObject{{"irradiance", 1}, {"color", QJsonArray{1, 1, 1}}}}})["created"].toObject()["entityIds"].toArray()[0].toString();
    QJsonObject position = address("entity", QString::number(entity), "transform.position", "x");
    Work() {
        call("entity.assignMaterial", {{"entityId", QString::number(entity)}, {"materialId", material}});
        call("animation.upsertPropertyKeyframe", {{"address", position}, {"time", 1}, {"value", 0}, {"outgoing", "linear"}});
    }
    std::vector<WireCase> mutations() const {
        auto cases = wireCases(position);
        cases[2].fields["materialId"] = material;
        cases[3].fields["entityId"] = QString::number(entity);
        cases[3].fields["materialId"] = material;
        cases[5].fields["entityId"] = sun;
        cases.erase(std::remove_if(cases.begin(), cases.end(), [](const auto& item) { return !item.mutation; }), cases.end());
        return cases;
    }
};
std::optional<api::ApiError> directFailure(api::EditorApiService& service, const api::ApiRequest& value) {
    const auto failure = [](const auto& result) {
        REQUIRE_FALSE(result.hasValue());
        REQUIRE(result.error);
        return result.error;
    };
    return std::visit([&](const auto& request) -> std::optional<api::ApiError> {
        using Request = std::decay_t<decltype(request)>;
        if constexpr (std::is_same_v<Request, api::MaterialCreateRequest>) return failure(service.createMaterial(request));
        else if constexpr (std::is_same_v<Request, api::MaterialUpdateRequest>) return failure(service.updateMaterial(request));
        else if constexpr (std::is_same_v<Request, api::EntityAssignMaterialRequest>) return failure(service.assignMaterial(request));
        else if constexpr (std::is_same_v<Request, api::SunCreateRequest>) return failure(service.createSun(request));
        else if constexpr (std::is_same_v<Request, api::SunUpdateRequest>) return failure(service.updateSun(request));
        else if constexpr (std::is_same_v<Request, api::AnimationUpsertPropertyKeyframeRequest>) return failure(service.upsertAnimationPropertyKeyframe(request));
        else if constexpr (std::is_same_v<Request, api::AnimationDeletePropertyKeyframeRequest>) return failure(service.deleteAnimationPropertyKeyframe(request));
        else if constexpr (std::is_same_v<Request, api::AnimationSetRationalSettingsRequest>) return failure(service.setRationalAnimationSettings(request));
        else { FAIL("Unexpected direct J1 fixture request"); return std::nullopt; }
    }, value);
}
} // namespace

TEST_CASE("J1 eleven requests are strict canonical and preserve business CAS", "[j1-api][codec]") {
    Fixture fixture;
    const auto cases = wireCases(fixture.address("entity", "1", "transform.position", "x"));
    REQUIRE(cases.size() == 11);
    for (const auto& test : cases) {
        CAPTURE(test.method);
        const auto params = withFields(fixture.wire(test.mutation), test.fields);
        REQUIRE(api::ApiJsonCodec::decodeRequest(test.method, params).hasValue());
        const auto canonical = api::ApiJsonCodec::canonicalParams(test.method, params);
        REQUIRE(canonical.hasValue());
        REQUIRE(api::ApiJsonCodec::decodeRequest(test.method, *canonical.value).hasValue());
        for (auto field = params.begin(); field != params.end(); ++field) {
            auto missing = params;
            missing.remove(field.key());
            REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest(test.method, missing).hasValue());
        }
        auto unknown = params;
        unknown.insert("unregistered", true);
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest(test.method, unknown).hasValue());
        if (test.mutation) {
            auto envelope = params;
            envelope.insert("clientSessionId", fixture.service.documentState().document.instanceId);
            envelope.insert("mutationSequence", "18446744073709551615");
            envelope.insert("timeoutMs", int(api::limits::mutationTimeoutMs));
            const auto replay = api::ApiJsonCodec::canonicalParams(test.method, envelope);
            REQUIRE(replay.hasValue());
            REQUIRE(*replay.value == *canonical.value);
            REQUIRE((*canonical.value)["expectedDocumentRevision"] == params["expectedDocumentRevision"]);
            REQUIRE_FALSE(canonical.value->contains("clientSessionId"));
            REQUIRE_FALSE(canonical.value->contains("mutationSequence"));
        }
    }
    const auto describe = fixture.service.describe();
    REQUIRE(describe.hasValue());
    REQUIRE(describe.value->apiVersion == "0.3.0");
    REQUIRE(describe.value->wireVersion == 1);
    REQUIRE(describe.value->methods.size() == 68);
    int animationMethods = 0;
    for (const auto& method : describe.value->methods)
        if (method.name.startsWith("animation.")) ++animationMethods;
    REQUIRE(animationMethods == 19);
}

TEST_CASE("J1 wire rejects malformed author addresses IDs values and incomplete parameters", "[j1-api][codec]") {
    Fixture fixture;
    const auto address = fixture.address("entity", "1", "transform.position", "x");
    const auto base = withFields(fixture.wire(true), {{"address", address}, {"time", 12.5}, {"value", 1}, {"outgoing", "linear"}});
    for (const auto& field : {QStringLiteral("documentUuid"), QStringLiteral("owner"), QStringLiteral("property"), QStringLiteral("component")}) {
        auto modified = address;
        modified.remove(field);
        auto params = base;
        params["address"] = modified;
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", params).hasValue());
    }
    for (const auto& uuid : {QStringLiteral("00000000-0000-0000-0000-000000000000"),
                             QStringLiteral("12345678-1234-1234-1234-123456789ABC"), QStringLiteral("not-a-uuid")}) {
        auto params = base;
        params["address"] = withFields(address, {{"documentUuid", uuid}});
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", params).hasValue());
    }
    for (const QJsonValue id : {QJsonValue("0"), QJsonValue("01"), QJsonValue("+1"), QJsonValue("18446744073709551616"), QJsonValue(1)}) {
        auto params = base;
        params["address"] = withFields(address, {{"owner", QJsonObject{{"kind", "entity"}, {"entityId", id}}}});
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", params).hasValue());
    }
    auto params = base;
    params["address"] = withFields(address, {{"owner", QJsonObject{{"kind", "entity"}, {"entityId", "1"}, {"materialId", "1"}}}});
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", params).hasValue());
    params["address"] = withFields(address, {{"property", "arbitrary.reflection.path"}});
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", params).hasValue());
    params["address"] = withFields(address, {{"component", "scalar"}});
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", params).hasValue());
    for (const double time : {0., 100000.25, std::numeric_limits<double>::infinity()}) {
        params = base;
        params["time"] = time;
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", params).hasValue());
    }
    params = base;
    params["value"] = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", params).hasValue());
    params = base;
    params["outgoing"] = "unknown";
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", params).hasValue());
    auto pbr = parameters();
    pbr.remove("coverage");
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("material.create", withFields(fixture.wire(true), {{"parameters", pbr}})).hasValue());
    for (const double value : {-0.1, 1.1, std::numeric_limits<double>::infinity()}) {
        pbr = parameters(value);
        REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("material.create", withFields(fixture.wire(true), {{"parameters", pbr}})).hasValue());
    }
    const double precise = .12345678901234567;
    const auto decoded = api::ApiJsonCodec::decodeRequest("material.create", withFields(fixture.wire(true), {{"parameters", parameters(precise)}}));
    REQUIRE(decoded.hasValue());
    REQUIRE(std::bit_cast<std::uint64_t>(std::get<api::MaterialCreateRequest>(*decoded.value).parameters.roughness) == std::bit_cast<std::uint64_t>(precise));
}

TEST_CASE("J1 all eleven invoke paths create real authors and sample the same three properties", "[j1-api][author]") {
    Fixture fixture;
    const auto entity = fixture.cube();
    const auto created = fixture.call("material.create", {{"parameters", parameters()}});
    const auto material = created["materialId"].toString();
    REQUIRE_FALSE(material.isEmpty());
    REQUIRE(created["command"].toObject()["status"] == "committed");
    const auto readMaterial = fixture.call("material.get", {{"materialId", material}}, false);
    REQUIRE_FALSE(readMaterial.contains("state"));
    REQUIRE(readMaterial["material"].toObject()["id"] == material);
    REQUIRE(readMaterial["material"].toObject()["definitionVersion"] == "1");
    REQUIRE(readMaterial["material"].toObject()["parameters"] == parameters());
    REQUIRE(readMaterial["material"].toObject()["textures"].toArray().isEmpty());
    const auto updated = fixture.call("material.update", {{"materialId", material}, {"parameters", parameters(.4)}});
    REQUIRE(updated["materialId"] == material);
    REQUIRE(fixture.call("material.get", {{"materialId", material}}, false)["material"].toObject()["definitionVersion"] == "1");
    const auto assigned = fixture.call("entity.assignMaterial", {{"entityId", QString::number(entity)}, {"materialId", material}});
    const auto slot = assigned["materialSlotId"].toString();
    REQUIRE_FALSE(slot.isEmpty());
    const auto sunCreated = fixture.call("light.createSun", {{"name", "Sun"}, {"parentId", "0"}, {"transform", transform()},
        {"sun", QJsonObject{{"irradiance", 1}, {"color", QJsonArray{1, 1, 1}}}}});
    const auto sun = sunCreated["created"].toObject()["entityIds"].toArray()[0].toString();
    fixture.call("light.updateSun", {{"entityId", sun}, {"sun", QJsonObject{{"irradiance", 1.25}, {"color", QJsonArray{1, .75, .5}}}}});
    const auto entityState = fixture.call("entity.get", {{"entityId", QString::number(entity)}}, false)["entity"].toObject();
    REQUIRE((entityState["materialSlots"].toArray() == QJsonArray{QJsonObject{{"id", slot}, {"material", material}}}));
    REQUIRE(entityState["sun"].isNull());
    const auto sunState = fixture.call("entity.get", {{"entityId", sun}}, false)["entity"].toObject();
    REQUIRE(sunState["sun"].toObject()["irradiance"].toDouble() == 1.25);
    const auto current = api::ApiJsonCodec::invoke(fixture.service, "document.current", QJsonObject{})["result"].toObject();
    REQUIRE(current["documentUuid"] == QString::fromStdString(core::documentUuidString(fixture.model.scene()->documentUuid())));
    fixture.call("animation.setRationalSettings", {{"settings", rationalSettings()}});
    const auto state = fixture.call("animation.getState", {}, false);
    REQUIRE(state["rationalFps"] == rationalSettings()["rationalFps"]);
    REQUIRE(state["loopPolicy"] == "inclusiveFrameStep");
    REQUIRE(state["settings"].toObject()["fps"].toInt() == int(fixture.model.scene()->animation().settings.fps));
    const std::array<QJsonObject, 3> addresses{
        fixture.address("entity", QString::number(entity), "transform.position", "x"),
        fixture.address("material", material, "material.pbr.roughness", "scalar"),
        fixture.address("entity", sun, "light.sun.irradiance", "scalar")};
    const std::array<double, 3> left{0, .2, 1}, right{2, .8, 2};
    QJsonObject rightIdentity;
    for (std::size_t index = 0; index < addresses.size(); ++index) {
        const auto before = fixture.model.undoStack()->index();
        fixture.call("animation.upsertPropertyKeyframe", {{"address", addresses[index]}, {"time", 1}, {"value", left[index]}, {"outgoing", "linear"}});
        const auto key = fixture.call("animation.upsertPropertyKeyframe", {{"address", addresses[index]}, {"time", 24}, {"value", right[index]}, {"outgoing", "linear"}});
        REQUIRE(fixture.model.undoStack()->index() == before + 2);
        if (index == 0) rightIdentity = key["keyIdentity"].toObject();
    }
    const Snapshot beforeSample(fixture);
    for (const double frame : {24., 1., 12.5, 1.}) {
        const auto sample = fixture.call("animation.sampleProperties", {{"frame", frame},
            {"addresses", QJsonArray{addresses[0], addresses[1], addresses[2]}}}, false);
        REQUIRE(sample["frame"].toDouble() == frame);
        REQUIRE_FALSE(sample.contains("source"));
        REQUIRE_FALSE(sample.contains("state"));
        const auto values = sample["samples"].toArray();
        REQUIRE(values.size() == 3);
        const double fraction = (frame - 1) / 23;
        for (int index = 0; index < 3; ++index) {
            REQUIRE(values[index].toObject()["address"] == addresses[std::size_t(index)]);
            REQUIRE(std::abs(values[index].toObject()["value"].toDouble() - std::lerp(left[index], right[index], fraction)) <= 1e-12);
        }
        beforeSample.requireUnchanged(fixture);
    }
    const auto first = fixture.call("animation.readPropertyKeyframes", {{"address", addresses[0]}, {"limit", 1}}, false);
    REQUIRE(first["keys"].toArray().size() == 1);
    REQUIRE(first["nextAfterTime"].toDouble() == 1);
    const auto next = fixture.call("animation.readPropertyKeyframes", {{"address", addresses[0]}, {"afterTime", 1}, {"limit", 1},
        {"expectedDocumentRevision", first["documentRevision"]}}, false);
    REQUIRE(next["keys"].toArray()[0].toObject()["time"].toDouble() == 24);
    REQUIRE(next["keys"].toArray()[0].toObject()["id"] == rightIdentity["keyId"]);
    REQUIRE(next["nextAfterTime"].isNull());
    const auto replacement = fixture.call("animation.upsertPropertyKeyframe", {{"address", addresses[0]}, {"time", 24}, {"value", 2.25}, {"outgoing", "constant"}});
    REQUIRE(replacement["keyIdentity"] == rightIdentity);
    const Snapshot beforeNoChange(fixture);
    REQUIRE(fixture.call("animation.upsertPropertyKeyframe", {{"address", addresses[0]}, {"time", 24}, {"value", 2.25}, {"outgoing", "constant"}})["command"].toObject()["status"] == "no_change");
    beforeNoChange.requireUnchanged(fixture);
    const auto removed = fixture.call("animation.deletePropertyKeyframe", {{"address", addresses[0]}, {"time", 24}});
    REQUIRE(removed["keyIdentity"] == rightIdentity);
    const Snapshot beforeMissing(fixture);
    const auto missing = fixture.call("animation.deletePropertyKeyframe", {{"address", addresses[0]}, {"time", 24}});
    REQUIRE(missing["command"].toObject()["status"] == "no_change");
    REQUIRE(missing["keyIdentity"].isNull());
    beforeMissing.requireUnchanged(fixture);
}

TEST_CASE("J1 real wire authors preserve unrepresentable numeric IDs and adjacent binary64 times", "[j1-api][identity]") {
    constexpr std::uint64_t big = 9007199254740993ULL;
    Fixture fixture;
    auto scene = std::const_pointer_cast<core::Scene>(fixture.model.scene());
    core::SceneDocumentData data(*scene);
    data.idHighWatermarks = {big - 1, big - 1, big - 1, big - 1, big - 1, big - 1};
    data.allocationState.nextEntityId = big;
    std::string diagnostic;
    REQUIRE(scene->replaceDocumentSnapshot(data, diagnostic));
    const auto entity = fixture.cube();
    REQUIRE(entity == big);
    const auto material = fixture.call("material.create", {{"parameters", parameters()}}).value("materialId");
    REQUIRE(material.isString());
    REQUIRE(material == QString::number(big));
    const auto assigned = fixture.call("entity.assignMaterial", {{"entityId", QString::number(entity)}, {"materialId", material}});
    REQUIRE(assigned["materialSlotId"] == QString::number(big));
    const auto address = fixture.address("entity", QString::number(entity), "transform.position", "x");
    const double precise = 1.2345678901234567;
    const auto key = fixture.call("animation.upsertPropertyKeyframe", {{"address", address}, {"time", 12.5},
        {"value", precise}, {"outgoing", "constant"}})["keyIdentity"].toObject();
    for (const auto* field : {"actionId", "actionSlotId", "curveId", "keyId"}) {
        REQUIRE(key[field].isString());
        REQUIRE(key[field] == QString::number(big));
    }
    const double adjacent = std::nextafter(12.5, 13.);
    fixture.call("animation.upsertPropertyKeyframe", {{"address", address}, {"time", adjacent},
        {"value", 2.2345678901234567}, {"outgoing", "linear"}});
    const auto first = fixture.call("animation.readPropertyKeyframes", {{"address", address}, {"limit", 1}}, false);
    const auto firstKey = first["keys"].toArray()[0].toObject();
    REQUIRE(firstKey["id"] == QString::number(big));
    REQUIRE(std::bit_cast<std::uint64_t>(firstKey["value"].toDouble()) == std::bit_cast<std::uint64_t>(precise));
    const auto second = fixture.call("animation.readPropertyKeyframes", {{"address", address}, {"limit", 1},
        {"afterTime", 12.5}, {"expectedDocumentRevision", first["documentRevision"]}}, false);
    REQUIRE(std::bit_cast<std::uint64_t>(second["keys"].toArray()[0].toObject()["time"].toDouble()) == std::bit_cast<std::uint64_t>(adjacent));
    REQUIRE(second["keys"].toArray()[0].toObject()["id"] == QString::number(big + 1));
    REQUIRE(second["curveIdentity"].toObject()["curveId"] == QString::number(big));
}

TEST_CASE("J1 rational wire supports full uint32 and legacy settings replace an effective override", "[j1-api][time]") {
    Work fixture;
    const auto originalActions = fixture.model.scene()->animation().properties.actions;
    const auto maximum = qint64(std::numeric_limits<std::uint32_t>::max());
    const auto fields = QJsonObject{{"settings", rationalSettings(maximum, maximum - 1)}};
    const auto canonical = api::ApiJsonCodec::canonicalParams("animation.setRationalSettings", withFields(fixture.wire(true), fields));
    REQUIRE(canonical.hasValue());
    REQUIRE((*canonical.value)["settings"].toObject()["rationalFps"] == rationalSettings(maximum, maximum - 1)["rationalFps"]);
    fixture.call("animation.setRationalSettings", fields);
    REQUIRE(fixture.call("animation.getState", {}, false)["rationalFps"] == rationalSettings(maximum, maximum - 1)["rationalFps"]);
    const Snapshot beforeInvalid(fixture);
    for (const auto& fps : {QJsonObject{{"numerator", 0}, {"denominator", 1}},
                            QJsonObject{{"numerator", 30000}, {"denominator", 1000}},
                            QJsonObject{{"numerator", 120001}, {"denominator", 1000}},
                            QJsonObject{{"numerator", maximum + 1}, {"denominator", maximum}}}) {
        auto settings = rationalSettings();
        settings["rationalFps"] = fps;
        REQUIRE(errorCode(fixture.invoke("animation.setRationalSettings", {{"settings", settings}})) == "INVALID_ARGUMENT");
        beforeInvalid.requireUnchanged(fixture);
    }
    auto settings = rationalSettings();
    settings["rationalFps"] = QJsonValue(QJsonValue::Null);
    REQUIRE(errorCode(fixture.invoke("animation.setRationalSettings", {{"settings", settings}})) == "INVALID_ARGUMENT");
    beforeInvalid.requireUnchanged(fixture);
    fixture.call("animation.setSettings", {{"settings", QJsonObject{{"fps", 30}, {"startFrame", 1}, {"endFrame", 24}}}});
    const auto legacy = fixture.call("animation.getState", {}, false);
    REQUIRE((legacy["rationalFps"].toObject() == QJsonObject{{"numerator", 30}, {"denominator", 1}}));
    REQUIRE(legacy["settings"].toObject()["fps"].toInt() == 30);
    REQUIRE(fixture.model.scene()->animation().properties.actions == originalActions);
}

TEST_CASE("J1 known unsupported writes remain structured and never become linear", "[j1-api][codec]") {
    Work fixture;
    const Snapshot before(fixture);
    QJsonObject fields{{"address", fixture.position}, {"time", 12.5}, {"value", 1}, {"outgoing", "bezier"}};
    REQUIRE(api::ApiJsonCodec::decodeRequest("animation.upsertPropertyKeyframe", withFields(fixture.wire(true), fields)).hasValue());
    REQUIRE(errorCode(fixture.invoke("animation.upsertPropertyKeyframe", fields)) == "UNSUPPORTED_OPERATION");
    before.requireUnchanged(fixture);
    for (const auto* property : {"transform.rotation_euler_xyz_degrees", "transform.scale"}) {
        fields["address"] = withFields(fixture.position, {{"property", QString::fromLatin1(property)}});
        fields["outgoing"] = "linear";
        REQUIRE(errorCode(fixture.invoke("animation.upsertPropertyKeyframe", fields)) == "UNSUPPORTED_OPERATION");
        before.requireUnchanged(fixture);
    }
    core::Scene foreign;
    fields["address"] = withFields(fixture.position, {{"documentUuid", QString::fromStdString(core::documentUuidString(foreign.documentUuid()))}});
    REQUIRE(errorCode(fixture.invoke("animation.upsertPropertyKeyframe", fields)) == "INVALID_ARGUMENT");
    before.requireUnchanged(fixture);
    fields["address"] = withFields(fixture.position, {{"property", "material.pbr.roughness"}, {"component", "scalar"}});
    REQUIRE(errorCode(fixture.invoke("animation.upsertPropertyKeyframe", fields)) == "INVALID_ARGUMENT");
    before.requireUnchanged(fixture);
}

TEST_CASE("J1 typed codec uses page 2048 while legacy sample remains capped at 256", "[j1-api][limits]") {
    Fixture fixture;
    const auto address = fixture.address("entity", "1", "transform.position", "x");
    QJsonArray addresses;
    for (std::size_t index = 1; index <= api::limits::sourcePageMaximum; ++index)
        addresses.append(fixture.address("entity", QString::number(qulonglong(index)), "transform.position", "x"));
    auto params = withFields(fixture.wire(), {{"frame", 12.5}, {"addresses", addresses}});
    const auto accepted = api::ApiJsonCodec::decodeRequest("animation.sampleProperties", params);
    REQUIRE(accepted.hasValue());
    REQUIRE(std::get<api::AnimationSamplePropertiesRequest>(*accepted.value).addresses.size() == 2048);
    addresses.append(fixture.address("entity", "2049", "transform.position", "x"));
    params["addresses"] = addresses;
    const auto overflow = api::ApiJsonCodec::decodeRequest("animation.sampleProperties", params);
    REQUIRE_FALSE(overflow.hasValue());
    REQUIRE(overflow.error->code == api::ErrorCode::LimitExceeded);
    params["addresses"] = QJsonArray{address, address};
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.sampleProperties", params).hasValue());
    const auto page = api::ApiJsonCodec::canonicalParams("animation.readPropertyKeyframes", withFields(fixture.wire(), {{"address", address}}));
    REQUIRE(page.hasValue());
    REQUIRE((*page.value)["limit"].toInt() == 256);
    REQUIRE(api::ApiJsonCodec::decodeRequest("animation.readPropertyKeyframes", withFields(fixture.wire(), {{"address", address}, {"limit", 2048}})).hasValue());
    REQUIRE_FALSE(api::ApiJsonCodec::decodeRequest("animation.readPropertyKeyframes", withFields(fixture.wire(), {{"address", address}, {"afterTime", 12.5}})).hasValue());
    QJsonArray entityIds;
    for (std::size_t index = 0; index <= api::limits::animationSampleEntities; ++index)
        entityIds.append(QString::number(qulonglong(index + 1)));
    const auto legacy = api::ApiJsonCodec::decodeRequest("animation.sample", withFields(fixture.wire(), {{"frame", 12.5}, {"entityIds", entityIds}}));
    REQUIRE_FALSE(legacy.hasValue());
    REQUIRE(legacy.error->code == api::ErrorCode::LimitExceeded);
}

TEST_CASE("J1 eight write services reject initial CAS and Busy before calling a final guard", "[j1-api][guard]") {
    for (const bool busy : {false, true})
        for (int operation = 0; operation < 8; ++operation) {
            CAPTURE(busy, operation);
            Work fixture;
            auto test = fixture.mutations()[std::size_t(operation)];
            auto params = withFields(fixture.wire(true), test.fields);
            if (busy) fixture.service.setExternalBusy("J1 initial", true);
            else params["expectedDocumentRevision"] = "0";
            const Snapshot before(fixture);
            int guards = 0;
            const auto rejected = api::ApiJsonCodec::invoke(fixture.service, test.method, params, api::FileAccess::External,
                [&]() -> std::optional<api::ApiError> { ++guards; return std::nullopt; });
            REQUIRE(errorCode(rejected) == (busy ? "BUSY" : "REVISION_CONFLICT"));
            REQUIRE(guards == 0);
            before.requireUnchanged(fixture);
        }
}

TEST_CASE("J1 direct and codec writes preserve complete state on final cancellation and revocation", "[j1-api][guard]") {
    for (const bool codec : {false, true})
        for (const auto code : {api::ErrorCode::Cancelled, api::ErrorCode::PermissionDenied})
            for (int operation = 0; operation < 8; ++operation) {
                CAPTURE(codec, code, operation);
                Work fixture;
                fixture.call("animation.upsertPropertyKeyframe", {{"address", fixture.position}, {"time", 24}, {"value", 2}, {"outgoing", "linear"}});
                const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
                fixture.model.undo();
                REQUIRE(fixture.model.undoStack()->canRedo());
                const auto test = fixture.mutations()[std::size_t(operation)];
                const auto params = withFields(fixture.wire(true), test.fields);
                const Snapshot before(fixture);
                int guards = 0;
                api::BeforeCommitGuard denied = [&]() -> std::optional<api::ApiError> {
                    ++guards;
                    return api::ApiError{code, "J1 guard refusal", {}, api::Recovery::None, fixture.service.documentState()};
                };
                if (codec) {
                    const auto result = api::ApiJsonCodec::invoke(fixture.service, test.method, params, api::FileAccess::External, denied);
                    REQUIRE(errorCode(result) == (code == api::ErrorCode::Cancelled ? "CANCELLED" : "PERMISSION_DENIED"));
                } else {
                    const auto decoded = api::ApiJsonCodec::decodeRequest(test.method, params);
                    REQUIRE(decoded.hasValue());
                    fixture.service.exchangeBeforeCommitGuard(denied);
                    REQUIRE(directFailure(fixture.service, *decoded.value)->code == code);
                    fixture.service.exchangeBeforeCommitGuard({});
                }
                REQUIRE(guards == 1);
                before.requireUnchanged(fixture);
            }
}

TEST_CASE("J1 eight codec writes reject real expiry after one external callback", "[j1-api][guard]") {
    for (int operation = 0; operation < 8; ++operation) {
        CAPTURE(operation);
        Work fixture;
        const auto test = fixture.mutations()[std::size_t(operation)];
        const Snapshot before(fixture);
        int guards = 0;
        const auto result = fixture.invoke(test.method, withFields(test.fields, {{"timeoutMs", 100}}), true,
            [&]() -> std::optional<api::ApiError> { ++guards; QTest::qWait(120); return std::nullopt; });
        REQUIRE(errorCode(result) == "DEADLINE_EXCEEDED");
        REQUIRE(guards == 1);
        before.requireUnchanged(fixture);
    }
}

TEST_CASE("J1 final callback rechecks document and physical Busy without publishing", "[j1-api][guard]") {
    for (const bool busy : {false, true}) {
        Work fixture;
        int guards = 0;
        std::optional<Snapshot> afterCallback;
        const auto result = fixture.invoke("material.create", {{"parameters", parameters(.8)}}, true,
            [&]() -> std::optional<api::ApiError> {
                ++guards;
                if (busy) fixture.service.setExternalBusy("J1 callback", true);
                else fixture.call("document.new", {{"ifDirty", "discard"}});
                afterCallback.emplace(fixture);
                return std::nullopt;
            });
        REQUIRE(errorCode(result) == (busy ? "BUSY" : "STALE_DOCUMENT"));
        REQUIRE(guards == 1);
        REQUIRE(afterCallback);
        afterCallback->requireUnchanged(fixture);
    }
}

TEST_CASE("J1 six no changes preserve redo clean point and still honor the final guard", "[j1-api][guard]") {
    Work fixture;
    fixture.call("animation.setRationalSettings", {{"settings", rationalSettings()}});
    fixture.call("animation.upsertPropertyKeyframe", {{"address", fixture.position}, {"time", 24}, {"value", 2}, {"outgoing", "linear"}});
    const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
    fixture.model.undo();
    REQUIRE(fixture.model.undoStack()->canRedo());
    const std::vector<WireCase> cases{
        {"material.update", true, {{"materialId", fixture.material}, {"parameters", parameters()}}},
        {"entity.assignMaterial", true, {{"entityId", QString::number(fixture.entity)}, {"materialId", fixture.material}}},
        {"light.updateSun", true, {{"entityId", fixture.sun}, {"sun", QJsonObject{{"irradiance", 1}, {"color", QJsonArray{1, 1, 1}}}}}},
        {"animation.upsertPropertyKeyframe", true, {{"address", fixture.position}, {"time", 1}, {"value", 0}, {"outgoing", "linear"}}},
        {"animation.deletePropertyKeyframe", true, {{"address", fixture.position}, {"time", 99}}},
        {"animation.setRationalSettings", true, {{"settings", rationalSettings()}}}
    };
    const Snapshot before(fixture);
    for (const auto& test : cases) {
        CAPTURE(test.method);
        int guards = 0;
        const auto result = fixture.invoke(test.method, test.fields, true,
            [&]() -> std::optional<api::ApiError> { ++guards; return std::nullopt; });
        REQUIRE(result.contains("result"));
        const auto value = result["result"].toObject();
        const auto command = value.contains("command") ? value["command"].toObject() : value;
        REQUIRE(command["status"] == "no_change");
        REQUIRE(guards == 1);
        before.requireUnchanged(fixture);
        const auto denied = fixture.invoke(test.method, test.fields, true,
            [&]() -> std::optional<api::ApiError> {
                ++guards;
                return api::ApiError{api::ErrorCode::Cancelled, "no change refused", {}, api::Recovery::None, fixture.service.documentState()};
            });
        REQUIRE(errorCode(denied) == "CANCELLED");
        REQUIRE(guards == 2);
        before.requireUnchanged(fixture);
    }
}

TEST_CASE("J1 three queries call Busy provider once and honor reentrant physical Busy", "[j1-api][query]") {
    for (const auto& method : {QStringLiteral("material.get"), QStringLiteral("animation.readPropertyKeyframes"), QStringLiteral("animation.sampleProperties")}) {
        Work fixture;
        const auto fields = method == "material.get" ? QJsonObject{{"materialId", fixture.material}}
            : method == "animation.readPropertyKeyframes" ? QJsonObject{{"address", fixture.position}}
            : QJsonObject{{"frame", 12.5}, {"addresses", QJsonArray{fixture.position}}};
        const Snapshot before(fixture);
        int providers = 0;
        fixture.service.setBusyProvider([&] {
            ++providers;
            fixture.service.setExternalBusy("J1 query callback", true);
            return QStringList{};
        });
        REQUIRE(errorCode(fixture.invoke(method, fields, false)) == "BUSY");
        REQUIRE(providers == 1);
        before.requireUnchanged(fixture);
        fixture.service.setBusyProvider({});
        fixture.service.setExternalBusy("J1 query callback", false);
        fixture.call(method, fields, false);
        before.requireUnchanged(fixture);
    }
}
