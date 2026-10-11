# J0 联合合同：迁移、作业与资源

2026-10-09 · EXEC-004 · Owner：L1 · 版本 J0-JOINT-DRAFT-1。

本稿统一动画和渲染草案，补齐J-CON-05/06/08。合同文档可以审查；产品施工尚须EXEC-005/006真实实验、EXEC-007冻结和对应授权。它不宣布格式5、API0.3.0、长期作业或新后端已经存在。

## 1 输入与会签结论

依据是J1.1执行主册、同目录J1.0技术附件和两份J0草案。源码基线为`5f5c82ed7a9f361e263d4abe895bbf99b061e3e7`，附件SHA256仍为`BBB4E6AD7186C2B1E1F5B9AE1EB851B78CA4052DE39C0AF008571D208364D73A`。两位Leader已交接文件租约，无后台构建或GPU任务；合同不会改写原稿的完整范围。

| 合同 | 共同结论 | 本票落点 |
| --- | --- | --- |
| J-CON-01 | 文档域+typed owner+固定字段/Socket映射+分量；MaterialID为kind=Material的持久AssetID引用；MaterialSlotID与动作SlotID分开 | 单一身份注册和分配规则，无第二材质分配器；外部uint64为十进制字符串 |
| J-CON-02 | 作者与采样binary64；J1帧域[1,100000]；约分正uint32有理fps，数值1–120；默认24/1 | 共同SourceIdentity，不用运行期DocumentHandle冒充持久DocumentUUID |
| J-CON-03 | binary64完整affine及inverse/normal，刚性设备方向独立；几何stage和资产持有者随同一来源发布 | 旧float仅checked消费，不强拆TRS，不回写作者值 |
| J-CON-04 | 唯一历史、精确no_change、内容/历史与必要会话CAS，取消恢复raw快照 | 所有GUI/API命令复用服务；复制固定ID与取消旋转回归不放松 |
| J-CON-07 | scene-linear Linear Rec.709，FLOAT32母版、associated alpha、独立数据角色、一次OCIO | 配置/LUT/ICC与真实Display/View需实验锁定；不能自造同名AgX或双OETF |

J1位置为scene unit，不自动定义为米；roughness为有限[0,1]，新材质默认1；新SunIrradiance为有限非负的垂直入射辐照度，单位W/m²，默认1。Sun RGB为线性色权重，不能从RGB声称光谱积分或流明换算。旧灯默认0.72F、0–10和旧Lambert材质是legacy独立语义，迁移不自动换算物理参数。

ResultStatus是语义候选而非新增wire错误枚举：Succeeded、NoChange（仅编辑/控制）、InvalidValue/InvalidIdentity、StaleSource、Cancelled、UnsupportedFeature、LimitExceeded/Failed。API继续返回成功/ApiError的明确分支；无效类型映到InvalidArgument，缺引用映到NotFound，过期按实际对象映到StaleDocument/RevisionConflict/StaleEvaluation，取消映到Cancelled，未支持映到UnsupportedOperation，预算映到LimitExceeded，文件/GPU失败保留IoError/RenderFailed等原因。协议目录由唯一owner一次更新，不增加同义第二套错误表。

共同SourceIdentity采用动画草案列出的documentUUID、sourceKind、由kind决定的liveHandle/sourceBundleID、contentRevision、resourceSetIdentity、frameTime、rationalFps、algorithmBuild、evaluationProfile、purpose、authorContextIdentity、evaluationSessionID/evaluationID及仅Draft的draftIdentity。live发布重新核对当前来源；FrozenFormal输出持有自己的完整快照，不因后续编辑改读live。不同私有会话比较同源输入与精确时间，不要求复用evaluationID；同一安装状态的消费者保持完整状态身份。GPU context、view、paint/frameId是消费证据，不能只凭相同source宣称已画新帧。

## 2 J-CON-05：唯一版本与迁移清单

| 对象 | 当前已核对事实 | 候选演进及验收责任 |
| --- | --- | --- |
| scene JSON | 格式4；旧1/2/3兼容；NativeScenePreflight最大64MiB | 格式5候选，编号在施工前再核对；同一serializer严格解码/验证/编码 |
| 动画 | uint32键时间、整数fps、完整XYZ组轨；3000轨、100000键 | 最小Action/typed Slot/标量Curve/Key；binary64时间和原值、有理fps |
| 场景身份与引用 | EntityId及进程AssetManager资源键；后者不是持久资产身份 | DocumentUUID、持久资产/材质、稳定实体槽、typed引用和分配器high-water mark |
| 材质与灯 | 基础色/纹理/双面；legacy surface/方向灯 | legacy记录保留；新原生PBR和Sun字段独立声明，可编辑定义及绑定正式保存 |
| API/SDK/MCP | methods.json与mcp/package.json均0.2.0，wire1 | API0.3.0候选；同一方法目录、schema、服务、SDK/MCP和system.describe真实能力 |
| bulk及大资源 | 当前入口各自按实际限制工作 | compact bulk128MiB、scene JSON256MiB为原稿候选；媒体/模拟缓存独立容器，不塞场景JSON |

当前64MiB与候选256MiB必须分别记录。增大上限本身不能证明旧最大作品迁移成功。新scene输出按声明精度最短roundtrip：基础binary32不先升成double再长串输出，binary64原作者值最多17位有效数字；Constant/Linear不扩写无用Bezier柄。载入先核算预算，有预算地构造，不同时持有多个全量DOM/轨道/历史副本。精确字节计数和内存峰值是最终迁移证据，不能用“超限安全拒绝”代替可另存证明。（原稿B0386–B0404、B0616。）

V4迁移采用单一受控路径：

1. 严格解码到私有旧候选，保留原binary32 TRS和binary64键值、raw连续Euler、signed scale；精确validate/adopt不再次normalize基础quaternion。
2. 在新文档候选中建立持久UUID、资产/材质/槽映射。旧外部glTF索引作为来源，不当持久MaterialID；重开新稿恢复已保存映射。旧引用缺失不制造白材质掩盖错误。
3. 非空旧动画进入迁移SceneTake动作及各实体Slot，XYZ展开为三条标量曲线；整数帧精确转binary64，fps n转n/1。无rotation轨时保留基础quaternion；新部分轴默认另明确持久作者域。
4. 原3000组轨/100000组键可展开为9000标量曲线/300000标量键；规模预算按新计量声明，不能因为单位变了拒绝原合法容量。单轨10000组键展开后仍需明确每标量曲线预算。
5. 保留legacy半开播放循环，新作品用明确的inclusive frame-step策略；离线帧集合有独立start/end/step，不借播放周期猜测输出帧数。
6. 一次验证完整最终定义和引用，原子安装新内容；首次升级仅Save As到新路径，不默认覆盖V4原稿。非法/过期/取消不发布部分升级。

EXEC-012需把精确字段清单、方法目录、成功/失败schema及schema hash一次集成，API地址查询、作者命令、求值、材质和Sun服务走同一业务实现。旧方法保留已声明语义；不能把0.3.0仅写入版本号而不接消费者。wire1、本机同用户命名管道、默认关闭、拒绝提升运行、应用线程串行、显式文件根和allOf权限规则不在本次被放宽。版本已占用时先裁决，不重用已发行编号。

## 3 J-CON-06：两种Ready与持久来源

| 状态/对象 | 含义和锁 | 取消、失效及恢复 |
| --- | --- | --- |
| 短命令 | 私有完整候选、最后守卫后原子提交 | 提交前接受取消不安装；不可分提交后报告真实结局，不隐式撤销 |
| PreparedEdit.Preparing | 可取消分块准备；不改正式内容/历史/pose | 按时钟/取消检查交回事件循环，真实分片目标4ms需测量 |
| PreparedEdit.Ready | 进程内完整候选token，初始TTL30秒；不等于作业Ready | 绑定内容/历史/必要会话、Draft、参数、allocator代次；过期、来源变化或重启失效 |
| PreparedEdit.Committing/Committed | 独立幂等commit，最后CAS与守卫成立后提交 | status/cancel不能被自己Busy阻断；NoChange不破坏redo/clean或消耗正式ID |
| RenderJob.Preparing | 持久intent与规范参数；冻结并可取消打包，持有文档内容Busy | 包不完整的崩溃报告NonResumable，不从当前文档补成旧任务 |
| RenderJob.Ready | 完整Frozen Bundle及持久资源保留关系已经发布并核验 | 同一持久过程建立资源引用；Ready后释放文档内容Busy，编辑不改变旧Bundle |
| RenderJob.Running | 独立attemptEpoch与暂存目录，仅host有正式发布权 | 接受取消后旧epoch不能再发布；新resume使用新epoch，不重用旧worker结果 |
| Completed/Cancelled/Failed | 清单反映已提交帧与实际执行结果 | 取消保留已提交成品；UI已确认取消与GPU实际释放分开记录 |

PreparedEdit的Stale/Expired/Cancelled/Failed与RenderJob的失败不能合并为一个无来源提示。求值/作业状态不进入文档Undo栈；作者改动才复用唯一历史。（B0096–B0113、B0408–B0444。）

Frozen Bundle必须有版本、bundleID/内容hash、作者数据、稳定绑定、外部资产真实字节或被持久保留的不可变内容对象、资源清单、独立求值语境/初态/历史/检查点、算法及backend build、颜色配置和所有LUT/ICC、质量/设备profile、时间/快门/输出参数、依赖版本、路径目的和完整性信息。J1没有状态/脚本/媒体生产者时明确声明无该类型，不塞空对象冒充完整J6 Bundle。路径+hash或进程共享指针不足以恢复，Ready不要求预先算完全部帧。

发布顺序固定：持久reservation → 本epoch暂存输出 → flush并验证尺寸/通道/有限值/可解码/完整来源/hash → 持久finalized intent → 重新验证epoch及当前读写根/目录/所有权 → no-clobber或明确授权替换 → journal/manifest。finalized intent含Job/epoch/帧、snapshot/settings/build、最终hash、temp/final相对路径和策略。发布后记账前崩溃只能凭既有finalized intent核对并补账；同名外来文件不能临时算hash认领。多帧不是一次原子事务，全部完成后才写序列完成标记。（B0105–B0109、B1128–B1129。）

执行与恢复时重新校验当前权限，不把Bundle当永久授权：start/resume需要scene.read、viewport.observe、file.read、file.write；get需要file.read；readFrame需要file.read和viewport.observe；cancel需要file.read和file.write；Native/MCP两端按allOf执行。根撤销、当前路径变化、资源/脚本/后端不匹配正确拒绝，不读取live替换缺失来源。（B0110。）

J1 EXEC-014仅实现该切片所需的同帧固定尺寸离屏、最小Preparing/取消/清理；不以它关闭跨重启Ready恢复、完整Bundle/epoch或J4发布故障门。EXEC-006的自建隔离worker是实验进程，不改变产品长期线程/作业服务。

## 4 J-CON-08：三个资源profile与实际释放

| profile | 使用范围 | 当前数值与冻结状态 |
| --- | --- | --- |
| BasicAnimation-v0 | 现有核心作者、历史、数值求值 | 原稿候选distinct resident1GiB、job extra512MiB；保留既有3000组轨/100000组键等事实；扩展后重新按标量计量 |
| JointPreview-v0 | Qt、作者/有效状态、纹理/几何缓存、实时后端及显示转换 | 新CPU/GPU/显存和帧时上限待EXEC-005实测，EXEC-007先冻结后验收；不能拿BasicAnimation cap外推 |
| Offline-v0 | host+独立worker、采样、多plane、输出/恢复暂存 | CPU/worker/shared/GPU/磁盘各自实测；并发、输出尺寸和质量必须随profile声明；上限待EXEC-006与007冻结 |

profile最少包含device/driver/backend/build、支持场景规模、尺寸/DPR或独立目标尺寸、samples/质量、effect集合、CPU进程树resident与额外峰值、去重shared bytes、GPU/显存、缓存/暂存及正式输出磁盘、准入/保留和释放条件、单调时钟测量。分阶段定义不是缩减原稿支持规模；质量或兼容范围变化须有裁决和用户明确决定。

本机当前CIM核对为Acer Nitro AN515-45、物理内存16487870464字节；GPU/driver与可用显存需由原型实际设备入口记录，不能把AdapterRAM字段或2026-10-07历史报告当本轮认证。初始隔离下载/构建预算要求E盘至少20GiB余量，开工约89GiB；每次大写入重新检查。实验单重构建/单GPU任务，CPU/GPU重叠耗时不简单相加作墙钟帧时。

取消后只销毁本任务持有的资源；确认GPU fence/上下文/worker退出后才登记已释放，不能把UI取消提示当释放证据。迟到结果携带source/job/epoch/frame/pass/tile/sample身份，host拒绝旧代。诊断记录请求与实际身份、owner/typed地址、stage、错误码、预算及命令退出值，保留失败样本；已显示旧帧明确标旧来源。

## 5 精确施工包与授权边界

| 施工包 | 内容与单一owner | 当前门状态 |
| --- | --- | --- |
| EXEC-005/006隔离实验 | 官方Blender5.2.2、Filament1.77.3及对应Cycles/OCIO，E盘下载/解压/构建；隐藏测试进程、自建资产、GPU串行 | 用户直接批准；不安装系统、不跑资产脚本、不改全局配置、不发布 |
| EXEC-008共享核心 | typed身份/地址、时间、完整有效状态及旧pose受控适配；G1/G2/G5/CMake由单owner，精确文件在007列明 | 等前置实验与独审；核心接口施工授权需绑定最终冻结差异 |
| EXEC-009/010双线 | 独占动画纯核心/编辑与渲染资产/后端文件；共享接线另票 | 等008；不得猜接口抢先改共享文件 |
| EXEC-012迁移及API | 同一serializer格式演进、持久材质/槽/动作、API0.3候选目录/SDK/MCP；应用线程串行和旧权限保持 | AUTH-DATA明确确认、精确schema清单和专家审查后施工；不放宽管道安全 |
| EXEC-014固定离屏 | 独立尺寸/相机/来源、一次OCIO、文件发布和最小取消；不实现长期恢复 | 具体输出/执行边界在007冻结；长期并发不默许 |
| REN-4A及后续状态/脚本 | 持久作业/worker/epoch/恢复、模拟会话及Python信任/OS隔离 | 各自新施工包、AUTH-JOB/AUTH-SEC与专家门；本稿和实验授权不覆盖开放执行 |

共同字段由单owner实现，产品public schema/依赖/锁文件由L1或一次指定集成人处理。任何精确文件名单变化先记录租约；本轮不提交、不推送、不发布、不关闭用户窗口。专家本次普通模式例外已直接批准，不修改长期AGENTS。

## 6 失败状态与必验项

| 输入/故障 | 必须观察的结果 |
| --- | --- |
| V4合法最大轨/键与近64MiB非动画输入 | 成功另存候选格式的精确字节/峰值证明；只拒绝不算迁移通过 |
| 作者近邻double、720度、原非单位quaternion、复制后取消 | declared precision精确往返及Undo/Redo固定ID；不normalize、不借矩阵近似 |
| missing资产、错kind/重复ID、过期content/session | 完整拒绝，无部分作者/历史/IR发布 |
| PreparedEdit过期/重启/提交前取消 | token不可复用，原内容/历史不变；进入原子提交后报告真实结局 |
| Preparing中断、包缺字节或资源保留记录 | NonResumable，不借live补包，不提前Ready |
| Ready后编辑、更换live资产 | 作业继续读原Bundle，实际来源保持旧完整身份 |
| 旧epoch迟到、取消/重启恢复 | 无旧代发布；恢复核对Bundle/build/color/资源及已提交帧 |
| finalized意图前后崩溃、外来同名文件、磁盘满 | 无虚假完成；仅既有finalized可恢复认领，no-clobber与原成品保护 |
| 路径根撤销或输出路径改变 | Native/MCP当前allOf重新拒绝，旧JobID不绕过 |
| HDR>1/负色、零alpha发光、数据plane、TAA+DOF组合 | 保持真实线性/alpha/data语义，一次OCIO；不关效果冒充候选通过 |

文档票的验证是来源、现有代码事实、双方字段一致与Markdown/UTF-8结构检查。产品编译、迁移最大容量、真实像素和故障注入尚未执行。EXEC-007须将实验结论、图像/数值阈值、实际profile、精确文件租约、未认证能力和授权差异合在一个冻结记录；任何未做完的实验仍阻止产品进入Ready。

技术依据：当前`NativeScenePreflight.h:17`的64MiB；`Animation.h`的现有规模与精度；`ApiTypes.h`的DocumentHandle/结果/错误；`methods.json`和`mcp/package.json`的0.2.0。原稿B0096–B0113、B0144、B0386–B0408、B0418–B0444、B0450、B1127–B1129、B1174。完整公式和范围仍以原DOCX为准。
