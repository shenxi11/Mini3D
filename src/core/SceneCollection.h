/*
 * 模块名: SceneCollection
 * 功能概述: 定义独立于对象父子层级的单层集合及稳定成员身份。
 * 对外接口: CollectionId、SceneCollection
 * 依赖关系: EntityId、C++ 标准集合与字符串，无 Qt/GL
 * 输入输出: 集合 ID、名称、可见性和直接成员 ID；不改变对象变换或父关系。
 * 异常与错误: Scene 校验集合及成员后才发布，0 与最大 ID 均为保留值。
 * 维护说明: 单个对象最多属于一个集合；隐藏成员传播到其对象子树。
 */
#pragma once

#include "EntityId.h"

#include <cstdint>
#include <set>
#include <string>

namespace mini3d::core {
using CollectionId = std::uint64_t;

/** @brief 持久化的单层集合；成员不是 SceneNode 的 parent，不参与 TRS 组合。 */
struct SceneCollection {
    CollectionId id = 0;
    std::string name;
    bool visible = true;
    std::set<EntityId> members;
    bool operator==(const SceneCollection&) const = default;
};
} // namespace mini3d::core
