## 2026-08-30 - Task: 搭建 Mini3D Studio Day 1 基础环境

### What was done

- 初始化 `main` 分支 Git 仓库，建立 Windows x64 / MSVC 2022 / C++20 工程基线。
- 配置 CMake Presets、vcpkg Manifest、编译警告和格式/编码约束；未提前创建 UI、Renderer
  或其他后续阶段空类。
- 补齐项目范围、架构边界、八周路线图、首次构建和环境排查文档。
- 使用现有 MSVC 2022 与 vcpkg 完成第三方依赖恢复；保留 Qt 6 为正式构建前置条件，
  未降级使用本机 Qt 5。

### Testing

- `CMakePresets.json`、`CMakeUserPresets.json.example`、`vcpkg.json` 均通过 PowerShell
  JSON 解析。
- `cmake --list-presets=all -S E:\Mini3D` 通过；共享 Preset 以及临时加载的本机
  Preset 示例均被 CMake 3.31.0-rc1 正确识别。
- `vcpkg install --dry-run --triplet x64-windows` 通过，正确解析 GLM、fastgltf、
  nlohmann/json、spdlog、Catch2 及其传递依赖。
- `cmake --preset windows-msvc` 已成功识别 MSVC 19.40、Windows SDK 10.0.22621.0，
  并实际恢复全部 vcpkg 依赖；Configure 随后因本机缺少 Qt 6 的 `Qt6Config.cmake`
  退出，故本轮未执行 Build/Test，也未声称应用可运行。
- 所有本轮新增文本文件均通过严格 UTF-8 解码，保持 UTF-8 无 BOM；已逐项检查新增差异。

### Notes

- `.clang-format`：加入 LLVM 基线、4 空格缩进和 100 列 C++ 格式规则。
- `.editorconfig`：固定新工程文本的 UTF-8、LF 与缩进约定。
- `.gitattributes`：固定本轮及后续源码/文档的 LF，并把 DOCX 标记为二进制。
- `.gitignore`：忽略 CMake、vcpkg、IDE、本机 Preset 和运行日志输出。
- `CMakeLists.txt`：建立 MSVC 2022、C++20、Qt 6 与第三方依赖检查及项目编译选项。
- `CMakePresets.json`：建立共享的 x64 Configure、Debug/Release Build 与 Debug Test Preset。
- `CMakeUserPresets.json.example`：提供不入库的本机 Qt/vcpkg 路径示例。
- `vcpkg.json`：声明 GLM、fastgltf、nlohmann/json、spdlog 与 Catch2。
- `README.md`：说明项目定位、当前阶段、环境基线、首次配置和文档入口。
- `docs/architecture.md`：固化模块依赖、职责、数据与 OpenGL 生命周期边界。
- `docs/development-setup.md`：记录安装、自检、构建命令、故障排查和基线固定策略。
- `docs/roadmap.md`：落地 Day 1 与后续八周里程碑。
- `docs/scope.md`：固化 V1 的 P0、P1 和明确不做范围。
- `progress.md`：追加本轮实施、验证、缺口和回滚信息。
- `out/`：CMake/vcpkg 生成的忽略目录，包含已恢复依赖和未完成 Configure 的缓存。
- 许可证未创建：MIT、Apache-2.0 或暂不授权需由仓库所有者明确决定。
- 回滚本轮正式文件可在仓库根目录执行以下 PowerShell 7 命令；它只精确删除本轮新增项，
  不触碰原始方案与 `AGENTS.md`：

  ```powershell
  $mini3dAddedFiles = @(
      '.clang-format', '.editorconfig', '.gitattributes', '.gitignore',
      'CMakeLists.txt', 'CMakePresets.json', 'CMakeUserPresets.json.example',
      'vcpkg.json', 'README.md', 'docs/architecture.md',
      'docs/development-setup.md', 'docs/roadmap.md', 'docs/scope.md', 'progress.md'
  )
  Remove-Item -LiteralPath $mini3dAddedFiles -Force
  ```

  `out/` 可通过精确删除 `E:\Mini3D\out` 清理并由下次 Configure 重建；`.git` 与共享的
  `E:\vcpkg` 缓存不纳入上述回滚，避免误删仓库历史或其他项目可复用缓存。

## 2026-08-30 - Task: 验证 Qt 6.8.3 与 Day 1 基础构建

### What was done

- 验证新安装的 Qt 6.8.3 MSVC 2022 64-bit 套件完整可用，并确认其 ABI 为
  `win32-msvc`。
- 使用实际 Qt 与 vcpkg 路径重新生成 CMake 工程，完成 Debug Build 和 CTest 验证。
- 环境变量仅作用于本次构建进程，未修改系统 PATH，也未把机器专属绝对路径写入共享配置。

### Testing

- `qmake -query` 通过：Qt 版本为 6.8.3，安装前缀为
  `E:/Qt5.15/6.8.3/msvc2022_64`，QMake 规格为 `win32-msvc`。
- `cmake --fresh --preset windows-msvc` 通过：识别 MSVC 19.40、Windows SDK
  10.0.22621.0、Qt 6.8.3 以及全部 vcpkg 依赖并成功生成工程。
- `cmake --build --preset debug` 通过，MSBuild 退出码为 0。
- `ctest --preset test-debug` 退出码为 0；当前 Day 1 尚无测试 Target，因此输出
  `No tests were found`，不代表已有功能测试通过。

### Notes

- `progress.md`：仅在末尾追加本轮 Qt、Configure、Build 与 CTest 的验证证据。
- `out/build/windows-msvc/`：刷新 CMake 缓存和 Visual Studio 生成文件；该目录已被 Git
  忽略，可重复生成。
- 本轮未修改源码、共享 CMake 配置或产品行为。回滚生成物前先确认目标仍位于仓库内，
  然后可执行：

  ```powershell
  Remove-Item -LiteralPath 'E:\Mini3D\out\build\windows-msvc' -Recurse -Force
  ```

## 2026-08-30 - Task: 搭建 Day 2 Qt Widgets 编辑器壳

### What was done

- 建立 `mini3d_editor` 应用 Target、Qt 应用入口和 `MainWindow` 顶层窗口。
- 完成中央 Viewport 占位区，以及 Scene、Inspector、Console 三个可停靠面板；提供退出、
  面板显隐菜单和状态栏。
- 保持本轮边界为纯 View 装配，未提前实现 OpenGL、Scene Graph、Renderer 或 ViewModel。
- 同步更新运行方法、当前架构落点与路线图状态，确保后续开发能复现本轮结果。

### Testing

- 使用 clang-format 17.0.3 格式化本轮 3 个 C++ 文件；`--dry-run --Werror --style=file`
  复检通过。
- 10 个本轮相关源码、构建和文档文件均通过严格 UTF-8 解码，保持 UTF-8 无 BOM、LF、
  文件末尾换行，且无尾随空格。
- 设置 `VCPKG_ROOT=E:/vcpkg` 与
  `QT_ROOT=E:/Qt5.15/6.8.3/msvc2022_64` 后，
  `cmake --fresh --preset windows-msvc` 通过并成功生成 Visual Studio 2022 x64 工程。
- `cmake --build --preset debug` 通过；生成
  `out/build/windows-msvc/src/editor/Debug/Mini3DStudio.exe`，构建输出未报告编译警告。
- `ctest --preset test-debug` 退出码为 0，但当前没有测试用例，输出
  `No tests were found`，不代表功能测试通过。
- 将 Qt `bin` 加入子进程 `PATH` 并设置 `QT_QPA_PLATFORM=offscreen` 后，编辑器进程保持
  运行 3 秒；随后只终止本次启动的精确进程，证明应用入口、Qt 平台插件和事件循环可启动。

### Notes

- `CMakeLists.txt`：接入编辑器子目录，并为 MSVC 增加 UTF-8 源码编译选项。
- `src/editor/CMakeLists.txt`：新增 Qt Widgets 可执行 Target 及输出名称。
- `src/editor/MainWindow.h`：声明只负责 View 装配的编辑器主窗口。
- `src/editor/MainWindow.cpp`：实现中央占位区、三个 Dock、菜单和状态栏布局。
- `src/editor/main.cpp`：新增 Qt 应用入口并启动主窗口事件循环。
- `README.md`：更新 Day 2 状态、仓库布局和 Debug 编辑器运行命令。
- `docs/architecture.md`：记录当前 Editor View 落点及尚未创建的业务层边界。
- `docs/development-setup.md`：补充应用 Target 状态和 Debug 运行要求。
- `docs/roadmap.md`：将 Day 2 编辑器壳标记为已建立，并明确下一里程碑边界。
- `progress.md`：仅在末尾追加本轮实现、验证证据和回滚说明。
- `out/build/windows-msvc/`：刷新忽略的 CMake/MSBuild 生成物并生成 Debug 可执行文件。
- 当前仓库仍没有可引用的 Git 提交；完整回滚点为本条记录之前的 Day 1 工作树状态。
  仅回滚本轮新增编辑器源码时，先核对目标恰好是仓库内的 `src/editor`，再执行：

  ```powershell
  $mini3dRoot = (Resolve-Path -LiteralPath 'E:\Mini3D').Path.TrimEnd('\')
  $day2Editor = [System.IO.Path]::GetFullPath('E:\Mini3D\src\editor').TrimEnd('\')
  $expectedEditor = [System.IO.Path]::Combine($mini3dRoot, 'src', 'editor').TrimEnd('\')
  if (-not [string]::Equals(
          $day2Editor,
          $expectedEditor,
          [System.StringComparison]::OrdinalIgnoreCase)) {
      throw "拒绝删除非预期目录: $day2Editor"
  }
  Remove-Item -LiteralPath $day2Editor -Recurse -Force
  ```

  回到完整 Day 1 还需反向恢复本条列出的 5 个共享构建/文档文件；在建立首个 Git 基线
  提交前，不应使用 `git clean` 清理当前工作树，因为原始方案和全部正式文件目前也未跟踪。

## 2026-08-30 - Task: 修复 Qt Creator CMake 与 vcpkg 配置错误

### What was done

- 根据 Qt Creator 生成配置和 vcpkg 日志确认：MSVC 编译器可识别，实际失败点是 Qt Creator
  环境误用 Visual Studio 内置 vcpkg，且未向 vcpkg 暴露已安装的 PowerShell 7。
- 新增被 Git 忽略的本机 CMake Preset，固定 Qt 6.8.3、项目 vcpkg 和稳定的 `pwsh.exe`
  入口，并跳过 Qt Creator 重复的 vcpkg 自动包装。
- 补充 Qt Creator 正确导入根工程、选择本机 Preset 和排查 PowerShell 下载错误的说明；
  未修改业务源码或共享依赖版本。

### Testing

- 原失败日志确认 vcpkg 尝试下载 `PowerShell-7.6.1-win-x64.zip`，随后因代理连接返回
  curl 56；同一 Configure 日志同时证明 MSVC 19.40 的 `cl.exe` 已成功识别。
- `CMakeUserPresets.json` 与示例文件均通过严格 JSON 解析；CMake 能列出
  `windows-msvc-local`、`debug-local` 和 `test-debug-local`。
- `cmake --fresh --preset windows-msvc-local` 通过：明确使用
  `E:/Qt5.15/6.8.3/msvc2022_64`、`E:/vcpkg`、MSVC 19.40 和 Windows SDK
  10.0.22621.0；vcpkg 从本地缓存恢复依赖，未再下载 PowerShell。
- `cmake --build --preset debug-local` 通过，生成
  `out/build/windows-msvc-local/src/editor/Debug/Mini3DStudio.exe`。
- `ctest --preset test-debug-local` 退出码为 0；当前仍无测试用例，输出
  `No tests were found`，不代表功能测试通过。
- 修复后的可执行程序在 `QT_QPA_PLATFORM=offscreen` 下保持运行 3 秒，随后只终止本次
  启动的精确进程。

### Notes

- `CMakeUserPresets.json`：新增本机 Qt、vcpkg、PowerShell 路径和 Qt Creator 跳过项；
  该文件已被 `.gitignore` 排除，不会泄漏机器专属配置。
- `CMakeUserPresets.json.example`：为复制出的本机 Preset 默认跳过 Qt Creator 重复的
  vcpkg 自动包装。
- `docs/development-setup.md`：新增 Qt Creator 根工程导入、本机 Preset 和 PowerShell
  路径排查说明。
- `progress.md`：仅在末尾追加本轮诊断、修复、验证证据和回滚说明。
- `out/build/windows-msvc-local/`：新增被 Git 忽略的本机 Preset 构建输出，可重复生成。
- 完整回滚点为本条记录之前的工作树状态。只撤销机器专属修复可执行：

  ```powershell
  Remove-Item -LiteralPath 'E:\Mini3D\CMakeUserPresets.json' -Force
  ```

  若同时回滚共享说明，还需反向移除示例 Preset 的 `cacheVariables` 和开发环境文档中的
  `Qt Creator 导入` 小节；当前仓库无 Git 提交，不应使用 `git clean` 或 `git restore`
  代替精确回滚。
- `.gitignore`：精确忽略首次误开子模块时产生的 `src/editor/build/` CMake 输出，避免其被
  `git add .` 误收；现有目录内容未删除，也不影响根工程构建。

## 2026-08-30 - Task: 修复 Qt Creator 丢失 Preset 环境变量

### What was done

- 确认 Qt Creator 17 将本机 Preset 转换为 `cmake -S/-B` 后未传递其环境变量，并继续
  注入 Visual Studio 内置旧 vcpkg；这使正确构建目录仍尝试联网下载 PowerShell。
- 在根 `project()` 前增加可选的本机配置入口，由被 Git 忽略的本机文件固定项目 vcpkg、
  PowerShell 真实目录，并关闭 Qt Creator 重复的 vcpkg 包装。
- 将可复制示例和开发环境文档同步为该修复方式；未修改 C++ 业务源码或依赖版本。
- 单独确认 Copilot Language Client 报错源于其配置的语言服务器文件不存在，与 CMake、
  MSVC、Qt 和本轮构建错误无关，未改动 Qt Creator 全局插件配置。

### Testing

- `CMakeLists.txt.user` 检查证明活动配置使用 `windows-msvc-local` 构建目录，但
  `CMake.Configure.UserEnvironmentChanges` 为空，且仍注入
  `E:/VS_2019/VC/vcpkg` 工具链。
- 在移除全部 PowerShell PATH 后，`vcpkg fetch powershell-core` 对 WindowsApps 应用执行
  别名失败，对 `Get-Command pwsh` 返回的真实包目录成功定位 PowerShell 7.6.4。
- 严格模拟 Qt Creator：清除外部 Qt/vcpkg/PowerShell 环境、主动传入错误旧工具链和
  Qt Creator 自动设置脚本后，Configure 仍正确切换到 `E:/vcpkg`，未联网下载 PowerShell，
  并完成 Debug Build。
- 对 Qt Creator 实际使用的 `out/build/windows-msvc-local` 执行同款 Configure 通过；
  缓存确认 `CMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake`、
  `QT_CREATOR_SKIP_VCPKG_SETUP=ON` 和 Qt 6.8.3，随后 Debug Build 通过。
- 修复后的 vcpkg 日志不包含 `powershell-core` 下载、GitHub PowerShell URL 或 curl 错误。
- CTest 退出码为 0；当前没有测试用例，输出 `No tests were found`，不代表功能测试通过。

### Notes

- `CMakeLists.txt`：在 `project()` 前可选加载机器专属的 `CMakeLocalConfig.cmake`。
- `CMakeLocalConfig.cmake`：新增当前机器的真实 vcpkg、PowerShell 路径和 Qt Creator
  跳过项；该文件被 Git 忽略。
- `CMakeLocalConfig.cmake.example`：新增可提交的本机构建环境模板和路径校验。
- `CMakeUserPresets.json`：恢复为只维护当前机器的 Qt 与 vcpkg 路径，避免重复维护
  PowerShell 版本目录；该文件仍被 Git 忽略。
- `CMakeUserPresets.json.example`：移除已转移到本地 CMake 模板的重复跳过项。
- `.gitignore`：忽略机器专属的 `CMakeLocalConfig.cmake`。
- `docs/development-setup.md`：记录 Qt Creator 丢失 Preset 环境时的预加载方案，并明确
  WindowsApps 应用执行别名不能交给当前 vcpkg。
- `progress.md`：仅在末尾追加本轮诊断、修复、验证证据和回滚说明。
- `out/build/qtcreator-simulation/` 与 `out/build/windows-msvc-local/`：被 Git 忽略的验证
  构建输出，可重复生成。
- 完整回滚点为本条记录之前的工作树状态。仅停用机器专属预加载可执行：

  ```powershell
  Remove-Item -LiteralPath 'E:\Mini3D\CMakeLocalConfig.cmake' -Force
  ```

  完整回滚还需反向移除根 CMake 的可选 include、对应示例、忽略项和文档小节；当前仓库
  无 Git 提交，不应使用 `git clean` 或 `git restore` 代替精确回滚。

## 2026-08-30 - Task: 初始化 OpenGL 4.1 Core Viewport

### What was done

- 新建 `mini3d_renderer_gl` Target 和 `ViewportWidget`，由渲染模块承载 QOpenGLWidget、
  OpenGL 4.1 Core 函数表、Debug Logger 与基础清屏职责。
- 在 `QApplication` 创建前设置 4.1 Core、24 位深度、8 位模板和双缓冲格式；Debug 构建
  额外请求 Debug Context。
- 用真实 OpenGL Viewport 替换中央占位控件，Editor 只负责装配，不直接管理 GPU 句柄。
- 同步 README、架构、路线图和开发环境说明；未提前实现 Shader、几何、Camera 或 Grid。

### Testing

- Qt Creator 使用的 `out/build/windows-msvc-local` 重新 Configure 通过，新 Renderer 和
  Editor 源码均编译成功；最终链接因该目录中的旧 `Mini3DStudio.exe` 仍在运行而触发
  `LNK1168`，确认锁定进程为用户现有窗口，未强制关闭。
- 独立 `out/build/opengl-viewport-verify` 使用 MSVC 19.40、Qt 6.8.3 和 x64 Debug 完整
  Configure/Build 通过，生成 `Mini3DStudio.exe`，编译输出无警告。
- 通过 Windows 调试器捕获真实运行日志：Context 为 `OpenGL 4.1.0 NVIDIA 610.47`，GPU
  为 `NVIDIA GeForce RTX 3050 Laptop GPU`，且输出 `OpenGL debug logging enabled.`。
- 独立实例成功创建窗口并通过正常关闭消息以退出码 0 结束；调试输出未出现 OpenGL
  Context 或析构错误。
- 运行截图中 Viewport 三处采样均为 `RGB(14,18,24)`，与清屏常量一致，证明清屏结果
  实际显示且区域颜色稳定；验证截图位于 `out/validation/day3-opengl-viewport.png`。
- 本轮 5 个 C++ 文件通过 `clang-format --dry-run --Werror`；8 个构建/源码文件均通过
  UTF-8 无 BOM、LF 换行检查，源码中不再残留 Viewport 占位引用。
- CTest 退出码为 0；当前没有测试用例，输出 `No tests were found`，不代表功能测试通过。

### Notes

- `CMakeLists.txt`：在 Editor 前加入 `src/renderer_gl` 子目录。
- `src/renderer_gl/CMakeLists.txt`：新增 OpenGL Viewport 静态库及公开 Qt 图形依赖。
- `src/renderer_gl/ViewportWidget.h`：声明 SurfaceFormat、OpenGL 生命周期回调和日志器状态。
- `src/renderer_gl/ViewportWidget.cpp`：实现 4.1 Core 校验、Debug Logger、清屏与 Context 内清理。
- `src/editor/CMakeLists.txt`：让 Editor 链接 `mini3d_renderer_gl`。
- `src/editor/main.cpp`：在 `QApplication` 前设置默认 OpenGL SurfaceFormat。
- `src/editor/MainWindow.h`、`src/editor/MainWindow.cpp`：移除占位 Viewport 并装配真实控件。
- `README.md`：将当前状态、运行标题和仓库布局更新到 Day 3。
- `docs/architecture.md`：记录 Renderer/Editor 当前落点和 Context 边界。
- `docs/roadmap.md`：标记 Day 3 完成并限定下一里程碑。
- `docs/development-setup.md`：补充 OpenGL 运行判据和最小故障排查。
- `progress.md`：仅在末尾追加本轮实现、验证证据和回滚点。
- `out/build/opengl-viewport-verify/` 与 `out/validation/`：被 Git 忽略的构建和运行验证
  输出，可重复生成，不属于正式源码交付。
- 完整回滚点为本条记录之前的工作树状态。当前仓库尚无 Git 提交，回滚时应按上述文件
  清单反向恢复并删除 `src/renderer_gl/`，不得使用 `git clean` 或 `git restore` 扩大范围。

## 2026-08-30 - Task: Day 4 实现 ShaderProgram 与首个三角形

### What was done

- 新增自有 `ShaderProgram`，完成 Qt 资源中 GLSL 的读取、编译、链接、绑定和 Context 内
  删除，并在失败时输出 Shader 路径与完整驱动日志。
- 在 Core Profile 要求下创建最小 VAO，使用 `gl_VertexID` 绘制彩色三角形，不提前引入
  VBO、EBO 或 Mesh 抽象。
- 使用 Qt 目标式资源声明嵌入 Vertex/Fragment Shader，避免运行目录与外部文件路径差异。
- 更新当前状态、架构落点、路线图和运行故障排查，范围仍限定在 Day 4。

### Testing

- 首次把 `.qrc` 作为普通静态库源码时，构建证明确认 CMake 只将其列为 `None`，链接出现
  `qInitResources_renderer_shaders` 未解析；改用 `qt_add_resources(TARGET ...)` 后生成资源
  初始化库与 RCC 对象，Debug Configure/Build 通过。
- Windows 调试器捕获实际运行日志：OpenGL 4.1 Context、Debug Logger 和
  `First OpenGL triangle initialized.` 均成功，窗口通过正常关闭路径退出。
- `out/validation/day4-triangle.png` 目视确认三角形位置、插值颜色和背景均正确。
- 受控反向验证临时制造 Fragment Shader 语法错误，运行日志同时包含
  `:/mini3d/shaders/triangle.frag` 和 NVIDIA 驱动错误
  `syntax error, unexpected ')'`；随后恢复正确源码并重新构建通过。
- 本轮 4 个 C++ 文件通过 `clang-format --dry-run --Werror`；7 个构建、源码和 Shader 文件
  均通过 UTF-8 无 BOM、LF 换行检查。

### Notes

- `src/renderer_gl/ShaderProgram.h`：新增 Program 生命周期和编译接口。
- `src/renderer_gl/ShaderProgram.cpp`：新增 GLSL 读取、编译、链接与驱动日志实现。
- `src/renderer_gl/ViewportWidget.h`、`src/renderer_gl/ViewportWidget.cpp`：接入三角形 VAO、
  ShaderProgram 和 Context 内清理。
- `src/renderer_gl/CMakeLists.txt`：登记 ShaderProgram，并以目标式 Qt Resource 嵌入 GLSL。
- `assets/shaders/triangle.vert`、`assets/shaders/triangle.frag`：新增 Day 4 三角形 Shader。
- `README.md`：将当前状态更新为 Day 4 可编程渲染闭环。
- `docs/architecture.md`：记录 Viewport 与 ShaderProgram 当前职责边界。
- `docs/roadmap.md`：标记 Day 4 完成并限定 Day 5 范围。
- `docs/development-setup.md`：补充三角形运行判据和 Shader 日志排查入口。
- `progress.md`：仅在末尾追加本日实现、失败证据、验证和回滚点。
- `out/validation/Run-ViewportSmoke.ps1`、Day 4 截图与日志：被 Git 忽略的验证输出，可重复
  生成，不属于正式源码交付。
- 完整回滚点为本条记录之前的工作树状态。当前仓库无 Git 基线提交，回滚时应反向恢复
  上述现有文件并删除 ShaderProgram 与三角形 Shader，禁止使用 `git clean` 扩大范围。

## 2026-08-31 - Task: Day 5 实现 GpuMesh 与索引 Cube

### What was done

- 新增 `Renderer` 作为帧入口，将清屏、Depth Test、背面剔除、Shader 绑定和绘制顺序从
  Viewport 中分离，Editor 和 MainWindow 仍不接触 GPU 句柄。
- 新增 `GpuMesh` 独占 VAO/VBO/EBO，上传交错位置、法线、颜色和 32 位索引，并在当前
  Context 中执行索引绘制与逆序清理。
- 新增最小 CPU `MeshData`，构建 24 顶点、36 索引且各面为 CCW 绕序的 Cube；Mesh
  Shader 使用透视观察矩阵、法线变换和固定方向光显示立体朝向。
- 移除已被 Cube 完整取代的 Day 4 一次性三角形 Shader 与 VAO 路径，未留下孤儿资源。

### Testing

- CMake 重新生成资源和 Renderer Target 后，MSVC 19.40 x64 Debug 完整 Build 通过，
  `Renderer.cpp`、`GpuMesh.cpp`、ShaderProgram 和 Viewport 均无编译警告。
- Windows 调试器捕获 `Renderer initialized: indexed Cube with depth test and back-face
  culling.`，OpenGL 4.1 Context 和 Debug Logger 均保持有效。
- `out/validation/day5-cube.png` 目视确认 Cube 三个朝向面、透视、面颜色和光照正确；
  运行实例通过正常窗口关闭路径退出。
- 调试日志未出现 `GL_INVALID`、`GL_OUT_OF_MEMORY`、无当前 Context 删除或 GPU 清理警告。
- 本轮 9 个 C++ 文件通过 `clang-format --dry-run --Werror`；12 个构建、源码和 Shader
  文件通过 UTF-8 无 BOM、LF 换行检查；源码与资源中无三角形孤儿引用。

### Notes

- `src/renderer_gl/MeshData.h`：新增位置、法线、颜色和索引的最小 CPU Mesh 数据。
- `src/renderer_gl/GpuMesh.h`、`src/renderer_gl/GpuMesh.cpp`：新增 VAO/VBO/EBO 上传、绘制
  与 Context 内生命周期管理。
- `src/renderer_gl/Renderer.h`、`src/renderer_gl/Renderer.cpp`：新增帧入口、固定观察矩阵、
  Cube 数据和显式 OpenGL 状态。
- `src/renderer_gl/ShaderProgram.h`、`src/renderer_gl/ShaderProgram.cpp`：新增 `mat4`
  Uniform 写入接口。
- `src/renderer_gl/ViewportWidget.h`、`src/renderer_gl/ViewportWidget.cpp`：改为只转发
  initialize/resize/paint/cleanup 给 Renderer。
- `src/renderer_gl/CMakeLists.txt`：登记新渲染源码、GLM 公开依赖和 Mesh Shader 资源。
- `assets/shaders/mesh.vert`、`assets/shaders/mesh.frag`：新增基础 Mesh 变换与固定光照。
- `assets/shaders/triangle.vert`、`assets/shaders/triangle.frag`：删除已被 Cube 路径取代的
  Day 4 验证 Shader。
- `README.md`、`docs/architecture.md`、`docs/roadmap.md`、
  `docs/development-setup.md`：同步 Day 5 能力、模块边界和运行判据。
- `progress.md`：仅在末尾追加本日实现、验证证据和回滚点。
- Day 5 截图与调试日志位于被 Git 忽略的 `out/validation/`，可重复生成。
- 完整回滚点为本条记录之前的工作树状态。回滚时应反向恢复本日修改、删除 Renderer、
  GpuMesh、MeshData 和 Mesh Shader，并恢复 Day 4 三角形资源；禁止使用 `git clean`。

## 2026-08-31 - Task: Day 6 实现 EditorCamera 与视口导航

### What was done

- 新增纯 GLM `EditorCamera`，统一维护右手坐标系、Y 轴向上的 View/Projection、观察目标、
  距离和 Viewport 宽高比，并限制 Pitch、距离和零尺寸输入。
- 由 `Renderer` 持有相机状态并接管动态 View/Projection；`ViewportWidget` 只把中键拖动、
  Shift+中键拖动和滚轮分别转发为 Orbit、Pan、Zoom 意图，输入后请求重绘。
- Resize 同步更新相机投影尺寸，不引入 Scene、ViewModel、Picking 或 Gizmo。
- 同步 README、架构、路线图和开发环境说明，明确当前交互方式与职责边界。

### Testing

- 独立 `out/build/opengl-viewport-verify` 重新 Configure 后，MSVC 19.40 x64 Debug 完整
  Build 通过，EditorCamera、Renderer、ViewportWidget 与 Editor 均无编译警告。
- `out/validation/Run-CameraInteractionSmoke.ps1` 启动真实 Qt/OpenGL 窗口并注入输入；
  Orbit、Pan、Zoom 相邻截图分别变化 1406、2961、4265 个采样像素，证明三条事件路径
  都实际改变了画面，而非只完成接口接线。
- 窗口从 1456×939 调整为 1636×819 后继续正确绘制，画面未拉伸或崩溃；实例通过正常
  关闭路径退出，cdb 退出码为 0。
- `out/validation/day6-before.png`、`day6-orbit.png`、`day6-pan.png`、`day6-zoom.png` 和
  `day6-resize.png` 目视确认观察方向、构图位置、距离和宽高比变化符合预期。
- cdb 日志包含 OpenGL 4.1、NVIDIA RTX 3050、Debug Logger 和 Renderer 初始化记录；
  未命中 `GL_INVALID`、`GL_OUT_OF_MEMORY`、Shader 初始化失败或无当前 Context 模式。
- 本轮 6 个 C++ 文件通过 `clang-format --dry-run --Werror`。

### Notes

- `src/renderer_gl/EditorCamera.h`、`src/renderer_gl/EditorCamera.cpp`：新增相机状态、交互数学
  与 View/Projection 计算。
- `src/renderer_gl/Renderer.h`、`src/renderer_gl/Renderer.cpp`：持有 EditorCamera，并接入
  Resize、Orbit、Pan、Zoom 与动态观察矩阵。
- `src/renderer_gl/ViewportWidget.h`、`src/renderer_gl/ViewportWidget.cpp`：解析 Qt 中键、
  Shift 修饰键、鼠标位移和滚轮事件并请求重绘。
- `src/renderer_gl/CMakeLists.txt`：登记 EditorCamera 源码。
- `README.md`、`docs/architecture.md`、`docs/roadmap.md`、
  `docs/development-setup.md`：同步 Day 6 能力、边界、控制方式和排错入口。
- `progress.md`：仅在末尾追加本日实现、验证证据和回滚点。
- `out/validation/Run-CameraInteractionSmoke.ps1`、Day 6 截图与调试日志：被 Git 忽略的
  可重复验证产物，不属于正式源码交付。
- 完整回滚点为上一条 Day 5 记录之后的工作树状态。回滚时应反向恢复 Renderer、Viewport、
  CMake 和文档修改，并删除 EditorCamera 与 Day 6 验证产物；禁止使用 `git clean`。

## 2026-08-31 - Task: Day 7 实现 Grid、XYZ 轴与首周 Alpha 整合

### What was done

- 新增 `GridRenderer`，用独立 Shader、VAO 和 VBO 以单次 `GL_LINES` Draw Call 绘制
  20×20 范围的有限 XZ 地面网格，以及红 X、绿 Y、蓝 Z 世界轴。
- `Renderer` 在同一相机矩阵下先绘制 Grid/Axis、再绘制 Cube，保留显式 Depth Test、
  背面剔除和资源逆序清理；未把线段拓扑混入三角形 `GpuMesh`。
- 完成首周真实窗口整合与相机回归，生成 Alpha 截图并将正式副本放入 README。
- 同步架构、路线图和开发环境说明；仓库没有 Git 基线提交，因此未创建无法追溯的 Tag。

### Testing

- 独立 `out/build/opengl-viewport-verify` 重新 Configure/Build 通过，Qt Resource 成功生成
  Grid Shader，MSVC 19.40 x64 Debug 编译与链接无警告。
- 首次调用旧 `Run-ViewportSmoke.ps1` 时，脚本因直接比较正斜杠参数与反斜杠进程路径而
  无法识别已启动实例；确认没有残留进程后改用会先解析绝对路径的交互脚本，未重复同一
  失败调用结构。
- `Run-CameraInteractionSmoke.ps1` 在 Grid 接入后复跑通过；Orbit、Pan、Zoom 分别变化
  2826、3943、5172 个采样像素，窗口从 1456×939 Resize 到 1636×819 后正常绘制并退出。
- `out/validation/day7-alpha.png` 与正式截图目视确认有限网格、红 X/绿 Y/蓝 Z 轴、Cube
  遮挡和透视正确；Orbit 与 Resize 截图确认线段跟随同一相机矩阵。
- cdb 日志包含 OpenGL 4.1、NVIDIA RTX 3050、Debug Logger 和
  `Renderer initialized: indexed Cube, Grid and XYZ axes.`，未命中 OpenGL、Shader、
  Context 或未处理异常失败模式。
- 本轮 4 个 C++ 文件通过 `clang-format --dry-run --Werror`。

### Notes

- `src/renderer_gl/GridRenderer.h`、`src/renderer_gl/GridRenderer.cpp`：新增有限网格、XYZ 轴
  的 CPU 顶点生成、GPU 上传、绘制与 Context 内清理。
- `src/renderer_gl/Renderer.h`、`src/renderer_gl/Renderer.cpp`：接入 GridRenderer、共享
  ViewProjection，并按 Grid/Axis → Cube 顺序绘制。
- `src/renderer_gl/CMakeLists.txt`：登记 GridRenderer 与 Grid Shader 资源。
- `assets/shaders/grid.vert`、`assets/shaders/grid.frag`：新增线段位置变换和颜色输出。
- `README.md`、`docs/architecture.md`、`docs/roadmap.md`、
  `docs/development-setup.md`：同步 Day 7 能力、边界、运行判据和后续里程碑。
- `docs/images/mini3d-alpha-viewport.png`：新增 SHA-256 为
  `F17CE58F516321245989BDBC8D670FA629A43A5C3B55C4E6844361EC3DF5E6C2` 的首周正式截图。
- `progress.md`：仅在末尾追加本日实现、失败证据、验证结果和回滚点。
- Day 7 交互截图与 cdb 日志位于被 Git 忽略的 `out/validation/`，可重复生成。
- 完整回滚点为上一条 Day 6 记录之后的工作树状态。回滚时应反向恢复 Renderer、CMake、
  README 和 docs 修改，删除 GridRenderer、Grid Shader 与正式 Alpha 截图；禁止使用
  `git clean`。

## 2026-08-31 - Task: Day 8 实现 PrimitiveFactory、基础几何与无窗口测试

### What was done

- 新增纯 CPU `PrimitiveFactory`，统一生成 24 顶点/36 索引 Cube、16×24 分段且极点无
  退化面的 Sphere，以及朝 +Y 的索引 Plane；全部三角形从外侧观察为 CCW。
- `Renderer` 改为上传并绘制 Cube、Sphere、Plane 三个独立 `GpuMesh`，在同一 Mesh
  Shader 和 ViewProjection 下分开放置，并保持 Grid/Axis → Mesh 的帧顺序与逆序清理。
- 新增 `mini3d_tests` Catch2 无窗口 Target，直接验证纯 GLM 几何和相机逻辑，不链接 Qt
  Widgets、OpenGL 或真实 Viewport Context。
- 增加仅由 `MINI3D_VALIDATION_CAPTURE` 显式启用的应用内帧缓冲验证入口，解决自动化桌面
  无有效屏幕 DC 时无法诚实截图的问题；普通用户启动路径不执行该分支。
- 同步 README、架构、路线图和开发环境说明，并加入当前三种基础几何正式截图。

### Testing

- 独立 `out/build/opengl-viewport-verify` 重新 Configure/Build 通过，MSVC 19.40 x64 Debug
  成功生成 Editor、Renderer 和 `mini3d_tests.exe`，编译与链接无警告。
- CTest 的 `mini3d_unit_tests` 1/1 通过并带 `unit;headless` 标签；直接运行 Catch2 得到
  `All tests passed (4881 assertions in 6 test cases)`。
- 几何断言覆盖索引范围、三角形非退化、单位法线、外侧 CCW、Cube/Plane 固定数量、
  Sphere 半径与法线一致性；相机断言覆盖 Resize、Orbit、Pan、Zoom 上下限和矩阵有限性。
- 首次桌面截图在 `CopyFromScreen` 报无效句柄；延长等待并重建对象后同类错误复现，遂
  改用 `PrintWindow`。后者只得到白色静态缓存且交互前后 0 像素变化，被验证脚本拒绝；
  最终改为 `QOpenGLWidget::grabFramebuffer()`，没有把无效截图误报为成功。
- 应用内帧缓冲成功生成 852×659 PNG，采样得到 81 种颜色；目视确认 Cube、Sphere、
  Plane、Grid 与 XYZ 轴的几何、光照、遮挡和绕序正确。
- cdb 日志包含 OpenGL 4.1、NVIDIA RTX 3050、Debug Logger、五类画面元素初始化和
  帧缓冲保存记录；程序自动正常关闭、cdb 退出码为 0，未命中 OpenGL/Shader/Context
  或未处理异常失败模式。
- 本轮 7 个 C++ 文件通过 `clang-format --dry-run --Werror`。

### Notes

- `src/renderer_gl/PrimitiveFactory.h`、`src/renderer_gl/PrimitiveFactory.cpp`：新增三种基础
  几何的固定 CPU 拓扑、法线、颜色和索引生成。
- `src/renderer_gl/Renderer.h`、`src/renderer_gl/Renderer.cpp`：移除内嵌 Cube 工厂，接入
  三个 GpuMesh 的上传、变换、绘制、有效性检查和逆序清理。
- `src/renderer_gl/CMakeLists.txt`：登记 PrimitiveFactory 源码。
- `tests/CMakeLists.txt`、`tests/PrimitiveFactoryTests.cpp`、`tests/EditorCameraTests.cpp`：
  新增 Catch2 无窗口测试 Target 与 6 个测试用例。
- `CMakeLists.txt`：在 `BUILD_TESTING` 开启时加入 tests 子目录。
- `src/editor/main.cpp`：新增环境变量显式控制的 QOpenGLWidget 帧缓冲保存与自动关闭分支。
- `README.md`、`docs/architecture.md`、`docs/roadmap.md`、
  `docs/development-setup.md`：同步 Day 8 能力、测试边界、验证入口与运行判据。
- `docs/images/mini3d-primitives.png`：新增 SHA-256 为
  `FBBF900FDC87917EB8595243476EAECB6596FAB9C0FA8BAE6A3841D11333B217` 的正式截图。
- `progress.md`：仅在末尾追加本日实现、失败策略切换、验证证据和回滚点。
- `out/validation/Run-FramebufferSmoke.ps1`、成功帧缓冲和 cdb 日志被 Git 忽略；已删除
  PrintWindow 产生的 5 张白色失败截图，并恢复交互脚本的屏幕捕获实现。
- 完整回滚点为上一条 Day 7 记录之后的工作树状态。回滚时应反向恢复 Renderer、Editor、
  根与 Renderer CMake、README 和 docs，删除 PrimitiveFactory、tests 与 Day 8 正式截图；
  禁止使用 `git clean`。

## 2026-08-31 - Task: 连续 5 个开发日最终整体验收

### What was done

- 对 Day 4 至 Day 8 的 Shader、GPU Mesh、相机、Grid/Axis、PrimitiveFactory、测试与文档
  进行最终范围审计，确认未引入 glTF、Scene Graph、Picking、Gizmo、纹理或材质功能。
- 核对正式源码、资源、测试与截图均位于预期目录，构建和验证产物继续由 `/out/` 忽略。
- 最终验收阶段没有修改业务代码、构建配置或既有文档内容，只追加本条验证记录。

### Testing

- 20 个 `src/` 与 `tests/` C++ 文件全部通过 `clang-format --dry-run --Werror`。
- 独立验证目录最终 Debug Build 通过，Renderer、Editor 和 tests Target 均为最新状态。
- CTest 1/1 通过；固定随机种子直接运行 Catch2，6 个用例、4881 条断言全部通过。
- `dumpbin /dependents` 确认 `mini3d_tests.exe` 只导入 MSVC Debug 运行库与 Kernel32，
  不导入 Qt 或 OpenGL，符合无窗口测试边界。
- 未设置 `MINI3D_VALIDATION_CAPTURE` 时，编辑器保持运行超过 2.5 秒并通过主窗口正常关闭，
  退出码为 0，证明验证钩子不会改变普通启动路径。
- 最终应用内帧缓冲为 852×659、81 种采样颜色，SHA-256 为
  `FBBF900FDC87917EB8595243476EAECB6596FAB9C0FA8BAE6A3841D11333B217`，与正式 Day 8
  截图一致；cdb 退出码为 0，日志失败模式扫描通过。

### Notes

- `progress.md`：仅在末尾追加本次五日整体验收结论和证据。
- `out/validation/final-viewport.png`、`final-viewport-cdb.log`：被 Git 忽略的最终运行验证
  产物，可重复生成，不属于正式源码交付。
- 回滚点为本条标题之前；如不保留整体验收记录，只删除本条完整章节，不回滚 Day 4 至
  Day 8 的任何实现或逐日记录。当前仓库无 Git 基线，禁止使用 `git clean`。

## 2026-09-08 - Task: 第二周纹理与基础材质

### What was done

- 给三种基础几何补齐 UV，Sphere 使用接缝重复顶点，保持 2160 索引且无极点退化面。
- 接入 RGBA8 纹理、mipmap、基础颜色与可选顶点色/贴图；演示棋盘格与独立橙色材质。
- 新增离屏 GPU 自动验收，纯 CPU 测试维持独立；保留纹理在当前 Context 内释放的边界。

### Testing

- MSVC Debug Build 通过；CTest 2/2 通过，CPU 7 用例、9301 断言。
- GPU 测试验证翻转、像素转换、mipmap、材质切换与资源生命周期。
- 首次编辑器日志发现纹理 0 未定义的驱动警告，补白色默认纹理后重新构建和运行，
  该警告消失；最终日志未命中 GL_INVALID、GL_OUT_OF_MEMORY、上传失败或清理失败。
- 实机帧缓冲 852×659，目视确认棋盘格的左红右蓝标记位于 Plane 的 -Z 边，
  Sphere 橙色、Cube 面颜色与网格正确。证据：out/validation/week2-materials-verified*。

### Notes

- src/renderer_gl/GpuTexture.h、GpuTexture.cpp：新增二维纹理上传、采样与释放。
- src/renderer_gl/Material.h：新增基础材质参数与非拥有纹理引用。
- src/renderer_gl/MeshData.h、GpuMesh.cpp：扩展 UV 属性与 GPU 顶点布局。
- src/renderer_gl/PrimitiveFactory.cpp：增加 Cube/Plane UV 和 Sphere 接缝拓扑。
- src/renderer_gl/ShaderProgram.h、ShaderProgram.cpp：增加向量与整数 Uniform 写入。
- src/renderer_gl/Renderer.h、Renderer.cpp：绑定独立材质、棋盘格和白色默认纹理。
- src/renderer_gl/CMakeLists.txt：登记新增源码。
- assets/shaders/mesh.vert、mesh.frag：传递 UV 并计算材质颜色。
- tests/PrimitiveFactoryTests.cpp：补 UV 方向和连续性检查。
- tests/GpuTextureTests.cpp、tests/CMakeLists.txt：新增独立 GPU 验收及 CTest 配置。
- docs/week2.md：记录第二周纹理契约、测试与阶段边界。
- progress.md：仅追加本轮记录。
- 回滚点：out/validation/week2-before-20260908 为本轮开始前的逐文件副本。
  按上述清单从副本复制恢复原有文件，删除本轮新增文件即可回到 Day 8；不要覆盖
  用户的 .user 配置或其他构建目录，不执行 git clean。后续同轮数学变更另行记录。

## 2026-09-08 - Task: 第二周 AABB/Ray 与整体验收

### What was done

- 新建纯 C++/GLM Core 数学模块，实现网格包围盒、八角点仿射包围和射线相交。
- 明确命中参数与距离的区别，盒内/边界起点返回 0，未命中保持输出不变。
- 完成第二周整体交付文档和正式截图，明确场景树、Inspector 与拾取尚未进入实现。

### Testing

- MSVC Debug 与 Release 构建成功，两种配置的 CTest 均为 2/2 通过。
- Debug 固定种子 42：CPU 14 用例、9360 断言；GPU 1 用例、30 断言全部通过。
  GPU 日志的两次空图像拒绝来自预期负向测试，不是初始化失败。
- 新增数学测试覆盖基础几何边界、旋转/负缩放/非均匀缩放、六向射线、平行与近似
  平行、背向、盒内起点、角点擦边、薄平面和无效输入。
- dumpbin 确认 CPU 测试只导入 Windows/MSVC 运行库，不导入 Qt/OpenGL。
- Release 编辑器实际帧缓冲为 852×659、78 种采样颜色，cdb 退出码 0；日志无
  GL_INVALID、GL_OUT_OF_MEMORY、未定义纹理、上传失败或释放失败。
  证据：out/validation/week2-release-final.png、week2-release-final-cdb.log。
- Release PNG 与 Debug 正式截图 SHA-256 一致：
  852E7D903D3D0481EA04ABD8D9FD47566494DDEDA9165690F9FDE85F34677D4F。
- 17 个本轮 C++ 文件通过 clang-format --dry-run --Werror；29 个本轮文本文件通过
  UTF-8 无 BOM/LF 检查；相对起始副本检查确认历史进度记录未改写。
- 首次编码检查误用文化相关 StartsWith 比较不可见 BOM 字符，产生假阳性；读取原始
  字节确认后改用 EF BB BF 三字节检查通过，未对任何文件进行转码。

### Notes

- src/core/Aabb.h、src/core/Aabb.cpp：新增包围盒计算与仿射变换。
- src/core/Ray.h、src/core/Ray.cpp：新增射线与 slab 相交实现及契约。
- src/core/CMakeLists.txt：新增无 Qt/OpenGL 依赖的静态库。
- CMakeLists.txt：将 Core 加入构建。
- src/renderer_gl/CMakeLists.txt：声明 Renderer 对 Core 的依赖。
- src/renderer_gl/MeshData.h：新增 bounds()，同步 UV 和包围盒注释。
- tests/GeometryMathTests.cpp：新增七个纯数学用例。
- tests/CMakeLists.txt：链接 Core、加入数学测试并更新 CPU/GPU 边界注释。
- README.md：更新第二周能力、验证结果和截图入口。
- docs/week2.md：补齐数学契约、最终验收及阶段边界。
- docs/roadmap.md：标记第二周完成及第三周尚未开始。
- docs/architecture.md：同步 Core、材质资源所有权及独立 GPU 测试职责。
- docs/development-setup.md：更新两个测试入口、筛选方式与视口运行判据。
- docs/images/mini3d-week2.png：保存第二周正式视口截图，保留历史截图。
- progress.md：仅追加本次数学与最终验收记录。
- 完整第二周回滚点仍为 out/validation/week2-before-20260908。按本条及上一条文件
  清单，从副本精确恢复原有文件，删除两条记录所列新增文件，可恢复 Day 8；
  不覆盖任何 .user、本机配置或其他构建目录，不运行 git clean。恢复后重新 Configure/Build。
- 本轮未自动开始第三周，不创建 Git 提交或里程碑 Tag；仓库尚无基线提交。

## 2026-09-08 - Task: 第三周场景数据模型

### What was done

- 实现稳定实体标识、纯 CPU 场景树、局部/世界变换与继承可见性。
- 换父保留局部变换并拒绝循环；删除级联子树；拒绝非有限和奇异变换。

### Testing

- MSVC Debug mini3d_tests 构建通过；CPU 19 用例、9413 断言通过（种子 42）。
- 覆盖稳定 ID、父子列表、无效换父、换父后矩阵、负/非均匀缩放、继承隐藏、
  级联删除、中文命名和非法变换不写入。检查 Core CMake 差异，仅新增本阶段源码。

### Notes

- src/core/EntityId.h：定义稳定 ID 与无效值。
- src/core/Transform.h、Transform.cpp：实现 TRS 矩阵与有效性检查。
- src/core/SceneNode.h：定义节点和当前内置几何类型。
- src/core/Scene.h、Scene.cpp：实现场景树关系与受控修改接口。
- src/core/CMakeLists.txt：登记本阶段源码。
- tests/SceneTests.cpp、tests/CMakeLists.txt：加入五个场景测试。
- docs/week3.md：记录场景数据契约与阶段验证。
- progress.md：仅追加本条记录。
- 回滚点为 out/validation/week3-before-20260908。按本条清单从副本恢复原有文件、
  删除本阶段新增文件；不触及 .user 或其他本机配置。仓库尚无 Git 基线，不使用 git clean。

## 2026-09-08 - Task: 第三周场景树、属性编辑与渲染闭环

### What was done

- 用真实 Scene 替换空场景占位树；支持树选择、重命名、勾选可见性和 Create 内置对象。
- 接入独立 ViewModel、ID 选择状态与 Transform Inspector，支持局部 TRS 和 Parent 换父。
- Renderer 只读遍历场景，按父子世界矩阵与继承可见性绘制，镜像缩放调整正面绕序。
- 分离可复用编辑器 UI 库，新增树模型契约、属性无回写循环和实际帧缓冲联动测试。
- 完成第三周文档与整窗截图，未进入导入、鼠标拾取、Gizmo、Undo 或保存加载。

### Testing

- MSVC Debug、Release 构建均通过；两种配置的 CTest 均为 3/3 通过。
- CPU 19 用例/9413 断言、纹理 GPU 1 用例/30 断言、编辑器 3 用例/64 断言通过。
  Debug 开启可选截图时另含 1 条保存断言，共 65 条编辑器断言；直接运行使用种子 42。
- QAbstractItemModelTester 验证模型通知与索引契约；覆盖中文命名、选择同步、创建、
  换父防循环、TRS 编辑、外部数据反向刷新以及非法缩放保持原值。
- 补查发现零缩放虽被拒绝，但错误提示被 refresh 覆盖；先新增失败断言稳定复现，
  再将失败刷新收拢至 operationFailed 回调，复测确认提示保留且无重复业务写入。
- 真实窗口通过鼠标选树、键盘改名、菜单创建和 Parent 控件换父；GPU 读回证明父节点
  移动/隐藏改变画面，恢复操作后像素回到原始结果，负缩放几何仍可见。
- 独立编辑器启动帧缓冲 619×659、79 种采样颜色，cdb 退出码 0；日志无 GL_INVALID、
  GL_OUT_OF_MEMORY、未定义纹理、上传失败或释放失败。证据：
  out/validation/week3-final-viewport.png、week3-final-cdb.log。
- GPU 测试频繁读回产生 NVIDIA Pixel-path performance warning，为验证同步开销提示，
  未将其误记为 GL 正确性错误，也未声称运行日志完全无警告。
- 整窗截图目视确认层级、当前选择、父节点和局部 TRS 与渲染可读；正式 PNG 的 SHA-256：
  37A431F854666F5E6C6CBC889FA87BC2B9A1A0A06FCA3DE71A2022B13E5E53C3。
- 22 个本轮 C++ 文件通过格式检查，32 个文本文件通过 UTF-8 无 BOM/LF 检查；
  文档链接、逐文件差异空白检查、历史进度前缀保护通过。未全量格式化或转码。
- dumpbin 确认 CPU 测试仍不导入 Qt/OpenGL DLL。

### Notes

- src/editor/SelectionModel.h、SelectionModel.cpp：新增稳定 ID 单选状态和变更通知。
- src/editor/SceneViewModel.h、SceneViewModel.cpp：新增演示场景、统一编辑入口和模型通知。
- src/editor/SceneTreeModel.h、SceneTreeModel.cpp：新增 QAbstractItemModel 与精确属性通知。
- src/editor/TransformInspector.h、TransformInspector.cpp：新增名称、可见性、父节点和局部 TRS 控件。
- src/editor/MainWindow.h、MainWindow.cpp：装配模型、树与 Inspector，连接选择和 Create 菜单。
- src/editor/CMakeLists.txt：提取共享 UI 库并开启该库的 AUTOMOC，应用复用 UI 库。
- src/renderer_gl/Renderer.h、Renderer.cpp：从固定位置绘制改为只读 Scene 遍历，复用 GPU 资源。
- src/renderer_gl/ViewportWidget.h、ViewportWidget.cpp：共享只读 Scene 生命周期并在帧回调传给 Renderer。
- CMakeLists.txt：BUILD_TESTING 开启时查找 Qt Test 模块。
- tests/SceneEditorTests.cpp、tests/CMakeLists.txt：新增独立编辑器测试及 ui/gpu 标签。
- README.md：更新第三周能力、当前限制和截图入口。
- docs/week3.md：补齐操作、数据/UI 边界、验收与未实施范围。
- docs/architecture.md：同步 SceneViewModel、UI 库和场景驱动 Renderer。
- docs/roadmap.md：标记第三周完成，第四周尚未开始。
- docs/development-setup.md：更新三个测试入口、Qt Test 要求与实际操作判据。
- docs/images/mini3d-week3.png：新增包含场景树、Inspector 与视口的正式截图。
- progress.md：仅追加本轮实现与最终验收记录。
- out/validation/Validate-Week3.ps1：被忽略的定向编码、格式、文档及日志验证脚本。
- 完整第三周回滚点为 out/validation/week3-before-20260908。按本条与上一条清单
  从副本逐文件恢复既有文件，删除本轮新增源码/测试/文档/截图即可回到第二周。
  保留用户 .user、本机路径配置和其他构建目录，恢复后重新 Configure/Build。
- 当前换父保留局部变换，树重置后部分分支折叠；旋转显示等价欧拉角，输入精度三位小数。
  编辑只在内存中，关闭不保存，尚无 Undo。未创建 Git 提交或 Tag。

## 2026-09-08 - Task: 第四周 CPU 资源与 glTF 解析

### What was done

- 新建 Assets CPU 模块，解析默认场景层级、TRS/Matrix、三角形、基础颜色和 PNG/JPEG。
- 将共享网格布局下沉 Core，保留 Renderer 类型别名，避免 Assets 反向依赖渲染器。
- 添加规范化路径缓存，成功解析后分配稳定资源 ID；失败保持资源库不变。
- 引入 Box、BoxTextured、Duck 固定样例及署名/许可，未修改原始 GLB。

### Testing

- MSVC Debug Assets 与 asset_tests 构建通过；5 用例、110 断言通过，随机种子 42。
- 覆盖官方 GLB、外部 PNG/JPEG、Unicode 文件路径、交错步长、负缩放 Matrix、缺失法线、
  多 primitive 和实例复用；覆盖 accessor 越界、错误索引、循环层级、剪切矩阵和图片降级。
- BoxTextured 首次下载 curl 56（连接重置），切换 HTTP/1.1 后成功；未修改代理设置。
- 首次读取多段源码时 Select-Object 收到嵌套数组，改为先读数组再逐段索引，未重复失败结构。

### Notes

- src/core/AssetId.h：定义资源 ID 和 MeshRendererComponent。
- src/core/MeshData.h、src/renderer_gl/MeshData.h：共享网格布局迁入 Core，旧路径保留别名。
- src/assets/MeshAsset.h、TextureAsset.h、MaterialAsset.h：定义 CPU 几何、图片/采样器和材质。
- src/assets/GltfImporter.h、GltfImporter.cpp：实现静态导入、边界验证及错误定位。
- src/assets/AssetManager.h、AssetManager.cpp：实现规范化源缓存与局部/全局资源 ID 重映射。
- src/assets/CMakeLists.txt、src/core/CMakeLists.txt、CMakeLists.txt：登记 CPU 资源 Target 与源码。
- tests/GltfImporterTests.cpp、tests/CMakeLists.txt：新增无窗口 Assets 测试。
- assets/samples/Box.glb、BoxTextured.glb、Duck.glb：原样保存官方固定验收模型。
- docs/sample-assets.md：记录来源、署名、许可、下载日期与 SHA-256。
- docs/licenses/CC-BY-4.0.txt、SCEA.txt、LicenseRef-LegalMark-Cesium.txt：原样保存第三方许可。
- docs/week4.md：记录导入子集与阶段测试。
- progress.md：仅追加本条记录。
- 回滚点：out/validation/week4-before-20260908。按本条清单恢复已有文件并删除新增文件，
  不触及用户 .user 或本机配置。out/validation/Prepare-Week4.ps1 和下载原件是被忽略的验证产物。

## 2026-09-08 - Task: 第四周导入场景与 GPU 渲染闭环

### What was done

- 接入 File → Import glTF / GLB（Ctrl+I），导入后创建独立场景实例并自动树选中。
- 导入模型复用既有名称、可见性、父子关系和 Inspector 局部 TRS 编辑，失败保持场景及选择。
- 以稳定 AssetId 关联 CPU 资源和场景；Renderer 在有效 Context 中按 ID 缓存 GPU 网格/纹理。
- 接通采样器和双面材质，保留内置几何；成功/失败写入 Console，成功状态替换旧错误提示。
- 扩充文件与实际 UI/帧缓冲回归，更新第四周契约、使用说明、路线图和正式截图。
- 本轮仅完成第四周；未实施第五周拾取、高亮/Focus，也未加入 Gizmo、Undo 或保存。

### Testing

- MSVC 2022 Debug/Release 全量 Build 成功，各自 CTest 4/4 通过。构建目录为
  out/build/opengl-viewport-verify；状态栏修正后两种配置均重新构建并通过全套测试。
- 最终 Debug 种子 42：纯 CPU 19 用例/9413 断言，Assets 7/148，GPU 1/36，
  UI 4/100（包含截图保存 1 条；普通执行 4/99），普通总计 31 用例/9696 断言。
- Assets 新增 U8/U32/无索引、归一化 U8 UV、损坏 GLB、缺失外部缓冲和必需压缩扩展拒绝；
  保留 U16、PNG/JPEG、Unicode、Matrix、多 primitive、缓存与非法层级回归。
- UI 验证真实菜单接线、文件输入与 Open 点击、树选中、Inspector 变换、重复导入资源复用、
  继承隐藏的帧缓冲一致性，以及失败后再成功导入时状态栏恢复。使用非原生 Qt 文件框测试，
  正式应用仍为原生文件框，未声称覆盖 Windows 文件框内部实现。
- 文件框自动化起初用 selectFile/accept，孤立执行超时；改查文件模型就绪仍失败。
  查明没有完成真实选择后，改为文件名输入框键盘输入加 Open 点击，孤立和完整测试通过，
  随后 Debug 全套连续通过三轮；之后最终扩展用例和状态栏修正亦通过 Debug/Release。
- 查看正式截图，确认贴图方块、Duck、红色 Box、层级与 Inspector 均可见，
  Console 的 missing.glb 是预期负例；最新状态栏显示成功导入 Box。
- out/validation/week4-editor-tests-final.log 无 GL_INVALID、GL_OUT_OF_MEMORY、
  纹理不完整、资源上传或删除失败；同步帧缓冲读取的驱动性能提示不属于 GL 正确性错误。
- dumpbin /dependents 确认纯 CPU mini3d_tests.exe 仅依赖 Windows/MSVC 运行库，无 Qt/GL DLL。
- Validate-Week4.ps1 通过：26 个变更 C++ 文件格式，38 个变更自写文本 UTF-8 无 BOM/LF，
  文档链接、上游样例/许可字节一致性及 progress 历史前缀不变。未全量格式化/转码。
- 验证脚本首次误将 git diff --no-index --check 的 1 当成空白错误；实际诊断输出为空，
  检查差异确认仅内容变化，修正为区分差异退出码与错误输出后通过，未因此改动业务文件。
- 正式截图 SHA-256：29A9E75262204C11EB10CC97927F31B1478C543349F98C9D96B7BE7431BD8E70。

### Notes

- src/core/SceneNode.h：增加可选网格/材质资源引用。
- src/core/Scene.h：声明受控网格绑定接口。
- src/core/Scene.cpp：验证实体/网格 ID 并切换到导入网格节点。
- src/editor/SceneViewModel.h：声明资源共享、导入意图与完成信号。
- src/editor/SceneViewModel.cpp：完成 CPU 导入后实例化层级并更新选择/日志。
- src/editor/MainWindow.cpp：新增导入菜单、只读资源装配、Console 和状态栏通知。
- src/renderer_gl/ViewportWidget.h：声明只读资源共享接口与成员。
- src/renderer_gl/ViewportWidget.cpp：切换资源库时清理缓存并传入绘制。
- src/renderer_gl/Renderer.h：声明按 AssetId 索引的 GPU 缓存与导入绘制接口。
- src/renderer_gl/Renderer.cpp：实现懒上传、资源复用、材质配置与 Context 内销毁。
- src/renderer_gl/GpuTexture.h：增加兼容默认值的采样器参数。
- src/renderer_gl/GpuTexture.cpp：上传时应用 glTF 过滤和环绕方式。
- src/renderer_gl/Material.h：增加双面标记。
- src/renderer_gl/CMakeLists.txt：连接 CPU Assets Target。
- tests/GltfImporterTests.cpp：在首阶段测试基础上扩充索引/UV 与损坏文件回归。
- tests/GpuTextureTests.cpp：验证自定义采样器的实际 GL 状态。
- tests/SceneEditorTests.cpp：新增菜单导入、场景编辑、缓存、错误恢复与截图测试。
- tests/CMakeLists.txt：向 UI 测试提供固定样例路径。
- README.md：更新第四周结果、入口、截图和文档索引。
- docs/week4.md：补齐场景/GPU 契约、操作、限制与最终验收。
- docs/architecture.md：同步 Assets、MeshRendererComponent 和 GPU 缓存边界。
- docs/roadmap.md：标记第四周完成，第五周等待明确指示。
- docs/development-setup.md：同步四类测试与模型导入操作。
- docs/images/mini3d-week4.png：新增最终整窗导入验收截图。
- progress.md：仅追加本轮实现与验收记录，不改写首阶段及历史记录。
- out/validation/Validate-Week4.ps1：被忽略的定向只读验收脚本。
- out/validation/week4-editor-tests-final.log、week4-import-accepted.png：被忽略的最终 UI 验证产物；
  此前调试日志和截图仍保留，未覆盖历史证据。
- 整个第四周回滚点为 out/validation/week4-before-20260908（66 个正式文件）。
  按本条与上一条清单逐文件从快照恢复已有文件，移出本轮新增 Assets、测试、样例、许可、
  文档及截图后重新 Configure/Build，即可回到第三周；不触及 .user、本机配置或其他产物。
  仓库无 Git 基线，未创建提交/Tag，不使用 git clean/reset 回滚。
- 导入同步执行，大文件可能卡顿；路径缓存无热重载/卸载，源文件修改后需重启。
  Alpha/PBR/动画等限制详见 docs/week4.md；编辑仍仅在内存中。

## 2026-09-09 - Task: 第五周 CPU 拾取与聚焦数学

### What was done

- 生成视口世界射线，保留近裁剪面起点与统一坐标尺度。
- 统一内置/导入资源局部盒、可见子树世界盒和最近几何拾取；局部射线保持参数尺度。
- 聚焦保留观察方向，适配横竖视口，并扩展大模型的观察距离和远裁剪面。

### Testing

- 第四周 Debug 基线 CTest 4/4 通过，施工前保存 86 个正式文件快照。
- 两个无窗口目标 Debug Build 与 headless CTest 2/2 通过；新增屏幕射线、
  大小模型聚焦、最近/等距命中、隐藏/空节点、变换参数和导入实例测试。
- 定向 clang-format 完成，并检查 EditorCamera 相对快照差异，无无关改动。
- 前序读取方案时 Select-Object 的双范围组成嵌套数组而失败；改为读取数组后分别索引，
  未重复失败命令。方案明确本周采用 AABB 高亮/拾取，未扩展到三角形检测或 Gizmo。

### Notes

- src/renderer_gl/EditorCamera.h、EditorCamera.cpp：增加屏幕射线、聚焦和大模型观察范围。
- src/renderer_gl/RayCaster.h、RayCaster.cpp：新增无 OpenGL 调用的包围/拾取查询。
- src/renderer_gl/CMakeLists.txt：登记 RayCaster 源码。
- tests/EditorCameraTests.cpp：新增射线与 Fit 测试。
- tests/RayCasterTests.cpp：新增最近命中、变换、可见性和导入实例用例。
- tests/CMakeLists.txt：把 CPU RayCaster 纳入无窗口资源测试，未引入 GL 依赖。
- docs/week5.md：记录坐标、距离、可见性和聚焦契约。
- progress.md：仅追加本条记录。
- out/validation/Prepare-Week5.ps1：被忽略的快照与编码检查脚本。
- 回滚点为 out/validation/week5-before-20260909；按清单逐文件恢复既有文件，
  移出新增 RayCaster、测试及 week5 文档后重新 Build。不操作 .user、本机配置或 Git 历史。

## 2026-09-09 - Task: 第五周视口选择、高亮与聚焦闭环

### What was done

- 打通无修饰左键点击到唯一 SelectionModel 的选择通路，树/Inspector/视口同步，空白取消。
- 新增橙色世界 AABB 覆盖层，选中导入根时包围可见子树；高亮不修改材质或深度缓冲。
- 接入视口/树的 F 与 View → Focus Selection；保留观察方向并框选可见几何，不抢占文字输入。
- 限制左键长位移不拾取，中键导航不改选择；统一 Qt 逻辑像素与射线坐标，覆盖窗口缩放。
- 完成测试、第五周文档和正式截图；未加入 Gizmo、复制删除、Undo 或保存，第六周等待指示。

### Testing

- MSVC 2022 / Qt 6.8.3 Debug、Release 全量 Build 成功，各自 CTest 4/4 通过。
  验证目录 out/build/opengl-viewport-verify；最终新增 Resize/DPI 断言后两种 UI 目标重建、
  Release 全套通过，Debug 四个入口均连续通过三轮。
- 种子 42 最终普通执行：CPU 21 用例/9654 断言，Assets/拾取 11/187，GPU 2/53，
  UI 6/157，共 40 用例/10051 断言。前两阶段 CPU 和 GPU 断言数亦由直接执行核对。
- UI 新增最近模型点击、树选择回传、名称刷新、空白清空当前/选中行、隐藏后命中后方物体、
  左键拖动和 Ctrl 点击不改变对象、中键 Orbit 后选择保持、旋转再 Resize 后投影点击一致。
- F 用实际键盘事件验证树与视口两种焦点；Inspector 输入 F 正常且不聚焦，
  隐藏/无选择不改相机，菜单命令可聚焦导入根，聚焦后点击选中真实几何子节点。
- QT_SCALE_FACTOR=2 下完整 UI 测试通过，并断言实际 devicePixelRatioF == 2；
  非默认 DPI 多一条断言。不声称覆盖所有显卡、多显示器或任意模型规模。
- GPU 离屏 FBO 验证橙色线段实际像素、空盒不绘制、深度状态恢复，以及资源删除和重建。
- 查看 out/validation/week5-selection-debug.png 并复制为正式截图，确认 Duck 根在 X=-10
  时仍可完整聚焦且包围盒可见。截图阶段 UI 6/155 含保存断言，此后新增三个操作断言，
  当前普通 UI 6/157；最终业务代码未再改变，未覆盖此前截图证据。
- 初次 GPU 编译遇到 C2039：QOpenGLFunctions_4_1_Core 不提供 glVertexAttrib3f；
  查询本机 Qt 头文件确认后，改为顶点位置/颜色共同存入固定 VBO，重新构建并通过 GPU/UI。
  初次失败后误启动的 CTest 使用旧二进制，其结果不计入第五周验证证据。
- dumpbin 确认纯 CPU mini3d_tests.exe 无 Qt 或 OpenGL DLL 依赖。
- 定向差异和 Validate-Week5.ps1 检查通过：17 个变更 C++ 文件格式、25 个自写文本
  UTF-8 无 BOM/LF、文档链接、progress 历史前缀与 GL 日志。未全量格式化或转码。
- out/validation/week5-editor-final.log 无 GL_INVALID、GL_OUT_OF_MEMORY、纹理不完整、
  GPU 上传/删除失败；Pixel-path 性能提示来自测试同步读取帧缓冲。
- 正式截图 SHA-256：575E34626A0C6189E0392ED9FBAB065C14090C7BFE3D5C316C73FAA1D890B596。

### Notes

- src/renderer_gl/SelectionRenderer.h：声明独占线框资源的生命周期和绘制接口。
- src/renderer_gl/SelectionRenderer.cpp：创建固定单位盒线段，绘制橙色覆盖层并恢复深度状态。
- src/renderer_gl/Renderer.h：增加只读相机、实体聚焦与高亮成员接口。
- src/renderer_gl/Renderer.cpp：接入选择包围盒绘制、聚焦和资源生命周期。
- src/renderer_gl/ViewportWidget.h：增加拾取信号、选择 ID 镜像和点击状态。
- src/renderer_gl/ViewportWidget.cpp：实现逻辑像素点击、短点击判定、高亮刷新和聚焦入口。
- src/renderer_gl/CMakeLists.txt：登记 SelectionRenderer 并启用 Viewport 信号所需 AUTOMOC。
- src/editor/SceneViewModel.h：声明射线选择意图。
- src/editor/SceneViewModel.cpp：查询最近命中并写入统一选择模型。
- src/editor/MainWindow.cpp：装配选择信号、高亮订阅与作用域限定的 F 菜单动作。
- tests/GpuTextureTests.cpp：新增选中线框像素、状态与生命周期用例。
- tests/SceneEditorTests.cpp：新增拾取/聚焦、输入隔离、Resize 和高 DPI 验收；既有像素等值
  回归显式恢复相同选择，避免新增高亮令不同选择的图像不再可比。
- README.md：更新第五周能力、限制、截图和文档入口。
- docs/week5.md：补齐操作、UI/GPU 契约、近似选择限制和最终测试计数。
- docs/architecture.md：记录 CPU RayCaster、统一选择、SelectionRenderer 和 Focus 分层。
- docs/development-setup.md：更新测试规模与左键/F 操作指引。
- docs/roadmap.md：标记第五周完成，第六周尚未开始。
- docs/images/mini3d-week5.png：新增聚焦与选中包围盒正式截图。
- progress.md：仅追加本轮结果和证据，不改写前阶段及历史记录。
- out/validation/Validate-Week5.ps1：被忽略的定向只读验收脚本。
- out/validation/week5-editor-debug.log、week5-editor-final.log、week5-selection-debug.png：
  被忽略的阶段/最终测试日志和图像证据，保留原始记录。
- 整个第五周回滚点为 out/validation/week5-before-20260909（86 个正式文件）。
  按本条与上一条清单从快照逐文件恢复已有文件，移出新增 RayCaster/SelectionRenderer、
  RayCasterTests、week5 文档/截图后重新 Configure/Build。保留 .user、本机配置与构建产物。
  仓库仍无 Git 基线，未创建提交或 Tag，不使用 git clean/reset 回滚。
- AABB 近似选择可能命中模型表面外的盒内区域；空容器只能在树选中。极小模型沿用
  最小观察距离，未增加空间加速结构；具体限制见 docs/week5.md。

## 2026-09-09 - Task: 第六周阶段 1 - 变换撤销基础

### What was done

- 接入 QUndoStack 和 TransformEntityCommand，变换通过命令回放且不递归入栈。
- 无变化/非法变换不产生历史，撤销后新编辑丢弃 redo 分支；未纳入撤销的修改清空历史。

### Testing

- 第五周 Debug 基线 4/4，通过后保存 93 文件快照；代码 UTF-8 无 BOM/LF。
- Debug 编辑器测试目标构建成功；command 用例通过，验证撤销、重做、无效/无变化及历史边界。
- 定向格式化并检查 ViewModel 差异，未改动用户本机配置。

### Notes

- src/editor/TransformEntityCommand.h、TransformEntityCommand.cpp：新增变换快照命令。
- src/editor/SceneViewModel.h、SceneViewModel.cpp：拥有历史与受控回放接口。
- src/editor/CMakeLists.txt：登记命令源码。
- tests/EditorCommandTests.cpp、tests/CMakeLists.txt：加入命令回归。
- docs/week6.md：记录撤销范围与历史边界。
- progress.md：仅追加阶段记录。
- out/validation/Prepare-Week6.ps1：被忽略的快照和编码检查脚本。
- 回滚点 out/validation/week6-before-20260909；逐文件恢复既有文件并移出本阶段新增文件，
  再重新构建；不使用 git clean/reset，不触及 .user 或本机配置。

## 2026-09-09 - Task: 第六周阶段 2 - 世界空间三轴手柄

### What was done

- 增加固定屏幕尺寸的三轴覆盖层、轴拾取和悬停高亮，W 在树/视口切换移动工具。
- 准备冻结平面及世界位移转父节点局部位移的纯 CPU 计算，供下一阶段接入拖动。

### Testing

- 全量 Debug 构建成功，CTest 4/4 通过；包含轴拾取/尺寸及 GPU 手柄绘制状态恢复检查。
- 本阶段改动 C++ 已定向 clang-format，保留 UTF-8 无 BOM/LF。

### Notes

- src/renderer_gl/GizmoController.h、GizmoController.cpp：新增手柄拾取与轴拖动计算。
- src/renderer_gl/GizmoRenderer.h、GizmoRenderer.cpp：新增三轴 GPU 覆盖层。
- assets/shaders/gizmo.vert、gizmo.frag：新增单色手柄着色器。
- src/renderer_gl/EditorCamera.h、EditorCamera.cpp：增加逻辑像素到世界长度计算。
- src/renderer_gl/Renderer.h、Renderer.cpp：管理手柄资源与选中对象原点。
- src/renderer_gl/ViewportWidget.h、ViewportWidget.cpp：增加工具开关与悬停拾取。
- src/editor/MainWindow.cpp：接入限定焦点范围的 W 菜单动作。
- src/renderer_gl/CMakeLists.txt：登记新模块和着色器。
- tests/GizmoTests.cpp、GpuTextureTests.cpp、CMakeLists.txt：增加 CPU/GPU 验证。
- docs/week6.md：记录工具使用方式；progress.md：追加阶段结果。
- 回滚点 out/validation/week6-before-20260909；按上述清单恢复既有文件、移出新增文件，
  再执行 CMake 构建。保留本机配置和 .user，不使用 git clean/reset。

## 2026-09-09 - Task: 第六周阶段 3 - 轴拖动预览、提交与取消

### What was done

- 接通手柄优先命中、实时预览、一次提交和 Esc/中断取消；拖动期间不改变相机。
- 接入 Edit 撤销/重做及限定焦点的快捷键，预览不污染历史和 redo 分支。

### Testing

- Debug 完整构建和 CTest 4/4 通过，新增父旋转/负非均匀缩放的三轴计算回归。
- 真实窗口鼠标拖动、单条历史、Ctrl+Z/Y、Esc、失焦恢复及帧缓冲往返一致通过。
- 初次构建发现新测试仅含 MainWindow 前置声明，补直接 SceneViewModel include 后构建通过。
- 定向差异检查与格式化完成；未触及既有 SceneEditorTests 的 CRLF 编码。

### Notes

- src/editor/SceneViewModel.h、SceneViewModel.cpp：增加可取消预览事务和历史入口互斥。
- src/renderer_gl/ViewportWidget.h、ViewportWidget.cpp：增加拖动状态与生命周期中断处理。
- src/editor/MainWindow.cpp：连接移动意图和 Edit 菜单。
- tests/GizmoTests.cpp：增加父变换下的三轴拖动数学检查。
- tests/EditorCommandTests.cpp：增加预览提交/取消与 redo 分支检查。
- tests/GizmoEditorTests.cpp：新增真实窗口拖动验收；tests/CMakeLists.txt：登记用例。
- docs/week6.md：记录事务和快捷键；progress.md：追加结果。
- 回滚点 out/validation/week6-before-20260909；逐文件恢复清单中既有文件，移出新增文件，
  再重新 Configure/Build；不删除构建目录或用户配置，不使用 git clean/reset。

## 2026-09-09 - Task: 第六周阶段 4 - 子树复制、删除与撤销

### What was done

- 复制生成独立节点并共享资源引用，删除与恢复保留原实体 ID、层级和兄弟顺序。
- 接入 Ctrl+D/Delete 与 Edit 菜单；混合变换、复制、删除可连续撤销重做。

### Testing

- Debug 构建成功，CTest 4/4 通过。
- Core 检查快照冲突原子拒绝、父丢失拒绝、资源引用共享、原 ID/顺序恢复和单调 ID。
- 命令回归验证 glTF 子树、资源数量不变及四条混合历史往返；树模型契约检查通过。
- 真实窗口 Ctrl+D/Delete/Ctrl+Z 正常；Inspector Delete/Ctrl+Z 编辑文本，不误删场景对象。
- 定向检查 Scene 差异并格式化本阶段 C++，未改动外部协议、数据库或线程路径。

### Notes

- src/core/Scene.h、Scene.cpp：新增封装的内存子树快照、恢复与复制。
- src/editor/SubtreeCommand.h、SubtreeCommand.cpp：新增结构命令回放。
- src/editor/SceneViewModel.h、SceneViewModel.cpp：增加复制/删除选中对象意图。
- src/editor/MainWindow.cpp：增加对象管理动作与焦点限定快捷键。
- src/editor/CMakeLists.txt：登记结构命令源码。
- tests/SceneTests.cpp、EditorCommandTests.cpp、GizmoEditorTests.cpp：增加数据、命令和 UI 回归。
- docs/week6.md：记录复制、删除与缓存契约；progress.md：追加本阶段结果。
- 回滚点 out/validation/week6-before-20260909；按清单恢复原文件，移出新增命令文件后重建。
  不使用 git clean/reset，不触及本机配置与用户 .user 文件。

## 2026-09-09 - Task: 第六周阶段 5 - 双配置回归、使用文档与交付截图

### What was done

- 补齐拖动期间滚轮冻结、窗口失活、丢失捕获、Resize 和关闭工具的取消回归。
- 同步当前能力、快捷键、历史边界及验收方法；第六周五阶段完成，未进入第七周。
- 生成并查看真实窗口截图：两个共享资源的 BoxTextured 实例、三轴手柄和同步 Inspector。

### Testing

- Debug/Release 完整构建成功，分别 CTest 4/4 通过；Debug Editor 连续三次通过。
- 固定 seed 42：CPU 24 用例/9722 断言、Assets 11/187、GPU 2/56、Editor 11/240 全部通过。
- QT_SCALE_FACTOR=2：Editor 11 用例/242 断言通过，包含真实 devicePixelRatio 检查。
- 截图模式 gizmo-ui 2 用例/45 断言通过；截图由 MainWindow.grab() 生成并人工视觉核对。
- 截图 SHA256：D3D1F5E38FB49680148A3829EDC9218CD443F210A467D2E2D094640CE460C4BE。
- dumpbin /dependents 显示 CPU 测试只依赖 Windows/MSVC 运行库，无 Qt/OpenGL。
- 定向校验通过：24 个 C++ 格式、35 个 UTF-8 无 BOM/LF 文本、文档链接、diff 空白与日志仅追加。
- GPU/UI 日志未发现 GL_INVALID、GL_OUT_OF_MEMORY、资源上传/删除或初始化错误；
  帧缓冲读回的 Pixel-path 同步性能提示保留，未当成错误隐藏。
- 读取核查中修正了测试资源路径和文档未定义的 Release 测试 Preset；
  rg 的 Windows 通配路径错误改用目录配合 -g，未重复失败结构。

### Notes

- tests/GizmoEditorTests.cpp：补齐交互中断验证与显式环境变量控制的截图入口。
- README.md：更新第六周使用入口、截图与历史限制。
- docs/week6.md：汇总五阶段能力、验证计数、边界、回滚和本机复验命令。
- docs/roadmap.md：标记第六周完成及 V1 尚缺内容，第七周未开始。
- docs/architecture.md：记录意图流、命令/快照和 Core/GPU 资源边界。
- docs/development-setup.md：更新快捷键与四组测试的当前覆盖范围。
- docs/images/mini3d-week6.png：新增真实窗口交付截图。
- progress.md：仅追加本阶段结果，保留前五周及本周各阶段历史。
- out/validation/Test-Week6.ps1、Validate-Week6.ps1：被忽略的执行与只读核验脚本。
- out/validation/week6-mini3d_tests.log、week6-mini3d_asset_tests.log、week6-mini3d_gpu_tests.log、
  week6-mini3d_editor_tests.log、week6-editor-dpi2.log、week6-capture.log：被忽略的验证证据。
- 周前回滚点 out/validation/week6-before-20260909（93 个正式文件）；按五阶段清单恢复
  快照已有文件，移出本周新增 GizmoController/GizmoRenderer、TransformEntityCommand/
  SubtreeCommand、三个新测试文件、gizmo Shader 和 week6 文档/截图，再 Configure/Build。
  保留用户配置/.user 与旧构建产物；仓库无 Git 提交基线，未创建提交或 Tag。
- 尚无保存加载；变换、复制、删除之外的成功编辑会清空历史，未宣称 V1 全部完成。
- 收尾只读检查 `ctest --preset test-debug-local -C Release --show-only` 可解析四个入口，
  但提示 windows-msvc-local 尚无 Release 测试产物；该目录未执行本轮构建。
  本轮全部通过证据来自 opengl-viewport-verify，不把 Preset 列举视为测试通过。

## 2026-09-09 - Task: 第七周阶段 1 - 光照与实例基础材质

### What was done

- 增加全局单方向光、环境项、每实例乘色和纹理/顶点色开关，Inspector 可编辑且支持撤销。
- 保持默认画面和资源共享，双面材质背面法线翻转；未新增阴影、PBR 或 Light 实体。

### Testing

- 第六周 Debug 基线 CTest 4/4 通过；保存 108 个正式文件周前快照。
- Debug 完整构建和 CTest 4/4 通过；GPU 检查方向光/环境项变化，命令检查撤销和非法零方向。
- 定向格式化 C++ 并检查 Renderer 差异；既有源码 UTF-8 无 BOM/LF。

### Notes

- src/core/Appearance.h：新增光照和实例样式值类型及验证。
- src/core/SceneNode.h、Scene.h、Scene.cpp：保存外观/光照，复制保持样式。
- src/editor/EditCommand.h：新增按值捕获的简单属性回放命令。
- src/editor/AppearanceInspector.h、AppearanceInspector.cpp：新增外观与光照编辑视图。
- src/editor/SceneViewModel.h、SceneViewModel.cpp：新增可撤销光照/外观意图。
- src/editor/MainWindow.cpp：Inspector 使用滚动容器并装配外观视图。
- src/editor/CMakeLists.txt：登记新增模块。
- src/renderer_gl/Renderer.h、Renderer.cpp：写入场景光照并叠加实例样式。
- assets/shaders/mesh.frag：参数化 Lambert 光照及背面法线。
- tests/GpuTextureTests.cpp、EditorCommandTests.cpp：新增光照和外观验证。
- docs/week7.md：记录阶段能力；progress.md：仅追加记录。
- out/validation/Prepare-Week7.ps1：被忽略的周前快照脚本。
- 回滚点 out/validation/week7-before-20260909；按清单恢复已有文件并移出新增文件后重建，
  不使用 git clean/reset，不触及 .user、本机配置和用户资源。

## 2026-09-09 - Task: 第七周阶段 2 - 场景文档、相对资源和完整编辑历史

### What was done

- 新增版本 1 UTF-8 场景编解码、原子保存、临时加载验证和资源 ID 重映射。
- 保存节点/层级/样式/光照/观察相机；资源使用相对路径，不复制模型或 GPU 数据。
- 创建、导入、重命名、显隐、换父加入历史，保存点和相机变化共同管理未保存状态。

### Testing

- Debug 完整构建、CTest 4/4 通过；定向差异检查和源码格式化完成。
- JSON 往返及未知字段通过；未知版本、缺字段、重复/负 ID、循环、孤儿、非法 TRS、
  非法相机/光照和损坏 JSON 拒绝且不修改输出。
- 临时目录保存/整体移动/重开成功，实例共享网格；缺失资源保持旧场景/资源库/路径。
- 写入不存在目录失败后保持脏状态；创建/导入撤销重做、换父恢复兄弟顺序、撤销至保存点通过。

### Notes

- src/core/SceneSerializer.h、SceneSerializer.cpp：新增纯 CPU JSON 文档及相机状态。
- src/core/Scene.h、Scene.cpp：导出/验证替换节点及兄弟顺序恢复。
- src/core/CMakeLists.txt：登记序列化并使用已安装的 nlohmann/json，不新增依赖包。
- src/assets/AssetManager.h、AssetManager.cpp：记录网格源文件和 primitive 序号。
- src/assets/SceneDocument.h、SceneDocument.cpp：新增原子 IO/资源重建服务。
- src/assets/CMakeLists.txt：登记文档服务。
- src/renderer_gl/EditorCamera.h、EditorCamera.cpp：导出和恢复相机状态。
- src/editor/SubtreeCommand.h、SubtreeCommand.cpp：创建/导入的子树历史与稳定 ID 回放。
- src/editor/SceneViewModel.h、SceneViewModel.cpp：新建/打开/保存、脏状态和补齐属性历史。
- tests/SceneSerializerTests.cpp、SceneDocumentTests.cpp：新增文档/历史验收。
- tests/EditorCommandTests.cpp、GizmoEditorTests.cpp：历史基数改为包含创建/导入，保留旧交互断言。
- tests/CMakeLists.txt：登记新测试；docs/week7.md：记录正式文件/历史契约。
- progress.md：仅追加记录。
- 回滚点 out/validation/week7-before-20260909；逐文件恢复已有文件并移出新增模块后重建，
  不改用户配置、资源或任何网络/线程路径。

## 2026-09-09 - Task: 第七周阶段 3 - 文档操作、未保存保护与整合验收

### What was done

- 完成新建、打开、保存、另存为及关闭保护；取消对话框或保存失败时保留当前编辑。
- 标题显示未保存状态，观察视角与场景一起恢复；替换文档资源库时刷新视口 GPU 缓存。
- 补齐真实对话框、跨目录另存、相机恢复与极限导航测试，修正销毁通知和属性容器截断。
- 同步第七周操作/架构/范围文档及真实窗口截图，未自动进入第八周。

### Testing

- 独立目录 out/build/opengl-viewport-verify：Debug/Release 完整构建成功，各自 CTest 4/4 通过。
- Debug UI 连续三次通过；固定 seed 42：CPU 28 用例/9929 断言、Assets 11/187、GPU 2/58、
  Editor 17/343；QT_SCALE_FACTOR=2 为 Editor 17/346，验证实际 DPI 与 Inspector 无横向截断。
- 截图模式 document-ui 3 用例/46 断言通过，MainWindow.grab() 图片经视觉核对字段完整。
- 截图 SHA256：FB94B08B36315D27830501493FFE46156EB8A834E3A2CA771D0C782C496A840B。
- 文档 UI 验证 Ctrl+S、另存子目录、重新打开后画面/相机/样式一致；取消新建/关闭、
  保存对话框取消、写入失败阻止关闭、明确丢弃和成功保存后关闭全部通过。
- 首次 UI 验证遇到 QUndoStack 析构通知 MainWindow 的 Qt 类型断言；已在 ViewModel 析构断开通知。
  修复后文件对话框因完整路径补全未提交：改先进入目录再输入文件名，并增加超时保护。
- 原始往返截图有 10/561468 个像素差异，最大通道差 63、平均通道差 0.000582。
  隔离相机重建确认浮点舍入影响边缘；相同重建条件下继续严格逐像素相等，未放宽图像断言。
  新增相机 CPU 对照验证普通/大模型位置、投影矩阵及视距恢复。
- 极限缩放新测试复现相机正常状态被拒绝，定位长度重算略超上限；加入 1e-6 相对长度容差后，
  20 组方向、近远极限及再次恢复有效，非法距离仍拒绝；相机文档子集 2 用例/166 断言通过。
- dumpbin /dependents 确认 CPU 测试仅依赖 Windows/MSVC 运行库，无 Qt/OpenGL。
- 定向验证通过：33 个 C++ 文件 clang-format、44 个 UTF-8 无 BOM/LF 文本、文档链接、
  diff 空白、progress 仅追加；检查 Scene/Viewport/MainWindow 等相关差异，未全量格式化或转码。
- GPU/UI 日志无 GL_INVALID、GL_OUT_OF_MEMORY、上传/删除或初始化错误；截图同步读回的
  Pixel-path 性能提示为预期记录，未隐瞒为无日志。验证仅使用临时项目，未覆盖用户模型。

### Notes

- src/editor/MainWindow.h：声明文档保存、确认关闭和标题刷新入口。
- src/editor/MainWindow.cpp：装配 File 操作、相机通知、关闭保护和 Inspector 自适应最小宽度。
- src/editor/SceneViewModel.h：声明析构函数，配合历史通知生命周期管理。
- src/editor/SceneViewModel.cpp：析构前断开撤销栈通知，避免回调已析构窗口。
- src/renderer_gl/ViewportWidget.h：增加文档相机恢复接口、导航信号和初始化前待应用状态。
- src/renderer_gl/ViewportWidget.cpp：导航发布相机状态，新建/打开恢复视角并处理初始化顺序。
- src/renderer_gl/Renderer.h：声明相机状态恢复入口。
- src/renderer_gl/Renderer.cpp：转发文档相机状态到 EditorCamera。
- src/core/SceneSerializer.cpp：移除本轮无用 include，修复极限视距浮点舍入的误拒绝。
- tests/DocumentEditorTests.cpp：新增真实文件操作、渲染往返、关闭保护、布局和显式截图验收。
- tests/EditorCameraTests.cpp：补充普通/大模型相机往返和极限导航持久化验证。
- tests/SceneEditorTests.cpp：原有四处清理改 hide，未保存关闭专由新测试验证。
- tests/CMakeLists.txt：登记文档 UI 测试源。
- README.md：更新当前能力、操作、截图和未完成范围。
- docs/week7.md：记录三阶段交付、文档契约、失败修复、验证证据和复验/回滚说明。
- docs/roadmap.md：标记第七周完成，第八周及 Light/Camera 场景实体仍未完成。
- docs/architecture.md：更新序列化、资源重建、MVVM 和历史/相机边界。
- docs/development-setup.md：更新文件操作、材质光照、快捷键与测试入口。
- docs/images/mini3d-week7.png：保存跨目录另存重开后的真实编辑器截图。
- progress.md：仅追加本阶段结果，保留已有阶段记录。
- out/validation/Compare-Week7Frames.ps1：被忽略的隔离截图误差诊断脚本。
- out/validation/Test-Week7.ps1：被忽略的双配置/重复 UI/DPI/截图顺序回归脚本。
- out/validation/Validate-Week7.ps1：被忽略的定向格式、编码、链接、历史和 GL 日志只读检查脚本。
- out/validation/week7-*.log、week7-roundtrip-before.png、week7-roundtrip-after.png：被忽略的测试/诊断证据。
- 回滚点 out/validation/week7-before-20260909（108 个正式文件）：按本周三阶段清单恢复快照已有文件，
  移出新增 Appearance、EditCommand、SceneSerializer、SceneDocument、对应新测试及 week7 文档/截图后重建。
  保留用户配置、.user、用户资源与旧构建目录；无 Git 提交基线，不使用 git reset/clean。
- 按 C++/Qt 与 Qt 命名技能保持 MVVM 分层；按 UTF-8 技能保存编码与模块中文注释；
  故障排查技能用于最小隔离与证据验证，交接技能用于组织结论和后续阅读入口。
- 验证产物在 opengl-viewport-verify；Qt Creator 其他构建目录需自行 Build 才会更新。
  当前只有全局光照和观察相机；不实现 Light/Camera 实体、资源打包、自动保存或第八周发布工作。

## 2026-09-09 - Task: 第八周阶段 1 - 发布回归与异常输入

### What was done

- 增加 100 Cube 创建、整组删除/恢复、完整撤销重做与重开稳定 ID 验收。
- 增加空场景、中文路径和损坏场景失败保留文档/选择/历史验证。
- 明确本轮交付本地候选包，不擅自裁剪未实现的 P0 或发布正式版本。

### Testing

- 周前 Release CTest 4/4 通过，保存 123 个正式文件快照。
- Debug 完整构建及 CTest 4/4 通过；release-regression 新增两用例/624 断言通过。
- 定向 clang-format 检查通过；快照前验证源文件 UTF-8 无 BOM/LF。

### Notes

- tests/SceneDocumentTests.cpp：新增百对象完整历史和异常文档验收。
- docs/week8.md：记录范围、发布门槛和第一阶段验收。
- progress.md：仅追加本阶段记录。
- out/validation/Prepare-Week8.ps1：被忽略的本周快照与编码核验脚本。
- 回滚点 out/validation/week8-before-20260909；恢复 SceneDocumentTests.cpp，移出新增 week8 文档，
  保留 progress 历史并追加回滚记录；不修改用户配置或使用 git clean/reset。

## 2026-09-09 - Task: 第八周阶段 2 - 性能基线和快速稳定性

### What was done

- 增加默认关闭的 Release 验收工具，测量首次帧、导入、文档 IO、三个规模离屏帧和资源/历史循环。
- 保存可复验原始 JSON 和带方法限制的数据说明；不将离屏等效 FPS 当成窗口刷新率。

### Testing

- Release 全量构建时发现 Windows SDK psapi.h 依赖 windows.h 的包含顺序被格式化打乱；
  对这两个头文件局部固定顺序后工具构建成功，未改全局格式配置。
- 工具冒烟通过；正式 1920×1080、10 帧预热、120 帧采样三档平均耗时 1.023/4.358/6.670 ms。
- 60 秒执行 19654 次导入/删除/撤销重做/聚焦/渲染循环，状态与 GL 检查通过。
- 工作集 105.129→105.191 MiB；33 ms 级以下最慢帧详见 JSON，未宣称无内存泄漏或完整耐久通过。
- C++ 定向格式检查通过；工具正确拒绝 Debug 测量由代码入口限制，未把它计为运行测试。

### Notes

- CMakeLists.txt：新增默认关闭的 MINI3D_BUILD_TOOLS 开关。
- tools/CMakeLists.txt：新增独立性能工具目标，不影响常规四组 CTest。
- tools/PerformanceProbe.cpp：新增测量与可选时长的重复操作检查，无生产功能改动。
- docs/performance-week8.json：保存实测结果。
- docs/week8.md：记录方法、机器、数据和 30 分钟/多显示器/新机器验证缺口。
- progress.md：仅追加阶段记录。
- 回滚点 out/validation/week8-before-20260909；恢复根 CMakeLists.txt，移出新增 tools 及性能 JSON，
  保留日志历史并追加回滚说明，然后重新 Configure/Build。当前无正式版本/Tag 发布。

## 2026-09-09 - Task: 第八周阶段 3 - Release 候选包与环境隔离验证

### What was done

- 新增不覆盖的本地 ZIP 打包和环境隔离验证脚本，携带 Qt、MSVC、glTF 运行依赖。
- 附带固定场景、相对模型路径、许可原文来源和 SHA256 文件清单；固定已验证 vcpkg baseline。
- 为显式截图验收入口增加场景参数，普通启动行为不变。

### Testing

- 固定 baseline 后 Configure、Release 全量构建和 CTest 4/4 通过，依赖版本未变化。
- 首包 windeployqt 返回 0 但提示 VCINSTALLDIR 缺失，检查证实缺少 CRT；
  改为显式 MSVC 再分发目录后第二包构建成功，未重复依赖环境猜测。
- Test-Package 对第二包校验清单、复制到新路径、清理开发 PATH/Qt 环境；
  Demo/随包场景运行成功，缺失场景退出 4；11 个关键 DLL 的实际加载路径均在包内。
- 实际包帧缓冲已视觉核对：纹理盒、球体、棋盘地面可见。验证只启动/结束本轮进程。
- main.cpp 定向格式检查通过；不将本机隔离验证宣称为全新机器或许可合规验收。

### Notes

- vcpkg.json：固定本机验证的 builtin-baseline，不改变依赖列表。
- src/editor/main.cpp：仅截图模式支持 MINI3D_VALIDATION_SCENE，加载失败退出 4。
- assets/scenes/showcase.m3dscene：新增相对引用样例场景，不修改模型原文件。
- tools/Package-Release.ps1：收集 Release 依赖、许可、样例、清单并压缩。
- tools/Test-Package.ps1：清单和新路径/净 PATH 验证，检查实际 DLL 来源及场景画面改变。
- docs/third-party-notices.md：说明依赖、原始许可来源和公开分发前待核查义务。
- docs/package-readme.md：新增用户包运行、操作和使用限制。
- docs/week8.md：记录部署命令、真实失败修复与验证边界。
- progress.md：仅追加本阶段记录。
- out/packages/Mini3D-week8-candidate-check1、check2 及对应 ZIP：被忽略的部署检查产物。
- out/validation/week8-package-check2：被忽略的独立副本、模块清单和帧缓冲证据。
- 回滚点 out/validation/week8-before-20260909；恢复 main.cpp/vcpkg.json，移出新增打包/验包脚本、
  showcase 及两份包文档，保留日志并记录回滚；不处理用户本机配置、Qt 安装或已有进程。

## 2026-09-09 - Task: 第八周阶段 4 - 演示材料与本地候选整合交付

### What was done

- 新增真实窗口自动回放工具，生成截图、16 秒 GIF 摘要和 128 秒操作视频。
- 汇总当前能力、复现方式、第三方边界与正式发布缺口，生成携带最新文档/演示的最终候选 ZIP。
- 本轮完成本地候选交付，不冒称独立实体、完整耐久、新机器或公开发布已完成。

### Testing

- Debug/Release 完整构建与 CTest 各 4/4 通过；Debug UI 连续三次通过。
- 固定 seed 42：CPU 28/9929、Assets 11/187、GPU 2/58、Editor 19/967；DPI 200% 为 Editor 19/970。
- DemoCapture 首次编译缺少 EditorCamera 直接 include，定位后补齐；真实回放 640 帧全部生成，
  手柄位移、Ctrl+Z/Y、保存关闭重开和缺失文档保留状态均通过内置断言。
- PerformanceProbe Debug 返回后出现 C4702 不可达代码警告，改为配置分支编译；再次 Debug 构建无此警告。
  Debug 工具实测退出 2，Release 对已有报告拒绝覆盖并退出 1（预期的负向验证，不是通过测量）。
- MP4 经 ffprobe 确认 H.264、1440×900、640 帧、128 秒、913389 字节；完整解码通过，
  抽样核对手柄与重开帧。GIF 为 720×450、128 帧、16.01 秒、802906 字节，完整解码通过；
  palettegen 的重复色提示不影响编码/解码，不作媒体内容错误处理。
- 最终 ZIP 为 28990837 字节，SHA256：6A94AEC88EC322E14F61FDB2CD67637FFB3DD53831342815F0E123157B73F902。
- 从最终 ZIP 解压再执行 Test-Package：清单、带空格独立目录、净 PATH、Demo/场景画面不同、
  缺失场景拒绝、11 个关键依赖从包内加载均通过。不是只检查展开前目录或仅凭部署退出码。
- 定向检查通过：4 个 C++ 文件格式、20 个 UTF-8 无 BOM/LF 文本、PowerShell 语法、JSON、
  文档链接、diff 空白、progress 历史仅追加；GPU/UI 日志没有 GL_INVALID/GL_OUT_OF_MEMORY/资源错误。
- CPU 测试 dumpbin 仍无 Qt/OpenGL 依赖。未执行原方案 30 分钟人工交互、跨显示器或新机器验收。

### Notes

- tools/DemoCapture.cpp：新增真实 UI 意图回放和带断言的 640 帧截图生成。
- tools/CMakeLists.txt：登记可选演示工具及 Qt Test 依赖，不随用户包分发。
- tools/PerformanceProbe.cpp：修正 Debug 配置分支，避免不可达代码警告。
- tools/Package-Release.ps1：最终包附带演示说明和媒体。
- docs/demo.md：记录自动化回放属性、分镜、素材与编码复现。
- docs/media/mini3d-week8.mp4：新增 128 秒自动化操作演示。
- docs/images/mini3d-week8.gif：新增 16 秒加速摘要。
- docs/images/mini3d-week8.png：新增真实手柄操作截图。
- README.md：新增候选包入口、动图/视频与未达正式发布门槛说明。
- docs/week8.md：汇总四阶段结果、测试计数、包入口和正式发布缺口。
- docs/roadmap.md：标明本地候选交付完成、正式 V1 仍待门槛满足。
- docs/development-setup.md：更新测试计数、固定 baseline、工具开关与验收环境变量。
- docs/architecture.md：记录独立验收工具和生产入口边界，场景协议未改变。
- progress.md：仅追加本阶段记录。
- out/validation/Test-Week8.ps1、Validate-Week8.ps1：被忽略的本轮回归与定向检查脚本。
- out/validation/week8-*.log、week8-demo-frames、week8-video-check.png：被忽略的测试与视频源证据。
- out/packages/Mini3D-week8-candidate-windows-x64.zip 及同名目录：最终本地候选产物。
- out/validation/week8-zip-extracted、week8-package-final：最终 ZIP 解压和二次环境隔离证据。
- 回滚点 out/validation/week8-before-20260909（123 个文件）；按本周清单恢复已有文件，
  移出新增工具、场景、文档与媒体，保留 progress 并追加回滚记录后重新 Configure/Build。
  不使用 git clean/reset；未修改用户 .user/Qt 配置、网络或线程路径，未创建提交/Tag 或上传发布。
- Qt/C++、命名和 UTF-8 技能约束工具边界与编码；故障排查技能用于读取实际错误及隔离验证，
  交接技能用于标明交付物、验证范围和后续需确认事项，不将待定范围当作已完成。

## 2026-09-10 - Task: Camera/Directional Light 核心组件与层级契约
### What was done
- 按用户同意补齐独立组件，定义相机透视参数、方向光旋转/颜色/强度和单灯生效规则。
- 支持组件随子树复制、删除恢复；拒绝非法参数及几何/设备组件冲突。
### Testing
- 实施前 Debug CTest 4/4 通过；Debug mini3d_tests 构建成功。
- `[camera-light]` 用例 1 个、35 断言通过；已检查相对本轮快照的 Core 差异。
### Notes
- src/core/SceneNode.h：新增相机与方向灯组件及值校验。
- src/core/Scene.h：声明组件写入、世界旋转、相机投影和有效光照接口。
- src/core/Scene.cpp：实现组件生命周期、层级语义与光照选择。
- tests/SceneTests.cpp：增加组件和层级回归。
- docs/camera-light.md：记录本轮已确认范围和行为契约。
- progress.md：追加本阶段记录。
- out/validation/Prepare-CameraLight.ps1：被忽略的快照/编码检查脚本。
- 回滚点：out/validation/camera-light-before-20260910（135 个正式文件）；按上述清单从
  快照 Copy-Item 恢复既有文件，移出新增文档，保留 progress 并追加回滚记录。无 Git 提交可 reset。

## 2026-09-10 - Task: Camera/Directional Light 场景格式兼容
### What was done
- 新版保存写 version 2 组件字段，继续读取 version 1，旧场景不自动增添设备节点。
- 严格拒绝无效相机参数、未知灯类型与互斥组件，失败保留输出。
### Testing
- Debug mini3d_tests 构建成功，全量 30 用例、10000 断言通过（seed 42）。
- 已检查序列化差异；新增旧版读取、新版往返与 8 类组件错误验证。磁盘/UI 验证待集成阶段。
### Notes
- src/core/SceneSerializer.h：更新格式版本说明。
- src/core/SceneSerializer.cpp：新增 version 2 读写并保留 version 1 分支。
- tests/SceneSerializerTests.cpp：新增组件兼容性和错误隔离测试。
- docs/camera-light.md：记录格式字段、升级与旧程序不兼容注意事项。
- progress.md：追加本阶段记录。
- 回滚点沿用 out/validation/camera-light-before-20260910；Copy-Item 恢复上述既有源码/测试，
  保留追加日志；新版用户场景需保留副本，不可指望旧程序读取 version 2。

## 2026-09-10 - Task: Camera/Directional Light 编辑预览与整合验收
### What was done
- 接通 Create 菜单、组件 Inspector、可撤销编辑和实体相机预览；灯光实体驱动实际渲染。
- 预览不改编辑相机、不标脏；退出/隐藏/删除/文档切换恢复编辑视图，失败打开保留状态。
- 同步当前能力、格式兼容和使用说明，明确旧候选包/视频未重制、正式发布门槛仍未满足。
### Testing
- Debug/Release 全量构建与 CTest 各 4/4 通过；Debug UI 连续三次通过。
- 设备专项 3 用例、113 断言通过；200% 缩放全量 Editor 22 用例、1083 断言通过（seed 42）。
- CPU 全量 30 用例、10000 断言通过；dumpbin 证实 mini3d_tests 不依赖 Qt/OpenGL DLL。
- 新测覆盖组件创建/编辑/复制/删除/历史全回放、保存重开、旧版示例升级、非法文件保留文档及预览。
- 真实窗口验证 FOV/裁剪、灯光颜色/强度/旋转/显隐会改变帧缓冲，撤销恢复；预览禁用导航/拾取且
  不改变编辑相机，Esc/按钮退出后恢复原视图，重开组件场景预览画面一致。
- 已视觉查看 out/validation/camera-light-ui.png；定向格式、26 个变更文本 UTF-8 无 BOM/LF、
  Git 差异空白检查及历史 progress 前缀检查通过，文档链接与辅助脚本语法已纳入最终校验。
- 首次旧版升级测试将 E 盘资产场景保存到 C 盘临时目录而失败；检查临时路径和既有同盘约束后，
  改为把旧场景与模型复制进同一临时目录，专项及完整回归通过；未放宽生产路径规则。
### Notes
- src/editor/SceneViewModel.h：声明创建/组件编辑和瞬时预览状态。
- src/editor/SceneViewModel.cpp：实现组件命令、初始姿态、预览切换及退出条件。
- src/editor/AppearanceInspector.h：声明设备属性及预览控件。
- src/editor/AppearanceInspector.cpp：按选择绑定光照/相机、校验回显并提示预览状态。
- src/editor/MainWindow.cpp：连接两类创建菜单与预览信号。
- src/renderer_gl/Renderer.h：新增可选预览实体与宽高比状态。
- src/renderer_gl/Renderer.cpp：消费场景相机矩阵和有效灯光，预览关闭辅助覆盖层。
- src/renderer_gl/ViewportWidget.h：声明预览镜像和退出意图。
- src/renderer_gl/ViewportWidget.cpp：接入预览绘制、暂停鼠标交互及 Esc 退出。
- tests/SceneDocumentTests.cpp：新增设备历史/文件和原 version 1 示例升级测试。
- tests/DocumentEditorTests.cpp：新增真实组件控件与帧缓冲联动测试。
- README.md：新增入口并标明旧 ZIP/视频不含新增实体。
- docs/camera-light.md：补齐操作、交付边界和实测证据。
- docs/architecture.md：记录组件/预览状态分层和格式 1/2 通路。
- docs/roadmap.md：更新 P0 补齐状态，保留正式发布缺口。
- docs/week8.md：区分原候选包和后续源码补齐状态。
- docs/scope.md：链接已批准的实体补齐约定，说明单灯和设备显示边界。
- docs/development-setup.md：补充重新构建、预览模式和旧程序兼容提醒。
- progress.md：仅追加本阶段记录。
- out/validation/Test-CameraLight.ps1、Validate-CameraLight.ps1：被忽略的双配置与定向检查脚本。
- out/validation/camera-light-*.log、camera-light-diff-*.txt、camera-light-ui.png：被忽略的验证证据。
- 回滚点：out/validation/camera-light-before-20260910（135 个正式文件）；从快照逐项 Copy-Item
  恢复本轮清单中的既有文件，移出新增 docs/camera-light.md，保留 progress 并追加回滚记录，
  重新 Build/CTest；不要清理用户配置或 reset/clean。已保存的 version 2 场景须另留副本。
- 旧候选 ZIP/视频未覆盖；未运行完整人工耐久、跨显示器或新机器验收，未提交/打 Tag/公开发布。
- C++/Qt、命名与 UTF-8 技能约束了 MVVM 分层、接口命名和局部编码保护；交接技能用于说明
  已测范围、旧包兼容风险及后续人工验收入口，没有新增依赖或网络/并发路径。

## 2026-09-10 - Task: 插入全面汉化计划
### What was done
- 在 V1 人工验收前插入简体中文汉化阶段，明确文案清点、主界面、对话框/运行提示和回归验收四步。
- 规定术语、标准对话框、中文布局和输入的验收范围，保留用户名称、文件协议及底层诊断。
- 仅更新计划，未修改程序、生成中文构建或覆盖候选包。
### Testing
- 已检查路线图相对本轮副本的差异及空白，新增内容明确标记待实施。
- 本轮两个文档进行 UTF-8 无 BOM/LF、进度仅追加及原路线图内容保留核验。
- 未运行构建或程序测试：本轮仅修改 Markdown 计划；汉化后的运行验证列为后续验收条件。
### Notes
- docs/roadmap.md：插入全面汉化范围、执行顺序、兼容边界和验收标准。
- progress.md：仅追加本轮计划变更记录。
- out/validation/localization-plan-before-20260910/roadmap.md、progress.md：被忽略的修改前副本。
- 回滚点：out/validation/localization-plan-before-20260910；可执行
  `Copy-Item -LiteralPath 'out/validation/localization-plan-before-20260910/roadmap.md' -Destination 'docs/roadmap.md'`，
  恢复前核对是否存在后续路线图编辑；保留 progress 历史并追加回滚记录，不使用 git reset/clean。
- 文档阅读技能用于保持既有里程碑事实；安全 PowerShell 技能用于副本和编码保护；交接技能用于
  区分计划与实现，不把本轮文档校验视为中文版程序已通过验收。

## 2026-09-10 - Task: 全面汉化文案清点与实施计划
### What was done
- 确定简体中文术语、界面/动态/错误文案范围和保留项，明确四步实施及标准对话框方案。
- 保存本轮前 136 个正式文件副本，路线图标记实施中；本阶段未改程序行为。
### Testing
- 实施前 Debug CTest 4/4 通过；找到 Qt 6.8.3 qtbase_zh_CN.qm，136673 字节。
- 目标文本 UTF-8 无 BOM/LF 核验通过，已核对实际菜单、属性、导入/文档及渲染诊断入口。
### Notes
- docs/localization.md：新增文案清单、中文术语、实施顺序和验收条件。
- docs/roadmap.md：更新实施状态并链接专项说明。
- progress.md：仅追加本阶段记录。
- out/validation/Prepare-Localization.ps1：被忽略的快照/编码检查脚本。
- 回滚点 out/validation/localization-before-20260910：逐项 Copy-Item 恢复本轮既有文件，
  移出新增文档，保留 progress 并追加回滚记录；不清理用户配置、历史包或资产。

## 2026-09-10 - Task: 全面汉化主界面与对象名称
### What was done
- 完成菜单、停靠面板、属性标签、相机/光照说明、未保存提醒和新建名称汉化。
- 分离显示标签与稳定控件标识，复制名称使用“副本”，保留用户内容与 Core 接口行为。
### Testing
- Debug 编辑器测试构建成功；Debug CTest 4/4 通过（8.73 秒）。
- 定向差异、clang-format、14 个变更文件 UTF-8 无 BOM/LF、文档链接与历史日志仅追加检查通过。
### Notes
- src/editor/MainWindow.cpp：汉化菜单、面板、文档入口并分离创建动作标识。
- src/editor/TransformInspector.cpp：汉化变换属性并保留三轴控件标识。
- src/editor/AppearanceInspector.cpp：汉化材质、相机和灯光说明。
- src/editor/SceneTreeModel.cpp：汉化对象编号提示。
- src/editor/SceneViewModel.cpp：中文默认名称与历史动作。
- src/editor/SubtreeCommand.cpp：中文复制后缀与增删历史。
- src/editor/TransformEntityCommand.cpp：汉化变换历史。
- tests/SceneEditorTests.cpp：更新默认对象显示预期。
- tests/EditorCommandTests.cpp：更新新建名称预期。
- tests/SceneDocumentTests.cpp：更新文档默认名称预期。
- tests/GizmoEditorTests.cpp：更新复制名称预期。
- docs/localization.md：记录阶段结果。
- progress.md：仅追加本阶段记录。
- out/validation/Validate-Localization.ps1：被忽略的定向差异、编码及格式验证工具。
- 回滚点 out/validation/localization-before-20260910：从同名路径逐项 Copy-Item 恢复上述既有源码和测试；
  保留 progress 并追加回滚记录，不覆盖用户 Qt Creator 配置，不使用 reset/clean。

## 2026-09-10 - Task: 全面汉化运行提示与标准对话框
### What was done
- 完成导入、场景读写、参数校验和图形日志中文说明，保留原始底层诊断。
- 内嵌官方 Qt 简体中文资源，文件入口显式使用 Qt 对话框，不依赖运行机器系统语言。
### Testing
- Debug 全量构建和 CTest 4/4 通过，8.87 秒；既有错误、取消与文档往返用例通过。
- CMake 自动查询当前 Qt 资源并成功嵌入；29 个已变更文本的编码、定向格式、链接和日志保留检查通过。
- 证据 out/validation/localization-stage3-tests.log；专项中文断言和隔离部署在下一阶段执行。
### Notes
- src/editor/ChineseUi.h：新增中文资源初始化接口说明。
- src/editor/ChineseUi.cpp：新增随应用生命周期安装的内嵌翻译器。
- src/editor/CMakeLists.txt：查询当前 Qt 译文并嵌入资源，缺少文件明确失败。
- src/editor/MainWindow.cpp：控件创建前安装中文译文，文件入口固定 Qt 非原生对话框。
- src/editor/SceneViewModel.cpp：中文参数校验与操作结果。
- src/editor/main.cpp：中文显式验证日志。
- src/assets/AssetManager.cpp：中文资源缺失信息。
- src/assets/SceneDocument.cpp：中文读写、解析及路径约束上下文。
- src/assets/GltfImporter.cpp：中文校验/警告、导入阶段及空名称回退，保留 glTF 属性标识。
- src/core/SceneSerializer.cpp：中文自有校验异常，不改 JSON 协议或数值。
- src/renderer_gl/GpuMesh.cpp：中文网格上传与释放诊断。
- src/renderer_gl/GpuTexture.cpp：中文纹理诊断。
- src/renderer_gl/GridRenderer.cpp：中文网格线释放诊断。
- src/renderer_gl/SelectionRenderer.cpp：中文选中轮廓释放诊断。
- src/renderer_gl/ShaderProgram.cpp：中文着色器编译/链接上下文。
- src/renderer_gl/Renderer.cpp：中文资源初始化及上传消息。
- src/renderer_gl/ViewportWidget.cpp：中文 OpenGL 初始化和驱动诊断前缀。
- tests/SceneEditorTests.cpp：更新校验错误和导入成功文本预期。
- docs/localization.md：记录资源依赖、对话框选择及阶段验证。
- progress.md：仅追加本阶段记录。
- 回滚点 out/validation/localization-before-20260910：逐项 Copy-Item 恢复既有文件，移出新增 ChineseUi 源码，
  重新 Build/CTest；保留 progress 并追加回滚说明，不恢复用户配置，不清理历史产物。

## 2026-09-10 - Task: 全面汉化回归与交付衔接
### What was done
- 完成中文标准控件、名称输入、文件路径、取消/失败保护和布局自动化验收，路线图标记开发及本机验收完成。
- 更新当前中文操作说明和 Qt 译文构建依赖，保留历史周验收、旧视频与旧候选包事实。
- 生成独立 Release 验证包及中文含空格路径副本，验证净 PATH 下中文界面和旧场景加载。
### Testing
- Debug/Release 全量构建成功，CTest 各 4/4 通过（9.81/5.95 秒）；Debug 编辑器连续三次通过。
- 最终汉化专项 4 用例、127 断言通过；100% 明确缩放下再次验证并截图。
- 200% 缩放全量 Editor 26 用例、1210 断言通过；正常/最小窗口、中文对话框和确认按钮已查看实际图像。
- 生产文件入口在关闭测试全局非原生开关后仍使用中文 Qt 对话框；中文名称提交、F/W 输入、
  复制/撤销/重做、中文含空格路径导入/保存/重开与原始 JSON 诊断保留通过。
- 既有版本 1 示例升级、版本 2 设备场景、损坏与缺失资源隔离、保存失败和取消退出回归通过。
- 独立部署两次通过：清单完整、默认/旧场景帧缓冲不同、缺失文件退出 4、11 项运行依赖均来自包内；
  全窗口截图中文正确，包中没有外部 translations 目录。最终脚本复验目录为 out/validation/汉化独立部署 最终复验。
- Release CPU 可执行依赖检查不含 Qt/OpenGL；39 个变更正式文本 UTF-8 无 BOM/LF、定向格式、
  Markdown 链接、PowerShell 脚本解析与历史 progress 前缀检查通过，无全仓转码。
- 证据：out/validation/localization-build-*.log、localization-ctest-*.log、localization-repeat.log、
  localization-focused-final.log、localization-editor-dpi2.log、localization-dpi1-final-*.png、localization-dpi2-*.png。
### Notes
- tests/LocalizationTests.cpp：新增四个汉化专项用例与显式截图入口。
- tests/CMakeLists.txt：将汉化测试接入现有编辑器聚合测试。
- src/editor/main.cpp：既有验证模式额外支持完整界面 PNG，失败退出 5；普通启动不启用。
- tools/Package-Release.ps1：注明内嵌翻译部署方式，携带当前设备和汉化操作文档。
- tools/Test-Package.ps1：部署验证增加完整界面截图，未新增普通运行行为。
- README.md：当前状态、中文入口、Qt 译文依赖及历史包边界。
- docs/localization.md：完整四阶段结果、证据及真实输入法/新机器人工复核边界。
- docs/roadmap.md：标记汉化开发及本机自动化验收完成，保留正式发布门槛。
- docs/package-readme.md：中文版运行、保存兼容和设备操作说明。
- docs/camera-light.md：更新中文菜单和属性入口，链接译文依赖说明。
- docs/development-setup.md：中文操作、Qt 译文构建要求、当前测试数量和截图验证变量。
- docs/architecture.md：记录应用级内嵌翻译与显示标识分离边界。
- docs/third-party-notices.md：将 Qt 中文翻译资源纳入待发布许可核查。
- progress.md：仅追加各阶段完成记录。
- out/validation/Test-Localization.ps1、Validate-Localization.ps1：被忽略的双配置/布局与定向检查工具。
- out/validation/localization-package-20260910 及同名 ZIP：独立验证产物，不是替换旧候选包的正式发布；
  ZIP SHA256 为 581AEE0C66F8076610F98B7405F1A255B9377B0F9BCCEB1DCC6CC30B4C834B9C。
- 回滚点 out/validation/localization-before-20260910：从快照同名路径逐项 Copy-Item 恢复本轮已列既有文件，
  移出新增 src/editor/ChineseUi.h、ChineseUi.cpp、tests/LocalizationTests.cpp、docs/localization.md，
  保留 progress 并追加回滚说明，再执行 Build/CTest。不要回滚无关配置，不运行 reset/clean。
- 所有源码保持 UTF-8 无 BOM/LF；C++/Qt 与命名技能用于约束资源生命周期、控件标识和最小改动，
  交接技能用于明确已测范围。未改变协议/网络/并发路径，未创建分支、提交、Tag 或公开发布。
- 实际拼音输入法候选窗口、英文 Windows 新机器、跨显示器和完整耐久验收仍待人工进行；
  本轮不自动推进其他功能。终端对 Qt 原生 stderr 的代码页解码问题不等于界面或源码乱码。

## 2026-09-10 - Task: 高优先级优化 1/6：旋转与缩放手柄
### What was done
- 接受用户对现有功能验收通过的确认，按授权启动六项高优先级交互增强，保存 140 文件快照。
- 增加旋转环、轴向缩放与中心等比缩放，W/E/R 互斥切换，复用预览/一次提交/取消/撤销流程。
- 保留 TRS 和缩放符号，对不可表达剪切拒绝预览并中文提示，不改文件协议。
### Testing
- 实施前 Debug CTest 4/4 通过；实现后 Debug 全量构建、CTest 4/4 通过（10.82 秒）。
- 新增纯数学与真实鼠标测试覆盖旋转、缩放、原点保持、剪切拒绝、撤销、取消、工具切换及输入隔离。
- 12 个变更文本编码/定向格式/链接和历史日志前缀检查通过；证据 out/validation/interaction-stage1.log。
### Notes
- src/renderer_gl/GizmoController.h、GizmoController.cpp：工具类型、旋转和缩放计算及剪切验证。
- src/renderer_gl/GizmoRenderer.h、GizmoRenderer.cpp：旋转环/缩放柄 GPU 几何与释放。
- src/renderer_gl/Renderer.h、Renderer.cpp：渲染参数传递工具类型。
- src/renderer_gl/ViewportWidget.h、ViewportWidget.cpp：工具互斥、鼠标预览和中文拒绝提示。
- src/editor/MainWindow.cpp：中文工具菜单与 W/E/R 快捷键。
- tests/GizmoTests.cpp：新增旋转/缩放数学用例。
- tests/GizmoEditorTests.cpp：新增真实鼠标与历史集成用例。
- docs/interaction-optimization.md：六项顺序、约定与第 1 项结果。
- progress.md：仅追加本项记录。
- out/validation/Prepare-Interaction.ps1、Validate-Interaction.ps1：被忽略的快照与定向验证脚本。
- 回滚点 out/validation/interaction-before-20260910：从快照逐项 Copy-Item 恢复所列既有文件，
  移出新增专项文档；保留 progress 并追加回滚记录，重新 Build/CTest，不使用 reset/clean。

## 2026-09-10 - Task: 高优先级优化 2/6：世界与局部坐标
### What was done
- 增加中文世界/局部坐标选择，绘制、拾取和冻结拖动共用旋转坐标基。
- 局部旋转和缩放直接修改局部分量，坐标切换取消未提交事务，保持保存协议不变。
### Testing
- Debug 全量构建与 CTest 4/4 通过（10.09 秒），见 out/validation/interaction-stage2.log。
- 新增局部轴拾取、带旋转和负非等比父节点的位移/缩放/旋转测试；真实鼠标局部拖动及坐标切换取消通过。
- 定向格式、13 个变更正式文本的 UTF-8/LF 与历史日志前缀检查通过。
### Notes
- src/renderer_gl/GizmoController.h、GizmoController.cpp：坐标基与局部变换计算。
- src/renderer_gl/GizmoRenderer.cpp：几何使用同一坐标基。
- src/renderer_gl/Renderer.h、Renderer.cpp：从场景旋转构造手柄方向。
- src/renderer_gl/ViewportWidget.h、ViewportWidget.cpp：保存会话坐标系并取消旧手势。
- src/editor/MainWindow.cpp：中文坐标系菜单。
- tests/GizmoTests.cpp：局部变换数学验证。
- tests/GizmoEditorTests.cpp：真实局部轴交互验证。
- docs/interaction-optimization.md：使用约定及第 2 项结果。
- progress.md：仅追加本项记录。
- 回滚点 out/validation/interaction-before-20260910；按本轮清单逐项 Copy-Item 恢复既有文件，
  保留 progress 并追加回滚说明，重新 Build/CTest；该快照同时撤回本轮前项，不覆盖无关配置。

## 2026-09-11 - Task: 第 3 项平面移动与步进吸附

### What was done

- 完成三平面移动、菜单吸附与 Ctrl 临时吸附，沿用单次手势撤销及取消规则。

### Testing

- Debug 全量构建成功；CTest 4/4 通过（11.37 秒），记录 out/validation/interaction-stage3.log。
- 新增平面拾取、平行射线拒绝、相对起点步进、旋转/缩放步进与真实窗口拖动撤销测试。

### Notes

- src/renderer/GizmoController.h、src/renderer/GizmoController.cpp：平面选择和吸附计算。
- src/renderer/GizmoRenderer.h、src/renderer/GizmoRenderer.cpp：平面手柄绘制与资源释放。
- src/editor/ViewportWidget.h、src/editor/ViewportWidget.cpp：常驻与临时吸附输入。
- src/editor/MainWindow.cpp：中文吸附菜单。
- tests/GizmoTests.cpp、tests/GizmoEditorTests.cpp：数学与窗口回归。
- docs/interaction-optimization.md：操作与结果；progress.md：本轮记录。
- 回滚点：out/validation/interaction-before-20260910。可执行 Copy-Item -LiteralPath 'out/validation/interaction-before-20260910/src/renderer/GizmoController.cpp' -Destination 'src/renderer/GizmoController.cpp'，按上述已有文件逐项恢复；该快照同时撤回前两项交互优化，恢复前须保留后续改动。

补充勘误：上条第 3 项记录中的 src/renderer/ 应为 src/renderer_gl/；ViewportWidget 实际也位于 src/renderer_gl/，并非 src/editor/。可执行回滚示例为 Copy-Item -LiteralPath 'out/validation/interaction-before-20260910/src/renderer_gl/GizmoController.cpp' -Destination 'src/renderer_gl/GizmoController.cpp'。此勘误仅补正记录中的路径，不更改历史文本。

## 2026-09-11 - Task: 第 4 项视图快捷切换

### What was done

- 完成前/右/顶/自由观察方向及透视/正交切换，保持导航、拾取、手柄尺寸一致。
- 顶视创建相机采用实际姿态；保留旧场景文件格式与自由观察保存语义。

### Testing

- Debug 全量构建成功，CTest 4/4 通过（11.32 秒），out/validation/interaction-stage4.log。
- 新增数学与真实窗口测试，覆盖顶视有限性、平行射线、导航、拾取拖动、相机创建、预览保护及输入隔离。
- 定向差异、clang-format、UTF-8/LF、文档链接与历史日志前缀验证通过。

### Notes

- src/renderer_gl/EditorCamera.h、src/renderer_gl/EditorCamera.cpp：会话方向/投影与射线数学。
- src/renderer_gl/Renderer.h、src/renderer_gl/Renderer.cpp：视图模式转发。
- src/renderer_gl/ViewportWidget.h、src/renderer_gl/ViewportWidget.cpp：切换意图、预览保护与实际姿态。
- src/editor/SceneViewModel.h、src/editor/SceneViewModel.cpp：从实际姿态创建相机。
- src/editor/MainWindow.cpp：中文视图菜单与快捷键。
- tests/EditorCameraTests.cpp、tests/GizmoEditorTests.cpp：方向、投影与窗口回归。
- docs/interaction-optimization.md：使用与兼容边界；progress.md：记录。
- 回滚点：out/validation/interaction-before-20260910。按文件执行 Copy-Item -LiteralPath 'out/validation/interaction-before-20260910/src/renderer_gl/EditorCamera.cpp' -Destination 'src/renderer_gl/EditorCamera.cpp'（其余同理）；该快照会同时撤回重叠文件上的前序优化，需先备份后续工作。

## 2026-09-11 - Task: 第 5 项文件拖拽导入

### What was done

- 完成本地 GLB/glTF 拖入视口，顺序多文件导入且不移动源文件，每项复用已有撤销与错误流程。
- 拒绝目录、远程 URL、混合类型、失效路径与相机预览中的拖入。

### Testing

- Debug 全量构建成功，CTest 4/4 通过（11.60 秒），out/validation/interaction-stage5.log。
- 真实 dragEnter/dragMove/drop 事件验证成功导入、中文大写路径、多文件、撤销重做及损坏输入保持场景。
- 定向 diff、格式、UTF-8/LF 与历史日志检查通过。

### Notes

- src/renderer_gl/ViewportWidget.h、src/renderer_gl/ViewportWidget.cpp：本地拖放输入验证与意图信号。
- src/editor/MainWindow.cpp：接入已有导入意图入口。
- tests/GizmoEditorTests.cpp：拖放回归。
- docs/interaction-optimization.md：导入约定和边界；progress.md：记录。
- 回滚点：out/validation/interaction-before-20260910。按文件执行 Copy-Item -LiteralPath 'out/validation/interaction-before-20260910/src/renderer_gl/ViewportWidget.cpp' -Destination 'src/renderer_gl/ViewportWidget.cpp'（其余同理）；此快照也撤回重叠文件前序优化，先备份后续工作。

## 2026-09-11 - Task: 第 6 项场景树增强

### What was done

- 完成同文档单对象拖动换父/回根、中文右键菜单及不隐藏层级的搜索定位。
- 保持局部 TRS 与已有历史，拒绝自身/后代换父、跨会话拖入和行间排序。

### Testing

- Debug 全量构建成功，CTest 4/4 通过（12.95 秒），out/validation/interaction-stage6.log。
- QAbstractItemModelTester 通过；真实树控件的模拟拖放事件、搜索展开/选择同步、右键操作与撤销检查通过。
- 定向差异、格式、UTF-8/LF、链接与日志前缀检查通过。

### Notes

- src/editor/SceneTreeModel.h、src/editor/SceneTreeModel.cpp：拖放凭据与换父验证、模型意图转发。
- src/editor/MainWindow.h、src/editor/MainWindow.cpp：场景搜索、右键菜单与树拖放装配。
- tests/SceneEditorTests.cpp：模型契约、模拟拖放和中文操作回归。
- docs/interaction-optimization.md：约定和结果；progress.md：记录。
- 回滚点：out/validation/interaction-before-20260910。按文件执行 Copy-Item -LiteralPath 'out/validation/interaction-before-20260910/src/editor/SceneTreeModel.cpp' -Destination 'src/editor/SceneTreeModel.cpp'（其余同理）；共享 MainWindow 会同时撤回前序优化，恢复前备份后续修改。

## 2026-09-11 - Task: 六项高优先级优化整合回归与交付

### What was done

- 完成六项顺序施工后的跨功能检查，修正顶视等比缩放方向及局部轴缩放下限，补充右键重命名/聚焦验证。
- 同步当前能力、操作步骤、会话视图兼容边界和本机验收证据；保持原资源、文件协议与发布产物不变。

### Testing

- 两个缩放缺陷先以专项测试复现（interaction-scale-before.log），修复后 2 用例 / 7 断言通过（interaction-scale-after.log）。
- 右键重命名最初因未等待 Qt 委托排队提交而断言失败，失败拆窗时出现 SIGSEGV（interaction-rename-before.log）；等待提交后树专项 2 用例 / 63 断言通过，随后全量与重复运行通过。未用重试掩盖断言失败。
- Debug/Release 全量构建成功、无新增编译警告；CTest 各 4/4，12.96/7.22 秒（interaction-final-Debug.log、interaction-final-Release.log）。
- UI 连续三次通过（35.59 秒）；200% 完整 UI 33 用例 / 1350 断言通过（interaction-ui-repeat.log、interaction-ui-dpi2.log）。
- 截图专项 4 用例 / 101 断言通过；额外 DPI2 树专项 2 用例 / 64 断言通过。已查看旋转、缩放、顶视平面手柄、中文树及高分屏截图。
- 25 个正式变更文件通过定向 diff/格式/UTF-8 无 BOM/LF/文档链接检查；历史日志保持前缀，assets 与根 CMakeLists.txt 对比快照未变。
- 实际跨进程资源管理器原生拖放仍建议用户按文档手工复验；自动化已覆盖真实 Qt 控件上的模拟拖放事件，不把该项表述为手工验收通过。

### Notes

- src/renderer_gl/GizmoController.h：增加真实视图横向并更新手柄接口说明。
- src/renderer_gl/GizmoController.cpp：修正中心缩放方向及局部单轴的最小缩放约束。
- src/renderer_gl/Renderer.cpp：传递相机实际横向给手柄。
- src/renderer_gl/GizmoRenderer.cpp：同步手柄模块说明。
- src/editor/MainWindow.cpp：场景结构变化时关闭旧右键菜单，防止保留过期对象操作。
- tests/GizmoTests.cpp：新增两个可复现的缩放回归用例。
- tests/GizmoEditorTests.cpp：加入顶视实际缩放断言与缩放截图入口。
- tests/SceneEditorTests.cpp：补充右键重命名、聚焦，等待 Qt 编辑委托提交。
- README.md：更新六项能力和当前操作入口。
- docs/scope.md：记录用户批准的 V1.1 扩展，保留历史 P0/P1 划分。
- docs/roadmap.md：追加验收后优化进度。
- docs/interaction-optimization.md：六项结果、最终证据、边界及人工复验步骤。
- progress.md：逐项施工与本轮最终记录，仅末尾追加。
- out/validation/Prepare-Interaction.ps1：本轮初始快照脚本，拒绝覆盖旧快照。
- out/validation/Validate-Interaction.ps1：检查定向差异、编码、格式、历史和受保护资产。
- out/validation/Run-InteractionValidation.ps1：串行构建与最终回归、重复运行和应用截图。
- out/validation/interaction-*：本轮快照、差异、构建/测试报告及截图；不改旧候选包与历史媒体。
- 全轮其他代码文件已在对应阶段列出；编码保持 UTF-8 无 BOM/LF，没有全量转码或全仓格式化。
- 全轮回滚点：out/validation/interaction-before-20260910。恢复指定文件可执行 Copy-Item -LiteralPath 'out/validation/interaction-before-20260910/src/editor/MainWindow.cpp' -Destination 'src/editor/MainWindow.cpp'，其他变更文件按相同相对路径逐项恢复；新增 docs/interaction-optimization.md 不在旧快照，完整撤回时可对该明确路径执行 Remove-Item -LiteralPath 'docs/interaction-optimization.md'。不要在已叠加后续修改时直接恢复，先保存后续工作；progress.md 保留历史并追加回滚说明。

## 2026-09-11 - Task: 当前项目首次 Git 提交

### What was done

- 按用户要求准备 main 分支首次本地基线提交，纳入当前源码、测试、方案、文档和示例资产；不执行远程推送。
- 修正 docs 全目录文本属性覆盖二进制媒体的风险，显式保护 PNG/GIF/MP4/GLB，并保留既有 DOCX 二进制规则。

### Testing

- 暂存范围为 152 个文件；构建、临时验证、本机 CMake 配置和 IDE 用户配置均未暂存。
- 15 个二进制文件的暂存对象哈希与原始字节哈希逐项一致；属性检查确认 text/diff 均为 unset。
- 完整 git diff --cached --check 仅报告第三方许可证 docs/licenses/LicenseRef-LegalMark-Cesium.txt 的 6 处既有行尾空格；原文保留，排除此文件后的全项目检查通过。
- 常见私钥及访问令牌特征扫描未命中；此检查不等同于完整安全审计。
- 本轮未改功能代码，未重跑构建与功能测试；沿用上轮 Debug/Release 4/4、UI 重复与 200% 缩放通过记录。

### Notes

- .gitattributes：新增现有图片、视频与模型格式的二进制属性，避免提交时换行转换。
- docs/development-setup.md：追加提交范围、二进制保护与 commit/push 边界。
- progress.md：仅追加本轮提交准备和验证记录。
- 其余暂存文件为现有项目基线，本轮未改写其内容；当前无远程仓库，不修改 Git 身份与全局配置。
- 属性修正的回滚方式：在 .gitattributes 中仅移除本轮新增的 *.png、*.gif、*.mp4、*.glb 四行并恢复开发文档新增小节；不会修改媒体文件，但再次暂存媒体前应保留正确的二进制保护。首次提交完成后可用 git show HEAD:<相对路径> 查看该基线，恢复文件前先备份后续修改。
## 2026-09-11 - Task: 二期 M0 基线、详细计划与关键契约

### What was done

- 完整提取阅读 R2 DOCX 正文/表格和两个 HTML 原型，核对真实代码基线，建立 M0–M7/P1 逐项计划、范围、依赖、允许模块与 S01–S12 验收；没有将原型当实际运行结果。
- 记录 Y-up、新旧键位、模态安全取消、唯一历史、EditableMesh/revision 和格式 3 设计；协议升级和 P1 单独批准已询问，确认前不实施对应高风险部分。
- 修正本机已失效 PowerShell 路径并恢复二期构建基线，不改项目业务、依赖 manifest 或用户原始输入。

### Testing

- Debug/Release 构建 exit 0，CTest 分别 4/4（13.88s / 7.11s）；JUnit 位于 out/validation/v2-baseline-debug.xml 与 v2-baseline-release.xml。
- Debug、QT_SCALE_FACTOR=2 的 [localization] 4 用例/127 断言通过，生成实际窗口截图；不代表真实 IME 或跨屏通过。
- 首次 build 的 MSB3073 来自已移除的 7.6.4 路径；仅刷新缓存后 Configure 被本机路径校验拒绝。读取配置、验证实际 7.6.6 安装后修正一行，再 Configure 成功并重建依赖。未重复未诊断的失败命令。
- git diff --check 通过；本机配置与备份 diff 只有版本路径一行；已核对输入 SHA256 与 UTF-8 无 BOM/LF。计划按 R2 24 章、P0/P1 与不做项人工核对，未运行二期功能验收。

### Notes

- CMakeLocalConfig.cmake（Git 忽略）：只将本机 PowerShell 7.6.4 目录改为已安装的 7.6.6。
- docs/v2-development-plan.md：新增详细二期实施计划、任务表、验收与授权状态。
- docs/v2-contracts.md：新增输入/坐标/取消 ADR 与待批准数据协议设计。
- docs/blender-compatibility.md：新增二期兼容性目标和当前未实现说明。
- docs/v2-baseline.md：新增本机构建修复、实际测试、输入哈希和旧发布缺口。
- progress.md：仅追加本轮记录。
- out/validation/v2-before-20260911/CMakeLocalConfig.cmake：本机配置修改前副本；v2-baseline-*.xml/png 为可再生成验证输出。
- 回滚本机配置：Copy-Item -LiteralPath 'out/validation/v2-before-20260911/CMakeLocalConfig.cmake' -Destination 'CMakeLocalConfig.cmake'；这将恢复失效的旧路径，仅用于撤回本轮修改，不保证旧路径可运行。公共源码仍为 6dd209f；新增四份 docs 可在备份后逐个 Remove-Item -LiteralPath 撤回，禁止目录递归删除；progress.md 保留并追加回滚说明。

## 2026-09-11 - Task: 二期 UI-01 真实工作台区域与侧栏

### What was done

- 复用真实 GL 视口、场景树和属性，将场景/属性放在右侧上下区，增加 Header、工具说明、自制图标工具条和独立只读 N 侧栏；控制台保留菜单入口、默认隐藏。
- 复用现有 QAction/SceneViewModel，不改核心协议、场景数据或历史栈。显隐不标脏；窄窗自动收起侧栏；修正中央宿主包装后的视口刷新路径。
- 本项未实现工作区持久化、T/N 快捷键、Edit 或建模算子，不展示假完成按钮。

### Testing

- Debug/Release build exit 0；CTest 各 4/4，15.63s / 7.48s，JUnit 在 out/validation/v2-ui01-debug.xml、v2-ui01-release.xml。
- 新增 [workbench] 带截图两用例/35 断言通过；150%/200% 的 [workbench],[localization] 各 6 用例/156 断言通过。
- 实际检查区域、不穿透、DPR 帧缓冲、GL Context 不销毁、工具条复用、选择/历史不变、最小视口；真实截图见 out/validation/v2-ui01-dpi1.5.png、v2-ui01-dpi2.png。
- 首轮暴露窄窗视口 156 DIP，已自动收起 N；回归暴露主题应用时序导致 18 DIP 横滚，提前主题后原断言通过；旧固定像素吸附夹具改为相机换算 0.6 世界单位，保留原业务断言。
- 改动 C++ 定向 clang-format、git diff --check 通过；保持 UTF-8 无 BOM/LF。单机 DPI 不是跨显示器验收。

### Notes

- src/editor/MainWindow.cpp：右侧区域装配、主题初始化、动作连接和真实子视口刷新。
- src/editor/MainWindow.h：新增宿主成员声明。
- src/editor/workbench/WorkbenchShell.h / .cpp：新增 Header/工具条/侧栏与窄窗规则，图标由 Qt 绘制。
- src/editor/CMakeLists.txt：编译新宿主。
- tests/WorkbenchTests.cpp：新增真实窗口验收。
- tests/GizmoEditorTests.cpp：让吸附位移夹具独立于窗口高度，未改生产算法。
- tests/CMakeLists.txt：注册工作台测试源码。
- docs/v2-development-plan.md：标记 UI-01 实测状态。
- docs/v2-workbench.md：当前入口、限制、失败修正与验证说明。
- progress.md：仅追加本项记录。
- 回滚点为 6dd209f。先备份后续改动，可对本项七个已跟踪源码/构建文件逐个使用 git restore --source=6dd209f -- <明确文件路径>；新增 WorkbenchShell.h/.cpp、WorkbenchTests.cpp、v2-workbench.md 在备份后逐个 Remove-Item -LiteralPath 撤回，并恢复计划本项状态。禁止整仓恢复；保留进度历史并追加回滚说明。

## 2026-09-11 - Task: 二期 UI-02 工作区与属性分类

### What was done

- 增加布局/建模/检查三工作区，独立保存 Dock、T/N、属性分类/折叠、工具/坐标系/吸附；应用关闭后写用户偏好，启动布局完成后恢复，不写项目。
- 属性分为垂直标签页和可折叠组，轴字段纵向排布；统一 13 DIP 字体与 #3B3B3B 视口背景。未改历史栈、协议或建模业务。
- 数值拖动/非法输入等行为独立为 UI-NUMERIC 验收单元，仍属于原 R2 需求，不把布局完成当其完成。

### Testing

- Debug/Release build exit 0，CTest 各 4/4（18.82s / 7.90s）；证据 out/validation/v2-ui02-debug.xml、v2-ui02-release.xml。
- 工作区/汉化/相机专项 8 用例/230 断言通过；150%/200% 的工作区/汉化各 7 用例/182 断言通过。
- 临时 INI 验证同窗切换与新窗恢复，GL 身份不变、模式/选择/历史/dirty 不变；测试未写用户设置。截图 v2-ui02-dpi1.5.png / v2-ui02-dpi2.png 为本机构建。
- 旧文件菜单测试搜索任意首个滚动区，改页后命中隐藏页导致横滚断言失败；改为定位 ObjectPropertiesScroll 并增加 isVisible 断言后全部通过。
- 定向 clang-format、git diff --check 通过；改动源码保持 UTF-8 无 BOM/LF，无全仓格式化。真实跨屏、IME 和最终模型验收未执行。

### Notes

- src/editor/workbench/WorkspaceManager.h / .cpp：新增独立工作区状态和偏好存储适配。
- src/editor/workbench/WorkbenchShell.h / .cpp：增加工作区标签与访问接口。
- src/editor/MainWindow.h / .cpp：装配管理器、独立属性页/折叠组、偏好生命周期及字体。
- src/editor/CMakeLists.txt：编译工作区管理器。
- src/editor/TransformInspector.cpp、src/editor/AppearanceInspector.cpp：三轴字段改为纵排，删除因此不再需要的 HBox include。
- src/renderer_gl/Renderer.cpp：只变更清屏 RGB 三个常量，属于方案主题范围。
- tests/WorkbenchTests.cpp：增加工作区持久化与文档/GL 不变量测试。
- tests/LocalizationTests.cpp、tests/DocumentEditorTests.cpp：按实际分类页操作，保留原数据和布局断言。
- docs/v2-development-plan.md、docs/v2-workbench.md：完成状态、使用入口、偏好范围与测试说明。
- progress.md：仅追加本项记录。
- 回滚 UI-02：先备份后续工作，已在 UI-01 修改的文件可从 out/validation/v2-ui01-before-ui02/ 按同一相对路径逐个 Copy-Item -LiteralPath 恢复；本项首次修改的两个 Inspector、Renderer 和两个既有测试可逐个 git restore --source=6dd209f -- <明确文件路径>。新增 WorkspaceManager 两文件在备份后逐个撤回，保留日志并更新计划状态；不整仓恢复、不递归删除。

## 2026-09-11 - Task: 二期 UI-NUMERIC 数值提交、取消与单次拖动历史

### What was done

- 新增 CommitSpinBox 并接入变换/外观/相机/灯数值字段：Enter 或合法失焦提交、Esc 恢复、非法文本与域错误保留输入并展示原因。
- Alt＋左键拖动从原值重算候选文字，释放时只写模型一次；Esc、失焦、失活可取消，不新增历史。明确仅字段预览，不冒充实时几何预览。
- 保留程序 setValue 与现有 ViewModel/QUndoStack 路径，没有修改场景协议或模型业务校验。

### Testing

- Debug/Release build exit 0，CTest 各 4/4（21.42s / 8.28s）；证据 out/validation/v2-numeric-debug.xml、v2-numeric-release.xml。
- 数值专项首轮 3 用例/34 断言通过；新增真实 QLineEdit 按键、超范围、nan、1e999 回归后，200% 数值/工作台/汉化 10 用例/227 断言通过。
- 验证文字输入不提前修改、Esc 无历史、有效输入一次历史、无效输入保留、零缩放及相机近远裁剪拒绝不改变数据、拖动候选不累积、松开单次提交、Esc/失活取消释放鼠标、Undo 恢复。
- 定向 clang-format 与 git diff --check 通过；保持 UTF-8 无 BOM/LF，不全仓格式化。

### Notes

- src/editor/workbench/CommitSpinBox.h / .cpp：新增字段输入事务与失败反馈，不持有文档或历史栈。
- src/editor/TransformInspector.cpp、src/editor/AppearanceInspector.cpp：替换数值控件构造并转发当前提交的业务拒绝原因。
- src/editor/MainWindow.cpp：数值错误边框样式。
- src/editor/CMakeLists.txt、tests/CMakeLists.txt：注册新控件与测试源码。
- tests/NumericFieldTests.cpp：输入、域错误、单次拖动/取消/撤销验收。
- docs/v2-development-plan.md、docs/v2-workbench.md：实际完成状态、操作方式、预览边界和未完成范围。
- progress.md：仅追加本项记录。
- 回滚：先备份后续工作，旧文件可从 out/validation/v2-ui02-before-numeric/ 按相对路径逐个 Copy-Item -LiteralPath 恢复；新增 CommitSpinBox 两文件与 NumericFieldTests.cpp 在备份后逐个 Remove-Item -LiteralPath 撤回，更新计划状态。不得回滚整个仓库或删除历史日志。
- 长计划仍未完成：INPUT-01 起尚待实施；场景格式 3 与 P1 的单独确认尚未收到。不自动 commit/push/tag，不覆盖 V1 候选包或原始资料。

## 2026-09-11 - Task: 二期 INPUT-01 区域输入路由与互斥键位

### What was done

- 新增区域输入路由；旧键保持焦点语义，新键按 Qt 鼠标事件命中区域，主键/小键盘独立且不回落旧键。文本/IME/弹窗优先，离开/失活清空区域，不抢焦点、不跨窗口。
- 保留现有菜单动作与业务通路，移除树/视口重复快捷键关联，测试单次分发。新键位暂为内部接口，等待 GRS 完成再开放默认迁移；未实现操作不注册。

### Testing

- Debug/Release build exit 0；CTest 各 4/4（23.43s / 9.11s），证据 out/validation/v2-input01-debug.xml、v2-input01-release.xml。
- [keymap] 4 用例/49 断言通过；200% 输入/数值/工作台/汉化 14 用例/276 断言通过。
- 首轮指针区域为空；QTest 移动与显式 QCursor 设置均无效。取证确认 GetCursorPos 返回 false/error 5，而 widgetAt(真实布局坐标) 命中正确。改用 Enter/MouseMove 坐标跟踪，Leave/失活清空，合成事件验证命中及拒绝，不通过跳过区域判断掩盖失败。
- 定向 clang-format、git diff --check 通过；UTF-8 无 BOM/LF。真实物理鼠标、真实中文 IME 和跨屏验证仍待最终验收。

### Notes

- src/editor/operations/KeymapRouter.h / .cpp：新增互斥键表、文本保护、事件驱动区域跟踪及单次动作适配。
- src/editor/MainWindow.cpp：在已有动作建立后装配路由。
- src/editor/CMakeLists.txt、tests/CMakeLists.txt：注册输入路由和测试。
- tests/KeymapRouterTests.cpp：新旧键、主键/小键盘、文本/IME/弹窗、区域/失活和外窗拒绝。
- docs/v2-input-operations.md：已实现边界、默认迁移限制与诊断证据。
- docs/v2-development-plan.md：更新 INPUT-01 完成状态，不宣称整个键位与建模完成。
- progress.md：仅追加本项记录。
- out/validation/v2-input-desktop-probe.ps1：只读 Win32 光标权限诊断，可再生成，不属于发布物。
- 回滚：先备份后续修改；MainWindow.cpp 和两个 CMake 文件按相对路径从 out/validation/v2-before-input01/ 逐个 Copy-Item -LiteralPath 恢复；新增 KeymapRouter 两文件、KeymapRouterTests.cpp、v2-input-operations.md 备份后逐个 Remove-Item -LiteralPath 撤回。同步计划状态，保留日志并追加说明，不整仓恢复。

## 2026-09-11 - Task: 二期 OPS-01 操作注册与 F3 搜索

### What was done

- 静态登记 25 个已实现操作的稳定 ID、双语名称、分类/快捷键、前置校验和历史能力；F3/菜单替代入口支持搜索排序、方向键、Enter/双击、Esc、禁用原因和原焦点恢复。
- 打开前冻结区域/键位/文档代际/选择，执行前复核；新建/打开即使复用 Scene 地址也拒绝旧上下文。搜索和已注册快捷键复用菜单/工具条的同一个 QAction 与 ViewModel 历史路径。
- 不注册未实现的挤出/内插/F9。相机预览、无可见几何、无选择/历史有中文说明；搜索框 IME 预编辑期间不拦方向键。
- 连续窗口回归发现系统 widgetAt 命中仍可能为空；已改为在 Qt 事件接收窗口的布局内 childAt 命中，保持外窗拒绝、Leave/失活清空。

### Testing

- Debug/Release build exit 0；最终 CTest 各 4/4（25.63s / 9.22s），证据 out/validation/v2-ops01-debug.xml、v2-ops01-release.xml。
- 搜索/输入专项 9 用例/171 断言；200% 搜索/输入/数值/工作台/汉化 19 用例/398 断言。
- 验证中文/英文/当前快捷键指向相同 ID，25 个 ID 唯一且对应真实动作；菜单和 F3 创建结果及单次历史等价；工具条/菜单/搜索/键盘共用动作；取消零增量，Undo/Redo 恢复；旧文档、他窗、失效选择拒绝。
- 新增树焦点测试使当前行成为真实选择，原“无选择”夹具前提不成立；显式清空并断言后通过，未放松业务断言。
- 连续窗口错误取证：系统 widgetAt 返回 nullptr，同屏幕坐标 Qt childAt 返回 ViewportWidget；改为 Qt 接收窗口内命中后全套通过。定向 clang-format、git diff --check 通过，UTF-8 无 BOM/LF。

### Notes

- src/editor/operations/OperatorRegistry.h / .cpp：新增静态描述表、搜索/校验、文档代际和 QAction 适配。
- src/editor/workbench/OperatorSearchPopup.h / .cpp：新增非阻塞 F3 弹窗与冻结上下文，保护 IME 导航。
- src/editor/operations/KeymapRouter.h / .cpp：接入已注册快捷键，区域命中改为 Qt 窗口内部 childAt。
- src/editor/MainWindow.cpp：装配注册表、搜索与菜单入口，转发失败说明。
- src/editor/CMakeLists.txt、tests/CMakeLists.txt：注册新模块和搜索测试。
- tests/OperatorSearchTests.cpp：新增注册、搜索、冻结上下文、业务等价、焦点/IME 验收。
- tests/KeymapRouterTests.cpp：pointAt 改用同一 Qt 内部命中断言，不依赖系统窗口查询。
- docs/v2-input-operations.md、docs/v2-development-plan.md：使用入口、能力边界、已验证状态。
- progress.md：仅追加本项记录。
- 回滚：先备份后续修改，旧文件从 out/validation/v2-before-ops01/ 按相对路径逐个 Copy-Item -LiteralPath 恢复；其中 KeymapRouterTests.cpp 是依据唯一已知单行差异重建的前态。新增 Registry 两文件、SearchPopup 两文件、OperatorSearchTests.cpp 备份后逐个 Remove-Item -LiteralPath 撤回；保留日志并追加回滚说明，不整仓恢复。

## 2026-09-11 - Task: 二期 OPS-02 Q 快捷收藏与偏好保存

### What was done

- 增加有序收藏模型，F3/Q 共用操作结果弹窗；右键加入/移出，星标提示，Q/菜单打开、Enter/双击执行与 Esc 取消。
- 两键位共用无冲突的 Q，文字输入优先；收藏沿用 Registry 冻结上下文与执行前校验，不复制业务代码或历史。
- 仅保存有序稳定 ID 到 workbench/v2/quickFavorites；确认关闭保存、下次启动恢复；初始空列表，未知/重复 ID 忽略，不标脏、不写工程。

### Testing

- Debug/Release build exit 0；CTest 各 4/4（34.37s / 9.61s），证据 out/validation/v2-ops02-debug.xml、v2-ops02-release.xml。
- 带截图收藏/搜索/输入专项 12 用例/235 断言；200% 收藏/搜索/输入/数值/工作台/汉化综合 22 用例/462 断言通过。
- 临时 INI 验证另一窗口恢复顺序、去重、未知 ID 清理；真实 Qt 右键事件验证加入/移出；F3/Q 创建结果和一条历史一致，Undo/Redo、无选择、文档重置拒绝、文本 Q 保留。
- 已查看本次真实弹窗的 100% 搜索/收藏及 200% 收藏截图，文字/图标/提示可读。证据 out/validation/v2-ops02-debug-search.png、v2-ops02-debug-favorites.png、v2-ops02-dpi2-search.png、v2-ops02-dpi2-favorites.png；不充当未实现的建模配方证据。
- 定向 clang-format、git diff --check 通过；逐个严格 UTF-8 解码通过，无 BOM/LF，无全仓格式化或转码。

### Notes

- src/editor/operations/QuickFavorites.h / .cpp：新增有序稳定 ID 偏好模型，不执行场景业务。
- src/editor/workbench/OperatorSearchPopup.h / .cpp：复用 F3 弹窗显示 Q 收藏，增加标题、右键管理和列表确认。
- src/editor/operations/KeymapRouter.cpp：两配置增加无冲突 Q 入口。
- src/editor/MainWindow.h / .cpp：装配收藏偏好模型、菜单及正常启动/关闭的偏好读写。
- src/editor/CMakeLists.txt、tests/CMakeLists.txt：注册收藏模块与测试。
- tests/QuickFavoritesTests.cpp：偏好往返、右键、F3/Q 等价、历史/文本/文档保护与可选实际截图。
- docs/v2-input-operations.md、docs/v2-development-plan.md：入口、保存范围、边界和已验证状态。
- progress.md：仅追加本项记录。
- 回滚：先备份后续修改；旧文件从 out/validation/v2-before-ops02/ 按相对路径逐个 Copy-Item -LiteralPath 恢复；新增 QuickFavorites 两文件、QuickFavoritesTests.cpp 备份后逐个 Remove-Item -LiteralPath 撤回。收藏偏好键可保留（旧版忽略），不清空用户全部设置；保留日志并追加回滚说明，不整仓恢复。

## 2026-09-11 - Task: 二期 INPUT-02 数值缓冲基础（模态变换仍在开发）

### What was done

- 实现独立 NumericInputBuffer：符号/小数/退格/清空/范围，区分空、不完整、非法、越界和有效状态；只有有效结果携带数值。
- 保留非法原文，拒绝算式、科学计数、单位和非有限文本，不 eval、不做前缀截断；可通过退格修复。纯标准 C++，没有 Scene/Qt/GL 副作用。
- 仅完成 INPUT-02 的数值基础；G/R/S 的快照、鼠标预览、模态生命周期与历史接入尚未实现，不开放新默认键位。

### Testing

- Debug/Release 数值 CPU 专项各 3 用例/100 断言；正负与前导小数、未完成前缀、非法表达式、修复、400 位溢出、范围闭边界全部通过。
- Debug/Release 全 build exit 0；CTest 各 4/4（28.52s / 10.16s），证据 out/validation/v2-input02-numeric-debug.xml、v2-input02-numeric-release.xml。
- dumpbin /DEPENDENTS 确认 Release mini3d_tests.exe 无 Qt/OpenGL DLL；定向 clang-format、git diff --check 通过，新增文本为 UTF-8 无 BOM/LF。

### Notes

- src/editor/operations/NumericInputBuffer.h / .cpp：新增带状态的十进制原文解析，不连接变换行为。
- tests/NumericInputBufferTests.cpp：新增不依赖窗口的语法/修复/范围验收。
- src/editor/CMakeLists.txt、tests/CMakeLists.txt：UI 库与独立 CPU 测试编译同一缓冲实现，不给 CPU target 链接 Qt。
- docs/v2-input-operations.md：记录数值边界和模态接入注意事项，包括弹窗关闭时序。
- docs/v2-development-plan.md：INPUT-02 保持进行中，明确数值基础已验证、不等于 G/R/S 完成。
- progress.md：仅追加本项记录。
- 回滚：先备份后续修改，两个 CMake 与文档从 out/validation/v2-before-input02-numeric/ 按相对路径逐个 Copy-Item -LiteralPath 恢复；新增 NumericInputBuffer 两文件与 NumericInputBufferTests.cpp 备份后逐个 Remove-Item -LiteralPath 撤回。保留日志并追加说明，不整仓恢复。
- 长计划仍 active；格式 3/P1 未收到单独批准；未改核心协议、未自动提交/推送/发布，未覆盖用户原始资料。

## 2026-09-11 - Task: 二期 INPUT-02 单对象即时变换与安全取消

### What was done

- 接通单对象即时移动/旋转/缩放、X/Y/Z 约束与二按切空间、排轴移动/缩放、精确数字、Shift 连续精细和 Ctrl 异或吸附；中文 HUD 反馈状态和错误。
- 从固定 before/父矩阵/实际相机计算预览，使用原 ViewModel 事务与唯一历史栈；确认一条、无变化零条，取消完整恢复且保留 redo。精确数学拒绝不可表示的剪切及非法缩放，不改变协议。
- 菜单、Registry/F3/Q 共用三个真实动作；先关闭搜索并恢复焦点再启动模态。默认暂 Legacy，Blender 内部 G/R/S 已验收，界面键位选择及其他输入收尾在 INPUT-03。
- 测试复现并修复两处接入问题：旧搜索输入框释放/重绘误取消模态；旧手柄预览尚未取消时提前取快照。另清理模态接管前残留相机拖动状态，避免后续点击拾取被阻止。

### Testing

- Debug/Release 全 build exit 0；CTest 各 4/4（33.55s / 12.93s），证据 out/validation/v2-input02-modal-debug.xml、v2-input02-modal-release.xml。
- 对象数学+数值 CPU 专项两配置各 6 用例/128 断言；带截图模态/键位/搜索/收藏专项 23 用例/542 断言；200% 综合 33 用例/780 断言通过。
- 覆盖 G X 2、R Y 45、S Shift+Z .5、鼠标平面/旋转/缩放、负非均匀父层级、轴二按、长按不重复切轴、退格连发、Shift 无跳变、Ctrl 临时反转、无效输入修复、剪切拒绝及局部替代。
- Esc/右键/视口外点击/失焦/失活/隐藏/Resize/UngrabMouse/IME/选择变化/删除/新建均恢复；F3/Q 关闭后的真实事件循环保持模态，确认/Undo/Redo 与零历史测试通过。
- 已查看 out/validation/v2-input02-modal-debug.png、v2-input02-modal-dpi2.png；HUD/属性可读。截图中的工具设置栏仍静态标 Legacy，随 INPUT-03 改为实际状态；未声明真实物理鼠标、跨屏、真实 IME 或 S01–S12 最终通过。
- 定向 clang-format dry-run、git diff --check 通过；Release CPU dumpbin 仅 Windows/MSVC 运行库，无 Qt/OpenGL。编码保持 UTF-8 无 BOM/LF，无全仓格式化/转码。

### Notes

- src/editor/operations/ObjectTransformMath.h / .cpp：新增无状态单对象 TRS 候选数学。
- src/editor/operations/ObjectTransformSession.h / .cpp：新增模态快照/输入/预览/安全结束适配器。
- src/renderer_gl/ViewportWidget.h / .cpp：提供实际 CPU 相机副本；重置交互时清理残留相机拖动状态。
- src/editor/MainWindow.cpp：装配即时变换菜单、会话和中文 HUD。
- src/editor/operations/OperatorRegistry.cpp：注册三个对象模态动作及可见性/相机预览拒绝原因。
- src/editor/workbench/OperatorSearchPopup.cpp：预校验后先关闭弹窗，恢复焦点，再执行并复核。
- tests/ObjectTransformMathTests.cpp：新增父层级/方向/精确 TRS/剪切拒绝 CPU 测试。
- tests/ObjectTransformSessionTests.cpp：新增真实窗口变换、取消、历史、输入修复和生命周期测试及可选截图。
- tests/KeymapRouterTests.cpp、tests/OperatorSearchTests.cpp：将新注册动作纳入断言，旋转路由后显式取消。
- src/editor/CMakeLists.txt、tests/CMakeLists.txt：注册模态和数学模块及独立测试，不改变依赖。
- docs/v2-input-operations.md、docs/v2-development-plan.md：补充真实入口、行为/限制/验证；INPUT-02 完成，列出 M2 原输入契约的 INPUT-03 收尾。
- progress.md：仅追加本项记录。
- 回滚：先备份后续修改，旧文件从 out/validation/v2-before-input02-modal/ 按相对路径逐个 Copy-Item -LiteralPath 恢复；新增 ObjectTransformMath/ObjectTransformSession 四文件和两个测试文件备份后逐个 Remove-Item -LiteralPath 撤回；保留日志并追加回滚说明，不整仓恢复。
- 长计划仍 active；格式 3/P1 未批准；未提交、推送或发布，未修改原始资料。

## 2026-09-11 - Task: 二期 INPUT-03 默认键位与偏好子项（视图输入仍待收尾）

### What was done

- 在对象模态通过后正式采用 Blender 风格默认，编辑菜单可切回 Legacy Mini3D；快捷键、菜单勾选和工作台工具/键位文字同步，两套冲突键不混用。
- Blender 的 W 回到选择工具；R 为即时旋转，Legacy 的 R 仍为缩放手柄。切换键位取消未确认变换，不更改已提交对象或历史。
- 用户键位仅保存 workbench/v2/keymap 的稳定值，启动恢复、确认关闭保存；首次/未知值使用 Blender，不写场景或清理其他偏好。
- 现有兼容测试显式使用 Legacy，不放宽断言；新增新默认/菜单/偏好测试。同步 README 和 docs，明确新用户默认变化与旧包未更新。

### Testing

- Debug/Release 全 build exit 0；CTest 各 4/4（34.27s / 12.46s），证据 out/validation/v2-input03-preferences-debug.xml、v2-input03-preferences-release.xml。
- 带截图偏好/模态/输入/搜索/收藏专项 25 用例/579 断言；200% 综合 35 用例/817 断言通过。
- 临时 INI 验证另一窗口恢复 Legacy、未知值回落但读取不写回、不改其他键、偏好零历史且不标脏。新默认 W 选择/R 旋转、旧 R 缩放、模态切配置恢复、互斥快捷键与实际标签断言通过。
- 已查看 out/validation/v2-input03-preferences-debug.png、v2-input03-preferences-dpi2.png；当前工具与 Blender 风格显示一致。仍不替代跨屏/真实中文输入法验收。
- 定向 clang-format dry-run、git diff --check、逐个严格 UTF-8 无 BOM/LF 检查通过；无全仓格式化或转码。本子项没有构建或行为测试失败。

### Notes

- src/editor/operations/KeymapRouter.h / .cpp：新默认、W 选择和独立 QSettings 偏好读写。
- src/editor/workbench/WorkbenchShell.h / .cpp：绑定真实键位状态，更新当前工具文字，不保留静态 Legacy 标签。
- src/editor/MainWindow.cpp：键位菜单、模态取消连接、正常启动/关闭偏好读写。
- tests/KeymapPreferencesTests.cpp：新增默认/往返/未知值/无污染/菜单与互斥输入测试、可选截图。
- tests/CMakeLists.txt：注册偏好测试。
- tests/KeymapRouterTests.cpp：旧行为明确选择 Legacy，新 W 断言调整为选择工具。
- tests/GizmoEditorTests.cpp、tests/SceneEditorTests.cpp：一期快捷键兼容用例明确选择 Legacy。
- tests/OperatorSearchTests.cpp、tests/QuickFavoritesTests.cpp：保留焦点语义的旧入口测试明确使用 Legacy，新区域语义由模态/偏好测试覆盖。
- README.md：增加真实二期状态及新默认操作说明，旧包/视频不冒充更新。
- docs/v2-input-operations.md、docs/v2-development-plan.md：记录键位子项完成和剩余视图输入，INPUT-03 仍进行中。
- docs/v2-contracts.md、docs/blender-compatibility.md、docs/v2-workbench.md：同步默认迁移、兼容边界和历史工作台说明。
- progress.md：仅追加本项记录。
- 回滚：先备份后续修改，旧文件从 out/validation/v2-before-input03-preferences/ 按相对路径逐个 Copy-Item -LiteralPath 恢复；新增 tests/KeymapPreferencesTests.cpp 备份后单独 Remove-Item -LiteralPath 撤回。偏好键可保留给未来版本读取，不清空用户设置；保留日志并追加说明，不整仓恢复。
- 下一步仍在 M2/INPUT-03：实现 Home、小键盘 0、区域最大化与对应回归；不能宣称 M2、组件建模、格式 3 或全部二期完成。协议/P1 授权状态未变化，长计划 active。

## 2026-09-11 - Task: 二期 INPUT-03 全部框景与实际相机预览

### What was done

- 接通 Home、菜单/Header/F3/Q 的全部框景：包含可见导入/原生几何、父变换与无几何对象原点，隐藏整棵子树不参与，不要求选择。
- 小键盘 0 复用真实实体相机预览，再次按下或 Esc 返回精确编辑视图（预设/正交保留）；按所选、最近、最小 ID 的可见相机顺序选择，不自动创建相机或新增工程字段。
- 最近相机仅在会话保留、切文档清空。没有对象/可见相机有明确拒绝原因，预览内导航受保护，但返回编辑视图不被 Registry 误拦截；Legacy 可用菜单入口。

### Testing

- Debug/Release 全 build exit 0；CTest 各 4/4（48.48s / 12.88s），证据 out/validation/v2-input03-navigation-debug.xml、v2-input03-navigation-release.xml。
- 两配置 CPU 拾取/范围专项各 5 用例/53 断言；导航/搜索/键位专项 12 用例/273 断言；200% 综合 38 用例/911 断言通过。
- 检查整个范围八角均在真实视锥内、隐藏远对象排除、导入实例和负非均匀父变换正确；框景不改选择或对象历史，文本 Home 不被抢。
- 多相机候选顺序/最近相机/删除隐藏回落/新建清空、主键0和小键盘0区分、无相机提示、预览内滚轮/Home保护、精确顶视正交矩阵返回、菜单勾选同步和 Legacy 替代入口通过。
- 初次编辑器测试编译发现 Aabb 没有 center 成员，改为现有最小/最大角中点后通过，没有为测试扩展 Core。定向 clang-format dry-run、git diff --check、严格 UTF-8 无 BOM/LF 检查通过。
- 单屏200%不等于跨屏/真实IME验收；本子项不包含区域最大化及重绑定，仍不能宣布 M2 全部完成。

### Notes

- src/renderer_gl/RayCaster.h / .cpp：新增全部可见对象范围查询，无几何对象按原点纳入。
- src/renderer_gl/Renderer.h / .cpp：以全部范围调用已有相机框景数学。
- src/renderer_gl/ViewportWidget.h / .cpp：全部框景适配与编辑相机通知，不更改 GL 生命周期。
- src/editor/SceneViewModel.h / .cpp：可见相机候选/最近会话状态与预览切换。
- src/editor/MainWindow.cpp：接通全部框景与预览/返回菜单、勾选同步。
- src/editor/workbench/WorkbenchShell.cpp：Header 视图菜单复用两个动作。
- src/editor/operations/OperatorRegistry.cpp：增加两个实际导航动作、禁用原因及允许预览返回。
- tests/RayCasterTests.cpp：增加全部范围/原点/层级隐藏与导入实例验证。
- tests/ViewportNavigationTests.cpp：新增矩阵级框景和相机往返/输入隔离测试。
- tests/OperatorSearchTests.cpp、tests/CMakeLists.txt：登记新增操作数量和导航测试。
- README.md、docs/v2-input-operations.md、docs/v2-development-plan.md：实际入口与范围、相机选择规则、验证和剩余最大化任务。
- progress.md：仅追加本项记录。
- 回滚：先备份后续修改，旧文件从 out/validation/v2-before-input03-navigation/ 按相对路径逐个 Copy-Item -LiteralPath 恢复；新增 tests/ViewportNavigationTests.cpp 备份后单独 Remove-Item -LiteralPath 撤回；保留日志追加说明，不整仓恢复。
- 格式仍为2，未改协议、未引入线程、未提交/推送/发布；长计划 active，下一项为区域最大化及可重绑定入口。

## 2026-09-11 - Task: 二期 INPUT-03 区域最大化与快捷键重绑定，完成 M2 输入上下文

### What was done

- 实现视口、场景树、属性区和控制台的临时最大化与原布局恢复；复用现有控件，保留 OpenGL Context、Dock 浮动状态/原坐标与焦点，不产生对象历史或文档修改。
- Blender 配置默认 Ctrl+Space，长按只切换一次；提供视图菜单替代、单组合重绑定/清空/恢复默认，冲突拒绝并解释。文本/输入法/弹窗优先，Legacy 使用菜单。
- Registry/F3/Q 使用冻结源区域执行最大化；切工作区或保存/恢复偏好前先还原，最大化不污染常规工作区。快捷键存独立用户偏好，不写工程。
- 同步操作说明与阶段状态；按 R2 的 M2 契约核对 Registry、F3/Q、文本保护、对象模态与导航已交付，未把 Edit/F9/组件建模宣称完成。

### Testing

- 最终 Debug/Release 全 build exit 0；串行 CTest 各 4/4（38.52s / 13.83s），证据 out/validation/v2-input03-maximize-debug.xml、v2-input03-maximize-release.xml。
- 200% 综合 43 用例/1017 断言通过，包含最大化/导航/模态/键位/搜索/收藏/数值/工作台/汉化。带截图相关首轮专项 19 用例/385 断言通过；最终以完整回归为准。
- 校验原 Dock 几何、浮动位置/特征、可见性、GL Context 身份/无销毁、文本优先、F3 区域冻结、快捷键冲突/禁用/INI 往返/取消对话框、Legacy 菜单和无历史/dirty。
- 首轮属性宽度断言缺 Qt 布局等待；补齐等待，不放宽尺寸标准。200% 发现 restoreState 将浮窗向屏幕内移动 6 DIP，隔离记录坐标后增加原浮窗坐标恢复，专项 1 用例/19 断言和全回归通过。
- 首次 DPI 综合启动时前一 CTest 未确认结束，且误用不存在的模态标签；不采用其验收结论。改为 v2-input03-maximize-verify.ps1 明确串行运行，并用真实 object-transform-ui 标签覆盖模态。
- 已查看100%/200%真实视口和属性最大化截图；不代替真实中文输入法/跨屏验收。定向 clang-format、git diff --check、逐文件严格 UTF-8 无 BOM/LF 检查通过，未全仓格式化或转码。

### Notes

- src/editor/workbench/AreaMaximizer.h / .cpp：新增临时布局快照/恢复与浮窗坐标保护，不重挂 GL 控件。
- src/editor/workbench/WorkspaceManager.h / .cpp：布局捕获/偏好恢复前通知还原临时最大化。
- src/editor/operations/KeymapRouter.h / .cpp：最大化组合重绑定、冲突校验、偏好往返、区域分发与长按保护。
- src/editor/operations/OperatorRegistry.h / .cpp：登记第31个实际操作并在执行期间提供冻结源区域。
- src/editor/MainWindow.cpp：装配最大化、菜单替代、中文重绑定对话框与状态提示。
- src/editor/CMakeLists.txt：注册最大化模块。
- tests/AreaMaximizerTests.cpp：新增布局、GL、文本、浮窗、工作区和重绑定5个用例及截图。
- tests/OperatorSearchTests.cpp：同步实际注册数量。
- tests/CMakeLists.txt：注册最大化测试。
- README.md：增加当前最大化与重绑定操作入口。
- docs/v2-input-operations.md：真实行为、偏好键和验证证据。
- docs/v2-workbench.md：追加工作台最大化能力与边界。
- docs/v2-development-plan.md：INPUT-03/M2完成，后续网格/协议状态仍分开记录。
- progress.md：仅追加本项记录。
- 回滚：先备份后续修改，旧文件从 out/validation/v2-before-input03-maximize/ 按相对路径逐个 Copy-Item -LiteralPath 恢复；新增 AreaMaximizer.h/.cpp 和 tests/AreaMaximizerTests.cpp 备份后逐个 Remove-Item -LiteralPath 撤回；偏好键可留存，不清空用户其他设置，日志保留并追加回滚说明。
- 长计划 active，下一项为 MESH-01 独立 CPU 网格内核；格式3与P1未获单独批准，未提交/推送/发布或修改原资料。

## 2026-09-11 - Task: 二期 MESH-01 可编辑网格快照与基础拓扑校验

### What was done

- 新增无 Qt/GL 的 EditableMesh 快照，保存显式64位顶点/面/面角身份、共享位置和独立 UV/硬法线/颜色/材质引用；无向源边按端点 ID 规范化，不用容器下标作身份。
- 直接构造原生8点/12边/6四边面 Cube，保持一期单位边长、外侧CCW、Y-up及逐面UV/硬法线方向，不从渲染拆点猜测或自动焊接。
- 新增只读校验：ID/引用/环、有限属性、零边/退化面积、最多两面共边与相反绕序、顶点单扇连通。支持开边界、孤立顶点和多个独立分量，明确不宣称全局自交检测。
- 保持Scene/文件协议/Renderer不变，本项仅独立候选基础；ID分配、单调revision、文档安装、渲染派生和Edit入口仍随对应任务接入。

### Testing

- Debug/Release CPU专项各7用例/251断言；全build exit 0，CTest各4/4（38.61s / 14.17s），证据out/validation/v2-mesh01-debug.xml、v2-mesh01-release.xml。
- 验证Cube真实8/12/6与24面角、法线/UV、64位ID、重排/环旋转后身份与属性、边界计数、坏ID/缺引用/短环/重复点、非有限属性、零法线、零边/零面积及尺度/大平移。
- 三面共边、共享边反向不一致、开放双扇和两个闭壳仅共用一顶点明确拒绝；两个完全独立Cube允许。有限候选校验前后完整快照相等。
- Release mini3d_tests.exe 的 dumpbin /DEPENDENTS 仅Windows/MSVC运行库，无Qt/OpenGL DLL。定向clang-format、git diff --check、严格UTF-8无BOM/LF检查通过，无编码转换。本子项无编译或行为测试失败。

### Notes

- src/core/modeling/EditableMesh.h / .cpp：新增纯CPU多边形快照、稳定ID查询、边键和原生Cube工厂。
- src/core/modeling/MeshValidation.h / .cpp：新增只读拓扑/属性/流形校验及中文错误。
- src/core/CMakeLists.txt：登记四个Core模块文件，不增加外部依赖。
- tests/EditableMeshTests.cpp：新增7个CPU用例，包含合法与拒绝夹具。
- tests/CMakeLists.txt：在纯CPU测试目标注册新网格测试。
- docs/v2-editable-mesh.md：新增实际数据结构、校验范围、限制与验证说明。
- docs/v2-development-plan.md：MESH-01完成，其余M3/协议状态保持分开。
- progress.md：仅追加本项记录。
- 回滚：先备份后续改动，旧文件从out/validation/v2-before-mesh01/按相对路径逐个Copy-Item -LiteralPath恢复；新增四个Core文件、tests/EditableMeshTests.cpp和docs/v2-editable-mesh.md备份后逐个Remove-Item -LiteralPath撤回，不递归删除目录；保留日志追加回滚说明。
- 下一项MESH-02可在不改协议的独立CPU范围继续；格式3与P1仍未获单独批准，长计划active，未提交/推送/发布或修改原资料。

## 2026-09-11 - Task: 二期 MESH-02 源半边邻接、三角化与稳定来源映射

### What was done

- 从合法源面角重建半边next/previous/twin及源边/顶点/面索引；边界没有虚构面，半边下标只在当前构建有效，三角化内部对角线不进入源边集。
- 实现局部归一化主轴投影检查和耳切，支持凸/凹/反向/连续共线边界环及简单投影非共面面；交叉、接触、回折重叠或退化明确拒绝。
- 生成兼容现有MeshData的面角拆点与三角索引，逐顶点保留64位源点/角身份，逐三角保留源面/材质；失败不返回部分结果，不改变源快照。
- 派生外观对照一期Cube后补齐原生源面角逐面颜色，位置/UV/法线/颜色逐项一致；显式硬法线输出归一化，无硬法线计算当前面法线。未接入Scene/GPU安装或改场景协议。

### Testing

- Debug/Release网格专项（MESH-01/02）各14用例/1498断言；全build exit 0，串行CTest各4/4（38.58s / 14.09s），证据out/validation/v2-mesh02-debug.xml、v2-mesh02-release.xml。
- 验证Cube24半边/12源边、环与twin互反、边界/孤立点、24渲染顶点/12三角/18三角边、来源ID/面角属性/材质匹配及原外观一致，不将内部对角线视为源边。
- 凹面及反向、连续共线边界、3–20角交替凹凸多边形按三角正面积/质心位于原环内/总面积一致验证；非共面四边形、小到1e-20和大到1e20的尺度、重排后64位来源映射通过。
- 投影交叉、非相邻接触、相邻回折和坏引用拒绝，即使前面已有可派生面也不发布部分结果；原快照保持完整相等。空结果边界明确测试。
- Release CPU测试依赖仅Windows/MSVC运行库，无Qt/OpenGL DLL；定向clang-format、git diff --check、严格UTF-8无BOM/LF检查通过。本子项无编译或行为测试失败。

### Notes

- src/core/modeling/MeshTopology.h / .cpp：新增可重建半边邻接与源元素索引。
- src/core/modeling/MeshDerivation.h / .cpp：新增简单投影检查、耳切、面角拆点及稳定来源映射。
- src/core/modeling/EditableMesh.cpp：补齐原生Cube面角颜色，保持一期外观；已额外备份到本项回滚点。
- src/core/CMakeLists.txt：登记新Core模块，不增加外部依赖。
- tests/MeshDerivationTests.cpp：新增7个CPU用例，独立验证几何、邻接、来源、属性和拒绝场景。
- tests/CMakeLists.txt：在纯CPU目标注册派生测试。
- docs/v2-editable-mesh.md：追加真实邻接/派生能力、投影/性能边界与验证证据。
- docs/v2-development-plan.md：MESH-02完成，下一EDIT-01明确停在格式3授权关口。
- progress.md：仅追加本项记录。
- 回滚：先备份后续改动，旧文件从out/validation/v2-before-mesh02/按相对路径逐个Copy-Item -LiteralPath恢复；新增MeshTopology.h/.cpp、MeshDerivation.h/.cpp、tests/MeshDerivationTests.cpp备份后逐个Remove-Item -LiteralPath撤回，不递归删除目录；保留日志追加回滚说明。
- 全局自交/多孔面/任意空间面支持和性能预算未宣称完成；Edit/组件历史/保存纵切片尚未接通。长计划active、二期未完成；格式3与全部P1仍需单独批准，未提交/推送/发布或修改原资料。

## 2026-09-12 - Task: 记录二期剩余任务完整授权并继续 EDIT-01

### What was done

- 根据用户“批准完整完成后续所有任务，无需同意”的明确指示，解除格式 3 及全部 P1 授权关口；保留历史记录，不把批准标为实现完成。
- 继续既定顺序实施 EDIT-01，不扩大到方案外功能，不自行提交、推送或发布。

### Testing

- 已核对原授权消息与计划/契约范围；目标文件严格 UTF-8 无 BOM、LF 检查通过；文档差异和授权状态交叉核对。
- 本轮仅授权文档更新，无运行行为改动；EDIT-01 代码验证另记。

### Notes

- docs/v2-development-plan.md：更新当前授权状态，EDIT-01 实施中，FILE-01/P1 已授权待办。
- docs/v2-contracts.md：标记协议设计获授权，明确与验收状态分离。
- progress.md：只在末尾追加授权记录。
- 回滚点：out/validation/v2-before-edit01/；先备份后续修改，再对两份文档逐个 Copy-Item -LiteralPath 恢复；日志不删，另追加回滚说明。

## 2026-09-12 - Task: 二期 EDIT-01 可编辑网格场景绑定、历史、渲染与最小格式 3

### What was done

- 接通原生 Cube 的独立可编辑真源，Scene 持有 MeshId 与不可变源/派生快照；绑定与静态来源互斥，复制分配独立身份，相机/灯拒绝绑定。
- 复用唯一 QUndoStack：候选先校验/派生，确认安装一次快照，Undo/Redo 不重跑算法，三类 revision 保守单调前进；无变化不入历史，坏候选保持当前网格/历史/保存点。
- 将派生网格接入真实 GPU 绘制和包围盒/对象拾取，按身份、revision 与内容失效；上传、替换与释放仍在有效 Context。此为纵切片必要的 Renderer/RayCaster 接线，不扩展到组件拾取。
- 正式升级为写3读1/2/3，保存源点/面/面角属性与网格绑定，保留原设备、相对资产路径、meshIndex、TRS/WXYZ。旧工程首次升级强制另存新路径，继续 QSaveFile 原子写与临时文档验证。
- 补结构 schema、迁移/实现边界说明；本项通过 ViewModel 和真实窗口自动化验证，Tab/组件模式/组件交互仍按后续任务实施，不宣称二期完成。

### Testing

- Debug/Release 全 build exit 0；串行 CTest 各4/4（43.60s / 15.98s），证据 out/validation/v2-edit01-debug.xml、v2-edit01-release.xml。
- 两配置 CPU 专项各8用例210断言；文件/编辑器/GPU专项各4用例124断言，含真实截图与工程输出。200% 专项4用例121断言；已查看100%/200%截图。
- 验证64位ID/面角属性完整往返、16种坏拓扑/绑定/字段原子拒绝、1/2迁移、独立副本、删除恢复、无变化/失败历史、保存点、Undo/Redo、保存重开继续更改、设备拒绝。
- 真实帧缓冲验证首次可编辑化保持原Cube外观、单顶点改动改变图像/范围，Undo/Redo恢复对应帧，重开后相等且Context身份未变；不是组件鼠标输入验收。
- 旧版本同路径保存拒绝且原字节不变，失败/取消保持保护状态，保存菜单建议新文件名。生成工程通过结构schema，非法数组反例被拒绝；语义验证仍由Core负责。
- Release CPU依赖仅Windows/MSVC运行库，无Qt/GL DLL。25个交付文件严格UTF-8无BOM/LF、17个C++文件格式检查及git diff --check通过；只格式化本轮差异/新增文件，无全仓转码。
- 本项无编译或行为测试失败；文档补丁首轮因表格上下文不完整未应用，核对原行后修正。驱动读回性能警告为既有截图诊断，不是GPU错误。

### Notes

- src/core/SceneNode.h：增加独立MeshId绑定。
- src/core/Scene.h / .cpp：拥有真源/派生内容与运行期revision，预备/安装快照，独立复制，加载绑定校验。
- src/core/SceneSerializer.h / .cpp：版本3源网格字段编解码、来源版本记录、读1/2兼容与坏候选拒绝。
- src/assets/SceneDocument.h / .cpp：原子文件路径接入网格与来源版本，不改变静态资源映射。
- src/editor/SceneViewModel.h / .cpp：可编辑化/替换意图与唯一快照历史，旧版首次另存保护。
- src/editor/MainWindow.cpp：旧文件保存强制弹出新路径对话框并建议-v3文件名。
- src/renderer_gl/Renderer.h / .cpp：加入revision感知可编辑GPU缓存与Context内清理。
- src/renderer_gl/RayCaster.h / .cpp：场景范围与对象拾取使用真实可编辑源顶点范围。
- tests/EditableSceneTests.cpp：新增5个CPU场景/格式3用例。
- tests/EditableDocumentTests.cpp：新增4个历史/文件/GPU/保存菜单用例。
- tests/SceneSerializerTests.cpp：现写版本断言改为3，保留设备和旧格式回归。
- tests/CMakeLists.txt：登记新CPU与编辑器测试。
- docs/scene-format-v3.schema.json：新增格式3结构schema，语义限制另由Core校验。
- docs/v2-scene-format.md：新增所有权、保存、迁移、限制和验证说明。
- docs/v2-editable-mesh.md：同步文档接入状态，保留既有阶段证据。
- docs/v2-development-plan.md：记录EDIT-01完成，必要渲染接线范围及下一EDIT-02。
- README.md：更新当前读写版本、旧文件保护与UI尚未接通的边界。
- progress.md：仅追加本项记录。
- 回滚：先备份后续修改，再从out/validation/v2-before-edit01/逐个Copy-Item -LiteralPath恢复本清单既有文件；新增两测试及两份格式文档备份后逐个Remove-Item -LiteralPath撤回。日志保留并追加说明，不整仓恢复；原始DOCX/MD/HTML与V1候选包未改动。
- 当前外部面材质非零引用明确拒绝，导入网格编辑、组件选择/变换、完整分配失败注入、修改器/editorState仍待对应任务。授权已齐，未提交/推送/发布；下一项EDIT-02。

## 2026-09-12 - Task: 修复 Qt Creator 构建目录的 PowerShell 旧路径 MSB3073

### What was done

- 确认本机PowerShell已更新到7.6.6，7.6.4的Store包路径不存在；本机配置虽已更新，windows-msvc-local的vcpkg两项缓存及五个目标的依赖复制命令仍引用7.6.4。
- 本机配置及可复制示例在project()前，将已校验的PowerShell路径同步到Z_VCPKG_PWSH_PATH与Z_VCPKG_POWERSHELL_PATH，避免只修改PATH却沿用旧缓存。
- 在用户实际报错目录重新Configure并成功完成Debug构建，不删除整个构建目录、不关闭applocal依赖部署、不改MSVC targets或Qt Creator Kit，不重装工具链。
- 补充环境排错说明。此次仅修复构建配置，二期功能任务保持暂停，未改业务源码、未提交或推送。

### Testing

- 原路径Test-Path=False，当前pwsh为7.6.6；修复后两项缓存均为有效7.6.6路径，五个报错vcxproj的各配置命令均不再引用7.6.4。
- E:/cmake-3.31.0-rc1-windows-x86_64/bin/cmake.exe -S E:/Mini3D -B E:/Mini3D/out/build/windows-msvc-local：exit 0。
- 同目录--build --config Debug --parallel 4：exit 0，编辑器及四个测试目标全部成功，未再出现MSB3073。Release未单独构建；其生成命令路径已同步复核。
- 首轮Debug CTest为3/4：编辑器76用例中一条ObjectTransformSessionTests.cpp:207历史数量断言失败（3与2），不是编译或applocal失败；保留pwsh-applocal-windows-msvc-local-debug.xml。
- 对该断言进行只读代码核对，再按已有验证环境QT_SCALE_FACTOR=1、QT_LOGGING_RULES=*.debug=false;*.info=false隔离运行：1用例27断言通过，证据pwsh-applocal-modal-isolated.xml。相同固定环境全套CTest复验4/4通过（41.92s），证据pwsh-applocal-windows-msvc-local-debug-recheck.xml；首轮UI不稳定原因未确定，不据复验宣称已修复该业务问题。
- pwsh-applocal-check.ps1通过：两项缓存、五个目标工程、四个编辑文件的严格UTF-8无BOM/LF及git diff --check。既有编码未转换。

### Notes

- CMakeLocalConfig.cmake（Git忽略）：增加11行同步当前显式PowerShell路径到vcpkg缓存，保留机器专属路径及其他设置。
- CMakeLocalConfig.cmake.example：同步11行配置，供其他本机配置复用；未加入机器专属绝对路径。
- docs/development-setup.md：增加MSB3073/applocal旧Store路径排错及重新生成命令。
- progress.md：仅追加本次修复与首轮失败/复验结果。
- out/build/windows-msvc-local/：由CMake重新生成缓存和工程、执行Debug增量构建；不手改生成的vcxproj。
- out/validation/pwsh-applocal-preflight.ps1、pwsh-applocal-check.ps1及上述3份XML：本机备份/复核助手和验证证据，不提交产物。
- 回滚点：out/validation/before-pwsh-applocal-20260912/。先备份后续改动，按相对路径逐个Copy-Item -LiteralPath恢复两份CMake配置和环境说明；日志保留并追加回滚说明。随后运行`& 'E:/cmake-3.31.0-rc1-windows-x86_64/bin/cmake.exe' -U Z_VCPKG_PWSH_PATH -U Z_VCPKG_POWERSHELL_PATH -S E:/Mini3D -B E:/Mini3D/out/build/windows-msvc-local`重新探测。旧CMakeCache仅作诊断备份，不恢复其中失效的7.6.4路径。

## 2026-09-12 - Task: 二期 EDIT-02 单对象编辑模式、源组件选择与真实点击反馈

### What was done

- 接入真实 Object/Edit 上下文：Tab、Header 和菜单共用入口；首次 Cube 转换沿用已有独立网格历史，模式/选择本身不压入 Undo，也不改变已保存状态。
- 用稳定 64 位 ID 和规范化端点边键保存单一选择域，独立管理活动项；实现替换/追加/移除/切换、全选/取消、确定性点边面投影和删除后清理，面角/三角拆点不冒充编辑身份。
- 接通真实视口点/边/面点击和 Shift 多选：点边 10 逻辑像素半径，面射线命中映射回源面，场景三角遮挡、父变换与负/非均匀缩放生效。新增深度检查选区覆盖层，导航不重传同一缓冲，不重挂 GL Widget。
- Header、N 侧栏和树同步当前模式、选区数量、活动 ID/局部位置；Edit 中禁用整对象变换/手柄/创建/复制/删除/换父，并在 ViewModel 入口再次拒绝。Tab 先取消旧对象预览，切对象/隐藏祖先/绑定失效/切文档清理上下文。
- Registry 增加 6 个真实操作到 37 项，F3/Q 冻结模式、网格/求值 revision 与选择代际；即使模式往返或 Undo 后内容恢复，也不使用旧上下文。文本、IME、Legacy 和工作区边界保留。
- 同步使用说明、兼容边界和计划。组件变换/建模、B 框选、X-Ray、元素隐藏仍是后续任务，没有把本次结果标成二期整体完成。

### Testing

- Debug/Release 全 build exit 0；最终串行 CTest 各 4/4（45.90s / 16.92s），证据 out/validation/v2-edit02-ctest-debug.xml、v2-edit02-ctest-release.xml。
- 两配置选择专项各 4 用例/68 断言，拾取专项各 2 用例/36 断言，编辑专项各 7 用例/139 断言；对应 v2-edit02-selection-*、picker-*、editor-*.xml。
- 150%/200% 模式/工作台/输入/Registry 综合各 19 用例/395 断言，证据 v2-edit02-dpi1.5.xml、v2-edit02-dpi2.xml。独立真实点击截图专项各 1 用例/27 断言。
- 验证活动项独立、重排与大整数、删除清理、投影规则、无变化/非法命中无副作用、保存点不变、隐藏祖先退出、模式往返/网格变更/Undo 使旧上下文失效；相机/灯/不支持几何拒绝 Edit。
- 真实窗口验证 Tab 取消 G 预览、文本/IME 不抢键、Legacy 不混用，点边面点击、Shift 追加/移除、空白取消、N 不穿透。帧缓冲在选择变化后不同，取消后恢复相同；源网格、对象选择、历史位置及 Context 身份不变。
- 已查看 100% 和 200% 的 v2-edit02-click-*.png。首轮截图早于侧栏布局请求处理，三行选择文字被裁掉；补充事件等待与“2/6 文字、三行高度”断言后重新跑全套并复核图像。不是删除断言或修改几何来通过测试。
- 本次没有编译/行为测试失败；一次读取了不存在的 GeometryMath.h 后由 rg 确认实际 Ray.h，一次空补丁的格式化上下文未匹配后重新读取定位，不重复盲试。驱动像素回读性能提示为截图诊断，不是 GL 错误。
- Release CPU 选择测试依赖仅 Windows/MSVC，无 Qt/GL；39 个交付文件严格 UTF-8 无 BOM/LF、C++ 格式、git diff --check 和旧进度日志前缀校验通过。仅格式化本轮差异/新增文件，未全仓转码。

### Notes

- src/editor/ComponentSelection.h、src/editor/ComponentSelection.cpp：新增不依赖 Qt 的稳定选择值模型、活动项和域投影。
- src/editor/SceneViewModel.h、src/editor/SceneViewModel.cpp：拥有编辑上下文和选择通知，执行模式/失效清理及对象意图互斥。
- src/editor/operations/ComponentPicker.h、src/editor/operations/ComponentPicker.cpp：新增逻辑像素源组件单击与精确场景遮挡策略。
- src/editor/operations/ComponentInteraction.h、src/editor/operations/ComponentInteraction.cpp：把真实点击交给 ViewModel，并生成只读源选区显示数据。
- src/editor/operations/KeymapRouter.cpp：启用 A/Alt+A 组件动作并防止 Tab 长按反复切模式。
- src/editor/operations/OperatorRegistry.h、src/editor/operations/OperatorRegistry.cpp：登记 6 项模式/选择操作并冻结网格和选择代际。
- src/editor/operations/ObjectTransformSession.cpp：Tab 取消旧对象会话，拒绝在 Edit 中启动整对象模态。
- src/editor/workbench/WorkbenchShell.h、src/editor/workbench/WorkbenchShell.cpp：Header 模式/域入口及活动项侧栏，禁用不适用对象工具。
- src/editor/workbench/OperatorSearchPopup.cpp：展示冻结的真实模式，不再固定标为对象模式。
- src/editor/TransformInspector.cpp：编辑模式锁定整对象 TRS/换父，并给出说明。
- src/editor/MainWindow.cpp：装配组件交互与视口模式通知。
- src/editor/CMakeLists.txt：登记组件选择、拾取与交互模块。
- src/renderer_gl/ComponentOverlayRenderer.h、src/renderer_gl/ComponentOverlayRenderer.cpp：新增单 Context、revision 感知的真实源面/线/点覆盖层。
- src/renderer_gl/Renderer.h、src/renderer_gl/Renderer.cpp：接入覆盖层生命周期与绘制，保留对象模式旧反馈。
- src/renderer_gl/ViewportWidget.h、src/renderer_gl/ViewportWidget.cpp：接收显示数据、转交组件点击/Shift、屏蔽对象手柄，DPI 仅影响点显示尺寸。
- src/renderer_gl/CMakeLists.txt：登记覆盖层模块和着色器资源。
- assets/shaders/component.vert、assets/shaders/component.frag：新增带微小深度偏移和透明度的组件绘制程序。
- tests/ComponentSelectionTests.cpp：新增 4 个纯 CPU 选择/投影用例。
- tests/ComponentPickerTests.cpp：新增 2 个无窗口几何拾取/遮挡用例。
- tests/EditModeTests.cpp：新增 7 个模式、上下文、真实点击与绘制用例。
- tests/CMakeLists.txt：登记选择、拾取与模式测试，CPU 选择不链接 Qt。
- tests/OperatorSearchTests.cpp：注册数量改为 37，保留原有 ID 唯一性和可执行入口断言。
- docs/v2-edit-mode.md：新增使用规则、限制、数据流和验证说明。
- docs/v2-development-plan.md：记录 EDIT-02 完成与下一 PICK-01，不缩减总范围。
- docs/v2-input-operations.md：更新 37 项 Registry 和模式上下文说明。
- docs/v2-workbench.md：补当前编辑模式/侧栏状态，保留历史阶段说明。
- docs/blender-compatibility.md：同步已获授权及最小格式 3/当前 Edit 能力，明确未支持项。
- README.md：同步当前编辑模式能力和组件建模尚未完成的边界。
- progress.md：仅追加本项实现、验证和回滚记录。
- out/validation/v2-edit02-preflight.ps1、v2-edit02-format.ps1、v2-edit02-verify.ps1、v2-edit02-check.ps1：定向备份、格式化、串行验证和编码/日志检查；XML/PNG 为本机证据，不提交产物。
- 回滚点：out/validation/v2-before-edit02/。先备份后续用户修改；对本清单已有文件按相对路径逐个执行 `Copy-Item -LiteralPath <回滚点/相对路径> -Destination <仓库/相对路径>`；对新增组件模块、两着色器、三测试和新编辑模式文档备份后逐个 `Remove-Item -LiteralPath <精确文件路径>`。保留 progress.md 并追加回滚说明，然后重新运行 CMake。不得整仓 reset/checkout，不能覆盖前序 EDIT-01 或本机 PowerShell 修复。
- 尚未完成：组件 G/R/S、拓扑算子、F9、游标/吸附/隔离、Mirror、完整格式/OBJ、P1 和最终 QA；当前边段越过近/远裁剪面会跳过，PICK-01 将沿同一策略补齐截断、B 框选与 X-Ray。合成 IME/单屏 DPI 不冒充跨屏与真实输入法验收。未提交、推送、发布或修改原始 DOCX/MD/HTML。

## 2026-09-12 - Task: PICK-01 组件框选、穿透选择与独立覆盖层

### What was done

- 完成 B 框选：点投影、边段触碰、面中心规则；无修饰替换、Shift 追加、Ctrl 移除，矩形/HUD 预览不写选区，释放仅批量发布一次。
- 点选与框选共用六平面齐次裁剪及真实三角深度策略。连续区间差处理部分遮挡和亚像素开口，修复跨近/远/侧裁剪面的边被整条跳过；保留稳定来源 ID 与导入单双面/负变换绕序。
- X-Ray 取消几何遮挡但不选择隐藏对象；覆盖层总开关独立，关闭不清选区、不退出 Edit、不改变穿透或材质/保存状态；GL 深度和绘制状态正确恢复。
- 菜单/Header/B/Alt+Z/Shift+Alt+Z/F3/Q 共用真实 QAction，Registry 为 40 项；Legacy 使用菜单替代，无别名混用。
- 框选冻结编辑对象、网格 revision、选区/代际、域、相机、尺寸及穿透状态；Esc/右键/失焦/失活/隐藏/尺寸/文档/网格/模式/键位等变化取消，侧栏不穿透。补观察方向切换的视图通知。
- 本次必要范围包括选择/意图与工作台适配，不只改 Renderer；未修改核心协议、并发、网络、原始方案或用户场景。用户运行的 windows-msvc-local 程序标题仍为“未命名*”，不强制关闭，全部功能验证使用独立目录。

### Testing

- `out/validation/v2-pick01-verify.ps1` exit 0：Debug/Release 全 build 成功；全套 CTest 各 4/4，耗时 49.41s / 17.26s。
- 两配置 `[component-selection]` 各 5 用例/86 断言；`[component-picker]` 各 9/145；`[component-overlay]` 各 1/48；`[box-session],[edit-mode],[operator-search],[keymap]` 各 21/475。窗口框选独立 5 用例/133 断言，启用截图增加一断言。
- 150%/200% 加入 `[workbench]` 的综合回归各 24 用例/535 断言。实际查看 `v2-pick01-box-debug.png` 与 `v2-pick01-box-dpi2.png`，矩形、提示、模式/开关可读；不冒充真实输入法或跨屏验收。
- 查询覆盖源点/边/面、正交/透视、三角来源、负/非均匀父变换、可见/隐藏对象、真实 glTF 遮挡、单双面与负缩放、部分遮挡/深度交叉、亚像素开口、近远侧裁剪和视口外区域。
- 窗口覆盖一次通知、无预览写入、替换/追加/移除、F3/Q/菜单、Legacy/文本/IME、取消/失效、侧栏不穿透、真实 X-Ray 单击/框选、Overlay 关仍能选、帧缓冲变化与恢复、原 Context/历史/保存点不变。GPU 读回确认遮挡/穿透差异，深度不写入且状态恢复。
- 首轮窗口 5 用例中 2 失败，保留 `v2-pick01-editor-first.xml`：方向切换未通知导致会话继续，补 `viewModeChanged`；文本夹具错误要求 Qt 的 Alt+Z 不输入字符，改为验证 B 正常输入和建模状态不变。定位后重建，复验 `v2-pick01-editor-recheck.xml` 通过，最终完整回归也通过，未循环盲试。
- `v2-pick01-check.ps1`：本轮 31 个交付文件严格 UTF-8 无 BOM/LF、C++ 格式、git diff --check 及 progress.md 原前缀检查通过；无转码，全量格式化未执行。Release 纯 CPU 测试依赖表仅 Windows/MSVC，无 Qt/GL。
- XML 证据：`out/validation/v2-pick01-{selection,picker,gpu,editor,ctest}-{debug,release}.xml`、`v2-pick01-dpi1.5.xml`、`v2-pick01-dpi2.xml`；PNG 同前缀。原 MSB3073 修复仍有效；用户目录另一个 LNK1168 是未关闭程序占用，本项未通过结束用户进程绕过。

### Notes

- src/editor/operations/ComponentPicker.h：增加 X-Ray 参数与批量框选查询契约。
- src/editor/operations/ComponentPicker.cpp：共用齐次投影、真实三角深度与连续可见边区间，替代仅单击/端点全在视锥内的旧逻辑。
- src/editor/ComponentSelection.h：声明批量集合更新。
- src/editor/ComponentSelection.cpp：实现替换/追加/移除/切换、无效 ID 过滤和活动项修复。
- src/editor/SceneViewModel.h：暴露批量组件选择意图。
- src/editor/SceneViewModel.cpp：批量更新仅发布一次变化，不创建历史。
- src/editor/operations/ComponentInteraction.h：声明框选会话和事件过滤入口。
- src/editor/operations/ComponentInteraction.cpp：接入 X-Ray 单击、冻结框选、矩形/HUD、提交和取消生命周期。
- src/editor/MainWindow.cpp：将同一框选 QAction 和键位切换连接到交互适配器。
- src/editor/workbench/WorkbenchShell.cpp：增加框选/穿透/覆盖层菜单与显示按钮、模式可用性。
- src/editor/operations/KeymapRouter.cpp：登记 B/Alt+Z/Shift+Alt+Z 并阻止切换动作自动重复。
- src/editor/operations/OperatorRegistry.cpp：增加三个稳定操作 ID，复用现有上下文与禁用策略。
- src/renderer_gl/ViewportWidget.h：声明独立穿透/覆盖层显示状态和通知。
- src/renderer_gl/ViewportWidget.cpp：传递显示状态、更新视图通知，不改变几何或 Context。
- src/renderer_gl/Renderer.h：增加默认兼容的覆盖层/穿透绘制参数。
- src/renderer_gl/Renderer.cpp：覆盖层关闭时不绘制辅助网格/选区/手柄，保持实体着色。
- src/renderer_gl/ComponentOverlayRenderer.h：声明可选穿透绘制参数。
- src/renderer_gl/ComponentOverlayRenderer.cpp：按穿透状态控制深度并恢复之前的深度开关。
- tests/ComponentSelectionTests.cpp：新增批量集合/活动项用例。
- tests/ComponentPickerTests.cpp：新增 7 个框选/裁剪/可见区间/导入遮挡用例。
- tests/ComponentBoxTests.cpp：新增 5 个真实窗口框选、取消、输入、显示用例。
- tests/GpuTextureTests.cpp：新增穿透覆盖层的颜色/深度读回与 GL 状态恢复用例。
- tests/OperatorSearchTests.cpp：登记数量更新为 40，保留唯一性与真实 QAction 断言。
- tests/CMakeLists.txt：登记新增窗口测试。
- docs/v2-component-picking.md：新增操作规则、数据流、显示边界、验证与回滚说明。
- docs/v2-edit-mode.md：更新共享查询和框选能力，保留 EDIT-02 历史证据。
- docs/v2-input-operations.md：同步 40 项 Registry、快捷键与 Legacy 替代入口。
- docs/blender-compatibility.md：说明已接通 PICK-01 与后续未实现项。
- docs/v2-development-plan.md：PICK-01 完成，下一 EDIT-03，全部二期目标不缩减。
- README.md：同步当前框选、穿透、覆盖层能力。
- progress.md：仅追加本轮结果、失败定位、证据和回滚点。
- out/validation/v2-pick01-preflight.ps1、v2-pick01-format.ps1、v2-pick01-verify.ps1、v2-pick01-check.ps1：本轮备份、差异格式、串行验证与检查助手；本机产物不提交。
- 回滚点：`out/validation/v2-before-pick01/`。先备份后续修改，按上述旧文件相对路径执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-pick01/<相对路径>' -Destination 'E:/Mini3D/<相对路径>'`；新增窗口测试/组件框选文档逐个移入独立保留目录或备份后撤回。保留 progress.md 并追加回滚说明，然后重新运行 CMake 与两配置测试。不得全仓 reset/checkout，也不恢复失效 PowerShell 缓存。
- 仍待：EDIT-03、拓扑算子/F9、游标/枢轴/吸附/隔离/组件隐藏、Mirror、完整格式/OBJ、全部 P1、PERF 与 QA。当前查询每次构建 CPU 投影数据，未证明 1万/5万/10万点性能；未把小场景通过当成性能达标。未提交、推送、发布或更新旧候选包。

## 2026-09-12 - Task: EDIT-03 点边面 G/R/S 与独立预览、历史和文件闭环

### What was done

- 完成组件移动、旋转与缩放：按源点/边端点/面角去重顶点，使用世界增量与完整逆世界矩阵回写局部坐标，支持负/非均匀父变换；不修改对象 TRS 或其他实例。
- 组件事务冻结 before、网格/revision、世界矩阵和完整选区，每帧从 before 重算。候选与 Scene 真源分离，预览不改变 dirty、历史或 Scene revision，也不发布场景树结构重建。
- 视口实体、组件覆盖层、N 侧栏共读候选；原 GL Context 内更新 GPU，关闭覆盖层仍显示真实实体变形，取消切回原帧。侧栏明确标记尚未写入文档。
- 确认只安装一次 after，最多新增一条历史；无变化零条并保留 redo。Undo/Redo 在仍 Edit 同一对象时恢复操作选区和内容，revision 向前推进。保存取消未确认候选，确认后的所有点/边/面 GRS 组合可保存重开。
- 复用 ObjectTransformSession 输入解析，通过显式 Object/Components 目标分流到原对象事务或新组件事务，不复制一套模态解析。新增三个独立 Registry ID；菜单、F3、Q、Blender G/R/S 共用动作，快捷键标签按模式互斥，Legacy W/E/R 保留对象手柄语义。
- 完成轴/平面、局部轴切换、精细连续、Ctrl 反转步进、非法候选修正、Esc/右键/失焦/模式/相机/文档取消及 B 会话互斥。当前组件枢轴为去重选区中心，其他枢轴和顶点吸附仍待对应计划。
- 同步用户说明、兼容边界与计划。保持 Y-up、单线程与唯一历史；未改网络/并发路径、文件协议、依赖清单，也未提交、推送或发布。

### Testing

- 最小 Debug 编辑器测试目标编译通过后接入显示与输入。首次接入编译产生局部变量遮蔽告警 C4456，已对本轮新增冲突定向改名；后续编译通过。
- 最终 Debug/Release 完整 build 成功（独立 opengl-viewport-verify 目录，包含编辑器、四个测试目标及已启用 tools）。两配置核心数学各 4 用例/221 断言通过：去重质心、仅选中点、拓扑/UV/颜色、法线、负/非均匀矩阵、固定 before、无变化与无效/塌缩输入。
- 两配置组件事务/真实 UI 专项各 8 用例/495 断言通过（含截图）：真源/三类 revision/dirty/历史/树通知隔离；完整选择 Undo/Redo；保存中取消与确认后重开；点/边/面 × G/R/S 九组合；空选择/零缩放/非法修正；真实 GPU 帧差异与精确恢复；侧栏数值；菜单/F3/Q/Legacy/文本/合成 IME；轴二按、精细、吸附取反、相机/Tab/B 取消。
- 既有对象模态、Edit、框选、搜索与键位回归，两配置各 32 用例/781 断言通过。150% 组件专项 8/495、输入回归 32/781；200% 组件专项 8/495、输入回归 32/792。已查看 100% / 200% 真实截图，未把模拟 DPI 当作跨屏或真实中文输入法验收。
- 最终 CTest：Debug 4/4（57.85s），Release 4/4（21.71s）。首轮 Debug 3/4，ObjectTransformSessionTests 的取消测试在 begin() 后 qWait(10) 检查失活；保留 v2-edit03-ctest-debug.xml。增加只读生命周期事件轨迹，未修改产品取消规则/断言/等待时长；原随机种子 3849071529 全部 96 用例/3142 断言通过，随后完整 CTest 复验通过。首轮失活未重复捕获，原因仍不确定，QA-01 保留跟踪，不声称已修复该偶发现象。
- XML：out/validation/v2-edit03-core-{debug,release}.xml、v2-edit03-{debug,release}-dpi1-final.xml、v2-edit03-regression-{debug,release}-dpi1-final.xml、v2-edit03-ctest-debug-recheck.xml、v2-edit03-ctest-release.xml、v2-edit03-editor-debug-lifecycle.xml；高 DPI 与 PNG 同 v2-edit03-* 前缀，初始/扩展轮证据保留。
- 32 个改动文件的严格 UTF-8 无 BOM/LF、C++ clang-format、Markdown 本地链接、辅助脚本语法、git diff --check 和 progress 历史前缀检查通过；仅格式化本轮差异/新增源码，无全仓转码。Release CPU 测试依赖表只有 Windows/MSVC 运行库，无 Qt/GL。

### Notes

- src/core/modeling/VertexTransform.h：新增纯 CPU 组件变换结果、世界质心与候选接口。
- src/core/modeling/VertexTransform.cpp：实现去重顶点变换、属性/法线保持、候选校验和非法拒绝。
- src/core/CMakeLists.txt：登记组件变换数学源码。
- src/core/Scene.h：为已验证几何快照提供只读内容访问，不暴露可变真源。
- src/editor/ComponentSelection.h：声明所选组件对应的去重源顶点查询。
- src/editor/ComponentSelection.cpp：复用选择域投影及 reconcile 生成顶点并集。
- src/editor/SceneViewModel.h：声明独立组件事务、显示查询、完成/预览信号与几何历史 helper。
- src/editor/SceneViewModel.cpp：实现冻结/预览/确认/取消、真源与显示分离、历史选区恢复及已有意图取消边界。
- src/editor/operations/ObjectTransformSession.h：增加显式变换目标和组件世界中心，保留原对象入口兼容。
- src/editor/operations/ObjectTransformSession.cpp：共用输入输出组件增量矩阵，按事务分流、显示组件 HUD 并在相机变化取消。
- src/editor/operations/KeymapRouter.h：增加编辑模式镜像接口，限定用于动作和快捷键标签分发。
- src/editor/operations/KeymapRouter.cpp：GRS 按模式选择独立组件/对象动作，维护互斥标签。
- src/editor/operations/OperatorRegistry.cpp：新增 mesh.translate/rotate/scale 和空组件选择原因，总计 43 项。
- src/editor/operations/ComponentInteraction.cpp：覆盖层读取候选，同步视口实体预览，保证框选/变换互斥。
- src/editor/MainWindow.cpp：接入组件变换菜单、会话目标和模式路由通知。
- src/editor/workbench/WorkbenchShell.cpp：N 侧栏显示候选活动元素坐标并说明未确认状态。
- src/renderer_gl/ViewportWidget.h：声明只读 CPU 显示候选入口及独立显示 record。
- src/renderer_gl/ViewportWidget.cpp：保存/清除显示候选，在 paintGL 传递，不在输入中上传 GPU。
- src/renderer_gl/Renderer.h：为绘制传入可选实体候选，保留已有调用默认值。
- src/renderer_gl/Renderer.cpp：递归绘制目标实体时使用候选，复用内容/revision GPU 缓存。
- tests/VertexTransformTests.cpp：新增纯 CPU 数学、属性、非法输入和坐标验证。
- tests/ComponentTransformTests.cpp：新增事务、保存与实际窗口/GPU/输入综合验收。
- tests/ObjectTransformSessionTests.cpp：为全量失活诊断增加只读事件轨迹，不改变被测取消行为。
- tests/OperatorSearchTests.cpp：真实 Registry 数量从 40 更新至 43。
- tests/CMakeLists.txt：登记新增纯 CPU 与编辑器测试文件。
- docs/v2-component-transform.md：新增使用、坐标、事务、输入、测试与回滚说明。
- docs/v2-edit-mode.md：同步已实现组件变换、侧栏/历史选区语义和真实操作数量。
- docs/v2-input-operations.md：同步组件操作 ID、模式互斥路由与说明链接。
- docs/blender-compatibility.md：区分组件 GRS 与后续拓扑/枢轴/转换未完成边界。
- docs/v2-development-plan.md：记录 EDIT-03 完成与下一 MODEL-01，QA 跟踪首轮未复现失活。
- README.md：同步组件变换入口与当前未完成项。
- progress.md：仅末尾追加本轮事实、验证、风险与回滚点。
- out/validation/v2-edit03-{foundation-preflight,input-preflight,format,test,verify,check,diagnose,recheck}.ps1：本机定向备份、差异格式、验证与失败诊断助手，不提交生成产物。
- 回滚点：out/validation/v2-before-edit03-foundation/ 与 out/validation/v2-before-edit03-input/，另有 out/validation/v2-edit03-ObjectTransformSessionTests.before.cpp。先备份后续改动；按本清单相对路径逐个 Copy-Item -LiteralPath 恢复已有文件，ObjectTransformSessionTests.cpp 使用单文件备份恢复；新增两个数学文件、两个测试文件和组件变换文档单独备份后撤回。保留 progress.md 并追加回滚说明，然后重新 CMake/build/test。禁止全仓 reset/checkout，不恢复已失效 PowerShell 缓存。
- 尚待：MODEL-01 挤出与后续 F9/内插/环切/补面删除、游标/其他枢轴/顶点吸附/隔离/组件隐藏、Mirror、静态 glTF/球体/平面编辑转换、完整格式/OBJ、全部 P1、PERF 和最终 QA。小 Cube 的正确性不代表 1万/5万/10万点性能达标；未检测全局自交，分配失败注入仍须 HISTORY 门槛。二期目标保持 active。

## 2026-09-12 - Task: 固化 PowerShell 升级后的构建路径修复，暂停二期开发

### What was done

- 按用户指示暂停二期，仅处理 PowerShell 构建环境；保留全部既有二期未提交/未完成源码，不继续拓扑开发、不提交或推送。
- 消除本机配置及示例对 Store 版本目录的硬编码，在 project() 前共用自动定位模块。通过运行 pwsh 获取真实进程路径并校验 7.6.x，每次配置同步 vcpkg 两项缓存和当前进程 PATH。
- 支持从稳定应用执行别名、标准目录或 PATH 查找；可选非标准安装提示已失效时继续查找。找不到安装、启动失败或版本不符时明确停止配置，不改系统 PATH、不下载/重装、不关闭依赖部署。
- 增加独立配置回归及环境使用说明。Store 补丁升级后仍需运行 CMake，使已有生成工程刷新；不承诺直接调用未刷新的旧工程也能恢复。

### Testing

- PowerShell 7.6.6 实际进程及 WindowsApps 稳定别名均可运行；旧 7.6.4 包路径不存在。未改系统安装或应用执行别名设置。
- `cmake -P tests/PowerShellConfigTests.cmake`：4/4 通过，覆盖旧两项缓存和失效目录提示、无 PowerShell PATH 的 Store 别名解析、显式实际目录及二次探测、缺少安装时提前报错；测试不改实际构建缓存。
- 在 `out/build/windows-msvc-local` 配置命令中显式传入两项失效 7.6.4 缓存，自动恢复为真实 7.6.6；Configure exit 0，无 PowerShell 下载。证据：`out/validation/pwsh-stable-configure.log`。
- 同目录完整 Debug build exit 0，编辑器和四个测试目标均构建成功，无 MSB3073。证据：`out/validation/pwsh-stable-build-debug.log`。这仅证明当前业务源码能编译，不表示未完成 MODEL-01 功能已验收。
- 两项缓存、五个目标各四种配置的生成命令均使用当前真实路径；对五个 Debug 二进制分别实际运行 vcpkg applocal.ps1，全部 exit 0。证据：`out/validation/pwsh-stable-verify.log`。Release 仅检查生成命令，未重新构建。
- 定向 Debug CTest 核心、资源和 GPU 3/3 通过（1.25s），证据：`out/validation/pwsh-stable-ctest-debug.xml`。本轮未运行完整编辑器业务测试，不将之前的 4/4 结果当作本轮结果。
- 六个正式改动文件严格 UTF-8 无 BOM/LF 校验、历史 progress 前缀及 `git diff --check` 通过；无编码转换或全量格式化。

### Notes

- `CMakeLocalConfig.cmake`（Git 忽略）：去掉机器上的 Store 版本目录和重复缓存设置，加载自动定位模块；保留 vcpkg 和 Qt Creator 设置。
- `CMakeLocalConfig.cmake.example`：同步自动定位入口，非标准 PowerShell 目录改为可选提示，不携带本机专属路径。
- `cmake/Mini3DPowerShell.cmake`：新增真实进程路径探测、7.6.x 校验和每次配置刷新两项缓存的共用逻辑。
- `tests/PowerShellConfigTests.cmake`：新增四场景独立 CMake 配置回归，无业务/Qt 依赖。
- `docs/development-setup.md`：更新安装基线、自动发现规则、旧本机配置迁移、升级后重新运行 CMake 的边界和回归命令。
- `progress.md`：仅追加本轮修复、验证证据及二期暂停状态。
- `out/validation/pwsh-stable-preflight.ps1`、`pwsh-stable-verify.ps1` 及上述日志/XML：本机备份、生成命令/部署/编码复核与验证产物；不提交构建输出。
- `out/build/windows-msvc-local/`：CMake 重新生成并完成 Debug 构建和依赖部署，未手改 vcxproj。
- 回滚点：`out/validation/before-pwsh-stable-20260912/`。先备份后续改动，逐个执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/before-pwsh-stable-20260912/<相对路径>' -Destination 'E:/Mini3D/<相对路径>'` 恢复两份 CMake 本机配置及环境说明；本轮新增模块和测试文件备份后分别执行 `Remove-Item -LiteralPath 'E:/Mini3D/cmake/Mini3DPowerShell.cmake'`、`Remove-Item -LiteralPath 'E:/Mini3D/tests/PowerShellConfigTests.cmake'` 撤回。保留 progress 历史并追加回滚说明，随后运行 `& 'E:/cmake-3.31.0-rc1-windows-x86_64/bin/cmake.exe' -S E:/Mini3D -B E:/Mini3D/out/build/windows-msvc-local`。回滚恢复的是前一版 7.6.6 显式配置，不恢复失效的 7.6.4 缓存；禁止全仓 reset/checkout 或覆盖二期源码。

## 2026-09-12 - Task: 恢复二期并完成 MODEL-01 区域挤出的实现与验收

### What was done

- 按用户“现在继续二期开发”恢复既有顺序，完成中断中的 MODEL-01，不重启基线、不改变已固化的 PowerShell 自动定位，也不扩大到其他未完成工具。
- 新增纯 CPU 区域挤出：共享边连通、可定向面域及合法边界生成顶盖/侧壁；支持非共面区域和不接触的多边界环，明确拒绝整闭壳、不连通、边界接触/分叉、零位移、退化与 ID 耗尽。
- 顶盖保留原 FaceId/CornerId、UV、颜色、硬法线和材质，选中面自然转为顶盖；侧壁继承边界相邻面的材质及两端颜色，生成法线与矩形 UV。只清理由本次替换留下的孤立旧点。
- 复用组件独立候选和唯一历史：每帧从 before 重算，预览不改真源/dirty/revision；安全取消不留零距离重叠拓扑；确认一次一条历史，撤销重做恢复快照和选区，保存重开保留源多边形。
- Blender E、菜单、F3、Q 共用 mesh.extrude_region，Registry 为 44 项；Legacy E 保留旧旋转手柄。接通默认面积加权法线、抵消时最小 FaceId 替代法线、正负数值、轴/排轴、Shift 精细、Ctrl 步进反转及视线对齐时竖向鼠标距离。
- 修复无效输入后恢复有效预览仍残留属性错误的问题，只刷新提示，不每帧重建父对象列表。高 DPI 截图的裁切被证实是合成按键后未交付布局请求，增加截图前布局与高度断言，未修改 WorkbenchShell 产品尺寸。
- 更新操作说明、兼容性、计划和当前能力；不把 F9、其余拓扑工具、P1 或整体二期标成完成。保持 UTF-8 无 BOM/LF、单线程、Y-up、Core 无 Qt/GL 及原 GL Context；没有提交、推送、发布或覆盖 V1 交付物。

### Testing

- 最终命令：`& 'E:/Mini3D/out/validation/v2-model01-verify.ps1' -Suffix layout-final`，exit 0；Debug/Release 完整 build 成功。CTest 各 4/4，Debug 67.63s、Release 25.19s，包含核心、资源、GPU 和编辑器。
- 两配置纯 CPU 各 7 用例/399 断言：正负挤出、单面 12V/20E/10F、非共面多面、多边界环、法线抵消、侧壁绕序/材质/端点颜色、稳定 ID/UV/硬法线、原始独立点、不连通/退化/ID 耗尽及 before 不变。
- 两配置挤出事务与真实 UI 各 9 用例/215 断言（含两张截图）：多帧不累积、独立候选/dirty/通知/revision、顶盖选择、负非均匀父矩阵、确认一历史、撤销重做和文件往返；E/菜单/F3/Q/Legacy、数值修正、轴约束、精细/吸附、上下文取消、GPU 帧变化和取消精确恢复。
- 共用组件/对象输入、Edit、框选、搜索、收藏与键位回归，两配置各 43 用例/1343 断言。Debug 150%：挤出 9/215、回归 43/1343；200%：挤出 9/215、回归 43/1354。已查看 100/150/200% 的真实 Qt 截图，N 侧栏提示完整可读；不代替真实跨屏、中文输入法或 Blender 对照验收。
- 属性提示先复现后修复：v2-model01-stale-feedback-before.xml 为 1 用例失败，after.xml 为 1 用例/19 断言通过。此处确有产品修复，不只是改测试预期。
- 原回归出现过两处失败：EditMode 窗口未激活，以及对象 no-op 历史数 3 != 2，保留 v2-model01-debug-dpi1-final-regression.xml。增加窗口状态、MouseMove 坐标/spontaneous、位置/缩放和历史文字诊断，未改取消规则、等待时长或放宽断言。原种子 593451477 带诊断 40 用例/1279 断言通过，尚未重复捕获根因；QA-01 继续跟踪，不能宣称已修复。
- 随后完整 Debug CTest 发现旧收藏用例把 mesh.extrude_region 当未知操作（v2-model01-ctest-debug-recheck.xml）。换为 unknown.operator，保留未知拒绝断言并增加已注册挤出收藏增删检查；专项 3 用例/64 断言通过，未改收藏产品代码。
- 首轮完整复验 complete-run 两配置全量与高 DPI 已通过，但查看截图发现侧栏裁切。诊断 v2-model01-dpi2-layout-diagnostic.xml 证实 200% 下提示所需高度 80，未处理布局时实际 60；交付 LayoutRequest 后实际 80。2 用例/48 断言及两截图通过，最终 layout-final 将该检查纳入所有配置/DPI，不通过盲加延时或改侧栏规避问题。
- 最终 XML/PNG/build 日志为 out/validation/v2-model01-*-layout-final*；早期 initial/expanded/final/recheck/complete-run 和诊断失败证据全部保留。Release CPU 测试依赖表只含 Windows/MSVC，无 Qt/GL。
- PowerShell 7.6.6 下再次运行 `cmake -P tests/PowerShellConfigTests.cmake`，stale/alias/explicit/missing 四场景全部通过，无配置回退。
- 27 个交付文件严格 UTF-8 无 BOM/LF、C++ clang-format、Markdown 本地链接、git diff --check、历史 progress 前缀及中途 PowerShell 固化记录检查通过；无全量格式化或转码。

### Notes

- src/core/modeling/ExtrudeRegion.h：新增区域分析、法线信息和绝对位移候选接口。
- src/core/modeling/ExtrudeRegion.cpp：实现连通边界、顶盖/侧壁生成、属性保持、孤立点定向清理及完整候选校验。
- src/core/CMakeLists.txt：登记挤出算法源码。
- src/editor/SceneViewModel.h：声明挤出事务入口、禁用原因和当前挤出信息。
- src/editor/SceneViewModel.cpp：复用组件候选/快照历史，转换世界法线与局部位移，拒绝零初始确认。
- src/editor/operations/ObjectTransformSession.h：增加显式 ExtrudeRegion 目标及法线位移状态。
- src/editor/operations/ObjectTransformSession.cpp：接入 E 的默认法线、数值/约束/精细/步进输入与安全取消反馈。
- src/editor/operations/KeymapRouter.cpp：Blender Edit E 路由到挤出，保留 Legacy 原语义。
- src/editor/operations/OperatorRegistry.cpp：登记 mesh.extrude_region 及上下文禁用原因。
- src/editor/MainWindow.cpp：接入同一挤出 QAction、中文菜单及模态启动。
- src/editor/TransformInspector.h：声明独立提示刷新入口。
- src/editor/TransformInspector.cpp：有效候选或取消清除旧错误，不在预览每帧重建属性列表。
- tests/ExtrudeRegionTests.cpp：新增算法、ID、区域拓扑、边界拒绝及逐面材质/端点颜色夹具。
- tests/ExtrudeRegionEditorTests.cpp：新增事务/保存、真实窗口/GPU/输入、错误恢复、多面与侧栏布局验收。
- tests/CMakeLists.txt：登记新 Core/Editor 测试。
- tests/OperatorSearchTests.cpp：将真实注册表数量更新至 44，保留唯一性检查。
- tests/QuickFavoritesTests.cpp：更新未知操作夹具，增加已注册挤出收藏增删验证。
- tests/ObjectTransformSessionTests.cpp：扩充旧偶发 no-op 失败的鼠标/变换/历史诊断，不更改产品或断言语义。
- tests/EditModeTests.cpp：为窗口激活失败增加可见性和活动窗口诊断，保留原激活要求。
- docs/v2-extrude-region.md：新增使用、法线/属性/区域限制、事务、验证和回滚说明。
- docs/v2-component-transform.md：同步挤出与当前 Registry 数量，保留 EDIT-03 当时数量。
- docs/v2-edit-mode.md：更新顶盖选区、挤出入口及支持范围。
- docs/v2-input-operations.md：更新 E/44 项操作与未实现边界。
- docs/blender-compatibility.md：区分安全原子挤出取消、支持面域和后续工具。
- docs/v2-development-plan.md：MODEL-01 完成，下一 HISTORY-01/02，S06 专项通过但最终 QA 未完成。
- README.md：同步挤出当前能力和说明入口，保留内插/环切/F9 未完成声明。
- progress.md：仅末尾追加本项恢复、实现、所有失败/复验事实和回滚点，保留 PowerShell 修复记录。
- out/validation/v2-model01-{preflight,format,test,verify,check,layout-test}.ps1 及相关 XML/PNG/log：本机备份、验证/格式检查和布局诊断产物，不作为发布包、不提交构建输出。
- 回滚点：out/validation/v2-before-model01/。先备份后续改动，按上述旧文件相对路径逐个执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-model01/<相对路径>' -Destination 'E:/Mini3D/<相对路径>'`，不要覆盖 progress.md。TransformInspector.h/.cpp 使用 v2-before-model01-feedback/ 中同名备份；docs/v2-component-transform.md 使用该目录的 v2-component-transform.md；三个既有诊断/收藏测试使用 v2-before-model01-diagnostics/ 中同名备份。五个新增文件为 ExtrudeRegion.h/.cpp、ExtrudeRegionTests.cpp、ExtrudeRegionEditorTests.cpp 和 docs/v2-extrude-region.md，逐个保留备份后撤回。保留历史日志并追加回滚说明，重新 CMake/build/test；禁止全仓 reset/checkout，不恢复旧 PowerShell 路径或覆盖其他二期工作。
- 仍待 HISTORY-01/02（before 重算、原子替换、失败/分配失败、save→F9 dirty、面板）、内插/环切/补面删除、游标/枢轴/吸附/隔离/Mirror、完整格式/OBJ、全部 P1、PERF 与最终 QA；二期整体未完成。小 Cube 通过不代表大网格性能达标，尚无全局自交检测。

## 2026-09-12 - Task: HISTORY-01 可重开挤出的历史基础与保存点保护

### What was done

- 在 MODEL-01 完成记录后依序推进 HISTORY-01；新增 HistoryService 作为唯一 QUndoStack 的非拥有适配器，不增加第二个历史栈。
- 挤出确认记录原 before、after、结果选区、世界位移和离线重算上下文；调整始终从该次 before 求值，不叠加侧壁或消耗新的逻辑历史。连续两次挤出各自保留 before，调整仅作用于最新一次。
- 采用同一 Qt 命令内替换已准备 after，避免临时 undo 后 push 再分配的失败窗口；首次确认仍 push，Undo/Redo 只安装快照。同步更新派生契约说明，未改用户原始方案资料。
- 重算、几何校验、选区和通知副本均在写真源前完成；准备分配失败返回中文错误且保留原几何、选区、参数、revision 和保存点。实际安装只移动准备状态且不发通知，提交完成后才发布变化。
- 已保存的顶端被替换时 resetClean，不因 index 相同误报已保存；保存点在 before 时仍可 Undo 回 clean。参数/几何不变无操作，不更新 revision、不清除保存点、不写磁盘。
- ViewModel 接入可用性/世界位移查询、调整意图和参数变化信号；导航保留，未结束变换暂禁用，其他数据命令/历史不在末端/文档重置后不可重开旧项。回到相同对象 Edit 可恢复使用，调整使用冻结面域而非新临时选区。
- 本项只完成历史基础，尚未注册 F9 动作或创建参数面板；HISTORY-02 仍待办。保持原 E、组件 GRS、Y-up、单线程、Core/GL 边界和 PowerShell 固化配置，没有提交、推送或发布。

### Testing

- 先运行无窗口/无 GL 的真实 Scene/QUndoStack 夹具，初版 3 用例/86 断言通过（v2-history01-Debug-initial.xml），再接入 ViewModel；随后将第二个故障点改为实际通知副本构造抛 bad_alloc，并增加浮点量化后几何无变化检查。
- 新增父变换验证首次编译报 C2678：Transform 无整体 operator==。仅将新增测试改为分别比较位置/旋转/缩放，未扩展生产 Transform API；修正后编译通过。集成筛选 19 用例/533 断言通过（v2-history01-Debug-integrated.xml）。
- 最终 `& 'E:/Mini3D/out/validation/v2-history01-verify.ps1'` exit 0，Debug/Release 完整 build 成功；CTest 各 4/4，Debug 65.51s、Release 24.49s。包含完整核心、资源、GPU、编辑器回归。
- 两配置历史专项各 8 用例/207 断言通过：绝对 before 重算、连续多次调参无累积、同一命令地址/索引/条数、结果选择、revision、无效/零/NaN/退化、相同参数和量化无变化、离线重算/通知副本分配失败、原保存点和顶端保存点、Undo/Redo 不运行算法、其他数据/非顶端/清空上下文。
- ViewModel 验证实际保存文件：保存→调整不覆写磁盘、dirty 正确、Undo/Redo 后仍未保存、再次保存重开几何一致且无 F9 历史；通知监听者看到完整新参数/dirty/index。覆盖冻结选区、负非均匀父变换下世界位移，以及第二次挤出只替换自己的 after。
- 200% 共用挤出验证 Core 7/399、真实 UI 9/215（含两截图）、组件/对象/编辑模式/框选/搜索/收藏/键位回归 43/1354 全部通过。当前无新增 UI，F9 按键、参数面板与真实调参截图留在 HISTORY-02，不把本项意图测试当作可见入口验收。
- 证据：out/validation/v2-history01-{Debug,Release}-final.xml、v2-history01-ctest-{Debug,Release}-final.xml、v2-history01-build-{Debug,Release}-final.log；高 DPI 为 v2-model01-debug-dpi2-history01-final-* 与对应 PNG。既有 MODEL-01 失败与复验文件保留，旧偶发窗口/历史问题仍由 QA 跟踪。
- 13 个交付文件 UTF-8 无 BOM/LF、C++ clang-format、Markdown 本地链接、辅助脚本语法、git diff --check 及完整 MODEL-01/progress 历史前缀检查通过。只格式化新增源码及本项 ViewModel 差异，无全量格式或转码。

### Notes

- src/editor/operations/HistoryService.h：声明几何历史状态、离线重算/无分配安装/通知契约和唯一栈适配。
- src/editor/operations/HistoryService.cpp：实现可重开快照命令、仅末端查询、after 替换、准备失败保护和保存点失效。
- src/editor/SceneViewModel.h：增加上一步查询/调整/通知入口、位移记录及历史适配成员。
- src/editor/SceneViewModel.cpp：挤出确认使用可重开命令，冻结原面域/世界矩阵，分离安装与通知，接入上下文与参数意图。
- src/editor/CMakeLists.txt：登记历史适配模块。
- tests/HistoryServiceTests.cpp：新增真实唯一历史夹具、no-op/保存点/故障注入和快照回放验证。
- tests/HistoryEditorTests.cpp：新增 ViewModel/实际文件、通知完整性、上下文、父矩阵和连续两次挤出的验证。
- tests/CMakeLists.txt：登记两个历史测试文件。
- docs/v2-history.md：新增当前意图边界、世界参数、原子安装、保存点、故障注入与回滚说明。
- docs/v2-contracts.md：将建议 undo/push 原型明确落实为同命令 after 替换，说明避免中途分配的原因。
- docs/v2-extrude-region.md：区分已接通历史基础和仍未开放的 F9 按键/面板。
- docs/v2-development-plan.md：HISTORY-01 完成，下一 HISTORY-02，二期整体保持未完成。
- progress.md：仅追加本项实施、编译问题、验证和边界，不改前序记录。
- out/validation/v2-history01-{preflight,format,test,verify,check}.ps1：本机定向回滚、差异格式、串行验证和只读检查助手；产物不作为发布包。
- 回滚点：out/validation/v2-before-history01/，该点保留已完成的 MODEL-01。先备份后续修改，按本项旧文件相对路径执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-history01/<相对路径>' -Destination 'E:/Mini3D/<相对路径>'`，不覆盖 progress.md；新增 HistoryService.h/.cpp、HistoryServiceTests.cpp、HistoryEditorTests.cpp、docs/v2-history.md 分别备份后撤回。保留历史并追加回滚说明，重新 CMake/build/test；不全仓 reset/checkout，不恢复任何过期 PowerShell 路径。
- 边界：分配失败保证针对可恢复的候选/提交准备阶段，提交回调必须无分配无异常；不声称能恢复系统/Qt/驱动致命 OOM。用户询问进度时已说明 F9 UI 尚未接通；本轮完成当前验证与记录，不把 HISTORY-02 或其后需求算作已完成。

## 2026-09-12 - Task: HISTORY-02 F9 与左下上一步参数面板

### What was done

- 从既有 HISTORY-02 施工现场继续，核对代码与回滚点后完成面板验收；没有重做已完成的历史服务或 PowerShell 修复。
- 挤出确认后左下自动出现折叠标题，不抢焦点；F9、菜单、F3 与 Q 共用同一面板。Blender F9 展开并聚焦 Z，Legacy 保持菜单/F3/Q 替代入口；Registry 合计 45 个真实操作。
- 字段明确是世界 XYZ 位移，每次从原 before 重算并替换同一历史；只改用户提交的轴，其他轴保持精确参数。普通 Enter/失焦提交，非法内容保留并提示，Esc 恢复草稿，收起不偷提交。
- 修复点击收起前的焦点转移提前提交问题：只在鼠标将焦点转向收起标题时，于字段失焦提交前丢弃草稿；不改变属性区原有普通失焦行为。
- 未结束变换、退出 Edit、其他数据命令和非末端历史禁用字段并解释；导航保留，文档重置隐藏面板/清草稿。面板叠加在原 GL Widget 上，鼠标、滚轮、快捷键不穿透相机或组件选择。
- 同步使用说明、输入契约、能力边界和计划；当前 F9 仅支持挤出，不把内插/环切或二期整体记为完成。保持单线程、唯一历史、Y-up 和原文件协议；未提交、推送、发布或覆盖 V1 包。

### Testing

- 新测试编译前核实相机 getter 和 Inspector 名称，改为真实 editorCameraSnapshot()->state() / EntityName；新测试 Debug 构建通过。
- 首轮专项在 F3/Q 测试崩溃：查找了不存在的 OperatorQuery，实际为 OperatorSearchQuery；读取弹窗源码后修正名称并增加指针断言，Q 使用真实结果列表确认。保留 v2-history02-Debug-1-initial.xml。
- 第二轮发现收起草稿实际被提交，以及关闭提示测试的模拟方式错误。关闭测试改为真实点击 QMessageBox Cancel，原 done(Cancel) 未设置 clickedButton 会走到保存；仅影响本测试临时文件，未操作用户文档。诊断确认收起顺序为 value=0.75 → editingFinished → pressed → toggled=0；按失焦边界修复后，5用例123断言通过。失败/诊断证据为 control-name-fix、focus-diagnostic，修复证据为 focus-boundary-fix。
- 扩充三轴收起、六位显示不污染未编辑高精度参数、真实滚轮导航、模态/退出 Edit 临时禁用与恢复；面板专项最终6用例161断言通过（v2-history02-Debug-1-extended.xml）。不放宽原几何、历史或焦点断言。
- `& 'E:/Mini3D/out/validation/v2-history02-verify.ps1'` exit 0。Debug/Release 完整 build 成功；CTest 各4/4，耗时74.21s / 24.35s，覆盖核心、资源、真实 GPU、编辑器全量。Release 编辑器119用例3730断言通过。
- 面板/数值/历史/挤出/搜索/收藏/键位综合两配置各38用例905断言通过，150%与200%各38用例905断言通过。真实保存→调参→关闭提示→取消、同一命令地址/条数、10面不累积、Undo/Redo、文件重置、精度、文本/合成IME、侧栏/缩放布局和原GL Context均验证。
- 已查看100/150/200%实际Qt截图，中文字段和提示可读。200%请求的大窗口高度被当前屏幕工作区夹取，断言针对真实视口；不声称跨屏、真实中文输入法或Blender对照通过。驱动像素传输性能提示及既有终端诊断编码提示不作为本项源码错误。
- 最终证据：out/validation/v2-history02-build-{Debug,Release}-final.log、v2-history02-ctest-{Debug,Release}-final.xml、v2-history02-{Debug,Release}-1-final.xml/png、v2-history02-Debug-{1.5,2}-final.xml/png。Debug首轮脚本截图环境变量曾传到随后CTest，导致同一路径截图再次保存；已在助手finally清理该变量，独立extended截图和XML保留，Release/高DPI不再继承。此项不影响产品或测试断言。
- 21个交付文件UTF-8无BOM/LF、局部C++格式、Markdown链接、脚本语法、git diff --check及progress原历史前缀检查通过。保留已有二期改动，不全量格式化/转码；沿用C++/Qt/UTF-8技能，排障技能用于事件证据定位和定向复验。

### Notes

- src/editor/workbench/LastOperationPanel.h：新增同一可折叠面板的视图接口与字段成员。
- src/editor/workbench/LastOperationPanel.cpp：接入字段、草稿、可用性、布局、F9聚焦与输入隔离。
- src/editor/workbench/CommitSpinBox.h：声明不提交的resetInput接口。
- src/editor/workbench/CommitSpinBox.cpp：复用原cancelInput清除草稿/错误，不修改普通提交规则。
- src/editor/MainWindow.cpp：添加调整菜单并连接面板和可用性通知。
- src/editor/operations/KeymapRouter.cpp：隔离面板区域并忽略F9自动重复。
- src/editor/operations/OperatorRegistry.cpp：登记调整操作、挤出reopenable及世界位移schema/禁用原因。
- src/editor/CMakeLists.txt：登记面板源码。
- tests/LastOperationPanelTests.cpp：新增6个真实交互/历史/保存/精度/布局用例及焦点顺序诊断。
- tests/OperatorSearchTests.cpp：注册数量更新为45并保留唯一性断言。
- tests/CMakeLists.txt：登记面板测试。
- README.md：说明挤出F9已接通、其余拓扑工具未完成。
- docs/v2-history.md：新增可见面板操作、草稿/焦点规则、验证与回滚说明。
- docs/v2-input-operations.md：更新45项Registry及新旧键位入口。
- docs/v2-extrude-region.md：接入挤出后F9调参的操作说明。
- docs/blender-compatibility.md：限定当前可重开算子与世界XYZ语义。
- docs/v2-contracts.md：同步面板输入和同一历史契约。
- docs/v2-component-transform.md：保留历史数量并链接现已通过的F9专项。
- docs/v2-edit-mode.md：新增调整上一步入口并更新当前Registry数量。
- docs/v2-development-plan.md：HISTORY-02完成，S11挤出专项通过，下一MODEL-02。
- progress.md：仅追加本项完成、失败与复验、边界和回滚记录。
- out/validation/v2-history02-preflight.ps1：保存16个旧文件的原始定向回滚点，未重复运行覆盖。
- out/validation/v2-history02-doc-backup.ps1：补存两处关联旧说明的回滚副本。
- out/validation/v2-history02-format.ps1：只格式化本项旧文件差异与新增源码。
- out/validation/v2-history02-test.ps1：保存隔离专项XML/截图并清理本次截图变量。
- out/validation/v2-history02-verify.ps1：串行构建、两配置全量与高DPI相关回归。
- out/validation/v2-history02-check.ps1：只读检查编码、格式、链接、脚本和追加日志。
- 回滚点：out/validation/v2-before-history02/，含18份旧文件副本。先备份后续修改，再按上述旧文件相对路径执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-history02/<相对路径>' -Destination 'E:/Mini3D/<相对路径>'`；不要覆盖progress.md。新增LastOperationPanel.h/.cpp与LastOperationPanelTests.cpp分别备份后撤回；保留历史日志并追加回滚说明，重新CMake/build/test。不全仓reset/checkout，不撤回HISTORY-01或旧PowerShell修复。
- 下一项依原计划为MODEL-02单个共面凸面等距内插，随后环切/补面删除及其余获授权需求；二期整体仍未完成，旧偶发UI失活/no-op历史问题仍留QA跟踪。

## 2026-09-12 - Task: MODEL-02 单个共面凸面等距内插与局部厚度 F9

### What was done

- 在 HISTORY-02 闭环后完成单个共面凸面内插；逐边偏移求交点，不以中心缩放替代。厚度为对象局部单位，HUD/字段明确标注，非均匀父缩放后不宣称世界等宽。
- 支持有效共线边点，按首条边坍缩计算严格上限；零/负/超限、非凸、非共面、自交环及ID不足拒绝。中心面保留FaceId/CornerId，边框使用新ID，原边界及邻面不动。UV/颜色/硬法线按原渲染三角形重心坐标插值。
- I、菜单、F3/Q共用真实模态；鼠标横向200逻辑像素对应坍缩上限，Shift精细，Ctrl反转局部0.1步进，显式数字优先，XYZ不适用并解释。Esc/右键及统一中断恢复整个before。
- 从冻结before求独立候选，真实实体/覆盖层/N侧栏读取同一预览，确认一历史，Undo/Redo及保存重开保留几何和稳定面选择；内插后可继续挤出中心面。
- HistoryService增加vector3/double参数类型，保留唯一栈与原无分配安装路径。F9面板按顶端算子显示世界XYZ或局部内插厚度；调整替换同一after、不增加第二圈，保存点失效与字段草稿保护保留。
- 同步使用说明、契约、能力边界、README及计划；MODEL-02完成，下一MODEL-03。文档示例测试超时由60秒调为180秒，依据当前Debug全编辑器实际约80秒，不放宽任何功能断言。
- 继续使用C++/Qt、Qt命名、UTF-8及PowerShell安全技能；保留原编码和既有成果，不全量格式化/转码，不修改线程/网络/协议或PowerShell配置，未提交、推送、发布。

### Testing

- Core初版5用例574断言通过，补属性插值后6用例608断言通过；VM/历史集成10用例304断言通过。新UI前共用回归32用例831断言通过；内插UI初版5用例168断言、扩展8用例234断言通过，最终删除一条无意义几何中心自包含断言，保留有效检查。
- `& 'E:/Mini3D/out/validation/v2-model02-verify.ps1'` 最终exit 0。Debug/Release完整build成功，CTest各4/4，耗时80.91s / 28.36s；两配置完整编辑器各127用例3966断言通过。
- 两配置Core专项各6用例608断言，覆盖Cube、三角/梯形/五边形、共线点、尺度/平移/斜面/绕序、属性共享、ID重排/耗尽、非法厚度及输入不变。等距以原边到新边有向距离独立验证。
- 内插/面板/数值/历史/挤出/组件/对象模态/选择/搜索/收藏/键位综合两配置各77用例2208断言通过，150%同样通过，200%77用例2219断言通过。包含真实GPU帧取消恢复、局部单位与负非均匀父矩阵、连续内插、保存→F9→关闭提示、类型误用拒绝、文本/合成IME/中断、后续挤出切换字段、同一历史和Context。
- 已查看Debug/Release的100%、Debug150%/200%四张最终Qt截图；内插中心/边框和厚度字段可读。200%请求窗口高度被屏幕工作区夹取，采用实际视口验证；不把单屏DPI或合成IME当跨屏/真实输入法/Blender对照。
- 无算法、编译或测试失败。施工中一次无实质改动的补丁因格式化换行不匹配而拒绝，读现文件后正常补测试；一次rg将HistoryService.*当作Windows文件参数报123，改为明确h/cpp路径。未重复同类失败结构。git --no-index返回1为存在差异，不是验证失败。
- 证据为out/validation/v2-model02-core-{Debug,Release}-final.xml、v2-model02-editor-{Debug,Release}-1-final.xml/png、v2-model02-editor-Debug-{1.5,2}-final.xml/png、v2-model02-ctest-{Debug,Release}-final.xml及build日志；initial/attributes/pre-ui-regression/ui-initial/ui-extended证据保留。驱动像素同步性能提示和旧终端诊断编码不作为源码故障。
- 30个定向文件UTF-8无BOM/LF、局部clang-format、Markdown链接、辅助脚本语法、git diff --check与progress原始前缀检查通过；记录追加后再次执行同一只读检查。

### Notes

- src/core/modeling/InsetFace.h：新增局部等距内插分析和候选接口。
- src/core/modeling/InsetFace.cpp：实现归一化偏移、坍缩上限、属性插值、稳定ID与离线校验。
- src/core/CMakeLists.txt：登记内插算法模块。
- src/editor/SceneViewModel.h：声明内插事务、参数查询/调整及预览发布接口。
- src/editor/SceneViewModel.cpp：接入候选、确认/历史、冻结before重算和内插参数意图。
- src/editor/operations/HistoryService.h：定义两类带类型参数与内插调整接口。
- src/editor/operations/HistoryService.cpp：共用原子替换路径，类型不匹配拒绝，保留保存点语义。
- src/editor/operations/ObjectTransformSession.h：增加内插目标与冻结厚度上限。
- src/editor/operations/ObjectTransformSession.cpp：接入I模态距离、鼠标/数字、HUD与轴约束拒绝。
- src/editor/operations/KeymapRouter.cpp：仅Blender风格I路由内插。
- src/editor/operations/OperatorRegistry.cpp：注册mesh.inset_face、局部thickness schema及禁用原因。
- src/editor/MainWindow.cpp：添加面内插菜单并接入统一模态。
- src/editor/workbench/LastOperationPanel.h：增加内插字段容器及顶端类型显示状态。
- src/editor/workbench/LastOperationPanel.cpp：切换局部厚度/世界XYZ、F9聚焦和草稿状态。
- tests/InsetFaceTests.cpp：新增等距算法、属性、稳定身份与拒绝夹具。
- tests/InsetFaceEditorTests.cpp：新增8个事务、父变换、历史/文件及真实UI用例。
- tests/HistoryServiceTests.cpp：适配带类型参数并验证挤出拒绝内插参数。
- tests/OperatorSearchTests.cpp：Registry数量更新为46，保留唯一性和真实入口校验。
- tests/CMakeLists.txt：登记两个内插测试文件。
- README.md：标注内插及F9已接通、环切仍待办。
- docs/v2-inset-face.md：新增实际操作、单位/限制、几何属性、验证与回滚说明。
- docs/v2-history.md：扩展到挤出与内插两种参数和同一面板规则。
- docs/v2-input-operations.md：登记46项、I快捷键和局部厚度schema。
- docs/v2-extrude-region.md：同步数量并说明内插后继续挤出的顶端历史。
- docs/blender-compatibility.md：明确已实现内插与局部单位、F9边界。
- docs/v2-contracts.md：同步两类历史参数和输入契约。
- docs/v2-component-transform.md：链接内插专项，保持原GRS规则。
- docs/v2-edit-mode.md：新增内插/调参入口、稳定中心面及Registry状态。
- docs/v2-development-plan.md：MODEL-02完成、S03/S04/S05专项通过、下一MODEL-03；更新可运行超时示例。
- progress.md：仅追加本项实施、验证、限制和回滚记录。
- out/validation/v2-model02-{preflight,integration-backup,format,core-test,editor-test,verify,check}.ps1：本机备份、定向格式与只读检查、串行测试和独立XML/PNG证据，不作为发布包。
- 回滚点：out/validation/v2-before-model02/，含25份旧文件，保留HISTORY-02。先备份后续改动，再按上述相对路径逐个执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-model02/<相对路径>' -Destination 'E:/Mini3D/<相对路径>'`，不要覆盖progress.md。五个新增文件InsetFace.h/.cpp、InsetFaceTests.cpp、InsetFaceEditorTests.cpp、docs/v2-inset-face.md分别备份后撤回；保留历史并追加回滚说明，重新CMake/build/test，禁止全仓reset/checkout或恢复过期PowerShell路径。
- 限制：无全局自交检测、无任意多面内插。参数字段Esc会清自身错误，但Inspector/状态栏可能保留最近业务失败文字，沿用既有operationFailed机制，未宣称同步清空。旧UI偶发失活/no-op历史问题仍留QA，不因本项通过宣称根因解决；大网格性能、环切及其后全部需求仍待实施，二期整体未完成。

## 2026-09-12 - Task: MODEL-03 规则四边形带单环切与滑移，完成本组后暂停

### What was done

- 完成单条环切：鼠标命中真实源边，沿相邻四边面对边找到整带；闭合带完整闭合，开放带只在两个真实边界结束。每条被切边共享一个新点，每面拆为两个四边面，不在三角面前截断留下T接。
- 保留原顶点、材质和原面角属性，包含原首角的半面保留FaceId。新点按源EdgeKey、面角按源FaceId分配稳定ID；UV/颜色/归一化硬法线逐面插值，不合并属性接缝。三角面/非四边面、高价极点、重复穿面、方向冲突、退化和ID耗尽整次拒绝。
- 接通Ctrl+R、菜单、F3/Q同一环切入口，Registry合计47项。Edit任意选择域可启动，无须预选组件；Legacy使用菜单/F3/Q。预览阶段左键/Enter进入Slide，右键取消；Slide左键/Enter确认当前比例，右键居中确认，Esc恢复整次before。自动重复Enter不能跳阶段提交。
- 滑移0为中点，数字输入百分数本身；200逻辑像素对应100%，Shift精细连续、Ctrl反转10个百分点步进、显式数字优先。严格拒绝端点及float量化退化，非法文字不能提交旧候选。失活、尺寸/相机/文档/选择变化、文本/IME和视口外点击安全取消。
- 沿用排他组件事务，真源/原选区/代际/dirty/历史在预览期间不变；覆盖层和N侧栏读取候选新边选区，确认才切换新边域并安装一条普通快照历史。Undo/Redo、父变换下后续GRS、保存重开均接通。环切不开放F9，F9仍只支持挤出/内插。
- 在完整验收发现旧对象模态用例失败后补充启动坐标、鼠标起点和生命周期诊断，并按原种子定向/整组复测；没有修改对象变换生产逻辑、延长等待或放宽原断言。原失败未复现，保留证据及QA风险，不把复验通过当作根因修复。
- 同步使用说明、输入/历史契约、兼容性、计划和S07专项状态。按用户最新“完成该组任务后暂停”要求，在MODEL-03本组闭环后暂停，不进入MODEL-04删除/补面；二期整体仍未完成。
- 遵守C++/Qt、Qt命名、UTF-8和PowerShell安全技能；保持单线程、唯一QUndoStack、Y-up、同一GL控件和既有PowerShell修复。保留既有二期改动，未提交、推送、发布或覆盖V1包。

### Testing

- Core首轮三角面拒绝夹具错误地移除了共享边，导致输入先于环切判为无效；读取校验失败后保留共享边(5,6)，改成合法相邻三角面，5用例331断言通过。扩展时一次测试遍历临时拓扑子对象产生悬空引用，改为持有完整局部拓扑对象后6用例392断言通过。保留initial、triangle-fixture、owned-topology证据，不改产品判断以通过错误夹具。
- Editor首轮6通过1失败，原因是合成IME预编辑未结束，下一Ctrl+R被正确保护；补“仍被保护”断言及空QInputMethodEvent结束合成输入后7用例182断言通过。扩展含Enter重复/resize/视口外点击、N侧栏与三阶段截图，最终9用例222断言通过（v2-model03-editor-Debug-1-extended.xml）。共享旧功能先行回归74用例2163断言通过。
- 首次完整验收Debug构建、Core专项通过，但综合Editor在已有“Starting modal discards an unfinished handle preview before taking its snapshot”启动后位置等于0的断言失败：85通过1失败，2427断言通过1失败，种子2057042620，证据v2-model03-editor-Debug-1-final.xml。该证据没有失败时具体坐标，不能证明是撤销失败还是后续事件改变预览。
- 增加诊断后，原种子定向modal-start为1用例10断言通过（modal-diagnosis）；原种子同一综合筛选为86用例2431断言通过（modal-sequence-diagnosis）。检查新增环切未激活时事件过滤返回false、对象启动仍先撤销手柄预览；没有复现原失败，不据此推定其来源。保留QA-01追踪，后续失败时日志可区分起点、坐标与实际事件。
- `& 'E:/Mini3D/out/validation/v2-model03-verify.ps1' -Suffix 'diagnostic-regression'` exit 0。Debug/Release完整build成功，CTest各4/4，耗时85.15s / 29.97s；各配置完整Core89用例13635断言、Assets21用例346断言、GPU3用例106断言、Editor136用例4187断言通过。CTest XML对成功用例输出有截断，另外按配置保留完整LastTest日志副本作为计数依据。
- 两配置环切Core专项各6用例392断言通过；覆盖所有Cube源边、开放面带、正负滑移、完整分边、凹边界合法侧与越界侧、属性接缝、原身份保留/排序、ID耗尽与内插→挤出→环切链路。综合两配置及150%各86用例2431断言，200%86用例2442断言通过，包含环切/内插/F9/历史/组件和对象变换/输入保护等回归。
- 已查看100/150/200%各预览、35%滑移、右键居中确认共9张实际Qt截图；切线、中文HUD和N侧栏正确。200%请求窗口超出屏幕工作区，高度被Windows夹取，测试依真实视口断言，属性使用现有滚动区。未把单屏DPI或合成IME当作跨屏、真实输入法、外部Blender核验。驱动像素同步警告和旧终端诊断编码提示未被改成源码错误或忽略的失败。
- 最终证据：out/validation/v2-model03-build-{Debug,Release}-diagnostic-regression.log、v2-model03-core-{Debug,Release}-diagnostic-regression.xml、v2-model03-ctest-{Debug,Release}-diagnostic-regression.xml/.log、v2-model03-editor-{Debug,Release}-1-diagnostic-regression.xml及Debug-{1.5,2}对应XML/三阶段PNG。首次final失败及前序夹具证据全部保留，脚本拒绝覆盖，复测使用新Suffix。
- 已定向审阅相对本组回滚点的核心接入、事件/菜单、显示选区和文档差异。30个检查对象（含仅备份未修改的TransformInspector.cpp）UTF-8无BOM/LF、局部clang-format、Markdown链接、脚本语法、git diff --check及progress原历史前缀通过；追加最终记录后再次执行只读检查。没有全仓格式化/转码。

### Notes

- src/core/modeling/LoopCut.h：新增面带分析、单切候选和新切线接口。
- src/core/modeling/LoopCut.cpp：实现完整面带、方向传播、分边拆面、属性/稳定ID与离线校验。
- src/core/CMakeLists.txt：登记环切核心源码。
- src/editor/operations/LoopCutSession.h：声明独立预览/滑移两阶段输入会话。
- src/editor/operations/LoopCutSession.cpp：实现源边拾取、HUD、数字/精细/步进、确认及统一安全取消。
- src/editor/SceneViewModel.h：声明不要求预选的环切事务和只读显示选区。
- src/editor/SceneViewModel.cpp：接通before候选、新边选区、排他事务与普通快照历史。
- src/editor/operations/ComponentInteraction.cpp：覆盖层读取候选显示选区，不改变原输入选区。
- src/editor/workbench/WorkbenchShell.cpp：N侧栏同步候选新切线计数和活动项。
- src/editor/operations/KeymapRouter.cpp：Blender Ctrl+R路由到环切。
- src/editor/operations/OperatorRegistry.cpp：登记真实mesh.loop_cut，保持非reopenable/repeatable。
- src/editor/MainWindow.cpp：增加环切菜单、会话与键位取消/状态栏连接。
- src/editor/CMakeLists.txt：登记环切输入会话源码。
- tests/LoopCutTests.cpp：新增6个纯Core拓扑/几何/属性/拒绝/稳定ID用例。
- tests/LoopCutEditorTests.cpp：新增9个VM与真实Qt/GL两阶段、历史/保存、DPI和输入用例。
- tests/ObjectTransformSessionTests.cpp：仅补失败位置附近的事件、坐标和起点诊断，不放宽断言。
- tests/OperatorSearchTests.cpp：真实注册数量更新为47，保留唯一性校验。
- tests/CMakeLists.txt：登记两个环切测试文件。
- README.md：说明单环切已接通，删除/补面和其余二期仍待办。
- docs/v2-loop-cut.md：新增真实操作、面带定义、属性/历史/F9边界、验证证据及回滚说明。
- docs/v2-input-operations.md：同步Ctrl+R、两阶段右键规则与47项Registry。
- docs/v2-edit-mode.md：同步候选新边显示与确认/撤销选区语义。
- docs/v2-contracts.md：明确环切中点也是新拓扑、预览选区隔离和普通历史。
- docs/v2-history.md：明确已实现环切不开放F9。
- docs/v2-inset-face.md：更新Registry数量并链接环切，保留内插F9规则。
- docs/v2-extrude-region.md：更新Registry数量并链接环切，保留挤出F9规则。
- docs/blender-compatibility.md：登记单环切已实现及不支持的面带/多切/端点行为。
- docs/v2-development-plan.md：MODEL-03完成、S07专项通过、QA风险保留，标明按用户要求暂停。
- progress.md：仅追加本组结果、失败与复测、暂停位置、文件清单和回滚方式。
- out/validation/v2-model03-preflight.ps1：保存初始18个旧文件回滚点，未覆盖重跑。
- out/validation/v2-model03-sidebar-backup.ps1：补存N侧栏旧源码。
- out/validation/v2-model03-doc-backup.ps1：补存三个关联说明文档。
- out/validation/v2-model03-modal-backup.ps1：补存对象模态测试加诊断前的原文件。
- out/validation/v2-model03-format.ps1：仅格式化本项旧文件差异及6个新增源码/测试。
- out/validation/v2-model03-core-test.ps1：定向构建和Core筛选，保存不覆盖的日志/XML。
- out/validation/v2-model03-editor-test.ps1：串行Editor/DPI/三截图，增加可选Seed复现并finally还原进程环境。
- out/validation/v2-model03-verify.ps1：串行两配置全build/CTest与高DPI相关回归，首错停止。
- out/validation/v2-model03-check.ps1：只读检查编码、局部格式、链接、脚本与日志前缀。
- 回滚点：out/validation/v2-before-model03/共23份旧文件副本，保留已完成MODEL-02；其中TransformInspector.cpp本组实际未变更。先备份后续修改，再按上述改动旧文件相对路径逐个执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-model03/<相对路径>' -Destination 'E:/Mini3D/<相对路径>'`，不要覆盖progress.md。新增LoopCut.h/.cpp、LoopCutSession.h/.cpp、LoopCutTests.cpp、LoopCutEditorTests.cpp、docs/v2-loop-cut.md分别备份后撤回，保留旧日志并追加回滚说明，重新CMake/build/test。不全仓reset/checkout，不撤回内插成果或恢复过期PowerShell路径。
- 暂停点：本组完成后停止开发与验证进程；只有用户明确恢复才开始原计划MODEL-04删除/补面。保留全部未提交二期成果，不把暂停当作整体完成；无全局自交检测、任意四边网格/大网格性能与跨屏/真实IME仍未验收，旧对象模态偶发失败仍需QA证据定位。

## 2026-09-12 - Task: 参考 Blender 修复旋转后对象世界轴缩放被 TRS 校验拒绝

### What was done

- 修复立方体旋转后部分世界轴不能拉伸的问题，手柄和 S/X/Y/Z 数字入口使用同一缩放规则。保留位置与旋转，世界方向倍率按对象真实世界轴的长度变化和方向符号换算成局部缩放分量；不再强求拉伸矩阵精确分解为 TRS，不烘焙或改写网格。
- 按用户“参考 Blender”的指示核对官方 v4.5.0 Object Resize、对象轴准备与矩阵长度/反射处理源码，独立实现相同对象缩放语义。包含非均匀/镜像父级，保留 Local/等比缩放、既有吸附、手柄不跨零和数字负倍率能力。零、非有限及最终局部缩放绝对值小于 0.001 仍拒绝，世界旋转的严格 TRS 校验保持不变。
- 保留冻结 before 的预览、唯一撤销栈、确认一条历史与取消保留 redo；缩放失败改为倍率/范围/平面提示。补数学、真实 Qt/GL、文件和网格不变回归，更新原先断言“旋转后世界缩放应拒绝”的测试及使用说明。
- 仅处理本缺陷，二期长计划保持 MODEL-03 后暂停，不进入 MODEL-04。遵守 C++/Qt、命名、UTF-8、PowerShell 7.6 安全执行、官方来源核对与交接技能；无协议/依赖/线程/网络处理改动，无全仓格式化、转码、提交、推送或发布。

### Testing

- 先在旧实现稳定复现：`rotated-scale-core-Debug-repro.xml`，种子 2960543913；手柄与数字两个用例均在任意旋转后的世界 X 候选被拒绝处失败（3 断言，1 通过/2 失败）。修复后 shared-scale 为 12 用例/151 断言通过，扩展 edge-cases 为 13 用例/287 断言通过。
- CPU 独立期望覆盖任意旋转 X/Y/Z 拉长与缩短、平面与方向基、负倍率、原负 scale、90° 后世界轴映射、负非均匀父级、位置/旋转不变、重复预览不累积、倍率 1 原样返回及非法/过小结果拒绝；手柄与数字入口一致。保留不可表示世界旋转的拒绝测试。
- Debug/Release 完整构建通过，包含 Mini3DStudio.exe。完整 Core 两配置各 92 用例/13801 断言、Assets 21/346、GPU 3/106 通过。
- 首轮 Debug CTest 为 3/4，88.28 秒：Editor 138 用例中 137 通过/1 失败，4340 断言通过/1 失败。失败是旧 AreaMaximizerTests.cpp:104 的 `REQUIRE_FALSE(model->isModified())`，空场景 Ctrl+Space 最大化恢复后变脏；当时没有选中对象或执行缩放。本轮缩放相关用例均通过。
- 保留该失败和种子 1217477366，检查最大化/dirty/相机通知路径后，原种子定向 `[area-maximize]` 为 5 用例/102 断言通过；再按原种子运行完整 Editor 为 138 用例/4342 断言通过。没有复现原失败，未修改最大化生产代码或测试、未延长等待或放宽断言；记为 QA 待定位风险，不将复测通过当作根因修复。
- Release CTest 4/4，30.39 秒；完整 Editor 138 用例/4342 断言通过。Debug 手柄/对象模态/缩放专项 100%、150% 各 20 用例/578 断言，200% 为 20/592，全部通过。真实 Qt 输入覆盖旋转后三轴拖拽、GPU 帧变化、数字负倍率、全局/局部切换、Esc、Undo/Redo、单次历史、未改写 primitive/EditableMesh 内容和代际、保存重开后的变换与网格。
- 最终证据：`out/validation/rotated-scale-{Debug,Release}-full.xml`、对应 `-build.log`/`-test.log`/`-last-test.log`，`rotated-scale-Debug-{ui-regression,area-diagnosis,full-editor-original-seed,dpi150,dpi200}.xml` 及对应日志。Core 的 repro/shared-scale/edge-cases 证据分别保留，全部使用独立后缀，不覆盖失败结果。
- 对照 Blender 为固定版本官方源码语义核对，没有运行 Blender 二进制。参考 math_matrix_c.cc 直链下载一次超时且不完整，改用 GitHub contents API 获取完整副本后核对；残缺文件未被当作完整依据，未引入下载源码到构建。单屏 DPI/合成事件不等同跨屏或真实输入法验收；既有终端诊断乱码和驱动像素同步性能提示不作为源文件编码变化。
- 11 个 C++ 文件仅按本轮回滚点差异行格式化，16 个正式文件 UTF-8 无 BOM/LF、定向差异及空白、文档本地链接、辅助脚本语法通过；progress 原历史前缀保持完全一致。追加本条后再次只读验证。

### Notes

- src/core/Transform.h：声明共享的保位置/旋转缩放计算接口，不改 Transform 字段。
- src/core/Transform.cpp：实现世界轴长度及符号换算、Local/等比快捷路径和有效性校验。
- src/editor/operations/ObjectTransformMath.h：明确世界缩放与世界旋转的不同处理规则。
- src/editor/operations/ObjectTransformMath.cpp：数字对象缩放委托共享计算，保留旋转的精确分解。
- src/editor/operations/ObjectTransformSession.cpp：区分缩放非法值与其他变换剪切提示。
- src/renderer_gl/GizmoController.cpp：手柄缩放委托共享计算，保留倍率钳制与吸附。
- src/renderer_gl/ViewportWidget.cpp：缩放单独给出范围/平面提示，其他变换保持 TRS 提示。
- tests/GizmoTests.cpp：更新旧拒绝断言，增加三轴、保持旋转、数字一致性和重复/恢复快照测试。
- tests/ObjectTransformMathTests.cpp：补世界轴长度独立期望、父变换/镜像/负倍率/平面/无操作及非法结果回归。
- tests/GizmoEditorTests.cpp：补旋转后三轴真实拖动、GPU 帧、取消和唯一撤销历史用例。
- tests/ObjectTransformSessionTests.cpp：补旋转后三轴数字输入、Local、负倍率、取消/撤销与保存/网格保持用例，并更新旧剪切拒绝预期。
- docs/interaction-optimization.md：标注一期旧缩放限制已被本修复取代，保留阶段验证记录。
- docs/v2-input-operations.md：同步对象缩放规则、数值倍率与旋转拒绝边界。
- docs/v2-component-transform.md：澄清组件直接变形与对象保旋转缩放的区别。
- docs/rotated-object-scaling.md：新增使用示例、Blender 官方依据、实现边界、真实验证与回滚说明。
- progress.md：仅追加本轮修复、验证证据及保留风险。
- out/validation/rotated-scale-{backup,core-test,check,verify,final-check}.ps1：本机定向回滚点、局部格式/编码、串行测试及日志前缀检查工具，不作为发布包；验证脚本支持筛选和原种子，首次失败证据不覆盖。
- out/validation/blender-4.5-scale-reference/：保存官方参考来源副本（含一个保留的残缺下载），仅供本机查证，不编译或发布。
- 回滚点：out/validation/rotated-scale-before-20260912/，原 14 份文件加补存的组件说明共 15 份旧文件。先备份后续修改，再对上述改动旧文件逐个执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/rotated-scale-before-20260912/<相对路径>' -Destination 'E:/Mini3D/<相对路径>'`；不要覆盖 progress.md。新增 docs/rotated-object-scaling.md 另行备份后撤回，保留历史并追加回滚记录，重新构建和测试。不要全仓 reset/checkout 或恢复过期 PowerShell 路径。
- 后续仅需用户重新构建/运行并复验旋转立方体的世界/局部缩放手感；已有最大化偶发 dirty 断言和此前 QA 风险未宣称解决，二期原计划保持暂停。

## 2026-09-12 - Task: 按当前实现编写附真实操作截图的 HTML 使用手册

### What was done

- 交付可离线打开的中文使用手册，共 18 章、21 张真实程序截图。覆盖快速上手、工作台、视角导航、对象变换与旋转后拉伸、场景层级、点边面与穿透框选、组件变换、E 挤出、I 内插、Ctrl+R 单环切、F9、导入/外观/设备、保存和快捷键/故障排查。
- 每项主要操作明确前提、步骤、确认/取消、预期结果与限制；特别区分工作区与 Edit、主键与小键盘、世界与局部单位、环切两阶段右键、预览与已保存几何。不把未完成删除/补面或其他二期规划写成可用功能。
- 从现有 Qt 测试入口重新构建并截取实际窗口，原始图不覆盖；正式图片仅编号、引线、必要裁剪，图注说明状态与裁剪，来源表保留可追溯用例。Legacy 手柄图、内插用例前一条拒绝提示的裁剪范围、样例资源来源均有说明。
- 页面支持章节检索、目录锚点、截图弹窗/原图、键盘关闭与焦点返回、响应式布局和打印样式，无 CDN、远程字体或新增运行依赖。README 增加入口；依据和验证说明归档到 docs。
- 按代码阅读、截图、Web 文档排版、UTF-8、PowerShell 安全执行及交接技能完成。仅做本轮文档，不改生产 C++/配置/测试源码、不改用户偏好、不提交/推送/发布；二期计划仍暂停。

### Testing

- `out/build/opengl-viewport-verify` Release `mini3d_editor_tests` 重新构建成功；取图脚本串行执行 17 个既有用例、497 条断言全部通过。保存 24 张原始 PNG，手册选用 21 张。证据为 `out/validation/user-guide-20260912/build.log`、17 组 XML/日志和 `raw/`。
- 静态校验通过：18 章、21 张图、连续图号、唯一 ID、所有 HTML 锚点和本地图片/参考文件、PNG 尺寸与非空替代文字；HTML、来源 Markdown、README 均 UTF-8 无 BOM/LF，无替换字符。README 相比本轮前备份只增加一行入口。
- Edge 152.0.4191.66 本地 file 页面实测：1440/1024/768/390/320 五种宽度无横向溢出；21 图加载与弹窗、18 目录跳转、检索命中/无结果/复位、Enter 打开、Esc/按钮关闭与焦点归还通过；390/320 弹窗补测通过，无页面脚本错误、失败资源或远程请求。
- 打印按钮调用和 print CSS 检查通过，导航/工具按钮隐藏、正文侧边距归零；未生成正式 PDF，不将其声明为逐页印刷验收。浏览器验证入口 `out/validation/user-guide-check.py`，最终 `qa-02/report.json` 与桌面/移动截图。
- 实际查看原始联系表、正式标注图及浏览器桌面/手机截图。首轮布局截图在图片懒加载解码前取得，出现灰底，补充解码与绘制等待后复验图像正常；移动部分引线避免遮挡提示。QA 脚本编辑曾有一行缩进错误，执行前中止，读错误定位后修正并复验通过，未放宽断言或改动生产功能。
- 本轮仅文档验证与现有截图用例，不替代全套 C++ 回归、真实中文输入法、跨屏/高 DPI、二期人工功能验收。旧最大化偶发 dirty 风险保持原记录，不在此任务内处理。
- 追加本记录后检查历史字节前缀、定向差异、正式文件编码、图片/文档落点；历史进度不重写。

### Notes

- docs/Mini3D_使用手册.html：新增 18 章离线操作手册和原生阅读交互。
- docs/user-guide-sources.md：记录内容基线、逐图来源、截图条件、验证与维护边界。
- README.md：文档列表新增使用手册入口，保留此前二期改动。
- progress.md：只在末尾追加本轮结果、验证与回滚边界。
- docs/images/user-guide/overview.png：标注完整工作台七个主要区域。
- docs/images/user-guide/workbench.png：标注窄窗口 N 侧栏和属性滚动条。
- docs/images/user-guide/object-move.png：标注 G X 2 对象预览和同步属性。
- docs/images/user-guide/rotate.png：标注 Legacy 旋转工具和圆环。
- docs/images/user-guide/scale.png：标注 Legacy 缩放工具、中心和轴端点。
- docs/images/user-guide/tree.png：标注场景搜索、父子关系和父对象字段。
- docs/images/user-guide/edit.png：标注 Edit 面选区、计数及对象变换锁定。
- docs/images/user-guide/box.png：标注 B 框选拖动中的提示与矩形。
- docs/images/user-guide/component-move.png：标注组件 G Z 1 未确认预览。
- docs/images/user-guide/extrude.png：标注 E 法线挤出 0.5 的预览状态。
- docs/images/user-guide/inset.png：裁取内插几何与 F9 厚度 0.3 参数区并标注。
- docs/images/user-guide/loop-preview.png：标注环切第一阶段的面带预览。
- docs/images/user-guide/loop-slide.png：标注环切第二阶段的 35% 滑移预览。
- docs/images/user-guide/loop-center.png：标注右键居中确认后的切线和边计数。
- docs/images/user-guide/history.png：标注挤出确认后的斜视结果与 F9 世界位移。
- docs/images/user-guide/import.png：标注真实 glTF 样例、层级与外观页入口。
- docs/images/user-guide/camera.png：标注实体相机只读预览及参数/返回入口。
- docs/images/user-guide/dialogs-dialog-SaveSceneAs.png：标注中文保存框文件名与保存按钮。
- docs/images/user-guide/dialogs-unsaved.png：保留原始未保存提示小图，无编号叠加。
- docs/images/user-guide/operators-search.png：标注 F3 搜索、结果与操作说明。
- docs/images/user-guide/operators-favorites.png：标注 Q 收藏列表及管理说明。
- out/validation/user-guide-capture.ps1：本机取图、前稿备份、串行现有测试与不覆盖证据脚本。
- out/validation/user-guide-images.py：本机 Pillow 联系表、编号引线和裁剪脚本，不改原始图。
- out/validation/user-guide-check.py：本机结构/编码/资源及 Edge 响应式交互验证脚本。
- out/validation/user-guide-20260912/：本机前稿、原始截图、测试日志/XML、联系表和 QA 截图/报告，不作为正式发布包。
- 回滚点：`out/validation/user-guide-20260912/before/` 保存本轮前 README 与 progress。仅撤回上述新增 HTML、来源文档和图片目录；README 若无后续改动，可执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/user-guide-20260912/before/README.md' -Destination 'E:/Mini3D/README.md'`，否则只移除本轮新增的一行入口。不要覆盖 progress.md，应保留历史并追加回滚说明；新增文件先移到已校验位于工作区的备份目录再撤回，不全仓 reset/checkout，不回退二期或 PowerShell 修复。
- 下一步只需用户用浏览器打开 HTML，按需要阅读/试用；不自动恢复开发或开始新需求。

## 2026-09-16 - Task: 恢复二期开发并完成 MODEL-04 组件删除与单环补面

### What was done

- 按用户“现在开始恢复开发”从 MODEL-03 暂停点继续，完成 MODEL-04 的实现、验证和使用说明。本轮仅覆盖删除/补面，不开始游标、枢轴、Mirror 或其他后续项目。
- 编辑模式新增 X/Delete 删除菜单，明确区分点、边及面删除。删除点/边连带移除关联面，删除面保留其他面仍使用的边界，统一清理孤立顶点；不冒充溶解或保留散边。删空后保留空可编辑对象，能保存重开。
- 点/边域新增 F 单环共面补面，支持简单凹环，按邻面反向确定绕序；非共面、自交、多环、内部边、已有重叠面、无效 ID 或 ID 耗尽整次拒绝。旧属性不变，新面用对象默认材质、白色顶点色、计算法线与平面 UV，不宣称恢复被删面的属性。
- 候选校验后才安装，沿用唯一历史与组件事务；Undo/Redo 恢复几何、保存点、原选择域和选区，补面成功选中新面。F3/Q 共用真实入口，Registry 从47增至52项；菜单冻结上下文，防止弹出后目标变化造成误操作。删除/补面不开放 F9；对象 Delete、Legacy F 聚焦和模态 X 轴约束保持原语义。
- HTML 使用手册增至19章25张真实截图，补齐删除与补面的操作步骤和边界；同步 README、计划、编辑模式、输入与 Blender 兼容说明。
- 按 C++/Qt、Qt 命名、UTF-8、PowerShell 安全执行与7.6技能实施；Web 技能使手册沿用原排版并补响应式/交互验证。未改场景协议、线程、依赖和 PowerShell 定位修复；未提交、推送、打包或发布。内置 Browser 无可调用接口，使用已有本机隔离 Edge 验证，不读取用户浏览器资料。

### Testing

- 最终 `out/build/opengl-viewport-verify` Debug/Release 完整构建成功，CTest 各4/4，分别90.54s / 30.86s。证据：`out/validation/v2-model04-{Debug,Release}-1-final-build.log`、`-ctest.xml`。Release 完整测试为Core 97用例14227断言、Assets 21用例346断言、GPU 3用例106断言、Editor 145用例4524断言。
- 两配置新Core专项各5用例426断言、新Editor专项各7用例176断言全部通过；证据同前缀的`-core.xml`、`-editor.xml`。覆盖删除三域/选择投影/孤立点/空网格、Cube六方向补洞绕序、凹环/尺度/输入重排、属性身份、非法输入原子拒绝、保存重开、保存点、一次Undo/Redo、补面后挤出、取消旧预览、F3/Q、菜单过期/文本/合成IME/Legacy/重复键及对象删除与模态X互斥。
- GPU窗口检查删除/补面前后帧缓冲确有变化，完整Undo恢复原帧，GL Context身份不变；空网格仍可绘制而不删除对象。截图来自真实Qt菜单及窗口，不使用效果图代替功能。
- Release 150%/200%相关模式、入口与新功能回归各26用例607断言通过，Core再次各426断言通过。证据：`out/validation/v2-model04-Release-{1.5,2}-dpi-*`。已查看菜单、洞口/新面及高DPI截图，200%时属性区仍依赖原滚动条，不宣称跨屏验收。
- 手册最终静态与浏览器检查通过：19章、25图、连续图号、唯一ID、本地锚点/资源、尺寸/替代文字、四张新增图与Release原图SHA-256一致；Edge153.0.4234.32在1440/1024/768/390/320五种宽度无横向溢出，目录检索/跳转、所有图的弹窗/Esc/焦点归还、打印导航隐藏、无脚本错误及远程请求通过。证据：`out/validation/v2-model04-guide-qa04/report.json`和页面截图，已实际查看。
- 首次编译发现QWidget::mapToGlobal的花括号实参重载歧义，明确QPoint后构建通过。首次Editor专项有一处测试夹具仍选面2，却按面1洞口选边；恢复预期面1后复验通过，未放宽补面拒绝规则。旧原始失败证据保留在`-initial-*`。HTML菜单图高度初填136，与真实120不符，静态检查检出后修正；超长元素截图带入固定辅助链接，改用真实视口截图，并限制小菜单图显示宽度，未改无障碍导航。
- 本轮阅读命令的Select-Object多范围数组曾报类型错误，后续改用单段/完整读取；补丁中一次旧断言定位不匹配，确认无部分写入后按实际行修改。备份检测最初使用可忽略字符的字符串比较报告了错误BOM状态，改为原始字节检查；前稿和结果实际均UTF-8无BOM/LF，未进行转码。
- `out/validation/v2-model04-check.ps1`通过定向差异、UTF-8无BOM/LF、替换字符、新源码clang-format与进度历史字节前缀检查；修改仅针对本项差异/新增文件，没有全仓格式化。追加本日志后再次检查历史前缀。
- 以上不代替真实中文输入法、跨屏、全局自交、性能与最终S01–S12验收；既有QA偶发失活/对象模态/dirty问题未在本项修复，不因本轮全量通过而删除原风险记录。

### Notes

- src/core/modeling/DeleteComponents.h：新增三个分域删除候选接口与明确语义。
- src/core/modeling/DeleteComponents.cpp：按关联面删除、清理孤立点并验证候选。
- src/core/modeling/FillFace.h：新增边界点/边补面接口及新面ID结果。
- src/core/modeling/FillFace.cpp：构建单环、共面/绕序/身份/属性和派生校验。
- src/core/CMakeLists.txt：将删除与补面模块加入纯Core构建。
- src/editor/SceneViewModel.h：公开删除/补面意图与禁用原因查询。
- src/editor/SceneViewModel.cpp：接入候选安装、原子失败、选择与唯一历史。
- src/editor/MainWindow.cpp：新增补面和分域删除菜单，冻结弹窗上下文。
- src/editor/operations/OperatorRegistry.cpp：注册5个真实入口并补前提说明。
- src/editor/operations/KeymapRouter.cpp：Edit Delete/X/F分发及长按抑制，保留模式差异。
- tests/FillDeleteTests.cpp：新增纯CPU几何/拓扑/身份/拒绝专项。
- tests/FillDeleteEditorTests.cpp：新增历史、选择、文件、真实输入、GL与截图专项。
- tests/OperatorSearchTests.cpp：真实Registry计数更新为52。
- tests/CMakeLists.txt：接入两份新测试。
- README.md：更新当前能力与Delete的模式区别。
- docs/v2-fill-delete.md：新增操作语义、限制、历史、验证和回滚说明。
- docs/v2-development-plan.md：记录恢复开发，将MODEL-04标为完成并列验证证据。
- docs/v2-input-operations.md：更新删除/补面键位、入口与Registry计数。
- docs/v2-edit-mode.md：更新编辑模式操作表、选择恢复和当前能力边界。
- docs/blender-compatibility.md：记录已实现删除/补面及与Blender散边/溶解的差异。
- docs/Mini3D_使用手册.html：新增第19章及4图，同步旧边界文案，保留原阅读交互。
- docs/user-guide-sources.md：记录新版内容基线、四图来源、验证及原稿回滚边界。
- docs/images/user-guide/delete-menu.png：新增真实删除菜单截图。
- docs/images/user-guide/delete-hole.png：新增删除面后的洞口截图。
- docs/images/user-guide/fill-boundary.png：新增完整边界选区截图。
- docs/images/user-guide/fill-result.png：新增F补面后新面选区截图。
- progress.md：只在末尾追加本项事实、验证、文件清单与回滚点。
- out/validation/v2-model04-backup.ps1：本机定向前稿备份与编码预检脚本。
- out/validation/v2-model04-format.ps1：本机仅对本项差异和新增源码进行格式化。
- out/validation/v2-model04-verify.ps1：本机串行构建、Core/Editor、完整CTest与DPI证据脚本。
- out/validation/v2-model04-guide-check.py：本机离线HTML/图片与真实浏览器验证脚本。
- out/validation/v2-model04-check.ps1：本机定向编码、差异和历史前缀检查脚本。
- out/validation/v2-before-model04-20260915/：保存本项开始时16个涉及文件的真实当前版本，包含之前未提交二期成果；`out/validation/v2-model04-*`为各轮不覆盖的日志、XML和PNG，不进入正式发布包。
- 回滚点为上述前稿目录。先保存后续修改，按本清单定向恢复已有文件，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-model04-20260915/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`；新增两组Core文件、两份测试、专项文档和四图另行备份后撤回。不要覆盖progress.md，保留历史并追加回滚记录；重新CMake/build/test，不全仓reset/checkout，不回退PowerShell或世界缩放修复。
- 本组已闭环，下一最小动作是在当前源码构建中按手册第19章试用“删一面→选四边→F→Undo”。二期剩余游标/枢轴/吸附/Mirror等未标完成，不在本项自动施工。

## 2026-09-17 - Task: 从暂停点恢复并完成 CURSOR-01 3D 游标

### What was done

- 按用户“继续开发任务”恢复本项，完成表面放置、精确坐标、选择中心、定点创建、保存与使用说明闭环；不自动进入 PIVOT-01、SNAP 或 Mirror。
- 默认 Blender 键位支持 Shift＋右键单次定位；两套键位均可由菜单、N 面板或 F3 启动一次左键放置，真实动作可加入 Q 收藏。最近可见三角表面优先，无命中回落 XZ 地面；近平行、裁剪外或非法坐标拒绝并保留原位置，不用包围盒冒充表面。
- N 侧栏新增世界 XYZ、到选择/原点及显示按钮。对象取世界原点，组件取去重顶点的世界质心；输入提交/取消/非法保留与原数值控件一致。游标是辅助状态，不是场景节点、不改变选择、不标脏、不入唯一历史；下次主动保存才落盘。
- 立方体、球体、平面、空对象在游标位置创建，Undo/Redo 使用创建时的位置；相机、方向光及导入行为不变。菜单等待与变换/框选/环切互斥，失焦、切文档/选区/视角等取消，保留正式多键输入保护。
- 复用现有 GL 覆盖层绘制固定逻辑像素的红白圆环和十字，独立资源随有效 Context 初始化/销毁；关闭自身显示/总覆盖层、相机预览或视锥外隐藏。真实帧缓冲验证显示变化与关闭后恢复，未采用无效的 QPainter 混绘方案。
- 在既有格式3授权内新增可选 editorState.cursor3D，读旧格式/缺字段使用原点可见，非法字段原子拒绝；只做本项最小状态，不声称 FILE-01 完成。手册增至20章27图，并修正旧“N侧栏全部只读”说明。
- 使用 C++/Qt、Qt命名、UTF-8、PowerShell安全及7.6技能；Web规范使手册沿用旧版样式并验证响应式/键盘交互，交接技能用于整理结论与后续边界。没有新增依赖、线程或网络逻辑，没有提交、推送、打包或发布；保留之前未提交成果及PowerShell修复。

### Testing

- `out/build/opengl-viewport-verify` Debug/Release完整构建成功，CTest各4/4，分别97.24s / 36.05s。最终证据为 `out/validation/v2-cursor01-Debug-1-resume-isolation-*`、`v2-cursor01-Release-1-final-*` 的build日志及ctest.xml。Release完整回归：Core98用例14257断言、Assets23用例376断言、GPU3用例106断言、Editor150用例4694断言。
- 两配置序列化专项各1用例30断言、拾取专项各2用例30断言通过；覆盖旧版/缺省/新字段往返、非有限数/类型错误原子拒绝、真实最近表面、父变换/负尺度/隐藏、球体非AABB、透视/正交地面和近平行拒绝。
- Release游标专项5用例165断言通过；Debug数值＋游标隔离复验8用例211断言通过。覆盖不脏/历史隔离、四类对象创建及重做、保存重开、世界选择中心、N非法/合成IME/相机预览、Blender/Legacy、F3/Registry、选区不变、模态取消与真实GL帧缓冲/Context。
- Release150%/200%游标、数值、工作台、搜索和框选相关回归各21用例590断言通过，证据 `v2-cursor01-Release-{1.5,2}-dpi-*`；同期Core/Assets专项再次通过。已查看100%两张实际截图及200%表面/侧栏图，侧栏和属性区按需滚动，不宣称跨屏实测。
- 本项首轮测试曾因编译期间修改测试控件名留下旧对象文件，实际对象字节仍有旧控件名，定位后串行重新编译解决；后续遵守编辑→构建→测试顺序。原QPainter绘制的游标在已验证视野/显示前提下不改变帧缓冲，补事件等待仍失败，转用现有GL覆盖层后真实像素验证通过。
- 上次Debug完整回归149/150编辑器用例通过，游标Shift＋右键未触发。缩小到数值＋游标组合后确认：NumericFieldTests模拟失活后未配对释放合成左键，Qt Test保留按钮状态为1，后续右键成为多键组合。仅补测试mouseRelease，不放宽生产条件；相同组合与本轮完整两配置/DPI通过。原失败/探针证据保留，其他历史QA偶发失活或模态坐标问题不视为本项已修复。
- 手册检查 `out/validation/v2-cursor01-guide-check.py --output out/validation/v2-cursor01-guide-qa01` 通过：UTF-8无BOM/LF、20章27图、唯一ID/锚点/离线链接、图号/尺寸/替代文字、两图与Release原图SHA-256一致，格式3真实保存样本及cursor3D错误类型/缺项反例通过schema验证。
- 隔离Edge153.0.4234.32在1440/1024/768/390/320五种宽度通过目录过滤/跳转、27图弹窗/Esc/焦点归还、无横向溢出、打印导航隐藏、无脚本错误/远程请求；实际查看桌面/手机页面截图。内置Browser无可调用接口，未读取用户浏览器资料；不将打印CSS检查说成完整PDF分页验收。
- `out/validation/v2-cursor01-check.ps1`通过定向差异、严格UTF-8无BOM/LF、无替换字符、新源码clang-format、图片哈希、测试XML/页面报告与progress历史字节前缀检查；日志追加后再次运行。没有全仓格式化/转码。上述验证不代替实机输入法、跨屏、最终S01–S12、性能或二期整体验收。

### Notes

- src/core/SceneSerializer.h：新增有限坐标的Cursor3D及文档末尾辅助状态。
- src/core/SceneSerializer.cpp：格式3读写最小游标字段，旧版缺省及非法值拒绝。
- src/assets/SceneDocument.h：传递加载游标，保存接口末尾增加兼容默认参数。
- src/assets/SceneDocument.cpp：原子加载/保存中接入游标校验和状态。
- src/editor/SceneViewModel.h：声明游标查询、定位、选择中心及变化信号。
- src/editor/SceneViewModel.cpp：接入不脏/历史隔离、创建位置、新建复位及保存恢复。
- src/editor/MainWindow.cpp：装配定位、菜单与旧模态互斥、键位同步和取消路径。
- src/editor/operations/ComponentPicker.h：新增定位结果与查询接口。
- src/editor/operations/ComponentPicker.cpp：复用真实三角命中、可见性/材质及地面拒绝规则。
- src/editor/operations/OperatorRegistry.cpp：新增四个游标真实动作，Registry总数56。
- src/editor/workbench/WorkbenchShell.h：声明游标面板控件和动作状态同步。
- src/editor/workbench/WorkbenchShell.cpp：N滚动侧栏XYZ及共享动作，保持选择信息只读。
- src/renderer_gl/ViewportWidget.h：新增游标镜像、放置状态/请求及独立覆盖层资源。
- src/renderer_gl/ViewportWidget.cpp：单次定位输入、模态取消及固定逻辑像素GL游标绘制。
- tests/CMakeLists.txt：接入新游标编辑器用例。
- tests/SceneSerializerTests.cpp：增加辅助状态序列化/兼容/非法原子拒绝专项。
- tests/ComponentPickerTests.cpp：增加真实表面及XZ地面定位专项。
- tests/OperatorSearchTests.cpp：更新真实Registry计数为56。
- tests/NumericFieldTests.cpp：补失活后合成左键配对释放，消除跨用例鼠标状态残留。
- tests/CursorEditorTests.cpp：新增游标状态、历史、输入、真实GL与可选截图用例。
- README.md：更新当前游标能力、基础体创建位置和保存边界。
- docs/v2-cursor.md：新增操作、限制、保存语义及验证结果说明。
- docs/v2-development-plan.md：记录本轮恢复及CURSOR-01完成，后续任务仍待办。
- docs/v2-input-operations.md：补游标键位/模态优先级与56项Registry计数。
- docs/v2-scene-format.md：说明可选editorState.cursor3D和旧文件默认行为。
- docs/scene-format-v3.schema.json：新增游标最小结构，保留旧格式3缺字段可读。
- docs/Mini3D_使用手册.html：增加第20章、两图和操作/保存说明，纠正旧只读表述。
- docs/user-guide-sources.md：记录新截图来源、字节一致性、浏览器检查和回滚边界。
- docs/images/user-guide/cursor-surface.png：新增真实表面定位窗口图。
- docs/images/user-guide/cursor-numeric.png：新增精确坐标创建窗口图。
- progress.md：仅末尾追加本项结果、真实验证、文件清单与回滚点。
- out/validation/v2-cursor01-backup.ps1：本机定向前稿备份和编码预检。
- out/validation/v2-cursor01-format.ps1：本机仅格式化本项差异及新测试。
- out/validation/v2-cursor01-verify.ps1：本机串行构建、专项/完整/DPI及截图证据入口。
- out/validation/v2-cursor01-build-probe.ps1：本机只读检查旧对象文件控件名的诊断探针。
- out/validation/v2-cursor01-guide-check.py：本机手册、图片、schema与隔离浏览器检查。
- out/validation/v2-cursor01-deliver-images.ps1：本机原图字节复制与哈希确认，不覆盖已有图。
- out/validation/v2-cursor01-check.ps1：本机编码/差异/格式/历史/截图及最终证据检查。
- 回滚点：`out/validation/v2-before-cursor01-20260916/`保存本项前26个已有文件的真实dirty版本，恢复时另补的NumericFieldTests原稿也在同路径，共27份；`v2-cursor01-resume-20260917/tests/`保留本次恢复前两份测试。各轮`v2-cursor01-*`日志/XML/PNG为本机证据，不是发布包。
- 可执行回滚示例（先保存后续改动）：`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-cursor01-20260916/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`。按上列清单逐个恢复本项已有文件；新增CursorEditorTests、v2-cursor及两张游标图另行备份后撤回。不要覆盖progress历史，不动前25图、用户原方案、PowerShell修复或其他未提交成果；回滚追加记录并重新构建/测试，不全仓reset/checkout。
- 下一最小动作是用户在当前源码构建中按手册第20章试用“Shift＋右键→N输入XYZ→创建→Ctrl+S”。本轮不自动开始游标枢轴或选择到游标，未标记二期整体完成。

## 2026-09-17 - Task: 完成 PIVOT-01 变换枢轴与保持偏移定位

### What was done

- 按用户“继续下一步开发”完成 PIVOT-01；对象及编辑模式支持选择质心、活动元素、3D游标三种枢轴，与全局/局部方向独立。组件质心按去重源顶点计算，活动项沿用稳定回退规则，R/S开始冻结世界枢轴。
- 对象旋转/缩放手柄的绘制、拾取和实际变换统一使用所选枢轴；绕游标操作同时改变对象大小/姿态及原点位置，父变换下换算回局部位置。保留现有TRS规则和非法网格拒绝，不改变G/E/I算法，不新增组件手柄。
- 接入头部、编辑菜单、F3及Q共享动作，新增“选择到游标（保持偏移）”：对象世界原点或组件去重质心移到游标，组件统一平移而不坍缩。几何确认只产生一条历史，取消无历史，保存重开保留结果。
- 枢轴模式为窗口偏好，不标脏、不入历史、不写工程；同窗口新建/打开保留，新窗口默认质心。切换枢轴或游标取消未确认变换；无有效选区、隐藏、只读预览等上下文禁止相关操作。
- 更新操作说明及计划，手册增至21章29图，新增两张真实窗口截图。沿用Web技能要求的既有布局并验证响应式与键盘交互；使用C++/Qt、Qt命名、UTF-8、PowerShell安全及7.6规范，保留无BOM/LF，不做全仓格式化/转码。
- 本轮不自动开始SNAP-01、Mirror或其他任务；未修改核心文件格式、依赖、线程、网络或PowerShell定位，没有提交、推送、打包或发布。

### Testing

- `out/build/opengl-viewport-verify` Debug/Release完整构建通过，CTest各4/4，分别100.79s / 35.11s；证据为 `out/validation/v2-pivot01-{Debug,Release}-1-final-*` 的build日志和ctest.xml。Release完整回归为Core99用例14279断言、Assets23用例376断言、GPU3用例106断言、Editor155用例5000断言。
- 两配置CPU相关14用例309断言、编辑器相关34用例1384断言通过；覆盖三枢轴×全局/局部×旋转/缩放的独立矩阵期望、点边面质心与活动项、父旋转/负非均匀尺度、保持偏移、撤销重做、取消、保存、共享动作和真实GL手柄/帧缓冲/Context。JUnit展开SECTION后有39条编辑器记录，不等于39个TEST_CASE；检查脚本已明确输出JUnit entries。
- Release150%相关34用例1384断言、200%相关34用例1386断言通过；证据 `v2-pivot01-Release-{1.5,2}-dpi-*`。实际查看两张初始窗口图、Release组件图和200%对象图，侧栏/属性区按需滚动，不宣称跨屏实测。
- 首轮新编辑器5用例有1失败；增加诊断后确认“游标枢轴＋局部R Z30＋部分面选区”使既有面投影交叉/退化，候选被网格校验拒绝，而非枢轴矩阵错误。未放宽生产校验：合法矩阵组合改用全选网格，原部分选区夹具保留为拒绝/Enter不可提交/Esc恢复测试；最终两配置及DPI均通过，initial/diagnose证据保留。
- 初次多文件补丁因测试计数定位名不符原子失败，检查无部分写入后修正定位。收尾曾尝试读取不存在的ctest.log，目录证据表明实际产物为ctest.xml，改读真实XML确认结果；未将缺文件视为测试失败或重跑构建。
- `out/validation/v2-pivot01-guide-check.py --output out/validation/v2-pivot01-guide-qa01`通过：21章29图、UTF-8无BOM/LF、唯一ID/锚点/本地链接、图号/尺寸/alt与两张原图SHA-256一致。隔离Edge153.0.4234.32通过1440/1024/768/390/320宽度、目录过滤/空结果/清空/第21章跳转、29图弹窗/Esc/焦点归还、无横向溢出、打印导航隐藏、无脚本错误或远程请求；已查看桌面和手机截图。内置浏览器接口不可调用，未读取用户浏览器资料。
- `out/validation/v2-pivot01-check.ps1`检查定向UTF-8无BOM/LF、差异空白、新测试clang-format、历史字节前缀、截图哈希、测试XML及浏览器报告；日志追加后再次执行。上述自动化与合成输入不代替实机输入法、跨屏、最终S01–S12或二期整体验收，历史QA偶发问题不宣称本项已修复。

### Notes

- src/editor/SceneViewModel.h：声明三种枢轴、位置查询、变化信号及选择到游标接口。
- src/editor/SceneViewModel.cpp：计算世界枢轴、维护窗口偏好并接入保持偏移的唯一历史事务。
- src/editor/operations/ObjectTransformSession.h：记录模态开始时的枢轴名称与状态。
- src/editor/operations/ObjectTransformSession.cpp：统一围绕冻结枢轴的对象/组件R/S及HUD提示。
- src/editor/operations/OperatorRegistry.cpp：新增三种枢轴与选择到游标真实动作，Registry共60项。
- src/editor/workbench/WorkbenchShell.h：声明枢轴菜单、共享动作及同步接口。
- src/editor/workbench/WorkbenchShell.cpp：接入头部/编辑菜单、N面板与动作状态同步。
- src/renderer_gl/GizmoController.cpp：外部枢轴下的对象旋转/缩放同时更新原点位置。
- src/renderer_gl/Renderer.h：渲染和手柄查询末尾增加兼容默认的世界枢轴参数。
- src/renderer_gl/Renderer.cpp：统一手柄绘制与拾取的世界枢轴。
- src/renderer_gl/ViewportWidget.h：声明视口只读枢轴镜像。
- src/renderer_gl/ViewportWidget.cpp：同步枢轴、取消旧拖动并统一绘制/悬停/拾取。
- tests/GizmoTests.cpp：新增父变换下外部枢轴旋转/缩放数值验证。
- tests/CMakeLists.txt：编入新枢轴编辑器测试。
- tests/OperatorSearchTests.cpp：更新真实动作总数为60。
- tests/CursorEditorTests.cpp：更新游标搜索结果及选择到游标历史属性断言。
- tests/PivotEditorTests.cpp：新增数学、状态、历史、UI、真实GL与可选截图用例。
- README.md：补充当前枢轴与保持偏移定位能力。
- docs/v2-pivot.md：新增操作、生命周期、边界与实际验证说明。
- docs/v2-development-plan.md：将PIVOT-01标为完成，后续SNAP仍未开始。
- docs/v2-input-operations.md：更新枢轴/定位语义及60项真实动作。
- docs/v2-component-transform.md：更新组件R/S所用枢轴说明。
- docs/v2-cursor.md：标明本轮已接入枢轴和选择到游标，保留历史验证记录。
- docs/Mini3D_使用手册.html：新增第21章与两图，更新旧能力及保存边界说明。
- docs/user-guide-sources.md：追加截图来源、字节一致性、页面验证与回滚边界。
- docs/images/user-guide/pivot-object.png：新增真实对象游标枢轴手柄截图。
- docs/images/user-guide/pivot-components.png：新增真实组件活动元素枢轴预览截图。
- progress.md：仅末尾追加本轮成果、验证、清单及回滚点。
- out/validation/v2-pivot01-backup.ps1：本机定向备份本项前稿，已执行，不重复覆盖。
- out/validation/v2-pivot01-format.ps1：本机仅格式化本项C++差异及新测试。
- out/validation/v2-pivot01-verify.ps1：本机串行构建、专项/完整/DPI测试与截图入口。
- out/validation/v2-pivot01-guide-check.py：本机手册静态及隔离Edge交互验证。
- out/validation/v2-pivot01-deliver-images.ps1：本机原图复制及哈希检查，不覆盖已有图。
- out/validation/v2-pivot01-check.ps1：本机编码/差异/历史/格式/截图及最终证据检查。
- 回滚点：`out/validation/v2-before-pivot01-20260917/`保存本项前24个已有文件的真实dirty版本；各轮`v2-pivot01-*`为本机证据，不是发布包。不要使用CURSOR前稿回滚本项，以免丢失上一项成果。
- 可执行回滚示例（先保存后续改动）：`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-pivot01-20260917/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`。按上列清单逐个恢复已有文件；新增PivotEditorTests、v2-pivot及两图另行备份后撤回。不覆盖progress历史，不动前27图、已有游标功能、用户原方案、PowerShell修复或其他二期dirty；追加回滚记录后重新构建/测试，不全仓reset/checkout。
- 下一最小动作：按手册第21章切换“3D游标”枢轴，体验R/S及“选择到游标（保持偏移）”。本轮到此交付，SNAP-01未开始，不标记二期整体完成。

## 2026-09-19 - Task: 完成 SNAP-01 移动步进与顶点吸附

### What was done

- 按用户“现在开始下一项吸附功能的开发”仅实施SNAP-01，不扩展到VIS-01或Mirror。复用现有步进，新增G移动的顶点模式、窗口级类型偏好、头部下拉/视图菜单及F3/Q共享动作，Registry总数63。
- 明确源锚点为对象世界原点或组件去重质心，开始操作时冻结；组件统一平移保持偏移。顶点模式使用10逻辑像素屏距、真实三角遮挡及稳定身份排序，拒绝隐藏/裁剪外/被遮挡目标，不用包围盒角点替代顶点。
- 对象移动排除移动子树的候选及遮挡，防止预览反馈自遮挡闪烁；组件只排除所选源顶点，允许同网格未选点。组件候选/遮挡使用已确认真源几何，不由预览反向改变候选；此边界已写入说明。
- 命中显示青绿色目标框与HUD身份；无命中回到同一开始快照的自由移动，取消清除反馈。数字优先，轴/平面投影优先，保持唯一历史、Undo/Redo、父级换算与原文件保存；非法退化候选仍拒绝确认，不自动焊接。
- Ctrl按下/释放临时反转G吸附，不修改持久勾选；对象手柄的步进从“开关或Ctrl”统一为异或。顶点模式限G对象/组件移动，手柄及R/S/E/I沿用既有步进，实施前已向用户说明，不承诺完整Blender吸附选项。
- 更新README、输入说明和二期计划，新增吸附增量使用文档。HTML手册及原截图仍截至PIVOT-01，本项不伪造图文更新；真实自动化窗口截图保留在本机验证目录。
- 遵循C++/Qt、Qt命名、UTF-8及PowerShell7.6安全技能；原文件保持UTF-8无BOM/LF，仅格式化本项C++差异和新测试。交接技能用于明确可用范围、验证与后续边界。未改协议、依赖、线程、网络、用户原方案或PowerShell定位，未提交、推送、打包或发布。

### Testing

- `out/build/opengl-viewport-verify` Debug/Release完整构建成功，CTest各4/4，分别105.51s / 32.69s；证据前缀 `out/validation/v2-snap01-Debug-1-expanded-fixed-*` 与 `v2-snap01-Release-1-final-*`，含build日志、专项XML及ctest.xml。
- Release全量：Core99用例14279断言、Assets25用例410断言、GPU3用例106断言、Editor162用例5172断言，全部通过。Debug全量同样4/4；不从截断的Debug控制台输出推造逐项断言数。
- 两配置拾取相关13用例209断言、编辑器相关36用例1391断言通过。新增2个拾取用例及7个编辑器用例；覆盖阈值、源/子树排除、同网格未选目标、背面/隐藏拒绝、父负尺度、透视/正交、稳定平局、连续预览、Ctrl双向反转、数值优先、全局/局部轴/平面、视线正对轴、唯一历史、失焦、模式切换、退化拒绝与保存重开。
- Release150%同组36用例1391断言、200%36用例1393断言通过；每次拾取13用例209断言同步通过。证据 `v2-snap01-Release-{1.5,2}-dpi-*`。Qt真实帧缓冲验证目标框显示与清除改变像素、Context保持；已查看Debug100%和Release200%实际窗口图，200%属性区正常滚动，不宣称跨屏实测。
- 首轮构建、拾取13用例209断言及初始编辑器4用例110断言通过。扩展父变换测试时误用SelectionModel不存在的select方法，编译器C2039定位到测试行；依据头文件改为setSelectedEntity，串行重建及扩展/全量/DPI通过。原initial/expanded失败日志保留；生产算法未为通过测试放宽网格校验。
- 本项首次目录级差异查看将未备份的旧文件列作新增，仅为只读比较噪声，未据此修改文件；最终检查改为按18份前稿逐文件比较，避免把其他二期成果算入本项。
- `out/validation/v2-snap01-check.ps1 -Final`用于最终严格UTF-8无BOM/LF、差异空白、新测试clang-format、progress历史字节前缀及四组XML证据校验；追加日志后执行。测试中的非法glTF导入错误为既有拒绝夹具，GL像素传输性能警告不是断言失败。
- 大模型CPU遮挡性能预算、真实输入法、跨屏、S01–S12最终联调及独立发布包仍未验收；本项不宣称修复历史QA偶发失活问题，也不标记二期整体完成。

### Notes

- src/editor/SceneViewModel.h：声明窗口级吸附类型与变化信号。
- src/editor/SceneViewModel.cpp：类型切换先取消预览，不标脏、不入历史。
- src/editor/MainWindow.cpp：统一吸附开关名称及G/手柄能力提示。
- src/editor/workbench/WorkbenchShell.cpp：头部开关/类型下拉、视图菜单及共享动作同步。
- src/editor/operations/ComponentPicker.h：新增顶点目标身份与查询接口。
- src/editor/operations/ComponentPicker.cpp：复用投影/遮挡，接入顶点阈值、源集/子树排除及稳定排序。
- src/editor/operations/ObjectTransformSession.h：记录冻结源身份、排除顶点及命中状态。
- src/editor/operations/ObjectTransformSession.cpp：G顶点移动、约束/数值优先、HUD和目标清除。
- src/editor/operations/OperatorRegistry.cpp：新增开关、步进类型、顶点类型三个真实非历史操作。
- src/renderer_gl/ViewportWidget.h：声明只读目标镜像和独立覆盖层资源。
- src/renderer_gl/ViewportWidget.cpp：固定逻辑像素目标框、Context内资源维护及手柄Ctrl异或。
- tests/ComponentPickerTests.cpp：新增过滤、遮挡、阈值、变换和静态几何目标用例。
- tests/SnapEditorTests.cpp：新增7个真实窗口/GL、输入、历史及保存用例。
- tests/OperatorSearchTests.cpp：真实注册项计数更新为63。
- tests/CMakeLists.txt：接入吸附编辑器测试。
- README.md：增加当前吸附能力和专项说明入口。
- docs/v2-snapping.md：新增操作步骤、源/目标、输入优先级、生命周期及实际验证说明。
- docs/v2-input-operations.md：更新63项Registry及吸附当前能力。
- docs/v2-development-plan.md：记录SNAP-01完成，VIS-01仍待办。
- progress.md：仅末尾追加本轮完成/验证/清单/回滚记录。
- out/validation/v2-snap01-backup.ps1：本机保存18个已有文件真实前稿并预检编码。
- out/validation/v2-snap01-format.ps1：本机仅格式化本项差异和新测试。
- out/validation/v2-snap01-verify.ps1：本机串行构建、专项、完整、DPI及可选截图入口。
- out/validation/v2-snap01-check.ps1：本机定向编码/差异/历史/格式及证据检查。
- 回滚点：`out/validation/v2-before-snap01-20260919/`保存本项前18个已有文件的真实dirty状态。各轮`v2-snap01-*`日志/XML/PNG为本机证据，不是发布包，不覆盖此前PIVOT/CURSOR证据。
- 可执行回滚示例（先保存后续修改）：`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-snap01-20260919/src/editor/operations/ComponentPicker.cpp' -Destination 'E:/Mini3D/src/editor/operations/ComponentPicker.cpp'`。按清单逐个恢复已有文件；新增SnapEditorTests及v2-snapping另行备份后撤回；不要覆盖progress历史，应追加回滚记录。保留此前二期、原方案、HTML手册、29张图及PowerShell修复，不全仓reset/checkout；恢复后重建并测试。
- 下一最小动作：用户重新构建，在头部选择“吸附·顶点”并开启，G靠近目标顶点后确认/撤销。下一计划项VIS-01局部隔离未启动，本轮到此交付。

## 2026-09-19 - Task: VIS-01 元素隐藏与局部隔离

### What was done

- 按用户“开始下一步功能”承接计划 VIS-01，完成 H 临时隐藏、Alt+H 恢复和小键盘 `/` 局部视图；对象模式处理所选子树，编辑模式处理当前点/边/面选区。不继续 Mirror，不提交、推送、打包或发布。
- 隐藏元素、临时对象隐藏、持久 visible 和局部隔离分别管理；不修改完整网格真源或原对象可见性，不新增撤销记录、不因隐藏本身标脏，保存仍保留完整网格。恢复显示不自动重选，退出编辑会话清组件掩码，文档重置清全部掩码。
- 显示三角/源笼覆盖层、点选/框选/全选/域切换/历史活动项、X-Ray、吸附、游标表面查询、对象射线/聚焦/选择框/手柄统一遵守可见性；保留隔离子对象的父级变换。隐藏面不再充当遮挡面；环切若跨隐藏面则明确拒绝。
- 切换可见性取消未确认交互；树中选中隔离范围外对象、删除/持久隐藏隔离根、进入相机预览均按既定规则退出隔离。隔离切换自身不移动相机，不修改灯光规则。
- 菜单/F3/Q新增三个真实非历史操作，总Registry为66；Blender绑定H/Alt+H/小键盘斜杠，Legacy保留菜单入口与文本保护。状态栏显示临时隐藏或局部视图，菜单同步勾选。
- 按Qt/MVVM与UTF-8技能保持ViewModel状态权威、Core无Qt/GL、原有无BOM/LF；定向备份保留原dirty成果，未全量格式化或转码。使用连续失败排查流程定位并修复非整数DPI下状态栏引起的视口高度变化，没有放宽图像验证断言。

### Testing

- `out/build/opengl-viewport-verify` 最终 Debug/Release完整build通过，CTest各4/4，耗时103.11s / 34.98s；证据前缀 `out/validation/v2-vis01-Debug-1-stable-*` 与 `v2-vis01-Release-1-stable-*`，包含build日志、专项XML、ctest.xml和真实窗口图。
- 新增VIS专项两配置各8用例251断言通过，覆盖三选择域、隐藏闭包/共享边/活动项、X-Ray拒绝、真实GL隐藏与恢复、父级变换/子树隔离、原持久显隐及临时隐藏恢复、无历史/保存点污染、取消模态、Undo/Redo、环切拒绝、保存重开/删除/文档重置、两键位/主副键盘/文本保护及Registry冻结上下文。
- Release150%和200%串行联动各46用例1253断言通过，覆盖VIS、吸附、环切、框选、游标、键位、搜索与收藏。证据 `v2-vis01-Release-1.5-stable-*` / `v2-vis01-Release-2-stable-*`；已查看100%与200%实际窗口图，局部视图/临时隐藏提示可读，200%属性区保留滚动。
- 首轮150%联动45/46通过，失败为局部视图恢复图像不等；没有跳过断言。加入尺寸/矩阵/帧图诊断后稳定复现：视口1056×740变为1056×739，帧图1584×1110变为1584×1109，投影改变。根因是新增状态栏标签空文本与中文文本的sizeHint高度差异；按非空文本预留最小高度后，原像素相等断言与新增视口尺寸/投影不变断言、两配置全量及DPI回归均通过。保留`Release-1.5-dpi`及`Release-1.5-diagnose`失败XML/诊断PNG。
- Debug初轮7用例226断言、扩展轮8用例248断言及4/4完整回归亦通过；Release修正状态栏前8用例249断言及4/4全量通过。最终以stable前缀为准，截图保存断言仅在设置捕获路径时启用。
- 最终通过`out/validation/v2-vis01-check.ps1 -Final`校验严格UTF-8无BOM/LF、逐文件前稿差异空白、新增C++格式、docs链接、progress历史字节前缀及四组XML证据。只读探索中曾引用不存在的`src/core/EditableMesh.h`及未展开的rg路径通配符，已根据实际`core/modeling`路径与目录查询纠正，未做任何错误路径写入。
- GL像素传输性能警告与200%时Windows对窗口高度夹取1物理像素的提示未造成断言失败。大网格性能预算、跨屏、真实输入法、二期最终S01–S12与独立发布包不在本轮完成声明内；此前QA偶发失活问题仍保留原跟踪，不声称本项将其修复。

### Notes

- src/core/ViewportVisibility.h：新增会话可见性值对象和对象/三域/三角过滤规则，不增持久协议字段。
- src/editor/SceneViewModel.h：声明隐藏、恢复、局部视图及只读掩码/通知接口。
- src/editor/SceneViewModel.cpp：管理会话生命周期、过滤隐藏选择/活动项、取消预览及拒绝跨隐藏面的环切。
- src/editor/MainWindow.cpp：接入三项菜单和视口同步，游标查询传掩码；新增稳定高度的状态提示。
- src/editor/operations/ComponentPicker.h：为四类CPU查询增加向后兼容的默认可见性参数。
- src/editor/operations/ComponentPicker.cpp：统一过滤对象/组件候选、三角遮挡、吸附目标和游标表面。
- src/editor/operations/ComponentInteraction.cpp：更新点选/框选及覆盖层，过滤独立显示候选的派生三角，不写source。
- src/editor/operations/LoopCutSession.cpp：起始边拾取遵守隐藏掩码。
- src/editor/operations/ObjectTransformSession.cpp：对象变换可见性校验及顶点目标查询传入掩码。
- src/editor/operations/KeymapRouter.cpp：绑定Blender视口H、Alt+H和小键盘斜杠。
- src/editor/operations/OperatorRegistry.cpp：登记三项操作并统一可见性/范围校验。
- src/renderer_gl/RayCaster.h：扩展可见性范围和射线查询参数，旧调用保留默认值。
- src/renderer_gl/RayCaster.cpp：聚焦/对象射线/包围框过滤临时掩码，遍历保留祖先变换。
- src/renderer_gl/Renderer.h：声明非持久视口可见性镜像。
- src/renderer_gl/Renderer.cpp：节点绘制/手柄/选框/聚焦使用掩码，隔离外祖先不截断子树遍历。
- src/renderer_gl/ViewportWidget.h：声明可见性同步接口和只读状态镜像。
- src/renderer_gl/ViewportWidget.cpp：同步Renderer及重建Context后的掩码，不重建GL Context。
- tests/VisibilityEditorTests.cpp：新增8项CPU/真实窗口/GL验收，保留DPI诊断与尺寸/矩阵不变量。
- tests/OperatorSearchTests.cpp：真实Registry计数更新为66。
- tests/CMakeLists.txt：接入可见性测试源文件。
- README.md：增加当前隐藏/隔离能力和专项说明链接。
- docs/v2-visibility.md：新增操作步骤、隐藏闭包、状态/历史边界、生命周期和验证说明。
- docs/v2-input-operations.md：记录三个新操作和两键位入口。
- docs/v2-development-plan.md：标记VIS-01完成、记录验证与DPI修复；Mirror仍待办。
- progress.md：只在末尾追加本轮结果、证据、清单与回滚点。
- out/validation/v2-vis01-backup.ps1：本机22份已有文件前稿备份及编码预检。
- out/validation/v2-vis01-format.ps1：仅格式化本轮差异及新增C++文件。
- out/validation/v2-vis01-verify.ps1：本机串行构建/专项/完整/DPI验证与独立日志/XML/PNG。
- out/validation/v2-vis01-check.ps1：本机最终编码/前稿差异/历史追加/格式/文档与证据校验。
- 回滚点：`out/validation/v2-before-vis01-20260919/`保存本项前22个已有文件的真实状态，包含已经完成的SNAP成果。不要使用SNAP前稿回滚VIS，也不要全仓reset/checkout。
- 可执行回滚示例（先另存后续修改）：`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-vis01-20260919/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`。按上述清单逐个恢复已有文件；新增ViewportVisibility.h、VisibilityEditorTests.cpp和v2-visibility.md先另存后撤回，CMake恢复前稿后重建验证。不要覆盖progress历史，应追加回滚记录。原二期资料、HTML手册/29图和PowerShell修复保持不动。
- 下一最小动作：用户在Qt Creator重新构建当前工程，按`docs/v2-visibility.md`验收H/Alt+H和小键盘斜杠；本轮到此交付，MIRROR-01不自动启动。

## 2026-09-19 - Task: MIRROR-01 镜像求值基础（依赖调整）

### What was done

- 按用户“现在继续开发下一个任务”承接 MIRROR-01，完成局部单轴镜像、中心对应边界 Merge、独立 Clipping 会话、来源映射和 Scene 只读离线求值。未自动进入 MIRROR-02，不提交、推送、打包或发布。
- 实施前说明依赖调整：原 MIRROR-01 的持久修改器状态和 Registry 入口移交 MIRROR-02，与卡片、实时显示、选择、历史和保存一起闭合。当前没有 Mirror 界面入口，不能把核心完成表述为修改器已可供用户操作；Registry 仍为66，S09仍未验收。
- Merge 仅把两端均在阈值内的源边界边端点投影到原点平面，并与自身镜像对应点共用身份；不焊附近独立部件、内部点或孤立点，不自动切半/删中心盖。完全由合并点构成的平面面不重复生成。
- 反射坐标/硬法线并反转面环，保留材质/UV/颜色；投影和夹持引起面形变化时重算法线。源ID保留，生成ID按各域源ID排序，支持64位及耗尽拒绝；镜像面/角始终标记只读来源，中心共用点不能单独作为可写性判据。
- Clipping 冻结 before/选区/参数，仅所选边界点参与初始阈值捕获、阈值内或跨平面捕获、沿平面移动和会话内锁定；与Merge独立。候选失败不推进锁定状态，拓扑/属性变动和越选区移动拒绝。
- Scene求值不改变源、绑定、revision；合法结果可经已有几何候选安装并用原快照恢复，revision保持单调，普通应用后几何可按现有格式3往返。本轮未新增修改器持久字段/历史命令或放宽已有材质限制。
- 遵循C++/Qt命名与UTF-8技能，Core无Qt/GL，保持单线程和唯一历史栈；旧文件保留无BOM/LF，未转码或全仓格式化。按交接摘要技能明确核心/界面交付边界。

### Testing

- `out/build/opengl-viewport-verify` Debug/Release完整build通过；最终镜像专项两配置各10用例1596断言通过。Debug构建证据 `out/validation/v2-mirror01-Debug-final-build.log`，最终专项/完整复验证据前缀 `v2-mirror01-Debug-recheck-*`；Release构建/专项/全量证据前缀 `v2-mirror01-Release-final-*`。
- Debug完整CTest复验4/4（106.94s）、Release完整CTest4/4（36.32s），覆盖Core、Assets、GPU和Editor。本轮未新增UI功能或DPI验收声明，不复用旧截图冒充Mirror实际显示。
- 专项覆盖开口半立方体8点5面到12点10面闭合、XYZ反射绕序/法线/角属性、共享中心点上的镜像面角来源、阈值包含边界、只有一端接近平面不误焊、独立部件/孤立点/整闭壳、不重复平面面、54位以上ID与容器重排、三类ID耗尽/非法参数/退化拒绝、三轴Clipping捕获和跨越、禁用/独立开关、失败不锁定、局部父变换/负尺度、Scene源与revision不变、候选安装/恢复及格式3往返。
- 初轮Debug专项9用例1465断言通过，但测试helper在FAIL后return触发C4702不可达警告；按实际原因改为查找并REQUIRE后返回，保留失败断言。扩展轮10用例1581断言通过，再补三轴跨平面断言后为最终1596。最终两配置构建日志不再出现该警告。
- Debug首次全量有1个旧游标UI用例失败：`CursorEditorTests.cpp:253` 的 `placements.count()==1` 实际为0，170用例169通过，原种子589959412，保留 `v2-mirror01-Debug-final-ctest.xml`。检查确认失败处为鼠标输入到信号环节，尚未进入几何定位；本轮未改动该路径。以未改二进制和原种子定向运行 `[cursor-ui]`，3用例96断言通过（`v2-mirror01-Debug-cursor-seed.xml`），随后完整复验通过。未稳定复现、未证实根因，不跳过/放宽断言，不声称已修复；转QA跟踪，未扩展修改游标模块。
- `out/validation/v2-mirror01-check.ps1 -Final` 检查本轮严格UTF-8无BOM/LF、前稿差异空白、新增C++格式、文档链接、progress历史字节前缀和两配置最终XML。GL像素传输性能警告不等同于失败；大网格性能、全局自交、Mirror实时交互和S09不在本轮完成声明内。

### Notes

- src/core/modeling/Mirror.h：新增纯Core镜像参数、求值结果/来源及夹持会话接口。
- src/core/modeling/Mirror.cpp：实现反射、对应边界合并、来源身份和夹持原子候选。
- src/core/CMakeLists.txt：注册Mirror模块。
- src/core/Scene.h：声明不修改源与revision的只读镜像求值接口。
- src/core/Scene.cpp：从可编辑绑定委托离线求值，不自动转换静态对象。
- tests/MirrorTests.cpp：新增10个核心/Scene回归用例。
- tests/CMakeLists.txt：将Mirror专项加入纯Core测试程序。
- README.md：说明镜像核心已完成但界面入口未接通。
- docs/v2-mirror-core.md：新增规则、API、来源/应用契约、验证结果与未完成边界。
- docs/v2-development-plan.md：记录核心完成与依赖调整，MIRROR-02/S09仍待办，追加游标偶发失败QA项。
- progress.md：仅末尾追加本轮结果、实际验证、文件清单与回滚点。
- out/validation/v2-mirror01-backup.ps1：保存7个已有文件的本轮真实前稿并预检编码。
- out/validation/v2-mirror01-format.ps1：仅格式化本项已有文件差异与新增源码。
- out/validation/v2-mirror01-verify.ps1：串行构建、专项与完整回归，保存独立日志/XML。
- out/validation/v2-mirror01-ui-diagnose.ps1：按原随机种子定向复现旧游标UI失败，不修改原断言。
- out/validation/v2-mirror01-check.ps1：检查编码、前稿差异、历史追加、格式、文档及最终测试证据。
- 回滚点：`out/validation/v2-before-mirror01-20260919/`保存本项前7个已有文件，包含已完成VIS/SNAP及此前二期成果。可执行示例（先另存后续改动）：`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-mirror01-20260919/src/core/Scene.cpp' -Destination 'E:/Mini3D/src/core/Scene.cpp'`。同法逐文件恢复Scene.h、Core/Tests CMake、README和计划；新增Mirror.h/.cpp、MirrorTests.cpp、v2-mirror-core.md先备份后撤回，再构建测试。progress不覆盖历史，应另追加回滚记录。不能用VIS/SNAP前稿、全仓reset/checkout；保留原方案、HTML手册及29图、PowerShell修复和用户其他修改。
- 下一最小动作：按MIRROR-02接入单Mirror持久状态与参数卡片，并与显示/源侧选择/夹持/历史/保存共同验证；本轮未启动该项。

## 2026-09-29 - Task: 合并参考文件的子代理协作设置

### What was done

- 按用户指定读取 `C:/Users/SL/Downloads/AGENTS (2).md`，仅提取子代理相关规则，在项目AGENTS第8节追加角色/模型路由/并发/任务卡/升级/复核规则；原有规范保持不变，不推进MIRROR-02。
- 采用Astra设计与关键验收、Sol实施/复核/按需探索，一层委派和默认最多2个子代理；保留项目任务顺序、scope、用户授权与统一日志要求。规则不代表模型切换，未启动代理或修改全局配置。
- 按UTF-8与PowerShell技能保存真实前稿、定向补丁并检查编码；交付按change-summary-handoff技能说明验证边界。

### Testing

- 通过 `& ./out/validation/agents-subagents-20260929.ps1` 检查：严格UTF-8无BOM/LF、原AGENTS及progress逐字节前缀未变、参考文件哈希未变、定向Git diff空白无误。
- 人工对照参考文件核查四类角色、路由不可确认时交接、并发上限/禁止递归、单写入者/独立副本、任务卡/风险分级、升级与独立验收、受保护文件边界；只追加8.1–8.5，未整份替换或引入其他项目规则。
- 文本自检状态 `SELF_CHECKED`，独立代理复核与模型路由探测 `NOT_RUN`，路由 `UNVERIFIED`。无C++或运行配置变更，未运行构建/CTest，不宣称多模型已实际生效。

### Notes

- AGENTS.md：第8节追加完整子代理分工、路由核验、并发及交付约束。
- docs/agent-collaboration.md：记录参考来源、迁移范围和配置/验证边界，执行规范仍以根AGENTS为准。
- progress.md：只追加本轮工作、验证、文件清单和回滚说明。
- out/validation/agents-subagents-20260929.ps1：本机前稿备份与编码/前缀/差异/参考完整性验证脚本。
- 回滚点：`out/validation/agents-subagents-before-20260929/` 保存修改前AGENTS、progress及参考副本。先另存后续修改，再执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/agents-subagents-before-20260929/AGENTS.md' -Destination 'E:/Mini3D/AGENTS.md'` 恢复规则；本轮新增说明可另存后撤回，progress仅追加回滚记录，不覆盖历史。不修改用户参考文件，不全仓reset，不提交或推送。

## 2026-09-29 - Task: MIRROR-02 单 Mirror 修改器开发接入

### What was done

- 把单 Mirror 参数与完整求值放入不可变几何快照，参数修改、删除与应用均先验证候选后用唯一历史栈安装；真源编辑保留当前参数，应用撤销恢复真源、参数和选区。
- 接通属性卡片、菜单与 Registry；仅可编辑对象能添加。显示、遮挡、游标、聚焦使用求值几何，组件仍从真源拾取；临时隐藏通过来源面过滤，G/R/S 接入固定 before 的 Clipping 会话。
- 格式 3 增加可选单 `modifier`，旧无字段仍无修改器；严格解析并在载入时验证求值。开发优先策略下，最终全量验收与 S09 后置，本轮状态为 `IMPLEMENTED / SELF_CHECKED`，不是 `ACCEPTED`。

### Testing

- 工作目录 `E:/Mini3D`，PowerShell 7.6.6，Release `out/build/opengl-viewport-verify`：`cmake --build ... --config Release --target mini3d_core` 通过；`mini3d_editor`、`mini3d_tests`、`mini3d_editor_tests` 目标构建通过，退出码均 0。
- `mini3d_tests.exe '[mirror02-core]'`：PASS，2 用例/35 断言；`mini3d_editor_tests.exe '[mirror02-editor]'`：PASS，3 用例/36 断言；Registry 定向搜索：PASS，1 用例/158 断言。编辑器测试进程需将 `E:/Qt5.15/6.8.3/msvc2022_64/bin` 前置 PATH；首次未配置 PATH 直接启动退出码 1、无输出，依据 `ctest -C Release -N -V` 环境信息修正后通过，没有断言失败。
- `git diff --check`、Schema JSON 解析、17 个变更源码文件严格 UTF-8 无 BOM 检查通过。Debug、完整 CTest、DPI、S09 复杂模型、性能、耐久和旧游标偶发问题均 `NOT_RUN`，移交统一验收；独立 reviewer 待执行。

### Notes

- `src/core/Scene.h`：为资源与不可变内容加入可选 Mirror 参数、完整求值及显式参数/应用候选接口。
- `src/core/Scene.cpp`：构造先验候选、保留真源编辑的参数并原子载入修改器。
- `src/core/SceneSerializer.cpp`：读写并严格校验格式 3 单 Mirror 字段。
- `src/core/ViewportVisibility.h`：将求值三角映射回源面执行临时隐藏。
- `src/renderer_gl/Renderer.h`：为最终显示和可见性缓存刷新添加身份与版本字段。
- `src/renderer_gl/Renderer.cpp`：上传求值三角并按源掩码过滤隐藏面。
- `src/renderer_gl/RayCaster.cpp`：用最终可见几何计算对象与场景包围盒。
- `src/editor/SceneViewModel.h`：暴露 Mirror 参数、应用入口和组件夹持状态。
- `src/editor/SceneViewModel.cpp`：以唯一历史栈提交参数/应用并限制组件 G/R/S 越过镜像平面。
- `src/editor/MirrorInspector.h`：声明单 Mirror 属性卡片。
- `src/editor/MirrorInspector.cpp`：接入添加、参数、应用与删除控件。
- `src/editor/CMakeLists.txt`：编译属性卡片。
- `src/editor/MainWindow.cpp`：把卡片与三个真实菜单动作接入窗口。
- `src/editor/operations/ComponentPicker.cpp`：求值几何用于遮挡/游标/吸附，组件选择仍限真源。
- `src/editor/operations/ComponentInteraction.cpp`：源编辑笼按源面隐藏过滤。
- `src/editor/operations/OperatorRegistry.cpp`：注册三个可执行 Mirror 动作及条件。
- `tests/Mirror02Tests.cpp`：验证 Core 候选、应用快照与格式 3 原子往返。
- `tests/MirrorEditorTests.cpp`：验证应用历史、保存重开、Clipping 与卡片入口。
- `tests/OperatorSearchTests.cpp`：按真实动作数更新 Registry 断言。
- `tests/CMakeLists.txt`：加入两组定向专项。
- `docs/scene-format-v3.schema.json`：声明可选单 Mirror 结构约束。
- `docs/v2-scene-format.md`：说明修改器字段和停用应用语义。
- `docs/v2-mirror-core.md`：明确 MIRROR-01 页面是历史边界并链接本批说明。
- `docs/v2-mirror-modifier.md`：记录本批操作、数据/显示规则与待验收项。
- `docs/v2-development-plan.md`：登记开发优先、MIRROR-02 实现与统一验收后置。
- `progress.md`：仅在末尾追加本轮记录。
- 回滚点：`out/validation/v2-before-mirror02-20260929/manifest.csv` 记录本批已有文件的 SHA-256 与同目录真实前稿。先另存后续改动，再对本批所改已有文件逐一执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-mirror02-20260929/src/core/Scene.cpp' -Destination 'E:/Mini3D/src/core/Scene.cpp'` 形式的恢复；本批新增的 `MirrorInspector.h/.cpp`、`Mirror02Tests.cpp`、`MirrorEditorTests.cpp`、`v2-mirror-modifier.md` 先另存后撤回。`progress.md` 历史不覆盖，回滚另追加记录；不使用全仓 reset/checkout。

## 2026-09-29 - Task: MIRROR-02 Clipping 失败候选捕获修复

### What was done

- 按独立定向复核发现的问题，把组件 Clipping 捕获改为两级提交：先在会话副本夹持，完整 Scene 镜像候选发布成功后才保留该副本；失败候选不改变已有捕获状态。
- 用两相邻四边面夹具复现“源候选合法、镜像合并后非流形”场景，验证失败后退回正 X 可继续自由移动；不扩展 Mirror Core 算法或其他模块。

### Testing

- 工作目录 `E:/Mini3D`，PowerShell 7.6.6，Release `out/build/opengl-viewport-verify`：重建 `mini3d_editor_tests` 与 `mini3d_editor` 目标均 PASS、退出码 0。
- `mini3d_tests.exe '[mirror02-core]'`：PASS，2 用例/35 断言；`mini3d_editor_tests.exe '[mirror02-editor]'`：PASS，4 用例/46 断言，其中新增回归单独运行 1 用例/10 断言 PASS。编辑器测试 PATH 前置 `E:/Qt5.15/6.8.3/msvc2022_64/bin`。
- 本次仅为定向修复自检；统一全量验收仍 `NOT_RUN`，最终独立复核结论与文档状态由主代理确认。

### Notes

- `src/editor/SceneViewModel.cpp`：延后发布完整候选之前的 Clipping 捕获提交。
- `tests/MirrorEditorTests.cpp`：新增失败求值后不锁死边界点的回归夹具。
- `progress.md`：仅追加本轮修复与真实验证记录。
- 回滚点沿用 `out/validation/v2-before-mirror02-20260929/` 的原始前稿和 `manifest.csv`；`after-manifest.csv` 记录本轮最终哈希。若回退完整 MIRROR-02 批次，先另存后续更改，再按上一轮 Notes 的逐文件 `Copy-Item` 示例恢复已有文件，另存后撤回本批新增文件；`progress.md` 只追加回滚记录，不覆盖历史。

## 2026-10-02 - Task: 子代理目标改为 GPT-6.1 Sol / max

### What was done

- 按用户明确要求，统一 builder、reviewer、explorer 的目标为 `gpt-6.1-sol`，思考强度 `max`；主代理、全局配置、源码和既有开发成果不变。
- 同步显式启动参数、上下文传递与禁止静默降级约束。规则编辑由主代理执行，不将受保护规则文件交给子代理自行修改；本轮文档自检为 `SELF_CHECKED`。

### Testing

- 工作目录 `E:/Mini3D`，PowerShell 7.6.6。已检查相对本轮前稿的定向差异；`git diff --check -- AGENTS.md docs/agent-collaboration.md progress.md` 通过。`git diff --no-index` 返回 1 表示存在预期差异，不是验证失败。
- 原生 `spawn_agent` 最小调用 1 次：`task_name=route_probe_sol61`、`model=gpt-6.1-sol`、`reasoning_effort=max`、`fork_turns=none`。返回 `Unknown model`，实际支持列表仅含 `gpt-6-astra, gpt-6-sol, gpt-6-luna, gpt-5.6-sol, gpt-5.6-terra`。路由 `BLOCKED`，没有启动子代理；推理强度实际执行、后端模型与运行器内部版本 `UNVERIFIED`。未重试或降级。
- 官方子代理页面读取 HTTP 200；首次读取误传空 OutFile 导致本地参数校验失败，移除该无效参数后成功。未运行 C++ 构建/测试，因为本轮没有程序改动。

### Notes

- `AGENTS.md`：修改三个子代理角色的目标模型和思考强度，明确后续显式启动参数；其他执行边界保留。
- `docs/agent-collaboration.md`：追加本次参数调整、真实路由拒绝证据及恢复条件。
- `progress.md`：仅追加本次任务与验证记录。
- 回滚点：`out/validation/agents-sol61-before-20261002/` 为本轮修改前的三份真实文件，不覆盖 9 月 29 日前稿。先另存后续改动，再执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/agents-sol61-before-20261002/AGENTS.md' -Destination 'E:/Mini3D/AGENTS.md'` 和 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/agents-sol61-before-20261002/agent-collaboration.md' -Destination 'E:/Mini3D/docs/agent-collaboration.md'` 恢复本轮规则与说明；progress 历史不覆盖，回滚另追加记录。

## 2026-10-02 - Task: 以用户《子代理设置.md》为准合并协作规则

### What was done

- 完整读取指定文档，将其协作规则合并到项目第 8 节，替代旧主代理 Astra、所有子代理 Sol 的约定。新目标为主代理/实施/独立审查 `gpt-6.1-sol / max`，调研 `gpt-6-luna / max`，专家 `gpt-6-astra / high`。
- 同步小任务主代理直做、按收益分派、默认两个子代理及三个独立只读研究例外、单文件写入所有权、简短任务包、专家触发条件和按风险验收规则。保留原第 1–7 节及工具边界，不改业务代码或全局配置。
- 本次为主代理执行的规则文档调整；没有子代理委派。文档约定不是可执行模型配置，不声称本会话模型已切换；上一轮启动失败无新环境证据，不重复调用，不自行降级。

### Testing

- 在 `E:/Mini3D`、PowerShell 7.6.6 下检查最终合并文本：原项目规范前缀完全保留；用户文档正文完整保留，仅调整章节层级/编号及相应内部引用。源文件及本轮前稿 `Get-FileHash` 一致，用户原文未修改。
- 检查相对本轮前稿的定向差异；严格 UTF-8 无 BOM / LF、progress 仅追加以及 `git diff --check -- AGENTS.md docs/agent-collaboration.md progress.md` 均 PASS，检查命令退出码 0。状态 `SELF_CHECKED`，不是独立代理复核。
- 已读取官方子代理页面（HTTP 200），核对文档规则与实际配置不同的边界。未安装自定义代理配置，未运行 C++ 构建/测试（无程序变更），未运行模型探针；新角色实际生效值 `UNVERIFIED`，先前 Sol 6.1 路由拒绝仍是已知限制。

### Notes

- `AGENTS.md`：以指定文档替换子代理协作条款，保留原有项目执行规范。
- `docs/agent-collaboration.md`：新增当前五角色分工、调度规则和实际路由限制，将旧迁移说明明确归为历史。
- `progress.md`：仅追加本轮合并结果、检查和限制。
- 本轮前稿及用户原文副本存于 `out/validation/agents-policy-before-20261002/`。回滚前另存后续改动，再执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/agents-policy-before-20261002/AGENTS.md' -Destination 'E:/Mini3D/AGENTS.md'` 和 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/agents-policy-before-20261002/agent-collaboration.md' -Destination 'E:/Mini3D/docs/agent-collaboration.md'`；progress 不覆盖历史，另追加回滚记录。不提交或推送。

## 2026-10-02 - Task: 修复活动 CLI 缺少 GPT-6.1 Sol 的模型目录配置

### What was done

- 依据进程父链、CODEX_HOME、配置与同版本 model/list 对照，定位当前会话使用 D 盘 CLI home，未加载 C 盘已更新模型目录；不是 Mini3D AGENTS 角色名称导致。
- 按用户本轮模型列表修复授权，仅在 D 盘活动 config.toml 增加 model_catalog_json，引用 C 盘用户现有目录。不修改默认模型/思考强度、接口、凭据、权限、模型目录内容或业务代码；CLI 列表将随该完整目录更新。
- 保存真实配置前稿，补充可复跑的本地目录探针和使用说明。当前旧后台仍需重启，已向用户说明共享服务中断风险并询问；未自行重启或恢复二期开发。

### Testing

- 环境 `E:/Mini3D`、PowerShell 7.6.6；`codex app-server daemon version` 显示 CLI/运行中服务均 0.157.0。官方配置参考确认 model_catalog_json 在启动时加载。
- `& ./out/validation/probe-model-catalog-20261002.ps1`：修复前 model/list 无 gpt-6.1-sol；加 `-Catalog 'C:/Users/SL/.codex/cockpit-model-catalog.json'` 后包含目标及 max，两次均退出码 0。配置写入后的默认目录也已返回目标/max；没有调用模型推理或启动开发任务。
- 一次配置后验证命令误用了占位备份文件名，文件读取失败；目录探针本身已返回新列表。随后使用真实备份路径核查，确认配置仅新增一行、UTF-8 无 BOM / LF 不变，退出码 0；未因该读文件错误重复修改配置。
- 配置变化后最小子代理请求 `sol61_catalog_recheck`（gpt-6.1-sol/max/none）仍返回旧服务 Unknown model；未降级、未重复重试。目录修复为 PASS；现有服务重载及真实推理为 NOT_RUN/待确认，不声称子代理已恢复可用。
- 后置组合检查曾错误用 LASTEXITCODE 判断无原生命令的 PowerShell 探针，导致误报失败；探针已正常输出目录，无需重跑。移除不适用判断后，独立执行定向 diff、日志前缀和严格 UTF-8 无 BOM/LF 检查均 PASS、退出码 0。本机配置差异自检 PASS；业务构建测试 NOT_RUN（无业务代码修改）。

### Notes

- `D:/Lang/codex-cli-home/config.toml`：增加模型目录路径；本次唯一工作区外正式配置修改，授权来自用户修复模型列表请求。
- `out/validation/probe-model-catalog-20261002.ps1`：新增本机 JSON-RPC 目录对照探针，只关闭自己创建的临时服务进程。
- `docs/agent-collaboration.md`：同步根因、配置修复、目录共用影响、重启步骤及实际验证边界。
- `progress.md`：仅追加本轮结果。
- 回滚：先保存后续配置修改，再执行 `Copy-Item -LiteralPath 'D:/Lang/codex-cli-home/config.toml.before-sol61-catalog-20261002-183458.bak' -Destination 'D:/Lang/codex-cli-home/config.toml'`；服务重启后恢复原目录行为。项目文档前稿在 `out/validation/sol61-catalog-before-20261002/agent-collaboration.md`，按需逐文件恢复；日志不覆盖历史。探针可留作诊断或单独撤回，不做全仓重置。

## 2026-10-02 - Task: CLI 模型列表未刷新排查与获准重启交接

### What was done

- 用户确认缺少模型的是 CLI `/model`。核查配置仍存在，前台已重开，但共享后台 PID 16120 仍自 11:42:26 运行，早于配置修改时间 18:34:58。
- 新服务按默认配置再次识别 gpt-6.1-sol/max。没有重复修改模型配置；采用失败复盘技能区分新服务验证和用户实际入口，未将目录识别当作推理成功。
- 用户明确回复“允许现在重启”，授权范围为 Codex CLI 共享后台及其会话中断。已提示当前会话可能断开；下一动作执行官方 daemon restart，成功与否须重连核验，不在本条预写成功。

### Testing

- PowerShell 7.6.6、CLI/daemon 0.157.0；配置路径/进程创建时间核查通过。
- 运行中服务 proxy 的管道查询退出码 1、10054；改成保持连接逐条请求后 20 秒超时，仍退出码 1。已停止该路径，未声称取得运行中 model/list；仅关闭自身代理进程，没有关闭服务。
- 临时 RunningService 开关已撤回，探针 SHA-256 与本轮前稿一致。原探针默认模式返回目标及 max，退出码 0。
- restart 及重连后实际 spawn 核验尚待执行；无业务代码修改，C++ 构建/测试 NOT_RUN。

### Notes

- `docs/agent-collaboration.md`：追加 CLI 前台与共享后台的时间证据、失败查询边界和重启授权状态。
- `progress.md`：只追加本轮排查及重启前交接记录。
- `out/validation/probe-model-catalog-20261002.ps1`：曾临时增加实时查询开关，现已恢复本轮前稿，无最终差异。
- 本轮前稿位于 `out/validation/sol61-live-before-20261002/`。文档回滚前另存后续修改，再执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/sol61-live-before-20261002/agent-collaboration.md' -Destination 'E:/Mini3D/docs/agent-collaboration.md'`；progress 只追加回滚记录。全局配置本轮未再修改，沿用上一条备份与回滚方式。

## 2026-10-02 - Task: CLI 后台重启完成并验证 Sol 6.1/max 子代理

### What was done

- 按用户“允许现在重启”的明确授权执行官方 daemon restart。会话因后台重启中断后自动恢复，先核对进程，不重复重启。
- 原后台 PID 16120 已退出，新后台 PID 19824 于 18:44:58 启动；修复后的模型目录配置仍存在，CLI/daemon 版本均为 0.157.0。
- 以指定模型与推理强度真实创建最小子代理并收到响应，消除先前 Unknown model 阻塞；未切换主会话默认模型，未恢复二期业务开发。

### Testing

- `codex app-server daemon restart` 在前一会话执行，原命令工具结果被服务重启打断，不能补写退出码；运行器恢复通知、daemon.pid 与进程创建时间共同证实后台替换完成。
- 重连后 `codex app-server daemon version` 退出码 0，状态 running、版本 0.157.0；配置定向检查保留 model_catalog_json 引用。
- 原生 `spawn_agent` 请求 `sol61_after_restart_probe`、`model=gpt-6.1-sol`、`reasoning_effort=max`、`fork_turns=none` 成功返回任务路径；子代理正常完成并响应“启动验证完成”，启动/响应 PASS。未读写业务文件，没有子代理测试或开发动作；提供商内部后端映射 UNVERIFIED，不以自报身份证明模型。
- 本轮仅更新验证记录；C++ 构建/测试 NOT_RUN。CLI `/model` 前台列表未直接进行 UI 操作，提示用户重新打开列表确认显示。

### Notes

- `docs/agent-collaboration.md`：将目录修复状态更新为重启及真实子代理启动验证通过，保留诊断过程和证据边界。
- `progress.md`：仅追加最终结果，保留此前失败与重启前记录。
- 文档前稿在 `out/validation/sol61-restart-before-20261002/`；另存后续修改后可执行 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/sol61-restart-before-20261002/agent-collaboration.md' -Destination 'E:/Mini3D/docs/agent-collaboration.md'`。日志仅追加回滚记录。全局配置恢复使用此前 `config.toml.before-sol61-catalog-20261002-183458.bak`，需获准再次重启才加载旧目录；本轮不再修改配置。

## 2026-10-02 - Task: 恢复二期长计划并闭合 Mirror 独立复核

### What was done

- 按用户最新明确授权恢复剩余二期开发顺序，保留开发优先/阶段定向检查/末尾统一验收策略，不提交、不推送、不发布。
- 保存现有未提交二期代码和文档的完整相关前稿，修正文档中 Mirror 界面与专项数量的旧状态。
- 对 Clipping 修复后的冻结快照完成独立只读复核，无阻碍进入 FILE-01 的明确问题；不将聚焦复核作为二期全部验收。

### Testing

- PowerShell 7.6.6；Release `mini3d_tests.exe '[mirror02-core]'` 通过 2 用例/35 断言，`mini3d_editor_tests.exe '[mirror02-editor]'` 通过 4 用例/46 断言（Qt bin PATH 前置），退出码均 0。
- 独立审查请求 `gpt-6.1-sol/max/fork=none` 被接受并返回结论，9 个相关文件开始/结束 SHA256 均与冻结稿及 manifest 一致；未发现夹持提交、参数应用/删除/快照历史或格式原子发布的阻碍问题。提供商内部映射未验证。
- `git diff --check` 通过；已有修改文件 UTF-8 无 BOM/LF 保持。完整 Debug/Release、DPI、S09、耐久及性能未在收尾批运行，后置统一验收。
- 原目标后台仍显示 paused；工具无恢复状态接口。用户恢复授权已生效，沿同一目标继续施工，未创建重复目标或错误标记完成。

### Notes

- `README.md`：更新真实 Mirror 入口及开发/验收区别。
- `docs/v2-mirror-modifier.md`：更新夹持回归数量与独立复核范围。
- `docs/v2-development-plan.md`：登记恢复授权、执行顺序和当前 FILE-01 落点。
- `progress.md`：仅追加本轮记录。
- `out/validation/v2-completion-snapshot.ps1`：创建不可覆盖的相关源码/文档前稿与 SHA256 清单。
- 回滚点：`out/validation/v2-before-completion-20261002/`；先另存后续改动，再逐文件恢复，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-completion-20261002/README.md' -Destination 'E:/Mini3D/README.md'`。同法恢复两份 docs，日志只追加回滚说明，不覆盖历史；不全仓重置。

## 2026-10-02 - Task: 按用户要求全部暂停二期任务

### What was done

- 按“先全部暂停”中断在途 FILE-01 实施代理，停止主任务及所有后续开发/验证，保留现有改动，不回滚、不提交。
- 固定暂停点：Mirror 收尾完成；FILE-01 完成定位与方案确认但未写业务源码/新测试；FILE-02 完成只读入口调研但未实施。
- 保留已创建的阶段定向构建/测试脚本，其生成不代表 FILE-01 已构建或验证通过。

### Testing

- 只读核查 FILE-01 四个已有源码文件与恢复前稿 SHA256 一致；两份新兼容性测试文件尚不存在。未执行 FILE-01 构建或测试。
- 未发现 Mini3D 相关 cmake/ctest/MSBuild 或 mini3d 测试进程；在途实施代理已通过 interrupt_agent 中断，另发送保持停止通知（不触发新回合）。
- `v2-completion-verify.ps1` 在暂停前已通过 PowerShell AST 语法检查；脚本尚未用于执行构建或测试。
- 暂停核查中 rg 对尚不存在的测试文件返回缺文件错误，无仓库修改；依据前一步文件存在性检查确认尚未落地，不重试不存在的路径。

### Notes

- `docs/v2-development-plan.md`：登记用户暂停指令与准确落点。
- `progress.md`：仅追加暂停记录。
- `out/validation/v2-completion-verify.ps1`：暂停前新增可复用的定向构建/测试日志脚本，尚未执行阶段验证。
- 回滚点沿用 `out/validation/v2-before-completion-20261002/`。先保存后续改动，再按文件恢复计划，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-completion-20261002/docs/v2-development-plan.md' -Destination 'E:/Mini3D/docs/v2-development-plan.md'`；新增脚本可另存后单独撤回，日志只追加回滚说明。不因暂停自动回滚已有二期成果。

## 2026-10-02 - Task: 再次恢复全部二期任务并完成 FILE-01 开发闭环

### What was done

- 按用户“全部重新启动继续完成所有任务”撤销暂停，沿原顺序继续，不重复已完成的 Mirror 复核，不提交或发布。
- 格式3显式声明 Y-up，旧版本缺省仍按 Y-up；拒绝显式其他坐标轴。补齐源网格、64位ID、逐角属性、变换、设备、Mirror、游标及资源子网格往返。
- 载入失败保留场景、资源、选择、历史/保存点和路径；修复坏文件提前取消对象/组件活动预览的问题。旧版另存与失败写入不损坏原件。

### Testing

- Release 定向构建 editor/core tests/editor tests 成功；CPU `[file-compatibility]` 3用例123断言、Editor `[file-compatibility-editor]` 5用例271断言通过，退出码0。
- 活动预览回归修复前稳定失败2用例（对象预览被恢复、组件事务被取消），修复读取成功后才取消的时点后随整组通过。日志在 `out/validation/v2-completion-file01-preview-repro/` 和 `v2-completion-file01/`。
- 初次新增回归误用了 Transform 的不存在相等运算符，编译C2678；核查接口后改比较localMatrix，未为测试修改领域接口。随后未使用变量警告已通过实体断言消除。
- 新测试仅在本轮范围clang-format，UTF-8无BOM/LF；最终diff与编码检查通过。Debug全量/DPI/外部工具仍留统一验收，未标最终ACCEPTED。
- 实施代理请求Sol6.1/max被接受；提供商内部映射仍未独立核验。原长目标工具仍paused且没有恢复接口，沿同一用户授权目标实际施工，不创建重复目标。

### Notes

- `src/core/SceneSerializer.cpp`：写Y轴声明并校验旧/新文件显式轴。
- `src/core/SceneSerializer.h`：说明缺省轴兼容约定。
- `src/editor/SceneViewModel.cpp`：成功读取之后才取消旧事务。
- `tests/FileCompatibilityTests.cpp`：新增完整字段与损坏输入原子拒绝CPU用例。
- `tests/FileCompatibilityEditorTests.cpp`：新增资源/旧件保护与活动预览回归。
- `tests/CMakeLists.txt`：接入上述两类测试。
- `docs/scene-format-v3.schema.json`：可选upAxis限定Y。
- `docs/v2-scene-format.md`：同步坐标与版本解释。
- `docs/v2-file-compatibility.md`：记录行为、边界与实际验证。
- `docs/v2-development-plan.md`：保留暂停历史，记录恢复和FILE-01开发状态。
- `progress.md`：仅追加本轮闭环。
- 回滚点：`out/validation/v2-before-file01-resume-20261002/`，未包含的Serializer.h/SceneViewModel.cpp用 `v2-before-completion-20261002/`。先另存后续改动，再按文件恢复，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-completion-20261002/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`；新增两测试及兼容文档可逐个另存撤回，日志只追加回滚说明，不全仓重置。

## 2026-10-02 - Task: FILE-02 所选源/求值 OBJ 导出开发闭环

### What was done

- 文件菜单提供源与修改器结果两种明确导出，沿用保存对话框覆盖确认；活动预览未确认时拒绝，不取消事务。
- 纯CPU编码保留多边形环或静态真实三角/拆点，烘焙父子世界变换，逆转置法线与负缩放绕序/角属性一致。
- 独立原子写盘关闭直接回退；导出成功/失败/取消均不改变工程路径、资源、选择、历史或保存点。只输出几何/UV/法线，不冒充MTL或纹理导出。

### Testing

- Release editor/core tests/editor tests构建成功。CPU `[obj-export]` 7用例1882断言，Editor `[obj-export-editor]` 4用例70断言通过，退出码0；日志 `out/validation/v2-completion-file02/`。
- 检查Cube源8点6四边面、Mirror结果16点12面，静态Cube24拆点12三角，真实glTF索引数量、父变换、旋转/非均匀/负尺度、非法输入、locale、名称换行、历史/预览保持和菜单默认覆盖确认/取消。
- 初次Editor新测试误把EditableMeshRecord当作带id记录，C2039；改为从SceneNode稳定meshId查询后构建通过，未改变领域接口。
- 新增源码限定clang-format、UTF-8无BOM/LF与diff检查通过。外部Blender、两配置全量/DPI与完整统一验收未运行，仍待办。
- 核心实施请求Sol6.1/max被客户端接受，提供商内部映射未独立核验；两个后续独立纯CPU模块已划定新文件边界并行开发，按计划顺序整合。

### Notes

- `src/core/modeling/ObjExporter.h/.cpp`：新增只读源/静态OBJ编码。
- `src/assets/ObjDocument.h/.cpp`：新增QSaveFile原子输出。
- `src/core/CMakeLists.txt`、`src/assets/CMakeLists.txt`：接入新增模块。
- `src/editor/SceneViewModel.h/.cpp`：校验上下文、选择源/求值并发出导出结果，不改历史。
- `src/editor/MainWindow.cpp`：新增双导出菜单与保存对话框。
- `tests/ObjExporterTests.cpp`、`tests/ObjExportEditorTests.cpp`、`tests/CMakeLists.txt`：编码与文档/UI定向验证。
- `docs/v2-obj-export.md`、`docs/v2-development-plan.md`、`README.md`：操作、限制与开发状态。
- `progress.md`：仅追加本轮记录。
- 回滚点 `out/validation/v2-before-file02-20261002/`；先保存后续改动，再逐文件恢复，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-file02-20261002/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`。新增OBJ模块/测试/说明逐个另存撤回，日志只追加回滚说明，禁止全仓重置。

## 2026-10-02 - Task: P1-SELECT Loop/Ring/连通片选择开发闭环

### What was done

- 规则Loop沿四价点拓扑对边、Ring跨四边面对边，边界/极点/非四边面停止；不按位置猜测或跨断开部件。
- 接通Alt点击、Ctrl+Alt点击、Shift追加、悬停L及活动组件菜单/F3/Q，点/边/面连通投影一致，隐藏结果过滤。
- 稳定源组件作为选择权威，选择不改几何/文档/撤销历史；Legacy不响应新增Blender点击/按键。

### Testing

- Release构建editor/core tests/editor tests成功；CPU `[topology-selection]` 6用例93断言、Editor `[topology-selection-editor]` 3用例53断言通过，退出码0；日志 `out/validation/v2-completion-select/`。
- 核心覆盖规则平面/边界/闭合带、极点/非四边面停止、64位ID/重排、孤立点/断开部件/非法种子。编辑器覆盖精确选集/活动边、隐藏过滤、三域连通、真实Alt/Ctrl+Alt/L、Registry和Legacy。
- 一次多文件补丁锚点误把eventFilter末尾return false当作return true，apply_patch拒绝且未写任何文件；核查后缩小锚点重施成功，无覆盖或回滚旧成果。
- 新模块/测试UTF-8无BOM/LF与限定格式检查、最终diff检查通过；两配置全量/DPI仍后置，不标最终ACCEPTED。代理请求Sol6.1/max被接受，内部映射未独立核验。

### Notes

- `src/core/modeling/TopologySelection.h/.cpp`：新增只读拓扑路径与连通选择。
- `src/core/CMakeLists.txt`：接入CPU模块。
- `src/editor/SceneViewModel.h/.cpp`：投影/过滤稳定选择并保持活动种子。
- `src/editor/operations/ComponentInteraction.h/.cpp`：Alt点击与真实悬停L输入。
- `src/editor/operations/KeymapRouter.cpp`：登记互斥L键位。
- `src/editor/operations/OperatorRegistry.cpp`：三项真实选择操作与域禁用原因。
- `src/editor/MainWindow.cpp`：菜单与同一选择入口。
- `tests/TopologySelectionTests.cpp`、`tests/TopologySelectionEditorTests.cpp`、`tests/CMakeLists.txt`：CPU及输入/历史定向用例。
- `docs/v2-topology-selection.md`、`docs/v2-development-plan.md`、`README.md`：操作、限制和状态。
- `progress.md`：仅追加本轮记录。
- 回滚点 `out/validation/v2-before-select-20261002/`；先另存后续改动，按文件恢复，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-select-20261002/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`。新增拓扑模块/测试/说明逐个另存撤回，日志只追加回滚记录，不全仓重置。

## 2026-10-02 - Task: P1-PROP 比例编辑开发闭环

### What was done

- 普通组件G/R/S接通Smooth世界欧氏距离与Connected影响，O切换、滚轮调半径、实际世界投影半径圈及HUD提示。
- 每帧从原before重算，半径回扩不累积；Esc恢复全部影响点，确认一次历史；对象和拓扑模态不受开关影响。
- 为真实比例/Mirror组合扩展Clipping影响集，中心非选中边界同样受约束；复制会话仅在成功预览后发布。顶点吸附明确排除整个编辑源对象。

### Testing

- Release定向构建editor/core tests/editor tests成功；`[proportional-transform],[mirror02-core]` 8用例633断言，`[proportional-editor],[mirror02-editor]` 7用例116断言通过，退出码0；日志 `out/validation/v2-completion-prop/`。
- 检查世界非均匀尺度、Connected/断开片、G/R/S、原快照/identity、非法输入/退化原子拒绝；界面O、数字、滚轮、圈图像、半径回扩、取消/历史及非选中Mirror中心点通过。
- 最终差异及UTF-8无BOM/LF检查；新模块限定格式检查。两配置全量/DPI/统一验收后置，不称二期全部通过。独立Sol只读复核同步检查此处Clipping扩展，结果后续追加。
- 请求子代理Sol6.1/max被接受，提供商内部映射未独立核验；目标工具仍paused且无恢复接口，实际按同一用户恢复授权继续，不创建重复目标。

### Notes

- `src/core/modeling/ProportionalTransform.h/.cpp`：新增权重与加权世界位移候选。
- `src/core/modeling/Mirror.h/.cpp`：扩展显式影响集约束与中心边界记录。
- `src/core/CMakeLists.txt`：接入比例核心。
- `src/editor/SceneViewModel.h/.cpp`：窗口偏好、原before求值、隐藏过滤与Clipping候选。
- `src/editor/operations/ObjectTransformSession.h/.cpp`：O/滚轮/HUD与源集吸附排除。
- `src/renderer_gl/ViewportWidget.h/.cpp`：投影世界半径圈与清理。
- `src/editor/MainWindow.cpp`、`src/editor/operations/OperatorRegistry.cpp`、`src/editor/operations/KeymapRouter.cpp`：菜单及互斥入口。
- `tests/ProportionalTransformTests.cpp`、`tests/ProportionalEditorTests.cpp`、`tests/CMakeLists.txt`：核心与真实界面定向检查。
- `docs/v2-proportional-editing.md`、`docs/v2-development-plan.md`、`README.md`：操作、限制、验证和状态。
- `progress.md`：仅追加本轮闭环。
- 回滚点 `out/validation/v2-before-prop-20261002/` 与Mirror专用 `v2-before-prop-clip-20261002/`；先另存后续改动，再按文件恢复，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-prop-20261002/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`。新增比例模块/测试/说明逐个另存撤回，日志只追加回滚说明，不全仓重置。

## 2026-10-02 - Task: P1-BEVEL 单段闭合倒角开发闭环

### What was done

- 单外凸流形边与三价端角构造四交点，闭合邻面和端部，明确拒绝不支持几何/宽度。
- 接通Ctrl+B、菜单/F3/Q、局部宽度模态、原子取消、新面选区及独立F9宽度参数；确认一次历史，重算不累积，保留已有Inset/Extrude类型。
- 比例/Mirror扩展及倒角核心独立Sol只读复核无可操作发现。启用四边细分无法处理倒角五边端面，已说明，不自动关算子。

### Testing

- Release倒角/细分算法联合14用例2601断言通过。最终接线Core9用例986断言，Editor16用例555断言，Assets10用例162断言通过，所有构建/测试退出码0。
- Editor包含4个新增倒角用例、3个细分用例及既有History/F9回归，真实菜单/Ctrl+B/数字/F9、取消/选区/保存点/Undo与Mirror父变换通过。
- 日志 `out/validation/v2-completion-bevel-subdiv-core/` 和 `v2-completion-bevel-subdiv-integration/`。严格UTF-8无BOM/LF、限定变更行格式及差异空白检查通过；全量两配置/DPI后置。
- 实施与审查请求Sol6.1/max被接受，内部映射未独立核验。新增验证脚本只检查明确文件，不全量格式化或转码。

### Notes

- `src/core/modeling/BevelEdge.h/.cpp`：新增单条闭合等距倒角CPU候选。
- `src/core/CMakeLists.txt`：接入倒角及后续细分核心。
- `src/editor/SceneViewModel.h/.cpp`：倒角事务、隐藏约束与冻结原边F9重算。
- `src/editor/operations/HistoryService.h/.cpp`：新增明确Bevel参数，不混淆double内插。
- `src/editor/operations/ObjectTransformSession.h/.cpp`：倒角距离/HUD/确认取消输入。
- `src/editor/workbench/LastOperationPanel.h/.cpp`：独立宽度字段及草稿隔离。
- `src/editor/MainWindow.cpp`、`src/editor/operations/OperatorRegistry.cpp`、`src/editor/operations/KeymapRouter.cpp`：真实菜单/快捷键/描述。
- `tests/BevelEdgeTests.cpp`、`tests/BevelEditorTests.cpp`、`tests/CMakeLists.txt`：算法与最终上层回归。
- `docs/v2-bevel-edge.md`、`docs/v2-development-plan.md`、`docs/blender-compatibility.md`、`README.md`：操作与真实组合限制/状态。
- `out/validation/v2-text-check.ps1`、`v2-format-changed.ps1`：本轮新增定向编码/差异行格式辅助；`v2-completion-verify.ps1`：追加真实Assets筛选验证入口。
- `progress.md`：仅追加。本轮和细分共同接线，联动回滚前稿为 `v2-before-bevel-20261002/`（editor/CMake）及 `v2-before-subdiv-20261002/`（Scene/Serializer/schema）。先另存后续修改再逐文件恢复，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-bevel-20261002/src/editor/operations/HistoryService.cpp' -Destination 'E:/Mini3D/src/editor/operations/HistoryService.cpp'`。新增模块/测试/说明逐个另存移出，不全仓重置；日志只追加回滚说明。

## 2026-10-02 - Task: P1-SUBDIV 固定细分链开发闭环

### What was done

- Catmull–Clark 1–2级四边网格/边界/接缝/平滑法线、组合来源映射及Scene不可变候选接通；源笼仍是唯一可编辑几何。
- 添加启用/层级/移除/应用卡片与Registry；单步唯一历史与保存重开。应用Mirror保留细分，应用细分烘焙最终链并删除两算子。
- 写modifiers数组、兼容旧单Mirror并拒绝歧义/顺序/重复/类型错误；OBJ、可见性、聚焦及外部新点吸附使用最终结果，组件自吸附排除细分求值源。

### Testing

- Release四类目标构建成功；算法联合14用例2601断言，Core9/986、Editor16/555、Assets10/162全部通过，日志同上一项最终接线目录。
- 包含4个新Scene用例、3个新Editor用例及1个新Picker用例；Cube26/24与98/96、固定链52/48、源面隐藏16三角、最终48面OBJ、Undo/文件重开、旧Mirror/FILE-01损坏夹具、级别字段及源点8选区通过。
- 独立Sol核心/稳定Scene链只读复核无发现；最终差异/范围格式/UTF-8无BOM/LF通过。一次读取尚未创建的BevelEditorTests报告缺路径，确认作者尚未写入后未重复查询，收到落盘消息后才读取；无文件损坏。新增Picker测试属性名在构建前按实际VertexSnapHit接口改为position，无领域API扩张。
- 统一Debug/Release全量/DPI/耐久后置，模型provider内部映射仍未核验，不宣称完整二期验收。

### Notes

- `src/core/modeling/Subdivision.h/.cpp`：细分Options、CPU求值和源面映射。
- `src/core/Scene.h/.cpp`：固定求值链、参数/应用快照与统一求值/来源访问。
- `src/core/SceneSerializer.cpp`：新数组与旧单字段兼容解析，保留upAxis。
- `src/core/ViewportVisibility.h`：组合来源映射驱动隐藏。
- `src/core/CMakeLists.txt`：注册核心。
- `src/editor/SceneViewModel.h/.cpp`：参数/应用唯一历史及最终OBJ选择。
- `src/editor/SubdivisionInspector.h/.cpp`、`src/editor/CMakeLists.txt`：真实细分卡片与构建。
- `src/editor/MainWindow.cpp`、`src/editor/operations/OperatorRegistry.cpp`：固定卡片链与菜单/搜索入口。
- `src/editor/operations/ComponentPicker.h/.cpp`、`src/renderer_gl/RayCaster.cpp`：最终点吸附、自集排除及求值包围盒。
- `tests/SubdivisionTests.cpp`、`tests/SubdivisionSceneTests.cpp`、`tests/SubdivisionEditorTests.cpp`、`tests/ComponentPickerTests.cpp`、`tests/CMakeLists.txt`：核心/Scene/UI/显示输出证据。
- `tests/Mirror02Tests.cpp`、`tests/FileCompatibilityTests.cpp`：沿新字段更新原损坏夹具，不放宽断言。
- `docs/v2-subdivision.md`、`docs/v2-scene-format.md`、`docs/scene-format-v3.schema.json`、`docs/v2-obj-export.md`、`docs/v2-development-plan.md`、`docs/blender-compatibility.md`、`README.md`：协议、链行为、组合限制和状态。
- `progress.md`：仅追加。本轮前稿 `v2-before-subdiv-20261002/`；公共editor接线与倒角联动回滚到 `v2-before-bevel-20261002/`，Picker/RayCaster原稿在 `v2-before-completion-20261002/`。先另存后续内容，逐文件恢复，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-subdiv-20261002/src/core/SceneSerializer.cpp' -Destination 'E:/Mini3D/src/core/SceneSerializer.cpp'`；新增细分文件逐个另存移出，日志只追加，不全仓重置。

## 2026-10-02 - Task: 独立审查发现的倒角隐藏结果F9缺口修复

### What was done

- 独立Sol复核上层发现：倒角确认保存后隐藏新面，旧F9只查原邻面，仍能改变隐藏结果并污染保存点。
- 补结果面可见性守卫，拒绝时不改变模型、参数、选区、历史或clean；揭示后可重新调整。

### Testing

- 新 `[bevel-hidden-result]` 回归修复前稳定失败：adjustLastBevel(.3)返回true，8断言中1失败；日志 `out/validation/v2-completion-bevel-hidden-repro/`。
- 根据实际失败在发布之前补新面检查；Release editor/editor tests构建成功，`[bevel-editor],[history-service],[last-operation-panel]` 14用例510断言通过，退出码0；日志 `v2-completion-bevel-hidden-fix/`。
- 限定差异行格式检查及UTF-8/差异检查通过；未重复CPU/细分已通过且未改动算法的验证。

### Notes

- `src/editor/SceneViewModel.cpp`：F9结果面隐藏原子拒绝。
- `tests/BevelEditorTests.cpp`：保存→隐藏新面→失败状态不变→揭示成功回归。
- `docs/v2-bevel-edge.md`：说明隐藏结果及新增实际证据。
- `progress.md`：仅追加。回滚点 `out/validation/v2-before-bevel-hidden-20261002/`；先另存后续差异，再恢复本补丁，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-bevel-hidden-20261002/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`，日志只追加回滚说明，不全仓重置。

## 2026-10-02 - Task: P1-COLLECTION 单层集合开发闭环

### What was done

- 接通独立单层集合、稳定编号/高水位、对象单归属及祖先集合隐藏传播；不改变对象树或TRS，删集合保留对象。
- 大纲成员列表、新建/中文名称/显隐/移入移出、成员单对象选择、同一撤销栈接通；复制与子树删除/恢复带归属增量，不覆盖无关集合改动。
- 格式3保存集合并严格验证身份/成员，旧版本缺省或空集合可读；坏文件加载保留活动组件预览、源、选区与保存点。
- 独立审查发现仅增减名称空格的no-op缺少刷新，会使后续勾选被当重命名；稳定复现后补item信号结束的排队同步。

### Testing

- Release editor/core/editor tests构建成功。Core `[collections],[scene],[serializer],[file-compatibility]` 17用例605断言通过；初始Editor13/1041，修复后的同组13用例1042断言通过，退出码0。
- 4个新增CPU及4个新增Editor用例，包含真实中文保存重开、历史clean、层级/世界位置、ID耗尽、成员引用、删除/恢复/复制、窗口新建/成员点击/显隐/移入移出与坏文件预览保护。
- 修复前 `[collections-editor]` 4用例中1失败（87断言中1失败），明确证明等价名称未同步；最终相关文档/历史回归通过，不放宽断言。日志 `out/validation/v2-completion-collection/`、`v2-completion-collection-name-repro/`、`v2-completion-collection-name-fix/`。
- 独立Sol审查其余跨模块契约无新增可证实缺陷；严格UTF-8无BOM/LF、限定格式、JSON结构解析和git diff空白检查通过。一次错读不存在的schema名称改查实际scene-format-v3.schema.json；一次提前读尚未落盘CPU测试，收到作者释放后再读取；均无写入损坏。
- 全量Debug/Release/DPI后置；代理启动接受约定型号/强度，provider内部映射仍未核验。

### Notes

- `src/core/SceneCollection.h`：新增独立集合数据与单归属约束。
- `src/core/Scene.h/.cpp`：集合验证、高水位、可见性及子树成员增量回放。
- `src/core/SceneSerializer.h/.cpp`：顶层集合结构、旧版边界和原子严格读取。
- `src/assets/SceneDocument.cpp`：实际文件集合透传。
- `src/core/CMakeLists.txt`：登记SceneCollection头。
- `src/editor/CollectionPanel.h/.cpp`：真实集合/成员视图、按钮及安全排队同步。
- `src/editor/SceneViewModel.h/.cpp`：集合原子快照进入唯一历史。
- `src/editor/MainWindow.cpp`、`src/editor/CMakeLists.txt`、`src/editor/operations/OperatorRegistry.cpp`：大纲、新建菜单与F3入口。
- `tests/CollectionTests.cpp`、`tests/CollectionEditorTests.cpp`、`tests/CMakeLists.txt`：CPU/实际文件/窗口回归与登记。
- `docs/v2-collections.md`、`docs/v2-scene-format.md`、`docs/scene-format-v3.schema.json`、`docs/v2-development-plan.md`、`docs/blender-compatibility.md`、`README.md`：操作、协议与真实开发状态。
- `progress.md`：仅追加。回滚点 `out/validation/v2-before-collection-20261002/`；先另存后续修改，逐文件恢复，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-collection-20261002/src/core/Scene.cpp' -Destination 'E:/Mini3D/src/core/Scene.cpp'`。新增模块/测试/说明逐个另存移出；核心CMake只撤回SceneCollection.h登记，不能恢复更早稿而删除已完成倒角/细分登记。集合Panel前稿包含未登记草稿，回滚时一并移出新增Panel并撤回登记；不全仓重置，日志只追加回滚说明。

## 2026-10-02 - Task: P1-UI 真实饼菜单、着色和Repeat开发闭环

### What was done

- Z/反引号环形饼菜单、扇区鼠标/数字键、取消和中文禁用原因接通同一Registry；冻结上下文，执行前重新校验，不抢文本输入。
- 视口材质/中性实体/真实线框共用Header/菜单/F3/Z；仅改变窗口显示，线框后恢复填充，不改材质、几何、选区、历史或文件。
- Shift+R重复历史末端挤出/内插/倒角的最新参数，在当前源/对象/选区新执行并新增一条历史；F9仍从原before替换原after，超限新尝试完整取消。

### Testing

- Release editor/editor tests/GPU目标构建成功。最终Editor `[p1-ui],[history-service],[last-operation-panel],[keymap],[operator-search]` 25用例672断言通过；GPU `[shading-gpu],[component-overlay]` 2用例74断言通过，退出码0。
- 新7个编辑器用例及1个GPU用例覆盖实际菜单/快捷键/鼠标、过期与文本保护、连续新历史/新目标/超限原子拒绝、保存点与F9、真实像素及GL_FILL恢复。五张真实截图已查看，目录 `out/validation/v2-completion-p1-ui-final/screens/`。
- 初次失败仅旧Registry库存69与实际85不符，核对静态表后更新并保留唯一性/真实Action及补6个新ID断言；第二次GPU失败是Core Profile统一模式查询被误当兼容模式的第二槽，按实际驱动行为修正，并预先置GL_LINE增强恢复检查。失败日志 `v2-completion-p1-ui/`、`v2-completion-p1-ui-final/`；最终GPU日志 `v2-completion-p1-ui-gpu-coremode/`。
- 独立Sol只读审查Repeat/F9、Popup生命周期、输入与显示状态无新增可操作发现；上下文重建仅静态核对，首次GL初始化前设模式有实测。限定格式、UTF-8无BOM/LF及差异检查通过。Debug/Release全量和DPI留QA；provider内部模型映射未核验。

### Notes

- `src/renderer_gl/ViewportShading.h`、`Renderer.h/.cpp`、`ViewportWidget.h/.cpp`：三种真实会话显示与初始化回放。
- `src/editor/workbench/OperatorPiePopup.h/.cpp`：环形绘制/命中、上下文校验和取消。
- `src/editor/SceneViewModel.h/.cpp`：当前源Repeat事务，不复用F9闭包。
- `src/editor/MainWindow.cpp`、`src/editor/workbench/WorkbenchShell.cpp`、`src/editor/operations/OperatorRegistry.cpp`、`KeymapRouter.cpp`、`src/editor/CMakeLists.txt`：菜单/Header/快捷键与Registry接线。
- `tests/P1UiEditorTests.cpp`、`tests/GpuTextureTests.cpp`、`tests/OperatorSearchTests.cpp`、`tests/CMakeLists.txt`：操作/显示/原子性回归和库存修正。
- `docs/v2-pie-repeat.md`、`docs/v2-development-plan.md`、`docs/blender-compatibility.md`、`README.md`：真实使用范围、Repeat/F9差别和本项状态。
- `out/validation/v2-completion-verify.ps1`：增加可选GPU筛选参数；`progress.md`仅追加。回滚点 `out/validation/v2-before-ui-20261002/`，库存测试前稿另存于 `v2-before-ui-registry-20261002/`。先保存后续修改再逐文件恢复，例如 `Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-ui-20261002/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`；新增Popup/显示枚举/测试/说明逐个另存移出，不能用不完整Renderer快照覆盖整目录；日志仅追加回滚说明，不全仓重置。

## 2026-10-02 - Task: Git启动失败空窗口的限时隐藏兜底

### What was done

- 根据用户指定标题与错误命令定位WindowsTerminal失败窗口；暂缓开发测量，保留正常终端和共享后台。
- 新增精确标题/进程/单标签/错误正文四重校验，只发送窗口关闭，不批量结束Git或Terminal进程。
- 启动120分钟隐藏兜底，每5秒检查，提供状态、关闭结果日志及停止文件；不安装自启或改全局配置。

### Testing

- 首次发现21个Git标题窗口，均归属WindowsTerminal PID32460，无运行中的git.exe；随后只读复查仅余7个正常/当前终端窗口，21个目标已消失，本轮当时未关闭任何窗口。
- 最初按标题读UIA两次未找到目标，改为重新枚举全部终端确认外部状态变化；不把两次找不到写成已成功关闭。
- PowerShell7.6.6、脚本解析、7项匹配规则自检、一次关闭模式扫描均通过；扫描目标0/关闭0。隐藏守护PID37012启动参数CreateNoWindow=true/WindowStyle=Hidden，MainWindowHandle=0，状态持续更新。
- 当前没有真实错误窗口，真实WM_CLOSE路径尚未实测，后续以events.jsonl为准；不宣称底层启动错误根因已修复。

### Notes

- `tools/Close-FailedGitWindows.ps1`：精确匹配、安全窗口关闭、自检和限时守护。
- `docs/failed-git-window-guard.md`：保护范围、真实诊断、使用、停止及验证限制。
- `out/validation/git-window-inspect.ps1`、`git-window-uia-inspect.ps1`、`start-git-window-guard.ps1`：本轮枚举、读取与隐藏启动证据；`out/validation/git-window-guard-20261002/`：实际运行日志，不进入发布包。
- `progress.md`：仅末尾追加。回滚先执行 `New-Item -ItemType File -Path 'E:/Mini3D/out/validation/git-window-guard-20261002/stop.request'`，等state.json中running=false；先备份后将新增脚本/说明逐个移出，保留日志并追加回滚记录。不结束旧PID、不重置仓库。

## 2026-10-02 - Task: Git空窗口兜底增加当前会话不可关闭规则

### What was done

- 先通过停止文件退出旧守护，新增当前会话窗口硬保护后再隐藏启动新守护；本轮不推进其他二期开发。
- 显式固定当前Mini3D会话句柄593240，并保存启动时的控制台、前台及正常Terminal窗口句柄；标题后来改变也保留，保护优先于完整错误匹配。
- 扫描与发送WM_CLOSE前再次跳过前台窗口，发送前尊重停止请求；不结束Git、共享Terminal后台或当前CLI。

### Testing

- PowerShell7.6.6脚本解析、UTF-8无BOM/LF检查、7项错误匹配及4项保护自检通过；保护测试使用完整相同错误正文，验证当前/前台窗口仍不能关闭。
- 关闭模式扫描目标0、关闭0，state.json包含当前句柄；实际枚举确认标题为“研读方案并搭建C++ 3D基础环境 | Mini3D”的会话窗口存在，进程WindowsTerminal PID41792。
- 旧守护running=false；新隐藏守护PID37564，CreateNoWindow=true、WindowStyle=Hidden、MainWindowHandle=0，状态持续更新且当前会话仍存在。新日志目录out/validation/git-window-guard-protected-20261002/，截至核对closedTotal=0；限时120分钟，每5秒检查，预计北京时间2026-10-03 00:05:40退出。
- 初次编码验证两次误报：查实际首字节为23、LF且无BOM，定位为文化敏感StartsWith把U+FEFF当可忽略字符；改用Ordinal后通过。中途仅对本轮四个文件运行dos2unix，最终差异确认没有内容扩散或全仓转码。
- 没有真实错误窗口，WM_CLOSE成功关闭仍未实测，不能声称根因修复或已关闭先前21个窗口。源脚本、说明与前稿差异已人工检查。

### Notes

- `tools/Close-FailedGitWindows.ps1`：新增固定受保护句柄、前台双重检查、停止请求关闭门禁及4项保护自检。
- `docs/failed-git-window-guard.md`：说明当前窗口不可关闭、当前运行目录、停止方式、实测结果与限制。
- `out/validation/start-git-window-guard.ps1`：向隐藏守护显式传递当前会话句柄，并切换新日志目录。
- `out/validation/check-git-window-protection.ps1`：本次会话的解析/编码/保护及隐藏启动验证；其中句柄不得沿用到后续会话。
- `out/validation/git-window-guard-20261002/stop.request`：退出旧守护的请求；`git-window-protection-scan-20261002/`及`git-window-guard-protected-20261002/`保存真实扫描/运行证据，不进入发布包。
- `progress.md`：仅末尾追加。回滚点为`out/validation/git-window-before-current-protection-20261002/`；先执行`New-Item -ItemType File -Path 'E:/Mini3D/out/validation/git-window-guard-protected-20261002/stop.request'`并确认running=false，再逐文件恢复，例如`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/git-window-before-current-protection-20261002/Close-FailedGitWindows.ps1' -Destination 'E:/Mini3D/tools/Close-FailedGitWindows.ps1'`。说明与启动脚本同样从该快照对应文件恢复，新增检查脚本另存移出；保留日志。旧稿不具本轮硬保护，不得自行重启；不强杀旧PID、不全仓重置。

## 2026-10-02 - Task: PERF-01真实编辑网格优化、三档测量与规模提示

### What was done

- 复用可见性屏幕桶、覆盖层位置索引与排序边、流形面角DSU和逐面派生scratch；保持遮挡、绘制顺序、隐藏和原拒绝语义。
- 普通组件变换由Scene内部完成完整校验并复用本次派生，公开入口不接收外部伪造缓存；无变化仍完整校验，修改器和比例/夹持路径保持。
- 独立进程测量1万/5万/10万源点真实MainWindow/GL路径。1万达预算；更大网格超预算，加入常驻规模提示，不强行限制模型或降级正确性。
- 正式交付完整JSON、基线、哈希及性能说明，同步F1手册说明；本项完成后进入QA，二期不标整体ACCEPTED。

### Testing

- 最终Release构建Core/editor/Assets/GPU/probe成功。核心定向40用例7254断言、编辑器18/791、拾取9/145；规模提示/工作台/覆盖层6/124均通过。日志：out/validation/v2-completion-perf-single-derive-fixed/、v2-completion-perf-scale-hint/。
- 含最终规模提示构建：每档各5项20完整样本、2次预热，无截断；源哈希/选区/历史取消后不变，框选独立预期ID集精确一致。1万预览API/反馈P95=36.1166/38.5911ms，5万=222.5662/232.4013ms，10万=495.5441/514.7837ms；累计峰值167.09/283.14/426.36MiB。
- 1万返回0并通过全部预算；5万/10万返回3，明确为测量成功但超预算，不是通过或崩溃。保留所有前轮失败/未达预算报告，没有重复无变化的失败方法。正式原始数据在docs/performance/v2/，探针EXE SHA256=C6FC843E9D8ED31434982444E10FD248A261E3B9CEBDBC33E399BF9BDA5E8A92。
- 两轮独立Sol/max静态审查：旧DSU/scratch/Overlay与新增single-derive私有可信路径无可操作P0–P2。请求配置获工具接受，provider内部映射未验证。
- 整合初轮新增QA截图辅助函数const与QWidget::grab不兼容，已根据C2662修正后构建通过，原失败日志保留。首次整文件clang-format检查包含已有格式差异；改为仅本项增量行格式化与复验，UTF-8无BOM/LF保留。一次前稿读取误用根层Scene.cpp，按实际子目录定位后未重复错误路径。
- 验证限规则平面四边格、点选择、无修改器/比例编辑；未把1万预算外推到全部操作或其他硬件，Debug全量与QA仍待下一项。

### Notes

- `src/editor/operations/ComponentPicker.cpp`：可见性单次桶及裁剪查询快速路径。
- `src/editor/operations/ComponentInteraction.cpp`：位置索引、单次边收集和选择/隐藏传播，保留绘制顺序。
- `src/core/modeling/MeshValidation.cpp`：面角DSU单扇检查和容量复用。
- `src/core/modeling/MeshDerivation.cpp`：逐面派生scratch复用，保留耳切与来源映射。
- `src/core/modeling/VertexTransform.h`、`src/core/modeling/VertexTransform.cpp`：返回刚完成的完整派生候选。
- `src/core/Scene.h`、`src/core/Scene.cpp`：内部变换工厂与私有准备helper；普通入口继续完整校验。
- `src/editor/SceneViewModel.cpp`：普通组件预览消费可信工厂，候选/选择/信号/历史语义保持。
- `src/editor/MainWindow.cpp`：Edit大于1万源点的常驻提示，预留中文行高，不依赖N面板。
- `tests/ComponentPickerPerformanceTests.cpp`：查询完整预期ID与性能夹具回归。
- `tests/ComponentBoxTests.cpp`：稀疏ID位置与原绘制语义独立像素回归。
- `tests/VertexTransformTests.cpp`、`tests/EditableSceneTests.cpp`：新旧派生逐字段等价、修改器、无变化和失败原子性。
- `tests/WorkbenchTests.cpp`：规模提示显隐与历史/脏状态不变。
- `tools/EditablePerformanceProbe.cpp`：真实编辑/选择/换帧、取消正确性、样本/内存/机器证据。
- `tools/CMakeLists.txt`：可选EXCLUDE_FROM_ALL编辑网格探针目标。
- `docs/v2-performance.md`、`docs/v2-development-plan.md`、`docs/Mini3D_使用手册.html`：真实数值、支持范围与提示说明。
- `docs/performance/v2/baseline-vertices-10000.json`、`vertices-10000.json`、`vertices-50000.json`、`vertices-100000.json`、`manifest.json`：完整正式原始证据及哈希。
- `out/validation/v2-perf-run.ps1`、`v2-perf-delivery-run.ps1`、`v2-collect-performance.ps1`、`v2-format-increment.ps1`、`v2-final-snapshot.ps1`与各v2-completion-perf-*目录：本机执行/前稿/测量证据，不进入候选包。
- `progress.md`：只追加。回滚先另存后续修改；本项旧源从out/validation/v2-before-perf-20261002/按相对路径恢复，Core算法从v2-before-perf-kernel-20261002/恢复；single-derive七文件可用v2-before-perf-single-derive-20261002/恢复。例如`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-perf-kernel-20261002/src/core/modeling/MeshValidation.cpp' -Destination 'E:/Mini3D/src/core/modeling/MeshValidation.cpp'`。提示/最终文档从v2-before-final-handoff-20261002/恢复对应文件，新增探针/报告逐个另存移出；不得回退其他二期功能、覆盖历史日志或全仓重置。

## 2026-10-02 - Task: QA全量暴露的模态析构、比例圈与场景树回归修复

### What was done

- 在主窗口子控件拆除前取消活动对象/组件事务，避免退出或异常展开时向正在拆除的状态栏回调；候选不提交、历史不增加。
- 二维比例影响圈不再继承三维深度检查与背面剔除，绘制后恢复原状态；开关、半径与取消仍按原快照重算。
- 树交互测试适配真实可见区域：先滚动并验证命中，再点击；根层拖放前展开区域并验证目标是真空白。不改变对象树的业务拒绝规则。

### Testing

- 首次Debug全量失败保存于out/validation/v2-completion-final-Debug/；比例单用例稳定复现帧相等和SIGSEGV，CDB栈确认MainWindow状态回调→clearSession→组件取消→窗口拆除。
- 新[modal-lifecycle]直接销毁活动比例变换，13断言通过；原O/滚轮用例在修复绘制状态后25断言通过，实际前后PNG已查看，修复前无圈、修复后虚线圈可见。
- 两条SceneEditor单用例均先复现；坐标证据显示树高54、far行y84，以及所谓空白点实际命中实体2。修正夹具后原种子3844122194综合6用例/164断言通过。证据：out/validation/v2-debug-qa-interactions-fixed-20261002/。
- 五文件仅增量格式化、严格UTF-8无BOM/LF及差异检查通过。一次补丁使用不存在的include锚点被拒，无文件写入；依据实际include一次纠正。编码预检最初用文化相关StartsWith把零宽BOM误识为前缀，已用字节证据更正，未做转码。
- 最終Debug/Release全量、三档最终构建配方与候选包尚待后续，不将此定向结果当整体通过。

### Notes

- `src/editor/MainWindow.h`：声明显式析构及子控件清理边界。
- `src/editor/MainWindow.cpp`：析构时同步取消未确认事务。
- `src/renderer_gl/ViewportWidget.cpp`：比例圈绘制期间关闭并恢复深度检查和背面剔除。
- `tests/ProportionalEditorTests.cpp`：增加活动窗口直接析构回归、帧/控件诊断及可选PNG证据。
- `tests/SceneEditorTests.cpp`：真实行命中和空白根层夹具断言。
- `docs/v2-acceptance.md`：记录已确认根因与定向证据，纠正外壳循环/保存重开的次数描述。
- `out/validation/v2-before-qa-lifecycle-20261002/`、`v2-lifecycle-preflight.ps1`、`v2-format-lifecycle.ps1`、`v2-debug-isolate.ps1`及相关v2-debug-*目录：独立前稿、本机执行和失败/通过证据，不进入包。
- `progress.md`：只追加。本项回滚先另存后续修改，再从out/validation/v2-before-qa-lifecycle-20261002/逐个恢复五文件，例如`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-qa-lifecycle-20261002/src/editor/MainWindow.cpp' -Destination 'E:/Mini3D/src/editor/MainWindow.cpp'`；文档只移除本项新增节并还原一句循环描述，不覆盖后续QA结果或历史日志，不全仓重置。

## 2026-10-03 - Task: QA-01最终两配置、三档配方、原生作品与离线手册闭环

### What was done

- 补齐S01–S12真实Qt操作配方与逐项截图/尺寸/DPR/源OBJ哈希，串行执行最终构建三档复验；三件可继续编辑的原生作品与五份OBJ正式落地。
- 使用隔离配置的本机Blender5.1.1实际导入五OBJ、重建Mirror和二级有限Catmull–Clark；连接、绕序与位置核对，不冒充参考4.5或全功能等价。
- 完成最终Debug/Release全量，并归档构建、原始分组日志、源文件/二进制SHA256；修复失败证据和以前未定位的偶发记录保留。
- 同步二期计划、README、兼容性、原生样例来源及30章34图离线手册，明确开发完成/自动通过/性能未达/人工缺口之间的区别。

### Testing

- 最终Debug/Release完整构建及CTest均4/4通过：Debug编辑器160.56秒、Release47.61秒。Release完整统计Core160/25164、Assets30/524、GPU4/132、Editor220/8315（用例/断言）；日志在out/validation/v2-completion-final-{Debug,Release}-fixed/和docs/validation/v2/final/regression/。
- 最终Qt实际DPR1/1.5/2各8用例1739断言、完整S01–S12通过，三档测试EXE SHA256为245CE43FC024F251BFC787862C0176F6219FF1CB30840D72851886862A6FF19E。3场景/5OBJ与正式样例逐字节一致，不冒用旧binary运行证据。
- Blender5.1.1/b70da489d7f4：5OBJ位置误差0、Mirror12V10F误差0、Mirror→二级细分162V160F最大误差7.300048565639372e-08，小于1e-5；完整面环/绕序一致。关闭limit surface、边界ALL，不证明UV/材质/默认极限曲面。半壳来自公开Cube夹具，保存/重开为VM API，不冒充Bisect/文件对话框。
- 最终探针三档各20完整样本、2预热，选区/取消/源hash/历史正确性通过，但三档均返回3超预算：1万API/反馈P95=55.0115/58.2539ms，5万225.2972/234.4144ms，10万571.8501/594.138ms。旧1万36.12/38.59ms不删除，不以重试挑好结果，不猜测波动根因；最终probe SHA256=EF5AD5D6C308EC37C3F777E30655C71118624A13132C9D1058A4CB4EE09E2033。
- 最终离线页面检查通过：Edge154.0.4258.48，30章34图、1440/1024/768/390/320五宽度、目录检索/跳转、34图放大/Esc/焦点归还、打印、图片/锚点/尺寸/哈希，无脚本错误/远程请求。实际查看1440/320页面截图。
- 独立Sol/max对析构与QPainter修复只读复核无可操作P0–P2；文档实施Sol/max完成四文件，主代理复核差异。工具接受指定配置，provider实际模型映射未验证，不把角色名当证明。
- 首轮F3/Q配方漏计Undo导致count断言失败，依据实际[inset,history.undo,inset]修正为3后全部通过。CTest共享LastTest在Release完成时覆盖Debug，误放副本已按内容移至Release目录；Debug只归档可信分组摘要，不伪造详细日志。
- 编码、增量格式、差异及文档112本地链接、五新图与原图SHA、八作品清单逐项检查通过。UTF-8无BOM/LF保留，未全仓转码或格式化。完整人工IME、Windows原生/mixed-DPI、新机器、人工30分钟/发布许可仍未验证，二期不标整体ACCEPTED。

### Notes

- `tests/V2AcceptanceTests.cpp`：S01–S12、F1及三原生作品的实际操作/报告/截图，补F3/Q执行次数。
- `tools/Run-V2Acceptance.ps1`：三档串行运行、绝对DPR/配方/源OBJ/构建hash校验和环境恢复。
- `tools/Compare-V2Blender.py`：隔离Blender导入及Mirror/有限级细分的真实数值比较。
- `assets/scenes/v2/shell.m3dscene`、`symmetric.m3dscene`、`subdivision.m3dscene`：外壳、对称、细分原生保存作品。
- `assets/scenes/v2/shell-source.obj`、`symmetric-source.obj`、`symmetric-evaluated.obj`、`subdivision-source.obj`、`subdivision-evaluated.obj`：三作品源/求值输出。
- `assets/scenes/v2/manifest.json`：逐文件来源和SHA256；本輪最終輸出与已交付文件一致，未覆盖素材。
- `docs/v2-acceptance.md`：实际配方、数值复核、回归/构建来源与未验证边界。
- `docs/v2-development-plan.md`：P0/P1与QA-01本机状态、真实性能缺口和S表更新。
- `docs/v2-performance.md`、`docs/performance/v2/delivery/*.json`：最终三档报告和独立manifest，旧基线/优化结果原样保留；每个JSON路径/哈希逐项见delivery/manifest.json。
- `docs/blender-compatibility.md`、`docs/v2-obj-export.md`：已实现的支持边界、Blender版本和实际数值复核。
- `README.md`、`docs/package-readme.md`：功能/使用规模/候选状态；纠正包说明Ctrl+D复制、小键盘句点聚焦。
- `docs/sample-assets.md`：三原生作品和五OBJ生成来源、使用及许可边界。
- `docs/user-guide-sources.md`：原29图历史保留，图30–34真实来源/哈希与最终页面证据。
- `docs/Mini3D_使用手册.html`：30章34图、最终功能/性能/原生作品/人工验收边界，无新增远程资源。
- `docs/validation/v2/`及`final/`：原和最终三档逐项JSON/PNG/场景/OBJ/日志、Blender、回归及页面报告；完整逐文件清单见各manifest.json，正式证据不替换旧失败。
- `out/validation/v2-before-qa-recipes-20261002/`、`v2-before-delivery-docs-20261002/`、`v2-before-final-handoff-20261002/`及本轮v2-completion-*、v2-run-delivery-evidence.ps1、v2-perf-delivery-confirm.ps1、v2-record-final-regression.ps1：前稿和本机原始执行证据，不进入包。
- `progress.md`：只追加。回滚先保留后续修改；配方两文件从v2-before-qa-recipes-20261002/恢复，四份说明从v2-before-delivery-docs-20261002/按相对路径恢复，例如`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-delivery-docs-20261002/README.md' -Destination 'E:/Mini3D/README.md'`；其余已有文件从v2-before-final-handoff-20261002/恢复对应文件。新增正式报告/作品逐个另存移出，保留原始资料、日志和此前二期实现，不全仓重置。长期goal面板仍为paused且工具无恢复接口，本轮依用户恢复授权实际继续执行，不伪报面板状态。

## 2026-10-03 - Task: QA-02实际二期ZIP隔离验收与候选交付收尾

### What was done

- 独立生成二期Windows x64候选ZIP，包含当前Release程序、运行依赖、三原生作品/五OBJ、离线手册、兼容性、性能及已完成回归证据；未覆盖一期包、媒体或用户原始资料。
- 从实际ZIP解压再迁移到带空格的新目录，净PATH、不同工作目录验证程序/场景/依赖；最终报告外置并绑定ZIP哈希，不为加入报告而重打已测档案。
- 修复包验收器的真实日志通道问题：Qt GUI强制stderr，按本机已确认ANSI936读取中文帮助路径，日志保存UTF-8；路径越界检查保持严格，不改生产程序。
- 更新当前开发与候选交付状态：所有获授权P0/P1开发和主要本机自动验收已完成，但性能预算及人工/实机/许可仍缺，不标整体ACCEPTED，不提交、推送或发布。

### Testing

- `& './out/validation/v2-package-script-check-20261003.ps1'`：本轮两脚本AST、严格UTF-8无BOM/LF、前稿增量与空白检查通过；Git行尾提示没有引发转码或全量格式化。
- 首轮实际ZIP失败保留于out/validation/v2-completion-actual-zip/：demo退出0、两张PNG存在，但日志仅2字节，无帮助行。一次诊断强制stderr后取得449字节原始日志，按ANSI936正确解码得到包内帮助路径；不将其误归因为帮助文件实际越界。
- `& './out/validation/v2-test-delivery-zip.ps1'`修复后退出0：新目录out/validation/v2-completion-actual-zip-fixed-20261003/；demo/showcase/shell/symmetric/subdivision各退出0并生成视口与工作台截图，missing退出4且无正常渲染截图；全部manifest哈希、手册/34图引用、包内帮助路径和11项包内运行依赖通过。已实际查看三原生作品工作台PNG。
- ZIP大小45,119,445字节、SHA256=547020BEAA707816E0F4824334B7D6ED5767C393BA1FCF093AA651F31E97D589；包中主程序SHA256=74B61866E5F9F853B4FAABB65E79E56A21900D39D4560AA7BB5CCBC619CEBA45，与最终回归一致。验证工具SHA256=6F1DBCB2C163CE59BAE4D7D832952A755C5EA495A6FD34FC7C81E6E2D455969C；正式报告和ZIP旁报告逐字节一致。
- `& './out/validation/v2-package-final-check-20261003.ps1'`：目标文件严格UTF-8无BOM/LF、工具AST、精确差异、144项本地链接、19个正式包证据文件哈希/长度、ZIP/最终回归主程序/验证工具一致性通过；progress历史前缀逐字节保持。生产源码本轮未再改动，不无收益重跑此前已通过的两配置完整回归或性能探针。
- 本机隔离验证不等于新机器；包烟测没有再次模拟F1或打开外部浏览器，F1操作由此前三档配方证明。真实性能预算、中文IME、Windows原生/mixed-DPI、参考Blender4.5、30分钟人工耐久、正式许可仍未全部通过。
- 未关闭任何用户窗口，测试进程使用CreateNoWindow/Hidden；守护最后state为running=false，closedTotal=0，不夸称清除过真实错误窗、不用过期PID执行关闭。

### Notes

- `tools/Package-Release.ps1`：二期材料前置检查、离线手册/专项文档/性能/回归材料入包；本项实际生成ZIP并检查语法，此前二期增量保留。
- `tools/Test-Package.ps1`：Qt GUI stderr日志和中文编码修复，保留净PATH、路径/依赖及场景拒绝检查。
- `README.md`：本机候选包与正式外部报告入口，明确功能完成与未通过验收的区别。
- `docs/package-readme.md`：仓库当前包验收补充、文档快照边界与维护者复验命令。
- `docs/v2-development-plan.md`：QA-02本机候选交付状态，人工和性能缺口仍保留。
- `docs/v2-acceptance.md`：实际ZIP方法、哈希、失败原因、修复证据和未验收范围。
- `docs/user-guide-sources.md`：来源登记同步实际ZIP已通过本机隔离，保留历史图源与包内快照。
- `docs/validation/v2/final/package/report.json`：本机候选ZIP正式报告；`manifest.json`：19个旁列证据的长度和SHA256。
- `docs/validation/v2/final/package/test-package.log`：验收器结果；`modules.json`：真实模块加载路径。
- `docs/validation/v2/final/package/demo.log`、`scene.log`、`shell.log`、`symmetric.log`、`subdivision.log`、`missing.log`：各场景日志和缺失文件拒绝证据。
- `docs/validation/v2/final/package/demo.png`、`demo-ui.png`、`scene.png`、`scene-ui.png`、`shell.png`、`shell-ui.png`、`symmetric.png`、`symmetric-ui.png`、`subdivision.png`、`subdivision-ui.png`：实际迁移包视口/工作台原图，不是手绘或旧构建截图。
- `out/packages/Mini3D-v2-candidate-windows-x64-20261002.zip`：实际已测候选档案；同名目录：部署stage，仅用于生成；同名`.validation.json`：不可变ZIP对应的外部验收结果，不进入Git或发布渠道。
- `out/validation/v2-test-delivery-zip.ps1`：实际ZIP解压/迁移与外部报告生成；`v2-package-log-diagnostic.ps1`：单次原始日志诊断；`v2-package-final-preflight-20261003.ps1`、`v2-package-script-check-20261003.ps1`、`v2-package-final-check-20261003.ps1`：独立前稿、编码/语法/差异及最终一致性检查。各原始失败/通过目录原样保留。
- `progress.md`：只追加。回滚先另存后续修改，再从out/validation/v2-before-package-final-20261003/按相对路径恢复本项既有文件，例如`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/v2-before-package-final-20261003/tools/Test-Package.ps1' -Destination 'E:/Mini3D/tools/Test-Package.ps1'`；Package-Release前稿为out/validation/v2-before-package-final-20261002/Package-Release.ps1。新正式报告/包产物逐个另存移出，不递归删除、不覆盖一期/源资料/旧证据，progress保留历史并追加回滚说明。长期goal面板仍paused且无恢复接口，实际按用户恢复授权接续，不伪报面板恢复或整体目标完成。

## 2026-10-03 - Task: PERF-01性能收尾与三档原预算闭环

### What was done

- 完成真实编辑器选择、拾取、覆盖层、普通组件变换和同步UI热点优化，不改变既有选择/隐藏/活动项、几何拒绝、修改器或历史规则，不新增后台线程或降低预算。
- 普通变换只从Scene可信不可变快照增量派生，重算受影响面的动态几何和耳切；旧公开EditableMesh入口保留完整校验，Scene同址重载拒绝旧来源快照。
- 删除菜单改为只读投影非空判断，工作台点/面直接计数；覆盖层复用精确匹配的拓扑与已算选中标记，编辑枢轴不重复计算质心。
- 当前Release的1万/5万/10万源点均通过原50ms API / 100ms换帧P95预算，正式新档案绑定源码/探针/主程序与最终回归；本轮各失败报告独立保留。
- 同步性能、计划、验收、README及离线手册，明确达标只覆盖本机规则网格夹具，旧ZIP未包含本轮优化，人工/实机/许可仍缺，不宣布整体ACCEPTED或发布。

### Testing

- 先保存30个相关前稿及编码/SHA256，WorkbenchShell另存独立前稿；所有本轮源码仅增量clang-format，严格UTF-8无BOM/LF保持，未全库转码或格式化。
- 旧代码独立1万点baseline返回3，preview API/反馈P95=64.8784/67.9461ms；100k CPU诊断揭示全量校验/派生热点，诊断字段不用于验收，完整正式路径仍返回3。可选`--diagnostics`不改默认预算、完整ID校验或样本数量。
- 第一阶段100k真实20/2测量返回3：点击达标，可见框选87.2694ms、X-Ray框选167.8396ms、preview API119.1175ms、反馈154.0564ms。去掉UI集合重建后preview39.4617ms/反馈55.353ms通过，但可见框选51.3699ms仍未通过；这些报告均保留，不无改动重测挑结果。
- 最终三档同一探针、独立进程、串行20完整样本/2预热，全部实际退出0、无截断。可见点击/框选、preview API/反馈P95分别：1万2.2499/3.9682、6.5525/9.7727ms；5万11.4369/24.0047、26.8475/36.8795ms；10万20.8131/45.5482、36.7328/52.8792ms。X-Ray点击/框选与begin均≤50ms，准确预期ID、源hash、单点选区、history count/index/clean恢复通过。
- 最终统一Release构建及定向回归通过：Core39用例12870断言、Picker16/397、Editor24/1003、工作台/删除/枢轴UI16/544。`perf-closeout-verify-20261003.ps1 -Batch overlay-center-fixed-20261003`与`perf-closeout-ui-verify-20261003.ps1`日志独立保留。
- 最终Release完整CTest4/4通过：Core174/33524、Assets33/679、GPU4/132、Editor221/8394；Editor44.52秒，完整45.56秒。实际源/二进制哈希与LastTest日志在正式regression目录，未与第一阶段或旧Debug/DPI日志混用。
- 原始像素参考对照覆盖隐藏/恢复、预览/取消、稀疏重排/undo/redo与同ID面角引用置换；加强为真正发生位置变化的+Z活动面后单项1/79通过，最终全量亦包含该用例。单扇/退化/非共面/耳切、旧与新变换等价、错误/no-op入口、同址重载来源token和失败原子性均有针对性回归。
- 两位独立Sol/max分别静态复核非本人实施的Core/Overlay与选择/ROI拾取，发现的来源token P2已修复并重新验证，无未解决P0–P2。工具接受指定路由，但provider内部模型/推理映射无法独立核实；主会话不伪称已自动换型。
- 初版诊断探针误用const Scene、首轮新像素测试误用const undoStack，依据实际错误改为独立诊断Scene与公开VM undo/redo；失败日志保留。格式变化导致补丁锚点失配时原子拒绝未写文件，随后先读实际段再精确修改；不存在的研究文件路径也未据此编造证据。
- 正式归档脚本退出0，三档完整契约、测量源码hash、当前probe/binary和最终Release回归来源一致；23个旁列证据记录长度与SHA256。本轮最终资料一致性检查入口为`out/validation/perf-closeout-final-check-20261003.ps1`，结果使用独立同名目录，不进行新的性能/UI测量。
- 新主程序SHA256=`0B55C152DC9F230D217F3B42658B30C315FF2DF52A59200ABB0375709DC7E69A`，探针=`333ABD6157D51423274240F15B82D22249B27117E149D59C2B9F53B641952D1A`。未新增Debug/DPI、复杂拓扑/大范围变形/修改器性能、IME/mixed-DPI/新机器/耐久/许可或新包验收；旧ZIP及其原始证据保持，不提交、推送或发布。

### Notes

- `src/core/Scene.h`：声明可信GeometrySnapshot变换入口与来源token。
- `src/core/Scene.cpp`：校验场景/实体/mesh快照来源，采用增量派生并消除重复源复制，成功replaceNodes切换token。
- `src/core/modeling/VertexTransform.h`：仅Scene可调用的源变换接口，保留公开完整校验入口。
- `src/core/modeling/VertexTransform.cpp`：分离源变换与派生，收集受影响面并保留有限仿射/法线/溢出拒绝。
- `src/core/modeling/MeshDerivation.h`：声明私有可信增量派生入口。
- `src/core/modeling/MeshDerivation.cpp`：复用不变派生数据，只重算受影响面的动态校验和耳切。
- `src/core/modeling/MeshValidation.h`：提供逐面动态几何检查入口。
- `src/core/modeling/MeshValidation.cpp`：提取与原完整校验等价的逐面几何规则，不放宽完整拓扑拒绝。
- `src/editor/ComponentSelection.h`：新增只读hasSelectionInDomain，不改变集合状态。
- `src/editor/ComponentSelection.cpp`：只匹配请求ID、直接域投影及早停可用性判断，保持旧ID/孤立点/零角面语义。
- `src/editor/SceneViewModel.cpp`：普通预览使用可信快照，删除菜单调用非空判断，编辑枢轴只求一次双精度质心。
- `src/editor/operations/ComponentPicker.cpp`：复用双裁剪缓冲、保守ROI遮挡桶，顶点框选不创建未使用的位置哈希。
- `src/editor/operations/ComponentInteraction.h`：持有源拓扑复用状态并保持析构边界。
- `src/editor/operations/ComponentInteraction.cpp`：精确匹配身份/排列/角引用后复用拓扑，每次更新位置/显隐/颜色，点色复用标记。
- `src/editor/workbench/WorkbenchShell.cpp`：已验证点/面直接计数，边仍使用源拓扑去重。
- `tools/EditablePerformanceProbe.cpp`：独立可选CPU诊断、测量probe与相关生产源SHA256，默认真实路径/预算/样本不变。
- `tests/ComponentSelectionTests.cpp`：各集合操作、256稀疏子集投影和只读可用性等价边界。
- `tests/ComponentPickerPerformanceTests.cpp`：ROI、近裁剪w、容差和连续边可见区间回归。
- `tests/VertexTransformTests.cpp`：保持完整拒绝规则及无变化/法线变换行为。
- `tests/EditableSceneTests.cpp`：可信快照与full等价、错误来源/同址重载拒绝和失败原子性。
- `tests/MeshDerivationTests.cpp`：逐面几何拒绝与原完整派生一致回归。
- `tests/ComponentBoxTests.cpp`：独立旧语义像素参考及拓扑复用生命周期验证。
- `docs/v2-performance.md`：最新三档预算、优化/回归/失败过程、构建来源和适用限制，历史测量仍保留。
- `docs/v2-development-plan.md`：PERF-01限定预算达标，旧包/旧Debug/DPI与人工缺口区分。
- `docs/v2-acceptance.md`：追加新Release性能与回归证据，不重写旧QA/ZIP事实。
- `README.md`：当前源码性能状态与旧候选二进制边界。
- `docs/package-readme.md`：仓库补充新源码达标，明确旧包仍是收尾前性能。
- `docs/Mini3D_使用手册.html`：更新性能段与验证来源，34张既有截图不替换，不冒称本轮截图。
- `docs/performance/v2/closeout-20261003/manifest.json`：本轮正式文件清单、长度/hash、当前binary与验收限制。
- `docs/performance/v2/closeout-20261003/vertices-{10000,50000,100000}.json`：三档完整20/2通过报告，逐文件SHA256见manifest.files。
- `docs/performance/v2/closeout-20261003/sources.json`：归档时源/测试/文档hash，不包含随后追加的progress；测量源码另由每档build元数据证明。
- `docs/performance/v2/closeout-20261003/{build-release,core-targeted,picker-targeted,editor-targeted,ui-count-delete}.log`：同一最终构建的定向验证日志。
- `docs/performance/v2/closeout-20261003/regression/{ctest-release.log,LastTest-release.log,sources-at-test.json,binaries-at-test.json,result.json}`：最终Release4/4与真实源/binary归档，文档允许在测试后更新，不冒称它们与旧快照相同。
- `docs/performance/v2/closeout-20261003/history/`：四次baseline/诊断/两阶段未通过测量的JSON与日志、initial-build-failure.log；全部九文件的具体相对路径/长度/hash在manifest.evidenceFiles。
- `out/validation/perf-closeout-before-20261003/`与`perf-closeout-ui-before-20261003/`：原30文件及Workbench独立前稿，不覆盖既有二期修改。
- `out/validation/perf-closeout-{preflight,ui-preflight,format-root,verify,ui-verify,regression,overlay-refinement,final-regression,collect,final-check}-20261003.ps1`：本机前稿、增量格式、串行验证、归档和一致性脚本，不进入旧候选包。
- `out/validation/v2-completion-perf-closeout-*`及`perf-closeout-final-check-20261003/`：各轮原始构建失败/通过、性能和一致性日志，未覆盖以前报告。
- `progress.md`：只追加。回滚先另存后续修改，再按以上文件清单逐个从`E:/Mini3D/out/validation/perf-closeout-before-20261003/`恢复；Workbench使用独立ui前稿。例如`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/perf-closeout-before-20261003/src/editor/SceneViewModel.cpp' -Destination 'E:/Mini3D/src/editor/SceneViewModel.cpp'`及`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/perf-closeout-ui-before-20261003/src/editor/workbench/WorkbenchShell.cpp' -Destination 'E:/Mini3D/src/editor/workbench/WorkbenchShell.cpp'`。新正式证据按manifest逐项另存移出，progress保留历史并追加撤销说明；不全仓reset/checkout、不覆盖旧ZIP或其他二期功能。长期goal面板此前paused且无恢复接口，本轮依当前性能请求实际完成，不伪报面板恢复或整体目标完成。

## 2026-10-03 - Task: 当前Release构建后原生桌面核心流程视觉验收

### What was done

- 按用户“人工验收由代理执行、直接构建后测试”授权，构建当前Release并独立部署，使用真实Windows点击/按键/拖动/滚轮逐步观察，不要求用户操作，不用Qt Test/VM API制造GUI结果。
- 完成内插/F9、挤出取消、环切历史、中文路径保存重开/OBJ、X-Ray框选、比例圈、Local View、旋转后缩放及未保存关闭保护、视图/覆盖层、着色收藏、游标/移动吸附、集合、Mirror夹持/应用、细分整链应用/撤销的实际覆盖路径。
- 记录209步、258原始截图，正式复制49代表步骤的52张原图/对应文本、四个场景及一个OBJ；同步当前计划和验收结论，未确认覆盖路径的阻断性产品缺陷，但不标整体ACCEPTED。
- 正常关闭本轮应用，无遗留Mini3D进程；恢复应用偏好后导出哈希与原备份一致，不关闭用户窗口、不改变系统设置。
- 生产源码、原样例、旧ZIP、旧回归证据保持；没有提交、推送、发布或新打包。真实IME/原生与mixed-DPI/新机器/连续耐久/许可及部分功能GUI复验缺口仍明确保留。

### Testing

- `& './out/validation/visual-manual-build-20261003.ps1'`构建目标mini3d_editor和windeployqt均退出0；独立测试EXE SHA256=0B55C152DC9F230D217F3B42658B30C315FF2DF52A59200ABB0375709DC7E69A，与性能收尾主程序一致，HEAD=6dd209f38122aca837ec04077d58b78da2f47726、dirty=true。
- 原生桌面步骤逐项截图观察：I0.2→F9改0.3仍10面，一次Undo恢复6面；E1预览14面、Esc恢复，F3中文挤出执行；环切居中/Undo/Redo，真实文件对话框保存重开/导出通过。G/R/S及视图操作均只以实际动作范围作结论，不冒称全量S01–S12原生输入重跑。
- Mirror中心4点Clipping开启G/X/1保持X=0、关闭后到X=1；Merge开启应用12点、关闭16点，Undo恢复8点源笼。细分2→1/关闭/Undo形态一致，整链应用162点160面，Undo恢复8点源和Mirror→二级细分。
- `visual-manual-collect-20261003.mjs?validated-scene-shape=1`归档通过：全部258截图原始SHA256核对；24项相关源码/测试/探针与性能回归快照相同；8项正式样例和旧ZIP原SHA256相同。四保存副本格式3/Y-up，源点/面20/18、20/18、8/5、8/5；镜像/细分副本与正式样例逐字节相同。
- 实际导出OBJ为20点18多边形、72UV/72法线；索引合法、连接/绕序与保存源相同，世界平移后最大位置误差0。独立面角字段检查依据正式schema四份均通过；生成面角的normal允许省略，未以自写检查器代替C++校验或全局自交验证。
- `& './out/validation/visual-manual-restore-preferences-20261003.ps1'`退出0；窗口后查及进程检查确认0遗留。测试前/恢复后偏好导出SHA256均为66994D650EC2DD2CF81D4DDA2FCC485C6FA77CC7E11093E92E81DDBEA43DAC4E；仅保存元数据于正式证据，注册表内容留本机before。
- 两次归档器失败完整保留：首次把回归后更新的7份文档与旧快照错误比较；只读查证24项编译相关文件都匹配。第二次误把可选normal当必填；读schema及四场景，先独立字段/有限向量校验，再由归档器消费该通过结果。两次均在正式目录创建前停止，不改产品或模型来迎合检查器。
- Alt+Z被NVIDIA截获，关闭本次触发遮罩后改工具栏X-Ray；修改器控件焦点下Undo无变化，恢复视口焦点后正确恢复；旧索引/未观察焦点的工具拒绝经刷新观察后再继续。未确认这些为产品缺陷，不算入成功输入。滚轮缩放曾dirty，不宣称全部视图操作不标脏。
- 中键/Shift中键拖动不受当前drag API支持；GUI删除未执行（要求现场确认），Loop/Ring/连通、倒角、补面、Repeat及全部错误边界未分别复验；原生Windows缩放、mixed-DPI、真实IME、新机器、连续30分钟耐久/许可不冒称通过。旧全量CTest和性能无源码变化，不无收益重测。

### Notes

- `docs/v2-acceptance.md`：新增当前Release原生视觉证据、实际覆盖项、环境恢复与仍缺边界。
- `docs/v2-development-plan.md`：同步核心路径视觉状态，不把开发完成升级为整体ACCEPTED。
- `docs/validation/v2/visual-manual-20261003/report.md`：详细业务结论、16类动作矩阵、真实原图、文件核对和缺口。
- 同目录`report.json`：机器可读步骤数量、来源、结果和限制；`manifest.json`：每个正式文件的路径/长度/SHA256。
- 同目录`build.json`：实际构建身份；`build-release.log`：本轮主程序构建；`deploy.log`：独立Qt部署日志。
- 同目录`actions.jsonl`：完整209步索引，未精选原始截图路径明确留在本机out，不声称正式目录包含全量PNG。
- 同目录`source-hash-check.json`：24项编译相关文件匹配与7项回归后文档排除说明；`scene-shape-check.json`：四文件必填/可选面角字段及向量检查。
- 同目录`cleanup.json`：窗口/进程结束和偏好逐字节恢复元数据，不包含偏好内容。
- 同目录`collect-first-failure.txt`：旧文档快照误比较原失败/诊断；`collect-second-failure.txt`：可选法线错误假设原栈/依据与分段校验策略。
- 同目录`captures/*.png`：52张未经编辑的真实截图；`captures/*.txt`：49对应步骤的accessibility观察。完整逐文件清单及本轮各图简述由manifest/report矩阵列出，不替换旧证据或用生成图冒充。
- 同目录`artifacts/视觉验收_内插挤出环切.m3dscene`：GUI制作并重开的20点18面作品副本。
- 同目录`artifacts/视觉验收_视图与游标.m3dscene`：同一几何/对象，另存游标与中文集合。
- 同目录`artifacts/视觉验收_镜像.m3dscene`：恢复至原8点源笼和Mirror的副本。
- 同目录`artifacts/视觉验收_细分.m3dscene`：恢复原源笼和整链的副本，最后显式保存。
- 同目录`artifacts/视觉验收_柱体.obj`：文件菜单实际输出的求值世界几何。
- `out/validation/visual-manual-build-20261003.ps1`：本轮构建/独立部署/偏好与文档前稿准备，不修改生产源码。
- `out/validation/visual-manual-restore-preferences-20261003.ps1`：无运行进程前提下导入原偏好并重新导出精确核对。
- `out/validation/visual-manual-collect-20261003.mjs`：真实动作/文件/原样例核对和正式证据复制，修正两项检查器假设。
- `out/validation/visual-manual-final-check-20261003.mjs`：本轮资料编码、链接、增量、源hash与正式清单最终核对入口，不再测GUI/性能。
- `out/validation/visual-manual-20261003/`：独立程序、全量原图/文本/动作、场景/OBJ、恢复前后偏好导出、字段检查及失败原件；`before/`：两份文档与progress前稿及私有偏好备份，不公开注册表内容。
- `progress.md`：仅末尾追加。回滚先另存后续修改，再执行`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/visual-manual-20261003/before/docs/v2-acceptance.md' -Destination 'E:/Mini3D/docs/v2-acceptance.md'`与`Copy-Item -LiteralPath 'E:/Mini3D/out/validation/visual-manual-20261003/before/docs/v2-development-plan.md' -Destination 'E:/Mini3D/docs/v2-development-plan.md'`；新正式目录按manifest逐项另存移出，保留原始验收证据。progress保留历史并追加撤销说明，不恢复全仓或覆盖既有二期/性能改动。未启用子代理，主会话provider模型映射未独立核实；既有长期goal面板状态未在本轮改写为complete。

## 2026-10-03 - Task: 当前Release视觉验收证据最终封存

### What was done

- 创建正式证据清单并完成归档后的资料核对；不重复构建、GUI或性能测量，不改变此前带未覆盖项的验收结论。

### Testing

- `visual-manual-final-check-20261003.mjs`实际通过：核对117个正式证据文件、52张原图、24项源码、106个本地文档链接及测试主程序SHA256；偏好精确恢复，progress原历史前缀未变，编辑Markdown保持UTF-8无BOM/LF。
- 正式manifest SHA256=D93456D0BFCD3FF69E04A0346600D5A101089930D98D3C9CD1A3F8D7DD4A8529；原始结果见`out/validation/visual-manual-20261003/final-check.json`，status=passed。资料校验通过不等于完整V2或发布验收通过。

### Notes

- `docs/validation/v2/visual-manual-20261003/manifest.json`：117项正式证据的路径、长度及SHA256，不包含私有注册表内容。
- `out/validation/visual-manual-20261003/final-check.json`：本轮实际校验结果、数量和manifest校验值。
- `progress.md`：仅末尾追加本轮封存结果。回滚沿用上一任务的文档前稿与按manifest逐项另存移出方式，保留日志历史并追加撤销说明；不删除原始证据、不全仓重置。

## 2026-10-03 - Task: 当前二期成果Git上传准备与证据保真

### What was done

- 按用户“上传到git”授权，将当前二期代码、测试、方案、样例、性能收尾和原生视觉验收资料纳入提交范围；不重打包、不创建发布、不修改生产源码。
- 限定正式性能/视觉证据与配套样例为逐字节保存，禁止Git换行转换；只放行正式档案中的33份日志，继续排除本机out、构建产物和机器配置。
- 核对目标为既有仓库https://github.com/shenxi11/Mini3D的main分支；通过当前已监听的HTTP代理完成远程读取，不修改全局代理配置、不关闭TLS验证、不启动新终端窗口。

### Testing

- 初次直接fetch报SSL_ERROR_SYSCALL；核对Windows代理为127.0.0.1:7897且端口实际监听，单次Git参数http.proxy=http://127.0.0.1:7897后fetch退出0。上传前HEAD与origin/main均为6dd209f38122aca837ec04077d58b78da2f47726，双向领先计数0/0，没有分叉。
- 暂存848项变更、约34.18MB；逐文件Git blob SHA1与本机字节一致，143项性能/视觉清单条目和8个样例SHA256一致，未合并索引项为0。上传范围中无私有注册表、密钥文件、可执行程序、压缩包或100MiB以上文件；常见凭据模式扫描无命中，该检查不等同完整安全审计。
- Git属性检查确认正式证据与样例text=unset、源码仍text=set。原33份正式日志纳入索引；两份历史LastTest日志不是UTF-8，保留原字节，改为ASCII凭据模式检查，不为检查器转码。原检查器UTF-8失败已定位为历史日志，不是源文件损坏。
- 首次暂存空白检查将样例清单的原CRLF判为行尾空白；启用单次cr-at-eol规则并排除原始证据目录后检查退出0，没有修改样例或降低普通源码空白规则。950个索引文件无冲突，正式原图及清单不变。
- 大索引一次性传入检查工具超过64000字节限制，未执行检查；改为每批200条的内存分段快照后完成逐文件核对。首次诊断错误引用非持久作用域变量后改用完整返回对象，不使用旧临时变量继续推断。
- 本轮仅调整Git上传保真规则和协作文档，既有源文件无新增改动，不重复构建/CTest或GUI验收。提交与推送完成状态以本轮Git返回值和远程提交号核对为准，不预写推送成功。

### Notes

- `.gitattributes`：仅增加正式性能/验收档案及v2样例的-text规则，不转码或格式化原文件。
- `.gitignore`：仅放行两处正式档案的.log，本机构建与运行日志仍排除。
- `docs/development-setup.md`：说明正式证据原字节保护、暂存核对和提交/推送/发布边界。
- `progress.md`：仅末尾追加本轮上传准备与验证事实。原开发成果只纳入提交，不在本轮改写；完整提交文件清单通过本轮提交的git show --name-only查看。
- 回滚点为本次提交前的6dd209f38122aca837ec04077d58b78da2f47726。需要撤销整次上传时，核对HEAD确为本轮最终提交后可执行git revert --no-edit HEAD；若已存在后续提交，使用最终反馈中的本轮提交号作为revert对象。只生成可审查的反向提交，不reset、不强推、不删除工作区或原证据。
- 未启用子代理，主会话provider内部模型映射未独立核实；原生IME、多DPI、新机器、耐久及部分功能GUI缺口仍以验收报告为准，上传不升级整体ACCEPTED或发布状态。
