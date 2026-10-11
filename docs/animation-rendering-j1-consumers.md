# J1 跨域消费者接线补充

2026-10-11 · EXEC-011 · 设计审查后施工准入；实现与运行结果另在执行台账追加。

本文件解释冻结 J1-CONTRACT-2 的实际消费策略，不改其字节、数值门、作者字段或比较函数。格式仍为4，API/MCP仍为0.2.0；持久化与外部接口留EXEC-012。独立专家按用户同意的Sol6.1/max普通模式备用复核，provider最终路由未独立核验，不称fast。

## 1 消费路径

完整性和消费域分开。纯旧场景即使拥有CompleteSceneJ1状态，仍使用原LegacyTrsV1 float数值、材质和显示模式；有实际typed动作/绑定、PBR作者或Sun的新场景使用JointJ1。仅有rationalFps不触发新画面。分类先于Base动画输入或几何准备，不能给纯旧Base强加新动画规模/资源门。

InstalledPose保留旧numerics，并持有准备时已checked的sceneNumerics。Joint显示、拾取、bounds、设备和手柄通过同一ScenePoseView门消费新数值；缺状态、节点、几何stage或引用整体拒绝，不回退基础Scene、旧数值或白材质。paint和pick不重新求值或适配。

## 2 Base 与动画来源

旧installedAnimationPose()在Base仍为空，FrameDisplayState.pose也仍为空。新增独立baseDisplay字段，复用同一InstalledPose类型，不能创造第二套IR或身份。

JointBase只消费正式基础作者，不取样键：共用生产入口用空动画保留fps和循环语义，并在algorithmBuild中明确base-only求值标签。否则同文档/修订/时间的Base与动画会因不同有效结果被错误判同源。保持现有SourceIdentity比较函数；独立输出使用与实际输入一致的算法语义。

缓存按真实文档域、正式revision、作者拥有者、时间/fps、视图和完整visibility失效；稳定读取共享同一pointer/evaluationId。最终来源守卫通过后才发布并推进现有求值计数，缓存凭据同步到发布后状态。不每paint分配UUID/稳定ID，失败不耗计数、不写作者/历史。

活动对象或组件手势不能准备正式JointBase：当前临时变换可能直接改变Scene但不推进revision。旧缓存不能冒充当前正式结果。pure legacy手势继续原行为。保持旧帧时必须保留旧来源；拒绝不能作为新的成功捕获。

## 3 画面与失败

Joint整体调用FrozenStateRenderer的真实render/present，由唯一固定OCIO2.5 Standard processor显示一次。实际观察矩阵来自同次受控视图，尺寸来自实际viewport。内部帧证据记录真实source、geometry stages、camera、backend及color身份，不把原始模块读回称为ImageFrame或正式文件输出。

present前清目标depth/stencil；Joint覆盖层独立于母版且always-visible，不宣称具有母版遮挡深度。非Material、临时编辑网格预览、未支持的元素掩码等明确拒绝，不能使用旧数据画成成功帧。纯旧显示模式保持。

frameId继续表示真实paint尝试，evaluationId表示求值发布；二者不混用。render、present及覆盖层失败写入resources.ready=false，现有观察捕获守卫拒绝。Joint播放遇到渲染故障由装配层停止并报告，不把旧图重贴新来源。

## 4 本地依赖与施工范围

固定本机SDK的OpenColorIO2.5.0和其Imath3.2.2运行库按真实Debug/Release imported target部署到主体及独立GPU/editor测试目标旁。保留用户显式包路径；机器专属默认值只放Git忽略的CMakeLocalConfig.cmake。自有色彩配置继续从Qt资源读取。此接入不是跨机器发行、产品分发许可或系统安装；不得清理仍被本机构建引用的隔离SDK。

产品唯一owner租约：InstalledPose.h/.cpp、SceneAnimationSession.cpp、SceneViewModel.h、SceneViewModel.cpp仅selectRay、MainWindow.cpp仅显示装配、Renderer.h/.cpp、RayCaster.cpp、ViewportWidget.h/.cpp；测试仅InstalledPoseTests、AnimationSessionTests、AnimationPoseGpuTests和AnimationObservationTests。root持构建定义、依赖默认值、日志及动态验证。Core、serializer、API/schema/MCP和010两文件不在本票修改范围；跨范围先重新登记。

## 5 退出门

- 纯legacy完整state仍消费原float；旧Base零计数、手势及大场景保持。
- typed位置与旧numerics不同及父shear/负scale条件下，画面、pick、bounds、camera绑定同一checked stage。
- JointBase合法PBR/Sun实际绘制、不采动画键；重复查询复用，失效后单次发布，重入/手势/失败不发布。
- 产品Renderer/MainWindow三个时间点发生预定真实像素变化，内部帧身份与消费内容一致。
- 缺绑定/checked状态及不支持模式/渲染故障不能成功捕获。

GUI空白创作、格式5/API0.3、正式PNG/EXR及完整J1作品仍分别由012至015验收，本票不得提前签收。

## 6 实际调用点的精确租约补充

施工只读定位及root复核确认：SceneViewModel.cpp的cursorSelectionCenter还供transformPivotPosition使用，是实际手柄枢轴消费者；setPreviewCamera仍直接验证旧numerics。因此同一owner额外获得cursorSelectionCenter与setPreviewCamera中仅数值消费切共同门的窄租约，不改原事务、guard或历史，不扩大其他功能。

const游标中心查询不隐式发布求值、不使用const_cast。非Base从同一InstalledPose/ScenePoseView读取；JointBase只读经过当前来源核对的已有显示缓存，缺失或过期明确不可用，不回退live Scene，正常显示生产者随后准备。purelegacyBase保持原路径。transformPivotPosition及其他公共接口的const约定不在本次扩租约修改范围。

## 7 观察验收环境

独立editor测试通过PRIVATE编译定义读取与GPU测试相同的冻结manifest，不依赖工作目录，也不复制第二份相机或质量门。仅测试子进程固定HighDpiScaling=0、ScreenScaleFactors=1、ScaleFactor=1；实际viewport尺寸1280×720和DPR1仍须运行时断言，环境变量本身不是验收证据。测试窗口设置ShowWithoutActivating，不改系统缩放或用户窗口。

测试runner将manifest和全部011消费源纳入逐次运行前后指纹，发现来源漂移即不签收。构建、GPU与真实窗口检查串行，整包冻结前不配置或编译。三时点与独立参数变化检查仍绑定原ROI；当前没有正式PBR/Sun创作接口，测试首次分类前的隔离作者bootstrap必须明确标为消费端验收，不能代替013从空白GUI创作及015完整作品链。
