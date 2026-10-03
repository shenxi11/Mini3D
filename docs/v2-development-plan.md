# Mini3D V2.0 R2 二期实施与验收计划

## 结论与当前状态

2026-09-11 启动长计划。目标是完成真实 C++ 建模闭环，不以 HTML 原型或菜单占位代替功能。
实施按下表逐项完成“实现 → 验证 → progress.md 追加”，一项失败先定位，不带病进入下一项。
没有完成全部门槛前，不标记 V2 完成，也不覆盖旧候选包或自动提交、推送、发布。

2026-10-03 当前交付：全部获授权P0/P1开发实现已落地，最终Debug/Release完整回归各4/4、
S01–S12在Qt DPR1/1.5/2各8用例1739断言通过。三份原生作品及五份OBJ已交付，
Blender5.1.1实际数值复核通过（不冒充参考4.5实测）。实际独立二期候选ZIP的本机隔离验收通过，
外部报告见[包验收](validation/v2/final/package/report.json)，包内保留打包前文档快照。
2026-10-03 性能收尾后当前 Release 三档完整样本均通过原预算；预览 API/换帧 P95
分别为 6.55/9.77、26.85/36.88、36.73/52.88ms。新 Release 完整回归 4/4 通过，
旧 Debug/DPI/ZIP 为收尾前证据，候选 ZIP 未包含新性能代码；详见[v2-performance](v2-performance.md)。
人工IME/mixed-DPI/新机器/耐久与许可仍缺，故开发完成不等于整体ACCEPTED；详见[v2-acceptance](v2-acceptance.md)。

2026-10-03 用户授权代理代行视觉验收：当前Release构建后完成真实原生桌面核心流程，
209步/258原始截图、52张正式精选图；保存/重开/OBJ与Mirror/细分应用撤销核对通过。
所覆盖路径未确认阻断缺陷，偏好与窗口已恢复；详细边界见[视觉报告](validation/v2/visual-manual-20261003/report.md)。
未覆盖真实IME/原生与mixed-DPI/新机器/连续耐久/许可及部分功能输入，不标整体ACCEPTED，
不把本轮代行视觉验收写成全部S01–S12或所有P0/P1的原生GUI重验。

2026-09-29 用户授权继续完成二期剩余开发，采用开发优先：各项完成实现与最小定向检查，
Debug/Release 全量、DPI、性能测量及 QA-01/02 统一验收后置。开发阶段状态不标为最终 ACCEPTED。
MIRROR-02 已完成开发实现与 Release 定向自检，详见[单 Mirror 修改器](v2-mirror-modifier.md)；
统一验收与独立复核结论仍待记录。

2026-10-02 用户再次明确开启长计划并授权完成剩余开发。沿用原目标和顺序：
Mirror 收尾 → FILE-01/02 → P1-SELECT/PROP/BEVEL/SUBDIV/COLLECTION/UI → PERF-01 → QA-01/02。
每项先实现、最小相关验证并追加日志；所有功能落地后集中全量验收，不自动提交或发布。
现有未提交二期源码已保存前稿于 `out/validation/v2-before-completion-20261002/`。
Mirror 收尾完成：Release 核心 2/35、编辑器 4/46 复验通过，独立 Sol/max 聚焦复核无阻碍问题。
本轮进入 FILE-01；S09 与两配置全量验收仍后置。

2026-10-02 用户要求“先全部暂停”，主任务与在途 FILE-01 实施代理已停止。
暂停点：Mirror 收尾完成；FILE-01 仅完成定位与方案确认，尚未修改业务源码或新增测试。
FILE-02 已完成只读入口调研，未实施。后续全部任务等待用户明确恢复，不自动排程。

2026-10-02 用户随后明确“全部重新启动继续完成所有任务”，暂停授权撤销，
从 FILE-01 接续全部剩余任务，保持上次前稿与已通过 Mirror 收尾证据，不重复验收。

本次恢复已依序完成FILE-01/02、P1-SELECT/PROP，以及倒角/细分的开发与定向检查。
后两项共用最终接线回归：Core9/986、Editor16/555、Assets10/162；核心算法14/2601。
集合开发闭环已完成：Core17/605、最终Editor13/1042；独立审查发现的等价重命名显隐
问题已稳定复现并修复。P1-UI也已完成开发：Editor25/672、GPU2/74通过；
真实饼菜单/三种着色/当前源Repeat接通。PERF优化与测量已完成，QA自动回归通过，
候选包已完成本机隔离交付，未通过/外部验收项见本页最新状态；尚未标记整体ACCEPTED。

- 代码基线：`6dd209f38122aca837ec04077d58b78da2f47726`，开始时跟踪文件干净。
- 已完整读取：用户提供的 R2 DOCX 正文/表格、效果预览说明 1/2 HTML 原文。
- 同名 Markdown、DOCX 和两份 HTML 是用户原始资料，保持原样，不重写、不移动。
- HTML 1 为原创工作台交互原型，网格统计和几何是静态示意；HTML 2 为联网历史截图参考册。
  未把其当成本机 C++ 运行或 Blender 4.5 实测证据，尚未在浏览器渲染核验。
- 基线事实：C++20 / Qt 6.8.3 Widgets / OpenGL、右手 Y-up、单线程、唯一 QUndoStack，
  场景写 2 读 1/2；已有相机/灯光、对象变换、资源导入、汉化及历史。
- 现有 README 的 V1 状态仍是候选交付；许可、全新机器与完整耐久未通过，不能据此宣布正式发布。
- M0、UI-01、UI-02、UI-NUMERIC、INPUT-01 已完成各自实现/验证/记录；说明见 [当前工作台](v2-workbench.md)、[输入框架](v2-input-operations.md)。
  OPS-01/02 的 Registry/F3/Q、INPUT-02 数值缓冲及对象模态 G/R/S 也已完成并验证。
  新默认键位、选择器、Home/小键盘 0 及区域最大化/重绑定已验证，M2 操作与输入上下文完成。
  M3 的 MESH-01/02 独立网格快照、校验、邻接、三角化及来源映射已完成。
  2026-09-12 用户明确授权完整完成后续全部任务，覆盖格式 3 核心协议升级及全部 P1。
  EDIT-01 场景绑定、快照历史、GPU 刷新与最小格式 3 已通过两配置验证。
  EDIT-02 的 Tab 单对象编辑、稳定点/边/面选择、可见单击/Shift、多选反馈与模式互斥
  已通过两配置及 150/200% 验证。PICK-01 的 B 框选、X-Ray、独立覆盖层开关、
  连续遮挡区间与裁剪边段也已完成两配置和 DPI 验证。
  EDIT-03 的点/边/面 G/R/S、独立预览、取消、快照历史与保存重开已完成两配置及
  150/200% 验证，详见 [组件变换](v2-component-transform.md)。MODEL-01 区域挤出已完成
  连通面域、侧壁属性、真实预览、安全取消、顶盖选择与保存重开，两配置全量及高 DPI
  验证通过，详见 [区域挤出](v2-extrude-region.md)。HISTORY-01 的 before 重算、同一历史
  替换、分配失败保护和保存点已完成，两配置全量通过，见 [F9 历史基础](v2-history.md)。
  HISTORY-02 已接通 F9/菜单/F3/Q 与左下同一参数面板，世界 XYZ 调参不累积，草稿与输入
  隔离已验证；两配置完整构建/CTest 各4/4，150/200%综合各38用例905断言通过。
  MODEL-02 单个共面凸面等距内插已完成，I/菜单/F3/Q、局部厚度 F9、取消/历史/保存均通过。
  两配置完整 build/CTest 各4/4；Core各6用例608断言，综合各77用例2208断言；
  150%同样通过，200%为77用例2219断言。详见 [面内插](v2-inset-face.md)。
  MODEL-03 规则四边形带单环切已完成：Ctrl+R预览→滑移、右键居中确认、Esc整次取消，
  新切线选择、唯一历史与保存重开通过。两配置全build/CTest各4/4，Core各6用例392断言；
  综合两配置/150%各86用例2431断言，200%为86用例2442断言，详见 [环切](v2-loop-cut.md)。
  2026-09-15 用户明确恢复开发，本轮继续 MODEL-04 删除/补面，未扩展到游标或 Mirror。
  2026-09-16 MODEL-04 完成：分域删除/孤立点清理、F 单环共面补面、新面选择与一次历史、
  保存重开均已验证。Debug/Release 全 build/CTest 各4/4；Core专项各5用例426断言、
  Editor专项各7用例176断言；150/200%相关回归各26用例607断言，见 [删除与补面](v2-fill-delete.md)。
  2026-09-17 从暂停点恢复并完成 CURSOR-01：表面 / XZ 地面放置、N 世界 XYZ、
  游标到选择 / 原点、基础体在游标创建，以及格式 3 最小辅助状态保存。
  Debug/Release 全 build/CTest 各4/4；序列化1用例30断言、拾取2用例30断言、
  Release游标5用例165断言；150/200%相关回归各21用例590断言，见 [3D 游标](v2-cursor.md)。
  CURSOR-01阶段手册增至20章27图，当时未进入PIVOT-01。
  随后按用户“继续下一步开发”完成PIVOT-01：质心/活动元素/游标枢轴、独立方向空间、
  对象与组件R/S及对象手柄绕游标、选择到游标（保持偏移）。两配置全build/CTest各4/4，
  CPU相关14用例309断言，编辑器相关34用例1384断言；150%同样通过，200%34用例1386断言。
  详见[变换枢轴](v2-pivot.md)。PIVOT-01交付时未自动开始SNAP-01。
  2026-09-19 按用户指令完成SNAP-01：G移动步进/顶点模式、可见目标高亮、源集排除、
  数字优先/轴平面约束、Ctrl临时反转及对象手柄步进异或。两配置全build/CTest各4/4，
  拾取相关13用例209断言，编辑器相关36用例1391断言；150%相同、200%1393断言。
  详见[吸附说明](v2-snapping.md)。顶点吸附限G，手柄仍为步进；SNAP-01交付时未自动开始VIS-01。
  随后按用户“开始下一步功能”完成VIS-01：H/Alt+H临时隐藏/恢复、小键盘 `/` 子树隔离，
  元素、持久visible与局部掩码分离；显示/选择/X-Ray/吸附/游标/聚焦共用过滤。
  两配置完整build/CTest各4/4，新增专项各8用例251断言；150/200%联动各46用例1253断言。
  150%状态栏文字导致视口缩高1逻辑像素的问题已修复，尺寸/投影/原图恢复断言保留。
  详见[隐藏与局部视图](v2-visibility.md)；VIS-01交付时未自动开始MIRROR-01。
  随后按用户“现在继续开发下一个任务”完成MIRROR-01核心：局部单轴反射、对应中心边界Merge、
  独立Clipping会话、只读来源映射、Scene离线求值和应用候选；详见[镜像核心](v2-mirror-core.md)。
  两配置全build通过，镜像专项各10用例1596断言；Debug全量复验/Release全量CTest各4/4。
  Debug首轮旧游标点击未发出放置信号，原种子定向与完整复验通过但未定位根因，保留QA跟踪。
  依赖调整：原MIRROR-01持久修改器状态/Registry移至MIRROR-02，与显示/历史/保存一起闭合。
  当前没有可用Mirror界面入口，S09未通过，本轮不自动开始MIRROR-02。
  F9仍只支持区域挤出和内插，二期整体尚未完成。
  既有 UI 回归中出现过
  偶发失活与无变化历史计数失败，带诊断及最终全量复验通过；保留原证据供 QA 跟踪，
  不声称其根因已经修复。本轮首次综合回归另有对象模态启动后坐标断言失败，补充诊断后
  原种子定向/整组与最终两配置/DPI均通过，但未复现原失败，仍留QA跟踪，不能归因于环切或环境。
  PowerShell 自动定位修复保持不变。
  授权状态不是实现或验收状态，也不包含自行提交、推送或发布。

## 成功标准与范围

最终用本次真实 C++ 构建从原生 Cube 制作外壳：选面、内插、挤出、环切，预览反馈准确；
取消无残留，撤销能恢复，保存重开继续编辑，OBJ 数值正确。所有支持限制可在帮助查看。

P0 全部必交：工作台；新/旧互斥键位；Registry/F3/Q/F9；Object/Edit 单对象状态机；
点边面选择与框选/X-Ray；组件 G/R/S；游标/枢轴/移动步进和顶点吸附/Local View；
受限挤出、内插、环切、补面、删除；Mirror；EditableMesh 与 revision；格式 3 和 OBJ。

P1 全部已获批准，依序实施，不偷换成 P0 已完成：Loop/Ring/连通选择、Connected
Smooth 比例编辑、基础集合、视图/着色饼菜单、重复上一步、单条外凸边单段倒角、1–2 级
Catmull–Clark 与有限 Mirror → Subdivision 组合。

明确排除：动画/时间轴、雕刻、节点、完整渲染器、任意分屏、多对象同时 Edit、.blend、
任意修改器排序、复杂倒角、多段轮廓、布尔、UV 编辑和 Z-up 项目迁移。

## 执行约束

1. 一个 Issue 只改该项允许模块；具体文件在施工前确认。Core 不引用 Qt/GL，UI 不写拓扑。
2. QUndoStack 仍由 SceneViewModel 独占；预览不入历史，确认只入一条；不新建平行历史。
3. 保留关闭确认、汉化、相机/灯光、glTF meshIndex/相对路径、WXYZ、GL Context 生命周期。
4. 不增加方案外线程、网络、鉴权路径。格式 3 授权关口已于 2026-09-12 完成。
5. 所有旧文件以基线可回溯；本机配置先备份。新增验证输出放 out/validation/v2-*，
   正式说明放 docs/，progress.md 只追加。每次写入检查 UTF-8、差异和最小行为测试。
6. 失败候选离线校验，失败不得替换当前 Scene/Assets/EditableMesh，也不得损坏保存点。
7. 不展示可点击的假算子、虚构面数、假预览、假修改器。未实现入口不注册或明确禁用。

## 里程碑与依赖

| 阶段 | 业务交付 | 前置 | 完成门槛 | 初始估算 |
| --- | --- | --- | --- | --- |
| M0 | 基线与关键契约 | 原始资料、当前代码 | Debug/Release 四类测试、ADR、输入来源登记 | 10–16h |
| M1 | 可实际使用的工作台 | M0 | 右上树/右下属性、Header、T/N、布局恢复、旧入口回归 | 28–40h |
| M2 | 统一操作与输入上下文 | M1 | 新/旧键位互斥、F3/Q、文本保护、对象模态 GRS | 28–40h |
| M3 | 原生 Cube 建模纵切片 | M2、协议批准 | 8V/12E/6F、单点变换/取消/历史/格式 3 重开 | 54–78h |
| M4 | 主要拓扑工具 | M3 | 挤出/内插/环切/删除/补面成功与拒绝夹具 | 54–80h |
| M5 | F9 与对称工作流 | M4 | 调整不累积、游标/枢轴/吸附/隔离、Mirror 应用撤销 | 34–48h |
| M6 | 完整持久化与输出 | M5 | read1/2/3、故障原子性、源/求值 OBJ、缓存验证 | 20–28h |
| P1 | 已明确列出的扩展 | P1 批准、相关 P0 通过 | 每项独立算法/历史/输入/说明验收 | 见下文 |
| M7 | 联调与可复核候选交付 | 所有获批准功能 | S01–S12、DPI、性能、耐久、包、真实作品证据 | 32–50h |

R2 的 P0 260–380h、含 25% 风险约 325–475h 是工程估算，不是本次已用时间或日历承诺。
P1 原文估算：比例 12–20h、倒角 24–40h、细分 24–40h、集合/饼菜单 12–20h；
连通选择与 Repeat 未给独立估算，待纵切片后重估，不重复计算重叠工作。
阶段编号用于追踪，不代表必须整阶段巨量改动：F9 历史夹具提前到挤出之后，
最小格式 3 读写必须在 M3 完成，不能拖到 M6 才验证建模保存。

## 详细任务与允许范围

每行是一个连续执行单元；“待办”不等于已实现。测试随对应功能一起提交到本地工作区。

| ID / 状态 | 实现范围（允许模块） | 前置与不变量 | 最小验证与交付 |
| --- | --- | --- | --- |
| M0-01 完成 | 本机构建配置、docs/、progress.md | 固定 SHA，不改依赖清单/源业务 | Debug/Release 四类通过；200% 中文 4 用例/127 断言 |
| ADR-01 完成 | docs/v2-contracts.md、兼容性说明 | Y-up 保留；输入不抢文本 | 新/旧键位、取消规则、状态归属已记录，非功能完成 |
| ADR-02 已授权 | docs/v2-contracts.md、格式 schema 规划 | 真源/求值/渲染分离；协议 3 | 明确绑定、ID、revision、原子历史/读写契约 |
| UI-01 完成 | editor/MainWindow、editor/workbench、UI CMake、EditorTests | 复用现有树/Inspector/同一个 GL Widget | 两配置 4/4；150/200% 6 用例各 156 断言；当前通过菜单显隐 |
| UI-02 完成 | editor/workbench、Inspector、Renderer 背景常量、EditorTests | Workspace 不切 Edit，不标脏 | 两配置 4/4；150/200% 7 用例各 182 断言，独立 INI 重开验证 |
| UI-NUMERIC 完成 | editor 数值控件、Inspector、EditorTests | 不改变协议与唯一历史 | 两配置 4/4；200% 综合 10 用例/227 断言；输入/域错误/拖动取消 |
| INPUT-01 完成 | editor/operations、MainWindow、EditorTests | 文本/IME 优先，禁自动抢焦点；默认暂 Legacy | 两配置 4/4；输入 4 用例/49 断言；200% 综合 14 用例/276 断言 |
| OPS-01 完成 | editor/operations、workbench、MainWindow、EditorTests | 静态 Registry，复用同一动作/历史 | 两配置 4/4；搜索/输入 9 用例/171 断言；200% 综合 19 用例/398 断言 |
| OPS-02 完成 | 同 OPS-01 | 收藏仅存稳定 OperatorId；不影响项目/历史 | 两配置 4/4；带截图专项 12 用例/235 断言；200% 综合 22 用例/462 断言 |
| INPUT-02 完成 | operations/ObjectTransformSession、NumericInputBuffer、Viewport 适配 | before 重算、原事务/唯一历史；默认暂 Legacy | G X 2/R Y 45/S Shift+Z .5、轴二按、精细无跳变、Ctrl 反转、F3/Q/中断回归；两配置 4/4 |
| INPUT-03 完成 | KeymapRouter、MainWindow、workbench、SceneViewModel、CPU 相机/范围、EditorTests、docs | 新默认/Legacy 偏好、W 选择、Home/小键盘 0、Ctrl+Space、菜单替代及重绑定 | 两配置全 build/CTest 4/4；200% 综合 43 用例/1017 断言，原布局/浮窗坐标/Context 保留；不代表 Edit/F9 完成 |
| MESH-01 完成 | core/modeling、Core CMake、CPU tests | Stable Vertex/Face/Corner ID；边端点键；纯候选快照不装入 Scene | Cube 8/12/6；ID/引用/绕序/流形/有限数；两配置各 7 用例/251 断言，全 build/CTest 各 4/4；见 v2-editable-mesh.md |
| MESH-02 完成 | core/modeling、派生 MeshData、CPU tests | 面角拆点保留来源；半边下标只在当前构建有效；无 GL | 凸/凹/简单投影三角化、原外观、64位映射、非法原子拒绝；两配置网格14用例/1498断言，全build/CTest各4/4 |
| EDIT-01 完成 | Scene/Node、SceneViewModel、SceneSerializer、SceneDocument、Renderer/RayCaster、tests | Cube 独立副本；相机灯拒绝绑定；单调 revision | 两配置全 build/CTest 各4/4；CPU 8用例210断言，文件/GPU4用例124断言，200% 4用例121断言；见 v2-scene-format.md；Tab/组件交互在后续任务 |
| EDIT-02 完成 | SceneViewModel/组件 Selection、operations、workbench、Viewport/Renderer、tests | selectedIds 与 activeId 分开；稳定 ID；只读源选择与对象意图互斥 | Tab、1/2/3、A/Alt+A、可见单击/Shift、隐藏对象/删除 ID 清理；两配置全 build/CTest 各4/4；选择4用例68断言、拾取2用例36断言、编辑7用例139断言；150/200%各19用例395断言；见 v2-edit-mode.md |
| PICK-01 完成 | 组件查询/Selection、operations/工作台/SceneViewModel、Viewport/Renderer、Editor/GPU tests | 一个投影深度策略；真实 viewport rect；批量选择无历史 | 两配置全 build/CTest 各4/4；查询9用例145断言、GPU1用例48断言、输入编辑综合21用例475断言；150/200%各24用例535断言；见 v2-component-picking.md |
| EDIT-03 完成 | modeling 变换、共用 ObjectTransformSession、SceneViewModel、Renderer、tests | 独立候选不写真源，唯一历史，revision 单调 | 两配置全 build、CTest 复验各4/4；Core4用例221断言；组件8用例495断言；150/200%通过；见 v2-component-transform.md，首轮异步取消另记 QA |
| MODEL-01 完成 | modeling/Extrude、Registry、共用组件模态输入与反馈、CPU/Editor tests | 连通可定向面域与有效边界 | 两配置完整 build/CTest 各4/4；Core7用例399断言；挤出9用例215断言；共用回归43用例1343断言；150/200%通过，F9参数面板留 HISTORY-01/02 |
| HISTORY-01 完成 | operations/HistoryService、SceneViewModel、Editor tests | 唯一栈，先离线验证再替换顶端命令的 after | 两配置全 build/CTest 各4/4；8用例207断言，before 重算/不累积/无变化/故障注入/保存点/Undo/Redo；200%旧输入回归通过；UI入口留 HISTORY-02 |
| HISTORY-02 完成 | LastOperationPanel、Registry、Editor tests | 仅当前顶端 reopenable；导航不失效 | 两配置全build/CTest各4/4；面板6用例161断言；综合两配置/150%/200%各38用例905断言；真实保存关闭/草稿/精度/输入隔离/Context |
| MODEL-02 完成 | modeling/Inset、Registry、参数 UI、CPU/Editor tests | 单个共面凸面，真正等距偏移；厚度为对象局部单位 | 两配置全build/CTest各4/4；Core各6用例608断言；综合各77用例2208断言，150%同样通过，200%77用例2219断言；I/F3/Q/F9/保存/取消 |
| MODEL-03 完成 | modeling/LoopCut、ModalSession、Registry、CPU/Editor tests | 无分叉四边形带，单切，不留 T 接 | 两配置全build/CTest各4/4；Core各6用例392断言；综合两配置/150%各86用例2431断言，200%86用例2442断言；预览→Slide/居中/取消/保存；完成后按用户要求暂停 |
| MODEL-04 完成 | modeling/Fill/Delete、Registry、CPU/Editor tests | 简单共面边界无孔；删除语义区分域 | 两配置全 build/CTest各4/4；Core各5用例426断言、Editor各7用例176断言；150/200%各26用例607断言；分域菜单/补面/选择/Undo/保存，见 v2-fill-delete.md |
| CURSOR-01 完成 | editorState、operations、workbench、Viewport、tests | Cursor 非 SceneNode、不标脏/不入历史；显式保存 | 两配置全build/CTest各4/4；序列化1用例30断言、拾取2用例30断言；Release游标5用例165断言、150/200%各21用例590断言；真实表面/XZ地面/平行拒绝、N/选择中心/创建/GL与保存，见v2-cursor.md |
| PIVOT-01 完成 | SceneViewModel、操作上下文、workbench、Renderer/手柄、tests | 去重质心/活动项/游标；与轴空间独立，冻结枢轴；窗口偏好不写工程 | 两配置全build/CTest各4/4；CPU14用例309断言，综合34用例1384断言；150%相同/200%1386断言；三枢轴R/S、对象手柄、父变换/选择到游标保持偏移/历史/保存，见v2-pivot.md |
| SNAP-01 完成 | 组件拾取、ModalSession、workbench、tests | 可见目标/10逻辑像素/排除源集，Ctrl异或；G顶点，手柄仍步进 | 两配置全build/CTest各4/4；拾取13用例209断言、综合36用例1391断言，150%同样/200%1393断言；源锚点/高亮/无命中自由移动/数字优先/历史/保存，见v2-snapping.md |
| VIS-01 完成 | 组件 Selection、viewport mask、operations、tests | 元素隐藏≠持久对象 visible≠局部隔离；原真源/显隐不改 | 两配置全build/CTest各4/4；新增8用例251断言，150/200%各46用例1253断言；H/Alt+H、Local View/父变换/取消/保存/历史/真实GL，见v2-visibility.md |
| MIRROR-01 核心完成（依赖调整） | core/modeling 求值、Scene只读入口、tests | 单个局部轴、对象原点，源/revision不变；不开放修改器入口 | 对应边界Merge、三轴Clipping、反射绕序/法线、只读来源、应用候选/恢复；见v2-mirror-core.md；持久状态/Registry移交02 |
| MIRROR-02 开发及本机自动验收完成 | Scene持久修改器状态、Registry、Properties卡片、SceneViewModel、Renderer、序列化、tests | 单局部轴；只读求值不可选择为可写源ID | S09三档及最终两配置全量通过；开关/阈值/夹持/应用/撤销/保存，见v2-mirror-modifier.md |
| FILE-01 开发及本机自动验收完成 | Serializer/Document/ViewModel、schema、文件 fixtures、tests | read1/2/3，损坏不得替换当前文档或活动预览 | 全字段/64位ID/路径/meshIndex/失败保留/旧件另存及最终两配置通过，见v2-file-compatibility.md |
| FILE-02 开发及本机自动验收完成 | modeling/ObjExporter、assets原子IO、ViewModel/文件UI、CPU/Editor tests | 明确源或求值、Y-up/单位；不自动覆盖 | 最终两配置、三档作品OBJ及Blender5.1.1导入/连接/绕序/数值通过，4.5实机未测 |
| PERF-01 性能收尾完成（本机限定夹具预算通过） | tools、选择/拾取/派生/覆盖层/同步UI、规模提示、docs证据 | 源/拓扑/求值 revision 单调；可信快照边界；Context 内资源操作 | 当前Release三档各20完整样本/2预热，Visible/X-Ray选择及begin/preview≤50ms、实际换帧≤100ms；正确性和新Release4/4通过，旧失败/ZIP保留，见v2-performance.md |
| P1-SELECT 开发及本机自动验收完成 | modeling 邻接选择、ViewModel/点击/Registry、Editor/CPU tests | 规则 Loop/Ring，极点停；L 连通片 | Alt/Ctrl+Alt/L与菜单、隐藏过滤/源真选区/无历史及最终两配置通过 |
| P1-PROP 开发及本机自动验收完成 | modeling 变形、ModalSession、overlay、tests | Smooth 世界欧氏距离+Connected，不宣称测地 | 修复真实半径圈和活动窗口析构；O/滚轮/取消/历史与最终两配置通过，见v2-proportional-editing.md |
| P1-BEVEL 开发及本机自动验收完成 | modeling/Bevel、Registry、tests | 单外凸流形边、单段、三价端角闭合 | Ctrl+B/取消/数字/F9/Undo/保存及最终两配置通过，见v2-bevel-edge.md |
| P1-SUBDIV 开发及本机自动验收完成 | modeling/Subdivision、卡片、求值链、tests | 流形四边网格含定义边界，1–2级 | 源笼/应用撤销/固定链/OBJ及两配置通过，Blender有限级数值误差7.3e-8，见v2-subdivision.md |
| P1-COLLECTION 开发及本机自动验收完成 | Scene 集合、树/操作、Serializer、tests | 单层单归属，不改对象父节点/TRS | 建/删/成员/选择/显隐/复制/Undo/文件隔离及最终两配置通过，见v2-collections.md |
| P1-UI 开发及本机自动验收完成 | workbench 饼菜单、Registry/Repeat、Editor/GPU tests | 只列已实现功能；Repeat≠F9 | Z/反引号、真实三模式、当前源Repeat、拒绝边界及最终两配置通过，见v2-pie-repeat.md |
| QA-01 本机自动与核心路径视觉验收完成（剩余边界保留） | tests/tools、docs 证据、兼容性 F1 | 全部获批准功能已过各自测试 | 最终两配置各4/4、三档S01–S12各8/1739；当前Release原生桌面209步及52精选图；未覆盖部分GUI/IME/原生与mixed-DPI/新机器/耐久，旧失败保留，不冒充根因修复 |
| QA-02 本机候选交付完成（人工缺口保留） | tools、候选包与 docs | 不覆盖 V1 包/媒体，不改变许可 | 实际ZIP解压再迁移、净PATH/不同cwd、五场景/缺失拒绝、11依赖与离线帮助通过；外壳/对称/细分已交付，人工耐久/新机器/许可未验收 |

## 输入、历史和网格的共同验收

- 输入：系统/文本/IME → 弹窗与数值 → 模态会话 → 当前区域/模式 → 文件帮助。
  F3 在打开前冻结区域/文档，关闭搜索不丢上下文；鼠标进入不强制 focus。
- 数字：独立解析符号、小数、退格和范围，不 eval；Enter 提交，Esc 恢复；非法输入保留并解释。
- 历史：一个拖动一条；无变化零条；F9 原始 before 重算替换，不能再次累加挤出；
  非顶端不能重开；失败与分配失败保证原态；保存后 F9 失效 clean point。
- 网格：ID 不因容器重排漂移；引用存在/ID 唯一/每面至少三个不同点/邻接和绕序合法/有限坐标。
  拓扑与几何 revision 撤销后也前进。全局自交未实现时只标“未检测”。
- 选择：可见性真实检查；X-Ray 不选隐藏；框选固定用顶点投影、边段、面中心规则；
  渲染三角对角线不是拓扑边；Mirror 求值部分只读；Overlay 开关不改选择。
- 文件：临时对象读完再发布；QSaveFile 原子写入；旧工程另存新路径；不保存 Undo 历史。

## S01–S12 配方追踪

| 编号 | 固定动作 | 通过依据 | 状态 |
| --- | --- | --- | --- |
| S01 | 建模工作区 | 区域、T/N、状态栏、无假时间轴 | 最终三档通过 |
| S02 | Tab→3→Cube 顶面 | 模式/面选择/活动项/N/树一致 | 最终三档通过 |
| S03 | F3 内插/inset | 同 ID，上下文冻结与禁用原因 | 最终三档通过 |
| S04 | Q 执行收藏 | 与 F3 同参数同结果 | 最终三档通过 |
| S05 | I 确认→F9 | 调参替换，不再内插一次 | 最终三档通过 |
| S06 | E 预览→Esc | 全部拓扑、属性、选择等于 before | 最终三档通过 |
| S07 | Ctrl+R→左键→右键 | 居中、一历史、撤销原拓扑 | 最终三档通过 |
| S08 | T/N 显隐 | 正确真实矩形/DPR、不穿透 | 最终三档通过 |
| S09 | Mirror/Clipping | 源笼/结果/中心行为/参数同步 | 最终三档通过 |
| S10 | Local View 进出 | 不改原 visible，不污染历史 | 最终三档通过 |
| S11 | 保存→F9→关闭 | dirty 正确且询问未保存 | 最终三档通过 |
| S12 | DPI200/跨屏 | 字/图标/布局/拾取可用 | Qt三档与真实同DPR双屏通过，Windows原生/mixed-DPI未测 |

每份证据至少记录 caseId、buildSha（含未提交差异标记）、referenceVersion、keymap、
resolution、dpr、modelHash、expected、actual、limitations。历史 Blender 图只作视觉参考。
合成 IME 事件不代替真实中文输入法、跨屏不能以单屏 DPI 模拟冒充，外部 Blender 数值核验
缺少运行环境时必须明确列缺口，不编造通过。当前性能收尾构建在本机限定夹具的三档均达到
P95≤50ms、反馈≤100ms，完整测量与限制见v2-performance.md，不泛化为全部拓扑/硬件保证。
旧包仍是收尾前版本；新构建未重复 Debug/DPI 或打包验收，人工/实机/许可门槛仍保留。

## 验证命令与交付证据

本机 CMake/CTest 为 `E:/cmake-3.31.0-rc1-windows-x86_64/bin/`，构建目录沿用
`out/build/opengl-viewport-verify`。本机路径不写入可移植 CMake 定义。

```powershell
& 'E:/cmake-3.31.0-rc1-windows-x86_64/bin/cmake.exe' --build out/build/opengl-viewport-verify --config Debug --parallel 4
& 'E:/cmake-3.31.0-rc1-windows-x86_64/bin/ctest.exe' --test-dir out/build/opengl-viewport-verify -C Debug --output-on-failure --timeout 180
& 'E:/cmake-3.31.0-rc1-windows-x86_64/bin/cmake.exe' --build out/build/opengl-viewport-verify --config Release --parallel 4
& 'E:/cmake-3.31.0-rc1-windows-x86_64/bin/ctest.exe' --test-dir out/build/opengl-viewport-verify -C Release --output-on-failure --timeout 180
```

UI/GPU 测试串行运行，避免争抢活动窗口；纯 CPU 可独立执行。每项先跑筛选用例，再相关
目标回归，里程碑完成跑全套两配置。只对改动 C++ 文件检查 clang-format，不全仓格式化。
MODEL-02 后 Debug 全编辑器已实测约80秒，示例单测试超时改为180秒，不放宽功能断言。

交付文件：本计划、契约 ADR、`docs/blender-compatibility.md`、格式 schema/迁移夹具、
各阶段测试/性能记录、最终使用说明与隔离候选包。基线与每项结果统一在 progress.md。

回滚原则：先备份后续用户修改，再按基线只恢复本任务明确旧文件；新增文件按本轮日志清单
逐个撤回；不使用全仓 reset/checkout 或递归删除。progress.md 保留记录并追加回滚说明。
