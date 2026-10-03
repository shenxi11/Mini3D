# 子代理协作规则迁移说明

## 当前约定（2026-10-02，按《子代理设置.md》更新）

以用户指定的 `C:/Users/SL/Downloads/子代理设置.md` 为本次合并依据，执行文本位于
[AGENTS.md 第 8 节](../AGENTS.md#8-工具与并行协作)。下文旧迁移记录仅为历史，不再作为当前分工。

| 角色 | 目标模型 | 思考强度 |
| --- | --- | --- |
| 主代理 | `gpt-6.1-sol` | `max` |
| 调研代理 | `gpt-6-luna` | `max` |
| 实施代理 | `gpt-6.1-sol` | `max` |
| 独立审查代理 | `gpt-6.1-sol` | `max` |
| 按需专家 | `gpt-6-astra` | `high` |

小范围低风险任务由主代理直接处理；只在存在独立工作且有收益时分派。
默认最多两个子代理，三个互不依赖且范围不重复的只读研究可临时并行三个，仍受运行器上限约束。
每个可写文件只有一个所有者；优先独立工作树，不能隔离时必须划分不重叠文件并隔离共享产物。
调研只读，可能写缓存或产物的检查交给实施者；独立审查按风险启用，Astra 仅在规定专家条件下启用。
保留项目原有 scope、权限、验证、文档和进度日志要求，不启动二期业务开发。

上述规则合并时未安装自定义代理文件、修改全局配置或改变已启动主会话；后续模型目录修复见下节。
原生启动时按角色显式传入 `model` 和 `reasoning_effort`，使用支持参数覆盖的上下文模式
（当前工具为 `fork_turns: "none"` 或正整数字符串）；本地自定义代理文件的字段则为
`model` 和 `model_reasoning_effort`，不可混用，需核对最终实际配置。

`gpt-6.1-sol / max` 曾被当前运行器以 `Unknown model` 拒绝；规则更新本身不改变模型目录。
后续已按用户要求修复活动 CLI 的目录配置并重启共享后台；Sol 6.1/max 子代理创建及响应已通过。

检查入口为相对本轮前稿的差异、原有第 1–7 节完整保留、模型/角色对照、严格 UTF-8 检查、
进度日志只追加及用户原文哈希不变；本轮为 `SELF_CHECKED`，无程序变更，不运行 C++ 测试。
前稿：`out/validation/agents-policy-before-20261002/`；回滚办法见 progress 本轮记录。

## 2026-10-02 活动 CLI 模型目录修复（已重启并通过启动验证）

最终结果：用户批准后已执行官方 restart，原后台 PID 16120 已退出，新后台 PID 19824
于 18:44:58 启动，版本仍为 0.157.0。重连后以 `model: "gpt-6.1-sol"`、
`reasoning_effort: "max"`、`fork_turns: "none"` 创建 `sol61_after_restart_probe` 成功，
该子代理已返回“启动验证完成”。这是运行器接受参数并完成响应的证据；未依赖模型自报身份，
提供商内部实际后端映射仍不可观察。CLI 前台需重新打开 `/model` 列表查看刷新结果。
以下为诊断过程，不应将其中重启前的待办状态误读为当前结论。

当前命令进程的父进程为 D 盘 Codex 0.157.0 app-server；活动 `CODEX_HOME` 是
`D:/Lang/codex-cli-home`，不是桌面客户端使用的 `C:/Users/SL/.codex`。
D 盘配置没有 `model_catalog_json`，独立同版本服务的 `model/list` 只返回旧目录；
C 盘用户现有 `cockpit-model-catalog.json` 已包含 `gpt-6.1-sol` 和 `max`。

仅在 `D:/Lang/codex-cli-home/config.toml` 增加一行：

```toml
model_catalog_json = "C:/Users/SL/.codex/cockpit-model-catalog.json"
```

该配置使 CLI 与桌面客户端共用用户维护的完整模型目录；不会只额外添加一个模型。
因此 CLI 列表也会跟随该目录的其他增删，模型/推理默认值、接口地址、凭据及权限未改。
原配置备份在同目录 `config.toml.before-sol61-catalog-20261002-183458.bak`。

本机探针 `out/validation/probe-model-catalog-20261002.ps1` 通过独立短生命周期
`app-server --stdio` 执行 `initialize` / `model/list`，不创建任务、不发送模型推理请求。
验证：原配置无目标；临时指定新目录后有目标/max；写入配置后不带覆盖参数也有目标/max。
新服务的目录识别已通过，但当前旧服务再次 `spawn_agent` 仍返回旧模型列表。
目录于服务启动时加载；配置生效与真实子代理推理是两个不同验收项。

用户反馈 CLI `/model` 仍缺模型后的检查：配置修改时间为 2026-10-02 18:34:58，
但 CLI 后台 PID 16120 仍从当日 11:42:26 运行；重新打开前台窗口没有重启共享服务。
当前配置下的新服务再次返回 Sol 6.1/max。运行中服务 proxy 查询分别遇到连接关闭与超时，
不能作为旧服务目录的直接证据；临时探针改动已逐字节恢复，不重复同类查询。
用户已明确同意现在重启共享后台，接下来执行 restart；重启结果及实际子代理待重连核验。

重启会影响同一后台服务中的其他任务，须先确认没有需保留的在途工作；获准后执行：

```powershell
& 'D:/Lang/codex-cli-home/packages/app-server-daemon/releases/0.157.0-x86_64-pc-windows-msvc/bin/codex.exe' app-server daemon restart
```

上述重启及重连后的最小子代理探针现已完成，结果见本节开头。
无需重装 Qt、修改 Mini3D 源码或降级模型；主会话默认模型仍未擅自切换。
参考：[模型目录配置](https://developers.openai.com/codex/config-reference)、
[app-server 模型列表](https://developers.openai.com/codex/app-server)。

## 历史：2026-09-29 初次合并

2026-09-29 按用户要求，参考 `C:/Users/SL/Downloads/AGENTS (2).md` 中的子代理设置，
合并到 [根目录 AGENTS.md 第 8 节](../AGENTS.md#8-工具与并行协作)。执行规范仍以根文件为准，本文仅记录迁移范围。

本次引入 Astra 设计/指导/验收、Sol 实施/独立复核/按需探索的分工，实际模型路由核验与
真实会话降级，一层委派及默认最多两个活动子代理，单工作树单写入者、隔离并行与冻结快照验收，
任务卡、风险分级、升级和交付证据要求。保留现有任务顺序、scope、高风险用户确认与日志规则。

没有整份替换参考文件中的通用规范，也没有修改业务代码、开发计划或全局模型配置。
本次只做规则迁移和文本自检，未启动子代理或运行模型路由探测；路由状态为 `UNVERIFIED`。
后续首次实际使用多模型流程时，必须按根文件核验，不能用角色名或模型自报身份替代真实调用证据。

验证：UTF-8 无 BOM / LF、原规范完整前缀、progress 仅追加、Git 差异空白检查和参考文件未修改。
未运行 C++ 构建/CTest（无程序变更），未声称独立代理复核或模型配置已生效。

本机前稿与检查入口：`out/validation/agents-subagents-before-20260929/`、
`out/validation/agents-subagents-20260929.ps1`。回滚与变更清单见本次 `progress.md` 记录。

## 2026-10-02 子代理模型与思考强度调整

用户明确指定所有实施、审查和探索子代理采用 `gpt-6.1-sol`，思考强度 `max`。
主代理目标仍为 Astra，不修改全局模型配置。后续原生启动显式传入
`model: "gpt-6.1-sol"`、`reasoning_effort: "max"`、`fork_turns: "none"`
（也可用正整数字符串传递必要上下文），不继承旧参数。

实际启动核验状态为 `BLOCKED`：本次最小调用请求 `route_probe_sol61`，运行器返回
`Unknown model 'gpt-6.1-sol' for spawn_agent`，列出的可用模型为
`gpt-6-astra, gpt-6-sol, gpt-6-luna, gpt-5.6-sol, gpt-5.6-terra`。
虽然当前工具声明列出该目标，实际创建请求未被接受；没有成功启动新子代理，
`max` 的实际执行也未核验。不能将规则变更等同于模型已切换，不自动降级到旧 Sol。
需由支持指定模型的运行器重新做最小调用；成功前不恢复批量委派。

已读取[官方子代理文档](https://developers.openai.com/codex/subagents)，
其模型/推理配置说明不等于本机会话拥有相应模型访问能力。
运行器内部版本、实际后端模型与用量未从本次返回值暴露，记为 `UNVERIFIED`。
本机前稿：`out/validation/agents-sol61-before-20261002/`。本轮仅修改规则与说明，
未继续二期功能开发，未修改已有开发成果。
