# Mini3D 通用 API 与 MCP 开发指导方案

日期：2026-10-06。状态：**首版本机单用户/Codex CLI适用范围已验收，见[实施状态](api-mcp-implementation-status.md)及[最终证据](validation/api-mcp-m7-20261006.md)**。源码核对基线：`5bfd37f8bcbadbd14d810d6bea0e7f50cfce4dc5`。

本方案把调研结论收敛为可施工的接口、状态规则、模块落点和验收门槛。实施任务与多 agent 分工见 [任务板](api-mcp-task-board.md)，前期现状见 [调研交接](api-mcp-research-brief.md)。正文保留最初规划与源码基线，实际落地以实施状态/验收证据为准；未实施的拟开关不能因首版验收自动视为功能。实现未改变场景格式。

## 1. 结论、范围与开工条件

可以沿现有内核建设共享 API；不能把当前 GUI 方法逐个包装后就称为通用 API。首版采用 **显式目标的应用服务 + 本机命名管道桥 + 独立 TypeScript MCP**，继续只有一个 Scene 和一个 QUndoStack。

这里的 MCP 指 Model Context Protocol。报告开头关于“用户曾把 MCP 展开为其他名称”的描述，不作为本项目需求事实；实际用户目标是让 AI 通过 MCP 操作 Mini3D 建模。

### 1.1 本轮与未来施工边界

- 最初规划交付：分析报告、制定开发规范和可分派任务，并保存在 `docs/`。用户随后已要求实施，当前代码进展见[实施状态](api-mcp-implementation-status.md)；未提交或推送。
- 未来最小闭环：查询 → 创建对象/合法网格 → 参数编辑 → 真实截图 → 保存 → 撤销/重做 → 重开。GUI 和 MCP 操作相同状态。
- 未来完整首版：补齐本体已支持的有限网格算子、修改器、集合、导入导出和有界批次，不增加本体原来没有的通用布尔、多段倒角或完整材质系统。
- 暂缓：插件加载器、外部插件宿主、C ABI、Python SDK、远程 HTTP、共享内存、后台建模线程、通用任务队列和自动订阅事件。只在出现真实需求后另立任务。
- 牛高达建模是 API/MCP 验收通过后的使用任务，不把部件名称、尺寸或建模策略写进 API。

最初调研请求只授权出方案；用户随后已要求开始实施，本体 API 与隔离原型已推进。后续进入生产通信协议、访问控制、文件授权或线程路径前，仍须按 [AGENTS.md](../AGENTS.md) 获得针对这些边界的明确确认，不因文档或内部 API 完成自动开放监听服务。

### 1.2 成功标准

1. 接口目标、单位、失败语义、版本与历史粒度均有明确契约；开发者不必猜当前选区。
2. 已实现能力与拟新增能力分开；每个开发任务能定位文件、依赖、验证和回退方式。
3. 多 agent 有独占写入范围、串行集成点和独立审查；沿用根 AGENTS 的 Sol/max 主导、Luna/max 调研及按条件 Astra/high 专家分工，无静默替换。
4. 首版失败不会留下可见半成品、破坏 redo 分支或假称截图对应新版本；未知提交结果不自动重做。
5. 普通构建仍只编译主体；API/MCP 测试保持显式单独构建。

## 2. 对调研报告的审阅与调整

输入为 `C:/Users/SL/Downloads/deep-research-report (2).md`；SHA256：`5A76E33A5230595B34CB2F7F30DA1685012D1F010EE111ACB5A0FEE0861F273F`。原件只读，不复制进公开仓库。报告中的检索占位引用无法独立点击，本方案使用源码入口和重新核实的公开链接替代关键依据。

| 报告建议 | 本方案处理 | 施工理由 |
| --- | --- | --- |
| 共享应用服务，MCP 进程外 | 采纳 | 保留现有业务与撤销权威；协议升级不进入建模内核 |
| 薄 facade，逐步提取 SceneViewModel | 采纳并细化 | 先提取实际重复的提交逻辑，不搬迁全部窗口代码 |
| Qt Named Pipe + 长度前缀 JSON-RPC | 作为首选，先做互通门禁 | Qt/Node、ACL、多个实例与本机部署需真实验证 |
| MCP 2026-07-28、官方 TS SDK v2 | 已锁定server/client2.3.1、Node24.13.0并验证两代stdio及本机CLI | 不把CLI证据扩展为Desktop当前聊天或任意宿主兼容 |
| 文档 revision | 补充 GUI、F9、相机、预览语义 | 只在 API 层加计数会漏掉 GUI；按 sceneChanged 计数会把预览当提交 |
| 批处理、单步撤销 | 提升为专门任务与验收门禁 | QUndoStack macro 不提供失败原子性；失败后 undo 也不能恢复丢失的 redo 分支 |
| requestId 缓存保证幂等 | 增加有界窗口、序号高水位与过期拒绝 | 缓存淘汰后仅查 miss 再执行，会再次创建对象 |
| 部分可靠性要求放第二里程碑 | 调整为第一次开放写接口前必需 | BUSY、旧句柄拒绝、断线去重不是可后补的安全装饰 |
| 32 MiB 消息、大网格能力 | 改成首轮保守预算并实测 | 不把旧几何性能数据当 JSON 解析与新算子能力保证 |
| 截图 blob / shared memory | 首版用有界内联 PNG；blob 后置 | 避免首版同时引入临时文件授权、回收和第二数据通道 |
| worker 解析、插件宿主、沙箱、远程 | 首版不实施 | 同线程事件循环足够支持有界请求；超预算先缩小限额，不自动加线程 |
| 全面 SDK/Schema 代码生成 | Schema 先成为统一契约，暂不造生成框架 | 本体仍做领域校验；契约样例跨语言跑通即可控制漂移 |
| 6–9 工程周估算 | 保留为外部估算，不作承诺 | 以里程碑证据推进；先完成小闭环再估后续吞吐 |

另需修正报告示例：挤出的顶盖在现有内核中保留原 FaceId，不能假定顶盖都是 `created.faceIds`；world TRS 也不总能从层级矩阵无损分解，本方案不承诺任意 world 矩阵写入。

### 2.1 已重新核实的外部事实

核实日期为 2026-10-06；这是资料核实，不是依赖安装或互通测试。

| 事实 | 依据 | 本方案应用 |
| --- | --- | --- |
| 2026-07-28 正式规范采用无协议级握手/会话的新核心 | [官方发布说明](https://blog.modelcontextprotocol.io/posts/2026-07-28/)；[官方规范发布](https://github.com/modelcontextprotocol/modelcontextprotocol/releases) | 文档身份由 Mini3D 自己管理，不依赖 MCP 会话 |
| TS SDK v2 为稳定线，使用独立 server/client 包 | [官方 SDK 文档](https://ts.sdk.modelcontextprotocol.io/v2/) | 以 v2 为首选；M0 锁定版本与 Node 要求 |
| `serveStdio` 管理 stdio；SDK 有旧客户端适配 | [stdio](https://ts.sdk.modelcontextprotocol.io/v2/serving/stdio.html)；[旧客户端支持](https://ts.sdk.modelcontextprotocol.io/v2/serving/legacy-clients.html) | 使用 SDK 的协议兼容层，不在 Mini3D 手写两代 MCP 生命周期 |
| QLocalServer 属于 Qt Network，权限必须在 listen 前设置 | [Qt 6.8 QLocalServer](https://doc.qt.io/qt-6.8/qlocalserver.html) | 新依赖只进入通信模块；先设 UserAccessOption |
| Named Pipe 访问受安全描述符控制 | [Microsoft Named Pipe 安全](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights) | 不能把“本机管道”当认证或恶意代码沙箱 |
| QOpenGLWidget 抓帧涉及真实渲染和 GPU 读回 | [Qt 6.8 QOpenGLWidget](https://doc.qt.io/qt-6.8/qopenglwidget.html) | 帧标记必须绑定抓帧实际产生/读取的画面，不能沿用请求到达时版本 |

最初规划时未核实的事项包括本机 MCP 宿主、Node、Qt/Node 管道、跨用户 ACL 和安装包。随后已实测 Node v24.13.0、官方 server/client 2.3.1 与 Qt 6.8.3 隔离原型，详见[实施证据](validation/api-mcp-m0-m1-20261006.md)；Codex CLI0.159.0直接工具/image盲验、最终成套包迁移和宿主实际建模通过，见[最终证据](validation/api-mcp-m7-20261006.md)。Desktop当前会话、跨用户 ACL、全新机器和公开部署未实测，不扩大本机候选结论。

## 3. 源码现状与最小改动落点

下表是基线源码事实，不以旧阶段文档的“尚未接入”判断当前功能。

| 现有位置 | 已有能力/限制 | 计划改动 |
| --- | --- | --- |
| [SceneViewModel](../src/editor/SceneViewModel.h) / [实现](../src/editor/SceneViewModel.cpp) | 同步 UI 线程；场景、资产、选择、事务、唯一 QUndoStack；createEntity 使用游标并选中 | 增显式目标入口、忙碌查询与业务提交通知；保留 GUI 包装行为 |
| [Scene](../src/core/Scene.h) | 几何候选/快照、节点及子树快照；不存在可直接套用的任意多对象原子事务 | 为原子创建/有限批次添加最小的离线准备与无失败安装能力 |
| [EditableMesh](../src/core/modeling/EditableMesh.h) / [网格验证](../src/core/modeling/MeshValidation.h) | 点、面、面角为 uint64 ID；边为有序端点对；源面与渲染三角形分离 | 创建时分配身份并返回映射，不改变现有拓扑合法性 |
| [ExtrudeRegion](../src/core/modeling/ExtrudeRegion.h) 等 | 有纯 CPU 候选算法，但 GUI begin/preview/finish 依赖模式和选区 | 定向调用候选算法，复用最终安装/历史，不伪装鼠标会话 |
| [HistoryService](../src/editor/operations/HistoryService.h) | 几何快照与结果选区，F9 能替换栈顶 after | 在成功替换时发布提交事件；不能只监听栈 index |
| [OperatorRegistry](../src/editor/operations/OperatorRegistry.h) | UI 操作登记和私有 documentGeneration，上下文与 QAction 耦合 | 参考操作命名，不把它当 RPC 路由器或公共文档身份 |
| [ViewportWidget](../src/renderer_gl/ViewportWidget.h) / [实现](../src/renderer_gl/ViewportWidget.cpp) | 真实 GL 视口；setEditorCamera 用于恢复而不回发 cameraChanged；仅有 focusSelection | 增显式 focusEntity、真实视图快照和一次捕获关联，不改用户选区 |
| [EditorCamera](../src/renderer_gl/EditorCamera.cpp) | 保存用 state 与轴向/正交实际视图不同 | 返回实际投影/视图与相机状态，不能只返回文件 CameraState |
| [main](../src/editor/main.cpp) | 验收截图为固定 timer 后抓帧 | 保留旧验收入口；生产观察独立实现，不以固定睡眠代替帧证据 |
| [顶层构建](../CMakeLists.txt) / [测试构建](../tests/CMakeLists.txt) | 基线尚无 Qt Network；tests EXCLUDE_FROM_ALL；mini3d_test_suite 独立 | 已新增Network，服务运行默认关闭；原拟编译裁剪未实施；新测试加入显式suite，不加入日常运行目标 |

### 3.1 拟新增目录及边界

```text
api/schema/                   公共 JSON Schema、方法目录、合法/非法契约样例
src/editor/api/               DTO、EditorApiService、文档版本与结构化结果
src/editor/automation/        帧编解码、鉴权/路径策略、实例连接、请求账本
src/editor/observation/       视口命令与捕获协调；不拥有 GL 资源
mcp/                         TypeScript adapter、bridge client、工具映射、测试
tests/*Api*Tests.cpp          共享业务和契约测试
tests/*Automation*Tests.cpp   协议、权限、真实连接测试
docs/                        使用、兼容、任务与验收说明
```

这是目录责任分区，不要求先创建空类、空 target 或每种接口一个类。小规模实现可以合并同责文件。Core 不新增 Qt/MCP；Renderer 不反向依赖 editor，帧标记若跨层，只用不含业务服务指针的值类型。

## 4. 目标架构与调用规则

```text
GUI 意图 ──► SceneViewModel GUI 包装 ──┐
                                     ▼
MCP Host → TS adapter → 本机桥 → 共享强类型编辑逻辑
                                     │
                      单一 Scene / Assets / QUndoStack
                                     │
                           Viewport 真实渲染与观察
```

首轮不要求所有 GUI 都改为经 JSON 或经 facade 调用。要求同一业务动作的校验、候选计算和历史提交只有一份；GUI 包装可继续负责游标、选择与模态交互。随着 API 方法接入，定向抽取该方法共享逻辑，不做全量 MVVM 重写。

### 4.1 三种不同入口

- **查询**：一致、只读；不改选区、模式、相机、文件路径或历史。
- **领域命令**：显式目标、参数、前置版本；成功一个逻辑历史，无变化零历史，已知失败零历史。
- **观察/文件命令**：相机导航、截图、保存/打开等不是普通建模撤销命令，各自声明副作用，不能混入原子建模批次。

不执行 QAction、不模拟键鼠、不让 MCP 直接写场景 JSON、不让 adapter 持有可变场景副本。adapter 可以缓存只读结果，但每次写入仍提交显式句柄和版本。

### 4.2 Schema 与 C++ 权威如何分工

`api/schema/` 是外部字段、默认值、错误结构、单位和方法清单的唯一规范；公共方法目录记录方法名、读写类别、权限、输入/输出 Schema、阶段及限制。Schema 先于并行客户端实现冻结。

C++ DTO 使用已有 `core::EntityId`、`core::Transform`、GLM 和强枚举；不因为报告示例而再引入 QVector3D/QQuaternion 的平行数学体系。JSON 编解码在边界统一完成，Core 仍只接收已类型化参数。不得把 bool + 中文提示反向解析成错误码。

首版不做通用代码生成框架：TS 从同一 JSON Schema 构建/校验工具输入；C++ 显式解码并做领域验证。两端共享契约样例，验证必填/未知字段、大整数和错误映射。若 SDK 桥接 Schema 的实际 API 受限，在 M0 选定受支持方式后记录，禁止悄悄维护另一份 Zod 真相。

## 5. 身份、版本、忙碌与历史契约

### 5.1 身份与数值

| 字段 | 规则 |
| --- | --- |
| instanceId | 每次应用进程启动新 UUID；不得以 PID 代替 |
| documentId | 每次成功新建/打开新 UUID；打开同一路径也改变；打开失败不改变 |
| document handle | `{instanceId, documentId}`；暂不再叠加无独立用途的 generation 字段 |
| entityId/meshId/vertexId/faceId/cornerId/collectionId | wire 为十进制字符串，范围 1…uint64_max；0 仅用于 schema 明定的根/无值；查询可用 null 表示缺失 |
| component ref | document + entityId + meshId + 元素身份 + 源版本，不接受孤立 FaceId |
| edge | 两个不同 VertexId，按**数值**升序，不按字符串字典序；不是边数组序号 |
| revision | wire 为十进制字符串；不是时间戳，不序列化到 `.m3dscene` |
| 坐标 | 右手、Y-up、现有场景单位；不得自动当毫米/米 |
| TRS | `translation`、`rotationQuaternion:[x,y,z,w]`、`scale`；矩阵 T×R×S |

输入拒绝非有限数值与 double→float 后溢出，旋转按既有规则验证/归一化。当前 `Transform::isValid()` 的单轴缩放绝对值下限为 0.001，负缩放合法；不能简单写成“scale 必须 > 0”。矩阵查询明确返回列主序 16 元素。

M0 将 `entity.*` 作为统一对象命名，不混用 `object.*`；ID 数字串不接受指数、符号、空格和前导零（合法的 `"0"` 除外）。未知请求字段拒绝以发现拼写错误；客户端须容忍新增响应字段。

### 5.2 三类版本，分别解决问题

| 版本 | 变化来源 | 不变化的例子 |
| --- | --- | --- |
| documentRevision | 已确认场景内容、修改器、实例外观、保存用相机、随保存持久化的游标内容；GUI/API/undo/redo/F9 均覆盖 | 只读查询、未确认预览、失败、no_change；纯保存不改变内容版本 |
| historyRevision | push、undo、redo、F9 after 替换、历史清空、保存点改变 | 只读查询、相机导航、临时选择 |
| viewportRevision | 影响画面的已确认状态、实际相机/投影、选区覆盖、隐藏/隔离、着色、视口大小/DPR、预览起止 | 相同值重复设置、无关日志 |

计数在当前 documentId 内单调递增，Undo 也不倒退；新文档可从 1 重置，因为身份已改变。`isModified` 保留当前产品规则，**版本变了不一定标脏或产生历史**，例如游标。保存改变保存点/路径元数据，返回实际 documentRevision 与新的 historyRevision，不假造建模修改。

网格已有 topology/geometry/evaluation revision 继续返回。当前 installGeometry 在 Undo 安装时也向前分配 revision，并保守地同时更新三类版本；不能承诺“只移动顶点时 topologyRevision 不变”。这些版本不覆盖整份文档和对象属性，写请求仍必须同时匹配 documentRevision。查询分页 token 绑定文档版本，不能在 Undo 后重新使用旧分页上下文。

**实现硬点**：不能只接 `sceneChanged`、`entityChanged` 或 `QUndoStack::indexChanged`。对象预览会发场景信号，F9 可能不改变栈 index。应在实际成功提交/undo/redo/F9 的业务边界发布一次带类别的通知；同一逻辑动作的内部多个信号只计一次。M1 列全修改通路后逐项验证。

### 5.3 Busy 与 GUI 共存

首版采用保守策略：外部领域写入只在 Object 模式且无未确认交互时执行；mesh API 在 Object 模式下仍可编辑明确对象的可编辑源，不依赖进入 GUI Edit。用户若处于 Edit，即使没有拖拽，也返回 `BUSY` 并说明原因，不偷偷切换模式。

Busy 至少覆盖对象 Gizmo、即时 G/R/S、组件预览、环切、F9 调整、框选、导航交互、文件对话框/文档切换、应用关闭、正在提交与正在捕获。查询 `document.current` 可返回 busy 原因；预览期间 scene/mesh/capture 返回 BUSY，不拿被对象预览暂写的 Scene 冒充已确认快照。

现有 Viewport 只提供 navigationStarted，尚无完整的活动查询/结束通知。M1-02 必须先补最小导航生命周期（活动、正常结束、取消/失焦结束），再由 MainWindow 聚合；不能只置 busy 不释放，也不能等 M3 观察完成后才补。滚轮等即时动作在该事件完成时结束，不用猜测超时恢复 busy。

检查分两次：接收时快速拒绝；在应用线程真正执行前再次检查。禁止 nested event loop、`processEvents()`、阻塞 `waitForReadyRead()`，防止执行中插入 GUI 编辑。多连接不意味着多写线程；所有业务串行。

### 5.4 历史约定

- 只有现有 QUndoStack；API 不维护自己的撤销栈。
- 完整对象创建（名字、父节点、TRS、外观）是一条；批次是一条；每个网格算子是一条。
- no_change 不入栈、不增加 documentRevision；已知失败保留场景、选区、redo 分支、保存点。
- `history.undo/redo` 要求 documentRevision 与 historyRevision，执行前读到的目标历史条目必须仍一致；只承诺撤销共享栈的最后动作，不承诺“只撤销 AI”。
- 文件打开/新建成功使旧句柄和历史失效；保存不清空历史。API 几何命令首版不承诺可 F9 重开，除非该方法已显式接入 HistoryService 并验收；不能让 GUI 调整到错误的前一条历史。
- EntityId 按现有文档生命周期高水位规则不复用；redo 复用本命令原 ID。组件算子目前可能按当前源网格最大 ID 分配，Undo 后的新分支可能复用元素数值，因此组件身份必须连同 documentRevision/meshId 使用，不能承诺裸 VertexId/FaceId 全会话永不复用。失败允许分配空洞，但不能发布失败候选 ID。

## 6. 公共方法与参数规范

下列为实施合同草案；M0 冻结为机器可读 Schema 后并行开发。MVP 为最小闭环，扩展为完整首版。`system.describe` 只报告真正实现并通过对应验证的方法，不提前显示占位功能。

| 阶段 | 方法 | 关键参数/结果 |
| --- | --- | --- |
| MVP | system.describe / document.current | 版本、instance/document、busy、能力、限额、权限；服务当前 documentRevision/historyRevision |
| MVP | scene.getSummary / entity.get / scene.listEntities | 层级、local TRS、worldMatrix、有效显隐、外观、包围盒、网格类型；分页，不隐含选中 |
| MVP | entity.create | primitive=empty/cube/sphere/plane，name、parentId、local transform、surface；返回 entityId |
| MVP | entity.update | 同一次更新的 name/local transform/surface/visible；先校验全部字段，一次提交 |
| MVP | mesh.create | parent/name/TRS、positions、有序面索引环、可选面角属性；返回对象/网格/输入索引到 ID 的映射 |
| MVP | mesh.getSummary / mesh.readSourcePage | 源身份、源/求值统计、各 revision；按稳定 ID 排序分页 |
| MVP | mesh.extrudeRegion | entity/mesh、源版本、faceIds、local/world offset；返回 capFaceIds 和变更摘要 |
| MVP | mesh.insetFace | 单一 faceId、localThickness；返回 innerFaceIds/rimFaceIds |
| MVP | viewport.getState / viewport.setView / viewport.focus / viewport.capture | 实际视图、显式目标/视图参数、匹配版本的真实 PNG |
| MVP | file.saveAs / document.open | 批准根目录下路径；首版 saveAs 仅新路径；open 默认拒绝丢弃脏文档 |
| MVP | history.getState / history.undo / history.redo | 共享栈状态、期望历史版本与实际结果 |
| 完整首版 | entity.duplicate / entity.delete / entity.setParent | 明确对象及子树影响；setParent 首版 keepLocal |
| 完整首版 | mesh.makeEditable / mesh.transformComponents | 前者仅内置 Cube；后者显式点/边/面、空间、枢轴和变换，不读取选区偏好 |
| 完整首版 | mesh.bevelEdge / mesh.loopCut / mesh.deleteComponents / mesh.fillFace | 单边单段、单环切、显式域、闭环边界；沿用内核边界 |
| 完整首版 | modifier.setMirror / modifier.setSubdivision / modifier.apply | 固定顺序；显式参数、启用、删除；烘焙返回重新查询提示 |
| 完整首版 | collection.create/update/delete/assign | 单层集合；删集合不删对象；不改父子关系 |
| 完整首版 | camera.create/update / light.create/update | 实体组件能力；现有单有效方向灯规则，不创造多灯渲染能力 |
| 完整首版 | file.save / file.importGltf / file.exportObj / document.new | 明确 overwrite、资产依赖路径、源/求值导出、未保存保护 |
| 完整首版 | batch.createEntities / batch.setTransforms | 有界同类命令；批次不接受任意 method 数组 |

### 6.1 通用命令信封与结果

Mini3D 私有 RPC 使用 JSON-RPC 2.0；它不是 MCP wire。请求 `id` 只用于响应关联。认证握手后分配 clientSessionId；写命令另带单调 mutationSequence，详细规则见第 9 节。直接 C++ 调用无需伪造网络会话。

```json
{
  "jsonrpc": "2.0",
  "id": "rpc-17",
  "method": "entity.create",
  "params": {
    "clientSessionId": "e9e9a370-583f-43c9-a6a4-8e0053cc70e1",
    "mutationSequence": "1",
    "document": {
      "instanceId": "cdf82e3f-49f2-461b-a24a-7b5f99c0c701",
      "documentId": "1597846b-bc6c-4566-867c-b963fbe42ace"
    },
    "expectedDocumentRevision": "12",
    "primitive": "cube",
    "name": "胸部占位",
    "parentId": "0",
    "transform": {
      "space": "local",
      "translation": [0.0, 6.0, 0.0],
      "rotationQuaternion": [0.0, 0.0, 0.0, 1.0],
      "scale": [2.4, 1.5, 1.0]
    },
    "surface": {"tint": [0.12, 0.15, 0.22], "useVertexColor": false, "useTexture": false}
  }
}
```

```json
{
  "jsonrpc": "2.0",
  "id": "rpc-17",
  "result": {
    "status": "committed",
    "document": {
      "instanceId": "cdf82e3f-49f2-461b-a24a-7b5f99c0c701",
      "documentId": "1597846b-bc6c-4566-867c-b963fbe42ace"
    },
    "documentRevision": "13",
    "historyRevision": "8",
    "created": {"entityIds": ["42"]},
    "affectedEntityIds": ["42"],
    "undoable": true,
    "selectionChanged": false
  }
}
```

统一结果状态：`committed`、`no_change`；文件成功为 `saved`/`opened`，截图为 `captured`，不能暗示都可撤销。领域失败放 JSON-RPC error；断线后的 `outcome_unknown` 属于客户端传输结果，不能伪装成“服务端确认未提交”。错误含机器 code、中文 message、字段路径、当前版本、建议动作；不通过中文字符串判断分支。

| 类别 | code | 客户端动作 |
| --- | --- | --- |
| 参数 | INVALID_ARGUMENT / INVALID_TOPOLOGY / LIMIT_EXCEEDED | 修正输入；不要原样循环重试 |
| 身份/并发 | STALE_DOCUMENT / REVISION_CONFLICT / NOT_FOUND | 重新查询并重新规划，不能自动重放旧写入 |
| 交互/渲染 | BUSY / VIEWPORT_UNAVAILABLE / VIEW_CHANGED / RENDER_FAILED / CAPTURE_TIMEOUT | 保留用户交互；等待或修复实际渲染条件，不能返回旧图冒充 |
| 能力 | UNSUPPORTED_OPERATION / UNSUPPORTED_TRANSFORM | 使用已支持参数/局部变换，不伪造成功 |
| 权限/文件 | PERMISSION_DENIED / PATH_DENIED / UNSAVED_CHANGES / OVERWRITE_DENIED / IO_ERROR | 按具体原因修正，不扩大目录权限 |
| 请求结果 | RESULT_EXPIRED / SESSION_EXPIRED / SESSION_IN_USE / SEQUENCE_CONFLICT / REQUEST_KEY_REUSED | 查询/人工或上层规划核对；禁止自动重新执行或顶掉活连接 |
| 生命周期 | DEADLINE_EXCEEDED / CANCELLED / INTERNAL | 先确认提交状态；取消只保证尚未开始的业务不执行 |

JSON-RPC 标准错误保留 -32700/-32600/-32601/-32602/-32603；应用错误可统一为 -32010，细分码放 `error.data.code`。Schema 非法是协议参数错误；输入通过 Schema 但拓扑不成立是领域错误。`retryable` 不用单个 true 诱导盲重试，改返回 `recovery: refetch / correct_input / wait / query_result / none`。

### 6.2 变换、选区与查询的具体边界

- entity.create/update 首版只接受 local TRS。查询返回 worldMatrix，不把有剪切的世界矩阵伪装成可无损 world TRS。
- 完整首版 mesh.transformComponents 支持显式世界位移/旋转/缩放语义，复用现有数学；非均匀父缩放/反射必须测试。world 对象缩放若暴露，定义为既有 `scaleTransform` 行为，而不是任意矩阵分解。
- setParent 首版明确 keepLocal；keepWorld 为不支持，后续另立变换语义任务。删除是子树删除，返回受影响 ID；复制返回 old→new 映射。
- 外部成功写入默认不抢选区。删除使已选目标消失时必须按既有规则清理，结果明确 `selectionChanged:true`，Undo 恢复规则有测试。
- bounds 说明 source/evaluated、local/world，空对象允许 null；visible 同时返回自身开关和层级/集合有效显隐，临时隐藏另列。
- `mesh.readSourcePage` 请求字段集与稳定排序 cursor，默认 256 条、最大 2048 条；cursor 绑定 documentRevision/meshId/查询字段。版本变化返回冲突，不拼接不同时刻的网格。
- 默认只查询源笼；求值网格先只给统计/包围盒。若后续支持求值三角形下载，其索引不能作为源编辑身份。

## 7. 自定义网格与参数化算子

### 7.1 创建网格的数据合同

最小输入：positions 数组、有序面环（引用 positions 的 0 起始索引）、对象名字/父节点/local TRS。面从外部看 CCW；每面至少三个不同顶点。首次创建不接受客户端自定稳定 ID，也不接受任意 AssetId。可选 corners 与面角数量逐一匹配，支持 UV、颜色、硬法线；省略时使用当前默认属性与对象默认材质。

先检数量、有限数值、索引范围及重复项，再用现有 MeshValidation/派生/求值逻辑检查拓扑和当前限制。开放边界等是否有效以当前验证器为准，不新增“必须闭合”这种无依据限制；也不宣称已有验证器能检测所有全局自交。

返回 `entityId`、`meshId`、`vertexIdsByInputIndex`、`faceIdsByInputIndex`、`cornerIdsByFace` 和实际 revisions。数组索引只在本条创建请求内有意义。

原子路径：先准备源网格、派生数据、对象属性、父引用及撤销 before/after；全部验证/分配成功后才发布。禁止先创建可见 Cube、再 makeEditable、再替换网格。这会制造中间对象、多条历史和失败残留。

`prepareEditableGeometry` 当前依赖已存在目标，不能直接声称已解决新对象原子创建。M2 需补最窄的“准备新实体及可编辑几何”入口，保留原 Scene 对象身份和内部几何快照来源标记，避免全场景 replaceNodes 让旧历史快照失效。

现有 prepare 可能预留未绑定网格槽位并消耗 MeshId；所谓“离线”指未发布可见对象、绑定、源内容或提交版本，不是保证 Scene 内部零写入。Core 当前安装自定义网格还要求面 material=0，首版保持该约束，不提供用户可填任意材质资源 ID 的入口。

### 7.2 算子执行模板

1. 检查文档、模式/busy、对象类型、meshId、documentRevision 和 topology/geometry revision。
2. 从已确认源网格读取明确组件，拒绝不存在或不属于此网格的 ID；不读取 GUI 当前选区。
3. 调用现有纯 CPU 算法产生独立候选；算子条件不满足时返回带约束的领域错误。
4. 准备派生/修改器求值及 before/after 快照，准备结果选择/身份摘要。
5. 一次历史提交、一次版本推进、一次聚合通知；API 不进入跨请求模态预览。
6. 返回结果组件语义；新/删 ID 过大时返回计数、受影响范围和分页查询入口，不无限膨胀响应。

| 算子 | 保留的限制与结果语义 |
| --- | --- |
| extrudeRegion | 连通可定向面域与合法闭边界；显式 local/world offset；原选面 ID 成为 capFaceIds，新侧壁另外返回 |
| insetFace | 单个共面凸面；局部厚度；中心面保留原面/面角身份，返回内面与边圈身份，不凭数组末尾猜 |
| bevelEdge | 单条流形外凸源边、单段、端点/邻面限制；命名用单数，不能声称任意 bevelEdges |
| loopCut | 规则四边形带单环；显式 seed EdgeKey、slide；不靠悬停，不加入多刀 |
| deleteComponents | 显式 domain 和 IDs，沿用关联面删除规则；结果明确删除影响 |
| fillFace | 单一可补共面闭环，明确点/边目标；不做任意孔洞修复 |
| Mirror/Subdivision | 单轴镜像与固定 Mirror→Subdivision，细分仅支持既有全四边条件/1–2 级；应用后重查源身份 |

几何处理不默认沿用隐藏/隔离/比例编辑/吸附偏好。算法语义与视图状态分开：明确 API 目标即业务目标，是否允许写暂时隐藏对象在权限/方法说明中固定；首版允许明确 ID 的对象写入，不依赖屏幕可见性。比例编辑不包含在首版 API，普通组件变换默认不启用它。

## 8. 真正原子的有界批次

完整首版只做两种同类批次：批量创建基础对象、批量设置 local TRS。默认最多 64 项，具体字节/内存限额见第 9 节。批内 parentId 只引用已经存在对象，首版不引入前向引用、临时别名或批内父子 DAG。

必须先把整批输入在隔离候选中验证，准备所有对象、属性、快照、返回 ID 映射和历史命令，再提交一个复合命令。批量 TRS 中同一目标只允许一次。整组提交期间不处理其他事件，禁止观察中间状态。

不能使用以下捷径：

- `beginMacro` 后逐条调用 setter，失败后 `undo`：第一条 push 已可能删掉 redo 分支，无法还原历史。
- 用 new/open/replaceNodes 做全场景替换：可能重置文档身份、资产和历史快照来源。
- 先发 UI 通知再决定是否回滚：树模型和 GL 缓存已观察到半成品。

推荐在 Scene 内增加只服务上述操作的 prepared change，安装阶段只交换/移动预先分配的数据，保留共享 Scene 身份、来源标记与高水位。before/after 足够重复 Undo/Redo；历史命令的 redo 不再验证用户输入或重新导入资源。若尚不能证明安装无失败，保持 capability=false，不以“多数时候成功”开放批次。

原子性承诺针对可恢复业务/校验/准备失败和有控制的安装故障；进程崩溃、系统断电、Qt 内部分配耗尽不承诺跨进程事务持久性。对 Qt push 的分配与 redo 调用顺序须审查并做故障注入，不把 `catch(...)` 当强异常安全证明。

必测：第二项非法、prepare 最后一步失败、当前存在 redo 分支、刚保存后失败、父对象消失、Undo/Redo 连续往返。结果断言包括实体/几何、选择、history index/count/clean index、脏状态和 revision。文件 IO、截图、导入、打开、新建不进入这两种原子批次。

## 9. 本机通信、权限与断线语义

用户于 2026-10-06 明确批准按本节的本机边界实施和隔离测试：同用户命名管道、默认关闭、拒绝提升权限开启、启动时明确指定读写根、应用线程串行建模。批准不包含远程服务、推送/发布或修改用户 MCP 配置；实际代码与安全验收仍以任务板证据为准。

### 9.1 连接与实例

M3实现采用原生Windows Named Pipe监听加QLocalSocket异步字节流；Qt6.8.3的UserAccessOption只提供SID ACL，不能保证本机限定，因此每个实际实例显式设置PIPE_REJECT_REMOTE_CLIENTS和当前SID的受保护DACL。无需TCP端口或后台建模线程，业务在QApplication线程串行。Qt/驱动/系统内部线程不属于“新增业务写线程”的承诺范围。

运行开关已为`--automation`，必须同时指定新的`--automation-descriptor`，可重复指定`--read-root`/`--write-root`；普通启动默认关闭。当前构建包含Qt Network，原拟MINI3D_ENABLE_AUTOMATION尚未实现，不把运行期关闭说成编译裁剪。开启后使用随机实例管道；**提升权限启动时拒绝开启服务**，防止普通同用户进程通过服务获得提升后的文件写权限。

主机连接必须指定实例描述文件或实例 ID；多个窗口时不取第一个，不自动附着其他文档。描述文件包含 instanceId、bridgeId（每次服务启停的新 UUID）、PID/启动时间、pipe 名称、API/wire 版本、批准根目录和有效权限，认证材料单独保护或处于同样受限的文件中。PID 仅诊断，不是身份。

启动时生成足够随机的 bootstrap secret，以当前用户受限文件 ACL 传给 adapter，不放命令行、公开日志或仓库。连接先执行私有 `bridge.hello`，验证 secret、instanceId、bridgeId 和兼容版本；失败不泄露文档信息。服务停止关闭连接，使旧 bridge 会话全部失效，不销毁用户文档或关闭窗口。

`bridge.hello` 明确分支：`mode=new` 创建会话并返回 clientSessionId、highWater、nextMutationSequence；`mode=resume` 必须同时带原 clientSessionId 和有效 secret/instanceId/bridgeId，恢复原账本而不分配新会话。每个连接只能绑定一个会话；同一会话仍有活连接时返回 SESSION_IN_USE，不顶掉或关闭原连接。恢复失败返回 SESSION_EXPIRED，不自动降级为 new。断开但仍有排队/执行命令的会话不能被清除，10 分钟保留期从所有已接收请求终结后开始；容量满时拒绝新会话，不驱逐 in-flight 会话。

描述文件可位于应用私有数据目录，属于未来正式运行数据；开发原型/测试描述文件、截图与缓存必须按 Windows 临时存储规则落到已验证的 E:/D: 任务目录。应用自身数据路径与 AI 临时输出不可混为一谈。

ACL、随机管道名和 secret 用于减少误连和未授权访问，不声称抵御已完全取得当前 Windows 用户权限的恶意程序。不得未经确认设计成远程服务或把命名管道转发到 TCP。

### 9.2 私有 wire v1

固定 **4 字节 little-endian 无符号长度 + UTF-8 JSON 字节**；长度不含头部，按字节而非字符计。MCP stdio 仍由官方 SDK 处理，不使用这套长度头。

解码状态为 Header → Body → Decode → Validate → Queue。覆盖分片头部、中文 UTF-8 分片、多个帧粘包、半包断开与部分写出；socket 可读写不等于整条消息到齐。零长度、超限和不合法 UTF-8 拒绝；JSON 深度/节点预算在 SAX 或等价有界解析时检查，不能等构建完整巨树后才限制。

私有协议首版只执行单个带 id 的请求对象；合法 JSON-RPC notification 不执行领域写入，也不回复。完整合法 UTF-8 帧里的 JSON 解析失败返回 `-32700/id:null`；非法请求对象或不支持的 batch 返回 `-32600/id:null`。这与“合法 notification 不回复”分开，不能一概把无 id 的错误吞掉。非法帧头/超限/不合法 UTF-8 属于传输错误，关闭该连接，不尝试回传无限错误。批量业务只能调用第 8 节的方法；受限 profile 在 describe 中声明。

首轮保守上限如下；M0/M7适用性能已测，见[性能说明](api-mcp-performance.md)，**限额不是响应时间保证**：

| 项目 | 初始值 | 行为 |
| --- | --- | --- |
| 单请求帧 | 8 MiB | 读头即拒绝超限，不先分配 payload |
| 单响应帧 | 16 MiB | 结果先预算；过大返回缩小查询/图像尺寸的错误 |
| JSON 最大嵌套 | 32 层 | 有界解析；禁止递归爆栈 |
| 连接/待执行队列 | 最多 4 连接，全局最多 16 条排队 | 超限拒绝，单连接顺序执行，无忙循环 |
| 单网格创建 | 10,000 点、10,000 面、40,000 面角 | 与帧/内存限制同时生效；不是所有算子都可处理到上限 |
| 实际算子候选 | 输出也受点/面角/内存预算约束 | 挤出和细分不能只检查输入 |
| 批次 | 最多 64 对象、估算候选内存 64 MiB | 任一上限超过即整批拒绝 |
| 截图 | 最长边 1600、最多 2,560,000 像素、编码 PNG≤8 MiB | 保持比例，不把降采样图标为原始分辨率 |
| 排队期限 | 默认 10 秒、最长 30 秒 | 单调计时；开始不可中断提交后不做强制半途终止 |
| 已完成命令结果 | 每会话最近 128 条，同时受全局 16 MiB 限制 | 淘汰结果但保留序号高水位 |
| 会话 | 最多 4 个，断开后最多保留 10 分钟 | 过期句柄只能被拒绝，不自动复活 |

bytesToWrite/读缓冲同样受总预算限制；慢客户端不持续累积输出。结果预算在提交前保证可返回摘要；提交后的编码/断线失败不回滚已确认场景，按账本恢复查询。限额写入 system.describe，单一配置来源，不能 C++、TS、文档各一个值。

期限与取消也属于 wire 合同：通用 params 可带整数 `timeoutMs`，写命令默认 10000、范围 1…30000；从服务端完整请求首次通过信封/认证并入队时按单调时钟计时，相同命令重放不重置期限。排队、候选准备完毕且尚未提交时检查到期；进入不可中断提交后必须完成并记账，即使客户端已超时也不能报告“确认未提交”。不宣称能中断正在同步计算的算子。

capture 独立使用 `timeoutMs`（默认 5000、最大 10000），到期结束捕获状态机，返回 CAPTURE_TIMEOUT 并清理待捕获状态。私有 `bridge.cancel` 以认证会话为范围，target 判别为 `{kind:mutation, mutationSequence}` 或 `{kind:request, requestId}`；后者用于尚在等待的 capture，请求 id 在该会话待处理期内必须唯一。尚未开始的写命令取消后写入 CANCELLED 终态并消费序号；已开始的写命令返回 cannot_cancel_started，只能查询最终结果；等待中的 capture 可取消，不改变场景。

adapter 将 SDK 可用的调用取消/本地超时映射到上述 best-effort cancel，并继续按账本确认写命令结果。无法收到确认时返回 outcome_unknown；不得将宿主取消直接翻译成业务已回滚。服务失联时不为发 cancel 而连接新实例。

### 9.3 去重与结果查询

建议采用比“UUID + LRU miss 就重做”更明确的首版规则：

1. 首次 `mode=new` 认证成功由本体分配 `clientSessionId`，记录同一 bridge 生命周期的最后消费序号 highWater；`mode=resume` 只恢复原会话。
2. 每个会话写命令使用 `mutationSequence = highWater + 1`，一个 in-flight。JSON-RPC id 可以不同，但不决定去重。
3. 对 `(clientSessionId, mutationSequence)` 保存规范化方法/参数摘要、执行状态和最终结果；目标文档与预期版本也属于摘要。
4. 相同序号且相同摘要：排队/执行中返回 pending，完成后重放既有结果；不会再次执行。缓存内相同序号不同内容返回 REQUEST_KEY_REUSED。
5. `sequence ≤ highWater` 但结果已淘汰：RESULT_EXPIRED，绝不执行；`sequence > highWater+1` 返回 SEQUENCE_CONFLICT。
6. 有效信封进入账本后，不论 committed、no_change 或领域失败，都消费序号；认证/无法解析的请求不消费。响应给出 nextMutationSequence。
7. `bridge.requestStatus` 为只读，返回 pending/completed/result_expired/not_seen 或 session_expired；重连同一有效会话可恢复。not_seen 仅表示该有效会话尚未接收此序号，过期会话不能这样推断。
8. 服务重启/会话过期时旧句柄一律 SESSION_EXPIRED；adapter 不把旧命令换成新序号或新会话自动重放。上层须查询当前场景再决定后续动作。

账本归本体而非 adapter 所有；adapter 崩溃后不可用本地缓存猜是否执行。规范化摘要按解码后的类型/字段顺序生成，不能仅对原始 JSON 字节取 hash 导致字段顺序变化误判。

去重检查在认证/信封校验之后、**新的业务前置版本检查之前**：已提交命令的旧 expectedDocumentRevision 不能阻止重放成功结果。highWater 表示已终结序号；入队时另保留 pending 的下一序号槽位，相同序号关联同一执行，其他新写请求须等其终结。已接受但排队到期/取消也形成终态并推进 highWater，不能留下永远无法使用的序号空洞。

原始结果可能是文档 R13，但重连时 GUI 已到 R20；重放结果标 `replayed:true`，保留 `committedDocumentRevision:R13` 并另报当前状态，禁止把旧结果当最新快照。不承诺进程崩溃后的 exactly-once，也不落盘建设事务数据库。

### 9.4 文件能力与路径

默认权限分为 scene.read、scene.write、viewport.observe、viewport.control、file.read、file.write；能力授权由本体控制，MCP annotation 只是提示，不是访问控制。启用时明确批准读/写根目录，工具调用不能自行扩大它。

路径检查覆盖绝对路径归一化、路径段边界、现有父目录、reparse point/junction、UNC、设备路径、ADS、越界与覆盖。首版只允许批准根目录内的本地普通路径，拒绝经过 reparse point 的路径；不能用字符串 startsWith 判断。不存在的新文件以已验证父目录拼接合法文件名。

不仅检查顶层文件：`.m3dscene` 引用的模型以及 glTF 的外部 buffer/image 都必须落在批准读根，禁止由导入文件间接读取未批准文件或发起 URL 请求。资产加载策略在实际打开依赖前执行；若现有 loader 无法注入路径策略，应先补最小钩子，不能先读取后审计。

MVP saveAs默认不得覆盖存在文件；M3使用同目录完整暂存、释放真实句柄后MoveFileExW(flags=0)原子不替换发布，失败清理自有暂存。GUI仍沿用QSaveFile原子覆盖，不能认为QSaveFile自动提供“禁止覆盖”。完整首版save/overwrite需明确授权和再次检查文件状态，旧格式首次升级另存保护保留。路径重检不承诺防御当前用户恶意进程的所有TOCTOU攻击，这是本机信任模型边界。

document.open/new 默认 `ifDirty=reject`。只有后续明确授权的 discard 操作才能丢弃修改；工具描述必须注明。文件选择不能弹 UI 对话框等待 AI 猜，失败直接返回结构化原因。打开失败保留旧场景、历史、身份和路径。保存/导出报告实际路径与内容版本，不返回机器无关的虚假下载 URL。

## 10. 真实视口观察与截图

### 10.1 观察状态

新增 ObservationService 只做协调，Renderer 继续拥有 GL。`viewport.getState` 至少返回：document handle/revision、viewportRevision、view preset、透视/正交、实际位置/朝向/target、viewMatrix/projectionMatrix、场景相机预览 ID、着色、覆盖开关、临时隐藏/隔离概要、像素尺寸与 DPR。

保存用 CameraState 不包含全部瞬时轴向/正交状态；不能以它代替实际渲染视图。明确目标聚焦新增 `focusEntity(id)` 或 `focusEntities(ids)`，不临时改选区再调用 focusSelection。观察命令先检查 busy，不直接调用会取消用户操作的导航初始化函数。

free orbit/setCamera/focus 若改变现有保存用相机，走同一回写路径更新文档相机/dirty；轴向、正交等仅会话状态沿用产品规则。不要为了 API 把正交模式偷偷加入 `.m3dscene`，也不绕开回写直接改 Renderer。

### 10.2 捕获状态机

```text
检查明确文档/目标版本/视图版本与 busy
  → 创建 captureId，异步请求 update
  → paintGL 记录实际渲染输入 stamp 和 frameId
  → 在有效 GL 生命周期抓取与该 stamp 对应的图像
  → 检查实际抓帧后的 stamp、资源状态、尺寸
  → 返回 PNG + 元数据，或明确冲突/不可用
```

每个 capture 要求**精确匹配** expectedDocumentRevision 和 expectedViewportRevision，首版不支持“尽量接近”或旧帧 fallback。等待期间用户导航/文档变化则 VIEW_CHANGED/REVISION_CONFLICT，调用方重查后再请求。系统发起的普通重绘不更改 viewportRevision。

`frameSwapped` 可用于等待与显示证据，但不能单独证明图像身份：`grabFramebuffer` 可能再次触发渲染，必须以该次实际 paint stamp 为准。生产异步等待使用 Qt 回调/取消状态机，不用嵌套 QEventLoop、固定 sleep 或 `processEvents()`。窗口关闭/最小化不可渲染、context 未就绪、到期无匹配帧都返回错误，不自动打开/关闭用户窗口。

图像元数据包括 captureId、frameId、contextGeneration、完整 document handle/revision、viewportRevision、实际相机/投影、原始像素尺寸、输出像素尺寸、DPR、mimeType、byteLength、sha256、overlayIncluded。Context 重建时改变 contextGeneration，使旧 FBO/帧关联失效；查询返回只保存元数据不构成截图验收。

**渲染失败不能伪装成功**：当前 Renderer 存在只记录 qWarning 的 GPU 上传失败路径；非空 QImage 可能仍缺少对象。需增加针对本次绘制所用资源的 readiness/已知错误返回，出错返回 `RENDER_FAILED`，不能只凭“PNG 非空”。不要求重写整个渲染错误系统，只补观察闭环必要的状态。

首版将受限 PNG 以 base64 在私有 RPC 响应中发送，adapter 转成 MCP image content，不把它作为巨大的文本字段呈现。较大图像返回 LIMIT_EXCEEDED；后续实际证据证明有需要再增加 blob 句柄。临时截图文件不是用户任意路径写入工具。

## 11. MCP adapter 与可维护工具面

官方 SDK v2 + stdio 为首选，精确 package/Node/TypeScript 版本在 M0 实测后用 lockfile 固定。SDK 的 MCP 生命周期与兼容层由 SDK 处理；不得把报告示例中的 0.3.0 当成 Mini3D 已有版本，也不得硬写尚未测试的 npm 版本。

v0 API 版本拟从 0.1.0 开始，私有 wire=1；Mini3D 应用版本、API 版本、wire 版本、MCP 版本、场景文件版本分开记录。0.x 同样不能随意改字段/单位；破坏变更更新明确兼容线与迁移说明。

M4 实际工具逐项映射 21 个已实现 RPC，名称由点号/驼峰机械转换成下划线，不合并不同输入合同：

| MCP 工具名 | RPC 映射/说明 |
| --- | --- |
| mini3d_system_describe / mini3d_document_current | 能力和当前文档分别查询 |
| mini3d_scene_get_summary / mini3d_scene_list_entities | 场景摘要与分页分别查询 |
| mini3d_entity_get / create / update | 对应对象查询、创建、更新 |
| mini3d_mesh_create / get_summary / read_source_page | 创建、摘要、源分页分别调用 |
| mini3d_mesh_extrude_region / mini3d_mesh_inset_face | 对应单个算子 |
| mini3d_viewport_get_state / set_view / focus / capture | 对应真实观察/控制/匹配帧 image |
| mini3d_file_save_as / mini3d_document_open | 文件副作用明确，不替用户丢弃/覆盖 |
| mini3d_history_get_state / undo / redo | 查询、撤销、重做各自严格合同 |

完整首版再按已实现 RPC 添加 bevel/loop/delete/fill、批次、修改器、文件和集合工具，不为追求“≤20”而退化为 `action:string,args:any`。静态工具目录稳定，具体上下文是否可用在返回状态和调用错误中说明，不随 GUI 选区频繁增删。

资源不是首版依赖：大网格通过分页查询，图片通过 image content。若宿主确实支持再补只读 resource URI；宿主能看到文本但看不到图片时，不称为视觉建模闭环通过。

业务失败转换为 MCP tool execution error（例如 SDK 支持的 isError 结果），同时保留 Mini3D code/recovery/current revision；MCP 协议错误不要吞成业务 success。输出有结构化结果及简短中文说明；64 位值始终字符串。适配器日志只能 stderr，不泄露 secret、整段网格或私有绝对路径到不需要的日志。

生命周期：宿主退出只关闭 adapter 自己的管道/资源，不结束 Mini3D；本体退出让 adapter 报实例不可用，不扫描并接管其他窗口。禁止 `execute_code`、shell、任意 UI 点击、任意文件读写、自动下载运行包。正式使用不以每次 `npm install latest` 为启动步骤。

## 12. 里程碑与集成顺序

| 阶段 | 交付 | 进入下一阶段的条件 |
| --- | --- | --- |
| M0 契约/兼容验证 | Schema v0、方法目录、SDK/宿主及 Qt/Node 原型结论、批准的权限范围 | 版本/模型/IPC 门禁明确；未通过项不写“已选型可用” |
| M1 本体基础 | 身份/版本、busy、查询、完整对象创建/更新、共享历史、文件基础 | GUI/API 混用与预览拒绝通过，无第二 Scene/Undo |
| M2 网格最小闭环 | 合法网格原子创建、分页、显式挤出/内插 | 源身份正确、失败无残留、结果组件可继续使用 |
| M3 通信/观察 | 有界管道、权限/路径、请求账本、精确帧捕获 | 断线不重复、越界被拒绝、真实图像关联版本 |
| M4 MCP 最小闭环 | 固定依赖、首批工具、目标宿主真实 E2E | 经 MCP 创建和编辑 → 截图 → 保存 → 重开 → 旧句柄拒绝 |
| M5 本体现有能力覆盖 | 余下网格算子/修改器/集合/设备/导入导出 | 能力逐项可发现，限制与现有产品一致 |
| M6 有界批次 | 批量创建/TRS、失败/redo 分支保护 | 第二项失败等故障注入与跨 GUI 历史通过 |
| M7 性能/部署收尾 | 限额定标、Release 包、运行说明/兼容表 | 默认不开服务；独立测试构建；包内通信与真实图像通过 |

观察与 framing 可在 M1/M2 契约冻结后独立推进，但不得在版本/busy/去重未完成时开放正式写服务。M4 是可开始小模型试用的门槛，不代表 M5/M6 已完成。

详细任务、文件所有者和串行/并行波次见 [任务板](api-mcp-task-board.md)。采用里程碑，不承诺固定天数；M0/M1 完成后再用实际任务耗时估后续工期。

## 13. 验证与证据要求

按风险做最小有效验证，不每次跑全量；但协议、数据一致性与真实图像门禁不可留到最后才测。

| 编号 | 验证面 | 必须证明 |
| --- | --- | --- |
| V01 | 身份/Schema | 超过 2^53 的 ID 无损；uint64 溢出、非法数字串、未知字段拒绝 |
| V02 | 文档/版本 | GUI push/Undo/Redo/F9/相机/游标均按规则变化；预览不被当提交；旧文档拒绝 |
| V03 | 交互共存 | Gizmo、G/R/S、Edit、环切、框选、文件对话框时不取消用户操作、不读伪快照 |
| V04 | 创建/网格 | 完整原子创建；无效面、非有限数值、属性长度、ID 映射和修改器限制 |
| V05 | 历史/批次 | no_change/失败零历史；保留 redo/clean；API 后 GUI Undo、GUI 后 API Undo |
| V06 | 断线/去重 | 排队前后、提交前后断线；相同键、不同内容、过期结果/会话、重启不自动重放 |
| V07 | 协议/资源 | 半包、粘包、UTF-8、超帧/深度/队列、慢读者、取消/超时、无 stdout 污染 |
| V08 | 路径/权限 | 默认关闭、wrong secret、跨用户 ACL、提升进程拒绝；依赖资产路径同样授权 |
| V09 | 图像 | 真实上下文、目标版本/视图匹配、中文/多 DPI 尺寸、上传失败、关闭/换视图冲突 |
| V10 | 建模 E2E | MCP 创建小装甲块→挤出→内插→观察→保存→Undo/Redo→重开语义一致 |
| V11 | 构建/部署 | 普通 build 不编测试/Node；显式 suite 可构建运行；Network DLL 与 adapter 版本成套 |
| V12 | 性能 | direct API 与 IPC 成对测量、解析/排队/计算/读回分项、空闲不轮询、持续调用内存有界 |

MVP E2E 使用简单自建装甲夹具，不上传用户说明书。检查对象/源网格/TRS/颜色/文件重开一致性，并附正面、侧面和透视真实截图；图像肉眼/模型观察与结构化断言互补，不能由纯算法测试替代。

现有可执行验证命令（仅示例，本轮未运行构建）：

```powershell
cmake --build --preset debug-local --target mini3d_editor
cmake --build --preset debug-local --target mini3d_test_suite
ctest --test-dir E:/Mini3D/out/build/windows-msvc-local -C Debug -R '^mini3d_editor_tests$' --output-on-failure
```

未来新增 `mini3d_api_tests`、`mini3d_rpc_tests` 只在有必要隔离依赖时建立，否则使用现有 editor 测试目标中的新文件。若新增 target，由主代理同时接入显式 suite 与 CTest；尚未创建时不能执行它们或声称已通过。MCP 的 npm ci/build/test/typecheck 脚本在 M4 才建立，不加入主体编译前置。

每任务只跑受影响测试；M4/M7 做完整相关回归与真实 Windows/OpenGL E2E。跨用户/新机器条件不可用必须标未验收，不能用单用户同进程测试替代。性能先测基线；旧 10 万点验收不能为新的 API 通信/截图预算背书。

正式证据写 `docs/validation/api-mcp/<阶段>/` 的摘要/必要截图；大日志、构建和临时数据放已验证 E:/D: 路径。记录 commit、脏树差异、构建配置、Qt/Node/SDK/宿主版本、命令/退出码、图像哈希和限额。敏感 token、用户原始资料不进证据包。

## 14. 回退、未决项与交接

回退粒度按阶段而不是全仓 reset：关闭 automation 运行开关可停止外部写入；禁用未通过能力可止损；代码回退使用阶段定向反向补丁或经授权的 revert。GUI 共用逻辑已经改变的阶段不能仅删 facade 就宣称恢复，须跑对应回归。退出 adapter 不回滚用户已保存文件。

最初M0待冻结的精确SDK/Node/CLI组合、命名管道原型、批准根/开启方式、预算数据和Schema到SDK桥接已在首版落地。跨用户ACL、Desktop当前聊天和新机器仍未实测；编译裁剪未实施。实际限额、证据、候选包及关闭回退见[最终证据](validation/api-mcp-m7-20261006.md)和[运行说明](api-mcp-runtime.md)，不交给调用方猜测或自行扩大权限。

第 9 节本机安全/线程边界已获明确施工批准；超出该边界仍须重新确认。任何方案不通过时先记录失败证据、比较最小替代，不静默切到 HTTP、任意代码执行、后台写场景或不同模型。

### 14.1 规划交付基线与多 agent 实际配置

本轮源码核对使用两个只读子 agent，启动参数均显式为 `gpt-6.1-sol` + `max`；一个负责业务/历史/网格，一个负责视口/构建/通信落点。工具确认任务已创建，但未返回可独立证明底层 provider 路由的模型元数据，因此“请求配置已指定”，不写“底层执行模型已独立验证”。最终文档由主代理整合，子代理没有写仓库。

用户随后明确保留旧协作规则，因此本方案最终沿用根 AGENTS 的角色路由，不执行“全部子代理统一 Sol”的草稿设定；上述两位代理继续承担独立审查，使用 Sol/max 与原规则一致。未改 AGENTS 或客户端模型配置。

最初规划轮只生成开发指导与任务板、README入口和progress追加，验证限于资料/源码/文档交叉核对；不是实现安全性已验收。随后用户已授权实施并完成首版M0–M7适用范围，结果与未实测条件见实施状态和最终证据。后续不自动发布、持久安装MCP配置或恢复作品建模。

两条独立审查共指出六处可执行性缺口，已由主代理补齐：导航 busy 生命周期前置、API 查询文件所有权、new/resume 握手、notification 与无效 JSON 错误区分、timeout/cancel 合同、资产路径钩子所有权。审查只针对本方案与源码关系，不是未来实现安全性或正确性已验收的证明。
