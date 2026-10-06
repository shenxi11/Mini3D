/*
 * 模块名: ObservationJsonCodec
 * 功能概述: 严格实施 M3 的 JSON 输入边界，编码实际视图和捕获元数据。
 * 对外接口: decodeRequest、encode、response。
 * 依赖关系: ObservationTypes、ApiJsonCodec、Qt JSON。
 * 输入输出: JSON 到明确 DTO，实际帧与 PNG 到规范 wire 结果。
 * 异常与错误: 未知字段、非规范身份、非法相机或数值在施工前拒绝。
 * 维护说明: 不调度事件或操作 GL；未知输出字段由客户端兼容。
 */
#pragma once

#include "ObservationTypes.h"
#include "editor/api/ApiJsonCodec.h"

namespace mini3d::editor::observation {
/** @brief 观察的独立 JSON 边界，复用共享 uint64 与错误编码。 */
class ObservationJsonCodec final {
  public:
    static api::ApiResult<ObservationRequest> decodeRequest(const QString& method,
                                                            const QJsonValue& params);
    /** @brief 从已解码 DTO 规范编码业务参数及默认值，不包含桥会话/序号。 */
    static api::ApiResult<QJsonObject> canonicalParams(const QString& method,
                                                       const QJsonValue& params);
    static QJsonObject encode(const ViewState& result);
    static QJsonObject encode(const ViewCommandResult& result);
    static QJsonObject encode(const CaptureResult& result);
    template <typename T> static QJsonObject response(const api::ApiResult<T>& result) {
        if (result.error)
            return {{QStringLiteral("error"), api::ApiJsonCodec::encodeError(*result.error)}};
        return {{QStringLiteral("result"), encode(*result.value)}};
    }
};
} // namespace mini3d::editor::observation
