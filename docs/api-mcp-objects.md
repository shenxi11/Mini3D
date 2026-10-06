# 显式对象、集合和设备 API

合同：API 0.1.0，`api/schema/m5.schema.json`。本文件描述 M5-01 合同；当前开发和验收状态见[实施状态](api-mcp-implementation-status.md)，运行期以 `system.describe` 为准，不凭静态 MCP 工具存在判断可用。

## 共同规则

写入提供 document 与 expectedDocumentRevision；外部会话/序号由适配器拥有。只在无待确认交互的 Object 模式执行，失败不取消用户预览。完整输入、候选、结果及历史命令先准备，再通过最终期限/取消守卫提交一次历史。no_change 不改版本、保存点或 redo 分支。

直接C++调用同样检查timeoutMs范围、可选会话UUID及非零写序号；不能通过绕过JSON解码来提交非法元数据。错误优先级仍为文档、版本、Busy，再参数。

MCP 名称由方法名机械转换，例如 `entity.setParent` → `mini3d_entity_set_parent`；每个工具仅对应一个领域动作。

## 对象子树

| 方法 | 输入与行为 | 返回与限制 |
| --- | --- | --- |
| entity.duplicate | 明确 entityId，完整复制子树；新根与原根同父，追加尾部，根名附加 Copy | 最大 2048 节点；entityIdMap 为 sourceEntityId→entityId，created/affected 为全部新 ID；不抢选区 |
| entity.delete | 明确 entityId，删除完整子树 | 最大 2048 节点；affected 为全部删除 ID；仅清理消失的选择，Undo 恢复本操作删除的原选择 |
| entity.setParent | entityId、parentId；0 为根；mode 仅 keepLocal（默认） | 保留精确局部 TRS，世界变换可变化；拒绝自身、循环、不存在父；不支持 keepWorld |

复制保留设备、外观、显隐和集合成员；导入资产仍共享资源，editable mesh 分配独立绑定并共享不可变内容，后续编辑不修改原对象。Undo/Redo 回放已经准备的身份，不再执行复制算法。复制的 Undo 只有在当前选择指向被撤销的复制节点时才清理，不恢复或抢其他选择。

## 集合

单层集合不参与父子和 TRS；每个对象最多一个集合。`collection.create` 创建空组，name 必填，visible 默认 true；`update` 一次更新 name/visible 中至少一个；`delete` 删除组但保留全部成员对象及父关系；`assign` 按 entityId 分配到 collectionId，0 表示取消归属。结果包含 collectionId；update/delete 的 affectedEntityIds 列直接成员，显隐对后代的影响通过查询反映。

`scene.getSummary.collections` 返回 collectionId/name/visible/entityIds。`entity.get` 和列表返回 collectionId，0 表示未归属；effectiveVisible 同时反映祖先及集合显隐，viewportVisible 还包含会话临时显隐。

## 相机与方向灯

`camera.create` / `light.create` 接收完整 name、parentId、local transform、组件和可选 visible，一次创建，不借 GUI 当前视图、选择或偏好填参数。`camera.update` / `light.update` 仅更新已经存在的同类组件；名字、变换、外观与显隐仍通过 `entity.update`。

- camera：fieldOfView 为度、范围 1～179；nearPlane ≥0.001；farPlane > nearPlane 且 ≤1,000,000。局部 -Z 朝前、+Y 朝上，缩放不影响设备朝向。
- light：color 三通道 0～1、intensity 0～10；方向由节点局部 -Z 表达。可存多个方向灯，但现有渲染只选可见灯中 ID 最小者；有灯且全隐藏时仅环境光，无灯时沿用场景级兼容光照。

查询中 camera/light 为组件值或 null，不把设备误报为几何。设备及集合仍沿用现有场景格式，不新增文档格式版本或多灯渲染能力。
