# M5-01 对象/集合/设备验收

日期2026-10-06；API0.1.0/wire1；基线5bfd37f8bcbadbd14d810d6bea0e7f50cfce4dc5。仅本机批准根/同用户/串行应用线程。使用合同见[对象说明](../api-mcp-objects.md)。

## 已验收结果

11个方法已实现：entity.duplicate/delete/setParent、collection.create/update/delete/assign、camera.create/update、light.create/update。共享目录和官方SDK两代发现32工具。复制上限2048，完整子树及old→new映射、独立editable绑定/不可变源共享；删除集合保留对象；keepLocal保持精确局部TRS；API不抢选择。设备不扩多灯渲染或场景文件版本。

Core和本体均先准备完整候选/结果/命令，最终守卫后一次历史；失败/no_change不改变栈、redo、保存点和版本。Undo/Redo恢复准备好的身份，不重跑复制。

## 最终证据

产物根 `E:/CodexTemp/20261006/api-mcp-longplan-3e6a91bc`：

- verify-m5-object.ps1 -Step Core：构建成功；22用例/3,023断言通过，包含6个PreparedSubtree用例及真实副本几何编辑/恢复、原source未变。
- verify-m5-object.ps1 -Step Editor：主体/editor_tests构建成功；73用例/5,528断言通过，包含14个object-api用例及final guard/Busy/2048边界。
- MCP独立build/typecheck/test：25测试通过、0跳过；11合法/16非法共享M5合同样例和逐项映射/输出检查；mock不作为真实本体证据。
- verify-m5-error-envelope-green.ps1：主体/editor_tests最终构建成功；31用例/3,488断言全通过，2现有环境用例跳过（无实际网络盘/无no-ACL卷）。
- mcp-real-m5-object-e2e.mjs退出0；`real-m5-object-1791276777273/evidence.json`：真实官方SDK2.3.1→stdio→本体，全部11方法、负/非均匀父变换、集合显隐/直接成员、keepLocal、no_change、循环拒绝、4节点复制映射、删除单步Undo/Redo、中文设备保存重开及旧document拒绝通过。自有应用PID退出，不结束用户窗口。

## 真实错误回归

首次真实调用合法循环拒绝本应INVALID_ARGUMENT，却收到OUTCOME_UNKNOWN。失败证据`real-m5-object-1791276323189/failure.json`保留。根因：finishMutation使用可变QJsonObject的operator[]读取缺失result，插入null，使错误与result并存，严格adapter拒绝。

新增真实QLocalSocket回归：red 44断言中3失败（初次错误/原序号重放/requestStatus）；修改为value读取后上述green通过。同时验证错误消耗序号、场景/历史不变及下一序号成功；未弱化客户端或让OUTCOME_UNKNOWN冒充预期错误。

## 独立审查和限制

补充typed元数据回归：交接发现直接C++调用可绕过JSON的timeout/session/sequence检查。新增11方法×4非法组合，red 662断言中44失败；共享validateApiMutation仅一行复用既有checkMeshEnvelope，最终`verify-m5-object-envelope.ps1 -Stage green`主体/editor_tests成功，63用例/6,052断言全部通过（object-api/editor-api/mesh-api/history-editor）。拒绝发生在候选前，保留document→revision→Busy优先级；Sol只读定点关闭。该修改同时统一已有entity.create/update的typed元数据语义，wire合同不变。最终真实SDK再次退出0，最新证据`real-m5-object-1791277356321/evidence.json`覆盖同一11方法闭环。

未参与实现的Sol/max两波只读审查覆盖Core+Schema/TS与最终VM/API/codec/对象测试，未发现新P1/P2；错误信封修复与真实pipe回归定点关闭。启动参数已指定，底层provider路由未独立验证；主代理取得最终构建/测试与真实SDK证据。

本组不重新验收Codex宿主直接接入、跨用户/远程主机/新机器或真实网络卷；未改用户MCP配置，未提交/推送/发布。其余M5算子/修改器/文件、M6批次、M7性能部署未完成。

## 回退

先保存最新定向差异，用apply_patch仅逆向M5对象DTO/codec/service/VM/Core准备子树块、11目录项/结果扩展及TS映射测试和CMake新测试接线；移出新增PreparedSubtreeTests/ObjectApiTests及m5合同前先移除引用。保留M1–M4及后续改动，不全仓reset。错误信封修复是独立必要修复，若只撤M5应保留。重构建主体及受影响独立测试；progress只追加回退记录。运行期不传--automation即可关闭外部服务。
