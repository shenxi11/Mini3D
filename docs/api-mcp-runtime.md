# API / MCP 运行、部署与故障恢复

日期：2026-10-06。API 0.1.0、私有 wire 1、47项工具。首版是 Windows x64 本机同用户自动化，
不是远程服务、任意脚本执行入口或多用户协同编辑。运行期能力和限额以 `mini3d_system_describe` 为准。
实现与验收见[实施状态](api-mcp-implementation-status.md)，通信细节见[本机桥](api-mcp-local-bridge.md)。

## 1. 普通启动与先决条件

双击 `Mini3DStudio.exe` 仍只启动编辑器，不开放管道。不使用自动化时无需 Node/npm。
测试是独立 `mini3d_test_suite` 目标，adapter 是单独 npm 构建，二者都不是正常主体构建依赖。
Windows 10/11 x64、OpenGL 4.1 驱动和包内 Qt 6.8.3/VC143 DLL 要保持相对位置。

使用 MCP 时另需安装外部 Node：允许 `>=24.13.0 <25`，已实测24.13.0。
首版不内嵌、不自动下载 Node，不在启动时执行 npm install。
固定生产依赖来自 `mcp/package-lock.json`；构建组合见 `compatibility.json`。

## 2. 明确启动自己的实例

先在本地支持ACL的卷创建私有目录及批准的文件根。目录不能经过 junction/symlink，
不要提升权限启动。描述文件的父目录须存在，描述文件本身须不存在。
以下路径只是示例，操作者必须换成自己的实际路径，不要覆盖已有会话。

```powershell
$application = 'E:/Mini3D-package/Mini3DStudio.exe'
$applicationArguments = @(
    '--automation'
    '--automation-descriptor'
    'E:/Mini3D-session/instance.json'
    '--read-root'
    'E:/Mini3D-session/projects'
    '--write-root'
    'E:/Mini3D-session/projects'
)
& $application @applicationArguments
```

`--read-root`、`--write-root` 可重复；省略根会关闭相应文件权限，不代表可以访问全盘。
只有启用 `--automation` 才允许这些参数。界面照常展示，所有建模仍在应用线程串行执行。
不可绘制的隐藏窗口可能没有新帧，capture 会明确失败，不能用旧图替代。

描述文件包含当前实例身份、管道、权限和 secret，并受当前用户DACL保护。
只把文件的绝对路径交给 adapter，不把 secret 放到命令行、仓库、截图或普通日志。
同用户恶意程序仍处于信任边界内；本地ACL不是同用户沙箱。

## 3. 启动成套 adapter

含 MCP 的完整包采用以下相对布局，不能只复制 main.js：

```text
package/
  Mini3DStudio.exe + Qt6Network.dll + 其他 DLL/插件/assets
  mcp/dist/main.js
  mcp/node_modules/                 锁定生产依赖，无开发工具链
  mcp/package.json + package-lock.json
  api/schema/                      与本体版本一致的公共合同
  compatibility.json
  mcp-manifest.json + manifest.json
  docs/
```

```powershell
$adapterArguments = @('E:/Mini3D-package/mcp/dist/main.js'
    '--descriptor'
    'E:/Mini3D-session/instance.json'
)
& 'D:/NodeJs/node.exe' @adapterArguments
```

该进程的 stdout 是 MCP stdio 协议，不能混入普通输出；诊断写 stderr。
宿主配置选择绝对 Node 路径、包内 main.js 和本次 descriptor。
本轮仅验收进程内临时配置，没有为用户安装或持久新增任何 MCP 项。
配置字段须以所用宿主官方说明为准，不把其他客户端配置复制到 Codex Desktop。

宿主工具审批与本体文件权限是两层独立控制。本次非交互CLI验收使用auto时，
写工具被审批策略never拦截；依据[官方配置说明](https://learn.chatgpt.com/docs/extend/mcp?surface=cli)，
仅在用户明确批准后，对专用临时工具白名单服务器使用approve，未改变全局审批或sandbox。
该配置仅单次CLI的`-c`生效，进程结束即移除；正式安装配置仍需要单独授权。
本机首版结果见[最终证据](validation/api-mcp-m7-20261006.md)和[包报告](validation/api-mcp-m7-package-20261006.json)。
运行默认关闭已实现；编译时自动化裁剪开关尚未实现，不能当作已提供的构建选项。

## 4. 一次建模调用的正确顺序

1. describe 确认权限与实际能力；document.current 获取文档身份和当前版本。
2. 查询对象或网格摘要，用稳定ID及最新版本构造明确目标的命令。
3. 读取成功返回的新版本；不能在本地猜测版本加一，不能把旧重放结果当最新场景。
4. 控制视图后重新读取视图版本，capture 必须匹配文档及 viewportRevision。
5. saveAs 仅新路径；保存当前路径覆盖须明确参数，open/new 默认拒绝脏文档。

API不抢 GUI 选区、不自动取消用户预览。遇到 BUSY 时等待用户交互完成后重新查询。
多个建模 AI 应由一个协调者串行提交，其他 AI 只读摘要/图片；相同旧版本写入不自动合并。
批次最多64完整对象/局部TRS，详情见[批次](api-mcp-batches.md)。
对象、组件、修改器和文件的实际限制分别见[对象](api-mcp-objects.md)、
[网格](api-mcp-modeling.md)、[修改器](api-mcp-modifiers.md)、[文件](api-mcp-files.md)。

## 5. 失败、断线与关闭

| 情况 | 正确处理 |
| --- | --- |
| 版本冲突 / 文档已替换 | 重新查询当前身份/版本，重新评估意图，不自动覆盖 |
| BUSY / 权限或路径拒绝 | 不取消 GUI 或扩大根；在批准范围内调整输入 |
| LIMIT_EXCEEDED | 减少项目、页大小或图片尺寸；10k创建上限不是每个算子的性能保证 |
| OUTCOME_UNKNOWN | 暂停后续写入；显式 document.current 触发原会话账本查询；确认前不重发 |
| RESULT_EXPIRED | 原序号已消费，不再次执行；检查场景后决定新意图 |
| SESSION_EXPIRED / 本体重启 | 不恢复旧命令；明确选新描述文件并重启 adapter，重新查询场景 |
| CAPTURE_TIMEOUT | 检查自己的GUI是否可见/可绘制及最新版本，不把超时当成功 |
| 文件已存在 / 依赖越界 | saveAs换新路径；依赖必须在批准读根，不能只授权顶层文件 |

取消只是 best-effort；已开始提交不能被强行中断，断线不代表已回滚。
关闭 adapter 只关闭它自己的连接，不结束本体或撤销已提交模型。
要停止服务，正常关闭自己开启的本体，再普通启动、不加 `--automation`。
正常关闭核验身份后回收自己描述文件；异常终止可能留下失效文件，不能凭该文件推断服务存活。
移除文件前核实它属于已结束的会话，保留其他实例和用户窗口。

## 6. 从源码打包与复验

维护者使用 PowerShell 7.6，所有中间产物放独占且已验证可写的 E:/D: 目录：

```powershell
$mcpPackageParameters = @{
    OutputDirectory = 'E:/CodexTemp/mcp-candidate-new'
    WorkDirectory = 'E:/CodexTemp/mcp-build-new'
    NodePath = 'D:/NodeJs/node.exe'
    NpmPath = 'D:/NodeJs/npm.ps1'
}
& 'E:/Mini3D/tools/Package-Mcp.ps1' @mcpPackageParameters
$releasePackageParameters = @{
    BuildDirectory = 'E:/Mini3D/out/build/windows-msvc-local'
    QtBinDirectory = 'E:/Qt5.15/6.8.3/msvc2022_64/bin'
    MsvcRuntimeDirectory = 'E:/VS_2019/VC/Redist/MSVC/14.40.33807/x64/Microsoft.VC143.CRT'
    OutputDirectory = 'E:/CodexTemp/Mini3D-api-mcp-new'
    McpPackageDirectory = 'E:/CodexTemp/mcp-candidate-new'
}
& 'E:/Mini3D/tools/Package-Release.ps1' @releasePackageParameters
$validationParameters = @{
    PackageDirectory = 'E:/CodexTemp/Mini3D-api-mcp-new'
    OutputDirectory = 'E:/CodexTemp/package-validation-new'
}
& 'E:/Mini3D/tools/Test-Package.ps1' @validationParameters
```

输出目录须全新；脚本拒绝覆盖，不推送或发布。adapter单独验证使用 Test-McpPackage.ps1，
默认只测成套产物、Schema与缺少描述文件时的启动拒绝；明确传入 DescriptorPath 后，
才连接指定活实例并验证两代官方SDK的stdio查询与截图。
本体的显式验收进程可设置 `MINI3D_VALIDATION_SETTINGS` 为已存在的独占绝对目录，
将布局/键位/收藏偏好写入该目录的INI，而非日常用户注册表；不设置时普通行为不变。
Test-Package自动使用自己的tmp目录；该变量不是MCP工具、不能由调用动态更改批准根。
最终验收应再用迁移后的包、净应用 PATH、包内 adapter 真建模/捕获，并核对 Qt6Network.dll
实际加载路径。单用户本机结果不能替代跨用户ACL、远程拒绝或全新机器实测。
