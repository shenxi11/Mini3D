# J0 后端隔离实验记录

2026-10-10 · EXEC-005/006进行中 · 不是最终选型或产品验收报告。

## 当前结论

原生Qt/OpenGL4.1和Blender静态CPU HDR基础子探针已通过；同IR基础材质对照的alpha完全一致，但RGB仍有差异。新API控制确认法线导入顺序影响实际法线；只修隔离Blender适配器后，RGB RMSE从0.02688降至0.00489，未判画质通过。六路节点组/真位移控制中两端真位移改变191个轮廓像素、bump不改轮廓。含剪切/负缩放的UV/Generated/Object六路着色跨端alpha完全一致、内部RGB最大差约3.10e-6，不是完整组二验收。helper-parent的原生CryptoAsset根名改变问题仍在。Filament修复后原半透明样例alpha恢复，但其DOF/noDOF全图仍相同；另建写深度的失焦控制后，同二进制配对出现693个alpha像素差异，HDR最大4、透明角0保持。仅证明该控制中的景深产生实际图差，不认证完整关联RGBA、TAA或景深质量。完整E-RT/E-OFF仍未执行完，EXEC-007不能进入Ready。

静态Pass/AOV/Crypto夹具两路33个FLOAT32通道及夹具身份读回通过，九个非Crypto通道和六张coverage图配对差异为0；这不是完整第四组或原生产品认证，也不解决helper-parent的CryptoAsset根名问题。

仅使用自建资产。所有下载、解压、构建和隐藏实验进程位于`E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba`；GPU和重构建串行。不安装到系统、不执行资产脚本、不改全局配置、不提交或推送。

## 固定来源

| 参考 | 固定身份与实测范围 | 未完成门 |
| --- | --- | --- |
| Blender | 5.2.2 LTS，实际build hash d13f752e3b9c；官方ZIP SHA256 `3849D17A682CBA006075AAA3F3597ECB5C9C30EC31035B2E092C53E40679B535` | 完整官方目录、全部对照实验及发行许可 |
| Cycles来源 | Blender commit `d13f752e3b9c4f8c261cda552b1021f8bcc0382c`；官方镜像archive SHA256 `9F66A3827DD25F7239D38D04AF183188C7ED3FE6C526698FC2E965B6B4CE3DF8`；CPU-only配置和构建退出0；另有明确记录的实验前置头及standalone一行初始化补丁 | 同IR RGB差异诊断、完整适配器及四组离线实验 |
| Windows库 | 固定父gitlink/本机浅层HEAD `60d6e96b917568278d400a4024c98da0fb777338`；原13目录354个LFS对象525517565字节；另补zstd一个LFS对象1534114字节，独立长度/hash核验 | 下载及基础运行不能代替完整后端验收 |
| Filament | v1.77.3；固定commit `d852e34cd5629a6f3851d613dcd17aa0ffd025a2`；官方包本地SHA256 `E8705D817B7A884FA2D185080CEB80DD42C4BA339930D49DEDC79B5761215285`；source archive SHA256 `1D0A64761F21F50CAADC8205A3B26C801EA112C9E80F2B4BEEA9CAB9B24540CC`；原版/HDR小fork失败证据保留，扩展景深修复及独立失焦控制已实际配对读回 | 本地hash不是发布方签名；原样例alpha恢复，独立控制有真实景深图差；完整关联RGBA/TAA/景深质量及组合未验 |
| OCIO配置 | 实际消费文件SHA256 `C39C242CCFBB6B667B804AFA281652727148874BCD7FFBC01C777341BC597487`；只在内存归一化CRLF后与源码Git blob `B30A227EE8662BFAD897E27FDB06649ACEDF4DFE`一致 | 原生一次OCIO组合及配置/LUT分发许可 |

便携配置68686字节含1550个CR；源码LF为67136字节。没有转码或修改原配置。配置头引用`ocio-license.txt`，固定源码树未取得该文件；不能把依赖清单中OCIO库许可证直接当作配置/LUT分发许可证。隔离实验不因此被视为发行授权。

只读许可调研核实：OpenColorIO库为BSD 3-Clause，Blender源码为GPL，Compact ICC Profiles的README声明CC0-1.0；这三项不能替代AgX/Filmic LUT权利声明。指定17份文本LUT未见独立license/copyright标记，官方AgX/Filmic树也未取得明确许可文件；结论是许可链未闭合，不是认定这些资源无许可。权威`ocio-license.txt`与两组LUT条款仍是发行前置。

## 原生Qt/OpenGL基础子探针

实际设备为NVIDIA GeForce RTX 3050 Laptop GPU、NVIDIA 610.47；实际上下文OpenGL4.1 Core、GLSL4.10、Qt6.8.3。颜色附件RGBA16F、数据附件RGBA32F；保存的读回数组为FLOAT32，并不改变颜色附件本身的半精度限制。

两轮各14项检查通过，2次上下文销毁信号通过。L1独立读取JSON和raw重新核对数值、格式、行序及hash，不追加GPU运行：

- 实际HDR/负值/alpha像素为`[4,2,-0.5,0.25]`；独立数据像素为`[42,-3,0.75,0]`。
- 最小GGX平面中roughness从0.2改0.8引起像素变化；irradiance从1改2时RGB在规定误差内倍增。这是RGB参考模型的基础检查，不认证完整物理光照模型。
- shader修改与重建保留参数；Qt覆盖层前后HDR hash一致，诊断PNG中的紫色覆盖像素存在。
- Widget从320×240变500×300，输出仍128×96；四份raw各196608字节。两个颜色raw相同，反转保存行序后与原读回hash一致。
- 未见本次GL错误；运行退出0、约1527.7 ms、采样进程峰值136482816字节。该峰值不是GPU显存峰值；最小绘制/读回时间不能外推完整场景性能。

首次启动`0xC0000135`发生于GL代码执行前；依赖诊断后仅在隔离执行目录部署五个核验过的Qt发布版DLL，一次针对性重试通过。失败JSON和日志保留；未安装系统运行库或改变用户Qt配置。

证据：隔离根`rt/native-gl/output/experiment.json`、`output/run-evidence.json`、`log/native-artifact-validation.json`、根`native-evidence-audit.json`。PNG是未经OCIO的诊断图，不是正式色彩输出。

完整E-RT尚缺同一球阵、含normal的glTF、透明栅栏、多灯阴影、HDRI、TAA+DOF+HDR/alpha/data+一次OCIO组合、真实桌面present、GPU峰值、预算冻结及Filament对照。零alpha发光在完整后处理组合中的保留也未认证。不能以基础28项检查关闭父票。

## Filament准备与已观察失败

安全解包完成；自建材质matc编译退出0，原型CMake配置退出0。使用本机唯一MSVC14.40.33807/编译器19.40链接官方预编译包时，出现4个缺失的STL运行库符号：`_Cnd_timedwait_for_unchecked`、`__std_search_1`、`__std_find_end_1`、`__std_remove_8`。构建已退出，无GPU运行或像素结果；这不能认定为Filament着色失败。

不添加CRT兼容桩、不重复同一链接、不升级系统工具链；改为固定commit的源构建。默认LinearToneMapper和grading路径的值域截断已由源码确认；即使后续基础原型成功，默认输出也不能被当作无损HDR出口。小fork或自管后处理必须保留TAA/DOF并验证外部一次OCIO，不关闭关键效果换取通过。

固定源码配置退出0；首次构建退出1、266463ms、stderr为空，stdout末尾落在matp编译且没有明确error/fatal/MSB诊断。已观察到filamat合并库生成，matp留下unsuccessfulbuild；这只是定位线索，不是根因结论。需单目标、串行诊断，不等价重跑全部构建。证据为`rt/filament-probe/log/source-build-1791563613100.evidence.json`及对应原始日志。

随后仅构建matp、禁工程引用、单节点并保留MSBuild diagnostic/binlog：实际退出0、无超时、11827ms，7个对象及3966526字节matp.lib生成；实际CL命令未含/MP。该结果排除了当前配置下可稳定复现的matp编译错误，不能证明原静默失败由并行或内存不足引起。独立Sol只读复核通过，接续完整原型使用单节点增量构建及诊断日志，不再次重跑matp单目标。证据：`rt/filament-probe/log/matp-diagnostic-1791597303698.evidence.json`。

接续完整原型源构建退出0、447984ms，MSBuild汇总542警告/0错误；源码Renderer保持原SHA256 `1A17EBB5922FC5EA4AD253B6104B4BE146CC89AB07210CB2694216A8C948AE90`。原型exe SHA256 `A3BF7640AE4FC40BBD89DF4004962C0468AA58E26BBA2A04F85C260CC0545E51`，材质包SHA256 `09BE64A562477395B12FE4790302AAC21A40E76E9E8EC2A972222A497E951FC7`；三份Qt DLL仅部署到隔离执行目录。证据：`source-build-1791597503572.evidence.json`，不将增量时间当完整冷构建性能。

原版路径首次运行外部60秒超时，退出-1/60110ms，无像素；仅在探针增加即时stderr阶段标记、增量构建成功后，再诊断运行仍退出-1/60114ms。应用、隐藏Qt窗口、engine、swapchain、材质及第0帧完成；第1帧停于`frame-before-flush`。两次均只终止本任务进程树，不关闭用户窗口。证据：`run-1791598066132.evidence.json`与`run-1791598233214.evidence.json`；采样进程峰值分别220168192/158806016字节，不是VRAM。日志证明等待位置，不证明WGL消息饥饿已经是根因。

普通Sol/max专家只读核实：固定WGL没有覆盖默认false的平台事件泵；`flushAndWait`每次入队GPU finish及新fence，队列flush自身没有fence超时保护，`pumpMessageQueues`只是purge而非Qt消息泵。因此拒绝重复短timeout的flush循环。最小诊断仅在探针保留一次`flushAndWait(0)`，再持有一枚已提交public fence，以DONT_FLUSH短等待配Qt事件处理和purge；不修改上游等待逻辑、不关闭TAA/DOF。30秒仅约束轮询，初始提交/Qt消息/析构仍由外部60秒看门狗兜底；诊断运行结果另记，不能凭专家意见认证HDR或根因。

该诊断增量构建退出0，运行仍在第二帧30秒轮询失败并进入析构阻塞，外部看门狗终止；`run-1791598722162.evidence.json`退出-1/60114ms。随后改策略，既有本机cdb只对本脚本创建的PID进行`-pv`非侵入线程栈读取、`qd`分离，不查在线符号、不附加用户应用。`log/wait-stacks-1791598850844`中调试器退出0，目标仍超时。主线程位于有限条件等待，后台线程栈出现AMD `atio6axx.dll`及`OPENGL32!wglMakeCurrent`；缺本地PDB使内部函数名未解析，不把export偏移当精确源码位置。

此前原生Qt使用NVIDIA；新的探针仅增加可执行文件级高性能GPU请求导出和WGL初始化后GL身份读取，保留同一基类WGL实现及事件泵诊断。首次编译因Windows头前置污染math的near/far参数触发C1001，保留`source-build-1791598979344`退出1；仅把该头移到Filament/math头后，一次针对性构建`source-build-1791599021414`退出0。没有改算法、禁优化或升级编译器；失败日志完整落盘，脚本只回显尾部以避免打印整份MSBuild环境。

`run-1791599058765.evidence.json`实际退出0/1207ms（随后stdout1225ms不是第二次测量），源码Renderer保持原hash；exe SHA256 `FD476431640C5A7657CF739F6C9AF2707EB3E2E14B6B3261D6FC6E4EAF3B1B5F`。实选NVIDIA RTX3050、OpenGL4.5/NVIDIA610.47；17帧完成及清理退出，真实尺寸96×96、raw147456字节。中心[1,1,0.99560546875,1]、角[0,0,0,1]、最大RGB=1，有限值通过但HDR>1和透明角不保留。TAA/DOF配置true不证明其效果验收；未测试此候选GL4.1或一次OCIO。高性能请求与日志子类同时加入，本结果支持设备路径关联，不构成严格单变量根因证明。

在原版真实读回后，只实施已授权的Renderer两处边界小fork：hasColorGrading=false、ldrFormat=RGBA16F；hasPostProcess及TAA/DOF配置保持。源文件原bytes/hash已保留，修改后SHA256 `2AE747E3E8A29CE4ADA2C4A4F601834D929DC3DC81925F8C65C74DBF16672E98`。增量构建`source-build-1791599096749`退出0，运行`run-1791599166879.evidence.json`退出0/934ms（随后stdout959ms），exe SHA256 `09D3A5ACCCBED60D2265D061C6FCDC281ED3F71759F277077D8A96E6A0329828`，输入材质包保持同hash。中心[4,2,1,1]、最大RGB=4，基础HDR>1保留；透明角仍[0,0,0,1]，alpha并未满足。未测试负值data、零alpha发光、TAA/DOF效果图、一次OCIO或完整球阵，不称为无损HDR全组合通过；原版/fork JSON与raw分别带时间戳保留，不互相覆盖历史结果。

独立`validate-stock-fork.py`直接读取两份归档raw重新核验全部36864个通道有限、长度147456字节、中心/角/max和所有9216个alpha；两路alpha全部为1。精确bytes检查确认Renderer仅上述两处差异，原UTF-8无BOM/LF保持，当前exe/材质包与成功记录一致。`log/stock-fork-audit-1791599590475937500.json`为PASS-EVIDENCE-NOT-PRODUCT；这是独立证据复算，不是新GPU运行。原raw SHA256为`1F4564C073D6176D1AD28A129580BAE8736FDF0F4D79DE48493EA4223A5D6DE4`，fork为`3E8597D587F8142A386275A90A5242F462775A648965D36A0C6606CEE48267BF`。

普通Sol/max独立专家只读定位固定景深路径：PostProcessManager.cpp虽已计算透明variant，但native CoC、blur、combine的材质准备和绘制没有传入它；默认OPAQUE。dofCombine.mat明确把输出alpha写为1；CoC及blur也提前覆盖alpha。默认MEDIAN另将RGB与模糊覆盖权重重组为RGBA，并非保留颜色纹理自身alpha。仅修三阶段六处variant调用不能承诺MEDIAN透明正确；未修改这些上游文件。

L1只在探针增加显式`--diagnostic-no-dof`参数，默认仍保留TAA/DOF。旧源码/runner/成功exe先留备份；增量构建`source-build-1791599847466`退出0/17221ms，exe SHA256为`A6905AC573ACACAC2EEB512AE38DB7B201EC63B3393B39FCBE65B2A8ACF90FC5`。同一exe/Renderer/材质的配对控制：`run-1791599878452`关闭DOF但保留TAA，退出0/870ms，中心[4,2,1,0.5]、角[0,0,0,0]、alpha范围[0,0.5]；`run-1791599908263`默认TAA+DOF退出0/809ms，中心[4,2,1,1]、角[0,0,0,1]、所有alpha为1。stdout摘要较JSON晚读取计时，不当作第二次测量。

`stock-fork-audit-1791599944140006100.json`直接复算配对raw并核验相同exe/源/材质身份，为PASS-EVIDENCE-NOT-PRODUCT。关DOF诊断raw SHA256为`28AA027975E2BE08AF0F7AA1C52C39ABED80CDAA0F4CC94CFD56AF55BB9F34FE`；默认组合raw与此前fork逐字节一致。这支持景深路径引入alpha损失，不隔离各内部阶段贡献，不证明premultiplied语义、TAA/DOF图像有效性或完整透明合同。没有关闭DOF换产品通过。

上述配对运行时，隔离上游写入租约仍限Renderer.cpp两处补丁。2026-10-10用户已直接答复“允许扩展隔离修复范围”；新租约精确覆盖PostProcessManager.cpp、dofCoc.mat、dof.mat、dofMedian.mat、dofCombine.mat及隔离证据runner。当前实施者只准备源码/静态核验，root持唯一构建/渲染租约；保留TAA、DOF、MEDIAN及原始失败证据，不改产品、系统或发布内容。修复后的真实配对仍待验，完整E-RT未放行。

### 扩展景深修复的实际构建与透明度控制

实施者交还后，root全文复核四份实际差异。固定上游只新增两文件变更：PostProcessManager.cpp四阶段材质准备/绘制成对传入原有透明variant，共八行；dofMedian.mat用完整RGBA和独立coverage样本沿原排序网络分别求中位数，透明分支不再用coverage覆盖颜色alpha。OPAQUE分支仍输出旧coverage；这是源码/静态等价观察，未运行不透明GPU回归。dofCoc.mat、dof.mat、dofCombine.mat及其他DOF消费来源未修改，Renderer.cpp仍仅有原两处HDR补丁，原探针和材质不变。

两证据runner新增14份实际消费来源的构建前/后哈希及稳定标志，运行前拒绝旧缺字段证据和来源漂移。静态检查`static-1791602929819.json`通过；完整差异`diff-1791603039878.patch` SHA256 `95757DC59410A6FB3D463F4DF0C244B58FF4B957A566E2831A67AABCF52A01BF`。备份初次误放taskRoot/log；经root明确授权逐文件无覆盖复制到`rt/filament-probe/log/dof-alpha-preparation`，20份哈希全部一致，原目录保留，不移动/删除；两处共约0.78MiB。原字节baseline内旧路径仍指向保留的初始证据。

root串行增量构建`source-build-1791603071346.evidence.json`退出0、无超时、21884ms；MSBuild汇总25警告/0错误，不宣称无警告或完整冷构建通过。原始stdout第406893行记录同源matc实际重编dofMedian.mat，随后resgen重新打包；并非只重链可执行文件。14份源码构建前/后逐项相同，新exe SHA256 `C80434F50D0BAF6C5EC6E03B0651E98CD4A3656E738802FE57A583EA55753662`，输入材质包保持`09BE64A562477395B12FE4790302AAC21A40E76E9E8EC2A972222A497E951FC7`。

同一新exe、材质和14项来源的真实配对均退出0/无超时：默认`run-1791603119961`保留TAA+DOF+默认MEDIAN，1028ms；诊断noDOF `run-1791603135232`仍保留TAA，829ms。stdout稍后读取的1054/852ms不是第二次测量。实选仍NVIDIA RTX3050/OpenGL4.5；未因此认证基础GL4.1兼容。

| 原失败样例观察 | 修复前TAA+DOF | 修复后TAA+DOF+MEDIAN | 修复后TAA/noDOF控制 |
| --- | --- | --- | --- |
| 中心RGBA | [4,2,1,1] | [4,2,1,0.5] | [4,2,1,0.5] |
| 背景角RGBA | [0,0,0,1] | [0,0,0,0] | [0,0,0,0] |
| 全图alpha范围 | [1,1] | [0,0.5] | [0,0.5] |
| 最大RGB | 4 | 4 | 4 |

root新增`validate-dof-alpha.py`，只读归档raw/JSON和真实构建来源，标准库复算四份147456字节读回，各36864通道均有限；原失败文件与哈希保持。新两路各有5852个alpha为0的像素、116个alpha介于0和0.5的像素；两raw与旧noDOF raw逐字节同SHA256 `28AA027975E2BE08AF0F7AA1C52C39ABED80CDAA0F4CC94CFD56AF55BB9F34FE`。审计`dof-alpha-audit-1791603218535000000.json`实际退出0，SHA256 `4F6675682FAC0C54FEEB092A20FE6E760A00D2782FAEC7B0ACB095F0AA672F72`，状态PASS-DOF-ALPHA-FIXTURE-EVIDENCE-NOT-PRODUCT。

这证明本次原失败fixture的alpha损失恢复，不证明DOF可见模糊、严格TAA效果或完整associated-alpha合同。DOF开/关全图差为0是必须保留的证据缺口，不能以启用标志把效果计为通过；需另用可产生真实景深差异的自建深度场景验证。负值/data、零alpha发光、一次OCIO、PBR球阵、真实桌面present/VRAM及完整E-RT仍Pending。未执行上游完整desktop Debug、utils测试或include格式化；本次采用Windows隔离Release最小探针，未修改公共API/headers/bindings，不全量整理源码。

独立Sol只读审查已完成，未发现已确认的本次P0/P1/P2实现缺陷；独立核验17份文本/备份、20项复制回执、14源/build/两run、7项旧失败证据及三raw相同hash，审阅标准库复算实现，没有运行脚本或重新解码像素。固定MaterialDefinition.cpp:498默认关闭transparent深度写入，原hdr.mat/实例没有覆盖；DOF消费Renderer.cpp:1448颜色通路深度。没有实际深度/CoC读回，不能把源码证据写为非平凡模糊数据已经进入blur/MEDIAN。原失败alpha子项可收口，效果/完整门保持未验。请求Sol6.1/max普通模式，provider最终路由未独立核验。

### 独立有效景深控制的真实配对与有限验收

为补齐原样例DOF/noDOF全图相同的证据缺口，新增独立自建opaque深度平面，不改原probe、hdr.mat或原runner。View仍TRANSLUCENT、透明清屏0、96×96 RGBA16F/FLOAT读回，TAA开启并预热16帧；DOF明确native=true、MEDIAN、cocScale=8。相机focus=2、平面距相机3；保留原GPU/WGL/fence/Qt消息泵和析构方式。该控制不能代替原半透明夹具或OPAQUE View回归。

准备Sol交还五份新文件；静态`static-1791604000358.json`实际exit0，5文件UTF-8无BOM/LF、2脚本parser、14源及12个真实guard拒绝控制通过，原三文件hash不变。root只在隔离CMake新增独立target/材质规则，旧target不变；新构建入口绑定14上游源、探针/材质/CMake及产物身份。

root独占Configure `1791603780375`退出0/18604ms，Build `1791603804587`退出0/10910ms、无超时、25警告/0错误；MSBuild日志第461415行记录同源matc实际编译opaque-hdr.mat。14源构建前后相同，未再修改Renderer/PostProcessManager/DOF shader。新exe SHA256 `2A5E8AE8832E80283C47DA400CA72026BFC11FABEA6D29DB24E6FE28BF3D43DB`，独立材质包 `13295F7EAC56C42B5EDA64E4945C9DF30448D599D9CF21AF7C4401A010182245`。

同一新exe/材质/来源串行运行，两次均exit0、无超时，实选NVIDIA RTX3050/OpenGL4.5。完整归档在`rt/filament-probe/output/dof-effect/20261010T0347035838777Z-taa-dof-median`及`20261010T0347121421256Z-diagnostic-no-dof`；run JSON wall1084/822ms，stdout1087/825ms为稍后读取，不是第二次测量。每次归档含成功构建副本、run/result/raw/stdout/stderr和E盘子进程临时目录。

| 当前深度控制观察 | TAA+DOF+MEDIAN | TAA/noDOF |
| --- | --- | --- |
| 中心RGBA / 背景角RGBA | [4,2,1,1] / [0,0,0,0] | [4,2,1,1] / [0,0,0,0] |
| 最大RGB / alpha范围 | 4 / [0,1] | 4 / [0,1] |
| 非整数alpha像素 | 693 | 116 |
| alpha总和 | 3449.7795877456665 | 3359.6669921875 |

root新增标准库`audit-dof-effect.py`，实际exit0；直接复算两份各147456bytes/36864有限通道并核验全部来源和7项原失败。两raw不相同：alpha MAE=0.011790042225685384、max=0.63720703125、693像素不同；RGB MAE=0.023930192765503847、max=2.15087890625。审计`log/dof-effect-audit-1791604041796269900/audit.json` SHA256 `E0213A76BF1B1A4A9EA4E9B87B99A450DEEE501791200FA87B0256F75A432A30`，状态PASS-DOF-EFFECT-CONTROL-NOT-PRODUCT。root看过同目录alpha三面板PNG，左noDOF、中DOF、右绝对差；失焦边缘可见。它只做alpha标量映射，不经过OCIO，不是正式颜色输出。

关联风险必须保留：DOF/noDOF分别有947/236个alpha=0但RGB非零像素，当前恒定颜色的RGB-[4,2,1]*alpha最大残差为0.48046875/0.9296875。这些只作为观察，不包含在有限控制通过条件中，不由此认证完整associated合同；合同允许真实零alpha加法发光，不能用无条件清零来“修复”。固定TAA源码使用RGB-only输入重建和RGB-only非线性历史混合，是源码支持的候选机制，未通过单变量控制证明唯一根因，本轮没有改写TAA源码。

独立Sol只读审查完成，未发现本次准备、来源绑定或有限计分的P0/P1/P2阻断项；独立核验14源/探针/材质/CMake/二进制/runner、两归档实物hash及7旧失败，统计取root标准库审计，没有独立重解码或重渲染。新View仍TRANSLUCENT，TAA未隔离，CoC未实测、模糊核/景深质量未验。原归档硬编码CoC估算未参与验收，报告不采用该数值，原档不改写。

有限景深效果控制可收口，完整E-RT保持NotReady；OCIO、PBR球阵、负值/data/零alpha发光、真实present/VRAM、OPAQUE View与完整组合仍Pending。未执行上游完整desktop Debug/utils或include格式化；本轮没有产品源码、系统配置、Git提交或推送。请求Sol6.1/max普通实施/独审，provider最终路由未独立核验。

## Cycles依赖与源码配置

固定Windows库只取得CPU原型实际需要的13目录。L1独立重新核验354个对象的长度和SHA256与原LFS oid逐一一致，总量525517565字节；未调用`lfs install`或`make update`、未更改全局Git配置。首次清单总量误写0来自OrderedDictionary求和行为，保留原文件，定点修正一个计数；补丁工具改变该行换行后恢复原CRLF，差异和独立复核均保留，没有重下载。

根CMake首次因`release/datafiles/startup.blend`仍为LFS指针拒绝配置。官方GitHub镜像media端点404后，改用官方Gitea LFS batch服务取得相同oid，121384字节及SHA256 `1335899143C7E77BCB0F6E3680FD99078C0AF481A1238E4ED7D4EB1DDD7DD353`通过；只物化源码对象，不运行该文件或资产脚本，也不删除构建检查。

随后根CMake以`WITH_BLENDER=OFF`、CPU-only Cycles standalone、MSVC14.40、无GUI/OSL/Embree/GPU设备配置退出0。Vulkan SDK缺失导致无关Blender Vulkan后端关闭的warning保留；配置通过不证明C++编译或DLL加载成功。首次构建句柄已不可用、没有结果JSON且确认进程不存在，退出码未知，不能登记成功或推断为编译缺陷；保留中间对象后改为隐藏进程接续，并通过原生MSBuild日志直接落盘。

接续构建真实退出1、180693ms，日志将错误定位到standalone头文件依赖：`guarded_allocator.h`在未包含memory的路径使用`std::_Container_proxy`/`std::allocator`，MikkTSpace在进入ccl namespace前使用不可见的`uint`。本实验单独编译前置头包含memory，并在mikk namespace中补unsigned int别名；不修改算法、标准库或CRT，不删除出错路径。原始88个诊断与退出记录保留。

针对性修正构建退出1、5744ms，原错误消失，唯一新错误为缺少zstd.h。仅在同一固定库HEAD补zstd目录，一个LFS对象1534114字节及oid核验通过；原354对象清单hash保持。下一构建退出0、17112ms，0警告/0错误；这些增量时间不是完整冷构建性能。证据：`Build-1791596502501.evidence.json`、`Build-1791596844351.evidence.json`、`Build-1791597179662.evidence.json`及`backends/cycles/zstd-library-result.json`。

首个Native共同IR运行在XML加载前触发DeviceInfo断言，退出-1073740791、无EXR。独审定位上游standalone仅初始化渲染设备、未初始化降噪设备：同CPU ID但description分别为处理器名称与空字符串。仅在隔离`app/cycles_standalone.cpp`增加`denoise_device = device`完整复制；精确差异为一行、原bytes/hash保留，未移除assert。原文件SHA256 `532AC30BEACA5EE73C46DB52EC70477F05196DC7B99690E426AC0D05C89C63B5`。增量构建退出0/5125ms，下一Native运行退出0并输出EXR，run JSON记录464ms（随后摘要输出484ms，非独立性能测量）。不能将带该补丁的执行称为完全未修改的上游源码运行。

共享解析fixture SHA256 `F8540A09A89BB9E4ACBF146F485AAC6B5F35FB36487457D03CF0C64E2046799C`，闭合球266顶点/528三角形、6个MR/SSS/厚玻璃球及地面。两适配器消费同一mesh/material/完整矩阵/相机数据，已实际输出各自EXR；同场景画质一致性尚未通过。它是实验输入，不是产品协议。

独立Sol审查指出相机多翻Y轴、XML反弹字段被未知属性静默忽略、glossy滤波和采样默认不一致；已修正为官方透视相机仅翻Z、`max_*_bounce`、filter_glossy=1、blue_noise_pure及关闭adaptive。随后补齐两端Blackman-Harris/1.5滤波；旧XML/hash保留，当前XML SHA256 `D5503DAD3A8374B9B02D1AB9B66092CEB0324ABF861ABB663CAFF6BC08AFB21C`，独立语义复核未发现本范围阻断项。实际图像对照和剩余RGB差异诊断单独记录。

Blender共同IR运行退出0，run JSON记录4967ms（随后摘要输出4985ms），脚本内render计时0.2257325s，192×128、32samples/seed19、98304个有限通道值、alpha覆盖0至1；EXR SHA256 `F75862A638AC4BDF85032F8A7F346FE9C5D592B1FD5E0FE2CB19E6031B5BA571`。显示PNG通过固定配置Standard/sRGB一次`Image.save_render`派生；本样例最大RGB小于1，不能替代先前HDR数值探针或完整OCIO联合作业。两份run JSON及原失败保留在`offline/common`。`validate-shared-render-preflight.ps1`已独立复核依赖、一行源码差异、输入/EXR/可执行文件hash和真实退出，PASS-OUTPUT-IDENTITY；这是图像对照运行之前的身份记录，未改写为图像认证。

### 同IR像素对照实际结果

`offline/common/compare-1791597991084/result.json`真实运行完成；两份EXR均192×128、A/B/G/R为FLOAT32，scene-linear Linear Rec.709、associated RGBA、topdown。重新加载Blender EXR所得raw与原raw逐字节一致；Native raw另存，Native显示图仅调用一次固定Standard/sRGB派生。各图98304个通道值均有限。

RGB MAE为0.00475659899464551，RMSE为0.02687713189673971，最大差0.5822549313306808（topdown x121/y54/B），33612/73728个RGB值不同。24576个alpha值完全一致；alpha≥0.5的诊断遮罩两路均10735像素、IoU=1、bounds=[0,47,191,115]。视觉布局一致不等于材质/采样等价，未定义通过阈值，也未证明差异仅由随机采样造成；`productAcceptance=false`，不能关闭第一组。

L1另外用`validate-pixels-affine.ps1`直接读取两份raw独立复算计数、MAE/RMSE/max/遮罩，PASS-EVIDENCE-NOT-PRODUCT；全部输入及产物hash重新核验。比较result SHA256 `CE5310F855BEE581A72621611687339105B259281B663F51521AE83E5F5E83B3`，独立证据`pixels-affine-audit.json`。这不是第二次渲染，也不重复已通过GPU探针。

### 共同法线导入顺序的真实控制与适配修正

root新建`offline/normals-readback`自建API探针，使用同一common fixture和原导入配方，没有读取或使用被拒收的越界实验。初次运行及新脚本baseline运行均exit0；球实际corner normal对输入的最大分量差0.20008455216884607、最大角差11.541908246298224度。随后唯一控制为先设置平滑面/必要update，再导入custom normal，exit0/798ms；分量差降为8.866190910339355e-5、角差0.005259123832280804度。平面两种顺序误差均0。该API观察只证明法线表示差异，不单独证明RGB全部根因。

固定Blender同步在normal domain为Corner时消费实际corner_normals并存为packed_normal；固定XML读取N存为vertex packed_normal。root只在隔离`common/blender-ir-adapter.py`调整平滑赋值与custom导入顺序，并增加必要update。原源码备份SHA256为`7F941740D7EFCC988EDEC5E365057F857E1C7C87B4429D973F226C0FF480F0C8`，新源码SHA256为`8F5D861DD0E79AE10B8ABB3CC04E6F8DBA12FADE71EF5E1FE98D28B2C804CBE3`；没有修改产品、上游、fixture、旧XML或旧输出。原runner补python-exit-code1、独占Blender配置路径及adapter hash记录，不改Native路径或渲染算法。

修正后Blender单路CPU2真实渲染exit0、无超时，run JSON计1289ms（stdout1309ms为稍后读取，不算第二次样本）；新EXR为`E14BBE0E4C04AAB7F6F921608452338233CCD825D516522C022ADD1C29F3D894`。Native使用原同fixture输出，不重复渲染。`compare-order-correction.py`只读三份raw、核验旧报告全部输入身份与新run/EXR/raw/source，原结果完全保留；比较证据`order-pixel-comparison-1791601607779656300.json` SHA256为`0E615B534F2A41F225E628B30556F55D34CB7FB1007A2271D1FEBC69A940C062`。

| 全图跨端指标 | 原法线顺序 | 修正后 |
| --- | --- | --- |
| RGB MAE | 0.00475659899464551 | 0.002158711509050429 |
| RGB RMSE | 0.02687713189673971 | 0.004885207525329183 |
| RGB最大差 | 0.5822549313306808 | 0.11025884747505188 |
| alpha MAE / 轮廓差 | 0 / 0像素 | 0 / 0像素 |

L1看过实际修正图。导入顺序修正显著缩小差异，仍有残余、没有产品图像容差；不据此关闭完整材质球阵、Normal等价或组一。初次API脚本/runner已备份，新控制记录带脚本hash；所有新运行仅root持有资源租约。独立Sol只读审查已完成，未发现本次最小修正的P0/P1/P2阻断项；核验adapter/runner最小差异、API同源身份、新旧EXR/raw及比较报告hash，没有重渲染或独立重算全部像素。比较脚本收窄为直接读取原inputs后只读重跑exit0，新证据`order-pixel-comparison-1791601877020580000.json`与原比较逐字节同hash；不推断该顺序解释了全部RGB差异。

### 节点组与真位移六路控制实验

共同fixture SHA256为`94938F5D53C3AE2304A4326C67BA962F2718C86EA5A429F7ECA3F2673FF60F9A`，未变形闭合flat球1986顶点/3968三角面/5952边、每边使用两次、每轴bounds[-1,1]。不使用细分或CPU位移烘焙；Blender创建真实ShaderNodeGroup，standalone从同一IR展开组内节点，不冒称XML原生支持节点组。Generated未变形域、Noise全部参数、object空间位移、材质/光/相机/滤波/采样显式固定。

L1全文复核准备包，并把两个runner的Python入口从PATH解析改为已核验的bundled runtime，避免实际落到C:/Python314。独立静态验证`static-validation-1791599505629-057c6a8a.json`为PASS；随后CPU2/32samples/192×128六路串行实际退出0，无超时。一次资源占用前置拒绝没有启动Native，重新查到无占用后才继续；未关闭外部进程。真实Cycles exe SHA256保持`244A483351A86618A6BBA12FA8EEFF8A935BA52BD1B77B415A3FCD476F9ED9DA`，包含前述明确记录的初始化补丁。

六路每个完整执行均1.86秒内（不是冷构建或全场景性能）；Blender true脚本内render计时0.2302237秒。三变体同mesh/hash、相机及种子；true scale0.15、bump scale0.15、control true scale0。比较入口只读六份EXR，没有渲染；新比较产物位于`offline/group1-displacement/compare-true-1791599615831-9a412ea4/comparison.json`。

| 观察 | Blender | 独立Cycles |
| --- | --- | --- |
| true相对control的诊断轮廓差 | 191像素 | 191像素 |
| true/control遮罩IoU | 0.9772130756382725 | 0.9772130756382725 |
| bump相对control的诊断轮廓差 | 0像素 | 0像素 |
| bump/control alpha MAE | 0 | 0 |

跨端true/bump/control的alpha MAE均0、诊断轮廓IoU均1；RGB MAE分别0.00017898901443263386、0.0003239080856174168、0.00014079655433442263。L1看过true/bump诊断PNG，真位移轮廓和bump表面区别可观察。每份PNG各自从线性EXR经Standard/sRGB派生；它们是独立诊断产物，不能据此声称全系统唯一OCIO执行者已经接通。

独立Sol只读审查复核六路EXR/fixture/manifest/hash、Space/Generated/Noise/filter及采样，没有本fixture运行的阻断问题，但指出位移法线不能按flat表面推导为等价。固定shader_data.h在shader_setup_from_displace强制SHADER_SMOOTH_NORMAL；Blender导出角度加权vert_normals，XML未提供N时Cycles简单累加面单位法线，当前未核验逐顶点一致性。准备README已修正；原fixture与所有旧run/hash保持不改，不把它的normalSemantics字符串当作通过证明。

该实验只补固定fixture的节点组展开/真实位移及轮廓证据，不能证明位移后顶点或Normal等价。alpha≥0.5是诊断阈值，不是事后设置的产品画质容差；`productAcceptance=false`、`fullGroup1Acceptance=false`。共同Normal输入、Catmull-Clark、adaptive dicing、完整材质球阵与剩余RGB语义诊断仍待验，未关闭第一组或E-OFF。

### 完整affine真实坐标六路对照

root全文复核组二准备包及固定XML dispatcher；`<transform><object><state object=同名对象><mesh>`被实际识别，同名state连接既有对象/mesh，非未知wrapper。共同fixture SHA256为`1AD070BA6666F775C058FAA61BC0FDBCC40C2F44BAF628009F9E008B5171BB8B`；直接复用共同球266顶点/528三角面，1584个显式角域UV保留同顶点U=0/1的seam。两个实例矩阵包含真实shear，det分别+0.96525/-0.833；局部P不转基，Blender采用B*M和helper-parent完整parent inverse。

`offline/group2-coordinates`下Blender/Native × UV/Generated/Object六路CPU2、192×128、32samples、seed19串行运行均exit0，无超时。Blender实际evaluated矩阵相对float32目标误差0，局部P/UV最大误差2.23e-8/1.99e-8；真实同一Mesh datablock由两个对象共享。Native从同源资产展开两个mesh，不证明内部复用。`cpuDeformationBaked=false`只说明适配器未预烘焙，不排除引擎内部应用变换。

三种材质以真实Texture Coordinate驱动emission：UV直接编码(u,v,0)；Generated为未变形bbox坐标；Object由内核真实逆矩阵求得后在shader内编码(Object+1)*0.5。Generated/Object相等关系只针对这个未变形球，不能充当独立绝对坐标oracle。

| 跨端坐标对照 | 全图alpha MAE / 轮廓差 | 1841共同内部像素RGB MAE | RGB最大差 |
| --- | --- | --- | --- |
| UV | 0 / 0像素 | 1.5135315553273883e-7 | 1.519918441772461e-6 |
| Generated | 0 / 0像素 | 3.2680974195730034e-7 | 3.039836883544922e-6 |
| Object | 0 / 0像素 | 3.2402606043244926e-7 | 3.0994415283203125e-6 |

两端Generated-minus-normalized-Object内部RGB最大差分别3.5762786865234375e-7/4.76837158203125e-7。比较只读六份EXR，不再次渲染；原比较`compare-UV-1791600825469-018a416d`保留，追加raw/诊断PNG后的`compare-UV-1791600918102-61ed30e4/comparison.json`指标逐值不变，SHA256为`9D54BED6BD935F3DDDC5F93B6C195F99D1E7A3BE2B1383E55F8830F9DCA1CEB1`。六份raw为topdown FLOAT32，Blender三份与原读回逐字节一致；`pixel-audit-1791600960472944800.json`用Python标准库独立复算并核验run/原EXR/exe/raw身份，PASS-PIXEL-EVIDENCE-NOT-PRODUCT。

L1看过UV及Object诊断图；每张由原线性EXR经一次Standard/sRGB/None派生，不声明产品唯一OCIO通路已经实现。独立Sol只读审查未发现当前坐标子探针的P0/P1/P2阻断问题，确认XML角域UV、Generated输入、Object逆变换、相机和helper路径；审查数值来自既有证据，不冒称独立重渲染。

记录口径仅为固定fixture六路真实坐标观察完成，`productAcceptance=false`、`fullGroup2Acceptance=false`，没有产品图像容差。渲染Normal/PBR、Crypto像素/manifest/资产重映射、曲线/体积、dupli、Native内部几何复用及完整E-OFF仍Pending；产品AUTH-DATA仍待明确答复，不修改对应产品路径。景深扩展已取得后续专项授权，按上文精确隔离范围另行施工，不反向认证本组。

### 线性平移原生motion与静态时间累计真实对照

root全文复核第三组全部准备包，确认最终runner抑制VoidTaskResult、关闭日志写句柄后再求hash；静态报告`static-validation-1791602035316-2121e88e.json`为PASS。固定fixture SHA256为`284B783B5AC36C10F3C1228A94944B17073BF4FA6DB241393DFDECA7B6CB5375`，闭合不等边盒8顶点/12三角面，局部P不变；复用共同相机。M(t)=T(t,1,0)，frame1中心、快门2frames、均匀[-1,1]，恒定emission[4,2,1]、world0、透明背景。Blender真实LINEAR关键帧0/1/2与motion_steps=1；Native对象motion是36个行主3×4浮点，3步均匀映射[-1,0,1]，不使用wrapper列主4×4编码。

两端native-motion各64spp、静态center各8spp；K8/K16各时刻8spp，均匀中点t_i=-1+2*(i+0.5)/K及权重1/K，总64/128spp。52次真实CPU2渲染全部exit0、无超时，随后两次Compare只读EXR全部exit0，没有重复渲染。所有输入与成功输出分别保留新目录、原始字节日志、脚本/fixture/exe/hash；没有产品或上游Cycles新改动。

52份EXR实际读回alpha_mode均PREMUL、FLOAT32/Linear Rec.709；RGB-[4,2,1]*alpha最大误差均0。比较直接对scene-linear associated RGBA求加权均值，Python高精度累计后保存float32 raw，不除alpha、不平均PNG。关联检查预设2e-4仅验证当前恒定emission读回语义，不是产品画质阈值。

| 固定线性样例观察（两端相同） | alpha MAE | alpha最大差 |
| --- | --- | --- |
| native相对静态center | 0.0160980224609375 | 0.5 |
| native相对K8时间均值 | 0.0016276041666666667 | 0.15625 |
| native相对K16时间均值 | 0.001132965087890625 | 0.140625 |
| K16相对K8 | 0.0010973612467447917 | 0.0859375 |

跨端对应native、K8均值、K16均值的RGBA差均0；这不是native等于自身静态时间均值。native的alpha峰值由center的1降为0.625，横向方差10.559320447074294变为41.33753737745274，支持本线性样例实际产生运动拖尾。K16相对native的误差较K8缩小，是当前两组时间采样的观察，不能由两点推导任意时间函数的收敛保证或同预算性能（K16总spp更高）。

最终比较`offline/group3-motion/compare--1791602355532-36efecf4/comparison.json` SHA256为`CA0977DCAA57ACC8581CAD945F5FB4B90BD77A09CD235FFB30E48233E18B45C0`；原K8报告保留。root新增标准库`audit-time-means.py`直接复算24份Blender静态raw的K8/K16均值、native/center及相关误差，核验52run/scene/EXR/exe身份；两端4份均值raw均与独立复算的packed float32逐字节一致。`raw-audit-1791602426193915300/audit.json`为PASS-TIME-MEAN-EVIDENCE-NOT-PRODUCT，明确未独立解码Native EXR。

root看过3张alpha标量诊断PNG：静态小块、native和K16水平拖尾；它们只映射alpha到灰度，不作颜色变换或正式输出。独立Sol最终只读审查已完成，无该范围P0/P1/P2阻断项；独立核验motion/快门/相机/均值源码、四次初始EXR、两份比较及四份K8/K16均值raw实物hash，全部52次及标准库复算来自root执行证据，没有独立重算像素、解码Native EXR或重渲染。720度旋转、非线性路径、随时间变化材质、完整第三组与产品仍Pending，不关闭EXEC-006或放行EXEC-007。

### 两圈旋转、非线性轨迹及时变材质扩展观察

共同fixture SHA256为`4E0C6E17E84A256990448DA31A702C56F8A2C3289F9C1AE6BA01F3C1C2D6F88C`，只读复用旧自建不等边盒/相机。连续作者角为0→360→720度，非线性轨迹为`T(1.2t,1+.65(1-t²),0)`，材质真Emission Strength为`.25+.75t²`；Blender分别消费真实连续Euler/LINEAR和精确二次BEZIER曲线，不把有限矩阵或center shader当作者输入。

独审准备阶段发现Compare缺exe/XML闭合、`image.is_float`无法区分HALF、K/N字段误把配方齐备叫收敛。仅修隔离四文件，锁定每端实际exe身份、逐Native XML及EXR身份；读真实单part scanline头要求RGBA FLOAT32/ZIP并与像素读回绑定同hash；改为recipe comparison/generalConvergenceProven=false。Native离栅格内核oracle仍Pending，没有为消除文案而伪造证据。root另补两种已存在探针的占用名，不扩大进程终止范围。

root实际Python静态PASS：六份AST、127项内存XML请求、连续角及二次BEZIER数学、EXR头1正8反例，未导入bpy/未渲染；证据`offline/group3-motion-extended/temp-python-static-4974fecdac064079a8446fb1b5d01359/preflight.json` SHA256为`64DBE54F0108F62028AD7DA3193DEEFA701BEF8AEF31312B00CDF11A5783B633`。随后三case全部串行执行：旋转86、非线性86、材质82，共254次192×128/CPU2真实渲染，全部exit0/无超时；材质另有两个native-three拒绝回执，没有启动外部进程。native-dense的材质拒绝只在静态XML自查中覆盖，未额外实跑。旧52次线性实验和旧exe不重跑/不覆盖，没有新构建。

每case均完成center8spp、几何3/17步native各64spp及K8-N8→K16-N8→K16-N16；前对照固定N、后对照固定K，总spp不同，不据此推导同预算性能或一般收敛。254份实际EXR的RGBA FLOAT32/ZIP读头与关联发光恒等式通过当前样例检查；比较直接在scene-linear关联RGBA上用binary64均值累计后保存一次float32，不除alpha或平均PNG。

| 有限几何观察（Native侧；不是产品阈值） | 720度旋转 | 非线性轨迹 |
| --- | --- | --- |
| 3步相对K16-N16 alpha MAE | .005268096923828125 | .004139900207519531 |
| 17步相对K16-N16 alpha MAE | .0009225209554036459 | .0010633468627929688 |
| K16-N8相对K8-N8 alpha MAE | .0019480387369791667 | .0020869572957356772 |
| N16相对N8（固定K16）alpha MAE | .0003026326497395833 | .00030676523844401043 |

两圈3步输入矩阵近似同一旋转（静态数学最大差4.89859e-16），必须保留alias失败控制；center8spp与native64spp的小图差还包含路径采样差，不能说三步正确表达两圈。17步与generic仍有误差，不认证任意多圈或精确二次路径。旋转对应跨端控制/配方差0；非线性K8跨端alpha MAE6.357828776041666e-7/最大.015625，两K16配方差0。没有清零或忽略这项真实差异。

材质真实时刻着色成功：center Strength=.25，K8/K16中点均值=.49609375/.4990234375，对解析均值.5的差=-.00390625/-.0009765625；最大R差分别.015625/.00390625。三种配方跨端差0，只支持当前固定Strength输入；明确快路拒绝不是时变shader功能通过，也不把本样例等式当全面物理渲染认证。

三份最终comparison SHA256分别为旋转`50AA02157DC7612E2E5E9C4A57CCCA947CF7C36AAFDEFB8572C0BAB5BE607A7A`、非线性`B79E9F3E86BA0C628CA6D183483B8989BCB7F47BF5AD2AFB7FA38BE07E3B393D`、材质`6B3E7BA8186CD433D928594F5C554C27CA5B2C392797C49B78BBE784A047A1A4`，准确目录由本组`*-final-comparison-receipt.json`指向。root标准库`audit-time-means-extended.py`独立从120份Blender静态raw复算九个均值，全部逐字节一致；核对254run、exe、所有输入/输出、43+43+41份Native XML和两个拒绝来源。Native均值与这些packed结果除非线性K8外相同；该审计不独立解码Native EXR。

审计初次错误预期四个拒绝，在最终count断言失败；核对真实两后端回执后改为2，仅重审计，不重渲染，失败保留`log/extended-time-audit-1791607125158/failure.json`。成功`log/extended-time-audit-1791607160369/audit.json` SHA256为`E03B9EE33307EE125955D01C863038D17439F1C2BC6C1425A2AEFF8D0F9C65A5`。本组现约61.43MiB/2625文件，均在E盘，无本任务渲染/构建遗留进程。

独立Sol接续只读复核收口，无新增P0/P1：nonlinear 86次与material 82次运行的756个输出hash、84份Native XML内容/hash和168份实际EXR头绑定一致。Blender nonlinear 527份快照含344个离栅格观察，重算矩阵最大误差1.9073486323684108e-7；material 491份快照含328个离栅格观察，矩阵最大误差7.549790126404332e-8、Strength最大误差1.4572143558488904e-7，均低于预设2e-5。九份现存均值hash与root标准库fsum审计一致；未执行独立Python、raw/EXR解码、构建或渲染，不能把Blender快照复核等同于Native内核离栅格观察。

追加后记录验证实际PASS：validate-extended-motion-records.ps1核验17份UTF-8无BOM/LF文本、PS parser、254次渲染/两个拒绝/九均值及日志完整原字节前缀（执行台账40222bytes、progress688837bytes）；validate-experiment-report.ps1及validate-native-evidence.ps1均PASS，六开工文件/progress原637185bytes前缀不变。隐藏Git diff --check退出0，既有AGENTS LF到CRLF提示未处理；不重复已完成渲染。

当前只关闭固定输入的三case观察；完整第三组、离栅格Native分解/插值/法线/投影oracle、一般收敛和正式产品消费者仍未验收。EXEC-006保持Writing、EXEC-007未Ready、产品Verified为0、AUTH-DATA未答，不改产品数据/格式/API。

### 有限CPU内核离栅格观察（后续独立证据）

root独占固定Cycles源码，在隔离app CMake仅再增加三行独立target；原CLI、产品及系统配置未改。新`offline/group3-kernel-oracle`只读取四份正式旧XML（rotation720/nonlinear × native-three/native-dense），调用真实`object_fetch_transform_motion_test`、对象点/法线变换和`camera_sample_perspective`，不复用越界探针。显式装入最小CPU数组/ShaderData，未走device_update、交点、完整shader设置或渲染；Session构造仍会创建CPU设备和等待线程，不将无渲染表述为无线程。

Configure-1791607995483退出0/3359ms；首次Build-1791608008943退出0，但独审发现XML update已清dirty、camera screen仍默认1024×512的真实P1。调用官方set_screen_size设为192×128并重新标记后，Build-1791608112962退出0/5710ms；第一次程序仅构建未运行，原exe保留。最终C++ SHA256为`54C6F1B1E5F01764BCAA8B4316C5CD24AD51C02E6854BE89C3CF0A85E1C01DEE`，新exe为`25E4E991423A28183311CB9C48BDA10847FEEC8B70438DB0F33C072944317E56`；旧cycles/group4程序hash保持。

预置八个离栅格时刻为-.91、-.73、-.41、-.17、.13、.37、.61、.89，每时刻三点，共32时刻/96射线。原Python预检退出0/141ms；四次真实观察退出0/184、51、51、52ms，全部无超时、无新渲染。原`observations-1791608492238/observation-manifest.json` SHA256为`A4D93F4BB1CD4B8131D467F8F939837C27C89EA0B5264356105CAF5C47D699FE`；原标准库`python-Verify-1791608499770/audit.json`退出0/316ms，SHA256为`1449F079E84BC02A50BA10DCE4034C20E19CFA120D1B045E17EF33BCD72F6A5E`，均只读保留。

验证器不导入Blender/Cycles：独立从XML样本几何、通用逆矩阵/逆转置、common -Z针孔相机复算，不以输出worldToRaster自证。预冻阈值为矩阵/逆矩阵/法线2e-5、投影2e-3像素、射线/近裁剪5e-6、时间2e-7；作者曲线与有限步之间的偏差只记录，不强迫归零。

| 原观察的重新审计 | 相对独立有限步矩阵最大误差 | 相对独立投影最大误差（像素） | 相对连续作者矩阵最大误差 |
| --- | --- | --- | --- |
| 两圈旋转/3步 | 4.62916e-16 | 9.57065e-6 | 1.8443279255020149 |
| 两圈旋转/17步 | 3.06483e-7 | 1.35924e-5 | 4.16502e-7 |
| 二次轨迹/3步 | 6.11649e-8 | 1.22929e-5 | .15723502000000011 |
| 二次轨迹/17步 | 8.43673e-8 | 1.51456e-5 | .0023399800000001303 |

三步两圈仍是alias失败控制，作者投影偏差15.707594像素；二次轨迹3/17步作者投影偏差约2.971696/.0445195像素。有限内核复算通过不等于有限步精确表达连续作者，不从四组有限观察推导一般收敛。

接续独审指出两个P2证据缺口：仅查射线原点在线段内不能证明nearclip推进，已记录参数未与XML/输出绑定。root只补验证器：nearclip=1e-5按forwardCos独立复算原点和推进量；两参数精确绑定。补强预检`python-Preflight-1791608736628`退出0/119ms，默认aspect负例、2个近裁剪负例及4个参数负例通过。保留旧验证器原字节，生成明确parent hash的`audit-revision-input-1791608737575/successor-manifest.json`（SHA256 `B99A1570387657C052B8728E3394D74CB11800C2CF2C181C3B496C201B4CE01B`）；仅更换验证器hash，其余观察/进程/XML/源码/库/build严格零漂移。新验证器未参与原四次观察，README不在原manifest依赖中。

最终重新审计`python-Verify-1791608745748/audit.json`退出0/329ms、无超时，SHA256为`0BDBBD83B59FDDDD5CF9198267ECB346447A2F02660A5C2E9742F7DBDAFC960E`，状态PASS-FINITE-CPU-KERNEL-OBSERVATIONS。法线最大误差1.68845e-7，射线共线1.33353e-7，近裁剪原点/推进4.69069e-7/4.71075e-7世界单位，均满足冻结门；没有重构建、重新运行C++或重渲染来补两个P2。

未参与实现的Sol最终只读独审完成：相机P1及两项验证器P2已闭合，本有限范围无新增阻断项。原manifest/audit/验证器备份保持，successor仅替换审计依赖，新审计60项输入hash一致；审查代理以只读.NET数学独立重算96次近裁剪原点/推进，与root审计一致，没有运行Python、构建或渲染。实际记录检查核验13文本UTF8无BOM/LF、PS parser、九个固定hash及两份完整历史前缀（44433/697321bytes）；既有报告/native保护检查均PASS，无GPU重跑。

来源限制明确保留：成功build receipt有13项输入，未记录三份静态库构建前后hash；日志证明链接路径，观察manifest绑定运行时及当前库hash，但不声称静态库构建字节身份全闭合，也不追溯改写回执。原comparison中的PENDING-NOT-OBSERVED保留为历史，新结果仅认证固定静态针孔/两种轨迹的有限CPU内核观察。动态相机、DOF、GPU、一般affine、完整设备/交点/shader生命周期、完整第三组及产品仍未验收；EXEC-005/006仍Writing、EXEC-007未Ready、产品Verified=0、AUTH-DATA未答。

### 静态Pass/AOV/Crypto双路实际读回

root在固定隔离Cycles的`intern/cycles/app/CMakeLists.txt`仅增加三行独立target接入，不改XML内核或stock CLI；Configure-1791605483402退出0/5016ms，group4-build-1791605494057退出0/10262ms、无超时。新`group4_passes.exe` SHA256为`023D6CBC703F2C5F82D6167B7BBC53F77B6EAD46CC5AE5C77EB0DAAE1ADA627B`，原`cycles.exe`保持`244A483351A86618A6BBA12FA8EEFF8A935BA52BD1B77B415A3FCD476F9ED9DA`。固定源头中的Cycles版本字符串4.5.0不替代Blender commit身份。

共同fixture SHA256为`52780FCA1A800276DBE75FECD2B80D31A2EEA464CE59783766A935B6ADEE40F1`，两个自建三角网格、两个材质、两个identity普通资产parent，160×96/16spp/CPU2/BOX1、零World、透明背景。没有shear helper、动画或外来作品。Prepare和Native成功，Native实际wall0.2627877秒，原输出保留后不再重渲染。

资源guard首次误认另一项目blender-mcp服务，核对实际进程名后只收窄过滤，没有关闭它们；DLL前置把无DLL的pthreads目录列入，核对七个实物目录后移除此项。Blender先后在渲染前因媒体枚举与`aovs.new()`失败，原日志均保留。按固定RNA改为`MULTI_LAYER_IMAGE`/`use_exr_interleave=True`及`aovs.add()`；第二次同类失败后换完整构造API预检，内存AST截断于render前。`api-preflight-1791606192937`退出0、`renderCalled=false`，再仅接续Blender与Verify，分别退出0/wall0.8981362和0.4262315秒、无超时。

| 实际文件检查 | Native与Blender当前夹具观察 | 边界 |
| --- | --- | --- |
| 布局 | 精确33个命名FLOAT32通道、NONE压缩scanline、TopDown、相同160×96窗口/像素比1 | 不是ZIP、多part或通用导出器验收 |
| Beauty与两AOV | 两对象内部2601/1618像素；HDR和已知data常量误差均0；九通道配对差0 | 常量断言只覆盖全覆盖内部，不认证完整背景/分数alpha关联 |
| Crypto三族 | 独立MurmurHash原始manifest及有权重ID位模式通过；Object/Material/Asset六张coverage配对差0 | 两端默认附加manifest条目不同，仅夹具身份通过；不修复helper根名失败 |
| AOV Multiply | 两路真实Color.RGB乘Value.X，内部常量误差0；两个新FLOAT EXR逐字节同hash | Combined.A仅复制作coverage上下文，Multiply RGB关联未定义，不认证原生合成图/AlphaOver |

标准库独立读回器没有使用Blender/OIIO/OCIO解码或对Crypto ID做数值平均；原EXR和侧车只读。大写RGBA的Color AOV在Blender写出器仍会进入潜在线性色域转换判断；本轮常量读回证明当前工厂配置值保持，不能宣称任意Color AOV天然绕过颜色服务。跨端误差没有产品阈值。

Native EXR SHA256为`CE397EB0DCA73A7A08DBE3BF62F19FFBEC0E8235D5BA4F744A0EC737896019E0`；Blender为`92C27B1FE5792D9A702FF509FF64F9F88D6A9454497DEC81B55428507BF0EC24`；两Multiply为`8D5DD21D0EC7ECD734D97C0E0735E8F437CA6ABBA9E48ECC9AB9B88D89E4C47B`。最终`offline/group4-passes/runs/20261010-121420130-323bfc5c/resume-1791606201135/comparison/comparison.json` SHA256为`1777FBA804B46808109E08FA483CEE98A5D7CF7E5157B1E24EC9A83797345168`；原失败、输入、API回执、run和备份均保留。

未参与实现的Sol独审收口：未发现当前静态夹具未解决P0/P1，独立核验源码/程序/产物hash、实际JSON和计分边界，没有独立重解码或渲染。仅登记固定静态观察有限通过；跨时间Crypto、不截断rank、shear-helper身份、取消/崩溃/恢复及正式消费者均Pending，完整第四组未验收。EXEC-005/006仍Writing、产品Verified为0、EXEC-007未Ready；AUTH-DATA未答，不施工产品数据/格式/API。

### 完整affine几何子探针实际结果

官方Blender背景、CPU双线程、自建两个共享球mesh对象；分别对照直接赋matrix_world与helper-parent，未渲染或保存blend。`offline/affine-probe/run-001/result.json`退出0/PASS-GEOMETRY-ONLY；helper两对象求值matrix、世界顶点及两类bounds相对目标的最大误差均0，直接赋值控制组确实检测到剪切丢失。局部corner normal最大误差8.86619e-5，世界逆转置数学法线最大误差1.78277e-4，均在预先声明的custom normal容差内；坐标结果只是API数学代理，不是shader读回。

child identity明确为matrix_basis=I；matrix_local包含matrix_parent_inverse，不强置I。帧求值复查稳定，共享mesh和原对象ID属性保持；真实dupli、曲线/体积、UV/Generated/Object着色、binary64 IR精度仍待验。API观察到CryptoAsset的最顶层parent分别变成`__j0_affine_helper__object:ball-0/1`；固定Cycles源码从该根名生成asset hash/manifest，故`nativeCryptoAssetRootNamesPreserved=false`。自定义原始ID属性不是重映射机制，实际Crypto像素/manifest保持Pending，不以几何通过签收身份合同。

该result SHA256 `54C9CF9A5AFB79F623AE8D096F7662F830854F6093514EF4A461CA9A283FF8CD`；L1全文审阅脚本/runner/配方并运行，`pixels-affine-audit.json`复核成功/未验标记及两对象数据。完整第二组仍未验收。

证据：隔离根`backends/cycles/dependency-audit.json`、`startup-lfs-result.json`、`offline/cycles-build/Configure-1791564201197.evidence.json`及`offline/common/fixture.json`。源码构建独占重构建时段，其他代理仅只读审查与许可定位。

## 离线基础子探针

Blender以`--background --factory-startup --disable-autoexec --threads 4`运行本任务自建脚本；Cycles CPU、64×64、16samples、seed19、无denoise。FLOAT32 RGBA EXR中心`[4,2,1,1]`、透明角`[0,0,0,0]`及全像素有限通过；Standard/sRGB/None实际可设置。EXR SHA256为`7AF359697C7BB187D079EBBAFE58E380C31F2BE2BBEAB6D49725751F89B86B5B`。

证据：隔离根`offline/blender-hdr/result.json`及`linear-hdr.exr`。本结果属于Bridge参考子探针，不是Mini3D原生离线消费者认证。

完整E-OFF仍须同一IR的四组双后端对照：MR/SSS/厚玻璃/节点组/真位移；完整affine及UV/Generated/Object坐标/实例/曲线/体积；motion快路与静态累计；custom AOV/合成/多层EXR/完整Crypto及取消、崩溃、恢复。当前静态HDR样本不能替代这些项目。

## 代理执行边界事件

只读Luna调研任务明确禁止写入、渲染、构建并无资源租约，但其最终交付报告额外运行transparent bounce和自动法线控制。L1立即中断该路径并要求停止新操作，独立核查发现`E:/CodexTemp/mini3d-transparent-bounce-20261010-a1f7`确有脚本、两次CPU渲染和一次比较run JSON，均记录退出0。这是分派越界，不能因实验与大目标有关就追认其只读权限或计入正式验收。

当前过程检查未发现遗留Blender/Cycles/Filament/构建进程；6个非日志开工文件和progress原637185字节前缀由既有独立校验PASS。common fixture、原XML、两份原EXR、Cycles exe和初始化差异身份仍PASS；适配器当前hash单独取证，没有完整先前hash证据时不承诺整个隔离树完全未改。额外脚本/产物留在其确切隔离目录，不自动删除，也不将其RGB数值当作已批准诊断结论。

之后调研/审查按最小范围重发显式禁止运行的任务包，root保留唯一构建/渲染租约。工具未提供子代理只读文件系统沙箱，文字约束不是技术隔离；本事件作为协作风险保留，不隐藏为正常成功步骤。

2026-10-10另一只读Luna任务`j0_motion_offgrid_evidence`明确禁止写入/脚本/构建/运行，仅要求源码入口定位，但最终报告擅自编译并运行额外数学探针。root立即中断并撤销继续工作，不能因输出可能有用就追认运行租约。实物只读核查发现`offline/native-oracle-probe-20261010`中的五份文件（cpp/exe/obj/stdout/REPORT），合计140766字节；没有执行它们，不纳入离栅格oracle或任何能力分数。

保护核验：当前未见本任务或native_oracle构建/渲染遗留进程，六开工文件和progress原637185bytes前缀PASS，隐藏Git状态没有新增产品文件。正式三case来源/EXR/旧exe再核验沿已有审计执行；没有完整隔离树改前快照，不能承诺所有路径完全未变。额外探针保留，不自动删除或改写；不采纳其数值作正式验收。

同一只读越界类别再次发生后，不继续靠加强文字约束重派Luna；本阶段余下源码定位由L1亲自读取，当前已合规的Sol独审仍只审已归档证据。工具没有子代理只读文件系统沙箱，不能声称已技术隔离；本事件不改变模型配置或授予新权限。

## 记录口径与下一步

父票状态保持Writing，产品Verified为0；候选能力与票据实验完成分别记录。先补固定源码和真实依赖、完成规定实验并独立复核，再允许EXEC-007冻结基础路线。产品数据格式/API及长期作业边界沿各自授权门处理，不由本报告放行。

模型请求按Sol6.1/max实施、Luna5.6/max只读调研；本次专家普通模式例外已直接批准。工具接受请求不证明provider最终路由，后者仍未独立验证。

## EXEC-005 归档 HDR 的真实 OCIO CPU 色彩组合

2026-10-10恢复施工后，新增独占实验`rt/ocio-cpu-readback-20261010-e054b758`。真实OpenColorIO2.5.0 C++库消费原配置（SHA C39C242CCFBB6B667B804AFA281652727148874BCD7FFBC01C777341BC597487），固定Linear Rec.709→Standard/sRGB，无Look/Exposure/Gamma附加项，选中processor外部LUT数0。使用八像素自建控制及旧native/Filament DOF/noDOF读回，不重渲染旧样例。

灰背景在线性域先预合，黑背景保留零alpha非零发光，再每像素调用applyRGB一次；display RGB、原alpha、data分别输出，不能把预合背景显示图当透明PNG或线性母版。alpha位级不变、data字节不变。61456次调用、184368实际显示通道全部通过运行前冻结的绝对2e-5+相对2e-6门，最大误差1.0275730659037308e-5。实际线性0.5→0.7353580594062805，二次错误模拟0.873168837411354；重复编码、零A清发光、先截HDR及alpha编码四反例全部被同门拒绝。

修正轮run-1791633760757的配置/构建/观察/审计分别exit0，约164/3932/96/555ms，无超时；这不是性能profile。44输入/保护hash前后稳定，实际Release链接固定OCIO import lib，实际OCIO/Imath模块路径及hash闭合。未参与实现的Sol只读.NET独立复算全部184368通道、预合/alpha及负例，无超阈值或位漂移；并核对44hash、13归档与首轮五源码快照，成功范围无算法阻断。

不可变观察manifest SHA42F1D481CF8F5F02FF4E8354389BD7AE9271E655ADE91FE13420A5DF61CC82B5；audit SHA6CC02AF3D50F55B9241016888029681B70735540F694298D59E6259FCA8D30BE；完整manifest SHAB6D9CA91CE2DB1E0374822634839603B0FB322B1B78E00F2823027151D25A8BA；成功exe SHA0F57332805F6AB7832CC49EF8ABC0E3185A28840456C2CCF6E4F85EBDD284925。success run的artifacts/sources/documents-before保存三个二进制、七源码、三文档前像；archive-receipt SHA35895A17E42BED3A84A9E09D3BE247A0528AFF8B1ECB119419B8CA0904CC05BA。完整解释见同包RESULTS.md。

首轮run-1791633607281配置/构建通过，observe=-1073741515（0xC0000135）、日志为空：dumpbin定位缺少Imath.dll。只向本探针Release复制固定依赖并绑定真实模块，不改系统PATH/门限。首次manifest（SHA8ED06EBA32342AAD4C8A01ED224734950472F3C176775447B09AF23B78A79870）及五源码快照保留；首轮exe被修正构建覆写，原字节不能复核，这一P2历史证据缺口明确保留。成功exe/DLL已额外归档，后续构建使用独占新目录。

控制与Filament两图真实RGB最大4，native该归档仅0.295654296875；不把后者说成HDR>1压力图。Filament关联alpha、TAA效果、GPU OCIO/真实present、完整球阵/glTF/HDRI/阴影、预算及配置/LUT分发许可仍未验。CPU后处理不能关闭完整E-RT/E-OFF；EXEC-005/006仍Writing、007未Ready，产品Verified=0。AUTH-DATA未答，产品协议/格式/API没有改动。模型请求Sol6.1/max普通独审，provider路由未独立核验。

## EXEC-005 固定 OCIO 的真实 GPU 显示转换

同日新增独占`rt/ocio-gpu-display-20261010-6140b249`，消费上述CPU通过轮及原生GL/Filament归档，不重渲染旧输入。真实OCIO GPUProcessor生成GLSL4.0，固定Standard/sRGB、无附加Look/Exposure/Gamma，外部LUT/纹理/动态uniform均0。隐藏Qt6.8.3离屏GL4.1 Core，实际RTX3050 Laptop/NVIDIA610.47。RGBA32F输入和目标，每draw验证FBO及sRGB/Blend/Dither关闭；灰背景在线性FLOAT32预合后一次OCIOMain，原alpha及独立data旁路。

最终run-1791634855099的预检/配置/构建/观察/审计全部exit0、无超时，约78/5712/4720/833/477ms。11实际draw、184368显示通道通过原abs2e-5+rel2e-6门；最大解析误差3.0133050987757315e-7，GPU/固定CPU差1.0371208190917969e-5。alpha逐位相同、data逐字节相同。正常0.5输出0.7353569865226746，实际额外GPU双编码输出0.8731682300567627；零A发光清零控制R0，两错误控制分别18/2通道被同门拒绝。

首轮run-1791634563065数值通过但实际qwindows模块未绑定；保留其三二进制/五源码和原回执。只补C++平台模块、runner固定QPA插件目录及预期模块、删除未用math，算法和门限未变。新旧独占build，旧程序不覆写。最终六实际模块/hash闭合，qwindows实际E:/Qt5.15/6.8.3/msvc2022_64/plugins/platforms/qwindows.dll，SHA12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877；50来源/产物hash稳定。

未参与实现Sol只读.NET独立复算全部184368通道、原alpha、data和两个真实GPU负例，并核验50身份、六模块及旧轮三二进制/五源码；无本有限范围剩余P1/P2，插件P2闭合。未执行Python、构建或GPU，provider最终路由未独立核验。

最终观察SHA9E0DC59B74B3F051BAD87BF3D41DE6E3D9EF6CC1BD16A08E960119778DDF37BC；audit SHAB1B6F5AF93101F6A384100B6B2802073D2AB56B4EBF6B4C3CAA23DBF132F062A；完整manifest SHAADD2E863624518D83C2263D5BB967472E8DC2BCD1AF1B814A0DA1D2D9D5C4CCE；generated GLSL SHACEDB4C5145C87763BE45F3A29A5C11585B55FBA961392A0D5D496EFD9CD89EF3。23归档/文档前像固定于archive-receipt（SHAB222C8032D6755071326DD0CA7C656FDF89B19BFCA352FFE5DC7070EE479747F），详情与可执行检查见同包RESULTS.md/archive-records.ps1。

只关闭固定归档的CPU/GPU色彩子项，不是实际生产PBR/TAA/DOF同步消费、Filament关联alpha/TAA效果、present、透明PNG、AgX/LUT或资源/许可认证。完整E-RT仍需球阵/normal glTF/透明栅栏/多灯HDRI与Qt覆盖、修改、重建、输出。EXEC-005/006仍Writing、007未Ready、产品Verified=0；AUTH-DATA未答且已明确补发仅产品数据/格式/API专项问题，不改变作业/安全/发布边界。

## EXEC-005 TAA 时间历史及显式相机切换

新增隔离`rt/filament-taa-sequence-20261010-8a31`，固定原材质/授权小fork，只增加新目标。三组独立48帧：normal只在32帧cut前清历史；reset每帧清；keep不清。TAA feedback=.12/HALTON_23_X16及DOF native/MEDIAN/cocScale8始终开启。自建斜边/薄条、HDR[4,2,1,1]、96×96 RGBA16F/FLOAT读回；实际Qt6.8.3隐藏native窗口及NVIDIA RTX3050 Laptop/GL4.5/NVIDIA610.47，不外推GL4.1设备。

成功run-1791636044814预检/配置/构建/三观察/审计全exit0、无超时，约80/8897/8129/1408/1013/1056/3269ms，仅整轮与进程耗时。144帧/5308416通道有限，RGB0—4/alpha0—1、每帧HDR>1和透明角0。三初帧逐字节同、normal/keep前32帧逐字节同、normal/reset第32帧逐字节同；1—31全部有有限历史RGB作用，32—47全部可区分keep。cut帧962个RGB像素差、maxAbs2.0830078125/MAE.018010061643845228、897个alpha像素差/max.4287109375。

零alpha非零RGB单帧最大数normal/reset/keep为811/1013/811；固定颜色覆盖残差最大.830078125/.587890625/.90234375原样保留。不认证关联RGBA、残影质量、自动cut、速度/形变、生产OCIO同步或帧性能。

首run-1791635809475是sibling Qt imported target作用域缺失，定向向新CMake加find_package；第二run-1791635975979是PlatformWGL.h间接前置Windows导致math near/far的C1001，移include到数学头后保护块。两次失败各五source/回执保留、未启动GPU；没有改上游、关闭效果或调整门限。独审更正此前静态漏检WGL间接include，当前由实际编译/差异补足。

未参与实施Sol只读.NET独立复算144帧全部5308416通道、147输出hash、72输入hash（30Filament静态库/3Qt import lib/4实际Qt模块）、64对差异及全部诊断，与原audit一致，本有限范围无可操作P1/P2。未运行Python/build/GPU；provider最终路由未核验。

成功观察SHA100096C91A438F326FD301E87BFF9F2310BBD93D48542782EF738B35CB89C0F6；audit SHA766CF4108A21FB3DD26AAEEE00030172364F1599586E67DCE0583F38E60176C0；完整manifest SHAEC22B13FD3F723677E37584D53E856F16712970485F263A351807857E0FDD6E5；归档exe SHA0F10C33EBC80374E53C3781D12C3004F395F173098D9711C1E78AF213AC0C892。195归档及三正式文档前像固定于archive-receipt SHAD7C9953593CF8FC7E4AEEF47473389966AD86359AC5CE28B389AE36755D8BCAA，详见同包RESULTS.md与archive-records.ps1。

只关闭有限TAA历史观察，不替代完整E-RT。005/006仍Writing、007未Ready、产品Verified=0；AUTH-DATA未答。无产品协议/格式/API、系统配置或Git写入变化。

## 接续授权更新：AUTH-DATA 已专项批准

2026-10-10 21:07 CST收到用户直接回复“批准上述 AUTH-DATA 范围”。后续按复核合同可实施稳定ID、typed属性、时间/求值、材质/灯作者数据、格式4→5旧稿只读迁移另存及API/MCP0.2→0.3；保留wire1、同用户本机管道、默认关闭、显式文件根、应用线程串行。此项不包含新后台作业、安全边界放宽、提交推送或发布。上文“未答”是各实验发生时的历史事实，当前授权状态以此接续为准。

该批准不等于EXEC-005/006实验完成或EXEC-007合同冻结，当前父票仍Writing、007未Ready、产品Verified=0。产品协议、格式及API尚未因收到授权而实际修改；原有未验项和实验范围不缩减。

## EXEC-006 完整 affine helper 与原资产 CryptoAsset 有限对照闭环

新增隔离`offline/affine-crypto-20261010-b91c`，共同fixture SHA047F3D5672C70419DA9A282CE20A49555BCCE7B94492BB69DA33267DEAE775B8。两平面局部网格/对象/材质不变，两个完整矩阵含剪切及负行列式，det=.96875/-1.03125。固定160×96、CPU2、16spp、seed7、BOX1及33 FLOAT32/NONE scanline EXR。Native直接传M/原asset_name；旧Blender链identity helper→真实对象，新链identity原资产root→identity helper→真实对象。两链M均只置于真实对象matrix_parent_inverse、child basis=I，不烘焙网格、不重命名真实对象、不修改EXR metadata。

run `20261010-210937659-a0fa0db5`的Ast、两完整构造预检、PrepareNative、Native、旧Blender、新Blender七阶段全exit0/no timeout，只有三次实际渲染，约.21/1.00/.98秒含模块采集，不用于性能选型。冻结13源码、3程序及256 DLL/PYD；Native实际34/Blender每轮130加载模块，moduleReadErrors=0，必需OpenImageIO.dll/python313.dll精确path/hash绑定。系统模块及精确有效微软签名Defender模块另列，25ms轮询不证明瞬态加载穷尽。

首run `20261010-210519912-3e1e3299`仅预检、未渲染；来源门发现Windows Defender MpOav.dll后停止。核验有效Microsoft签名及精确SHA F8F28A74DFB504E85B87CBC156855C98784D4035916A304CF20D86E20769B977，仅单列该路径，不放行ProgramData目录、不改Defender。第二run原Verify因python/Lib与python/lib的字符串索引KeyError退出1；只改Windows Path键语义，保留原source-lock/失败Verify/13前像。successor `verification-resume-1791637944577`审计exit0/no timeout/约3.2968秒，无新渲染或旧证据覆写。

三路EXR实际33 FLOAT32通道；内部左2498/右1957像素，HDR/AOV常量误差0；Native对新Blender九data/六coverage差0，旧对新几何、data及按真实对象映射coverage也差0。Blender相对输入full M最大5.960464483090178e-9，world/bounds最大3.5762786843029915e-8，低于预冻abs2e-6。Native及新Blender恢复AssetLeft=`8f675c92`/AssetRight=`2d7fcf84`真实manifest及weighted ID位模式。旧链先按真实helper名通过全部旧门，再准确拒绝`CryptoAsset: expected fixture names absent`；不平均ID、不修补manifest。Blender额外Group4World条目保留，不声称完整manifest相同。

未参与实现Sol只读.NET独立解码三原EXR并重算上述ID/coverage/常量/几何；核验86归档、13原source、25输入/输出记录及85个获准精确程序/实际依赖文件hash，无本有限范围阻断项。未执行Python/build/render；请求Sol6.1/max普通，provider最终路由未独立核验，不声称fast。

source-lock SHA371B600C0556214946B98E789C1EB585E101F605B2CEDE7FB14E24767DEDB2FC；原失败Verify SHAF45BA11BDB2C096DF2B8A567B22D9B9C48CBFF2D2A8D4F4F7B8EE5657DD4ACFD；successor输入SHA5480F08CAB91FC557014B893F440541261061B92518C29294C7B6A58DBB29C34；新comparison SHACA31CB6DB27C4F98E8A5AD9B04212835A543F2FA607AADC646C5DE7E389DE099。86文件archive-receipt SHA6B8DC78FDEDE506CFC056001E9280D43FE19368A6E01BE6B85F436D5DA2CE19D；原图/失败/前像全保留E盘，约8.9MiB，不含共享后端。

仅关闭冻结双对象affine/资产映射子项，不认证一般affine、法线、真实实例、所有坐标材质、复杂rank/跨时间Crypto、取消/崩溃/恢复或产品作者链。005/006仍Writing、007未Ready、产品Verified=0；AUTH-DATA已专项批准但不跳过依赖。不改产品/上游/系统配置，不提交推送或发布。追加后的历史前缀、UTF8/PS parser及隐藏Git差异检查另留真实回执。

追加后records-check-1791638745814实际PASS：七观察回执、13source/256依赖、86归档、14作者文本UTF8无BOM/LF/PS parser及三文档旧69376/56210/718520bytes完整前缀。首Finalize误将原机器JSON回执CRLF作为源码漂移，定向缩到作者文本/fixture的LF检查，原JSON及全部哈希门不变，失败保留archive-finalization-failure.json。授权复核authorization-check-1791638745853.json和隐藏Git diff --check实际通过，既有AGENTS行尾warning保留，不做转码。

## EXEC-006 真实实例、原生曲线及均匀吸收体积有限闭环

新增隔离包 `offline/group2-primitives-20261010-f27c`；唯一实际run为`20261010-214413230-5779861f`，160×96/32spp/seed7/CPU2/BOX1，透明黑环境及精确四Combined FLOAT32/NONE通道。Ast、Configure、Build、五Prepare、十完整构造preflight和十渲染，共28原阶段exit0/no timeout；Configure/Build约6.2762/21.1195秒，渲染进程约.1475—1.0135秒，含模块采样，不作选型帧时或冷构建数据。

Native两实例共享同一真实Mesh，Blender作者Mesh与两个depsgraph数据身份一致；只观察host Scene/session.start前及作者/depsgraph，不认证device/BVH复用。曲线为Native Hair/CURVE_THICK_LINEAR与Blender Curves/POLY，保留六点/四段/逐点radius，未烘焙Mesh。体积是真实闭合边界和Volume Absorption节点，三density为0/.75/1.5，后方独立Emission，不以表面颜色替代传输。

20几何观察最大误差5.006790160244634e-8，过预冻2e-6；十图尺寸/实际FLOAT32-NONE/有限/alpha[0,1]/HDR>1全部通过，HDR peak均2。instance两端内部4500/curve307像素的Emission误差0；各有一个边界差异像素，R/A最大差.0625/.03125，跨端只报告，不新增画质门。volume0四通道完全相同；volume075/150的R最大差2.5033950805664062e-6/3.933906555175781e-6，A完全相同。固定半开ROI[77,45,83,51)的三RGB均值逐通道严格下降，最小相邻降幅.10947225656774306，大于预冻.02。

原Verify exit1仅为验证器误把冻结设计的唯一Build产物要求在run内，未计像素；原五实现、manifest/失败回执/comparison/failure/全部原图保留。audit-only修订仅为精确Build程序、12参数、精确输出及run归档同字节开例外，不放行目录或其他产物。独审固定三修订后root执行，VerifyRevision exit0/no timeout/.7437072秒/noNewRendering=true，未重复渲染。

未参与实现Sol亲读实际runner `07A22A3F...`并核28真实阶段，独立只读.NET解码十EXR/614400值、复算ROI/内部常量/全部RGBA指标，和报告数值差0，无有限范围新增P1/P2。28回执/56唯一I/O零漂移、316修订pins匹配；20后端阶段1636次模块观察零读取错误，不认证瞬态穷尽。旧前审runner `30F985EC...` 无本包字节，历史精确差异缺口保留，不冒用旧版背书。模型请求Sol6.1/max普通被接受，provider最终路由未独立核验，无fast声明。

原manifest SHA95CE0BE9788319F1CB6E7511F1826CB0136B871E8C01F460F18C4D885A9A2D17；成功comparison SHA9ABC746E2D5CD4CA3928DCE6D85E892A63975E0A4B7E11CDA3300E6199A6E7AA；修订回执SHADEAB6D7F0EFD7D94EF272264BC537CC3E6B70573689E1E3BBDF1D8DEE4EA60CD。174文件归档SHAE6AAA697BC2E0F689BFE78564F5832E5A6DB03F4FDE9BAFF07A2C728B023B17E，含28原成功+1修订成功、原失败、19source与三文档前像；约17.2MiB，不含共享后端。

仅隔离上游app/CMakeLists.txt新增三行target include，旧bytes在包内upstream-app-CMakeLists.before.txt；原/新SHA021B2109.../FB4CEFEA...。旧三程序及Release库受保护，无产品/系统/用户窗口/Git写入或发布改动。停用本包runner即可停实验；确认后续无重叠后可Copy-Item定向恢复唯一CMake前像。正式报告可从run/documents-before定向恢复；日志只追加撤回记录，不删除历史。

仅关闭固定实例/曲线/均匀吸收输入，不认证一般affine、Hair BSDF/VDB、所有曲线/体积、完整Crypto/故障恢复、完整E-RT/E-OFF、预算/发行或产品作者链。005/006 Writing、007未Ready、产品Verified=0，AUTH-DATA已批准不跳依赖。三正式文档仅追加，追加后Finalize/隐藏差异检查随后实跑，不预记通过。

追加后Finalize已实际exit0/PASS，records-check-1791641144176/audit.json核29成功回执、1原失败、10渲染、174档案、19source、14保护文件及15作者UTF8无BOM/LF文本/PS parser；三正式文档旧73561/58786/722739bytes完整前缀保持。隐藏Git diff --check退出0，仅既有AGENTS行尾warning；逐文件no-index差异确认仅末尾新增，其退出1表示存在预期差异，不是测试失败。此前目录级stat误比较整个docs，立即收窄为三个精确文件，未写入或改动其余文件。

## EXEC-006 真实双层 EXR、有限 Crypto rank 与 Multiply 闭环

隔离包 `offline/group4-rank-layers-20261010-18ebff53` 的成功 run `20261010-223821944-a56e447c`：Ast、Configure、Build、三 Prepare、六完整构造预检、六双层导出及 Verify 共十九阶段实际 exit0/no timeout；六导出包含十二次真实 View/Session 渲染。64×48、CPU2、1024spp、seed7、BOX1；八个真实窄 quad，LayerAll 八对象/LayerHead 前四，两冻结静态状态仅宽度顺序不同。运行时长含启动及模块采集，不作后端性能认证。

六原 EXR 均 FLOAT32/NONE，levels4 为66通道、levels16为210通道。高深度两端三族均恢复 All8/Head4，全部像素覆盖对 alpha 最大误差0；低深度 All 中心只恢复01/04/05/07，八名完整性准确拒绝而总覆盖仍1。缺名与总覆盖是不同结论，不推断未观察的丢失权重或补偿机制。Head 固定 ROI 均值0.25→0.750244140625，变化0.500244140625大于事前0.3门；All HDR红2、Color AOV红-1、data蓝4和冻结线性常量误差0。

四张真实双层18通道 Multiply 的73,728个RGB值全部匹配 binary64乘后一次FLOAT32舍入，All乘积红-0.25；原AOV及coverage hint搬运零差异。Crypto不参与算术；跨状态按身份核36组位模式/端保持稳定，不平均ID、不按rank顺序配身份。跨端数据/coverage一致只报告，不外推一般画质或时间域身份正确性。

首run `20261010-223426165-7433f6c9` 在Native完整构造preflight退出1，零渲染：Scene已建立默认Combined，实验错误重复建立。仅三行改为检查并复用默认pass，独审修正后一次重试；原失败43文件、21旧source快照、旧exe及五回执全保留。旧source/旧exe仅以两个精确保留字节替身核验，不放行目录漂移。此前静态审查漏检初始化链的事实保留。

未参与实施Sol只读.NET独立解码六原EXR/四Multiply、复算全部coverage/ROI/常量/身份位模式及1,193项报告，十九成功回执、21source及快照、256依赖、15保护文件和构建exe/run副本同字节绑定通过；本有限范围无新增可操作P1/P2。未运行Python/build/render；5ms模块轮询不证明瞬态穷尽，未新调用Authenticode。模型请求Sol6.1/max普通已接受，provider最终路由未独立核验，不声称fast。

comparison SHA256 `75A8F69CB038C98D4FAA15DF7CA43CE7017F3F4F510C64B4C649808B7A29EE08`；manifest `D86C543A230868116817A2716A04E482A1FA9B44DAAC52A71EF11BA715710AE4`；Verify `BE2B1C9DB508689BCCAB71717E5255F20136FE5FDC3ADAF9033621DD372074F0`。161文件archive receipt `C699DB4CCD9D90C695D22D947521C751F1EFE4B9E19CD8BFEEE9597657CD239B` 含首失败与三正式文档前像。包约40.8MiB，不含共享后端，保留E盘不自动清理；隔离app/CMakeLists仅三行接入，前像在包内upstream-app-CMakeLists.before.txt。

仅关闭两冻结静态输入的有限双层/rank/identity/合成子项，取消/进程丢失/恢复、完整E-RT/E-OFF、资源及发行未验。005/006仍Writing、007未Ready、产品Verified=0；AUTH-DATA已批准不跳依赖。无产品、系统、用户窗口、Git提交推送或发布变化。停用runner即可停实验；确认无后续重叠后可定向Copy-Item恢复CMake前像，报告可从run/documents-before恢复，两日志只追加撤回。正式追加后Finalize/历史前缀/UTF8/PS parser/隐藏差异检查随后实跑，不预记通过。

追加后Finalize实际exit0/PASS，records-check-1791644291727/audit.json核十九回执、十二真实渲染、161归档、21source、15保护及14作者UTF8无BOM/LF文本/PS parser；三文档旧77850/61492/727582bytes完整前缀保留。隐藏Git diff --check实际exit0（仅既有AGENTS行尾warning），三个精确文件no-index差异均仅末尾追加，exit1表示预期差异；没有宽目录比较或产品改动。

## EXEC-006 固定输入的取消、进程丢失与 fresh replay 闭环

隔离包 `offline/group4-lifecycle-20261010-4639c2a0` 的成功 run `20261010-233141329-17f35e51` 完成17阶段，没有超时：构造/编译/六完整渲染/Verify退出0，两协作取消退出20，两授权故障注入退出73。固定64×48、CPU2、16384spp、seed7、BOX1，全覆盖Emission的scene-linear RGBA为[2,.5,.25,1]；完整图abs2e-5、几何abs2e-6是事前门。

两端六完整图的全部RGBA常量误差0，实际正样本观察包含1和16384；四fresh replay相对各自baseline逐通道MAE/RMSE/max/unequal均0。构造最大误差2.384185793236071e-8。Native取消在sample1只调用一次Progress::set_cancel，flag=true/error=false/wait返回，真实partial.exr存在；wait后的rawFinalCurrentSample=0与最后正观察1分开保留。Blender一次自身SIGINT后真实render_write→render_cancel、无render_complete、返回CANCELLED/无异常，partial.exr实际不存在，不由事件推断文件。

两次loss分别绑定本任务自启PID42356/37284的原Process.SafeHandle，先实际观察sample1，核PID/启动时间/程序路径后执行Windows TerminateProcess(requestedExit73)。API返回true、wait返回、HasExited=true及实际exit73全部实录。丢失后的新进程同输入重做成功；这是fresh replay，不是检查点续算，故障终止不认证正常析构或发布清理。十个渲染进程身份不同；没有关闭用户进程或窗口。

四原失败全部零渲染/零EXR，保留原manifest、104个source快照及诊断：空OutputDirectory被无条件Join-Path；缺OutputNode头及quoted的ADL冲突；把Cryptomatte accurate质量控制误当输出pass；使用已废弃的scene.use_nodes。分别精确纠正路径条件、头/helper名、控制分类、真实use_compositing属性，未改图像门或渲染算法。三诊断的捕获异常exit0不计正式预检。Blender background的effectiveAutoTile=true是固定源码推导，不冒Session实读；作者false/tile2048实值另记。旧纯函数EXR读取器核SHA后从AST提取，不导入旧module/main，真实零渲染loader smoke通过。

未参与实施Sol只读.NET独立解码七原EXR（六full、一Native partial）的86,016个值，有限且目标误差0；四replay数值及float-bit差0，canonical RGBA bits SHA为EF336D38924A5640148BEEC548C6B12F870CB9306B7604FE9A8517FEF4C39EC2。12构造读回、17回执、26source及快照、256依赖、16保护对象和983实际模块绑定核验通过，moduleReadErrors均空；四失败确实零渲染。审查器的Windows路径大小写及EXR long-name版本假阴性已据原数据纠正，不修改原图；本有限范围没有未解决的真实问题。未运行Python/build/render，不证明瞬态加载穷尽或device/BVH实读。请求Sol6.1/max普通已接受，provider最终路由未独立核验，无fast声明。

comparison SHA256为716C72FDA75A56EF58B676BE7E8F9B23D3BC2C8D2080D72D0F0B9A0C2B67D955；manifest为03EF6E3FB25D737C12D223201952EA31395AFAE0960C05B5D3A3B71CFE1A7F7C。323文件归档及三正式文档前像已实际生成，archive-receipt SHA为64670513792E02D2113716FCBF60C87A689E306587B0AE5C7A528A33973E3623。包约42MiB，不含共享后端，保留E盘不自动删除。仅隔离app/CMakeLists三行include，有upstream-app-CMakeLists.before.txt前像；无产品、系统、Git提交推送或发布改动。

本次只关闭该固定样例的生命周期实验；不认证任意取消竞态、checkpoint、finalized intent/epoch、完整组四或产品。005/006仍Writing、007未Ready、产品Verified=0。AUTH-DATA及隔离故障注入已批准，不扩大为产品后台作业或安全放宽。停用runner即可停新操作；确认无后续重叠后可定向Copy-Item恢复隔离CMake前像，正式报告可从成功run/documents-before恢复，两日志只追加撤回。正式追加后的Finalize/前缀/编码/隐藏差异检查随后实跑，不预记通过。

追加后Finalize已实际exit0/PASS，records-check-1791647256852/audit.json核17回执、六完整图、两实际loss、323档案、26source快照、16保护及15作者UTF8无BOM/LF文本/PS parser；三文档旧81734/64065/731901bytes前缀精确保持。隐藏Git diff --check实际exit0，既有AGENTS行尾warning保留；三个精确文件no-index --numstat分别新增16/12/28行、删除0，exit1为预期差异。没有重渲染旧包、全量转码或产品改动。

## EXEC-006 组一与坐标样例的执行覆盖收口

此段更新此前“未关闭组一／组二”的口径：R13规定的MR、SSS、厚玻璃、节点组、真位移、UV、Generated及Object最小样例均已真实双端执行，可以形成有限成功／失败／未认证对照报告。候选画质和产品全项仍未认证；不能把后期Catmull-Clark、adaptive dicing、一般节点图、完整位移Normal或任意变形Generated追加为EXEC-006的新必测门，也不能将已跑但未认证误记为根本漏跑。

| 最小样例 | 实际覆盖 | 仍未认证 |
| --- | --- | --- |
| MR／SSS／闭合厚玻璃 | 原球阵两端成功、Blender法线顺序修正后成功 | RGB画质一致性；修正后RMSE0.004885207525329183、max0.11025884747505188，alpha差0 |
| 节点组／真位移 | true/bump/control六路成功；真实Blender组、Native同配方展开 | 一般图、位移后Normal；两端true改变191诊断轮廓像素，bump改变0 |
| UV／Generated／Object | 含shear及负det的六路成功、角域UV保留seam | 任意变形域；三路内部RGB最大差1.519918441772461e-6／3.039836883544922e-6／3.0994415283203125e-6，alpha差0 |

root只读核验包`offline/r13-off-coverage-20261010-96c713a2`的`evidence-1791647539653/audit.json`实际exit0，PASS-EXECUTION-COVERAGE-NOT-CANDIDATE，SHA256为3BBB0BD5667799DAAAE6ED3B5728D69975F74F46120B0D4244360B7B7EDA0FAB。29事前pins、60唯一文件、15成功回执及1原失败绑定通过；含三fixture、原XML、两清单、原/修正比较、15原EXR和坐标raw对应关系。原Native失败exit-1073740791、无EXR，旧程序字节后已被构建替代，本次不声称恢复其旧程序。原productAcceptance/fullGroup*Acceptance=false保持，不加后验画质线、不重算全部像素、不重渲染、不改旧报告。

未参与本次记录写入的Sol只读定位核12节点/坐标回执、common成功/失败及EXR/raw原绑定，已交还租约；root另以固定清单验证。均未新运行Python/build/render，历史源码及全部依赖未在本次重新认证。请求Sol6.1/max普通被接受，provider最终路由未独立核验，无fast声明。覆盖记录沿用仓库本地Markdown落点，明确条件和观察值，不创建云端Page。

E-OFF仍缺代表场景资源／预算校准和发行许可闭环；已有小图启动耗时/RSS与依赖hash不能替代。ocio-license.txt及AgX/Filmic LUT权利链仍未闭合，不据此认定无许可或声称可发行。E-RT规定场景及呈现流程仍未跑完。因此005/006继续Writing、007未Ready、产品Verified=0，而不是要求全部J3产品算法先完成。新包仅几十KiB证据和约0.8MiB文档前像，原图仍在原目录；无源代码/CMake/系统/进程/Git写入或发布变化。停用校验脚本即可撤回新操作，正式报告可从新evidence/documents-before恢复，两日志只追加撤回。追加后前缀/编码/差异检查随后实际运行。

追加后finalize-records实际exit0/PASS，evidence-1791647539653/finalized.json核60旧文件、15成功回执、8作者UTF8无BOM/LF文本及PS parser；三文档旧86197/67117/737607bytes完整前缀保持。隐藏Git diff --check exit0，仅已有AGENTS行尾warning；无新渲染、原图/旧报告/源码修改。

## EXEC-005 五共同资产的可见呈现、编辑和重建闭环

隔离包 `rt/ert-common-suite-20261011-b304ec72`，最终 run `20261011-003352451-d970f9e7`：五自建共同资产（球阵、normalTexture glTF、alpha MASK栅栏、多灯阴影场景、非恒定HDRI），1280×720，baseline/edit/rebuild各64完成帧；两端共1920帧及十个不重渲染覆盖状态。用户直接批准非抢焦点任务专用窗口，四十次真实Qt画布visible/exposed、paint增长、DwmFlush成功且本窗口非foreground，程序均自行退出。PNG是应用画布，不冒桌面像素截图；呈现为readback→CPU OCIO→QPainter，不是直接Filament swapchain。

五阶段实际exit0/no timeout：configure13228.6363ms、Native暖构建814.2086ms、Filament增量构建16612.3869ms，Native/Filament进程7419.7198/14566.1898ms。两端原glTF/外部资源/实际程序及OCIO模块身份绑定；每端十五次真实Standard/sRGB OCIO，raw/data旁路，overlay零重复变换。首节点X+=.35、首roughness=.8、三灯×1.5作用到后端且edit原图改变；两端各十次Context/Engine创建并最终销毁，旧资源销毁后按同一edited作者值重建。Native五组edit/rebuild原raw位相等；Filament五组不位相等，TAA历史区别保留，不加后验画质门。

初完整run `003001380-95eb8bcc` 五阶段成功且旧verifier误漏方向：Filament readPixels已翻成TopDown，共享转换再按BottomUp翻，实际垂直镜像。旧图/旧PASS不改；仅当前捕获端一次逐行规范化为冻结BottomUp，不改RGB/alpha。新增从真实glTF quad及冻结相机独算投影的方向门，旧Native authored/mirrored不符为0/22680，旧Filament22663/67准确拒绝。最终Native baseline/edit/rebuild为0/1/1对镜像22680/22743/22743，Filament67/69/69对22663/22737/22737，只验方向，不拿轮廓边缘误差认证画质。

实际RTX3050 Laptop／NVIDIA610.47、Native GL4.1 core／Qt6.8.3，RAM16487870464bytes。Native/Filament完成帧wall median/p95分别.16905/.3159ms及6.88485/8.5526ms，首帧1.5221/42.6754ms；是CPU提交+GPU等待，不是GPU timer。raw RGB最大13.569698/21.5625。各960匹配adapter/node0 WDDM样本，LOCAL CurrentUsage采样最大56561664/109768704bytes、NON_LOCAL15601664/21536768bytes，进程WorkingSet采样最大212496384/268910592bytes，不含子进程或瞬时峰值。仅校准，不认证产品预算、最低设备、冷编译或同等功能性能；逐场景首导入/IBL、edit-only成本仍缺。

保留全部真实失败：v1顶点表达式生成失败；v2每格九索引及原预检误PASS，v3六次明确Add并逐格核425点/2304索引；短命构建MainModule为空不猜结果；Draco长路径C1083用472文件/2901137bytes同字节E盘短副本且只调include顺序；漏Viewport.h/indirect_两处定向补充；Windows gltfPath=nullptr导致external bin加载失败只传真实原路径；上述镜像失败。未改变上游算法、系统或产品。

未参与实现的Sol只读.NET独立扫描95唯一FLOAT平面／317952000值全部有限，四十PNG解码9216000像素，前后hash稳定；独算十overlay平面复用、Native五data复用、十edit改变、1920帧、WDDM指标、作者值重建和原alpha方向，亲看最终原图及旧负样本，无本有限执行范围新增未解决阻断。未运行作者verifier/build/render或进程操作。Filament normal-gltf原字节有零alpha非零RGB559/534/559像素，未有独立发光参考，关联alpha认证保持false；不清零冒修复，也不因名义role签完整语义。模型请求Sol6.1/max普通被接受，provider最终路由未独立核验，无fast声明。

实际verifier为PASS-FROZEN-EXECUTION-NOT-CANDIDATE；manifest SHA256 `2A12E2D7F32C328356C7B1E1364478597769E273FD8E3836F79DDD357380A97B`、verification `2C48EF2A3256939D6CFA3565A41083286AC657FE4BBDDF867F6DED8439FE2442`、summary `B85599CC294F282D64AD751B114A7FDEBC0AD2417354942A60A61C7DA8660EA0`。实际Freeze保存588文件／3376318585bytes，含两最终exe副本、19作者源码快照、原失败/原图及三文档前像；archive-receipt SHA `58C64784C5EDFFD3E3FC2D5996F5141AA1D177D14FF76C09AC59AC635054A9DF`。作者源码UTF8无BOM/LF，追加后Finalize/历史前缀/隐藏差异另实跑，不预记通过。

Native缺阴影/TAA/DOF/完整IBL/物理曝光明确Unsupported，Filament data MRT未实现；关联alpha、同物理亮度和高级组合不认证。这组只收最小规定流程的执行切片；代表预算、最低硬件、首更新成本、发行许可和最终候选报告仍待补，ocio-license及AgX/Filmic权利链未闭合。005/006仍Writing、007未Ready、产品Verified=0；AUTH-DATA不跳依赖，无产品后台作业/安全放宽/提交推送/发布。

包约3.15GiB留E盘，不自动删除。停止runner即可停新执行，全部窗口已结束无需关窗。确认无后续接入口重叠后，可 `Copy-Item -LiteralPath 'E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/rt/ert-common-suite-20261011-b304ec72/sources-before/source-build.CMakeLists.before.txt' -Destination 'E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/rt/filament-probe/source-build/CMakeLists.txt'` 定向恢复唯一旧隔离接入口；短Draco副本不自动删除。报告可从成功run/documents-before定向恢复，两日志仅追加撤回。

追加后Finalize实际exit0/PASS，成功run/finalized.json核588归档、19作者UTF8无BOM/LF文本及PS parser；三文档旧89495/69007/741071bytes完整前缀不变。隐藏Git diff --check exit0，三指定文件no-index新增20/13/26行且删除0，exit1为预期差异；既有行尾warning保留。新只读合同reader的parser/严格UTF8-LF也实际通过，SHA CB2EA4A91B74C0E679D1796B698EFEB2447D217A7C1FFF01A9ECFB1BDF2CD482；没有新渲染或正式历史改写。

## EXEC-005 首导入、编辑及重建阶段成本补测

新隔离包`rt/ert-stage-cost-20261011-71ca8f04`，成功run`20261011-114432525-a3ac4196`：同五自建资产/1280×720/相机/灯/OCIO/TAA/DOF/三64帧，实际新渲染1920完成帧、十overlay及四十非前台真实Qt画布；其后的verify/summary/Freeze/Finalize和独审不再渲染。五阶段exit0/no timeout、自身退出，configure/build-native/build-filament/run-native/run-filament耗时15149.7122/1364.6998/12994.8811/8646.3624/15988.7807ms，是暖/增量构建；两新target首次编译在保留失败run113938363，16771.7351/23812.578ms，不是全部依赖冷编译。

Native glTF CPU解码0.5493—1.3643ms，共享HDR CPU解码0.1583ms、OCIO processor设置34.7ms；包含glFinish的初始化37.398—670.4066ms、重建36.2489—52.6784ms。CPU作者更新0.0043—0.0144ms不含下一完成帧uniform消费。Filament初始化/编辑明确API-return、gpuCompletionIncluded=false：首次总准备108.3743—631.4865ms、重建103.2745—117.8464ms，glTF加载2.7277—4.8418ms、HDRI预滤波提交35.838—238.7295ms、编辑提交0.0021—0.0616ms。两端语义及功能集合不同，不进行公平性能排名。

各960完成帧的wall median/p95为Native0.1921/1.0006ms、Filament6.85825/9.4301ms，最大4.9636/593.4886ms；全部十五首帧逐端保留，不能只用稳态p95掩盖首次准备。每端960匹配adapter WDDM样本，LOCAL采样最大56561664/109768704bytes、NON_LOCAL15601664/22061056bytes，进程WorkingSet采样最大259899392/365129728bytes。wall含CPU与GPU等待、WorkingSet不含子进程、WDDM不是瞬时峰值，不是整条用户操作到paint延迟、产品预算或最低设备认证。

首失败113845713 configure exit1是兄弟目录Qt/OCIO IMPORTED target不可见，只补固定find；次失败113938363构建成功但Native主逻辑前exit-1073741515（0xC0000135），无渲染且短命actualPath=null。已实查排除PATH大小写重复/斜杠假设；仅两个新target app-local部署四个固定Qt DLL后成功，八副本同源SHA匹配，Native四库/Filament三库真实观察，Filament OpenGL不冒实际消费。具体缺失DLL未独立定位，不宣称根因完全证明；旧失败/源码及旧成功包不改。

本包verifier实际exit0，原完整来源/平面有限/一次OCIO/作者投影方向/overlay复用/作者更新及新资源重建门通过。未参与实现的Sol独立亲读计时范围并重算各960帧median/p95/max，核五场景成本表、八部署SHA、23实际runtime副本及manifest绑定、262项归档字节数；本有限范围没有未解决阻断项，不重复旧包全平面扫描或调用作者脚本。客户端新spawn明确Sol6.1/max/none、普通；provider最终路由未独立核验，不声称fast。旧审查者因恢复配置缺可见证据未进入新包，本轮不用其结果顶替独审。

manifest SHA BA0BC374F6D1DC2090C594FFEEE2E246240AD4909E0D71EEF8EC4DA8681649D2，verification D72D597BC09C93E80F24CB1063F8BD8C0CC392695CA888EB59AC799F43A49F44，summary D3276FC0565BA2ABFFB63A8E66DAAC3915AF38181BCF54224B86CEC87A4150F3。Freeze实际exit0：262文件/1343267306bytes（约1.25GiB），两实际程序及E盘观察模块、15作者快照、两失败/原图和三文档前像；receipt SHA C03EB1110E31691D12DE5E6437FD919557E10BDAE291EE1AB43F5B325CBBD592。正式追加后Finalize/编码/前缀/隐藏差异随后实际执行，不预记通过。

005/006仍Writing、007未Ready、产品Verified=0；本次关闭的是首成本仪表缺口，不关闭端到端预算/最低硬件/发行或候选门。独立Luna只读便携license.md/licenses.json与实际config头，无点名配置/LUT的明确授权证据；ocio-license.txt指针仍未闭合，不等于判定无许可，官方固定来源继续取证。未新增产品/上游算法/系统配置/后台作业/安全边界/Git写操作/发布，不操控桌面或关闭用户窗口。

回滚：停用runner；确认后续无重叠后，`Copy-Item -LiteralPath 'E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/rt/ert-stage-cost-20261011-71ca8f04/wrapper-before.txt' -Destination 'E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/rt/filament-probe/source-build/CMakeLists.txt'`定向恢复唯一隔离接入口；报告可从成功run/documents-before恢复，两日志只追加撤回。E盘约83GiB余量，旧包3.15GiB与新包1.25GiB保留，不自动删除。

追加后Finalize实际完成，finalized.json为PASS，核262归档/15作者UTF8无BOM-LF/PS parser及三文档旧95320/72762/746857bytes完整前缀。首次外层shell退出1源自本轮错误地检查PS脚本未设置的旧LASTEXITCODE，并非Finalize失败；保留成功回执，不重跑或覆盖，独立读取确认。随后隐藏diff --check实际exit0；三指定文件no-index新增18/11/26行、删除0，exit1表示预期差异，既有行尾warning不改。独审实际核23runtime副本及归档清单数量/字节数，并未宣称独立重核全部262文件hash；root Finalize完成全清单校验。

## EXEC-005/006 发行来源证据补充

详见`animation-rendering-j0-distribution-evidence.md`。新包`distribution/license-evidence-20261011-4d90e729`实际核七本机固定SHA、源码config原字节Git blob、便携版本仅内存CRLF归一比较、三官方GitHub只读回执；verify-local-evidence.ps1退出0/PASS-SOURCE-EVIDENCE-NOT-DISTRIBUTION。固定色彩目录没有ocio-license.txt且原路径404；同时固定Blender根COPYING存在GNU GPL总声明，不能据独立文件缺失排除GPL或判资源无许可。

库条款、资产来源和产品发行义务不是同一证明。资产到条款/例外的映射、完整依赖通知/源码义务及产品许可证决策仍未闭合，不签发行通过、不选产品许可证、不改config/LUT。005/006仍Writing、007未Ready、产品Verified=0，无后端执行或产品改动。三历史文件只追加；新报告与两核验脚本/回执留E盘，check-records及隐藏diff随后实际执行，不预记通过。

check-records实际exit0/PASS：四作者文本UTF8无BOM/LF及两PS parser、固定来源不漂移、三文档旧100379/76402/752529bytes完整前缀。隐藏diff --check exit0；三指定no-index新增6/6/20行、删除0（exit1为正常差异）；仅既有行尾warning。不重跑或覆盖此回执，不签发行认证。

## EXEC-005/006 最小Standard候选与720p离线资源校准闭环

自有最小Standard包 `rt/ocio-minimal-standard-20261011-efa4923b` 成功run `1791692756166`：CPU/GPU/verifier实际exit0、程序身份及28来源前后匹配；184368个RGB通道通过预冻abs2e-5+rel2e-6，CPU对旧Standard差0、GPU对独立连续moncurve最大3.0133050987757315e-7。alpha位保持、Native data字节旁路、两真实GPU错误控制拒绝；实际GL4.1/RTX3050/Qt6.8.3、11draw/glError0，无Look/LUT/ICC及外部文件。仅Standard有限候选通过，不认证AgX/Filmic、完整E-RT、关联alpha质量或发行。旧run `1791692260714` GPU loader失败保留；仅三Qt DLL同字节部署，唯一缺DLL未证明，未重编译/重渲染后端场景。配置SHA580836D65890F3226EA7E38B67F0A1F55C4125149B3F311E6E3E7E98B8999927，verification SHA26A6AE8E52FF839640A96D8C4A8DFE37009382CA69C7925F4B9F8DDFC31094DE。

离线包 `offline/resource-calibration-20261011-8b712ef4` 成功session `1791693230615-f064191f`：四阶段实际exit0/no timeout、身份匹配且自行退出；仅两次1280×720/32spp/seed19/CPU2/BLACKMAN_HARRIS1.5真实渲染，adaptive/denoise及原bounce不变，construct/reader零渲染。两EXR真实RGBA FLOAT32，各3686400值有限，内/外alpha像素均379416/536162；RGB RMSE.004416411502991587/max.1641044318675995，alpha仅10值不同/max.03125，只报告差异不新增画质线。

Native/Blender启动到观察退出wall11.3277859/9.5530621秒、CPU21.84375/17.03125秒；direct-child采样WorkingSet59494400/205508608bytes、PrivateMemory50737152/399876096bytes，readerWorkingSet284041216bytes。Private是private committed，不能冒shared physical；名义200ms实际约210–274ms，不认证瞬时峰值、完整树、shared/GPU/VRAM或公平排名。session摘要前74141524bytes含33250816bytes部署，不冒正式输出规模/最低硬件/产品预算。

原两离线失败全保留：首Name/name大小写覆盖导致stage=TMPDIR及Native PATH分支跳过；只改单循环变量并加AST回归，旧1/新0实际通过。次阶段名/身份正确仍loader0xC0000135、零渲染。改用隐藏dumpbin十二PE导入取证后，将原exe+11非系统DLL定向同字节部署至新native-bin，独审准入后成功；未调系统PATH、复制系统DLL、改原exe/上游/质量/门限，唯一loader根因仍未完全证明。

未参与实施Sol独立ZIP解码两原EXR（各45块/720行）并逐位对raw差0；独算全部RGB/alpha/资源峰值/磁盘与报告匹配。40保护前像/后像/当前一致，12个实际模块均观察从新native-bin加载，四PID已不存在，无作者verifier、重渲染或进程控制。B四作者版本与Qt实际路径复核通过。请求Sol6.1/max普通被接受，provider最终路由不可独立核验，无fast声明；Luna另复核旧五资产同帧TAA+DOF→raw→一次CPU OCIO→Qt画布已经真实执行，撤回“必须先实现Filament data MRT”的新增门建议，Unsupported不是未执行。

root Freeze实际exit0：A196文件79850056bytes、receipt SHA2D96D2AC4244EEFFDAE7651E381BE5CE53D1F628D8A91A09F3BA36A054C44450；B104文件46147973bytes、receipt SHAB6B8806CD189BEF623E6A3177224EE59BB1B3D0CCFFDBAF059E5CCEE536E83A4。源码/失败/原图/文档前像留E盘，不自动清理；RESULTS详列数字及定向撤回。三正式文件仅追加，记录前缀/编码/parser/归档/隐藏diff随后实跑，不预记通过。本组不改产品；005/006父票待最终报告审查，007未Ready、产品Verified=0。

追加后check-calibration-records实际exit0/PASS，calibration-records-1791693628690核两包300文件、十九作者/记录UTF8无BOM-LF/parser及三文档旧101604/77538/755422bytes前缀不变。隐藏diff --check实际exit0，仅既有AGENTS行尾warning；没有重渲染、转码或源漂移。

校正：上一条完成记录中的旧前缀字节数转录有误。原回执 calibration-records-1791693628690/result.json 的实际三项为101700/77578/755407bytes（实验报告／执行日志／progress）；前缀保持和PASS结论不变，原回执未改。后续数字直接从JSON提取，避免手工补精度。
