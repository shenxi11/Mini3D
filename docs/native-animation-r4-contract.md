# 原生动画 R4 接口与可信观察合同

版本：0.1，2026-10-07。状态：施工合同已冻结，正在实施；不是 R4 验收通过记录。

依据：[开发计划](native-animation-development-plan-20261007.md)第3.4、6节、原专家审计报告第10章，以及本机R4专项设计预审。R0–R3维持已关闭；本阶段须完成实现、实际运行、专项专家和独立审查后才可关闭。

## 1. 成套版本与权限

应用API、描述文件、hello、结果合同、MCP适配器成套升级为0.2.0；private wireVersion仍为1。旧0.1 hello明确返回VERSION_MISMATCH，不创建会话；旧适配器拒绝新描述文件。原47方法保留业务语义，配套新客户端总计61方法。API核心未装配观察服务时不报告4个旧viewport方法，仍报告14动画方法，共57项。

保持本机同用户命名管道、默认关闭、既有六种权限和文件根、应用线程串行。新增动画不改变授权、线程或传输边界。不把npm、动画验收或测试挂到普通主体构建，不修改用户MCP配置或作品。

## 2. 共用字段与14方法

Q：document，可选expectedDocumentRevision。W：document、必填expectedDocumentRevision、既有mutation信封（clientSessionId、mutationSequence、timeoutMs）。C：W加必填expectedSessionRevision。MCP只移除适配器拥有的会话/序号，不移除内容或会话CAS。

ID、修订、evaluationId为规范uint64十进制字符串；关键帧整数1–100000；sample/setFrame为有限double子帧1–100000。channel为position/rotationEulerXYZDegrees/scale；interpolation为constant/linear；mode为base/preview_paused/playing/pose_draft。所有XYZ值是完整raw double组，不折叠连续Euler。

| 方法 | 类别/权限 | 输入（加共用字段） | 结果（加完整文档/内容/历史版本） |
| --- | --- | --- | --- |
| animation.getState | query / scene.read | Q | settings、mode/frame/loop、sessionRevision/evaluationId |
| animation.listTracks | query / scene.read | Q；可选entityId、afterTrack、limit | tracks(entityId/channel/keyframeCount)、nextAfterTrack |
| animation.readKeyframes | query / scene.read | Q；entityId/channel；可选afterFrame/limit | entityId/channel、完整raw键、nextAfterFrame |
| animation.sample | query / scene.read | Q；frame、entityIds | frame、实体localTransform/worldMatrix、rotationSource、可选raw Euler |
| animation.setSettings | mutation / scene.write | W；完整settings | 既有command结果 |
| animation.upsertKeyframes | mutation / scene.write | W；完整items；必填onConflict=reject/replace | 既有command结果 |
| animation.deleteKeyframes | mutation / scene.write | W；明确items(entityId/channel/frame) | 既有command结果 |
| animation.removeTrack | mutation / scene.write | W；entityId/channel | 既有command结果 |
| animation.moveKeyframe | mutation / scene.write | W；entityId/channel/fromFrame/toFrame；必填onConflict | 既有command结果 |
| animation.setPreview | mutation / viewport.control | C；enabled | controlResult |
| animation.setFrame | mutation / viewport.control | C；frame | controlResult |
| animation.play | mutation / viewport.control | C | controlResult |
| animation.pause | mutation / viewport.control | C | controlResult |
| animation.setLoop | mutation / viewport.control | C；enabled | controlResult |

分页默认256、最大2048，轨道按数值EntityId和固定channel顺序，键按整数帧顺序。续页必须带expectedDocumentRevision并复核完整document。过滤entityId与afterTrack的实体不一致时拒绝。页尾next字段明确为null。存在的实体没有轨道时readKeyframes返回空页；不存在实体仍为NOT_FOUND。

五定义写通过ViewModel共用typed事务，不在Service复制数学、准备或历史业务。批内重复(entityId,channel,frame)整批拒绝；upsert/move必须显式选择冲突策略；delete/move以最终候选验证新邻接；不存在轨道removeTrack为no_change。精确no_change保留redo/clean点，但必须先通过CAS、deadline和最终guard。

sample只对基础数据与正式定义求完整Core数值Pose，再返回明确实体；不借用草稿或GUI安装姿态，不改变时钟、历史、内容/会话版本或evaluationId。允许Playing/PoseDraft中的正式查询；真实基础对象/组件临时交互和物理Busy不能被查询绕过。没有Rotation轨时raw Euler缺省而不是零向量；rotationSource为baseQuaternion/animationTrack。localTransform沿既有local TRS字段，worldMatrix为列主序16数。

## 3. 控制准入与typed诊断

controlResult包含status=committed/no_change、完整文档版本、mode/frame/loop/sessionRevision/evaluationId，可选结构化diagnostic。GUI五个bool动作是同一typed业务的薄包装，不反解析中文operationFailed。

控制沿现有mutation序号、账本、幂等恢复和before-commit guard；内容CAS和会话CAS均保留。物理Busy用busyReasons(false)，再由ViewModel统一核动作/模式许可，不用普通内容写Busy锁死Playing的pause或实体相机只读预览的play/pause。navigation、modal、未决写槽、commit等真实阻塞不得过滤。最终guard重新检查CAS、deadline和动态Busy，no_change也不能跳过；tick不推进sessionRevision，evaluationId不作为pause CAS。

Base的seek/play为PREVIEW_DISABLED，Playing/Draft中的非法动作仍BUSY。会话CAS冲突沿REVISION_CONFLICT，fieldPath为expectedSessionRevision。guard失败原样返回；获准pause后确实停钟，即使后续包装/求值失败退出Base，也返回成功伴diagnostic，不能把已执行停止报告成完全未执行。不得为同一动作重复运行外部guard。

Object准入不放到所有控制的共享守卫：Base且无真实手势时，Edit模式下的pause/setPreview(false)合法no_change，setLoop按Base/Paused模式合同也允许；这些动作不修改基础网格。setPreview(true)仍明确要求Object模式。四查询在Playing/Draft可读正式定义，但不能绕过真实基础交互和物理Busy。

失败与当前不可用动作通过typed错误报告；本阶段不引入无法证明实时许可的canX字段，也不把中文历史提示当作结构化错误。

## 4. 一次冻结的显示身份

Viewport新增FrameDisplayState：完整FrameDocumentStamp、真实PoseIdentity、sessionRevision、可选InstalledPose，由单个provider一次返回。装配层从真实ViewModel控制器构造：Base也有真实evaluationId/frame/mode；初始evaluationId=0合法，禁止从optional缺省制造0。Base的frame只是游标，不声称按该帧求值。

非Base逐字段核对PoseIdentity等于pose.identity，并要求完整numerics/geometry；sourceRevision等于当前documentRevision。没有装配provider的独立Renderer仍可静态绘制，但受控observationState/capture必须拒绝，不能给画面贴虚假的应用身份。

m3.viewState平铺新增必填evaluationId、frame、mode、sessionRevision；沿现有document/documentRevision携带完整来源。静态变化靠documentRevision区分，不要求每次paint增evaluationId。

## 5. 指定帧捕获与MCP呈图

viewport.capture仍为viewport.observe，新增必填expectedEvaluationId（含真实0）。请求无额外会话CAS，但admission内部冻结完整identity、sessionRevision、contextGeneration和afterFrameId。Playing/PoseDraft明确拒绝。

成功顺序：暂停/开启预览/seek → 状态核对 → capture准入 → 新真实paint → grab后的实际frame stamp → 检查完整document、内容/视图/evaluation/frame/mode/session/context及frameId大于准入 → 资源就绪/像素尺寸 → PNG/hash/字节限制 → MCP比较请求与返回的document、内容/视图/evaluation身份 → 呈图。

通过MainWindow将内容、会话和Pose变化转发到Viewport显示状态信号，Observation直接校验pending并主动终结。Base setLoop只改变session也必须使pending失效。grabFramebuffer可同步再绘制；晚回调、被取消请求和在完成回调里新建请求不能串到替代pending。每请求恰好完成一次，没有旧帧回退。

MCP不能仅检验PNG/hash：合法PNG携带错误身份也必须在生成image content前拒绝。适配器不把账本重放的旧evaluationId当作当前捕获凭据。

## 6. 预算与独占实施

methods.json为API预算源，CMake生成ApiLimits。动画新增：3000轨、100000总键、10000单轨键、1024批次目标、256sample实体、10000完整Pose节点、40000预检节点访问。列表复用256/2048，候选复用64MiB。Core无API依赖，通过编译断言/合同测试检查固定位预算与生成值一致。

Sol实施A独占ViewModel/Session/Editing、ApiTypes/Codec/Service、native-animation schema及动画API/会话测试。Sol实施B独占Observation、Viewport、MainWindow身份装配、m3 schema、观察测试及UI两处provider迁移。主代理独占公共目录/limits、bridge版本、CMake、MCP、文档/日志和串行构建。共享接口变更必须先协调；同一文件不得同时改。

请求配置为Sol/max；R4专项预审为Astra/high。工具没有provider生效回执，实际模型路由未独立验证，不能用角色名声称已核验。

## 7. 必须取得的验收证据

- 14方法runtime/schema/MCP一一对应；0.2成套、旧0.1拒绝，原47语义回归。
- 整批原子、重复与最终邻接、raw double/Euler、续页旧文档/旧版本拒绝、sample零副作用。
- tick后原session CAS仍可pause；相机预览play/pause；no_change过期CAS/deadline失败；停钟后故障返回诊断。
- Base同revision换文档拒绝；等待中setLoop也立即失效；二次paint实际stamp；取消/晚回调/新pending不串单。
- 实际SDK/MCP指定帧PNG，以及正确PNG错误身份的拒呈图负例。
- Debug/Release受影响测试、两配置主体单独构建；专项Astra和未参与实现的Sol复核。

当前只完成合同及部分版本/预算接线和静态语法检查，未宣称上述运行门禁通过。证据落点为E盘任务目录的r4-implementation；R5性能、中立夹具和交叉验收不由本合同代替。
