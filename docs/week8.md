# 第八周：发布准备与验收

## 范围和发布门槛

本周按顺序完成回归/异常检查、性能记录、本地候选包、演示与交付文档。
保留第七周文件格式，不修改核心协议或扩展线程/网络路径。

第八周候选生成时不能宣称正式 `v1.0.0`：P0 的独立 Camera/Directional Light 实体尚未实现，
项目许可证未由所有者确定，仓库没有提交基线。候选包仅用于本地验收，不创建 Git Tag、
不上传 Release，也不擅自裁剪 P0 或授予代码再分发许可。

后续补齐：用户于 2026-09-10 确认后，当前源码已增加两类实体与格式 2，
详见 [Camera/Light 说明](camera-light.md)。本页包、演示与性能数据仍是原候选快照，未更新。

## 阶段 1：回归与异常

新增 100 Cube 场景创建、整组删除/恢复、撤销到底/重做到末尾及文件重开验证，
检查稳定 ID、父关系、位置和撤销栈状态。另验证空场景、中文文件名和损坏文件失败隔离。
其余导入、材质、选择、移动和未保存保护沿用现有四组测试。

状态：Debug 构建与 CTest 4/4 通过，新增两用例/624 断言通过；Release 周前基线 4/4 通过。
后续阶段和实测证据将在完成后追加。

## 阶段 2：性能基线

原始数据见 [performance-week8.json](performance-week8.json)。机器为 Ryzen 7 5800H、
RTX 3050 Laptop、Qt 6.8.3/MSVC Release，GPU 驱动报告 OpenGL 4.1 NVIDIA 610.47。
物理 1920×1080 离屏 FBO，每帧 glFinish，10 帧预热后测 120 帧；固定内置球体重复实例，
全部摆放在聚焦范围内，无选择/手柄覆盖层。这是 CPU 提交加 GPU 等待耗时，不是屏幕刷新 FPS。

| 三角形 | 几何 Draw 数 | 平均/最慢帧 ms | 等效平均/最低 FPS |
| ---: | ---: | ---: | ---: |
| 100080 | 139 | 1.023 / 5.850 | 977.7 / 170.9 |
| 500400 | 695 | 4.358 / 9.712 | 229.5 / 103.0 |
| 1000080 | 1389 | 6.670 / 13.083 | 149.9 / 76.4 |

Draw 数按每几何一次提交计算，不包括网格/坐标轴，不是驱动采样计数。
首次 Demo 帧从 main 入口起约 524.5 ms；Duck 120484 字节、4212 三角形，导入约 1.776 ms，
首次绘制含上传约 3.043 ms（不等同于纯 GPU 上传耗时）。100 实体保存/打开约 2.916/8.613 ms。
这是单机单次基线，未进行冷热磁盘缓存控制或优化前后对比，不作“性能显著提升”结论。

60 秒快速稳定性循环执行 19654 次导入、删除、Undo/Redo、聚焦和渲染，未触发状态/GL 错误。
工作集从 105.129 MiB 到 105.191 MiB；进程峰值 112.426 MiB（含此前启动/测量，不是导入独立峰值）。
此检查不替代原方案的 30 分钟交互耐久、多显示器或全新机器验收，后者仍待发布门槛验收。

工具默认不构建、不随包安装。复验时输出路径必须不存在，以保留历史证据：

```powershell
cmake -S . -B out/build/opengl-viewport-verify -D MINI3D_BUILD_TOOLS=ON
cmake --build out/build/opengl-viewport-verify --config Release --target mini3d_performance
$env:PATH = 'E:/Qt5.15/6.8.3/msvc2022_64/bin;' + $env:PATH
& out/build/opengl-viewport-verify/tools/Release/mini3d_performance.exe assets/samples/Duck.glb out/validation/performance-new.json 60
```

最后参数可设 1800 执行 30 分钟重复操作；该工具不模拟多显示器切换或完整人工交互。

## 阶段 3：本地候选包

新增 `tools/Package-Release.ps1`：从 Release 目录收集程序和 fastgltf/simdjson，
调用 Qt 自带部署工具复制库/平台/图片插件，显式复制指定 MSVC x64 CRT。
跳过未使用的 generic 触控插件及 DXC 部署；程序使用桌面 OpenGL，不增加网络或 D3D12 渲染路径。
附带相对路径 showcase 场景、许可说明、Qt SPDX、vcpkg copyright 和逐文件 SHA256 清单。
脚本拒绝覆盖已有目标，ZIP 与展开目录并存；没有安装系统组件或写注册表。

```powershell
pwsh -NoProfile -File tools/Package-Release.ps1 -BuildDirectory out/build/opengl-viewport-verify -QtBinDirectory E:/Qt5.15/6.8.3/msvc2022_64/bin -MsvcRuntimeDirectory E:/VS_2019/VC/Redist/MSVC/14.40.33807/x64/Microsoft.VC143.CRT -OutputDirectory out/packages/Mini3D-week8-candidate-new
pwsh -NoProfile -File tools/Test-Package.ps1 -PackageDirectory out/packages/Mini3D-week8-candidate-new -OutputDirectory out/validation/package-new
```

包验证将内容复制到带空格的新目录，以仅 Windows 系统目录的 PATH、清理 Qt 插件环境启动。
默认 Demo 和随包场景均以退出码 0 生成真实帧缓冲；缺失场景退出码 4 且不生成截图。
观察到 Qt 核心/平台、fastgltf、simdjson、MSVC 共 11 个关键模块从包内加载。
重开截图经视觉确认纹理盒、球体和地面正确；此结论是本机环境隔离，不替代全新 Windows 机器验证。

首个检查包部署虽返回 0，但警告没有 VCINSTALLDIR，检查发现未带 CRT。
现改为必填的 MSVC 再分发目录并检查必需 DLL，不把部署工具退出码独自当作可携带证明。
应用 WIN32 子系统的 Qt 日志可能进入调试器而非重定向标准错误；包验证以退出码、截图和模块路径为证。

vcpkg 已固定到本次实际恢复成功的 baseline `2b65c20fc66eda893aa15a15a453c3cf09500b19`，
重新 Configure 与 Release Build/CTest 4/4 通过，没有更新依赖版本。
许可边界见 [第三方说明](third-party-notices.md)，用户包说明见 [package-readme.md](package-readme.md)。

## 阶段 4：整合交付

已生成 [128 秒自动化演示](media/mini3d-week8.mp4)、[GIF 摘要](images/mini3d-week8.gif)
和 [截图](images/mini3d-week8.png)，制作方法和分镜见 [demo.md](demo.md)。
演示基于真实窗口和业务断言，包含手柄鼠标事件与 Ctrl+Z/Y，不是静态 UI 设计图。

最终回归：Debug/Release 完整构建与各自 CTest 4/4 通过；Debug UI 连续三次通过。
固定 seed 42：CPU 28 用例/9929 断言、Assets 11/187、GPU 2/58、Editor 19/967；
200% 缩放下 Editor 19/970 通过。Debug 性能工具明确退出 2；正式测量只能使用 Release。

本机最终交付：`out/packages/Mini3D-week8-candidate-windows-x64.zip` 与同名展开目录。
运行方式见包内 README。ZIP 和测试日志在 ignored 的 out 中，不作为 Git 已发布版本。
本次没有创建提交或 Tag，没有修改本机 Qt Creator 配置，也没有关闭用户原有编辑器。

## 正式 v1.0.0 门槛（未完成）

- P0 Camera/Directional Light 场景实体：已获批准并在后续源码补齐，见专项说明；旧候选包不含此能力。
- 项目许可证及 Qt/MSVC 等公开分发条件：须由所有者确定，当前许可清单不等于合规批准。
- 全新 Windows 机器、跨显示器与原方案 30 分钟交互耐久验收：本轮只有本机隔离和 60 秒快速循环。
- Git 基线、版本 Tag 与外部 Release：尚未创建，也未获得发布授权。

因此结论是“第八周本地候选交付已完成”，不是“全部 V1 已完成并可公开发布”。
无基线提交时回滚使用 `out/validation/week8-before-20260909`（123 个周前正式文件），
按 progress.md 本周清单恢复已有文件并移出本周新增工具/文档/素材，保留日志追加回滚记录。
