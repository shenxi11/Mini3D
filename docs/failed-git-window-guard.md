# Git 启动失败空窗口兜底

2026-10-02 用户报告标题为 `D:\Git\cmd\git.exe` 的空窗口，正文为
`2147942632 / 0x800700e8`，失败命令含 `safe.bareRepository=explicit`、
`core.hooksPath=NUL`、`core.fsmonitor=false` 和 `remote get-url origin`。

首次只读扫描发现21个对应窗口，均属于同一个 WindowsTerminal 后台；没有运行中的 git.exe。
随后再次扫描时这21个窗口已自行或被其他操作移除；本轮当时未发关闭指令，不能归功于脚本。
错误码表示启动管道关闭，但尚未定位哪个组件创建窗口；本工具是清理兜底，不是底层启动根因修复。

## 保护范围

**当前会话窗口不得关闭，窗口保护优先于所有错误匹配。**
启动时固定保留当前控制台、前台窗口以及所有正常 Windows Terminal 窗口的句柄；
即使这些窗口后来改成 Git 标题并出现同样错误，也不关闭。
可用 `-ProtectedWindowHandle` 显式固定当前会话句柄，本轮已指定 Mini3D 会话句柄 `593240`。
扫描时和发送关闭请求前再次检查前台窗口，用户切入目标窗口时本轮跳过；
无法确认正文的窗口仍保留。不得仅凭标题或批量结束进程来扩大清理范围。

`tools/Close-FailedGitWindows.ps1` 同时核对进程、指定标题、单标签页及完整错误命令特征。
只对符合条件的窗口发送 `WM_CLOSE`，随后确认窗口句柄消失。
读取失败、普通终端、仍正常显示的命令和多标签页窗口均保留；不结束 git.exe 或共享
WindowsTerminal 后台，不关闭 Qt Creator，不修改默认终端、Codex 配置或开机任务。
记录仅包含目标句柄、标题和关闭/跳过结果，不保存正常终端正文。

## 使用与停止

在 PowerShell 7.6 中执行，默认只有检查，关闭必须明确带 `-Close`：

```powershell
& .\tools\Close-FailedGitWindows.ps1 -SelfTest
& .\tools\Close-FailedGitWindows.ps1
& .\tools\Close-FailedGitWindows.ps1 -Close
```

本轮已用 `ProcessStartInfo.CreateNoWindow=true`、`WindowStyle=Hidden`、结构化参数启动
120分钟兜底，每5秒检查一次，无可见辅助窗口，也没有开机自启。
旧守护已通过停止文件退出，新增窗口保护后使用新目录重新启动。
本轮日志目录为 `out/validation/git-window-guard-protected-20261002/`，运行状态见 `state.json`，
实际关闭结果见存在时的 `events.jsonl`。PID以状态文件为准，不凭旧PID结束其他进程。
`protectedWindowHandles` 列出本次固定保护的句柄；`protectedSkipped` 记录本轮因保护跳过的候选数。
句柄只用于本次运行，之后不得把旧句柄当作新会话标识。

可以提前停止本轮守护：

```powershell
New-Item -ItemType File -Path 'E:/Mini3D/out/validation/git-window-guard-protected-20261002/stop.request'
```

最多一个同名守护，停止后会在下一次检查退出；也会在限时结束后退出。
再次运行应选择新日志目录；不要把退出后的旧PID用于强制结束进程。

## 验证及限制

七项错误匹配和四项窗口保护检查均通过：正确错误正文、重复路径分隔符、普通标题、非Terminal进程、
多标签页、正常命令、其他错误命令、受保护当前窗口、多句柄保护、前台窗口及允许关闭的非保护目标。
保护检查给出与真实错误相同的标题和正文，验证仍必须保留当前窗口。
实际扫描与守护状态均包含当前会话句柄 `593240`，启动后实测该窗口仍存在，守护无可见主窗口。
一次关闭模式扫描目标数0、关闭数0；隐藏守护进程
及状态文件已实测。真实失败窗口在实现前已消失，因此尚没有真实 `WM_CLOSE` 成功证据。
后续出现时以日志确认；无法读取正文时宁可跳过，不以仅匹配标题扩大关闭范围。

回滚先创建上述停止文件并核对 `running=false`，然后逐个另存移出新增脚本和本说明。
保留进度记录及诊断证据，不需要重置仓库或重启共享后台。
