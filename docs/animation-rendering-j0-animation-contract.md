# J0 动画合同草案：身份、时间与编辑事务

版本：J0-ANIM-DRAFT-1 · 2026-10-09 · 票据：EXEC-002 · 状态：Draft / PendingSignoff。

本稿提出 J1 所需的 J-CON-01/02/04 最小合同，供 EXEC-003 会签、EXEC-004 统一和 EXEC-007 冻结。它是文档交付，不是共享接口批准或产品实现证明。现有代码事实与拟新增语义分开列出；只有冻结后、取得对应施工授权的字段才进入产品。

依据为 J1.1 第 3–5 节、EXEC-002/009 和第 7 节，以及原稿 `animation-rendering-j1.0-source.docx`。已直接读取原 DOCX 的 `word/document.xml`，SHA256 为 `BBB4E6AD7186C2B1E1F5B9AE1EB851B78CA4052DE39C0AF008571D208364D73A`。下文 B 编号为正文 block、R 编号为表格 row；原公式仍以 DOCX 为准。

## 1 本轮边界与现有事实

J1 成功标准是位置、roughness 和一种已声明单位的灯强度能沿同一作者、求值和消费链录键、替换、删除、取样；帧 1/12.5/24 有明确数值；取消、no_change、历史和过期拒绝保持一致。

| 当前事实 | J1 草案新增或保留的边界 |
| --- | --- |
| `FrameTime=double`；持久键帧是 uint32；fps 为整数 1–120；取样支持有限 1–100000 子帧 | 持久键时间拟采用 binary64；帧率拟采用有理数。J1 时间域仍为 [1,100000] |
| `(EntityId, AnimationChannel)` 绑定完整 XYZ 的 Position/EulerXYZDegrees/Scale；仅 Constant/Linear | 新属性采用 typed 地址和标量曲线；沿用 Constant/Linear。Bezier、Graph、NLA 属后续票据 |
| `EvaluatedPose` 只含完整 TRS 数值，文档身份在 `InstalledPose::identity` | 拟增加完整有效属性和共同 `SourceIdentity`；不能把现有 pose 称作完整新状态 |
| 正式 `SceneAnimation` 是不可变快照；编辑、草稿和 API 共用提交与历史 | 保留这一事务入口，新增属性不另建 Undo 栈 |
| `MaterialAsset` 只有 baseColor、纹理引用、doubleSided；资源 AssetId 是 AssetManager 生命周期内的键 | 持久 MaterialID、实体材质槽和 roughness 尚未实现；运行期 AssetId 不能直接冒充持久身份 |
| 单方向 `LightComponent::intensity` 是 binary32，默认 0.72F，域 [0,10] | 这是 legacy 强度。新 Sun 物理字段另登记，不从 0–10 推导 W/m² |

本轮只修改本文件。代码、格式、API、配置、执行台账和 progress.md 由对应 owner 后续处理。本次请求配置为 `gpt-6.1-sol / max`，未另要求 fast；provider 最终路由没有独立核验接口，不能将请求值写成已核验值。

## 2 J-CON-01：稳定身份与 typed 属性地址

### 2.1 一份身份登记

以下均为语义类型草案，不是已存在的 C++ 定义。数字 ID 为非零 uint64，带 kind，由持久 `DocumentUUID` 限定作用域；0 仅作明确的无引用值，不作属性 owner。API 边界继续用十进制字符串传递 uint64，不能经 JavaScript Number 舍入。名称、列表序号、内存地址、GL 句柄均不参与持久身份。

| 类型 | J1 责任与关系 |
| --- | --- |
| DocumentUUID | 持久文档域，拟新增；与当前 API 的运行期 DocumentHandle 区分。重开不能靠同名文件沿用旧 live 句柄 |
| EntityID | 实体身份；Light 属性使用该实体 ID，并验证其具有对应灯组件，不另发一套 LightID |
| AssetID / MaterialID | 一份持久资产表；MaterialID 是 kind=Material 的 AssetID 的 typed 引用，不另建第二份材质分配器。运行期加载结果通过映射引用它 |
| MaterialSlotID | 实体持久材质槽身份，指向 MaterialID；与 Action 的 SlotID 是不同类型。重排不改变 ID，槽序号不是地址 |
| NodeID / SocketID | 同一材质图登记中的节点/插口身份；Socket 引用明确包含 MaterialID、NodeID、SocketID。J1 固定 PBR 字段不要求先实现任意节点图 |
| ActionID / SlotID / CurveID / KeyID | 最小动作、类型化绑定、标量曲线和键身份；各类型不能互换，迁移和 Redo 恢复同一 ID |

已发布 ID 不复用，Undo 不回退分配器 high-water mark。候选可私有分配，提交才发布；失败不返回或消耗正式可见身份。复制实体默认复制该实体 owner 的动作并重绑定，显式 linked 才共享实体动作；共享 MaterialID 仍是同一材质 owner，其材质动作保持共享，显式复制材质才产生新的材质 owner。Redo 使用保存的 ID。相同数值可以存在于不同 kind，但 `(DocumentUUID, kind, ID)` 重复或错类型引用须整笔拒绝。依据：原稿 B0242–B0247，以及 EXEC-003 的共享材质复制合同。

### 2.2 地址、注册项与绑定

```text
PropertyAddress = (DocumentUUID, OwnerRef, PropertyRef, Component)
OwnerRef         = Entity(EntityID) | Material(MaterialID)
PropertyRef      = RegisteredField(PropertyID)
                 | RegisteredSocket(MaterialID, NodeID, SocketID)
Component        = Scalar | X | Y | Z       [按注册类型验证]
RegistryEntry    = ownerKind + valueType + authorPrecision + unit
                 + creationDefault + validDomain + supportedInterpolation
```

`PropertyID` 是登记的 typed 字段标识；下面的点号名称仅是它的固定外部拼写，不提供任意字符串反射路径。字段查询必须解析到唯一正式地址，并核对 owner kind、字段能力、分量、单位和引用。不存在的 owner 返回 NotFound；已知但未支持的字段/插值返回 UnsupportedFeature；错误类型或数值返回 InvalidValue。协议错误码在 EXEC-004/012 统一映射，本稿不改 wire。

roughness 在 J1 的规范地址为 MaterialID + `MaterialField::PbrRoughness` + Scalar。以后若材质图把某 Socket 登记为该输入，必须显式解析到同一规范 PropertyID，不能在字段和 Socket 两处保存两条互相覆盖的同义曲线。Socket 的作者默认、连线来源和 effective 值分别可读；连接生效时，默认录键不能被伪称为画面已经变化（原稿 B0093/B0094）。

动作最小模型按 B0241/B0242 保留：一个 Action 持有按目标类型分开的 Slot，Slot 持有标量 Curve；Curve 用 `(PropertyID, Component)` 唯一定位，键保存 KeyID、binary64 time/value 和出射插值；OwnerBinding 指向 ActionID/SlotID。owner 的地址在绑定后才解析，不能把一次绑定的 EntityID 固化成所有共享动作的隐含目标。J1 不预建 NLA、Driver、rig 或任意属性反射体系。

### 2.3 J1 三个属性与具体地址

假定持久文档域为 D，立方体 EntityID=11，材质 MaterialID=31，Sun EntityID=21。单位和默认值属于注册合同，不能根据 GUI 控件范围隐式决定。

| 规范地址实例 | 类型、单位及创建默认 | 合法域和求值 |
| --- | --- | --- |
| `(D, Entity(11), TransformField::Position, X)`，外部拼写 `transform.position` | scalar binary64 作者/采样值；XYZ 组合为 Vec3；scene unit；新对象位置默认 (0,0,0) | 原值有限且可供现有 binary32 TRS 消费；保持现有右手 Y-up、local-to-parent 位置语义。scene unit 不自动等于米 |
| `(D, Material(31), MaterialField::PbrRoughness, Scalar)`，`material.pbr.roughness` | scalar binary64；无量纲；新材质创建默认 1，glTF 保留源值/规范默认 | 有限 [0,1]，Constant/Linear；越界拒绝，不静默钳位；物理 roughness 到 BRDF 内部参数的映射由渲染合同确定 |
| `(D, Entity(21), LightField::SunIrradiance, Scalar)`，`light.sun.irradiance` | scalar binary64；单位 W/m²，垂直入射辐照度；新 Sun 创建默认 1 W/m² | 有限且非负，Constant/Linear；只适用于本阶段 Sun；实际后端转换须 checked，不能溢出或冒用 legacy 强度 |

Sun 单位、L=1 W/m² 及 roughness/Sun 创建默认已由动画与渲染双方在本轮确认，仍须 L1 随共享合同正式冻结。W/m² 是渲染侧 RGB 模型的参考辐照度尺度，不将 RGB 分量之和称为真实光谱总功率；PBR 灯颜色的 scene-linear 色权重与组合规则由渲染合同冻结。动画只在上述已登记强度上求值，不另定义辐射模型。用户显式设置的作者默认可以不同于创建默认，不从当前帧取样重新生成。

旧字段独立保留 `LightField::LegacyDirectionalIntensity`：既有 binary32 正式值、默认 0.72F、域 [0,10]，单位为 legacy 无量纲增益。它与 SunIrradiance 不是别名；没有经确认的转换策略就保留旧字段或明确拒绝该升级，不自动赋予物理单位。J1 作品使用新 Sun 字段，不依赖这项换算。

### 2.4 author、采样与 effective 值

正式 author 值/默认值、动作键值、Draft 值和 effective 值各有明确来源。已有基础 entity 读取继续返回 base；作者查询返回原作者值与键；取样返回 evaluated 值、是否来自曲线及其 SourceIdentity。无曲线时取对应 owner 的正式作者默认/基础值，不从当前画面或另一 Action 重新 seed。迁移 Euler 缺轴语义见第 5 节。

完整状态至少包含本阶段全场景变换、材质槽到 MaterialID 的绑定、支持属性的有效值、Sun 设备参数与只读资产持有者；同一次发布一起替换。已支持属性的渲染、观察、拾取、bounds 和输出从同一结果消费，不能重新取 live Scene 的 roughness/灯值补洞。未知或缺失必需结果导致整次失败，不把 base 当作新成功结果。

作者键、时间和 scalar effective 值保持 binary64。现有基础 TRS 保持 binary32 声明精度；运行期 `Transform`/矩阵和后端参数需要 binary32 时，在消费边界校验原值、转换值和实际矩阵/设备结果，转换后须有限且满足相应合法域。roughness 不以舍入把原值 -1e-12 接受为 0；位置不以 float 舍入接受超范围原值。float 派生值禁止回写作者键、默认或取消快照。精确端点返回存储的 binary64 值；图像和矩阵比较使用冻结的容差，不能把它们当作作者精确相等的替代。

## 3 J-CON-02：时间、求值结果与来源

### 3.1 时间和插值

J1 的 `FrameTime` 为有限 binary64，域 [1,100000]；关键帧 time 拟采用相同类型，允许 12.5 等持久子帧。原稿 A04 的 0/负帧目标保留给后续合同差异，本切片不放宽当前域。播放范围不会删除键或限制其他合法 seek；改变 fps 不移动任何作者键。

`RationalFps=(numerator,denominator)` 拟采用正 uint32，约分为 gcd=1 的规范形，J1 数值范围沿用 1–120；用 checked 整数验证，不先算 float fps。旧 fps n 映为 n/1；本夹具为 24/1。相对于明确原点帧 f₀ 的时间为 `seconds=(frameTime-f₀)×denominator/numerator`，本夹具 f₀=1，因此帧 1/12.5/24 分别是 0、23/48、23/24 秒。FrameTime 表示作品帧，不混作秒或墙钟时间。

同 Curve 的键时间严格递增，按声明类型精确相等判冲突，不用 epsilon 合并相近键；+0/-0 在精确数值比较中相等（且 J1 均不在合法域内）。键以 KeyID 定位；移动与重排不改 ID。批量最终撞时刻默认整笔拒绝；replace 必须明确且只能替换允许替换的目标，不能在两选中源撞时按迭代顺序丢一个。

Constant 使用左键的出射插值：`t0 <= t < t1` 取 v0，精确 t1 取 v1。Linear 对内部时间取 `u=(t-t0)/(t1-t0)`、逐分量 lerp(v0,v1,u)；精确键和端外 Hold 直接返回原键值。不得近键吸附、改写连续角或在访问下一帧时补写键。J1 仅承诺 Constant/Linear；Bezier 的原稿公式 B0260 为 `Bx(u)=requestedTime ⇒ value=By(u)`，留给后续曲线任务，不能提前给其实现计分。

循环与取样分开：V4 迁移保留 legacy 半开周期（1–49 的周期为 48，显式 seek 49 仍取端点）；新作品采用包含末帧的 frame-step 策略，末帧区间 hold。离线帧集合必须显式声明 inclusive start/end/step，不从播放周期推导输出数。具体持久字段随 EXEC-004 的单一迁移清单冻结。

### 3.2 共同 SourceIdentity

`SourceIdentity` 为动画结果和渲染 IR 共用的来源类型候选，以下是 J1 必须表达的语义；序列化字段名与布局由共享 owner 一次冻结。

候选字段为 `documentUUID`（UUID128）、`sourceKind`（LiveFormal/LiveDraft/FrozenFormal）、`liveHandle`（现 DocumentHandle）或 `sourceBundleID`（由 kind 决定，不同时借用两种来源）、`contentRevision`（uint64）、`resourceSetIdentity`、`frameTime`（binary64）、`rationalFps`、`algorithmBuild`、`evaluationProfile`、`purpose`、`authorContextIdentity`、`evaluationSessionID`、`evaluationID`；LiveDraft 另带 `draftIdentity={draftID,generation,baseSource,sessionRevision}`。输入快照由只读持有者保持，不把运行期内存地址序列化成身份。

| 身份部分 | 必须覆盖的内容 |
| --- | --- |
| 正式来源 | 持久 DocumentUUID；live document instance/handle；contentRevision（适配现有 documentRevision）；不可变作者快照。冻结离屏来源有独立 sourceBundle identity，不用正在编辑的 live 版本替换 |
| 资产来源 | 被使用的稳定 ID、版本/内容 hash 和真正的只读字节持有者；仅文件路径或 AssetID 不足以固定实际内容 |
| 时间与算法 | 精确 FrameTime、规范 RationalFps、算法 build、evaluationProfile 和 purpose；依赖求值的作者语境。J1 不靠隐含的当前时间解释请求 |
| 实例来源 | 独立 evaluationSessionID、单调 evaluationID；Draft 如被取样，另带 draftID/generation、base source 和会话版本，不能冒用正式状态 |

SourceIdentity 不是一个脱离输入的任意 hash。Live 发布前重新核对输入版本、必要会话身份和请求取消代次；过期则拒绝发布。输出持有的冻结来源与 live 内容变更分开：正确冻结的旧作品可以继续输出，但仍必须标明旧 bundle 的完整身份，不能改贴新 live revision。跨 preview/output 核对同源作品时比较冻结作者、资产、算法/profile、语境和精确时间，不要求两个独立会话复用 evaluationID；同一已发布状态的各消费者保留该状态的完整身份。视口新 paint、GL contextGeneration、视图/尺寸和输出帧身份另由消费者记录；只有 SourceIdentity 相同不足以证明产生过新图像。

请求语义为 `source + frameTime + rationalFps + purpose + profile/build + statePolicy + cancellationEpoch`；结果包含 `status + sourceIdentity + evaluatedTime + completeSceneState + diagnostics`。这不是新增 API 方法目录。J1 采用无状态 direct，任意访问 24→1→12.5→1 的同源取值相同，纯 sample 不修改游标、选择、历史或已安装画面。

| 结果分支 | 内容与发布合同 |
| --- | --- |
| Succeeded | 只返回完整只读状态，身份、时间、数值、设备和资产来源相符；已校验的缓存复用仍报告真实来源 |
| NoChange | 只用于通过最后守卫的编辑/控制命令；第 6 节规定历史和会话行为。纯取样相同值不冒称发生内容提交 |
| InvalidValue / InvalidIdentity | 字段、单位、重复身份或最终求值值非法；无新完整状态，无部分发布 |
| StaleSource | 文档、内容、资产、语境、Draft 或取消代次不再匹配；拒绝旧候选改贴新来源 |
| Cancelled | 提交/发布前接受取消；释放本次私有资源，保留原正式内容和历史 |
| UnsupportedFeature | 类型、属性、插值或状态策略尚未实现；拒绝正确不表示该功能已完成 |
| LimitExceeded / Failed | 超出本次预算或无法形成可消费结果；带稳定 owner/地址/时间诊断，不用旧姿态伪造新成功 |

失败时若保留上一次成功画面，必须显示其旧时间/身份和失败原因并停止推进播放时钟；正式输出不得给旧图贴上新帧成功标签。隐藏、未请求的节点不能掩盖完整场景错误。现有 V1 预算仅是已有门，新增材质/资产/离屏预算在 J-CON-08 冻结，不把现有 40000 次预检预算外推给全部新后端。

有状态未来接口仅声明边界：replay/verified_cache 须绑定初态或检查点、输入历史、dt/substeps、seed、算法/profile 和资源版本，并提供实际重放区间；独立会话不得共享 live 可变模拟实例。J1 未实现这两种策略时返回 UnsupportedFeature，不能用 direct 结果代替。完整持久 Bundle 和 RenderJob.Ready 属 EXEC-004 的作业合同，本稿不实现作业。

## 4 J1 固定数值验收夹具

正式作者默认显式设为位置 (0,0,0)、roughness=1、SunIrradiance=1 W/m²；它们在取样后不改变。三个属性分别设置 time=1 和 time=24 的 Linear 键，Y/Z 无动画，立方体无父、无额外约束，其他 TRS 为 identity。材质槽明确指向 MaterialID=31；灯 EntityID=21 是 Sun。尺寸、相机、色彩/OCIO hash 和设备 profile 由渲染合同冻结。

| 输入 FrameTime | X 位置（scene unit） | roughness | SunIrradiance（W/m²） | 结果要求 |
| --- | --- | --- | --- | --- |
| 1 | 0 | 0.2 | 1 | Succeeded；精确返回各自端点键值 |
| 12.5 | 1 | 0.5 | 1.5 | Succeeded；u=0.5，完整状态标明 time=12.5 |
| 24 | 2 | 0.8 | 2 | Succeeded；精确返回各自端点键值 |

上表为数学期望，不是当前产品已通过的结果。0.2/0.8 是相应十进制解析得到的 binary64；中点的 scalar 数值初始检查容差建议为绝对 1e-12，矩阵/图像阈值须在 EXEC-007 冻结，作者往返、取消和 no_change 不使用这项容差。

测试必须执行 24→1→12.5→1 并核对同源数值、每次真实新 paint 身份和作者默认未变；独立离屏 sample/session 不能借用 live evaluationID。若将左键改 Constant，则 1/12.5 取左值，精确 24 取右值。修改正式默认、修改键、Undo/Redo、另存重开和 GUI/API 等价创作属于后续联合产品验收证据。

## 5 旧 TRS 到最小新模型的映射

| V4 来源 | 映射规则 |
| --- | --- |
| `(EntityId, Position)` 的 vec3 键 | 同对象 Slot 下 `TransformField::Position` 的 X/Y/Z 三条 scalar 曲线；逐轴 double 值、原帧 time、出射 Constant/Linear 和端外 Hold 保留 |
| `(EntityId, RotationEulerXYZDegrees)` | `TransformField::RotationEulerXYZDegrees` 的三分量 raw binary64；qz×qy×qx 只用于派生旋转，不折叠 720.000000001，也不换 shortest-arc |
| `(EntityId, Scale)` | `TransformField::Scale` 三分量；保留符号，最小绝对值 0.001。Linear 最终区间不能穿零；Constant 正负跳变沿用现有语义 |
| 完全无动画/无 rotation 轨 | 不制造无用动作；无 rotation 轨仍使用原 binary32 基础 quaternion，不从显示矩阵重新生成 Euler。新部分轴动画缺轴从持久 owner defaults 取值，不能换 Action 就 reseed |
| 原整数 fps、时间与动画绑定 | n→n/1、整数键→精确 binary64；已有动画对象进入迁移 SceneTake Action 的各自 Slot，稳定 ID 映射固定，legacy 循环策略保留 |
| 基础 Transform、材质和灯 | 基础 TRS 仍保留 binary32 解码值；运行期派生单位化不能回写原 quaternion。旧材料资源 ID 经持久资产映射；legacy 灯不自动映为 SunIrradiance |

旧 3000 组轨/100000 组键展开为最多 9000 scalar 曲线/300000 scalar 键，新预算按 scalar 计数，不以改计数单位拒绝原合法容量。正式格式和 API 候选仍由 EXEC-004/012 一次登记；本稿不宣布格式 5/API 0.3.0 已生效。首次升级 Save As 保护旧稿；最大合法 V4 的实际保存字节/内存门仍须最终 serializer 证明。

取消、保存、tick 和 Undo 不再次 normalize、重选 Euler 解或烘焙当前预览。现有 `Scene::replaceNodes` 的 base quaternion 归一化与精确取消是不同问题；B0390 提出的严格 validate/adopt 路径尚属后续迁移施工，不因当前取消已修好就视为迁移已经精确保留非单位 q。

## 6 J-CON-04：编辑、Draft 与唯一历史

### 6.1 一次正式提交

所有 GUI/API 作者改动复用同一业务入口。先验证准入与来源，私有准备完整最终候选和受影响求值，随后执行最终内容/历史 CAS、必要会话 CAS、deadline、取消、Busy 和实际外部 guard；最后原子安装内容、唯一历史条目及对应结果，再通知消费者。候选失败前不得修改一部分属性再补偿。

内容守卫包含 live DocumentHandle、contentRevision、historyRevision、不可变 before、资产版本和 typed 引用。依赖当前编辑语境的命令还包含 sessionRevision、Action/Slot、帧、选择、Draft ID/generation 和手势状态；显式地址的纯内容命令不凭空要求无关选择，但不能跳过实际 Busy/来源守卫。同步 provider/guard 若重入，返回后重查，不能继续使用回调前的旧来源。

精确 no_change 按所有正式字段的声明类型和值、稳定 ID、引用、presence 与插值比较，不用 epsilon，也不以等价矩阵代替 raw 作者值；既有数值 `==` 语义保持，恢复快照不能另做数值规范化。720 与 0、q 与 -q、插值变化、不同 MaterialID 即使画面相同，也不属于 no_change。候选相等仍执行最后守卫；真正 no_change 不 push、不切断 redo、不改 clean/content/history revision，不消耗正式 ID。关闭一个相等 Draft 可以推进真实会话版本，但不能冒称内容已变。

Undo/Redo 恢复精确 before/after 内容与固定 ID，不重做分配或从新显示姿态反解作者值。历史版本随真实历史操作变化，不能伪装回旧 revision。若历史内容已恢复而当前预览重建失败，报告真实恢复结果并退出/标明不可用预览，不能保留来源错配的画面宣称全部成功。

### 6.2 Draft 和精确取消

现有 TRS Draft 的准入继续作为 legacy 路径：PreviewPaused、对象模式、整数游标、选中可见对象、普通编辑视图、至少一个授权通道，无冲突手势/提交/Busy；Playing、相机预览或不匹配来源均拒绝。支持 sample=12.5 不代表旧 Draft 已允许子帧。未来 typed 属性直接录键与新 GUI 编辑按已冻结地址/时间能力准入，不能暗中放宽旧路径。

Draft 绑定正式 base source、typed owner/属性许可、起始作者快照和 draftID/generation。预览只更新 Draft 有效状态；手势 Enter 仅保留草稿，完整确认才一次录键和一次历史。没有许可的通道不能因 G/R/S 操作被附带改写。

Esc 两级恢复：有活动手势时先恢复手势前的 raw Draft 值，保持完整草稿；无活动手势时撤销完整草稿并恢复原正式预览。正式 author 值、raw Euler、signed scale、原始基础 quaternion、历史/redo/clean 均保持原快照；不再次 normalize、Euler Filter 或重新生成键 ID。恢复被实际守卫拒绝时保持草稿并返回具体原因，不能声称已经取消。

旧基础对象 G/R/S 的取消直接安装原始 Transform 快照，继续保留实体通知。回归必须覆盖“复制→G/R/S→Esc→Undo/Redo”，比较原 quaternion 分量、raw 连续角、signed scale 与固定复制 ID；只比 world 矩阵不能证明取消精确。

### 6.3 可取消准备的边界

短命令沿用一次准备/提交。需要长准备时按原稿 B0418–B0421 声明 PreparedEdit：Preparing→Ready→Committing→Committed，另有 Cancelled/Expired/Stale/Failed；Preparing/Ready 只持有进程内私有候选，不改正式内容/历史/pose。token 绑定来源、内容/历史/必要会话、作者语境、Draft、规范参数、allocator 代次和 expiry。原稿初始 Ready TTL=30 秒，实际分片和预算由相应施工票验证。

最终提交前已接受取消胜出；进入不可分割提交后的 cancel 返回实际已提交结局，不隐式撤销。status/cancel 不被自己占用的 Busy 拦死，过期/重启 token 不复用。PreparedEdit.Ready 不等于持久 RenderJob.Ready。现有队列取消不能充当正在执行准备的中断证明；本草案也不声称这套长操作状态机已实现。

## 7 必须保留的反例与判定

以下均是后续实现的验收输入；本轮只做文本和来源核验，没有执行产品测试。

| ID / 输入 | 期望判定及不变量 |
| --- | --- |
| ANIM-CON-01：同文档同 kind 注册两个 EntityID=11 或 MaterialID=31 | InvalidIdentity/DuplicateID；整候选拒绝，原注册表与历史不变 |
| ANIM-CON-02：Entity(31) 与 Material(31) 混用；或材质槽序号当 MaterialSlotID | typed 引用错误，不能按数字碰巧相等接受；槽重排后的正式地址仍指同一材质 |
| ANIM-CON-03：一批命令两次解析到相同 Action/Slot/Curve/Key 或同属性同 time | DuplicateTarget；默认拒绝整批。显式 linked 的共享键只应用一次，不移动两遍 |
| ANIM-CON-04：准备基于 contentRevision=7，期间基础值/材质/灯变为 revision=8 | StaleSource/RevisionConflict；不安装旧 effective 值，不改贴 revision=8，不增加历史 |
| ANIM-CON-05：文档重开、换 Action/帧/选择或 Draft generation 改变后提交旧 token | 按实际依赖返回 StaleSource；同一文件名、EntityID 和帧相同不能授权旧 live 句柄 |
| ANIM-CON-06：roughness=-1e-12、1.000000001 或 NaN；Sun=-1、Infinity；位置原值超 float 域 | InvalidValue；不靠钳位/舍入成功，无部分材质、灯或 pose 发布 |
| ANIM-CON-07：fps denominator=0、time=NaN/Infinity/0、未知字段或 Bezier 请求 | 前两类 InvalidValue；time=0 超出 J1 域；未实现能力 UnsupportedFeature，不按 Linear 代做 |
| ANIM-CON-08：Scale 1→-1 Linear；删除键后产生跨零新邻接 | InvalidValue/ScaleCrossesZero；完整最终轨道检查，旧作者和历史不变 |
| ANIM-CON-09：复制对象的非单位基础 q、rawEuler=720.000000001、负 scale；G/R/S 后 Esc | 按原声明精度恢复快照；Undo 删除同一副本，Redo 恢复同一 ID，旋转圈数不丢 |
| ANIM-CON-10：已有 redo/clean 点时，精确 replace 同一键值；外部 guard 又拒绝 | 允许时 NoChange 且保留 redo/clean/版本；guard 拒绝时返回真实失败，不能跳过守卫 |
| ANIM-CON-11：同一端外/子帧按 24→1→12.5→1 访问；某隐藏节点求值非法 | 同源 direct 结果与访问顺序无关；隐藏节点非法时整次失败，不只返回所选立方体的伪成功 |
| ANIM-CON-12：取消旧 evaluation 后其结果才返回；资产同路径但字节已变 | StaleSource/Cancelled；无旧代次发布。冻结结果持有自己的字节和身份，不借 live 资产顶替 |

现有错误枚举里的 InvalidArgument、StaleDocument、RevisionConflict、StaleEvaluation 等与上表语义的对应由统一 API 票冻结；新增语义名称不是已发行错误码。失败诊断须保留请求/实际来源、稳定地址和时间，不能只给一条中文提示。

## 8 会签事项、证据与施工落点

动画侧提出的最小地址、标量精度、J1 时间域、SourceIdentity、author/effective 分离和事务不变量可供会签。双方已确认 roughness 默认 1/域 [0,1]、SunIrradiance 默认 1 W/m²/非负有限域及 legacy 独立语义；与 `animation-rendering-j0-render-contract.md` 的 SourceIdentity 字段候选和结果语义已对齐。精确布局/比较规则、BRDF 参数映射和后端实际数值域仍须统一 owner 在 EXEC-004/007 记录最终结论，不能由本稿独自冻结。

下一步最小动作是双方核对同一 MaterialID/MaterialSlotID/SocketID 登记、上述三地址及 SourceIdentity 后，把确认/分歧交 EXEC-004。共享头文件、V4 迁移 schema、API 错误映射、图像阈值和预算均进入对应后续票，不在这份草案中替代实施。需要改核心数据、接口、线程或权限时仍遵守其明确授权门。

### 技术依据（按需阅读）

| 依据 | 本稿使用的事实 |
| --- | --- |
| `src/core/Animation.h:26,42,50,63`；`Animation.cpp:61,65,140,189` | 当前 double 取样、整数键/fps、完整XYZ绑定、值域/重复实体检查及 Constant/Linear 端点 |
| `src/core/EvaluatedPose.h:29,41,61`；`EvaluatedPose.cpp:76,129,131,139` | 整体 pose 结果、binary32 消费转换、连续 Euler 的派生来源 |
| `src/core/Scene.h:89`；`Scene.cpp:111,130,140,147` | 不可变精确 before/after、来源 token 和安装/恢复守卫 |
| `src/core/EntityId.h:14`；`AssetId.h:14`；`src/assets/MaterialAsset.h:16` | 现有实体/资源身份的生命周期范围；当前材质尚无 roughness |
| `src/core/SceneNode.h:24,36`；`Appearance.h:35`；`Transform.h:17` | 现有 binary32 设备/基础值、legacy 灯范围、右手 Y-up/TRS 语义 |
| `src/renderer_gl/InstalledPose.h:30,41,61`；`src/editor/SceneAnimationSession.cpp:64,97,126,885,1019` | 当前姿态身份、来源/会话守卫、legacy Draft 准入与完整撤销 |
| `src/editor/SceneAnimationEditing.cpp:223,265,351`；`SceneAnimationApi.cpp:85,134`；`api/AnimationApiService.cpp:69` | 统一提交、no_change 最后守卫、重复目标与内容/会话 CAS |
| `src/editor/operations/AnimationDraftGesture.cpp:68,167`；`SceneViewModel.cpp:3009` | 手势先撤、完整草稿再撤；基础取消直接恢复原 Transform |
| `src/editor/api/ApiTypes.h:27,33,39,65,98` | 运行期文档句柄、内容/历史版本、当前结果和错误类型 |
| 原 DOCX B0054 R002/R003/R005，B0072–B0094，B0239–B0251，B0277–B0282，B0386–B0390，B0411–B0421 | 共同合同、状态重放边界、typed owner、动作最小模型、精度/旧TRS与事务；含直接核对的原表和 OMML B0260 |

文本核验覆盖 UTF-8 无 BOM/LF、源 hash、章节/三个合同/三地址/三帧/反例齐备、引用路径与行号存在、Markdown 结构。产品构建、GUI/API 运行、保存重开、像素和快照回归未运行；本轮证据不能替代这些后续验收。L1 在闭环后统一把实际文档检查结果追加 progress.md 和执行台账。

回滚点：本轮只新增本文件；经确认后用 `Remove-Item -LiteralPath 'E:\Mini3D\docs\animation-rendering-j0-animation-contract.md'` 即可撤回该草案，不影响产品或其他未提交改动。本轮没有执行此删除。
