/*
 * 模块名: EntityId
 * 功能概述: 第三周场景数据与变换支持。
 * 对外接口: EntityId
 * 依赖关系: C++ 标准库、GLM
 * 输入输出: 输入场景操作，输出节点状态或验证结果。
 * 异常与错误: 非法操作拒绝且保留既有状态；分配失败由运行时报告。
 * 维护说明: 不依赖 Qt/OpenGL，关联关系使用稳定 ID。
 */
#pragma once
#include <cstdint>
namespace mini3d::core {
/** @brief 场景生命周期内不复用的实体标识；0 表示无实体/根层。 */
using EntityId = std::uint64_t;
inline constexpr EntityId kInvalidEntity = 0;
} // namespace mini3d::core
