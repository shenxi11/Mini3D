/*
 * 模块名: NativeScenePreflightTests
 * 功能概述: 验证固定内存版本路由和DOM前的格式4结构预算。
 * 对外接口: Catch2 [animation][native-scene]。
 * 依赖关系: NativeScenePreflight、Catch2、标准库；无Qt/GL。
 * 输入输出: 真实原生JSON文本到路由、计数和原子拒绝断言。
 * 异常与错误: 不用预检成功证明完整场景语义正确。
 * 维护说明: 大文本由测试持有，预检不得另复制整份输入。
 */
#include "core/NativeScenePreflight.h"

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <utility>

using namespace mini3d::core;
namespace {
bool check(const std::string& text, std::string& error) {
    NativeSceneProbe probe;
    return validateNativeSceneText(text, probe, error);
}
std::string nested(std::size_t arrays, int version) {
    return "{\"futureOptional\":" + std::string(arrays, '[') + "0" + std::string(arrays, ']') +
           ",\"version\":" + std::to_string(version) + "}";
}
std::string tracks(std::size_t count, std::size_t keys) {
    std::string text =
        R"({"version":4,"animation":{"fps":24,"startFrame":1,"endFrame":250,"tracks":[)";
    for (std::size_t track = 0; track < count; ++track) {
        if (track)
            text += ',';
        text += R"({"entityId":1,"channel":"position","keys":[)";
        for (std::size_t key = 0; key < keys; ++key) {
            if (key)
                text += ',';
            text += R"({"frame":1,"value":[0,0,0],"interpolation":"linear"})";
        }
        text += "]}";
    }
    return text + "]}}";
}
} // namespace
TEST_CASE("Native version routing recognizes actual and escaped top level keys",
          "[animation][native-scene]") {
    for (const auto& [text, version4] :
         {std::pair<std::string, bool>{R"({"version":4,"note":"fake \"version\":3"})", true},
          {R"({"note":"fake \"version\":4","child":{"version":4},"version":3})", false},
          {R"({"child":{"version":3},"version":4})", true},
          {R"({"\u0076\u0065rsion":4})", true},
          {R"({"version":3,"version":3})", false}}) {
        std::istringstream input(text);
        NativeSceneProbe probe;
        std::string error;
        REQUIRE(probeNativeSceneVersion(input, probe, error));
        REQUIRE(probe.sourceBytes == text.size());
        REQUIRE(probe.includesVersion4 == version4);
    }
}
TEST_CASE("Any version four occurrence rejects repeated top level versions before DOM",
          "[animation][native-scene]") {
    std::string error;
    for (const auto* text : {R"({"version":4,"version":3})", R"({"version":3,"version":4})",
                             R"({"version":4,"version":4})"}) {
        REQUIRE_FALSE(check(text, error));
        REQUIRE(error.find("version") != std::string::npos);
    }
    REQUIRE(check(R"({"version":3,"version":3})", error));
}
TEST_CASE("Native depth counts root and unknown containers without restricting legacy",
          "[animation][native-scene]") {
    std::string error;
    REQUIRE(check(nested(63, 4), error));
    REQUIRE_FALSE(check(nested(64, 4), error));
    REQUIRE(error.find("64") != std::string::npos);
    REQUIRE(check(nested(4096, 3), error));
}
TEST_CASE("Native source byte check handles a tail version without token allocation",
          "[animation][native-scene]") {
    std::string text = "{\"futureOptional\":\"";
    text.append(kNativeSceneMaximumBytes, 'x');
    text += "\",\"version\":4}";
    std::string error;
    REQUIRE_FALSE(check(text, error));
    REQUIRE(error.find("64MiB") != std::string::npos);
    text[text.size() - 2] = '3';
    REQUIRE(check(text, error));
}
TEST_CASE("Native structural counters enforce track single track and total key budgets",
          "[animation][native-scene]") {
    std::string error;
    REQUIRE(check(tracks(3000, 0), error));
    REQUIRE_FALSE(check(tracks(3001, 0), error));
    REQUIRE(error.find("3000") != std::string::npos);
    REQUIRE(check(tracks(1, 10000), error));
    REQUIRE_FALSE(check(tracks(1, 10001), error));
    REQUIRE(error.find("10000") != std::string::npos);
    REQUIRE(check(tracks(10, 10000), error));
    REQUIRE_FALSE(check(tracks(11, 10000), error));
    REQUIRE(error.find("100000") != std::string::npos);
}
TEST_CASE("Native strict animation objects reject duplicates unknown fields and malformed JSON",
          "[animation][native-scene]") {
    std::string error;
    for (
        const auto* text :
        {R"({"version":4,"animation":{"fps":24,"fps":24,"startFrame":1,"endFrame":250,"tracks":[]}})",
         R"({"version":4,"animation":{"fps":24,"startFrame":1,"endFrame":250,"tracks":[],"extra":0}})",
         R"({"version":4,"animation":{"fps":24,"startFrame":1,"endFrame":250,"tracks":[]},"animation":{}})",
         R"({"version":4,"animation":{"fps":24,"startFrame":1,"endFrame":250,"tracks":[{"entityId":1,"channel":"position","keys":[],"keys":[]}]}})",
         R"({"version":4,"animation":{"fps":24,"startFrame":1,"endFrame":250,"tracks":[{"entityId":1,"channel":"position","keys":[{"frame":1,"value":[0,0,0],"interpolation":"linear","frame":2}]}]}})",
         R"({"version":4,})", R"({"version":4,"note":"unterminated})"}) {
        REQUIRE_FALSE(check(text, error));
        REQUIRE_FALSE(error.empty());
    }
    NativeSceneProbe sentinel{123, 7, true};
    const auto before = sentinel;
    REQUIRE_FALSE(validateNativeSceneText("", sentinel, error));
    REQUIRE(sentinel == before);
}
