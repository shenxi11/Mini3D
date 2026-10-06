/*
 * 模块名: AutomationFrame
 * 功能概述: 实施私有 wire v1 的有界分帧及构树前 JSON 预算校验。
 * 对外接口: AutomationFrameDecoder、encodeFrame、canonicalDigest。
 * 依赖关系: Qt Core、生成的 ApiLimits；不访问场景或认证材料。
 * 输入输出: 分片字节到单个 JSON 帧，JSON 响应到 little-endian 长度帧。
 * 异常与错误: 非法长度或 UTF-8 关闭连接，语法和预算错误交上层回应。
 * 维护说明: 每次只缓存一帧；深度和节点在 QJsonDocument 构树之前检查。
 */
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>

namespace mini3d::editor::automation {
enum class FrameStatus { Json, ParseError, InvalidRequest, LimitExceeded };
struct AutomationFrame {
    FrameStatus status = FrameStatus::Json;
    QJsonDocument document;
};
/** @brief 有界 Header/Body 状态机；false 表示调用方必须关闭该连接。 */
class AutomationFrameDecoder final {
  public:
    using Callback = std::function<bool(AutomationFrame)>;
    /** @brief 顺序交付完整帧；回调 false 停止本批读取，不保留后续输入。 */
    bool consume(QByteArrayView input, const Callback& callback);
    [[nodiscard]] qsizetype bufferedBytes() const;
    void reset();

  private:
    QByteArray header_;
    QByteArray body_;
    quint32 bodyLength_ = 0;
};
/** @brief 超出响应限额时返回空字节；长度按 UTF-8 字节计算。 */
QByteArray encodeFrame(const QJsonObject& response);
/** @brief 对已按 DTO 类型及默认值规范化的参数排序键并计算 SHA256。 */
QByteArray canonicalDigest(const QString& method, const QJsonObject& normalizedParams);
} // namespace mini3d::editor::automation
