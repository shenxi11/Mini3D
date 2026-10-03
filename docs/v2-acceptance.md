# 二期联调、作品与验收证据（QA-01/02）

2026-10-03：获授权 P0/P1 功能开发已落地。S01–S12 自动联调在实际 Qt DPR
1 / 1.5 / 2 三档通过；最终Debug/Release完整回归各4/4通过，实际候选 ZIP 的本机隔离验收通过。
当前源码性能收尾的 Release 三档已通过原预算及新完整回归 4/4；下述 Debug/DPI/ZIP
证据对应收尾前二进制，不冒称已随本轮重验。当前 Release 已新增原生桌面核心路径视觉验收，
真实IME/原生与mixed-DPI/新机器/连续耐久/许可及部分GUI路径仍缺，不能标整体ACCEPTED。
这是本地开发/自测及限定视觉证据，不是正式发布、全部实机验收或 Blender 全功能等价证明。

## 操作配方

每档 8 用例 / 1739 断言，固定 seed=20261002。逐项报告记录真实窗口/视口尺寸、
屏幕与窗口 DPR、帧像素、源 OBJ SHA256、构建 HEAD/dirty、截图和限制。
runner 核对绝对 DPR 与完整 S01–S12，缺项/哈希错/实际 DPR 不符即失败。

| 配方 | 已验证的实际动作与结果 | 100% 证据 |
| --- | --- | --- |
| S01 | 建模工作区、右侧树/属性、状态栏，无时间轴占位 | [JSON](validation/v2/final/dpr100/artifacts/S01.json) |
| S02 | Tab→3→顶面鼠标选择，活动面/N/树一致 | [JSON](validation/v2/final/dpr100/artifacts/S02.json) |
| S03 | F3 inset/中文查询，过期上下文拒绝且解释，正常内插 | [JSON](validation/v2/final/dpr100/artifacts/S03.json) |
| S04 | F3收藏菜单加入→Q执行，同参数完整源与OBJ相同 | [JSON](validation/v2/final/dpr100/artifacts/S04.json) |
| S05 | I→确认→F9真实厚度字段，替换同一历史而不加第二圈 | [JSON](validation/v2/final/dpr100/artifacts/S05.json) |
| S06 | E预览→Esc，完整内容/属性/选择/历史恢复 | [JSON](validation/v2/final/dpr100/artifacts/S06.json) |
| S07 | Ctrl+R→左键→右键，居中/一历史/Undo原拓扑 | [JSON](validation/v2/final/dpr100/artifacts/S07.json) |
| S08 | T/N四次切换，投影与拾取正确，侧栏不穿透 | [JSON](validation/v2/final/dpr100/artifacts/S08.json) |
| S09 | 可见Mirror卡片点击，Merge/Clipping及G/X/数字中心夹持 | [JSON](validation/v2/final/dpr100/artifacts/S09.json) |
| S10 | 树选对象→小键盘/进出，原显隐/历史/源不变，原帧恢复 | [JSON](validation/v2/final/dpr100/artifacts/S10.json) |
| S11 | 保存→F9→实际关闭提示→取消，保留修改/旧磁盘版本 | [JSON](validation/v2/final/dpr100/artifacts/S11.json) |
| S12 | DPR/实际帧一致、真实双屏移动后拾取/状态保持 | [JSON](validation/v2/final/dpr100/artifacts/S12.json) |

最终构建三档汇总：[100%](validation/v2/final/dpr100/report.json)、[150%](validation/v2/final/dpr150/report.json)、
[200%](validation/v2/final/dpr200/report.json)。旧dpr100/dpr150/dpr200证据保留，未冒用旧测试EXE哈希。
两块实际屏幕的 DPR 相同，因此不证明 mixed-DPI。
200% 系统限制了窗口几何，报告保存实际尺寸；没有把请求 1440×900 当作实际观测值。
Qt Scale 是有效 Qt DPR 的覆盖，不改变 Windows 原生缩放。

## 可打开、继续编辑的原生作品

使用当前程序“文件 → 打开场景”，先另存副本。场景与对应 OBJ 位于
`assets/scenes/v2/`；源/求值区分保留，文件哈希见该目录 `manifest.json`。
这些作品来自上述通过的真实 C++ 用例，不是 HTML 效果稿或 Blender 资产。

| 作品 | 真实制作和保存内容 | 源 / 求值 | 截图 |
| --- | --- | --- | --- |
| shell.m3dscene | Cube内插、挤出、环切，取消/撤销/重做/保存重开 | 源20点18面，无修改器 | [外壳](validation/v2/final/dpr100/artifacts/shell.png) |
| symmetric.m3dscene | 原生Cube程序化半壳，X Mirror / Merge / Clipping，应用再撤销 | 源8点5面；求值12点10面 | [对称件](validation/v2/final/dpr100/artifacts/symmetric.png) |
| subdivision.m3dscene | 同一半壳，固定Mirror→二级Catmull–Clark，源笼只读求值分离 | 源8点5面；求值162点160面 | [细分](validation/v2/final/dpr100/artifacts/subdivision.png) |

半壳与缝点选择是公开 VM 夹具，不是未实现的 Bisect 或鼠标选缝证据。
保存/重开/OBJ输出使用公开 VM API，不当作文件对话框鼠标验收；实际输入项单列在JSON。
外壳还有20轮取消及20轮确认/Undo检查，之后执行一次Redo、保存和重开；
这属于循环回归，不冒充每轮保存重开或30分钟人工耐久。

## Blender 实际数值复核

[报告](validation/v2/blender.json)由本机 Blender **5.1.1**（b70da489d7f4）在
`--background --factory-startup --disable-autoexec` 下运行，配置目录隔离到本次证据路径。
没有改用户设置、安装插件、读取用户 .blend 或使用 `E:/BlenderProject`。

- 真实导入 5 个源/求值 OBJ，位置误差为0；点/面数量、完整多边形连接与绕序一致。
- 从实际场景参数重建 Mirror：12点10面，位置误差为0。
- Mirror→二级有限 Catmull–Clark：162点160面，最大位置误差
  7.30005×10⁻⁸，小于 1×10⁻⁵ 容差；连接与绕序一致。

明确用 `forward_axis=Y, up_axis=Z` 保持 OBJ 的数值坐标；这不是把 Mini3D 改成 Z-up。
细分关闭 `use_limit_surface`，边界设 ALL，对比的是有限级离散位置，
不声称 Blender 默认极限曲面、UV/分裂法线/材质、交互或全部模型等价。
方案固定参考为 4.5.0；本机 5.1.1 实测不能冒充 4.5.0 实测。
最终构建重新输出的3场景/5OBJ与正式样例逐字节SHA256一致，因此这些未变OBJ可复用
该外部复核；没有把旧程序验证冒充为最终二进制运行。

可复跑：

```powershell
& './tools/Run-V2Acceptance.ps1' -BuildDirectory <构建目录> -QtBinDirectory <Qt-bin目录> -OutputDirectory <新目录> -Scale '1'
& '<Blender>/blender.exe' --background --factory-startup --disable-autoexec --python-exit-code 5 --python tools/Compare-V2Blender.py -- <通过的artifacts目录> <新报告.json>
```

分别运行 Scale 1.5 / 2；Qt/GL窗口验收必须串行，不与其他UI/GPU测试争抢窗口。
Blender复跑时隔离 `BLENDER_USER_CONFIG/SCRIPTS/DATAFILES`，本机示例见
`out/validation/v2-blender-run.ps1`。已有输出拒绝覆盖，失败证据保留。

## 最终验收边界

仍需真实中文 IME、原生 Windows 100/150/200% 与 mixed-DPI 跨屏、全新机器、
完整人工耐久和正式许可/公开发布核查。自动合成输入、同DPR双屏和本机隔离包不能替代它们。
以前偶发UI失活/无变化历史、对象启动坐标和游标点击缺信号的原证据保留；
最终回归通过也不等于已证明这些旧失败的根因被修复。

性能支持范围见 [性能报告](v2-performance.md)，所有功能限制见
[F1手册](Mini3D_使用手册.html)与[兼容边界](blender-compatibility.md)。
最终全量与实际 ZIP 结果见下文；后续人工/外部验收在本页和 progress.md追加，不标整体ACCEPTED。

## 本轮全量发现与定向修复

首次 Debug 全量在编辑器组发现3条失败并崩溃，证据保留在
`out/validation/v2-completion-final-Debug/`，没有以此前配方通过掩盖全量失败。

- 活动组件变换在窗口拆除时取消，状态回调访问正在销毁的子控件。
  现在主窗口在子控件拆除前取消事务；直接销毁活动比例会话的回归确认候选恢复、
  无新增历史、会话和鼠标抓取清理，并不需要先按Esc或隐藏窗口。
- 比例圈的QPainter继承了3D深度检查与背面剔除，实际截图确认数据正常却无圈。
  绘制二维圈时暂时关闭这两项状态，绘制完恢复；真实帧的开关差异和O/滚轮/取消通过。
- 集合面板加入后，树视口实际仅54逻辑像素。旧用例把不可见行或仍有对象的区域
  当作点击目标/空白根层。现测试先滚动并检查命中，拖放测试展开场景区并断言根层
  目标真的无行；没有放宽树模型行间排序、循环引用或跨文档拒绝规则。

同种子 Debug 定向6用例/164断言通过，独立Sol/max静态复核未发现可操作P0–P2。
指定配置获工具接受，但provider内部模型映射未验证；静态复核不代替实跑。
原失败及坐标诊断、比例圈前后PNG位于
`out/validation/v2-debug-*-20261002/`。两配置最终全量结果见下节，
不能将这些已定位的问题推广成以前所有偶发UI问题的根因。

## 最终本机回归与构建追踪

两配置完整构建及CTest各4/4通过，Debug编辑器160.56秒、Release47.61秒。
[正式报告](validation/v2/final/regression/report.json)保留二进制和源文件SHA256，
旁列分组原始build/ctest日志。Release详细统计为Core160/25164、Assets30/524、
GPU4/132、Editor220/8315（用例/断言）。

CTest共用的LastTest.log已被Release覆盖，故只归档到Release目录，不冒充Debug详细日志。
最终主程序SHA256为`74B61866E5F9F853B4FAABB65E79E56A21900D39D4560AA7BB5CCBC619CEBA45`；
配方测试EXE为`245CE43FC024F251BFC787862C0176F6219FF1CB30840D72851886862A6FF19E`。
HEAD/dirty不足以区分构建，正式sources.json补充源文件快照哈希。

性能收尾前交付构建的三档各20完整样本，正确性通过但均存在超预算项：1万点预览API/反馈
55.01/58.25ms，5万225.30/234.41ms，10万571.85/594.14ms。旧1万36.12/38.59ms仍保留，
但不覆盖本次结果，不推断波动根因；详见[性能报告](v2-performance.md)。

## QA-02 实际 ZIP 本机隔离交付

2026-10-03 已验证实际档案 `out/packages/Mini3D-v2-candidate-windows-x64-20261002.zip`，
不是以 stage 目录代替 ZIP。档案大小为45,119,445字节，SHA256为
`547020BEAA707816E0F4824334B7D6ED5767C393BA1FCF093AA651F31E97D589`。
其中主程序与上节最终 Release SHA256 一致，未修改或重新编译生产源码。

[正式外部报告](validation/v2/final/package/report.json)、
[逐文件证据清单](validation/v2/final/package/manifest.json)与ZIP旁同名 `.validation.json`
记录该档案的哈希。包内文档是生成ZIP前的快照；最终结果留在包外，不重打已验收档案。
回归sources.json只代表构建时源快照；后续验证工具和文档变更不冒称与该快照完全一致。

- 从ZIP解压到新目录，再复制到 `relocated app`，净PATH、不同工作目录启动。
- demo、showcase及三原生作品均退出0，生成各自视口/工作台截图；已查看三作品工作台截图，
  分别显示外壳、对称件和细分形体。缺失文件退出4，且未产生正常渲染截图。
- manifest全部文件哈希、离线手册及34图引用存在、帮助路径留在迁移包内均通过。
- 11项Qt/插件/解析/CRT必需模块实际加载自包内，不依赖开发机Qt/vcpkg的PATH。

首轮失败保留于 `out/validation/v2-completion-actual-zip/`：Release GUI默认Qt日志没有
写入重定向stderr，脚本读不到帮助行。一次诊断强制stderr后取得中文帮助路径，确认本机
实际日志编码ANSI936，帮助文件未越界。`tools/Test-Package.ps1`现在显式设置
`QT_FORCE_STDERR_LOGGING=1`并按本机ANSI编码读取，输出保存UTF-8；没有放宽路径检查。
修复后证据独立保存在 `out/validation/v2-completion-actual-zip-fixed-20261003/`，
报告记录实际验证工具SHA256及codepage936。

这是本机隔离部署，不是全新Windows、真实IME/mixed-DPI、30分钟人工耐久或许可验收。
包烟测检查帮助URL和离线文件，不冒充再次按F1或启动外部浏览器；F1操作另由三档配方验证。
此 ZIP 不包含随后完成的性能收尾代码；整体状态仍非ACCEPTED，不提交、推送或发布。

## 2026-10-03 PERF-01 性能收尾构建

当前 Release 三档独立进程、每项 20 完整样本/2 预热均通过：Visible/X-Ray 点击、框选与
begin/preview API P95≤50ms，preview 起点到实际 `frameSwapped` P95≤100ms。
预览 API/换帧 P95：1万 6.55/9.77ms、5万 26.85/36.88ms、10万 36.73/52.88ms；
10万可见框选 45.55ms。完整预期选择 ID 比对、取消源哈希/选区/历史恢复均通过。

新 Release 完整 CTest 4/4，Core174/33524、Assets33/679、GPU4/132、Editor221/8394
（用例/断言）；编辑器44.52s。定向验证与失败轮次独立保留，不无改动重测挑选好结果。
[正式证据与哈希](performance/v2/closeout-20261003/manifest.json)绑定当前测量、最终回归、
生产源和二进制；[性能说明](v2-performance.md)列出优化及测试边界。

这不是 Debug、三档 DPI、全部点边面/复杂拓扑/修改器、跨硬件或旧候选包的再次验收。
旧 ZIP 原 SHA256 不变；新主程序 SHA256 为
`0B55C152DC9F230D217F3B42658B30C315FF2DF52A59200ABB0375709DC7E69A`。
本轮未重打包、提交、推送或发布。人工IME/mixed-DPI/新机器/耐久/许可仍按上文保留。

## 2026-10-03 当前 Release 原生桌面视觉验收

用户授权代理构建后直接视觉测试、无需用户操作。当前 Release 构建/独立部署成功，
主程序哈希与性能收尾一致；真实点击/按键/拖动/滚轮后逐步观察截图，不使用 Qt Test/VM API
制造结果。209步记录含196桌面输入/激活动作，258原始截图核对，正式归档52张精选原图。
[报告与实际覆盖矩阵](validation/v2/visual-manual-20261003/report.md)、
[机器可读结果](validation/v2/visual-manual-20261003/report.json)及
[逐文件哈希](validation/v2/visual-manual-20261003/manifest.json)可复核。

内插/F9单历史、挤出取消、环切/Undo/Redo、中文路径保存重开/真实OBJ导出、X-Ray框选、
比例圈滚轮/取消、Local View、旋转后世界轴缩放、未保存关闭取消、视图/覆盖层、
着色收藏/游标/移动吸附、集合显隐、Mirror夹持/合并应用与细分整链应用/撤销均有实际输入证据。
四个测试场景和OBJ仅写新副本；OBJ20点18面、连接/绕序一致，世界位置误差0。
Mirror/细分撤销后的保存副本与正式样例逐字节相同；8个正式素材及旧ZIP未改变。
本轮未确认所覆盖路径的阻断性产品缺陷，24项相关源码/测试/探针哈希仍与性能回归相符。
偏好恢复后导出哈希与测试前一致，测试窗口正常关闭，无遗留Mini3D进程或用户窗口关闭。

不冒称重跑完整S01–S12、全部P0/P1或三档DPI。真实IME、原生/mixed-DPI、新机器、
完整连续耐久与许可仍未验收；中键/Shift中键拖动超出当前输入工具表达能力。
GUI删除、Loop/Ring/连通选择、倒角、补面、Repeat及全部异常边界未分别复验。
Alt+Z被NVIDIA截获，改用工具栏检查X-Ray；滚轮缩放曾dirty，完整观察限制见报告。
首轮归档文档旧快照和第二轮可选法线假设错误均保留，修正检查器不改产品或模型。
结论仅为本机覆盖路径通过，整体仍非ACCEPTED；不打包、提交、推送或发布。
