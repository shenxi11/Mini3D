/*
 * 模块名: ApiJsonCodec
 * 功能概述: 按冻结 Schema 统一解码请求和编码响应，JSON 不进入领域内核。
 * 对外接口: parseUint64、parseTransform、decodeRequest、canonicalParams、encode、invoke。
 * 依赖关系: ApiTypes、Qt JSON；invoke 只适配 EditorApiService。
 * 输入输出: JSON 值到强类型 DTO，DTO 到规范 wire 字段。
 * 异常与错误: 拒绝未知字段、非规范 ID、无效类型及 float 溢出。
 * 维护说明: 不拥有场景、历史或网络会话；默认文件调用为外部未授权。
 */
#pragma once

#include "ApiTypes.h"

#include <QJsonObject>
#include <QJsonValue>

namespace mini3d::editor::api {
class EditorApiService;
/** @brief 唯一 JSON 边界；所有 ID/版本均以十进制字符串往返。 */
class ApiJsonCodec final {
  public:
    /** @brief 严格解析 uint64 字符串；allowZero=false 用于真实对象身份。 */
    static ApiResult<std::uint64_t>
    parseUint64(const QJsonValue& value, const QString& fieldPath = {}, bool allowZero = true);
    /** @brief 检查 local TRS 和有限 float，保留有效原始旋转；归一化由共享提交业务负责。 */
    static ApiResult<core::Transform>
    parseTransform(const QJsonValue& value, const QString& fieldPath = QStringLiteral("transform"));
    /** @brief 严格解码完整请求；批次顶层元数据与每项字段分别校验。 */
    static ApiResult<ApiRequest> decodeRequest(const QString& method, const QJsonValue& params);
    /** @brief 解码后编码业务参数与实际默认值；桥会话/序号不参与业务摘要。 */
    static ApiResult<QJsonObject> canonicalParams(const QString& method, const QJsonValue& params);
    static QJsonObject encodeState(const DocumentState& state);
    static QJsonObject encodeError(const ApiError& error);
    static QJsonObject encode(const SystemDescription& result);
    static QJsonObject encode(const AnimationStateResult& result);
    static QJsonObject encode(const AnimationTrackPageResult& result);
    static QJsonObject encode(const AnimationKeyframePageResult& result);
    static QJsonObject encode(const AnimationSampleResult& result);
    static QJsonObject encode(const AnimationControlResult& result);
    static QJsonObject encode(const CurrentDocument& result);
    static QJsonObject encode(const SceneSummary& result);
    static QJsonObject encode(const EntityResult& result);
    static QJsonObject encode(const EntityListResult& result);
    static QJsonObject encode(const HistoryState& result);
    static QJsonObject encode(const MutationResult& result);
    static QJsonObject encode(const EntityDuplicateResult& result);
    static QJsonObject encode(const CollectionMutationResult& result);
    static QJsonObject encode(const ImportGltfResult& result);
    static QJsonObject encode(const ExportObjResult& result);
    static QJsonObject encode(const MeshCreateResult& result);
    static QJsonObject encode(const MeshSummaryResult& result);
    static QJsonObject encode(const MeshSourcePageResult& result);
    static QJsonObject encode(const MeshExtrudeResult& result);
    static QJsonObject encode(const MeshInsetResult& result);
    static QJsonObject encode(const MeshCommandResult& result);
    static QJsonObject encode(const MeshTransformComponentsResult& result);
    static QJsonObject encode(const MeshBevelEdgeResult& result);
    static QJsonObject encode(const MeshLoopCutResult& result);
    static QJsonObject encode(const MeshDeleteComponentsResult& result);
    static QJsonObject encode(const MeshFillFaceResult& result);
    static QJsonObject encode(const ModifierState& state);
    static QJsonObject encode(const ModifierCommandResult& result);
    /** @brief 返回 JSON-RPC 的 result/error 片段；传输层负责添加 jsonrpc/id。 */
    template <typename T> static QJsonObject response(const ApiResult<T>& result) {
        if (!result.hasValue())
            return {{QStringLiteral("error"), encodeError(*result.error)}};
        return {{QStringLiteral("result"), encode(*result.value)}};
    }
    static QJsonObject invoke(EditorApiService& service, const QString& method,
                              const QJsonValue& params, FileAccess access = FileAccess::External,
                              BeforeCommitGuard guard = {});

  private:
    static QJsonObject encodeEntity(const EntitySnapshot& entity);
};
} // namespace mini3d::editor::api
