# J1 持久化与 API 接入施工补充合同

2026-10-11 · EXEC-012 · J1-PERSISTENCE-API-2

本文件补足 J1-CONTRACT-2 的持久化、作者服务和外部 DTO 落点，不修改其数值、色彩、图像及资源阈值。设计已经独立 Sol 专家只读复核；本文件不是产品验收结果。EXEC-012 按 P（持久化）→A（共享作者服务与时钟）→B（API/桥/MCP）三个顺序租约施工，三部分全部验收才可关闭。EXEC-011 未交付或共享文件未释放时不写产品。

## 1 授权及既有保护

沿用户直接批准的 AUTH-DATA：稳定身份、typed 属性、时间/求值、材料/Sun 作者数据，格式4→5只读迁移另存及 API0.2→0.3。wire1、本机同用户命名管道、默认关闭、拒绝提升、明确文件根和应用线程串行不变。不新增后台线程、持久作业、系统安装、安全放宽、提交推送或发布。

取消操作须精确恢复原始 TRS；连续 Euler、多圈值、固定复制 ID、唯一 QUndoStack、准备/最终 CAS、取消/期限/撤权守卫、no_change、文件别名保护及 NewOnly 最终竞争保护全部保留。typed copy/delete 尚未支持时明确 NotReady，不登记为本票完成。

## 2 V5 正式数据

固定顶层 `format="Mini3DScene"`、`version=5`，增加 `documentUuid`、`idHighWatermarks`、`allocationState`、`authorAssets`。旧运行期 glTF `assets` 独立保留，不把运行期 AssetId 当持久 StableAssetId。

| 数据 | 固定字段 |
| --- | --- |
| 六种 typed 水位 | `idHighWatermarks.{asset,materialSlot,action,actionSlot,curve,key}` |
| 三种既有分配计数器 | `allocationState.{nextEntityId,nextMeshId,nextCollectionId}` |
| 作者资产表 | `authorAssets` 显式记录 SceneMaterialData.assets；MaterialId 等于其 kind=Material 记录 ID |
| 材质槽 | `entities[].materialSlots=[{id,material}]`；C++ 节点集合名仍为 nodes |
| Sun | 可选 `entities[].sun={irradiance,color}`；缺失表示无 Sun |
| typed 动画 | `animation.properties={rationalFps,loopPolicy,actions,bindings}` |
| 动作 | `actions[]={id,actionSlots:[{id,ownerKind,curves}]}` |
| scalar 曲线 | `curves[]={id,property,component,keys:[{id,time,value,outgoing}]}` |
| 绑定 | `bindings[]={documentUuid,owner,action,slot}` |

V5 所有 uint64 身份、版本戳、水位及分配计数器使用规范十进制字符串，包括既有 mesh 子元素 ID；接受 `"0"` 或无前导零正整数且检查 uint64 上界，不接受 JSON number。格式版本、uint32 帧范围、fps 分子分母及小索引仍为 JSON number。V1–4 保留既有解码规则。不同 ID 类型域允许相同底层数字，不做跨类型全局去重。

作者值和子帧保持有限 binary64，不走 float 数组解析、不加 epsilon、不截断小数。原 float TRS、非单位四元数分量、legacy Euler 连续原值按声明类型精确往返；仅计算规范化、不回写。definitionVersion 创建为1、参数更新保持、持久化原样，不新增其 CAS 语义。

J1 作者只生产无纹理 PBR；遇到已声明非空纹理或不支持作者资源明确拒绝，不忽略后成功。UUID 是现有 Core 规范非 nil UUID，绑定必须同文档。旧文件在私有升级候选生成一次 UUID，发布/Save As 复用。新文档默认 InclusiveFrameStep，V1–4 迁移显式 LegacyHalfOpen。即使没有动作或 rational override，V5 也始终写 properties/loopPolicy；不得依赖 hasAuthorData 丢掉该字段。

### 2.1 唯一原子载入入口

decoder 与 SceneDocument 共用一个 Scene 完整作者快照入口，不建第二 loader：

1. 检查 UUID/材料，创建 fresh 私有 Scene(uuid)，typed 水位从0开始。
2. 复用材料 prepare/install，再由 replaceNodes 验证节点、mesh、collection、legacy/typed 动画、槽及引用。
3. 对全部源节点用 installTransformSnapshot 精确恢复已验证的源 TRS；不得用 normalized candidate 覆盖源数据。
4. 校验导入六水位不小于实际已发行身份；三个 next 非零且严格高于存活最大 ID，UINT64_MAX 是耗尽哨兵。
5. 此时才安装导入水位/计数器，全部成功后一次 move 发布。失败不改变当前 Scene 或解码输出。

不提前安装高水位去阻断历史身份准入，不靠存活最大值重建分配状态而复用删除过的 ID。三个既有计数器单独持久化，不修改已冻结的六字段水位结构。

### 2.2 预检与旧稿保护

V5 使用现有64 MiB/depth64 SAX 预检，新增明确版本路由；旧轨道仍受旧预算约束，展开 XYZ 与 typed 共享总计9000 scalar curves/300000 scalar keys，不能建立可叠加预算。容量扩大不属于本票。

旧稿保护阈值统一从 `<4` 改为 `<5`；沿原路径/别名、取消、撤权、最终 CAS 和 NewOnly 竞争保护。新增 V4→V5 回归，保留 V1–3；原件字节不变，另存后保留身份与作者精确值。现有 V1–3→V4 证据不能冒称已证明 V4→V5。

## 3 共享作者服务与历史

GUI013 与 API 使用 SceneViewModel Explicit 业务命令，API 不直接调用 Scene setter。

- 材料创建/更新复用 PreparedMaterialData，一次历史；保持 definitionVersion 语义。
- 材质分配：0槽创建一个，1槽保留 MaterialSlotId 改引用，多槽 UnsupportedOperation。
- 新增可信 PreparedMaterialSlots，持 before/after、origin/token、源节点/材料检查、水位及 installed/published 状态，提供 prepare/canInstall/canRestore/install/restore。Undo 不降水位，Redo 精确恢复已发布 ID，不放宽普通 setter 的“新 ID”准入。
- Sun 进入既有 EntityCreateOptions/prepareEntity 的互斥校验及输出，一次 prepare、一次历史；不能 createEmpty 后 setter。updateSun 只更新已有 Sun。
- AnimationEditing 增结构化 prepare/commitResult(label,request*)；旧 bool GUI 包装委托它。commitResult 保留既有准备来源、document/history、domain 与 watermarks 复核，再进入正式提交链，API CAS 不替代这些检查。最终走 commitAnimationDefinition(after,label,false,request)，复用 AnimationCommitScope 初始/最终 CAS、Busy、deadline、取消、撤权与 no_change 守卫，不从中文诊断反推错误码。
- no_change 不消耗 ID、不清 redo、不移动 clean 点。所有准备结果在成功发布后才返回新身份。

sampleProperties 使用完整 EvaluatedSceneState 和独立 Sample 来源，不安装 pose、不改 cursor/selection/session/history，不用旧 float TRS 取样接口冒完整有效属性取样。

### 3.1 唯一真实播放时钟

纯 helper 放现有动画时间层，并由 elapsedAnimationFrame 实际调用。设 D=end-start、rate=numerator/denominator：

```text
phase = fmod((playStart-start)+elapsedSeconds*rate, D+1)
Inclusive looping frame = start+min(phase,D)
Non-looping frame = min(rawFrame,end)
```

Inclusive 最后一帧驻留一个帧时长后回绕，单帧范围保持原帧；入口 frame>end 才重置到 start，frame==end 的恢复不重启。LegacyHalfOpen 保持原数值及入口 >=end 重启行为，不增加第三策略。旧全字段 animation.setSettings 若已有 rational override，显式同步为 fps/1，避免旧 override 掩盖速度更新；后续 GUI 范围编辑保持已有 rational fps，不建立第二时钟。

## 4 API0.3 最小目录和 DTO

仅新增下列11方法，不开放后续 NLA/Driver/Action 管理。create/update、typed upsert/delete 与 setRationalSettings 继承 MutationRequest；material.get 继承 DocumentRequest；typed read/sample 继承 AnimationQueryRequest。实体/设备写请求沿既有 EntityMutationRequest/DeviceCreateRequest，结果复用 MutationResult。持久 rationalFps 为 null 或 {numerator,denominator}，新 setter 必须提供非 null 对象。

共同地址：`{documentUuid,owner,property,component}`。owner 为 `{kind:"entity",entityId:"7"}` 或 `{kind:"material",materialId:"8"}`；component 为 x/y/z/scalar，property 使用现有登记字符串。J1 写操作仅 Position XYZ、roughness Scalar、Sun irradiance Scalar。

| 方法 | 请求增量 | 结果增量 |
| --- | --- | --- |
| material.create | 完整 parameters={baseColor,roughness,metallic,coverage}，仅PBR | command,materialId |
| material.get | materialId | state,material:MaterialDefinition |
| material.update | materialId,完整parameters | command,materialId |
| entity.assignMaterial | EntityMutationRequest,materialId | command,entityId,materialSlotId,materialId |
| light.createSun | DeviceCreateRequest,sun={irradiance,color} | MutationResult.createdEntityIds |
| light.updateSun | EntityMutationRequest,完整sun | MutationResult |
| animation.upsertPropertyKeyframe | address,time,value,outgoing,linkedEdit | command,keyIdentity |
| animation.deletePropertyKeyframe | address,time,linkedEdit | command,keyIdentity |
| animation.readPropertyKeyframes | address,afterTime?,limit | state,address,curveIdentity?,keys,nextAfterTime? |
| animation.sampleProperties | frame,addresses | state,frame,samples=[{address,value}] |
| animation.setRationalSettings | settings={rationalFps,startFrame,endFrame,loopPolicy} | MutationResult |

keyIdentity={actionId,actionSlotId,curveId,keyId}，curveIdentity={actionId,actionSlotId,curveId}。Upsert 返回结果键身份，精确时间替换保留已存在 KeyId；同值同插值 no_change。成功 Delete 返回被删键身份，未找到 no_change/keyIdentity=null；不接受调用者分配 key/action/slot/curve ID。

outgoing=constant/linear；已知 Bezier 或登记但本切片未开放属性 UnsupportedOperation，不转成 Linear；未知地址/非法值结构化参数错误。linkedEdit=rejectShared/editLinked，默认 rejectShared，沿已存在共享曲线限制。键按精确时间分页，续页冻结同一 documentRevision，分页和取样地址数复用既有页面预算并由 catalog 宣告。

材料/Sun 复用 Core 值域，irradiance 单位 W/m²，无隐式单位或色域转换。document.current 增持久 documentUuid，不替代 DocumentHandle；entity.get 增作者 materialSlots/sun；animation.getState 增有效 rationalFps/loopPolicy 并保留旧 settings.fps。新 rational setter 写 override、不移动 keys。

C++ describe/catalog、descriptor/hello、schema、MCP 同为 API0.3.0，wire仍1。MCP tools 继续机械生成自权威 catalog，不新增第二注册表。limits/version 如需变化，只改 cmake/ApiLimits.cmake 与 ApiLimits.h.in，不编辑生成头。

## 5 租约、验收与撤回

root 持所有 CMake、依赖/锁文件、正式 docs/日志及动态验证。每次实施者先保存完整前像、编码和 SHA，获得精确文件租约，整包 Frozen 后 root 构建；不消费 Writing 源码。构建/GPU/UI 串行、独立 E 盘 TEMP/TMP/TMPDIR，保留原失败。

P：Scene.h/.cpp、SceneSerializer.h/.cpp、NativeScenePreflight.h/.cpp、SceneDocument.cpp、SceneViewModel.cpp 及对应精确测试。SceneDocument.h/NativeSceneRead.h 仅因实际类型/注释需要再申请。A 在 P 释放后接 Scene、Animation、AnimationEditing、SceneViewModel.h、SceneAnimationEditing、SceneAnimationSession 和拟新增 SceneJ1Authoring.cpp。B 最后接 API/codec/bridge/schema/MCP；所有新增路径先登记，不以整目录授无限权限。

必须通过：

1. V5 精确往返 UUID、六水位、三个计数器、全部材料/槽/Sun/动作/绑定/键身份及顺序、原 TRS/Euler/子帧、rational optional 和 loopPolicy。
2. 删除最高 entity/mesh/collection 后重开不复用；Undo 后持久化的 spent typed ID 不复用；耗尽安全拒绝；首个 slot Undo/Redo 恢复原 ID。
3. 非法 UUID、重复身份、失效引用、非法时间/fps/值域、超预算、不支持资源失败无部分发布。
4. V4 原件保护及 V1–3 回归，覆盖别名/取消/撤权/stale CAS/最终竞争目标。
5. 作者命令单一历史及 no_change；直调/codec/bridge/MCP 初始和最终守卫，大于2^53身份规范字符串。
6. API0.3 全落点一致、wire1/权限边界不变。
7. 独立取样24→1→12.5→1；中点 Position.x/roughness/irradiance=1/.5/1.5，容差仍合同 abs≤1e-12，不改变安装状态。
8. 纯 helper 与真实时钟均测30000/1001、末帧驻留/回绕/end恢复/单帧/end100000及旧半开策略。

优先受影响 mini3d_tests、mini3d_asset_tests、mini3d_editor_tests 和 MCP build/typecheck/test，最终集成状态复核。GUI创作、离屏PNG/EXR及联合作品仍由013/014/015独立签收；全面最大容量与后续族不由本票关闭。

撤回以每子租约的精确前像和最终 SHA 为回滚点，先核后续引用/差异，再定向逆补丁或恢复该前像，不 reset 仓库、不覆盖无关 dirty；新文件移存唯一 withdrawn 名称，正式日志只追加撤回。专家请求为 gpt-6.1-sol/max/普通备用（用户已接受），provider最终路由未独立核验，不称fast生效。

## 6 施工拼写勘误

J1-PERSISTENCE-API-2 仅消歧真实原生文件拼写：既有 V1–4 的 JSON 节点数组是 entities，SceneDocumentData.nodes 是 C++ 名称。V5 保留 entities，不改名、不增加 nodes 兼容别名；第2节材质槽/Sun 的落点随此修正。其余字段、数值、预算、历史、守卫与11方法语义不变。

authorAssets 显式记录为 `{id,kind:"material",material:{id,definitionVersion,model:"pbrMetallicRoughness",parameters,textures:[]}}`，外层稳定资产 ID 必须等于内层 MaterialId；两 ID 与 definitionVersion 都使用规范十进制字符串。只生产空 textures，声明非空仍拒绝。V1–4 既有 assets/entities 路由不变。

第1版完整字节与冻结 SHA5D0AB50AE9A9E00F7225144E9EBEB5CACD9AAAE961EBCA621A5E59E5D877468A 保留在本任务 slices/before/j1-persistence-api-1-5d0ab50a.md。此前专家报告属于第1版设计；本勘误由root亲读原 serializer 的 entities 写入/读取路径后作出，产品和最终独审另验，不冒称第1版已含该具体 wire 拼写。
