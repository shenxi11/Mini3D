# 旋转后对象缩放修复（2026-09-12）

## 结果与使用

修复对象旋转后，拖动部分世界缩放轴或输入 S/X/Y/Z 时提示“无法形成有效 TRS”的问题。
手柄和对象数字缩放共用同一计算规则；保留对象位置、旋转及网格数据，不扩展场景文件格式。

- 全局/世界空间：把所选方向的倍率换算为对象各局部缩放分量，因此旋转对象也能沿三条世界轴操作。
- 局部空间：直接修改对应局部缩放分量，行为不变。
- 手柄仍沿用防止跨零的下限与吸附规则；数字负倍率允许镜像。零、非有限或导致任一局部缩放绝对值小于 0.001 的结果仍拒绝。
- 每次预览从开始快照计算，重复输入不累乘；确认一条历史，取消恢复，Undo/Redo 和保存重开使用现有机制。

以绕 Z 旋转 45°、初始 scale=(1,1,1) 为例：世界 X 倍率 2 对应局部
scale≈(1.581139,1.581139,1)，rotation 保持原值；局部 X 倍率 2 则为 (2,1,1)。
这不是强行让局部 X 数值变为 2，也不是把立方体顶点剪切成斜体。

Blender 键位下可使用 S、X、2、Enter；同轴再次按下切换世界/局部。
Legacy 键位的 R 是缩放手柄，数字缩放请经“即时变换”菜单或 F3 搜索进入。

## Blender 依据与实现边界

查阅固定版本 **Blender v4.5.0** 的官方源码：

- [ElementResize / TransMat3ToSize](https://github.com/blender/blender/blob/v4.5.0/source/blender/editors/transform/transform_mode.cc#L934)：对象缩放把世界倍率矩阵作用于对象方向轴，取轴长度并根据原轴方向恢复符号，乘原始 scale，不改写 rotation。
- [对象变换数据准备](https://github.com/blender/blender/blob/v4.5.0/source/blender/editors/transform/transform_convert_object.cc#L176)：轴来自对象真实 object_to_world，包含父变换及负缩放。
- [方向轴归一化](https://github.com/blender/blender/blob/v4.5.0/source/blender/editors/transform/transform_orientations.cc#L338)：归一化矩阵列，而不是把含父非均匀缩放的坐标轴正交化。
- [mat3_to_rot_size](https://github.com/blender/blender/blob/v4.5.0/source/blender/blenlib/intern/math_matrix_c.cc#L1974)：提取长度、处理反射方向。

本项目在 Core 中独立实现上述缩放语义，GizmoController 和 ObjectTransformMath 调用共享入口。
计算使用真实世界边向量逐列归一化；世界倍率作用后按长度和符号修改 before.scale。
Local/等比倍率保留直接乘法，无操作精确返回 before。未将下载的 Blender 源码引入项目构建。

范围仅限当前单选、对象原点枢轴的对象缩放，不宣称全面复刻 Blender：

- 不保存剪切矩阵，不烘焙网格；对象模式的 TRS 表示仍然存在。
- 世界旋转在非均匀父变换下不可表示时仍拒绝，本次没有改变旋转规则。
- Edit 模式组件缩放仍直接变形顶点，与此对象缩放路径不同。
- Local 手柄方向仍沿用项目已有旋转基，不调整父级镜像下的手柄朝向规则。
- 本次是官方源码语义对照及本机自动化验证，没有运行 Blender 二进制做实机对比。

## 验证入口

CPU 回归在 GizmoTests.cpp、ObjectTransformMathTests.cpp 的 `[rotated-scale]`；
真实 Qt/GL 回归在 GizmoEditorTests.cpp、ObjectTransformSessionTests.cpp 的 `[rotated-scale-ui]`。
覆盖旋转后三轴、正负倍率、父级非均匀/镜像、预览不累积、撤销/取消、网格不变及保存重开。
更新旧的“世界缩放必须拒绝”断言，但世界旋转拒绝断言仍保留。

本机可重跑（Suffix 须使用未占用名称，不覆盖既有日志）：

```powershell
& 'out/validation/rotated-scale-core-test.ps1' -Suffix 'manual-core'
& 'out/validation/rotated-scale-verify.ps1' -Suffix 'manual-debug' -Full
& 'out/validation/rotated-scale-verify.ps1' -Configuration Release -Suffix 'manual-release' -Full
& 'out/validation/rotated-scale-verify.ps1' -Suffix 'manual-dpi2' -Scale '2'
```

最小手工复验：重新构建当前源码，任意旋转立方体，切世界缩放分别拖 X/Y/Z，
检查不再出现旧 TRS 提示；撤销，再切局部缩放确认行为区别。旧运行进程/旧发布包不含此修复。

### 本次实测结果

- 旧代码的两个复现用例均失败；修复后 CPU 定向 13 用例、287 断言通过。
- Debug/Release 完整构建通过；两配置 Core 92 用例/13801 断言、Assets 21/346、GPU 3/106 通过。
- Release CTest 4/4，完整 Editor 138 用例/4342 断言通过。
- 首轮 Debug CTest 3/4：缩放用例通过，已有 AreaMaximizerTests.cpp:104 的空场景未修改断言失败。
  保留失败日志；原种子 1217477366 定向最大化 5 用例/102 断言通过，再跑完整 Editor 138/4342 通过。
  未复现该失败，没有修改最大化功能或放宽断言，不能认定其根因已解决。
- Debug 缩放/对象模态/手柄交互 100%、150% 各 20 用例/578 断言，200% 为 20/592，全部通过。
  覆盖真实 Qt 鼠标/键盘、GPU 帧变化、单次历史、取消/撤销重做及网格不变、保存重开。
- 本轮 16 个正式文件保持 UTF-8 无 BOM/LF；定向差异、局部格式、文档链接、脚本语法和日志历史前缀检查通过。

日志/XML 位于 `out/validation/rotated-scale-*`，详细文件名及首次失败见 progress.md 本轮记录。
DPI 为本机单屏自动化验证，不替代跨屏、真实输入法或外部 Blender 实机对照。

## 回滚与范围

本轮旧文件快照：`out/validation/rotated-scale-before-20260912/`，保留所有先前二期成果。
需要回滚时先备份后续改动，然后对本轮修改过的旧文件逐个恢复，例如：

```powershell
Copy-Item -LiteralPath 'E:/Mini3D/out/validation/rotated-scale-before-20260912/src/core/Transform.cpp' -Destination 'E:/Mini3D/src/core/Transform.cpp'
```

其余文件按 progress.md 本轮 Notes 清单逐个替换相对路径；不要覆盖 progress.md，须追加回滚记录。
本新增说明可备份后单独移走。最后重新构建和测试，不全仓 reset/checkout。
本次不恢复二期长计划、不进入 MODEL-04，不提交、推送、打包或发布。
