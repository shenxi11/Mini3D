# API/MCP 合同实施约定

日期：2026-10-06。合同版本 API 0.1.0 / wire 1；当前能力仍以运行期 `system.describe` 为准。[任务板](api-mcp-task-board.md)区分合同冻结、开发中和已验收，不能将合同目录当成所有功能可用。

## 唯一来源与限额

输入/输出定义位于 `api/schema/`。`methods.json` 的 `limits` 是运行期限额的唯一来源：CMake 生成 `generated/api/ApiLimits.h`，本体 `describe.limits` 与以后 adapter 的检查都使用这些值。修改合同须重新配置并验证，不能分别维护 C++、TS 和文档数字。静态 Schema 中的上限须保持与该合同一致。

正式适配器对白名单本地 `$ref` 做有限展开，使用 SDK 的验证器接口装配 Ajv Draft 2020-12 与自定义 `uint64-decimal`（规范数字串及 BigInt 精确上界）；不维护第二份 Zod 业务合同。M0 原型曾验证正则派生，M4 正式实现以 Ajv 格式为准。官方 SDK 的真实 stdio 建模闭环已通过，目标 Codex 宿主直接图片显示仍未验收。

## 网格五方法

`mesh.create` 输入完整对象属性、positions、逐面 indices 与可选逐角 attributes；索引只在本条请求内有效。返回输入索引到稳定 Vertex/Face/Corner ID 的映射。源为空、孤立点和开放边界按现有内核规则处理，不额外要求闭合。角颜色只要求有限，不套用对象 tint 的 0～1 限制；硬法线为非零有限向量，不要求预归一化。客户端不能指定稳定 ID、材质资源或 AssetId。

`mesh.getSummary` 明确对象与网格绑定，区分源/最终求值统计和局部边界。`evaluated.vertexCount` 为最终求值可编辑几何点数，不是角属性拆分后的渲染点数；源和最终求值分别受 10,000 点、10,000 面、40,000 面角限制。渲染缓冲及全部同时持有的候选副本另计入 64 MiB 候选内存预算；求值渲染拆点不是可编辑源 ID。

`mesh.readSourcePage` 只读 vertices 或 faces，按数值 ID 升序。默认 fields 分别为 `position` / `cornerAttributes`，`[]` 只取身份/拓扑；面角始终返回 cornerId / vertexId。下一页 cursor 绑定 document handle、文档版本、entityId、meshId、源 topology/geometry revision、domain 和实际 fields。版本或查询条件变化必须重查，不能拼接旧页。cursor 不是访问授权。

`mesh.extrudeRegion` 使用明确源面 ID、源版本与 local/world 位移。世界位移必须按对象及祖先的实际线性矩阵逆变换；不将带剪切的世界矩阵伪装成 TRS。零位移在身份/版本/组件验证后返回 no_change，保留 redo。

`mesh.insetFace` 仅支持当前内核的单个共面凸面与正局部厚度。挤出顶盖和内插中心面保留原身份；side/rim 由稳定 ID 差集生成，不能猜容器末尾。返回的面 ID 可以直接用于下一算子。

先准备源、求值、结果与历史，检查源/求值数量及全部候选内存后，才提交一次共享历史；不发布临时 Cube、不进入 GUI 跨请求模态、不抢选择。Schema 类型错误为 INVALID_ARGUMENT；数量/内存超限为 LIMIT_EXCEEDED；通过类型检查但拓扑/算法约束不成立为 INVALID_TOPOLOGY。实际内核不支持的能力仍可明确返回 UNSUPPORTED_OPERATION。

## 真实观察四方法

`viewport.getState` 返回实际有效 view/projection、位置、forward/up、预设、场景相机预览身份、着色/覆盖/穿透、临时显隐摘要及像素/DPR，不以保存用 CameraState 代替实际视图。

`viewport.setView` 先验证完整候选再改变状态；自定义 camera 强制 orbit，非 orbit 的同时指定会拒绝。持久相机变化走原 cameraChanged 回写，轴向/正交/着色/覆盖保持会话语义。`viewport.focus` 使用明确对象列表，不借更改选区实现聚焦；当前没有可见几何范围时明确失败。控制不取消用户正在进行的操作。

`viewport.capture` 精确匹配 expectedDocumentRevision 和 expectedViewportRevision。普通重绘不改变视图版本；每次实际 paint 有单独 frameId/contextGeneration 和输入 stamp。grabFramebuffer 可能再次绘制，必须验证对应的实际绘制结果与资源 readiness，而非仅检查图片非空。异步等待必须能超时、取消、关闭清理；不得使用生产嵌套事件循环或固定 sleep。

截图范围明确为 `gl_viewport`，包含视口自身覆盖层，不包含独立导航子控件、整个窗口或桌面。保留原始与输出像素尺寸、DPR、捕获/帧/上下文身份、实际 view、PNG 长度及 SHA256。私有 wire 中的 pngBase64 由 adapter 转成 MCP image content，不作为大段文本输出。

## 授权与完成边界

本机默认关闭、同用户命名管道、拒绝提升权限开启、启动时明确指定读写根、应用线程串行及隔离测试已获用户明确批准。远程服务、推送/发布、改用户 MCP 配置不在授权内。跨用户 ACL、目标宿主真实图片和最终部署必须另有实测证据；批准施工不代表这些验证已通过。
