# 全面汉化：计划与验收

2026-09-10 用户授权制定计划并实施。默认简体中文，不新增语言切换设置。
保留场景格式 2（读取 1/2）、代码标识、快捷键、用户已有名称、导入名称及文件路径。

## 施工顺序与通过条件

1. 文案清点：核对界面、动态提示、导入与保存错误，建立术语及保留清单；验证原有测试基线。
2. 界面与对象：翻译菜单/面板/属性/名称与历史动作；验证稳定 objectName 和操作回归。
3. 提示与对话框：翻译应用自身错误/日志，接入 Qt 标准中文资源；验证错误、取消和原始诊断。
4. 验收与交付衔接：双配置、中文输入/路径、100%/200% 布局、旧场景兼容及隔离部署验证。

## 文案清单与术语

| 位置 | 原文范围 | 中文约定 |
| --- | --- | --- |
| MainWindow | File/View/Edit/Create、Scene/Inspector/Console、文档标题及菜单动作 | 文件/视图/编辑/创建、场景/属性/控制台；新建/打开/保存/另存为/导入 |
| TransformInspector | Name/Display/Parent、Position/Rotation/Scale、局部值提示 | 名称/显示/父对象、位置/旋转/缩放；旋转单位为度 |
| AppearanceInspector | Surface/Tint、Texture/Vertex color、Light direction/color/intensity/ambient | 表面材质/叠加色、纹理/顶点色、光源方向/颜色/强度/环境光 |
| 相机与预览 | FOV/Near/Far、Preview/Return、预览状态 | 垂直视角（度）/近裁剪/远裁剪、预览所选相机/返回编辑视图 |
| 新建及历史 | Demo/Empty/Cube/Sphere/Plane/Camera/Directional Light、Copy、Undo/Redo | 示例/空对象/立方体/球体/平面/相机/方向光、副本、撤销/重做 |
| SceneViewModel/SceneTreeModel | 校验失败、导入结果、保存/打开结果、Entity ID | 中文原因与操作结果，对象编号不变 |
| SceneDocument/SceneSerializer | 文件读取/写入失败、无效版本/字段/资源引用/层级 | 中文说明，保留格式键、路径和第三方 JSON 原始异常 |
| GltfImporter/AssetManager | 缺失资源、导入校验、纹理降级和格式限制 | 中文失败阶段与原因，保留 glTF 属性名及库诊断原文 |
| Renderer/Viewport/GPU/Shader/main | 应用自行产生的初始化、资源、着色器及验证消息 | 中文上下文，保留显卡型号、驱动输出、Shader 标识和错误日志 |
| Qt 标准控件 | 文件对话框、消息框、文本编辑右键菜单、系统错误提示 | 使用安装套件的 qtbase_zh_CN.qm；内嵌资源，避免运行时依赖 Qt 安装路径 |

菜单访问键保留为中文后的 (&F)/(&V)/(&E)/(&C)，Ctrl+N/O/S、Ctrl+I、W、F 等按键不变。
显示标签与 objectName 分离，不把中文标签用作测试定位、枚举判断或资源查找键。
新建/复制的默认名称中文化；保存的用户名称与导入文件内名称不改写。
Core 复制接口保持原行为，编辑器复制命令负责中文“副本”名称。

保留项：Mini3D Studio、Qt、OpenGL、glTF/GLB、PNG/JPEG、GPU、RGB、X/Y/Z、UV、
TRS/FOV 等标准标识（参数旁给出中文含义），扩展名/JSON 键/Shader uniform/资源路径、
Qt objectName、快捷键、测试用例名、第三方许可、第三方原始诊断和用户内容。
开发工具、历史媒体和第三方法律文本不汉化，不重制旧候选 ZIP。

## 实施前验证

已找到 Qt 6.8.3 的 qtbase_zh_CN.qm（136673 字节）。Debug CTest 基线 4/4 通过。
已保存 136 个正式文件到 `out/validation/localization-before-20260910`，确认目标文本 UTF-8 无 BOM/LF。
标准对话框计划使用 Qt 非原生实现，使英文 Windows 环境也能保持中文，不依赖系统语言。
本页后续按阶段记录实际结果，计划条目不代表已经实现或验证。

## 阶段 2 结果

菜单、属性、默认对象与撤销历史已汉化，稳定控件标识保留。
Debug 编辑器测试重新构建成功，全量 CTest 4/4 通过；14 个本轮变更文件的编码、定向格式、文档链接与历史日志保留检查通过。

## 阶段 3 结果

应用自产的导入/文档错误、操作结果及图形日志已中文化；底层异常与驱动原文保留。
Qt 标准译文在配置时通过当前 Qt6::qmake 查询，使用 qt_add_resources 内嵌。
所有文件入口显式使用 Qt 非原生对话框，安装译文不改数字区域格式。
缺少 qtbase_zh_CN.qm 时配置明确报错；运行不读本机 Qt translations 目录。
Debug 全量构建及 CTest 4/4 通过（8.87 秒），既有错误、取消、保存重开回归通过。
证据：out/validation/localization-stage3-tests.log。专项中文断言、布局与独立部署留阶段 4。

## 阶段 4：本机验收完成

2026-09-10，Qt 6.8.3 / MSVC / Windows / NVIDIA RTX 3050 Laptop GPU：

- Debug、Release 全量构建与各自 CTest 4/4 通过；编辑器连续三次通过。
- 新增 4 个汉化专项用例，最终共 127 个断言；200% 缩放全量编辑器 26 用例、1210 断言通过。
- 模拟英文数字区域设置，验证中文初始化不改变数值格式；保留稳定控件标识与快捷键。
- 关闭测试端的全局“禁用原生对话框”开关后，生产文件入口仍使用 Qt 对话框；
  打开、导入、另存为及取消分支通过，保存/丢弃/取消按钮与文本编辑右键菜单为中文。
- 输入法提交事件写入中文混合名称，F/W 输入不触发对象命令；复制、撤销、重做保留原名称。
  中文含空格路径下导入、保存、重开通过；损坏文档、资源缺失及空名称错误保持当前数据，
  JSON 底层异常原文可追溯。既有版本 1 示例升级及版本 2 设备场景往返回归通过。
- 实际查看了 100%/200% 正常和最小窗口截图、打开对话框与未保存确认。
  960×640 逻辑尺寸下没有横向截断；纵向滚动可到达相机预览及返回按钮，控件并非同时全部可见。
- 单独生成 Release 验证包，迁移到中文含空格目录并清空开发环境 PATH 后启动成功；
  默认示例、旧场景加载、缺失文件拒绝和 11 项依赖均通过，已查看完整中文版界面截图。
  包内没有外部 translations 目录，中文资源由可执行程序内嵌提供。

证据位于 out/validation：
localization-build-Debug/Release.log、localization-ctest-Debug/Release.log、
localization-repeat.log、localization-editor-dpi2.log、localization-ui-*.png、
localization-dpi2-*.png、汉化独立部署 20260910/（demo-ui.png、scene-ui.png、modules.json）。
复验脚本 Test-Localization.ps1、Validate-Localization.ps1 位于同目录。

Qt 在 Windows 控制台输出的原生日志受代码页及查看工具影响；测试断言报告直接写 UTF-8，
截图中的 UI 中文已核实，不以终端错误解码判断源码损坏。未全局修改 Windows 编码设置。

## 当前交付与人工复核

本机中文版可从源码重新构建，也可直接运行独立验证包：
out/validation/localization-package-20260910/Mini3DStudio.exe。
这是本次验证副本，不覆盖 out/packages 下的第八周旧候选包，不代表正式发布。

自动化输入法提交事件不替代真实拼音输入法候选窗口测试。英文 Windows 新机器、
真实输入法、跨显示器切换与长时间操作尚需人工复核；字体和极端窗口配置不作无条件保证。
本次没有修改 .m3dscene 协议；写 2、读 1/2 的能力沿用前轮。
旧第八周程序仍不能打开新版文件；原包/视频保留，项目和 Qt 资源许可仍待发布前确认。

后续最小动作是在中文版完成一次真实输入法的“重命名 → 保存 → 重开”人工检查；
不自动进入其他开发功能、重制演示或公开发布。
