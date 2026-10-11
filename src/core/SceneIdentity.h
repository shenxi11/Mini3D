/*
 * 模块名: SceneIdentity
 * 功能概述: 定义作者域、强类型稳定 ID、规范时间和共享求值来源。
 * 对外接口: DocumentUuid、RationalFps、SourceIdentity 及纯校验/比较函数。
 * 依赖关系: C++ 标准库；不依赖 Qt、文件系统或 OpenGL。
 * 输入输出: 作者和会话身份到可验证的来源记录。
 * 异常与错误: UUID 生成分配/随机源错误传播；非法时间或来源返回 false/空值。
 * 维护说明: 运行期 AssetId/DocumentHandle 保持原义；稳定 ID 不在取样期间分配。
 */
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace mini3d::core {
using FrameTime = double;

/** @brief 文档作者域；载入/另存持久化由格式集成负责，不能用运行期 handle 替代。 */
struct DocumentUuid {
    std::array<std::uint8_t, 16> bytes{};
    [[nodiscard]] bool isNil() const;
    auto operator<=>(const DocumentUuid&) const = default;
};
/** @brief 独立求值会话的 UUID；与文档 UUID 不隐式转换。 */
struct EvaluationSessionId {
    std::array<std::uint8_t, 16> bytes{};
    [[nodiscard]] bool isNil() const;
    auto operator<=>(const EvaluationSessionId&) const = default;
};
[[nodiscard]] DocumentUuid createDocumentUuid();
[[nodiscard]] EvaluationSessionId createEvaluationSessionId();
[[nodiscard]] std::string documentUuidString(const DocumentUuid& uuid);
[[nodiscard]] std::optional<DocumentUuid> parseDocumentUuid(std::string_view text);

/** @brief 非零稳定 ID；标签保证相同数字不能混用，分配由 Scene 正式发布边界拥有。 */
template <class Tag> struct StableId {
    std::uint64_t value = 0;
    constexpr StableId() = default;
    explicit constexpr StableId(std::uint64_t number) : value(number) {}
    [[nodiscard]] constexpr bool isValid() const { return value != 0; }
    auto operator<=>(const StableId&) const = default;
};
using StableAssetId = StableId<struct StableAssetTag>;
using MaterialId = StableId<struct MaterialTag>;
using MaterialSlotId = StableId<struct MaterialSlotTag>;
using ActionId = StableId<struct ActionTag>;
using ActionSlotId = StableId<struct ActionSlotTag>;
using CurveId = StableId<struct CurveTag>;
using KeyId = StableId<struct KeyTag>;
static_assert(std::is_default_constructible_v<MaterialId>);
static_assert(!std::is_convertible_v<std::uint64_t, MaterialId>);
static_assert(!std::is_constructible_v<MaterialId, StableAssetId>);
static_assert(!std::is_constructible_v<MaterialSlotId, ActionSlotId>);

/** @brief Material 与 Mesh/Texture 共用 asset 水位；没有独立的材质分配器。 */
struct SceneIdHighWatermarks {
    std::uint64_t asset = 0;
    std::uint64_t materialSlot = 0;
    std::uint64_t action = 0;
    std::uint64_t actionSlot = 0;
    std::uint64_t curve = 0;
    std::uint64_t key = 0;
    bool operator==(const SceneIdHighWatermarks&) const = default;
};

/** @brief 正规范有理帧率，n/d 已约分且位于闭区间 1..120。 */
struct RationalFps {
    std::uint32_t numerator = 24;
    std::uint32_t denominator = 1;
    bool operator==(const RationalFps&) const = default;
};
[[nodiscard]] bool isRationalFpsValid(const RationalFps& fps);
[[nodiscard]] std::optional<RationalFps> makeRationalFps(std::uint32_t numerator,
                                                        std::uint32_t denominator);
/** @brief frameOrigin=1；非法帧率/时间返回空值，不移动作者键。 */
[[nodiscard]] std::optional<double> frameTimeSeconds(FrameTime frame, const RationalFps& fps);
enum class AnimationLoopPolicy { LegacyHalfOpen, InclusiveFrameStep };
/** @brief 单一播放时钟的纯时间计算；输入须为已验证范围、规范帧率和非负 elapsed。
 * Inclusive 末帧驻留一帧，Legacy 保留半开周期；atEnd 仅报告非循环到达末帧。
 */
[[nodiscard]] FrameTime animationPlaybackFrame(FrameTime playStart, double elapsedSeconds,
                                               const RationalFps& fps, FrameTime start,
                                               FrameTime end, bool looping,
                                               AnimationLoopPolicy policy,
                                               bool* atEnd = nullptr);

enum class SourceKind { LiveFormal, LiveDraft, FrozenFormal };
enum class EvaluationPurpose { Preview, Sample, Output };
enum class EvaluationProfile { BasicAnimationJ1, JointPreviewJ1, OfflineJ1 };
struct LiveSourceHandle {
    std::string instanceId;
    std::string documentId;
    bool operator==(const LiveSourceHandle&) const = default;
};
struct FrozenSourceHandle {
    std::array<std::uint8_t, 16> sourceBundleId{};
    bool operator==(const FrozenSourceHandle&) const = default;
};
using SourceHandle = std::variant<LiveSourceHandle, FrozenSourceHandle>;

enum class SceneAssetKind { Mesh, Texture, Material };
/** @brief 外部消费资源的真实版本/hash及只读字节；无外部资源使用空清单。 */
struct ResourceIdentity {
    StableAssetId asset;
    SceneAssetKind kind = SceneAssetKind::Mesh;
    std::uint64_t version = 0;
    std::array<std::uint8_t, 32> contentHash{};
    std::shared_ptr<const std::vector<std::byte>> bytes;
    bool operator==(const ResourceIdentity& other) const;
};
using ResourceSetIdentity = std::vector<ResourceIdentity>;
struct AuthorContextIdentity {
    DocumentUuid documentUuid;
    std::uint64_t contentRevision = 0;
    LiveSourceHandle revisionDomain;
    bool operator==(const AuthorContextIdentity&) const = default;
};
struct DraftBaseSource {
    DocumentUuid documentUuid;
    std::uint64_t contentRevision = 0;
    EvaluationSessionId evaluationSessionId;
    std::uint64_t evaluationId = 0;
    bool operator==(const DraftBaseSource&) const = default;
};
struct DraftIdentity {
    std::uint64_t draftId = 0;
    std::uint64_t generation = 0;
    DraftBaseSource baseSource;
    std::uint64_t sessionRevision = 0;
    bool operator==(const DraftIdentity&) const = default;
};
/** @brief 共同来源头；准备候选允许 evaluationId=0，正式发布后才绑定真实 revision/ID。 */
struct SourceIdentity {
    DocumentUuid documentUuid;
    SourceKind sourceKind = SourceKind::LiveFormal;
    SourceHandle sourceHandle = LiveSourceHandle{};
    std::uint64_t contentRevision = 0;
    ResourceSetIdentity resourceSetIdentity;
    FrameTime frameTime = 1;
    RationalFps rationalFps;
    std::string algorithmBuild = "J1-CONTRACT-2/double-affine-1";
    EvaluationProfile evaluationProfile = EvaluationProfile::BasicAnimationJ1;
    std::uint32_t profileVersion = 1;
    EvaluationPurpose purpose = EvaluationPurpose::Preview;
    AuthorContextIdentity authorContextIdentity;
    EvaluationSessionId evaluationSessionId;
    std::uint64_t evaluationId = 0;
    std::optional<DraftIdentity> draftIdentity;
    bool operator==(const SourceIdentity&) const = default;
};
[[nodiscard]] bool isSourceIdentityValid(const SourceIdentity& source);
/** @brief 独立会话的同源作品比较；不要求会话/evaluation/输出用途相同。 */
[[nodiscard]] bool isSameWorkSource(const SourceIdentity& left, const SourceIdentity& right);
} // namespace mini3d::core
