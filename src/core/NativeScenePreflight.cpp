/*
 * 模块名: NativeScenePreflight
 * 功能概述: 无值缓存版本探针和有界格式4/5 SAX预检。
 * 对外接口: 见NativeScenePreflight.h。
 * 依赖关系: Animation、nlohmann/json和标准库。
 * 输入输出: UTF8字符流到预算结果；不访问文件系统。
 * 异常与错误: 语法/重复动画字段/预算失败返回false，完整语义仍由Serializer校验。
 * 维护说明: 64MiB是源字节限额，不宣称DOM或SAX峰值为64MiB。
 */
#include "NativeScenePreflight.h"

#include "Animation.h"

#include <array>
#include <nlohmann/json.hpp>
#include <streambuf>
#include <string_view>

namespace mini3d::core {
namespace {
bool whitespace(int value) {
    return value == ' ' || value == '\r' || value == '\n' || value == '\t';
}
bool delimiter(int value) {
    return value == std::char_traits<char>::eof() || whitespace(value) || value == ',' ||
           value == '}' || value == ']';
}
int hexDigit(int value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}
class VersionProbe {
  public:
    explicit VersionProbe(std::istream& input) : input_(input) {}
    bool scan(NativeSceneProbe& result, std::string& error) {
        std::size_t depth = 0;
        bool expectsKey = false, expectsValue = false, versionKey = false;
        int value = 0;
        while ((value = read()) != std::char_traits<char>::eof()) {
            if (value == '"') {
                bool matches = false;
                const bool key = depth == 1 && expectsKey;
                if (!readString(key, matches)) {
                    error = "场景版本探针遇到未结束或非法转义字符串。";
                    return false;
                }
                if (key) {
                    versionKey = matches;
                    if (matches)
                        ++probe_.versionFields;
                    expectsKey = false;
                } else if (depth == 1) {
                    expectsValue = false;
                }
            } else if (value == '{' || value == '[') {
                if (depth == 1)
                    expectsValue = false;
                ++depth;
                if (depth == 1)
                    expectsKey = value == '{';
            } else if (value == '}' || value == ']') {
                if (depth == 0) {
                    error = "场景JSON容器闭合无效。";
                    return false;
                }
                --depth;
            } else if (depth == 1 && value == ',') {
                expectsKey = true;
                expectsValue = false;
                versionKey = false;
            } else if (depth == 1 && value == ':') {
                expectsValue = true;
            } else if (depth == 1 && expectsValue && !whitespace(value)) {
                bool four = value == '4', five = value == '5';
                while (!delimiter(input_.peek())) {
                    read();
                    four = false;
                    five = false;
                }
                if (versionKey && four)
                    probe_.includesVersion4 = true;
                if (versionKey && five)
                    probe_.includesVersion5 = true;
                expectsValue = false;
            }
        }
        if (input_.bad() || depth != 0 || probe_.sourceBytes == 0) {
            error = "场景版本探针读取失败或JSON容器未结束。";
            return false;
        }
        result = probe_;
        error.clear();
        return true;
    }

  private:
    int read() {
        const int value = input_.get();
        if (value != std::char_traits<char>::eof())
            ++probe_.sourceBytes;
        return value;
    }
    bool readString(bool compareKey, bool& matches) {
        constexpr std::string_view expected = "version";
        std::size_t length = 0;
        bool equal = compareKey;
        int value = 0;
        while ((value = read()) != std::char_traits<char>::eof()) {
            if (value == '"') {
                matches = equal && length == expected.size();
                return true;
            }
            if (value == '\\') {
                value = read();
                switch (value) {
                    case '"':
                    case '\\':
                    case '/':
                        break;
                    case 'b':
                        value = '\b';
                        break;
                    case 'f':
                        value = '\f';
                        break;
                    case 'n':
                        value = '\n';
                        break;
                    case 'r':
                        value = '\r';
                        break;
                    case 't':
                        value = '\t';
                        break;
                    case 'u': {
                        int decoded = 0;
                        for (int index = 0; index < 4; ++index) {
                            const int digit = hexDigit(read());
                            if (digit < 0)
                                return false;
                            decoded = decoded * 16 + digit;
                        }
                        value = decoded;
                        break;
                    }
                    default:
                        return false;
                }
            } else if (value < 0x20) {
                return false;
            }
            if (equal && (length >= expected.size() || value != expected[length]))
                equal = false;
            // 一旦无法匹配就不再计任意长字符串，不存值/键文本。
            if (length <= expected.size())
                ++length;
        }
        return false;
    }
    std::istream& input_;
    NativeSceneProbe probe_;
};

using Json = nlohmann::json;
class TextInputBuffer final : public std::streambuf {
  public:
    explicit TextInputBuffer(std::string_view text) {
        // get区域只读取；不开放输出区域，探针/SAX均不会写入调用者输入。
        auto* start = const_cast<char*>(text.data());
        setg(start, start, start + text.size());
    }
};
enum class Kind {
    Other, Root, Animation, Tracks, Track, Keys, Key, Properties, Actions, Action,
    Slots, Slot, Curves, Curve, ScalarKeys, ScalarKey, Bindings, Binding, Owner, RationalFps
};
enum class Field {
    Other,
    Animation,
    Fps,
    Start,
    End,
    Tracks,
    Entity,
    Channel,
    Keys,
    Frame,
    Value,
    Interpolation,
    Properties, Actions, Slots, Curves, Id, OwnerKind, Property, Component, Time, Outgoing,
    RationalFps, LoopPolicy, Bindings, DocumentUuid, Owner, Action, Slot, Kind, Material,
    Numerator, Denominator
};
struct Level {
    Kind kind = Kind::Other;
    Field pending = Field::Other;
    unsigned fields = 0;
    std::size_t elements = 0;
};
class StructureProbe final : public nlohmann::json_sax<Json> {
  public:
    explicit StructureProbe(std::string& error, bool version5) : version5_(version5), error_(error) {}
    bool null() override {
        return scalar();
    }
    bool boolean(bool) override {
        return scalar();
    }
    bool number_integer(number_integer_t) override {
        return scalar();
    }
    bool number_unsigned(number_unsigned_t) override {
        return scalar();
    }
    bool number_float(number_float_t, const string_t&) override {
        return scalar();
    }
    bool string(string_t&) override {
        return scalar();
    }
    bool binary(binary_t&) override {
        return scalar();
    }
    bool start_object(std::size_t) override {
        return start(true);
    }
    bool start_array(std::size_t) override {
        return start(false);
    }
    bool end_object() override {
        const auto& level = levels_[depth_ - 1];
        const unsigned required = level.kind == Kind::Animation ? (version5_ ? 31U : 15U)
            : level.kind == Kind::Track || level.kind == Kind::Key || level.kind == Kind::Slot ? 7U
            : level.kind == Kind::Properties || level.kind == Kind::Curve ||
              level.kind == Kind::ScalarKey || level.kind == Kind::Binding ? 15U
            : level.kind == Kind::Action || level.kind == Kind::RationalFps ? 3U : 0U;
        if (required && level.fields != required)
            return fail("动画对象缺少必需字段。");
        --depth_;
        return true;
    }
    bool end_array() override {
        --depth_;
        return true;
    }
    bool key(string_t& name) override {
        auto& level = levels_[depth_ - 1];
        Field field = Field::Other;
        unsigned bit = 0;
        if (level.kind == Kind::Root && name == "animation") {
            field = Field::Animation;
            bit = 1;
        } else if (level.kind == Kind::Animation) {
            if (name == "fps") {
                field = Field::Fps;
                bit = 1;
            } else if (name == "startFrame") {
                field = Field::Start;
                bit = 2;
            } else if (name == "endFrame") {
                field = Field::End;
                bit = 4;
            } else if (name == "tracks") {
                field = Field::Tracks;
                bit = 8;
            } else if (version5_ && name == "properties") {
                field = Field::Properties;
                bit = 16;
            }
        } else if (level.kind == Kind::Track) {
            if (name == "entityId") {
                field = Field::Entity;
                bit = 1;
            } else if (name == "channel") {
                field = Field::Channel;
                bit = 2;
            } else if (name == "keys") {
                field = Field::Keys;
                bit = 4;
            }
        } else if (level.kind == Kind::Key) {
            if (name == "frame") {
                field = Field::Frame;
                bit = 1;
            } else if (name == "value") {
                field = Field::Value;
                bit = 2;
            } else if (name == "interpolation") {
                field = Field::Interpolation;
                bit = 4;
            }
        } else if (level.kind == Kind::Properties) {
            if (name == "rationalFps") { field = Field::RationalFps; bit = 1; }
            else if (name == "loopPolicy") { field = Field::LoopPolicy; bit = 2; }
            else if (name == "actions") { field = Field::Actions; bit = 4; }
            else if (name == "bindings") { field = Field::Bindings; bit = 8; }
        } else if (level.kind == Kind::Action) {
            if (name == "id") { field = Field::Id; bit = 1; }
            else if (name == "actionSlots") { field = Field::Slots; bit = 2; }
        } else if (level.kind == Kind::Slot) {
            if (name == "id") { field = Field::Id; bit = 1; }
            else if (name == "ownerKind") { field = Field::OwnerKind; bit = 2; }
            else if (name == "curves") { field = Field::Curves; bit = 4; }
        } else if (level.kind == Kind::Curve) {
            if (name == "id") { field = Field::Id; bit = 1; }
            else if (name == "property") { field = Field::Property; bit = 2; }
            else if (name == "component") { field = Field::Component; bit = 4; }
            else if (name == "keys") { field = Field::Keys; bit = 8; }
        } else if (level.kind == Kind::ScalarKey) {
            if (name == "id") { field = Field::Id; bit = 1; }
            else if (name == "time") { field = Field::Time; bit = 2; }
            else if (name == "value") { field = Field::Value; bit = 4; }
            else if (name == "outgoing") { field = Field::Outgoing; bit = 8; }
        } else if (level.kind == Kind::Binding) {
            if (name == "documentUuid") { field = Field::DocumentUuid; bit = 1; }
            else if (name == "owner") { field = Field::Owner; bit = 2; }
            else if (name == "action") { field = Field::Action; bit = 4; }
            else if (name == "slot") { field = Field::Slot; bit = 8; }
        } else if (level.kind == Kind::Owner) {
            if (name == "kind") { field = Field::Kind; bit = 1; }
            else if (name == "entityId") { field = Field::Entity; bit = 2; }
            else if (name == "materialId") { field = Field::Material; bit = 4; }
        } else if (level.kind == Kind::RationalFps) {
            if (name == "numerator") { field = Field::Numerator; bit = 1; }
            else if (name == "denominator") { field = Field::Denominator; bit = 2; }
        }
        if ((level.kind == Kind::Animation || level.kind == Kind::Track ||
             level.kind == Kind::Key || level.kind == Kind::Properties ||
             level.kind == Kind::Action || level.kind == Kind::Slot || level.kind == Kind::Curve ||
             level.kind == Kind::ScalarKey || level.kind == Kind::Binding ||
             level.kind == Kind::Owner || level.kind == Kind::RationalFps) &&
            !bit)
            return fail("动画对象包含未知字段。");
        if (bit && (level.fields & bit))
            return fail("动画对象包含重复字段。");
        level.fields |= bit;
        level.pending = field;
        return true;
    }
    bool parse_error(std::size_t, const std::string&,
                     const nlohmann::detail::exception& failure) override {
        return fail(std::string("场景JSON预检失败：") + failure.what());
    }

  private:
    bool fail(const std::string& message) {
        error_ = message;
        return false;
    }
    bool scalar() {
        return countValue();
    }
    bool countValue() {
        if (!depth_)
            return true;
        auto& parent = levels_[depth_ - 1];
        if (parent.kind == Kind::Tracks) {
            if (++parent.elements > kAnimationMaximumTracks)
                return fail("动画轨道数量超过3000上限。");
            totalCurves_ += 3;
            if (totalCurves_ > kAnimationMaximumScalarCurves)
                return fail("legacy XYZ 与 typed 曲线总数超过9000上限。");
        }
        if (parent.kind == Kind::Curves && ++totalCurves_ > kAnimationMaximumScalarCurves)
            return fail("legacy XYZ 与 typed 曲线总数超过9000上限。");
        if (parent.kind == Kind::Keys) {
            if (++parent.elements > kAnimationMaximumTrackKeys)
                return fail("单轨关键帧数量超过10000上限。");
            if (++totalKeys_ > kAnimationMaximumKeys)
                return fail("动画关键帧总数超过100000上限。");
            totalScalarKeys_ += 3;
            if (totalScalarKeys_ > kAnimationMaximumScalarKeys)
                return fail("legacy XYZ 与 typed 键总数超过300000上限。");
        }
        if (parent.kind == Kind::ScalarKeys && ++totalScalarKeys_ > kAnimationMaximumScalarKeys)
            return fail("legacy XYZ 与 typed 键总数超过300000上限。");
        return true;
    }
    bool start(bool object) {
        if (depth_ == kNativeSceneMaximumDepth)
            return fail("格式4/5 JSON容器深度超过64上限。");
        if (!countValue())
            return false;
        Kind kind = Kind::Other;
        if (!depth_ && object)
            kind = Kind::Root;
        else if (depth_) {
            const auto& parent = levels_[depth_ - 1];
            if (object && parent.kind == Kind::Root && parent.pending == Field::Animation)
                kind = Kind::Animation;
            else if (!object && parent.kind == Kind::Animation && parent.pending == Field::Tracks)
                kind = Kind::Tracks;
            else if (object && parent.kind == Kind::Tracks)
                kind = Kind::Track;
            else if (!object && parent.kind == Kind::Track && parent.pending == Field::Keys)
                kind = Kind::Keys;
            else if (object && parent.kind == Kind::Keys)
                kind = Kind::Key;
            else if (object && parent.kind == Kind::Animation && parent.pending == Field::Properties)
                kind = Kind::Properties;
            else if (!object && parent.kind == Kind::Properties && parent.pending == Field::Actions)
                kind = Kind::Actions;
            else if (object && parent.kind == Kind::Actions)
                kind = Kind::Action;
            else if (!object && parent.kind == Kind::Action && parent.pending == Field::Slots)
                kind = Kind::Slots;
            else if (object && parent.kind == Kind::Slots)
                kind = Kind::Slot;
            else if (!object && parent.kind == Kind::Slot && parent.pending == Field::Curves)
                kind = Kind::Curves;
            else if (object && parent.kind == Kind::Curves)
                kind = Kind::Curve;
            else if (!object && parent.kind == Kind::Curve && parent.pending == Field::Keys)
                kind = Kind::ScalarKeys;
            else if (object && parent.kind == Kind::ScalarKeys)
                kind = Kind::ScalarKey;
            else if (!object && parent.kind == Kind::Properties && parent.pending == Field::Bindings)
                kind = Kind::Bindings;
            else if (object && parent.kind == Kind::Bindings)
                kind = Kind::Binding;
            else if (object && parent.kind == Kind::Binding && parent.pending == Field::Owner)
                kind = Kind::Owner;
            else if (object && parent.kind == Kind::Properties && parent.pending == Field::RationalFps)
                kind = Kind::RationalFps;
        }
        levels_[depth_++] = {kind};
        return true;
    }
    std::array<Level, kNativeSceneMaximumDepth> levels_{};
    std::size_t depth_ = 0;
    std::size_t totalKeys_ = 0;
    std::size_t totalCurves_ = 0;
    std::size_t totalScalarKeys_ = 0;
    bool version5_;
    std::string& error_;
};
} // namespace
bool probeNativeSceneVersion(std::istream& input, NativeSceneProbe& result, std::string& error) {
    return VersionProbe(input).scan(result, error);
}
bool validateNativeSceneStructure(std::istream& input, const NativeSceneProbe& probe,
                                  std::string& error) {
    error.clear();
    if (!probe.includesVersion4 && !probe.includesVersion5)
        return true;
    if (probe.sourceBytes > kNativeSceneMaximumBytes) {
        error = "格式4/5场景源文件超过64MiB上限。";
        return false;
    }
    if (probe.versionFields != 1) {
        error = "格式4/5顶层version字段不得重复。";
        return false;
    }
    StructureProbe structure(error, probe.includesVersion5);
    return Json::sax_parse(input, &structure);
}
bool validateNativeSceneText(std::string_view text, NativeSceneProbe& result, std::string& error) {
    if (text.empty()) {
        error = "场景JSON不能为空。";
        return false;
    }
    NativeSceneProbe probe;
    {
        TextInputBuffer buffer(text);
        std::istream input(&buffer);
        if (!probeNativeSceneVersion(input, probe, error))
            return false;
    }
    TextInputBuffer buffer(text);
    std::istream input(&buffer);
    if (!validateNativeSceneStructure(input, probe, error))
        return false;
    result = probe;
    return true;
}
} // namespace mini3d::core
