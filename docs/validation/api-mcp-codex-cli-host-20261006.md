# Codex CLI 真实 MCP 图片门禁

结论：本机 **Codex CLI 0.159.0** 通过直接 MCP 工具调用及真实 image content 盲验。不是当前 Desktop 聊天热加载验收；未重启 Desktop 或共享后台，也未把官方 SDK client 的成功冒充宿主成功。

用户于 2026-10-06 批准临时新增 Mini3D 专用配置并在验收后移除。本次依据[官方单次覆盖](https://learn.chatgpt.com/docs/config-file/config-advanced)和[MCP stdio 配置](https://learn.chatgpt.com/docs/extend/mcp)，使用进程内 `-c mcp_servers.<唯一名称>...`。保留现有用户配置，CLI 退出条目即消失；两个既有 config.toml 前后 SHA256 一致，没有持久配置改动。

## 实际链路

自建 Mini3D Debug **API 0.1.0/wire 1**、Qt **6.8.3** → 受限本机管道 → 正式 adapter（Node **24.13.0**、官方 SDK **2.3.1**）→ 字节透明 stdio 证据转发 → 用户原有 CLI wrapper。wrapper 使用用户原有 `D:/Lang/codex-cli-home`，未自行覆盖 CODEX_HOME、修改 wrapper 或读取无关凭据。

CLI 单次请求 `gpt-6.1-sol`/`max`、ephemeral、read-only shell sandbox、关闭 shell tool。仅新增服务可用 document.current/getState/capture 三项；prompt 禁止调用其他工具、读本地文件、改场景/配置或创建子代理。Root 预先建立隔离几何，但未向 CLI 提供对象数量、形状、颜色数据、参考图或 `-i` 附件。

审计记录真实工具调用和结果：document.current → viewport.getState → viewport.capture；capture 返回 `image`+`text`，PNG 哈希与结构化结果一致。CLI exit **0**，模型实际描述三个物体从左至右为竖长矩形、带明暗球体、横长矩形，灰色、深灰背景、无网格/坐标覆盖；主代理查看下图确认一致。solid 模式本来显示灰色，不能拿几何 surface.tint 的彩色参数反推截图颜色。

![宿主直接看到的实际 image content](api-mcp-codex-cli-host-20261006.png)

PNG **944×799**、8,423 字节；captureId `7cfa6ecf-810e-4973-9e97-ff94514c796f`、frameId **12**、contextGeneration **1**、documentRevision **3**、viewportRevision **15**，SHA256 `54cf08c8dcb605b5bef6fe3c639619fb614cadcccd9804c8ce114cd0f905e044`。

## 证据、清理与边界

原始根 `E:/CodexTemp/20261006/api-mcp-longplan-3e6a91bc/codex-host-1791287289093`：`mcp-audit.jsonl`、`visual-report.txt`、`evidence.json`、`cli-events.jsonl`、`cli-stderr.log`。复跑入口 `codex-host-validation.mjs`；审计入口 `host-mcp-audit.mjs` 原样转发协议字节，不修改正式 adapter 或服务响应，不打印 secret。

临时条目 `mini3d_acceptance_1791287289095` 仅在该子进程中存在；进程已退出，adapter 已关闭，Root 仅结束自建应用 PID **20908**。用户当前窗口未操作。CLI 自身的应用管理数据库/插件缓存依旧由 CLI 管理，TEMP/TMP/TMPDIR 只控制任务临时输出，不宣称消除一切宿主自有写入。旧插件/其他 MCP 启动告警不在本任务范围，没有为本次验收改动它们。

本项证明 CLI 的真实图片接收和解读，不证明 Desktop 当前会话已安装工具，不替代正式 Release 包、新机器或跨用户 ACL 测试；指定模型/推理参数可查启动参数，底层 provider 路由没有独立回显。M4/M6 的完整建模/保存历史闭环另有官方 SDK 证据，不能将此只读宿主探针称为宿主完整写入闭环。
