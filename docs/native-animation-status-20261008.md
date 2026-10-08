# Mini3D 原生动画实现现状与升级参考

更新日期：2026年10月8日。适用对象：产品负责人、后续调研 AI 和开发代理。源码工作目录：`E:/Mini3D`。

## 当前结论

Mini3D 已完成原生对象动画第一版的 R0–R5 开发和本机候选验收。现在可以制作对象平移、连续多圈旋转、缩放、父子联动，以及实体相机和方向光的变换动画；GUI 和 API/MCP 共用正式动画数据、求值和撤销体系。它不是完整的 Blender 动画系统，也尚未达到全环境正式发布口径。

下一步升级不必重新建立动画基础，但应先决定要提升哪类创作能力：更方便地编辑对象动画、输出可交付的动画结果，还是进入角色骨骼动画。这三类目标的依赖与改动规模不同，不能仅按“动画已经完成”推断剩余工作很少。

本总结只记录现状，不修改动画实现或启动升级，也不代表重新全量验收。后续按用户授权同步 GitHub，不等于发布新安装包。

## 1 版本与交付基线

| 层次 | 当前状态 | 后续使用注意事项 |
| --- | --- | --- |
| 已提交及上次推送的动画第一版 | Git 提交 `ebe24ce15cda0c2e9dde6e8c223d89e80e054eaf`，标题 `feat: add native animation workbench and MCP controls`；仓库 [shenxi11/Mini3D](https://github.com/shenxi11/Mini3D) | 可作为第一版源码基线，不代表正式签名发行版 |
| 当前源码追加修复 | 在上述第一版基线上，已修复取消对象变换破坏复制撤销的 P2；修复与本总结均纳入本次 Git 提交范围 | 初稿编写时修复尚未提交；后续阅读应检查包含此修复的新提交，而非只读取第一版基线；详见第10节 |
| 应用和自动化版本 | 应用、API、MCP 成套 `0.2.0`；私有传输 `wireVersion=1` | 旧 `0.1` 客户端明确拒绝，不能与新主体混用 |
| 场景文件 | 写格式4，读取格式1–4 | 旧1/2/3首次升级必须另存；旧格式3程序不能继续编辑格式4 |
| 安装包 | 原 `0.1` 历史候选 ZIP 未同步更新 | 不能拿旧包判断当前源码是否支持动画；没有宣称新安装包已发布 |
| 构建入口 | 主体与测试目标仍分离，测试目录 `EXCLUDE_FROM_ALL` | 正常主体编译不应顺带编测试；动画升级不得破坏这项约定 |

当前已实测环境是 Windows x64、Qt 6.8.3、MSVC 14.40.33807、OpenGL 4.1；项目使用 C++20 和 Qt Widgets，不依赖 Qt3D、Qt Quick 3D 或完整游戏引擎。

阶段文档保留历史口径。例如格式4说明开头仍有“R3/R4待完成”，API说明开头仍有“R5待完成”。当前完成状态应以[最终验收](native-animation-acceptance-20261007.md)、[开发方案最终章节](native-animation-development-plan-20261007.md)及 `progress.md` 最新追加为准，不把阶段开头当成当前产品状态。

## 2 已实现的创作能力

| 能力 | 当前可以做什么 | 主要边界 |
| --- | --- | --- |
| 动画对象 | 普通对象、Empty、实体 Camera 和方向 Light 可录关键帧 | 不包含骨骼、蒙皮、顶点或材质通道 |
| 位置和缩放 | 编辑对象相对父节点的局部 XYZ 数值 | 每个通道是一整组 XYZ，不是三条独立轴曲线 |
| 旋转 | 连续 Euler XYZ 度数，支持多圈和明确旋转方向 | 不自动选择最短弧；首次从基础四元数提角不能恢复历史转数 |
| 插值 | Constant 和 Linear；关键帧外保持首末值 | 没有 Ease、Bezier、切线手柄、逐轴插值或曲线编辑器 |
| 关键帧编辑 | 插入/替换、移动、删除、修改插值 | 当前对象键表分页；不是完整 Dope Sheet 或 Graph Editor |
| 播放控制 | 播放、暂停、循环、整数帧和子帧定位、范围与帧率设置 | 存储的关键帧必须为整数帧；播放状态不保存 |
| 姿态草稿 | 在暂停的整数帧独立调整勾选的通道，确认后录键，取消不写真源 | 草稿期间禁止导航、切帧/选区、播放、保存和撤销 |
| 视口操作 | 草稿支持受限 G/R/S；G 可换算世界移动，R 支持指定轴数值增量，S 为原点倍率 | 不支持自由轴鼠标多圈旋转；手势确认不等于草稿已经录键 |
| 父子动画 | 父级动画影响无轨子孙，部件可围绕 Empty 轴心联动 | 含直接或后代动画轨道的子树，实际换父会拒绝 |
| 相机和灯光 | 动画姿态驱动实体相机视图和方向光方向 | 不支持相机 FOV、灯色/强度或可见性关键帧 |
| 撤销和对象生命周期 | 动画与建模共用历史；复制独立轨道，删除 Undo 恢复原 ID 和曲线 | 不能用近似浮点相等替代精确正式快照合同 |
| 文件闭环 | 保存基础场景及正式关键帧，打开后回基础模式 | 不把当前预览、草稿或播放进度烘焙进基础模型 |
| 自动化 | 14项动画 API/MCP 方法，可写键、分页读取、取样、控制和指定帧抓图 | 截图是单帧真实 PNG，不是视频或 PNG 序列导出 |

时间轴入口是“视图 → 动画时间轴”，默认收起，与底部控制台切换。常规属性面板仍显示基础模型值，不会随播放变成动画值；动画姿态应在视口、时间轴及取样结果中观察。

### 当前适合的作品

现有能力适合旋翼、轮子、机械部件绕轴运动、父节点带动整组部件移动、简单镜头移动，以及对象出现位置或尺寸的离散/线性变化。合理组织 Empty、部件轴心与父子关系后，可以制作由刚性零件组成的简单机械动画。

当前不适合直接完成角色走路、蒙皮变形、IK 手脚约束、复杂镜头曲线、表情、材质发光变化或动画成片输出。旋翼、关节部件的对象动画不等于骨骼系统，也不会自动拆分现有整体模型或重新安放轴心。

## 3 当前操作闭环与容易误解的语义

标准流程是：在基础模式整理模型与层级 → 录起始键 → 开启动画预览 → 暂停定位末帧 → 编辑姿态草稿 → 确认草稿并录键 → 检查中间帧 → 播放 → 暂停后保存。

已有[中立示例](../assets/samples/native-animation-neutral.m3dscene)包含5对象、2轨、24fps、1–49帧，无外部模型或纹理依赖。父 Empty 沿 X 从0移到10，转轴 Y 从0转到 `720.000000001` 度，子部件继承姿态；它不是百对象性能夹具。

| 容易混淆的行为 | 当前真实含义 |
| --- | --- |
| 字段填写 `Y=720` | 设置该帧局部 Y 连续角为720；有非零起始角时不自动表示增加两圈 |
| 草稿内 `R Y 720` | 在当前草稿连续角上增加720度；与字段直接设置不同 |
| 170→190 | 增加20度 |
| 170→−170 | 减少340度，不自动走最短弧 |
| 0和720的终态画面相同 | 只能说明终态朝向相同；转数需看中间帧和连续原值 |
| 循环1–49帧 | 使用起止帧半开区间；精确末键49应暂停后定位观察 |
| 调整帧率/缩小播放范围 | 不移动或删除既有关键帧 |
| `entity.get` | 返回基础对象变换，不是当前显示的动画姿态 |
| `animation.sample` | 对正式动画纯取样，不安装到窗口，不改变播放、历史或版本 |
| 暂停预览 | 仍不是基础编辑模式；修改基础 TRS、网格、修改器或导入/OBJ导出应先关闭预览 |
| 播放不标脏 | 播放本身不改内容；主动导航编辑观察相机仍遵循原项目保存规则 |

详细操作见[中文使用说明](native-animation-user-guide.md)。教程中的两张真实截图来自独立立方体 UI 夹具，不应当作最终旋翼 MCP 图片或完整作品验收。

## 4 动画数据与求值结构

### 三类数据严格分离

基础 TRS 是模型的静态局部变换。正式动画保存完整 double 关键帧原值。运行预览持有某一时刻的已求值姿态和显示身份，不能写回前两者。只有确认正式定义编辑才产生内容变更和一条共享撤销记录。

`SceneAnimation` 当前是一份场景级设置和轨道集合。轨道以稳定 `EntityId + AnimationChannel` 绑定，通道只有 `position`、`rotationEulerXYZDegrees`、`scale`。每个键保存整数帧、`glm::dvec3` 原值和插值方式；尚无多个 Action/Clip、片段混合或 NLA 数据模型。

旋转原值不折叠到0–360。只有运行求值时才约减角度并转换成实际 float TRS，避免保存时丢失多圈和 double 精度。无旋转轨的对象保留基础四元数，不凭空生成“零 Euler”字段。

### 从定义到可见画面

1. Core 接收覆盖完整场景、父先子后的最小数值输入及正式动画定义。
2. `evaluateAnimationPose` 校验并计算完整 local、world、worldInverse、worldNormal 和刚性世界旋转；非递归，不反复走每个节点的父链。任一节点失败，整次结果拒绝。
3. Editor 准备不可变几何范围、可见性、设备数据和姿态包装，核对真实 float 角点与相机投影等消费者条件。
4. 在最后来源和提交守卫通过后，安装内容/历史、推进版本、绑定已准备姿态，再发布有效选区和场景通知。
5. Renderer、拾取、边框、focus、设备和真实帧观察共用同一 `InstalledPose`，不分别重新解释动画。

Core 的有限矩阵校验不等于任意几何和投影都可消费，Editor 层校验不能省略。预览缺失节点时不能悄悄回退为基础姿态，否则画面、拾取与截图身份会不一致。

几何范围在准备阶段扫描；播放 tick 复用几何快照，不每帧扫描源点或上传未变网格。设备方向使用独立刚性旋转，不从带负缩放或剪切的世界矩阵猜方向。

## 5 会话状态与历史边界

| 会话模式 | 定位和播放 | 正式定义编辑 | 基础模型编辑 | 可信截图/保存 |
| --- | --- | --- | --- | --- |
| `base` | 不能 seek/play，可开启预览 | 允许，通过真实准入与守卫 | 按原建模规则 | 可抓基础画面和保存；frame只是游标 |
| `preview_paused` | 可定位、播放和关闭预览 | 允许；草稿须满足选区、整数帧等条件 | 基础 TRS、网格和修改器编辑拒绝 | 可抓指定帧和保存正式定义 |
| `playing` | 可暂停；不能 seek | 拒绝 | 拒绝 | 拒绝 capture 和保存 |
| `pose_draft` | 先确认或取消草稿 | 外部正式写拒绝，GUI可确认草稿 | 拒绝 | 拒绝 |

该表不是无条件许可。真实导航、模态交互、未决写槽和提交中等 Busy 状态仍会阻止操作。Playing/Draft 可以查询正式动画和纯 sample，但不能绕过真实基础交互阻塞。

时钟使用单调 elapsed，定时器只负责唤醒。普通播放 tick 不推进正式内容、历史或 `sessionRevision`；显式控制、到终点、隐藏或故障停态等状态变化才推进会话身份。暂停准入后必须实际停钟，后续姿态失败会返回诊断，不能报告成“完全没执行”。

动画与建模使用唯一 `QUndoStack`。精确 `no_change` 不剪掉 redo、不改变保存点，但仍须通过身份、CAS、期限和最后守卫。复制时新 ID 在入栈前固定，Undo/Redo 不重新分配；删除恢复原对象、轨道和相关快照。

## 6 API 与 MCP 的现状

静态完整方法目录共61项：原47项加14项动画方法。有观察服务的实例报告61项，没有观察服务的实例报告57项；最终可执行能力及权限以运行实例 `describe` 为准，不以静态数量替代授权。

| 分类 | 当前方法 | 权限与影响 |
| --- | --- | --- |
| 查询 | `animation.getState`、`animation.listTracks`、`animation.readKeyframes`、`animation.sample` | `scene.read`；读取正式定义/状态，无内容历史 |
| 定义写 | `animation.setSettings`、`animation.upsertKeyframes`、`animation.deleteKeyframes`、`animation.removeTrack`、`animation.moveKeyframe` | `scene.write`；完整候选原子提交，一次一条共享历史 |
| 会话控制 | `animation.setPreview`、`animation.setFrame`、`animation.play`、`animation.pause`、`animation.setLoop` | `viewport.control`；需内容与会话双 CAS |
| 真实单帧图像 | 既有 `viewport.getState`、`viewport.capture` | 截图另需 `viewport.observe`；只接受基础或暂停状态 |

MCP 名称按小写下划线加 `mini3d_` 转换，例如 `animation.getState` 对应 `mini3d_animation_get_state`。MCP 适配器管理连接会话和 mutation 序号，调用者仍要传入完整文档身份及内容/会话 CAS。

目前没有独立“修改插值”API方法；可通过 `upsertKeyframes` 的显式替换写入完整键值和插值。不要在升级接口清单里误算为已有第15项。

### 指定帧观察与写入恢复

可信截图流程是：查动画状态 → 开启预览 → 定位指定帧 → 查视口状态 → 带当前身份 capture → 等待新真实 paint → 返回经过核验的 PNG。

核验包括完整文档、内容/视图/求值版本、帧、模式、会话、Context 代际和新 frameId，不是仅核 PNG hash。即使 PNG 字节合法，身份不匹配也不能呈图；账本重放返回的旧 `evaluationId` 不能当作当前画面凭据。

定义写要求内容 CAS，控制额外要求会话 CAS。连接结果未知时按原 mutation 账本和 requestStatus 恢复，同一序号只能重发同一规范请求；不能用新序号盲重试可能已经执行的写入。分页续页也要固定同一完整文档和内容版本。

自动化安全边界仍为默认关闭、本机同用户命名管道、启动时明确读写根、应用线程串行，不包含远程服务、任意脚本执行或新增权限。升级动画能力不自动授权改变这些边界。

接口详情见[API/MCP说明](native-animation-api-mcp.md)及[R4合同](native-animation-r4-contract.md)。

## 7 文件兼容性与当前预算

格式4必需顶层 `animation`，静态场景也写默认设置和空轨集合。加载先验证临时完整场景、资源和动画，成功后才替换当前文档；损坏或超预算输入不能部分发布。普通保存采用 `QSaveFile` 原子替换，旧稿升级另存不覆盖原件。

| 项目 | 当前约束 |
| --- | --- |
| 帧率 | 整数1–120，默认24 |
| 关键帧域 | 整数1–100000；定位/取样允许此域内有限子帧 |
| 默认播放范围 | 1–250；改范围不删键，改帧率不移键 |
| 轨道与键 | 最多3000轨、单轨10000键、总100000键 |
| 完整数值 Pose | 最多10000节点 |
| 定义候选预检 | 最多40000节点访问；先核节点数与去重时刻数 |
| 一次定义写批次 | 最多1024项；批内重复目标整批拒绝 |
| 一次 sample | 最多256个明确实体；内部仍求完整场景姿态 |
| API分页 | 默认256、最大2048；页尾 next 明确为 null |
| GUI键表 | 当前对象，每页256键 |
| 格式4文件与输出 | 源文件/实际 UTF-8 输出64MiB；JSON容器深度64 |
| 缩放 | 分量绝对值至少0.001；Linear相邻分量同号，Constant可跳号 |
| 连续 Euler | 分量绝对度数不超过 `3.6e9` |
| 实体 ID | 原生文件为 uint64 整数；API/MCP为无损十进制字符串，不经 JS Number |

动画、轨道、键字段严格检查，未知/重复/缺失字段和乱序键会拒绝。结构 JSON Schema 不能代替绑定、帧序、整数词法和实际 TRS 语义检查。

有限时刻候选预检不证明整个连续区间都合法；运行时仍必须求值检查。1024项批次限额也不意味着1024个不同时间在任意场景都能通过40000访问预算。旧静态 GUI 复制/删除不因接入动画而继承全部动画/API预算。

详见[格式4说明](native-animation-scene-format-v4.md)和[结构schema](scene-format-v4.schema.json)。

## 8 明确未实现的能力

| 未实现项 | 对进一步升级的影响 |
| --- | --- |
| 逐轴曲线、Bezier/Ease、切线、Graph Editor | 精细加速/减速、曲线修形和独立轴时间控制尚不可用 |
| 完整 Dope Sheet、批量多对象关键帧操作、自动插键 | 当前工作流仍以当前对象键表和显式录键为主 |
| 多 Action/Clip、NLA、混合与叠加 | 当前只有场景级轨道定义，不能当作已有片段系统扩展UI |
| 骨骼、蒙皮、IK、约束、驱动 | 角色动画需要新的数据、求值与编辑能力，不只是添加菜单 |
| 材质、Shader、灯光参数、可见性通道 | 目前变换动画不能驱动这些属性 |
| 顶点、形态键、拓扑动画 | 无变形动画真源及对应渲染/历史/保存闭环 |
| 物理动画 | 没有仿真、烘焙或确定性缓存接口 |
| 视频、PNG序列和外部动画导入 | 单帧capture与静态OBJ导出都不能替代动画交付管线 |

这些项目是首版明确排除的产品范围，不是宣称已经开发但尚未打开的功能。其优先级、实施顺序和验收预算需要另行确定。

## 9 已有验证与性能证据

### 历史第一版验收

| 验证范围 | 实际结果 | 解释边界 |
| --- | --- | --- |
| R5 Release默认完整Editor | 514例：512通过、2环境跳过；56201断言 | 不含隐藏性能用例；不是全环境全部通过 |
| R5 Debug受影响Editor | 32例、1472断言通过 | 不是最终Debug完整Editor全量 |
| R5 Release完整Asset | 57例：54通过、3符号链接权限跳过；1632断言 | junction补验不能替代symlink用例 |
| Debug动画文档 | 8例、589断言通过 | 含正式样例、精确值和旧稿保护 |
| MCP适配器 | 冻结源码49/49、定向11/11；typecheck/build通过 | 与真实SDK/GL证据分层，不把mock当成真图 |
| 最终Debug/Release主体和正式SDK/MCP | 写键、纯sample、分页、原子拒绝、13/25/37/49真实PNG、旧eval拒绝、原session CAS暂停、中文格式4重开通过 | 使用正式SDK/adapter，不等于Desktop本聊天直接热加载呈图验收 |

不同阶段和过滤组不可相加为一次全量成绩。详细日志入口、A01–A18需求和X01–X15反例矩阵见[第一版本机验收](native-animation-acceptance-20261007.md)。

### 固定本机性能

Release夹具包含102节点、100个有轨对象、300轨/9000键、48500可编辑源点，并含真实 Mirror→Subdivision一级和无轨子孙。24fps，1–697帧；5秒预热后30秒×3。

三轮最大 Core P95 为 `0.2371ms`，最大真实 swap P95 为 `17.1105ms`，均低于该夹具的5ms/41.7ms门槛。各轮完整24fps槽720/720，missed ratio为0，错误身份swap为0，成功 editable mesh GPU 上传增量为0。

Core计时包含定义校验和结果分配，但不含输入准备、GUI、几何和GPU；swap反馈不是显示器扫描时间。GPU计数只覆盖成功 editable mesh 上传，不代表全部资源上传。内存峰值包含Qt/GL、历史和探针，不能当作动画独占内存或无泄漏证明。

该成绩仅适用于本机固定夹具，不保证任意作品、最大预算场景、其他机器或30分钟耐久。历史失败采样仍保留，不能用最终通过覆盖失败依据。完整方法及每轮数据见[性能报告](native-animation-performance-20261007.md)。

## 10 最新取消变换修复与本地差异

用户复现的 P2 是：带特定合法旋转的对象 → 复制 → G → Esc → Ctrl+Z。取消时再次归一化四元数会产生微小分量变化，导致复制历史的精确来源检查失败；Debug可能断言中止，Release可能历史索引回退但副本仍存在。

当前本地已改为精确恢复开始时的原始变换快照，不再经归一化路径。实体存在/合法性守卫和原刷新通知保留，正常预览、确认和精确历史检查不放宽。

回归扩大覆盖真实 Ctrl+D → G/R/S 数字预览 → Esc → Ctrl+Z/Ctrl+Y，检查精确 TRS、实际删除副本，以及Redo同 ID恢复。修复前Release为1例81断言6失败；修复后定向Debug/Release各1例111断言通过，最终受影响组合各39例1003断言通过，两配置主体独立构建成功。

这次没有重跑修复后的完整Editor或全仓测试，不能将第9节历史完整成绩写成此次修复的全量成绩。只读独立审查未发现需调整的问题。

修复涉及 `src/editor/SceneViewModel.cpp`、`tests/ObjectTransformSessionTests.cpp`、`docs/v2-contracts.md` 和追加日志，现与本总结一并纳入 Git 提交范围；推送结果以实际 Git 记录为准。原始证据位于 `E:/CodexTemp/mini3d-cancel-transform-20261008-4e88`，最终组合日志为 `Test-Debug-mini3d_editor_tests-214327415.log` 与 `Test-Release-mini3d_editor_tests-214441379.log`。

升级必须保留“取消精确恢复快照”的合同。等价旋转矩阵、近似四元数或历史索引回退，都不能替代对象真实恢复/删除的断言。

## 11 已知验收缺口与诊断限制

- Editor仍缺已有映射网络盘和FAT/exFAT环境验证；Asset的3项符号链接场景因当前账户权限跳过。
- 真实中文IME、原生系统缩放、mixed-DPI跨屏、新机器、跨用户ACL、30分钟耐久，以及Desktop当前聊天直接MCP呈图未验。
- 原间歇SIGSEGV未保留直接原AV栈，诊断重跑未复现，根因不能确证。已独立复现并修复的生命周期/文档发布问题，不能反推为该崩溃唯一根因。
- 正式许可、签名、安装包和发布验收未完成。多代理技术审计不是外部人类专家认证。

第一版各阶段的专项和独立审查记录已经关闭，含具体反例、修补和运行证据。指定模型属于请求配置，工具未提供最终provider生效核验回执；不能用代理角色名证明实际模型已切换。

这些限制不等于首版不能使用，但会影响正式发布承诺和升级后的回归基线。后续测试应继承其“未验/未确证”状态，不能默认为通过。

## 12 升级前需要确定的产品问题

以下问题用于下一轮需求和调研，不是已批准的开发清单或排期。

| 待决定的问题 | 为什么影响开发方案 | 当前可以复用的基础 |
| --- | --- | --- |
| 优先提高对象动画效率，还是进入角色动画 | 前者主要扩展轨道与编辑工作流；后者还需骨骼、蒙皮和约束求值体系 | 稳定ID、时间、完整Pose、历史、文件和API边界 |
| 是否需要逐轴曲线与Bezier/Ease | 会影响XYZ整组轨道、插值存储、求值、GUI和旧文件迁移 | 现有严格候选验证及确定性取样 |
| 动画交付形式是实时预览、序列图还是视频 | 决定是否需要离线帧任务、编码、输出根、取消与恢复语义 | 指定帧真实capture及身份核验 |
| 是否新增非TRS属性通道 | 相机/灯光/材质/可见性具有不同值域与消费者，不能照搬XYZ键 | 正式内容与预览分离、原子编辑 |
| 是否需要多个Clip、混合和循环设置持久化 | 当前只有一份场景级动画；loop仍是会话状态 | 场景格式版本与旧稿保护 |
| 自动化需哪些创作动作 | 当前已有显式写键，但没有GUI草稿控制工具、曲线/骨骼/导出任务API | 14方法、双CAS、账本恢复、分页和权限 |
| 升级性能目标针对什么场景 | 当前102节点短测不是最大预算或长期运行证明 | 固定夹具、计时分层、真实swap与上传计数 |
| 升级验收是否包含发布和环境矩阵 | 发布与新功能验收不能混成一次“完成”结论 | 当前测试分类及未验环境清单 |

调研方案应明确现有数据是否保留、需要新增哪种格式版本、旧文件如何迁移，以及GUI/API/MCP怎样保持同一业务行为。不能仅提出新界面而忽略保存、Undo/Redo、失败原子性、截图身份和预算。

## 13 升级必须保留的合同

1. 基础场景、正式动画和临时姿态分离；播放、seek及sample不写真源。
2. double连续角原值、稳定实体ID和精确快照保持；不在保存或取消时隐式归一化/折叠。
3. 完整候选先准备、最终守卫后提交，版本推进后才发布一致姿态和通知；失败不部分安装。
4. 建模和动画共用唯一历史，精确no_change保留redo/保存点；复制和恢复使用固定身份。
5. 显示、拾取、设备和截图共用完整姿态；缺节点或身份过期不回退旧帧/基础画面。
6. 内容CAS、会话CAS、mutation账本和分页版本边界保留；未知写结果不能盲重放。
7. 文件旧稿保护、格式预算和原子加载保存保留；Schema不替代语义校验。
8. 默认关闭、本机同用户、显式文件根、应用线程串行及主体/测试分离不变；扩大边界需另行明确授权。

## 14 技术依据与后续阅读入口

以下路径相对仓库根目录，便于本地阅读或在GitHub定位。

| 区域 | 核心入口 | 后续调研重点 |
| --- | --- | --- |
| 动画定义与取样 | [Animation.h](../src/core/Animation.h)、[Animation.cpp](../src/core/Animation.cpp) | 值域、连续Euler、插值、预算及精确相等 |
| 完整数值姿态 | [EvaluatedPose.h](../src/core/EvaluatedPose.h)、[EvaluatedPose.cpp](../src/core/EvaluatedPose.cpp) | 父先子后、实际float结果、inverse/normal/rigid |
| 场景与持久化 | [Scene.cpp](../src/core/Scene.cpp)、[SceneSerializer.cpp](../src/core/SceneSerializer.cpp)、[SceneDocument.cpp](../src/assets/SceneDocument.cpp) | 稳定ID、子树、格式4、临时加载和旧稿保护 |
| 编辑器会话与事务 | [SceneAnimationSession.cpp](../src/editor/SceneAnimationSession.cpp)、[SceneAnimationEditing.cpp](../src/editor/SceneAnimationEditing.cpp)、[SceneAnimationApi.cpp](../src/editor/SceneAnimationApi.cpp) | 草稿、时钟、准备/提交/回放、准入 |
| 历史集成 | [AnimationReplay.h](../src/editor/AnimationReplay.h)、[SubtreeCommand.cpp](../src/editor/SubtreeCommand.cpp)、[SceneViewModel.cpp](../src/editor/SceneViewModel.cpp) | Undo/Redo、固定快照、当前帧回放及P2取消恢复 |
| GUI与草稿手势 | [AnimationTimeline.cpp](../src/editor/AnimationTimeline.cpp)、[AnimationDraftGesture.cpp](../src/editor/operations/AnimationDraftGesture.cpp) | 当前键表、XYZ mask、字段设置与数值增量 |
| 统一显示 | [InstalledPose.h](../src/renderer_gl/InstalledPose.h)、[InstalledPose.cpp](../src/renderer_gl/InstalledPose.cpp) | 不可变几何、设备方向、身份和缓存复用 |
| 自动化与真实观察 | [AnimationApiService.cpp](../src/editor/api/AnimationApiService.cpp)、[ObservationService.cpp](../src/editor/observation/ObservationService.cpp) | typed事务、双CAS、停钟诊断及真实新paint |
| 协议与MCP | [methods.json](../api/schema/methods.json)、[动画schema](../api/schema/native-animation.schema.json)、[schema.ts](../mcp/src/schema.ts)、[tools.ts](../mcp/src/tools.ts) | 单点预算、0.2成套、输入/结果合同及image身份 |
| Core与文件测试 | [AnimationTests.cpp](../tests/AnimationTests.cpp)、[SceneAnimationFormatTests.cpp](../tests/SceneAnimationFormatTests.cpp)、[SceneAnimationDocumentTests.cpp](../tests/SceneAnimationDocumentTests.cpp) | 数值反例、严格格式和精确往返 |
| 会话与历史测试 | [AnimationSessionTests.cpp](../tests/AnimationSessionTests.cpp)、[AnimationPreparedReplayTests.cpp](../tests/AnimationPreparedReplayTests.cpp)、[SceneAnimationHistoryTests.cpp](../tests/SceneAnimationHistoryTests.cpp)、[ObjectTransformSessionTests.cpp](../tests/ObjectTransformSessionTests.cpp) | 失败回放、no_change、取消及对象实际恢复 |
| UI/API/观察与性能测试 | [AnimationUiTests.cpp](../tests/AnimationUiTests.cpp)、[AnimationApiTests.cpp](../tests/AnimationApiTests.cpp)、[AnimationObservationTests.cpp](../tests/AnimationObservationTests.cpp)、[AnimationPerformanceTests.cpp](../tests/AnimationPerformanceTests.cpp) | 真实准入、设备、图像身份及限定性能口径 |

建议后续AI先读本总结和[中文教程](native-animation-user-guide.md)，再按研究问题读取[当前验收](native-animation-acceptance-20261007.md)、[性能报告](native-animation-performance-20261007.md)、[API说明](native-animation-api-mcp.md)、[格式4说明](native-animation-scene-format-v4.md)及[R3](native-animation-r3-contract.md)/[R4合同](native-animation-r4-contract.md)。原始需求、审计经过分别在[需求文档](native-animation-requirements-20261007.md)、[开发方案](native-animation-development-plan-20261007.md)和[审计记录](native-animation-audit-20261007.md)。

下一轮方案的完成标准应是明确升级目标、真实改动边界、兼容迁移和可执行验收，而不是再次证明首版“有没有动画”。未获实施任务前，不自动改代码、协议、用户作品或MCP配置。
