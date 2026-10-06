# 受控文件读取与批准根

日期：2026-10-06。M3本机单用户服务/授权已验收；显式启动后可使用受控saveAs/open，普通启动默认关闭。[M3证据](validation/api-mcp-m3-20261006.md)说明适用范围。M5-04的save/import/export/new与覆盖/discard规则正在实施，见[文件合同](api-mcp-files.md)，不把新增静态目录当作已开放能力。

`FilePathPolicy::create(readRoots, writeRoots, error)` 固定启动根。根必须是本机现有普通目录，空列表没有相应权限。读目标必须存在且为普通文件；另存目标必须不存在，父目录必须存在。拒绝 UNC、设备路径、ADS、非法路径段、保留设备名、尾点/尾空格、符号链接、目录联接及其他重解析点。

普通 `.` / `..` 相对资源可归一化到已批准根；先检查原始路径上的每个既有祖先，不能通过 `link/..` 隐藏联接。根比较按路径段边界，并采用 Windows ordinal 大小写规则，不允许“批准目录-other”凭前缀获权，也不能用 Unicode 折叠把 NTFS 中不同的 K / K 目录误认为同一路径。

SceneDocument、AssetManager 和 GltfImporter 接受可选 `FileReadPolicy`。GUI 不提供策略时保留原有行为；受控调用检查顶层场景、相对 glTF，以及 glTF 的每个外部 buffer/image，授权前不得打开依赖。解析关闭 fastgltf 自动加载外部文件，统一手动加载；受控调用逐项授权，嵌入 GLB/data 数据无需额外文件权。外部 URL 拒绝。场景完整读取成功前不替换调用方已有 LoadedScene。失败通过 FileReadFailure 分类，不解析中文诊断。

缓存记录首次源字节 SHA256、首次请求依赖路径及实际打开文件的真实来源。Windows 的真实来源从已打开文件句柄取得，不依赖 Qt canonicalFilePath 解析 junction。受控缓存命中必须授权本次依赖、首次依赖和首次真实来源；源字节变化则拒绝旧缓存。不能把“当前同名文件获准”当成旧缓存授权，也不能只检查源哈希或策略回调存在。拒绝不发布新资产，GUI 仍保持首次成功缓存语义。

SceneDocument::write 的可选 beforeCommit 在完整序列化和临时文件写入后、最终 QSaveFile::commit 前执行；拒绝取消临时写入，既有目标和不存在目标都保持原状。GUI 默认不安装该守卫，API 提交错误由上层保留机器分类。

该路径检查不承诺抵御完全控制当前用户的恶意进程所制造的 TOCTOU 竞态；后续服务拒绝提升权限，避免扩大文件能力。QSaveFile 的原子写入不等于拒绝覆盖，另存仍需单独检查。正式文件 API 要将拒绝状态机器分类，不能解析中文诊断。

## 验证证据

在 E 盘隔离夹具运行 `verify-assets.ps1`：显式构建 `mini3d_asset_tests`，执行 `[file-path-policy],[asset-access],[assets]`。最新 19 用例：18 通过、1 跳过；404 条断言全通过。

覆盖中文/相似前缀/越界/空根/覆盖、合法兄弟资源、百分号编码越界 buffer/image、场景→glTF→依赖的完整链、LoadedScene 保留、嵌入 GLB、URL 拒绝。新增回归覆盖源内容改变后复用旧缓存、K / K 不同目录、junction 替换成同名普通目录后复用旧来源，以及文件最终提交拒绝。缓存/junction 用例修复前实际失败，证据保留于 tests-assets-junction-red.log；最终测试通过。真实目录联接负测通过；账户不能创建普通符号链接，该项明确跳过，不记作通过。Astra与Sol的M3复核及本机监听/提升拒绝证据已见[M3证据](validation/api-mcp-m3-20261006.md)；跨用户ACL仍未实测。M5-04新增覆盖授权须另行聚焦审查和实际文件验收。

日志与运行脚本：`E:/CodexTemp/20261006/api-mcp-longplan-3e6a91bc/verify-assets.ps1`、`build-assets.log`、`tests-assets.log`。脚本仅使用自身记录的隔离联接夹具；不要递归删除有联接的整片目录。

## 定向回退

先保存后续差异，再反向撤本轮 assets 中可选策略参数、读取前钩子及对应测试块；移出新 FileReadPolicy、FilePathPolicy 和 FilePathPolicyTests 文件前先撤其 CMake 接线。不可用全仓 reset，不能撤回已验收 M1。progress 只追加回退说明。
