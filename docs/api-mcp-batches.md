# 有界原子批次 API

M6两种批次已通过本机集成、准备分配故障、独立审查与官方SDK真实闭环，详见[验收证据](validation/api-mcp-m6-20261006.md)。统一合同为api/schema/m6.schema.json，4合法/19非法机器样例见m6-examples.json；实际能力以system.describe为准。

## 两种同类批次

请求共同字段为document、expectedDocumentRevision、items；timeoutMs及桥拥有的会话/序号沿用既有规则。items为1…64项，总准备候选估算最多64MiB，不接受任意method数组。MCP调用者不能设置桥拥有的会话/序号。

| 方法 | 每项完整字段 | 结果 |
| --- | --- | --- |
| batch.createEntities | primitive=empty/cube/sphere/plane、name、parentId、transform、surface；字段与entity.create同义 | 普通command结果；created.entityIds按输入顺序，affectedEntityIds包含新对象 |
| batch.setTransforms | entityId、完整transform（space=local） | 普通command结果；affectedEntityIds仅实际改变的明确目标，按输入顺序；全同值no_change且零历史 |

create的parentId只指已存在对象或0，不接受批内别名、前向引用、批内父子DAG、相机、灯或自定义网格。setTransforms的目标ID不得重复；这是本体领域验证，不用JSON uniqueItems假装保证目标唯一。父子可同批，按整批最终overlay验证所有目标及受影响后代的真实逆父链世界矩阵和几何边界；不能逐项先验证中间状态。

## 原子性与历史

应用线程串行执行：离线准备全部值、资源、结果ID与唯一历史命令，统一预分配实体表/父children容量；整组来源、身份、父节点、索引、状态预检通过后才安装。一批只形成一条共享历史，选择不变、通知只在完整提交后发出；Undo/Redo使用精确before/after，不重新执行算法。

禁止beginMacro+逐setter+失败undo，也禁止替换整个Scene。任一可恢复校验/准备/最终许可失败不得改变可见节点、几何、选择、history index/count/cleanIndex、redo分支、dirty或revision；内部高水位空洞允许但不得把失败ID返回。全同值不删redo或移动保存点。进程崩溃/断电/Qt内部分配耗尽不是跨进程持久事务承诺。

## 已确认Core接口

Core唯一作者与主代理确认以下接口，不另建猜测实现。Scene嵌套BatchPrepareFailure为InvalidArgument/NotFound/UnsupportedTransform/LimitExceeded；两个prepare末参数为BatchPrepareFailure* failure=nullptr，失败明确分类，API不解析中文。

- PreparedEntityBatch：entityIds()输入顺序、estimatedBytes()、affectedWorldMatrices()（新ID/最终世界矩阵）；prepareEntityBatch(const vector<EntityCreateOptions>&, string&, size_t maximumItems, size_t maximumCandidateBytes, BatchPrepareFailure* = nullptr)。canInstallPreparedEntityBatch/ installPreparedEntityBatch/removePreparedEntityBatch。
- TransformBatchItem{EntityId entity; Transform transform;}；PreparedTransformBatch：entityIds()请求目标输入顺序、hasChanges()、estimatedBytes()、affectedWorldMatrices()（目标与受影响后代最终矩阵）；prepareTransformBatch(const vector<TransformBatchItem>&, string&, size_t maximumItems, size_t maximumCandidateBytes, BatchPrepareFailure* = nullptr) const。canInstallPreparedTransformBatch/installPreparedTransformBatch/restorePreparedTransformBatch。
- canInstall接受const Prepared&，const成员函数；install/remove/restore接受Prepared&，返回bool。全部来源/状态/容量检查先完成，成功后的移动/swap/赋值不分配。批次不改几何来源token/mesh revision。TRS真正改变旋转时只在准备归一化，同值旋转保持精确值。

创建采用整组staging node handles和已有父children的完整after缓冲，统一实体表容量；TRS保存受影响节点/外部祖先的准确before/after来源，有限预算在每个缓冲增长前核算。Core只核验TRS与矩阵；API用准备的最终矩阵和本体真实几何检查世界边界，不让Core依赖资产或渲染层。

第N项非法、最终准备失败、父消失、旧来源、最终overlay溢出、连续Undo/Redo、dirty/redo/clean保护及唯一历史均为验收门槛。未提交/推送/发布或修改用户MCP配置。
