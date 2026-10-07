# Mini3D 原生动画第一版本机验收

任务日期2026-10-07，收口2026-10-08。当前状态：原生动画第一版R0–R5开发与本机候选收口完成，最终Astra专项与独立Sol证据复核均关闭。本文件不代表正式发布、所有环境验收或人类专家签字。

## 1. 交付结论与适用范围

R0–R4已分阶段完成实施、专项审计和独立审查；R5补齐中立样例、交叉回归、固定性能/内存测量与中文教程。新增对象/Empty/Camera/Light局部TRS整组关键帧、Constant/Linear、连续多圈Euler、中文时间轴、单调播放、姿态草稿、共享历史、格式4，以及14项动画API/MCP方法。

应用/API/MCP成套0.2.0、wire1；静态目录61方法，有观察服务实例实际61、无观察服务57，实际权限以describe为准。旧0.1明确拒绝。写格式4、读1–4，旧1/2/3首次升级另存。正常主体构建与测试目标继续分离。

本机候选适用于当前Windows x64、Qt6.8.3、MSVC14.40.33807、OpenGL4.1环境。保留默认关闭、同用户命名管道、显式读写根与应用线程串行边界；未提交、推送、发布、修改用户MCP配置或覆盖作品。

不包含骨骼/蒙皮/IK、曲线编辑器、逐轴曲线、Ease、材质/拓扑/Shader动画、视频/序列输出、外部动画导入、远程服务或脚本执行。未验环境与诊断限制见第5节，不能将其写成通过。

## 2. 最终运行证据索引

证据根为`E:/Mini3D/out/validation/native-animation-20261007-66e24b`。下表只列实际完成的独立运行；不同阶段、配置或过滤组合不能相加成一次全量成绩。中间失败和历史日志保留。

| 编号 | 实际验证 | 结果与原始来源 |
| --- | --- | --- |
| E1 | R1/R2纯Core与格式/历史 | 两配置动画CPU47例64466断言、完整CPU CTest各1/1；R2历史各10/149、文档各7/563；原始清单见[开发方案第10节](native-animation-development-plan-20261007.md#10-r2实际实施与收口证据) |
| E2 | R3统一姿态、GUI与设备 | 两配置UI18/340、相关回归83/6449；Core交换3/38、真实GPU2/272；会话/InstalledPose、DPI来源与版本边界见[开发方案第11节](native-animation-development-plan-20261007.md#11-r3实际实施与封板) |
| E3 | R4合同、账本和真实观察 | 两配置相同过滤组合263通过/3跳过、46203断言；API23/2570，观察12组；MCP冻结源码49/49、定向11/11、typecheck/build通过；[第12节](native-animation-development-plan-20261007.md#12-r4最终实施与验收)保留实际时间顺序 |
| E4 | R5最终Release默认完整Editor | 514例=512通过/2环境跳过，56201断言全过；`r5-implementation/Test-Release-mini3d_editor_tests-234435835.log`；包含增强文档发布、旧建模、F9、树选择、路径与观察回归；隐藏性能用例不在默认集内 |
| E5 | R5最终Debug受影响Editor | 32例1472断言全过；`Test-Debug-mini3d_editor_tests-235201435.log`；session、F9、树菜单、中文底层错误及独立junction夹具。不称Debug最终完整Editor全量 |
| E6 | R5新正式样例及文档 | Debug动画文档8/589，`Test-Debug-mini3d_asset_tests-235448515.log`；Release完整asset57例=54通过/3符号链接权限跳过、1632断言，`Test-Release-mini3d_asset_tests-235604472.log`；后者包含新样例和两项junction；两配置asset构建`235358631`/`235239713` exit0 |
| E7 | R5最终主体独立构建 | Debug/Release `Build-Debug-mini3d_editor-235716545.log` / `Build-Release-mini3d_editor-235721277.log` exit0，无test target；16个测试exe/PDB路径的存在性、已有文件SHA256及写入时间不变，`body-build-2e7efdda13d642baa9ba87e9968bafc5.json`。Release未生成PDB的路径保持不存在，不伪称16个文件都存在 |
| E8 | R5最终主体/正式SDK/MCP | `Real-Debug-235830933.log` / `Real-Release-235843394.log`均passed；证据`r4-real-1791388706365/evidence.json` / `r4-real-1791388720176/evidence.json`。61目录、双圈、无轨后代、纯sample、分页、批次原子拒绝、13/25/37/49真实PNG、旧eval拒绝、tick后原session CAS pause、中文格式4保存重开 |
| E9 | R5隐藏Release性能 | `Test-Release-mini3d_editor_tests-234134885.log`：1例14断言全过；完整JSON与两PNG在`r5-implementation/performance-42b44eba23534164a830f698323813e9`，口径/数据见[性能报告](native-animation-performance-20261007.md) |

E4–E7中无目录前缀的日志/JSON均在`r5-implementation`。E7嵌套隐藏构建的部分中文控制台输出有解码显示异常；ASCII目标、退出码及前后文件快照可独立核对，没有据乱码修改源文件或重写原日志。

E6第一次Release完整asset为52通过/5跳过、1604断言（`235312102`）；发现资产夹具使用`MINI3D_TEST_JUNCTION_FIXTURE`且目录形状不同，另建独占同盘夹具后才关闭两项junction缺口，未更改权限或放宽断言。Editor竞争夹具仍使用`MINI3D_FILE_API_REPARSE_FIXTURE`，两者不混用。

E8来自官方SDK2.3.1、Node24.13.0和正式adapter，不是mock PNG，也不是Desktop本聊天热加载工具验收。主代理已亲看最终Release四图：父平移逐帧向右，非对称旋翼180/360/540/720度方向交替；图片证明可见姿态，转数与精确值另由Core/sample/保存重开断言证明。

## 3. A01–A18需求交叉矩阵

原始验收要求见[需求第11节](native-animation-requirements-20261007.md#11-验收场景与验证矩阵)。下表“本机通过”仅指列出的实际层次，不抹去第5节限制。

| 需求 | 实施/可执行验证入口 | 运行证据与当前状态 |
| --- | --- | --- |
| A01线性平移 | [AnimationTests](../tests/AnimationTests.cpp)、[AnimationApiTests](../tests/AnimationApiTests.cpp)、正式MCP中立父Empty | E1/E3/E8；中点手算、直接取样、跳帧，本机通过 |
| A02常量与边界 | AnimationTests：精确键、端值保持、Constant左区间 | E1；本机通过 |
| A03连续两圈 | AnimationTests、[AnimationSessionTests](../tests/AnimationSessionTests.cpp)、[SceneAnimationDocumentTests](../tests/SceneAnimationDocumentTests.cpp)、正式MCP | E1/E5/E6/E8；0→720及720.000000001精确值/重开，四PNG本机通过，不仅比较首尾图片 |
| A04层级与负非均匀父链 | [EvaluatedPose](../src/core/EvaluatedPose.cpp)、[InstalledPoseTests](../tests/InstalledPoseTests.cpp)、[AnimationPoseGpuTests](../tests/AnimationPoseGpuTests.cpp)、[AnimationUiTests](../tests/AnimationUiTests.cpp) | E1/E2/E4/E8；完整world、设备/边框/focus与无轨后代，本机通过 |
| A05缩放与邻接 | AnimationTests、[SceneAnimationFormatTests](../tests/SceneAnimationFormatTests.cpp)、[SceneAnimationHistoryTests](../tests/SceneAnimationHistoryTests.cpp) | E1/E3/E4；raw与cast校验、Linear穿零拒绝、Constant跳号及删移新邻接，本机通过 |
| A06姿态草稿 | AnimationSessionTests、AnimationUiTests、[AnimationDraftGesture](../src/editor/operations/AnimationDraftGesture.cpp) | E2/E4/E5；通道mask、原点S/R、G/R/S数值、双Esc、单历史/取消，本机通过 |
| A07历史与保存点 | SceneAnimationHistoryTests、AnimationSessionTests、AnimationPerformanceTests | E1/E4/E5/E9；tick/seek不写内容，精确no_change保redo，定义写/Undo恢复，本机通过；主动编辑观察相机导航仍按旧合同标脏 |
| A08文件闭环 | SceneAnimationDocumentTests、[SceneDocument](../src/assets/SceneDocument.cpp)、正式MCP | E1/E6/E8；基础不烘焙、定义精确、重开Base，本机通过 |
| A09旧稿与坏文件 | [FileCompatibilityTests](../tests/FileCompatibilityTests.cpp)、[FileCompatibilityEditorTests](../tests/FileCompatibilityEditorTests.cpp)、[NativeScenePreflightTests](../tests/NativeScenePreflightTests.cpp)、SceneAnimationFormatTests | E1/E3/E4/E6；读1–4、旧1/2/3另存及字节保护、限长/坏引用/重复/乱序/未知整次拒绝，本机通过 |
| A10对象生命周期 | [SceneAnimationContentTests](../tests/SceneAnimationContentTests.cpp)、SceneAnimationHistoryTests、AnimationSessionTests | E1/E2/E4/E5；复制固定映射、删Undo原ID、名字/绑定、后代轨道换父保护，本机通过 |
| A11时钟与状态 | AnimationSessionTests、[SceneAnimationSession](../src/editor/SceneAnimationSession.cpp)、正式MCP | E2/E4/E5/E8；单调钟、子帧pause、loop、单帧、隐藏/故障停，本机通过，不保证30分钟耐久 |
| A12实体Camera/Light | InstalledPoseTests、AnimationPoseGpuTests、AnimationUiTests、AnimationSessionTests、真实性能设备夹具 | E2/E4/E5/E9；rigid方向、当前VP、GUI预览编辑与API Busy区分、编辑相机不被设备输出回写，本机通过 |
| A13快捷键与布局 | AnimationUiTests、[KeymapRouterTests](../tests/KeymapRouterTests.cpp)、[WorkbenchTests](../tests/WorkbenchTests.cpp) | E2/E4；实际DPR1/1.5/2定向及中文面板、文本优先，本机部分通过；真实中文IME、原生系统缩放、mixed-DPI跨屏未验 |
| A14API/MCP闭环 | AnimationApiTests、[AnimationApiService](../src/editor/api/AnimationApiService.cpp)、[MCP animation tests](../mcp/tests/animation.test.mjs)、正式SDK | E3/E4/E8；有界写/读/纯取样/跳帧/真实image，本机通过；Desktop本聊天直接呈图未验 |
| A15失败与过期 | AnimationApiTests、AnimationSessionTests、[AutomationBridgeTests](../tests/AutomationBridgeTests.cpp) | E3/E4/E5/E8；双CAS、deadline/no_change、busy、来源重入、整批原子、pause可用，本机通过 |
| A16指定帧竞态 | [AnimationObservationTests](../tests/AnimationObservationTests.cpp)、AnimationUiTests、[MCP stdio tests](../mcp/tests/stdio.test.mjs) | E2/E3/E4/E8；doc/view/eval/session/Context变化主动拒绝，二次paint/错身份拒图，本机通过 |
| A17旧建模与独立构建 | 完整Editor、完整asset、body-build快照、既有MCP合同/真实SDK | E4/E5/E6/E7/E8；本机覆盖通过；2 Editor环境项、3 asset符号链接项仍跳过，不称全环境通过 |
| A18性能与内存 | [AnimationPerformanceTests](../tests/AnimationPerformanceTests.cpp)、性能报告 | E9；三轮均达Core P95≤5ms、真实swap P95≤41.7ms；足额30秒×3，本机固定夹具通过，不推广到任意作品 |

## 4. X01–X15专家反例交叉矩阵

X编号来自用户Word第14节“审计追加的高价值反例”，不是外部报告历史审计自动继承。下面的入口和阶段证据均为本机新增或实际沿用验证。

| 反例 | 本机关闭依据 | 对应证据/限制 |
| --- | --- | --- |
| X01 double真源 | AnimationTests、SceneAnimationFormat/Document/HistoryTests；720.000000001、上限邻值、raw Scale、精确no_change与Undo | E1/E3/E6/E8；持久double不先经float |
| X02初始Euler | 独立RzRyRx手算矩阵、q/-q、±90°和重构；P/S不逆提角；sample无轨不造Euler | E1/E3/E8 |
| X03草稿枢轴 | mask是操作许可；冻结parent/world、selection/pivot；Scale-only不动Position、子孙继承；真实草稿数值/取消 | E2/E4/E5 |
| X04提交事件顺序 | prepared内容/历史→版本→绑定Pose→通知；copy/delete/undo/redo/rename通知真实身份断言；导航失败原子 | E1/E2/E4/E5；结构/容量测试不等于全局allocation failure注入 |
| X05同rev新文档 | 旧Pose先清、完整handle区别、同EntityId/rev也拒旧capture；R5真实非空Edit发布清旧mesh引用 | E2/E3/E4/E5 |
| X06历史求值失败 | 数值溢出及首次/中途/持续Provider异常，历史恢复成功但退出Base、清旧包装；守卫仍生效 | E2/E4/E5 |
| X07有限矩阵坏几何 | Core inverse/normal/rigid与Editor缓存AABB八角/设备VP分层；3.5e38实际角点失败原子 | E1/E2/E4 |
| X08工作预算 | 先核节点/轨键/40000访问；10000层非递归、changed-only/时间去重；1024unique batch真实服务与反馈 | E1/E3/E4/E9；性能批复用已有时间，不假装1024不同时间皆能过40000预算 |
| X09暂停CAS | tick不推进session、原play返回CAS可pause；自动停态推进；settle失败仍停钟并诊断 | E2/E3/E4/E5/E8 |
| X10同姿态异时间 | 0与360通道/时间不按终态判同值；同输入no_change；新eval与capture新paint | E2/E3/E4/E8 |
| X11二次paint | grab后实际stamp；完成回调同步快照；grab内更换请求不串旧结果 | E3/E4 |
| X12合法PNG错身份 | hash/尺寸有效但doc/rev/view/eval/mode/frame/session七类错身份，在MCP image层拒绝 | E3；冻结49/49与定向11/11，不用mock冒充真实GL |
| X13账本恢复 | 正式桥动画重放只执行一次，历史结果不当当前帧，未知写槽/取消语义保留 | E3/E4；断线/取消真实桥与SDK mock分层，不称每类故障均在最终真实实例注入 |
| X14 capture队列 | pending中seek/loop/play/preview/定义/doc变化、隐藏/关闭/取消和Context代际；一次完成、晚回调不串下一请求 | E3/E4；真实GL及既有观察回归，不代表任意驱动重建情形 |
| X15完整format4 | 真文件重开网格、Mirror/Subdivision、集合、cursor、Camera/Light、基础场景和精确动画；严格空轨/坏引用；旧版本带animation拒绝 | E1/E4/E6/E8；完整字段夹具与五对象两轨样例是不同夹具 |

## 5. 未验环境、保留风险与历史失败

- 最终Editor的2项环境跳过：无已有映射网络盘、无可访问FAT/exFAT卷。未创建用户盘符或更改磁盘格式。
- 最终asset的3项跳过：当前账户不能创建符号链接，分别为linked write root、final ancestor recheck、unprivileged reparse负向情形；独立junction两项通过不能替代symlink用例。
- 真实中文IME、原生系统缩放、mixed-DPI跨屏、新机器、跨用户ACL、30分钟耐久、正式许可/签名/发布和Desktop当前聊天直接MCP呈图未验。equal-DPR跨屏warning保留，不写成mixed-DPI通过。
- R3实际DPR1/1.5/2和PNG亲看已完成，但三档最新仅Draft定向各1/45；先前三档全套17/318与最终DPR1全套18/340有不同版本边界。
- 原间歇SIGSEGV缺其直接原AV栈；R4两生命周期P1及R5文档发布P1均有独立真实反例并已修复，不认定为原崩溃唯一根因，也不保证永不复现。
- 性能第一诊断组doc501→523、1287身份失配，来源未确定；第二组29999.9664ms/719槽不足，均真实失败并保留。最终新进程足额通过不改写失败组，详见性能报告。
- GPU计数仅成功editable mesh上传；Psapi峰值是进程启动累计、Undo保留redo，不是动画独占内存或“无泄漏”证明。性能PNG底部控制台页激活，时间轴交互证明另用R3。

这些是环境/验证边界及诊断限制，不以扩大产品功能、降低断言、提升权限或改用户配置来绕开。

## 6. 交付入口、审计与回滚

- 使用：[中文动画教程](native-animation-user-guide.md)，菜单“视图→动画时间轴”；默认收起，原底部控制台仍可访问。
- 样例：[native-animation-neutral.m3dscene](../assets/samples/native-animation-neutral.m3dscene)，5对象/2轨、24fps/1–49、无外部资源。先另存副本；Y末值720.000000001，sample帧25为360.0000000005。不是100对象性能夹具。
- 集成：[API/MCP说明](native-animation-api-mcp.md)、[格式4](native-animation-scene-format-v4.md)、[性能实测](native-animation-performance-20261007.md)、[开发方案](native-animation-development-plan-20261007.md)、[审计记录](native-animation-audit-20261007.md)。

每阶段由Astra专项与未负责实施的Sol独立审查形成具体问题/修补/运行复核；这指多代理技术审计，不是外部人类认证。请求配置Sol/max、Astra/high，工具没有独立provider最终生效回执，实际路由未独立验证；主会话不能由本文自动切换。

最终双线证据复核已关闭：Astra亲核A/X原始要求、测试日志、16路径（12已有文件、4个原本不存在的Release PDB）快照、两最终SDK身份及性能口径，未发现新可操作P1/P2，允许限定本机候选收口；独立Sol亲核三回归修补、教程/样例、各独立运行数字及两最终SDK，核八张PNG实际SHA256/字节数并亲看Debug13/49帧，未发现实质性不一致或未关闭P1/P2。性能原始分位数/足额槽由此前Astra独立重算，Sol没有重复该方法审查。主代理检查最终差异、亲看Release四图、运行文档/编码/历史前缀检查后完成整合验收；第5节限制仍有效。

回滚基线`2c350d9dd01880881297e76139b980e3585947f1`，各阶段尚未单独提交。仅撤R5时对progress列出的R5局部增量制作反向补丁，先隐藏运行`git apply --check --reverse <定向补丁>`，确认没有后续依赖再应用；新增样例/教程/性能测试按精确清单移动到独立已验证E盘归档，并撤对应测试CMake接入。保留R1–R4、共享文件其它变化、用户models/docs/modeling；progress只追加撤回说明，不整体reset/stash/restore，不降级覆盖格式4作品。
