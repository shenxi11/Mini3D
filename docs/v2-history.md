# F9 历史与上一步参数（HISTORY-01/02）

## 当前边界

区域挤出的“调整上一步”意图、唯一历史适配、F9/菜单/F3/Q 和左下参数面板已接通。
目前区域挤出和单面内插可重开；已实现的环切只使用普通快照历史，不开放环切F9。
当前不能把 Ctrl+Y/Redo 当作 F9；确认后再次按 E 仍是新的挤出和新的历史。

SceneViewModel 提供 `lastOperationWorldOffset()`、`lastOperationDisabledReason()`、
`adjustLastOperation(worldOffset)` 和 `lastOperationChanged`。参数是全局世界位移 X/Y/Z，
不是再次叠加的增量；面板明确显示“全局位移（世界单位）”，不是法线距离。
内插使用 `lastOperationInsetThickness()` / `adjustLastInset(thickness)`，参数是对象局部
厚度 double，面板显示“内插厚度（局部单位）”；见 [面内插](v2-inset-face.md)。
服务内部以 `GeometryOperationParameters` 的 vector3/double 区分参数类型，错用接口拒绝。

## 使用参数面板

1. 确认一次区域挤出或面内插后，真实视口左下自动出现折叠标题，不抢焦点。
2. 点击标题展开；Blender 风格在视口按 F9 展开同一面板并聚焦 Z 字段。
   菜单“编辑 → 调整上一步…”、F3 搜索“调整上一步 / Adjust Last Operation”，
   或将该操作加入 Q 收藏，均打开同一面板。Legacy 不绑定 F9，使用上述替代入口。
   内插时切换为厚度字段并聚焦该字段，不展示挤出的 XYZ 输入。
3. 编辑 X/Y/Z；Enter 或普通失焦提交该轴，其余轴取精确的已确认参数，不从舍入文字拼接。
   字段显示六位小数；只打开/聚焦/离开面板不会舍入修改未编辑参数。
4. 非法输入保留原文字和原因，不改变几何；Esc 恢复当前字段草稿。
   点击标题收起会丢弃尚未提交的草稿，不撤销已经确认的参数调整。
5. 其他数据操作/撤销到非末端时，已显示面板禁用字段并解释原因；新建/打开清空并隐藏。
   编辑会话内临时导航不使上一步失效，当前模态变换期间暂禁用，取消后可恢复。

面板属于 View，只发 ViewModel 意图，不直接写源网格或历史；HistoryService 管理原命令。
使用原 ViewportWidget 子控件叠加，不重建或重挂 GL 控件。说明/空白上的鼠标点击、滚轮
和 G/R/S 不穿透相机或组件选择，字段保留文本/IME 与本地文本撤销优先级。
收起按钮的鼠标焦点转移早于 pressed/toggled；字段在该失焦提交前清草稿，普通 Tab/失焦
仍按字段提交规则处理。右键菜单和原有文件未保存保护不被替换。

## 调整规则

- 仅当前历史顶端的可重开挤出或内插，且仍处于同一对象的编辑模式时可用。
  当前组件/对象变换未结束时拒绝调整，不静默提交或取消正在进行的操作。
- 冻结该次挤出的原 before、所选 FaceId 和完整世界矩阵，基于新世界位移重新求值。
  第一次挤出后的第二次挤出有自己的 before；调整第二次不会丢掉第一次的结果。
  内插冻结一个面及 before，按新局部厚度重新偏移边线；连续调整不会叠加边框。
- 使用原面域，之后的临时选区变化不改变挤出对象；成功调整后恢复该次顶盖选区。
  导航与查看面板不替换建模记录；退出 Edit 暂时禁用，回到相同对象可恢复使用。
- 其他数据命令成为栈顶后不可重开旧操作；撤销后不在历史末端也不可调整。
  重做回原挤出且其再次为末端时可用。打开/新建清空旧历史，不保留指向旧命令的地址。
- 零位移、非有限数、退化侧壁和其他区域挤出校验失败保留原模型与参数。
  内插的零/负厚度、边坍缩上限或非法形状同样拒绝，保留上一确认结果。
  相同参数直接无操作；浮点量化后相同几何也不改参数、revision、选择或保存点。

## 一条历史与原子提交

SceneViewModel 仍是唯一 QUndoStack 的拥有者，HistoryService 只持有其引用。
可重开快照命令由 QUndoStack 拥有，保存 before/after、选区、带类型参数和离线重算回调。
Undo/Redo 安装快照，不重新运行建模算法；revision 单调前进。

原方案推荐的临时 undo 再 push 路径需要处理回退后分配失败。当前采用同一逻辑命令中
替换 after：既不增加记录，也不临时发出 before 状态，不使用 const_cast 或第二套历史。

1. 离线生成并完整校验候选几何；结果相同直接返回。
2. 提前复制结果选区、安装状态及通知回调，所有可能分配的准备工作在写真源前完成。
3. 无分配安装 Scene 快照、移动选区和 after、更新参数。此段不发 UI 通知。
4. 必要时失效保存点，再通知选择、实体、场景和参数变化；监听者只看到完整新结果。

准备阶段捕获 `std::bad_alloc`，报告“内存不足，未修改模型或上一步参数”。
实际安装回调必须无异常且不能分配或递归修改历史；Scene 的预留网格槽与选择容器移动
满足本项提交路径。测试注入离线重算和通知副本构造的分配失败，不宣称能够恢复
操作系统、Qt 或图形驱动自身的致命内存耗尽。

若已保存的正是被替换的顶端，调用 resetClean，使 index 相同的新结果仍为未保存。
若保存点在 before，则保留该保存点，Undo 回 before 可正确显示已保存。
调整不自动覆写磁盘文件；再次保存才写新结果。工程文件不保存历史或 F9 参数记录。

## 验证与回滚

`tests/HistoryServiceTests.cpp` 是无窗口/无 GL 的真实 Scene/QUndoStack 夹具；
`tests/HistoryEditorTests.cpp` 验证 ViewModel、源几何、选择、通知、保存文件和父变换。
验证结果及失败记录见 [进度记录](../progress.md)，本机证据前缀为
`out/validation/v2-history01-*`；共用挤出高 DPI 回归使用 `v2-model01-*-history01-*`。

最终 Debug/Release 完整 build 与 CTest 均通过（各 4/4）；历史专项各 8 用例/207 断言。
200% 挤出 9 用例/215 断言、共用输入 43 用例/1354 断言通过，未改变现有 E 的操作方式。

回滚点 `out/validation/v2-before-history01/`，只恢复本项修改的文件，不回退 MODEL-01。
新增服务 h/cpp、两个历史测试与本文分别备份后撤回，旧文件按 progress 的清单用
`Copy-Item -LiteralPath` 定向恢复，保留进度历史并追加回滚说明，然后重新 CMake/build/test。
不修改 PowerShell 固化配置，不恢复过期路径，不执行全仓 reset/checkout。

### HISTORY-02 证据

`tests/LastOperationPanelTests.cpp` 验证真实 E/F9/数值输入、保存后关闭提示、菜单/F3/Q、
Legacy、过期上下文、输入隔离、六位显示与精确未编辑轴、导航、临时禁用、布局和 Context。
面板专项为 6 用例/161 断言；Debug/Release 完整 build 和 CTest 均通过（各 4/4），
面板/数值/历史/挤出/搜索/收藏/键位综合两配置各 38 用例/905 断言，150/200% 同样通过。
本机验证脚本为 `out/validation/v2-history02-test.ps1`、`v2-history02-verify.ps1`，
证据前缀为 `out/validation/v2-history02-*`。截图来自实际 Qt 窗口，不是效果示意图。
单屏 100/150/200% 与合成 IME 事件不能替代跨屏、真实中文输入法或 Blender 对照验收。
200% 请求的窗口高度受当前屏幕工作区夹取，布局断言以实际视口为准；面板内容保持可读。

本项回滚点 `out/validation/v2-before-history02/`：先备份后续改动，按 progress 清单用
`Copy-Item -LiteralPath` 恢复旧文件；新增面板 h/cpp 和测试单独移入回滚存档。
保留 progress 历史并追加回滚说明，重新 CMake/build/test。不撤回已通过的 HISTORY-01。
