# 二期输入与操作框架

## INPUT-01：已验证的输入边界

2026-09-11 完成输入路由基础层，并在 INPUT-02 对象模态通过后开放 Blender 风格默认与
Legacy 偏好选择。Edit 选择、组件 G/R/S、区域挤出、单面内插及两者的 F9 调参已接通。
Ctrl+R 单环切已接通独立的预览/滑移两阶段，不开放环切 F9。
Edit 中 X/Delete 分域删除菜单和 F 单环补面已接通；Legacy Delete 同样打开组件菜单，
X 不绑定、F 仍聚焦，补面使用菜单/F3/Q。详见 [删除与补面](v2-fill-delete.md)。

- Legacy 保留视口/树焦点下 W/E/R、1/3/7/0/5、F 及原历史/复制/删除键。
- Blender-style 主键 1/2/3 和小键盘 1/3/7 独立；不回落到旧 R/W/F 含义。
- 文本、SpinBox、可编辑组合框、IME 预编辑、弹窗/模态对话框优先；进入区域不抢焦点。
- 以 Qt Enter/MouseMove 的屏幕坐标命中本窗区域；Leave/失活清空，不跨窗口调用。
- 菜单保留 QAction 与快捷键显示；移除旧视口/树重复关联，一次按键最多执行一次。
- 未实现的拓扑工具不注册；Edit 状态与单击选择见 [编辑模式](v2-edit-mode.md)。
- F9 打开已确认挤出/内插的左下参数面板；字段/说明区域不向底下视口路由 G/R/S 或鼠标操作。

F3/Registry/Q 已实现。此处不证明建模、持久化和 S01–S12 完成。

## OPS-01：F3 搜索与稳定操作身份

在视口或场景树按 F3，或选“编辑 → 搜索操作”。菜单替代入口明确使用视口上下文。
输入中文、英文或当前快捷键，方向键选结果，Enter/双击执行，Esc 取消并恢复原焦点。
输入法预编辑期间不占用方向键。结果同时展示分类、快捷键、调用区域和禁用原因。

当前静态注册 66 个真实操作：原 31 项增加模式切换、点/边/面选择域、全选/取消，
PICK-01 的框选、穿透选择、覆盖层开关，EDIT-03 的组件移动/旋转/缩放、MODEL-01 区域挤出
及 HISTORY-02 的 `history.adjust_last`（调整上一步）、MODEL-02 的 `mesh.inset_face`、
MODEL-03 的 `mesh.loop_cut`（环切并滑移）；MODEL-04 再增加补面、删除菜单和三个分域删除入口，
CURSOR-01 增加四个游标定位/显隐操作，PIVOT-01 增加三种枢轴和选择到游标（保持偏移）。
SNAP-01 增加吸附开关、步进类型、顶点类型三个非历史操作，均复用Header/菜单入口。
VIS-01 增加隐藏所选、恢复隐藏项、局部视图三个非历史操作；Blender 视口键位为
H / Alt+H / 小键盘 `/`，Legacy 使用菜单/F3/Q。文本输入受保护；隐藏元素不能被 X-Ray 选中，
Alt+H 恢复不自动重选，隔离退出不改持久 visible，详见[隐藏与局部视图](v2-visibility.md)。
G/R/S 随 Object/Edit 路由至不同操作 ID 和菜单标签；详见 [组件变换](v2-component-transform.md)。
B、Alt+Z、Shift+Alt+Z 在 Blender 风格下路由，
Legacy 使用菜单/Header/F3/Q；框选的取消和显示状态规则见 [组件框选](v2-component-picking.md)。
中文名称匹配优先，其次英文、当前快捷键和稳定 ID；未实现的后续建模工具不伪装可用。
Blender 风格 E 通过 `mesh.extrude_region` 启动区域挤出，需 Edit/Face 和合法连通面域；
Legacy E 保留原手柄语义。菜单/F3/Q 共用入口，HUD 明确安全取消；详见 [区域挤出](v2-extrude-region.md)。
区域挤出标记 reopenable，parameterSchema 的 worldOffset 是 world 空间 vector3；
Blender 风格 I 启动单个共面凸面内插，Legacy 使用菜单/F3/Q。内插也标记 reopenable，
schema 的 thickness 为 local 空间 number，字段“内插厚度”。鼠标水平 200 逻辑像素对应
首条边坍缩上限，Shift 精细，Ctrl 反转局部 0.1 步进，精确数字优先，XYZ 不适用；
见 [面内插](v2-inset-face.md)。
F9、菜单“编辑 → 调整上一步…”、F3 搜索“调整上一步 / Adjust Last Operation”以及 Q 收藏
共用同一面板，不触发另一次 E。Legacy 不绑定 F9，可用菜单/F3/Q。禁用原因直接来自
当前 ViewModel；仅当前可重开末端可用，详见 [上一步参数](v2-history.md)。
其他无参数操作的 parameterSchema 仍为空、reopenable/repeatable 仍为 false。

环切Ctrl+R先用鼠标下的源边预览面带，左键/Enter才进入滑移；滑移数字为百分数、
0为中点，右键居中确认，Esc取消整次操作。预览阶段右键取消，不可用通用右键行为代替。
确认只生成一条普通快照历史，当前不标记reopenable/repeatable；Legacy菜单/F3/Q同入口。
操作、规则面带与取消边界见 [环切](v2-loop-cut.md)。

搜索/已注册快捷键经 Registry 校验后触发菜单和工具条使用的同一个 QAction，再进入原
ViewModel；不复制业务代码、不创建历史栈。文档代际、模型身份、区域、键位和选择在
弹出前冻结，执行前复核。新建/打开复用 Scene 地址，因此以 documentReset 推进代际。
不把旧搜索结果用于新工程或另一个窗口。无选择/无可见几何/相机预览/无历史都有说明。
EDIT-02 进一步冻结模式、MeshId、求值 revision 和组件选择代际；切回原模式或 Undo 后
也不复用旧上下文。Edit 中整对象变换/创建/复制/删除入口拒绝执行，避免改变错误对象。

## OPS-02：Q 快捷收藏

在 F3 结果上右键“加入快捷收藏”；在 F3/Q 结果上右键“移出快捷收藏”。星标表示已收藏。
视口或场景树按 Q，或用“编辑 → 快捷收藏”打开列表；方向键选择，Enter/双击执行，Esc
关闭。Q 与 F3 没有旧版冲突，所以两套键位均提供；文字输入期间 Q 仍是文字。

初始收藏为空，不预填未实现的建模操作。列表保持添加顺序，移除再添加会移到末尾；
禁用原因和执行复核与 F3 共用，不允许对新工程执行旧收藏弹窗里的操作。

收藏保存到用户 QSettings 的 `workbench/v2/quickFavorites`，只写有序稳定 ID。
应用确认关闭时保存，下次启动恢复；不写 `.m3dscene`、不产生历史、不标记工程修改。
未知/重复 ID 在读取时忽略。测试仅使用临时 INI，没有改写用户现有偏好。

## INPUT-02：单对象即时 G/R/S

`NumericInputBuffer` 为独立无 Qt/GL 的十进制缓冲：支持正负号、小数、退格、清空和调用方
范围；区分空输入、不完整、非法、越界和有效值。只有有效结果带数值；错误原文保留，
不把 `1/2`、`1e3`、单位或非有限字符串悄悄截成合法前缀，不调用 eval。

对象模态已接通，入口为“编辑 → 即时变换”或 F3 搜索“移动对象/旋转对象/缩放对象”，
也可加入 Q 收藏。Blender 风格默认使用 G/R/S；Legacy 下仍可经菜单/F3/Q 启动即时变换，
不要在 Legacy 下用 R 期待即时旋转，R 仍为缩放手柄。

- G X 2：沿全局 X 移动 2；R Y 45：绕全局 Y 旋转 45 度；S Shift+Z .5：以当前方向空间的 X/Y 倍率 0.5 缩放，Z 倍率为 1（旋转对象的局部 scale 分量经换算）。
- X/Y/Z 为单轴；同轴再按切换全局/局部，按住自动重复不反复切空间。Shift+轴排除该轴，
  只适用于移动/缩放，旋转排轴给出拒绝原因。初始空间取工具栏当前空间。
- 鼠标移动实时预览，Shift 精细倍率 0.1，切换时位置连续；Ctrl 临时反转吸附偏好。
  G 可选步进/顶点，数字优先，轴/平面按目标位移投影；手柄和其他操作仍使用步进，见[吸附说明](v2-snapping.md)。
  鼠标步进为移动 0.5、旋转 15°、缩放 0.1；显式数值优先，不再做步进舍入。
- 无约束数字移动沿当前鼠标方向；还没有方向时沿当前空间 X，排除 X 时沿 Y。
  旋转默认以对象原点为中心，也可切到游标枢轴；未选轴时使用视线方向，缩放数字表示倍率。
- Enter/视口内左键确认一次历史；Esc/右键、视口外左键、失焦/失活/隐藏/尺寸变化、
  鼠标捕获丢失、选择/文档变化均恢复 before。文件/帮助快捷键先取消再交给原入口。
- 无效数值保留原文和最后合法预览，禁止确认，退格可修复（支持长按退格）。不支持算式、
  科学计数或单位。零/过小/非有限缩放拒绝。世界旋转产生 TRS 不能精确表示的剪切仍拒绝，可尝试切局部空间。
- 对象世界轴缩放参考 Blender 4.5 的 Object Resize：按对象实际世界轴的长度变化和方向符号
  换算局部 scale，保留 position/rotation，不再要求世界拉伸矩阵能精确分解为 TRS；不会烘焙网格。
  局部缩放仍直接乘对应 scale 分量，数字负倍率可镜像，手柄仍不跨零。
  旋转后世界轴倍率不一定等于同名局部 scale 倍率，示例与限制见[专项说明](rotated-object-scaling.md)。

开始前先取消未结束的手柄预览，再冻结 before、父矩阵、方向空间和实际视口相机。
候选始终从 before 计算，Shift 仅重设输入锚点；预览和确认沿用 SceneViewModel 原事务和
唯一历史栈。无变化零历史，取消保留已有 redo；不改变持久工具和相机，不新增协议字段。
中文 HUD 展示操作、值、约束、空间、精细/吸附与错误；工具设置栏动态显示当前键位与持久工具。

F3/Q 执行顺序为：预校验上下文 → 关闭弹窗并恢复焦点 → Registry 再校验并启动。
旧搜索框的释放/重绘事件不再中断新会话；真正文本/IME 输入仍优先并取消变换。

对象变换仍是单对象；PIVOT-01已接入游标枢轴，SNAP-01已接入G顶点吸附。组件及挤出/内插F9已接通。
区域最大化与重绑定见下节，建模入口随后续对应任务接通。

## INPUT-03：默认键位、导航与区域最大化

“编辑 → 键位配置”可选择 Blender 风格（默认，G/R/S）或 Legacy Mini3D（W/E/R）。
菜单勾选、工作台键位文字和菜单快捷键即时一致。两配置互斥，Blender 的 W 为选择工具，
不再复用旧移动手柄；键位切换取消未确认模态并恢复对象。主键 1/2/3 不代替小键盘导航。

用户正常确认关闭后保存 `workbench/v2/keymap`，值为 `blender` 或 `legacy`；
启动读取，首次运行和未知值使用 Blender。无 Emulate Numpad 默认开启，也不清理其他设置。
偏好不写工程、不标脏、不进历史；取消关闭不保存。未配置该偏好的旧用户会迁移到新默认，
需要旧习惯时切回 Legacy 并正常关闭即可保留。菜单/F3/Q 始终提供即时变换替代入口。

本项接通默认迁移、偏好、下列导航及区域最大化。

### Home 与小键盘 0

- Home 聚焦全部可见对象，不要求先选择：包含导入几何、父层级变换，无几何的空对象/
  相机/灯按世界原点纳入；隐藏对象及隐藏祖先的后代不参与。保留观察方向和当前投影。
  空场景明确拒绝并保留相机。框景不改选择或对象历史；相机导航仍沿用一期未保存判断。
- 小键盘 0 在实际实体相机与编辑视图间切换，主键 0 在 Blender 配置下不作替代。
  入预览依序选择：所选可见相机、上次预览且仍可见的相机、最小 ID 的可见相机。
  没有可见相机时给出创建/显示提示，不自动创建。最近相机仅为会话状态，切文档清空。
- 再次小键盘 0 或 Esc 返回精确的原编辑视图（包括方向预设与正交投影）；预览期间
  Home/导航拒绝、滚轮不改变编辑相机；隐藏/删除正在预览的相机自动退出。
- “视图”菜单及 Header 视图菜单提供两个入口，Legacy 同样可用；F3 搜索 Frame All /
  Toggle Camera View，或加入 Q 收藏。Registry 允许相机预览中的退出操作，不误拦截返回。

### Ctrl+Space 与快捷键重绑定

- Blender 配置下，Ctrl+Space 最大化鼠标所在的可见编辑区域，再次触发还原。支持视口、
  场景树、属性区、控制台；长按不会连续切换。文本控件/输入法/弹窗仍优先，不抢组合键。
- “视图 → 最大化当前区域 / 还原”为替代入口，使用当前焦点区域，无区域时回落视口。
  Legacy、快捷键被系统占用、文本焦点或只读控制台焦点下可使用此菜单。
- F3/Q 的最大化操作沿用打开前冻结的源区域，不把搜索框当作目标；已最大化时总是还原。
  隐藏的目标不能通过最大化偷偷打开，需要先通过视图菜单显示该区域。
- 保留中央 GL 控件身份和 Context，仅改变显隐。还原原 Dock 位置、尺寸、浮动状态、
  浮窗逻辑坐标及焦点；最大化期间暂禁 Dock 关闭/浮动与显隐动作，保留还原菜单。
  切工作区、保存或恢复偏好前先还原，临时最大化不会污染常规布局、文档或历史。
- “视图 → 设置区域最大化快捷键…”允许设置一个组合、清空禁用或恢复 Ctrl+Space。
  Ctrl+S 等现有冲突键、确认/取消/帮助/文本保留键、多段序列拒绝并显示原因。
  确定后生效，取消保留原值；替换后旧 Ctrl+Space 不再触发。仅 Blender 配置启用此快捷键。
- 偏好键为 `workbench/v2/maximizeShortcut`，存 Qt PortableText，空字符串表示禁用，
  非法/冲突值回落 Ctrl+Space。与其他键位偏好一样，正常确认关闭才保存，不写工程。

## 验证与限制

### CURSOR-01 输入增量（2026-09-16）

- Blender 风格：视口内 Shift+右键立即放置 3D 游标，不改变对象/组件选择；Legacy 不启用此鼠标组合。
- 两套键位都可用“编辑 → 3D 游标 → 点击放置 3D 游标”，再左键放置一次；Esc/右键取消。
  N 面板提供同一动作及 XYZ 世界坐标，Enter/失焦提交，Esc 恢复，非法输入保留并提示。
- 放置优先最近可见真实表面，无表面时使用 XZ/Y=0 地面；近平行或交点在视野外拒绝且保留原位置。
- 文本/IME、弹窗及已有 G/R/S、框选、环切会话优先。会话中的右键只按原规则取消/确认，
  不顺便定位；启动其他会话、失焦、窗口失活、文档/选区/视角变化均结束等待点击模式。
- Registry 新增 `cursor.place`、`cursor.to_origin`、`cursor.to_selection`、`cursor.toggle_visibility`，
  CURSOR-01阶段共56项，后续PIVOT-01增至60项；四个游标辅助操作均不是undoable。
- 相机预览不允许定位；PIVOT-01已接入游标枢轴及“选择到游标（保持偏移）”，SNAP-01另提供G移动顶点吸附目标。
  保存与操作解释见 [3D 游标](v2-cursor.md)。

## PIVOT-01 增量

- 头部枢轴菜单独立于全局/局部方向，当前模式可见；菜单、F3、Q共用
  `transform.pivot_median` / `transform.pivot_active` / `transform.pivot_cursor`。
- 三种模式不标脏、不入历史，当前窗口保留，不写工程。R/S冻结枢轴；切模式或修改游标位置取消旧预览。
- `selection.to_cursor`保留内部偏移，以组件质心/对象原点到游标的世界位移产生一条历史。
- 主键区句点菜单未接入；现有小键盘聚焦不变。详见 [变换枢轴](v2-pivot.md)。

Debug/Release build 成功，CTest 各 4/4：
`out/validation/v2-input01-debug.xml`、`out/validation/v2-input01-release.xml`。
输入专项 4 用例/49 断言；200% 输入/数值/工作台/汉化 14 用例/276 断言。

诊断发现本执行会话 Win32 GetCursorPos 失败（error 5），Qt QCursor::pos 无有效坐标，
单窗口时 widgetAt 可命中，但连续窗口回归中仍可能返回空值，纯 Qt childAt 持续命中。
最终路由在 Qt 已选定的事件接收窗口内部，使用 childAt 与真实布局坐标跟踪区域；测试
以布局坐标注入事件并校验命中，未放宽跨区域保护。诊断脚本保存在
`out/validation/v2-input-desktop-probe.ps1`。合成 IME、单屏 DPI 不代替真实输入法和跨屏验收。

OPS-01 最终 Debug/Release CTest 各 4/4：`v2-ops01-debug.xml` / `v2-ops01-release.xml`。
搜索/输入专项 9 用例/171 断言；200% 搜索/输入/数值/工作台/汉化 19 用例/398 断言。

OPS-02 最终 Debug/Release CTest 各 4/4：`v2-ops02-debug.xml` / `v2-ops02-release.xml`。
带截图的收藏/搜索/输入专项 12 用例/235 断言；200% 综合 22 用例/462 断言。
实际 Qt 弹窗截图：`out/validation/v2-ops02-debug-search.png`、`v2-ops02-debug-favorites.png`、
`v2-ops02-dpi2-search.png`、`v2-ops02-dpi2-favorites.png`。这些是当前对象操作入口的证据，
不是尚未实现的 S03 内插或 S04 建模配方完成证明。

数值缓冲基础：Debug/Release CPU 专项各 3 用例/100 断言；全套 CTest 各 4/4，
`out/validation/v2-input02-numeric-debug.xml` / `v2-input02-numeric-release.xml`。
Release CPU 测试依赖表仅 Windows/MSVC 运行库，没有 Qt 或 OpenGL DLL。

对象模态：Debug/Release 全 build 成功，CTest 各 4/4（33.55s / 12.93s），
`out/validation/v2-input02-modal-debug.xml` / `v2-input02-modal-release.xml`。
数学+数值 CPU 专项两配置各 6 用例/128 断言；带截图模态/输入/搜索/收藏专项 23 用例/542
断言；200% 综合 33 用例/780 断言。已查看真实 Qt 截图 `v2-input02-modal-debug.png`、
`v2-input02-modal-dpi2.png`，HUD 和属性值可读。DPI200 属性区域按窗口空间滚动，不能冒充跨屏验收。

键位偏好子项：Debug/Release 全 build 成功，CTest 各 4/4（34.27s / 12.46s），
`out/validation/v2-input03-preferences-debug.xml` / `v2-input03-preferences-release.xml`。
带截图偏好/模态/输入/搜索/收藏专项 25 用例/579 断言；200% 综合 35 用例/817 断言。
临时 INI 验证往返、未知值、其他键保留与无工程污染；真实 Qt 截图为
`v2-input03-preferences-debug.png`、`v2-input03-preferences-dpi2.png`，已核对动态键位文字。

Home/相机导航子项：Debug/Release 全 build 成功，CTest 各 4/4（48.48s / 12.88s），
`out/validation/v2-input03-navigation-debug.xml` / `v2-input03-navigation-release.xml`。
两配置 CPU 拾取/范围专项各 5 用例/53 断言；导航/搜索/键位专项 12 用例/273 断言；
200% 综合 38 用例/911 断言。范围与恢复按实际视口相机矩阵验证，不以截图代替数值验证。

最大化/重绑定最终验证：Debug/Release 全 build 成功，串行 CTest 各 4/4（38.52s / 13.83s），
`out/validation/v2-input03-maximize-debug.xml` / `v2-input03-maximize-release.xml`；
200% 综合 43 用例/1017 断言通过。真实 Qt 截图 `v2-input03-maximize-dpi2.png` 及
`v2-input03-maximize-dpi2.png-properties.png` 已核验，视口/属性均最大化且中文可读。
首轮属性尺寸测试遗漏布局等待，补齐后保持原断言；200% 浮窗恢复发现 Qt 屏幕夹取导致
X 偏移 6 DIP，额外保存/恢复原逻辑坐标后隔离测试与完整回归通过。串行脚本为
`out/validation/v2-input03-maximize-verify.ps1`。单屏 DPI 验证仍不代表跨屏或真实 IME 验收。
