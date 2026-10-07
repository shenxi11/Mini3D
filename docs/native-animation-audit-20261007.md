# 原生动画第一版本机专家团审计记录

任务2026-10-07，第一版本机候选收口2026-10-08。关联：[开发方案](native-animation-development-plan-20261007.md)。

本文件记录实际发生的审计，不继承外部报告的已关闭状态；“设计认可”不代表本机运行验收。

## 当前阶段

| 环节 | 施工状态 | Astra 专项审计 | 独立 Sol 审计 | 验证 |
| --- | --- | --- | --- | --- |
| R0 合同与计划 | 已关闭；后续迁移/API施工已获用户批准 | 三项设计修正已落码 | 四项设计问题关闭 | 原件/HEAD、文档UTF8/链接/日志前缀已核对 |
| R1 Core | 已完成纯Core及16项测试 | 专项审查通过；两项测试可信度关闭 | 独立代码审查通过 | Debug/Release各16用例997断言；CPU各1/1；主体单独构建 |
| R2 内容与格式 | 已关闭，Debug/Release通过 | 实施修补及两配置运行日志复核通过 | 实施修补及最终Release日志复核通过 | 两配置CPU1/1、history10/149、document7/563、相关回归82通过/1跳过；主体独立构建 |
| R3 姿态与GUI | 已关闭，最终Debug/Release与主体构建通过 | A01–A08静态及运行复核关闭 | S01–S09静态、真实UI/DPI及最终日志复核关闭 | session27/1001，Release含pose36/11370；UI18/340；相关回归83/6449；真实GPU2/272；三DPI定向各1/45 |
| R4 API/MCP观察 | 已关闭，最终两配置与真实MCP通过 | 三项P2与两生命周期P1静态/运行闭环，无新增专项阻塞 | 独立源码及最终日志复核，具体结论见末节 | 两配置同组263通过/3环境跳过、46203断言；MCP49/49；最终真实四帧PNG与重开 |
| R5 交叉验收 | 已关闭，限定本机候选 | 性能专项、三回归修补及最终A/X矩阵/证据复核通过 | 实现/指南/样例/矩阵无实质性不一致，最终证据复核通过 | Release Editor512通过/2跳过/56201；Debug受影响32/1472；文档8/589；Release asset54通过/3跳过/1632；两主体与最终真实MCP通过；环境限制保留 |

## 调用配置与权限

- `animation_core_evidence`：请求 `gpt-6-luna/max`，只读Core/测试入口定位，禁止写入和下级分派。
- `animation_design_expert`：请求 `gpt-6-astra/high`，只读R0/R1数学与关键设计审计，禁止实施及扩大范围。
- `animation_core_implementation`：请求 `gpt-6.1-sol/max`，独占新增Animation/EvaluatedPose模块及AnimationTests；不修改Scene/Serializer/API/CMake/文档/progress，构建由主代理串行执行。
- `animation_plan_review`：请求 `gpt-6.1-sol/max`，未参与实施，独立只读计划/授权/版本/历史/守卫审查；R0四项设计关闭、R1实现审查通过，本轮继续承担R2历史/生命周期预审。
- 调度返回了代理标识，但未提供独立provider生效回执；实际路由先标未验证。主会话实际模型也不能由这份文档切换或证明。
- 两条审计线分别负责关键设计风险和实现/验证合同，不用重复复述充当审计。结果到达后逐项追加具体问题、处置与关闭证据。

## 已核实基线

- 用户Word原件83981B、1113个文本段落，已只读提取；原件未修改。当前HEAD与报告固定提交一致。
- 初始基线场景写3读1/2/3、API0.1.0/wire1；R2封板已写4读1–4、当时API仍0.1.0；当前R4最终主体/API/MCP成套0.2.0/wire1，运行证据见末节。
- 已有未提交 progress、需求文档及模型交付保留。Git只读命令隐藏运行，无用户窗口关闭。
- E盘剩余约101.9GB，仓库/out/build/out/validation祖先为非链接目录；scratch为 `E:/Mini3D/out/validation/native-animation-20261007-66e24b`。
- Word连接设备离线，已更换为本机只读ZIP/XML提取；不是文档内容缺失或已实施功能。

## 问题与复核

### R0 Astra 专项设计审查

结论：附条件可进入不改协议的R1，不是代码验收。只读审查报告与计划、Transform/Scene/SceneNode，并复现double舍入；没有编译或运行Qt/GL/MCP。三项具体发现已经补入计划及实施任务包：

| 编号 | 级别/反例 | 处置与关闭门禁 | 状态 |
| --- | --- | --- | --- |
| R0-A01 | P1：局部合法或world有限不代表inverse/normal、法线方向或实际几何有效；七层0.001缩放可能让法线归一化平方溢出 | Core验证数值local/world/inverse/normal/rigid及检查方向可规范化；Editor另验缓存AABB八角、射线具体输入、设备VP/实际帧 | 设计分工已写；R1/R3实现测试待验 |
| R0-A02 | P2：`.001/.001,u=.0006` 的普通加权结果为`.0009999999999999998` | `std::lerp`、精确key/端值分支；相等±.001和子帧测试，raw低于下限仍拒绝 | 设计已改；R1测试待验 |
| R0-A03 | P2：`Scene::nodes()`递归复制、旧worldMatrix逐节点重走父链使预算检查过晚 | 最小数值span输入、预算先于完整Pose分配、迭代父先子后；10000深链/10001拒绝/40000访问预算 | 设计已改；R1测试待验 |

其它锁定测试：double域首次逆解重构/q与-q/正负90与近奇异；raw先验证再cast；Scale修改后的完整邻接；负非均匀父链的rigid方向；直接/倒序sample确定性；0/360/720原值不能按终态判同值。

完整实施项只有在代码、定向测试及原审计者针对复核后才能标关闭。格式/API批准、Sol独立设计审查、实现与实机门禁仍待完成。

### R0 独立 Sol 合同审查

未发现阻断纯R1的问题；与Astra数学线互补。三项P1和一项P2已经形成具体修订，未据此宣称现有代码存在动画缺陷：

| 编号 | 级别/发现 | 修订/验收 | 状态 |
| --- | --- | --- | --- |
| R0-S01 | P1：计划曾简写为只显式控制推进sessionRevision，漏自动终点/隐藏/故障停 | 任何真实会话状态变化都推进，tick/sample/no_change不推进；A11/X09 | 独立Sol最终设计关闭，R3代码待验 |
| R0-S02 | P1：Playing导航未明确旧入口已有recordApiCommit(true,false)/dirty语义与新源Pose时序 | 导航prepare→版本→Pose发布，不重置clock；entityCamera不回写；失败保留旧源；A07/A12/X04 | 独立Sol设计关闭，R3代码待验 |
| R0-S03 | P1：四态细项/实体相机例外/混合patch未充分显式；旧camera_preview统一mutation守卫不能照搬 | 控制/内容按动作字段准入，Playing禁seek/preview/loop；Camera允许play/pause；混合patch整次拒绝；入口Object/无gesture；A12/A15 | 独立Sol设计关闭，R3/R4代码待验 |
| R0-S04 | P2：格式3能力判断的3/4兼容检查需明确 | 计划R2清单补==3/!=3能力分支、legacy<4及默认版本；A09/X15 | 独立Sol设计关闭，R2代码验收待执行 |

现有Debug CPU二进制基线CTest `mini3d_unit_tests` 1/1通过，1.40秒，日志在本轮scratch `Unit-Debug-mini3d_tests-143803.log`。这是新模块构建前基线，不是R1代码验收。

### R1 专项实现与测试审计

Astra完整静态读四个新Core文件，认可raw/cast顺序、std::lerp/精确key、span先预算/迭代父序、实际float world/inverse/normal及normal列方向/rigid设备方向检查，稳定Euler公式、q/-q和double重构。未发现生产代码阻断。其后审计完整测试发现两项具体P2，主代理在实施代理结束后取得测试文件所有权定向修复：

| 编号 | 问题 | 修复与关闭证据 |
| --- | --- | --- |
| R1-A01 | 测试先将大于float::max的double窄化，不能以越界转换结果作标准C++证据 | 移除窄化断言，改为raw>max与validator拒绝；重编译后Debug/Release997断言通过，Astra已针对复核关闭 |
| R1-A02 | quaternion误差归约的std::max可吞NaN造成假通过 | 规范化前检查所有分量finite、length有限且>0，之后比较q/-q；重新编译/运行，Astra针对复核关闭 |

主代理另补齐正负0.001/长区间帧7及7.375、正反/直接求值顺序确定性、0/360/720原值不等；并只修改两处CMake接入。独立Sol完整审查四个Core、最新完整测试、两处CMake及必要基础类型，未发现本范围可操作P0/P1，亲读Debug构建/定向/CPU日志后通过。Astra最终专项通过也绑定亲读Debug日志；主代理随后完成同源Release和主体构建，不把没有新代码的不同构建配置称为第二轮实现。

### R1 实际运行证据

以下文件位于本轮scratch，原始日志保留，不上传临时目录：

| 动作 | 结果 | 原始日志 |
| --- | --- | --- |
| 最终Debug测试重编译 | exit0，无编译/链接错误 | Build-Debug-mini3d_tests-144655.log |
| Debug动画定向 | 16用例/997断言全通过 | Animation-Debug-mini3d_tests-144707.log |
| Debug完整CPU | CTest1/1，1.47s | Unit-Debug-mini3d_tests-144710.log |
| Release测试编译 | exit0 | Build-Release-mini3d_tests-144843.log |
| Release动画定向 | 16用例/997断言全通过 | Animation-Release-mini3d_tests-144918.log |
| Release完整CPU | CTest1/1，0.14s | Unit-Release-mini3d_tests-144921.log |
| 普通Debug主体 | mini3d_editor构建exit0，未编译测试target | Build-Debug-mini3d_editor-144934.log |

第一次受限Build的vcpkg `current_path(E:/vcpkg)`访问被拒绝（Build-Debug-mini3d_tests-144359.log及manifest日志），尚未代码编译；原因核实后仅在批准的构建权限下重跑。重配置确认GLM1.0.3等全部依赖已安装，没有包升级/下载。后续构建通过。不以失败尝试或旧测试二进制冒充成功。

R1关闭范围：Core数学/数值结果、独立测试与链接集成。未关闭范围：R2格式/历史/完整Scene输入、R3实际bounds/射线/Camera VP/绘制/时钟草稿、R4 API/MCP/真实图片身份、R5性能/资源/内存。两位审计者请求配置分别为Sol/max与Astra/high，实际provider仍未独立核验；不是人类专家签字。

### R2 实施前双线只读预审

本轮在迁移/API明确批准仍缺失的情况下，只核实具体接入点、先决设计与测试基础；未修改产品源码、格式、历史或API，未运行构建/测试，不将预审计作R2施工验收。主代理亲读Serializer/SceneDocument/Scene/SubtreeCommand和GUI/API关键入口后采纳以下修订，详见开发方案第9节。

| 编号 | 级别/实际证据 | 修订与关闭门禁 | 当前状态 |
| --- | --- | --- | --- |
| R2-A01 | P1前瞻：SceneDocument.cpp184–204编码后直接落盘；仅限制v4读取会生成无法重开的文件 | 所有v4 writer落盘前按实际UTF8同64MiB限额拒绝，原文件/路径/clean点不变；真实读写边界测试 | 设计修订写入；未实施/未验证 |
| R2-A02 | P1前瞻：SceneDocument.cpp83 readAll，Serializer.cpp299 DOM；缺v4分配前预检 | 分块识别真实顶层version（含尾部），预检与解析重复字段合同一致；施工前冻结native深度和错误归类，不能借用API节点配额 | 门禁已列，具体合同未冻结；未实施 |
| R2-A03 | P1前瞻：新轨道map/空轨规范化可能吞重复或坏引用 | 先结构/绑定/重复检查，后规范化合法空轨，保持键顺序、拒重复/乱序；负向文件全状态不变 | 设计修订写入；未实施/未验证 |
| R2-S01 | P1前瞻：GUI SubtreeCommand.cpp37首次redo才duplicate；API SceneViewModel.cpp1370已prepared | push前共同准备固定映射/曲线/预算/当前姿态；保留名称与GUI/API选择差异 | 接入清单写入；未实施 |
| R2-S02 | P1前瞻：Scene.cpp1164恢复时复制集合/emplace/insert，1255删除递归/复制children | 提前准备原ID节点/成员/曲线及恢复容量；Undo/Redo不重分身份，不只附曲线快照 | 接入清单写入；未实施 |
| R2-S03 | P1前瞻：SceneViewModel.cpp2626/1464两个换父入口；Scene.cpp1272新父push_back | 子树直接轨道共用守卫、同父no_change、无轨keepLocal；预留容量与兄弟顺序，覆盖树拖放/菜单 | 接入清单写入；未实施 |
| R2-S04 | P1前瞻：SceneViewModel.cpp338/2699/2707版本推进晚于命令内通知 | prepare/guard/安装→版本→身份→通知；历史当前帧故障仍恢复内容并清姿态退出预览 | 接口门禁已写；未实施 |
| R2-S05 | P2回归：API2048子树限额与GUI无该限额的既有差异 | 共用prepare保留调用方策略，不扩大全局静态限制；失败允许ID空洞，不发布节点/映射 | 回归门禁已写；未实施 |

格式专项另核对三处==3/!=3能力分支、两份sourceVersion默认、legacy<3与-v3建议名、现float helper和schema；保留集合/设备原能力门槛、QSaveFile/NewOnly、相对资源与旧顶层未知字段规则。原任务书明确旧1/2/3带animation整次拒绝，已核对提取正文，不把审计建议当用户授权。

历史线核对GUI/API复制、删除、换父、Undo/Redo，并列出现有PreparedSubtreeTests/ObjectApiTests的固定映射、跨文档来源、final guard/no_change/2048断言基础；这些静态测试不证明动画接入。曲线快照回放方式和旧历史命令候选姿态适配仍为施工前设计项，R2/R3必须提供完整场景span。

两路只读审计均已返回最终结论。请求配置继续为Astra/high与Sol/max，实际provider路由未独立验证。没有因授权未到而绕过门禁；R2状态保持未实施，代码验收尚未开始。

### R2批准后实施与双线代码复核

用户随后明确批准所有后续施工场景，本阶段已实施格式4与正式动画/历史内容，不再沿用上节的未批准状态。两路审查互补亲读实现和实际日志，发现与修补如下：

| 编号 | 来源/级别/实际问题 | 修补及关闭证据 |
| --- | --- | --- |
| R2-I01 | Astra P1：同QFile句柄不冻结内容，probe后的无界读取可被增长绕过 | 提取readPreflightedSceneBytes，旧稿最多旧长度+1、v4最多64MiB+1；大小/EOF或v4路由分类变化拒绝且不换输出；真实追加/缩短测试通过 |
| R2-I02 | 独立Sol P1：换父/对象生命周期候选未验证完整当前数值姿态 | 六个GUI/API入口guard/push前共用候选输入与预检；1e19父scale×1e21子position换父反例两入口拒绝，保留版本/历史 |
| R2-I03 | 独立Sol P2：clean/index/命令通知可能先于正式版本 | prepared命令登记通知，recordApiCommit推进后flush；document/lastOperation/apiState观察断言通过 |
| R2-I04 | Astra P1：structureChanged/modelReset时仍有已删除选区ID | 先QSignalBlocker静默安装有效SelectionModel，再结束树reset，最后通知selectedEntityChanged；MainWindow原isResetting配套守卫已核对；最新历史测试通过 |
| R2-I05 | Astra P2：去重、changed-only、40000预算仅Core测试不足以证明历史入口 | 新增10000节点×4唯一帧通过、第5唯一帧拒绝且保redo；100旧键只改一键通过；Debug/Release最新10用例149断言 |
| R2-I06 | 迁移测试/schema P2：v3夹具误删editableMeshes，词法整数与schema数学整数不等价 | 只在<3删除editableMeshes；schema明确24.0/1e0词法区别由parser执行；完整CPU和相关旧稿回归通过 |

主代理根据实际失败日志修正既有OBJ超64MiB测试：格式4 writer正确拒绝大夹具，改用旧3合法输入；独立审查在运行前指出顶层须为entities而非nodes，已定向更正并增加size断言，最终回归通过。没有放宽生产限额或忽略失败。

### R2最终运行证据

下列日志均位于`out/validation/native-animation-20261007-66e24b/r2-implementation`，不是旧二进制结果：

| 动作 | Debug | Release |
| --- | --- | --- |
| 动画CPU专项47/64466 | Test-Debug-mini3d_tests-183556426.log | Test-Release-mini3d_tests-185622098.log |
| 完整CPU CTest1/1 | CTest-Debug-mini3d_tests-185116901.log，26.94s | CTest-Release-mini3d_tests-185626605.log，3.63s |
| 文档7/563 | Test-Debug-mini3d_asset_tests-185351638.log | Test-Release-mini3d_asset_tests-185707680.log |
| 历史10/149 | Test-Debug-mini3d_editor_tests-185405077.log | Test-Release-mini3d_editor_tests-185931785.log |
| 相关回归82通过/1跳过/18438断言 | Test-Debug-mini3d_editor_tests-185504826.log | Test-Release-mini3d_editor_tests-185951313.log |
| 主体单独build exit0 | Build-Debug-mini3d_editor-185514365.log | Build-Release-mini3d_editor-190002655.log |

一个既有FileApi reparse竞争测试因缺`MINI3D_FILE_API_REPARSE_FIXTURE`跳过，不能记成路径安全完整验收。本阶段未修改权限、命名管道、线程或MCP宿主配置。普通构建仍主体独立；测试只由显式目标触发。

两路最新静态复核无未关闭P0/P1/P2，亲读Debug成绩后关闭具体发现；Release最终日志复核单独记录。当前frame1.0只是R2过渡，R3必须接真实会话时间、保留当前候选Pose、实际bounds/设备VP、版本后无分配发布以及历史故障退出，不能把R2静态数值通过当全消费者显示通过。无分配结构/容量依据不等于已执行全局allocation failure injection。请求Sol/max、Astra/high，实际provider路由未独立核验。

### R3已冻结子模块的静态审计与修补

本节仅记录实际源码审查。下列修补已由原审查者定向静态复核，运行层面的关闭仍须独立构建与真实CPU/Qt/GL日志；当前不记作R3验收通过。

| 编号 | 来源/级别/实际问题 | 修补与待验证项 |
| --- | --- | --- |
| R3-A01 | Astra P1：Pose bounds按准备时隐藏face A，绘制却按Renderer当前隐藏face B | PoseGeometry冻结完整ViewportVisibility，pose绘制与拾取统一消费；新增真实FBO像素/同数量不同face-ID掩码反向消费测试 |
| R3-A02 | Astra P2：临时EditableMeshRecord丢失evaluationRevision，Base与Preview切换同内容重复上传 | entry保留真实revision，GPU缓存比较完整mask；窄上传计数只在成功真实上传后推进；新增跨模式/跨帧复用及真实内容更换测试 |
| R3-S01 | 独立Sol P1：两次文档读取可让新document stamp搭配旧Pose，同步viewportChanged也可重入seek | private FrozenAnimationState冻结document与pose；最终复核来源/mode/包装，变化拒绝；grab后再查当前完整身份；新增两读变化与同步seek测试 |
| R3-S02 | 独立Sol P2：长按一次Esc首次撤手势后，重复KeyPress取消整份已有草稿 | 忽略重复Esc；新增保留其它通道草稿值的普通Esc＋autoRepeat Esc测试 |
| R3-S03 | 独立Sol P2：鼠标Enter不更新起点，面板回视口立即G/S可能大幅跳变 | Enter采集实际位置；新增旧MouseMove→新Enter→G→一像素移动测试 |
| R3-S04 | 独立Sol P2：Draft/guard拒绝正交切换，checkable QAction先变checked却不恢复 | 拒绝后发viewModeChanged与中文诊断；新增从OrthographicView QAction触发的真实投影一致性测试 |

两位审查者亲读定向修补和测试代码，静态确认这些发现关闭；没有运行构建或测试。R3 session事务、历史/通知、整体运行成绩和回归仍待后续独立审计。请求Sol/max与Astra/high，实际provider路由未独立核验。

### R3会话专项审计的第二轮发现

以下为Astra实际反例，不是理论推测；会话所有者正在修补。修补冻结、回归及原审计者复核之前不关闭R3。

| 编号 | 级别/实际反例 | 修补门禁与状态 |
| --- | --- | --- |
| R3-A03 | P1：prepare后才取来源，guard隐藏对象/切选择或provider取消Draft可发布旧候选甚至解引用已清空草稿 | prepare前统一冻结来源，回调后复核且不再访问失效对象；seek、草稿、tick、pause、视图、历史真实重入回归待最终运行 |
| R3-A04 | P1：makeEditable→preview→undo的sceneChanged观察者看到新几何但旧document/historyRevision | 提交中的旧几何完成通知排队至版本后flush；保留开始信号；真实回调版本断言待运行 |
| R3-A05 | P2：pause视图provider抛bad_alloc后模式为Paused，包装仍为Playing，getter拒绝 | 成功Paused包装或明确故障退出Base；异常后可消费状态及停钟回归待运行 |
| R3-A06 | P2：复制/删除Undo的冻结条目revision沿用旧值，Core实际record已有新revision，造成跨模式重复GPU上传 | 仅未发布独占geometry安装后无分配绑定实际revision；相等断言与真实窗口上传计数待运行 |
| R3-A07 | P1：历史准备捕获Provider bad_alloc后，许可路径再次调用同一持续失败Provider，Undo异常逸出而不恢复 | 任何Provider前先冻结非视图来源；故障回放不重调用失败Provider，保留真实guard与身份/交互复核，退出Base再恢复历史；新增持续异常Undo/Redo回归待运行 |
| R3-A08 | Astra P2：首次Provider成功而matches/prepareReplay深处首次失败，通用catch未标记视图失败，许可再次调用失败Provider | 仅标识视图读取异常并跨准备栈保留类别，不将普通候选分配失败等同Provider失败；准备中途回归待修补与运行 |
| R3-S09 | 独立Sol P1：GUI相机/灯光新转发API后，实体相机预览的camera_preview Busy拒绝原有GUI编辑；真实旧FOV画面回归失败；修补的cancel同步回调还可切入Playing/Draft后继续写入 | API保留原准入，共用私有事务主体供GUI调用，主体入口复核Playing/Draft/提交中；不新增Object-only限制；FOV/far真实画面中间版1/52已恢复，取消回调重入与最终回归待运行 |
| R3-S05 | P1：Viewport focus在旧姿态算bounds后，beforeCommit可seek；view update也可覆盖guard刚改的相机；极点实际视图变化可能仍有相同保存CameraState | 两入口在guard后比较准备前document/mode/pose、实际camera与viewportRevision；sameCamera包括真实view/projection而非只比较保存值；变化拒绝，不撤回guard自己的修改；新增seek/真实相机/极点重入回归待运行 |
| R3-S06 | P2：新增资源复用用例只等frameId增长，上传失败也可能使成功计数不变而假通过 | 每次真实paint必须resources.ready，并核同一contextGeneration；待最终运行 |
| R3-S07 | P1：草稿手势更新持有State引用跨setAnimationDraftChannel；provider取消Draft后State已清空，外层仍写valid/读HUD | 调用前冻结文本与手势generation，返回后先核仍是该手势；新增真实输入触发provider取消Draft用例待运行 |
| R3-S08 | P2：Esc/失焦撤手势时，rollback被guard拒绝仍清手势/忙态，取消意图丢失且可确认错误草稿 | rollback成功后才清手势；失败保留可重试手势与忙态、提示重试；start不能覆盖未能取消的旧手势；新增拒绝后Esc重试回归待运行 |

本轮新增只读Viewport上传计数与真实窗口生命周期/Draft面板DPI用例，作为上述资源与UI门禁，不增加外部权限或控制接口。独立Core三例38断言与真实FBO两例272断言已在Debug运行通过；尚不代表正在施工的会话修补通过。

### R3最终关闭复核

上述“待运行”是发现时的历史状态，最终均已闭环。Astra亲读最新Debug会话27/1001、Release会话与Pose36/11370、两配置UI18/340与相关回归83/6449，结合Core/GPU两配置证据关闭A03–A08；独立Sol亲读最终编译、会话、UI、相关回归及三档DPI日志，关闭S01–S09。两配置主体单独产出Mini3DStudio.exe，日志未编译任何test target；主代理亲读并核对实际输出。

关键新增反例均有对应运行：Provider首次、准备中途、守卫后持续异常与guard拒绝/异常/重入；复制及删除Undo的实际revision及成功真实上传复用；极点保存CameraState相同但真实view不同；provider取消Draft后的手势引用；拒绝rollback后的忙态保留；GUI取消旧变换时同步回调进入Playing/Draft后正式设备写仍拒绝。no_change、版本和完成通知顺序均以实际断言验证。

最终原始日志清单见开发方案第11节。最初旧设备失败`Test-Debug-mini3d_editor_tests-scale1-201141409.log`（67通过/1失败）保留；最终83/6449包含正式FOV/far数值与真实画面断言，不能把最初失败删除或替换成通过。先前23/626、26/901和17UI中间版也不冒充最终27/1001、18/340。

最新三DPI定向stdout日志为scale1-202732457、scale1.5-202736353、scale2-202739769，各1/45；分别实际DPR1/1.5/2，与PNG真实尺寸一致。主代理亲看三档，独立Sol亲看200%并核全部stdout；不会把定向1例说成三档最终全套18例。R3未执行R4宿主MCP图片或R5性能/内存门禁，原junction夹具缺口未记作已验。请求Sol/max、Astra/high，实际provider路由未独立核实。

### R4第一批冻结审查及真实Debug候选

第一批Astra与独立Sol分别亲读冻结的MCP/Observation/Viewport/MainWindow身份装配及10项真实GL候选，未发现可操作P0/P1/P2；未审当时仍施工的typed API，也未将未运行GL用例算通过。两者亲读MCP完整49/49、定向11/11日志，无跳过；这是正式adapter与官方SDK/mock管道证据。原始日志为`r4-mcp/Test-211926619.log`及`Test-211909523.log`。

第二批Astra实际发现两项P2，独立Sol补充一项同类P2；由主代理在源码所有权交回后定向修补。API用例候选正在补齐反例但尚未构建运行，当前不关闭问题。

| 编号 | 实际问题与修补 | 门禁 |
| --- | --- | --- |
| R4-A01 | prepare内部Provider重入seek/rename使来源过期，却被三控制误报UnsupportedTransform；失败时先复核冻结来源和原请求CAS，再映射真实数学失败 | nested控制结果保留、准确expectedSessionRevision/expectedDocumentRevision及guard次数反例；原审查者复核 |
| R4-A02 | query Busy provider回调新设模型物理Busy后返回空列表，原二次检查仅doc/CAS；改为回调后重核模型物理Busy，不重复外部Provider | 四查询拒绝新Busy、sample不读基础临时预览、provider只一次；原审查者复核 |
| R4-S01 | 五定义写共用预检在Provider中途新增Busy/换文档时误报数学失败；仅Explicit路径传入同步原MutationRequest，在来源失效时恢复准确Busy/document/CAS错误，实际guard拒绝保持原错误 | setSettings代表的中途Busy/rename/同revision新文档反例，外层不发布；request不进入历史command；原审查者复核 |

Debug主体两次显式独立构建exit0：`r4-implementation/Build-Debug-mini3d_editor-214615333.log`和修补后的`Build-Debug-mini3d_editor-215202523.log`；均无test target。初次真实MCP截图失败保留`Real-Debug-214724310.log`和`r4-real-1791380835835/failure.json`。诊断对比旧成功启动器发现本轮错误地以windowsHide=true启动GUI，初始GL正常却不产生后续可捕获paint；只让自建GUI进程正常显示，Node/PowerShell runner仍隐藏、不关闭用户窗口。

修正验收启动器后，`Real-Debug-215030721.log`、`r4-real-1791381026502/evidence.json`通过：实际61目录/权限与预算、写键/raw连续double、无轨子孙继承、纯sample零副作用、稳定分页、重复批原子拒绝；官方SDK获取真实13/25/37/49帧image content，旧eval拒绝，tick后原sessionCAS pause及内容版本不变；中文格式4保存重开保留720.000000001。主代理亲看全部四张PNG，父平移和非对称旋翼朝向与帧一致。此运行发生在两项P2修补前的首个Debug候选，不冒充最终Release或反例成绩；后续须在最终状态重验。每轮仅结束其自身PID，不改宿主MCP配置或用户作品。

### R4最终实现、失败诊断与双线封板

A01/A02/S01已由原审查者静态复核并由最终两配置反例关闭，23项API测试独立2570断言通过（`r4-implementation/Test-Debug-mini3d_editor_tests-220720336.log`）。本阶段其它实际问题：

| 编号 | 发现与处置 | 修改前/后证据 |
| --- | --- | --- |
| R4-I01 | 截图夹具把完成结果与等待返回后的新paint比较；改保存完成回调同步frame快照，连接scope退出断开 | 原5!=6失败保留；最终12组观察及组合通过 |
| R4-I02 P1 | Qt基类析构delete context才发aboutToBeDestroyed，晚调用已结束生命周期的Viewport成员；派生析构起始断开当前context此信号到this | `Test-Debug-mini3d_editor_tests-222631028.log`真实lateCleanup失败；两配置最终无探针组合通过 |
| R4-I03 P1 | 全局AnimationDraftGesture过滤器在ViewModel死亡后仍读model；MainWindow析构起始同步销毁该唯一控制器 | `Test-Debug-mini3d_editor_tests-224156266.log`真实filterStillAlive失败；两配置最终组合通过 |
| R4-I04 | 旧batch夹具guard递归undo导致栈溢出；改一次性注入并验证内层授权；真实batch来源错误沿原request返回准确CAS字段 | `223001892`栈溢出日志保留；`223357861`1/46及最终组合通过 |
| R4-I05 | Release旧ledger测试auto保存临时非const QJsonObject下标QJsonValueRef，代理悬空；改const QJsonValue持有值，不改产品账本 | `225232429`1失败；Release最小`225418076`1/38、最终组合通过；Debug新编`225600790`后bridge`225652790`20通过/2跳过、548断言 |

最终Debug组合`224604325`与Release组合`225515852`均266例=263通过/3环境跳过、46203断言全过；Release新编日志`225356544`。Debug组合发生在I05测试一行修正之前，修正后只重新构建并补受影响bridge，不把旧组合冒充新编全组。3跳过为junction fixture、无已有映射网络盘、无可访问FAT/exFAT；R5补第一项。MCP源码冻结后49/49与11/11成绩有效，未再次无收益运行。

主体最终独立构建`225702060`/`225736744`均产出Debug/Release Mini3DStudio.exe、无test target。正式官方SDK真实最终主体运行`Real-Debug-225814480.log`及`Real-Release-225748757.log`，证据JSON为`r4-real-1791385090186`和`r4-real-1791385065689`。61目录、双圈double、无轨子孙、纯sample、分页、原子拒绝、13/25/37/49实际image、旧eval拒绝、tick后原sessionCAS pause、中文format4重开均通过。主代理亲看Release四张PNG；Astra亲读两组日志/元数据，确认双配置同帧hash一致和eval/session按seek变化，图形本批DPR1。独立Sol另亲读最终运行及源码；模型请求配置分别Astra/high、Sol/max，provider路由无独立回执。

原间歇SIGSEGV日志`221052302`、`221616655`、`223413744`保留；cdb三次未复现不能当根因证据。两项确定P1有独立失败反例并已关闭，但没有原AV栈，不能声称它们是原SIGSEGV确定/唯一根因或永不复现。临时VEH源码引用已撤、SceneEditorTests无diff，没有全局WER/调试器改动。R4按合同范围关闭，不等于R5性能/内存、原建模交叉验收或Desktop当前聊天直接MCP热加载通过。

### R5实际回归、性能专项与交付复核

R5实施代理`animation_r5_performance_implementation`请求Sol/max，只独占性能测试；root负责串行构建、实际测量、三回归修补与交付整合。原专项审查者`animation_design_expert`继续Astra/high，未负责实施的`animation_r4_independent_review`继续Sol/max独立只读审查。真实路由未独立核验；下表指实际发现与证据，不是角色复述。

| 编号 | 实际发现/风险 | 修补与关闭证据 |
| --- | --- | --- |
| R5-I01 P1 | F9取消/文档重置旧单例稳定AV；cdb栈为publishDocument→visibility→componentSelectionChanged→cursorSelectionCenter→旧mesh中心。公共setEditMode(false)被提交守卫拒绝，导致旧编辑引用跨Scene替换 | publishDocument内部先清旧Edit/entity/mesh/组件选区/隐藏元素，新Scene/API/history/visibility安装后只发一次退出；新增真实非空选区及新旧源通知身份断言。不放宽公共guard，不以中心空指针补丁掩盖状态。Astra/Sol静态复核及Release E4、Debug E5运行通过 |
| R5-I02 P2 | 树右键复制数据正确，但菜单关闭恢复焦点在reset完成后改选首个父对象；TreeStack为焦点选择栈，不是原SIGSEGV栈 | 菜单close连接structureAboutToChange，SceneTreeModel先置resetting守卫；保留命中child/Undo恢复parent及copy!=parent断言，诊断探针撤回。双线静态及E4/E5通过 |
| R5-I03 | 新版本探测错误丢旧中文“场景解析失败”外层，旧测试要求实际未执行的nlohmann parse_error | SceneDocument保外层path/真实原因，测试准确要求“JSON容器未结束”；保分类和预算，不放宽断言。双线静态及E4/E5/E6通过 |
| R5-A01 P2 | 原性能探针仅走ViewModel，不能声称完整API操作耗时 | 改走真实MainWindow EditorApiService，记录同步服务和匹配最终身份的新swap；排除运输/编解码口径明确，最终E9实测闭环 |
| R5-A02 P2 | 预热正确性虽记录但未纳总acceptance | 预热版本/源/相机/资源不变式纳总成功；最终E9全部通过 |
| R5-I04 | 第一性能组P95达标但doc501→523、1287身份失配，整体失败，来源未确定 | 只对自建窗口安装8输入族隔离，记录实际相机/输入/版本，不归咎用户、不断定tick写内容；失败组保留，最终新进程E9通过 |
| R5-I05 | 第二性能组首轮29999.9664ms仅719完整槽；其余正确性和P95通过仍整体失败 | 用同一单调时钟核剩余ns、提前唤醒补足，未放宽30秒断言；最终三轮30001.4269/30000.7705/30002.1397ms、各720槽通过 |
| R5-S01 文档小项 | 两圈教程未明确非零初始Y，可能把“末值720”误用为“增加720” | 教程明确末值=起始Y+720；独立Sol定向关闭；样例仍明确起始0/末720.000000001 |

最终性能运行`r5-implementation/Test-Release-mini3d_editor_tests-234134885.log`为1/14，三轮raw独立重算与足额槽/反馈由Astra复核通过，专项允许封板；Sol核数字与限定口径。详细数据、两失败组及未测条件见[性能报告](native-animation-performance-20261007.md)。

最终Release默认Editor514例=512通过/2环境跳过、56201断言；Debug受影响32/1472；Debug真实文档8/589（新增样例含精确double/另存/重开/源字节保护）；Release完整asset54通过/3symlink权限跳过、1632断言，两项junction已另建独占正确夹具验证。主体两配置exit0无tests，16测试exe/PDB路径存在性/已有hash/time不变；正式MCP两最终主体四PNG与中文格式4重开passed。原始索引及A/X矩阵见[本机验收](native-animation-acceptance-20261007.md)，root亲看Release最终四图，不把R4旧图冒充R5最终。

最终交付矩阵双线已关闭：Astra亲读原始A/X要求、验收/性能报告与最终日志，核主体12已有文件+4缺省Release PDB的16路径快照一致、两真实MCP帧/重开身份，未发现新可操作P1/P2，允许限定本机候选收口。独立Sol亲读最终记录，核三修补/教程样例/矩阵及两最终SDK一致；八张PNG的实际SHA256/字节数匹配，亲看Debug13/49帧，未发现未解决P1/P2或实质性不一致。性能raw重算为此前Astra专项，Sol本次只核数字/限定，不重复充当方法审查。root检查最终差异、文档/UTF8/历史前缀，完成整合验收。

仍保留2 Editor环境skip、3 asset符号链接权限skip、真实IME/原生系统缩放/mixed-DPI/新机器/跨用户/耐久/Desktop本聊天等未验项。原间歇SIGSEGV缺其原AV栈，上述I01的独立确定栈不被写成原崩溃根因。没有发布/推送/改MCP配置/关闭用户窗口，旧候选ZIP未更新。请求Sol/max、Astra/high，provider实际路由未独立验证；此结论不是人类专家认证或全环境发布。
