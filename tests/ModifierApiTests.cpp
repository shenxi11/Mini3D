/*
 * 模块名: ModifierApiTests
 * 功能概述: 验证固定镜像/细分链的显式参数、烘焙、完整状态及原子历史合同。
 * 对外接口: Catch2 [modifier-api] 测试。
 * 依赖关系: EditorApiService、ApiJsonCodec、SceneViewModel、Core、Qt、Catch2。
 * 输入输出: 独立场景及共享样例到 typed/wire/canonical、源与求值、历史断言。
 * 异常与错误: 覆盖版本、交互忙碌、参数/拓扑、候选预算和最终许可拒绝。
 * 维护说明: 不创建 GUI 模态或共享构建；GUI 入口仅建立合法夹具和验证同栈回放。
 */
#include "core/modeling/MeshValidation.h"
#include "editor/SceneViewModel.h"
#include "editor/api/ApiJsonCodec.h"
#include "editor/api/EditorApiService.h"
#include "editor/api/MeshApiSupport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <type_traits>

using namespace mini3d;
namespace {
namespace api = editor::api;
using Mirror = core::modeling::MirrorOptions;
using Subdivision = core::modeling::SubdivisionOptions;
using Kind = api::ModifierKind;

QJsonValue mirrorJson(const std::optional<Mirror>& options) {
    api::ModifierState state;
    state.mirror = options;
    return api::ApiJsonCodec::encode(state).value("mirror");
}
QJsonValue subdivisionJson(const std::optional<Subdivision>& options) {
    api::ModifierState state;
    state.subdivision = options;
    return api::ApiJsonCodec::encode(state).value("subdivision");
}
struct ModifierFixture {
    editor::SceneViewModel model;
    api::EditorApiService service{model};
    core::EntityId entity = 0;
    core::MeshId mesh = 0;
    ModifierFixture() {
        model.newScene();
        entity = create(core::PrimitiveKind::Cube);
        api::MeshMakeEditableRequest request;
        request.document = service.documentState().document;
        request.expectedDocumentRevision = service.documentState().documentRevision;
        request.entityId = entity;
        const auto result = service.makeEditable(request);
        REQUIRE(result.hasValue());
        mesh = result.value->mesh.meshId;
    }
    core::EntityId create(core::PrimitiveKind primitive) {
        api::EntityCreateRequest request;
        request.document = service.documentState().document;
        request.expectedDocumentRevision = service.documentState().documentRevision;
        request.primitive = primitive;
        request.name = QStringLiteral("修改器事务对象");
        const auto result = service.createEntity(request);
        REQUIRE(result.hasValue());
        return result.value->createdEntityIds.front();
    }
    const core::EditableMeshRecord& record() const {
        return *model.scene()->editableMesh(mesh);
    }
    const core::EditableMeshContent& content() const {
        return *record().content;
    }
    template <typename Request> Request request() const {
        Request result;
        result.document = service.documentState().document;
        result.expectedDocumentRevision = service.documentState().documentRevision;
        result.entityId = entity;
        result.meshId = mesh;
        result.expectedTopologyRevision = record().topologyRevision;
        result.expectedGeometryRevision = record().geometryRevision;
        return result;
    }
    QJsonObject context() const {
        const auto state = api::ApiJsonCodec::encodeState(service.documentState());
        return {{"document", state.value("document")},
                {"expectedDocumentRevision", state.value("documentRevision")},
                {"entityId", QString::number(entity)},
                {"meshId", QString::number(mesh)},
                {"expectedTopologyRevision", QString::number(record().topologyRevision)},
                {"expectedGeometryRevision", QString::number(record().geometryRevision)}};
    }
    QJsonObject params(const QString& method) const {
        auto result = context();
        if (method == "modifier.setMirror")
            result.insert("options", mirrorJson(Mirror{}));
        else if (method == "modifier.setSubdivision")
            result.insert("options", subdivisionJson(Subdivision{}));
        else
            result.insert("modifier", "mirror");
        return result;
    }
    api::MeshTargetRequest target() const {
        api::MeshTargetRequest result;
        result.document = service.documentState().document;
        result.entityId = entity;
        result.meshId = mesh;
        return result;
    }
    void bakeSubdivision(int levels) {
        Subdivision options;
        options.levels = levels;
        REQUIRE(model.setSubdivisionOptions(entity, options));
        REQUIRE(model.applySubdivision(entity));
    }
};
struct Remembered {
    api::DocumentState state;
    int count, index, clean;
    const QUndoCommand* redo;
    core::EntityId selection;
    std::size_t entities;
    core::PrimitiveKind primitive;
    bool visible;
    std::shared_ptr<const core::EditableMeshContent> content;
    std::uint64_t topology, geometry, evaluation;
};
Remembered remember(ModifierFixture& fixture) {
    const auto* history = fixture.model.undoStack();
    const auto* node = fixture.model.scene()->find(fixture.entity);
    const auto& record = fixture.record();
    return {fixture.service.documentState(),
            history->count(),
            history->index(),
            history->cleanIndex(),
            history->canRedo() ? history->command(history->index()) : nullptr,
            fixture.model.selection()->selectedEntity(),
            fixture.model.scene()->nodes().size(),
            node->primitive,
            node->visible,
            record.content,
            record.topologyRevision,
            record.geometryRevision,
            record.evaluationRevision};
}
void unchanged(ModifierFixture& fixture, const Remembered& before) {
    const auto state = fixture.service.documentState();
    REQUIRE(state.document == before.state.document);
    REQUIRE(state.documentRevision == before.state.documentRevision);
    REQUIRE(state.historyRevision == before.state.historyRevision);
    const auto* history = fixture.model.undoStack();
    REQUIRE(history->count() == before.count);
    REQUIRE(history->index() == before.index);
    REQUIRE(history->cleanIndex() == before.clean);
    REQUIRE(history->canRedo() == (before.redo != nullptr));
    if (before.redo)
        REQUIRE(history->command(history->index()) == before.redo);
    REQUIRE(fixture.model.selection()->selectedEntity() == before.selection);
    REQUIRE(fixture.model.scene()->nodes().size() == before.entities);
    const auto* node = fixture.model.scene()->find(fixture.entity);
    REQUIRE(node->primitive == before.primitive);
    REQUIRE(node->visible == before.visible);
    REQUIRE(node->editableMesh == fixture.mesh);
    REQUIRE(fixture.record().content == before.content);
    REQUIRE(fixture.record().topologyRevision == before.topology);
    REQUIRE(fixture.record().geometryRevision == before.geometry);
    REQUIRE(fixture.record().evaluationRevision == before.evaluation);
}
core::EntityId prepareRedo(ModifierFixture& fixture) {
    const auto other = fixture.create(core::PrimitiveKind::Empty);
    fixture.model.selection()->setSelectedEntity(other);
    const_cast<QUndoStack*>(fixture.model.undoStack())->setClean();
    REQUIRE(fixture.model.renameEntity(other, QStringLiteral("等待重做")));
    fixture.model.undo();
    REQUIRE(fixture.model.undoStack()->canRedo());
    REQUIRE(fixture.model.undoStack()->isClean());
    return other;
}
api::ApiResult<api::ModifierCommandResult> dispatch(ModifierFixture& fixture,
                                                    const api::ApiRequest& request) {
    if (const auto* mirror = std::get_if<api::ModifierSetMirrorRequest>(&request))
        return fixture.service.setMirror(*mirror);
    if (const auto* subdivision = std::get_if<api::ModifierSetSubdivisionRequest>(&request))
        return fixture.service.setSubdivision(*subdivision);
    return fixture.service.applyModifier(std::get<api::ModifierApplyRequest>(request));
}
void checkFlat(ModifierFixture& fixture, const QJsonObject& result, api::ResultStatus status,
               bool requery) {
    REQUIRE_FALSE(result.contains("command"));
    REQUIRE_FALSE(result.contains("mesh"));
    const auto current = api::ApiJsonCodec::encodeState(fixture.service.documentState());
    for (auto field = current.begin(); field != current.end(); ++field)
        REQUIRE(result.value(field.key()) == field.value());
    REQUIRE(result.value("status").toString() ==
            (status == api::ResultStatus::Committed ? "committed" : "no_change"));
    REQUIRE(result.value("undoable").isBool());
    REQUIRE(result.value("undoable").toBool() == (status == api::ResultStatus::Committed));
    REQUIRE(result.value("selectionChanged").isBool());
    REQUIRE_FALSE(result.value("selectionChanged").toBool());
    REQUIRE(result.value("requeryRequired").isBool());
    REQUIRE(result.value("requeryRequired").toBool() == requery);
    REQUIRE(result.value("entityId").toString() == QString::number(fixture.entity));
    REQUIRE(result.value("meshId").toString() == QString::number(fixture.mesh));
    REQUIRE(result.value("topologyRevision").toString() ==
            QString::number(fixture.record().topologyRevision));
    REQUIRE(result.value("geometryRevision").toString() ==
            QString::number(fixture.record().geometryRevision));
    REQUIRE(result.value("evaluationRevision").toString() ==
            QString::number(fixture.record().evaluationRevision));
    const auto state = api::modifierState(fixture.content());
    const auto modifiers = result.value("modifiers").toObject();
    const auto order = QJsonArray{"mirror", "subdivision"};
    REQUIRE(modifiers.value("order").toArray() == order);
    REQUIRE(modifiers == api::ApiJsonCodec::encode(state));
    REQUIRE(modifiers.size() == 3);
    REQUIRE(modifiers.value("mirror").isNull() == !state.mirror.has_value());
    REQUIRE(modifiers.value("subdivision").isNull() == !state.subdivision.has_value());
    if (state.mirror)
        REQUIRE(modifiers.value("mirror").toObject().size() == 5);
    if (state.subdivision)
        REQUIRE(modifiers.value("subdivision").toObject().size() == 2);
    const auto summary = fixture.service.meshSummary(fixture.target());
    REQUIRE(summary.hasValue());
    REQUIRE(summary.value->modifiers == state);
    const auto summaryJson = api::ApiJsonCodec::encode(*summary.value);
    REQUIRE(summaryJson.value("modifiers").toObject() == modifiers);
}
void checkResult(ModifierFixture& fixture, const api::ApiResult<api::ModifierCommandResult>& result,
                 api::ResultStatus status, bool requery) {
    INFO((result.error ? result.error->message.toStdString() : std::string{}));
    REQUIRE(result.hasValue());
    REQUIRE(result.value->command.status == status);
    REQUIRE(result.value->modifiers == api::modifierState(fixture.content()));
    checkFlat(fixture, api::ApiJsonCodec::encode(*result.value), status, requery);
}
void checkError(ModifierFixture& fixture, const api::ApiResult<api::ModifierCommandResult>& result,
                api::ErrorCode code, const Remembered& before) {
    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error->code == code);
    unchanged(fixture, before);
}
void checkWireError(ModifierFixture& fixture, const QString& method, const QJsonObject& params,
                    api::ErrorCode code, const Remembered& before,
                    api::BeforeCommitGuard guard = {}) {
    const auto result = api::ApiJsonCodec::invoke(fixture.service, method, params,
                                                  api::FileAccess::External, std::move(guard));
    REQUIRE(result.contains("error"));
    const auto error = result.value("error").toObject();
    const auto data = error.value("data").toObject();
    const auto current = api::ApiJsonCodec::encodeState(fixture.service.documentState());
    for (auto field = current.begin(); field != current.end(); ++field)
        REQUIRE(data.value(field.key()) == field.value());
    const auto expected = api::ApiJsonCodec::encodeError(
        {code, {}, {}, api::Recovery::None, fixture.service.documentState()});
    REQUIRE(error.value("data").toObject().value("code") ==
            expected.value("data").toObject().value("code"));
    unchanged(fixture, before);
}
void roundTrip(ModifierFixture& fixture, const Remembered& before) {
    const auto after = fixture.record().content;
    const auto revision = fixture.record().topologyRevision;
    fixture.model.undo();
    REQUIRE(fixture.record().content == before.content);
    REQUIRE(fixture.record().topologyRevision > revision);
    REQUIRE(fixture.model.selection()->selectedEntity() == before.selection);
    REQUIRE(fixture.model.scene()->find(fixture.entity)->editableMesh == fixture.mesh);
    const auto undone = fixture.record().topologyRevision;
    fixture.model.redo();
    REQUIRE(fixture.record().content == after);
    REQUIRE(fixture.record().topologyRevision > undone);
    REQUIRE(fixture.model.scene()->find(fixture.entity)->editableMesh == fixture.mesh);
    REQUIRE(fixture.model.selection()->selectedEntity() == before.selection);
}
QJsonObject examples() {
    QFile file(QDir(QString::fromUtf8(MINI3D_API_SCHEMA_DIRECTORY))
                   .filePath("m5-modifiers-examples.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto result = QJsonDocument::fromJson(file.readAll(), &error).object();
    REQUIRE(error.error == QJsonParseError::NoError);
    return result;
}
} // namespace

TEST_CASE("Modifier codec consumes frozen full nullable samples and canonical double thresholds",
          "[modifier-api][modifier-schema][api-canonical]") {
    const auto samples = examples();
    REQUIRE(samples.value("valid").toArray().size() == 9);
    REQUIRE(samples.value("invalid").toArray().size() == 18);
    ModifierFixture fixture;
    const auto before = remember(fixture);
    const auto description = fixture.service.describe();
    REQUIRE(description.hasValue());
    for (const auto& item : samples.value("valid").toArray()) {
        const auto sample = item.toObject();
        const auto method = sample.value("method").toString();
        const auto params = sample.value("value").toObject();
        REQUIRE(api::ApiJsonCodec::decodeRequest(method, params).hasValue());
        const auto canonical = api::ApiJsonCodec::canonicalParams(method, params);
        REQUIRE(canonical.hasValue());
        REQUIRE(canonical.value->value("timeoutMs").toInt() == int(api::limits::mutationTimeoutMs));
        if (method == "modifier.apply")
            REQUIRE(canonical.value->value("modifier") == params.value("modifier"));
        else
            REQUIRE(canonical.value->value("options") == params.value("options"));
        const auto again = api::ApiJsonCodec::canonicalParams(method, *canonical.value);
        REQUIRE(again.hasValue());
        REQUIRE(*again.value == *canonical.value);
        REQUIRE(std::any_of(description.value->methods.begin(), description.value->methods.end(),
                            [&](const auto& description) {
                                return description.name == method && description.externalEnabled &&
                                       description.permission == "scene.write";
                            }));
    }
    for (const auto& item : samples.value("invalid").toArray()) {
        const auto sample = item.toObject();
        const auto method = "modifier." + sample.value("definition").toString();
        const auto params = sample.value("value").toObject();
        INFO(sample.value("reason").toString().toStdString());
        const auto decoded = api::ApiJsonCodec::decodeRequest(method, params);
        REQUIRE_FALSE(decoded.hasValue());
        REQUIRE(decoded.error->code == api::ErrorCode::InvalidArgument);
        REQUIRE(decoded.error->protocolCode == -32602);
        REQUIRE_FALSE(api::ApiJsonCodec::canonicalParams(method, params).hasValue());
        checkWireError(fixture, method, params, api::ErrorCode::InvalidArgument, before);
    }
    unchanged(fixture, before);
}

TEST_CASE("Modifier legal samples use one explicit hidden-target history and exact GUI replay",
          "[modifier-api]") {
    const auto samples = examples();
    for (const auto& item : samples.value("valid").toArray()) {
        const auto sample = item.toObject();
        const auto method = sample.value("method").toString();
        for (const bool wire : {false, true}) {
            DYNAMIC_SECTION(method.toStdString() << " wire=" << wire << " "
                                                 << QJsonDocument(sample.value("value").toObject())
                                                        .toJson(QJsonDocument::Compact)
                                                        .toStdString()) {
                ModifierFixture fixture;
                const bool apply = method == "modifier.apply";
                const bool applyMirror =
                    apply &&
                    sample.value("value").toObject().value("modifier").toString() == "mirror";
                if (apply || (method == "modifier.setMirror" &&
                              sample.value("value").toObject().value("options").isNull()))
                    REQUIRE(fixture.model.setMirrorOptions(fixture.entity, Mirror{}));
                if (apply || (method == "modifier.setSubdivision" &&
                              sample.value("value").toObject().value("options").isNull()))
                    REQUIRE(fixture.model.setSubdivisionOptions(fixture.entity, Subdivision{}));
                REQUIRE(fixture.model.setVisible(fixture.entity, false));
                prepareRedo(fixture);
                const auto before = remember(fixture);
                const auto expectedSource =
                    apply ? (applyMirror ? before.content->mirrorEvaluation->mesh
                                         : before.content->evaluatedMesh())
                          : before.content->source;
                auto params = sample.value("value").toObject();
                const auto context = fixture.context();
                for (auto field = context.begin(); field != context.end(); ++field)
                    params.insert(field.key(), field.value());
                int guards = 0;
                const auto guard = [&]() -> std::optional<api::ApiError> {
                    ++guards;
                    unchanged(fixture, before);
                    return std::nullopt;
                };
                if (wire) {
                    const auto result = api::ApiJsonCodec::invoke(fixture.service, method, params,
                                                                  api::FileAccess::External, guard);
                    REQUIRE(result.contains("result"));
                    checkFlat(fixture, result.value("result").toObject(),
                              api::ResultStatus::Committed, apply);
                } else {
                    const auto decoded = api::ApiJsonCodec::decodeRequest(method, params);
                    REQUIRE(decoded.hasValue());
                    fixture.service.exchangeBeforeCommitGuard(guard);
                    checkResult(fixture, dispatch(fixture, *decoded.value),
                                api::ResultStatus::Committed, apply);
                    fixture.service.exchangeBeforeCommitGuard({});
                }
                REQUIRE(guards == 1);
                REQUIRE(fixture.content().source == expectedSource);
                REQUIRE(fixture.model.selection()->selectedEntity() == before.selection);
                REQUIRE_FALSE(fixture.model.scene()->find(fixture.entity)->visible);
                REQUIRE(fixture.model.undoStack()->index() == before.index + 1);
                REQUIRE(fixture.service.documentState().documentRevision ==
                        before.state.documentRevision + 1);
                REQUIRE(fixture.service.documentState().historyRevision ==
                        before.state.historyRevision + 1);
                REQUIRE(fixture.record().topologyRevision > before.topology);
                REQUIRE(fixture.record().geometryRevision > before.geometry);
                REQUIRE(fixture.record().evaluationRevision > before.evaluation);
                if (apply) {
                    REQUIRE_FALSE(fixture.content().mirror.has_value());
                    REQUIRE(fixture.content().subdivision.has_value() == applyMirror);
                }
                roundTrip(fixture, before);
            }
        }
    }
}

TEST_CASE("Modifier summary retains complete disabled axis flags and finite double options",
          "[modifier-api]") {
    for (const auto axis : {core::modeling::MirrorAxis::X, core::modeling::MirrorAxis::Y,
                            core::modeling::MirrorAxis::Z}) {
        DYNAMIC_SECTION("axis=" << int(axis)) {
            ModifierFixture fixture;
            const auto source = fixture.content().source;
            Mirror mirror;
            mirror.axis = axis;
            mirror.enabled = false;
            mirror.merge = axis == core::modeling::MirrorAxis::Y;
            mirror.clipping = axis != core::modeling::MirrorAxis::Z;
            mirror.threshold = axis == core::modeling::MirrorAxis::X ? 0 : 1e300;
            auto request = fixture.request<api::ModifierSetMirrorRequest>();
            request.options = mirror;
            checkResult(fixture, fixture.service.setMirror(request), api::ResultStatus::Committed,
                        false);
            auto subdivision = fixture.request<api::ModifierSetSubdivisionRequest>();
            subdivision.options = Subdivision{false, 2};
            checkResult(fixture, fixture.service.setSubdivision(subdivision),
                        api::ResultStatus::Committed, false);
            const auto summary = fixture.service.meshSummary(fixture.target());
            REQUIRE(summary.hasValue());
            REQUIRE(summary.value->modifiers.mirror == mirror);
            REQUIRE(summary.value->modifiers.subdivision == subdivision.options);
            REQUIRE(summary.value->source.vertexCount == 8);
            REQUIRE(summary.value->source.faceCount == 6);
            REQUIRE(summary.value->evaluated.vertexCount == 8);
            REQUIRE(summary.value->evaluated.faceCount == 6);
            REQUIRE(fixture.content().source == source);
        }
    }
}

TEST_CASE("Same modifier options including null preserve redo clean selection and all revisions",
          "[modifier-api]") {
    for (const bool mirror : {true, false})
        for (const bool present : {false, true}) {
            DYNAMIC_SECTION("mirror=" << mirror << " present=" << present) {
                ModifierFixture fixture;
                if (present) {
                    Mirror options;
                    options.enabled = false;
                    options.threshold = 1e300;
                    REQUIRE(fixture.model.setMirrorOptions(fixture.entity, options));
                    REQUIRE(
                        fixture.model.setSubdivisionOptions(fixture.entity, Subdivision{false, 2}));
                }
                prepareRedo(fixture);
                const auto before = remember(fixture);
                int guards = 0;
                const auto guard = [&]() -> std::optional<api::ApiError> {
                    ++guards;
                    unchanged(fixture, before);
                    return std::nullopt;
                };
                fixture.service.exchangeBeforeCommitGuard(guard);
                if (mirror) {
                    auto request = fixture.request<api::ModifierSetMirrorRequest>();
                    request.options = fixture.content().mirror;
                    checkResult(fixture, fixture.service.setMirror(request),
                                api::ResultStatus::NoChange, false);
                } else {
                    auto request = fixture.request<api::ModifierSetSubdivisionRequest>();
                    request.options = fixture.content().subdivision;
                    checkResult(fixture, fixture.service.setSubdivision(request),
                                api::ResultStatus::NoChange, false);
                }
                unchanged(fixture, before);
                REQUIRE(guards == 1);
                fixture.service.exchangeBeforeCommitGuard({});
                auto params = fixture.context();
                params.insert("options", mirror ? mirrorJson(fixture.content().mirror)
                                                : subdivisionJson(fixture.content().subdivision));
                const auto method = mirror ? "modifier.setMirror" : "modifier.setSubdivision";
                const auto result = api::ApiJsonCodec::invoke(fixture.service, method, params,
                                                              api::FileAccess::External, guard);
                REQUIRE(result.contains("result"));
                checkFlat(fixture, result.value("result").toObject(), api::ResultStatus::NoChange,
                          false);
                REQUIRE(guards == 2);
                unchanged(fixture, before);
            }
        }
}

TEST_CASE("Disabled modifiers remain applicable and follow the exact fixed-chain bake rule",
          "[modifier-api]") {
    for (const auto kind : {Kind::Mirror, Kind::Subdivision}) {
        for (const bool wire : {false, true}) {
            DYNAMIC_SECTION("kind=" << int(kind) << " wire=" << wire) {
                ModifierFixture fixture;
                Mirror mirror;
                mirror.enabled = kind != Kind::Mirror;
                REQUIRE(fixture.model.setMirrorOptions(fixture.entity, mirror));
                REQUIRE(fixture.model.setSubdivisionOptions(
                    fixture.entity, Subdivision{kind != Kind::Subdivision, 1}));
                prepareRedo(fixture);
                const auto before = remember(fixture);
                const auto expectedSource = kind == Kind::Mirror
                                                ? before.content->mirrorEvaluation->mesh
                                                : before.content->evaluatedMesh();
                if (wire) {
                    auto params = fixture.context();
                    params.insert("modifier", kind == Kind::Mirror ? "mirror" : "subdivision");
                    const auto result =
                        api::ApiJsonCodec::invoke(fixture.service, "modifier.apply", params);
                    REQUIRE(result.contains("result"));
                    checkFlat(fixture, result.value("result").toObject(),
                              api::ResultStatus::Committed, true);
                } else {
                    auto request = fixture.request<api::ModifierApplyRequest>();
                    request.modifier = kind;
                    checkResult(fixture, fixture.service.applyModifier(request),
                                api::ResultStatus::Committed, true);
                }
                REQUIRE(fixture.content().source == expectedSource);
                REQUIRE_FALSE(fixture.content().mirror.has_value());
                REQUIRE(fixture.content().subdivision.has_value() == (kind == Kind::Mirror));
                REQUIRE(fixture.model.undoStack()->index() == before.index + 1);
                roundTrip(fixture, before);
            }
        }
    }
}

TEST_CASE("Same null modifier options still require exactly one final commit permission",
          "[modifier-api]") {
    for (const bool mirror : {true, false}) {
        DYNAMIC_SECTION("mirror=" << mirror) {
            ModifierFixture fixture;
            prepareRedo(fixture);
            const auto before = remember(fixture);
            int guards = 0;
            const auto guard = [&]() -> std::optional<api::ApiError> {
                ++guards;
                unchanged(fixture, before);
                return api::ApiError{api::ErrorCode::Cancelled,
                                     QStringLiteral("同值许可拒绝"),
                                     {},
                                     api::Recovery::None,
                                     fixture.service.documentState()};
            };
            fixture.service.exchangeBeforeCommitGuard(guard);
            if (mirror)
                checkError(
                    fixture,
                    fixture.service.setMirror(fixture.request<api::ModifierSetMirrorRequest>()),
                    api::ErrorCode::Cancelled, before);
            else
                checkError(fixture,
                           fixture.service.setSubdivision(
                               fixture.request<api::ModifierSetSubdivisionRequest>()),
                           api::ErrorCode::Cancelled, before);
            REQUIRE(guards == 1);
            fixture.service.exchangeBeforeCommitGuard({});
            auto params = fixture.context();
            params.insert("options", QJsonValue(QJsonValue::Null));
            checkWireError(fixture, mirror ? "modifier.setMirror" : "modifier.setSubdivision",
                           params, api::ErrorCode::Cancelled, before, guard);
            REQUIRE(guards == 2);
        }
    }
}

TEST_CASE("Applying an absent modifier atomically rejects rather than treating disabled as absent",
          "[modifier-api]") {
    for (const auto kind : {Kind::Mirror, Kind::Subdivision}) {
        DYNAMIC_SECTION("kind=" << int(kind)) {
            ModifierFixture fixture;
            if (kind == Kind::Mirror)
                REQUIRE(fixture.model.setSubdivisionOptions(fixture.entity, Subdivision{}));
            else
                REQUIRE(fixture.model.setMirrorOptions(fixture.entity, Mirror{}));
            prepareRedo(fixture);
            const auto before = remember(fixture);
            auto request = fixture.request<api::ModifierApplyRequest>();
            request.modifier = kind;
            checkError(fixture, fixture.service.applyModifier(request),
                       api::ErrorCode::UnsupportedOperation, before);
            auto params = fixture.context();
            params.insert("modifier", kind == Kind::Mirror ? "mirror" : "subdivision");
            checkWireError(fixture, "modifier.apply", params, api::ErrorCode::UnsupportedOperation,
                           before);
        }
    }
}

TEST_CASE("Typed modifier options reject invalid axes levels and nonfinite double thresholds",
          "[modifier-api]") {
    for (int failure = 0; failure < 8; ++failure) {
        DYNAMIC_SECTION("failure=" << failure) {
            ModifierFixture fixture;
            prepareRedo(fixture);
            const auto before = remember(fixture);
            if (failure < 5) {
                Mirror options;
                options.enabled = false;
                if (failure == 0)
                    options.axis = static_cast<core::modeling::MirrorAxis>(99);
                if (failure == 1)
                    options.threshold = -0.1;
                if (failure == 2)
                    options.threshold = std::numeric_limits<double>::quiet_NaN();
                if (failure == 3)
                    options.threshold = std::numeric_limits<double>::infinity();
                if (failure == 4)
                    options.threshold = -std::numeric_limits<double>::infinity();
                auto request = fixture.request<api::ModifierSetMirrorRequest>();
                request.options = options;
                checkError(fixture, fixture.service.setMirror(request),
                           api::ErrorCode::InvalidArgument, before);
                if (failure >= 2) {
                    auto params = fixture.params("modifier.setMirror");
                    auto json = params.value("options").toObject();
                    json.insert("threshold", options.threshold);
                    params.insert("options", json);
                    checkWireError(fixture, "modifier.setMirror", params,
                                   api::ErrorCode::InvalidArgument, before);
                }
            } else if (failure < 7) {
                auto request = fixture.request<api::ModifierSetSubdivisionRequest>();
                request.options = Subdivision{false, failure == 5 ? 0 : 3};
                checkError(fixture, fixture.service.setSubdivision(request),
                           api::ErrorCode::InvalidArgument, before);
            } else {
                auto request = fixture.request<api::ModifierApplyRequest>();
                request.modifier = static_cast<Kind>(99);
                checkError(fixture, fixture.service.applyModifier(request),
                           api::ErrorCode::InvalidArgument, before);
            }
        }
    }
}

TEST_CASE("Enabled subdivision rejects legal nonquad topology without publishing options",
          "[modifier-api]") {
    ModifierFixture fixture;
    auto source = fixture.content().source;
    source.faces.resize(1);
    source.faces.front().corners.pop_back();
    REQUIRE(core::modeling::validateEditableMesh(source).isValid());
    REQUIRE(fixture.model.replaceEditableMesh(fixture.entity, source));
    prepareRedo(fixture);
    const auto before = remember(fixture);
    auto request = fixture.request<api::ModifierSetSubdivisionRequest>();
    request.options = Subdivision{};
    checkError(fixture, fixture.service.setSubdivision(request), api::ErrorCode::InvalidTopology,
               before);
    checkWireError(fixture, "modifier.setSubdivision", fixture.params("modifier.setSubdivision"),
                   api::ErrorCode::InvalidTopology, before);
    request.options = Subdivision{false, 2};
    checkResult(fixture, fixture.service.setSubdivision(request), api::ResultStatus::Committed,
                false);
    REQUIRE(fixture.content().source == source);
}

TEST_CASE(
    "Modifier metadata identity busy and final guard failures preserve the entire transaction",
    "[modifier-api]") {
    const std::array<api::ErrorCode, 15> errors{api::ErrorCode::InvalidArgument,
                                                api::ErrorCode::InvalidArgument,
                                                api::ErrorCode::InvalidArgument,
                                                api::ErrorCode::StaleDocument,
                                                api::ErrorCode::RevisionConflict,
                                                api::ErrorCode::RevisionConflict,
                                                api::ErrorCode::RevisionConflict,
                                                api::ErrorCode::InvalidArgument,
                                                api::ErrorCode::InvalidArgument,
                                                api::ErrorCode::NotFound,
                                                api::ErrorCode::NotFound,
                                                api::ErrorCode::UnsupportedOperation,
                                                api::ErrorCode::Busy,
                                                api::ErrorCode::Busy,
                                                api::ErrorCode::Cancelled};
    for (const QString method : {"modifier.setMirror", "modifier.setSubdivision", "modifier.apply"})
        for (int failure = 0; failure < int(errors.size()); ++failure) {
            DYNAMIC_SECTION(method.toStdString() << " failure=" << failure) {
                ModifierFixture fixture;
                if (method == "modifier.apply")
                    REQUIRE(fixture.model.setMirrorOptions(fixture.entity, Mirror{}));
                const auto other = prepareRedo(fixture);
                if (failure == 12)
                    fixture.service.setExternalBusy(QStringLiteral("test_modifier_pending"), true);
                if (failure == 13) {
                    fixture.model.selection()->setSelectedEntity(fixture.entity);
                    REQUIRE(fixture.model.setEditMode(true));
                }
                auto params = fixture.params(method);
                auto decoded = api::ApiJsonCodec::decodeRequest(method, params);
                REQUIRE(decoded.hasValue());
                std::visit(
                    [&](auto& request) {
                        using Request = std::decay_t<decltype(request)>;
                        if constexpr (std::is_base_of_v<api::MeshMutationRequest, Request>) {
                            switch (failure) {
                                case 0:
                                    request.timeoutMs = 0;
                                    params.insert("timeoutMs", 0);
                                    break;
                                case 1:
                                    request.mutationSequence = 0;
                                    params.insert("mutationSequence", "0");
                                    break;
                                case 2:
                                    request.clientSessionId = QStringLiteral("invalid-session");
                                    params.insert("clientSessionId", *request.clientSessionId);
                                    break;
                                case 3:
                                    request.document.documentId =
                                        QStringLiteral("33333333-3333-4333-8333-333333333333");
                                    params.insert("document", api::ApiJsonCodec::encodeState(
                                                                  {request.document, 0, 0})
                                                                  .value("document"));
                                    break;
                                case 4:
                                    --request.expectedDocumentRevision;
                                    params.insert(
                                        "expectedDocumentRevision",
                                        QString::number(request.expectedDocumentRevision));
                                    break;
                                case 5:
                                    --request.expectedTopologyRevision;
                                    params.insert(
                                        "expectedTopologyRevision",
                                        QString::number(request.expectedTopologyRevision));
                                    break;
                                case 6:
                                    --request.expectedGeometryRevision;
                                    params.insert(
                                        "expectedGeometryRevision",
                                        QString::number(request.expectedGeometryRevision));
                                    break;
                                case 7:
                                    request.entityId = 0;
                                    params.insert("entityId", "0");
                                    break;
                                case 8:
                                    request.meshId = 0;
                                    params.insert("meshId", "0");
                                    break;
                                case 9:
                                    request.entityId = std::numeric_limits<core::EntityId>::max();
                                    params.insert("entityId", QString::number(request.entityId));
                                    break;
                                case 10:
                                    request.meshId = std::numeric_limits<core::MeshId>::max();
                                    params.insert("meshId", QString::number(request.meshId));
                                    break;
                                case 11:
                                    request.entityId = other;
                                    params.insert("entityId", QString::number(other));
                                    break;
                            }
                        }
                    },
                    *decoded.value);
                const auto before = remember(fixture);
                int guards = 0;
                const auto guard = [&]() -> std::optional<api::ApiError> {
                    ++guards;
                    unchanged(fixture, before);
                    return api::ApiError{api::ErrorCode::Cancelled,
                                         QStringLiteral("最终许可拒绝"),
                                         {},
                                         api::Recovery::None,
                                         fixture.service.documentState()};
                };
                fixture.service.exchangeBeforeCommitGuard(guard);
                checkError(fixture, dispatch(fixture, *decoded.value), errors[std::size_t(failure)],
                           before);
                fixture.service.exchangeBeforeCommitGuard({});
                checkWireError(fixture, method, params, errors[std::size_t(failure)], before,
                               guard);
                REQUIRE(guards == (failure == 14 ? 2 : 0));
            }
        }
}

TEST_CASE(
    "Modifier writes reject legal GUI sources over the shared source budget before evaluation",
    "[modifier-api][modifier-budget]") {
    for (const QString method :
         {"modifier.setMirror", "modifier.setSubdivision", "modifier.apply"}) {
        DYNAMIC_SECTION(method.toStdString()) {
            ModifierFixture fixture;
            Mirror mirror;
            mirror.enabled = false;
            REQUIRE(fixture.model.setMirrorOptions(fixture.entity, mirror));
            auto source = fixture.content().source;
            for (core::modeling::VertexId id = source.vertices.back().id + 1;
                 source.vertices.size() <= api::limits::meshVertices; ++id)
                source.vertices.push_back({id, glm::vec3(0)});
            REQUIRE(core::modeling::validateEditableMesh(source).isValid());
            REQUIRE(fixture.model.replaceEditableMesh(fixture.entity, source));
            prepareRedo(fixture);
            const auto before = remember(fixture);
            const auto params = fixture.params(method);
            const auto decoded = api::ApiJsonCodec::decodeRequest(method, params);
            REQUIRE(decoded.hasValue());
            int guards = 0;
            const auto guard = [&]() -> std::optional<api::ApiError> {
                ++guards;
                return std::nullopt;
            };
            fixture.service.exchangeBeforeCommitGuard(guard);
            checkError(fixture, dispatch(fixture, *decoded.value), api::ErrorCode::LimitExceeded,
                       before);
            fixture.service.exchangeBeforeCommitGuard({});
            checkWireError(fixture, method, params, api::ErrorCode::LimitExceeded, before, guard);
            REQUIRE(guards == 0);
        }
    }
}

TEST_CASE("Modifier proposed-chain and applied-source budgets reject expansion before final guard",
          "[modifier-api][modifier-budget]") {
    for (int operation = 0; operation < 4; ++operation) {
        DYNAMIC_SECTION("operation=" << operation) {
            ModifierFixture fixture;
            fixture.bakeSubdivision(2);
            fixture.bakeSubdivision(2);
            if (operation == 0)
                fixture.bakeSubdivision(1);
            if (operation >= 2) {
                REQUIRE(fixture.model.setMirrorOptions(fixture.entity, Mirror{}));
                REQUIRE(fixture.model.setSubdivisionOptions(fixture.entity, Subdivision{true, 2}));
            }
            REQUIRE(fixture.content().source.vertices.size() <= api::limits::meshVertices);
            REQUIRE(fixture.content().source.faces.size() <= api::limits::meshFaces);
            prepareRedo(fixture);
            const auto before = remember(fixture);
            const auto method = operation == 0   ? "modifier.setMirror"
                                : operation == 1 ? "modifier.setSubdivision"
                                                 : "modifier.apply";
            auto params = fixture.params(method);
            if (operation == 1)
                params.insert("options", subdivisionJson(Subdivision{true, 2}));
            if (operation == 3)
                params.insert("modifier", "subdivision");
            const auto decoded = api::ApiJsonCodec::decodeRequest(method, params);
            REQUIRE(decoded.hasValue());
            int guards = 0;
            const auto guard = [&]() -> std::optional<api::ApiError> {
                ++guards;
                return std::nullopt;
            };
            fixture.service.exchangeBeforeCommitGuard(guard);
            checkError(fixture, dispatch(fixture, *decoded.value), api::ErrorCode::LimitExceeded,
                       before);
            fixture.service.exchangeBeforeCommitGuard({});
            checkWireError(fixture, method, params, api::ErrorCode::LimitExceeded, before, guard);
            REQUIRE(guards == 0);
        }
    }
}

TEST_CASE("Proposed modifier budget overload preserves existing content-based callers",
          "[modifier-api][modifier-budget]") {
    ModifierFixture fixture;
    core::EditableMeshContent proposed;
    proposed.source = fixture.content().source;
    for (const bool enabled : {false, true}) {
        Mirror mirror;
        mirror.enabled = enabled;
        mirror.threshold = enabled ? 0 : 1e300;
        proposed.mirror = mirror;
        proposed.subdivision = Subdivision{enabled, 2};
        const auto old = api::checkMeshCandidateBudget(proposed.source, &proposed,
                                                       fixture.service.documentState());
        const auto next =
            api::checkMeshCandidateBudget(proposed.source, proposed.mirror, proposed.subdivision,
                                          fixture.service.documentState());
        REQUIRE_FALSE(old.has_value());
        REQUIRE_FALSE(next.has_value());
    }
}
