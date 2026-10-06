/*
 * 模块名: ApiJsonCodec
 * 功能概述: 实施 api/schema 冻结的 M1/M2 边界，集中校验字段和数值。
 * 对外接口: ApiJsonCodec.h。
 * 依赖关系: Qt JSON、ApiTypes、EditorApiService。
 * 输入输出: canonical uint64 字符串/local TRS 到业务 DTO，再编码统一结果。
 * 异常与错误: Schema 失败使用 -32602，业务失败保留结构化 code/当前版本。
 * 维护说明: 不做中文提示反向解析，不维护另一份数学类型或场景状态。
 */
#include "ApiJsonCodec.h"

#include "EditorApiService.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <set>
#include <cmath>
#include <limits>
#include <type_traits>

namespace mini3d::editor::api {
namespace {
ApiError invalid(const QString& path, const QString& message) {
    return {ErrorCode::InvalidArgument, message, path, Recovery::CorrectInput, {}, -32602};
}
QString childPath(const QString& parent, const QString& field) {
    return parent.isEmpty() ? field : parent + "." + field;
}
bool isUuid(const QString& value) {
    static const QRegularExpression pattern(QStringLiteral(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return pattern.match(value).hasMatch();
}
struct Decoder {
    std::optional<ApiError> error;
    void fail(const QString& path, const QString& message) {
        if (!error)
            error = invalid(path, message);
    }
    void limit(const QString& path) {
        if (!error)
            error = ApiError{ErrorCode::LimitExceeded, QStringLiteral("数组数量超出 API 限额。"),
                              path, Recovery::CorrectInput, {}};
    }
    QJsonArray array(const QJsonValue& value, int minimum, std::size_t maximum, const QString& path) {
        if (!value.isArray() || value.toArray().size() < minimum) {
            fail(path, QStringLiteral("数组类型或数量无效。"));
            return {};
        }
        if (std::size_t(value.toArray().size()) > maximum) {
            limit(path);
            return {};
        }
        return value.toArray();
    }
    double number(const QJsonValue& value, const QString& path) {
        const auto result = value.toDouble();
        if (!value.isDouble() || !std::isfinite(result) ||
            std::abs(result) > double(std::numeric_limits<float>::max())) {
            fail(path, QStringLiteral("须为 float 范围的有限数值。"));
            return 0;
        }
        return result;
    }
    QJsonObject object(const QJsonValue& value, const QStringList& allowed,
                       const QStringList& required, const QString& path) {
        if (!value.isObject()) {
            fail(path, QStringLiteral("须为对象。"));
            return {};
        }
        const auto result = value.toObject();
        for (auto field = result.begin(); field != result.end(); ++field) {
            if (!allowed.contains(field.key()))
                fail(childPath(path, field.key()), QStringLiteral("不接受未知字段。"));
        }
        for (const auto& field : required) {
            if (!result.contains(field))
                fail(childPath(path, field), QStringLiteral("缺少必填字段。"));
        }
        return result;
    }
    QString string(const QJsonValue& value, const QString& path, int minimum = 0,
                   int maximum = std::numeric_limits<int>::max()) {
        if (!value.isString()) {
            fail(path, QStringLiteral("须为字符串。"));
            return {};
        }
        const auto result = value.toString();
        const auto length = result.toUcs4().size();
        if (length < minimum || length > maximum)
            fail(path, QStringLiteral("字符串长度超出允许范围。"));
        return result;
    }
    bool boolean(const QJsonValue& value, const QString& path) {
        if (!value.isBool())
            fail(path, QStringLiteral("须为布尔值。"));
        return value.toBool();
    }
    int integer(const QJsonValue& value, const QString& path, int minimum, int maximum) {
        const auto number = value.toDouble();
        if (!value.isDouble() || !std::isfinite(number) || std::floor(number) != number ||
            number < minimum || number > maximum) {
            fail(path, QStringLiteral("整数超出允许范围。"));
            return minimum;
        }
        return static_cast<int>(number);
    }
    std::uint64_t uint64(const QJsonValue& value, const QString& path, bool zero = true) {
        const auto result = ApiJsonCodec::parseUint64(value, path, zero);
        if (!result.hasValue()) {
            if (!error)
                error = result.error;
            return 0;
        }
        return *result.value;
    }
    std::vector<float> numbers(const QJsonValue& value, int count, const QString& path,
                               bool color = false, bool scale = false) {
        std::vector<float> result(std::size_t(count), 0);
        if (!value.isArray() || value.toArray().size() != count) {
            fail(path, QStringLiteral("数组长度不匹配。"));
            return result;
        }
        const auto array = value.toArray();
        for (int index = 0; index < count; ++index) {
            const auto number = array[index].toDouble();
            const auto itemPath = path + "[" + QString::number(index) + "]";
            if (!array[index].isDouble() || !std::isfinite(number) ||
                std::abs(number) > double(std::numeric_limits<float>::max())) {
                fail(itemPath, QStringLiteral("须为可表示为 float 的有限数值。"));
                continue;
            }
            if ((color && (number < 0 || number > 1)) || (scale && std::abs(number) < 0.001)) {
                fail(itemPath, color ? QStringLiteral("颜色须在 0～1 之间。")
                                     : QStringLiteral("缩放绝对值不得小于 0.001。"));
                continue;
            }
            const auto converted = static_cast<float>(number);
            if (!std::isfinite(converted)) {
                fail(itemPath, QStringLiteral("float 转换溢出。"));
                continue;
            }
            result[std::size_t(index)] = converted;
        }
        return result;
    }
    DocumentHandle document(const QJsonValue& value, const QString& path = QStringLiteral("document")) {
        const auto objectValue =
            object(value, {"instanceId", "documentId"}, {"instanceId", "documentId"}, path);
        const auto instance = string(objectValue["instanceId"], path + ".instanceId");
        const auto documentId = string(objectValue["documentId"], path + ".documentId");
        if (!isUuid(instance))
            fail(path + ".instanceId", QStringLiteral("须为 UUID。"));
        if (!isUuid(documentId))
            fail(path + ".documentId", QStringLiteral("须为 UUID。"));
        return {instance.toLower(), documentId.toLower()};
    }
    core::SurfaceStyle surface(const QJsonValue& value,
                               const QString& path = QStringLiteral("surface")) {
        const auto objectValue = object(value, {"tint", "useVertexColor", "useTexture"},
                                        {"tint", "useVertexColor", "useTexture"}, path);
        const auto tint = numbers(objectValue["tint"], 3, path + ".tint", true);
        core::SurfaceStyle result;
        result.tint = {tint[0], tint[1], tint[2]};
        result.useVertexColor = boolean(objectValue["useVertexColor"], path + ".useVertexColor");
        result.useTexture = boolean(objectValue["useTexture"], path + ".useTexture");
        return result;
    }
    core::CameraComponent camera(const QJsonValue& value) {
        const auto objectValue = object(value, {"fieldOfView", "nearPlane", "farPlane"},
                                        {"fieldOfView", "nearPlane", "farPlane"}, "camera");
        const auto fieldOfView = number(objectValue["fieldOfView"], "camera.fieldOfView");
        const auto nearPlane = number(objectValue["nearPlane"], "camera.nearPlane");
        const auto farPlane = number(objectValue["farPlane"], "camera.farPlane");
        if (fieldOfView < 1 || fieldOfView > 179)
            fail("camera.fieldOfView", QStringLiteral("视角须在 1～179 度之间。"));
        if (nearPlane < 0.001 || nearPlane > 1000000)
            fail("camera.nearPlane", QStringLiteral("近裁剪须在 0.001～1000000 之间。"));
        if (farPlane <= nearPlane || farPlane > 1000000)
            fail("camera.farPlane", QStringLiteral("远裁剪须大于近裁剪且不超过 1000000。"));
        const core::CameraComponent result{float(fieldOfView), float(nearPlane), float(farPlane)};
        if (!result.isValid())
            fail("camera", QStringLiteral("相机参数转换为 float 后无效。"));
        return result;
    }
    core::LightComponent light(const QJsonValue& value) {
        const auto objectValue =
            object(value, {"color", "intensity"}, {"color", "intensity"}, "light");
        const auto color = numbers(objectValue["color"], 3, "light.color", true);
        const auto intensity = number(objectValue["intensity"], "light.intensity");
        if (intensity < 0 || intensity > 10)
            fail("light.intensity", QStringLiteral("光源强度须在 0～10 之间。"));
        return {{color[0], color[1], color[2]}, float(intensity)};
    }
    core::Transform transform(const QJsonValue& value,
                              const QString& path = QStringLiteral("transform")) {
        const auto result = ApiJsonCodec::parseTransform(value, path);
        if (!result.hasValue()) {
            if (!error)
                error = result.error;
            return {};
        }
        return *result.value;
    }
    MutationRequest mutation(const QJsonObject& params) {
        MutationRequest result;
        result.document = document(params["document"]);
        result.expectedDocumentRevision =
            uint64(params["expectedDocumentRevision"], "expectedDocumentRevision");
        if (params.contains("clientSessionId")) {
            const auto session = string(params["clientSessionId"], "clientSessionId");
            if (!isUuid(session))
                fail("clientSessionId", QStringLiteral("须为 UUID。"));
            result.clientSessionId = session.toLower();
        }
        if (params.contains("mutationSequence"))
            result.mutationSequence = uint64(params["mutationSequence"], "mutationSequence", false);
        if (params.contains("timeoutMs"))
            result.timeoutMs = integer(params["timeoutMs"], "timeoutMs", 1, int(limits::mutationTimeoutMaximumMs));
        return result;
    }
    MeshDomain domain(const QJsonValue& value, const QString& path) {
        const auto name = string(value, path);
        if (name != "vertices" && name != "faces")
            fail(path, QStringLiteral("分页域须为 vertices/faces。"));
        return name == "faces" ? MeshDomain::Faces : MeshDomain::Vertices;
    }
    std::vector<MeshField> fields(const QJsonValue& value, MeshDomain domainValue, const QString& path) {
        std::vector<MeshField> result;
        if (!value.isArray() || value.toArray().size() > 1) {
            fail(path, QStringLiteral("字段集合须为空或包含当前域唯一支持的字段。"));
            return result;
        }
        for (const auto& field : value.toArray()) {
            const auto name = string(field, path);
            const auto expected = domainValue == MeshDomain::Vertices ? "position" : "cornerAttributes";
            if (name != expected)
                fail(path, QStringLiteral("字段不属于当前分页域。"));
            result.push_back(domainValue == MeshDomain::Vertices ? MeshField::Position : MeshField::CornerAttributes);
        }
        return result;
    }
    MeshSourceCursor cursor(const QJsonValue& value) {
        const QStringList keys{"document", "documentRevision", "entityId", "meshId", "topologyRevision",
                               "geometryRevision", "domain", "fields", "afterId"};
        const auto params = object(value, keys, keys, "cursor");
        MeshSourceCursor result;
        result.document = document(params["document"], "cursor.document");
        result.documentRevision = uint64(params["documentRevision"], "cursor.documentRevision");
        result.entityId = uint64(params["entityId"], "cursor.entityId", false);
        result.meshId = uint64(params["meshId"], "cursor.meshId", false);
        result.topologyRevision = uint64(params["topologyRevision"], "cursor.topologyRevision");
        result.geometryRevision = uint64(params["geometryRevision"], "cursor.geometryRevision");
        result.domain = domain(params["domain"], "cursor.domain");
        result.fields = fields(params["fields"], result.domain, "cursor.fields");
        result.afterId = uint64(params["afterId"], "cursor.afterId", false);
        return result;
    }
    MeshMutationRequest meshMutation(const QJsonObject& params) {
        MeshMutationRequest result;
        static_cast<MutationRequest&>(result) = mutation(params);
        result.entityId = uint64(params["entityId"], "entityId", false);
        result.meshId = uint64(params["meshId"], "meshId", false);
        result.expectedTopologyRevision = uint64(params["expectedTopologyRevision"], "expectedTopologyRevision");
        result.expectedGeometryRevision = uint64(params["expectedGeometryRevision"], "expectedGeometryRevision");
        return result;
    }
    core::modeling::EdgeKey edge(const QJsonValue& value, const QString& path) {
        const auto endpoints = array(value, 2, 2, path);
        if (endpoints.size() != 2)
            return {0, 0};
        const auto first = uint64(endpoints[0], path + "[0]", false);
        const auto second = uint64(endpoints[1], path + "[1]", false);
        if (first == second)
            fail(path, QStringLiteral("源边须包含两个不同端点。"));
        return {first, second};
    }
    std::vector<std::uint64_t> componentIds(const QJsonValue& value, const QString& path) {
        const auto values = array(value, 1, limits::meshVertices, path);
        std::set<std::uint64_t> distinct;
        for (const auto& item : values) {
            const auto id = uint64(item, path, false);
            if (!distinct.insert(id).second)
                fail(path, QStringLiteral("组件 ID 不能重复。"));
        }
        return {distinct.begin(), distinct.end()};
    }
    MeshComponentsRequest components(const QJsonObject& params, bool allowFaces) {
        MeshComponentsRequest result;
        static_cast<MeshMutationRequest&>(result) = meshMutation(params);
        const auto name = string(params["domain"], "domain");
        if (name != "vertices" && name != "edges" && !(allowFaces && name == "faces"))
            fail("domain", QStringLiteral("组件域须为此算子支持的 vertices/edges/faces。"));
        result.domain = name == "edges"   ? MeshComponentDomain::Edges
                        : name == "faces" ? MeshComponentDomain::Faces
                                          : MeshComponentDomain::Vertices;
        const auto expected = name == "edges"   ? QStringLiteral("edges")
                              : name == "faces" ? QStringLiteral("faceIds")
                                                : QStringLiteral("vertexIds");
        for (const auto& key :
             {QStringLiteral("vertexIds"), QStringLiteral("edges"), QStringLiteral("faceIds")}) {
            if (key != expected && params.contains(key))
                fail(key, QStringLiteral("只允许当前组件域的目标字段。"));
        }
        if (name == "edges") {
            const auto values = array(params["edges"], 1, limits::meshVertices, "edges");
            std::set<core::modeling::EdgeKey> distinct;
            for (const auto& value : values) {
                const auto parsed = edge(value, "edges");
                if (!distinct.insert(parsed).second)
                    fail("edges", QStringLiteral("无向边不能重复，包括反向端点。"));
            }
            result.edges.emplace(distinct.begin(), distinct.end());
        } else if (name == "faces") {
            result.faceIds = componentIds(params["faceIds"], "faceIds");
        } else {
            result.vertexIds = componentIds(params["vertexIds"], "vertexIds");
        }
        return result;
    }
    std::optional<core::modeling::MirrorOptions> mirrorOptions(const QJsonValue& value) {
        if (value.isNull())
            return std::nullopt;
        const QStringList fields{"axis", "enabled", "merge", "clipping", "threshold"};
        const auto params = object(value, fields, fields, "options");
        core::modeling::MirrorOptions result;
        const auto axis = string(params["axis"], "options.axis");
        if (axis != "x" && axis != "y" && axis != "z")
            fail("options.axis", QStringLiteral("镜像仅支持单局部轴 x/y/z。"));
        result.axis = axis == "y"   ? core::modeling::MirrorAxis::Y
                      : axis == "z" ? core::modeling::MirrorAxis::Z
                                    : core::modeling::MirrorAxis::X;
        result.enabled = boolean(params["enabled"], "options.enabled");
        result.merge = boolean(params["merge"], "options.merge");
        result.clipping = boolean(params["clipping"], "options.clipping");
        result.threshold = params["threshold"].toDouble();
        if (!params["threshold"].isDouble() || !result.isValid())
            fail("options.threshold", QStringLiteral("阈值须为有限非负 double。"));
        return result;
    }
    std::optional<core::modeling::SubdivisionOptions> subdivisionOptions(const QJsonValue& value) {
        if (value.isNull())
            return std::nullopt;
        const QStringList fields{"enabled", "levels"};
        const auto params = object(value, fields, fields, "options");
        core::modeling::SubdivisionOptions result;
        result.enabled = boolean(params["enabled"], "options.enabled");
        result.levels = integer(params["levels"], "options.levels", 1, 2);
        return result;
    }
    IfDirty ifDirty(const QJsonValue& value) {
        const auto name = string(value, "ifDirty");
        if (name != "reject" && name != "discard")
            fail("ifDirty", QStringLiteral("仅支持 reject/discard。"));
        return name == "discard" ? IfDirty::Discard : IfDirty::Reject;
    }
    glm::dvec3 deltaVector(const QJsonValue& value, const QString& path, bool scale = false) {
        glm::dvec3 result(0);
        const auto values = array(value, 3, 3, path);
        if (values.size() != 3)
            return result;
        for (int axis = 0; axis < 3; ++axis) {
            result[axis] = number(values[axis], path + QStringLiteral("[%1]").arg(axis));
            if (scale && std::abs(result[axis]) < 0.001)
                fail(path, QStringLiteral("缩放绝对值不得小于 0.001。"));
        }
        return result;
    }
    MeshTransformDelta delta(const QJsonValue& value) {
        const QStringList keys{"space", "pivot", "translation", "rotationQuaternion", "scale"};
        const auto params = object(value, keys, keys, "transform");
        MeshTransformDelta result;
        const auto space = string(params["space"], "transform.space");
        if (space != "local" && space != "world")
            fail("transform.space", QStringLiteral("空间仅支持 local/world。"));
        result.space = space == "world" ? MeshSpace::World : MeshSpace::Local;
        result.pivot = deltaVector(params["pivot"], "transform.pivot");
        result.translation = deltaVector(params["translation"], "transform.translation");
        result.scale = deltaVector(params["scale"], "transform.scale", true);
        const auto quaternion =
            array(params["rotationQuaternion"], 4, 4, "transform.rotationQuaternion");
        if (quaternion.size() == 4) {
            result.rotation = {number(quaternion[3], "transform.rotationQuaternion[3]"),
                               number(quaternion[0], "transform.rotationQuaternion[0]"),
                               number(quaternion[1], "transform.rotationQuaternion[1]"),
                               number(quaternion[2], "transform.rotationQuaternion[2]")};
            if (result.rotation == glm::dquat(0, 0, 0, 0))
                fail("transform.rotationQuaternion", QStringLiteral("旋转四元数不能为零。"));
        }
        return result;
    }
};
QString domainName(MeshDomain domain) {
    return domain == MeshDomain::Vertices ? QStringLiteral("vertices") : QStringLiteral("faces");
}
QJsonArray fieldNames(const std::vector<MeshField>& fields) {
    QJsonArray result;
    for (const auto field : fields)
        result.append(field == MeshField::Position ? "position" : "cornerAttributes");
    return result;
}
void addMeshIdentity(QJsonObject& json, const MeshIdentity& mesh) {
    json.insert("entityId", QString::number(mesh.entityId));
    json.insert("meshId", QString::number(mesh.meshId));
    json.insert("topologyRevision", QString::number(mesh.topologyRevision));
    json.insert("geometryRevision", QString::number(mesh.geometryRevision));
    json.insert("evaluationRevision", QString::number(mesh.evaluationRevision));
}
QJsonObject cursorJson(const MeshSourceCursor& cursor) {
    return {{"document", QJsonObject{{"instanceId", cursor.document.instanceId}, {"documentId", cursor.document.documentId}}},
            {"documentRevision", QString::number(cursor.documentRevision)},
            {"entityId", QString::number(cursor.entityId)}, {"meshId", QString::number(cursor.meshId)},
            {"topologyRevision", QString::number(cursor.topologyRevision)},
            {"geometryRevision", QString::number(cursor.geometryRevision)},
            {"domain", domainName(cursor.domain)}, {"fields", fieldNames(cursor.fields)},
            {"afterId", QString::number(cursor.afterId)}};
}
QJsonArray ids(const std::vector<core::EntityId>& values) {
    QJsonArray result;
    for (const auto value : values)
        result.append(QString::number(value));
    return result;
}
QJsonArray edgeJson(const core::modeling::EdgeKey& edge) {
    return {QString::number(edge.first), QString::number(edge.second)};
}
QJsonArray edgesJson(const std::vector<core::modeling::EdgeKey>& edges) {
    QJsonArray result;
    for (const auto& edge : edges)
        result.append(edgeJson(edge));
    return result;
}
QString componentDomainName(MeshComponentDomain domain) {
    return domain == MeshComponentDomain::Edges   ? QStringLiteral("edges")
           : domain == MeshComponentDomain::Faces ? QStringLiteral("faces")
                                                  : QStringLiteral("vertices");
}
QString modifierName(ModifierKind modifier) {
    return modifier == ModifierKind::Mirror ? QStringLiteral("mirror")
                                            : QStringLiteral("subdivision");
}
QJsonValue mirrorJson(const std::optional<core::modeling::MirrorOptions>& options) {
    if (!options)
        return QJsonValue(QJsonValue::Null);
    const auto axis = options->axis == core::modeling::MirrorAxis::Y   ? "y"
                      : options->axis == core::modeling::MirrorAxis::Z ? "z"
                                                                       : "x";
    return QJsonObject{{"axis", axis},
                       {"enabled", options->enabled},
                       {"merge", options->merge},
                       {"clipping", options->clipping},
                       {"threshold", options->threshold}};
}
QJsonValue subdivisionJson(const std::optional<core::modeling::SubdivisionOptions>& options) {
    if (!options)
        return QJsonValue(QJsonValue::Null);
    return QJsonObject{{"enabled", options->enabled}, {"levels", options->levels}};
}
QJsonArray vector(const glm::vec3& value) {
    return {value.x, value.y, value.z};
}
QJsonObject transformJson(const core::Transform& value) {
    return {{"space", "local"},
            {"translation", vector(value.position)},
            {"rotationQuaternion",
             QJsonArray{value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w}},
            {"scale", vector(value.scale)}};
}
QJsonObject surfaceJson(const core::SurfaceStyle& value) {
    return {{"tint", vector(value.tint)},
            {"useVertexColor", value.useVertexColor},
            {"useTexture", value.useTexture}};
}
QJsonObject cameraJson(const core::CameraComponent& value) {
    return {{"fieldOfView", value.fieldOfView},
            {"nearPlane", value.nearPlane},
            {"farPlane", value.farPlane}};
}
QJsonObject lightJson(const core::LightComponent& value) {
    return {{"color", vector(value.color)}, {"intensity", value.intensity}};
}
QJsonValue boundsJson(const std::optional<core::Aabb>& bounds) {
    if (!bounds)
        return QJsonValue(QJsonValue::Null);
    return QJsonObject{{"minimum", vector(bounds->minimum)}, {"maximum", vector(bounds->maximum)}};
}
QString primitiveName(core::PrimitiveKind primitive) {
    switch (primitive) {
        case core::PrimitiveKind::Empty:
            return QStringLiteral("empty");
        case core::PrimitiveKind::Cube:
            return QStringLiteral("cube");
        case core::PrimitiveKind::Sphere:
            return QStringLiteral("sphere");
        case core::PrimitiveKind::Plane:
            return QStringLiteral("plane");
    }
    return {};
}
QString errorName(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidArgument:
            return QStringLiteral("INVALID_ARGUMENT");
        case ErrorCode::InvalidTopology:
            return QStringLiteral("INVALID_TOPOLOGY");
        case ErrorCode::LimitExceeded:
            return QStringLiteral("LIMIT_EXCEEDED");
        case ErrorCode::StaleDocument:
            return QStringLiteral("STALE_DOCUMENT");
        case ErrorCode::RevisionConflict:
            return QStringLiteral("REVISION_CONFLICT");
        case ErrorCode::NotFound:
            return QStringLiteral("NOT_FOUND");
        case ErrorCode::Busy:
            return QStringLiteral("BUSY");
        case ErrorCode::UnsupportedOperation:
            return QStringLiteral("UNSUPPORTED_OPERATION");
        case ErrorCode::UnsupportedTransform:
            return QStringLiteral("UNSUPPORTED_TRANSFORM");
        case ErrorCode::PermissionDenied:
            return QStringLiteral("PERMISSION_DENIED");
        case ErrorCode::UnsavedChanges:
            return QStringLiteral("UNSAVED_CHANGES");
        case ErrorCode::OverwriteDenied:
            return QStringLiteral("OVERWRITE_DENIED");
        case ErrorCode::IoError:
            return QStringLiteral("IO_ERROR");
        case ErrorCode::Internal:
            return QStringLiteral("INTERNAL");
        case ErrorCode::ViewportUnavailable:
            return QStringLiteral("VIEWPORT_UNAVAILABLE");
        case ErrorCode::ViewChanged:
            return QStringLiteral("VIEW_CHANGED");
        case ErrorCode::RenderFailed:
            return QStringLiteral("RENDER_FAILED");
        case ErrorCode::CaptureTimeout:
            return QStringLiteral("CAPTURE_TIMEOUT");
        case ErrorCode::Cancelled:
            return QStringLiteral("CANCELLED");
        case ErrorCode::DeadlineExceeded:
            return QStringLiteral("DEADLINE_EXCEEDED");
        case ErrorCode::PathDenied:
            return QStringLiteral("PATH_DENIED");
    }
    return {};
}
QString recoveryName(Recovery recovery) {
    switch (recovery) {
        case Recovery::Refetch:
            return QStringLiteral("refetch");
        case Recovery::CorrectInput:
            return QStringLiteral("correct_input");
        case Recovery::Wait:
            return QStringLiteral("wait");
        case Recovery::QueryResult:
            return QStringLiteral("query_result");
        case Recovery::None:
            return QStringLiteral("none");
    }
    return {};
}
QString statusName(ResultStatus status) {
    switch (status) {
        case ResultStatus::Committed:
            return QStringLiteral("committed");
        case ResultStatus::NoChange:
            return QStringLiteral("no_change");
        case ResultStatus::Saved:
            return QStringLiteral("saved");
        case ResultStatus::Opened:
            return QStringLiteral("opened");
    }
    return {};
}
class ScopedCommitGuard final {
  public:
    ScopedCommitGuard(EditorApiService& service, BeforeCommitGuard guard)
        : service_(service), previous_(service.exchangeBeforeCommitGuard(std::move(guard))) {}
    ~ScopedCommitGuard() {
        service_.exchangeBeforeCommitGuard(std::move(previous_));
    }

  private:
    EditorApiService& service_;
    BeforeCommitGuard previous_;
};
} // namespace

ApiResult<std::uint64_t> ApiJsonCodec::parseUint64(const QJsonValue& value,
                                                   const QString& fieldPath, bool allowZero) {
    using Result = ApiResult<std::uint64_t>;
    const auto fail = [&] {
        return Result::failure(
            invalid(fieldPath, QStringLiteral("须为规范 uint64 十进制字符串。")));
    };
    if (!value.isString())
        return fail();
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text.front() == '0'))
        return fail();
    std::uint64_t result = 0;
    for (const auto character : text) {
        if (character < '0' || character > '9')
            return fail();
        const auto digit = std::uint64_t(character.unicode() - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return fail();
        result = result * 10 + digit;
    }
    if (!allowZero && result == 0)
        return fail();
    return Result::success(result);
}
ApiResult<core::Transform> ApiJsonCodec::parseTransform(const QJsonValue& value,
                                                        const QString& fieldPath) {
    Decoder decoder;
    const auto params =
        decoder.object(value, {"space", "translation", "rotationQuaternion", "scale"},
                       {"space", "translation", "rotationQuaternion", "scale"}, fieldPath);
    if (decoder.string(params["space"], childPath(fieldPath, "space")) != "local")
        decoder.fail(childPath(fieldPath, "space"), QStringLiteral("仅支持 local TRS。"));
    const auto position =
        decoder.numbers(params["translation"], 3, childPath(fieldPath, "translation"));
    const auto rotation = decoder.numbers(params["rotationQuaternion"], 4,
                                          childPath(fieldPath, "rotationQuaternion"));
    const auto scale =
        decoder.numbers(params["scale"], 3, childPath(fieldPath, "scale"), false, true);
    core::Transform result;
    result.position = {position[0], position[1], position[2]};
    result.rotation = {rotation[3], rotation[0], rotation[1], rotation[2]};
    result.scale = {scale[0], scale[1], scale[2]};
    if (!result.isValid())
        decoder.fail(fieldPath, QStringLiteral("变换须有限，四元数有效且缩放绝对值至少为 0.001。"));
    if (decoder.error)
        return ApiResult<core::Transform>::failure(*decoder.error);
    return ApiResult<core::Transform>::success(result);
}
ApiResult<ApiRequest> ApiJsonCodec::decodeRequest(const QString& method, const QJsonValue& params) {
    Decoder decoder;
    ApiRequest request{EmptyRequest{}};
    const QStringList mutationFields{"document", "expectedDocumentRevision", "clientSessionId",
                                     "mutationSequence", "timeoutMs"};
    if (method == "system.describe" || method == "document.current") {
        decoder.object(params, {}, {}, {});
    } else if (method == "scene.getSummary" || method == "history.getState") {
        const auto object = decoder.object(params, {"document"}, {"document"}, {});
        request = DocumentRequest{decoder.document(object["document"])};
    } else if (method == "entity.get") {
        const auto object =
            decoder.object(params, {"document", "entityId"}, {"document", "entityId"}, {});
        EntityGetRequest result;
        result.document = decoder.document(object["document"]);
        result.entityId = decoder.uint64(object["entityId"], "entityId", false);
        request = result;
    } else if (method == "scene.listEntities") {
        const auto object = decoder.object(
            params, {"document", "afterEntityId", "limit", "expectedDocumentRevision"},
            {"document"}, {});
        EntityListRequest result;
        result.document = decoder.document(object["document"]);
        if (object.contains("afterEntityId"))
            result.afterEntityId = decoder.uint64(object["afterEntityId"], "afterEntityId");
        if (object.contains("limit"))
            result.limit = decoder.integer(object["limit"], "limit", 1,
                                           int(limits::sourcePageMaximum));
        if (object.contains("expectedDocumentRevision"))
            result.expectedDocumentRevision =
                decoder.uint64(object["expectedDocumentRevision"], "expectedDocumentRevision");
        request = result;
    } else if (method == "entity.duplicate" || method == "entity.delete") {
        const auto object =
            decoder.object(params, mutationFields + QStringList{"entityId"},
                           {"document", "expectedDocumentRevision", "entityId"}, {});
        EntityMutationRequest target;
        static_cast<MutationRequest&>(target) = decoder.mutation(object);
        target.entityId = decoder.uint64(object["entityId"], "entityId", false);
        if (method == "entity.duplicate") {
            EntityDuplicateRequest result;
            static_cast<EntityMutationRequest&>(result) = target;
            request = result;
        } else {
            EntityDeleteRequest result;
            static_cast<EntityMutationRequest&>(result) = target;
            request = result;
        }
    } else if (method == "entity.setParent") {
        const auto object =
            decoder.object(params, mutationFields + QStringList{"entityId", "parentId", "mode"},
                           {"document", "expectedDocumentRevision", "entityId", "parentId"}, {});
        EntitySetParentRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.entityId = decoder.uint64(object["entityId"], "entityId", false);
        result.parentId = decoder.uint64(object["parentId"], "parentId");
        if (object.contains("mode"))
            result.mode = decoder.string(object["mode"], "mode");
        if (result.mode != "keepLocal")
            decoder.fail("mode", QStringLiteral("只支持 keepLocal。"));
        request = result;
    } else if (method == "collection.create") {
        const auto object = decoder.object(params, mutationFields + QStringList{"name", "visible"},
                                           {"document", "expectedDocumentRevision", "name"}, {});
        CollectionCreateRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.name = decoder.string(object["name"], "name", 1, 256);
        if (object.contains("visible"))
            result.visible = decoder.boolean(object["visible"], "visible");
        request = result;
    } else if (method == "collection.update" || method == "collection.delete") {
        const bool update = method == "collection.update";
        const auto object =
            decoder.object(params,
                           mutationFields + (update ? QStringList{"collectionId", "name", "visible"}
                                                    : QStringList{"collectionId"}),
                           {"document", "expectedDocumentRevision", "collectionId"}, {});
        CollectionMutationRequest target;
        static_cast<MutationRequest&>(target) = decoder.mutation(object);
        target.collectionId = decoder.uint64(object["collectionId"], "collectionId", false);
        if (update) {
            CollectionUpdateRequest result;
            static_cast<CollectionMutationRequest&>(result) = target;
            if (object.contains("name"))
                result.name = decoder.string(object["name"], "name", 1, 256);
            if (object.contains("visible"))
                result.visible = decoder.boolean(object["visible"], "visible");
            if (!result.name && !result.visible)
                decoder.fail({}, QStringLiteral("至少指定名称或显隐。"));
            request = result;
        } else {
            CollectionDeleteRequest result;
            static_cast<CollectionMutationRequest&>(result) = target;
            request = result;
        }
    } else if (method == "collection.assign") {
        const auto object = decoder.object(
            params, mutationFields + QStringList{"entityId", "collectionId"},
            {"document", "expectedDocumentRevision", "entityId", "collectionId"}, {});
        CollectionAssignRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.entityId = decoder.uint64(object["entityId"], "entityId", false);
        result.collectionId = decoder.uint64(object["collectionId"], "collectionId");
        request = result;
    } else if (method == "camera.create" || method == "light.create") {
        const auto component =
            method == "camera.create" ? QStringLiteral("camera") : QStringLiteral("light");
        const auto object = decoder.object(
            params,
            mutationFields + QStringList{"name", "parentId", "transform", "visible", component},
            {"document", "expectedDocumentRevision", "name", "parentId", "transform", component},
            {});
        DeviceCreateRequest target;
        static_cast<MutationRequest&>(target) = decoder.mutation(object);
        target.name = decoder.string(object["name"], "name", 1, 256);
        target.parentId = decoder.uint64(object["parentId"], "parentId");
        target.transform = decoder.transform(object["transform"]);
        if (object.contains("visible"))
            target.visible = decoder.boolean(object["visible"], "visible");
        if (method == "camera.create") {
            CameraCreateRequest result;
            static_cast<DeviceCreateRequest&>(result) = target;
            result.camera = decoder.camera(object["camera"]);
            request = result;
        } else {
            LightCreateRequest result;
            static_cast<DeviceCreateRequest&>(result) = target;
            result.light = decoder.light(object["light"]);
            request = result;
        }
    } else if (method == "camera.update" || method == "light.update") {
        const auto component =
            method == "camera.update" ? QStringLiteral("camera") : QStringLiteral("light");
        const auto object =
            decoder.object(params, mutationFields + QStringList{"entityId", component},
                           {"document", "expectedDocumentRevision", "entityId", component}, {});
        EntityMutationRequest target;
        static_cast<MutationRequest&>(target) = decoder.mutation(object);
        target.entityId = decoder.uint64(object["entityId"], "entityId", false);
        if (method == "camera.update") {
            CameraUpdateRequest result;
            static_cast<EntityMutationRequest&>(result) = target;
            result.camera = decoder.camera(object["camera"]);
            request = result;
        } else {
            LightUpdateRequest result;
            static_cast<EntityMutationRequest&>(result) = target;
            result.light = decoder.light(object["light"]);
            request = result;
        }
    } else if (method == "batch.createEntities" || method == "batch.setTransforms") {
        const auto object = decoder.object(params, mutationFields + QStringList{"items"},
                                           {"document", "expectedDocumentRevision", "items"}, {});
        const auto items = decoder.array(object["items"], 1, limits::batchItems, "items");
        if (method == "batch.createEntities") {
            BatchCreateEntitiesRequest result;
            static_cast<MutationRequest&>(result) = decoder.mutation(object);
            result.items.reserve(std::size_t(items.size()));
            const QStringList keys{"primitive", "name", "parentId", "transform", "surface"};
            for (qsizetype index = 0; index < items.size(); ++index) {
                const auto path = QStringLiteral("items[%1]").arg(index);
                const auto item = decoder.object(items[index], keys, keys, path);
                BatchEntityCreateItem target;
                const auto primitive = decoder.string(item["primitive"], path + ".primitive");
                if (primitive == "empty")
                    target.primitive = core::PrimitiveKind::Empty;
                else if (primitive == "cube")
                    target.primitive = core::PrimitiveKind::Cube;
                else if (primitive == "sphere")
                    target.primitive = core::PrimitiveKind::Sphere;
                else if (primitive == "plane")
                    target.primitive = core::PrimitiveKind::Plane;
                else
                    decoder.fail(path + ".primitive", QStringLiteral("不支持的基础几何类型。"));
                target.name = decoder.string(item["name"], path + ".name", 1, 256);
                target.parentId = decoder.uint64(item["parentId"], path + ".parentId");
                target.transform = decoder.transform(item["transform"], path + ".transform");
                target.surface = decoder.surface(item["surface"], path + ".surface");
                result.items.push_back(std::move(target));
            }
            request = std::move(result);
        } else {
            BatchSetTransformsRequest result;
            static_cast<MutationRequest&>(result) = decoder.mutation(object);
            result.items.reserve(std::size_t(items.size()));
            for (qsizetype index = 0; index < items.size(); ++index) {
                const auto path = QStringLiteral("items[%1]").arg(index);
                const auto item = decoder.object(items[index], {"entityId", "transform"},
                                                 {"entityId", "transform"}, path);
                result.items.push_back({decoder.uint64(item["entityId"], path + ".entityId", false),
                                        decoder.transform(item["transform"], path + ".transform")});
            }
            request = std::move(result);
        }
    } else if (method == "entity.create") {
        const auto object = decoder.object(
            params,
            mutationFields + QStringList{"primitive", "name", "parentId", "transform", "surface"},
            {"document", "expectedDocumentRevision", "primitive", "name", "parentId", "transform",
             "surface"},
            {});
        EntityCreateRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        const auto primitive = decoder.string(object["primitive"], "primitive");
        if (primitive == "empty")
            result.primitive = core::PrimitiveKind::Empty;
        else if (primitive == "cube")
            result.primitive = core::PrimitiveKind::Cube;
        else if (primitive == "sphere")
            result.primitive = core::PrimitiveKind::Sphere;
        else if (primitive == "plane")
            result.primitive = core::PrimitiveKind::Plane;
        else
            decoder.fail("primitive", QStringLiteral("不支持的基础几何类型。"));
        result.name = decoder.string(object["name"], "name", 1, 256);
        result.parentId = decoder.uint64(object["parentId"], "parentId");
        result.transform = decoder.transform(object["transform"]);
        result.surface = decoder.surface(object["surface"]);
        request = result;
    } else if (method == "entity.update") {
        const auto object = decoder.object(
            params,
            mutationFields + QStringList{"entityId", "name", "transform", "surface", "visible"},
            {"document", "expectedDocumentRevision", "entityId"}, {});
        EntityUpdateRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.entityId = decoder.uint64(object["entityId"], "entityId", false);
        if (object.contains("name"))
            result.changes.name = decoder.string(object["name"], "name", 1, 256);
        if (object.contains("transform"))
            result.changes.transform = decoder.transform(object["transform"]);
        if (object.contains("surface"))
            result.changes.surface = decoder.surface(object["surface"]);
        if (object.contains("visible"))
            result.changes.visible = decoder.boolean(object["visible"], "visible");
        if (!result.changes.name && !result.changes.transform && !result.changes.surface &&
            !result.changes.visible)
            decoder.fail({}, QStringLiteral("至少指定一项对象属性。"));
        request = result;
    } else if (method == "mesh.create") {
        const auto object = decoder.object(params, mutationFields + QStringList{"name", "parentId", "transform", "surface", "positions", "faces"},
                                            {"document", "expectedDocumentRevision", "name", "parentId", "transform", "surface", "positions", "faces"}, {});
        MeshCreateRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.name = decoder.string(object["name"], "name", 1, 256);
        result.parentId = decoder.uint64(object["parentId"], "parentId");
        result.transform = decoder.transform(object["transform"]);
        result.surface = decoder.surface(object["surface"]);
        const auto positions = decoder.array(object["positions"], 0, limits::meshVertices, "positions");
        for (qsizetype index = 0; index < positions.size(); ++index) {
            const auto value = decoder.numbers(positions[index], 3, QStringLiteral("positions[%1]").arg(index));
            result.positions.push_back({value[0], value[1], value[2]});
        }
        const auto faces = decoder.array(object["faces"], 0, limits::meshFaces, "faces");
        std::size_t totalCorners = 0;
        for (qsizetype index = 0; index < faces.size(); ++index) {
            const auto path = QStringLiteral("faces[%1]").arg(index);
            const auto face = decoder.object(faces[index], {"indices", "corners"}, {"indices"}, path);
            const auto indices = decoder.array(face["indices"], 3, limits::meshVertices, path + ".indices");
            if (std::size_t(indices.size()) > limits::meshCorners - totalCorners) {
                decoder.limit(path + ".indices");
                break;
            }
            totalCorners += std::size_t(indices.size());
            MeshFaceInput input;
            std::set<std::size_t> distinct;
            for (const auto& value : indices) {
                const auto id = std::size_t(decoder.integer(value, path + ".indices", 0, int(limits::meshVertices - 1)));
                if (id >= result.positions.size() || !distinct.insert(id).second)
                    decoder.fail(path + ".indices", QStringLiteral("面索引越界或重复。"));
                input.indices.push_back(id);
            }
            if (face.contains("corners")) {
                const auto corners = decoder.array(face["corners"], 3, limits::meshVertices, path + ".corners");
                if (corners.size() != indices.size())
                    decoder.fail(path + ".corners", QStringLiteral("面角属性须与面环长度匹配。"));
                input.corners.emplace();
                for (qsizetype cornerIndex = 0; cornerIndex < corners.size(); ++cornerIndex) {
                    const auto cornerPath = path + QStringLiteral(".corners[%1]").arg(cornerIndex);
                    const auto corner = decoder.object(corners[cornerIndex], {"uv", "color", "normal"}, {}, cornerPath);
                    MeshCornerInput attributes;
                    if (corner.contains("uv")) {
                        const auto value = decoder.numbers(corner["uv"], 2, cornerPath + ".uv");
                        attributes.uv = {value[0], value[1]};
                    }
                    if (corner.contains("color")) {
                        const auto value = decoder.numbers(corner["color"], 3, cornerPath + ".color");
                        attributes.color = {value[0], value[1], value[2]};
                    }
                    if (corner.contains("normal") && !corner["normal"].isNull()) {
                        const auto value = decoder.numbers(corner["normal"], 3, cornerPath + ".normal");
                        attributes.normal = glm::vec3(value[0], value[1], value[2]);
                        if (glm::length(glm::dvec3(*attributes.normal)) == 0)
                            decoder.fail(cornerPath + ".normal", QStringLiteral("硬法线不能为零。"));
                    }
                    input.corners->push_back(attributes);
                }
            }
            result.faces.push_back(std::move(input));
        }
        request = std::move(result);
    } else if (method == "mesh.getSummary" || method == "mesh.readSourcePage") {
        const bool page = method == "mesh.readSourcePage";
        const auto object = decoder.object(params, page ? QStringList{"document", "entityId", "meshId", "domain", "fields", "limit", "cursor"}
                                                       : QStringList{"document", "entityId", "meshId"},
                                            page ? QStringList{"document", "entityId", "meshId", "domain"}
                                                 : QStringList{"document", "entityId", "meshId"}, {});
        MeshTargetRequest target;
        target.document = decoder.document(object["document"]);
        target.entityId = decoder.uint64(object["entityId"], "entityId", false);
        target.meshId = decoder.uint64(object["meshId"], "meshId", false);
        if (page) {
            MeshSourcePageRequest result;
            static_cast<MeshTargetRequest&>(result) = target;
            result.domain = decoder.domain(object["domain"], "domain");
            if (object.contains("fields"))
                result.fields = decoder.fields(object["fields"], result.domain, "fields");
            if (object.contains("limit"))
                result.limit = decoder.integer(object["limit"], "limit", 1, int(limits::sourcePageMaximum));
            if (object.contains("cursor"))
                result.cursor = decoder.cursor(object["cursor"]);
            request = result;
        } else {
            request = target;
        }
    } else if (method == "mesh.extrudeRegion" || method == "mesh.insetFace") {
        const bool extrude = method == "mesh.extrudeRegion";
        const QStringList targetFields{"entityId", "meshId", "expectedTopologyRevision", "expectedGeometryRevision"};
        const auto required = QStringList{"document", "expectedDocumentRevision"} + targetFields +
                              (extrude ? QStringList{"faceIds", "space", "offset"} : QStringList{"faceId", "thickness"});
        const auto object = decoder.object(params, mutationFields + targetFields +
                                            (extrude ? QStringList{"faceIds", "space", "offset"} : QStringList{"faceId", "thickness"}), required, {});
        const auto mutation = decoder.meshMutation(object);
        if (extrude) {
            MeshExtrudeRequest result;
            static_cast<MeshMutationRequest&>(result) = mutation;
            const auto faceIds = decoder.array(object["faceIds"], 1, limits::meshFaces, "faceIds");
            std::set<core::modeling::FaceId> distinct;
            for (const auto& value : faceIds) {
                const auto id = decoder.uint64(value, "faceIds", false);
                if (!distinct.insert(id).second)
                    decoder.fail("faceIds", QStringLiteral("源面 ID 不能重复。"));
                result.faceIds.push_back(id);
            }
            const auto space = decoder.string(object["space"], "space");
            if (space != "local" && space != "world")
                decoder.fail("space", QStringLiteral("空间仅支持 local/world。"));
            result.space = space == "world" ? MeshSpace::World : MeshSpace::Local;
            const auto offset = object["offset"].toArray();
            if (!object["offset"].isArray() || offset.size() != 3)
                decoder.fail("offset", QStringLiteral("位移须为三个数值。"));
            else
                for (int axis = 0; axis < 3; ++axis)
                    result.offset[axis] = decoder.number(offset[axis], QStringLiteral("offset[%1]").arg(axis));
            request = result;
        } else {
            MeshInsetRequest result;
            static_cast<MeshMutationRequest&>(result) = mutation;
            result.faceId = decoder.uint64(object["faceId"], "faceId", false);
            result.thickness = decoder.number(object["thickness"], "thickness");
            if (result.thickness <= 0)
                decoder.fail("thickness", QStringLiteral("厚度须大于零。"));
            request = result;
        }
    } else if (method == "mesh.makeEditable") {
        const auto object =
            decoder.object(params, mutationFields + QStringList{"entityId"},
                           {"document", "expectedDocumentRevision", "entityId"}, {});
        MeshMakeEditableRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.entityId = decoder.uint64(object["entityId"], "entityId", false);
        request = result;
    } else if (method == "mesh.transformComponents" || method == "mesh.deleteComponents" ||
               method == "mesh.fillFace" || method == "mesh.bevelEdge" ||
               method == "mesh.loopCut") {
        const QStringList targetFields{"entityId", "meshId", "expectedTopologyRevision",
                                       "expectedGeometryRevision"};
        auto allowed = mutationFields + targetFields;
        auto required = QStringList{"document", "expectedDocumentRevision"} + targetFields;
        const bool components = method == "mesh.transformComponents" ||
                                method == "mesh.deleteComponents" || method == "mesh.fillFace";
        if (components) {
            allowed += QStringList{"domain", "vertexIds", "edges"};
            if (method != "mesh.fillFace")
                allowed += QStringList{"faceIds"};
            required += QStringList{"domain"};
            if (method == "mesh.transformComponents") {
                allowed += QStringList{"transform"};
                required += QStringList{"transform"};
            }
        } else if (method == "mesh.bevelEdge") {
            allowed += QStringList{"edge", "width"};
            required += QStringList{"edge", "width"};
        } else {
            allowed += QStringList{"seedEdge", "slide"};
            required += QStringList{"seedEdge"};
        }
        const auto object = decoder.object(params, allowed, required, {});
        if (components) {
            const auto targets = decoder.components(object, method != "mesh.fillFace");
            if (method == "mesh.transformComponents") {
                MeshTransformComponentsRequest result;
                static_cast<MeshComponentsRequest&>(result) = targets;
                result.transform = decoder.delta(object["transform"]);
                request = std::move(result);
            } else if (method == "mesh.deleteComponents") {
                MeshDeleteComponentsRequest result;
                static_cast<MeshComponentsRequest&>(result) = targets;
                request = std::move(result);
            } else {
                MeshFillFaceRequest result;
                static_cast<MeshComponentsRequest&>(result) = targets;
                request = std::move(result);
            }
        } else if (method == "mesh.bevelEdge") {
            MeshBevelEdgeRequest result;
            static_cast<MeshMutationRequest&>(result) = decoder.meshMutation(object);
            result.edge = decoder.edge(object["edge"], "edge");
            result.width = decoder.number(object["width"], "width");
            if (result.width <= 0)
                decoder.fail("width", QStringLiteral("宽度须大于零。"));
            request = result;
        } else {
            MeshLoopCutRequest result;
            static_cast<MeshMutationRequest&>(result) = decoder.meshMutation(object);
            result.seedEdge = decoder.edge(object["seedEdge"], "seedEdge");
            if (object.contains("slide"))
                result.slide = decoder.number(object["slide"], "slide");
            if (result.slide <= -1 || result.slide >= 1)
                decoder.fail("slide", QStringLiteral("滑移须严格位于 -1～1 之间。"));
            request = result;
        }
    } else if (method == "modifier.setMirror" || method == "modifier.setSubdivision" ||
               method == "modifier.apply") {
        const QStringList targetFields{"entityId", "meshId", "expectedTopologyRevision",
                                       "expectedGeometryRevision"};
        const auto field =
            method == "modifier.apply" ? QStringLiteral("modifier") : QStringLiteral("options");
        const auto object = decoder.object(
            params, mutationFields + targetFields + QStringList{field},
            QStringList{"document", "expectedDocumentRevision"} + targetFields + QStringList{field},
            {});
        const auto mutation = decoder.meshMutation(object);
        if (method == "modifier.setMirror") {
            ModifierSetMirrorRequest result;
            static_cast<MeshMutationRequest&>(result) = mutation;
            result.options = decoder.mirrorOptions(object["options"]);
            request = result;
        } else if (method == "modifier.setSubdivision") {
            ModifierSetSubdivisionRequest result;
            static_cast<MeshMutationRequest&>(result) = mutation;
            result.options = decoder.subdivisionOptions(object["options"]);
            request = result;
        } else {
            ModifierApplyRequest result;
            static_cast<MeshMutationRequest&>(result) = mutation;
            const auto modifier = decoder.string(object["modifier"], "modifier");
            if (modifier != "mirror" && modifier != "subdivision")
                decoder.fail("modifier", QStringLiteral("仅支持 mirror/subdivision。"));
            result.modifier =
                modifier == "subdivision" ? ModifierKind::Subdivision : ModifierKind::Mirror;
            request = result;
        }
    } else if (method == "history.undo" || method == "history.redo") {
        const auto object =
            decoder.object(params, mutationFields + QStringList{"expectedHistoryRevision"},
                           {"document", "expectedDocumentRevision", "expectedHistoryRevision"}, {});
        HistoryMutationRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.expectedHistoryRevision =
            decoder.uint64(object["expectedHistoryRevision"], "expectedHistoryRevision");
        request = result;
    } else if (method == "file.save") {
        const auto object = decoder.object(params, mutationFields + QStringList{"overwrite"},
                                           {"document", "expectedDocumentRevision"}, {});
        FileSaveRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        if (object.contains("overwrite"))
            result.overwrite = decoder.boolean(object["overwrite"], "overwrite");
        request = result;
    } else if (method == "file.importGltf") {
        const QStringList fields{"path", "name", "parentId", "transform"};
        const auto object =
            decoder.object(params, mutationFields + fields,
                           QStringList{"document", "expectedDocumentRevision"} + fields, {});
        ImportGltfRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.path = decoder.string(object["path"], "path", 1);
        result.name = decoder.string(object["name"], "name", 1, 256);
        result.parentId = decoder.uint64(object["parentId"], "parentId");
        result.transform = decoder.transform(object["transform"]);
        request = result;
    } else if (method == "file.exportObj") {
        const auto object = decoder.object(
            params, mutationFields + QStringList{"path", "entityId", "mode", "overwrite"},
            {"document", "expectedDocumentRevision", "path", "entityId", "mode"}, {});
        ExportObjRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.path = decoder.string(object["path"], "path", 1);
        result.entityId = decoder.uint64(object["entityId"], "entityId", false);
        const auto mode = decoder.string(object["mode"], "mode");
        if (mode != "source" && mode != "evaluated")
            decoder.fail("mode", QStringLiteral("仅支持 source/evaluated。"));
        result.mode = mode == "evaluated" ? ExportObjMode::Evaluated : ExportObjMode::Source;
        if (object.contains("overwrite"))
            result.overwrite = decoder.boolean(object["overwrite"], "overwrite");
        request = result;
    } else if (method == "document.new") {
        const auto object = decoder.object(params, mutationFields + QStringList{"ifDirty"},
                                           {"document", "expectedDocumentRevision"}, {});
        DocumentNewRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        if (object.contains("ifDirty"))
            result.ifDirty = decoder.ifDirty(object["ifDirty"]);
        request = result;
    } else if (method == "file.saveAs" || method == "document.open") {
        const auto object = decoder.object(params,
                                           mutationFields + (method == "document.open"
                                                                 ? QStringList{"path", "ifDirty"}
                                                                 : QStringList{"path"}),
                                           {"document", "expectedDocumentRevision", "path"}, {});
        FileRequest result;
        static_cast<MutationRequest&>(result) = decoder.mutation(object);
        result.path = decoder.string(object["path"], "path", 1);
        if (object.contains("ifDirty"))
            result.ifDirty = decoder.ifDirty(object["ifDirty"]);
        request = result;
    } else {
        return ApiResult<ApiRequest>::failure({ErrorCode::UnsupportedOperation,
                                               QStringLiteral("方法尚未实现。"),
                                               QStringLiteral("method"),
                                               Recovery::None,
                                               {},
                                               -32601});
    }
    if (decoder.error)
        return ApiResult<ApiRequest>::failure(*decoder.error);
    return ApiResult<ApiRequest>::success(std::move(request));
}
ApiResult<QJsonObject> ApiJsonCodec::canonicalParams(const QString& method, const QJsonValue& params) {
    const auto decoded = decodeRequest(method, params);
    if (!decoded.hasValue())
        return ApiResult<QJsonObject>::failure(*decoded.error);
    auto json = std::visit([](const auto& request) {
        using Request = std::decay_t<decltype(request)>;
        QJsonObject result;
        if constexpr (std::is_base_of_v<DocumentRequest, Request>) {
            result.insert("document", QJsonObject{{"instanceId", request.document.instanceId},
                                                 {"documentId", request.document.documentId}});
        }
        if constexpr (std::is_base_of_v<MutationRequest, Request>) {
            result.insert("expectedDocumentRevision", QString::number(request.expectedDocumentRevision));
            result.insert("timeoutMs", request.timeoutMs);
        }
        if constexpr (std::is_same_v<Request, EntityGetRequest> ||
                      std::is_same_v<Request, EntityUpdateRequest> ||
                      std::is_base_of_v<EntityMutationRequest, Request> ||
                      std::is_base_of_v<MeshTargetRequest, Request> ||
                      std::is_base_of_v<MeshMutationRequest, Request>)
            result.insert("entityId", QString::number(request.entityId));
        if constexpr (std::is_base_of_v<MeshTargetRequest, Request> ||
                      std::is_base_of_v<MeshMutationRequest, Request>)
            result.insert("meshId", QString::number(request.meshId));
        if constexpr (std::is_base_of_v<MeshMutationRequest, Request>) {
            result.insert("expectedTopologyRevision", QString::number(request.expectedTopologyRevision));
            result.insert("expectedGeometryRevision", QString::number(request.expectedGeometryRevision));
        }
        if constexpr (std::is_same_v<Request, EntityListRequest>) {
            result.insert("afterEntityId", QString::number(request.afterEntityId));
            result.insert("limit", request.limit);
            if (request.expectedDocumentRevision)
                result.insert("expectedDocumentRevision", QString::number(*request.expectedDocumentRevision));
        } else if constexpr (std::is_same_v<Request, EntityCreateRequest> ||
                             std::is_same_v<Request, MeshCreateRequest>) {
            result.insert("name", request.name);
            result.insert("parentId", QString::number(request.parentId));
            result.insert("transform", transformJson(request.transform));
            result.insert("surface", surfaceJson(request.surface));
            if constexpr (std::is_same_v<Request, EntityCreateRequest>) {
                result.insert("primitive", primitiveName(request.primitive));
            } else {
                QJsonArray positions, faces;
                for (const auto& position : request.positions)
                    positions.append(vector(position));
                for (const auto& face : request.faces) {
                    QJsonArray indices, corners;
                    for (std::size_t index = 0; index < face.indices.size(); ++index) {
                        indices.append(qint64(face.indices[index]));
                        const auto attributes = face.corners ? (*face.corners)[index] : MeshCornerInput{};
                        corners.append(QJsonObject{
                            {"uv", QJsonArray{attributes.uv.x, attributes.uv.y}},
                            {"color", vector(attributes.color)},
                            {"normal", attributes.normal ? QJsonValue(vector(*attributes.normal))
                                                          : QJsonValue(QJsonValue::Null)}});
                    }
                    faces.append(QJsonObject{{"indices", indices}, {"corners", corners}});
                }
                result.insert("positions", positions);
                result.insert("faces", faces);
            }
        } else if constexpr (std::is_same_v<Request, BatchCreateEntitiesRequest> ||
                             std::is_same_v<Request, BatchSetTransformsRequest>) {
            QJsonArray items;
            for (const auto& item : request.items) {
                QJsonObject value{{"transform", transformJson(item.transform)}};
                if constexpr (std::is_same_v<Request, BatchCreateEntitiesRequest>) {
                    value.insert("primitive", primitiveName(item.primitive));
                    value.insert("name", item.name);
                    value.insert("parentId", QString::number(item.parentId));
                    value.insert("surface", surfaceJson(item.surface));
                } else {
                    value.insert("entityId", QString::number(item.entityId));
                }
                items.append(value);
            }
            result.insert("items", items);
        } else if constexpr (std::is_same_v<Request, EntityUpdateRequest>) {
            if (request.changes.name)
                result.insert("name", *request.changes.name);
            if (request.changes.transform)
                result.insert("transform", transformJson(*request.changes.transform));
            if (request.changes.surface)
                result.insert("surface", surfaceJson(*request.changes.surface));
            if (request.changes.visible)
                result.insert("visible", *request.changes.visible);
        } else if constexpr (std::is_same_v<Request, EntitySetParentRequest>) {
            result.insert("parentId", QString::number(request.parentId));
            result.insert("mode", request.mode);
        } else if constexpr (std::is_same_v<Request, CollectionCreateRequest>) {
            result.insert("name", request.name);
            result.insert("visible", request.visible);
        } else if constexpr (std::is_base_of_v<CollectionMutationRequest, Request>) {
            result.insert("collectionId", QString::number(request.collectionId));
            if constexpr (std::is_same_v<Request, CollectionUpdateRequest>) {
                if (request.name)
                    result.insert("name", *request.name);
                if (request.visible)
                    result.insert("visible", *request.visible);
            }
        } else if constexpr (std::is_same_v<Request, CollectionAssignRequest>) {
            result.insert("collectionId", QString::number(request.collectionId));
        } else if constexpr (std::is_base_of_v<DeviceCreateRequest, Request>) {
            result.insert("name", request.name);
            result.insert("parentId", QString::number(request.parentId));
            result.insert("transform", transformJson(request.transform));
            result.insert("visible", request.visible);
        } else if constexpr (std::is_same_v<Request, MeshSourcePageRequest>) {
            const auto defaultField = request.domain == MeshDomain::Vertices
                                          ? MeshField::Position
                                          : MeshField::CornerAttributes;
            result.insert("domain", domainName(request.domain));
            result.insert("fields", fieldNames(request.fields.value_or(std::vector<MeshField>{defaultField})));
            result.insert("limit", request.limit);
            if (request.cursor)
                result.insert("cursor", cursorJson(*request.cursor));
        } else if constexpr (std::is_same_v<Request, MeshExtrudeRequest>) {
            result.insert("faceIds", ids(request.faceIds));
            result.insert("space", request.space == MeshSpace::World ? "world" : "local");
            result.insert("offset", QJsonArray{request.offset.x, request.offset.y, request.offset.z});
        } else if constexpr (std::is_same_v<Request, MeshInsetRequest>) {
            result.insert("faceId", QString::number(request.faceId));
            result.insert("thickness", request.thickness);
        } else if constexpr (std::is_base_of_v<MeshComponentsRequest, Request>) {
            result.insert("domain", componentDomainName(request.domain));
            if (request.vertexIds)
                result.insert("vertexIds", ids(*request.vertexIds));
            if (request.edges)
                result.insert("edges", edgesJson(*request.edges));
            if (request.faceIds)
                result.insert("faceIds", ids(*request.faceIds));
            if constexpr (std::is_same_v<Request, MeshTransformComponentsRequest>) {
                const auto& delta = request.transform;
                result.insert(
                    "transform",
                    QJsonObject{
                        {"space", delta.space == MeshSpace::World ? "world" : "local"},
                        {"pivot", QJsonArray{delta.pivot.x, delta.pivot.y, delta.pivot.z}},
                        {"translation",
                         QJsonArray{delta.translation.x, delta.translation.y, delta.translation.z}},
                        {"rotationQuaternion", QJsonArray{delta.rotation.x, delta.rotation.y,
                                                          delta.rotation.z, delta.rotation.w}},
                        {"scale", QJsonArray{delta.scale.x, delta.scale.y, delta.scale.z}}});
            }
        } else if constexpr (std::is_same_v<Request, MeshBevelEdgeRequest>) {
            result.insert("edge", edgeJson(request.edge));
            result.insert("width", request.width);
        } else if constexpr (std::is_same_v<Request, MeshLoopCutRequest>) {
            result.insert("seedEdge", edgeJson(request.seedEdge));
            result.insert("slide", request.slide);
        } else if constexpr (std::is_same_v<Request, ModifierSetMirrorRequest>) {
            result.insert("options", mirrorJson(request.options));
        } else if constexpr (std::is_same_v<Request, ModifierSetSubdivisionRequest>) {
            result.insert("options", subdivisionJson(request.options));
        } else if constexpr (std::is_same_v<Request, ModifierApplyRequest>) {
            result.insert("modifier", modifierName(request.modifier));
        } else if constexpr (std::is_same_v<Request, HistoryMutationRequest>) {
            result.insert("expectedHistoryRevision", QString::number(request.expectedHistoryRevision));
        } else if constexpr (std::is_same_v<Request, FileRequest>) {
            result.insert("path", request.path);
        } else if constexpr (std::is_same_v<Request, FileSaveRequest>) {
            result.insert("overwrite", request.overwrite);
        } else if constexpr (std::is_same_v<Request, ImportGltfRequest>) {
            result.insert("path", request.path);
            result.insert("name", request.name);
            result.insert("parentId", QString::number(request.parentId));
            result.insert("transform", transformJson(request.transform));
        } else if constexpr (std::is_same_v<Request, ExportObjRequest>) {
            result.insert("path", request.path);
            result.insert("mode",
                          request.mode == ExportObjMode::Evaluated ? "evaluated" : "source");
            result.insert("overwrite", request.overwrite);
        } else if constexpr (std::is_same_v<Request, DocumentNewRequest>) {
            result.insert("ifDirty", request.ifDirty == IfDirty::Discard ? "discard" : "reject");
        }
        if constexpr (std::is_same_v<Request, CameraCreateRequest> ||
                      std::is_same_v<Request, CameraUpdateRequest>)
            result.insert("camera", cameraJson(request.camera));
        if constexpr (std::is_same_v<Request, LightCreateRequest> ||
                      std::is_same_v<Request, LightUpdateRequest>)
            result.insert("light", lightJson(request.light));
        return result;
    }, *decoded.value);
    if (method == "document.open")
        json.insert("ifDirty", std::get<FileRequest>(*decoded.value).ifDirty == IfDirty::Discard
                                   ? "discard"
                                   : "reject");
    return ApiResult<QJsonObject>::success(std::move(json));
}
QJsonObject ApiJsonCodec::encodeState(const DocumentState& state) {
    return {{"document", QJsonObject{{"instanceId", state.document.instanceId},
                                     {"documentId", state.document.documentId}}},
            {"documentRevision", QString::number(state.documentRevision)},
            {"historyRevision", QString::number(state.historyRevision)}};
}
QJsonObject ApiJsonCodec::encodeError(const ApiError& error) {
    auto data = encodeState(error.state);
    data.insert("code", errorName(error.code));
    data.insert("message", error.message);
    data.insert("recovery", recoveryName(error.recovery));
    if (!error.fieldPath.isEmpty())
        data.insert("fieldPath", error.fieldPath);
    return {{"code", error.protocolCode}, {"message", error.message}, {"data", data}};
}
QJsonObject ApiJsonCodec::encode(const SystemDescription& result) {
    auto json = encodeState(result.state);
    json.insert("apiVersion", result.apiVersion);
    json.insert("wireVersion", result.wireVersion);
    json.insert("coordinates", "right-handed Y-up, scene units");
    json.insert("matrixLayout", "column-major");
    json.insert("entityPageDefault", result.entityPageDefault);
    json.insert("entityPageMaximum", result.entityPageMaximum);
    QJsonArray methods;
    for (const auto& method : result.methods) {
        QJsonObject entry{{"name", method.name},
                          {"kind", method.kind},
                          {"permission", method.permission},
                          {"externalEnabled", method.externalEnabled}};
        if (!method.externalGate.isEmpty())
            entry.insert("externalGate", method.externalGate);
        methods.append(entry);
    }
    json.insert("methods", methods);
    QJsonObject limitsJson;
    for (const auto& [name, value] : result.limits)
        limitsJson.insert(name, qint64(value));
    json.insert("limits", limitsJson);
    return json;
}
QJsonObject ApiJsonCodec::encode(const CurrentDocument& result) {
    auto json = encodeState(result.state);
    json.insert("path", result.path);
    json.insert("isModified", result.isModified);
    json.insert("requiresSaveAs", result.requiresSaveAs);
    json.insert("busy", !result.busyReasons.isEmpty());
    json.insert("busyReasons", QJsonArray::fromStringList(result.busyReasons));
    return json;
}
QJsonObject ApiJsonCodec::encode(const SceneSummary& result) {
    auto json = encodeState(result.state);
    json.insert("entityCount", qint64(result.entityCount));
    json.insert("collectionCount", qint64(result.collectionCount));
    json.insert("rootIds", ids(result.rootIds));
    QJsonArray collections;
    for (const auto& collection : result.collections)
        collections.append(QJsonObject{{"collectionId", QString::number(collection.collectionId)},
                                       {"name", collection.name},
                                       {"visible", collection.visible},
                                       {"entityIds", ids(collection.entityIds)}});
    json.insert("collections", collections);
    return json;
}
QJsonObject ApiJsonCodec::encodeEntity(const EntitySnapshot& entity) {
    QJsonArray matrix;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            matrix.append(entity.worldMatrix[column][row]);
    return {{"entityId", QString::number(entity.entityId)},
            {"parentId", QString::number(entity.parentId)},
            {"childIds", ids(entity.childIds)},
            {"name", entity.name},
            {"primitive", primitiveName(entity.primitive)},
            {"meshId", entity.meshId ? QJsonValue(QString::number(entity.meshId))
                                     : QJsonValue(QJsonValue::Null)},
            {"meshKind", entity.meshKind},
            {"transform", transformJson(entity.localTransform)},
            {"worldMatrix", matrix},
            {"surface", surfaceJson(entity.surface)},
            {"collectionId", QString::number(entity.collectionId)},
            {"camera",
             entity.camera ? QJsonValue(cameraJson(*entity.camera)) : QJsonValue(QJsonValue::Null)},
            {"light",
             entity.light ? QJsonValue(lightJson(*entity.light)) : QJsonValue(QJsonValue::Null)},
            {"visible", entity.visible},
            {"effectiveVisible", entity.effectiveVisible},
            {"viewportVisible", entity.viewportVisible},
            {"bounds", QJsonObject{{"kind", "evaluated"},
                                   {"local", boundsJson(entity.localBounds)},
                                   {"world", boundsJson(entity.worldBounds)}}}};
}
QJsonObject ApiJsonCodec::encode(const EntityResult& result) {
    auto json = encodeState(result.state);
    json.insert("entity", encodeEntity(result.entity));
    return json;
}
QJsonObject ApiJsonCodec::encode(const EntityListResult& result) {
    auto json = encodeState(result.state);
    QJsonArray entities;
    for (const auto& entity : result.entities)
        entities.append(encodeEntity(entity));
    json.insert("entities", entities);
    json.insert("nextAfterEntityId", result.nextAfterEntityId
                                         ? QJsonValue(QString::number(*result.nextAfterEntityId))
                                         : QJsonValue(QJsonValue::Null));
    return json;
}
QJsonObject ApiJsonCodec::encode(const HistoryState& result) {
    auto json = encodeState(result.state);
    json.insert("count", result.count);
    json.insert("index", result.index);
    json.insert("cleanIndex", result.cleanIndex);
    json.insert("isClean", result.isClean);
    json.insert("canUndo", result.canUndo);
    json.insert("canRedo", result.canRedo);
    json.insert("undoText", result.undoText);
    json.insert("redoText", result.redoText);
    return json;
}
QJsonObject ApiJsonCodec::encode(const MutationResult& result) {
    auto json = encodeState(result.state);
    json.insert("status", statusName(result.status));
    json.insert("created", QJsonObject{{"entityIds", ids(result.createdEntityIds)}});
    json.insert("affectedEntityIds", ids(result.affectedEntityIds));
    json.insert("undoable", result.undoable);
    json.insert("selectionChanged", result.selectionChanged);
    if (!result.path.isEmpty() || result.status == ResultStatus::Opened)
        json.insert("path", result.path);
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshCreateResult& result) {
    auto json = encode(result.command);
    addMeshIdentity(json, result.mesh);
    json.insert("vertexIdsByInputIndex", ids(result.vertexIdsByInputIndex));
    json.insert("faceIdsByInputIndex", ids(result.faceIdsByInputIndex));
    QJsonArray corners;
    for (const auto& face : result.cornerIdsByFace)
        corners.append(ids(face));
    json.insert("cornerIdsByFace", corners);
    return json;
}
QJsonObject ApiJsonCodec::encode(const EntityDuplicateResult& result) {
    auto json = encode(result.command);
    QJsonArray mapping;
    for (const auto& [source, copy] : result.entityIdMap)
        mapping.append(QJsonObject{{"sourceEntityId", QString::number(source)},
                                   {"entityId", QString::number(copy)}});
    json.insert("entityIdMap", mapping);
    return json;
}
QJsonObject ApiJsonCodec::encode(const CollectionMutationResult& result) {
    auto json = encode(result.command);
    json.insert("collectionId", QString::number(result.collectionId));
    return json;
}
QJsonObject ApiJsonCodec::encode(const ImportGltfResult& result) {
    auto json = encode(result.command);
    json.insert("rootEntityId", QString::number(result.rootEntityId));
    json.insert("warnings", QJsonArray::fromStringList(result.warnings));
    return json;
}
QJsonObject ApiJsonCodec::encode(const ExportObjResult& result) {
    auto json = encode(result.command);
    json.insert("entityId", QString::number(result.entityId));
    json.insert("mode", result.mode == ExportObjMode::Evaluated ? "evaluated" : "source");
    json.insert("byteLength", qint64(result.byteLength));
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshSummaryResult& result) {
    auto json = encodeState(result.state);
    addMeshIdentity(json, result.mesh);
    json.insert("source", QJsonObject{{"vertexCount", qint64(result.source.vertexCount)},
                                     {"faceCount", qint64(result.source.faceCount)},
                                     {"cornerCount", qint64(result.source.cornerCount)},
                                     {"edgeCount", qint64(result.source.edgeCount)},
                                     {"boundaryEdgeCount", qint64(result.source.boundaryEdgeCount)},
                                     {"bounds", boundsJson(result.source.bounds)}});
    json.insert("evaluated", QJsonObject{{"vertexCount", qint64(result.evaluated.vertexCount)},
                                        {"faceCount", qint64(result.evaluated.faceCount)},
                                        {"triangleCount", qint64(result.evaluated.triangleCount)},
                                        {"bounds", boundsJson(result.evaluated.bounds)}});
    json.insert("modifiers", encode(result.modifiers));
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshSourcePageResult& result) {
    auto json = encodeState(result.state);
    addMeshIdentity(json, result.mesh);
    json.insert("domain", domainName(result.domain));
    json.insert("fields", fieldNames(result.fields));
    QJsonArray vertices;
    for (const auto& vertex : result.vertices) {
        QJsonObject value{{"vertexId", QString::number(vertex.vertexId)}};
        if (vertex.position)
            value.insert("position", vector(*vertex.position));
        vertices.append(value);
    }
    QJsonArray faces;
    for (const auto& face : result.faces) {
        QJsonArray corners;
        for (const auto& corner : face.corners) {
            QJsonObject value{{"cornerId", QString::number(corner.cornerId)}, {"vertexId", QString::number(corner.vertexId)}};
            if (corner.attributes) {
                const auto& attributes = *corner.attributes;
                value.insert("uv", QJsonArray{attributes.uv.x, attributes.uv.y});
                value.insert("color", vector(attributes.color));
                value.insert("normal", attributes.normal ? QJsonValue(vector(*attributes.normal)) : QJsonValue(QJsonValue::Null));
            }
            corners.append(value);
        }
        faces.append(QJsonObject{{"faceId", QString::number(face.faceId)}, {"material", "0"}, {"corners", corners}});
    }
    json.insert("vertices", vertices);
    json.insert("faces", faces);
    json.insert("nextCursor", result.nextCursor ? QJsonValue(cursorJson(*result.nextCursor)) : QJsonValue(QJsonValue::Null));
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshExtrudeResult& result) {
    auto json = encode(result.command);
    addMeshIdentity(json, result.mesh);
    json.insert("capFaceIds", ids(result.capFaceIds));
    json.insert("sideFaceIds", ids(result.sideFaceIds));
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshInsetResult& result) {
    auto json = encode(result.command);
    addMeshIdentity(json, result.mesh);
    json.insert("innerFaceIds", ids(result.innerFaceIds));
    json.insert("rimFaceIds", ids(result.rimFaceIds));
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshCommandResult& result) {
    auto json = encode(result.command);
    addMeshIdentity(json, result.mesh);
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshTransformComponentsResult& result) {
    auto json = encode(static_cast<const MeshCommandResult&>(result));
    json.insert("affectedVertexIds", ids(result.affectedVertexIds));
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshBevelEdgeResult& result) {
    auto json = encode(static_cast<const MeshCommandResult&>(result));
    json.insert("bevelFaceId", QString::number(result.bevelFaceId));
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshLoopCutResult& result) {
    auto json = encode(static_cast<const MeshCommandResult&>(result));
    json.insert("cutEdges", edgesJson(result.cutEdges));
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshDeleteComponentsResult& result) {
    auto json = encode(static_cast<const MeshCommandResult&>(result));
    json.insert("deletedVertexIds", ids(result.deletedVertexIds));
    json.insert("deletedFaceIds", ids(result.deletedFaceIds));
    json.insert("deletedCornerIds", ids(result.deletedCornerIds));
    json.insert("deletedEdges", edgesJson(result.deletedEdges));
    return json;
}
QJsonObject ApiJsonCodec::encode(const MeshFillFaceResult& result) {
    auto json = encode(static_cast<const MeshCommandResult&>(result));
    json.insert("faceId", QString::number(result.faceId));
    return json;
}
QJsonObject ApiJsonCodec::encode(const ModifierState& state) {
    QJsonArray order;
    for (const auto modifier : state.order)
        order.append(modifierName(modifier));
    return {{"order", order},
            {"mirror", mirrorJson(state.mirror)},
            {"subdivision", subdivisionJson(state.subdivision)}};
}
QJsonObject ApiJsonCodec::encode(const ModifierCommandResult& result) {
    auto json = encode(static_cast<const MeshCommandResult&>(result));
    json.insert("modifiers", encode(result.modifiers));
    json.insert("requeryRequired", result.requeryRequired);
    return json;
}
QJsonObject ApiJsonCodec::invoke(EditorApiService& service, const QString& method,
                                 const QJsonValue& params, FileAccess access, BeforeCommitGuard guard) {
    auto decoded = decodeRequest(method, params);
    if (!decoded.hasValue()) {
        decoded.error->state = service.documentState();
        return {{"error", encodeError(*decoded.error)}};
    }
    if (const auto failure = service.checkThread())
        return {{"error", encodeError(*failure)}};
    ScopedCommitGuard scopedGuard(service, std::move(guard));
    const auto& request = *decoded.value;
    if (method == "system.describe")
        return response(service.describe());
    if (method == "document.current")
        return response(service.currentDocument());
    if (method == "scene.getSummary")
        return response(service.sceneSummary(std::get<DocumentRequest>(request)));
    if (method == "scene.listEntities")
        return response(service.listEntities(std::get<EntityListRequest>(request)));
    if (method == "entity.get")
        return response(service.entity(std::get<EntityGetRequest>(request)));
    if (method == "entity.create")
        return response(service.createEntity(std::get<EntityCreateRequest>(request)));
    if (method == "entity.update")
        return response(service.updateEntity(std::get<EntityUpdateRequest>(request)));
    if (method == "batch.createEntities")
        return response(service.createEntities(std::get<BatchCreateEntitiesRequest>(request)));
    if (method == "batch.setTransforms")
        return response(service.setTransforms(std::get<BatchSetTransformsRequest>(request)));
    if (method == "entity.duplicate")
        return response(service.duplicateEntity(std::get<EntityDuplicateRequest>(request)));
    if (method == "entity.delete")
        return response(service.deleteEntity(std::get<EntityDeleteRequest>(request)));
    if (method == "entity.setParent")
        return response(service.setParent(std::get<EntitySetParentRequest>(request)));
    if (method == "collection.create")
        return response(service.createCollection(std::get<CollectionCreateRequest>(request)));
    if (method == "collection.update")
        return response(service.updateCollection(std::get<CollectionUpdateRequest>(request)));
    if (method == "collection.delete")
        return response(service.deleteCollection(std::get<CollectionDeleteRequest>(request)));
    if (method == "collection.assign")
        return response(service.assignCollection(std::get<CollectionAssignRequest>(request)));
    if (method == "camera.create")
        return response(service.createCamera(std::get<CameraCreateRequest>(request)));
    if (method == "camera.update")
        return response(service.updateCamera(std::get<CameraUpdateRequest>(request)));
    if (method == "light.create")
        return response(service.createLight(std::get<LightCreateRequest>(request)));
    if (method == "light.update")
        return response(service.updateLight(std::get<LightUpdateRequest>(request)));
    if (method == "mesh.create")
        return response(service.createMesh(std::get<MeshCreateRequest>(request)));
    if (method == "mesh.getSummary")
        return response(service.meshSummary(std::get<MeshTargetRequest>(request)));
    if (method == "mesh.readSourcePage")
        return response(service.readSourcePage(std::get<MeshSourcePageRequest>(request)));
    if (method == "mesh.extrudeRegion")
        return response(service.extrudeRegion(std::get<MeshExtrudeRequest>(request)));
    if (method == "mesh.insetFace")
        return response(service.insetFace(std::get<MeshInsetRequest>(request)));
    if (method == "mesh.makeEditable")
        return response(service.makeEditable(std::get<MeshMakeEditableRequest>(request)));
    if (method == "mesh.transformComponents")
        return response(
            service.transformComponents(std::get<MeshTransformComponentsRequest>(request)));
    if (method == "mesh.bevelEdge")
        return response(service.bevelEdge(std::get<MeshBevelEdgeRequest>(request)));
    if (method == "mesh.loopCut")
        return response(service.loopCut(std::get<MeshLoopCutRequest>(request)));
    if (method == "mesh.deleteComponents")
        return response(service.deleteComponents(std::get<MeshDeleteComponentsRequest>(request)));
    if (method == "mesh.fillFace")
        return response(service.fillFace(std::get<MeshFillFaceRequest>(request)));
    if (method == "modifier.setMirror")
        return response(service.setMirror(std::get<ModifierSetMirrorRequest>(request)));
    if (method == "modifier.setSubdivision")
        return response(service.setSubdivision(std::get<ModifierSetSubdivisionRequest>(request)));
    if (method == "modifier.apply")
        return response(service.applyModifier(std::get<ModifierApplyRequest>(request)));
    if (method == "history.getState")
        return response(service.historyState(std::get<DocumentRequest>(request)));
    if (method == "history.undo")
        return response(service.undo(std::get<HistoryMutationRequest>(request)));
    if (method == "history.redo")
        return response(service.redo(std::get<HistoryMutationRequest>(request)));
    if (method == "file.saveAs")
        return response(service.saveAs(std::get<FileRequest>(request), access));
    if (method == "file.save")
        return response(service.save(std::get<FileSaveRequest>(request), access));
    if (method == "file.importGltf")
        return response(service.importGltf(std::get<ImportGltfRequest>(request), access));
    if (method == "file.exportObj")
        return response(service.exportObj(std::get<ExportObjRequest>(request), access));
    if (method == "document.new")
        return response(service.newDocument(std::get<DocumentNewRequest>(request)));
    return response(service.openDocument(std::get<FileRequest>(request), access));
}
} // namespace mini3d::editor::api
