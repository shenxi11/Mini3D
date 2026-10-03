/*
 * 模块名: NumericInputBufferTests
 * 功能概述: 验证模态数值缓冲的精确十进制语法、退格修复、范围和溢出行为。
 * 对外接口: Catch2 [numeric-buffer] 纯 CPU 用例
 * 依赖关系: NumericInputBuffer、Catch2，无 Qt/OpenGL
 * 输入输出: 字符序列到原文、状态及可选有限值。
 * 异常与错误: 前缀接受、静默丢字符、未完成输入变零或越界可提交即失败。
 * 维护说明: 本组只证明数值基础，不证明 G/R/S 模态或撤销已接通。
 */
#include "editor/operations/NumericInputBuffer.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string_view>

using mini3d::editor::NumericInputBuffer;
using mini3d::editor::NumericInputState;

namespace {
NumericInputBuffer input(std::string_view text) {
    NumericInputBuffer buffer;
    for (char character : text) {
        buffer.append(character);
    }
    return buffer;
}
} // namespace

TEST_CASE("Modal numbers distinguish empty incomplete and signed decimal input",
          "[numeric-buffer]") {
    REQUIRE(input("").parse().state == NumericInputState::Empty);
    REQUIRE_FALSE(input("").parse().value.has_value());
    for (const auto* text : {"-", "+", ".", "-.", "+."}) {
        INFO(text);
        const auto result = input(text).parse();
        REQUIRE(result.state == NumericInputState::Incomplete);
        REQUIRE_FALSE(result.value.has_value());
    }
    for (const auto& [text, value] : {std::pair{"2", 2.0},
                                      {"-2", -2.0},
                                      {"+2", 2.0},
                                      {"0.5", 0.5},
                                      {"-.5", -0.5},
                                      {"+.5", 0.5},
                                      {"45.", 45.0},
                                      {"0002.500", 2.5},
                                      {"-0", -0.0}}) {
        INFO(text);
        const auto result = input(text).parse();
        REQUIRE(result.state == NumericInputState::Valid);
        REQUIRE(result.value.has_value());
        REQUIRE(*result.value == value);
    }
    REQUIRE(std::signbit(*input("-0").parse().value));
}

TEST_CASE("Invalid modal expressions remain visible and can be repaired with backspace",
          "[numeric-buffer]") {
    for (const auto* text : {"1.2.3", "2-1", "1/2", "1e3", "nan", "inf", "2m", " 2", "2 ", "1,5",
                             "--1", "+-1", "=2", "0x10"}) {
        INFO(text);
        const auto buffer = input(text);
        REQUIRE(buffer.text() == text);
        REQUIRE(buffer.parse().state == NumericInputState::Invalid);
        REQUIRE_FALSE(buffer.parse().value.has_value());
    }
    auto buffer = input("12.3.");
    REQUIRE(buffer.parse().state == NumericInputState::Invalid);
    buffer.backspace();
    REQUIRE(buffer.text() == "12.3");
    REQUIRE(buffer.parse().state == NumericInputState::Valid);
    buffer.clear();
    buffer.backspace();
    REQUIRE(buffer.parse().state == NumericInputState::Empty);
    buffer.append('-');
    buffer.append('.');
    REQUIRE(buffer.parse().state == NumericInputState::Incomplete);
    buffer.append('5');
    REQUIRE(*buffer.parse().value == -0.5);
    buffer.backspace();
    REQUIRE(buffer.parse().state == NumericInputState::Incomplete);
}

TEST_CASE("Modal number overflow and operation ranges never produce a usable value",
          "[numeric-buffer]") {
    const auto overflow = input(std::string(400, '9'));
    REQUIRE(overflow.parse().state == NumericInputState::OutOfRange);
    REQUIRE_FALSE(overflow.parse().value.has_value());
    REQUIRE(overflow.text().size() == 400);
    REQUIRE(input("-10").parse(-10, 10).state == NumericInputState::Valid);
    REQUIRE(input("10").parse(-10, 10).state == NumericInputState::Valid);
    REQUIRE(input("10.01").parse(-10, 10).state == NumericInputState::OutOfRange);
    REQUIRE(input("-10.01").parse(-10, 10).state == NumericInputState::OutOfRange);
    REQUIRE_FALSE(input("-10.01").parse(-10, 10).value.has_value());
    auto independent = input("2");
    REQUIRE(*independent.parse().value == 2);
    independent.append('0');
    REQUIRE(*independent.parse().value == 20);
    independent.backspace();
    REQUIRE(*independent.parse().value == 2);
}
