# Mini3D J0 材质、几何与图像合同草案

版本：J0-REN-draft-1 · 2026-10-09 · 任务：EXEC-003 · Owner：渲染技术 Leader

## 1 业务结论与适用边界

本草案给出了 J1 三条真实通路需要的渲染消费合同：对象位置、材质 roughness、Sun 灯辐照度经过统一求值，进入不可变 IR，形成有明确来源的预览和固定尺寸输出。当前可以推进 EXEC-004 会签；本文件没有冻结公共头文件、格式、API 或后端，也不表示新增产品能力已实现。

合同范围为 J-CON-01 的材质/灯部分、J-CON-03 和 J-CON-07。地址形状、ID、结果状态、时间和来源身份与 EXEC-002 共用；正式共享定义由 EXEC-004 单一 owner 落笔。相机/光源刚性方向保留既有合同。节点系统、四灯型、变形、真实位移、运动积分、AOV/Crypto 和可靠作业仍按 J1.0 及对应后续票验收，本草案不缩减其范围。

依据是 `animation-rendering-j1.1.md` 第 3—5 节、EXEC-003/010/014 和第 7 节，以及 `animation-rendering-j1.0-source.docx`。主代理已核验原稿 SHA256：`BBB4E6AD7186C2B1E1F5B9AE1EB851B78CA4052DE39C0AF008571D208364D73A`。下文 B/R 定位指同一原稿的正文 block/表 row；提取文本只用于定位，公式、图和完整算法仍以 DOCX 为准。本票只读核对了相关原稿 XML；没有改写或替代原公式。

本票请求配置为 `gpt-6.1-sol / max`；启动配置与 provider 最终路由是两类证据，后者未独立核验。本票没有构建、运行 GPU、安装后端或修改产品。这里只定义草案和后续可执行验收条件。

### 1.1 J1 最小验收对象

一个立方体、一份可编辑的原生 PBR 材质、一盏 Sun 和一个实体相机。样例使用固定颜色配置、尺寸及设备 profile；第 1/24 帧分别为 X=0/2、roughness=0.2/0.8、辐照度=1/2 W/m²，第 12.5 帧的 Linear 结果为 X=1、roughness=0.5、辐照度=1.5 W/m²。X 的单位由共同单位注册表会签，本草案不自行把旧场景长度解释成米。

成功标准是作者值、有效值、真实新 paint 和同帧离屏图可沿同一身份追溯；保存重开、Undo/Redo、无效请求和取消不破坏来源。图像/数值阈值由 EXEC-007 在实际验收前冻结，不在失败后放宽。当前最小场景只覆盖既定联合验收的子集。

## 2 J-CON-01：持久材质、槽位和有效属性

### 2.1 共同身份与唯一地址

共同地址候选为 `documentUUID + typed owner + field/SocketID + component`，沿用动画侧定义。非零 uint64 ID 由 DocumentUUID 限定作用域并带 kind；不同 kind 不能混用。MaterialID 是同一持久资产表中 kind=Material 的 AssetID 的 typed 引用，不另建材质分配器。存储编码和分配策略由共享合同冻结；外部 uint64 继续用十进制字符串，不能经 JavaScript Number 舍入。

| 共同名称 | 本草案的含义 | J1 规则 |
| --- | --- | --- |
| EntityID | 稳定场景实体身份 | 对象/灯/相机都使用现有实体身份；复制分配新实体 ID，Undo 恢复原 ID |
| AssetID | 同一持久资产表的外部或内嵌资产身份 | 新持久语义不能直接等同于当前仅 AssetManager 生命周期内唯一的 `core::AssetId`；需迁移映射 |
| MaterialID | kind=Material 的 AssetID 的 typed 引用 | 保存重开和资源重新装配保持 ID；与 GPU 材质、glTF 数组索引、进程内 AssetId 分开；没有第二份材质身份 |
| MaterialSlotID | 某实体上稳定的材质绑定槽身份 | 面引用槽 ID；槽记录引用共享 MaterialID。原稿写作 MaterialSlotId，最终大小写由 EXEC-004 会签，含义只有一套 |
| SocketID | 材质图输入/输出的稳定身份 | 采用共同注册表；J1 的固定 PBR 字段不制造临时 SocketID |
| PropertyID | typed 字段的语义身份 | 后续 Socket 地址显式映射同一 PropertyID，避免同一 roughness 出现两套动画键 |

J1 地址实例：

```text
{ documentUUID: D, owner: EntityID(E), field: TransformField::Position, component: X }
{ documentUUID: D, owner: MaterialID(M), field: MaterialField::PbrRoughness, component: Scalar }
{ documentUUID: D, owner: EntityID(L), field: LightField::SunIrradiance, component: Scalar }
```

三个字段的固定外部拼写为 `transform.position`、`material.pbr.roughness`、`light.sun.irradiance`。这是共同地址的实例表示，不是已发行 wire/schema。位置地址、FrameTime 和 ResultStatus 的最终类型完全引用动画草案及 EXEC-004；渲染不另定义一套时间或材质 ID。J1 FrameTime 沿用有限 binary64 的 `[1,100000]` 帧域；RationalFps 使用约分正 uint32 的 n/d，J1 数值域 1—120，本夹具为24/1。

### 2.2 作者记录与槽位行为

MaterialDefinition 最小记录含 `materialId`、定义版本、材质模型、typed 基础参数、纹理绑定和来源。glTF 导入保存源资产身份/hash、材质索引及声明参数；导入索引是来源定位，不能代替持久 MaterialID。新原生材质不依赖 glTF 文件才能保存重开。

槽记录为 `entityId + slotId + materialId + explicitOverrides`。J1 作品只需要一个槽；稳定槽和有效绑定仍进入快照。显式实例 override 保存到槽绑定，继承原 MaterialID，不靠暗中复制共享材质获得“对象独立”。override 的 typed 地址由共同注册表登记；该作者能力未实现前，不宣称可编辑 override。

| 动作 | 确定结果 |
| --- | --- |
| 修改共享定义 | 所有引用该 MaterialID 的实例使用新定义；求值仍绑定同一文档版本 |
| 复制对象 | 默认共享 MaterialID；复制实体分配自己的槽 ID并原子重映射面引用；Undo/Redo 恢复已分配 ID |
| 显式复制材质 | 分配新 MaterialID，并显式改变指定绑定；不改变其他实例 |
| 重排槽显示顺序 | 槽 ID 和面引用保持不变，数组位置不承担身份 |
| 删除仍被引用的槽/材质 | 拒绝并定位引用；只有同一显式命令包含确定重映射时才可提交 |
| 重新导入/Apply/复制几何 | 保留或显式重映射槽关系；没有映射时失败，不退到白材质 |
| 保存重开 | 恢复 MaterialID、槽 ID、定义值及 override；重建资源和后端缓存不生成替代作者身份 |

无贴图是合法的“无绑定”，采样乘数为 1。已声明贴图、槽或材质找不到是缺引用错误，两者不能混淆。未分配材质的默认策略必须由作者/迁移明确记录，不能用它遮盖损坏的引用。

### 2.3 J1 参数域、单位和默认值（待共同冻结）

所有新增浮点作者值和采样结果均为 binary64；binary32 GPU 消费要 checked 转换并报告溢出/不可消费，不回写正式作者值。颜色的类型和语义独立于普通 Vec3。

| 属性 | 类型与语义 | 默认值及合法域 | J1 消费 |
| --- | --- | --- | --- |
| PBR.BaseColor | Color3，scene-linear Linear Rec.709 的反射率近似 | 新材质 `(1,1,1)`；各分量有限且在 `[0,1]` | 基础 BRDF；不把所有颜色类型统一钳到此范围 |
| MaterialField::PbrRoughness | Float64，感知 roughness，无量纲 | 新材质 `1`；闭区间 `[0,1]` | GGX 候选使用 `alpha = roughness²`；该映射进入后端版本身份 |
| PBR.Metallic | Float64，无量纲 | 新材质 `0`；`[0,1]` | 基础 metallic/roughness 模型；导入遵循源声明/规范默认，不套用原生默认 |
| PBR.Coverage | Float64，线性覆盖率 | `1`；`[0,1]` | J1 不透明表面使用 1；非 OPAQUE 模式在真实实现前返回不支持 |
| PBR.DoubleSided | Bool | `false`；导入遵循声明 | 剔除和两面法线语义一致 |
| BaseColorTexture | 可空 AssetID 及独立采样/色彩声明 | 无绑定 | RGB 从声明的输入空间解码；A 是线性 coverage |
| LightField::SunIrradiance | Float64，Sun 参考辐照度，W/m² | 新 Sun `1 W/m²`；有限且 `>=0`，不继承上限 10 | 垂直于光线的接收面所用辐照度尺度；相对入射由法线夹角处理 |
| Sun.Color | Color3，scene-linear RGB 色权重 | `(1,1,1)`；有限非负，本 J1 色权重域为 `[0,1]` | 与辐照度独立相乘；不由 RGB 推导光谱瓦特/流明换算 |

Sun 的单位用于 RGB 辐射度模型的参考光照尺度，不把 RGB 分量之和称为真实光谱总功率。J1 采用方向灯以保持既有设备方向合同；位置和缩放不改变 Sun 辐照度，光线传播方向是刚性朝向的局部 `-Z`，着色时“指向光源”方向是其反向。Point/Spot/Area 的单位与真实生产者在后续合同中分别冻结，不能复用此字段冒充四灯型完成。

roughness=0 和 1 都是合法作者值。数值实现对 BRDF 奇点的处理需在后端 profile 中明示并验证；不能把内部正则化写回作者值，也不能把 0 静默变成某个滑杆下限。NaN、Infinity、负 roughness、roughness>1 或负辐照度先拒绝，不钳位、不安装部分结果。

legacy `LightComponent.intensity`/`Lighting.intensity` 的默认 0.72、范围 0—10和 `SurfaceStyle.tint` 的 `[0,1]` 继续属于独立 legacy 字段。旧灯不自动换算成 W/m²，旧材质不自动改为物理 PBR；迁移记录明确的 legacy 模型或用户指定的物理参数。新的 HDR/emission/data Socket 按各自域登记，不能继承旧灯/tint 上限。（原稿 B0094、B0311。）

### 2.4 effective 值的生产者与消费规则

作者默认值、动画采样值、连线值、实例 override、最终有效值必须可区分。J1 的固定 PBR 字段采用：正式基础值 → 同一地址的动画采样（无轨则基础值）→ 已明确支持并登记的实例 override → 每实例有效材质记录。override 自身的作者/采样来源也必须可追溯；未实现时不能临时在 Renderer 中读取 live override。

后续连接 Socket 的默认值可保存和录键，但连接生效时最终输入取连线结果；UI 要说明默认值未参与画面。若允许 override，必须明确它覆盖的是哪一级，不能默认覆盖连接结果。本草案不为尚未实现的图求值增加第二套逻辑。（B0093、B1065。）

求值结果提供 `materialId + definitionVersion + slotId + effectiveParameters + provenance`；provenance 至少说明地址、基础/采样/连接/override 的实际来源与 SourceIdentity。相同 MaterialID 可以在不同实例产生不同有效值，IR 用实体/槽绑定区分，不能修改共享 MaterialDefinition 以保存一帧结果。均匀参数变动只更新参数资源；图结构版本变动才触发相应编译与原子替换。

## 3 J-CON-03：最小不可变 RenderSceneIR

### 3.1 来源身份和资源所有者

RenderSceneIR 只从完整成功的 EvaluatedSceneState 生成。后端消费求值结果，不解释曲线、约束、Driver 或模拟，也不反向修改作者数据。

| SourceIdentity 所需信息 | 当前可复用的来源 | 新合同缺口 |
| --- | --- | --- |
| 文档与应用实例身份 | PoseIdentity 的 `instanceId/documentId` | `documentUUID` 与现有文档身份的映射由共同 owner 冻结 |
| 正式作者内容版本 | `sourceRevision`，观察入口比较 documentRevision | 新候选 `contentRevision` 适配现有内容版本；不用它假冒独立材质/资产/定义版本 |
| 求值运行身份和时间 | `evaluationId/frame/mode`；观察携带 sessionRevision | 候选 FrameTime、rationalFps、evaluationSessionID/evaluationID/profile/算法身份引用 EXEC-002；不能把mode直接冒充新sourceKind |
| 定义与资源身份 | 已持有不可变编辑网格 content/revision 和 assets owner | 新材质定义、纹理字节/解码/采样、依赖资源集合版本/hash需明确纳入 |
| 合同与消费 profile | 当前数值/显示校验不能代替新增 profile | J-CON 版本、精度/质量 profile 与 snapshot hash 为候选新增，不能登记为已有字段 |

共同 SourceIdentity 字段候选与动画侧对齐如下，全部尚未进入产品：

```text
documentUUID: UUID128
sourceKind: LiveFormal | LiveDraft | FrozenFormal
liveHandle: 现有 DocumentHandle，或 sourceBundleID（由 sourceKind 决定）
contentRevision: uint64
resourceSetIdentity: 稳定资产ID、版本/内容hash及只读bytes持有者
frameTime: binary64；rationalFps: 约分正uint32 n/d
algorithmBuild / evaluationProfile / purpose / authorContextIdentity
evaluationSessionID / evaluationID
draftIdentity: id/generation/baseSource/必要session（仅LiveDraft）
```

这是一份共同来源语义，不能另外建立渲染专用SourceIdentity。最终字段布局、比较和过期规则由EXEC-004会签。预览候选安装必须核对当前内容、资源、必要作者语境/会话和取消代次；已冻结离屏请求比较自己的原快照，不因后来合法编辑而重新读取 live 内容。GPU context、paint frameId、view/尺寸属于独立消费者身份，不混进正式作者内容身份。

IR 持有 CPU 不可变资产所有者：网格拓扑/属性/槽映射、材质定义与有效参数、已解码纹理及原字节/色彩声明。只存裸指针、路径或 GL 句柄不足以证明生命期。旧 IR 被显示、pick、bounds 或输出持有时，不得覆盖其存储。GPU 句柄只属于后端 Context/generation 的缓存，不写入场景或 IR。（B0059—0060、B0381、B1054/R004、B1055。）

### 3.2 J1 消费字段

| IR 项 | 最小字段/语义 | 实际消费者 |
| --- | --- | --- |
| 帧头 | SourceIdentity、精确 evaluatedTime、合同版本、资源集合身份、单位/坐标 profile | 预览安装、离屏请求、观察身份与缓存校验 |
| 实例 | EntityID、parent identity、geometryStageId、完整 local/world affine、inverse/normal、orientation provenance、有效可见性和材质槽绑定 | 网格 pass、pick、bounds；新增 pass 同源 |
| 几何 | 不可变局部 position、拓扑、法线/切线、UV/颜色、面槽 ID、未变形 Generated/orco/texspace 及属性版本 | GPU 上传与空间查询；没有属性时按声明能力拒绝 |
| 材质 | 唯一 MaterialID、模型/定义版本、每实例 effective baseColor/roughness/metallic/coverage、doubleSided、纹理采样/解码身份 | PBR pass；不能再读 live node.surface |
| 灯集合 | EntityID、灯类型、有效颜色/辐照度、刚性方向、可见性 | J1 的 Sun 光照；未支持类型明确不支持，不选择最小 ID 掩盖其他灯 |
| 相机 | 实体或显式快照身份、刚性位置/朝向、投影/clip、目标尺寸、pixel aspect | 独立离屏与视口投影；不能继承输出时窗口 DPR |
| World/背景 | 明确的 scene-linear 背景、film transparency及已实现照明声明 | 最小样例可用固定黑背景/无环境光；不隐藏旧 ambient 常量 |
| 设置/颜色 | backend/build/device profile、采样/质量、color configuration身份与输出声明 | 缓存、图像和文件元数据；尚无实测时仍为候选 |

J1 采用一个表面阶段和无变形网格即可实现本作品；每个实例仍带真实 stage 身份。纹理最小通路是 baseColor，源规范 RGB 解码和 A coverage 分开；若请求 MR/normal/occlusion/emissive 等贴图而实现尚缺，返回不支持并保留作者数据，不把未支持资产退成灰色来签收。（B1061/R002—007。）

### 3.3 完整 affine 与几何 stage

候选矩阵采用 binary64、列向量和 GLM 列主存储；世界基沿现有右手/`+Y` 上、相机局部 `-Z` 前向约定，长度和导入缩放来自共同单位注册表。矩阵末行为 affine 的 `[0,0,0,1]`，非有限或不可逆结果不能交给需要 inverse/normal 的消费者。

```text
p_world = worldAffine * [p_local, 1]
worldInverse = inverse(worldAffine)
worldNormal = transpose(inverse(linear3x3(worldAffine)))
n_world = normalize(worldNormal * n_local)
```

这是本草案的消费约定，不替代 J1.0 的复杂算法公式。local/world affine、inverse、normal 必须来自同一求值结果。允许 shear、非均匀/负缩放；不为只读显示强制拆成 TRS。作者写回/Apply/Bake 的可表达性检查单独进行。摄像机和 Sun 的方向使用独立 rigid orientation/provenance，不能从镜像 affine 重新生成相反方向。checked binary32 消费需核对实际位置、八角 bounds、法线、方向和投影，不只检查矩阵元素有限。

负 determinant 的绕序、两面法线和切线手性同步处理；切线及法线规则遵循源规范，不能靠只保轮廓冒充材质一致。J1 没有 normal map 时不宣称已完成完整切线生成能力。

每个几何阶段最少记录 `geometryStageId/parentStageId/SourceIdentity`、控制拓扑和属性身份、对象 affine、算法/quality身份及不可变几何拥有者。未来真实 modifier/位移/细分把顺序、有效输入加入身份；依赖视图的细化再包含相机/投影/尺寸/view policy。J1 没有这些生产者时使用真实的无修改阶段，不制造“已位移”空壳。（B0063—0064。）

J1 的显示、pick、bounds、输出声明消费同一阶段；未来 shadow/reflection 消费同一渲染阶段。离线专属细化可与交互基础阶段不同，但必须在图像和 UI 明示；backend pick 需要对实际显示阶段作差分认证。不能更换顶点后沿用旧 stage 身份，也不能向自己的最终阶段反向求依赖。

Generated 保留未变形 `orco/texspace`；Object 坐标采用指定坐标对象的完整逆变换。shear 烘焙或后端坐标重定位保存原坐标属性和可逆映射，不从当前变形/烘焙后的 bounds 重算 Generated。J1 无程序纹理时可携带来源而暂不宣称真实消费；对应后端原型必须用局部坐标材质证明它们没有丢失。（B0059、B1057、B1146—1147。）

### 3.4 缺失、过期和安装结果

结果状态沿用动画侧语义候选：`Succeeded`、`NoChange`（仅编辑/控制）、`InvalidValue/InvalidIdentity`、`StaleSource`、`Cancelled`、`UnsupportedFeature`、`LimitExceeded/Failed`。它们不是已发行错误码，最终协议映射由 EXEC-004/012 单一 owner 完成。诊断必须包含 source、具体实体/材质/槽/资产或属性地址和失败阶段；missing_reference/resource_unavailable作为原因，不能另造第二套结果枚举。

| 情况 | 必须返回/安装的行为 |
| --- | --- |
| 完整成功 | 完整状态/资源 prepare → validate → 原子 install；每帧身份包括实际新 frameId |
| 精确 no_change | 作者事务不增历史/内容版本；渲染仍依据实际请求和新 paint 判断是否产生了新图 |
| owner/MaterialID/slot/必要资产缺失 | 无效引用或资源失败诊断；不发布部分 IR，不用默认色/旧灯掩盖；合法空绑定另行处理 |
| 来源、定义、资源或预览会话过期 | 来源过期；不安装候选。预览可显示有旧身份的上次成功图，观察不得冒充新帧 |
| 取消 | 停止发布本请求结果；已存在有效画面保留旧身份，作者取消保持既有精确恢复规则 |
| 未支持模型、灯型、纹理、非 OPAQUE 表面或数据组合 | 不支持并定位能力；拒绝成功不计该能力已实现 |
| GPU/解码/文件资源失败 | 共同失败语义及 resource_unavailable 诊断；正式输出没有新成功产物，不把旧图标新时间 |

显示失败不得混用新灯和旧网格。后端缓存键至少覆盖所用资源内容/版本、属性、几何 stage、实例矩阵、有效材质、灯、相机、时间、设置、颜色配置和设备 generation；图结构未变时保留参数更新通路。异步/并发实现不在本票，不能凭本草案改变线程路径。

## 4 J-CON-07：ImageFrame、PlaneSchema 和唯一色彩执行

### 4.1 图像与平面最小元数据

ImageFrame 是不可变像素结果和元数据，不是“QImage 看起来正常”的别名。plane 持有 CPU 像素或受生命期保护的后端存储；打包后文件 manifest 描述实际交付值。

| ImageFrame 信息 | J1 候选约定 |
| --- | --- |
| 来源/时间 | SourceIdentity、精确 evaluatedTime；实际 frameId；camera/view/settings/backend身份 |
| 几何来源 | 完整 geometryStage manifest及其hash，映射 EntityID 到实际消费 stage；多对象图不能只写一个 mesh ID |
| 像素区域 | pixel width/height、displayWindow、dataWindow；采用左上原点、整数半开区间 `[x0,x1)×[y0,y1)`；输出 EXR 时显式转换其文件窗口约定 |
| 几何比例/扫描 | positive rational pixelAspect；J1 为 1:1、TopDown、Progressive；GL读回翻转在边界执行一次 |
| 平面存储 | PlaneSchema 列出 channel name/role/dtype、layout/row stride、坐标/单位及 owner；payload尺寸与dataWindow一致 |
| 色彩 | workingSpace、transfer、colorStage、OCIO config/hash、LUT/ICC resource hash、processor参数身份 |
| alpha/时间语义 | alphaAssociation、coverage channel关联、timeSemantics；J1静态样本为 `instant`，不冒称 `shutter_integrated` |
| 编码归属 | encodingOwner和目标附件/文件格式、bit depth/色彩标签；标签说明现有编码，不再执行变换 |

displayWindow 是完整输出画布，dataWindow 是实际存储区域；crop 的空白不通过窗口/DPR猜测。J1 无 crop 时两者相同。尺寸、行向和窗口必须与读回/文件字节一致，元数据不通过只是声明“支持”。

| PlaneSchema 最小平面 | 类型/空间 | 可执行操作 |
| --- | --- | --- |
| Beauty.R/G/B | J1 诊断及离屏母版 FLOAT32、scene-linear Linear Rec.709、`transfer=linear` | 允许 >1 和颜色运算产生的负值；拒绝 NaN/Infinity；不作隐式 `[0,1]` 钳位 |
| Alpha | FLOAT32、线性 coverage `[0,1]` | 与 Beauty 关联；不套 OCIO/OETF，不当成第四个颜色分量 |
| display RGBA | PNG 候选为 UNORM8；RGB是明示processor输出的display codes，A是coverage | 打包/量化和标签，禁止第二次颜色编码 |
| Data/AOV/ID（后续） | 必须声明 data、单位/坐标、dtype、过滤与timeSemantics | 不走 Beauty 的 OCIO、曝光、去噪、alpha乘除或无条件平均；J1未实现时不支持 |

RGBA16F 是交互 HDR 附件候选，须实测 FBO 完整性、混合/过滤和数值范围；FLOAT32 母版不能未经验证缩成 HALF。Alpha 可与 Beauty 同一存储 plane，但通道角色仍独立。Depth/Position/Normal/Vector/ID/Crypto 的完整专用合同继续以 B1112/R004—008、B1114—1115 为准，本草案不提前认领其生产者。

### 4.2 scene-linear 与 OCIO 路径

J1 工作空间候选固定为原稿 Blender 5.2.2 配置的 `scene_linear=Linear Rec.709`、profile 2.5；`reference=ACES2065-1` 不表示内部 ACEScg。实际 config/LUT/ICC 集合必须通过依赖边界核验并以内容 hash 冻结；本票没有取得或安装这些后端资源，不能填造 hash。

J1 默认显示参数候选为 Display=`sRGB`、View=`Standard`、Look=`None`、Exposure=0、Gamma=1；配置中的实际名字和 CPU/GPU processor 语义须在原型中核实，再由 EXEC-004/007 锁定。不得以自制同名曲线代替固定 OCIO。AgX、Raw 诊断及后续参数使用同一色彩服务和完整身份，不再新增一份隐含 tone mapper。

```text
已编码颜色资产 --按声明输入解码一次--> scene-linear
线性数据资产 ---------------------> data（绕过颜色解码）
scene-linear 光照/透明/适用采样和合成
  ├─ 线性 EXR母版（不经过 Display/View）
  └─ 显式背景或透明文件策略 → 曝光及一个 OCIO processor → 打包 → UI覆盖层
```

颜色贴图/已编码媒体带输入空间与资源身份；baseColor RGB按源规范sRGB解码，alpha不解码。MR/normal等是线性 data，不能整图套 gamma。SceneStrip/GP/VSE 交换同一 ImageFrame/PlaneSchema；SceneStrip 返回未经 Display/View 的 scene-linear HDR，已编码媒体必须先按声明解码。（B0095、B1061、B1070—1071。）

默认 OCIO processor 已输出 display codes 时 `encodingOwner=OCIOProcessor`：GL_FRAMEBUFFER_SRGB关闭，PNG编码器只打包和写标签，不再执行 OETF。若未来输出 display-linear，必须改为一个明确的 OETF owner，并改变颜色身份；不能同时开两处。曝光只执行一次；CPU离屏和GPU预览使用同一processor含义。UI覆盖层在显示转换之后绘制，不混进HDR母版。

后端需提供未经隐藏 tone-map/clamp/OETF 的线性结果。后端内已有 tone-map 的结果不能重新标为 scene-linear 再套 OCIO；未能取得正确线性结果时阻断该候选，不能用“关后处理”的声明当成实验通过。

### 4.3 alpha 与输出政策

线性 Beauty 使用 `alphaAssociation=associated` 的关联/预乘表示，Alpha单独表示覆盖率；允许 `A=0` 且 RGB 非零的加法发光。禁止无条件除A、乘回、清零，也禁止用 epsilon 统一丢掉低A高能量像素。

| 消费目标 | 确定路径 |
| --- | --- |
| 默认屏幕预览/预合背景文件 | 在线性域 `RGB_out = RGB_associated + (1-A)×RGB_background`，再执行一次OCIO；背景和颜色配置进入身份 |
| 线性 EXR 候选 | 保存scene-linear FLOAT RGBA、associated语义和空间/窗口元数据；保留零alpha非零RGB；不执行Display/View |
| 透明 PNG 候选，A>0 | unpremultiply → 单次processor → straight RGB打包；A保留线性coverage；不再乘A，除非目标明确要求display premultiplied |
| 透明 PNG 候选，A=0且RGB非零 | 返回不可保真诊断，要求线性EXR、显式预合背景或显式Emission层；不得静默丢能量 |
| 透明 PNG 候选，A=0且RGB=0 | 输出约定的透明零色；此分支不能套用于发光像素 |

透明 PNG 是 straight-coverage 交付，非线性 View 后不能保证在任意背景上等价于 HDR 合成。PNG/EXR 的实际支持范围与依赖由输出票冻结；本票没有写文件或证明任一编码器可用。J1 不透明几何不等于图像 alpha 合同可以省略；未实现透明几何的组合保留 NotReady，色彩处理反例只证明该处理模块。

### 4.4 必须保留的 HDR/alpha 反例（待实际运行）

| 反例 | 预期，及被捕获的错误 |
| --- | --- |
| 线性 RGB `(4,2,1)`、A=1 | HDR母版仍为4/2/1，不能在RGBA8或隐藏tone-map处先截到1；显示可压缩，EXR不能保存显示结果冒充线性 |
| 颜色贴图编码值0.5与data值0.5 | sRGB颜色约解到0.214；data保持0.5，抓取通道误解码 |
| 线性0.5用明确sRGB诊断变换 | 单次sRGB编码约0.735；第二次编码约0.873，抓取processor/OETF重复；该解析检查不冒充AgX输出基准 |
| associated RGB `(2,1,0)`、A=0，黑背景 | 线性预览/EXR保留2/1/0的发光；透明PNG明确拒绝不可保真，不能清零 |
| associated RGB `(2,1,0)`、A=0.25 | straight转换先除到8/4/0，再processor；不能processor后再除A；Alpha不变 |
| 极低正A且RGB非零 | 保留并检查真实数值范围；无epsilon分支把能量清掉；若转换溢出给出诊断 |
| 父shear/镜像，Generated/Object材质与相机/Sun | 完整矩阵、坐标源和刚性朝向各自成立；只对轮廓不能过关 |
| 同帧改变材质/资源/颜色配置后复用旧图 | frameId/source/stage/color身份不匹配，应过期或重渲染，不因帧号相同接受 |

原稿还要求黑/白时间各半在线性域得到0.5而不是平均PNG得到的约0.214。J1只设 `instant` 图像；时域生产者完成后运行此反例，不据此宣称当前已支持快门积分。（B1111。）

## 5 当前作者/Renderer 接线映射

以下为本票实际读取的代码；“候选接线”不是已完成实现。技术证据行号以本次工作区为准。

| 当前入口与事实 | 候选接线与缺口 |
| --- | --- |
| MaterialAsset.h:16—20仅baseColor/texture/doubleSided；Material.h:17—21只有vec3基础色和纹理指针 | 持久MaterialDefinition与effective参数进入IR；GPU Material只是派生资源；需要真实roughness/PBR消费者 |
| AssetId.h:14—19是进程生命周期AssetId及mesh/material引用；AssetManager.h:26—46按路径缓存并持有材质 | 新持久AssetID/MaterialID映射由公共owner完成；不可把缓存键改名就当持久ID成立 |
| GltfImporter.cpp:284—289以索引+1关联材质，353—376只取基础色/双面/基础纹理并警告非OPAQUE退化 | 导入源MR参数及持久定义/槽映射；保留差异与不支持诊断，不沿用静默不透明退化签新能力 |
| Scene.cpp:582—587、1421—1427、1948—1951拒绝可编辑面非零material；1810起对象组件设置是另一路 | 面槽作者/迁移由公共票接入；J1单槽映射要落到真实几何，不能说已有多材质面能力 |
| SceneDocument.cpp:191—214载入通过path/meshIndex重建材质，263—274保存网格来源 | 新可编辑材质与稳定槽必须正式持久化；文件/API迁移由J-CON-05单owner实现 |
| InstalledPose.h:30—65原子持有PoseIdentity/numerics/geometry/assets；EvaluatedPose.h:28—38已有float world/inverse/normal和独立刚性rotation | 扩展统一完整有效状态；binary64候选不能声称当前float结构已满足；保持现有资源生命期与身份核对 |
| Renderer.cpp:208—254取ScenePoseView光照并用pose资源；299—360仍从live节点读meshRenderer/surface | 新消费者从同一IR取所有有效材质/绑定/灯/几何；旧legacy路径需有真实回归，不能混用新灯旧surface |
| InstalledPose.cpp:255—284从scene.lighting开始并选择最小可见ID灯；SceneNode.h:36—42为0.72/0—10方向灯 | 新Sun字段/有效灯集合独立登记；没有把旧值推导为物理功率；方向继续使用rigidWorldRotation |
| Renderer.cpp:372—440基础色乘surface.tint；mesh.frag:25—37是Lambert、固定alpha=1 | 基础PBR切片和alpha/输出服务需实际实现；新增结构不等于画面已接入 |
| mesh.vert:25—29以完整uModel及inverse-transpose转换法线；Renderer.cpp:323—328处理镜像绕序 | 继承实际完整affine消费，补typed矩阵/normal/stage身份及checked精度边界；无需先强拆TRS |
| GpuTexture.cpp:37—48转换RGBA8、垂直翻转、GL_RGBA8上传，未做sRGB/Gamma；TextureAsset.h:20—24无颜色声明 | 新增颜色/data解码声明与资源hash；行向只在边界翻转一次，HDR资源不能通过RGBA8截断 |
| ViewportWidget.cpp:594—617核对显示身份；681—701抓取后核对stamp；840—870记录真实frameId与资源状态 | 扩展观察到同一IR/stage/color身份；离屏输出独立目标尺寸/母版，不能直接把窗口截图包装为正式输出 |

## 6 后端未知点与会签清单

### 6.1 原型仍需给出的证据

| 未知点 | 最小证据与归属 |
| --- | --- |
| 固定OCIO config及资源可发行性、实际Display/View名字 | 固定内容/依赖hash、许可材料及CPU/GPUprocessor比较；EXEC-005/010与依赖owner，未核验前不能宣称同一AgX配置 |
| GL4.1/Qt生命周期、浮点附件、readback/alpha/颜色编码 | 本机独占原型查询FBO/能力并测真实通道，保持Qt自有FBO；不默认compute/SSBO；EXEC-005 |
| Filament内部HDR/透明、隐藏clamp/tonemap与TAA/DOF组合 | 真实组合实验取得scene-linear中间结果；“关后处理”不是证明；完整高级组合不提前由J1签收 |
| Blender/Cycles完整affine与局部材质坐标、身份 | 同一IR对照静态候选；helper parent或烘焙保留坐标/实例/原资产映射；EXEC-006；尚未选型通过 |
| Sun辐照度、roughness端点、BRDF映射及后端精度 | 固定单位和解析/像素fixture，公开backend映射/正则化及容差；EXEC-004/007/010 |
| 固定尺寸PNG/EXR、色彩标签/窗口/association | 解码实际文件核对像素和manifest，同源离屏；EXEC-014；不能仅检验合法文件字节 |
| GPU缓存/资源失效 | 同输入复用、参数变化更新、资产/Context generation变化失效及失败不安装；实现票验证，不在本票改并发 |

候选失败时保留具体原因；其他路线也必须通过同一语义门。静态图、库可用或设计审查不构成后端选型完成。

### 6.2 EXEC-004 必须会签的最小项

1. 共同ID编码与documentUUID映射、MaterialSlotID拼写/分配、typed地址和Socket→PropertyID映射。
2. roughness默认1/域`[0,1]`、binary64精度；Sun类型、默认1 W/m²/非负域、RGB色权重与legacy独立字段。
3. SourceIdentity的精确字段/比较规则、不可变资产拥有者与失败诊断；已冻结输出和当前预览的身份比较差别。
4. affine/inverse/normal、刚性朝向、长度/导入单位和stage的生产者及消费声明。
5. ImageFrame/PlaneSchema窗口/行向、scene-linear空间、associated alpha、`instant`时间语义和唯一encodingOwner。
6. J1实际输出格式和所用OCIO资源；缺失生产者保留NotReady，后端原型未完成保留未知点。

## 7 本票验证与交接

本票验收是草案完整、来源可追溯及两条线名称一致，不是产品编译或渲染验收。执行后检查：只新增本文件、UTF-8无BOM/LF、J-CON-01/03/07及共同地址完整、原稿B/R定位有效、上述当前源码位置真实存在；相关文字复核保留候选/已有/未验证的区别。

后续定向验收最少覆盖：持久材质/槽往返及复制Undo；roughness/灯值的作者→有效值→真实像素；缺引用/过期/取消不发布；完整affine与几何stage；单OCIO/HDR/零alpha发光；目标尺寸不随视口resize改变。实际数值/GPU/文件证据由对应实现票提供，本票不记录为通过。

本票唯一正式改动文件是本合同草案；progress.md、执行台账及共享会签由主代理串行记录。临时只读定位脚本位于本任务E盘独占render目录，不进入产品范围。回滚点是EXEC-003创建本文件前；主代理确认无后续编辑后，可只撤销本票新增文件，保留其他工作区内容。

### 技术依据（按需阅读）

原稿必要定位：B0054/R002—004及R008；B0058—0064；B0093—0095；B0380—0381；B1033/R002—009；B1035；B1054/R002—006；B1055—1063；B1065；B1070—1075；B1102—1103；B1112/R002—008；B1114—1115；B1146—1147。上述来源保留完整后续要求；本草案只提取下一条真实功能链所需字段。
