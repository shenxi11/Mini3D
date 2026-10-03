/*
 * 模块名: NumericInputBuffer
 * 功能概述: 保存模态数值输入原文，按十进制符号/小数语法解析，禁止表达式求值。
 * 对外接口: NumericInputBuffer、NumericInputState、NumericInputResult
 * 依赖关系: C++ 标准库，无 Qt/OpenGL
 * 输入输出: 输入字符、退格和清空到原文及带状态的有限数值结果。
 * 异常与错误: 非法或越界原文保留；不把未输入完成的符号/小数点当作零。
 * 维护说明: 调用方先处理轴/确认等快捷键，再送文本；业务域约束仍由操作校验。
 */
#pragma once

#include <limits>
#include <optional>
#include <string>

namespace mini3d::editor {
enum class NumericInputState { Empty, Incomplete, Invalid, OutOfRange, Valid };

/** @brief 只有 Valid 带有 value，其他状态不可提交，也不应修改场景预览。 */
struct NumericInputResult {
    NumericInputState state = NumericInputState::Empty;
    std::optional<double> value;
};

/** @brief 纯文本缓冲；每次解析完整原文，不累计上一数值，不使用区域设置或 eval。 */
class NumericInputBuffer final {
  public:
    /** @brief 保留输入字符（含非法字符），以便准确解释和退格修复，不静默丢弃表达式。 */
    void append(char character);
    void backspace();
    void clear();
    [[nodiscard]] const std::string& text() const;
    /** @brief 闭区间内有限十进制数有效；单位、科学计数和算式不在 P0 语法范围内。 */
    [[nodiscard]] NumericInputResult
    parse(double minimum = std::numeric_limits<double>::lowest(),
          double maximum = std::numeric_limits<double>::max()) const;

  private:
    std::string text_;
};
} // namespace mini3d::editor
