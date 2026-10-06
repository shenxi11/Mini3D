/*
 * 模块名: AutomationFrameTests
 * 功能概述: 验证真实帧状态机的分片、编码和构树前预算。
 * 对外接口: Catch2 [automation-frame]；不要求 GUI 或 QCoreApplication。
 * 依赖关系: AutomationFrame、生成的 ApiLimits、Qt Core。
 * 输入输出: 有界字节夹具到逐帧状态和稳定摘要断言。
 * 异常与错误: 传输失败与 JSON/profile 失败须被明确区分。
 * 维护说明: 夹具仅在内存构造，不写临时目录或共享服务。
 */
#include "editor/automation/AutomationFrame.h"

#include "api/ApiLimits.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QtEndian>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace mini3d::editor::automation;
namespace {
QByteArray frameBytes(const QByteArray& payload) {
    QByteArray frame(4, '\0');
    qToLittleEndian<quint32>(quint32(payload.size()), frame.data());
    frame.append(payload);
    return frame;
}
AutomationFrame decode(const QByteArray& payload) {
    AutomationFrameDecoder decoder;
    AutomationFrame frame;
    REQUIRE(decoder.consume(frameBytes(payload), [&](AutomationFrame decoded) {
        frame = std::move(decoded);
        return true;
    }));
    REQUIRE(decoder.bufferedBytes() == 0);
    return frame;
}
// 保留优化前的编码作为独立参考，防止摘要字节随序列化方式变化。
QByteArray referenceCanonicalJson(const QJsonValue& value) {
    QByteArray result;
    if (value.isObject()) {
        result.append('{');
        const auto object = value.toObject();
        auto keys = object.keys();
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys) {
            if (result.size() > 1)
                result.append(',');
            result.append(referenceCanonicalJson(key));
            result.append(':');
            result.append(referenceCanonicalJson(object[key]));
        }
        result.append('}');
    } else if (value.isArray()) {
        result.append('[');
        for (const auto& item : value.toArray()) {
            if (result.size() > 1)
                result.append(',');
            result.append(referenceCanonicalJson(item));
        }
        result.append(']');
    } else {
        auto scalar = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
        result = scalar.mid(1, scalar.size() - 2);
    }
    return result;
}
void requireCanonicalEncoding(const QString& method, const QJsonObject& params) {
    const QJsonObject envelope{{"method", method}, {"params", params}};
    const auto reference = referenceCanonicalJson(envelope);
    REQUIRE(QJsonDocument(envelope).toJson(QJsonDocument::Compact) == reference);
    REQUIRE(canonicalDigest(method, params) ==
            QCryptographicHash::hash(reference, QCryptographicHash::Sha256));
}
} // namespace

TEST_CASE("wire header and Chinese UTF-8 survive arbitrary byte splits", "[automation-frame]") {
    AutomationFrameDecoder decoder;
    const QJsonObject request{{"jsonrpc", "2.0"}, {"id", "中文请求"},
                               {"method", "system.describe"}, {"params", QJsonObject{}}};
    const auto bytes = encodeFrame(request);
    int count = 0;
    for (qsizetype index = 0; index < bytes.size(); ++index) {
        REQUIRE(decoder.consume(QByteArrayView(bytes.constData() + index, 1),
                                [&](AutomationFrame frame) {
            REQUIRE(frame.status == FrameStatus::Json);
            REQUIRE(frame.document.object() == request);
            ++count;
            return true;
        }));
        REQUIRE(decoder.bufferedBytes() <= bytes.size());
    }
    REQUIRE(count == 1);
    REQUIRE(decoder.bufferedBytes() == 0);
}
TEST_CASE("coalesced frames are delivered individually without a payload list", "[automation-frame]") {
    AutomationFrameDecoder decoder;
    int count = 0;
    const auto bytes = frameBytes("{}") + frameBytes("{\"second\":true}") + frameBytes("{}");
    REQUIRE(decoder.consume(bytes, [&](AutomationFrame frame) {
        REQUIRE(frame.status == FrameStatus::Json);
        ++count;
        return true;
    }));
    REQUIRE(count == 3);
}
TEST_CASE("zero and oversized lengths fail at the header", "[automation-frame]") {
    for (const auto length : {quint32(0), quint32(mini3d::editor::api::limits::requestBytes + 1)}) {
        QByteArray header(4, '\0');
        qToLittleEndian<quint32>(length, header.data());
        AutomationFrameDecoder decoder;
        bool called = false;
        REQUIRE_FALSE(decoder.consume(header, [&](AutomationFrame) {
            called = true;
            return true;
        }));
        REQUIRE_FALSE(called);
        REQUIRE(decoder.bufferedBytes() == 0);
    }
}
TEST_CASE("noncanonical UTF-8 closes transport instead of replacing bytes", "[automation-frame]") {
    for (const auto& bytes : {QByteArray::fromHex("c080"), QByteArray::fromHex("eda080"),
                             QByteArray::fromHex("f4908080"), QByteArray::fromHex("e282")}) {
        AutomationFrameDecoder decoder;
        bool called = false;
        REQUIRE_FALSE(decoder.consume(frameBytes(QByteArray("{\"name\":\"") + bytes + "\"}"),
                                      [&](AutomationFrame) {
            called = true;
            return true;
        }));
        REQUIRE_FALSE(called);
    }
}
TEST_CASE("valid UTF-8 syntax failures and scalar profile failures remain distinct", "[automation-frame]") {
    for (const auto& json : {"{", "{\"x\":tru}", "{\"x\":null", "[01]", "[1,]", "\"\\u12\""})
        REQUIRE(decode(json).status == FrameStatus::ParseError);
    for (const auto& json : {"null", "true", "0", "\"scalar\""})
        REQUIRE(decode(json).status == FrameStatus::InvalidRequest);
    REQUIRE(decode("[]").document.isArray());
}
TEST_CASE("depth and node limits reject before allocating a Qt JSON tree", "[automation-frame]") {
    using namespace mini3d::editor::api;
    const auto nesting = int(limits::jsonDepth);
    REQUIRE(decode(QByteArray(nesting, '[') + '0' + QByteArray(nesting, ']')).status ==
            FrameStatus::Json);
    auto deep = decode(QByteArray(nesting + 1, '[') + '0' + QByteArray(nesting + 1, ']'));
    REQUIRE(deep.status == FrameStatus::LimitExceeded);
    REQUIRE(deep.document.isNull());
    QByteArray many("[");
    for (std::size_t index = 0; index < limits::jsonNodes; ++index) {
        if (index)
            many.append(',');
        many.append('0');
    }
    many.append(']');
    const auto nodes = decode(many);
    REQUIRE(nodes.status == FrameStatus::LimitExceeded);
    REQUIRE(nodes.document.isNull());
}
TEST_CASE("canonical digest ignores key order but preserves array order", "[automation-frame]") {
    QJsonObject first{{"b", QJsonObject{{"y", 2}, {"x", 1}}}, {"a", QJsonArray{1, 2}}};
    QJsonObject second{{"a", QJsonArray{1, 2}}, {"b", QJsonObject{{"x", 1}, {"y", 2}}}};
    REQUIRE(canonicalDigest("entity.update", first) == canonicalDigest("entity.update", second));
    second["a"] = QJsonArray{2, 1};
    REQUIRE(canonicalDigest("entity.update", first) != canonicalDigest("entity.update", second));
    REQUIRE(canonicalDigest("entity.update", first) != canonicalDigest("entity.create", first));
}
TEST_CASE("compact digest encoding matches recursive reference for nested Unicode objects",
          "[automation-frame]") {
    const auto frame = decode(R"({"z":{"b":[null,true,false,{"y":2,"x":1}],"a":[]},
        "a":"quote\"\\/\b\f\n\r\t\u0000\u001f",
        "\u4e2d":"\u4e2d\u6587\u00e9e\u0301\ud83d\ude00",
        "\ud800":"\ud800","\udc00":"\udc00","\ue000":{},
        "\ud800\udc00":[[],{}]})");
    REQUIRE(frame.status == FrameStatus::Json);
    REQUIRE(frame.document.isObject());
    const auto params = frame.document.object();
    requireCanonicalEncoding(QStringLiteral("entity.中文\n\""), params);
    QJsonObject reordered;
    const auto keys = params.keys();
    for (auto key = keys.crbegin(); key != keys.crend(); ++key)
        reordered.insert(*key, params[*key]);
    requireCanonicalEncoding(QStringLiteral("entity.中文\n\""), reordered);
    REQUIRE(canonicalDigest("entity.update", params) ==
            canonicalDigest("entity.update", reordered));
}
TEST_CASE("compact digest encoding preserves Qt numeric boundaries and negative zero",
          "[automation-frame]") {
    const QJsonArray numbers{0.0,
                             -0.0,
                             1.0 / 3.0,
                             std::nextafter(1.0, 2.0),
                             1e-7,
                             1e21,
                             std::numeric_limits<double>::denorm_min(),
                             std::numeric_limits<double>::min(),
                             std::numeric_limits<double>::max(),
                             std::numeric_limits<double>::lowest(),
                             QJsonValue(std::numeric_limits<qint64>::min()),
                             QJsonValue(std::numeric_limits<qint64>::max()),
                             QJsonValue(qint64(9007199254740991)),
                             QJsonValue(qint64(9007199254740993))};
    requireCanonicalEncoding("mesh.create", {{"numbers", numbers}});
    const auto frame = decode(R"({"numbers":[-0,-0.0,0,1.2345678901234567,9007199254740993,
        9223372036854775807,-9223372036854775808,1e-308,1e308]})");
    REQUIRE(frame.status == FrameStatus::Json);
    REQUIRE(frame.document.isObject());
    const auto parsed = frame.document.object();
    requireCanonicalEncoding("mesh.create", parsed);
    requireCanonicalEncoding("mesh.create", {{"value", -0.0}});
    REQUIRE(canonicalDigest("mesh.create", {{"value", -0.0}}) ==
            canonicalDigest("mesh.create", {{"value", 0.0}}));
}
TEST_CASE("compact digest encoding retains raw invalid domain fallback parameters",
          "[automation-frame]") {
    // 领域解码失败时，桥接移除会话信封后将原始参数交给摘要。
    const auto frame = decode(R"({"positions":"invalid","faces":[{"indices":[0,"bad",2]}],
        "unknown":{"z":null,"a":[false,"\u4e2d\n"]},"timeoutMs":30000})");
    REQUIRE(frame.status == FrameStatus::Json);
    REQUIRE(frame.document.isObject());
    auto params = frame.document.object();
    requireCanonicalEncoding("mesh.create", params);
    const auto digest = canonicalDigest("mesh.create", params);
    params["positions"] = "different invalid value";
    requireCanonicalEncoding("mesh.create", params);
    REQUIRE(canonicalDigest("mesh.create", params) != digest);
}
TEST_CASE("oversized response payload is not framed", "[automation-frame]") {
    const auto size = int(mini3d::editor::api::limits::responseBytes);
    REQUIRE(encodeFrame({{"result", QString(size, 'x')}}).isEmpty());
}
