# 开发环境说明

## 支持基线

Mini3D Studio V1 只验证 Windows x64、MSVC 2022、C++20、Qt 6 Widgets 与 OpenGL
4.1 Core。Qt 5、MinGW、Visual Studio 2019 和其他平台不属于 V1 构建矩阵。

## 前置安装

1. 安装 Visual Studio 2022，并选择“使用 C++ 的桌面开发”和 Windows SDK。
2. 使用 Qt 官方安装器安装 Qt 6.8 或更高版本的 `msvc2022_64` 套件，包含 Translations 中的 qtbase_zh_CN.qm。
3. 安装 CMake 3.28 或更高版本、Git 和 PowerShell 7。
4. 克隆并引导 vcpkg；不要把 Qt 同时交给另一套包管理器维护。

## 路径自检

在 PowerShell 7 中设置当前会话的路径，然后检查关键文件：

```powershell
$env:VCPKG_ROOT = 'D:/dev/vcpkg'
$env:QT_ROOT = 'C:/Qt/6.11.2/msvc2022_64'

Test-Path -LiteralPath "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
Test-Path -LiteralPath "$env:QT_ROOT/lib/cmake/Qt6/Qt6Config.cmake"
cmake --version
```

两个 `Test-Path` 都必须返回 `True`。`QT_ROOT` 必须指向具体的 MSVC Qt 套件目录，
不能指向 Qt 总目录，也不能指向 MinGW 或 Qt 5 套件。

## Qt Creator 导入

1. 只打开仓库根目录的 `CMakeLists.txt`，不要把 `src/editor/CMakeLists.txt` 作为项目入口。
2. 复制 `CMakeUserPresets.json.example` 为被 Git 忽略的 `CMakeUserPresets.json`，并填写
   本机 `QT_ROOT` 与 `VCPKG_ROOT`。
3. 复制 `CMakeLocalConfig.cmake.example` 为被 Git 忽略的 `CMakeLocalConfig.cmake`，并
   填写本机 vcpkg 根目录与 `pwsh.exe` 所在的真实目录。
4. 在 Qt Creator 中选择 `Windows x64 / MSVC 2022 (local paths)` Configure Preset 和
   `debug-local` Build Preset。

本项目已经通过 `CMAKE_TOOLCHAIN_FILE` 管理 vcpkg，因此 `CMakeLocalConfig.cmake` 设置
`QT_CREATOR_SKIP_VCPKG_SETUP=ON`，避免 Qt Creator 再用 Visual Studio 内置 vcpkg 包装
一次。部分 Qt Creator 版本会在导入 Preset 后丢弃其中的环境变量；根 CMake 会在
`project()` 前可选加载 `CMakeLocalConfig.cmake`，确保 vcpkg 使用同一组本机路径。

如果 vcpkg 日志提示找不到合适的 `powershell-core`，在 PowerShell 7 中查询真实路径：

```powershell
(Get-Command pwsh).Source
```

Microsoft Store 提供的 `AppData/Local/Microsoft/WindowsApps/pwsh.exe` 是应用执行别名，
当前 vcpkg 无法直接访问；`MINI3D_LOCAL_PWSH_DIRECTORY` 必须填写上述命令结果的父目录。

## Configure、Build 与 Test

```powershell
cmake --preset windows-msvc
cmake --build --preset debug
ctest --preset test-debug
```

首次 Configure 会由 vcpkg 恢复 Manifest 中的第三方依赖。当前已有
`mini3d_core`/`mini3d_assets`/`mini3d_renderer_gl`/`mini3d_editor_ui` 静态库、`mini3d_editor` 应用及四个测试 Target。
CTest 当前执行 4 个聚合入口：纯 CPU 30 个用例不依赖 Qt；Assets/拾取 11 个用例使用 Qt Gui
解码但不创建窗口或 OpenGL Context；GPU 2 个用例使用离屏 Surface/FBO 验证纹理、线框与手柄；
编辑器 26 个用例验证场景树、属性、导入、拖动、历史、文件往返和真实文档对话框，
含百对象完整撤销重做、中文输入/路径、标准中文对话框、布局和损坏文档隔离。
启用 BUILD_TESTING 时还需要 Qt Test 模块。仅运行纯数学测试可使用
`ctest --preset test-debug -R '^mini3d_unit_tests$'`；无窗口测试（含 Assets）使用 `-L headless`；
GPU（含 UI）使用 `-L gpu`；仅编辑器使用 `-L ui`。
CTest 自动为依赖 Qt 的测试补入 Qt DLL 目录，Context 创建失败会报测试失败，不自动跳过。

若使用 `CMakeUserPresets.json.example`，复制为被 Git 忽略的
`CMakeUserPresets.json`，修改本机路径后使用 `windows-msvc-local`、`debug-local` 和
`test-debug-local`。

## 运行 Debug 编辑器

```powershell
$env:PATH = "$env:QT_ROOT/bin;$env:PATH"
& '.\out\build\windows-msvc\src\editor\Debug\Mini3DStudio.exe'
```

中央区域现在是真实的 OpenGL Viewport，正常结果是深色背景中显示 Cube、Sphere、
Plane、有限 XZ 网格，以及红 X、绿 Y、蓝 Z 世界轴。Cube 为顶点面颜色，Sphere 为橙色，
Plane 为棋盘格（-Z 边左红右蓝），见 [第二周实现与验收](week2.md)。可用中键拖动 Orbit、Shift+中键
拖动 Pan、滚轮 Zoom；调整窗口大小后画面不应拉伸或触发崩溃。Debug 构建的 Application
Output 应包含实际 OpenGL 版本、GPU 名称、`OpenGL debug logging enabled.` 和
`Renderer initialized: Cube, Sphere, Plane, Grid and XYZ axes; materials and checker texture.`；若驱动无法提供至少
OpenGL 4.1 Core，程序会输出明确的 Context 格式错误。

场景树现在显示 Demo 下的真实节点。选择后可在 Inspector 编辑名称、可见性、局部 TRS
及 Parent；Create 菜单用于新增内置物体。操作和限制见 [第三周场景编辑](week3.md)。
第七周起可用 Ctrl+S 保存场景，Ctrl+O 打开；每次启动仍显示演示场景，不自动恢复上次文件。
新建空场景用 Ctrl+N。编辑后关闭/新建/打开会提示保存、丢弃或取消，保存失败阻止继续。

第四周新增导入，当前中文入口为 文件 → 导入 glTF / GLB（Ctrl+I），可选择 `assets/samples/` 中的
Box、BoxTextured 或 Duck。取消勾选“示例” 可清空演示物体的视觉干扰，导入后展开树节点，
用属性面板修改位置、旋转或缩放。导入不自动移动相机；视野外的模型可在树中选中后按 F。
同一路径重复导入创建独立实例并复用资源；修改源模型不热重载，新建/打开会重建资源库。
静态格式子集、错误处理和大文件同步导入限制见 [第四周说明](week4.md)。

第五周起，视口无修饰左键点击选择最近 AABB 命中的可见几何，点空白取消；选中对象
显示橙色世界包围盒。在树中选择导入根节点可包围整个可见模型；F 仅在树或视口有焦点时
生效，也可使用 视图 → 聚焦所选对象。属性输入和树重命名不会被 F 快捷键抢占。
不开启移动工具时左键拖动不移动物体；中键导航不改变选择。选择验收见 [第五周说明](week5.md)。

第六周起，在树或视口按 W 开关移动工具（视图 → 移动工具（世界坐标）），选中可见物体后
拖动对应轴。松开提交，Esc/失焦/窗口尺寸变化取消。Ctrl+Z 撤销，Ctrl+Y 或 Ctrl+Shift+Z
重做，Ctrl+D 复制选中子树，Delete 删除选中子树；也可使用“编辑”菜单。
第七周已把创建、导入、重命名、显隐、换父及外观/光照加入撤销历史。复制在原位置生成副本，
可继续用手柄移开；不会复制网格/纹理资源。详细契约和验收步骤见 [第六周说明](week6.md)。

属性面板的“表面材质”调整选中几何的叠加色、纹理/顶点色开关；未创建灯实体时使用兼容场景光。
另存为使用 Ctrl+Shift+S，会重新计算相对资源路径；场景和模型需放在同一盘符。
项目迁移须一起移动模型及外部贴图，`.m3dscene` 不是资源包。
相机中键 Orbit、Shift+中键 Pan、滚轮 Zoom、F 聚焦均能保存，导航不进入对象撤销链。
文档契约、验收与本机复验命令见 [第七周说明](week7.md)。

## 无桌面截图验证

自动验证环境无法访问屏幕 DC 时，可让应用直接保存 QOpenGLWidget 帧缓冲。该入口只在
显式设置环境变量时启用，保存完成后应用自动关闭；普通运行前不要保留此变量。

```powershell
$env:MINI3D_VALIDATION_CAPTURE = 'E:/Mini3D/out/validation/framebuffer.png'
& '.\out\build\windows-msvc\src\editor\Debug\Mini3DStudio.exe'
Remove-Item Env:MINI3D_VALIDATION_CAPTURE
```

## 常见问题

| 现象 | 最小检查 |
| --- | --- |
| 找不到 Qt6 | 检查 `QT_ROOT/lib/cmake/Qt6/Qt6Config.cmake` 是否存在 |
| vcpkg 工具链未生效 | 在首次 Configure 前设置 `VCPKG_ROOT`，然后使用 `cmake --fresh --preset windows-msvc` |
| 生成器找不到 MSVC 2022 | 通过 Visual Studio Installer 补齐“使用 C++ 的桌面开发” |
| 误用了 Qt 5 或 MinGW | 改用 Qt 6 的 `msvc2022_64` 套件并重新 Configure |
| Viewport 黑屏且无初始化日志 | 检查显卡驱动是否支持 OpenGL 4.1 Core，并查看 Application Output 中的 Context 错误 |
| Viewport 只有背景没有 Cube | 查看 Application Output 中是否存在带 `:/mini3d/shaders/` 路径的编译日志或 GpuMesh 上传警告 |
| Cube 可见但 Grid/轴线缺失 | 检查 `grid.vert`、`grid.frag` 的资源路径日志和 GridRenderer 初始化记录 |
| 启动后约 1.5 秒自动退出 | 删除当前 PowerShell 会话中的 `MINI3D_VALIDATION_CAPTURE` 环境变量 |
| 中键或滚轮没有改变视角 | 相机只读预览中先在视口按 Esc 返回；普通编辑视图确认中键/Shift+中键输入 |
| 链接报 `LNK1168` | 先停止仍在运行的 `Mini3DStudio.exe`，再重新 Build |

## 版本固定策略

第八周已固定 `builtin-baseline=2b65c20fc66eda893aa15a15a453c3cf09500b19`，
对应本机已成功恢复和验证的版本；不再随 vcpkg 工作树更新而自动漂移。
Qt 仍使用单独安装的 6.8.3 MSVC 套件，升级前应重新验收。

## 第八周验收工具与候选包

常规构建无需工具：`MINI3D_BUILD_TOOLS` 默认 OFF。开启后增加性能和演示回放程序，
需要 Qt Test；工具不安装到用户包，性能工具 Debug 运行明确返回 2。
打包/净 PATH 验证和演示复现分别见 [第八周说明](week8.md)与 [演示说明](demo.md)。
只有设置 `MINI3D_VALIDATION_CAPTURE` 时，应用才额外读取 `MINI3D_VALIDATION_SCENE`；
它用于部署验证，场景无效则退出 4。普通启动不读取此场景变量，不自动打开文档。

Camera/Light 补齐后的操作和验证见 [专项说明](camera-light.md)。无需新增依赖或 CMake 配置，
重新构建即可；第八周旧 ZIP 未更新，新保存的格式 2 文件不能用旧包读取。

## 简体中文构建与验证

当前应用默认简体中文。CMake 从 Qt6::qmake 查询当前套件的 translations 目录，
要求 qtbase_zh_CN.qm 存在并内嵌到编辑器；若缺失，请在 Qt 安装器补齐该套件的 Translations。
无须新增应用 TS 文件或运行 lrelease。部署脚本的 --no-translations 可保留，因为译文已经内嵌。
不改 Windows 系统语言、数值格式、本机 Kit 或用户保存名称；所有文件对话框固定使用 Qt 实现。

重新构建后运行 editor 测试的 [localization] 标签可复验中文控件、输入和路径。
MINI3D_TEST_LOCALIZATION_CAPTURE 为测试程序的截图路径前缀。
生产程序的 MINI3D_VALIDATION_UI_CAPTURE 仅在已有 MINI3D_VALIDATION_CAPTURE 模式下生效，
额外保存全窗口 PNG；保存失败退出 5。普通运行前清除验证变量，完整说明见 [汉化验收](localization.md)。

## Git 提交约定

提交源码、测试、项目文档、示例模型和文档引用的演示媒体；构建产物、临时验证目录、
本机 CMake 配置和 IDE 用户配置由 .gitignore 排除。
.gitattributes 显式将 DOCX、PNG、GIF、MP4、GLB 标为二进制，避免 docs 目录的文本规则
对图片或视频执行换行转换。提交前可用 git check-attr text diff -- <文件路径> 确认。
本地 commit 不等于远程 push，推送需单独确认目标仓库与授权。
