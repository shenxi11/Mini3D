# J1 同步图像输出施工补充

2026-10-11 · EXEC-014 设计与依赖探针 · 产品输出未实施

与 J1-CONTRACT-2、J1-PERSISTENCE-API-2 一起使用。本说明落实已有同应用线程固定尺寸 PNG/FLOAT32 EXR 授权，不改变原数值门，不授予后台线程、持久作业、安全放宽、系统安装、提交或发布权限。012 完成之前不启动014产品写入。

## 1 来源与像素

- 先冻结正式作者输入，再在独立求值会话生成 FrozenFormal 状态，之后渲染；不能给先前 live 像素改贴 frozen 来源。
- ImageFrame 移动拥有真实母版像素，以独立会话 UUID 和单调 frameId 标识，不复用耗尽计数；只读载荷与完整 source、时间、camera、settings、backend、color、geometryStage 清单一起持有。
- 固定1280×720、TopDown、Progressive、左上半开 display/data 窗口、pixelAspect 1:1。母版为 FLOAT32 scene-linear Linear Rec.709 associated Beauty RGBA 及独立 coverage；UI覆盖层不入母版。
- 视口 resize 不改变输出目标尺寸。失败、取消或来源冲突不发布旧图作为成功，不把进程内冻结称为持久作业 Ready。

## 2 PNG 唯一打包规则

Alpha>0 时在线性域去关联，再执行一次已冻结 OCIO processor；alpha 不执行色彩变换。Alpha==0 且 RGB 非零时拒绝不可保真 PNG，不使用 epsilon 清零或吞掉发光。母版与 EXR 不因此改变。

创建非预乘 QImage::Format_RGBA8888，显式设置空 QColorSpace，并检查 iccProfile 字节为空。QImageWriter 保持默认 gamma=0；编码器只量化、打包、写出处，不再转换颜色。不写 iCCP、sRGB、gAMA，不把自有 Standard 连续 moncurve 宣称为 canonical sRGB transfer，也不承诺普通查看器色管认证。

仅通过 QImage::setText 写一个 keyword：mini3d.ImageFrame。值是完整 compact UTF-8 JSON 的标准 Base64、无换行，ASCII 长度至少40；本机 Qt6.8.3 探针确认该路径输出单一 zTXt、compression method=0。禁止混用 writer 的 Description 路径或增加第二标签真源。

正式 JSON 必须包含 ImageFrame 的完整出处，并明确实际载荷 PNG/RGBA8/straight/Beauty、独立 coverage 未保存、RGB执行一次OCIO/alpha零次。uint64 为规范十进制字符串，binary64 元数据采用精确位编码；OCIO版本、配置SHA、processorCacheId、shaderSha及固定参数保持真实。探针的 probe:true 小标签不能冒充正式元数据。

实际暂存文件关闭后，独立检查 IHDR、唯一 zTXt/keyword/压缩方式、原始压缩文本解压、Base64/JSON及完整出处；用 Qt 解码核标签和全部RGBA字节。冲突 ICC/gamma/sRGB 或矛盾 alpha/来源元数据必须拒绝。合法PNG字节只证明格式有效，不证明正确作品或帧。

## 3 FLOAT32 EXR

R/G/B/A/coverage 全部 FLOAT、1×1 sampling；不走 Rgba/half API，不执行OCIO或预合背景。合法 HDR>1、负RGB、零alpha非零RGB与独立coverage保留。使用无损 ZIP 或无压缩，文件构造显式 numThreads=0，不修改全局线程设置。

本机SDK含 OpenEXR 及独立 Debug/Release import，依赖 Imath、openjph。固定版本、CMake配置、真实DLL及许可证据须在014本地接入时核验；目前仅静态读取，尚未配置或运行产品EXR，不能计通过。解码必须核通道名/类型、sampling、窗口/尺寸、FLOAT32原值及完整出处。

## 4 文件发布和租约

只写已明确的验收根。关闭并验完本次私有暂存后，重查当前取消、CAS和写权限；紧接 NewOnly 最终发布，不在两者之间处理事件或执行任意回调。只清理本次拥有的暂存，不删除已有成品，不覆盖竞争目标。

候选独占文件是 ImageFrame、ImageFrameOutput、ImageDocument 各自 .h/.cpp 及对应两测试；正式授权前仍无产品租约。实际冻结来源、GL context和文件守卫消费端须另登记精确路径，不能从目录组推定权限。root独占CMake/依赖/正式日志及动态验收。

## 5 本机 PNG 探针证据与限制

独占目录：E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/product-slices-20261011-3f1dbe27/png-label-probe-8c64e639。

此前两次 Run 均以0xC0000135退出，未启动 main；PATH斜线纠正没有解决。保留全部失败，改为核SHA后的探针应用本地 Qt6Gui/Core 新文件部署。新 Build 和 Run 均实际exit0，Run回执是 Run-bde8ac9c36fa4f24b3d59bcf22c1e3df/result.json，sourceDrift=[]。本地部署消除了启动失败，但没有把“某个具体缺失DLL”作为已证明根因。

- Qt6.8.3，2×2自建CPU样本，无窗口/GPU/OCIO；唯一zTXt、正确keyword/method0、独立解压/Base64/compact JSON、无iCCP/sRGB/gAMA和全部16个RGBA字节往返通过。
- sRGB ICC负控制及 alpha=premultiplied矛盾标签负控制被同一验收门拒绝。
- main SHA1512A48D1782A71D0732A8ED91F31701372712E0352A3075AD41AB2CB5C10AFF，二进制SHAC1A543891CF9C121D1E537387F27B7B9D307B03958787AE34C6ABD1681733042。
- QtGui SHA CA9F20D9B59D5BC321A87005B926B853EBCD6A2394823E14D3C8B5FC5702FA8C；QtCore SHA 0CC62E841959FB8F6D366EA116C6F6BBC273E30F0EF90D8AA2E4ADEB81FD63F8。

root亲读真实回执、输出与探针源码；该证据只收口PNG包装不确定项，不签014、正式文件发布、OCIO渲染或J1作品通过。Sol/max请求沿约定，provider未独核，不声称fast。撤回此设计说明须先核后续引用并移存唯一withdrawn名称；正式日志只追加撤回，探针证据保留，不还原系统配置或用户文件。

## 6 本机 EXR 独立依赖探针证据

2026-10-11追加，不改前述产品输出合同。此前第3节“尚未配置或运行产品EXR”仍成立；本节只补独立DependencyProbe，不是014产品验收。

- 固定OpenEXR3.4.10、Imath3.2.2、openjph0.25.2的真实Debug/Release import分别配置、构建、app-local部署和运行；不映射配置，不改SDK/系统/PATH，不下载。每次OutputFile/InputFile显式numThreads=0。
- 两个2×2自建CPU样本均验证R/G/B/A/coverage五FLOAT、1×1 sampling、零原点窗口、INCREASING_Y、ZIP，20个FLOAT32位值及完整StringAttribute字节精确往返；包含不能降为half仍精确通过的4.000123F/-0.250123F、HDR、负RGB、零alpha非零RGB以及coverage与A不同。已有目标NewOnly拒绝通过。
- Debug回执：E:/CodexTemp/20261011/mini3d-j1-exr-probe-b22f9cd4/receipt-Run-Debug-31c1915f6ee94d45be90fd0d2fd2b82d/result.json，SHA39598D01747B81FDEEF7268B9F43DAB6CC020EC20E0EB361E170E3E85F50B503；Release回执：同根receipt-Run-Release-26044b36bccf46d3bce3003ab20f2736/result.json，SHA69AAFECC515C65F39B498E48185EE877CDC831C97B3FDCD3D1065E35BB32310B。每配置6个SDK DLL均核源/本地SHA并实际从目标旁加载。
- Audit交还回执：同根receipt-Audit-Release-8023c05327e6447a8062955bbba51355/handoff.json，SHAB6C5703BABE86F860406CCAB7BE137667E13AD78DFA70580708FEBCBAEBE6107；inventory.json在同一receipt目录，不在scratch根，SHA F457D5FCEBB466B57647A7A3436A39A1B2575F1EC0FCD9FF93B2B6E170CC4C51。19个直接原生进程均actual exit0/hasExited=true，sourceDrift/sdkConfigDrift/ownedRunning均为空，全部工作已退出并交还动态租约。产物约15MiB、可重建，保留原证据。
- root亲读两次result及Audit；本节不签正式ImageFrame/完整出处、OCIO、GPU渲染、source CAS、文件发布或014通过。许可证静态记录只读现有SDK，不等于产品分发许可。正式接入在012完成后另验。

## 7 正式调用者与同源派生的设计裁决

2026-10-11追加，独立Sol专家只读复核后由root接受。014仍等待012全部闭环；以下不是产品租约或运行通过，不修改既有SourceIdentity、isSameWorkSource或11方法API合同。

共同冻结入口放在SceneAnimationSession，复用现有准备来源、共同source builder和最后matches守卫。在复制正式作者/动画/原始pose输入及持有不可变几何资源的同一保护下，先生成请求精确frame的originSource：LiveFormal、完整动画算法标签、JointPreviewJ1、Preview。不得使用Base的/base-only-1，不把当前游标替代请求时间，不在渲染后从像素补造出处。

实际输出source从这个不可变origin受控派生为FrozenFormal、新FrozenHandle、OfflineJ1、Output、独立求值session及单调ID；作者UUID、revisionDomain、revision、资源、算法及精确时间保持。之后真实独立evaluateSceneState，不改贴旧内容标签。ImageFrame及PNG/EXR完整元数据同时保存origin和实际source，并声明派生规则。

保留既有严格同源比较：015先核isSameWorkSource(actualPreview.source, output.originSource)，再单独核唯一允许的origin→Frozen/Offline派生。不同revisionDomain、frame或Base算法标签的负控制必须拒绝，不能放松profile比较换取通过。

任意合法帧的相机消费本次完整有效state，不制造Frozen InstalledPose。InstalledPose只增加窄的只读state相机适配入口，复用deviceCameraView及checked pose/设备/几何验证；核相机存在、可见、实际可消费，固定1280×720、aspect16:9。旧Live/Joint安装守卫保持。实际设备矩阵仍为float计算，其提升到FrozenRenderCamera的double仅保留消费值，出处明确记录这一精度来源。

内部同步ImageOutputService编排冻结、GL消费、编码和提交，不新增render wire/catalog/MCP方法。ViewportWidget只在真实所属context/thread处理固定目标；Renderer在本次render/present成功后移动持有真实FrozenRenderPixels母版，让实际RenderedFrame携带只读载荷。不得先移走stage再留下不完整母版，覆盖层失败帧不计成功。

ImageDocument位于assets，只收编码bytes、关闭后可信验证及最终beforeCommit，不依赖renderer。顺序固定为同目录私有暂存→flush/关闭写句柄→验证实际文件并关闭读句柄→最终guard→MoveFileExW(...,0)。native path在guard前准备；guard成功后不处理事件、不调用任意callback。最终guard先运行可能重入的现有provider/guard，再核当前取消、deadline、Busy/草稿、CAS/冻结凭据、相机/视图/context及当前明确根授权。可用内部C++入口委托现有External新文件策略，不捕获旧权限副本、不用InternalTrusted绕过根。

候选新增ImageFrame、ImageFrameOutput、ImageDocument、editor/output/ImageOutputService各.h/.cpp及三测试；必要既有入口是SceneViewModel.h、SceneAnimationSession.cpp、EditorApiService.h/.cpp、InstalledPose.h/.cpp、ViewportWidget.h/.cpp和Renderer.h/.cpp。FilePathPolicy、ObjDocument、SceneIdentity不改。正式施工时逐路径另登记，root独占CMake、固定EXR依赖、说明与日志。013与这些共享文件交集先串行，不为双线并行放松单owner。

015必须比较两份实际母版、完整camera/settings/backend/color/stages，再独立解码文件核通道/尺寸/出处/原值；保留原RGB容差、alpha逐位门、PNG≤1/255及预冻ROI变化门。旧外部资源无完整身份、已声明纹理、不支持几何/相机/数值仍明确拒绝，暂存验证失败、最终取消/撤权/stale CAS及竞争目标均须无错误发布或他人成品删除。此次专家没有写产品或运行构建/GPU/文件动态；请求Sol6.1/max普通备用，provider未独核，不称fast。

## 8 本地依赖接入与当前施工状态

2026-10-11追加。012/P/A/B已验收，014进入Writing；前述设计与探针记录仍为历史证据，不改其当时状态。当前产品输出尚未构建或验收，不能称014/015完成。

产品本地构建新增固定OpenEXR3.4.10及openjph0.25.2，沿既有Imath3.2.2/OCIO2.5。默认包路径仅写本机被忽略的CMakeLocalConfig.cmake，保留命令行/Qt Creator显式选择；其他机器应提供这几个相同版本包的DIR，不能把本机E盘依赖当已分发安装。

PNG独立CRC/zTXt验证新增同一固定SDK所带zlib1.3.1的静态库，不是OpenEXR的新增动态运行库。ZLIB_INCLUDE_DIR、ZLIB_LIBRARY_RELEASE、ZLIB_LIBRARY_DEBUG默认对应SDK的include、libz_st.lib、libz_st_d.lib；EXACT版本及真实Debug/Release各自链接，不映射或下载依赖。具体ABI和正式编解码仍待本票产品构建/测试，现阶段只有头文件版本与库SHA静态核验。

ImageFrame/ImageFrameOutput登记在renderer_gl，编码器私有链接OpenEXR/ZLIB；ImageDocument登记在assets且不引入renderer；ImageOutputService登记在editor_ui。新增三测试分属GPU/Asset/Editor目标，正常主程序构建仍不顺带编译测试。

应用、GPU/Editor测试只把固定配置的OpenEXR、OpenEXRCore、Iex、IlmThread、openjph和既有OCIO/Imath DLL部署到目标旁。没有系统安装、用户级PATH改变、vcpkg清单升级或用户MCP配置变化。实际导入/加载/尺寸/原值/出处/发布与清理均待两个Writing包Frozen后串行验证，探针通过不能代签。

014五FLOAT验证测试直接私有链接同一固定OpenEXR包，以另开InputFile独立读取文件，不以产品validator作为唯一数值oracle。该测试依赖不进入纯CPU目标或改变正常主体与测试分离；目前仅登记与静态单行差异验证，真实解码仍待Frozen后的运行。

## 9 首版独立审查与定向修补

2026-10-11追加。014首版21路径已Frozen，独立Sol专家核对全部源码及完整交付身份后发现1项P1和3项P2；本票尚未构建或动态验收，不能计014/015通过。原首版、失败发现及交付清单保留，不覆盖为修后证据。

本次只在原21路径内修复：初始busyReasons及已有目标拒绝分支保留正在执行的当前callable；冻结producer在任何provider前设置作用域重入门，递归明确Busy，防止重复或回退独立求值ID；真实viewport测试首次show前禁止激活和接收窗口焦点。局部callable副本仅保护生命周期，不延续旧许可，最终提交仍核当前授权代际、取消、Busy及CAS。不改文件根、作者计数、共享协议、后台线程或作业，也不修改产品导航子控件。

补充回归要求为：回调替换自身之后捕获拥有者仍存活；已有成品拒绝且原字节不变；末次来源provider递归冻结得到Busy、之后ID严格增加且作者/游标/历史不变；测试窗口不调用activate或顶层raise。原编码/关闭后验证/最终NewOnly发布合同不变。两包修后停止写入并重新Frozen，才运行串行构建和真实测试；本节不是动态通过记录。

## 10 修补冻结及首批实际验证

2026-10-11追加。GUI四项原发现和输出四项原发现均已独立静态复核关闭。输出实施句柄消失后，root核对现有修补及全部21路径前像，仅4路径改动、17路径不变，接回租约并生成独立后继清单；没有重做代码或把未交接状态直接当已完成。

102路径集成冻结后，实际Configure、BuildGpu、BuildAsset均exit0；[image-output-codec]五例831断言通过，包含一次实际OCIO、PNG完整标签及字节、独立五FLOAT EXR原值和负控制；[image-document]三例68断言通过，覆盖句柄关闭、验证/取消清理和NewOnly竞争。它们不证明真实GUI作品、GL输出或015通过。

首个BuildEditor实际exit1，编辑器UI库已编译，测试中七处INFO三元表达式受Catch2流运算优先级影响。root仅加七组括号，严格字节比较证明所有断言、业务和其他源码不变，保存原失败与前像，生成新的21/102路径后继；重新构建及来源/控件/GPU/整窗验证仍在进行。既有两个纯模块测试不因测试日志括号改变而无收益重跑。

## 11 EXEC-014 产品同步输出验收闭环

2026-10-11追加。014已 Completed，仅指本票内部同步离屏服务；不新增 GUI 导出按钮或 wire/MCP 输出方法，不代签015、持久 RenderJob 或完整渲染方案。前文未验状态是原时点历史，不回写。

修后 BuildEditor 实际 exit0。`[image-output-source]` 4例255断言通过，覆盖完整三参数独立取样、两个自替换回调拥有者存活、权限代际和递归 Busy/单调 ID；`[image-output-service]` 5例967断言通过，使用真实显示的 Viewport/同GL context，验证1280×720/DPR1输出不随431×287视口resize变化、PNG/五FLOAT EXR实际文件、24→1→12.5→1同帧母版比较、Preparing重入/取消、最后撤权/CAS/Busy/View变化拒绝发布、旧成品字节保护及根外拒绝。

RGB采用事前 abs2e-5+rel2e-6、alpha及coverage逐位相等，PNG差≤1/255；未调整容差。初始四项审查发现及后续七个INFO括号均经独立Sol定向复核关闭；本轮真正收到所有退出且动态串行。驱动保留同步像素传输等性能警告，不视为性能/资源认证通过。

证据根为第10节所用 slices；回执分别为 `runs/20261011-201722279-TestEditor-a60124b9`、`runs/20261011-202353855-TestEditor-99783238`；最终主体 `runs/20261011-203023050-BuildApp-bd1a5340` exit0。源码/依赖漂移为空，测试二进制前后SHA一致。编码5例831断言和文件机制3例68断言仍沿第10节真实证据，无改动不重跑。

输出21路径后继 `output014-info-final-e0f84f27.json` SHA256 `9F429B1AA264827AA7B86514E3EFF7AE8ABFC86F69C4592472DC33C0E45A43B2`；最终整包102后继 `integrated-013014-visible-dock-d8fa7013.json` SHA256 `5F55C6B04414A8E710915AABEC19BE917892E3D8753FD08829307D95346BC21D`。旧GUI测试setup及可见性补丁只影响该测试文件，不改变输出产品或原编码证据。

撤回按原014完整before、四修补 `recovered-repair-4affc06b` 及 slices/before/ImageOutputServiceTests.before-info-e0f84f27.cpp，在核当前后继和依赖后定向逆补丁；新增文件移存独占withdrawn，不删除既有成品，日志只追加。本轮无系统安装、后台线程/持久作业、安全放宽、Git提交推送或发布。
