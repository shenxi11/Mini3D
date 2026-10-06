/*
 * 模块名: AutomationFrame
 * 功能概述: 在分帧后以有界语法扫描限制 JSON 分配，再交 Qt JSON 解码。
 * 对外接口: AutomationFrame.h。
 * 依赖关系: Qt JSON、SHA256、生成的 ApiLimits。
 * 输入输出: UTF-8 字节帧到 JSON 对象/错误，响应到 little-endian 字节帧。
 * 异常与错误: 长度和编码错误不可恢复；语法错误不会进入领域调用。
 * 维护说明: 递归仅用于已限制到 32 层的语法扫描，不构建中间节点树。
 */
#include "AutomationFrame.h"

#include "api/ApiLimits.h"

#include <QCryptographicHash>
#include <QtEndian>
#include <algorithm>

namespace mini3d::editor::automation {
namespace {
bool isStrictUtf8(QByteArrayView bytes) {
    qsizetype index = 0;
    while (index < bytes.size()) {
        const auto lead = static_cast<unsigned char>(bytes[index++]);
        if (lead < 0x80)
            continue;
        int continuation = 0;
        unsigned int codePoint = 0, minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            continuation = 1;
            codePoint = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            continuation = 2;
            codePoint = lead & 0x0f;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            continuation = 3;
            codePoint = lead & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (bytes.size() - index < continuation)
            return false;
        for (int count = 0; count < continuation; ++count) {
            const auto next = static_cast<unsigned char>(bytes[index++]);
            if ((next & 0xc0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if (codePoint < minimum || codePoint > 0x10ffff ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff))
            return false;
    }
    return true;
}
class JsonBudgetScanner final {
  public:
    explicit JsonBudgetScanner(QByteArrayView bytes) : bytes_(bytes) {}
    FrameStatus scan() {
        whitespace();
        const bool structured = peek() == '{' || peek() == '[';
        if (!value(0))
            return exceeded_ ? FrameStatus::LimitExceeded : FrameStatus::ParseError;
        whitespace();
        if (index_ != bytes_.size())
            return FrameStatus::ParseError;
        return structured ? FrameStatus::Json : FrameStatus::InvalidRequest;
    }

  private:
    char peek() const {
        return index_ < bytes_.size() ? bytes_[index_] : '\0';
    }
    bool take(char expected) {
        if (peek() != expected)
            return false;
        ++index_;
        return true;
    }
    void whitespace() {
        while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n')
            ++index_;
    }
    bool node() {
        if (++nodes_ > api::limits::jsonNodes) {
            exceeded_ = true;
            return false;
        }
        return true;
    }
    bool string() {
        if (!take('"'))
            return false;
        while (index_ < bytes_.size()) {
            const auto current = static_cast<unsigned char>(bytes_[index_++]);
            if (current == '"')
                return true;
            if (current < 0x20)
                return false;
            if (current != '\\')
                continue;
            if (index_ == bytes_.size())
                return false;
            const char escaped = bytes_[index_++];
            if (escaped == 'u') {
                for (int digit = 0; digit < 4; ++digit) {
                    const char hex = peek();
                    if (!((hex >= '0' && hex <= '9') || (hex >= 'a' && hex <= 'f') ||
                          (hex >= 'A' && hex <= 'F')))
                        return false;
                    ++index_;
                }
            } else if (escaped != '"' && escaped != '\\' && escaped != '/' && escaped != 'b' &&
                       escaped != 'f' && escaped != 'n' && escaped != 'r' && escaped != 't') {
                return false;
            }
        }
        return false;
    }
    bool digits() {
        const auto start = index_;
        while (peek() >= '0' && peek() <= '9')
            ++index_;
        return index_ != start;
    }
    bool number() {
        take('-');
        if (!take('0')) {
            if (peek() < '1' || peek() > '9' || !digits())
                return false;
        }
        if (take('.') && !digits())
            return false;
        if (peek() == 'e' || peek() == 'E') {
            ++index_;
            if (peek() == '-' || peek() == '+')
                ++index_;
            if (!digits())
                return false;
        }
        return true;
    }
    bool literal(QByteArrayView expected) {
        if (bytes_.size() - index_ < expected.size() ||
            bytes_.sliced(index_, expected.size()) != expected)
            return false;
        index_ += expected.size();
        return true;
    }
    bool value(std::size_t depth) {
        whitespace();
        if (!node())
            return false;
        if (peek() == '{' || peek() == '[') {
            if (depth >= api::limits::jsonDepth) {
                exceeded_ = true;
                return false;
            }
            const bool object = take('{');
            if (!object)
                take('[');
            const char closing = object ? '}' : ']';
            whitespace();
            if (take(closing))
                return true;
            do {
                whitespace();
                if (object) {
                    if (!node() || !string())
                        return false;
                    whitespace();
                    if (!take(':'))
                        return false;
                }
                if (!value(depth + 1))
                    return false;
                whitespace();
                if (take(closing))
                    return true;
            } while (take(','));
            return false;
        }
        if (peek() == '"')
            return string();
        if (peek() == 't')
            return literal("true");
        if (peek() == 'f')
            return literal("false");
        if (peek() == 'n')
            return literal("null");
        return number();
    }
    QByteArrayView bytes_;
    qsizetype index_ = 0;
    std::size_t nodes_ = 0;
    bool exceeded_ = false;
};
} // namespace

bool AutomationFrameDecoder::consume(QByteArrayView input, const Callback& callback) {
    while (!input.isEmpty()) {
        if (bodyLength_ == 0) {
            const auto count = std::min(input.size(), qsizetype(4) - header_.size());
            header_.append(input.data(), count);
            input = input.sliced(count);
            if (header_.size() != 4)
                continue;
            bodyLength_ = qFromLittleEndian<quint32>(header_.constData());
            header_.clear();
            if (bodyLength_ == 0 || bodyLength_ > api::limits::requestBytes) {
                reset();
                return false;
            }
            body_.reserve(bodyLength_);
        }
        const auto count = std::min(input.size(), qsizetype(bodyLength_) - body_.size());
        body_.append(input.data(), count);
        input = input.sliced(count);
        if (body_.size() != bodyLength_)
            continue;
        if (!isStrictUtf8(body_)) {
            reset();
            return false;
        }
        AutomationFrame frame;
        frame.status = JsonBudgetScanner(body_).scan();
        if (frame.status == FrameStatus::Json) {
            QJsonParseError error;
            frame.document = QJsonDocument::fromJson(body_, &error);
            if (error.error != QJsonParseError::NoError)
                frame.status = FrameStatus::ParseError;
        }
        body_.clear();
        bodyLength_ = 0;
        if (!callback(std::move(frame)))
            return false;
    }
    return true;
}
qsizetype AutomationFrameDecoder::bufferedBytes() const {
    return header_.size() + body_.size();
}
void AutomationFrameDecoder::reset() {
    header_.clear();
    body_.clear();
    bodyLength_ = 0;
}
QByteArray encodeFrame(const QJsonObject& response) {
    auto payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    if (std::size_t(payload.size()) > api::limits::responseBytes)
        return {};
    QByteArray result(4, '\0');
    qToLittleEndian<quint32>(quint32(payload.size()), result.data());
    result.append(payload);
    return result;
}
QByteArray canonicalDigest(const QString& method, const QJsonObject& normalizedParams) {
    return QCryptographicHash::hash(
        QJsonDocument(QJsonObject{{"method", method}, {"params", normalizedParams}})
            .toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256);
}
} // namespace mini3d::editor::automation
