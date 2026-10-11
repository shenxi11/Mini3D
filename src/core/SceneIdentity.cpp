/*
 * 模块名: SceneIdentity
 * 功能概述: 实现 UUID、规范帧率和来源记录的严格校验。
 * 对外接口: SceneIdentity.h 声明的纯函数。
 * 依赖关系: Animation 时间域、C++ 随机源/整数运算。
 * 输入输出: 精确身份和时间到规范结果；不读取 live 文档。
 * 异常与错误: UUID 随机源异常传播；非法值返回 false/空值。
 * 维护说明: 帧率范围以 uint64 交叉乘法检查，不借浮点舍入放行。
 */
#include "SceneIdentity.h"

#include "Animation.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <set>

namespace mini3d::core {
namespace {
template <std::size_t Size> bool allZero(const std::array<std::uint8_t, Size>& bytes) {
    return std::all_of(bytes.begin(), bytes.end(), [](auto byte) { return byte == 0; });
}
std::array<std::uint8_t, 16> uuidBytes() {
    std::random_device random;
    std::array<std::uint8_t, 16> bytes;
    for (auto& byte : bytes)
        byte = static_cast<std::uint8_t>(random());
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);
    return bytes;
}
int hexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}
} // namespace
bool DocumentUuid::isNil() const { return allZero(bytes); }
bool EvaluationSessionId::isNil() const { return allZero(bytes); }
DocumentUuid createDocumentUuid() { return {uuidBytes()}; }
EvaluationSessionId createEvaluationSessionId() { return {uuidBytes()}; }

std::string documentUuidString(const DocumentUuid& uuid) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(36);
    for (std::size_t index = 0; index < uuid.bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) result += '-';
        result += digits[uuid.bytes[index] >> 4];
        result += digits[uuid.bytes[index] & 0x0fU];
    }
    return result;
}
std::optional<DocumentUuid> parseDocumentUuid(std::string_view text) {
    if (text.size() != 36) return std::nullopt;
    DocumentUuid result;
    std::size_t offset = 0;
    for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            if (text[offset++] != '-') return std::nullopt;
        }
        const int high = hexDigit(text[offset++]);
        const int low = hexDigit(text[offset++]);
        if (high < 0 || low < 0) return std::nullopt;
        result.bytes[index] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return result.isNil() ? std::nullopt : std::optional(result);
}
bool isRationalFpsValid(const RationalFps& fps) {
    return fps.numerator != 0 && fps.denominator != 0 &&
           std::gcd(fps.numerator, fps.denominator) == 1 &&
           fps.numerator >= fps.denominator &&
           static_cast<std::uint64_t>(fps.numerator) <=
               static_cast<std::uint64_t>(fps.denominator) * 120;
}
std::optional<RationalFps> makeRationalFps(std::uint32_t numerator, std::uint32_t denominator) {
    if (numerator == 0 || denominator == 0) return std::nullopt;
    const auto divisor = std::gcd(numerator, denominator);
    RationalFps result{numerator / divisor, denominator / divisor};
    return isRationalFpsValid(result) ? std::optional(result) : std::nullopt;
}
std::optional<double> frameTimeSeconds(FrameTime frame, const RationalFps& fps) {
    if (!isAnimationFrameValid(frame) || !isRationalFpsValid(fps)) return std::nullopt;
    return (frame - 1.0) * fps.denominator / fps.numerator;
}
FrameTime animationPlaybackFrame(FrameTime playStart, double elapsedSeconds,
                                 const RationalFps& fps, FrameTime start, FrameTime end,
                                 bool looping, AnimationLoopPolicy policy, bool* atEnd) {
    const double rate = double(fps.numerator) / fps.denominator;
    const double rawFrame = playStart + elapsedSeconds * rate;
    if (atEnd)
        *atEnd = !looping && rawFrame >= end;
    if (start == end)
        return start;
    if (!looping)
        return std::min(rawFrame, end);
    const double duration = end - start;
    if (policy == AnimationLoopPolicy::LegacyHalfOpen)
        return start + std::fmod(rawFrame - start, duration);
    const double phase = std::fmod((playStart - start) + elapsedSeconds * rate, duration + 1.0);
    return start + std::min(phase, duration);
}
bool ResourceIdentity::operator==(const ResourceIdentity& other) const {
    return asset == other.asset && kind == other.kind && version == other.version &&
           contentHash == other.contentHash &&
           (bytes == other.bytes || (bytes && other.bytes && *bytes == *other.bytes));
}
bool isSourceIdentityValid(const SourceIdentity& source) {
    if (source.documentUuid.isNil() || source.contentRevision == 0 ||
        source.evaluationSessionId.isNil() || !isAnimationFrameValid(source.frameTime) ||
        !isRationalFpsValid(source.rationalFps) || source.algorithmBuild.empty() ||
        source.profileVersion == 0 ||
        source.authorContextIdentity.documentUuid != source.documentUuid ||
        source.authorContextIdentity.contentRevision != source.contentRevision ||
        source.authorContextIdentity.revisionDomain.instanceId.empty() ||
        source.authorContextIdentity.revisionDomain.documentId.empty())
        return false;
    if (source.evaluationProfile != EvaluationProfile::BasicAnimationJ1 &&
        source.evaluationProfile != EvaluationProfile::JointPreviewJ1 &&
        source.evaluationProfile != EvaluationProfile::OfflineJ1)
        return false;
    if (source.purpose != EvaluationPurpose::Preview && source.purpose != EvaluationPurpose::Sample &&
        source.purpose != EvaluationPurpose::Output)
        return false;
    if (source.sourceKind == SourceKind::FrozenFormal) {
        const auto* frozen = std::get_if<FrozenSourceHandle>(&source.sourceHandle);
        if (!frozen || allZero(frozen->sourceBundleId) || source.draftIdentity) return false;
    } else if (source.sourceKind == SourceKind::LiveFormal || source.sourceKind == SourceKind::LiveDraft) {
        const auto* live = std::get_if<LiveSourceHandle>(&source.sourceHandle);
        if (!live || live->instanceId.empty() || live->documentId.empty() ||
            source.authorContextIdentity.revisionDomain != *live) return false;
        if ((source.sourceKind == SourceKind::LiveDraft) != source.draftIdentity.has_value()) return false;
        if (source.draftIdentity) {
            const auto& draft = *source.draftIdentity;
            if (draft.draftId == 0 || draft.generation == 0 || draft.sessionRevision == 0 ||
                draft.baseSource.documentUuid != source.documentUuid ||
                draft.baseSource.contentRevision != source.contentRevision ||
                draft.baseSource.evaluationSessionId.isNil() || draft.baseSource.evaluationId == 0)
                return false;
        }
    } else return false;
    std::set<StableAssetId> ids;
    for (const auto& resource : source.resourceSetIdentity) {
        if (!resource.asset.isValid() || resource.version == 0 || !resource.bytes ||
            (resource.kind != SceneAssetKind::Mesh && resource.kind != SceneAssetKind::Texture &&
             resource.kind != SceneAssetKind::Material) ||
            allZero(resource.contentHash) || !ids.insert(resource.asset).second)
            return false;
    }
    return true;
}
bool isSameWorkSource(const SourceIdentity& left, const SourceIdentity& right) {
    return isSourceIdentityValid(left) && isSourceIdentityValid(right) &&
           (left.sourceKind == SourceKind::LiveDraft) == (right.sourceKind == SourceKind::LiveDraft) &&
           left.documentUuid == right.documentUuid && left.contentRevision == right.contentRevision &&
           left.resourceSetIdentity == right.resourceSetIdentity && left.frameTime == right.frameTime &&
           left.rationalFps == right.rationalFps && left.algorithmBuild == right.algorithmBuild &&
           left.evaluationProfile == right.evaluationProfile && left.profileVersion == right.profileVersion &&
           left.authorContextIdentity == right.authorContextIdentity && left.draftIdentity == right.draftIdentity;
}
} // namespace mini3d::core
