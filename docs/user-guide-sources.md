# Mini3D 使用手册：依据、截图与验证

用户文档入口：[Mini3D 使用手册（离线 HTML）](Mini3D_使用手册.html)，可用 F1 或
“帮助 → 使用手册与兼容性说明”打开。

## 范围与基线

- 当前基线：2026-10-02，本机二期已实现P0/P1限定功能；原手册于 2026-09-12 交付，旧图保留各阶段来源。
- 当前交付：30章中文说明、34张真实截图、目录检索、锚点、图片放大与打印样式；本轮页面与交付验证结论以 [二期验收](v2-acceptance.md) 的最终记录为准。
- 不以旧 V1 发布包或二期 HTML 效果稿作为功能证据；不声明二期全部完成。
- 原手册任务未修改生产代码；2026-09-16 随 MODEL-04 功能更新第 19 章与相关边界文案，未发布新版包。
- HTML 使用 UTF-8 无 BOM、LF；内联样式与少量原生 JavaScript，无 CDN、远程字体或新增运行依赖。
- 双击 HTML 即可阅读；分享时保留 `images/user-guide/` 相对目录。Markdown 链接是维护参考资料，不是用户完成操作的前提。

## 内容依据

当前 README、源码与专项文档交叉核对；旧阶段验收文档中的历史限制不直接当成现状。

| 主题 | 主要依据 |
| --- | --- |
| 界面、菜单、创建、文件、树、设备 | `src/editor/MainWindow.cpp`、`workbench/WorkbenchShell.cpp`、`TransformInspector.cpp`、`AppearanceInspector.cpp` |
| 模式与可编辑对象条件 | `src/editor/SceneViewModel.cpp`、[编辑模式](v2-edit-mode.md)、[可编辑源网格](v2-editable-mesh.md) |
| 键位、焦点、导航、F3/Q、最大化 | `src/editor/operations/KeymapRouter.cpp`、[输入与操作框架](v2-input-operations.md) |
| 框选、穿透、覆盖层 | [组件拾取](v2-component-picking.md)、当前组件拾取测试 |
| 组件变换、E、I、Ctrl+R、F9 | [组件变换](v2-component-transform.md)、[挤出](v2-extrude-region.md)、[内插](v2-inset-face.md)、[环切](v2-loop-cut.md)、[历史调参](v2-history.md) |
| 旋转后对象缩放 | [旋转后缩放说明](rotated-object-scaling.md)、Core 与 Editor 两个入口的既有修复 |
| 保存与兼容性 | [格式 3](v2-scene-format.md)、`src/assets/SceneDocument.cpp`、文档往返测试 |
| 吸附/隐藏/拓扑选择/比例 | [吸附](v2-snapping.md)、[可见性](v2-visibility.md)、[拓扑选择](v2-topology-selection.md)、[比例](v2-proportional-editing.md) |
| 倒角/修改器/集合 | [倒角](v2-bevel-edge.md)、[Mirror](v2-mirror-modifier.md)、[细分](v2-subdivision.md)、[集合](v2-collections.md) |
| 饼菜单/显示/Repeat/导出/兼容 | [P1-UI](v2-pie-repeat.md)、[OBJ](v2-obj-export.md)、[Blender边界](blender-compatibility.md) |
| 静态导入、基础外观和设备 | 当前 README、MainWindow/Inspector、[静态导入](week4.md)、[相机和灯](camera-light.md)、现有导入和设备测试；历史版本号以格式 3 专项为准 |
| 样例资源 | [样例与许可](sample-assets.md)，既有 glTF 样例及 `assets/scenes/v2/` 三份原生作品与哈希清单 |

## 截图取得方式

使用现有 `mini3d_editor_tests` 的真实 Qt 窗口与 QOpenGLWidget 截图入口；合成鼠标 / 键盘事件构造操作状态，不是手绘界面，也不是操作员手工全功能验收。

- 先重新构建 `out/build/opengl-viewport-verify` 的 Release `mini3d_editor_tests`。
- Qt 6.8.3 MSVC 2022 x64，截图缩放因子 1；窗口用例串行运行，使用隔离测试偏好。
- 原始证据：`out/validation/user-guide-20260912/raw/`，24 张；正式手册选用 21 张。
- 本机取图脚本：`out/validation/user-guide-capture.ps1`。拒绝覆盖已有输出；重取必须传新 `-Output` 目录。
- 标注脚本：`out/validation/user-guide-images.py`，以 Pillow 加编号、引线及裁剪，源图不覆盖。
- 原图中的对象、文本、几何状态未替换。部分截图裁掉状态栏、右侧不相关属性区，图注已说明。
- 特别说明：内插用例先验证厚度拒绝，再恢复合法值并 F9 调到 0.3。原图右侧和底部仍有先前拒绝提示；手册仅裁取视口与 F9 区域，未篡改该错误文字，也不把该原图描述成“所有错误提示已清除”。
- 旋转 / 缩放手柄截图采用该既有测试的 Legacy 配置，图注已明确，不能据此推导默认 R 的含义。
- 文件对话框只展示本仓库目录。其余多数图裁掉含临时测试路径的底部状态栏；未截取整个桌面或其他程序。

## 图号与来源

下列 PNG 均在 `docs/images/user-guide/`；来源用例筛选串及截图环境变量的精确对应在取图脚本中。

| 图 | PNG 文件 | 来源组 / 既有用例开头 | 截图所处状态 |
| --- | --- | --- | --- |
| 01 | `overview.png` | overview / Default W selects and R rotates | 默认键位、对象模式选中立方体，完整窗口 |
| 02 | `workbench.png` | workbench / Workbench tools reuse actions | 960×640 窄窗口，N 侧栏开启，裁底部 |
| 03 | `object-move.png` | object / Modal G X 2 R Y 45 | G X 2 未确认预览，裁底部 |
| 04 | `rotate.png` | gizmos / Rotate and scale mouse tools | Legacy 旋转工具，裁取视口 |
| 05 | `scale.png` | gizmos / Rotate and scale mouse tools | Legacy 缩放工具，裁取视口 |
| 06 | `tree.png` | tree / Tree view drop search | 层级和搜索操作后的对象状态，裁底部 |
| 07 | `edit.png` | edit / Viewport component clicks replace | 面 2/6、N 侧栏开启，裁底部 |
| 08 | `box.png` | box / B rectangle replace add remove | B 矩形拖动中，裁取视口 |
| 09 | `component-move.png` | component / Component preview reaches the same GL context | 组件 G Z 1 未确认预览，裁取视口 |
| 10 | `extrude.png` | extrude / E preview changes real mesh | E 法线 0.5 未确认预览，正面视角，裁取视口 |
| 11 | `inset.png` | inset / I preview changes the GPU frame | 内插已确认，F9 厚度 0.3，裁取视口 / 参数区 |
| 12 | `loop-preview.png` | loop / Ctrl R preview slide and right click | 环切面带预览，裁取视口 |
| 13 | `loop-slide.png` | loop / Ctrl R preview slide and right click | 滑移 35%，未确认，裁取视口 |
| 14 | `loop-center.png` | loop / Ctrl R preview slide and right click | 右键 0% 居中确认，新边域，裁取视口 |
| 15 | `history.png` | history / F9 edits one saved extrusion | 挤出后 F9 世界 Z 位移 0.75，裁取视口 / 参数区 |
| 16 | `import.png` | import / File import creates editable GLB instances | 多个真实样例；“editable”指对象属性，不是可进入网格 Edit，裁底部 |
| 17 | `camera.png` | camera / Camera preview and directional light controls | 实体相机只读预览、属性参数，裁底部 |
| 18 | `dialogs-dialog-SaveSceneAs.png` | dialogs / Qt dialogs and context menus use embedded Chinese | 空文件名的保存对话框，不是保存成功回执 |
| 19 | `dialogs-unsaved.png` | dialogs / 同上 | 保存 / 丢弃 / 取消提示，原始小图，无编号 |
| 20 | `operators-search.png` | operators / F3 context menu adds favorites | F3 搜索 Cube，已收藏结果，弹窗原尺寸 |
| 21 | `operators-favorites.png` | operators / 同上 | Q 中收藏的添加立方体，弹窗原尺寸 |

未采用的原图：`document.png`、`dialogs-dialog-ImportGltf.png`、`dialogs-dialog-OpenScene.png`，保留在本机原始证据目录，不作为新增正式图片交付。

## 验证与维护方法

1. 每次按当前实现更新操作步骤，检查模式、坐标系、单位、输入区域及确认 / 取消语义。
2. 重取截图时使用新输出目录，保留原始 PNG 和每组 XML / 日志；不得覆盖旧验证证据。
3. 本机浏览器校验入口：`out/validation/user-guide-check.py`，使用已有 Python、BeautifulSoup、Pillow、Playwright 和本机 Edge，检查本地文件页面，不安装项目依赖。
4. 静态检查包括 UTF-8 / BOM / LF、章节 / 图数、重复 ID、锚点、本地图片与参考链接、PNG 尺寸和连续图号。
5. 浏览器检查桌面、平板、手机宽度，图片加载、无水平溢出、目录过滤 / 跳转、图片弹窗 / Esc / 焦点归还、打印 CSS 与脚本错误。实际结果保存在本机 `out/validation/user-guide-20260912/` 的 QA 子目录。
6. 视觉检查原始联系表、正式标注截图与浏览器页面截图；合成输入 / 单屏截图不等于真实输入法、跨屏、高 DPI 或完整二期人工验收。

完成后的实际测试统计及落点见本轮 [progress.md](../progress.md) 末尾记录；本说明不把未运行的验证写为通过。

### 2026-09-12 原始手册验证结果

- Release 取图目标构建成功；17 个既有 Qt 用例、497 条断言全部通过，原始 XML / 日志在 `out/validation/user-guide-20260912/`。
- 最终页面检查为 `qa-02/report.json`，Edge 152.0.4191.66；1440、1024、768、390、320 像素宽度无页面横向溢出，无缺图、脚本错误或远程请求。
- 18 个目录锚点、检索命中 / 无结果 / 清空、21 张图的弹窗、Enter / Esc、关闭按钮和焦点归还通过；补测 390 / 320 宽度弹窗。
- 已检查打印调用与 print 样式：隐藏导航 / 工具按钮、移除正文侧边距。未交付 PDF，不把此项声称为完整分页印刷验收。
- 已查看原始联系表、标注图和浏览器桌面 / 手机实际截图。首轮布局截图未等候懒加载，出现尚未绘制的灰底；补充图片解码与绘制等待后重新取图，未据此误改文档图片路径。
- 校验脚本编辑中出现一次缩进语法错误，在执行浏览器检查前中止；定位并修复该行缩进后复验通过。未修改任何生产代码或放宽页面断言。

## 2026-09-12 原始手册回滚边界

README 与历史进度原稿在 `out/validation/user-guide-20260912/before/`。仅撤回本轮 HTML、本文及 `docs/images/user-guide/`，并移除 README 新增手册入口；不要回退生产源码和已有二期成果。`progress.md` 保留历史，追加回滚说明，不覆盖整份文件。

## 2026-09-16 MODEL-04 增量

新增第 19 章“删除组件与补面”，同步版本、快捷键和未实现功能边界；前 21 张图不改动。
内容依据为 [删除与补面](v2-fill-delete.md)、DeleteComponents/FillFace、SceneViewModel、
MainWindow、KeymapRouter/OperatorRegistry 与 FillDeleteEditorTests。

| 图号 | 文件 | 实际来源与状态 |
| --- | --- | --- |
| 22 | `delete-menu.png` | Release 专项 `X Delete menu and F fill…` 的 QMenu 原始截图；面删除项高亮 |
| 23 | `delete-hole.png` | 同一用例删除 Cube 的 +Z 面，未删对象 |
| 24 | `fill-boundary.png` | 同一用例选洞口四条边，F 前状态 |
| 25 | `fill-result.png` | 同一用例 F 后面域选中新面 |

原始证据 `out/validation/v2-model04-Release-1-final-{menu,hole,boundary,filled}.png`，
交付图片为字节一致复制，未后加标记、重绘或改变应用颜色。输入为 Qt 合成事件；
不宣称人工操作员已经验收全部功能。核心/编辑器用例 XML 与最终统计见 progress.md。

本次页面验证入口 `out/validation/v2-model04-guide-check.py --output <新证据目录>`，
检查 19 章/25 图、图片哈希/尺寸、连续图号、离线链接、五种宽度、目录、弹窗/焦点与无远程请求。
旧 `user-guide-check.py` 为原交付时的固定基线脚本，不适用于新章节计数。
本项前稿在 `out/validation/v2-before-model04-20260915/`，不能以旧手册回滚点覆盖本项后续改动。

最终页面证据为 `out/validation/v2-model04-guide-qa04/report.json` 与五种宽度截图，
Edge 153.0.4234.32，19章/25图、目录/弹窗/焦点、离线资源与无横向溢出检查通过。
菜单图固定原始显示宽度，避免在桌面被拉伸；首次静态检查发现图片高度标注错误，
按 PNG 实际271×120修正后通过。章节超长元素截图曾带入固定导航辅助链接，
最终改为真实视口截图取证，未据此修改无障碍导航行为。

## 2026-09-17 CURSOR-01 增量

新增第 20 章“3D 游标与定点创建”，同步能力、键位、保存提示与当前限制。
修正原“N 侧栏全部只读”的说明：对象 / 组件信息仍只读，游标 XYZ 可编辑。
原图 02 保留，图注说明它来自游标接入前；前 25 张图片未修改。
内容依据为 [3D 游标](v2-cursor.md)、SceneViewModel、ComponentPicker、ViewportWidget、
WorkbenchShell、MainWindow / OperatorRegistry 及 CursorEditorTests。

| 图号 | 文件 | 实际来源与状态 |
| --- | --- | --- |
| 26 | `cursor-surface.png` | Release `Cursor search actions execute real placement…`：Shift＋右键命中 Cube +Z 表面，N 显示世界 XYZ |
| 27 | `cursor-numeric.png` | Release `Cursor numeric sidebar submits…`：X 输入 2.75，在游标创建 Cube 后聚焦全部，游标与对象 X 一致 |

原始证据为 `out/validation/v2-cursor01-Release-1-final-{surface,numeric}.png`，
两图均为真实窗口 1440×900，交付图片逐字节复制，SHA-256 一致；没有重绘、裁切或新增标记。
测试使用 Qt 合成输入，不冒充人工验收。另已检查 200% 游标和滚动侧栏截图；不代表跨屏 DPI 实测。

验证入口为 `out/validation/v2-cursor01-guide-check.py --output <新证据目录>`。
最终证据 `out/validation/v2-cursor01-guide-qa01/report.json`，Edge 153.0.4234.32：
20章 / 27图、锚点 / 图号 / 尺寸 / 哈希、目录过滤和跳转、27图弹窗 / Esc / 焦点归还、
1440 / 1024 / 768 / 390 / 320宽度无横向溢出、打印导航隐藏与无脚本错误 / 远程请求均通过。
格式3实际保存样本及cursor3D错误类型 / 缺项反例也经schema检查。
使用隔离浏览器，不读取用户资料；已查看桌面 / 手机页面实际截图。

本项前稿为 `out/validation/v2-before-cursor01-20260916/`；回滚时只恢复本项已有文件，
另行备份后撤回两张新增游标图和专项文档，保留以前的25图与此前开发成果。
不要覆盖 progress.md 历史，回滚应追加记录。

## 2026-09-17 PIVOT-01 增量

新增第21章“变换枢轴与选区定位”，同步能力表、组件R/S说明、保存边界与第20章旧限制。
依据为[变换枢轴](v2-pivot.md)、SceneViewModel、ObjectTransformSession、WorkbenchShell、
Renderer / ViewportWidget / GizmoController及PivotEditorTests。既有27张图保持原样，
第26–27图注明来自游标初次交付，不能据旧图推断新版没有“选择到游标”按钮。

| 图号 | 文件 | 真实来源与状态 |
| --- | --- | --- |
| 28 | `pivot-object.png` | Release `Object numeric and real scale handle…`：对象原点X=1，经游标处的X缩放手柄拖动，再Undo/Redo后截图；位置X与缩放X同步变化 |
| 29 | `pivot-components.png` | Release `Pivot F3 Q header…`：两面选择，活动面3，局部Z旋转20°的未确认预览，HUD标明活动元素枢轴 |

原始素材为 `out/validation/v2-pivot01-Release-1-final-{object,components}.png`，
均1440×900，原样复制，SHA-256一致，未裁切、后加标记或重绘。输入为Qt合成事件，
已查看Release实际图及200%对象手柄/滚动侧栏图，不代替实机输入法、跨屏或完整人工验收。

页面验证入口 `out/validation/v2-pivot01-guide-check.py --output <新证据目录>`，
最终报告 `out/validation/v2-pivot01-guide-qa01/report.json`：Edge153.0.4234.32，
21章29图，图号/ID/离线锚点/资源/尺寸/哈希检查通过；五种宽度1440/1024/768/390/320
无水平溢出，目录检索/跳转、全部图片弹窗/Esc/焦点归还、打印导航隐藏、无脚本错误及远程请求通过。
已查看桌面/手机页面截图；内置Browser无可调用工具，使用本机隔离Edge，不读取用户资料。

本项前稿保存在 `out/validation/v2-before-pivot01-20260917/`。回滚按progress本项清单，
不能用CURSOR-01前稿覆盖其既有成果；新增两图和v2-pivot另行备份后撤回，保留前27图及进度历史。

## 2026-10-02 二期收尾增量

手册扩展为 30 章、34 图，补充已实现 P0/P1 的吸附、可见性、拓扑选择、比例、倒角、
Mirror、细分、集合、饼菜单/着色/重复和 OBJ/性能/兼容性说明；F1 与帮助菜单已接通。
前 29 图保留上文登记的各阶段真实来源，没有重绘旧图，也不把它们称为最终构建的新截图。
正文依据当前专项文档、源码和配方，限制以 [兼容边界](blender-compatibility.md) 为准。

### 图 30–34 的实际来源

原图目录为 `out/validation/v2-completion-p1-ui-final/screens/`。
取图入口是 `tests/P1UiEditorTests.cpp` 的真实 Qt 窗口用例，环境变量为
`MINI3D_TEST_P1_UI_CAPTURE_DIR`；使用 Qt 合成输入与应用截图 API，不是人工手绘界面。
原样复制映射在 `out/validation/v2-guide-assets.ps1`，本轮再次逐张核对 SHA256，五图均一致。

| 图 | 手册文件 | 原始文件 | 真实来源与状态 |
| --- | --- | --- | --- |
| 30 | `v2-shading-pie.png` | `shading-pie.png` | `Z and view pies reuse registry modes protect text and reject stale document context`：Z 打开真实着色饼菜单，弹窗截图 480×564 |
| 31 | `v2-view-pie.png` | `view-pie.png` | 同一用例：反引号打开真实视图饼菜单，弹窗截图 480×564 |
| 32 | `v2-solid.png` | `shading-solid.png` | `Shading state survives first GL initialization and never changes document history`：切实体显示，真实工作台 1440×900 |
| 33 | `v2-material.png` | `shading-material.png` | 同一用例：切材质显示，真实 Cube 逐面颜色，工作台 1440×900 |
| 34 | `v2-wireframe.png` | `shading-wireframe.png` | 同一用例：切线框显示，包含渲染三角化对角线，工作台 1440×900；不是新增可编辑源边 |

手册图与上述原图共用以下 SHA256；未裁切、后加标记或重绘：

```text
v2-shading-pie.png 91CC6002CE17A84F73DEF5B435224A906CF2E9069040E1B8C7DB0EAEF0AD1FAF
v2-view-pie.png    090579499CEF1C56E5398CD2B466E82DFC0BC6899B7BF1DE853CC30D578AE10B
v2-solid.png       DB53A2AC2CC4C74AEA5D96CFDB4BDC0A6A8502D29F0A8ED053717816B28DCAC2
v2-material.png    BEC87477145BA74D19F8D5DF9D570A5BAD31BAC6CB922EA0E16665CE52F63E8C
v2-wireframe.png   9AA41B214577BCEC845ECA80A755899FC3B512BC75B516DAAA0DEE99A3F06708
```

### 验证状态与维护入口

截图对应的 Release 编辑器定向日志为
`out/validation/v2-completion-p1-ui-final/mini3d_editor_tests-Release.log`，25 用例/672 断言通过。
该目录保留一次 GPU 失败日志；后续实际 GPU 补验在
`out/validation/v2-completion-p1-ui-gpu-coremode/mini3d_gpu_tests-Release.log`，2 用例/74 断言通过。
这两项是 P1-UI 定向证据，不替代最终两配置回归或当前页面 QA。

本机最终 Debug/Release 构建与 CTest 各 4/4 通过；原 S01–S12 与 F1 联调在实际 Qt
DPR 1/1.5/2 下各 8 用例/1739 断言通过，正式旧证据保留在 `docs/validation/v2/dpr100/`、
`dpr150/`、`dpr200/`。最终 Release 构建已重跑三档，同样各 8 用例/1739 断言、完整
S01–S12 通过，新报告位于 `docs/validation/v2/final/dpr100/`、`dpr150/`、`dpr200/`。
测试 EXE SHA256 为 `245CE43FC024F251BFC787862C0176F6219FF1CB30840D72851886862A6FF19E`。
最终复验重新生成的三场景/五 OBJ 与正式作品逐字节一致，旧 Blender 5.1.1 数值证据
仍针对这些未变 OBJ 有效，不将旧测试程序当作最终构建。2026-10-03实际候选ZIP的
本机隔离验收已通过，外部报告见 `docs/validation/v2/final/package/report.json`；包内文档
保留打包前快照。不把本来源核对或本机包验证写成整个二期已验收。

当前 30 章/34 图页面的维护验证入口为
`out/validation/v2-completion-guide-check.py --output <新证据目录>`，检查静态结构、
离线链接/图片、五种宽度、目录过滤、图片弹窗/焦点、打印样式和脚本/远程请求。
2026-09-12 和其后各专项页面报告保留历史版本计数；不能用旧 18/21、19/25、20/27 或
21/29 的报告代替当前页面验证。最终页面报告为
`docs/validation/v2/final/guide/report.json`：Edge154.0.4258.48，30章/34图、五种宽度、
离线资源、目录与放大/Esc/焦点、打印通过，无脚本错误或远程请求。
已查看1440/320宽度实际页面截图；没有读取用户浏览器资料。

新作品来源另见 [样例资源说明](sample-assets.md)。半壳来自原生 Cube 程序夹具，
保存/重开使用公开模型 API，不冒充 Bisect、文件对话框鼠标操作或人工耐久验收。
Blender 5.1.1 数值复核已通过，但固定参考 4.5.0、真实 IME、原生缩放/mixed-DPI、
新机器及 30 分钟人工耐久仍有缺口，详见 [二期验收](v2-acceptance.md)。

本次四份交付文档的修改前稿位于 `out/validation/v2-before-delivery-docs-20261002/`，
按原相对路径保存；回滚只恢复本次四份文档，不覆盖 HTML、图片、原生作品、代码或验收证据。
`progress.md` 由主验收统一追加并保留历史。
