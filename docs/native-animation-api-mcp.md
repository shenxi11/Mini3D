# 原生动画 API / MCP 使用说明

版本：0.2.0，2026-10-07。R4最终Debug/Release与官方SDK真实MCP闭环已通过，R5性能与交叉验收待完成；没有发布新安装包。完整边界见 [R4 合同](native-animation-r4-contract.md)，证据见[开发方案第12节](native-animation-development-plan-20261007.md#12-r4最终实施与验收)。

## 1. 接入与版本

主体和 MCP 适配器须使用配套 0.2.0；私有传输 wireVersion 仍为 1。旧 0.1 客户端会得到版本不匹配，不能混用。先显式启动本机自动化，使用该实例生成的受限 descriptor，不在参数或日志中复制 secret。

保持本机同用户、明确读写根、默认关闭以及原六类权限。14 个新动画方法中：查询需要 `scene.read`，动画定义编辑需要 `scene.write`，播放会话控制需要 `viewport.control`；截图另需 `viewport.observe`。普通运行不会自动启用 MCP。

以下示例使用 API 方法名。MCP 工具名转换为小写下划线并加 `mini3d_`，例如 `animation.getState` 对应 `mini3d_animation_get_state`。MCP 适配器自行管理 `clientSessionId` 和 `mutationSequence`，调用者仍必须保留内容与会话 CAS。

## 2. 正式内容和临时会话

基础对象变换、正式动画关键帧、显示预览是不同数据。`entity.get` 仍返回基础变换；`animation.sample` 返回正式动画的纯数值结果，不把结果安装到窗口，不推动播放或历史。

`animation.getState` 返回设置、模式、当前帧、循环、`sessionRevision`、`evaluationId` 和完整文档版本。所有 ID/修订使用规范十进制字符串；帧时间可为有限子帧，存储的关键帧必须为整数。

| 模式 | 可执行的主要动作 | 不能执行的主要动作 |
| --- | --- | --- |
| `base` | 基础编辑、定义编辑、开启预览、纯 sample | seek / play |
| `preview_paused` | seek / play、定义编辑、纯 sample、可信截图 | 基础变换、网格及修改器编辑 |
| `playing` | pause、正式定义查询、纯 sample | 定义写、seek、截图、保存 |
| `pose_draft` | GUI 草稿确认/取消、正式定义查询、纯 sample | 外部控制、定义写、截图、文件动作 |

没有预览时 seek/play 返回 `PREVIEW_DISABLED`；非法模式及真实交互阻塞返回 `BUSY`。实体相机只读预览不会阻止合法 play/pause。`pause` 成功后如带 `diagnostic`，表示停止已经执行，但姿态求值/包装发生故障；不能把它解释成“完全没有执行”。

## 3. 创建两圈旋转

先通过 `document.current` 获取真实文档和当前内容版本。假设旋翼实体 ID 为 `42`，一次写入完整 XYZ 组：

```json
{
  "document": {"instanceId": "<当前实例UUID>", "documentId": "<当前文档UUID>"},
  "expectedDocumentRevision": "<当前内容版本>",
  "onConflict": "reject",
  "items": [
    {"entityId": "42", "channel": "rotationEulerXYZDegrees", "frame": 1,
     "value": [0, 0, 0], "interpolation": "linear"},
    {"entityId": "42", "channel": "rotationEulerXYZDegrees", "frame": 49,
     "value": [0, 720, 0], "interpolation": "linear"}
  ]
}
```

调用 `animation.upsertKeyframes`。24 fps 下 1→49 的时间跨度为两秒，旋翼围绕自身 Y 轴转两圈。连续 Euler 不折叠；170→190 是 +20 度，170→−170 是 −340 度，不自动走最短弧。

`position`、`rotationEulerXYZDegrees`、`scale` 都写完整三分量组。插值只有 `constant` / `linear`；Constant 在下一键精确切换。Scale 各分量绝对值至少为 0.001，Linear 相邻分量不能异号；删除/移动键也会校验新邻接。

同一批中的重复目标整批拒绝，不部分提交。`onConflict=reject` 拒绝已有目标，`replace` 明确替换。五个定义写方法各形成一条共享历史；精确 no_change 不剪除 redo。

## 4. 指定帧可信截图

按以下顺序执行，每次写/控制都使用最新返回的文档与修订：

1. `animation.getState` → 获取内容和会话版本。
2. `animation.setPreview(enabled=true)` → 带内容 CAS 和 `expectedSessionRevision`。
3. `animation.setFrame(frame=25)` → 再带最新双 CAS。
4. `viewport.getState` → 核对 mode、frame、evaluationId。
5. `viewport.capture` → 带完整 document、`expectedDocumentRevision`、`expectedViewportRevision` 和 `expectedEvaluationId`。

截图必须来自准入后的真实新 paint，不是 sample，也不是旧缓存。捕获等待中内容、显示身份、循环会话或 Context 发生变化会终止请求。`STALE_EVALUATION`、`VIEW_CHANGED` 或版本冲突后重新查询，不复用旧身份。Base 的真实 evaluationId=0 合法，但不能由缺省字段猜出 0。

适配器核验返回 PNG 的尺寸/hash 和请求身份后才产生 MCP image content。仅 hash 正确不能证明帧正确。账本重放的控制结果是历史结果，其中 evaluationId 不能直接当当前截图凭据。

## 5. 方法与批次限制

查询：`getState`、`listTracks`、`readKeyframes`、`sample`。

定义写：`setSettings`、`upsertKeyframes`、`deleteKeyframes`、`removeTrack`、`moveKeyframe`。

控制：`setPreview`、`setFrame`、`play`、`pause`、`setLoop`。以上统一为 `animation.` 前缀。

轨道/键分页默认256、最大2048，页尾 next 字段显式为 null。续页须保留原完整文档和内容 CAS；内容变化后不能拼接两份版本的页。无轨实体的 `readKeyframes` 返回空页，不存在实体返回 `NOT_FOUND`。

单次定义批次最多1024项，sample最多256个明确实体，完整求值最多10000节点；正式定义最多3000轨、100000总键、10000单轨键。预算拒绝不部分发布。

sample 返回列主序16数的 worldMatrix、基础 quaternion/动画轨道来源。没有 Rotation 轨的实体不包含 raw Euler 字段，不应将缺省视为零旋转。

## 6. CAS、恢复与保存

定义写要求内容 CAS；控制额外要求会话 CAS。tick 不推动内容、历史或 sessionRevision，因此未自动停态的 tick 后仍可用 play 返回的会话版本 pause；自动终点、隐藏/故障停止或其它明确控制会让旧会话版本过期。

no_change 也须通过身份、CAS、deadline 和最终 guard。冲突后先重新查询实际状态，不直接无条件覆盖。连接结果未知时沿原 mutation 账本和 requestStatus 恢复，同一序号只能重发同一规范请求；不能用新序号盲重试可能已执行的写入。

正式动画保存到格式4；旧1/2/3首次升级需另存，不覆盖原件。预览会话、播放钟及姿态草稿不是正式文件数据。保存重开回 Base，raw double 关键帧值保留。中文 GUI 操作见[动画教程](native-animation-user-guide.md)，可打开[中立样例](../assets/samples/native-animation-neutral.m3dscene)并先另存副本；最终运行证据及未验环境见[动画验收](native-animation-acceptance-20261007.md)。
