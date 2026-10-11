# J1 首个作品的施工合同

2026-10-11 · EXEC-007/008 · 版本 J1-CONTRACT-2 · 状态：来源修订独立设计审查通过并冻结；产品实现另验。

本文件把 J0 三份草案中下一条真实功能链所需的字段、判定和施工边界固定成一份合同。与 J1.1、J1.0 完整附件一起使用；没有删除后续能力、缩减功能分母或修改原稿算法。当前产品仍是格式4/API0.2.0，新产品 Verified=0。独审结论和准入状态只在执行台账追加。

## 1 路线与施工顺序

J1 基础实现使用原生 OpenGL4.1 core；不引入 Filament 或 Blender worker 作为必需消费者。Filament 高级能力、Native Cycles 与 Bridge 的产品接入和发行认证仍按原案推进，未通过项不计分。J0 实验完结与候选认证分别登记，依据 `animation-rendering-j0-backend-report.md`。

EXEC-008 只实现共享作者类型、时间、完整有效状态及旧姿态适配；先完成头文件、真实调用者和契约测试，再释放 EXEC-009/010。008 不修改 serializer、API/schema、SDK/MCP、文件格式或版本；012 是唯一演进集成点。旧 TRS 接口保留原语义，精确取消、原始 Euler、固定复制 ID 和唯一历史不变。

用户已直接批准010/014的固定OCIO2.5及自有最小Standard本地产品构建接入、同应用线程固定尺寸PNG/FLOAT32 EXR离屏输出，仅明确验收文件根、不新增后台线程/持久作业/权限放宽、不安装系统/提交发布。该授权不是产品分发许可。长期线程、worker、跨重启Bundle/epoch/恢复以及安全边界仍不在本切片授权内。

## 2 身份、地址和正式作者数据

以下名称是共享 C++ 语义与固定外部拼写；012 再统一 wire/schema 字段。core 不依赖 Qt/OpenGL。现有 `EntityId`、运行期 `AssetId` 和 API `DocumentHandle` 不更名或偷换语义。

| 项 | 冻结规则 |
| --- | --- |
| DocumentUuid | UUID128，16字节；非nil，canonical外部字符串为小写带连字符UUID。新文档生成一次；载入V4在私有升级候选生成，V5另存后持久恢复。不能用live DocumentHandle或文件名代替 |
| 稳定ID | 非零uint64并带类型；Asset/Material、MaterialSlot、Action、ActionSlot、Curve、Key互不隐式转换。MaterialId是同一持久资产表kind=Material的引用，不另建材质分配器；Entity owner包装现有EntityId |
| 分配 | 候选私有预留、正式提交才发布；Undo不降低high-water mark；已发布不复用。Redo恢复已保存ID；失败/no_change不消耗可见ID |
| OwnerRef | `Entity(EntityId)`或`Material(MaterialId)`，地址同时带DocumentUuid；相同数字不表示相同类型 |
| PropertyId | 固定登记Position、RotationEulerXYZDegrees、Scale、PbrRoughness、SunIrradiance；legacy通道保留。点号文本只解析到登记项，不提供任意字符串反射 |
| Component | XYZ字段只允许X/Y/Z；标量只允许Scalar。错owner、错分量、未知字段或跨文档引用整笔拒绝 |
| Socket | J1不创建假的节点/Socket生产者。后续节点图须显式解析到同一规范PropertyId，不能生成第二条roughness曲线 |

最小动作定义为 Action→typed ActionSlot→scalar Curve→Key；OwnerBinding指向Action/Slot。Curve按(PropertyId,Component)唯一；Key保存稳定KeyId、binary64 time/value和出射插值。绑定时解析owner，不将某一次EntityId隐含烙入可共享Action。009实现该模型的实际编辑/取样；008不预建NLA/Driver/节点图。

复制对象复制实体动作并重绑定、默认共享MaterialId；显式linked动作才共享实体动作。每实体MaterialSlotId独立，复制重映射槽及面引用；复制材质才生成新MaterialId。删除仍被引用的定义/槽须拒绝或有同一笔明确重映射。

| 正式字段 | 创建默认、作者精度、合法域、单位 |
| --- | --- |
| transform.position | 原基础TRS为binary32，新scalar键/默认为binary64；(0,0,0)，有限且实际float可消费，local-to-parent scene unit，不自动等于米 |
| transform.rotation_euler_xyz_degrees | raw binary64连续度；继承现有最大绝对值3.6e9，派生qz×qy×qx；无轨时保留原基础quaternion，不重新求Euler |
| transform.scale | 基础binary32/新scalar binary64；(1,1,1)，保持符号，绝对值至少0.001；Linear新邻接不得穿零 |
| material.pbr.roughness | binary64、默认1、有限闭区间[0,1]、无量纲；规范owner为MaterialId |
| material.pbr.base_color / metallic / coverage | baseColor为scene-linear Linear Rec.709 Color3，默认(1,1,1)，各分量[0,1]；metallic默认0/coverage默认1，binary64 [0,1]。J1只承诺OPAQUE |
| light.sun.irradiance | binary64，默认1，有限非负，W/m²垂直入射参考辐照度；Entity owner必须有Sun组件，checked设备转换失败拒绝 |
| Sun.color | 线性RGB色权重Color3、默认(1,1,1)、各分量[0,1]；不把分量之和解释为光谱功率 |

MaterialDefinition持有模型、定义版本、作者参数和明确纹理绑定。无纹理合法；已声明却找不到不得退成白材质。新PBR和Sun与旧SurfaceStyle/LightComponent为独立模型；旧灯0.72F/[0,10]仍为legacy无量纲，不自动换算W/m²。J1只给roughness和SunIrradiance新增动画计分，其他字段默认值不冒充全属性动画实现。

## 3 时间、取样和来源身份

FrameTime为有限binary64，[1,100000]；新scalar键同类型，持久子帧合法。RationalFps为正uint32 numerator/denominator、gcd=1、以checked整数比较确定1–120；旧fps n映为n/1。以frameOrigin=1，seconds=(frameTime-1)×denominator/numerator。修改fps不移动键；播放范围不限制合法seek或删除键。新inclusive frame-step与V4 legacy半开周期分别标记，不能共用含混循环策略。

Constant在左键区间取左值，精确右端点取右键；Linear内部使用double lerp，精确键/端外Hold返回存储原值。键时间精确递增，碰撞不按epsilon合并；批量最终冲突整笔拒绝，replace是显式策略。已知未实现Bezier等返回Unsupported，不按Linear代做。

SourceIdentity是共同来源记录，字段如下；单一共享owner实现，不另外发明RenderSourceIdentity。

| 字段 | J1生产者与比较 |
| --- | --- |
| documentUuid、sourceKind | 持久作者域；LiveFormal/LiveDraft/FrozenFormal。kind决定是否有draft或frozen字段 |
| liveHandle或sourceBundleId | live含当前API instanceId/documentId；frozen使用自己的snapshot/bundle身份，不同时冒用live。014只生成进程内不可变快照，不称持久RenderJob.Ready |
| contentRevision | 当前documentRevision；最终CAS后才能按真实已提交revision绑定，不能给未复核旧状态贴新revision |
| resourceSetIdentity | 被消费稳定资产ID/版本/内容hash清单及真正只读字节/解码拥有者；无外部资源明确空清单，不伪造hash或以路径代替 |
| frameTime、rationalFps | 原binary64时间与规范fps；与effective结果相等，不使用四舍五入显示值 |
| algorithmBuild、evaluationProfile、purpose | 算法/合同build身份、BasicAnimation/JointPreview/Offline profile版本；Preview/Sample/Output用途 |
| authorContextIdentity | 所用作者定义/绑定语境身份，含documentUuid/contentRevision及原live修订域revisionDomain（instanceId/documentId）；后两项为不可变出处元数据，不是权限或活查询handle。不把临时选择写入作者内容 |
| evaluationSessionId、evaluationId | 独立会话及单调evaluation计数；独立sample不复用live计数，溢出不复用ID |
| draftIdentity（仅LiveDraft） | draftID/generation/baseSource/必要sessionRevision；正式与draft不可互换 |

比较分两种：同一安装状态各消费者核对完整身份；两个独立session的同源作品比较作者/资产/算法/profile/语境/精确时间，不要求evaluationId相同。authorContext的revisionDomain必须非空；LiveFormal/LiveDraft须等于outer liveHandle，FrozenFormal继承冻结输入的原作者context，outer handle仍仅为自身FrozenHandle。文档局部revision不跨域唯一，不能仅靠同UUID/同revision判同源；本比较保守区分重新打开的live修订域，不宣称跨域逐内容等价。paint frameId、GL contextGeneration、视图及尺寸是实际消费身份，另行记录；SourceIdentity相等不证明新绘制。

Direct sample无状态，不修改游标、选择、历史或安装画面；24→1→12.5→1同源数值一致。失败不发布部分状态；保留旧图时明示旧时间/来源和失败原因，停止继续推进播放，不以旧图作正式输出。

## 4 完整有效状态与旧消费者适配

EvaluatedSceneState一次拥有：SourceIdentity、精确时间、完整节点结果、完整材质定义有效值及实体槽绑定、可见Sun集合、相机/legacy设备快照、每实体无修改几何stage与不可变CPU资源。作者→采样→effective来源可读；尚未支持的连接/override不参与求值。未知必需引用/隐藏节点错误也拒绝整次结果。

节点保留binary64 local/world affine、inverse、normal及独立rigid orientation；右手Y-up、列向量/GLM列主、相机和Sun局部-Z。父链允许shear/镜像；只读消费不强拆TRS。checked float适配继续验证实际八角、法线、射线、设备方向和投影，不只查矩阵有限。不能先缩成float再称double求值；派生值不回写作者/quaternion/Euler。

精度兼容裁决：保留既有evaluateAnimationPose签名和legacy float运算路径，不为本轮重构改变旧TRS回归数值；新J1状态的double矩阵由作者/键在double域计算，并为新消费者checked转换。legacy numerics与资源以同一冻结来源单独持有并明示LegacyTrsV1，不将原float提升声称新double物理能力已验收。012以前旧V4仍走兼容入口；文档生命周期内只建立一次非nil UUID/兼容映射并随Scene复制保留，sample不分配ID，到012另存才持久化。无材质绑定的旧对象不强制制造PBR/槽，不因新字段缺失拒绝原合法legacy作品。

共同身份生产入口为SceneAnimationSession的animationPreparationSource、apiDocumentState及最后matches守卫；Scene提供作者域/正式数据，SceneViewModel提供独立evaluation session，geometry持有旧资源。真实无外部资源的新J1才使用明确空resource清单；旧外部资源可继续legacy只读路径，但没有原字节/解码身份清单时新资源身份能力标NotReady，不把shared_ptr/path冒完整hash，不每sample创建替代UUID/槽ID。要新增AssetManager访问接口须先记录精确租约，不在008悄悄扩大。

InstalledPose在原有numerics+geometry上持有同一完整state。旧TRS返回语义保留，geometry/材质/灯/资源与state整体安装。ScenePoseView作为受控消费门；新支持属性缺失不得回退live Scene。008先接入完整state创建/校验/持有，旧Renderer接线到新PBR由010/011完成，不能把008称作者到画面已通过。

无修改geometryStage由真实entity、mesh/content/属性版本、完整affine、source和算法profile确定，并持有原拓扑。Generated使用冻结未变形orco/texspace，Object使用指定对象逆矩阵；J1无程序纹理生产者不计该消费能力。pick/bounds/显示/输出记录同stage；新顶点不能沿用旧stage。GPU句柄不持久化。

## 5 渲染、色彩和图像门（事前冻结）

基础BRDF使用Lambert diffuse+isotropic GGX、height-correlated Smith、Schlick，IOR=1.5，F0=mix(0.04,baseColor,metallic)，F=F0+(1-F0)(1-V·H)^5，diffuse=(1-metallic)×(1-F)×baseColor/π。以a=alphaEffective、nh=N·H、nv=N·V、nl=N·L，D=a²/[π(nh²(a²-1)+1)²]，联合visibility=0.5/[nl×sqrt(nv²(1-a²)+a²)+nv×sqrt(nl²(1-a²)+a²)]，specular=D×visibility×F；不要再除一次4nl nv。Sun贡献为BRDF×irradiance×color×max(nl,0)。固定背景黑、无World/ambient常量，不从比较器lux归一化尺度借单位。无阴影/IBL的J1切片不宣称已完成这些生产者。

roughness到微表面alpha为r²；仅内部取alphaEffective=max(r²,0.001)，并把该正则化写入backend profile/build身份。作者/effective roughness=0仍精确保留0，不回写、不改滑杆最小值。N·V/N·L≤0贡献0；退化H不给无穷；设备转换和最终像素须有限。解析CPU参考独立计算同一已声明模型，不拿Blender Principled相似图充当此模型oracle。

唯一色彩配置为已验证自有最小Standard/OCIO2.5，config SHA256 `580836D65890F3226EA7E38B67F0A1F55C4125149B3F311E6E3E7E98B8999927`；Display=sRGB/View=Standard/Look=None/Exposure=0/Gamma=1。它是固定OCIO processor，不是手写同名shader替代；连续切线moncurve不是canonical固定12.92。无外部Look/LUT/ICC，AgX/Filmic仍未实现/认证。

本条明确取代J0-render草案4.2的Blender固定配置候选，仅冻结自有最小Standard范围，不复制Blender配置/LUT或推导其资产许可；实际OCIO2.5库build/DLL身份在010接入清单逐项绑定，旧隔离run的CPU/GPU转换证据不能直接冒新产品实现证据。

FLOAT32 scene-linear Linear Rec.709 associated Beauty及独立coverage母版；Alpha不套OCIO，data/ID不进入颜色变换。HDR>1/合法负RGB/零alpha非零RGB保留；预览在线性域预合背景后执行一次processor。GL_FRAMEBUFFER_SRGB关闭，PNG只量化/打包/标签；透明PNG零alpha发光明确拒绝不可保真。UI覆盖层不入母版。

| 验收项 | 预先判定 |
| --- | --- |
| 作者往返、取消、no_change、ID、端点/Constant | 声明类型精确相等，不以矩阵或epsilon代替 |
| J1中点位置/roughness/Sun | 绝对误差≤1e-12；双精度affine/inverse/normal与独立参考abs1e-10+rel1e-10 |
| checked float数值 | 与独立转换参考abs2e-5+rel2e-6；不可消费原值拒绝，不靠舍入越界 |
| OCIO CPU/GPU | abs2e-5+rel2e-6；alpha位相同/data字节相同，双编码/清零负控制必须拒绝 |
| BRDF/Sun内部采样像素 | FLOAT32 RGB相对独立double参考abs2e-5+rel2e-4；剔除边界前事先规定mask，不能失败后挑像素 |
| 同源preview/output | 相同1280×720、DPR1、相机/投影、1:1pixelAspect、无MSAA/TAA/DOF随机项；母版RGB abs2e-5+rel2e-6、alpha位相同；已编码PNG每通道差≤1/255 |
| 图像真实变化 | 分别固定位置/灯/roughness其余量；预冻同一ROI内至少16个RGB像素变化>1e-4，粗糙度在有高光斜面/球参考补图上验证，不只比文件hash |

三个时间点作品为立方体Entity11/Material31/Sun21/独立实体相机，端点X=0/2、roughness=.2/.8、Sun=1/2 W/m²，12.5为1/.5/1.5，fps24/1。GUI真实创建/录键、seek、改base/键、Undo/Redo、V5另存重开、API等价作者及独立同帧离屏仍按J1.1第7节全部执行，不能用fixture取代作者链。具体相机/ROI/clip/网格文件在010模块运行前固定于独占验收manifest，不在看到失败图后更换。

ImageFrame冻结source/evaluatedTime/frameId、camera/settings/backend/color身份、完整stage清单，width/height、TopDown、Progressive、左上半开display/data窗口、pixelAspect1:1、PlaneSchema名称/角色/FLOAT32/stride、associated/instant/encodingOwner与只读payload。PNG和EXR必须解码核真实数值/尺寸/标签，不能以合法文件字节签收。

## 6 三个资源profile（施工准入，不是硬件认证）

本机首个样本为Windows x64/Qt6.8.3/MSVC2022、RTX3050 Laptop/驱动610.47、RAM16487870464bytes；GL4.1必须由产品实际context核对。以下资源是本切片事前施工预算，尚未被产品测量验证；不把既有采样最大改称瞬时峰值/最低硬件。不据此缩减原稿容量或全部高级效果。

| profile | 本切片准入和保留 | 计量/释放 |
| --- | --- | --- |
| BasicAnimation-J1 | 继承基础候选distinct resident1GiB/单任务extra512MiB；现10k pose节点/40k同步preflight visits；新scalar计数至少兼容V4展开9000曲线/300000键，完整原稿候选65536/2000000分阶段验证，不拿旧100k名字改计量拒绝 | 按真实共享拥有者去重统计author/历史/求值；候选失败/取消释放私有结果，原正式数据/历史不变 |
| JointPreview-J1 | 首个4实体/单材质/单Sun/无贴图作品1280×720/DPR1，CPU进程内额外512MiB、可归因GPU附件/网格/材质/颜色额外128MiB预算；内容支持规模不被此验收fixture替代 | GPU查询及WDDM采样分列；帧/资产/context失效后仅释放本消费者资源，销毁及fence回执才能归还配额 |
| Offline-J1 | 同步单帧原生GL4.1同质量，独立1280×720目标，CPU额外512MiB/GPU额外128MiB，临时+成品磁盘单帧64MiB；E盘写前至少2GiB余量。没有worker/并行/持久恢复，不申请其配额 | 固定来源保留到编码/文件核验完成；取消不发布，清理只影响本次私有暂存，GL实际释放与磁盘产物分别计量 |

逐次准入按author/Undo/plan/state/CPU cache、GPU资源、磁盘分别合取；不能把共用Scene/AssetManager重复相加。J1新增事务先在既有主线程原子保留/失败释放；全局后台调度留AUTH-JOB票，不虚构已实现。profile未知字段使用未测标记，不填0；进程树/去重shared/瞬时峰值/全部VRAM未知不能签完整资源认证。

产品测试记录实际reserve、peak采样口径、取消/退出和GL fence/对象销毁回执。退出不等于恢复基线；重复加载/取消后差分增长和30分钟耐久留对应性能票。当前数字只控制新增切片，不对所有Cycles/AOV/4K/媒体/模拟施加此cap，不使用样本wall作公平后端速度排名。

## 7 精确文件所有权与验收入口

008唯一产品写入者由L1指定；两个子代理上限不为施工扩容。root持G0/正式日志/契约，新实现者未获租约前不写产品。每张后续票再展开路径，不能依靠目录通配符扩大范围。

008允许既有文件：`src/core/Animation.h`、`src/core/Animation.cpp`、`src/core/EvaluatedPose.h`、`src/core/EvaluatedPose.cpp`、`src/core/Scene.h`、`src/core/Scene.cpp`、`src/core/SceneNode.h`；`src/renderer_gl/InstalledPose.h`、`src/renderer_gl/InstalledPose.cpp`；`src/editor/SceneAnimationSession.cpp`、`src/editor/SceneViewModel.h`。最后一个头仅承载共同session/context生产者成员，不改API实现；若需AnimationReplay.h或AssetManager.*先登记租约差异。

008拟新增且必须有真实调用者：`src/core/AnimationProperty.h/.cpp`（typed地址、时间、注册域）、`src/core/SceneIdentity.h/.cpp`（持久域/typed ID及共同来源）、`src/core/SceneMaterial.h/.cpp`（最小作者PBR/槽/Sun类型）、`src/core/EvaluatedSceneState.h/.cpp`（完整状态生产者及检查）。不先建空目录、万能注册框架或全后续子系统。

G0登记新源仅改`src/core/CMakeLists.txt`；已用rg核对既有测试位于`tests/AnimationTests.cpp`、`tests/InstalledPoseTests.cpp`、`tests/AnimationSessionTests.cpp`、`tests/AnimationPreparedReplayTests.cpp`。优先扩展这些已注册入口；若新独立契约测试有必要，仅`tests/CMakeLists.txt`由root注册。构建和测试租约串行，临时/TEMP/TMP/TMPDIR在独占E盘；Git由既有隐藏wrapper执行，禁止弹窗或关闭当前窗口。

成功标准为008共享类型真正被Scene/prepareAnimationPose/InstalledPose消费并可编译，完整状态失败无部分发布；保持unit动画数值/历史回放与editor staging/CAS/取消/观察回归。GPU pose回归用于涉及实际float消费变更，010/011真实像素门不提前用旧成绩签收。未验项明列，不用定义了结构就称共享类型接线完成。

## 8 授权、证据和撤回

AUTH-DATA由用户直接批准：稳定ID/typed属性/时间求值、新材质灯作者数据、V4→5旧稿只读升级另存和API0.2→0.3；wire1、同用户本机管道、默认关闭、拒绝提升、明确文件根、应用线程串行不变。008只用其中核心数据部分；012再核版本占用/schema并安排迁移专家，不把这份合同当最终格式已发行。

本轮用户专项回复“批准上述本地接入与输出”覆盖固定OCIO2.5库/自有配置本地接入及同步PNG/FLOAT32 EXR输出。AUTH-JOB/SEC/GIT/产品分发许可仍未取得；010/014仍须008和相应接线依赖完成，不能仅凭授权抢先施工。专家按用户已接受Sol6.1/max普通备用，请求值可记录、provider最终路由未独立核验。原版“fast未核验”的事实不被改写。

005/006已由后端实验报告独审及其文档验证签收。本合同经未参与作者的Sol独立专家复核，无阻断008的P0/P1/P2设计项；root文档/源保护检查通过后冻结007，在执行台账登记008唯一owner与精确租约，不修改初始任务JSON或旧历史。修改共享语义须新版本/差异，不偷偷改已冻结含义。

初版007为文档交付；008源码实施由唯一产品owner另行验收。本次合同修订不改产品/CMake/系统。撤回：保护后续依赖后，使用本次隔离目录中的contract-v1.md精确恢复初版合同，台账/progress只追加撤回，不覆盖无关修改。

## 9 修订记录

J1-CONTRACT-2仅补足authorContextIdentity的原live修订域。现有ApiDocumentState的revision是局部单调值，Scene复制及未来V5重开保留作者UUID；两个独立live文档可能有同UUID/同revision而作者不同。独立Sol专家确认revisionDomain是兼顾真实live→frozen与跨evaluation session比较的最小补足；不把该元数据当活handle或额外授权，不引入内容hash占位。

实现需覆盖：同UUID/revision但不同域拒同源；同域不同evaluation session/ID/purpose仍同源；Frozen继承context可与原live同源；空域以及live域不符拒绝。SourceIdentity的合同build标签随本版更新为J1-CONTRACT-2/double-affine-1。其余数值路径、图像门、容量、授权及文件所有权不变。初版冻结SHA8D9D862FDA3C5AC4E21B2363BABC5C9B9E9376A46CD43EF2AA762E2F77724BCB，原字节保存于E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/product-core-20261011-96433832/contract-v1.md；不改旧007审查回执。
