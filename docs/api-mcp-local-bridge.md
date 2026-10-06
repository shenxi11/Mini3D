# 本机自动化桥：启动与安全边界

日期：2026-10-06。API 0.1.0 / 私有 wire 1。M3本机单用户范围与M4正式MCP适配器/官方SDK真实闭环已验收，后续能力按[实施状态](api-mcp-implementation-status.md)推进；本页不是用户MCP配置，也不自动安装服务。适配器启动见[mcp说明](../mcp/README.md)。

## 启动

普通运行保持关闭。需要外部自动化时显式指定一个**尚不存在的绝对描述文件路径**；父目录和批准根须已存在，均不得经过目录链接。不要提升权限运行。

```powershell
& 'E:/Mini3D/out/build/windows-msvc-local/src/editor/Debug/Mini3DStudio.exe' `
    --automation `
    --automation-descriptor 'E:/CodexTemp/mini3d-session/instance.json' `
    --read-root 'E:/CodexTemp/mini3d-session' `
    --write-root 'E:/CodexTemp/mini3d-session'
```

示例目录由操作者预先建立；这不是让工具动态授权根目录的接口。`--read-root`、`--write-root`可重复，不指定时相应文件权限不开放。关闭状态提供描述文件或根参数会明确拒绝。当前构建包含Qt Network，关闭指**运行期默认不监听**，不宣称已有可选编译开关。

描述文件包含随机pipe、启动身份、bridgeId、有效权限、根及secret；文件使用当前Windows用户的受保护DACL。本机且持久支持ACL的卷才可创建。不要打印、复制到仓库或在命令行上传secret。外部客户端必须指定此实例，不能扫描并选择第一个窗口。

## 文件与历史

- 所有建模仍在应用线程串行执行，使用GUI同一个Scene和UndoStack。忙碌期间不取消用户操作、不读取未确认预览。
- 另存仅新路径：完整暂存后原子“不替换发布”；最后一刻出现文件也不会覆盖。GUI既有覆盖保存不变。
- 打开拒绝丢弃脏文档；场景引用和glTF外部依赖在实际读取前逐项验证。越界、UNC/设备/ADS、目录链接等拒绝。
- 写命令按会话序号记账；断线只能恢复原会话并查结果。过期、服务重启或结果未知不能自动新会话重放。
- 截图必须对应明确文档/视图版本及实际新帧，包含PNG哈希与尺寸；不可绘制、超时或资源错误会失败，不用旧图顶替。

## 停止与故障恢复

关闭自有adapter只断开客户端，不结束Mini3D。正常关闭本体会停止自有连接和捕获，并核验身份后回收自有描述文件；不删除被替换的新文件。若普通读者仍持有不共享DELETE的句柄，安全保留描述文件并报告删除错误，不能宣称无条件立即重启。异常结束的描述文件不能证明服务仍在运行，重开须显式选择新描述路径和新实例。

桥使用原生Windows监听，每个pipe实例显式拒绝远程客户端，连接交给Qt异步字节流。ACL与secret不防御已完全取得同一Windows用户权限的恶意程序；不承诺全面防御目录替换竞态。跨用户/远程主机拒连、实际网络盘/无ACL卷仍有环境验证缺口，详见[M3证据](validation/api-mcp-m3-20261006.md)。

真实GUI测试必须允许其窗口实际展示；给GUI进程的STARTUPINFO施加SW_HIDE会抑制首次展示，即使Qt逻辑isVisible为真也可能无新paint。终端辅助进程仍隐藏，且只能回收本任务记录的自有PID，不按进程名关闭用户窗口。
