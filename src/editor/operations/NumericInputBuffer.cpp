/*
 * 模块名: NumericInputBuffer
 * 功能概述: 区分空输入、不完整、非法、越界和可提交数值，保持错误原文可修复。
 * 对外接口: NumericInputBuffer
 * 依赖关系: C++ from_chars、数学与字符串库
 * 输入输出: 完整十进制原文到有限 double；没有场景或历史副作用。
 * 异常与错误: 溢出、非有限和超出调用方范围均返回 OutOfRange，不抛解析异常。
 * 维护说明: 显式扫描语法防止 from_chars 的前缀匹配意外接受表达式或单位后缀。
 */
#include "NumericInputBuffer.h"

#include <charconv>
#include <cmath>

namespace mini3d::editor {
void NumericInputBuffer::append(char character) {
    text_.push_back(character);
}

void NumericInputBuffer::backspace() {
    if (!text_.empty()) {
        text_.pop_back();
    }
}

void NumericInputBuffer::clear() {
    text_.clear();
}

const std::string& NumericInputBuffer::text() const {
    return text_;
}

NumericInputResult NumericInputBuffer::parse(double minimum, double maximum) const {
    if (text_.empty()) {
        return {};
    }
    const std::size_t start = text_.front() == '-' || text_.front() == '+' ? 1 : 0;
    bool hasDigit = false;
    bool hasPoint = false;
    for (std::size_t index = start; index < text_.size(); ++index) {
        const char character = text_[index];
        if (character >= '0' && character <= '9') {
            hasDigit = true;
        } else if (character == '.' && !hasPoint) {
            hasPoint = true;
        } else {
            return {NumericInputState::Invalid, std::nullopt};
        }
    }
    if (!hasDigit) {
        return {NumericInputState::Incomplete, std::nullopt};
    }
    double value = 0;
    const char* begin = text_.data() + (text_.front() == '+' ? 1 : 0);
    const char* end = text_.data() + text_.size();
    const auto result = std::from_chars(begin, end, value, std::chars_format::fixed);
    if (result.ec == std::errc::result_out_of_range) {
        return {NumericInputState::OutOfRange, std::nullopt};
    }
    if (result.ec != std::errc{} || result.ptr != end) {
        return {NumericInputState::Invalid, std::nullopt};
    }
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        return {NumericInputState::OutOfRange, std::nullopt};
    }
    return {NumericInputState::Valid, value};
}
} // namespace mini3d::editor
