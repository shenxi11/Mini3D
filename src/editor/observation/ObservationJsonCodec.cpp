/*
 * 模块名: ObservationJsonCodec
 * 功能概述: 实施冻结的观察 JSON 合同，并编码实际绘制状态和 PNG。
 * 对外接口: ObservationJsonCodec.h。
 * 依赖关系: ApiJsonCodec、Qt JSON；不访问窗口、场景或 GPU。
 * 输入输出: 严格 JSON 参数到 DTO，实际观察结果到列主序矩阵和图像元数据。
 * 异常与错误: 字段、数值或身份无效时拒绝整个请求。
 * 维护说明: 限额引用生成的 ApiLimits；uint64 只编码成十进制字符串。
 */
#include "ObservationJsonCodec.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>

namespace mini3d::editor::observation {
namespace {
struct Decoder {
    std::optional<api::ApiError> error;

    void fail(const QString& field, const QString& message) {
        if (!error)
            error = api::ApiError{api::ErrorCode::InvalidArgument, message, field,
                                  api::Recovery::CorrectInput,     {},      -32602};
    }
    QJsonObject object(const QJsonValue& value, const QStringList& allowed,
                       const QStringList& required, const QString& path = {}) {
        if (!value.isObject()) {
            fail(path, QStringLiteral("须为对象。"));
            return {};
        }
        const auto result = value.toObject();
        const auto fieldPath = [&](const QString& field) {
            return path.isEmpty() ? field : path + "." + field;
        };
        for (auto entry = result.begin(); entry != result.end(); ++entry)
            if (!allowed.contains(entry.key()))
                fail(fieldPath(entry.key()), QStringLiteral("不接受未知字段。"));
        for (const auto& field : required)
            if (!result.contains(field))
                fail(fieldPath(field), QStringLiteral("缺少必填字段。"));
        return result;
    }
    std::uint64_t uint64(const QJsonValue& value, const QString& field, bool allowZero = true) {
        const auto parsed = api::ApiJsonCodec::parseUint64(value, field, allowZero);
        if (!parsed.hasValue()) {
            if (!error)
                error = parsed.error;
            return 0;
        }
        return *parsed.value;
    }
    int integer(const QJsonValue& value, const QString& field, int maximum) {
        const auto number = value.toDouble();
        if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number ||
            number < 1 || number > maximum) {
            fail(field, QStringLiteral("整数超出允许范围。"));
            return 1;
        }
        return int(number);
    }
    float number(const QJsonValue& value, const QString& field, double minimum) {
        const auto number = value.toDouble();
        if (!value.isDouble() || !std::isfinite(number) || number < minimum ||
            number > double(std::numeric_limits<float>::max())) {
            fail(field, QStringLiteral("须为允许范围内的有限 float 数值。"));
            return 0;
        }
        return float(number);
    }
    glm::vec3 vector(const QJsonValue& value, const QString& field) {
        if (!value.isArray() || value.toArray().size() != 3) {
            fail(field, QStringLiteral("须为三个数值组成的数组。"));
            return {};
        }
        const auto values = value.toArray();
        glm::vec3 result;
        for (int index = 0; index < 3; ++index)
            result[index] = number(values[index], field + "[" + QString::number(index) + "]",
                                   -double(std::numeric_limits<float>::max()));
        return result;
    }
    bool boolean(const QJsonValue& value, const QString& field) {
        if (!value.isBool())
            fail(field, QStringLiteral("须为布尔值。"));
        return value.toBool();
    }
    int enumeration(const QJsonValue& value, const QString& field, const QStringList& names) {
        const int index = value.isString() ? int(names.indexOf(value.toString())) : -1;
        if (index < 0)
            fail(field, QStringLiteral("枚举值无效。"));
        return std::max(index, 0);
    }
    api::DocumentHandle document(const QJsonObject& object) {
        // 复用公共边界对文档嵌套字段和 UUID 的严格校验。
        const auto parsed = api::ApiJsonCodec::decodeRequest(
            QStringLiteral("scene.getSummary"), QJsonObject{{"document", object["document"]}});
        if (!parsed.hasValue()) {
            if (!error)
                error = parsed.error;
            return {};
        }
        return std::get<api::DocumentRequest>(*parsed.value).document;
    }
    api::MutationRequest mutation(const QJsonObject& object) {
        api::MutationRequest result;
        result.document = document(object);
        result.expectedDocumentRevision =
            uint64(object["expectedDocumentRevision"], "expectedDocumentRevision");
        if (object.contains("timeoutMs"))
            result.timeoutMs = integer(object["timeoutMs"], "timeoutMs",
                                       int(api::limits::mutationTimeoutMaximumMs));
        if (object.contains("mutationSequence"))
            result.mutationSequence = uint64(object["mutationSequence"], "mutationSequence", false);
        if (object.contains("clientSessionId")) {
            static const QRegularExpression uuid(QStringLiteral(
                "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
            const auto value = object["clientSessionId"];
            if (!value.isString() || !uuid.match(value.toString()).hasMatch())
                fail("clientSessionId", QStringLiteral("会话身份须为 UUID。"));
            result.clientSessionId = value.toString().toLower();
        }
        return result;
    }
};
const QStringList& presets() {
    static const QStringList names{"orbit", "front", "right", "top", "back", "left", "bottom"};
    return names;
}
const QStringList& shadingModes() {
    static const QStringList names{"material", "solid", "wireframe"};
    return names;
}
QJsonArray vectorJson(const glm::vec3& value) {
    return {value.x, value.y, value.z};
}
QJsonArray matrixJson(const glm::mat4& value) {
    QJsonArray result;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            result.append(value[column][row]);
    return result;
}
QJsonObject sizeJson(const QSize& value) {
    return {{"width", value.width()}, {"height", value.height()}};
}
QJsonArray idsJson(const std::vector<core::EntityId>& values) {
    QJsonArray result;
    for (const auto value : values)
        result.append(QString::number(value));
    return result;
}
} // namespace

api::ApiResult<ObservationRequest> ObservationJsonCodec::decodeRequest(const QString& method,
                                                                       const QJsonValue& params) {
    using Result = api::ApiResult<ObservationRequest>;
    Decoder decoder;
    QStringList allowed{"document"}, required{"document"};
    const bool control = method == "viewport.setView" || method == "viewport.focus";
    if (control || method == "viewport.capture") {
        allowed.append({"expectedDocumentRevision", "expectedViewportRevision", "timeoutMs"});
        required.append({"expectedDocumentRevision", "expectedViewportRevision"});
    }
    if (control)
        allowed.append({"clientSessionId", "mutationSequence"});
    if (method == "viewport.setView")
        allowed.append({"preset", "orthographic", "camera", "shading", "overlays", "xRay"});
    else if (method == "viewport.focus") {
        allowed.append("entityIds");
        required.append("entityIds");
    } else if (method == "viewport.capture") {
        allowed.append("longestEdge");
    } else if (method != "viewport.getState") {
        return Result::failure({api::ErrorCode::UnsupportedOperation,
                                QStringLiteral("未知观察方法。"),
                                "method",
                                api::Recovery::None,
                                {},
                                -32601});
    }
    const auto object = decoder.object(params, allowed, required);
    ObservationRequest request;
    if (method == "viewport.getState") {
        request = ViewRequest{decoder.document(object)};
    } else if (method == "viewport.capture") {
        CaptureRequest capture;
        capture.document = decoder.document(object);
        capture.expectedDocumentRevision =
            decoder.uint64(object["expectedDocumentRevision"], "expectedDocumentRevision");
        capture.expectedViewportRevision =
            decoder.uint64(object["expectedViewportRevision"], "expectedViewportRevision");
        if (object.contains("longestEdge"))
            capture.longestEdge = decoder.integer(object["longestEdge"], "longestEdge",
                                                  int(api::limits::captureLongestEdge));
        if (object.contains("timeoutMs"))
            capture.timeoutMs = decoder.integer(object["timeoutMs"], "timeoutMs",
                                                int(api::limits::captureTimeoutMaximumMs));
        request = std::move(capture);
    } else if (method == "viewport.focus") {
        FocusRequest focus;
        static_cast<api::MutationRequest&>(focus) = decoder.mutation(object);
        focus.expectedViewportRevision =
            decoder.uint64(object["expectedViewportRevision"], "expectedViewportRevision");
        const auto ids = object["entityIds"];
        if (!ids.isArray() || ids.toArray().isEmpty()) {
            decoder.fail("entityIds", QStringLiteral("须提供非空实体数组。"));
        } else if (std::size_t(ids.toArray().size()) > api::limits::batchItems) {
            if (!decoder.error)
                decoder.error = api::ApiError{api::ErrorCode::LimitExceeded,
                                              QStringLiteral("实体数量超出限额。"),
                                              "entityIds",
                                              api::Recovery::CorrectInput,
                                              {}};
        } else {
            std::set<core::EntityId> seen;
            for (const auto& value : ids.toArray()) {
                const auto id = decoder.uint64(value, "entityIds", false);
                if (!seen.insert(id).second)
                    decoder.fail("entityIds", QStringLiteral("实体数组不接受重复 ID。"));
                focus.entityIds.push_back(id);
            }
        }
        request = std::move(focus);
    } else {
        SetViewRequest view;
        static_cast<api::MutationRequest&>(view) = decoder.mutation(object);
        view.expectedViewportRevision =
            decoder.uint64(object["expectedViewportRevision"], "expectedViewportRevision");
        auto& changes = view.changes;
        if (object.contains("preset"))
            changes.preset =
                renderer_gl::EditorView(decoder.enumeration(object["preset"], "preset", presets()));
        if (object.contains("shading"))
            changes.shading = renderer_gl::ViewportShading(
                decoder.enumeration(object["shading"], "shading", shadingModes()));
        if (object.contains("orthographic"))
            changes.orthographic = decoder.boolean(object["orthographic"], "orthographic");
        if (object.contains("overlays"))
            changes.overlays = decoder.boolean(object["overlays"], "overlays");
        if (object.contains("xRay"))
            changes.xRay = decoder.boolean(object["xRay"], "xRay");
        if (object.contains("camera")) {
            const auto camera = decoder.object(
                object["camera"], {"position", "target", "focusRadius", "maximumDistance"},
                {"position", "target"}, "camera");
            core::CameraState state;
            state.position = decoder.vector(camera["position"], "camera.position");
            state.target = decoder.vector(camera["target"], "camera.target");
            if (camera.contains("focusRadius"))
                state.focusRadius = decoder.number(camera["focusRadius"], "camera.focusRadius", 0);
            if (camera.contains("maximumDistance"))
                state.maximumDistance =
                    decoder.number(camera["maximumDistance"], "camera.maximumDistance", .6);
            if (!state.isValid())
                decoder.fail("camera", QStringLiteral("相机状态不满足现有观察约束。"));
            if (changes.preset && *changes.preset != renderer_gl::EditorView::Orbit)
                decoder.fail("preset", QStringLiteral("自定义相机必须使用 orbit。"));
            changes.camera = state;
        }
        if (!changes.preset && !changes.shading && !changes.camera && !changes.orthographic &&
            !changes.overlays && !changes.xRay)
            decoder.fail({}, QStringLiteral("至少提供一个显示更新字段。"));
        request = std::move(view);
    }
    if (decoder.error)
        return Result::failure(*decoder.error);
    return Result::success(std::move(request));
}

api::ApiResult<QJsonObject> ObservationJsonCodec::canonicalParams(const QString& method,
                                                                  const QJsonValue& params) {
    const auto decoded = decodeRequest(method, params);
    if (decoded.error)
        return api::ApiResult<QJsonObject>::failure(*decoded.error);
    const auto object = std::visit(
        [](const auto& request) {
            using T = std::decay_t<decltype(request)>;
            QJsonObject result{
                {"document", QJsonObject{{"instanceId", request.document.instanceId},
                                         {"documentId", request.document.documentId}}}};
            if constexpr (!std::is_same_v<T, ViewRequest>) {
                result.insert("expectedDocumentRevision",
                              QString::number(request.expectedDocumentRevision));
                result.insert("expectedViewportRevision",
                              QString::number(request.expectedViewportRevision));
                result.insert("timeoutMs", request.timeoutMs);
            }
            if constexpr (std::is_same_v<T, CaptureRequest>)
                result.insert("longestEdge", request.longestEdge);
            if constexpr (std::is_same_v<T, FocusRequest>)
                result.insert("entityIds", idsJson(request.entityIds));
            if constexpr (std::is_same_v<T, SetViewRequest>) {
                const auto& changes = request.changes;
                if (changes.camera) {
                    const auto& camera = *changes.camera;
                    result.insert("camera",
                                  QJsonObject{{"position", vectorJson(camera.position)},
                                              {"target", vectorJson(camera.target)},
                                              {"focusRadius", camera.focusRadius},
                                              {"maximumDistance", camera.maximumDistance}});
                }
                if (changes.preset)
                    result.insert("preset", presets()[int(*changes.preset)]);
                if (changes.shading)
                    result.insert("shading", shadingModes()[int(*changes.shading)]);
                if (changes.orthographic)
                    result.insert("orthographic", *changes.orthographic);
                if (changes.overlays)
                    result.insert("overlays", *changes.overlays);
                if (changes.xRay)
                    result.insert("xRay", *changes.xRay);
            }
            return result;
        },
        *decoded.value);
    return api::ApiResult<QJsonObject>::success(object);
}

QJsonObject ObservationJsonCodec::encode(const ViewState& result) {
    const auto& state = result.viewport;
    const auto& view = state.view;
    auto object =
        api::ApiJsonCodec::encodeState({{state.document.instanceId, state.document.documentId},
                                        state.document.documentRevision,
                                        state.document.historyRevision});
    const auto& visibility = state.visibility;
    object.insert("viewportRevision", QString::number(state.viewportRevision));
    object.insert("preset", view.previewCamera != 0 ? QStringLiteral("sceneCamera")
                                                    : presets()[int(view.preset)]);
    object.insert("projectionMode", view.orthographic ? "orthographic" : "perspective");
    object.insert("position", vectorJson(view.position));
    object.insert("target", vectorJson(view.target));
    object.insert("forward", vectorJson(view.forward));
    object.insert("up", vectorJson(view.up));
    object.insert("viewMatrix", matrixJson(view.viewMatrix));
    object.insert("projectionMatrix", matrixJson(view.projectionMatrix));
    object.insert("previewCameraId", QString::number(view.previewCamera));
    object.insert("shading", shadingModes()[int(state.shading)]);
    object.insert("overlays", state.overlays);
    object.insert("xRay", state.xRay);
    object.insert("logicalSize", sizeJson(state.logicalSize));
    object.insert("pixelSize", sizeJson(state.pixelSize));
    object.insert("devicePixelRatio", state.devicePixelRatio);
    object.insert("visibility",
                  QJsonObject{{"hiddenEntityIds", idsJson(visibility.hiddenEntityIds)},
                              {"isolatedEntityIds", idsJson(visibility.isolatedEntityIds)},
                              {"editedEntityId", QString::number(visibility.editedEntityId)},
                              {"hiddenVertexCount", qint64(visibility.hiddenVertexCount)},
                              {"hiddenEdgeCount", qint64(visibility.hiddenEdgeCount)},
                              {"hiddenFaceCount", qint64(visibility.hiddenFaceCount)}});
    return object;
}
QJsonObject ObservationJsonCodec::encode(const ViewCommandResult& result) {
    return {{"status", result.status == api::ResultStatus::Committed ? "committed" : "no_change"},
            {"selectionChanged", false},
            {"view", encode(result.view)}};
}
QJsonObject ObservationJsonCodec::encode(const CaptureResult& result) {
    return {{"status", "captured"},
            {"captureId", result.captureId},
            {"frameId", QString::number(result.frameId)},
            {"contextGeneration", QString::number(result.contextGeneration)},
            {"view", encode(result.view)},
            {"originalPixelSize", sizeJson(result.originalPixelSize)},
            {"outputPixelSize", sizeJson(result.outputPixelSize)},
            {"mimeType", "image/png"},
            {"byteLength", qint64(result.png.size())},
            {"sha256", result.sha256},
            {"overlayIncluded", result.overlayIncluded},
            {"capturedRegion", "gl_viewport"},
            {"pngBase64", QString::fromLatin1(result.png.toBase64())}};
}
} // namespace mini3d::editor::observation
