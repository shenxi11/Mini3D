# API/MCP 性能测量

本目录是独立测试工具，不接入主体默认构建。`run.ps1` 复用已构建的 Release 静态库，编出正式对象原样链接版和测试副本插桩版，再用外部 Node 客户端连接正式命名管道。

```powershell
& pwsh -NoLogo -NoProfile -NonInteractive -File E:/Mini3D/tools/api-mcp/performance/run.ps1
```

可用 `-Samples 20 -Warmup 3 -CaptureSamples 10 -WindowSeconds 5 -WindowCount 5` 指定真实采样数和持续调用窗口。`-Cases 'mesh:1000,mesh:10000,mesh:100000,entities:1000,entities:10000'` 指定本轮场景；`-ExistingProbeBuild <已验证E/D盘build目录>` 复用工具构建，输出仍进入新的独占 run 目录。

`-CaptureLongestEdge 1600` 会实际创建 1600×1000 的可见视口并请求最长边 1600；每个管道样本检查实际输出最长边达到 1600。默认基础场景使用 960×640。上限截图单独测量时可用 `-Cases 'mesh:1000' -Samples 1 -CaptureSamples 10 -WindowCount 0`，其他行为样本只作该独立夹具的准备与完整性检查，不代替正式 20 样本结论。

每个网格场景提供 typed API 和正式 pipe 的创建、256 项顶点/面分页、摘要、单顶点变换算子及真实 PNG 捕获结果。100k 档使用现有 `EditablePerformanceProbe.cpp` 的 400×250 四边格算法，通过新模型的 `makeEditable` / `replaceEditableMesh` 安装，网格只读，视图框景权限见下文；不放宽 10k API 创建上限。1k/10k 对象档通过新模型拥有的 Core Scene 入口构造空对象，只测页查询。报告记录夹具安装来源、源点/面/面角/派生三角数量。

截图夹具在计时前调用现有 `focusEntities`（pipe 通过正式 `viewport.focus`）框景唯一网格。仅自建测试实例开启现有 `viewport.control`，100k 不具备 `scene.write` 或 `file.write`，生产默认权限与协议不变。证据记录实例权限、聚焦来源、实际 capture view 的相机/矩阵/覆盖层和完整视口 PNG。早期默认镜头的灰色大平面图及性能数据仅作为 `cropped-baseline` 保留，不作为全网格画面验收，也不与聚焦后截图直接判断性能优化幅度。

`typed.json` 记录域调用时间，结果编码在该计时之外；`pipe.json` 记录 Node JSON 编码、正式管道往返及 Node 解码的总时间、实际帧字节、全部样本与 nearest-rank median/p95，预热单列。截图 typed 时间包含等待真实帧、抓取和 PNG 编码，pipe 另包含 base64 与 wire 编码。`trace.ndjson` 只含标量阶段计时和每秒进程 PrivateBytes、WorkingSet、累计 CPU、事件循环唤醒与队列状态；不保存 secret、请求参数或 base64。

插桩版使用四份冻结生产源码的生成副本，版本与生产/副本 SHA256 写入 `generated/instrumentation.json`。真实计时点分别覆盖 UTF-8/JSON 预算扫描/Qt JSON 解析、写参数规范化、规范摘要 hash、账本接收与完成、实际排队等待、typed 解码、域计算、DTO 与 wire 编码，以及帧等待、grabFramebuffer（含其可能触发的 paint）、图像缩放、PNG 与 base64 编码。JSON 阶段不含 socket 传输、分帧复制；未归因的总时间不称为解析时间，各阶段分位数也不能直接相加。正式对象版是性能结论的主证据，插桩输出与每秒资源采样会增加观测开销。

`summarize.mjs` 的阶段占比按同一请求实际耗时计算；入队前同步工作先对每条请求的互不重叠阶段求和，再取 median/p95。该值不含未计时的信封/校验胶水，不能把总往返耗时等同于连续 GUI 阻塞时间。直接截图阶段按实际采样数排除预热。

持续调用由五个等长时间预算窗口组成：网格页查询搭配同名 `entity.update` 的 `no_change` 写命令，以验证结果账本淘汰而不人为增加 Undo 历史；100k 与对象档只查询。每个窗口报告实际持续时长、调用数和窗口内内存/CPU。最后检查最早写结果淘汰为 `result_expired`。64 条查询 burst 验证队列拒绝；插桩读取实际缓存字节和会话数，正式版私有计数标为不可用。两段三秒无调用窗口报告空闲 CPU 与唤醒；探针本身每秒一次统计计时器会产生基线唤醒。

为了避免部署或回归测试污染采样，可在某一 `official/` 或 `instrumented/` 输出目录放入 `pause.flag`。工具在下一个场景启动前打印暂停通知并等待文件事件；移除同一自有文件后继续，不轮询。已经开始的场景先正常完成。

GUI 必须可见。工具只关闭自己创建的进程与窗口，不附着用户实例。构建、描述文件、图像及日志按 E→D→条件 C 临时策略保存；跨用户/新机器与包部署由独立验收负责。
