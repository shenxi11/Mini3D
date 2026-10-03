/*
 * 模块名: ComponentSelection
 * 功能概述: 保存单一选择域的稳定组件身份与独立活动项，不持有几何或渲染索引。
 * 对外接口: ComponentSelection、ComponentId、SelectionDomain、SelectionOperation
 * 依赖关系: Core EditableMesh、C++ 标准容器，无 Qt/GL。
 * 输入输出: 源网格与选择意图到选区、活动项及局部中心。
 * 异常与错误: 无效命中不改变选区；删除后 reconcile 清理失效身份。
 * 维护说明: 选择不进入历史；几何事务在 ViewModel 层组合选择快照。
 */
#pragma once

#include "core/modeling/EditableMesh.h"

#include <set>

namespace mini3d::editor {
enum class SelectionDomain { Vertex, Edge, Face };
enum class SelectionOperation { Replace, Add, Remove, Toggle };

/** @brief 点/面以 first 保存稳定 ID，second 为 0；边保存规范化的两个端点 ID。 */
struct ComponentId {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    auto operator<=>(const ComponentId&) const = default;
    [[nodiscard]] static ComponentId edge(core::modeling::EdgeKey key);
};

/** @brief 编辑上下文的值状态；操作返回是否改变状态，无变化不要求 UI 刷新。 */
class ComponentSelection {
  public:
    [[nodiscard]] SelectionDomain domain() const;
    [[nodiscard]] const std::set<ComponentId>& selectedIds() const;
    [[nodiscard]] std::optional<ComponentId> activeId() const;
    /** @brief 全选保留仍有效的活动项；否则取最小稳定 ID，不依赖容器顺序。 */
    bool selectAll(const core::modeling::EditableMesh& mesh);
    bool clear();
    /** @brief 替换/追加/移除/Shift 式切换；成功选中项成为活动项。 */
    bool select(const core::modeling::EditableMesh& mesh, ComponentId id,
                SelectionOperation operation);
    /** @brief 一次批量更新；过滤无效 ID，保留仍有效活动项，否则取最小 ID。 */
    bool selectMany(const core::modeling::EditableMesh& mesh, const std::set<ComponentId>& ids,
                    SelectionOperation operation);
    /** @brief 向低维投影取组成元素，向高维仅取边界全部选中的元素；活动项重取最小 ID。 */
    bool setDomain(const core::modeling::EditableMesh& mesh, SelectionDomain domain);
    /** @brief 仅判断域投影是否非空，不修改选区或物化投影集合。 */
    [[nodiscard]] bool hasSelectionInDomain(const core::modeling::EditableMesh& mesh,
                                            SelectionDomain domain) const;
    /** @brief 删除无效 ID，保留仍有效活动项，否则回退最小 ID；空选区没有活动项。 */
    bool reconcile(const core::modeling::EditableMesh& mesh);
    /** @brief 点位置、边中点、面角顶点均值；空选区返回空，坐标为对象局部空间。 */
    [[nodiscard]] std::optional<glm::vec3>
    activePosition(const core::modeling::EditableMesh& mesh) const;
    /** @brief 当前点/边/面选择对应的去重源顶点，供几何变换与质心计算。 */
    [[nodiscard]] std::set<core::modeling::VertexId>
    selectedVertices(const core::modeling::EditableMesh& mesh) const;
    [[nodiscard]] static std::set<ComponentId> elements(const core::modeling::EditableMesh& mesh,
                                                        SelectionDomain domain);
    bool operator==(const ComponentSelection&) const = default;

  private:
    void repairActive();
    SelectionDomain domain_ = SelectionDomain::Vertex;
    std::set<ComponentId> selectedIds_;
    std::optional<ComponentId> activeId_;
};
} // namespace mini3d::editor
