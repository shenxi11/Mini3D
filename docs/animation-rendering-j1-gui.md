# J1 最小创作界面：已验收范围

2026-10-11 · EXEC-013 · 本票 Completed；不代签 EXEC-015 或完整动画方案。

## 能做什么

可从空白通过中文创建菜单生成立方体、场景相机、Sun，创建无纹理 OPAQUE PBR 并分配给几何。数据与外观页区分基础作者值与当前求值值；Sun 使用 W/m²，不将旧无量纲方向光强度自动转换。

通过“视图→动画时间轴”打开时间轴。基础 roughness、Sun 辐照度及独立标量录键值使用当前 locale 的 binary64 输入，17 位有效数字显示；失败保留输入，Esc 恢复。预览中修改标量作者仍走同一正式命令服务，不把求值值回写基础。基础变换编辑仍须先退出预览。

标量选择包括位置分量、材料 roughness、Sun 辐照度；可录制/替换、读取稳定键 ID，并按 256 键翻页。合法极小值及相同键重录保持精确；失败帧定位不会在旧帧录键。材料/灯类型、未分配槽、播放/草稿等不支持情形明确拒绝，不启用虚假控制项。

## 实际作品链

真实 MainWindow 从空白创建三对象及材料，用控件录制 X 0→2、roughness .2→.8、Sun 1→2 W/m²，24fps、1–24 帧。依次定位 24→1→12.5→1，12.5 得到 1/.5/1.5，误差不超过 1e-12，实际新 paint 的 frameId 单调递增。

测试还修改基础 roughness 和已有位置键，执行 Undo/Redo、验证键 ID 不变，另存 V5、重新打开，核作者数值、UUID、水位、材料槽及新文档来源。关闭测试窗口后保留的不可变作者场景不变。

测试窗口设置禁止激活/接收焦点；仅将其内部动画 dock 切为当前页，录键和定位前检查控件可见区域。没有操控或关闭用户窗口。设置读写仅在 E 盘隔离 Ini 根，结束恢复两个 scope 路径和默认格式。

## 验证与失败保留

证据根 `E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/product-slices-20261011-3f1dbe27`，下称 slices。

| 真实步骤 | 结果 | slices/runs |
| --- | --- | --- |
| 最终 editor 测试增量构建 | exit 0 | 20261011-202836278-BuildEditor-cd1eee7c |
| 控件验收 | 7 例、687 断言通过 | 20261011-202334671-TestEditor-2b1843af |
| 实际可见整窗作者链 | 1 例、299 断言通过 | 20261011-202938112-TestEditor-70a6fa45 |
| typed/消费者/旧取消回归 | 37 例、12961 断言通过 | 20261011-202604565-TestEditor-f813f210 |
| 最终主体构建 | exit 0，不编译测试目标 | 20261011-203023050-BuildApp-bd1a5340 |

原控件失败为测试遗漏 Sun 名称；只补合法名称，不放宽产品校验。首个整窗通过仍有隐藏 tab 注入缺口；保留其回执，增加内部 dock 选中与可见区域断言后重跑，不能把旧结果冒称可见性通过。独立 Sol 对两补丁和输出测试七处 INFO 括号复核无残留问题。GL 驱动像素传输性能警告保留，不把功能通过当性能认证。

最终 GUI 六路径清单 `gui013-visible-dock-d8fa7013.json`，SHA256 `9F60B04A322F80BCBBBF2CD706C3B680B1E69BE3F0EAAD7BCEAF7881FE22CC2B`；集成 102 路径清单 `integrated-013014-visible-dock-d8fa7013.json`，SHA256 `5F55C6B04414A8E710915AABEC19BE917892E3D8753FD08829307D95346BC21D`。实际截图、新稿及 paint 清单在 `runs/20261011-202938112-TestEditor-70a6fa45/artifacts/j1-gui-author-TtllMv`。

## 边界与撤回

本票不证明固定相机/尺寸的 GUI→PNG/EXR 联合作品、API 等价作者链或三项独立像素变化；这些由 EXEC-015 按事前固定配方验收。Bezier、Graph、NLA、完整灯型及持久渲染任务未完成。

原六路径前像见 `E:/CodexTemp/20261011/mini3d-j1-gui-013-88369ed4/before` 及 `repair-7208b9e6`；root 两测试补丁前像见 slices/before 中 `J1GuiAuthoringTests.before-sun-setup-6155a4cf.cpp`、`J1GuiAuthoringTests.before-visible-dock-d8fa7013.cpp`。先核当前后继及依赖，再作定向逆补丁；新增测试/文档移存唯一 withdrawn，日志仅追加撤回，不 reset 或覆盖其他 dirty。

未改系统、用户 MCP 配置、线程/权限边界或 Git。代理请求 Sol 6.1/max 普通已批准备用；provider 最终路由未独核，不称 fast 生效。
