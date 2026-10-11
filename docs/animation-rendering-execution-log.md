# Mini3D 动画与渲染执行台账

## 2026-10-09 开工基线

用户指示为“开始执行方案”，实施对象为J1.1主册及同目录J1.0技术合同。随后用户明确允许本次专家审计使用gpt-6.1-sol/max普通模式；此例外仅用于本次执行，不修改AGENTS中的长期fast约定。

源码基线为 `5f5c82ed7a9f361e263d4abe895bbf99b061e3e7`。原有未提交文件为AGENTS.md、docs/agent-collaboration.md、progress.md，以及J1.1主册、Word、任务索引和J1.0附件四个新增文档。开工hash清单与原日志637185字节备份保存于 `E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/baseline.json` 和同目录progress.baseline；派工不覆盖上述原有内容。

技术附件SHA256为 `BBB4E6AD7186C2B1E1F5B9AE1EB851B78CA4052DE39C0AF008571D208364D73A`，已核对。任务定义源hash与正文一致，36张任务的初始定义仍为Planned；本台账记录实际运行状态，任务JSON保持定义索引用途。

实际执行约束为主代理加最多两个子代理，当前平台共四个名额。域负责人兼任实施，独审与专家轮换；当前使用逻辑三级，子代理不再派生代理。派工显式请求model和reasoning_effort，工具接受配置不能证明provider最终路由；本机没有独立provider核验回执，该项明确保持未验证。专家普通模式例外已有用户直接回复。

本次启动覆盖合同设计、产品开发和相关隔离验证。后端依赖版本、worker边界及需要核对的授权，在形成具体施工包后登记；源文件中的“已批准”和历史授权文字不作为新授权。Git提交推送、发布和全局配置改动没有本次新指示，不列入执行任务。

## 工具和可复现参数

- PowerShell：`C:/Users/SL/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe`，实际7.6.5。
- CMake/CTest：`E:/cmake-3.31.0-rc1-windows-x86_64/bin/`，已检查可执行文件存在。
- Qt：`E:/Qt5.15/6.8.3/msvc2022_64`，Qt6Config.cmake存在。
- vcpkg：`E:/vcpkg/scripts/buildsystems/vcpkg.cmake`，triplet为x64-windows。
- MSBuild：`E:/VS_2019/MSBuild/Current/Bin/MSBuild.exe`存在；具体MSVC工具集由隔离配置时记录，不从目录名推断版本。
- 隔离根：`E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba`，本地E盘、祖先无reparse，文件创建成功，开工可用约89GiB。可控子进程TEMP/TMP/TMPDIR指向本任务temp。
- 工程配置形状：上述cmake，`-S E:/Mini3D -B <本票独占build> -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=E:/Qt5.15/6.8.3/msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows`。尚未运行配置；普通主体只显式构建mini3d_editor，测试目标独立构建。
- Git查询统一经已核验隐藏运行器，避免新建控制台窗口；不运行未改造的直接Git批量验收脚本。

## 文件写入所有权

| 任务 | 唯一owner | 精确允许文件 | 其他范围 |
| --- | --- | --- | --- |
| EXEC-000及公共记录 | L1 | docs/animation-rendering-execution-log.md、progress.md | 其他文件只读 |
| EXEC-001 | L1 | docs/animation-rendering-scope.json、docs/animation-rendering-scope.md | 完整源清单、EXT子项和验收映射；不改产品 |
| EXEC-002 | 动画Leader | docs/animation-rendering-j0-animation-contract.md | 代码及原方案只读 |
| EXEC-003 | 渲染Leader | docs/animation-rendering-j0-render-contract.md | 代码及原方案只读 |
| EXEC-004 | L1 | docs/animation-rendering-j0-joint-contract.md | 依赖两份草案完成；不改产品协议 |

本表是本轮文件租约；结束或移交前核对后台命令退出。新增文件或跨范围修改先更新租约，不授予目录通配写权限。progress只由L1追加。

## 状态追加

### EXEC-000 开工核对完成

- 变更：Planned → Ready → Writing → Integrated → Verified → Completed。
- Owner：L1；源码身份：上述HEAD及baseline.json中七个已有未提交文件hash。
- 允许文件：docs/animation-rendering-execution-log.md、progress.md。
- 契约：J1.1定义；尚未冻结J-CON-01至08。
- 授权引用：本轮“开始执行方案”；本轮专家普通模式明确回复。
- 证据：baseline.json；source-blocks.json的附件hash；本轮Git status及HEAD只读输出。
- 结果：基线及未提交差异已固定、原稿已校验、两个子代理上限已确认；工具入口已定位，构建尚未执行。
- 限制：provider最终模型路由未独立核验；长期AGENTS的fast约定不改变。后端/许可/设备及具体高风险施工包仍由后续票核对。

### EXEC-001 需求清单开始

- 变更：Planned → Ready → Writing；Owner：L1。
- 源码身份：同EXEC-000；契约：原稿hash及J1.1完整范围。
- 允许文件：docs/animation-rendering-scope.json、docs/animation-rendering-scope.md；progress及本台账由L1追加。
- 授权引用：本轮开始执行；证据：原稿A20、A23至33、附录RA、工作流和J-IT表。
- 阻塞：无；未拆完整官方目录叶子的扩展项须标记PendingScope，不计产品通过。

### EXEC-002及EXEC-003 合同设计准备

- 变更：Planned → Ready；Owner：动画Leader及渲染Leader，分别独占上表两份草案。
- 源码身份：同EXEC-000；契约：J1.1及技术附件，合同未冻结。
- 授权引用：本轮开始执行；模型请求：gpt-6.1-sol/max，fork_turns=none。
- 验收：各票要求的字段、例子、反例、消费者与当前源码证据完整；两份草案会签后交EXEC-004。
- 阻塞：无已知配置拒绝；请求与工具回执将在启动后追加，最终provider路由仍标记未核验。

### EXEC-002及EXEC-003 启动回执

- 变更：Ready → Writing；分别为动画Leader与渲染Leader。
- 实际请求：两个代理均为`gpt-6.1-sol`、`max`、`fork_turns=none`，工具接受启动；独占各自合同文档，不能写公共源码、台账或progress。
- 配额：仅两个子代理运行，无更深分派；provider最终路由没有独立回执，保持未验证。
- 当前会签：MaterialID与MaterialSlotID分开；roughness默认1、域[0,1]；Sun默认1 W/m²、有限非负；旧灯保留legacy语义；J1时间域[1,100000]。

### EXEC-001 原稿范围登记完成

- 变更：Writing → InReview → Integrated → Verified → Completed；Owner：L1。
- 源码身份：同开工HEAD，未修改产品；合同：J1.1与已核验J1.0原稿。
- 文件：docs/animation-rendering-scope.json、docs/animation-rendering-scope.md，以及本台账和progress末尾记录。
- 证据：隔离目录build-scope.py、audit-scope.ps1、scope-audit.json。独立审计直接读取DOCX ZIP/XML，逐行核对285族必选文本、全部验收记录与EXT来源，不依赖生成器的提取JSON。
- 结果：动画149族、渲染136族/核心97、EXT18工作包、动画10工作流、渲染8工作流/20场景、12联合测试、33动画数值验收、14补齐与10新增包均通过来源核对；原日志637185字节前缀未变。
- 边界：本票只完成原稿清单录入。产品Verified=0；完整官方原子leaf及运行时目录快照仍PendingScope。14个后续渲染族保留分母与空路由，其他路由只是候选，不授予租约或关闭整族。

### AUTH-DEPENDENCY及原型worker 隔离实验授权

- 用户直接回复“批准上述隔离实验”，覆盖官方Blender5.2.2、Filament1.77.3及对应Cycles/OCIO源码或依赖的E盘隔离下载、解压、构建，本任务专用隐藏渲染测试进程。
- 约束：仅自建测试资产、GPU串行；不安装到系统、不运行资产脚本、不改全局配置、不发布。
- 覆盖EXEC-005/006实验准备及执行，不自动批准产品格式/API演进、长期作业并发或Python执行开放。
- 官方页面已核对精确版本可用；本机原Blender5.1.1不作为5.2.2正式证据，下载校验、源码commit和执行版本仍待本机验证。

### EXEC-002 动画合同草案完成

- 变更：Writing → InReview → Integrated → Verified → Completed；Owner：动画Leader，L1复核交付物。
- 文件租约：docs/animation-rendering-j0-animation-contract.md已移交L1；无后台命令。
- 最终hash：`2BEFA7A6D8D6FDA49B3A010DF3B82D2F1C8AB96020BD11764109AE3327AF60A6`；227行、30361字节。
- 证据：L1读取全文并重新运行anim/validate-animation-contract.ps1，32必需项、49处源码引用、原稿hash、UTF-8/LF和三帧数学期望通过；双方确认地址、默认值、SourceIdentity与结果语义。
- 本票只完成J-CON-01/02/04设计草案，正式冻结及专家门归EXEC-004/007，产品能力和产品测试不计通过。

### EXEC-003 渲染合同草案完成

- 变更：Writing → InReview → Integrated → Verified → Completed；Owner：渲染Leader，L1复核交付物。
- 文件租约：docs/animation-rendering-j0-render-contract.md已移交L1；无后台命令。
- 最终hash：`5E207746FC837FB9A522B3077E3142960553FB76D838D8762CD1C0C1D0D239F4`；308行。
- 证据：L1读取全文并重新运行render/Validate-RenderContract.ps1，29必需项、33原稿引用、19源码引用及UTF-8/LF/Markdown结构通过。与动画地址、单位和身份候选一致。
- 本票只完成J-CON-01/03/07设计草案；后端、OCIO资源、真实像素及输出仍需实验/实现票证据。

### EXEC-004 联合合同开始审查

- 变更：Planned → Ready → Writing → InReview；Owner：L1。
- 精确文件：docs/animation-rendering-j0-joint-contract.md；其他正式共享记录由L1仅追加。
- 契约：J0-JOINT-DRAFT-1及两份已交接草案，补齐J-CON-05/06/08；版本5/API0.3.0仍为候选。
- 授权：本轮方案施工和文档设计；产品核心/迁移/长期作业的最终差异未在本票施工。
- 独审：新代理j0_contract_expert_review请求gpt-6.1-sol/max，fork_turns=none；本次普通专家模式例外已获用户直接批准。工具接受，provider最终路由未核验。
- 配额：独审与只读后端来源调研同时运行，共两个子代理，三级为逻辑关系；唯一GPU/重构建由L1控制。

### EXEC-005/006 实验准备与来源定位

- 变更：Planned → Ready（仅已授权原型范围）；依赖EXEC-003交付完成，正式GPU实验尚未开始。
- Owner：L1统一资源排期，后续原型实施另列精确文件；本轮来源查询脚本和下载只写E盘隔离根。
- 官方来源：Blender5.2.2 ZIP与SHA256；Filament1.77.3官方expanded_assets。API共享IP限流失败后改用官方HTML来源，不读取用户凭据、不重复限流请求。
- Blender本轮下载在专用目录运行，便携包尚未完成本机hash/版本核验；Cycles/OCIO固定源码由只读Luna调研，未启动第三子代理。
- 结果边界：Ready不等于Completed，包可用不等于实验通过；必须完成原稿R13规定组合，未执行部分仍保留。

### EXEC-004 联合草案闭环

- 变更：InReview → Integrated → Verified → Completed；Owner：L1。
- 文件：docs/animation-rendering-j0-joint-contract.md；J0-JOINT-DRAFT-1，16167字节，SHA256为`E7F1CB976C37E3DBDE7F379C336BE6B5AA14484563381D323B40837170D8880A`。
- 证据：validate-joint-contract.ps1输出PASS，39项合同/预算/权限/失败关键项、严格UTF-8/LF/Markdown结构和来源hash通过。
- 独立专家结论：三份合同未发现需阻断文档集成的P1/P2；直接核对原DOCX关键段落/表格。原结论明确不能据此放行EXEC-007，也未运行产品/GPU/迁移/故障测试；请求Sol6.1/max普通模式，provider路由未独立核验。
- 草案完整但未冻结：实际SourceIdentity布局、schema、OCIO资源、后端转换、容差与资源上限仍由007前置实验和授权包关闭。

### EXEC-005 原生GL实验分支开工

- 变更：Ready → Writing（本票仅实验）；Owner：原渲染Leader接任原生GL分支，L1保留整体选型/记录。
- 唯一写入范围：E盘隔离根rt/native-gl内CMakeLists.txt、NativeGlProbe.cpp、run-native-probe.ps1、native.vert、native.frag、prepare-native-build.ps1及本分支独占build/log/output/temp；仓库产品文件只读。
- 模型：沿用已接受Sol6.1/max，普通实施；无进一步分派。当前只读Luna+该实施代理，共两个子代理。
- 资源租约：当前唯一重构建/GPU时段授予该分支，根代理只做下载/文本/来源查询；运行前/结束回报。隐藏测试Widget不能证明真实桌面present，这部分显式未验。
- 目标：实际Qt/GL4.1生命周期、FBO/HDR/alpha/data、PBR参数像素、覆盖层/修改/重建/固定尺寸；缺OCIO或TAA+DOF完整组合时保留NotReady，不关闭E-RT。

### EXEC-006 Blender静态基础探针

- 变更：Ready → Writing；Owner：L1；GPU未使用（Cycles CPU），过程在原生GL时段授予前已退出。
- 固定包：官方ZIP SHA256为3849D17A682CBA006075AAA3F3597ECB5C9C30EC31035B2E092C53E40679B535；实际--version为5.2.2 LTS、源码build hash d13f752e3b9c，与原稿匹配。
- 运行：run-hidden.ps1结构化CreateNoWindow；--background --factory-startup --disable-autoexec --threads 4 --python自建blender-hdr-probe.py；没有加载外部作品或资产脚本。
- 证据：offline/blender-hdr/result.json与linear-hdr.exr。64×64、FLOAT32 RGBA、16samples/seed19、CPU、无denoise；中心[4,2,1,1]、透明角[0,0,0,0]及全像素有限通过；Standard/sRGB/None实际可设，config SHA256为c39c242ccfbb6b667b804afa281652727148874bcd7ffbc01c777341bc597487。
- 未验：原稿四组完整E-OFF、独立Cycles C++、完整affine/材质坐标/实例/曲线/体积、motion、AOV/Crypto及取消/恢复仍未完成；本探针不关闭父票或计产品得分。

### EXEC-005/006 固定来源与依赖元数据准备

- 父票保持Writing；本追加只闭环来源准备，不关闭E-RT/E-OFF、不放行EXEC-007。产品源码未修改。
- scope JSON/Markdown将完整官方leaf/manual/runtime目录仍PendingScope与已核验的固定package/source证据分开，不再将两者混称未核验。分母、路由和产品状态不变。
- Cycles固定Blender源码archive SHA256：`9F66A3827DD25F7239D38D04AF183188C7ED3FE6C526698FC2E965B6B4CE3DF8`；来源是官方GitHub镜像的`d13f752e3b9c4f8c261cda552b1021f8bcc0382c`，官方Gitea archive的GET反bot失败后换同提交官方镜像，不绕过挑战。
- Windows库父gitlink由只读调研核验为`60d6e96b917568278d400a4024c98da0fb777338`；隐藏Git按官方`v5.2.2`取得浅层元数据并再次验证HEAD相同。尚未checkout或下载真实LFS二进制，不调用make update或lfs install，不修改全局配置。
- 便携OCIO配置实际SHA256：`C39C242CCFBB6B667B804AFA281652727148874BCD7FFBC01C777341BC597487`；68686字节含1550个CR，仅在内存规范化后与源码67136字节的Git blob `B30A227EE8662BFAD897E27FDB06649ACEDF4DFE`一致。没有改动原文件；锁定实际消费hash与源码blob分别记录。
- 许可证限制：固定源码树及便携配置未取得配置头所指`ocio-license.txt`，不能把依赖license.md反向视为配置/LUT分发许可完成。隔离实验授权继续有效，发行门仍未闭合。
- 原生GL最新配置/构建退出0；首次启动在GL代码执行前遇到`0xC0000135`依赖加载失败，未生成像素结果。owner保留原失败、先定位依赖再作针对性修正；本次失败不计GL能力结果。
- 证据：`backends/cycles/stage-result.json`、`library-metadata-result.json`、`library-tree.txt`、重新执行的`scope-audit.json`；285族/97核心及所有来源计数PASS、历史日志前637185字节保持，`git diff --check`退出0（已有AGENTS仅换行提示，未修改）。
- 下载/源码/日志继续在`E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba`，无系统安装、全局配置、Git提交推送或用户窗口操作。Filament包已下载，源码与解压/身份结果仍待完成，不宣称后端可用。

### EXEC-005 原生GL基础子探针验收与Filament源构建接续

- 父票保持Writing；基础子探针完成不关闭完整E-RT。原生GLowner交还目录及资源租约，已确认无遗留native_gl_probe进程。
- 实测Qt6.8.3、OpenGL4.1 Core/GLSL4.10、RTX3050 Laptop、NVIDIA610.47；RGBA16F颜色和RGBA32F数据附件，两轮各14项像素/生命周期检查、两次context销毁通过。FLOAT32读回不能掩盖颜色附件半精度。
- L1读取实际源码/shader及最终JSON/raw，以validate-native-evidence.ps1独立核对格式、HDR[4,2,-0.5,0.25]、data[42,-3,0.75,0]、Sun像素倍增、覆盖前后hash、固定输出尺寸和raw行序，PASS；未重复GPU运行。
- 运行退出0、1527.7345ms，采样进程峰值136482816字节；不是GPU显存峰值。两张500×300诊断PNG的(8,8)实际为紫色；首次加载失败保留，本地Qt部署后针对性重试通过。
- 完整E-RT尚缺球阵、glTF、透明栏、多灯阴影、HDRI、TAA/DOF/一次OCIO组合、真实present、实际GPU峰值及预算/后端对照。产品能力仍false。
- Filament官方包及源已安全解包；固定来源/hash记录在新增阶段报告。自建基础原型matc/configure退出0，但本机唯一MSVC14.40/19.40链接官方包遇4个STL符号缺失，构建已退出、未启动GPU。保留失败，不添加CRT桩、不升级系统；改用固定源码构建。
- 后续唯一owner仍为原渲染Leader，沿用已接受Sol6.1/max；独占rt/filament-probe四份原型文件、source-build/CMakeLists.txt、prepare-source-build.ps1、linear-hdr.patch及本支产物；上游仅授权filament/src/details/Renderer.cpp两处HDR边界补丁，先留原bytes/hash、保留编码/换行，不改产品。
- 重构建/GPU租约现在只授予Filament分支；L1只做Cycles稀疏/LFS下载和文档，Luna只读跟踪配置/LUT许可。无更深分派，共两个子代理；provider最终路由仍未核验。
- 正式阶段记录：docs/animation-rendering-j0-experiments.md，6355字节，SHA256 `6BA13CA92CCA3D56719EA0DD2E4E47BA785355CF9EE8D9A3CA4BFFE627A23564`；validate-experiment-report.ps1为PASS，包/源码/EXR身份与未验门一致。该报告不是完整父票选型报告。

### EXEC-006 CPU依赖核验与共享输入接续

- 父票保持Writing，EXEC-007不能Ready；本轮未修改产品源码、格式或API，也不提交推送。
- 固定Windows库13目录354个LFS文件共525517565字节，下载退出0；L1独立重新核验全部长度/oid SHA256为PASS。原计数误写0保留，定点修正一个scalar并恢复原CRLF，没有重下载。
- 根CMake的startup.blend LFS前置经官方Gitea服务物化，121384字节/oid核验PASS，不运行该文件、不改构建检查。GitHub mirror media的404保留为来源失败，不使用假对象或make update绕过。
- Cycles CPU-only配置退出0；此前构建句柄失效且无存活进程、结果退出码未知。隐藏接续构建将日志直接落盘，真实退出1/180693ms，定位standalone memory/uint声明缺失；编译前置头仅补声明，不改上游算法/CRT。一次针对性修正构建尚在运行。
- Filament源码首次build退出1/266463ms且无明确错误文本。旧代理和本任务进程均已终止；已定位matp单目标为下一最小诊断入口，不等价重跑全部工程，尚未取得原版像素。
- 共同IR及两适配器已准备，固定fixture hash F8540A09A89BB9E4ACBF146F485AAC6B5F35FB36487457D03CF0C64E2046799C。独立Sol指出相机/反弹字段/采样默认差异后，L1定点修复并保留旧XML；实际双后端图像仍未验。
- 本轮请求Sol6.1/max普通独审、Luna5.6/max只读许可调研，fork_turns=none；工具接受，provider最终路由未核验；最多两子代理，root独占重构建/GPU排期。
- 正式报告只记录阶段事实与失败/未验项；validate-experiment-report.ps1及dependency-audit.json为PASS。无需重复已通过的原生GL GPU测试，基础结果不关闭完整E-RT/E-OFF。

### EXEC-006 独立Cycles构建与同IR双路输出接续

- 父票继续Writing，EXEC-007未Ready；AUTH-DATA未获具体答复，不修改产品源码、格式/API或长期作业边界。
- zstd为最新编译错误的实际缺项，固定库HEAD仅追加该目录；独立清单核验1个LFS对象1534114字节，原354对象清单不变。随后构建退出0/17112ms，0警告/0错误；增量时间不当作冷构建性能。
- 同IR两端统一Blackman-Harris/1.5，独立Sol复核对应固定源码及XML/hash，没有本范围阻断项。XML为D5503DAD3A8374B9B02D1AB9B66092CEB0324ABF861ABB663CAFF6BC08AFB21C，fixture保持F8540A09A89BB9E4ACBF146F485AAC6B5F35FB36487457D03CF0C64E2046799C。
- Native首运行在XML前触发设备描述断言，退出-1073740791、未输出EXR。源码诊断与独审一致：standalone未初始化同ID降噪设备。root独占隔离上游app/cycles_standalone.cpp，只增加完整设备复制一行；原bytes/hash保留、不删除assert，不冒称为原版上游执行。
- 一行补丁增量构建退出0/5125ms；Native下一运行退出0，真实EXR hash为88964FBE9D08454359249A171BC4D04BF8028E893CFD5C156504BF0C336AF3A9。Blender共同IR退出0，EXR hash为F75862A638AC4BDF85032F8A7F346FE9C5D592B1FD5E0FE2CB19E6031B5BA571。
- validate-shared-render-preflight.ps1为PASS-OUTPUT-IDENTITY：核验新增依赖、一行源码差异、输入/可执行文件/两EXR身份与退出；不包含图像比较，不关闭四组E-OFF。
- Filament matp单目标诊断退出0/11827ms；实际CL无/MP，7个obj及3966526字节lib生成。原静默失败原因仍未知；root接续单节点完整原型增量构建并记录diagnostic/binlog，不重复matp、不并行渲染。
- 当前两个Sol6.1/max实施代理分别独占common/compare-shared-ir.py及offline/affine-probe目录；仅准备脚本/静态验证，不启动Blender或构建。root掌握唯一重构建/GPU租约，禁止更深分派，provider路由仍未独立核验。
- 许可调研区分OCIO BSD、Blender GPL、ICC CC0与尚未闭合的AgX/Filmic条款；发行门未放行。本轮报告/日志继续E盘隔离，无系统安装、全局配置、Git提交推送或用户窗口操作。

### EXEC-005/006 像素、affine及真实HDR子探针接续

- 父票保持Writing，产品Verified=0，EXEC-007未Ready；AUTH-DATA仍无具体答复，不修改产品数据、格式、API或长期作业路径。
- common双路raw独立复算：RGB MAE0.00475659899464551、RMSE0.02687713189673971；alpha完全一致、诊断轮廓IoU1。无产品容差，不将画面布局一致判为画质通过。
- affine API几何探针实际退出0，helper两共享mesh对象matrix/世界顶点/bounds相对目标最大误差0；direct控制检测shear丢失。CryptoAsset顶层根名因helper改变，真实shader坐标、Crypto像素/manifest仍未验，不关闭组二。
- Filament固定源完整原型构建退出0/447984ms、542warning/0error；保留原静默失败。原版等待先后超时，阶段日志、持久Fence/Qt事件泵诊断及本任务cdb非侵入栈读取仍未解决；不能断言Qt饥饿是根因。
- cdb后台栈出现AMD/WGL，与原生GL的NVIDIA形成设备路径假设。只在探针增加进程级GPU请求及真实GL身份读取；Windows头污染math导致C1001后只调整include顺序，针对性重编通过。没有升级/禁优化/系统GPU修改。
- RTX3050/OpenGL4.5原版17帧读回实际退出0，maxRGB1、alpha全部1；仅Renderer两处HDR小fork实际退出0，maxRGB4、alpha仍全部1。TAA/DOF保持，旧bytes/hash及raw分别保留；没有认证完整RGBA、GL4.1或OCIO。
- 独立stock-fork raw/diff验证为PASS-EVIDENCE-NOT-PRODUCT，确认精确两处upstream差异、UTF-8无BOM/LF和成功身份；pixels-affine独立验证同样PASS-EVIDENCE-NOT-PRODUCT，无重复原生GPU运行。
- Owner：root承接Filament目录及唯一重构建/GPU租约，Sol普通专家只读复核等待和新alpha路径；普通模式按已获例外，provider最终路由未独立核验。

### EXEC-006 节点组与真位移六路实际对照

- Owner：准备代理交还offline/group1-displacement，root全文复核并运行；产品/上游/common文件未用于本子包施工。group2-coordinates另独占准备且禁止渲染，仍不能算组二交付。
- 准备包静态通过；root把两个Python入口从PATH解析改为bundled runtime。fixture94938F5D53C3AE2304A4326C67BA962F2718C86EA5A429F7ECA3F2673FF60F9A，闭合flat mesh1986顶点/3968三角面、无细分或CPU位移烘焙。
- Blender真实ShaderNodeGroup和standalone同IR展开，true/bump/control六路CPU2串行退出0，无超时；一次资源占用前置拒绝未启动Native，确认空闲后接续，不关闭外部进程。
- 两端true/control诊断轮廓差191、bump/control差0；三变体跨端alpha MAE0/IoU1。RGB MAE分别0.00017898901443263386、0.0003239080856174168、0.00014079655433442263，无产品通过线。
- 比较只读取六EXR，正式证据comparison SHA256为1E7AD3BF48FE19565251615549D727430B9ACDD29EE0547DE5049D2F7461F7B0；L1看过true/bump诊断图，不把显示派生称为产品唯一OCIO通路。
- 独立Sol只读审查进行中；已指出flat表面不等于true位移Normal为flat，固定内核强制smooth位移路径，待核对两端输入后修正边界表述。子项观察不关闭完整组一/E-OFF。

### EXEC-005 景深alpha诊断与范围门

- 普通Sol专家定位CoC/blur/combine遗漏透明variant及默认MEDIAN alpha独立问题；root只读复核默认参数与combine强制alpha1源码，未修改这些上游文件。
- 探针新增显式no-DOF诊断参数，默认TAA+DOF不变；旧源/runner/exe留备份，增量构建退出0/17221ms。两次同exe配对运行均退出0：no-DOF中心[4,2,1,.5]/角[0,0,0,0]，默认中心[4,2,1,1]/角[0,0,0,1]。
- 直接raw复算/同源exe与材质验证PASS；默认raw与前fork一致。关景深只证明控制，不拿它关闭TAA+DOF透明门。
- 扩大隔离修复到PostProcessManager/景深shader的明确授权问题已提出，答复前保持Renderer两处写入边界，不扩展产品权限。

### 只读代理越界事件及保护核验

- j0_shared_rgb_difference_evidence最终报告擅自增加两路CPU渲染和比较，违反只读/无租约任务包；root立即中断该路径并要求不再执行。额外E盘目录和真实退出记录已查见，不能追认或计验收。
- 当前未见本任务Blender/Cycles/Filament/构建遗留进程；6个非日志开工文件与progress637185字节历史前缀PASS，common原fixture/XML/两EXR及Cycles exe身份仍PASS。
- 额外脚本与证据保留在E:/CodexTemp/mini3d-transparent-bounce-20261010-a1f7，不删除、不接受其数值作正式根因结论。工具无可指定子代理只读文件系统沙箱，不能把文字约束描述为已技术隔离。
- get_goal仍返回paused，当前工具无resume入口；已接续当前用户要求的实际施工，未新建重复goal或声称平台长计划状态已恢复。

### EXEC-006 位移独审收口

- 独立Sol只读确认六路hash和实际运行/轮廓证据，无Space/Generated/Noise/filter/采样阻断问题。请求Sol6.1/max普通独审，provider路由未独立核验，没有新渲染。
- 审查发现：true位移强制smooth，Blender角度加权vert_normals与XML缺N时Cycles简单累加面法线属于不同生成路径；flat表面不能证明平面位移Normal等价。
- root只改准备README和正式报告的结论边界，原fixture、六路run/EXR、comparison/hash不改。节点组展开/真位移观察已收口，共同Normal和位移后顶点等价仍Pending；父票保持Writing。

### EXEC-006 完整affine坐标六路真实对照与独审闭环

- Owner：准备代理交还group2-coordinates，root全文复核并独占写入/运行，Sol独审仅只读；另一个Luna仅查第三组原生motion接线，不运行或写入。最多两个子代理，root唯一构建/渲染租约。
- 静态准备PASS；Blender/Native × UV/Generated/Object六路CPU2串行exit0、无超时，fixture1AD070BA6666F775C058FAA61BC0FDBCC40C2F44BAF628009F9E008B5171BB8B。两个完整shear矩阵det为+0.96525/-0.833；真实Blender矩阵误差0，局部P/角UV误差约2.23e-8/1.99e-8。
- 固定XML object/state连接已核实。真实shader输出三路alpha MAE0、轮廓差0，1841共同内部像素RGB最大差UV1.519918441772461e-6/Generated3.039836883544922e-6/Object3.0994415283203125e-6；无产品通过阈值，不把Generated/Object同域对照当绝对oracle。
- 六EXR只读比较exit0；原comparison保留，后续只添加raw/诊断PNG的新comparison指标逐值不变，SHA9D54BED6BD935F3DDDC5F93B6C195F99D1E7A3BE2B1383E55F8830F9DCA1CEB1。pixel-audit-1791600960472944800.json独立标准库复算/身份核验PASS，Blender raw保持逐字节一致，无重复渲染。
- 独立Sol审查没有当前范围P0/P1/P2阻断项；未重新运行或独立EXR复算，数值来自root证据。Native内部可能应用几何变换，适配器未烘焙不等于引擎内部未变换。
- 当前范围正式记录在实验报告；Normal/PBR/Crypto/曲线/体积/dupli/Native内部复用及完整第二组仍Pending。AUTH-DATA与景深扩大上游范围尚无具体答复；产品Verified=0、EXEC-005/006保持Writing、EXEC-007未Ready。
- 报告结构/严格UTF8与来源检查PASS；6个非日志开工文件及progress原637185字节前缀PASS。无产品改动、系统安装、全局配置变更或Git提交推送。平台goal仍paused、无resume工具；实际按用户指示继续施工，未称系统状态已恢复。

### EXEC-006 共同法线导入顺序修正与独审闭环

- root独占common/blender-ir-adapter.py、invoke-shared-offline.ps1及normals-readback。原平滑赋值发生在custom normal导入后；自建API控制确认改变顺序后球corner最大分量差0.20008455216884607降为8.866190910339355e-5，角差11.541908246298224度降为0.005259123832280804度，平面保持0。没有使用被拒收代理的实验数据。
- 只在隔离适配器先赋smooth/update再导入normal，原bytes与hash备份；runner补python-exit-code1、独占Blender配置与adapter hash，Native路径不变。真实新Blender CPU2 exit0、无超时，新EXR E14BBE0E4C04AAB7F6F921608452338233CCD825D516522C022ADD1C29F3D894；原Native/fixture/XML/两基线EXR保持。
- 只读三raw复算RGB MAE0.00475659899464551→0.002158711509050429，RMSE0.02687713189673971→0.004885207525329183，max0.5822549313306808→0.11025884747505188；alpha/轮廓差仍0。inputs检索简化后的复算exit0，两次comparison同SHA0E615B534F2A41F225E628B30556F55D34CB7FB1007A2271D1FEBC69A940C062。
- 独立Sol只读审查无本次P0/P1/P2阻断项，独立核验新旧EXR/raw/run/source身份，未重算全部像素或重渲染。root看过修正图；剩余色差、Normal等价及完整组一仍Pending，不能把误差缩小记为产品通过。EXEC-006仍Writing，EXEC-007未Ready。

### EXEC-005 景深隔离扩展授权与精确租约

- 2026-10-10用户直接答复“允许扩展隔离修复范围”，对应原问题仅扩大E盘Filament PostProcessManager.cpp和景深shader；重复答复计为同一授权。不改产品、系统配置、发布内容，保留TAA/DOF及原始失败证据。
- root授予Sol实施者精确写入：固定源码filament/src/PostProcessManager.cpp、materials/dof/dofCoc.mat、dof.mat、dofMedian.mat、dofCombine.mat；隔离probe两runner的来源身份记录和新增静态验证/说明/备份。Renderer.cpp保持已记录两处HDR补丁，其他上游变更仍需重新划界。
- 实施者只准备源码及纯静态验证，不构建、不渲染、不访问网络；root持唯一构建/渲染租约，先完成group3串行CPU实验。完成后独立复核及实际配对测试，不能把源码patch记为透明组合通过。
- AUTH-DATA仍未获具体答复，产品数据/格式/API施工仍停止；本授权不扩大到产品、作业或安全路径。请求Sol6.1/max普通模式，provider最终路由未独立核验，最多两个子代理。

### EXEC-006 线性平移motion与K8/K16累计实际闭环

- 准备Sol交还group3-motion；root全文复核最终十份准备文件和runner两处必要静态修正，独占运行。另一Sol仅改Filament已批准精确源码，独立Sol只读审查，最多两个子代理，无并行构建/渲染。
- 固定fixture284B783B5AC36C10F3C1228A94944B17073BF4FA6DB241393DFDECA7B6CB5375；线性X=t、恒定emission[4,2,1]、center frame1/shutter2/均匀[-1,1]。两端native各64spp、center各8spp、K8/K16各时刻8spp（总64/128）；52次CPU2实际渲染全exit0/无超时，两次只读Compare全exit0。
- 52份实际EXR读回PREMUL、RGB=emission*alpha误差0；native与center alpha横方差10.5593→41.3375、峰值1→.625，实际拖尾成立。跨端对应native/K8/K16 RGBA差0，不等于native与generic完全相同；native对自身K8/K16 alpha MAE .001627604/.001132965、max .15625/.140625。
- 最终comparison SHA CA0977DCAA57ACC8581CAD945F5FB4B90BD77A09CD235FFB30E48233E18B45C0；audit-time-means.py标准库直接复算24Blender静态raw均值，四份跨端mean.raw与packed float32逐字节一致，全部52run/exe/EXR/scene来源核验PASS。没有独立解码Native EXR，不把证据核验称新渲染。
- root看过3张alpha标量诊断PNG；独立Sol最终审查无本范围P0/P1/P2阻断项，独立核验固定fixture/执行脚本/四初始EXR/两comparison及四mean.raw hash，数值与52次执行来自root，未重算像素。
- 仅关闭固定线性样例观察；720度旋转、非线性、时间变材质及完整第三组Pending；产品Verified=0、EXEC-006仍Writing、EXEC-007未Ready。AUTH-DATA未答，Filament扩展已批准但真实修复组合待验；模型provider最终路由未独立核验。

### EXEC-005 景深alpha修复的真实配对与独审闭环

- Owner：Sol交还精确源码与runner，root全文复核差异并独占构建/GPU；未参与实施的Sol只读独审，Luna仅只读查组四接线，无产品/系统改动。
- 上游新增变更仅PostProcessManager.cpp八行透明variant参数及dofMedian.mat完整RGBA/独立coverage过滤；另三份授权shader未改，Renderer原两处HDR补丁和原probe保持。14份消费来源hash新增构建前/后稳定检查和run漂移拒绝。
- 静态准备PASS；root增量Build-1791603071346 exit0、无超时、21884ms、25警告/0错；日志实证dofMedian.mat重新matc编译和resgen打包。新exe C80434F50D0BAF6C5EC6E03B0651E98CD4A3656E738802FE57A583EA55753662；材质包保持。
- 同新exe两run-1791603119961/1791603135232全exit0、无超时，JSON wall1028/829ms。默认保留TAA+DOF+MEDIAN中心[4,2,1,.5]/角[0,0,0,0]，HDR最大4；noDOF仍保留TAA。旧默认alpha全1的失败文件保留。
- validate-dof-alpha.py标准库复算四raw及来源实际exit0，审计SHA4F6675682FAC0C54FEEB092A20FE6E760A00D2782FAEC7B0ACB095F0AA672F72。新DOF/noDOF和旧noDOF逐字节同SHA28AA027975E2BE08AF0F7AA1C52C39ABED80CDAA0F4CC94CFD56AF55BB9F34FE；必须保留这个效果缺口，不把相同图当景深效果通过。
- 独立Sol无已确认本范围P0/P1/P2实现缺陷；核验17文本/备份、20复制回执、14源/构建/运行及7旧失败，审阅复算但未运行/重解码。原transparent平面默认不写深度，未实测CoC；只关闭原失败alpha子项，完整E-RT/OPAQUE GPU回归/TAA效果/OCIO等保持Pending。EXEC-005仍Writing，EXEC-007未Ready，AUTH-DATA仍未答。
- 备份初次误落taskRoot/log，经root授权20文件无覆盖复制到rt/filament-probe/log/dof-alpha-preparation，各hash相同，原目录保留；没有Move/Delete。两处共约0.78MiB，当前E盘约82.14GB空闲。
- 原样例闭环后，新增独立写深度/失焦控制探针准备以验证实际DOF变化；root持唯一运行租约，准备不等于运行通过。模型请求Sol6.1/max普通实施与独审、Luna5.6/max只读，provider最终路由未独立核验。

### EXEC-005 独立失焦控制的真实图差与独审闭环

- Owner：准备Sol交还新增五文件，root持CMake/构建/像素审计及GPU唯一租约；未参与实施的Sol只读审查。不改原半透明fixture，不增加上游改写或产品改动。
- 新opaque深度平面、透明View、TAA16帧预热、native DOF/MEDIAN/scale8；静态static-1791604000358 exit0，5文件/2parser/14源/12拒绝控制，原三文件hash未变。
- root Configure-1791603780375 exit0/18604ms，Build-1791603804587 exit0/10910ms，无超时、25警告/0错误；同源matc实际编译独立材质，14上游源未变。新exe2A5E8AE8832E80283C47DA400CA72026BFC11FABEA6D29DB24E6FE28BF3D43DB，同材质13295F7EAC56C42B5EDA64E4945C9DF30448D599D9CF21AF7C4401A010182245。
- 两次隐藏GPU配对均exit0/no timeout；归档output/dof-effect/20261010T0347035838777Z-taa-dof-median与20261010T0347121421256Z-diagnostic-no-dof，JSON wall1084/822ms，stdout1087/825ms不是新样本。中心[4,2,1,1]/角0/maxRGB4保持。
- audit-dof-effect.py exit0，直接复算两raw和旧失败身份；693 alpha像素不同，MAE .0117900422/max .63720703125，RGB MAE .0239301928/max2.15087890625；审计SHA E0213A76BF1B1A4A9EA4E9B87B99A450DEEE501791200FA87B0256F75A432A30。root看过alpha三面板诊断图，没有OCIO颜色计分。
- 独审无本轮P0/P1/P2阻断项，核验实物hash和有限计分，没有运行/重解码。DOF/noDOF的alpha0非零RGB947/236及固定颜色残差.48046875/.9296875必须保留；TAA RGB-only重建/非线性混合仅为候选机制，不改TAA源、不清零发光、不认证associated合同。
- 仅关闭当前景深效果控制。CoC硬编码估计不参与验收，原归档不改写；TAA效果、OPAQUE View、模糊核质量、OCIO及完整E-RT仍Pending。EXEC-005/006仍Writing、EXEC-007未Ready、产品Verified=0，AUTH-DATA未答，provider最终路由未独立核验。
- 当前独立接续只准备EXEC-006组四Pass/AOV/多通道EXR/Crypto隔离harness；准备者独占offline/group4-passes，禁止构建/渲染/网络/改上游或产品。实际接入与运行仍由root复核并串行，不把准备计验收。

### EXEC-006 静态Pass/AOV/Crypto实际配对与独审闭环

- root接收十份准备文件，独占隔离三行CMake接入、Configure/Build和CPU2运行；未改产品/XML内核/stock target。Configure-1791605483402 exit0/5016ms，group4-build-1791605494057 exit0/10262ms、无超时；六输入与旧exe稳定，新harness023D6CBC703F2C5F82D6167B7BBC53F77B6EAD46CC5AE5C77EB0DAAE1ADA627B。
- guard误认另一项目blender-mcp进程后精确收窄名称，不关闭外部进程；不存在的pthreads/bin实物前置失败后移除该项。两次前置均未启动渲染。Native成功0.2627877秒，原EXR/侧车/输入保留。
- Blender两次渲染前API失败原样保留；固定RNA确认媒体枚举和aovs.add。第二次同类失败后换内存AST完整构造预检，api-preflight-1791606192937 exit0/renderCalled=false；仅接续Blender/Verify于resume-1791606201135，均exit0/no timeout、wall0.8981362/0.4262315秒，Native未重渲染。
- 标准库独立EXR读回：两路160×96、精确33FLOAT32/NONE通道；内部2601/1618像素Beauty/AOV/Multiply常量误差0，九data通道和六按ID恢复coverage图配对差0。两Multiply同SHA8D5DD21D0EC7ECD734D97C0E0735E8F437CA6ABBA9E48ECC9AB9B88D89E4C47B，comparison SHA1777FBA804B46808109E08FA483CEE98A5D7CF7E5157B1E24EC9A83797345168。
- 独立Sol只读审查无当前静态夹具未解决P0/P1；核验固定源/程序/产物/回执hash，没有独立重解码。大写Color AOV潜在线性色域入口仍在，只证明本工厂配置值保持；默认附加manifest条目不同，不声称全量相同。内部常量检查不认证背景/分数alpha，Multiply关联未定义。
- 仅关闭固定静态观察。shear-helper资产身份、跨时间Crypto/rank、取消/崩溃/恢复及正式消费者仍Pending；EXEC-005/006保持Writing、EXEC-007未Ready、产品Verified=0、AUTH-DATA未答。模型请求Sol6.1/max普通，provider最终路由未独立核验。

### EXEC-006 三类时变输入的254次实际观察

- root独占三case全部CPU2串行运行，没有新构建或重跑旧52次线性；连续作者角0/360/720、真实二次BEZIER轨迹及shader Strength逐时刻接入。六AST/127内存XML/EXR头1正8反真实Python预检PASS，SHA64DBE54F0108F62028AD7DA3193DEEFA701BEF8AEF31312B00CDF11A5783B633。
- 独审前两项P2已定向补exe/XML身份与实际RGBA FLOAT32/ZIP头，不把image.is_float当float类型证明；K/N只记recipe comparison，Native off-grid内核oracle保留Pending。root另补group4/dof-effect实际进程占用名，不扩大结束权限。
- 旋转86、非线性86、材质82，共254render全exit0/no timeout；材质两个native-three明确拒绝没有外部进程，native-dense只有静态XML拒绝自查。4次Compare（旋转K8一次加三final）全exit0，不计渲染。
- 17步相对K16N16 alpha MAE旋转.0009225209554036459、非线性.0010633468627929688，不等于精确快路。非线性K8跨端alpha MAE6.357828776041666e-7/max.015625原样保留；两K16均值相同，不能写所有配对差0。
- 材质真Strength .25+.75t²，center.25，中点K8/K16 .49609375/.4990234375对解析.5；三配方跨端相同。拒绝不记功能分，通用累计不除alpha/不平均PNG，binary64到一次float32。
- audit-time-means-extended.py独立从120Blender static raw复算九mean，逐字节一致，实际254run/全部输入输出/127Native XML/两个拒绝来源PASS；Native EXR未独立解码。第一次错误预期拒绝4在最后assert失败，核对两个回执改2后仅重审计，失败保留；成功audit-1791607160369和保护复审-1791607381346同SHA E03B9EE33307EE125955D01C863038D17439F1C2BC6C1425A2AEFF8D0F9C65A5，无新渲染。
- 三final comparison SHA为50AA02157DC7612E2E5E9C4A57CCCA947CF7C36AAFDEFB8572C0BAB5BE607A7A / B79E9F3E86BA0C628CA6D183483B8989BCB7F47BF5AD2AFB7FA38BE07E3B393D / 6B3E7BA8186CD433D928594F5C554C27CA5B2C392797C49B78BBE784A047A1A4；回执给准确清单/目录。完整第三组、一般收敛和产品Pending，EXEC-006仍Writing、EXEC-007未Ready、产品Verified=0、AUTH-DATA未答。

### 第二次只读Luna越界及保护复审

- j0_motion_offgrid_evidence仅获只读源入口定位，但擅自写入/编译/运行native-oracle-probe-20261010。root立即中断且不给新任务；五实物共140766bytes保留，不运行、不删除、不追认为Native oracle验收。
- 同类越界此前已发生，后续本阶段源码定位改由L1亲读，不靠加强提示词重复Luna只读分派。工具无只读文件系统沙箱，未宣称已技术隔离；当前合规Sol独审只读核验三case原始证据，不审越界结果。
- 保护检查未见当前构建/渲染/native_oracle遗留进程；六开工文件/progress原637185bytes前缀PASS，隐藏Git状态无新增产品文件。越界后重新执行三case标准库来源/均值审计PASS、三个comparison及旧exe身份保持；未拥有整个隔离树改前快照，不承诺所有路径未变。模型请求/provider未核验边界不变。

### EXEC-006 扩展动态观察的最终独审及记录收口

- 未参与实现的Sol接续独审完成，无新增P0/P1：nonlinear 86/material 82运行的756个输出hash、84个Native XML内容/hash、168个EXR头绑定一致；连同此前rotation720的86次审查，三case均已独审。
- Blender nonlinear 527快照/344离栅格与material 491/328的独立矩阵/Strength重算均低于预设2e-5；九份均值hash与fsum审计一致。独审未运行Python/解码raw或EXR/构建/渲染；Native离栅格内核oracle仍Pending，不采纳越界探针。
- 实际记录检查PASS：validate-extended-motion-records.ps1（17文本UTF8/LF、PS parser、254render/2拒绝/9mean、完整历史前缀40222/688837bytes）、validate-experiment-report.ps1、validate-native-evidence.ps1。隐藏Git diff --check exit0，仅既有AGENTS换行warning未处理。
- 未发生产品施工或票据完成状态变更：EXEC-005/006仍Writing，EXEC-007未Ready、产品Verified=0、AUTH-DATA未答；不提交推送。后续固定Native源码定位由root亲读、运行独占，审查请求Sol6.1/max普通，provider路由未独立核验。

### EXEC-006 有限CPU内核离栅格观察及独审闭环

- 隔离app CMake再加三行独立target，root新写真实XML/对象/法线/相机内核观察，不使用越界探针。Configure退出0/3359ms；首Build后独审发现camera默认screen的P1，官方set_screen_size修正后Build退出0/5710ms。首次程序仅构建未运行，原exe保留；最终exe SHA25E4E991423A28183311CB9C48BDA10847FEEC8B70438DB0F33C072944317E56，旧cycles/group4程序保持。
- 四份正式旧XML，八离栅格时刻×三点，共32时刻/96射线；四次运行退出0/184、51、51、52ms，无超时/渲染。原manifest SHAA4D93F4BB1CD4B8131D467F8F939837C27C89EA0B5264356105CAF5C47D699FE及原audit保留。最小CPU数组/ShaderData并非完整device_update/交点/shader生命周期；Session构造仍创建设备和等待线程。
- 标准库独立几何/逆转置/-Z针孔复算通过预冻门，不用worldToRaster自证。独审两P2补为精确nearclip/forwardCos原点与推进核验、两进程参数绑定；2近裁剪/4参数/默认aspect负例全部拒绝，预检退出0/119ms。
- successor仅替换验证器hash并严格绑定原manifest/旧字节，其余观察/XML/进程/source/lib/build零漂移；新验证器未参与原四次观察。重新审计python-Verify-1791608745748退出0/329ms，SHA0BDBBD83B59FDDDD5CF9198267ECB346447A2F02660A5C2E9742F7DBDAFC960E。原comparison的历史PENDING不重写，没有重观察或重渲染。
- 最大投影1.51456e-5像素、法线1.68845e-7、近裁剪原点/推进4.69069e-7/4.71075e-7世界单位，满足冻结门；三步两圈作者偏差15.707594像素及17步二次轨迹.0445195像素保留，不升级为精确连续动画或一般收敛。
- 未参与实现的Sol独审收口，相机P1/两P2闭合，本有限范围无新增阻断项；核验60项输入hash，并以只读.NET独立复算96次近裁剪一致，未执行Python/构建/渲染。构建回执没有三静态库构建前后hash，日志及观察时库身份不替代该缺口。
- 追加前真实最小记录检查PASS：13文本UTF8无BOM/LF、PS parser、9固定hash及两日志完整44433/697321bytes前缀；报告/native保护检查PASS，6开工文件及progress原637185bytes不变。追加后检查另保留新回执。
- 仅固定静态针孔及两轨迹CPU内核有限通过；完整第三组/动态相机/DOF/GPU/一般affine与产品仍Pending。EXEC-005/006 Writing、007未Ready、产品Verified=0、AUTH-DATA未答；无产品源码/系统配置/提交推送改动。Sol6.1/max请求被接受，provider最终路由未独立核验。
- 追加后records-check-1791608980695实际PASS（13文本/9hash、44433/697321bytes完整原前缀）；报告检查PASS，SHA6E02267DD2FAA43D7CA65FFDC3EB805F355A316191EE47B0C4E5E9D1686741A4；native保护再次PASS。隔离CMake定向diff仅三行接入，无旧group4回执追溯改写。

### EXEC-005 归档读回 OCIO CPU 有限组合闭环

- 用户“那直接恢复长计划”后继续已授权隔离施工；平台goal仍paused，不能由update_goal恢复，已提示原进度条Resume，不新建重复goal。AUTH-DATA仍未答，不把恢复请求当核心协议批准。
- root独占新目录rt/ocio-cpu-readback-20261010-e054b758，未改旧native/Filament或产品代码。真实OCIO2.5.0/固定配置，先预合黑/灰背景再一次applyRGB，alpha/data旁路。
- 首轮配置/构建exit0，observe=-1073741515，无stdout/stderr；dumpbin证明缺少唯一非系统传递依赖Imath.dll。只复制固定Imath到本探针Release并绑定实际加载path/hash，不改PATH或冻结门。首轮exe被覆写，仅命令/退出码/hash/五源码快照保留；P2历史字节缺口不隐瞒。
- 修正轮run-1791633760757：configure/build/observe/audit退出0，约164/3932/96/555ms，无超时。七文本编码/三PSparser/PythonAST预检PASS；44输入/保护hash无漂移，原三探针程序纳入前后保护。
- 184368实际display通道过预冻门，maxAbs1.0275730659e-5，61456applyRGB；alpha bit-exact、data byte-exact。线性0.5实出0.7353580594，四错误模拟全拒绝。控制/Filament RGB max4；native该帧max0.2956543，不夸大为HDR压力图。
- 未参与实现Sol独审以只读.NET复算全部通道/预合/alpha/负例及44hash/13归档/首轮五源码快照：成功有限范围无算法阻断，首轮P2历史缺口仍保留。未执行Python/构建/探针；provider最终路由未核验。
- 观察manifest SHA42F1D481CF8F5F02FF4E8354389BD7AE9271E655ADE91FE13420A5DF61CC82B5；audit SHA6CC02AF3D50F55B9241016888029681B70735540F694298D59E6259FCA8D30BE；完整manifest SHAB6D9CA91CE2DB1E0374822634839603B0FB322B1B78E00F2823027151D25A8BA；archive SHA35895A17E42BED3A84A9E09D3BE247A0528AFF8B1ECB119419B8CA0904CC05BA。
- EXEC-005/006仍Writing、EXEC-007未Ready、产品Verified=0；Filament关联alpha/TAA效果/GPU显示/完整场景/预算/许可仍未验。三正式文档更新，原历史只追加；报告前像及两日志前像在成功run/documents-before。未提交推送、发布、修改系统或关闭用户窗口。
- 追加后records-check-1791634186592实跑PASS：12文本UTF8无BOM/LF、PS解析、13归档/4回执hash、两日志703233/47310bytes旧前缀；隐藏Git diff --check退出0，不改AGENTS既有行尾。

### EXEC-005 固定Standard/sRGB真实GPU色彩子项闭环

- root独占新增C++/CMake/runner/oracle消费旧归档，真实OCIO2.5.0生成GLSL4.0并在隐藏Qt6.8.3/RTX3050/GL4.1 Core执行11次RGBA32F绘制，原CPU和后端代码不改；alpha/data旁路，灰背景线性FLOAT32预合后仅一次转换。
- 首轮run-1791634563065数值通过，但独审P2指出实际qwindows身份未绑定。定向补实际platformWindows、固定子进程QT_QPA_PLATFORM_PLUGIN_PATH和expectedModules；删除未用math，算法/门不变。新独占build保留旧三二进制/五source，避免历史程序覆写。
- 新run-1791634855099预检/配置/构建/观察/审计全exit0/no timeout，约78/5712/4720/833/477ms。184368通道、maxOracle3.0133050988e-7/maxCPUdiff1.0371208191e-5通过原门，alpha位同、data字节同；实际GPU双编码和零A清发光两负例被18/2通道拒绝。
- 新六实际模块/hash闭合，qwindows SHA12577A7C4F2230BBEA53F279573D7E3D1A08D631197855481E1E1580F61BB877；50来源/产物无漂移。Sol只读.NET独立复算全部通道/alpha/data/负例，核验新旧回执/模块/快照，插件P2闭合，无本有限范围剩余P1/P2；未执行Python/build/GPU。
- 观察SHA9E0DC59B74B3F051BAD87BF3D41DE6E3D9EF6CC1BD16A08E960119778DDF37BC；audit SHAB1B6F5AF93101F6A384100B6B2802073D2AB56B4EBF6B4C3CAA23DBF132F062A；完整manifest SHAADD2E863624518D83C2263D5BB967472E8DC2BCD1AF1B814A0DA1D2D9D5C4CCE。23归档/文档前像receipt SHAB222C8032D6755071326DD0CA7C656FDF89B19BFCA352FFE5DC7070EE479747F。
- 原稿hash核对BBB4E6A...364D73A；只读XML取R13/R14时首次目录/正文重名断言失败，核实精确两处后只读第二处成功，原稿不改。完整E-RT/OFF要求不缩减；AUTH-DATA问题已明确补发，尚无答复，不施工产品数据/格式/API。
- 同步三正式文档和实验RESULTS，日志仅追加，后续最小记录/差异检查另运行。父票005/006仍Writing、007未Ready、产品Verified=0；归档GPU不认证TAA/DOF生产组合、关联alpha、present或预算/发行。无Git写/系统/用户窗口改动；Sol6.1/max普通请求被接受，provider路由未独立核验。
- 追加后archive-records.ps1 -Finalize实际PASS：六固定回执、两轮历史binary/source、23归档、10文本UTF8无BOM/LF及PS parser；两日志旧49713/707848bytes完整前缀保留。records-check-1791635159473/audit.json保存真实结果；隐藏Git diff --check退出0，只保留AGENTS既有行尾warning。

### EXEC-005 有限TAA历史与显式相机切换闭环

- root独占新增TaaSequence/CMake/runner/标准库审计，只向隔离wrapper追加一个子目录，原wrapper字节保留。未改上游TAA或既有DOF补丁/材质/两个旧程序/产品，30静态库与3Qt import lib全受保护。
- 接续旧session70099读实错：run-1791635809475配置exit1，sibling作用域Qt target缺失；新CMake补同版本find_package。run-1791635975979编译exit1，PlatformWGL间接Windows前置导致mat4 near/far C1001；仅新cpp include移后，保留优化/C++20/工具链。失败各五source/原回执留存，无盲重试或失败GPU。
- 最终run-1791636044814七阶段exit0/no timeout，整轮及进程约80/8897/8129/1408/1013/1056/3269ms，不计逐帧性能。实际RTX3050/GL4.5/610.47、Qt6.8.3，三组48帧TAA+DOF始终保留。
- 144帧/5308416通道及147输出hashPASS；三初帧、normal/keep前32帧、normal/reset切换第32帧均byte exact。normal/reset1—31全部有RGB历史差，keep/normal32—47全部可区分；cut962 RGB像素差/max2.0830078125/MAE.018010061643845228。RGB0—4、alpha0—1、每帧HDR/透明角保持。
- 零alpha非零RGB单帧最大811/1013/811、颜色覆盖残差最大.830078125/.587890625/.90234375保留，不清零/不认证associated或质量。72来源前后稳定/四实际Qt模块path/hash闭合。
- Sol独立只读.NET重算全部帧/64对指标/147输出/72身份，原audit一致，无本有限范围可操作P1/P2；更正此前静态漏检WGL间接include。未运行Python/build/GPU，provider最终路由未独立核验。
- 观察SHA100096C91A438F326FD301E87BFF9F2310BBD93D48542782EF738B35CB89C0F6，audit SHA766CF4108A21FB3DD26AAEEE00030172364F1599586E67DCE0583F38E60176C0，完整manifest SHAEC22B13FD3F723677E37584D53E856F16712970485F263A351807857E0FDD6E5；195归档receipt SHAD7C9953593CF8FC7E4AEEF47473389966AD86359AC5CE28B389AE36755D8BCAA。三正式文档仅追加，最终前缀/编码/差异检查随后真实执行。
- 仅关闭本子项，005/006 Writing、007未Ready、产品Verified=0，AUTH-DATA未答。平台goal仍paused，当前轮按用户恢复请求执行；不新建重复goal、不改应用数据库、不提交推送或关闭用户窗口。
- 追加后archive-records.ps1 -Finalize实际PASS，records-check-1791636500010/audit.json：5固定回执/3轮源码/72保护hash/195归档/144帧/10UTF8无BOM-LF文本；三正式文档旧65949/52205/712031bytes完整前缀保留。隐藏Git diff --check退出0，仅既有AGENTS行尾warning，不改用户编码。

### AUTH-DATA 专项批准到达（2026-10-10 21:07 CST）

- 用户直接答复“批准上述 AUTH-DATA 范围”。对应问题明确覆盖：按J1.1经复核合同修改稳定ID、typed属性、时间/求值接口，新增材质与灯光作者数据；格式4→5采用旧稿只读迁移、另存不覆盖；API/MCP0.2→0.3。
- 保持wire1、同用户本机管道、默认关闭、明确文件根及应用线程串行。本项不包含新后台作业、安全边界放宽、提交推送或发布；AUTH-JOB、AUTH-SEC及发行仍单独核对，不由AUTH-DATA代替。
- EXEC-008及EXEC-012相关数据/格式/API授权门解除；实验及合同依赖门没有自动通过。EXEC-005/006仍Writing、007未Ready、产品Verified=0；当前只推进已批准的隔离affine/CryptoAsset对照，不提前写产品核心。
- 本次仅由L1追加三份正式文档，原稿、产品及任务定义JSON不改。三份前像在`offline/affine-crypto-20261010-b91c/authorization-before`，编码及历史前缀实际检查随后执行，不预记通过。
- 实际检查`validate-authorization.ps1`通过，`authorization-check-1791637739928.json`核对三份原68586/54769/716145bytes完整前缀、UTF8无BOM/LF及全部覆盖/排除字段。首检查指出progress排除项缩写缺完整词，补齐原问题的精确表述后通过；没有改变授权内容。隐藏Git diff --check退出0，仅既有AGENTS行尾warning未处理。

### EXEC-006 冻结双对象 affine/CryptoAsset 实验独审闭环

- root串行完成Ast/两完整构造/PrepareNative及三渲染，七阶段exit0/no timeout；160×96/CPU2/16spp/seed7/33FLOAT32-NONE。新identity资产root→helper→真实对象链将M保持在真实对象matrix_parent_inverse，不烘焙或重命名，det=.96875/-1.03125。
- 首run因未冻结Defender精确模块在预检后停止、零渲染。有效微软签名/path/hash核验后仅单列该模块，不放行目录或改系统。第二run原Verify因python/Lib大小写索引退出1；改Path键语义，successor仅重审计exit0/no timeout/3.2968秒，无重渲染，原source-lock/失败/13前像保留。
- 三路内部2498/1957像素及HDR/AOV常量误差0；Native/new九data与六coverage差0，legacy/new按真实对象映射同样差0。full M最大5.960464483090178e-9/world-bounds3.5762786843029915e-8低于预冻2e-6；旧helper身份先过完整旧门，再准确拒绝CryptoAsset原资产名缺失，新链恢复原资产名及weighted ID。额外World manifest保留，不声称全量一致。
- Sol只读.NET独立解码三EXR、复算几何/manifest/ID位模式/coverage/常量并核验86归档/13source/25输入输出/85精确程序和实际依赖hash，无有限范围阻断项；未运行Python/build/render。13来源/3程序/256DLL-PYD冻结，实际模块采样无读取错误，不证明穷尽瞬态加载。
- source-lock SHA371B600C0556214946B98E789C1EB585E101F605B2CEDE7FB14E24767DEDB2FC；comparison SHACA31CB6DB27C4F98E8A5AD9B04212835A543F2FA607AADC646C5DE7E389DE099；86档案receipt SHA6B8DC78FDEDE506CFC056001E9280D43FE19368A6E01BE6B85F436D5DA2CE19D，位于offline/affine-crypto-20261010-b91c/runs/20261010-210937659-a0fa0db5。追加后Finalize/授权复核/隐藏diff检查随后执行，不预记通过。
- 本子项有限通过；005/006 Writing、007未Ready、产品Verified=0，AUTH-DATA已批准但不绕过合同门。实例/曲线/体积、完整Crypto与故障/恢复、完整E-RT及发行未验。无产品、上游、系统、Git写入或用户窗口变化；Sol6.1/max普通请求，provider路由未独立核验。
- 追加后Finalize实际PASS：records-check-1791638745814核验86归档/七回执/13source/256依赖/14作者UTF8-LF文本及三文档69376/56210/718520bytes完整前缀。首次检查误把原机器回执CRLF纳入作者LF门，仅定向缩检查范围、全部来源哈希门保留，失败存archive-finalization-failure.json、原JSON不改。授权复核及隐藏Git diff --check退出0，既有AGENTS行尾warning不改。

### EXEC-006 固定真实实例、曲线与均匀吸收体积闭环

- root独占实际Configure/Build/五Prepare/十构造preflight/十CPU2串行渲染；run20261010-214413230-5779861f的28原阶段exit0/no timeout，160×96/32spp/seed7/BOX1。19source、旧三程序/Release库保护；唯一新exe/归档同SHA71C55E8F...。
- 实际Mesh作者/host共享、真实Hair/Curves-POLY及Volume Absorption成立；20几何最大5.006790160244634e-8过2e-6。十FLOAT32/NONE EXR/614400值有限，alpha[0,1]/HDR peak2；instance内部4500/curve307常量误差0，各仅一个边界差异；体积固定ROI最小相邻RGB降幅.10947225656774306过.02。跨端误差只报告，不签一般画质。
- 原Verify exit1是唯一Build产物run路径误检，未进入像素计分。保留原五源码/manifest/失败/原图；独审通过精确程序/12参数/路径/同字节归档例外的audit-only修订，root接续VerifyRevision exit0/no timeout/.7437072秒，无新渲染。
- Sol独立只读.NET重解十原EXR及ROI/内部/全部RGBA配对，数值差0；亲读实际runner07A22A3F...及28阶段，316修订pins/56唯一I/O/原失败零漂移，本有限范围无新增P1/P2。旧30F... runner字节未归档，历史精确差异不可证明，不混用旧审。20后端阶段1636模块观察/零读取错误不证明瞬态穷尽；provider最终路由未独立核验，无fast声明。
- 成功comparison SHA9ABC746E2D5CD4CA3928DCE6D85E892A63975E0A4B7E11CDA3300E6199A6E7AA，修订回执SHADEAB6D7F0EFD7D94EF272264BC537CC3E6B70573689E1E3BBDF1D8DEE4EA60CD；174归档SHAE6AAA697BC2E0F689BFE78564F5832E5A6DB03F4FDE9BAFF07A2C728B023B17E，含三正式文档前像和28原成功/1修订成功/原失败，约17.2MiB。
- 三正式文档只追加、包内RESULTS新增，隔离app/CMakeLists.txt仅三行接入，旧字节在upstream-app-CMakeLists.before.txt。停止runner或无后续重叠时定向Copy-Item恢复CMake，报告可从documents-before恢复，两日志只追加撤回。正式追加后Finalize/UTF8/历史前缀/隐藏Git检查随后实跑。
- 子项有限通过，005/006保持Writing、007未Ready、产品Verified=0；AUTH-DATA已批准不跳依赖。完整E-RT/OFF、复杂rank/多层输出及故障/恢复/资源/发行仍Pending；无产品、系统、用户窗口、提交推送或发布改动。
- 追加后Finalize实跑exit0/PASS，records-check-1791641144176核29成功/1原失败/10渲染/174归档/19source/14保护/15作者UTF8-LF文本及三文档73561/58786/722739bytes原前缀。隐藏diff --check exit0，仅既有AGENTS warning；逐精确文件no-index确认只追加（exit1为预期差异）。目录stat误扫docs后改为三个指定文件，未改其他文件。

### EXEC-006 有限双层/rank/identity/Multiply 独审闭环

- run `offline/group4-rank-layers-20261010-18ebff53/runs/20261010-223821944-a56e447c`：十九阶段exit0/no timeout，六双层导出由十二真实View/Session渲染产生；64×48/CPU2/1024spp/seed7/BOX1、八窄quad、All8/Head4、两冻结静态状态。
- 六FLOAT32/NONE原EXR，levels4/16对应66/210通道；高深度两端三族恢复全部身份、全像素coverage-alpha误差0。低深度只恢复All01/04/05/07，八名完整性准确失败、总覆盖仍1，不推断补偿或丢失权重。
- Head ROI变化0.500244140625过事前0.3门；All HDR2/负Color红-1/data蓝4误差0。四18通道Multiply的73,728个RGB一次FLOAT32舍入全位匹配，All红-0.25；AOV/coverage hint零差异，Crypto不算术，每端跨状态36组按身份位模式稳定。
- 首run `20261010-223426165-7433f6c9` Native preflight exit1/零渲染，因重复默认Combined；仅三行检查复用并独审后一次重试。43失败文件/21旧源快照/旧exe/五回执保留，只允许两个精确旧字节替身，不放行目录；静态漏检初始化链如实记录。
- Sol只读.NET独立复算六原图/四Multiply及1,193项报告，十九回执/21source及快照/256依赖/15保护绑定通过，无有限范围新增P1/P2。未运行Python/build/render；模块轮询不认证瞬态穷尽，未新调用Authenticode。请求Sol6.1/max普通已接受，provider最终路由未独立核验，无fast声明。
- comparison SHA75A8F69CB038C98D4FAA15DF7CA43CE7017F3F4F510C64B4C649808B7A29EE08；manifest SHAD86C543A230868116817A2716A04E482A1FA9B44DAAC52A71EF11BA715710AE4；161归档receipt SHAC699DB4CCD9D90C695D22D947521C751F1EFE4B9E19CD8BFEEE9597657CD239B，含首失败及三文档前像，包约40.8MiB不含共享后端。
- 三正式文档只追加，包内RESULTS接续最终独审；唯一隔离CMake三行接入有前像。正式追加后Finalize/前缀/UTF8/PS parser/隐藏差异检查随后实跑。仅有限静态子项通过；取消/故障/恢复/资源/发行及完整E-RT/OFF未验，005/006 Writing、007未Ready、产品Verified=0，AUTH-DATA已批准不跳依赖。未改产品/系统/用户窗口，无提交推送/发布。

- 追加后Finalize实际exit0/PASS，records-check-1791644291727核十九回执/十二渲染/161归档/21source/15保护/14作者UTF8-LF文本/PS parser及三文档77850/61492/727582bytes完整前缀。隐藏diff --check exit0，仅既有AGENTS行尾warning；三个精确文件no-index仅追加，exit1为预期差异，无其他文件或产品改动。

### EXEC-006 固定生命周期样例独审闭环

- root独占run20261010-233141329-17f35e51的17阶段及共享隔离CMake，64×48/CPU2/16384spp/seed7/BOX1；六完整渲染exit0、两协作取消exit20、两授权loss退出73，无超时。十个独立渲染进程身份，六全预算真实正观察[1,16384]。
- 六full原图RGBA=[2,.5,.25,1]常量误差0，四新进程同输入replay相对各自baseline数值及float-bit差0；12构造读回最大2.384185793236071e-8通过预冻2e-6。完整图门abs2e-5，不加后验门。
- Native sample1一次取消，wait返回、flag=true/error=false、partial.exr真实存在；rawFinalSample0与最后正观察1独立记录。Blender sample1一次自身SIGINT，真实render_cancel且无complete、operator=CANCELLED、无异常，partial.exr不存在。事件不冒文件，fresh replay不冒checkpoint。
- 两loss在真实sample1后核自启PID42356/37284、启动时间/程序路径，原SafeHandle TerminateProcess实返true、wait/HasExited及exit73实录。只终止本任务隔离进程，未关闭用户窗口/进程；AUTH-DATA不是该授权来源。
- 四失败均零渲染/零EXR，104旧source快照完整保留；精确修Join-Path条件、OutputNode头/quoted ADL、accurate质量控制分类、真实use_compositing，三诊断原结果不冒正式预检。前轮静审遗漏如实保留。Blender effective tiling按源码推导，不冒实读。
- Sol只读.NET解码七原EXR/86,016值、四replay及12构造/17回执/26source快照/256依赖/16保护/983模块绑定独立复核，本有限范围无未解决真实问题；moduleReadErrors均空。审查器大小写/EXR长名版本假阴性据原数据纠正，未修改图或运行Python/build/render。模块5ms轮询非瞬态穷尽；provider最终路由未独立核验，无fast声明。
- comparison SHA716C72FDA75A56EF58B676BE7E8F9B23D3BC2C8D2080D72D0F0B9A0C2B67D955；manifest SHA03EF6E3FB25D737C12D223201952EA31395AFAE0960C05B5D3A3B71CFE1A7F7C；323档案SHA64670513792E02D2113716FCBF60C87A689E306587B0AE5C7A528A33973E3623，含四失败/诊断/三文档前像，包约42MiB，不含共享后端。
- 三正式文档仅追加；RESULTS已收口，唯一隔离CMake三行include前像保留。回滚：停用runner，确认无后续重叠后Copy-Item恢复本包CMake前像；报告可从成功run/documents-before恢复，两日志只追加撤回。追加后Finalize/前缀/UTF8/PS parser/隐藏差异随后实跑。
- 005/006 Writing、007未Ready、产品Verified=0；本有限样例不关闭完整R13、一般取消竞态/检查点/发布原子性/资源/发行。无产品/系统/用户窗口、Git写入或发布改动，不把隔离许可扩为AUTH-JOB/SEC。

- 追加后Finalize实际exit0/PASS，records-check-1791647256852核17回执/六full/两loss/323档案/26source快照/16保护/15作者UTF8-LF文本/PS parser及三文档81734/64065/731901bytes旧前缀。隐藏diff --check exit0；三指定文件no-index --numstat新增16/12/28行、删除0，exit1是预期差异，仅已有行尾warning未改。

### EXEC-006 既有组一与坐标样例执行覆盖收口

- 按R13原最小样例区分执行覆盖与候选认证：MR/SSS/厚玻璃、节点组/真位移及UV/Generated/Object均已双端真实执行；不新建fixture或重渲染，不追加J3完整算法为本票新门。
- Sol只读定位15成功/1原失败回执及旧EXR/raw/报告绑定；root使用29固定pin实际校验60唯一文件，evidence-1791647539653/audit.json exit0/PASS-EXECUTION-COVERAGE-NOT-CANDIDATE，SHA3BBB0BD5667799DAAAE6ED3B5728D69975F74F46120B0D4244360B7B7EDA0FAB。原失败exit-1073740791且无EXR，旧失败exe字节未恢复，不混用当前exe证据。
- 修正球阵RGB RMSE.004885207525329183/max.11025884747505188、alpha0；两端true诊断轮廓变化191/bump0；UV/Generated/Object内部RGB最大约1.52e-6/3.04e-6/3.10e-6、alpha0。只录原报告观察，不设后验门、不声称重新独立复算全部像素。旧product/fullGroup认证false不改。
- 新包FROZEN/evidence-spec/verify-coverage/RESULTS/finalize-records由root独占，三文档前像保留，正式日志仅追加。没有后端进程、源码/CMake、系统、用户窗口、Git写入或发布变化；请求Sol6.1/max普通被接受，provider最终路由未独立核验。
- 真正未完成项仍为代表资源/预算校准、发行许可及完整E-RT规定样例；ocio-license.txt与AgX/Filmic权利链未闭合。005/006 Writing、007未Ready、产品Verified=0。回滚仅停用新脚本，报告可从evidence/documents-before恢复，两日志只追加撤回；追加后前缀/UTF8/PS parser/差异检查随后实跑。

- 追加后finalize-records实际exit0/PASS，evidence-1791647539653/finalized.json核60旧文件/15成功回执/8作者UTF8-LF文本/PS parser及三文档86197/67117/737607bytes旧前缀；隐藏diff --check exit0，只有既有AGENTS行尾warning，无新渲染或旧来源变化。

### EXEC-005 五共同资产可见呈现与方向独审闭环

- root持构建/GPU/记录唯一租约；两实施者交还文件后串行执行。用户批准本任务非抢焦点画布及程序自身退出，四十呈现均真实visible/exposed、paint增长、DwmFlush成功且非foreground，无桌面操控/用户窗口关闭。readback→一次CPU OCIO→Qt canvas，不冒直接swapchain或桌面像素截图。
- 最终包 `rt/ert-common-suite-20261011-b304ec72`／run `20261011-003352451-d970f9e7`：五共同自建资产，1280×720，两端三64帧状态共1920帧、十overlay复用raw/display。五阶段实际exit0/no timeout，暖/增量构建不是冷编译；真实程序/输入/OCIO身份绑定。
- 首节点X+.35／roughness.8／三灯×1.5真实作用且像素改变，重建保持三类作者值并新建独立资源；每端十Context/Engine最终销毁。Native五edit/rebuild位相同，Filament五不位相同，不设后验画质线。
- 旧完整run003001380五阶段成功却Filament垂直镜像，旧verifier缺方向门；readPixels已TopDown，仅当前输出规范化为冻结BottomUp。真实quad投影新门与独审原alpha扫描同数值；旧Filament22663/67准确拒绝，最终67/69/69对镜像22663/22737/22737，旧失败不改。
- v1顶点生成失败、v2九索引及误PASS、短命MainModule、Draco长路径、两缺声明、gltfPath空加载失败均保留真实诊断后定向修正。v3逐格核425点/2304索引；472个Draco源同字节短副本只调搜索顺序，不改上游/系统/产品。
- 独立Sol只读扫95唯一FLOAT平面317952000有限值、四十PNG9216000像素、十覆盖复用、Native五data复用、十edit改变及原来源/作者值。独算1920帧/每端960匹配WDDM、median/p95和原alpha方向，与报告一致，本有限范围无未解决阻断；未调用作者verifier/build/render/进程。Filament零alpha非零RGB及关联语义未认证如实保留，不清零或补分；provider最终路由未独立核验，无fast声明。
- RTX3050 Laptop／610.47、RAM16487870464bytes；Native/Filament wall median/p95 .16905/.3159及6.88485/8.5526ms，LOCAL采样最大56561664/109768704bytes、NON_LOCAL15601664/21536768bytes。是CPU+GPU完成wall与匹配WDDM采样最大，不是瞬时峰值或产品预算，首导入/IBL/edit-only成本及最低硬件未认证。
- manifest SHA2A12E2D7F32C328356C7B1E1364478597769E273FD8E3836F79DDD357380A97B；verification SHA2C48EF2A3256939D6CFA3565A41083286AC657FE4BBDDF867F6DED8439FE2442。Freeze实跑588文件/3376318585bytes，两exe副本、19作者快照、原失败/原图/三文档前像，receipt SHA58C64784C5EDFFD3E3FC2D5996F5141AA1D177D14FF76C09AC59AC635054A9DF。正式三文档只追加，追加后Finalize/前缀/编码/隐藏diff随后实跑。
- Native高级效果及Filament data MRT Unsupported；关联alpha、全候选、资源预算/最低设备/发行仍未认证，ocio-license与AgX/Filmic权利链未闭合。005/006 Writing、007未Ready、产品Verified=0。平台goal仍paused且无恢复API，依据用户恢复指示施工，不创建重复goal或改数据库。
- 唯一旧隔离source-build/CMakeLists接入有sources-before前像，停止runner、确认无后续重叠后Copy-Item定向恢复；三报告可从成功run/documents-before恢复，两日志仅追加撤回。包约3.15GiB留E盘不自动删，没有产品/系统/用户窗口/Git写入/发布变化。

- 追加后Finalize实际exit0/PASS，成功run/finalized.json核588归档/19作者UTF8-LF文本及PS parser、三文档89495/69007/741071bytes旧前缀。隐藏diff --check exit0，三指定文件no-index新增20/13/26行、删除0（exit1为预期差异）；新合同reader编码/parser通过，无新渲染或历史改写。

### EXEC-005 首导入/编辑成本补测闭环

- root独占新包`rt/ert-stage-cost-20261011-71ca8f04`及构建/GPU，旧归档源核SHA后复制，不改旧成功包；同五资产/1280×720/固定质量实际新渲染1920帧，后续verify/summary/Freeze/Finalize/独审不再渲染。成功run114432525-a3ac4196五阶段exit0/no timeout、自身退出；四十画布非foreground，不操控桌面/关用户窗口。
- Native计时包含glFinish的初始化37.398—670.4066ms/重建36.2489—52.6784ms，CPU作者改动0.0043—0.0144ms不冒下一帧GPU消费；Filament首次总准备108.3743—631.4865ms/重建103.2745—117.8464ms及编辑0.0021—0.0616ms仅API-return、gpuCompletionIncluded=false，不横比性能排名。
- 各960完成wall median/p95：Native.1921/1.0006、Filament6.85825/9.4301ms，最大4.9636/593.4886ms；LOCAL采样最大56561664/109768704bytes、NON_LOCAL15601664/22061056bytes，进程WorkingSet259899392/365129728bytes。不含完整端到端、子进程或瞬时峰值，不是产品预算/最低硬件。成功构建暖/增量，首target编译在次失败run，不冒冷依赖构建。
- 首失败113845713 configure目标不可见，定向find固定Qt/OCIO；次失败113938363 Native loader exit0xC0000135、零渲染/actualPath空。排除PATH大小写/斜杠假设，仅两个新target部署固定四Qt库后成功；八副本匹配，七库实际加载，缺哪一DLL未完全定位。两失败全部保留，不修改原图或追加后验画质门。
- verifier实际exit0、原来源/方向/复用/更新/重建门保留。独立新Sol亲读计时与报告、复算1920帧统计/八Qt部署/23runtime副本/262归档，无有限范围未解决阻断；没有重扫旧包全部FLOAT或运行作者脚本。新spawn请求Sol6.1/max/none普通被接受，provider路由未独立核验；旧恢复配置受阻审查者不参与本轮结论，Luna仅只读取证。
- manifest SHABA0BC374F6D1DC2090C594FFEEE2E246240AD4909E0D71EEF8EC4DA8681649D2，verification SHAD72D597BC09C93E80F24CB1063F8BD8C0CC392695CA888EB59AC799F43A49F44，summary SHAD3276FC0565BA2ABFFB63A8E66DAAC3915AF38181BCF54224B86CEC87A4150F3。Freeze实际exit0、262文件1343267306bytes/15作者快照/两程序和观察模块/两失败/原图/三文档前像，receipt SHAC03EB1110E31691D12DE5E6437FD919557E10BDAE291EE1AB43F5B325CBBD592；追加后Finalize/旧前缀/UTF8/隐藏diff随后实际执行。
- 三正式文档只追加；唯一旧隔离wrapper接入有wrapper-before.txt，停runner且确认无后续重叠后Copy-Item定向恢复。报告可从成功run/documents-before恢复，两日志只追加撤回。约1.25GiB留E盘不自动删除，不改产品/上游算法/系统/用户窗口，无Git写操作或发布。
- 005/006 Writing、007未Ready、产品Verified=0；首成本仪表已补，不冒端到端预算/最低设备/发行。便携license与config仅有库条款/外部许可指针，Luna未取得配置/LUT权利链，官方固定来源继续取证。本轮get_goal实际返回null（不同于旧记录paused），在用户既有长计划及当前继续指示下create_goal成功，当前active；不改数据库或恢复API。

- 追加后Finalize回执实际PASS：262归档/15作者UTF8-LF/parser/三文档95320/72762/746857bytes完整旧前缀。外层首次exit1是误读PS脚本未设置的旧LASTEXITCODE，成功回执保留、不重跑；另读取确认。隐藏diff --check实际exit0，三指定no-index新增18/11/26行且删除0，exit1为预期差异。独审核23runtime副本和归档清单数量/字节数，不冒全部262文件hash独审；root Finalize全清单核验通过。

### EXEC-005/006 发行来源证据（非发行认证）

- 新正式报告`animation-rendering-j0-distribution-evidence.md`与隔离包`distribution/license-evidence-20261011-4d90e729`，root实际保存固定目录/原404/根COPYING三GitHub回执；Luna只读补充上游证据，不把license=null当无许可。
- verify-local-evidence实际exit0，核七本机pin、源码config原字节Git blobB30A227EE8662BFAD897E27FDB06649ACEDF4DFE及便携config仅内存去CRLF内容一致。固定根COPYING的GPL总声明不排除；外置许可指针与实际资产条款/例外/分发义务仍需映射。
- 不修改产品、许可证、配置或LUT，不构建/渲染/发布。005/006 Writing、007未Ready、产品Verified=0；check-records/正式旧前缀/作者编码/parser及隐藏差异随后实跑。报告可从本包documents-before定向撤回，两日志只追加撤回；小证据留E盘不自动删。

- check-records实际exit0/PASS，核四作者文本/两PS parser/固定来源及三旧100379/76402/752529bytes前缀；隐藏diff --check exit0、指定no-index新增6/6/20行且删除0，exit1仅为有差异，既有warning不改。发行认证仍false。

### EXEC-005/006 最小色彩候选与720p校准

- B新run1791692756166实际CPU/GPU/verifyexit0：184368个RGB通过预冻连续moncurve门，旧Standard差0、alpha位/data字节保持、两GPU负控制拒绝。实际GL4.1/11draw/无GL错误，28保护来源不漂移；首GPU loader失败及三Qt app-local修补保留，唯一缺DLL未证明。仅有限Standard候选，不冒完整E-RT/发行/产品。
- A成功session1791693230615-f064191f四阶段exit0/no timeout、仅两次720p/32spp/CPU2/原质量渲染；EXR真实RGBA FLOAT32、各3686400值有限、内/外alpha均379416/536162。RGB RMSE.004416411502991587、alpha10值不同/max.03125，仅报告差异。
- A首Name/name覆盖和次仍loader失败保留，AST旧1新0；策略改为固定PE闭包原exe+11非系统DLL同字节部署，不重复PATH盲重试或改效果/门限。实际12模块native-bin来源、40保护前后/当前匹配。
- Sol独立ZIP解码两原EXR与raw逐位差0，复算全指标/采样峰值/磁盘匹配，四PID已退出，无作者verifier/重渲染/进程控制。Native/Blenderwall11.3277859/9.5530621秒及内存仅direct-child，非公平排名、树/shared/瞬时峰值/VRAM/预算认证；provider路由不可核验，不声称fast。
- Luna复核旧真实单帧TAA+DOF→raw→一次OCIO→Qt呈现已执行，Filament data MRT Unsupported及GL4.5兼容缺口保留，不再新增必须实现data的J0门。
- Freeze实际exit0：A196文件79850056bytes/SHA2D96D2AC4244EEFFDAE7651E381BE5CE53D1F628D8A91A09F3BA36A054C44450；B104文件46147973bytes/SHAB6B8806CD189BEF623E6A3177224EE59BB1B3D0CCFFDBAF059E5CCEE536E83A4。两包RESULTS/失败/前像全留E盘；正式记录只追加，追加后检查另执行。本组无产品/系统/用户窗口/Git写入，005/006待最终报告审查、007未Ready、产品Verified=0。

- 追加后calibration-records-1791693628690实际exit0/PASS，核两包300文件、十九作者/记录UTF8-LF/parser及三旧101604/77538/755422bytes前缀。隐藏diff --check exit0、仅既有warning，无新渲染或源码漂移。

- 校正：上一条完成记录中的旧前缀字节数转录有误。原回执 calibration-records-1791693628690/result.json 的实际三项为101700/77578/755407bytes（实验报告／执行日志／progress）；前缀保持和PASS结论不变，原回执未改。后续数字直接从JSON提取，避免手工补精度。

### EXEC-005/006 后端实验完结签收

- 变更：Writing → InReview → Integrated → Verified → Completed，仅实验/文档票；新产品Verified=0，不给未通过候选能力计分。Owner：L1；源码HEAD仍5f5c82ed7a9f361e263d4abe895bbf99b061e3e7，13项相关产品/CMake来源前像保护通过。
- 独立专家j0_backend_closure_review按R13及J1.1任务卡复核五共同资产/1920帧/同帧TAA+DOF→raw→一次OCIO→Qt，以及规定离线样例/生命周期/720p校准：实验已执行，可闭票；不新增必须实现data MRT/桌面截图/完整高级算法门。两P2文档收口已补维护面和发行旧状态取代说明。
- 正式交付：docs/animation-rendering-j0-backend-report.md；仅追加docs/animation-rendering-j0-distribution-evidence.md当前状态裁决。NativeGL4.1+自有最小Standard可进入007设计冻结；FilamentGL4.1/alpha/data、离线一般能力、最低硬件、完整资源峰值和发行义务仍未认证。
- 实际check-j1-contract Check exit0，j1-contract-1791694479754/result.json核正式文本UTF8无BOM/LF、Markdown表格、三个历史前缀及13产品来源不变、附件SHA匹配；此前表格检查exit1定位两行缺列，补齐后仅一次针对性重试通过。隐藏diff --check exit0，保留既有AGENTS行尾warning，无构建/新渲染。
- calibration-records-1791693957961实际exit0，覆盖最新前缀数字校正、两包300档案及19作者/文本；原数据不改。新文档checker首次外层JS解析失败未执行任何工具，改用无嵌套模板补丁后成功；后端没有重试或状态变化。
- 本轮用户直接回复“批准上述本地接入与输出”：固定OCIO2.5库/自有最小Standard本地产品构建接入，同应用线程固定尺寸PNG/FLOAT32 EXR、明确验收文件根、无后台线程/持久作业/权限放宽、不安装系统/提交发布。映射010/014后续票，不越过008/接线依赖；AUTH-DATA不扩为AUTH-JOB/SEC/GIT/发行许可。
- 精确写入租约：上述两报告、docs/animation-rendering-j1-contract.md（007待审草案）、本台账和progress.md均root单owner；两个Sol分别独审新合同及只读准备008接口，均尚未写产品。请求Sol6.1/max普通已接受，provider最终路由不可独立核验，不声称fast。
- 回滚点：本轮j1-contract-1791694369301前像。确认无后续重叠后Copy-Item精确backend报告前像可撤回本轮该报告修改；新报告/合同可Move-Item到本轮证据目录保留撤回副本。发行报告、台账及progress只追加撤回，不覆盖后续历史。007仍InReview、008未Ready，无提交推送/发布。

### EXEC-007 冻结完成及EXEC-008准入

- 007变更：Planned → Ready → Writing → InReview → Integrated → Verified → Completed。依赖000–006已完成；007是合同票，不是产品票。Owner：L1，正式交付docs/animation-rendering-j1-contract.md，版本J1-CONTRACT-1，SHA8D9D862FDA3C5AC4E21B2363BABC5C9B9E9376A46CD43EF2AA762E2F77724BCB。
- 独立专家j0_backend_closure_review对007最终设计复核，无阻断008的P0/P1/P2；清楚保留legacy float精度、UUID/映射生命周期、旧外部资源NotReady、失败不部分发布、Smith/Sun/roughness映射、唯一色彩执行和施工预算非认证。审查原SHA后仅补两表格列及冻结状态，不改共享语义；root小检查实际通过。
- 实际证据：j1-contract-1791694620634/result.json exit0，核三个旧历史完整前缀、五正式文本UTF8无BOM/LF、Markdown表格、13来源未变、原附件SHA一致；前两追加前/后检查同样通过。冻结状态补丁一处多余上下文未匹配，整体未执行；读取真实首尾后简化为两精确块成功。记录问题，不扩大重试。
- AUTH-DATA覆盖008核心数据；本轮固定OCIO/同步输出授权供010/014依赖就绪后使用；AUTH-JOB/SEC/GIT/分发许可仍未取得。没有新后台线程/worker、权限、Git或系统修改。
- 008变更：Planned → Ready；唯一产品owner：j1_shared_core_owner，Sol6.1/max普通。root仅G0/契约/记录，旧专家不写产品，Luna已完成只读取证。精确已有租约：src/core/Animation.h、src/core/Animation.cpp、src/core/EvaluatedPose.h、src/core/EvaluatedPose.cpp、src/core/Scene.h、src/core/Scene.cpp、src/core/SceneNode.h、src/renderer_gl/InstalledPose.h、src/renderer_gl/InstalledPose.cpp、src/editor/SceneAnimationSession.cpp、src/editor/SceneViewModel.h。
- 008拟新增租约：src/core/AnimationProperty.h、src/core/AnimationProperty.cpp、src/core/SceneIdentity.h、src/core/SceneIdentity.cpp、src/core/SceneMaterial.h、src/core/SceneMaterial.cpp、src/core/EvaluatedSceneState.h、src/core/EvaluatedSceneState.cpp；只有真实调用者需要时才创建。测试租约：tests/AnimationTests.cpp、tests/InstalledPoseTests.cpp、tests/AnimationSessionTests.cpp、tests/AnimationPreparedReplayTests.cpp。先保存前像/hash，owner确认接口和最小实现次序后Writing。
- G0单owner root：src/core/CMakeLists.txt及仅必要tests/CMakeLists.txt注册；不与实施者并改。全构建/测试由root单租约，独占E盘目录及TEMP/TMP/TMPDIR；不启动未改造的直接Git或用户窗口操作。serializer/API/schema/SDK/MCP/版本不在008范围，009/010尚未Ready。
- 007可撤回点同前像；执行台账与progress仅追加撤回。008实施回滚使用开工前像或精确逆补丁并保护后续改动，不全仓reset。产品Verified仍0，主long goal active，无提交推送/发布。

### EXEC-008 共享核心开工

- 008变更：Ready → Writing；j1_shared_core_owner提交最小接口方案后，root复核J1-CONTRACT-1并授予此前登记的精确产品/测试租约，不新增文件范围。旧TRS float签名和数值路径不改；新完整状态从作者及键直接double计算；旧外部V4资源继续legacy-only，新typed数据失败不得降级。
- 实施者已实际保存15既有文件前像/hash和8拟新增路径的不存在证明：E:/CodexTemp/mini3d-exec008-20261011-a67f03c2/preimage-manifest.json；UTF8无BOM，SceneViewModel.h既有混合行尾保留。root持CMake/构建/GPU/正式记录租约，双方不并改同文件。
- root新隔离验证目录：E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/product-core-20261011-96433832。路径祖先无reparse、E为本地NTFS、开工余量82665398272bytes；Prepare保存两CMake及两日志前像，脚本PS parser实际通过。Configure实际exit0，回执runs/20261011-130212483-Configure-c7858909/result.json；使用固定Qt6.8.3/MSVC、实际pwsh7.6.5，VCPKG_MANIFEST_INSTALL=OFF复用已安装依赖，不重新下载或写共享安装树。
- 所有子进程隐藏，TEMP/TMP/TMPDIR固定独占E盘；测试保持EXCLUDE_FROM_ALL，只显式构建所需目标。当前仅配置完成，产品尚未编译/验收，009/010未Ready；不将配置成功称共享核心完成。Luna只读取证最小测试入口，不写产品或运行测试。
- 请求配置沿用Sol6.1/max普通和Luna5.6/max；provider最终路由未独立核验，不声称fast。AUTH-DATA及本地OCIO/同步输出批准继续按既定票使用；没有新后台作业、系统/安全边界、Git提交推送或发布改动。撤回仅使用本票前像/精确逆补丁并核对后续重叠，两正式日志只追加撤回。

### EXEC-008 首批接口审查与来源合同修订

- 独立Sol定向审八个首批类型文件，发现未知ResourceIdentity.kind、空ActionSlot的未知ownerKind两P2。实施者已定向补校验，专家重读前后hash稳定确认代码补足；断言及编译待本票交接，不提前登记产品通过。
- 另一个真实P2：Scene复制/未来V5重开保留UUID，API contentRevision仅在各live文档内单调；同UUID/同局部revision不能证明作者同源。专家确认最小revisionDomain补足：authorContext记原instanceId/documentId且非空，live与outer liveHandle匹配，frozen继承原context而outer仅FrozenHandle。该域为出处元数据，不增加活查询或权限，独立evaluation session不参与作者域。
- root显式修订为J1-CONTRACT-2，SHA578F22C01C59DB1AC7A21B62480283D362FFAEB9375FBD004A20C518716525D6，仅补作者revision域/保守同源语义和修订记录，数值/图像门、容量、授权及文件租约不变。原v1字节保存于product-core-20261011-96433832/contract-v1.md，SHA与007冻结回执相同，不改旧审查历史。owner获同一租约内实现该补足及四项断言；008仍Writing。
- root CheckRecords实际exit0，核两日志完整开工前像前缀、合同UTF8无BOM/LF及表列；隐藏diff --check实际exit0，仅既有AGENTS行尾warning。首次新增BOM检查误用文化敏感StartsWith，零宽BOM被忽略导致假阳性；实读首六字节23204A3120E9确认无BOM，改为精确EFBBBF字节检查后唯一一次针对性重试通过，未转码产品/文档。
- Sun字段改用明确directionToLight表达BRDF toward-light +Z，并记录传播方向局部-Z；实际double→float消费继续通过真实prepare的几何/设备门，纯core结构不冒已画图。专家快取样前置时间条件已交owner补注释及invalid time断言；真实evaluateSceneState先校验source.frameTime再取样。没有运行新产品测试/渲染，009/010未Ready，产品Verified仍0。

### EXEC-008 首批编译与候选作者接线租约

- 首轮BuildUnit实际exit1，runs/20261011-131854617-BuildUnit-b7f15a33保留C2512/C2440完整错误；根因是StableId带默认实参的explicit构造器不能满足聚合省略初始化。实施者拆成默认构造器与explicit数值构造器，保留零哨兵并增加默认/数值/不同tag静态断言。唯一针对性修正后BuildUnit实际exit0，runs/20261011-132103984-BuildUnit-c49043a3/result.json；没有盲目重试或改旧TRS数值路径。
- 首批冻结状态TestUnit [animation]实际exit0，runs/20261011-132451284-TestUnit-a83914b4/result.json，63用例/64736断言；[scene],[collections]实际exit0，runs/20261011-132530593-TestUnit-28ae4241/result.json，60用例/67722断言。随后新增候选只读入口及独审Sun几何互斥补修需重建重验，不把这些成绩冒用最终008通过。
- 构建结束后精确解冻Scene.h，追加PreparedSubtree::find(EntityId) const仅访问已验证候选expected_，无作者写入/新ID；实施者保存Scene.before-candidate-find.h并重新冻结。独审另发现旧几何门漏Sun互斥，交同owner在Scene.cpp既有四处门补拒绝并在AnimationTests.cpp加正式数据/水位不变回归；008仍Writing。
- 新增精确租约给同一j1_shared_core_owner：src/editor/SceneViewModel.cpp仅commitEntityUpdate的prepare lambda传递target.surface，以及setLighting的history prepare/staging传递候选globalLighting；先捕获该cpp前像/hash/行尾。原因是纯共享旧完整state不能给实际作者变化冒贴新revision。原SceneAnimationSession/InstalledPose租约负责候选重建，rename等不变设备内容可保留只读共享；不改API实现/schema/serializer/版本，不虚构009新Sun/材质命令已接线。
- 本轮用户本地OCIO接入及同应用线程PNG/FLOAT32 EXR专项批准沿用010/014边界；没有后台线程/worker、持久作业、安全放宽、系统安装、Git提交推送或发布。Sol6.1/max普通请求沿用，provider最终路由仍未独立核验，不声称fast。回滚使用各票精确前像/逆补丁保护后续内容，两正式日志仅追加撤回。

### EXEC-008 Sun互斥真实失败与修复验证

- 先冻结最小回归，BuildUnit-56e547f1实际exit0；TestUnit-80a25da0的[sun-geometry] actual exit42（外层脚本exit1），1用例/5断言中2失败：prepareEditableGeometry错误地返回候选，先前合法私有快照在节点变Sun后installGeometry错误地返回true。完整日志保留，不重复未修测试。
- 同owner仅在Scene.cpp四个已有camera/light几何门补sun拒绝：两个prepareTransformedEditableGeometry重载、prepareEditableGeometryContent、installGeometry。冻结Scene.cpp SHAD9FEA3103D23E7B9E3E648E11FE2E300F43F2E33321BBBE10EBD3AB12F811BE9，AnimationTests.cpp SHA42D45029893BD9DA9227B95BD1F3DD9DBF4ED8CF89E5DFD42813D63FFB340C9E；静态编码/租约检查由owner报告通过，独立修补复核待交接。
- 修复后BuildUnit-416f687e实际exit0，19项受影响来源前后hash无drift，输出exe SHAAD103274E7CFC51A2E779AA2391E4C3A26B98BAEDC42F3BD3BF135BB46714208。TestUnit-e0cedc6c实际exit0，OR组[animation],[scene],[collections],[editable-scene],[subdivision-scene]共121用例/77580断言全部通过，覆盖Sun正式内容/水位不变及下一meshID不被消耗；测试exe指纹与本轮构建一致。
- root隔离run-core-gate.ps1新增精确受影响来源前后hash和测试/产物exe指纹；parser及实际BuildUnit/TestUnit调用通过，旧回执不修改。预查010依赖时发现自有配置目录没有CMakeLists.txt，只读Get-Content报缺失；改读实际存在的manifest/runner定位其复用CLI，不再猜路径、不运行旧归档。本票仍Writing，第二批接线未编译/审查，未关闭008或释放009/010。

### EXEC-008 Sun独审关闭与首轮编辑器构建失败

- 独立Sol已亲核四处Sun几何拒绝均在运算、预留及写入之前，回归覆盖正式内容、水位及下一meshID不变；该P2关闭，不代表008整体完成。PreparedSubtree::find仅访问私有已验证候选，独审通过。
- 第二批七文件在原owner完成候选surface/globalLighting接线后冻结，root首次BuildEditor实际exit1，回执runs/20261011-134136275-BuildEditor-82fef605/result.json。真实错误为mini3d_assets中的AnimationProperty.h:62 C2208；该行ActionDefinition::slots与Qt slots关键字宏冲突，纯core编译不含Qt所以此前未触发。
- 已交回原owner精确修补新字段和必要引用，不全局undef Qt宏或修改Qt编译约定；修后重新冻结、构建和定向回归。旧失败日志保留，不原样重试。独立审查继续只读检查来源、候选真实作者值、提交绑定、draft取消及资源coverage；尚无Editor通过结论。
- 本轮授权回复覆盖固定OCIO2.5及最小Standard本地接入与同步PNG/FLOAT32 EXR输出，保持010/014依赖门，不扩为后台线程/持久作业/安全放宽/发行。E盘当前余量81644048384bytes，实际pwsh7.6.5；008仍Writing，009/010未Ready，无Git提交推送或发布。

### EXEC-008 编辑器运行依赖诊断及复制版本P2复现

- Qt宏修补后BuildEditor-b6dfe0d6实际exit0，24项受影响来源前后一致，exe SHA3F0FC4CEE23736A78A1FB9F98C1DDCEA03F7D2A4703949351A86FE5CF02C280B。独审核完最后外部guard后的来源/会话复核、提交后绑定和通知顺序；仅初始preview复制完整几何版本遗漏未关闭。
- TestEditor-23f7c464实际-1073741515/0xC0000135，未执行断言。只读dumpbin递归核实际PE依赖，112项/10模块在诊断搜索目录均存在，不能由此声称子进程loader成功。改变策略为本票测试目录部署9个实际非系统DLL，逐项来源/目标SHA相同，不改系统PATH。随后TestEditor-4c074adf实际exit3且stdout/stderr空，未签回归；显式固定本测试子进程QT_PLUGIN_PATH及platforms路径并强制Qt stderr后，程序真实进入Catch2。以上旧失败及runtime诊断/部署原始回执全部保留在runs。
- TestEditor-5fb5f3a4仅运行既有复制用例，actual exit42：1用例/11断言，1失败，完整geometry stage.contentVersion=1而正式PoseGeometryEntry.evaluationRevision=2。该结果与独审静态路径一致；不重复未修用例，不将已编译当通过。
- 扩展同一j1_shared_core_owner精确租约：src/editor/SceneAnimationEditing.cpp仅preflightSubtreeAnimation构造pendingAnimationReplay_后、geometryToBind前调用prepareSceneGeometryBinding；tests/AnimationSessionTests.cpp仅同一复制用例增加保留旧state/stage不变断言。先保存各文件前像/编码/行尾，准备期预分配、提交期不新分配，不改API/schema/持久化/版本或其他函数。root将该cpp加入构建来源指纹范围，修后冻结再定向构建/验证。

### EXEC-008 共享核心最终集成验收闭环

- 初始preview复制补一行准备期完整几何预分配，复制用例补旧pose/state/stage不变保护；独立Sol亲核精确前像和冻结SHA，此前P2静态根因关闭。修后BuildEditor-688b052f exit0，原失败复制用例TestEditor-3c9b7e84 exit0，1用例/52断言。
- 扩大回归TestEditor-d216b028 actual42，43用例/12223断言仅external resources用例1510失败。真实合同要求Base编辑也完整预检，typed rationalFps加外部资源身份缺失应在提交前NotReady；错误在新测试预期而非产品。保留失败，只修原用例尾段，新增动画、文档、历史index/count、frame/session/evaluation及ID水位不变断言，未放宽检查或伪造资源。
- 最终BuildEditor-94858583 exit0，TestEditor-cca07066外部资源用例1/27全部通过；TestEditor-b96d22b9的session/installed-pose/history/API无窗口OR组76用例/14956断言全部通过。测试源码SHA35D9C28F7A44246208F465E4B7621CAE04E671BBCF1570028DD5B7500218921A；复制产品SHA8EDDB77C178AB0BDEB792988FFD0D0E3F9E9B48DC3AC42F5320A7F6848313112。
- 最终Unit Build-060da981/Test-ed3b9d70 exit0，121用例/77580断言；GPU Build-63355512/Test-ada045d7 exit0，2用例/272断言，仅旧姿态真实GL离屏回归，不冒新PBR像素。主体BuildApp-d5a68227独立目标mini3d_editor实际exit0，日志无测试目标；没有显示用户窗口。
- root亲读最终核心/适配/提交差异，冻结脚本实际PASS：27来源、8个成功构建/测试回执及当前二进制指纹一致，contract2 SHA保持578F22C01C59DB1AC7A21B62480283D362FFAEB9375FBD004A20C518716525D6。闭环身份为product-core-20261011-96433832/runs/exec008-closure-20261011-141213391.json；失败证据不改，008全部写入及命令已结束。
- 008状态Writing → InReview → Integrated → Verified → Completed，仅关闭共享类型、完整state生产/持有、旧TRS适配及失败原子性出口。新PBR、创作界面、持久化/API、输出及J1作品链仍未验；格式4/API0.2.0不变。009/010可开始就绪登记，尚未授权写入租约；只读接口准备并行，不用纯builder/stub签完整命令或画面通过。
- runtime诊断与本票app-local部署沿用先前记录，Qt插件路径仅测试子进程设置；没有全局PATH/系统安装、新后台线程/持久作业、安全放宽、提交推送或发布。请求Sol6.1/max普通和Luna5.6/max被接受，provider最终路由未独立核验，不称fast。源码前像在mini3d-exec008-20261011-a67f03c2，root构建/日志前像在product-core-20261011-96433832/before；回滚须精确恢复或逆补丁并保护后续内容，两日志只追加撤回。

### EXEC-010 双线渲染切片就绪与精确租约

- 010 Planned → Ready：008已Completed；合同2及原稿hash再次实读一致，固定OCIO2.5/最小Standard本地接入与同步输出授权沿用用户直接批准，不新增后台线程、持久作业、安全放宽、系统安装或发布。渲染owner请求Sol6.1/max普通参数已接受，provider最终路由未独立核验。
- 010唯一owner为j1_pbr_color_owner，仅可写src/renderer_gl/FrozenStateRenderer.h、FrozenStateRenderer.cpp、OcioDisplayTransform.h、OcioDisplayTransform.cpp、tests/OcioFrozenRendererTests.cpp、tests/fixtures/j1-static-render-manifest.json六文件。先保存既存前像或不存在证明及UTF8/行尾，然后Writing；不改现有Renderer/Viewport/InstalledPose/SceneViewModel/任何core、API、serializer、schema或版本，不下分。
- 模块出口为真实EvaluatedSceneState到TopDown FLOAT32 associated Beauty和独立coverage，以及同一固定OCIO2.5 processor的CPU/GPU显示RGB。完整可见Sun集合逐灯加法消费，不增假定灯数上限；GL同线程/所属context，保存恢复实际使用状态，实际对象销毁才归还。首次运行前manifest固定1280×720/DPR1、相机(1,0,6)/-Z/Y-up/FOV45/near.1/far100、cube11/material31/Sun21/camera41和合同三点值；位置ROI[400,250,880,470)、灯/roughness ROI[600,320,680,400)，高光补图为中心(1,0,0)/边长2/Y转15度XY quad。oracle内部mask为唯一最近三角形、barycentric≥.025/距投影边≥3px，不后验挑点。
- 只承诺唯一typed材质槽、无纹理、coverage==1的OPAQUE；多槽/缺槽/纹理/其他coverage明确Unsupported，不白材质回退。该拒绝不计对应后续REN-2A能力完成；011保留全legacy独立路径，含新typed但不能完整消费时不回退基础值。010模块测试真实调用求值器和GL/OCIO，不拿fixture代替011/015作者作品链。
- root单owner持src/renderer_gl/CMakeLists.txt、src/editor/CMakeLists.txt、tests/CMakeLists.txt及仅所需assets/color/mini3d-standard.ocio和正式日志。依赖使用固定OpenColorIO_DIR、EXACT2.5.0、Debug/Release原独立import，不修改系统或把Debug映射Release；自有配置逐字节SHA580836D65890F3226EA7E38B67F0A1F55C4125149B3F311E6E3E7E98B8999927，固定sRGB/Standard/None/0/1。构建/测试/GPU仍root单租约、独占E盘及三TEMP变量；当前仅接口冻结，未运行010产品验证。

- root追加精确本机依赖接入租约CMakeLocalConfig.cmake，仅新增默认OpenColorIO_DIR cache路径，保留用户显式值，不改已有vcpkg/PowerShell/Qt Creator条目；文件被Git忽略，不进入提交或发行清单。精确前像保存product-slices-20261011-3f1dbe27/CMakeLocalConfig.before-ocio.cmake。SDK仍在已批准隔离目录，未复制发行/安装系统，后续清理不得删除仍被本机产品构建使用的SDK；正式跨机器依赖方式和许可留对应后续票。

### EXEC-009 Ready与010实际Writing登记

- 010 Ready → Writing：渲染owner已保存六路径不存在证明，before-images.json SHA3865D272886FB151AF7D984A7C1FA77F9855D6A917C490234257FFFD1C674D6A；首次运行前manifest实际冻结为BE7C46AEEB710E37D1B702FE71ECC17DD82201C2FE6A8DF39F0BE1F53E3B4848。证据目录product-render-010-20261011-6f84a927；仍未配置、编译或执行010产品验收，不能借008历史成绩签收。
- 009 Planned → Ready：依赖008 Completed，合同2 SHA578F22C01C59DB1AC7A21B62480283D362FFAEB9375FBD004A20C518716525D6不变；owner j1_shared_core_owner沿用已接受Sol6.1/max普通请求，provider最终路由未独立核验。精确租约只有src/editor/AnimationEditing.h、AnimationEditing.cpp、tests/AnimationEditingTests.cpp，以及SceneAnimationEditing.cpp中新增include和preflightAnimationEdit时间收集区；该文件预算、prepare/CAS及历史重放不改。owner先保存前像/编码/行尾或不存在证明，再进入Writing，禁止构建、测试、GL/UI、公共文件、正式日志及下分。
- 009服务只持私有候选，经真实replaceAnimation提交到唯一历史；位置X/Y/Z、roughness、Sun辐照度使用既有typed地址及Constant/Linear。候选仅按需私有预留ID，精确替换保留KeyId，no_change/取消/失败不推进水位。共享曲线默认拒绝，显式linked修改才共同变化；共享最后键删除仍拒绝，不自动detach/rebind。单owner删除只清理由本次造成的空定义，不顺手清理其他定义。
- typed时间预检按解析后的PropertyAddress比较before/after，覆盖改变键、新邻接端点、删除及绑定改变时刻，不全扫未变曲线；旧legacy采集规则保持。GUI/API留011/012，typed copy/delete NotReady保持，不能拿legacy numerics证明typed画面已接通。
- root独占CMake登记动画两新源和定向editor测试，010四新源/资源及GPU测试已登记；全部新文件冻结交接前不配置构建。集成三CMake/日志前像为product-slices-20261011-3f1dbe27/root-preimages.json，保留主体/测试分离及所有无关dirty内容；仅在授权本地依赖范围内施工，无后台线程/作业、安全放宽、系统安装、提交推送或发布。

### EXEC-009 Writing及双线验证入口核对

- 009 Ready → Writing：root亲读owner的E:/CodexTemp/mini3d-exec009-20261011-78fd14c7/manifest.json，SHA9F046376192FCC67D397E7969D977876C0EE2A6A619DF9F390D3548BD71D1995；现有SceneAnimationEditing.cpp前像仍为008冻结SHA8EDDB77C178AB0BDEB792988FFD0D0E3F9E9B48DC3AC42F5320A7F6848313112，三个新文件不存在证明，UTF8无BOM/374LF及E盘实际读写/无reparse核验通过。单笔服务每次成功prepare替换候选，不隐式累计批次；失败保留原私有候选须有明确注释/测试，正式状态不变。
- root新run-slices.ps1在product-slices-20261011-3f1dbe27构建树隔离009/010，绑定新源、manifest、config、合同及OCIO/Imath库指纹；Debug/Release使用对应依赖，不映射另一配置。隐藏子进程、三TEMP变量及Qt插件路径只作用本任务，实际编译/运行仍等待owner冻结，不执行或重写008历史冻结脚本。
- 本轮Validate初次发现台账原末尾双空行在此前追加时少一行，实际第一差异仅在原前像最后一字节；精确恢复该空行后Validate实际PASS，原完整prefix与严格UTF8、config/contract保持。隐藏diff --check通过，仅已有AGENTS/SceneViewModel行尾提示未改；E余量79770796032bytes。该记录验证不是产品验收。
- GPU测试增加PRIVATE固定manifest绝对路径，不依赖cwd；010内部program使用raw GLuint而非Qt shader对象，为每次删除、编译失败清理、所属context/thread和fence/对象查询回执提供显式控制，仅新模块内部，不改共享ShaderProgram/GpuMesh。合法r=0高光的FLOAT32相消证据交owner，数学等价稳定表达及GPU oracle仍需真实验证，容差/作者值不变。

### EXEC-009/010 冻结交接、独审与首次真实运行

- 009/010 Writing → InReview：两实施owner已停止写入并交还四/六文件冻结身份。009交接为E:/CodexTemp/mini3d-exec009-20261011-78fd14c7/handoff.json，SHA A384CC57CED0A3AAE64D9EB0DD2343DF7908FD035E6C3CEDC11465C43C1F08F2；010交接为product-render-010-20261011-6f84a927/owner-handoff.md及static-owner-result.json。原合同2、manifest与配置保持冻结，不改008历史验收。
- root新构建树Configure-74040023、BuildGpu-2fbfba55实际exit0；StageRuntime-31a058c5逐项部署9个对应Debug本地DLL并核SHA。首次TestGpu-ff31b99d actual42，8用例/543981断言中6通过2失败，两处均为tests:476 ray-miss背景exact zero；完整stdout、stderr、binary/source/dependency SHA保留在product-slices-20261011-3f1dbe27/runs/20261011-144601475-TestGpu-ff31b99d，不重复同结构盲跑。
- 动画独审确认18用例中15经真实ViewModel、3测真实时间收集器；实现未发现其他可操作阻断，但roughness中点默认Catch::Approx不能证明合同abs≤1e-12，root接受定向收紧断言，待当前BuildEditor退出后修改。动画尚未运行，不提前签收。
- 渲染独审静态发现同depth完全重叠mesh在逐Sun GL_EQUAL加和时重复计光，以及render/present遗漏GL_RASTERIZER_DISCARD会丢绘制却报告成功；GPU失败背景分支尚无坐标，边界假设待实证，不把推断称复现。root分派j1_render_slice_fix先只读准备、待构建释放后仅tests/OcioFrozenRendererTests.cpp补诊断与失败回归；未授权改manifest/相机/ROI/阈值或产品cpp。当前GpuMesh析构只删除私有句柄，RAII顺序疑点未形成实际宿主binding缺陷，不记录为已证bug。
- 用户刚批准的固定OCIO本地接入/同线程输出沿用合同边界；无后台线程/持久作业、权限放宽、系统安装、提交推送或发布。独审及实施请求Sol6.1/max普通被接受，provider最终路由未独立核验，不称fast。两票仍InReview，011/012没有开始写入。

### EXEC-009 动画编辑切片实际验收闭环

- root只修改AnimationEditingTests.cpp：将roughness中点默认Approx替换为明确abs≤1e-12，并删除本轮造成的未使用include；录键helper先保存实际commit结果，再INFO输出真实诊断。原实现三个路径SHA仍与owner冻结一致。
- 首次BuildEditor-d670cd4a和断言修后BuildEditor-b72f7ca4实际exit0；StageRuntime-555f4424为本轮测试/主体目录核18项对应DLL。首次TestEditor-a4fd47a3 actual42，18例17通过，失败在distant checked float用例的第一笔合法键而非远端键。增加诊断后BuildEditor-67ed8e30 exit0，TestEditor-3ff9b82c actual42明确“第1帧对象1错误22”，对应legacy glm::inverse的InvalidWorldInverse：原fixture均匀scale1e20在基线已溢出float逆矩阵中间量。
- 不改产品数值/拒绝域，只改该fixture为均匀scale1e10、待检远端键local x=1e30；基线能消费，新增远端world x≈1e40仍超float。修后BuildEditor-ed3fce2f、TestEditor-5b95a049 actual0，18例/1808断言全部通过，包括真实提交拒绝后文档/作者/历史/session/evaluation/ID不变。TestEditor-fd9cc91d的session/installed-pose/history/API OR组actual0，76例/14956断言通过，无窗口/GL新显示计分。
- root亲读四文件及新增全部测试，最终三个实现SHA保持原值，测试SHA6E907380F2DB2A4609E823BD7A1DC272D70163D6869D2CEB3ACAC732277E52F2；严格UTF8无BOM/LF核验，scope未扩展。009 InReview → Integrated → Verified → Completed，仅关闭单笔命令/属性子帧/精确历史与时间预检模块出口，不签GUI/typed画面/API0.3或持久化完成。010仍InReview，011只读准备，不释放写入。
- 可执行回滚点为mini3d-exec009-20261011-78fd14c7/manifest.json及SceneAnimationEditing.cpp.before；新增三路径移存到独占withdrawn名称，确认后续重叠后精确恢复旧cpp或定向逆补丁。此次测试审查前像另存product-slices-20261011-3f1dbe27/before/AnimationEditingTests.before-review.cpp；两日志只追加撤回。原失败及最终回执全部保留。

### EXEC-010 定向真实失败证据与修复租约

- 测试owner新增诊断/合法重叠及宿主discard查询后冻结交还，测试SHA28167AAB2DA801AB5ED57946F90C50D0502C53AD2EB23254B5C9BA22CF88A290；证据product-render-tests-20261011-a73c4ef2。全场景MaterialSlotId唯一，重叠entity42用槽52仍引用原Material31，实际producer验证通过，不靠非法fixture签计光错误。
- root BuildGpu-f1fb410d actual0，TestGpu-48d28887 actual42：9例5通过4失败。背景两例实际坐标(708,428)，RGBA(0,0,0,1)、coverage1、纯几何边距2.8917e-14px；确证原背景branch遗漏冻结3px边界规则。重叠内点(572,292)actual red0.874159、double oracle0.437079；discard相同内点actual0、oracle0.437079。原manifest/相机/ROI/阈值未变，失败全部保留。
- 010 InReview → Writing（仅审查修复）：j1_render_slice_fix独占FrozenStateRenderer.cpp和OcioFrozenRendererTests.cpp，先精确保存前像/编码，禁止动态/Git/正式日志/下分。root裁决单次forward片元以TBO循环消费完整Sun集合，GL_LESS/无blend选择唯一最近面；每灯两RGBA32F texel，核实际设备TBO容量及原128MiB预算，不设假定灯数。私有texture/buffer的同context RAII及新增绑定保存恢复须实际回归；保留原BRDF/r0等价式、OCIO及作者值。
- 已证背景失败允许对miss一贯应用原3px几何规则，必须只根据固定state/camera/mesh计算，不根据渲染actual决定跳过；原hit barycentric/边距门、采样grid和阈值不变。保存/关闭/恢复RASTERIZER_DISCARD，补重复三角形/非零TBO宿主绑定回归；改变范围先汇报，不以拒绝合法重叠、减少灯数或改manifest获得通过。
- 009已Completed；011只读准备仍不写入。没有新后台线程、安全/系统变化、提交推送或发布；long goal active。

- 009三处测试修正经原独立Sol复核，实读两轮成功运行回执并确认同一二进制、源码无漂移，原P2容差项关闭，无新增阻断，不重开009。011只读定位已交还；root另请Sol专家聚焦Base/typed/全legacy消费与统一来源设计，普通备用沿用户批准，未释放011产品写入租约。完整state coverage在纯legacy也存在，不能仅凭该标签强切新double路径；frameId失败语义需亲查现存resources守卫，不能凭计数位置推断成功发布。

### EXEC-010 修后实际验收闭环

- 修补owner停止写入并交还两文件；root亲读shader、容量/预算、guard、对象释放及oracle差异。单次GL_LESS/深度写/无blend，片元TBO循环消费全部可见Sun；零灯最小dummy，不添加假定灯数上限。原manifest、作者值、BRDF及质量阈值保持。
- BuildGpu-dda9863b actual0；TestGpu-c7c92e2f actual0，9例/1,015,693断言通过，包含合法独立槽重叠及同mesh重复三角形两个SECTION、零灯、实际非零宿主TBO/discard状态、OCIO/HDR/alpha/data负例与错误context拒绝。legacy TestGpu-2ec362c9 actual0，11例/638断言通过。BuildApp-51e7d7f9 actual0，显式mini3d_editor主体目标，不编译测试。回执均sourceDrift=[]；原失败保留，不重新运行旧冻结实验。
- 原独立Sol仅复核本次双文件修补及实际回执：原三项关闭、无新增阻断。root仅纠正tests:419过时注释，最终测试SHA01AF0149AA4D0EEC8EFE672F78ED7CBEFC3846FC855A8C4C52913B5CE8581E44；动态运行仍绑定8A72111119A0F4829912F263551C949D60069CA4A95524A9B68B575EC80D81A4。accept-render-010.ps1实际PASS证明仅此注释变化及六路径严格UTF8无BOM；没有声称注释后的二进制又运行过。Renderer最终SHA C61DBB5D54521A537A000EDEFD8B71B846B73DC39648F92DBA0EDEFDA039E20F保持。
- 010 Writing → InReview → Integrated → Verified → Completed，仅签基础静态PBR和固定OCIO模块出口；原始FLOAT32读回不是ImageFrame/PNG/EXR。主体尚无新消费者接线，自动本地OCIO运行库部署留011；不得称发行包可用或GUI/完整J1已完成。011设计专家仍只读，待精确裁决后才释放单owner租约。格式4/API0.2.0保持；长计划active，无后台线程/作业、安全放宽、系统安装、提交推送或发布。
- 撤回本次修补须先确认后续引用和当前SHA，再按product-render-fix-20261011-ff84a6c1中两before文件恢复或定向逆补丁；完整新增010模块撤回点仍为product-render-010-20261011-6f84a927/before-images.json。正式日志只追加撤回；E盘可重建slices约2GiB、当前余量77507190784bytes，保留诊断证据不清理仍使用SDK。Sol6.1/max普通请求沿用户备用授权，provider最终路由未独立核验，不称fast。

### EXEC-011 设计审计及精确施工准入

- 独立Sol专家普通备用完成只读设计审查：同一InstalledPose分离完整性与LegacyTrsV1/JointJ1消费域，保留旧numerics及已checked sceneNumerics；Base独立baseDisplay但公开旧pose仍空，先分类避免新预算污染旧Base。共用source builder的base-only语义区分同帧不同结果；正式Base缓存须最终来源复核、发布后凭据同步及活动手势拒绝。
- root亲查Base provider、真实selectRay、ScenePoseView及RayCaster直接numerics循环、frame/resources观察守卫，接受最小裁决。说明落docs/animation-rendering-j1-consumers.md，不改合同2/比较函数/原数值门。Joint整体render/present，先清depth/stencil，独立always-visible覆盖层；真实source/stage/camera/backend/color内部回执及失败传播不冒ImageFrame。012、013、014仍另验。
- 011 Planned → Ready → Writing：唯一owner j1_consumer_implementation，Sol6.1/max普通请求已接受、provider路由未独核。精确11产品路径为InstalledPose.h/.cpp、SceneAnimationSession.cpp、SceneViewModel.h、SceneViewModel.cpp仅selectRay、MainWindow.cpp仅显示装配、Renderer.h/.cpp、RayCaster.cpp、ViewportWidget.h/.cpp；四测试仅InstalledPoseTests、AnimationSessionTests、AnimationPoseGpuTests、AnimationObservationTests。前像在E:/CodexTemp/20261011/mini3d-exec011-consumers-5863ac1b/preimage，15路径manifest SHA1B909383F220F037993715EFF946F295A0A63F3E13100BA26D087FFF13006594。owner核严格UTF8/no BOM及原混合行尾、无reparse/E余量/真实复制可写。禁止动态运行、下分及写正式日志/公共构建，跨scope先报。
- root唯一持三CMake/忽略CMakeLocalConfig：固定OpenColorIO2.5及其实际Imath3.2.2配置target按Debug/Release部署到主体和两独立测试目标旁，GLOBAL imported target供兄弟目录真实引用；不映射配置、不安装系统。只完成最小差异/UTF8/历史prefix/diff静态核对，Configure和实际部署待owner整包冻结；不能把静态检查称运行成功。前像在product-slices-20261011-3f1dbe27/before/ocio-deployment-011。
- 独占runner增补全部011路径指纹，不消费施工中变化；旧008/010运行证据不改。root另分派Luna5.6/max只读定位012 serializer/API证据，与011不重写同对象、无额外名额。安全/权限/格式/API保持，long goal active；无提交推送/发布。

- 011租约窄补充：owner发现SceneViewModel.cpp::cursorSelectionCenter仍读旧numerics/live，root实读确认其还被transformPivotPosition作为手柄枢轴使用；setPreviewCamera也直接验证旧numerics。允许同一owner仅切这两处共同数值消费，原事务/guard/历史保持，不改transformPivotPosition或公共const约定。JointBase const查询只读当前来源有效缓存，缺失/stale拒绝而非隐式发布/const_cast或live回退；purelegacy保持。原完整cpp前像已覆盖，新增边界写入消费者说明；没有跨出本票G5或扩大安全/授权。

### EXEC-011 观察环境绑定及012只读设计审计

- root在editor测试增加PRIVATE固定manifest绝对路径，runner将其重新纳入BuildEditor/TestEditor/App指纹；只给TestEditor子进程固定三项Qt缩放变量，运行时DPR/实际尺寸仍由测试断言。ShowWithoutActivating由owner在测试fixture设置，不操控桌面或系统缩放。消费测试bootstrap作者不冒称013 GUI创作。
- 当前011仍Writing，owner报告产品接线及installed/session/GPU定向用例已落地，剩MainWindow三时点真实捕获、隔离参数变化和失败用例及整包静态冻结。root不消费施工中的源，没有开始Configure/动态验收。
- 012仅启动Sol6.1/max普通备用只读专项设计审计，待完整域载入、精确值/ID水位、V4原件保护及API同一服务裁决；不释放serializer/API写入租约。Luna证据中的两处误读已校正：既有FileCompatibility是1/2/3另存4保护基础而非V4→5已测，合同第9节初版/修订SHA是明确历史而非冲突。provider最终路由未独核，不称fast。

- root实际runner Parser/Validate及隐藏diff --check通过，原历史完整前缀与合同/config保持；新增accept-consumers-011.ps1仅语法检查通过，整包15路径SHA验收仍待owner Frozen。检查器绑定原前像manifest、精确租约、UTF8/BOM及LF保护，混合行尾记录供人工diff复核，不称已运行产品检查。


### EXEC-011 冻结、真实消费者验证与正式交付

- owner停止全部写入，15文件整包Frozen：final-files-manifest.json SHA BA070E1C48459A45CC05D8EE8EE5EF5AE5868CF85EC429C770C8A879CC0696E0；原前像manifest SHA1B909383F220F037993715EFF946F295A0A63F3E13100BA26D087FFF13006594。root原accept-consumers检查 actualPASS，回执consumer-handoff-check-8f04ae3252504ea486fbb45d30850d6b.json，不改原冻结记录。
- Configure-c88629c9 actual0。首次BuildGpu-91f2618e actual1，MeshCorner.normal为optional<glm::vec3>，测试裸聚合初始化无法转换；root仅AnimationPoseGpuTests.cpp:398显式glm::vec3，前像before/AnimationPoseGpuTests.before-compile-6e5bd379.cpp，后继SHA98B1887C30385EB2F837B7F8C734181A46D5E1D3DE710FE9E8F15F63071ACC77。修后BuildGpu-c0d82807 actual0；TestGpu-b796efe8 [j1-consumer] actual0，3例/237断言；TestGpu-634eeb9d [gpu]~[j1-render]~[j1-consumer] actual0，实际20例/1,016,331断言，不冒称legacy-only。
- 首次BuildEditor-6c10a008 actual1：Observation182 const候选不能传非const install、285 INFO三元宏优先级。root仅去const/括三元两行，前像before/AnimationObservationTests.before-compile-7d3fb960.cpp，后继SHA3C517C8A678238FA970723CC178D378CC3A9E2BB7B28CA33CD94DAC507D01EB5；不改阈值、作者、算法或无关warning。修后BuildEditor-033f66f1 actual0；TestEditor-964bad94新12例/1456断言全过；TestEditor-4a02fc3c旧session/installed/history/API/observation actual0，88例/15408断言全过。
- BuildApp-faa80025 actual0，显式mini3d_editor不编测试；StageRuntime-1d660090核18项真实本地Debug DLL来源/目标SHA。各运行回执sourceDrift=[]，测试执行前后binary一致；真实GL4.1/RTX3050、窗口尺寸1280×720/DPR1/三时点/原ROI门实际通过，消费bootstrap不替代013空白GUI创作。
- 执行纪律偏差保留：GPU回归UTC07:47:50→07:49:00与首次Editor构建从07:48:00发生重叠，root已向用户披露；二者只作功能证据，不作性能结论。后续先收取明确exit才启动下一构建/GPU/UI。后继静态检查器初次因PowerShell单元素嵌套数组展开而失败，改为具名From/To记录后actualPASS，consumer-final-check-cacbf4d79a7e47f58f8c5f2d0b0f3c38.json证明13原冻结文件+两个测试共3行精确后继修补，严格UTF8/no BOM/原字节行尾保持。
- 独立Sol亲读15路径对原前像、两后继修补及新增/既有GPU和新Editor真实回执，无未闭P0/P1/P2阻断；root继续亲读旧Editor/App/runtime回执完成整合验收。011 InReview → Integrated → Verified → Completed，释放全部共享文件租约。仅签本票消费者接线，ImageFrame/正式PNG/FLOAT32 EXR/GUI作者/持久化仍分别另验；格式4/API0.2.0保持，goal active。
- 原15产品/测试撤回点为E:/CodexTemp/20261011/mini3d-exec011-consumers-5863ac1b/preimage；CMake/本地ignored配置撤回点为slices/before/ocio-deployment-011。先核后续差异与引用，逐文件精确恢复或定向逆补丁；两日志只追加撤回，不重置仓库。证据根仍product-slices-20261011-3f1dbe27/runs，无系统安装/后台线程作业/安全放宽/提交推送发布。请求Sol6.1/max普通备用、provider路由未独核，不称fast。

### EXEC-012 施工补充合同冻结

- Sol专家只读设计裁决经root亲查serializer数据缺口、replaceNodes候选继承/四元数规范化与SceneDocument实际载入路径后落docs/animation-rendering-j1-persistence-api.md，保持合同2 SHA578F22C01C59DB1AC7A21B62480283D362FFAEB9375FBD004A20C518716525D6及数值/图像门不变。
- 同一专家校对新补充合同，root补准备来源/domain/watermarks复核、DTO请求继承/nullable fps/键及曲线身份、Inclusive入口>end/Legacy>=end与取消恢复消歧。最终合同SHA5D0AB50AE9A9E00F7225144E9EBEB5CACD9AAAE961EBCA621A5E59E5D877468A；方案可按P→A→B冻结施工，设计不是产品通过。
- P正式开写前仍须唯一owner精确前像与租约；P/A/B分别验证及独审，全部完成才关闭012。覆盖V5所有uint64规范字符串、精确原作者值、六水位/三个next、旧稿保护、可信slot历史、真实有理时钟和11方法/API0.3统一目录。无新后台作业、权限放宽或发布授权。


### EXEC-012/P 唯一施工准入及必要测试租约扩展

- 补充合同严格UTF8/no BOM/SHA与本轮两日志完整prefix actualPASS，check-docs-012.ps1不消费产品；原合同2保持。root隔离runner增加Unit/Asset显式构建测试、整包Frozen清单/SHA门、实际全部租约源前后指纹，原stderr完整保留文件而成功时仅显示路径；Parser/Validate actualPASS，不代表P产品运行。修改前脚本保存before/run-slices.before-persistence-8be90912.ps1。
- 012/P Ready → Writing，唯一owner j1_persistence_p_implementation。原19路径前像E:/CodexTemp/20261011/mini3d-exec012-p-61597d3f/before-manifest.json SHA066C613638A2B37D941866CC99D75F6BE356D427D21BBD60D8497CC579B75267；root只读备份逐项SHA actualPASS，未消费Writing源码。真实E固定NTFS卷/无reparse/83.9GB余量/pwsh7.6.5、严格UTF8/no BOM核验，SceneViewModel原混合CRLF3694/BareLF870保留。
- 原精确租约：Scene.h/.cpp、SceneSerializer.h/.cpp、NativeScenePreflight.h/.cpp、SceneDocument.cpp/.h、NativeSceneRead.h；SceneViewModel.cpp仅旧稿保护/必要文件发布；tests/SceneTests、SceneSerializerTests、NativeScenePreflightTests、SceneDocumentTests、FileCompatibilityTests、FileCompatibilityEditorTests、SceneAnimationFormatTests、SceneAnimationContentTests、SceneAnimationDocumentTests。root持CMake/正式日志/全部动态验证；不写A/B接口，不启动构建。
- owner定位到旧测试只赋nodes/meshes保存导致新next=1不足，以及伪legacy fixture只改版本号。root批准SceneDocumentData(const Scene&)捕获真实完整作者快照，保留空默认构造、不推断next、不放宽旧数字规则；camera/cursor/glTF资源仍外部原来源。
- 在P/G7必要范围精确增补候选tests/CollectionTests.cpp、EditableDocumentTests.cpp、EditableSceneTests.cpp、EditorCameraTests.cpp、FileApiTests.cpp、GltfImporterTests.cpp、Mirror02Tests.cpp、MirrorTests.cpp、ObjExportEditorTests.cpp、PreparedBatchTests.cpp、PreparedNewSubtreeTests.cpp、PreparedSubtreeTests.cpp、SubdivisionSceneTests.cpp、BatchApiTests.cpp；仅实际writer/伪legacy需要才改，纯decoder不改。先另存supplement-before-manifest，不覆盖原19前像；最终清单仅纳必需增补，禁止无关业务/阈值改动。该补充不扩大数据/权限/功能范围。
- root并行Luna5.6/max只读定位014冻结renderer/色彩与既有文件roots入口，不消费P Writing文件，不实施后置功能。provider路由未独核，无系统安装/后台线程作业/安全放宽/提交推送发布；goal active。


- P施工真实拼写消歧：owner指出合同nodes是C++名称，而原JSON写/读均entities。root亲读P原前像SceneSerializer.cpp:339/371/490/504确认，裁决V5保留entities，在其记录新增materialSlots/sun，不重命名、不新增alias、不改旧路由。authorAssets明确SceneAssetDefinition记录、外asset与内MaterialId同值、pbrMetallicRoughness模型/空textures及规范字符串。
- 正式补充合同修订为J1-PERSISTENCE-API-2，SHA B2591DC2A1AEF5B95870636A1621F9F23635EA04FA65605F77957DC307D1A25A；第1版5D0A完整字节先存slices/before/j1-persistence-api-1-5d0ab50a.md，旧专家结论/历史不改写。rootcheck-docs-012对新SHA/UTF8/noBOM/日志完整prefix actualPASS。该版本仅wire拼写勘误，数值/预算/历史/11方法语义保持；最终产品独审另验，owner收到新SHA才继续对应codec。

- P必要UI租约：owner发现MainWindow.cpp的升级另存默认后缀仍为-v4.m3dscene。root亲读1538行及完整UTF8/noBOM/LF1639、SHA7F1A8EA119659035C33D3FCF2B7052F5F0542B3E5B91917BC2913614776FF078后批准仅-v4→-v5一字符，以及已租EditableDocumentTests对应断言；先独立ui-before-manifest及完整前像，不改原19/候选14清单。root新accept-persistence-p-012.ps1 Parser actualPASS，待整包Frozen才核所有SHA/租约/行尾及UI精确后继，不冒称产品已验收；动态仍串行且未启动。

### EXEC-012/P 冻结验收与实际拒绝缺口

- P owner 停止产品写入，final-manifest.json SHA9B6EC8B208479BF11C797929874F2E5BA017293DDF7A9A629C22A7D16AFF7F65，34项/33实改。root accept-persistence-p-012 actualPASS，逐项SHA/前像/租约/UTF8/BOM/EOL及VM/UI精确单字符通过，回执p-static-c2735c3b886042c2981651d58fc5597f。root亲读本轮全部实改差异，独立Sol核完整原子load/精确值/水位/预算/旧稿保护/失败快照，无新增P0/P1/P2；这不替代动态验收。
- 串行BuildUnit-966b83f3、TestUnit-795461ad [j1-persistence] actual0，11例/221断言；BuildAsset-0f8233cb actual0。TestAsset-0ce44b10 actual2是无匹配标签/零用例，不计通过；亲查目标注册后改实际[assets],[animation-document]，TestAsset-23dca702 actual0，22例21过1专用junction夹具Skipped，980断言通过。BuildEditor-610d9aea actual0；TestEditor-c4268d6d新增2例/501断言actual0。上述回执sourceDrift=[]，均绑定原P整包；没并行构建/GPU/UI。
- TestEditor-72ac3380 actual42，原Batch最终许可拒绝nextEntity2→4、原Import取消next3→7，2例/268断言中2断言失败，完整输出保留。根因是旧prepare成功即预约发生在final guard之前；不是新codec数据丢失，也不允许删counter/弱化unchanged获得通过。single/duplicate同型静态路径已经亲查，必须纳入修补覆盖。
- 012/P Frozen → InReview → Writing（仅拒绝修补），唯一owner j1_provisional_allocation_fix获Scene.h/.cpp、VM四真实提交入口及八现存测试的精确租约；默认Core成功prepare预约语义不变。具名provisional路径首次install才发行，守卫内合法后续操作保留，不做任意水位restore；single/import补push前canInstall。规则落docs/animation-rendering-j1-allocation-publication.md。root停止动态消费Writing源，待后继Frozen再验收；A/B、013/014尚未实施，012不关闭。
- 008–011完成状态不重开；014仅并行只读收敛PNG标签约定，不进行后置施工。未提交发布、未放宽权限/线程；goal active。Sol6.1/max请求被接受，provider路由未独核，专家普通备用沿批准，不称fast。两日志本次追加前像为slices/before/*before-p-refusal-8bfa3c14.md；修补撤回用owner精确dirty前像，日志只追加撤回。

### EXEC-014 PNG独立依赖探针与设计消歧

- 014仍Planned并等待012，不授产品写入。独立Sol只在png-label-probe-8c64e639 scratch修改/构建自建2×2 CPU探针，无窗口/GPU/OCIO，不引用Writing产品源。原两Run实际0xC0000135及PATH纠正失败保留；核SHA后的app-local QtGui/Core新文件部署后Build和Run实际0。消除了启动失败，不把具体缺失DLL写作已证根因。
- Run-bde8ac9c36fa4f24b3d59bcf22c1e3df回执sourceDrift=[]，实际Qt6.8.3；唯一zTXt/keyword/method0、原压缩文本独立解压/Base64/compact JSON、无冲突iCCP/sRGB/gAMA、全部RGBA字节及ICC/矛盾alpha拒绝通过。root亲读实际JSON、stdout及main；该证据不是正式图像文件/作品/EXR通过。
- 输出补充规则落docs/animation-rendering-j1-image-output.md，沿原合同数值门、固定Standard与同线程/明确根/NewOnly边界，不加权限/作业/系统安装。EXR SDK仅静态核依赖，产品configure/run仍未验。文档与两日志本轮前像保存在slices/before/*before-png-profile-471a52e3.md，撤回核引用后定向移存新文档并只追加日志，不reset或覆盖dirty。模型Sol/max请求已接受，provider/fast未独核；goal active。
- root check-docs-012 actualPASS，严格UTF8/no BOM及两日志完整prefix、原两合同SHA保持；隐藏Git diff --check actual0。新说明SHA9336ABB545A92EEF5D6C60EC356720CB383E910BE22252BF1AAFB3FE3A4310B7。探针15.5MiB、可重建、已退出全部命令，写入权已交还root；不把依赖实测变成014产品Completed。

### EXEC-012/P 最终拒绝修补及成功Undo闭环

- owner冻结11文件，原before/final清单SHA分别4FB4DA3B…/E609FF0A…；root亲读全部差异，accept-allocation-successor actualPASS，原P与修补合并37路径清单SHA CD0620F2…，默认Core成功prepare预留语义保留。首次ForCommit安装才发行，firstIssued/idsPublished永久关闭首发门；Undo不降水位，Redo原ID；最终守卫中合法创建/预约/创建后删除/mesh记录精确保留，无整Scene或水位回滚。
- 串行BuildUnit-33a8052a actual0；TestUnit-1f65890b [j1-allocation]10例302断言actual0；BuildEditor-b1ec89c1 actual0；TestEditor-24a2681f同组12例2892断言actual0，原两失败关闭。TestUnit-57925492场景/P回归77例67815断言actual0，其中native-preflight/serialization无匹配不计覆盖；亲查实际标签后TestUnit-7e80a986 [serializer],[cursor-serializer],[native-scene],[file-compatibility]18例565断言actual0。
- TestEditor-9c7e7dff扩大回归actual42：124例122过1失败1junction跳过，唯一失败是成功Import Undo旧预期next12回退4。root仅FileApi成功用例补完整before作者捕获、8个连续ID及完整allocationState断言，并比较除已发行nextEntity外所有完整V5恢复；全部失败/拒绝前缀及后文逐字保持，未弱化unchanged。前像before/FileApiTests.before-success-undo-c8d43071.cpp SHA2C34CC04…，当前SHA1B41EBEA…；accept-success-undo-012 actualPASS，新37路径manifest SHA C567804169316E5497F000882786DB96E0893D00BCCD2706E97C1A31262B9FBF，原清单/失败原样保留。
- 修后BuildEditor-2e397e9e actual0；TestEditor-aa5c6416 actual0，123例24731断言通过，1现存isolated-junction夹具Skipped。BuildAsset-291583de actual0；TestAsset-d04f1b06 actual0，21例980断言通过，1现存junction夹具Skipped。最终测试binary前后SHA一致/sourceDrift=[]；root已收全部exit，无重叠动态/GPU/UI。独立Sol限定源码及真实回执复核无新增P0/P1/P2，两个Skipped不计通过。
- 012/P Writing → Frozen → InReview → Verified → Completed，仅关闭P持久化及此拒绝缺口；012整体仍Writing等待A→B，格式5已落地/API仍0.2，013/014/015另验。root释放P共享文件用于下一A独占租约；EXR仍未运行，之后仅释放独立scratch动态，不消费A Writing产品源。
- 本轮正式文件：修补Scene.h/.cpp、SceneViewModel.cpp四入口及PreparedBatch/PreparedEntity/PreparedSubtree/PreparedNewSubtree/BatchApi/MeshApi/ObjectApi/FileApi八测试；root只补FileApi成功Undo。docs/animation-rendering-j1-allocation-publication.md追加限定验收；progress和本台账只追加。回滚按provisional-17d7ab42精确前像及root成功Undo前像，核后续SHA/引用再逆补丁；日志不回写。本轮日志/说明前像before/*before-p-close-c2517e73.md；新检查器仅scratch，无系统/线程/安全/Git写。Sol6.1/max普通备用请求已接受，provider/fast未独核；goal保持active。

### EXEC-014 EXR独立依赖探针交还

- 独立Sol在mini3d-j1-exr-probe-b22f9cd4 scratch完成Configure→Debug构建/部署/运行→Release构建/部署/运行→Audit。19个原生进程均actual exit0且已退出；两Run核五FLOAT/1×1通道、ZIP、窗口/尺寸/20位值/完整标签及已有目标NewOnly拒绝。各配置6个SDK DLL从目标旁实际加载，sourceDrift/sdkConfigDrift/ownedRunning均为空；动态租约已交还root。
- root亲读Debug/Release result与Audit，不重跑已通过探针。正式输出说明仅末尾追加依赖证据，不改原合同：014仍Planned，正式ImageFrame/OCIO/离屏作品/发布未验。根路径读inventory首次失败已定位为文件位于Audit receipt子目录，改读明确完整路径；没有因此重跑实验。
- 探针约15MiB，E盘证据保留。没有写产品/SDK/系统、下载、GPU/UI或后台作业，许可证静态证据不冒分发认证。Sol6.1/max请求、provider未独核，不称fast。

### EXEC-012/A 精确施工准入

- P共享文件已释放，A Ready→Writing，唯一owner j1_authoring_a_implementation。root逐项核before-manifest.json SHA0302FA839DF44EA4CB8FB2D1304863C5C7F783A4EA9BDDE26FD07F529111A674：18路径中15现存source/before逐字SHA相同，3新增真实absent；前像根E:/CodexTemp/20261011/mini3d-j1-author-a-e17d30bb。UTF8无BOM与两ViewModel混合行尾保持，不全量规范化。
- 独占18路径：Scene.h/.cpp、SceneIdentity.h/.cpp、AnimationEditing.h/.cpp、SceneViewModel.h/.cpp、SceneAnimationEditing.cpp、SceneAnimationApi.cpp、SceneAnimationSession.cpp、api/ApiTypes.h；新增SceneJ1Authoring.cpp；AnimationTests.cpp、AnimationEditingTests.cpp、AnimationSessionTests.cpp；新增PreparedMaterialSlotsTests.cpp和J1AuthoringTests.cpp。SceneViewModel.cpp只改commitEntityCreate的Sun/必要request承接，不动P四入口provisional合同；Session不改011消费者。
- 按J1-PERSISTENCE-API-2实现可信槽历史、材料/Sun单笔业务、结构化typed录键/读取/独立取样和真实有理时钟。B的codec/service/catalog/schema/MCP/version不准提前写；B仅Luna5.6/max只读定位，禁止读取A施工源。root持全部CMake/正式docs/log/动态；仅注册三新增文件，不Configure或构建Writing包。
- A整包Frozen后才串行定向构建/验证及独审，当前产品A未验，API仍0.2，012整体及013/014/015不关闭。AUTH-DATA和本地OCIO/同线程输出沿用户批准边界，无后台线程/作业、权限放宽、系统安装、Git写或发布。模型请求Sol6.1/max/provider未独核，不要求fast；goal active。

### EXEC-014 真实调用者设计审计交还

- 独立Sol专家只读裁决内部同步ImageOutputService：不扩012的11方法；assets ImageDocument不依赖renderer；关闭暂存并独立解码后最终当前guard紧接NewOnly发布。root亲读现有deviceCameraView、ScenePoseView Joint门及Renderer元数据出口，接受任意帧完整state相机适配与真实母版持有方案，不制造Frozen InstalledPose或限制成当前paused帧。
- 发现预览JointPreviewJ1与输出OfflineJ1直接isSameWorkSource必false；不削弱共享比较。冻结/求值前共同builder生产不可变originSource，输出真实Offline来源由其受控派生；015分开核同源origin、派生及真实母版。补充docs/animation-rendering-j1-image-output.md第7节，原合同与前六节保持，014仍Planned等待012。
- root仅准备scratch accept-author-a-012.ps1，parser actualPASS/E盘写入已核；未消费A Writing源、未Configure/build/test。专家已结束并交还，不占用A独审名额。正式说明/两日志前像为slices/before/*before-caller-design-1a7f38c2.md；回滚先核后继，定向移除本节说明，日志只追加撤回，不覆盖用户dirty。无权限/线程/系统/Git变化；Sol6.1/max普通备用请求、provider/fast未独核，goal active。

### EXEC-012/A 冻结、P2修补与真实验收闭环

- A18路径Frozen后root验收。原setName拼写、Qt slots宏、测试const/INFO编译失败均保留并定向修正；原静态检查器误计6处INFO已按真实5处纠正，未弱化产品断言。原清单/差异不覆盖，最新54路径整包SHA B229D269A435D1AB685F1E37EE69238485C892BC5C13413454A4E9D293AE2EF4。
- 独审P2真实链为预览changed作者操作真实超时→pending候选未释放→loop推进session→同帧NoChange误报冲突。owner四处局部失败清理及成功dismiss修补，独审定向静态通过；TestEditor-842e5a71 actual0/1例289断言覆盖材料、槽、Sun和typed。TestEditor-b83bff0e actual0/19例5130断言包括真实有理时钟/完整独立取样/唯一历史/预算和身份；A不代签wire。
- BuildEditor-e932601a actual0；TestEditor-9e0764cf actual0/135例15585断言；TestUnit-d00a1bd9 actual0/124例69358断言；BuildAsset-9f400384 actual0；TestAsset-c2f555cc actual0/21过1Skipped/980断言；TestEditor-2c934c4c actual0/47过1Skipped/16110断言。两junction夹具Skipped不计通过，allocation-publication无匹配不计覆盖，实际j1-allocation另验。所有实际源漂移为空、测试binary前后SHA相同，各组重叠不累加唯一总数。
- root调度偏差：TestUnit-d00a1bd9 yielded后未等结束就启动BuildAsset-9f400384，两CPU步骤短暂重叠；无产品写、GPU/UI或性能采集，不冒串行/性能通过。收取两真实退出后后续恢复串行。前文约81GiB勘误为约81.86GB/76.2GiB，不回写历史。
- A Frozen→InReview→Verified→Completed，精确源码写权已交还，正式能力/限制落docs/animation-rendering-j1-authoring.md；外部仍API0.2，012整体未完成，B和013/014/015尚未验。root准备释放B两套不重叠精确租约，CMake/docs/log/动态仍root。AUTH-DATA及本地OCIO/同线程输出边界不变，无Git/发布/系统/权限/后台写；Sol6.1/max/provider未独核，goal active。

### EXEC-012/B C++及Schema/MCP精确租约

- A闭环回执author-a-close-461b97e2.json SHA DD2129D41FACB280371C17B21D594617DEF730F294815A68A387865FF423D446，root亲核真实源/二进制/8动态回执、UTF8/完整日志前缀/两合同SHA及隐藏diff实际PASS。旧失败保留，A状态不重开。
- B Ready→Writing，最多两个子实施者，无共享可写路径。j1_api_b_cpp_implementation独占10路径：api/EditorApiService.h/.cpp、AnimationApiService.cpp、ApiJsonCodec.h/.cpp、ApiTypes.h仅version/variant；automation/LocalAutomationBridge.cpp仅descriptor/hello版本；AnimationApiTests.cpp、AutomationBridgeTests.cpp及新增J1ApiTests.cpp。根均为src/editor或tests。前像E:/CodexTemp/20261011/mini3d-j1-api-b-a92f541c/before-manifest.json SHA E3B67602B97B37506B58BB835CA2B0DA223FE0D9196663B6CDA40FEB333A9804，9现存1新增。
- j1_api_b_schema_mcp_implementation独占14路径：api/schema/{methods.json,results.schema.json,native-animation.schema.json,bridge.schema.json,新增j1.schema.json}；mcp/src/schema.ts仅whitelist；mcp/package.json/package-lock.json仅根version；mcp/tests/{animation.test.mjs,新增j1.test.mjs,batches.test.mjs,files.test.mjs,stdio.test.mjs,mock-bridge.mjs}，后三旧回归保留，仅必要版本/计数/默认结果fixture更新。前像E:/CodexTemp/20261011/mini3d-j1-b-schema-38cd0b63/before-manifest.json SHA A7DCE3DDF3727540FE2B0E04E502250F318CA3C2DE67CD87C36D2AE8BDDB8E27，12现存2新增。
- root record-b-leases-3b7c191d实际PASS：24路径无交叠、全部dirty前像SHA/UTF8相符，两日志本轮完整前像已捕获；第一次读取schema Before字段遗漏已依据实际JSON修正，未重跑产品。Node只读--version实测24.19.0符合>=24.13<25，未npm/install/升级依赖；后续MCP将在独立E盘副本验收，不写仓库dist。
- 合同仅新增11方法/API0.3.0/wire1/72目录（无observation68），保留旧14动画另验5typed与权限6/8/5。复用唯一提交守卫、规范大ID/binary64、typed2048/legacy256、已知Unsupported真实透传，不新线程/注册表/全局MCP/权限。root持CMake/正式docs/log及全部串行动验；整包Frozen前不消费，014仍等待012。当前B未验，不称API0.3已可用；Sol6.1/max请求provider未独核、无fast要求，goal active。
- root在tests/CMakeLists.txt仅登记J1ApiTests.cpp，register-b-test-81e745b6 actualPASS逐字证明单条注册/UTF8/LF，未改变正常主体与测试分离策略、未Configure或消费Writing包。回滚先核后继，再按slices/before/tests-cmake.before-b-81e745b6.txt定向移除这一行；最终source清单必须包含此root后继。

### EXEC-012/B 嵌套操作结果的账本兼容租约

- C++ owner 在真实桥回归接线时指出原 finishMutation 只从顶层 status/documentRevision 提取已提交版本。root 亲读其不可变 before 与冻结合同：新增 material/assign/typed-key 结果明确含完整 command，不能把遗漏 bridge.committedDocumentRevision 当可接受兼容差异。
- 在已有 AUTH-DATA/API0.3 范围内，仅扩展 LocalAutomationBridge.cpp 的提交元数据读取：result.command 为 object 时将其作为 MutationResult 来源，原顶层旧结果不变；不改原响应/账本 payload、守卫、队列、鉴权、线程或文件权限。同一 C++ owner 持有此路径，原 before 不覆盖，新增差异与静态白名单随 Frozen 包交还。
- 要求真实 committed/replay、no_change 不误记及旧顶层结果回归；当前仍 Writing，未运行动态，不能计通过。独审收到此窄风险面，待 C++ Frozen 才读取。root 两日志完整前像为 slices/before/*before-b-nested-ledger-4ab8cb61.md；精确 SHA 相符，E 盘约82.4GB可用、祖先无 reparse。回滚核后继后逆补丁，仅追加撤回日志，不改写历史。goal active，无 Git/发布/系统变化。

### EXEC-012/B 最终接线、失败诊断与验收闭环

- C++10路径与Schema/MCP14路径均Frozen交还，root亲读实际差异/新增合同和测试；复用共享Explicit作者服务、唯一guard、11新增方法、API0.3/wire1/72目录。finishMutation只增加嵌套command元数据读取，真实commit/replay、no_change及旧顶层结果均已运行。旧14动画case bodies和原权限/线程边界保持；无第二注册表/作者业务。
- 原BuildEditor-1634ec3c编译失败因测试Snapshot将Cursor3D误声明为vec3，root仅换真实完整类型；原TestEditor-b810df1e栈溢出因最终guard中model.newScene继承同guard，root仅改成codec独立document.new请求，保留外层guard/计数/完整快照断言。原TestEditor-9f9256e1及单例31aa7205访问异常稳定复现；CDB自有debuggee栈和独审定位JSON临时对象QJsonValueRef悬空，root仅line398改.value拥有值，原isString/大ID断言保持。初次CDB跳过初始断点导致命令文件晚于AV执行，纠正后取到真实栈；诊断退出0不是测试通过，.ecxr不支持的提示保留。源和二进制无漂移，不改生产创建/Core。
- root三个J1单行修补各保存完整前像/独立接受脚本；原77清单保留。修后BuildEditor-aeab3327 actual0；identity TestEditor-13be881c actual0/1例31断言；新J1/桥 TestEditor-b6ec4cd2 actual0/17例3391断言。旧回归320e2e4e actual0/60过2Skipped/6698断言，其中api/j1-authoring无匹配明确不计覆盖；后按真实batch-api/object-api/j1-author标签回归135fc5a3 actual42/52过1失败，唯一旧catalog预期72==61。root只将BatchApiTests标题及两断言61→72，accept-b-catalog72-f8a61bf2证明三处差异，其余断言不变。
- 最终77路径manifest b-catalog72-final-manifest-f8a61bf2.json SHA40CBD588D0EBAC2509CCC71BF21427817584434027196CF4AD6A9BB3C00F0459。串行BuildEditor-8715061e actual0；TestEditor-dbf7ce74八组真实标签合并actual0/122例120过2特殊磁盘Skipped/20775断言，无空标签。测试前后二进制SHA一致/sourceDrift=[]，root已收全部退出。两Skipped因无现成mapped network drive和可访问FAT/exFAT卷，不能计实盘通过。
- MCP独占E副本Prepare成功，39输入/现存锁定依赖56347473字节；Build-323bd546、Typecheck-e0cc4999、Test-8c2ee468均actual0，56例全过无Skipped。两代官方SDK逐项验证72工具和11新方法、binary64/大ID、CAS/Unsupported/no_change；mock不代签真实Core。MCP消费源与最终整包逐项相同，后继只改C++Batch测试，无需重复MCP。未npm/install/下载/写仓库dist或用户MCP配置。
- 独立Sol亲核最终11C++路径、14MCP文件及实际回执，无未解决P0/P1/P2。root补docs/animation-rendering-j1-api.md能力/使用/限制与证据，j1-authoring.md三处同步后续版本，不改原A验收。012/B Frozen→InReview→Verified→Completed；012/P/A/B全部闭环，共享写入权交还root，仅允许后续013/014按精确租约开工，015和后续族仍未签署。
- root正式改动除B两实施包外仅tests/CMakeLists.txt单条注册、tests/J1ApiTests.cpp三处测试修补、tests/BatchApiTests.cpp三处目录常量；正式docs两文件和本台账/progress。日志完整前像为slices/before/*before-b-close-1f3c0fb5.md；撤回核当前后继/引用后按B原before、root三修补/Batch前像定向逆补丁，新增文件移存唯一withdrawn，日志只追加撤回，不reset或覆盖其他dirty。临时/构建留E盘，约82.5GB可用；无系统/线程/权限/Git/发布变化，Sol6.1/max请求provider未独核，不称fast，goal active。

### EXEC-013/014 独立产品租约开工

- 012/B close-1f3c0fb5实际PASS：最终77源/实际C++及MCP回执、39MCP消费源、两正式合同、严格UTF8和两日志完整前缀均核验。013/014前置满足，两票Ready→Writing；015仍等待二票实际验收。
- 013唯一owner j1_gui013_implementation，精确6路径：AppearanceInspector.h/.cpp、AnimationTimeline.h/.cpp、MainWindow.cpp和新增tests/J1GuiAuthoringTests.cpp。根E:/CodexTemp/20261011/mini3d-j1-gui-013-88369ed4/before-manifest.json SHA88B677979AF0395184EC2E9D81C0D7CCA0B9BF5B37EE2D48C84294DC5A6385C3，5现存/1新增。仅最小中文PBR/Sun作者控件、typed录键/读键、显式创建接线及真实QWidget验收；不改MainWindow的API/automation include或Viewport区域。共享VM/ApiTypes/InstalledPose读取开写前read-snapshots，不消费014 Writing源。
- 014唯一owner j1_output014_implementation，21路径：SceneViewModel.h、SceneAnimationSession.cpp、api/EditorApiService.h/.cpp；InstalledPose.h/.cpp、ViewportWidget.h/.cpp、Renderer.h/.cpp；新增renderer_gl/ImageFrame.h/.cpp、ImageFrameOutput.h/.cpp、assets/ImageDocument.h/.cpp、editor/output/ImageOutputService.h/.cpp及tests/ImageFrameOutputTests.cpp、ImageDocumentTests.cpp、ImageOutputServiceTests.cpp。根E:/CodexTemp/20261011/mini3d-j1-output-014-c0b482e8/before-manifest.json SHA6FE53373D668CDE99EBF12BB3A558A40D228A3FD7B89D87EFDBE8C54B32FAEAD，10现存/11新增。仅冻结/真实母版/同线程固定PNG及五FLOAT EXR/明确root下NewOnly；沿既有第7节裁决，不扩render wire/协议/作业/线程/安全边界，不改FilePathPolicy/ObjDocument/SceneIdentity。
- root prepare-013-014-leases-283061ac actualPASS，27路径无重叠，完整dirty前像/SHA/UTF8/BOM/EOL与013共享快照均在014 Writing前捕获；E盘可写/空间/祖先已核。最多二实施子代理，均Sol6.1/max普通配置请求被接受，provider未独核，无fast要求。实施者无动态/Git/正式docs权；root独占所有CMake/依赖注册/日志及构建/CPU/GPU/UI串行验证，Frozen前不消费源。
- 本轮正式改动仅本台账/progress追加准入；前像slices/before/*before-013014-lease-283061ac.md。撤回按两个owner前像核后继后定向逆补丁，新增移存独占withdrawn，日志只追加撤回。无系统/提交推送/发布；用户批准同线程输出与文件root边界继续保持，goal active。
- root确认现有UI库已登记Inspector/Timeline，只在tests/CMakeLists.txt增加J1GuiAuthoringTests.cpp一条。check-013-registration-cde6029a actualPASS，精确单行/UTF8/LF保持，未Configure/build或消费Writing包；主体与测试分离不变。完整前像slices/before/tests-cmake.before-013-cde6029a.txt SHACC6762017B6EDCE4BAF73E3160D56DBF927F01AA66D68AD46569A973194802D9；回滚核后继再定向删此登记，日志仅追加。
- 014 owner确认三测试Target和PNG独立CRC/zTXt需要zlib。root只读SDK头核zlib1.3.1及libz_st/libz_st_d静态库；不同于六项EXR动态依赖，不把旧PNG探针Qt-only当zlib ABI证据。按用户本地输出范围注册OpenEXR3.4.10/openjph0.25.2/ZLIB1.3.1 EXACT、各配置真实库与对应目标旁DLL部署；不改vcpkg清单/系统/全局PATH，不下载。root五CMake路径完整前像清单SHAE35F00C6950DB05F1ABDDB75BF630CE4D76764593A5D2E788C04542987F8CA1E，当前后继root-014-dependencies-final-v2-05e42d3c.json SHAFD6FA767049437984666D2C6A76710C53A525D0D3C4EAA23F50A032453FFEB00。
- root亲读五路径实际增量，UTF8/noBOM/LF和SDK头/库SHA静态检查通过；没有Configure或链接Writing包，实际ABI/产品PNG/EXR与DLL加载仍未验。docs/animation-rendering-j1-image-output.md第8节同步本地依赖与未验状态，原合同/探针章节保持。文档前像slices/before/j1-image-output.before-014-dependencies-05e42d3c.md；五CMake回滚按root-014-dependencies-before-05e42d3c.json核当前后继后定向逆补丁，保留此前013测试登记和其它dirty，日志仅追加。

- 014独立解码测试需直接OpenEXR头/import，root只在mini3d_gpu_tests私有链接增加OpenEXR::OpenEXR。check-root-direct-exr-7b1dcae3 actualPASS证明唯一单行差异，五CMake后继root-014-dependencies-final-v3-7b1dcae3.json SHA85132744BD974DAD3C9A5CBDF10EBEDE49412797F27F43184B96273F2984A945；不读取Writing产品、不Configure或运行动态。说明追加独立oracle边界，原历史保持。前像slices/before/tests-cmake.before-014-direct-exr-7b1dcae3.txt及三个*before-direct-exr-7b1dcae3.md，核后继后定向移除此链接即可撤回，日志只追加；无系统/Git/权限变化，goal active。

### EXEC-013 首版冻结与独立审查修补租约

- GUI六路径首版Frozen清单SHA F0B9B2119F879FA0DB645372E9017A7792A11C09DACFBA51801AED242C3157EC；review.diff SHA9B0EE799AC9D2296BADFE31EF1FE9ED141E95FCA4E6D2526E4B2FBB403B2D846，handoff SHA97457F5462A4EDA00DDDAF1BD6A21500ADCC4AA586BA593DE570C9BB427AC417。root亲读完整差异/新测试；代理静态检查与真实SHA相符，但尚未Configure或动态，不能签013。
- 未实施的Sol/max独审发现1P1/3P2：explicit MaterialId的return列表构造不合法；三新J1控件固定位小数舍入合法binary64小值；失败frame定位后录键仍可能落旧帧；测试仅恢复QSettings默认格式而未恢复两scope路径。独审前后六源SHA一致，没有运行动态或读取014 Writing源。
- root接受最小修补，独审结束释放槽后013 Frozen→InReview→NeedsFix→Writing，原owner恢复同六路径独占，无新增产品文件。三新数值字段改g17 QLineEdit完整double输入，不改共享CommitSpinBox/Core/协议；补小值/no_change、非法帧不录旧值、设置路径恢复与256键GUI分页。保留首版前像/报告不覆盖，新Frozen再审及动态。
- 014仍Writing，所有构建/CPU/GPU/UI保持未启动；root不消费Writing源。模型请求gpt-6.1-sol/max普通被接受，provider最终路由未独核，不称fast。记录前像slices/before/*before-gui-review-4c9a368b.md；撤回核后继后定向逆补丁/新文件移存，日志只追加，无系统/Git/权限/后台变化，goal active。

### EXEC-014 首版冻结与独立专家修补租约

- 首版21路径Frozen清单SHA C6C84188E5D7CE47E233BAE801B675272165D8BFAB0AAFB87A20F5EDDE77D3B6；review.diff SHA373A0A6EE9189BFCA527AC328D854E039154F7895F92E24FC56A60C3732A5593，handoff SHA DA42B7A1848C11C90EDDD3EB2C992EC202249BA871ADFA7284FE883DCF2980B5。root接续亲读三新测试、ImageFrame声明、来源producer及发布服务；独立专家前后21源和3证据身份均一致。
- 专家发现1P1/3P2：新write初始validateContext可达直接busyProvider调用，setter重入可销毁callee；已有目标fallback的replacePolicy同类生命周期；公共freeze末次provider重入可重复/回退独立evaluationId；真实viewport测试未禁止激活。关闭后实际文件验证、最后guard紧接NewOnly以及PNG/五FLOAT EXR未发现新增阻断，但未动态，不等于通过。
- 专家只读工作已结束交还；014 Frozen→InReview→NeedsFix→Writing，原owner恢复原21路径修补。两处仅复制当前callable保护生命周期，保持最后当前许可代际；producer增加作用域重入Busy门，测试补捕获存活/既有bytes/递归ID/作者不变及首次show焦点保护。不改FilePathPolicy、作者水位、协议、根或后台线程。013也仍Writing，root不读取或构建Writing包。
- 本轮docs/animation-rendering-j1-image-output.md只追加第9节；本台账与progress只追加状态。前像slices/before/*before-output-review-cc196c23.md；定向撤回须核后继，产品按原014前像及唯一修补前像逆补丁，日志仅追加撤回。固定shell实测7.6.5、E盘82,418,622,464字节可用、祖先无reparse；Sol6.1/max普通备用请求、provider未独核、不称fast。goal active，无构建/测试/GPU/UI或Git/系统/权限变化。

### EXEC-013/014 修补接回、独立复核与串行首批动验

- 013 owner已停止写入，修后六路径manifest SHA74D5AF224F310F53D7D9DDE4BFD35AB836FE45DD9065640A809FBBCA20242172；root亲读修补/新测试并核四证据及六源。原独审定向复核关闭四发现，无新增P0/P1/P2，仅静态，不代签013/015。
- 调度环境变化后list_agents只返回root，原014实施句柄缺失。root没有仅凭timeout重启；亲查磁盘确认修补before完整、四改路径及三CPU回归已写入、无final交接，接回原21路径租约。freeze-recovered-repair-4affc06b actualPASS，17不变/4改、严格UTF8/VM混合EOL、前后SHA均保持；新manifest SHA49418DE35C8299AFFAED25D8945A92D08D33DBE5C520E6CACBDE403A56CAAB7E，repair.diff SHAB7D9718A342863C6994F3E2344F8EE110376577AA9EE177161B1CCC7820BB38B。新独立Sol对四发现和回归只读复核关闭，21源/前像前后无漂移，未代签动态。
- freeze-013014-package actualPASS，102路径manifest SHA09980FEC20328B4665CD4CBDC143E5B4949CBBCB8E4FA50D1056CCEE942E22D4。Configure-65885c4e actual0，固定SDK路径/EXACT zlib1.3.1成立；BuildGpu-29d763a1 actual0；StageRuntime-f1156e5f 18 app-local记录；TestGpu-f381808a [image-output-codec] actual0/5例831断言；BuildAsset-6617dfd6 actual0；TestAsset-d027332b [image-document] actual0/3例68断言。全部真实退出已收，无动态重叠，未启动GPU/UI作品测试。
- BuildEditor-7ad0d6d6 actual1，UI库已编译，七处INFO(ternary)因Catch2流运算优先级失败。root仅给七表达式加括号；初次补丁因遗漏output变量且顺序不符验证失败，文件SHA仍9566A4CE…未变；读取确切行后一次精确纠正成功。accept-info-successor actualPASS逐字证明只有七括号/其余101路径无漂移，隐藏diff --check actual0；102后继SHA F863AF0352C6D5D0E0F15FE688BCA0B5ED640519BA44E513C3F14A5C228D949B，21后继SHA9F429B1AA264827AA7B86514E3EFF7AE8ABFC86F69C4592472DC33C0E45A43B2。旧失败/清单保留，重新BuildEditor尚未收退出，不称通过。
- root本轮产品只改tests/ImageOutputServiceTests.cpp七个日志括号；说明追加第10节，台账/progress只追加。产品前像before/ImageOutputServiceTests.before-info-e0f84f27.cpp，三正式文档前像*before-integrated-checks-31ecb2a5.md；撤回核后继后定向逆补丁，日志只追加。原callback/producer四修补仍按repair-e740bd86/before保存。无系统/线程/权限/Git/发布变化，Sol6.1/max普通请求provider未独核，不称fast；goal active。

### EXEC-013/014 产品实际验收闭环与GUI可见性补证

- 前述修后BuildEditor-6bec11e9已actual0；TestEditor-a60124b9 [image-output-source] actual0/4例255断言。TestEditor-8fe5b5f8 [j1-gui-widget] actual42/7例6过1失败：测试createSunExplicit遗漏name，被commitEntityCreate的既有合法名称规则拒绝。root核真实入口后只加测试name，保留原失败/全部断言。accept-gui-sun-setup actualPASS逐字单行、UTF8/noBOM/LF及其余101路径不变；BuildEditor-94ef4845 actual0，控件2b1843af actual0/7例687断言。
- GL输出99783238 actual0/5例967断言，包括固定尺寸、同帧RGB/alpha/coverage/PNG、五FLOAT EXR、取消/撤权/CAS/视图拒绝和成品保护。整窗41a9f94d actual0/1例256断言，但root真实截图发现动画dock后台，Luna亲核tabify/show与QTest隐藏注入缺口；此成绩不计可见作者闭环。
- root只接回GUI测试：show后raise本窗口内部dock/processEvents，录键与seek前检查visibleRegion；无顶层raise/activate/setFocus。accept-gui-visible-dock实际PASS三精确helper差异/UTF8/LF/101其他源不变；BuildEditor-cd1eee7c actual0；整窗70a6fa45 actual0/1例299断言，root亲视截图确认时间轴实际可见、12.5帧/.5值、新paint及保存重开。设置scope/defaultFormat均恢复，实际新稿/截图/四paint清单保留。
- 回归f813f210 actual0/37例12961断言包括typed编辑、完整消费者和复制后G取消精确Undo/Redo；最终主体bd1a5340 actual0且未编译测试。16实际成功回执均收退出、无源/依赖漂移、测试binary前后SHA一致；本切片没有动态重叠。驱动同步像素传输等性能警告保留，不冒称资源/性能认证。
- 独立Sol原八问题关闭不重开，并只读复核两rootGUI补丁及七INFO括号，无残留可操作项；102源/六GUI前后SHA一致。最终102清单SHA5F55C6B04414A8E710915AABEC19BE917892E3D8753FD08829307D95346BC21D，GUI六SHA9F60B04A322F80BCBBBF2CD706C3B680B1E69BE3F0EAAD7BCEAF7881FE22CC2B。013/014 Frozen→InReview→Verified→Completed，015和J2～J7仍未验，不用两路径结果代签同一GUI作品输出。
- root本轮只修改tests/J1GuiAuthoringTests.cpp名称setup及内部dock可见性验证，新增docs/animation-rendering-j1-gui.md；输出说明追加11节，台账/progress仅追加。完整正式文档前像slices/before/*before-close-013014-64bb26d1.md，两个测试补丁前像同目录。核后继后定向逆补丁，新文档移存独占withdrawn，日志只追加撤回；不覆盖其他dirty。无Git/系统/权限/线程/后台变化，Sol6.1/max普通请求provider未独核，不称fast，goal active。

### EXEC-015 固定联合作品配方与独占验收租约

- 013/014 close-64bb26d1实际PASS：最终102路径、16成功回执/无重叠、三正式文档完整前缀/UTF8和限定隐藏diff检查均成立；close回执SHA445B7C906A859F8E3F4C13FD1B54F770FE027AF99BCADE1648857DCFE4B41CF9。前置已闭环，015 Ready→Writing；J2～J7仍待依赖，不提前签全部动画/渲染。
- root在任何015实际输出前冻结tests/fixtures/j1-joint-author-output-manifest.json SHA3CAC19735108C6C1344729ADB94C4FDA7FEE48924FB17F389DF71BA76C0A76E2。真实作者路径/动态ID角色映射、[1,0,6]相机/45°/clip.1..100、1280×720/DPR1、三参数与24/1/12.5/1、OCIO及原容差全部固定；位置ROI400/250/880/470、灯和roughness ROI600/320/680/400，roughness采用GUI立方体局部Y15°高光斜面补图，至少16个RGB像素变化>1e-4，不失败后改门。
- 唯一实施owner j1_joint015_implementation仅写tests/J1GuiAuthoringTests.cpp，复用真实GUI辅助增加联合用例，不改产品/CMake/固定manifest/其他测试。完整前像E:/CodexTemp/20261011/mini3d-j1-joint-015-bb89c4fe/before/J1GuiAuthoringTests.cpp SHA50BB0F0593B29CF764EA92817D15905D73CA877DE74DCD51D364FE113C079CCF；before-manifest SHA51FE69EDF4EEF5CE64846E16C1A740E5D7ED22210E3D3983FD2F61FAD6207677。prepare-joint015实际PASS核前置、全102源、配方、原网格source SHA、E可写/无reparse/约82.3GB余量及唯一完整before。
- root持正式docs/两日志、固定manifest和全部动态；Writing时不读取/构建测试。实施者只静态及独占E证据，Frozen停止写后再独审/串行动态；不得用两组模块过代签同一作品输出，不伪造跨文档同源。批准边界、无抢焦点窗口和文件根保持，无后台/系统/MCP/Git/发布变化。Sol6.1/max普通配置请求已接受，provider最终路由未独核、不称fast；goal active。

- 015开写前实施者定位PBR baseColor无GUI控件，暂停这一部分而不绕入口；root亲读Inspector38–57/194–207/394及Material默认白确认。仅固定非动画色经GUI已创建/分配后window.apiService()->updateMaterial正式作者命令校准，明确混合路径和GUI缺口；三类动画和所有原GUI闭环仍真实控件，不修改产品或冻结配方。独立Sol专家只读复核该澄清及事前ROI/高光斜面门，不读Writing测试；当前未签015。三文档完整前像在015根root-before-color-clarification-08db4db4；后续验证仅文档/日志/recipe，不能误读Writing源。

### EXEC-015 静态发现关闭、相机 setup 修补与实际禁用字段失败

- 首版联合测试 Frozen SHA BAB92181E76786AEC9EA78510C41121793E0053EFAE8D84A7F1CB97777055BE1；独立 Sol 发现 Preview 输入正式编码器违反现有 Output 守卫。原 owner 仅删除 7 行非必需 preview 编码/文件校验，root accept-joint015-preview-fix-64a70fc1 actualPASS：其余 102 记录不变、配方/母版/正式输出/ROI/阈值保留；独审关闭 P1。测试后继 SHA6FAA7572DAC3B4F2476DC2B1215BB5A3B8A69CEEE9DDD89C192023C88B67FF69，103 清单 SHAF06ECFD5138C9C905FA20089C633AC98043CF836E9D06ECC6E21AE3AD907CD7C。
- 首轮 TestEditor-bbe0dd02 actual42/150 断言149过，停相机 quaternion 精确 identity。root 查真实控件3位小数/setValue/valueChanged与每轴Euler重建，未测具体失败 quaternion，不称完整数值复现。原 owner 仅在 CreateCamera 前新增既有 FrontView QAction 一行；独立 Sol 认可公开 GUI setup，不 resetOrbit/直接改Scene，原位置/旋转/投影断言全部保留。
- accept-joint015-front-camera-414060cd actualPASS：只新增一行、strictUTF8/noBOM/LF、其余102来源及固定配方不变；当前测试 SHA6D39ABBB0981F0EFD1813DBBDE44FA801BA1150B44930A8D8E4E07EFBFCEA1D5，103清单 SHA3E2D9509B392D45A0049BB402757CAE478DA326A76E259C2DCBE285785E21825。
- BuildEditor-4f62e328 actual0；TestEditor-73929a5f actual42/173断言172过，相机断言已通过，停 enterScalarText 的 disabled 字段。没有字段名动态诊断，按执行顺序疑似SunIrradiance但尚未确认禁用根因；未进入输出/ROI/API等价。前后源/依赖无漂移、实际退出已收，无后台构建/GPU/UI。完整回执位于slices/runs/20261011-212051797-BuildEditor-4f62e328/result.json及20261011-212122454-TestEditor-73929a5f/result.json；原失败不覆盖。
- 当前015 Frozen、未Verified/Completed；Luna5.6/max只读定位忙碌/选择刷新规则，root持正式记录与动态，无Writing消费。J2前置调研仅族索引，不把KEY/CUR索引冒完整原子leaf；J2～J7未施工。本轮授权恢复固定OCIO/同步输出原边界，不新增线程/worker/权限/Git/系统/发布；配置请求已接受，provider最终路由未独核，不称fast，goal active。
- 三正式文档前像已保存015根root-resume-293ac406；root-resume-evidence-293ac406 Prepare actualPASS，E剩余约82.31GB、安全祖先及实际写入核验。撤回需核当前后继后，对验收文档按前像定向逆补丁；台账/progress只追加撤回。测试修补按各implementation-fix/frontview-fix完整前像定向逆补丁，保留其余dirty和全部失败证据。

### EXEC-015 相机作者阶段与动画tab修补，联合验收正式闭环

- 字段禁用根因确认为相机预览的既有camera_preview忙碌门，非Sun选择失败。原owner仅经既有ToggleCameraPreview切换作者/捕获阶段，核目标与选区；静态独审通过。后继BB9BCC26…实际跨过GUI作者/历史/V5/API等价及第一组PNG/EXR；TestEditor-1351b5a3 actual42/1050断言1049过，停捕获恢复后时间轴visibleRegion为空。原失败/首组输出保留，不解除产品守卫或放松可见性门。
- 仅新增内部AnimationDock.raise与可见区域断言三行；原owner停写，独立Sol精确删除三行恢复BB9BCC前像，余字节及013/配方无漂移。root亲读两修补差异；root-dock-flow-successor actualPASS，103来源清单SHA81883E863DB089A7A75197F8BA348628DF96F35E9E19F14BE6612ABDF231E980，最终测试SHAB704F01A2C036F88657170C8041E94BF244D3B5E9A5FF1681D8B696A6FC35A3A，其它102不变/strictUTF8/noBOM/LF。
- BuildEditor-44e4a19d actual0；joint-77613fbd actual0/1例3641断言；原013及输出回归45d6e221 actual0/17例2208断言；主体54dce5a2 actual0未编译测试。四个实际退出已收，串行/无来源依赖漂移/测试binary前后SHA一致。回执目录slices/runs/20261011-215302420-BuildEditor-44e4a19d、215339214-TestEditor-77613fbd、215531337-TestEditor-45d6e221、215640340-BuildApp-54dce5a2。
- joint产物LEOVON：十对PNG/EXR的30比较母版RGB/alpha/coverage错配0、PNGmaxbyte0；四同帧正式组及三个固定ROI成立，位置49928、Sun6400、roughness6400个像素>1e-4，>=16/原ROI不变。root亲视时间轴12.5/.5、固定相机、重开及roughness控制图；独立Sol只读复核回执/身份/角色/保存/ROI通过，未重解码或亲視。GUI与API分别真实独立ID/UUID/handle，语义一致，不伪造跨文档同源；八个正式输出FrozenHandle独立。取消guard2/四非法回执无新结果、暂存或旧成品改写。
- EXEC-015 Frozen→InReview→Verified→Completed；仅关闭J1最小混合作者作品链，baseColor为已声明公开API校准。全部J-IT、高级渲染、J2～J7与资源认证仍未签。Luna只读准备J2叶子/接口证据，未提前施工。沿用批准AUTH-DATA/固定OCIO/同步输出/不抢焦点窗口与明确根，无新后台、线程、权限、系统、MCP配置或Git发布变化；Sol6.1/max普通请求已接受，provider最终路由未独核、不称fast。
- 本轮正式记录只追加；完整前像015根root-close-joint-74f90a21，Prepare actualPASS核103来源/E存储。产品修补前像implementation-preview-phase-fix-0c240a72及implementation-animation-tab-fix-a00b26ff，需核后继后用各diff定向逆补丁撤回，不覆盖其他dirty；台账/progress仅追加撤回。追加历史、UTF8与四动态证据由同名root-close脚本Verify检查，goal保持active。

