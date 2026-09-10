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
