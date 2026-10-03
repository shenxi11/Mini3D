# MIRROR-01：镜像求值基础

本页记录 2026-09-19 的 MIRROR-01 交付边界。2026-09-29 的修改器接入进展另见
[单 Mirror 修改器](v2-mirror-modifier.md)；以下“当前”指 MIRROR-01 当时状态。

## 当前交付边界

2026-09-19 完成单轴 Mirror 的纯 Core 求值、中心边界 Merge、变换 Clipping 会话，
以及 Scene 只读求值入口。**当前编辑器还不能添加或使用 Mirror 修改器。**

本轮把原 MIRROR-01 中的持久修改器状态和 Registry 入口调整至 MIRROR-02，
与参数卡片、实时显示、编辑笼、历史及保存一起接入，避免出现能添加却不能显示或保存的入口。
没有新增持久字段、快捷键或可点击占位项；Registry 仍为 66 项。S09 对称工作流未验收。
本项不自动开始 MIRROR-02，不覆盖旧候选包或 HTML 使用手册。

## 求值规则

- 单个 X / Y / Z **局部轴**，镜像平面经过对象原点；父级旋转、负尺度不改变局部求值规则。
- `MirrorOptions` 默认 X、enabled/merge/clipping 均为 true、threshold 为 0.001。
  阈值是局部坐标到平面的距离，包含等于阈值的情况；必须是有限非负数。
- 输入保持不变；关闭 enabled 返回源的恒等求值。Clipping 是变换约束，
  `evaluateMirror()` 本身不根据 clipping 修改源。
- 反射坐标与硬法线对应分量，同时反转面环；保留面材质、角 UV 和颜色。
  Merge 投影或 Clipping 夹持改变面形时，清除受影响面硬法线，由派生层重算。
- 求值前后校验身份、引用、流形邻接、面绕序及三角化。非法候选返回中文原因，不发布部分结果。

### Merge：只合并自身对应的中心边界

一条源边界边的两个端点都在阈值内，才将端点投影至平面并与各自的镜像点共享 ID。
只靠近一个端点不合并；孤立点和闭合网格内部点不合并。
不同部件即使距离小于阈值，也不会互相焊接。

完全由已合并中心点组成的平面面只保留源面，不再复制一张重合反向面。
闭合整立方体没有开口边界，直接镜像会产生另一份可能重叠的闭壳；不会自动切半或删除中心盖。
建议后续界面用开口半网格验证对称工作流。这里不提供 Bisect、Mirror Object、多轴组合、
全局自交检测或任意近邻焊接。

### Clipping：单次变换的捕获和锁定

`MirrorClipSession::begin()` 冻结 before、所选源顶点和参数。仅所选边界点参与：

1. 初始位于阈值内的点锁定；其他点在候选进入阈值或跨过平面时捕获。
2. 锁定点在该次会话内轴坐标为 0，仍可沿平面移动；拖回原侧也不解除锁定。
3. 每帧传从同一 before 算出的原始候选，不能在上一帧夹持结果上累积变换。
4. 拓扑/身份、面环、材质、UV、颜色不得改变，不能移动选区外顶点。
   退化或非法候选拒绝，且不推进已经接受的锁定集合。
5. 关闭 enabled 或 clipping 不夹持；Merge 与 Clipping 独立。
   初始捕获不直接安装网格，调用方结束或取消时丢弃会话。

## 来源和只读选择契约

`MirrorEvaluation` 同时给出 EditableMesh、DerivedMesh 及顶点/面/角到源的映射。
源 ID 保留；新增 ID 在各自域按源 ID 排序分配，支持 64 位身份，溢出拒绝。
该稳定性针对源容器重排，不保证源拓扑增删后生成 ID 不变。

DerivedMesh 的 triangleSources / vertexSources 指向**求值**面、顶点和角，
应再查 `MirrorEvaluation.faces/corners/vertices` 取得源身份及 `mirrored` 标记。
镜像面与角标记为 mirrored，不能直接成为可写源选择。
**中心共用顶点的标记保留源侧 false；不能仅看顶点标记判断镜像面是否可写。**
后续拾取应使用面/角来源区分镜像侧，并继续以源边为编辑笼，不使用三角化内部边。

## Scene 接入与应用候选

`Scene::evaluateMirror(entity, options)` 只接受已绑定 EditableMesh 的对象，
不存在或静态几何返回原因；不自动转换、不保存参数、不分配 MeshId，不推进 revision。

求值网格可交现有 `prepareEditableGeometry()`，校验成功后显式 `installGeometry()`；
原快照可恢复，安装/恢复时 revision 保持单调。安装后的普通 EditableMesh 可通过已有格式 3 保存。
这验证了应用的底层候选路径，**不等于已完成修改器应用按钮、撤销命令或参数往返**。
现有 Scene 对面材质槽的限制保持不变；Core 属性保留测试不代表 Scene 新支持任意材质槽。

MIRROR-02 仍需一次闭合：持久单 Mirror 状态、参数卡片/Registry、唯一撤销栈、
源笼与只读镜像显示/拾取、组件变换夹持、缓存失效，以及应用/删除/保存重开。

## 验证

专项入口：`mini3d_tests.exe '[mirror]'`；夹具见 `tests/MirrorTests.cpp`。
覆盖开口半立方体闭合、独立部件不误焊、三轴法线/绕序/属性/来源、阈值边界、
64 位 ID 与容器重排、耗尽/退化拒绝、Clipping 捕获/锁定/失败原子性、
Scene 源与 revision 不变、显式安装/恢复和普通几何格式 3 往返。

Debug/Release 完整构建通过；两配置镜像专项各 10 用例 / 1596 断言通过。
Debug 完整 CTest 复验、Release 完整 CTest 均为 4/4，分别耗时 106.94s / 36.32s。
Debug 首轮旧游标 UI 测试曾出现一次点击未发出放置信号（170 用例中 1 项失败）；
未改动游标代码，原随机种子定向 3 用例 / 96 断言及完整复验通过。
原因尚未定位，保留失败证据进入 QA，不能把复验通过表述为根因已修复。

本机最终验证结果见本轮 `progress.md`；证据位于 `out/validation/v2-mirror01-*`，不随发布包分发。
本轮没有 Mirror 界面截图，也不以旧界面回归或旧截图声称镜像实时交互已经可用。
大网格性能、全局自交、S09 完整对称工作流仍待后续验证。

参考：[二期计划](v2-development-plan.md)、[核心契约](v2-contracts.md)、
[EditableMesh](v2-editable-mesh.md)、[格式 3](v2-scene-format.md)。
