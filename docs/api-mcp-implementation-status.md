# API / MCP 实施状态与内部 API 使用

实施开始：2026-10-06。代码基线：`5bfd37f8bcbadbd14d810d6bea0e7f50cfce4dc5`。本文件记录实际实现，不把[开发指导](api-mcp-development-guide.md)中的未来方法当成现有能力。

## 当前边界

用户已要求开始实施方案。M3已接入默认关闭的本机管道、身份认证、去重账本、受控文件和真实观察；普通启动不监听，没有新增建模线程、场景格式或任意代码执行入口。仅显式启动且指定实例描述文件和批准根后开放相应文件权限，详见[启动说明](api-mcp-local-bridge.md)。正式MCP adapter和官方SDK真实建模闭环已通过，启动见[mcp说明](../mcp/README.md)；Codex CLI 0.159.0直接图片盲验及最终包实际写入建模已通过，Desktop当前会话未实测，见[最终证据](validation/api-mcp-m7-20261006.md)。

首版范围沿用指导方案：仅本机、默认关闭、同用户权限、明确实例及文档、单一 Scene / QUndoStack。用户于 2026-10-06 明确批准按同用户命名管道、拒绝提升权限开启、启动时明确指定读写根、应用线程串行建模的边界实施和隔离测试；不包括远程服务、推送/发布或修改用户 MCP 配置。原型与正式服务分开；原型通过不表示 Mini3D 生产通信已验收。

## 本阶段结果

M1–M7首版本机单用户及Codex CLI适用范围已验收，现有47工具。最终三组CTest通过，完整editor同seed409通过/3跳过/40,544断言全过，TS46项/typecheck通过；最新Release主体不更新测试产物。最终成套ZIP、净PATH迁移、12包内依赖、官方SDK批次和CLI实际创建/内插/挤出/保存/Undo/Redo/重开/图像闭环通过，见[M7最终证据](validation/api-mcp-m7-20261006.md)及[包报告](validation/api-mcp-m7-package-20261006.json)。[M2](validation/api-mcp-m2-20261006.md)、[M3](validation/api-mcp-m3-20261006.md)、[M4](validation/api-mcp-m4-20261006.md)、[M5-01](validation/api-mcp-m5-objects-20261006.md)、[M5-02](validation/api-mcp-m5-modeling-20261006.md)、[M5-03](validation/api-mcp-m5-modifiers-20261006.md)、[M5-04](validation/api-mcp-m5-files-20261006.md)、[M6](validation/api-mcp-m6-20261006.md)保留各轮独立结果和限制，不累加不同轮次数字。Desktop当前聊天、跨用户及新机器仍未实测；编译裁剪开关未实施，不将运行默认关闭等同编译裁剪。

主体、Core 和编辑器测试目标构建成功；Core **29 用例 / 7,985 断言**、API/导航 **15 用例 / 420 断言**、受影响 GUI/历史回归 **42 用例 / 1,352 断言**全部通过。主体单独构建期间，测试 exe/PDB 未被更新。完整证据与已复现问题见[本阶段验收](validation/api-mcp-m0-m1-20261006.md)。

## 已落地接口

`api/schema/` 为输入与输出合同；`methods.json` 是已实现M1–M6的47方法目录，不是特定实例的权限/可执行能力宣告。运行期能力以 `EditorApiService::describe()` 为准。Core 仍不依赖 Qt 或 MCP。

| 接口 | 当前行为 |
| --- | --- |
| system.describe / document.current | 返回能力、文档句柄、版本、路径、脏状态和 Busy；不因用户正在交互而取消操作 |
| scene.getSummary / scene.listEntities / entity.get | 一致只读快照；返回 local TRS、列主序 worldMatrix、显隐和求值边界；分页上限 2048 |
| entity.create | 完整名字、父节点、局部 TRS、外观一起准备；一次历史；API 不抢选区 |
| entity.update | 同一对象多个属性全部先验证；一次提交；失败和 no_change 不污染历史 |
| entity.duplicate / delete / setParent | 明确子树复制映射/删除影响；最多2048节点；keepLocal精确保留局部TRS；不抢选择 |
| collection.create / update / delete / assign | 单层集合；显隐传播；删除集合保留对象；0取消归属，摘要/对象查询含集合值 |
| camera.create / update、light.create / update | 完整设备参数一次历史；查询返回组件；保留既有单方向灯渲染规则，详见[对象说明](api-mcp-objects.md) |
| history.getState / history.undo / history.redo | 使用 GUI 的同一个栈；Undo/Redo 同时校验文档和历史版本 |
| mesh.create / mesh.getSummary / mesh.readSourcePage | 完整显式网格、输入映射及绑定版本的稳定身份源分页，区分源和求值统计 |
| mesh.extrudeRegion / mesh.insetFace | 明确源面和版本，保留顶盖/中心面身份，一次共享历史且不抢选区 |
| mesh.makeEditable / transformComponents / bevelEdge / loopCut / deleteComponents / fillFace | 显式稳定组件/边/源版本；世界变换支持负/非均匀父链；返回准确算子身份，详见[网格说明](api-mcp-modeling.md) |
| modifier.setMirror / setSubdivision / apply | 完整参数/移除/应用；固定链、准确快照历史，summary区分真实源/求值并返回完整参数；apply后重取源身份，详见[修改器说明](api-mcp-modifiers.md) |
| file.save / saveAs / importGltf / exportObj、document.open / new | 当前路径显式覆盖、另存永不覆盖、完整导入单步历史、世界空间OBJ不改工程保存点；open/new默认拒绝脏文档，显式discard成功才替换，依赖逐项授权；详见[文件说明](api-mcp-files.md) |
| batch.createEntities / setTransforms | 1…64完整基础对象/局部TRS，整组最终overlay验证，一次历史、精确Undo/Redo与no_change零副作用，详见[批次说明](api-mcp-batches.md) |
| viewport.getState / setView / focus / capture | 实际观察、控制不抢选区，真实帧/上下文/版本及PNG哈希，支持异步取消/超时 |
| bridge.hello / requestStatus / cancel | 私有通信方法；指定实例new/resume、结果查询及best-effort取消，不是建模工具 |

ID 和版本在 wire 使用十进制字符串，禁止浮点 ID。`uint64-decimal` 是需由验证器实现的格式：仅规范数字串，最大 `18446744073709551615`。不能只验证字符串长度而忽略上界。

每次成功新建/打开更换 documentId；失败不更换。documentRevision仅计已确认内容，historyRevision覆盖push/Undo/Redo/F9替换及保存点；对象预览不计提交，游标与保存用相机有明确内容版本。viewportRevision、生产截图和上下文代际已在M3接入实际绘制，不从保存相机假造图像身份。

同值四元数从 C++ 或 JSON 查询原样回传时返回 no_change，不破坏 redo/保存点；只改位置、缩放或其他属性及其 Undo/Redo 均保留已确认旋转。新旋转仅准备时归一化，历史使用精确快照安装。局部值合法但世界矩阵/几何边界超出有限 float 范围时，entity/list 返回 UNSUPPORTED_TRANSFORM，而不是成功返回非法 number 或假空边界。

## 内部调用

通过 `MainWindow::apiService()` 获取当前窗口服务，不新建另一个 Scene 或 UndoStack。请求包含 `service.documentState().document` 和提交前的 documentRevision；历史操作还要提供 historyRevision。请求完成后使用返回的新版本，不能靠本地加一推算。

```cpp
auto& service = window.apiService();
const auto state = service.documentState();
mini3d::editor::api::EntityCreateRequest request;
request.document = state.document;
request.expectedDocumentRevision = state.documentRevision;
request.name = QStringLiteral("装甲占位");
request.primitive = mini3d::core::PrimitiveKind::Cube;
request.transform.position = {0, 6, 0};
request.transform.scale = {2.4F, 1.5F, 1};
const auto result = service.createEntity(request);
// result.value / result.error 为明确分支；失败不能凭中文消息猜错误类别。
```

所有调用必须在 ViewModel 的应用线程。JSON 边界统一使用 `ApiJsonCodec`，不在通信层再次实现编辑逻辑。`FileAccess::InternalTrusted` 只供应用内部和测试，不能由未来 RPC 字段决定。

窗口每次调用同步读取导航、GRS、框选、环切、F9 参数面板、游标放置、模态对话框和 closing；API 不轮询这些状态。Object/组件预览拒绝场景快照；外部领域写入还拒绝 Edit 模式和相机只读预览。

## 构建与针对性验证

普通编译保持只编主体；没有把 npm 或测试加入主体前置条件。

```powershell
cmake --build --preset debug-local --target mini3d_editor
cmake --build --preset debug-local --target mini3d_tests
cmake --build --preset debug-local --target mini3d_editor_tests
```

测试运行前给当前子进程设置已验证 E:/D: 任务目录的 TMPDIR、TEMP、TMP；Qt DLL 使用当前 6.8.3 kit。API 测试通过 `MINI3D_API_SCHEMA_DIRECTORY` 读取同一份合同样例，而不是复制测试数据。

本阶段测试标签为 `[editor-api]`、`[api-navigation]`、`[api][scene]`。正式验收证据和剩余项见[任务板](api-mcp-task-board.md)与 `progress.md`；未运行的真实宿主/跨用户条件不能算通过。

## 交接与回退

M1–M7及最终CLI建模/image门禁已验收，详细启动、权限、未知结果恢复和关闭见[运行说明](api-mcp-runtime.md)。当前没有持久安装用户MCP项；正式宿主接入或恢复牛高达作品是后续单独授权的使用任务。Desktop当前会话、跨用户/新机器未实测，适用范围须与最终结论一起披露。

本轮不提交、推送或发布。已保存 `E:/CodexTemp/20261006/api-mcp-implementation-a761d4/implementation-code.patch`，只含本轮既有源码/构建文件的差异，反向 apply 检查通过。撤回时先保存最新定向 diff，执行 `git apply --reverse --check` 确认仍可应用，再反向移除该补丁并移出本轮新增 API/测试文件；不得回滚此前规划文档或用户改动，不使用全仓 reset。`progress.md` 仅追加撤回说明。
