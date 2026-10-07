# Mini3D 原生动画第一版开发方案

版本：实施计划1.0，任务2026-10-07、收口2026-10-08。当前状态：R0–R5第一版开发、本机运行与每环节双线审计已完成；R5最终两路证据复核已关闭，交付限定本机候选，不是正式发布或全环境通过。来源边界及未验环境见[验收矩阵](native-animation-acceptance-20261007.md)。

## 1. 目标、来源与授权

本次交付是原生动画 P0 第一版，不只是播放演示：中文时间轴、对象关键帧、连续多圈旋转、独立预览与草稿、撤销、文件保存，以及配套 API/MCP 指定帧观察形成闭环。当前已存在的静态建模和两份用户作品必须保持。

依据：

- 用户提供《Mini3D_原生动画实现方案_专家审计版.docx》，版本 1.0、2026-10-07；原件 SHA256 为 `BEECAFA061B875ABE12CEA277F4BBDE70FA55D4D3CAE5343294F57D51EE2E12D`。
- [原生动画需求 Draft 0.1](native-animation-requirements-20261007.md)。
- 当前本地基线 `2c350d9dd01880881297e76139b980e3585947f1`，与报告锁定源码一致。
- [仓库执行规范](../AGENTS.md)、[现有架构](architecture.md)、[文件兼容](v2-scene-format.md)、[历史合同](v2-history.md)、[API/MCP 状态](api-mcp-implementation-status.md)。

文档内推荐及历史审计是设计输入，不是已经完成的代码或额外权限。用户已要求开发第一版和每环节专家审计，并在本轮明确批准后续所有施工场景：写格式4读1–4、旧稿首次升级另存、应用/API/MCP成套0.2.0拒绝旧0.1、wire1不变。保持本机同用户、默认关闭、现有读写根与权限、应用线程串行；本任务仍不自动提交、推送、发布、修改用户MCP配置或覆盖用户模型。

第一版开发与本机候选的完成条件是R0–R5实现、指定本机验证及双线审计关闭；验收要求中的未验环境仍须逐项保留，不能称为全环境门禁通过或产品正式上线。R1完成不能称动画产品已完成。跨轮保持长计划目标，遇到需要用户决定的边界停止依赖步骤，继续安全且已授权的工作。

## 2. 第一版范围与不做项

第一版覆盖对象、Empty、实体 Camera 和 Light 的局部 Position / RotationEulerXYZDegrees / Scale。稳定 EntityId 绑定，XYZ 整组关键帧；Constant / Linear；显式插入、替换、移动、删除；播放、子帧暂停、循环、跳帧；分组姿态草稿；唯一历史；保存重开；复制删除与动画绑定；14 项动画 API/MCP 能力和可信真实 PNG。

不包含自动插键、逐轴曲线编辑器、Ease、可见性/FOV/灯参数动画、骨骼、蒙皮、IK、约束、顶点/拓扑/材质动画、Shader 特效、物理、NLA、视频/PNG 序列导出、外部动画导入或任意脚本。城堡和基地的拆件、转轴修复不属于本次任务。

## 3. 实施合同

### 3.1 数学与定义

- `SceneAnimation`：默认 24 fps、1–250 帧；fps 1–120，帧域 1–100000，start ≤ end。范围缩小不删键，fps 改动不移动键。
- 原始关键帧为整数时间及完整 `glm::dvec3` 值；轨道按 EntityId/通道排序、键严格递增。全部候选合法后才能提交。
- 连续 Euler 度数不折叠，明确 `Rz × Ry × Rx` / `qz × qy × qx`。三角运算前 double 约减，运行结果才转现有 float TRS。
- 两键 1:Y=0、49:Y=720 在 24 fps 下转两圈；170→190 是 +20，170→−170 是 −340，不自动最短弧。
- 首次 Rotation 录键才逆解基础 quaternion，并验证重构；只录 Position/Scale 不反提角。无旋转轨道时原始连续角为缺省，不伪造零值。逆解y采用与正交矩阵asin公式等价的稳定 `atan2(-r20,hypot(r00,r10))`，保持±90°范围、1e-8奇异阈值和double域2e-8重构门禁，不因float转换放宽容差。
- Constant 在左键区间保持，到下一键精确切换；Linear 使用 double `std::lerp` 逐分量插值，并保留精确键/端值分支；键外保持端值；无轨回基础值。专家已复现普通加权公式使两个0.001端点下溢为0.0009999999999999998的反例，不放宽raw门槛来掩盖。
- raw 及转 float 后分别验证。Scale 每分量绝对值 ≥0.001；Linear 相邻键同号，Constant 可跳号。删除/移键/改插值形成的新邻接必须重新校验。
- Rotation 原值上限 3.6×10^9 度；Position/Scale 必须可转为有限 float。持久相等判断使用原始有限 double 与插值，不按 quaternion 或显示小数判断。

### 3.2 统一姿态与状态

基础 Scene、正式动画、临时求值/草稿三者分离。不调用现有写真源 `previewTransform` 充当动画预览，不每帧复制完整 Scene，不仅覆盖渲染矩阵。

Core 只生成数值 `EvaluatedPose`，包含 local/world/刚性 world rotation 与 EntityId。父先子后，未绑定轨道的后代仍继承父动画；设备方向不从带负缩放/剪切的矩阵分解。Renderer、选框、拾取、bounds、focus、Camera、Light、图标和截图使用同一已安装姿态。

R1的Core门禁负责数值local/world、可用inverse/normal、rigid rotation；R3的Editor门禁另负责缓存local AABB的实际float八角、设备view/projection和完整身份。Core通过不代表全部可绘制姿态已通过。按有界节点输入/迭代父先子后求值，不调用 `Scene::nodes()` 复制完整节点后才核预算、不对每节点重走 `worldMatrix()` 父链。10000层单位Empty父链须不递归完成；10001节点须在构造完整数值Pose前预算拒绝，不能把限制检查放在全部SceneNode副本分配之后。

Editor 安装包装包含完整文档 handle、内容版本、evaluationId、frame 和 mode。新文档发布前清旧姿态。基础 API 仍返回基础 TRS/world/bounds，显式 sample 返回动画结果。

| 状态 | 允许的动作 | 必须拒绝的动作 |
| --- | --- | --- |
| Base | 基础编辑、定义读写、开启预览 | seek/play，返回 PREVIEW_DISABLED |
| PreviewPaused | seek/play、定义编辑、保存、准备复制删除、Undo/Redo | 基础 TRS、网格/修改器、导入/OBJ 导出，须先关闭预览 |
| Playing | pause、状态/定义查询、纯 sample、既有 GUI 观察相机导航 | seek、写键、设置、保存、历史、capture |
| PoseDraft | 选中通道编辑、确认/取消、正式数据只读 | 切选区/时间/模式/pivot、导航、播放、历史、文件、capture |

Draft 要求 Object 模式、整数时间、普通编辑视图、没有既有 gesture。mask 是编辑许可，不只是提交过滤。G 用冻结 evaluated parentWorld 逆变换；R X/Y/Z 数值修改连续 Euler 通道，HUD 明示；S/R 使用对象原点，不使用游标/活动枢轴引起未选 Position 改动。自由轴 R 和鼠标自由多圈不进入第一版。gesture Esc 先撤 gesture，之后 Esc 才取消完整草稿；面板切焦点不丢草稿。

控制细项：`setPreview(true)`入口本身要求Object模式、无既有Object/component gesture。`setPreview` 在Playing/Draft一律BUSY，不能暗中pause；Base关闭、Paused开启是no_change。`setFrame`在Base为PREVIEW_DISABLED、Playing/Draft为BUSY。`play`在Base拒绝、Playing为no_change；`pause`在Base/Paused为no_change、Draft为BUSY。`setLoop`仅Base/Paused允许，Playing/Draft为BUSY。所有no_change先做身份/CAS/deadline校验。

实体Camera只读预览允许动画play/pause，seek仍限PreviewPaused；不得照搬现有 `camera_preview` 内容写busy守卫锁死这些控制。Draft必须无有效实体previewCamera。Paused进入Edit/F9先显式关动画预览，Playing/Draft仍拦截。Paused重命名可做，但混合name+transform patch含禁用字段时整次拒绝，不先改name。

### 3.3 时钟、版本与原子性

应用线程串行，QElapsedTimer 单调计时，QTimer 只负责约 16ms 唤醒。直接按 elapsed 求当前帧，不按回调次数或逐步累加；循环 `[start,end)`，单帧范围不启动。暂停保留子帧，隐藏停止且恢复不补跑。

tick 不改变 document/history/sessionRevision，只在成功安装新输入时更新 evaluationId。真实会话状态变化推进 sessionRevision，包括显式控制、自动终点、隐藏停止和故障停止；纯sample、tick和同输入no_change不推进。因此play之后无其它控制/停态的tick仍允许原会话CAS暂停，实际自动停止则使旧CAS过期。CAS/deadline 先于 no_change。pause 准入后必须停钟，即使最终求值失败也返回暂停和诊断。

Playing中的既有GUI编辑观察相机导航继续按原合同推进documentRevision、标脏而不入对象历史。这是用户导航的例外，不是动画tick写内容。导航前准备当前时间的新源数值姿态和合法视图；失败不改旧视图/文档/有效姿态。成功推进版本后绑定并发布新源Pose，时钟基线不重置，不能继续显示旧版本Pose或另起播放。动画实体Camera的输出不发cameraChanged回写editorCamera；Draft内禁导航。A07的“不标脏”测量须排除用户主动导航，A12/X04核对导航事件身份和不误写编辑相机。

正式编辑时：准备候选定义/索引/资源/所需时刻姿态与 bounds → 完整检查及 before-commit guard → 历史和内容安装 → 正式版本推进 → 绑定最终身份、无分配发布 → 通知消费者。不得 push 后重新分配求值。Undo/Redo 在当前时间求值失败仍恢复历史，但退出预览/清旧姿态并报告诊断。

### 3.4 历史、格式、API 和观察（依批准实施）

唯一 QUndoStack；动画编辑每操作一条历史，push 前判 no_change，不剪 redo。复制必须 GUI/API 共用 prepare 和固定旧→新 ID 映射；删除同时快照轨道，Undo 恢复原 ID。自身或后代有直接轨道的子树禁止换父，无轨子树保留原 keepLocal。

统一写场景 4、读 1–4；旧 1/2/3 首次升级另存，不覆盖。4 必需 animation 对象，独立 double 读写；严格字段、坏引用/重复/非法邻接拒绝，空轨先验证绑定再规范化。完整保留原网格、修改器、集合、cursor、相机、灯、相对资源和编辑视角。先临时加载完整合法文档再发布，QSaveFile/NewOnly 语义保留。

R2专项清单：Serializer内 `sourceVersion==3/!=3` 的cursor、editableMeshes及网格绑定能力判断改为明确兼容3/4；旧稿升级判定更新为 `<4`；LoadedScene和SceneDocumentData的默认版本同步。不能只加版本白名单。X15完整夹具重开和A09旧稿另存分别验证每条分支。

应用与适配器成套 0.2.0，wire 1 不变，原 47 方法保留业务语义；旧 0.1 明确版本失败。继续本机同用户、默认关闭、明确读写根、六种现有权限，不改网络、鉴权或线程模型。

14 方法：getState/listTracks/readKeyframes/sample/setSettings/upsertKeyframes/deleteKeyframes/removeTrack/moveKeyframe/setPreview/setFrame/play/pause/setLoop，统一 `animation.` 前缀。定义写用内容 CAS，会话控制追加 session CAS；沿现有 mutation 序号、账本与未知结果恢复。纯 sample 不安装 GUI。

capture 增加 expectedEvaluationId：暂停 → preview → seek → 状态核对 → admission 记录 afterFrameId/context → 新真实 paint → grab 后实际 stamp → 校验完整 doc/view/eval/context/frame → PNG/hash/尺寸 → MCP 再比较请求身份后呈图。Playing/Draft 拒绝；等待中任何身份变化主动终止；恰好完成一次，晚回调不能串请求。Hash 正确不证明帧正确。

## 4. 阶段任务与门禁

| 阶段 | 工作包 | 必须提交的证据 | 依赖 |
| --- | --- | --- | --- |
| R0 合同与计划 | 比对报告/本地源码；确认范围、迁移与 API；落本计划、审计记录、状态 | 来源/HEAD、授权边界、两路独立审计与问题关闭 | 无 |
| R1 独立 Core | 定义/严格校验/确定性取样；Euler helper；完整数值姿态与有限性；CPU 测试 | A01–A05、X01/X02 数学部分；无 Qt/GL、无基础 Scene 修改 | R0 数学关口 |
| R2 内容集成 | Scene 正式动画；prepared 命令/复制删除；格式 4/schema/加载预检/另存 | A07–A10/X15；精确 double、no_change 保 redo、坏文件不发布 | R1；格式明确批准 |
| R3 姿态与 GUI | InstalledPose；全消费者/bounds 缓存；时钟；中文时间轴/键表；Draft/快捷键/守卫 | A04/A06/A11–A13、X03–X10；真实 Qt/GL、DPI、资源复用 | R1/R2；身份接口先冻结 |
| R4 API/MCP/观察 | 14 方法、0.2 成套、权限/CAS/分页/ledger；真实指定帧 PNG | A14–A16/X11–X14；实际 SDK/MCP 和负向身份用例 | R2/R3；API 明确批准 |
| R5 交叉验收 | 中立夹具、原建模回归、性能/内存、中文使用说明、未闭问题处理 | A17/A18 + 所有门禁交叉检查；最终专家复核 | R1–R4 |

共享文件 `Scene/Serializer`、`SceneViewModel`、API schema/limits、CMake、依赖清单、progress 由单一所有者整合。不得所有代理同时写共享接口。纯 Core 文件与独立 UI 视图只有接口锁定后才并行；两名子代理常态并行上限，实施与审计同文件不能同时修改。

## 5. 每环节专家团审计机制

专家团指独立多代理技术审计，不是人类专家签字。用户本轮明确要求专家审查，适用仓库 8.8 条；不把附件此前的“20 项关闭”记成本机施工审计通过。

每环节设两条互补审计线：

1. Astra/high：数学、状态/边界、迁移或关键风险的专项审计；只读，不实施、不扩大权限。
2. 未参与该环节实现的 Sol/max：独立核对代码、接口、回归和验证证据；只读，与 Astra 不重复泛扫。

实施为 Sol/max，必要只读定位为 Luna/max。启动时显式传配置；工具不能提供最终 provider 生效回执时记录“请求配置、实际路由未独立验证”，不得用角色名证明切换。若指定档不支持，暂停受影响分派并报告，不静默换型号/降档。主会话模型由客户端决定，不能在本文声称已自动切换。

每轮流程：独占实现/候选 → 定向验证 → 两路独立审计 → 按 P0/P1/P2 列具体问题与反例 → 文件所有者修复 → 重跑受影响验证 → 原审计者针对复核 → 主代理检查最终 diff → 更新状态/追加日志。P0/P1 未关闭不进下一依赖环节；P2 只能有明确理由和剩余风险记录，不能一律忽略。

审计证据统一见 [本机动画审计记录](native-animation-audit-20261007.md)。每项记录来源、范围、严重性、反例/证据、处置、测试、关闭复核和限制。设计通过、编译通过、CPU 正确、真实显示正确、宿主 MCP 呈图正确是不同门禁，分别报告。

## 6. 预算与性能验收

采用报告第 11 章候选：3000 轨、100000 总键、10000 单轨键、1024 单次目标、列表默认256/最大2048、sample最多256实体、动画全 Pose最多10000节点、预检40000节点访问、候选64MiB、格式4源文件64MiB。静态预览关闭不因动画节点预算被新增全局限制。

R2预审修订：统一写格式4后，所有正式writer也必须在落盘前按实际UTF-8编码字节执行同一64MiB上限，超限不改变文件、路径或保存点；不能生成本程序无法重开的文件，不能静默写回3绕过限制。格式4读取必须在readAll/DOM/完整动画候选之前完成分块有界预检；顶层version可能位于文件尾，不能用头部正则猜版本。预检与正式解析对重复version/动画字段的处理必须一致。R2施工冻结原生容器深度64（根为1）、格式4重复version拒绝及动画字段严格检查；不把API的262144节点配额套给native场景。旧1/2/3正常读取合同保留。

定义编辑预检：局部原值/排序/邻接 + 当前 frame + 受影响新键/新邻接端点，时间去重后先核 N×|S|，不扫描所有曲线全部关键时刻。各运行时 Pose 再验证，有限时刻预检不证明全连续区间合法。

现 API 信封8/16MiB、JSON262144节点与PNG最长边1600/8MiB沿用。R4 将新 limits 纳入 methods.json 单点生成，Core 不依赖 API；R1 固定值为施工候选，后续须与生成值核对，不建通用可配置动画引擎。

中立性能夹具：100对象、300轨、9000键、约5万源点，包含父Empty、无轨子孙、非对称旋翼、Camera和Light。Release 预热后30秒×3；记录硬件/窗口/VSync、求值P50/P95/P99（P95目标≤5ms）、真实frameSwapped（P95目标≤41.7ms）、UI暂停/跳帧/单键/1024批次停顿、missed ratio、modifier revision、GPU上传、内存峰值与Undo后留存。不用用户作品作可覆写夹具，不把目标写成已达成绩。

普通主体构建继续只编译 `mini3d_editor`；测试只在显式目标执行，不自动挂 npm/测试到主体构建。本机 pwsh 已核验7.6.5；MSVC14.40.33807、Qt6.8.3、既有E盘build可用性按每轮实际结果记录。所有临时脚本/日志/构建使用验证过的E盘任务路径；Git隐式窗口禁止，子进程CreateNoWindow，不关闭任何用户当前窗口。

## 7. 验收和交接

沿任务书 A01–A18 与报告 X01–X15 全部保留；R1仅关闭纯数学子项，R2关闭持久/历史子项，R3关闭UI/显示子项，R4关闭MCP/截图子项，R5最终核对，不能合并成一个“测试通过”。

最小端到端：父Empty x0→10；独立非对称旋翼Y0→720；Camera两键运镜；GUI录键、13/25/37/49跳帧、播放/暂停、保存/重开；MCP对同一中立夹具写键/sample/seek/真实capture。后续覆盖负非均匀父链、失败原子、换文档同revision、二次paint、合法PNG错身份、账本恢复、键预算和UI焦点。

正式交付包括源码与定向测试、状态/审计与验证证据、格式/API使用说明、中文动画说明及中立样例；不自动生成公开Release或上传。每完成一个记录任务只向 progress.md 末尾追加，保留已有模型/文档和历史字节。

回滚原则：先确认没有后续依赖和用户新编辑；基线为上述HEAD，新模块按本轮清单移到独立E盘归档，现有共享文件按定向反向补丁撤回，保留原未提交内容；progress只追加撤回说明。不reset、stash、force push、不整体覆盖日志。格式4文件不能回写旧版而丢动画，作品始终保留原件。

当前交付已收口，可按中文教程打开中立样例并先另存副本。R5实际证据见第13节；未验环境与发布项保留，不自动扩展到骨骼/Shader、作品改造或发布施工。

## 8. 本轮实际交付与 Core 使用

已完成R1：`src/core/Animation.h/.cpp` 的定义、校验、确定性double取样和Euler转换；`src/core/EvaluatedPose.h/.cpp` 的完整数值姿态；`tests/AnimationTests.cpp` 的16项定向CPU测试。两处CMake只增加源码与独立CPU测试，不改变主体/测试分离。

调用示例（纯C++，不安装GUI、不产生历史、不保存场景）：

```cpp
using namespace mini3d::core;
const std::array<AnimationPoseInput, 2> inputs{{{1, 0, {}}, {2, 1, {}}}};
SceneAnimation animation;
animation.tracks[{2, AnimationChannel::RotationEulerXYZDegrees}] = {
    {{1, {0, 0, 0}, AnimationInterpolation::Linear},
     {49, {0, 720, 0}, AnimationInterpolation::Linear}}};
const auto result = evaluateAnimationPose(inputs, animation, 25.0);
// 成功时 result.pose->find(2)->rotationEulerXYZDegrees->y 为360，基础inputs不变。
// 失败时没有部分Pose，result.validation定位失败类别/实体/通道。
```

使用方必须提供完整场景的最小数值span并保证父先子后；Core不能知道调用者是否漏了无轨后代。空轨应在R2外部结构/绑定校验后规范化移除，Core只接受非空规范轨道。checked入口每次校验定义，当前不假设有持久缓存；全部键扫描成本纳入R5实测。

实际验证：

- Debug与Release均重新编译成功，分别16用例/997断言全部通过；完整CPU CTest分别1/1通过（1.47s/0.14s）。
- 普通 `mini3d_editor` Debug目标单独构建成功，日志没有编译任何测试目标。
- 两路R1独立审查通过。测试自身的超域cast和NaN比较两项问题已定向修正再重新编译/运行，见审计记录。
- 新源码/文档及改动CMake严格UTF8无BOM，diff空白及progress历史字节前缀检查列入最终交付验证。

证据保存在 `E:/Mini3D/out/validation/native-animation-20261007-66e24b`。起初受限构建的vcpkg目录访问被拒绝，核对实际日志后改用批准的构建权限，已有依赖确认全部安装，没有升级或下载包；本机shell7.6.5，CMake构建环境发现可运行的Store pwsh7.6.6。没有修改用户MCP配置、关闭窗口或上传。

R1封板时尚未实现：Scene正式动画存储/格式4/QUndoStack命令、时间轴/播放控制/Draft、统一Renderer与拾取接入、动画API/MCP与指定帧真实图片、性能/内存和端到端验收。当时UI仍不能使用原生动画；该段保留阶段来源，不描述最终源码状态，后续实际实施见第10–13节。

## 9. R2双线只读预审与批准后的施工落点

本节保留批准前的历史预审与施工落点，当时格式/API批准尚未收到，未修改产品源码。之后用户已明确批准，第10节记录实际R2实施；本节不能充当实施后的审计证据。两条审计线的请求配置与实际路由限制继续适用第5节。

### 9.1 格式、加载与保存

| 实际入口 | 已观察事实 | 批准后的精确动作 |
| --- | --- | --- |
| [SceneSerializer.cpp](../src/core/SceneSerializer.cpp)，228/300 | 固定写3、只读1/2/3 | 写4读1–4；4必需animation；1/2/3携带animation按任务书明确拒绝，不能忽略 |
| 同文件，339/345/413 | cursor、editableMeshes、editableMesh绑定分别使用==3/!=3 | 逐处明确兼容3/4；集合<3和设备>=2的既有能力判断保留，不全量替换数字 |
| [SceneSerializer.h](../src/core/SceneSerializer.h)，41；[SceneDocument.h](../src/assets/SceneDocument.h)，18 | 两份来源默认值均为3 | 同步默认4，但读入保持真实来源版本 |
| [SceneViewModel.cpp](../src/editor/SceneViewModel.cpp)，3914/4085 | 只有来源<3才记旧稿路径，保存拒绝旧原路径 | 来源<4，真实v3也进入保护；保留大小写/解析后路径比较和成功写完才解除保护 |
| [MainWindow.cpp](../src/editor/MainWindow.cpp)，1461 | 升级建议名为-v3.m3dscene | 改v4；取消仍无修改 |
| [EditorApiService.cpp](../src/editor/api/EditorApiService.cpp)，803–809 | file.save已经检查requiresSaveAs | 保留现入口，验证v3不能绕过另存；本阶段不新增动画API |
| SceneSerializer.cpp，17–34 | 通用vector/number链使用float | 动画独立double链，原值精确往返；不得先转glm::vec3/get<float>() |
| [SceneDocument.cpp](../src/assets/SceneDocument.cpp)，83/96–136/157–204 | readAll后DOM；临时资源与Scene成功后才返回；写正式Scene | 加格式4有界预检与正式动画数据；不保存临时Pose；保留QSaveFile/NewOnly及最终guard |
| [格式3 schema](scene-format-v3.schema.json) | 现schema锁定3 | 保留旧定义，新增格式4/animation合同；新动画严格字段，不顺手改变旧顶层未知字段兼容性 |

原生预检的实现方向是分块扫描、正确处理字符串/转义/嵌套、识别真正的顶层version，不累计整份JSON；v4再进入受限读取和完整语义解析。版本字段位于尾部、重复版本、字符串内伪字段必须有负向用例。具体深度与错误合同是施工前门禁，不将尚未决定的方案写成已实现。

解析动画先检查结构、字段、合法实体引用和重复(EntityId,channel)，再规范化合法空轨。重复空轨不能被移除规则掩盖；键原顺序必须严格递增，不能先排序或map覆盖后再查重复。

### 9.2 历史与对象生命周期

| 实际入口 | 已观察事实 | 批准后的共同施工合同 |
| --- | --- | --- |
| SceneViewModel.cpp，3420；[SubtreeCommand.cpp](../src/editor/SubtreeCommand.cpp)，34–43 | GUI复制首次redo才调用duplicateSubtree并捕获副本 | 改为push前完成复制与曲线预算/姿态准备；固定旧→新映射；保留GUI选副本、API不抢选择以及名称差异 |
| SceneViewModel.cpp，1370；[Scene.cpp](../src/core/Scene.cpp)，675 | API复制已有prepareDuplicateSubtree和entityIdMap | 作为共用准备基础；新曲线独立、Undo/Redo不重分身份 |
| SceneViewModel.cpp，3429/1418；Scene.cpp，1164/1255 | 删除用SubtreeSnapshot；restore复制集合、emplace节点、insert兄弟；删除递归并复制children | 删除前准备原ID节点、成员、轨道和恢复容量；安装/恢复不重新分配候选或身份，不能只给旧命令附加曲线快照 |
| SceneViewModel.cpp，2626/1464；Scene.cpp，1272 | GUI/API换父入口分离，Core写入新父children | 两入口共用整个子树轨道守卫；真正换父前预留容量和顺序；同父no_change；无轨子树仍keepLocal |
| [SceneTreeModel.cpp](../src/editor/SceneTreeModel.cpp)，162；MainWindow.cpp，1427 | 树拖放和菜单经GUI换父 | 纳入验证，避免只保护API或属性面板 |
| SceneViewModel.cpp，338/2699/2707 | push/Undo/Redo内命令先通知，之后才recordApiCommit | 提交包装保证准备→最终guard/来源复核→内容/历史安装→版本推进→绑定姿态身份→通知；不能把旧姿态贴成新版本 |

禁止把API默认2048子树上限或10000动画Pose节点预算扩大为普通静态GUI操作的全局上限。共用prepare保留调用方策略。允许准备阶段消耗身份高水位形成空洞，但失败不得发布节点、曲线或返回新映射。

动画修改用原始有限double及插值判no_change，在push前返回，不先push再obsolete以免剪redo。Undo/Redo当前时刻求值失败仍恢复历史内容，再退出预览/清旧姿态/报告诊断。正式定义快照的可回放存储、旧历史命令的候选姿态适配或清姿态故障路径，需要R2/R3接口冻结后实施，不在本轮凭空添加通用框架。

### 9.3 最小施工顺序与定向验收

1. Scene正式持有动画，提供绑定校验、精确before/after准备及无分配安装；完整数值span包含全部节点和无轨后代。
2. 冻结复制、删除、换父共同准备结果：ID映射、原ID恢复、轨道候选、容量/预算和姿态输入；再统一GUI/API提交，不增加第二历史栈。
3. 接版本/通知、no_change及历史求值故障合同；再接文件4、另存保护、读取预检、double写读与schema。
4. 实现后运行定向测试，重新进行Astra专项与独立Sol代码审计、修复/复核，才可关闭R2并进入R3。本次预审不能代替施工后审计。

可复用现有测试入口：[FileCompatibilityTests.cpp](../tests/FileCompatibilityTests.cpp)、[SceneSerializerTests.cpp](../tests/SceneSerializerTests.cpp)、[SceneDocumentTests.cpp](../tests/SceneDocumentTests.cpp)、[EditorCommandTests.cpp](../tests/EditorCommandTests.cpp)、[PreparedSubtreeTests.cpp](../tests/PreparedSubtreeTests.cpp)、[ObjectApiTests.cpp](../tests/ObjectApiTests.cpp)。它们当前只证明既有静态合同，新增动画断言必须另落实现。

必须增加的证据：完整v4真实文件重开全部字段/资源/修改器/集合/设备/游标/动画；真实旧1/2/3另存与原字节保护；宽ID及720.000000001/3599999999.9/3600000000精确往返；缺/未知字段、重复/乱序/坏引用/非法邻接/旧版带动画的整次拒绝；合法与非法空轨；64MiB读写对称、版本尾部和深度/轨键提前预算；final guard/NewOnly竞争失败不发布；GUI/API复制固定映射和曲线独立；删除Undo恢复原ID；后代轨道阻止换父；no_change保redo/clean点；静态GUI不继承API新限制；版本/姿态/通知一致和历史故障恢复。

## 10. R2实际实施与收口证据

用户明确批准后，Scene正式持有不可变动画，新增PreparedAnimation/PreparedSubtree/PreparedParentChange及完整候选数值输入；GUI/API复制删除与换父共用发布前准备路径。精确no_change保redo，删除恢复原ID/轨道/成员，通知在内容版本推进后发出且选区有效。格式4写/读、旧1/2/3首次另存保护、double独立编解码、固定内存版本探测、有界SAX、同句柄限长读取与64MiB writer/schema已实施。详见[格式4说明](native-animation-scene-format-v4.md)。

实际Debug/Release均完成：动画CPU47用例64466断言；完整CPU CTest各1/1；真实文档7用例563断言；Qt历史10用例149断言；相关编辑器回归各82通过、1既有junction夹具跳过、18438断言通过；主体单独构建均输出Mini3DStudio.exe且没有测试target。原始日志位于`out/validation/native-animation-20261007-66e24b/r2-implementation`，实际审计闭环见[审计记录](native-animation-audit-20261007.md)。

实施后审计修补包括：候选换父/复制/删除完整Pose预检；预检后文件增长/缩短的限长读取；历史版本/选择/modelReset通知一致性；去重/changed-only与40000访问边界集成测试。既有迁移夹具同步为真实1/2/3；OBJ大名称限额夹具改用合法旧3输入，未放宽格式4 writer。

限制：R2数值预检仍固定第1帧，尚未保留/安装带完整文档身份的显示Pose；几何八角、设备VP、播放/时间轴/Draft、R4动画方法和真实指定帧PNG待下一阶段。无分配回放依据为实现结构及地址/容量测试，没有全局分配失败注入。junction跳过不记作已验，不将R2通过称第一版全量完成。

## 11. R3实际实施与封板

统一InstalledPose已经接入渲染、拾取、选框、focus、设备和真实帧身份；几何局部盒与完整隐藏掩码冻结并复用。单调播放时钟、四种会话状态、中文时间轴/键表、连续Euler姿态草稿、受限G/R/S、复制删除及当前时间历史回放已落地。普通属性仍显示基础值，预览与草稿不污染基础TRS；时间轴默认收起，从“视图→动画时间轴”打开。

两路审查发现并关闭的实际问题详见审计记录，包括来源重入、通知早于版本、pause包装失败、几何revision绑定、极点真实矩阵导航、手势失效引用/取消失败、持续及准备中途Provider异常历史恢复，以及GUI设备编辑误用API Busy。原FOV真实画面回归的失败日志保留；私有共用提交主体恢复GUI预览许可，公共API限制不变，并在取消旧手势的同步通知后重新核对动画准入。

最终证据目录：`out/validation/native-animation-20261007-66e24b/r3-implementation`。

| 门禁 | Debug | Release |
| --- | --- | --- |
| 最终Editor构建 | Build-Debug-mini3d_editor_tests-scale1-203041310.log，exit0 | Build-Release-mini3d_editor_tests-scale1-203626850.log，exit0 |
| 会话 | Test-Debug-mini3d_editor_tests-scale1-203121117.log，27/1001 | 与InstalledPose合并Test-Release-mini3d_editor_tests-scale1-203703447.log，36/11370 |
| InstalledPose | Test-Debug-mini3d_editor_tests-scale1-201205436.log，9/10369 | 包含于上述36例 |
| 最终真实UI | Test-Debug-mini3d_editor_tests-scale1-203315687.log，18/340 | Test-Release-mini3d_editor_tests-scale1-203844243.log，18/340 |
| 导航/历史/API/设备/对象相关回归 | Test-Debug-mini3d_editor_tests-scale1-203147410.log，83/6449 | Test-Release-mini3d_editor_tests-scale1-203718703.log，83/6449 |
| 主体独立构建 | Build-Debug-mini3d_editor-scale1-203910775.log，exit0 | Build-Release-mini3d_editor-scale1-204028880.log，exit0 |

Core新交换接口两配置各3/38，真实GPU两配置各2/272；完整CPU CTest两配置1/1（Debug25.24s、Release3.49s），完整Release GPU CTest1/1（1.03s），这些模块冻结后未重复无收益验证。两路审查者亲读实际最终日志，静态发现与对应运行闭环；没有全局分配失败计数，Provider异常为定向注入，不宣称整个分配路径均已注入。

DPI分别取得actualDPR=1、1.5、2，逻辑/像素尺寸为1440×900/1440×900、1440×900/2160×1350、1280×680/2560×1360；最新Draft定向用例各1/45。三档先前全套UI17/318与最终scale1全套18/340分别保留版本边界，不能称三档最新全套均为18例。主代理亲看三档PNG，中文字段及确认/取消按钮可见；200%视口较矮但仍在窗口内。

R3封板时剩余项为R4和R5，当时公共API/MCP仍为0.1；现在R4成套0.2最终运行已通过，见第12节、[R4合同](native-animation-r4-contract.md)及[调用说明](native-animation-api-mcp.md)。原FileApi junction夹具缺口仍留到交叉验收；本阶段定向回归不等同全仓全量验收。没有提交、推送、发布、修改用户MCP配置或覆盖作品；模型请求配置及实际路由限制沿第5节。

## 12. R4最终实施与验收

14项类型化动画事务、双CAS、分页、完整预算、纯sample、准确Busy/来源错误、账本恢复及指定帧真实capture已经落地。应用/API/MCP成套0.2.0，wire1，原47业务方法保留；61为完整静态目录，有观察服务的实例实际61、无观察服务57，权限仍以运行describe为准。

最终Debug组合266例=263通过/3环境跳过，46203断言通过（`r4-implementation/Test-Debug-mini3d_editor_tests-224604325.log`）；最终Release同组同成绩（`Test-Release-mini3d_editor_tests-225515852.log`）。API独立23例2570断言通过；真实GL观察12组包含两项析构生命周期反例。MCP固定源码的完整49/49、定向11/11及typecheck/build已通过，不重复无收益运行。

额外发现并修复：Context在Qt基类析构阶段才发销毁信号，晚调用已结束生命周期的Viewport成员；MainWindow全局输入过滤器晚于ViewModel销毁。两项均有修改前真实失败反例及修改后无探针组合通过。旧batch守卫夹具因Undo也授权而无限递归，改为一次性重入；真实batch来源错误复用原请求返回准确内容CAS字段。Release旧账本测试保存临时QJsonValueRef悬空，改为持有QJsonValue；最小1/38及完整Release通过，Debug重新构建后bridge20通过/2环境跳过、548断言通过（`225652790`）。未取得原间歇SIGSEGV的直接AV栈，不能认定两生命周期缺陷是它的确定或唯一根因。

主体最终独立构建Debug/Release均exit0、无test target（`Build-Debug-mini3d_editor-225702060.log`、`Build-Release-mini3d_editor-225736744.log`）。重新对最终主体执行官方SDK/正式adapter实际MCP：`Real-Debug-225814480.log`、`Real-Release-225748757.log`，分别对应`r4-real-1791385090186/evidence.json`与`r4-real-1791385065689/evidence.json`。两配置覆盖61目录、连续double两圈、无轨后代、纯sample、稳定分页、原子拒绝、13/25/37/49真实PNG、旧eval拒绝、tick后原sessionCAS pause及中文格式4保存重开。主代理亲看最终Release全部四张图，位移和非对称旋翼方向一致；不是mock或仅哈希验证，也不是Desktop当前聊天直接工具热加载验收。

三项环境跳过为缺isolated junction夹具、无已有映射网络盘、无可访问FAT/exFAT卷；R5补第一项，后两项明确保留环境限制，不创建用户盘符或改磁盘格式。所有失败日志保留，临时诊断探针已撤，未安装全局调试器或修改WER。R5未完成之前仍不宣称第一版全量交付。

## 13. R5最终本机运行与交付

R5使用独立中立作品，不覆写用户城堡/基地。新增中文教程及两张真实UI说明图、五对象两轨的格式4样例、隐藏Release性能测试和A01–A18/X01–X15交叉矩阵，入口见[教程](native-animation-user-guide.md)、[样例](../assets/samples/native-animation-neutral.m3dscene)、[验收](native-animation-acceptance-20261007.md)及[性能实测](native-animation-performance-20261007.md)。

固定性能夹具为102节点、100有轨、300轨/9000键、48500可编辑源点，含父Empty、无轨子孙、非对称旋翼、Camera/Light、实际Mirror→Subdivision。预热后独立30秒×3均足额720/720槽；Core P95为0.2194/0.2173/0.2371ms，真实swap P95为17.1054/17.0920/17.1105ms。正式服务单键/1024实际变化批次同步耗时3.0224/9.2944ms，匹配新换帧反馈10.4951/17.0381ms。上传窄计数增量0、身份失配0、版本/相机/正式数据不变；内存与Undo保redo口径见报告，不称动画独占峰值或无泄漏。

交叉回归中修复三处真实回归：新文档发布内部先清旧Edit/mesh/组件选择并在新源安装后通知；场景树菜单在reset守卫内提前关闭，避免焦点恢复覆盖副本选择；读取预检失败保留中文外层分类与实际底层原因。没有放宽操作守卫或断言来让失败通过；具体反例和专家复核见[审计](native-animation-audit-20261007.md)。

| 最终动作 | 成绩/来源 |
| --- | --- |
| Release默认完整Editor | 514例=512通过/2环境跳过、56201断言；`r5-implementation/Test-Release-mini3d_editor_tests-234435835.log` |
| Debug受影响Editor | 32例1472断言；`Test-Debug-mini3d_editor_tests-235201435.log` |
| Debug动画文档/新样例 | 8例589断言；`Test-Debug-mini3d_asset_tests-235448515.log` |
| Release完整asset | 57例=54通过/3symlink权限跳过、1632断言；`Test-Release-mini3d_asset_tests-235604472.log`；两junction已补夹具通过 |
| 两主体独立构建 | Debug `235716545` / Release `235721277` exit0无tests；16测试exe/PDB路径存在性及已有hash/time不变，`body-build-2e7efdda13d642baa9ba87e9968bafc5.json` |
| 两最终主体正式SDK/MCP | `Real-Debug-235830933.log`、`Real-Release-235843394.log`；实际61目录、四帧image、double、pure sample、分页/原子拒绝、旧eval、原session CAS pause、中文保存重开均passed |
| 隐藏Release性能 | `234134885`，1例14断言；最终证据`performance-42b44eba23534164a830f698323813e9` |

无前缀的R5测试/build/JSON均在证据根`out/validation/native-animation-20261007-66e24b/r5-implementation`。最终真实MCP目录为`r4-real-1791388706365`（Debug）和`r4-real-1791388720176`（Release），位于其父证据根；主代理已亲看最终Release全部四图。

最终两路证据复核已关闭：Astra允许在当前Windows固定环境及明确边界内本机候选收口；独立Sol核实现/指南/样例/矩阵与最终日志无实质性不一致、无未解决P1/P2。请求Sol/max、Astra/high，实际provider路由仍无独立回执。2 Editor环境skip、3 asset符号链接权限skip、真实IME/mixed-DPI/新机器/耐久/Desktop本聊天等限定，以及原间歇SIGSEGV缺直接AV栈，均保留到验收矩阵；不把本机候选包装成全环境发布。旧ZIP保持0.1快照，未自动重打或上传。
