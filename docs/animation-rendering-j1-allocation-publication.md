# J1 提交身份发行修补

2026-10-11 · EXEC-012/P 验收修补 · 未验收

## 已观察问题

完整 V5 快照包含分配计数器，揭示了旧准备流程的实际缺口：最终拒绝的批量创建把 nextEntityId 从 2 推进到 4，取消导入把它从 3 推进到 7，虽未发布实体。真实失败保留在隔离 slices 的 `runs/20261011-164711342-TestEditor-72ac3380`，不得缩小失败快照绕过。

该修补沿现有 AUTH-DATA，不改鉴权、线程、协议版本或文件根。格式 5 持久化与 API0.3 的原补充合同保持；本说明不代表 EXEC-012 已完成。

## 最小施工规则

- 默认 Core prepare 成功即预约身份的既有语义保持，放弃成功准备的 ID 不复用。
- 真实提交入口另用具名 provisional 准备：batch 延迟实体计数器；single 有源网格时延迟实体和网格；duplicate 延迟实体和网格；import 延迟实体。复制材质槽继续沿首次安装发行。
- 不恢复任意水位，不回滚整个 Scene。最终守卫内另一笔合法编辑、成功 Core 预约、创建后删除留下的 spent ID 都必须保留。
- 首次安装必须核对原来源、token、父/源节点、容量及所用计数器起点。全部满足才发行并发布；不能仅检查 ID 当前没有存活对象。
- 待发行状态首次成功后永久关闭。Undo 不降计数器，Redo 恢复原 ID，不重开首次起点检查。
- single 增 canInstallPreparedEntity；import 补 canInstallPreparedSubtree。均在保留的最终守卫之后、pushHistory 之前返回结构化冲突，不能靠历史回放断言处理可恢复冲突。
- 不删除 prepare 的不可见容量/空网格占位；尤其不得清除被守卫内合法操作使用的记录。

## 精确租约与验收

唯一修补 owner 为 j1_provisional_allocation_fix。生产租约仅 Scene.h/.cpp，以及 SceneViewModel.cpp 的 createEntitiesExplicit、commitEntityCreate、duplicateEntityExplicit、importGltfExplicit 相应准备/安装检查。其他路径不得顺手变更。

测试租约为 PreparedBatchTests、PreparedEntityTests、PreparedSubtreeTests、PreparedNewSubtreeTests、BatchApiTests、MeshApiTests、ObjectApiTests、FileApiTests。新增用例使用 `[j1-allocation]`；全部仍由独立测试目标显式构建。失败/取消/拒绝使用完整 V5，成功 Core 预约/Undo 的既有业务内容与 spent 水位分别检查。

必选证据：原两项真实失败修后通过；single 源网格和带 mesh/slot 的复制拒绝保持全部状态；direct/wire 相关入口；守卫合法创建、仅 Core 预约及创建后删除时外候选冲突且 afterGuard 精确保留；首次安装、多次 Undo/Redo、重开不复用；默认 Core 预约回归不变。

实施者先保存精确 dirty 前像及编码，整包 Frozen 停写后才由 root 串行构建和动态运行；原 P 清单及失败记录保留，新修补作为明确后继，不改写原冻结证据。独立 Sol 审查修补及最终动态证据后才可关闭缺口。

撤回必须先核后续引用与当前 SHA，以修补独占前像定向恢复，不使用 Git reset 或覆盖无关 dirty；正式日志只追加撤回。root 持 CMake、正式 docs/progress 与动态验证。专家普通备用已获用户批准，provider/fast 未独立核验。

## 2026-10-11 限定切片验收结论

EXEC-012/P 身份修补已完成限定验收，本文开头的“未验收”是施工前状态，历史不改写。EXEC-012 整体仍待 A 作者服务/真实有理时钟及 B API/桥/MCP，不据此声明 API0.3、GUI作者或正式图像输出完成。

- Core 新增拒绝/发行/回放用例10例302断言通过；Editor同组12例2892断言通过，包含原 batch/import 的两个真实失败。Core场景回归77例67815断言通过；另用实际 serializer/cursor/native-scene/file-compatibility 标签运行18例565断言通过，先前两个无匹配标签不计覆盖。
- 扩大Editor回归先揭示成功导入 Undo 的旧预期仍要求 nextEntityId 回退（12→4）。成功操作已经发行8个身份，按合同应保留 spent；root仅在该成功用例显式验证连续发行、完整 allocationState 及保留 spent 的完整 V5 Undo 快照，失败/拒绝测试逐字不变。修后显式构建及回归exit0，123例24731断言通过，另1专用junction夹具跳过不计通过。
- Asset重新构建exit0，回归21例980断言通过，另1专用junction夹具跳过。独立Sol只读复核修补、成功Undo勘误及真实回执，无新增P0/P1/P2阻断；root亲读最终差异与回执。所有动态串行，sources前后无漂移，测试二进制SHA保持。

证据根为 `E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/product-slices-20261011-3f1dbe27`。原失败 `runs/20261011-164711342-TestEditor-72ac3380` 与扩大回归失败 `runs/20261011-171930103-TestEditor-9c7e7dff` 均保留。最终37文件清单为 `success-undo-static-0da1baf5dcd74662a97c2d2975fd359d/combined-final-manifest.json`，SHA `C567804169316E5497F000882786DB96E0893D00BCCD2706E97C1A31262B9FBF`；最终Editor证据为 `runs/20261011-172259997-BuildEditor-2e397e9e`、`runs/20261011-172354613-TestEditor-aa5c6416`，Asset证据为 `runs/20261011-172519483-BuildAsset-291583de`、`runs/20261011-172617072-TestAsset-d04f1b06`。

修补11文件前像/清单仍在 `E:/CodexTemp/20261011/mini3d-j1-provisional-17d7ab42`；root成功Undo测试前像为证据根 `before/FileApiTests.before-success-undo-c8d43071.cpp`，后继SHA `1B41EBEAE90F73343A9005D152E5E5B266D7533EFDC966CF196BB8BEB17F6557`。撤回先核后续引用和SHA，仅定向逆补丁/恢复相应前像；两日志只追加撤回。没有新线程/作业、安全放宽、系统安装或Git写。
