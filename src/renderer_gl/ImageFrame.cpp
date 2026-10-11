/*
 * 模块名: ImageFrame
 * 功能概述: 验证母版并序列化完整来源、相机、色彩和几何 stage 出处。
 * 对外接口: ImageFrame.h。
 * 依赖关系: Core 来源比较、Qt JSON、C++ bit_cast；无文件或 GL 副作用。
 * 输入输出: FLOAT32 母版到只读拥有者及精确 metadata。
 * 异常与错误: 不完整来源或非法数值拒绝；分配异常传播。
 * 维护说明: binary64 用 IEEE 位串保存，不通过 JSON 浮点舍入身份。
 */
#include "ImageFrame.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <bit>
#include <cmath>
#include <set>
#include <utility>

namespace mini3d::renderer_gl {
namespace {
QString decimal(std::uint64_t value) { return QString::number(qulonglong(value)); }
QString binary64(double value) {
    return QString::number(qulonglong(std::bit_cast<std::uint64_t>(value)), 16)
        .rightJustified(16, QLatin1Char('0')).toUpper();
}
template <typename T> QString uuid(const T& value) {
    core::DocumentUuid document{value.bytes};
    return QString::fromStdString(core::documentUuidString(document));
}
QString hash(const std::array<std::uint8_t, 32>& value) {
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(value.data()),
                                        qsizetype(value.size())).toHex().toUpper());
}
QJsonObject liveHandle(const core::LiveSourceHandle& handle) {
    return {{"instanceId", QString::fromStdString(handle.instanceId)},
            {"documentId", QString::fromStdString(handle.documentId)}};
}
QJsonObject sourceJson(const core::SourceIdentity& source) {
    QJsonArray resources;
    for (const auto& resource : source.resourceSetIdentity) {
        const char* kind = resource.kind == core::SceneAssetKind::Mesh ? "Mesh"
                         : resource.kind == core::SceneAssetKind::Texture ? "Texture" : "Material";
        resources.append(QJsonObject{{"asset", decimal(resource.asset.value)}, {"kind", kind},
            {"version", decimal(resource.version)}, {"contentSha256", hash(resource.contentHash)},
            {"ownedByteCount", decimal(resource.bytes ? resource.bytes->size() : 0)}});
    }
    QJsonObject handle;
    if (const auto* live = std::get_if<core::LiveSourceHandle>(&source.sourceHandle))
        handle = liveHandle(*live);
    else {
        core::DocumentUuid bundle{std::get<core::FrozenSourceHandle>(source.sourceHandle).sourceBundleId};
        handle = {{"sourceBundleId", uuid(bundle)}};
    }
    const char* kind = source.sourceKind == core::SourceKind::LiveFormal ? "LiveFormal"
                     : source.sourceKind == core::SourceKind::FrozenFormal ? "FrozenFormal" : "LiveDraft";
    const char* profile = source.evaluationProfile == core::EvaluationProfile::JointPreviewJ1
                             ? "JointPreviewJ1"
                         : source.evaluationProfile == core::EvaluationProfile::OfflineJ1
                             ? "OfflineJ1" : "BasicAnimationJ1";
    const char* purpose = source.purpose == core::EvaluationPurpose::Preview ? "Preview"
                         : source.purpose == core::EvaluationPurpose::Output ? "Output" : "Sample";
    QJsonValue draft = QJsonValue::Null;
    if (source.draftIdentity) {
        const auto& value = *source.draftIdentity;
        draft = QJsonObject{{"draftId", decimal(value.draftId)}, {"generation", decimal(value.generation)},
            {"sessionRevision", decimal(value.sessionRevision)},
            {"baseSource", QJsonObject{{"documentUuid", uuid(value.baseSource.documentUuid)},
                {"contentRevision", decimal(value.baseSource.contentRevision)},
                {"evaluationSessionId", uuid(value.baseSource.evaluationSessionId)},
                {"evaluationId", decimal(value.baseSource.evaluationId)}}}};
    }
    return {{"documentUuid", uuid(source.documentUuid)}, {"sourceKind", kind},
        {"sourceHandle", handle}, {"contentRevision", decimal(source.contentRevision)},
        {"resourceSetIdentity", resources}, {"frameTimeBinary64", binary64(source.frameTime)},
        {"rationalFps", QJsonObject{{"numerator", qint64(source.rationalFps.numerator)},
                                    {"denominator", qint64(source.rationalFps.denominator)}}},
        {"algorithmBuild", QString::fromStdString(source.algorithmBuild)},
        {"evaluationProfile", profile}, {"profileVersion", qint64(source.profileVersion)},
        {"purpose", purpose},
        {"authorContextIdentity", QJsonObject{{"documentUuid", uuid(source.authorContextIdentity.documentUuid)},
             {"contentRevision", decimal(source.authorContextIdentity.contentRevision)},
             {"revisionDomain", liveHandle(source.authorContextIdentity.revisionDomain)}}},
        {"evaluationSessionId", uuid(source.evaluationSessionId)},
        {"evaluationId", decimal(source.evaluationId)}, {"draftIdentity", draft}};
}
template <typename Matrix> QJsonArray matrixJson(const Matrix& matrix) {
    QJsonArray values;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            values.append(binary64(matrix[column][row]));
    return values;
}
bool finiteMatrix(const glm::dmat4& matrix) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row])) return false;
    return true;
}
} // namespace

bool isAllowedImageOutputDerivation(const core::SourceIdentity& origin,
                                    const core::SourceIdentity& output) {
    if (!core::isSourceIdentityValid(origin) || !core::isSourceIdentityValid(output) ||
        origin.sourceKind != core::SourceKind::LiveFormal || origin.draftIdentity ||
        origin.evaluationProfile != core::EvaluationProfile::JointPreviewJ1 ||
        origin.purpose != core::EvaluationPurpose::Preview ||
        origin.algorithmBuild.find("/base-only-1") != std::string::npos ||
        output.sourceKind != core::SourceKind::FrozenFormal || output.draftIdentity ||
        output.evaluationProfile != core::EvaluationProfile::OfflineJ1 ||
        output.purpose != core::EvaluationPurpose::Output || output.evaluationId == 0 ||
        output.evaluationSessionId == origin.evaluationSessionId)
        return false;
    // 严格同源比较本身不变：只在明确验证派生字段后恢复 origin 的 profile 再比较。
    auto comparable = output;
    comparable.evaluationProfile = origin.evaluationProfile;
    return core::isSameWorkSource(origin, comparable);
}

ImageFrame::ImageFrame(FrozenRenderPixels&& pixels, core::SourceIdentity origin,
                       ImageFrameStamp stamp)
    : pixels_(std::move(pixels)), originSource_(std::move(origin)), stamp_(std::move(stamp)) {}

std::shared_ptr<const ImageFrame> ImageFrame::create(
    FrozenRenderPixels&& pixels, const core::SourceIdentity& origin,
    const ImageFrameStamp& stamp, QString& error) {
    const auto reject = [&](const QString& message) -> std::shared_ptr<const ImageFrame> {
        error = message;
        return {};
    };
    const bool output = pixels.source.purpose == core::EvaluationPurpose::Output;
    if (!core::isSourceIdentityValid(pixels.source) || pixels.source.evaluationId == 0 ||
        stamp.sessionId.isNil() || stamp.frameId == 0 || stamp.contextGeneration == 0 ||
        !std::isfinite(stamp.devicePixelRatio) || stamp.devicePixelRatio <= 0 ||
        (output ? !isAllowedImageOutputDerivation(origin, pixels.source)
                : (origin != pixels.source || pixels.source.purpose != core::EvaluationPurpose::Preview ||
                   (pixels.source.sourceKind != core::SourceKind::LiveFormal &&
                    pixels.source.sourceKind != core::SourceKind::LiveDraft))))
        return reject(QStringLiteral("ImageFrame 来源或本次消费身份不完整。"));
    if (pixels.width <= 0 || pixels.height <= 0 ||
        (output && (pixels.width != 1280 || pixels.height != 720 ||
                    stamp.devicePixelRatio != 1 || stamp.cameraEntity == 0)) ||
        pixels.beauty.size() != std::size_t(pixels.width) * std::size_t(pixels.height) ||
        pixels.coverage.size() != pixels.beauty.size())
        return reject(QStringLiteral("ImageFrame 尺寸或母版 plane 不完整。"));
    if (!finiteMatrix(pixels.camera.viewMatrix) || !finiteMatrix(pixels.camera.projectionMatrix) ||
        !std::isfinite(pixels.camera.position.x) || !std::isfinite(pixels.camera.position.y) ||
        !std::isfinite(pixels.camera.position.z) || pixels.backendBuild.isEmpty() ||
        !pixels.color.libraryVersion.startsWith(QStringLiteral("2.5.")) ||
        pixels.color.configSha256.toUpper() !=
            QByteArray("580836D65890F3226EA7E38B67F0A1F55C4125149B3F311E6E3E7E98B8999927") ||
        pixels.color.processorCacheId.isEmpty() || pixels.color.shaderSha256.size() != 64 ||
        pixels.color.display != QStringLiteral("sRGB") || pixels.color.view != QStringLiteral("Standard") ||
        pixels.color.look != QStringLiteral("None") || pixels.color.exposure != 0 || pixels.color.gamma != 1)
        return reject(QStringLiteral("ImageFrame 相机、backend 或实际 OCIO 出处不完整。"));
    std::set<core::EntityId> entities;
    for (const auto& stage : pixels.geometryStages)
        if (stage.entity == 0 || !entities.insert(stage.entity).second || !finiteMatrix(stage.worldAffine))
            return reject(QStringLiteral("ImageFrame 几何 stage 身份无效。"));
    if (output && !entities.contains(stamp.cameraEntity))
        return reject(QStringLiteral("ImageFrame 缺少本次完整几何/设备 stage。"));
    for (std::size_t index = 0; index < pixels.beauty.size(); ++index) {
        const auto& rgba = pixels.beauty[index];
        if (!std::isfinite(rgba.r) || !std::isfinite(rgba.g) || !std::isfinite(rgba.b) ||
            !std::isfinite(rgba.a) || rgba.a < 0 || rgba.a > 1 ||
            !std::isfinite(pixels.coverage[index]) || pixels.coverage[index] < 0 ||
            pixels.coverage[index] > 1)
            return reject(QStringLiteral("ImageFrame 含非法 Beauty/coverage 原值。"));
    }
    error.clear();
    return std::shared_ptr<const ImageFrame>(new ImageFrame(std::move(pixels), origin, stamp));
}
const FrozenRenderPixels& ImageFrame::pixels() const { return pixels_; }
const core::SourceIdentity& ImageFrame::originSource() const { return originSource_; }
const ImageFrameStamp& ImageFrame::stamp() const { return stamp_; }

QByteArray ImageFrame::provenanceJson(ImageFileFormat format) const {
    const bool png = format == ImageFileFormat::Png;
    const auto& pixels = pixels_;
    const auto seconds = core::frameTimeSeconds(pixels.source.frameTime, pixels.source.rationalFps);
    QJsonArray stages;
    for (const auto& stage : pixels.geometryStages) {
        const char* primitive = stage.primitive == core::PrimitiveKind::Cube ? "Cube"
                              : stage.primitive == core::PrimitiveKind::Sphere ? "Sphere"
                              : stage.primitive == core::PrimitiveKind::Plane ? "Plane" : "Empty";
        stages.append(QJsonObject{{"entity", decimal(stage.entity)}, {"primitive", primitive},
            {"mesh", decimal(stage.mesh)}, {"contentVersion", decimal(stage.contentVersion)},
            {"worldAffineBinary64ColumnMajor", matrixJson(stage.worldAffine)}});
    }
    const QJsonArray window{0, 0, pixels.width, pixels.height};
    const QJsonObject color{{"library", "OpenColorIO"}, {"libraryVersion", pixels.color.libraryVersion},
        {"configSha256", QString::fromLatin1(pixels.color.configSha256)},
        {"processorCacheId", QString::fromLatin1(pixels.color.processorCacheId)},
        {"shaderSha256", QString::fromLatin1(pixels.color.shaderSha256)},
        {"display", pixels.color.display}, {"view", pixels.color.view}, {"look", pixels.color.look},
        {"exposureBinary64", binary64(pixels.color.exposure)}, {"gammaBinary64", binary64(pixels.color.gamma)},
        {"standardTransfer", "owned-continuous-moncurve"}};
    const QJsonObject metadata{{"schema", "mini3d.ImageFrame/1"},
        {"originSource", sourceJson(originSource_)}, {"source", sourceJson(pixels.source)},
        {"derivation", pixels.source.purpose == core::EvaluationPurpose::Output
                           ? "LiveFormal-JointPreview-Preview-to-FrozenFormal-Offline-Output/1" : "identity"},
        {"evaluatedTimeBinary64", binary64(pixels.source.frameTime)},
        {"secondsBinary64", binary64(seconds.value())},
        {"frameSessionId", uuid(stamp_.sessionId)}, {"frameId", decimal(stamp_.frameId)},
        {"contextGeneration", decimal(stamp_.contextGeneration)},
        {"viewportRevision", decimal(stamp_.viewportRevision)},
        {"camera", QJsonObject{{"entity", decimal(stamp_.cameraEntity)},
            {"precisionOrigin", "checked-device-float32-consumption-promoted-to-binary64"},
            {"positionBinary64", QJsonArray{binary64(pixels.camera.position.x),
                                             binary64(pixels.camera.position.y), binary64(pixels.camera.position.z)}},
            {"viewBinary64ColumnMajor", matrixJson(pixels.camera.viewMatrix)},
            {"projectionBinary64ColumnMajor", matrixJson(pixels.camera.projectionMatrix)}}},
        {"settings", QJsonObject{{"width", pixels.width}, {"height", pixels.height},
            {"devicePixelRatioBinary64", binary64(stamp_.devicePixelRatio)}, {"samples", 0},
            {"temporalSamples", 1}, {"taa", false}, {"depthOfField", false},
            {"background", "linear-black"}, {"uiOverlaysInMaster", false},
            {"rowOrder", "TopDown"}, {"scan", "Progressive"},
            {"displayWindowTopLeftHalfOpen", window}, {"dataWindowTopLeftHalfOpen", window},
            {"pixelAspect", QJsonArray{1, 1}}}},
        {"backend", QJsonObject{{"build", pixels.backendBuild}, {"api", "OpenGL4.1Core"},
                                  {"roughnessAlphaRegularization", "max(r*r,0.001)"}}},
        {"color", color}, {"geometryStages", stages},
        {"master", QJsonObject{{"encoding", "scene-linear"}, {"colorSpace", "LinearRec709"},
            {"alpha", "associated"}, {"temporalMeaning", "instant"}, {"encodingOwner", "renderer"},
            {"planes", QJsonArray{
                QJsonObject{{"name", "Beauty"}, {"role", "color"}, {"channels", "RGBA"},
                    {"sampleType", "FLOAT32"}, {"pixelStrideBytes", int(sizeof(glm::vec4))},
                    {"rowStrideBytes", decimal(std::uint64_t(pixels.width) * sizeof(glm::vec4))}},
                QJsonObject{{"name", "coverage"}, {"role", "coverage"}, {"channels", "coverage"},
                    {"sampleType", "FLOAT32"}, {"pixelStrideBytes", int(sizeof(float))},
                    {"rowStrideBytes", decimal(std::uint64_t(pixels.width) * sizeof(float))}}}}}},
        {"file", QJsonObject{{"format", png ? "PNG" : "EXR"},
            {"sampleType", png ? "RGBA8" : "FLOAT32"}, {"alpha", png ? "straight" : "associated"},
            {"planes", png ? QJsonArray{"Beauty"} : QJsonArray{"R", "G", "B", "A", "coverage"}},
            {"independentCoverageSaved", !png}, {"rgbOcioApplications", png ? 1 : 0},
            {"alphaOcioApplications", 0}, {"backgroundPrecomposite", false},
            {"colorSpace", png ? "owned-Standard-display" : "LinearRec709"},
            {"encodingOwner", "ImageFrameOutput"},
            {"compression", png ? "PNG-lossless" : "ZIP-lossless"}}}};
    return QJsonDocument(metadata).toJson(QJsonDocument::Compact);
}
} // namespace mini3d::renderer_gl
