# Mini3D Studio 二期中文本地验收候选包

这是当前二期源码的 Windows x64 本地候选包，不是公开发布版本；程序内部版本仍为 0.1.0。
截至 2026-10-02，P0/P1 开发已落地，本机最终 Debug/Release 构建与 CTest 各 4/4 通过。
候选 ZIP 文件名为 Mini3D-v2-candidate-windows-x64-20261002.zip；
最终构建三档实际 Qt DPR 1/1.5/2 已各 8 用例/1739 断言通过。
本包是打包时文档快照，ZIP生成后的独立包报告随ZIP外部交付；不由此正文预先声明通过。
实际交付状态见 docs/v2-acceptance.md。
仓库当前补充：2026-10-03 实际 ZIP 的本机隔离验收通过，外部报告为
docs/validation/v2/final/package/report.json，ZIP 同目录另附同名 .validation.json。
此段不反写进已测试 ZIP；包内本文件仍是打包前快照。
随后当前源码 Release 已通过性能收尾三档原预算和新全量回归，见 docs/v2-performance.md；
本 ZIP 仍含收尾前程序，未更新，不可用它宣称新性能。人工/实机/许可与发布门槛未全部通过。
无需 Qt Creator。解压后运行根目录 Mini3DStudio.exe，保留 DLL、插件和 assets 的相对位置。
需要 Windows 10/11 x64 和支持 OpenGL 4.1 的显卡驱动。不要从 ZIP 内直接运行。

文件 → 打开场景 打开 assets/scenes/showcase.m3dscene。先另存副本，再编辑材质、光照、
层级和视角。Ctrl+S 保存、Ctrl+O 打开、Ctrl+N 新建、Ctrl+Shift+S 另存。
默认 Blender 风格：中键旋转视角，Shift+中键平移，滚轮缩放，小键盘句点聚焦所选；G/R/S 开始变换，
X/Y/Z 约束轴，Enter 确认，Esc 取消。Tab 切换编辑模式，1/2/3 选择点/边/面，B 框选，
Alt+Z 穿透选择。Ctrl+Z 撤销、Ctrl+Shift+Z 重做、Ctrl+D 复制；Legacy 键位可在界面切换。

二期原生作品位于 assets/scenes/v2/，用“文件 → 打开场景”打开 .m3dscene，先另存副本：

| 场景 | 用途与保存内容 | 源网格 / 求值网格 |
| --- | --- | --- |
| shell.m3dscene | 原生 Cube 内插、挤出和环切的外壳 | 20 点 18 面，无修改器 |
| symmetric.m3dscene | X Mirror、Merge、Clipping 的对称件 | 源 8 点 5 面 / 求值 12 点 10 面 |
| subdivision.m3dscene | 固定 Mirror → 二级 Catmull–Clark | 源 8 点 5 面 / 求值 162 点 160 面 |

Tab 进入 Edit 后编辑源笼，修改器结果只读。目录中的 -source.obj 与 -evaluated.obj
分别用于源/结果复核，不能通过“打开场景”当作工程加载；manifest.json 登记来源和 SHA256。
半壳来自原生 Cube 程序夹具，不代表提供 Bisect；保存/重开证据使用公开模型 API，
不冒充文件对话框人工验收。详细来源见 docs/sample-assets.md。

F1 或“帮助 → 使用手册与兼容性说明”打开 docs/Mini3D_使用手册.html。
手册包含 30 章、34 张真实截图，前 29 图保留各阶段来源，新增图源见 docs/user-guide-sources.md。
手册和截图均可离线阅读。请保留 docs/images 相对目录；各功能的限制以手册第30章和
docs/blender-compatibility.md 为准，不把相似布局理解为完整 Blender 能力。
编辑模式支持组件变换、区域挤出、单面内插、规则单环切、删除补面、有限倒角、
拓扑选择和比例编辑；属性面板提供有限 Mirror/四边细分。集合不改变对象父子关系。
临时预览未确认前不会写入保存文件；Shift+R 是新操作，F9 是修改最后一项历史。

中文界面与当前操作见 docs/localization.md、docs/camera-light.md；第七/八周文档与视频是历史记录。移走场景时也需保持模型/纹理的相对路径；
场景与模型须在同一盘符。创建 → 相机/方向光 可添加独立设备，在属性面板编辑或预览。
保存写入格式 3，兼容读取格式 1/2/3；旧第八周程序不能读取新文件，请先另存副本。
格式3保存源网格、修改器和集合，不保存 GPU 缓存、临时选区、隐藏/局部视图或着色模式。
OBJ 导出需选择源网格或修改器结果；项目采用 Y-up，不自动转换为 Blender 的 Z-up。
程序默认简体中文，Qt 标准译文已内嵌，无需单独安装 Qt 或拷贝 translations。
用户名称、导入名称和原始驱动诊断保留；尚无自动保存或发布安装器。

基础编辑仍建议每对象不超过 1 万源点，但最终复测的预览 API P95 55.01ms 超过 50ms 预算；
反馈 P95 58.25ms 达到 100ms 预算，不能将 1 万点称为稳定达标。更大网格有规模提示。
复杂拓扑、比例和修改器另有开销，完整规模数据与限制见 docs/v2-performance.md。
本机 Blender 5.1.1 的 OBJ/Mirror/有限二级细分数值复核已通过，固定参考 4.5.0 尚未实测。
真实中文 IME、原生系统缩放/mixed-DPI、全新机器和 30 分钟人工耐久仍需验收。

本项目许可证尚未确定，第三方许可条件仍需正式发布核查。
请阅读 docs/third-party-notices.md；不要把本地候选包视为已获授权的公开分发版本。
manifest.json 提供各文件 SHA256，用于检查交付完整性，不是代码签名。

维护者复验目录包：在 PowerShell 7.6 中执行
`& './tools/Test-Package.ps1' -PackageDirectory '<解压后的包目录>' -OutputDirectory '<不存在的新证据目录>'`。
脚本强制 Qt GUI 消息写入 stderr，并按已确认的本机 ANSI 编码读取，中文帮助路径仍严格检查；
输出日志保存为 UTF-8。已有证据目录拒绝覆盖，超时只结束本脚本创建的进程，不关闭用户窗口。
这只证明本机隔离部署，不能替代新机器验收。
