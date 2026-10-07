# Mini3D MCP adapter

这个 adapter 通过官方 SDK 2.3.1 的 stdio 兼容层连接显式指定的 Mini3D 本机实例。Node 24.13.0 / npm 11.6.2 是当前实测组合；TypeScript 7.0.2、Ajv 8.20.0、ajv-formats 3.0.1 和全部传递依赖由 lockfile 固定。日常 Mini3D 构建不依赖 Node。

2026-10-07任务、10-08收口：API0.2.0/wire1、61方法，必须配套0.2本体和adapter；旧0.1明确拒绝。capture新增必填expectedEvaluationId，应先查询真实viewport状态。最终两配置主体/官方SDK真实MCP、固定性能与交叉回归已通过本机候选门槛，adapter冻结源码49项测试通过；详见[动画验收与未验环境](../docs/native-animation-acceptance-20261007.md)、[动画R4合同](../docs/native-animation-r4-contract.md)、[调用说明](../docs/native-animation-api-mcp.md)和[教程](../docs/native-animation-user-guide.md)。下述47工具成绩及旧候选包仍是0.1历史，不代表Desktop本聊天或新机器已验收。

## 构建与启动

在 `mcp/` 运行 `npm ci --ignore-scripts`、`npm run build`、`npm run typecheck`。使用 PowerShell 7.6；将 `npm_config_script_shell` 指向该 `pwsh.exe`，避免 npm 在 Windows 默认采用其他 shell。

启动命令：`node dist/main.js --descriptor E:/approved/private/instance.json`。该路径是语法示例，应换为 Mini3D 本次启动输出的真实、受保护描述文件绝对路径。认证材料来自文件，不作为命令行参数。工具调用不能改变实例、权限或批准的读写根；服务重启后必须明确选择新的描述文件并重启 adapter。

## 工具与结果

`api/schema/methods.json` 中的方法静态逐项注册，不在适配器硬编码总数。名称将 RPC 点号和驼峰转为下划线，例如 `entity.create` 对应 `mini3d_entity_create`，`viewport.capture` 对应 `mini3d_viewport_capture`。`mini3d_system_describe` 返回本体实际能力；静态工具存在不表示该实例已授权或当前可以执行。当前本机/官方SDK已验收61工具目录及动画实际闭环。实际验收阶段见[实施状态](../docs/api-mcp-implementation-status.md)，操作限制见[对象说明](../docs/api-mcp-objects.md)、[网格说明](../docs/api-mcp-modeling.md)、[修改器说明](../docs/api-mcp-modifiers.md)、[文件说明](../docs/api-mcp-files.md)。

M6已新增两种有界批次，当前47工具/46项TS测试及本机真实闭环通过，详见[批次说明](../docs/api-mcp-batches.md)。M7固定生产adapter、最新Release净PATH迁移、实际ZIP清单和Codex CLI0.159.0创建/内插/挤出/保存/Undo/Redo/重开/真实image均通过，见[最终证据](../docs/validation/api-mcp-m7-20261006.md)。仅临时进程配置，已有配置未变化；不代表Desktop当前聊天热加载、跨用户或新机器验收。

输入与输出由同一公共 Schema 解引用。调用者不能提供顶层 `clientSessionId` 或 `mutationSequence`；所有 ID/版本必须是规范 uint64 十进制字符串。未知输入字段、嵌套字段和条件分支严格校验，未知响应字段可以保留。领域合法性继续由本体决定。

成功时 `structuredContent` 是对应领域结果。账本与恢复信息位于独立 text content：`{source:"adapter",bridge:...}` 或 `{adapterRecovery:{source:"adapter",...}}`，不冒充 `document.current` 的本体字段。业务失败为 `isError:true`，保留原 `code`、`recovery`、当前版本及桥账本元数据。截图的 `pngBase64` 只转成 MCP image content；结构化元数据保留尺寸、帧身份、版本、字节数和哈希。损坏或不匹配的图像拒绝呈现。

## 断线与取消

每个 adapter 仅保留一个写请求。失联后只恢复原实例、原 bridge 的原会话并查询原序号；不自动重放原命令、不切换序号、不创建新会话。未确认结果返回 `OUTCOME_UNKNOWN` 并阻断后续写入。

显式调用 `mini3d_document_current` 会先查原请求账本，并在独立 `adapterRecovery` text 中给出结果：`completed` 保留原领域结果/错误和账本；`result_expired` 表示结果已淘汰，但服务确认序号已消费；`not_seen` 表示有效原会话没有接收原序号，可由调用方在重查后明确发起新意图。仍未确认则保持阻断。服务/会话过期不会自动恢复为新会话。

SDK 取消或本地期限只发送 best-effort `bridge.cancel`。已开始的写入继续查询和确认终态；取消不代表提交已回滚。capture 取消按原 request ID 发送。宿主退出只关闭 adapter 自己的管道。

## 测试范围

在已验证的 E:/D: 独有测试目录设置 `MINI3D_MCP_TEST_ROOT`，并同时设置 `TMPDIR`、`TEMP`、`TMP` 和 npm cache 到该目录，再运行 `npm test`。测试使用 Node 本机管道 mock，官方 client 验证 legacy 与 `2026-07-28` 两种 stdio 路径。

测试证明合同、帧、错误、恢复和合成 PNG content；不能代替真实 Mini3D 建模 E2E、目标 Codex 图片显示、跨用户 ACL 或正式安装包验收。没有修改用户 MCP 配置。
