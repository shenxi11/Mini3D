# Mini3D 动画与渲染联合开发执行方案

版本 J1.1 · 2026年10月9日

本方案把 J1.0 的三级责任体系转换为当前项目能够执行的任务、资源和验收规则。默认由总负责人统筹，动画与渲染两位技术 Leader 各自承担实施，独立调研、审查与专家按任务轮换进入。两条线只在接口已确定、文件不重叠时并行。

本版交付的是执行方案，不代表产品开发已经开始、模型路由已经验证或新增权限已经批准。下一次获准实施后，从 EXEC-000 开始；全部新任务的产品状态均为 Planned。现有动画首版的通过记录仍是历史证据，不能直接计入本方案的新能力。

## 1 文档组成与适用顺序

本执行主册的 Markdown 是可维护正文，Word 是同内容的阅读版；任务 JSON 是从正文任务卡生成的索引，不构成第二份需求真源。完整技术合同保存在同目录的 `animation-rendering-j1.0-source.docx`，与用户原文件逐字节一致，不删公式、图示、来源或能力表。

原稿 SHA256 为 `BBB4E6AD7186C2B1E1F5B9AE1EB851B78CA4052DE39C0AF008571D208364D73A`。主册与附件应一起交接；附件缺失或校验不符时，相关算法和完整范围任务不得靠摘要补写。

适用顺序是平台与用户当前授权、实际加载的项目规范、本版执行裁决、J1.0 未被裁决覆盖的技术合同。方案不是新的 AGENTS 文件，不能自行覆盖权限、模型限制或执行规范。发现已加载规则、磁盘规则和当前用户要求冲突，EXEC-000 只暂停受影响分派并记录具体差异，不自动回退旧模型。

| J1.0 位置 | J1.1 的处理 | 保留部分 |
| --- | --- | --- |
| J02 角色及运行结构 | 使用本册第3节模型和轮换调度，取消两位纯管理 Leader 常驻的前提 | 三级责任、独立审查、禁止四级分派 |
| J07 J09 写入归属 | 使用第4节文件组和逐票写入租约 | 单文件单写入者、短分支、小步集成 |
| J08 J12 派工顺序 | 使用第6至8节任务依赖和就绪检查 | J0至J7总体阶段、第一条真实作品链 |
| J11 代理预算 | 先遵守项目两个子代理上限，再核对平台容量 | 产品资源准入、性能口径、不得用代理数除工期 |
| J14 既往审查 | 仅作为原方案的设计历史；本版及产品分别验证 | 所有历史发现和出处 |
| A01至A33 R01至R16 附录RA至RC | 不缩减需求，不在本次改写算法合同 | 动画149族、EXT、渲染136族、所有硬门与完整技术细则 |

动画149族及其必选子项继续完整建设。渲染W20、R38、F48、S30四轨分别验收，不相互补分；保留各轨至少80%、核心全过及8条工作流的门槛。REN-F48仍是联合必需。EXT按原案完成叶子清单后冻结，不把GP、物理、VSE、blend各算一分，也不把Bridge算Native。计划覆盖不等于已实现。

## 2 开工事实与范围保护

编制时主仓库是 `E:/Mini3D`，HEAD为 `5f5c82ed7a9f361e263d4abe895bbf99b061e3e7`。已有未提交修改是AGENTS.md、docs/agent-collaboration.md和progress.md的代理约定更新。本轮新增方案文件不属于产品实现；后续开工必须重新记录HEAD、完整差异与每个待提交文件，不能把本次快照当永久状态。

当前动画为局部TRS、整组XYZ、Constant/Linear，已有基础场景与预览姿态分离、统一历史、内容和会话CAS。材质主要是baseColor及基础纹理，渲染仍有读取基础surface的路径。文件格式为4，API/MCP为0.2.0。完整求值状态、通用属性、持久可编辑材质、PBR及离线作业不能登记为已有能力。

保持C++20、Qt Widgets 6.8.3、MSVC2022、Windows x64、OpenGL4.1基础兼容。继续保留取消时精确恢复原始TRS快照、原始连续角、固定复制ID、原子候选提交、失败不安装部分结果、真实观察身份校验、主体和测试分开构建。不要为了方便新功能而放松这些合同。

本版不授权提交、推送、发布、修改全局Codex/MCP配置、运行未知脚本或删除用户作品。不改变用户当前窗口及其他任务。方案实施中的授权必须绑定具体票据及行为，不能引用原文中的“已批准”作为新的授权。

## 3 三级责任与实际调度

### 3.1 固定角色

| 角色 | 模型 | 推理及模式 | 工作边界 |
| --- | --- | --- | --- |
| L1总负责人 | gpt-6.1-sol | max | 确定依赖、文件租约、契约裁决、集成及最终签收 |
| L2动画技术Leader | gpt-6.1-sol | max | 动画域负责人，可在自己独占范围直接实施 |
| L2渲染技术Leader | gpt-6.1-sol | max | 渲染域负责人，可在自己独占范围直接实施 |
| L3调研 | gpt-5.6-luna | max | 只读证据定位；构建和写缓存不属于只读调研 |
| L3实施及独立审查 | gpt-6.1-sol | max | 分人承担实现和审查；按任务轮换，不常驻全套 |
| L3专家 | gpt-6.1-sol | max及fast | 只处理明确风险，不以普通审查替代必须的专家检查 |

本表延续2026年10月9日用户指定的更新，不沿用J1.0中的旧Luna和Astra路由。fast是独立模式，不拼入模型名称。启动参数被接受只证明工具接受请求，不等于provider最终路由已核验；记录请求值、工具回执和未核验项。明确不支持或规则要求的配置无法满足时，暂停该分派，不能擅自降级。

当前工具可以显式指定上述模型及max，但未提供独立fast参数。实际专家fast尚未核验；需要专家的任务保持Blocked，原因登记为专家配置门未满足，直到客户端可验证地支持或用户明确接受替代方案。其他不依赖该专家结论的已授权任务可以继续。主会话也不能被提示词自动切换。

### 3.2 默认运行方式

容量计算同时遵守两条限制：本项目默认最多两个子代理；编制时平台为含主代理在内共四个活跃名额。默认只使用主代理加最多两个子代理，不占用“第三个只读研究”例外为施工扩容。平台上限是资源上限，不是项目授权。

| 工作状态 | 同时工作的角色 | 实施方式 |
| --- | --- | --- |
| 共享接口期 | L1及一位实现者，必要时另一位只读提供消费要求 | 公共文件只有一个owner；没有两套接口实现 |
| 双线实施期 | L1及两位L2技术Leader | 两位Leader各编码一个无文件交叉的任务包 |
| 独立审查期 | L1及一位L2及一位L3审查者 | 另一条线先完成交接并结束当前运行；不得仅假定等待释放名额 |
| 专项研究或专家期 | L1及受影响L2及一位L3 | 另一条线只在配额实际允许时恢复；不超过项目上限 |
| 集成验证期 | L1及最多一位修复owner和一位独立审查者 | 合并共享文件、同一集成快照验证；GPU测试串行 |

三级首先是责任和审查关系。实际L1→L2→L3嵌套仅在L1给出具体派工授权、平台支持且全局配额足够时使用，L3不得再下分。若工具不支持嵌套，由L1直接创建并路由L3，记录 `hierarchyMode=logical`，不得把逻辑三级称为已运行的嵌套三级。此降为直接调度不改变模型、职责和审查独立性。

L2不能把自己的两个子任务上限再乘到全局额度上。L1是唯一调度者：L2提交具体请求，由L1分配名额和范围；当前嵌套工具可用时，L1才明确允许该L2创建指定L3。没有可执行工作时，交接后结束，不创建长期等待的管理代理。

若将来要使L1、两个纯管理Leader、两个L3实施者同时运行，至少需要五个真实名额，同时取得项目并发规则的明确修订；再加独立审查则需要六个。该模式不是本版开工前提。

### 3.3 调度与交接算法

1. L1只从依赖已完成且授权齐全的任务中选择；每条线最多一个Writing任务。
2. 发包前给出base HEAD、现有patch hash、契约版本、实际文件列表、禁止目录和验证命令；目录组不是无限写权限。
3. 同一文件或共享构建目录被占用时，后票排队；不靠通知消息假定对方已停止。
4. 任务交付时保存变更清单、补丁或提交、结果和未验项；确认后台命令结束，再移交写入租约。
5. 独立审查必须由未写该补丁的代理执行。新建名称但继承作者完整工作并自证通过，不构成独立性；审查者收到规范、差异和证据，独立阅读必要源码。
6. 审查发现原文保留。修复回到文件owner；复核只覆盖修补和受影响路径。若换人修复，先完成租约移交。
7. L1整合后验证；分支通过不等于集成通过。先签当前阶段，再按用户授权继续下一阶段。

派工使用小上下文和明确model、reasoning_effort；需要覆盖模型时不使用会忽略覆盖的完整历史继承模式。当前支持的请求示例是 `model=gpt-6.1-sol`、`reasoning_effort=max`、`fork_turns=none`。角色提示词不能代替这些参数，也不能改变沙箱。

## 4 文件所有权与隔离

下面是核对当前目录后划定的责任组。标为“拟新增”的路径是实施候选，不宣称已经存在；进入票据前由owner在契约设计中定名，再把精确路径写入任务记录。非必要不迁移整个目录。

| 文件组 | 当前入口或拟新增范围 | 唯一写入责任 |
| --- | --- | --- |
| G0 记录与集成 | progress.md、执行台账、CMakeLists.txt、各模块CMakeLists.txt、CMakePresets.json、依赖和锁文件 | L1或其单次指定的集成人 |
| G1 共享核心 | src/core/Scene.h及.cpp、SceneNode.h、SceneSerializer.h及.cpp、src/assets/SceneDocument.h及.cpp | 当前JOINT任务的单一owner |
| G2 动画纯核心 | src/core/Animation.h及.cpp、EvaluatedPose.h及.cpp；拟新增属性地址、曲线和求值结果模块 | 动画Leader，公共类型先经双方审阅 |
| G3 动画编辑 | src/editor/SceneAnimationEditing.cpp、SceneAnimationSession.cpp、SceneAnimationApi.cpp、AnimationTimeline.h及.cpp、operations/AnimationDraftGesture.* | 动画Leader |
| G4 渲染资产与实现 | src/assets/MaterialAsset.h、GltfImporter.cpp；src/renderer_gl/Renderer.*、Material.h、GpuTexture.*、GpuMesh.*及相关shader | 渲染Leader |
| G5 跨域显示入口 | src/renderer_gl/InstalledPose.*和ViewportWidget.*；src/editor/SceneViewModel.*、SelectionModel.*、AnimationReplay.h、MainWindow.*、AppearanceInspector.*、observation/* | 相关JOINT票单一owner；双方仅提消费需求 |
| G6 API及安全边界 | src/editor/api/*、automation/*、api/schema/*、mcp/src/*及mcp/package.json | 单一接口集成人；安全票单独授权 |
| G7 测试与文档 | tests中本票精确文件、领域docs文件、隔离样例 | 按票分文件；公共测试夹具和注册入口归G0 |
| G8 后续新增子系统 | 曲线编辑器、rig、media、simulation、render IR、backend、jobs等拟新增文件 | 由阶段任务先定接口和精确路径，再分配；不预建空模块 |

G1、G5、G6默认串行；不能因为分别属于动画和渲染就同时改同一文件。G2/G3与G4可在已冻结接口下并行。文件组中的通配符只是候选清单，派工时必须展开为明确文件，不能授予整目录无限改动。

默认在现有工作区采用不重叠文件的双线方式，共享接线和构建验证串行；不为了创建工作树而擅自提交未提交代码。已有适用管理工作树时优先复用。若有用户授权的干净代码提交基线，可使用任务级独立工作树；必须明确复制哪些未提交内容，否则新工作树不会包含它们。没有提交授权时，用HEAD加受控补丁和文件hash固定候选身份，不执行commit或push。

所有AI可控临时产物按技能验证E盘优先、D盘次之。每个任务使用独占build、TEMP/TMP/TMPDIR、日志、样例输出、管道名和后端工作目录。不共享运行中的CMake构建树；初始只允许一个重构建或GPU验收过程。纯CPU测试仅在输入和输出隔离且资源预算允许时并行。

progress.md只由L1在任务闭环后追加；子代理提交建议日志片段，不直接并发追加。正式docs以领域文件分工，共享契约由单一owner写入。临时证据不进入正式能力表或Git提交清单。

## 5 共享合同的最小冻结内容

只冻结下一条真实功能链需要的字段、类型与规则，不在J0一次写完全部未来系统。后续扩展通过契约差异和兼容性说明进入，不允许私自改同名字段语义。

| 合同 | J1前必须确定 | 主责及复核 |
| --- | --- | --- |
| J-CON-01 身份与属性 | Entity/Asset/Material/Socket稳定ID，typed地址、单位、默认值、合法域；最小位置、roughness及灯强度通路 | 动画提出地址，渲染提出材质及灯定义；共享owner落笔 |
| J-CON-02 时间与求值 | 子帧和有理fps表示、返回状态、source identity、失败不发布；有状态任务接口只声明重放边界 | 动画主责，渲染校核采样需求 |
| J-CON-03 几何与IR | 完整affine、inverse/normal、几何stage、未变形Generated/Object坐标、不可变资产持有者 | 渲染主责，动画校核生产者 |
| J-CON-04 编辑事务 | 唯一历史、精确no_change、内容和会话CAS、取消精确恢复、Draft准入 | L1指定共享owner |
| J-CON-05 格式迁移 | V4兼容、拟V5与API0.3.0的单一清单、精确值、大小和旧稿保护 | serializer/API集成人，双Leader复核 |
| J-CON-06 作业 | PreparedEdit.Ready与RenderJob.Ready分开；Preparing锁、完整Bundle、epoch、发布证据和当前权限 | 渲染主责；持久作业实现留到对应票 |
| J-CON-07 图像及色彩 | ImageFrame/PlaneSchema、scene-linear HDR、alpha、数据通道、唯一显示和编码执行者 | 渲染主责，动画及媒体消费者签收 |
| J-CON-08 资源及诊断 | 基础动画、联合预览、离线三个profile；CPU/GPU/磁盘计量，实际释放和错误身份 | L1整合，不把V1预算外推到全部后端 |

最小接口必须提供成功、无变化、无效值、来源过期、取消和不支持能力的明确结果。Unsupported仅证明拒绝正确，不给功能记分。通过新接口后，旧消费者的行为保持可验证；迁移消费者时不能仅把新数据结构定义出来就声称接入。

渲染不得再次解释曲线、约束或模拟；动画不得保存第二份shader定义。显示、拾取、bounds、观察和输出都绑定同一求值来源，并明确各自几何stage。模拟、媒体、脚本的求值使用独立会话或已验证缓存，不能共享live可变状态。

格式4到新格式及API0.2.0到新API属于一次受控演进。格式5/API0.3.0仅是当前候选版本；开工若已有正式版本，占用情况优先于文档，不重用已发行号码改变语义。wire1保持原合同，新增能力按system.describe真实声明。新功能不得绕过已有本机同用户管道与应用线程串行命令入口。

## 6 首批任务与执行顺序

全部任务卡的状态初始为Planned；任务列表存在不代表依赖或授权已通过。EXEC-000为获准启动后的入口。每张卡通过就绪检查才进入Ready：依赖完成、所需合同版本确定、文件租约无冲突、运行模型可用、风险授权有记录、验证入口可执行。

默认首轮排程如下。批次排列表示默认前置顺序，并非自动执行授权；独立审查进入时先按第3节释放一个名额。

| 批次 | 动画线及L1 | 渲染线 | 汇合条件 |
| --- | --- | --- | --- |
| 基线 | EXEC-000后，L1登记EXEC-001 | 等基线明确 | 不在旧工作区身份上派工 |
| 合同 | EXEC-002 | EXEC-003 | 两份草案共同交EXEC-004 |
| 原型 | EXEC-004及范围记录补齐 | EXEC-005后串行EXEC-006 | EXEC-001至006全部完成才进EXEC-007 |
| 公共实现 | EXEC-008唯一owner | 提供消费端要求 | 公共类型验收后释放双线 |
| 首个切片 | EXEC-009 | EXEC-010 | 交接后EXEC-011，再EXEC-012 |
| 创作与输出 | EXEC-013 | EXEC-014 | 文件独立可并行，GPU验收串行 |
| J1签收 | L1完成EXEC-015 | 配合缺陷修复 | 第7节真实作品链通过 |

### EXEC-000 核对基线和执行边界

- Owner: L1
- Phase: J0
- Depends: none
- Scope: G0
- Gate: 批准实施本方案；本卡仅做只读核对及执行记录
- Exit: 固定HEAD和未提交差异；核对实际加载规范、模型参数和配额；记录专家fast为已核验或阻塞；列出权限差异；确认两个子代理以内的调度可执行。

不得为了“工作区干净”重置或提交当前内容。先保存精确文件清单和hash；本卡输出写入执行台账，不修改产品协议。模型或权限有冲突时，记录具体阻塞及仍能推进的独立部分。

### EXEC-001 完整范围与证据清单

- Owner: L1
- Phase: J0
- Depends: EXEC-000
- Scope: G0,G7
- Gate: 原稿hash核对通过
- Exit: 从A20登记149族，从附录RA登记136族及核心标记；从A23至A33登记EXT和新增包；保留全部必选子项、Native/Bridge、来源、owner和验收ID；未拆叶子的记录为PendingScope。

同时映射20个渲染场景、8条渲染工作流、动画W1至W10及J-IT-01至12。原表的“后续”不移出分母，联合必需项优先。先抽查来源与计数，再按下一阶段逐族细化；计数检查不能替代算法和叶子完整性审查。

### EXEC-002 身份和时间合同

- Owner: ANIM
- Phase: J0
- Depends: EXEC-000
- Scope: G7
- Gate: 模型及调研运行要求满足；只写合同文档
- Exit: 形成J-CON-01/02/04最小草案，给出位置、roughness、灯强度的类型和地址实例；1、12.5、24帧输入输出例子、重复ID/过期身份反例及旧TRS映射。

与EXEC-003可并行设计，不写共享头文件。渲染侧提出实际参数单位与消费者要求，不让动画侧从旧灯0至10范围推导物理灯功率。

### EXEC-003 材质与几何合同

- Owner: REN
- Phase: J0
- Depends: EXEC-000
- Scope: G7
- Gate: 模型及调研运行要求满足；只写合同文档
- Exit: 形成J-CON-01/03/07最小草案；定义持久材质/槽位、effective属性、IR、完整affine和图像元数据；列出J1最小PBR/OCIO/输出消费字段及后端不确定项。

地址与单位草案需要与EXEC-002会签后才成为共享合同。不要在两份草案中分别发明同一MaterialID或Socket类型。

### EXEC-004 统一迁移与作业合同

- Owner: JOINT
- Phase: J0
- Depends: EXEC-002,EXEC-003
- Scope: G7
- Gate: L1指定唯一文档owner；暂不修改产品协议
- Exit: 完成J-CON-05/06/08、版本清单、资源profile、Bundle和两种Ready、失败及取消状态表；列出公共文件owner及新增安全边界审批包。

把前台短命令、可取消准备和长期离线作业分开。记录V4实际64MiB限制与未来256MiB候选的关系；容量变更必须验证，不能混淆现有产品事实。完整旧格式最大场景另存仍是最终迁移硬门。

### EXEC-005 实时后端验证原型

- Owner: REN
- Phase: J0
- Depends: EXEC-003
- Scope: G7,G8
- Gate: 隔离原型环境与依赖范围获得授权；同GPU独占
- Exit: 按R13的E-RT比较原生GL与Filament；验证Qt生命周期、覆盖交互、TAA及DOF同开后的HDR/alpha/一次OCIO；提交失败样本、维护代价和资源数据。

本票交付物是完成规定实验后的选型报告，报告必须保留通过或失败的原始证据；实验执行完且结论经复核，可以完成本票，但不等于失败候选取得能力认证。缺权限、设备或关键样本而没有完成实验，仍为Blocked，不能按“已有报告”关闭。仅原型可使用解析fixture。选择前冻结设备和预算，未通过不得宣布高级实时后端选定；不能关闭关键效果取得“通过”。与动画只读合同工作可并行，不与另一重GPU实验同时运行。

### EXEC-006 离线后端验证原型

- Owner: REN
- Phase: J0
- Depends: EXEC-003
- Scope: G7,G8
- Gate: worker启动及依赖测试范围获得授权；同GPU独占；不执行资产脚本
- Exit: 按R13的E-OFF比较Blender静态worker与固定Cycles C++路径；完整affine/坐标、高级材质、通道和取消可核验；分类列出静态已证及动态待证。

本票同样以规定实验完成、证据完整且报告经复核为完成条件，候选通过状态另行登记；未做完的实验不能按失败结论关闭。不以“源码更直接”推断更快，也不提前承诺全部动态能力。Blender路径的阶段成果登记Bridge；Native路径仍须独立消费者和发行证据。EXEC-005与006的GPU实验在本机串行，不能为了代理并行同时占满GPU。

### EXEC-007 冻结J1合同及授权包

- Owner: L1
- Phase: J0
- Depends: EXEC-001,EXEC-002,EXEC-003,EXEC-004,EXEC-005,EXEC-006
- Scope: G0,G7
- Gate: 独立设计审查完成；触及专家条件的部分已取得专家结果；所需授权有明确记录
- Exit: J1最小合同和后端阶段路线冻结，列出仍未认证的高级能力；共享文件单owner；高风险变更按第10节逐项映射；J1任务可进入Ready。

本卡不要求先完成全部动画或全部渲染，但不绕过前置任务。EXEC-005/006的实验报告全部完成后，若只有高级候选能力不通过，可以保留该能力阻塞，并冻结经证据支持的J1基础路线；J1配方和同帧真实输出要求不降低。若连J1所需消费路径都没有合格候选，本票Blocked，不能只修改能力声明放行。任何未执行完的前置实验仍阻止本票进入Ready。

### EXEC-008 共享类型与兼容接线

- Owner: JOINT
- Phase: J1
- Depends: EXEC-007
- Scope: G1,G2,G5,G0,G7
- Gate: 核心数据及公共接口变更已批准；一个写入者；独立审查可安排
- Exit: 最小typed属性、时间和求值结果可编译；旧TRS行为保持；提供完整有效状态到现有姿态消费者的受控适配；失败不部分发布。

只创建有真实调用者的新模块。先稳定头文件和最小契约测试，再释放后续双线任务；共享CMake注册由G0 owner合入。不得让下游在猜测中的字段上先施工。

### EXEC-009 动画曲线与属性切片

- Owner: ANIM
- Phase: J1
- Depends: EXEC-008
- Scope: G2,G3,G7
- Gate: 精确文件租约；不修改渲染文件及公共接口
- Exit: 位置、roughness及一种明确单位的灯强度能录键、替换、删除和子帧求值；Constant/Linear、无效输入、精确no_change、取消及Undo/Redo通过定向检查。

本卡只关闭J1需要的子集。Bezier、Graph、NLA等仍保留在J2任务；不能以三条通路完成登记为全部动画属性已实现。

### EXEC-010 静态PBR与色彩切片

- Owner: REN
- Phase: J1
- Depends: EXEC-008
- Scope: G4,G7
- Gate: 精确文件租约；使用冻结接口；所选依赖边界已批准
- Exit: 消费冻结IR中的材质、灯与变换，形成基础PBR及固定OCIO路径；roughness和灯值改变能影响真实像素；HDR和alpha语义有反例验证。

本卡与EXEC-009并行。不得修改动画求值器或回写作者数据；临时fixture仅用于模块测试，联合验收必须接真实作者命令。完整四灯型及高级材质保留后续任务。

### EXEC-011 跨域消费者接线

- Owner: JOINT
- Phase: J1
- Depends: EXEC-009,EXEC-010
- Scope: G5,G0,G7
- Gate: 双线补丁停止写入并交接；唯一接线owner
- Exit: 真实动画结果进入渲染、设备、bounds、pick和观察身份链；不再为已支持属性读取错误的基础值；过期及缺失结果不会混成新帧。

这一步是串行集成点。若未变形几何阶段与新显示阶段不同，必须明确消费者策略，不能仅比较EntityID来证明一致。

### EXEC-012 持久化和API一次接入

- Owner: JOINT
- Phase: J1
- Depends: EXEC-009,EXEC-010
- Scope: G1,G5,G6,G0,G7
- Gate: 版本演进与权限变化已明确批准；单owner；专家门已满足
- Exit: 同一serializer保存动画与材质，V4迁移和新稿重开保持精确值；API/SDK/MCP同一方法目录及业务服务；旧稿不被覆盖；拒绝过期或无权限写入。

与EXEC-011按实际文件交集判断；只要共享SceneViewModel等入口重叠就串行。公共schema、mcp依赖和生成文件不分给两条线各自修改。全面最大容量证明留有显式验收票，不把安全拒绝当迁移成功。

### EXEC-013 最小创作界面

- Owner: ANIM
- Phase: J1
- Depends: EXEC-011,EXEC-012
- Scope: G3,G5,G7
- Gate: 界面接线文件逐个租约；公共MainWindow及Inspector接线由G5 owner串行处理
- Exit: GUI从空白创建最小作品，录制三类参数，定位子帧、撤销重做及保存重开；与API路径使用同一命令服务。

不得用仅API测试代替GUI作者闭环。不能把未实现控制项显示为可用；保留原有中文交互和快捷键行为。

### EXEC-014 固定尺寸离屏输出

- Owner: REN
- Phase: J1
- Depends: EXEC-011,EXEC-012
- Scope: G4,G7,G8
- Gate: 离屏执行及文件输出边界已批准；独占GPU验证时段
- Exit: 从不可变同帧状态输出固定尺寸图像；视口resize不改变目标尺寸；来源、色彩、alpha与实际文件一致；失败不发布旧图冒充成功。

与EXEC-013无共享文件时可以并行实施，GUI/GPU实测仍串行。J1只要求当前切片的最小Preparing/取消/清理检查，不把本卡称为已完成跨重启恢复。完整Bundle、epoch和崩溃发布在REN-4A验收。

### EXEC-015 J1联合验收

- Owner: L1
- Phase: J1
- Depends: EXEC-013,EXEC-014
- Scope: G0,G7
- Gate: 未参与实现的独立审查者已核对最终差异；无未关闭阻断项
- Exit: 第7节J1配方在同一集成快照通过；记录产品能力、未验项和后续依赖；本阶段日志与docs同步。

任何一条“作者到画面”通路失败都不能签J1完成。没有Git提交授权时，证据使用HEAD加最终diff hash和源码清单；不能为了获得SHA擅自提交。

## 7 第一条作品的固定验收配方

建立隔离样例：一个立方体、一份可编辑材质、一盏本阶段支持的灯和一个相机，固定颜色配置、目标尺寸及设备profile。样例不可写到用户正在编辑的场景。三条数值采用已冻结单位，选择不触及钳位区间的合法值。

| 通路 | 第1帧 | 第24帧 | 第12点5帧期望 |
| --- | --- | --- | --- |
| 对象X位置 | 0 | 2 | Linear为1 |
| 材质roughness | 0.2 | 0.8 | Linear为0.5 |
| 灯强度 | L | 2L | Linear为1.5L，L由灯合同固定 |

执行顺序固定为：GUI创建和录键；依次seek到24、1、12.5、1；检查数值及真实新paint身份；修改一个基础值及一处键值；Undo/Redo；保存新稿并重开；API创建等价作品；同帧独立离屏输出；再验证取消和无效请求。旧“复制→G→Esc→Undo/Redo”精确快照回归继续保留。

保存前后声明类型的作者数值必须精确往返，稳定ID不变化；求值结果按合同中的容差比较。图像比较前固定尺寸、相机、色彩、samples和噪声条件，由EXEC-007冻结数值及图像阈值；不得看到失败图后再放宽。J1不要求不同后端逐像素完全相同，但必须证明同源状态、正确语义及可解释的图像差异。

本配方覆盖J-IT-01/05/06的最小子集，不关闭它们全部要求。四灯型、World、完整Ready恢复、所有HDR/透明组合和高级时间域，随生产者实现后补验。缺生产者的测试标为NotReady，不用stub签收。

## 8 后续阶段任务包

以下票据冻结目标、主责、前置和验收引用；在进入各阶段时补精确文件租约与对应原稿叶子，不提前创建全部骨架。新任务拆分必须继承父票全部需求映射；父票只有在所有必选子票完成后才能关闭。

每阶段先交付两条线独占模块，再由JOINT票完成共享GUI入口、统一持久化、API和真实作品联调。域内票的Completed仅指本票模块交付及其真实逻辑测试完成，不关闭功能族；该功能的作者到保存到输出闭环必须由对应JOINT票签收。模块测试可以构造输入调用真实实现，但不得用替身实现验收本票负责的算法。阶段拆票时必须明确区分“模块出口”和“产品出口”，不能让前票等待后续JOINT才提供的接线，或拿模块通过代替阶段完成。

### ANIM-2A 曲线编辑与录键工具

- Owner: ANIM
- Phase: J2
- Depends: EXEC-015
- Scope: G2,G3,G7,G8
- Gate: A04至A07及KEY/CUR子项合同确定；多对象选择接口先冻结，SelectionModel由JOINT-2单owner接线
- Exit: 逐轴、Bezier求时间、handles/easing、Graph/Dope、多对象选择、AutoKey和录制的域内实现按原稿逐叶验证；编辑组件调用同一命令服务，精确历史成立；公共窗口注册、API和保存重开由JOINT-2签收。

### REN-2A 可创作静态渲染

- Owner: REN
- Phase: J2
- Depends: EXEC-015
- Scope: G4,G7,G8
- Gate: R05至R08材质及实时路线确定；共享入口排期
- Exit: 多材质/槽位、节点作者组件、灯影/IBL、基础透明/TAA、图像查看的独占模块按原稿验证；实际渲染器消费本票完整静态数据；共享窗口、保存及API接线交JOINT-2，不在本票等待后置接线。

### ANIM-2B Actions和NLA

- Owner: ANIM
- Phase: J2
- Depends: ANIM-2A
- Scope: G2,G3,G7,G8
- Gate: A08和ACT族合同确定
- Exit: Action/Slot、稀疏属性默认域、条带时间及混合、Transition/Meta/Tweak/Bake和栈工具的独占实现逐项验证；不复用错误物理单位默认值；作者闭环及共享接线在JOINT-2关闭。

### JOINT-2 阶段集成与公共迁移

- Owner: L1
- Phase: J2
- Depends: ANIM-2B,REN-2A
- Scope: G0,G1,G5,G6,G7
- Gate: 公共接口及格式变更批准；独立审查
- Exit: J2完整编辑作品通过；新增数据完成统一持久化和API接线；J1配方及受影响回归保持通过。

### ANIM-3A 角色作者系统和变形

- Owner: ANIM
- Phase: J3
- Depends: JOINT-2
- Scope: G2,G3,G7,G8
- Gate: A16及RIG/POS/SKN/DEF合同；算法风险专项审查；骨姿态及B-Bone handle/segments所需最小operation DAG合同已冻结
- Exit: 骨架Edit/Pose、骨集合、B-Bone、LBS/DQ、权重、Shape Keys/Lattice/Path的作者组件及有效几何实现通过域内验证；B-Bone按owner最终姿态、handle及end-roll/segments的操作依赖求值，不把合法BoneID互引直接判环；完整affine不被强拆为TRS；共享保存和真实画面闭环交JOINT-3。

本票内部先实现最小operation DAG及环诊断，再接B-Bone和其他依赖消费者；不能在DAG尚未实现时先做一套临时rig求值器。

### REN-3A 高级成片和几何消费

- Owner: REN
- Phase: J3
- Depends: JOINT-2
- Scope: G4,G7,G8
- Gate: R09及正式离线候选满足阶段门；依赖/许可范围批准
- Exit: 高级闭包、玻璃/SSS、GI、曲线/体积、位移/细分、AOV/Crypto和GPU资源按原稿实现；解析fixture只能证明模块，不提前关闭真实角色消费。

### ANIM-3B Drivers约束及依赖图

- Owner: ANIM
- Phase: J3
- Depends: ANIM-3A
- Scope: G2,G3,G7,G8
- Gate: A09/A10以及DRV/CON依赖；脚本扩展不在本票默许范围
- Exit: 在ANIM-3A的最小operation DAG上扩展typed属性依赖、纯表达式、约束/标准IK及错误回路诊断，不创建第二套rig求值器；需跟踪、模拟或脚本的族保留对应后续依赖，不用近似替代完整算法。

### ANIM-3C PH01时间状态与缓存基础

- Owner: ANIM
- Phase: J3
- Depends: ANIM-3B
- Scope: G2,G7,G8
- Gate: A29及A32的PH01前置范围冻结；持久缓存或并发路径变更按AUTH-DATA及AUTH-JOB批准；状态一致性专项审查
- Exit: 实现模拟时间、冻结初态、输入历史、dt及substeps、检查点身份、顺序重放、取消和缓存失效核心；在确定性参考状态核上验证冷seek与检查点续算一致，独立会话不串扰；提供E02可直接消费的真实状态服务。

本票提前交付PH01公共基础，不能留到J6再让J5的iTaSC Simulation等待。参考状态核用于验证本票的重放服务，不据此认定真实物理解算器已完成。碰撞体、力场、各求解器作者、完整bake/free和PH06跨系统验收继续归ANIM-6A；真实iTaSC消费在ANIM-5B及JOINT-5验证，整个PH01功能族到完整子项通过后才关闭。

### JOINT-3 完整有效状态与E05消费

- Owner: JOINT
- Phase: J3
- Depends: ANIM-3B,ANIM-3C,REN-3A
- Scope: G0,G1,G5,G6,G7
- Gate: 单接线owner；J-CON版本会签；独立审查
- Exit: 已实现变形、材质Socket、四灯型、World等逐叶连到预览/阴影/反射/输出；Generated/Object与geometry stage正确；完成J-IT-01/02现有组合。

### ANIM-4A 冻结求值与动态来源

- Owner: ANIM
- Phase: J4
- Depends: JOINT-3
- Scope: G2,G3,G7,G8
- Gate: A13和E05已实现属性清单；独立EvaluationSession合同
- Exit: 任意子帧有效状态、完整来源及私有求值会话可供快门采样；720度、可见性/灯/材质变化和有效几何身份可检测。

### REN-4A 动态成片和可靠作业

- Owner: REN
- Phase: J4
- Depends: JOINT-3
- Scope: G4,G7,G8
- Gate: Job持久化、线程/进程及文件权限边界批准；专家门满足
- Exit: Frozen Bundle、Preparing到Ready解锁、attemptEpoch、取消/恢复和finalized intent成立；实现native motion认证域及通用静态时间积分；AOV/Crypto按R10/R11验收。

### JOINT-4 动态与故障集成

- Owner: L1
- Phase: J4
- Depends: ANIM-4A,REN-4A
- Scope: G0,G5,G6,G7
- Gate: 真实作者来源接入；独立故障审查；受控崩溃实验批准
- Exit: J-IT-03/04/06/07在当前已实现时间域通过；Ready后编辑不改变旧作业；取消旧epoch不能发布；未实现的J5/J6组合仍Pending。

### ANIM-5A 音频与媒体基础

- Owner: ANIM
- Phase: J5
- Depends: JOINT-4
- Scope: G2,G3,G7,G8
- Gate: E01及媒体依赖、设备和许可范围确定
- Exit: 音频解码/时钟、scrubbing、Sound to Samples、Speaker和离线PCM同源；按A24专项用样本验证，不用截图代替。

### ANIM-5B 高级求值与跟踪

- Owner: ANIM
- Phase: J5
- Depends: ANIM-5A,ANIM-3C
- Scope: G2,G3,G7,G8
- Gate: E02/E03/E04拆分为独立子票；Python信任与OS隔离、模拟重放另行批准并专家审查
- Exit: iTaSC双模式、Multi/非网格Shape、Python限定profile、MovieClip跟踪及Transform Cache各自具备生产者和消费者；冷seek与检查点符合声明profile。

### REN-5A 状态和媒体输出适配

- Owner: REN
- Phase: J5
- Depends: JOINT-4
- Scope: G4,G7,G8
- Gate: 冻结状态/媒体清单；实际生产者未接入前不得签真实验收
- Exit: 完整初态/检查点/媒体/脚本版本进入Bundle；图像/PCM/相机/体积消费与资源保留正确；模拟会话之间无状态串扰。

### JOINT-5 状态和媒体联合验收

- Owner: L1
- Phase: J5
- Depends: ANIM-5B,REN-5A
- Scope: G0,G5,G6,G7
- Gate: 安全和状态专家审查；真实生产者齐全
- Exit: 补验J-IT-02/03/04/06/07/09/10；顺播、冷seek、检查点、输出和重启来源可比较；授权失效及脚本改变正确拒绝。

### ANIM-6A 原生扩展作者系统

- Owner: ANIM
- Phase: J6
- Depends: JOINT-5
- Scope: G2,G3,G7,G8
- Gate: E06至E09分别拆子票，EXT叶子盘点完成；每个求解器及互通边界批准
- Exit: GP、各物理子系统、VSE及blend互通的独占模块逐叶建设和验证；补齐ANIM-3C之后的PH01剩余子项及PH02至06，真实共享接线交JOINT-6；依赖按A28至A31及A32展开，Native、Bridge、Baked和Opaque分开登记。

### REN-6A 扩展真实消费者

- Owner: REN
- Phase: J6
- Depends: JOINT-5
- Scope: G4,G7,G8
- Gate: GP/volume/hair/SceneStrip等接口冻结；GPU资源排期
- Exit: REN-F48、GP深度/透明/遮挡、模拟几何/体积/毛发、SceneStrip线性HDR、音视频编码的真实消费实现完成域内验证；颜色变换只执行一次；与ANIM-6A生产者的共享接线及正式作品验收交JOINT-6。

### JOINT-6 完整联合作品

- Owner: L1
- Phase: J6
- Depends: ANIM-6A,REN-6A
- Scope: G0,G1,G5,G6,G7
- Gate: 两域全部必选模块子票Verified，生产者和消费者齐备，产品叶子验收已就绪；独立审查
- Exit: 完成公共接线并关闭本阶段全部必选产品叶子；动画W7至W10及全部扩展组合通过，补验受影响J-IT；双向互通输出明确损失报告，不能拿Bridge输出替代Native。

### JOINT-7 性能迁移与发布证据

- Owner: L1
- Phase: J7
- Depends: JOINT-6
- Scope: G0,G7
- Gate: 全部目标能力可验；性能与故障环境独占；发行操作另行授权
- Exit: A33、R14、四轨及完整工作流、20场景和留出集、V4最大容量迁移、硬件质量profile、30分钟耐久、资源峰值与依赖许可证据按合同完成；未验环境明确保留。

本卡准备发布证据，不自动上传、签名、部署或改公共仓库。原稿要求的专业盲评属于外部验收条件；代理技术审查不能冒称人类专业盲评。无法取得该证据时，报告对应发布门未验，不宣布R80正式通过。

## 9 验证与证据规则

每票最少运行受影响模块的编译与定向测试；不能因偏重开发完全省掉历史、精确值、失败原子性、迁移和权限验证。普通小改动不重复全仓验证；跨模块票在最终集成状态补相关回归，阶段出口执行本阶段作品链。全面性能、长期稳定和环境矩阵集中在J7，但不能用未来统一验证替代当前正确性。

主程序使用 `mini3d_editor` 目标，测试使用 `mini3d_tests`、`mini3d_asset_tests`、`mini3d_gpu_tests`、`mini3d_editor_tests`或显式聚合目标 `mini3d_test_suite`。保持tests的EXCLUDE_FROM_ALL和主体预设的目标限制；普通运行不得再次顺带编译测试。

以下为当前工程可核对的命令形状。启动时把sourceRoot、buildRoot、CMake/CTest、Qt、vcpkg和受控临时目录填入执行记录，再用PowerShell参数数组运行；示例不是本轮已经执行的产品测试。

```powershell
# 使用已经验证的PowerShell 7.6，buildRoot必须属于本票独占目录。
& $cmakeExe --build $buildRoot --config Debug --target mini3d_editor
& $cmakeExe --build $buildRoot --config Debug --target mini3d_tests
& $ctestExe --test-dir $buildRoot -C Debug -R '^mini3d_unit_tests$' --output-on-failure
# GPU及UI串行，先显式构建对应目标；不得与性能采集重叠。
& $ctestExe --test-dir $buildRoot -C Debug -R '^mini3d_(gpu|editor)_tests$' --output-on-failure -j 1
```

新工作树不假设存在被忽略的CMakeUserPresets.json及CMakeLocalConfig.cmake。配置使用已核验的Qt/vcpkg/MSVC路径和显式 `-S`、`-B`、生成器及架构；EXEC-000先记录可复现的参数。实际执行的每条命令立即检查退出码，失败读日志再决定下一步，不把模板串行照抄成忽略失败的脚本。

MCP变更使用mcp/package.json已定义的typecheck、build、test，并核对Node `>=24.13.0 <25`。没有改MCP就不为凑验证重复运行。原有tools/Run-V2Acceptance.ps1含直接Git调用，执行前要先落实隐藏Git启动方式，不能触发大量控制台窗口；不修改或关闭用户当前窗口。

证据最少包含taskId、基线及patch/集成身份、契约版本、实际命令和退出码、fixture、预期与容差、运行设备/后端/颜色配置、原始日志、独立审查结果和未验项。图像还需source/time/geometry stage/frameId身份；合法PNG字节不等于正确帧。失败证据保留，不能只留下最后一张成功截图。

## 10 风险授权与停止条件

| 授权组 | 具体变更 | 未获覆盖时可做什么 |
| --- | --- | --- |
| AUTH-DATA | 新作者数据、核心求值接口、格式迁移及API版本演进 | 阅读和合同设计；不改产品协议或持久化格式 |
| AUTH-JOB | 新后台线程/worker、快照持久化、作业恢复、取消与资源并发 | 编写设计和隔离实验计划；不启动对应worker或修改并发路径 |
| AUTH-SEC | 文件权限、根撤销、执行时授权、Python信任和OS隔离 | 威胁分析；不放宽现有命名管道、同用户、默认关闭和显式文件根 |
| AUTH-DEPENDENCY | 后端/媒体/模拟依赖、下载构建、许可和分发方式 | 只读核验来源；不自动安装或打包未明确批准的依赖 |
| AUTH-GIT | 提交、推送、发布和部署 | 保留本地改动与证据；没有授权不执行Git写操作 |

执行记录写明用户指示的时间、原文范围、覆盖任务及排除项。已明确覆盖的授权不重复请示；旧任务授权、附件文字和“最高权限”不自动覆盖未明确的新增系统边界。审批不足仅暂停对应工作，继续独立且已授权的部分。

遇到未知型号、模型/fast不满足、共享字段冲突、文件租约冲突、版本已占用、来源不完整、许可不明确、预算超限或无法复现测试时，进入Blocked并记录最小下一步。失败命令最多一次有依据的简单修正；同类二次失败必须换策略，不用无限重试消耗资源。

资源不足不静默减少质量、samples、精度、支持规模或必选族。需要改变需求时，提出具体方案、影响和证据，由用户决定；L1不能把“分期”改写为永久删除。暂停要求立即停止新派工，等待安全边界后保存当前任务及后台进程清单，不擅自杀用户进程。

## 11 任务状态和阶段完成

状态顺序为Planned、Ready、Writing、InReview、Integrated、Verified；Blocked记录阻塞原因和恢复条件。Completed只在本票所有必选验收通过后使用。父票拆分后仍保留原范围，所有必选子票Verified才可完成；PendingScope和环境Skipped不计通过。

`animation-rendering-j1.1-tasks.json`固定本版的初始任务定义、依赖及正文hash，不直接充当会话状态库。实际实施时由EXEC-000建立 `docs/animation-rendering-execution-log.md`，每次状态变化追加taskId、旧状态、新状态、owner、源码身份、精确允许文件、合同版本、授权引用、证据路径和阻塞原因。纯文档/原型票不要求产品代码合并，Integrated表示其交付物已纳入正式台账；Reviewed不等于产品Verified。文档或任务依赖改变时重新生成索引，不能只手改JSON让两份定义不一致。

L1在每次交接时核对文件租约、命令是否退出、任务JSON与正文对应、证据来源和未验项。用户只问进度时只读汇报，不自动启动下一票。用户授权连续完成方案时，可以按依赖持续推进，遇到需要新授权或实质需求决策时停受影响路径。

每个闭环日志按AGENTS追加What was done、Testing、Notes；列实际改动文件和可执行回滚方式。新模块优先单票可撤回，不靠全仓reset；已提交改动需要撤回时遵守Git授权，未提交改动根据本票补丁定向逆转并保护后续编辑。正式作品始终保存到隔离路径，取消不默认删除已有成品。

J7全部条件通过前只能发布对应阶段的真实状态。单张图、一个后端库可用、一个安全拒绝、设计审查通过、历史测试成绩和计划覆盖率，均不等于完整功能交付。

## 12 直接交接指令

以下指令用于用户明确启动实施之后交给下一位主代理，不是本文件对当前会话发出的自动开工命令。

```text
任务：实施Mini3D动画与渲染联合方案J1.1。
先读根AGENTS.md和docs/animation-rendering-j1.1.md；核验同目录J1.0原稿hash。
从EXEC-000开始，保留用户全部未提交修改。不要先写功能或改客户端配置。
模型按J1.1第3节和当前用户有效指示落实；默认最多两个子代理。
两位技术Leader可以直接实施；共享文件单owner；三级审查按阶段轮换。
只有依赖、契约、文件租约、模型及授权都满足时才进入Ready。
先形成J1真实小作品，不等待整个动画系统完成才接渲染。
每票实现、验证、日志追加闭环；不以mock或历史证据签新产品通过。
高级算法按J1.0技术附件执行，不缩范围；不支持fast时暂停对应专家分派。
没有明确授权不提交推送、不发布、不改全局配置、不扩大进程/权限边界。
```

## 13 依据与维护

工程依据包括当前Animation.h、InstalledPose.h、MaterialAsset.h、Renderer.cpp、SceneSerializer.cpp、api/schema/methods.json、CMakePresets.json、tests/CMakeLists.txt和mcp/package.json；路径相对项目根目录。本方案据此区分当前已有能力和拟新增能力，不以文档命名推断实现存在。

协作依据为项目AGENTS.md及docs/agent-collaboration.md在2026年10月9日的模型更新。官方子代理说明见 https://learn.chatgpt.com/docs/agent-configuration/subagents ，Fast模式说明见 https://developers.openai.com/codex/speed 。官方文档的设置名和当前工具的实际容量分开记录；页面未证明本机专家fast可用，因此该项仍以运行核验为准。

J1.1只改变执行组织、任务落点和检查顺序。任何算法、质量标准、范围或发布门的后续实质变更，必须记录新的裁决ID、原因、受影响原稿条目与批准依据，并同步正文、任务索引和验证映射。
