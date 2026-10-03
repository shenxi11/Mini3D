# P1-SELECT：拓扑选择

2026-10-02 开发实现与 Release 定向检查完成；最终联调仍后置。

进入单对象 Edit，按 2 切到边选择：Alt+左键选择 Loop（边循环），Ctrl+Alt+左键选择
Ring（跨四边面对边的边环）。Shift 同时按住可追加。菜单“编辑 → 组件拓扑选择”
以活动源边执行相同操作，也可从 F3/Q 使用 Loop / Ring。

Loop 在四价内部点沿唯一拓扑对边继续；普通二/三价边界沿边界环行走。
内边到边界、高价极点、非四边面处停止，不按位置猜测、不跨断开部件。
Ring 跨四边面对边继续，到边界或非四边面停止。Cube 三价内部角是极点，
因此原生 Cube 的 Loop 可能只选种子边，这不是失效；规则细化网格可形成长循环。

鼠标悬停在源点/边/面上按 L，追加其所属连通片，并投影到当前选择域。
菜单在没有视口悬停时使用活动组件；L 在空白处不跳回旧选择。
连通性来自源面环，不把渲染三角的对角线或 Mirror 求值部分当作可写组件。
隐藏项始终从结果过滤，穿透选择不绕过隐藏。选择不修改网格、不入历史、不标脏。
Legacy 键位不响应新增 Alt 点击或 L，仍可使用菜单。

Release CPU `[topology-selection]`：6 用例 / 93 断言；Editor `[topology-selection-editor]`：
3 用例 / 53 断言通过，包含真实 Alt/Ctrl+Alt 点击、L、Registry 和 Legacy 互斥。
日志：`out/validation/v2-completion-select/`。DPI/完整回归留最终验收。
