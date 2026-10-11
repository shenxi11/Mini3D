# J0 后端实验完结与 J1 路线报告

2026-10-11 · 对应 EXEC-005/006 · 状态：独立完结审查通过，实验票签收；不是候选能力/产品/发行认证。

## 1 结论与签收边界

规定最小样例已实际执行，报告保留成功、失败、不支持及未认证，不要求先把每个高级候选修成全支持。独立Sol已按任务卡复核实验覆盖，补齐维护代价与发行旧状态说明后签收005/006。以原生OpenGL4.1+固定最小Standard/OCIO进入J1合同冻结；Filament和离线两候选继续按后续能力门推进。007仍须其独立设计审查，不是已选定全部高级后端、许可决定或产品验收。

产品源码仍为旧格式4、API/MCP0.2.0，本方案新增产品 Verified=0。没有将实验fixture当GUI作者通路、没有提交推送/发布。旧计划完整范围、149/136功能族、四渲染轨及联合工作流分母不变；Bridge不计Native，Unsupported不计能力分。

## 2 E-RT 的实际覆盖及候选分类

| 项目 | 原生 GL | Filament | 判定用途 |
| --- | --- | --- | --- |
| 基础 HDR/alpha/data | GL4.1真实RGBA32F与独立data附件，有限控制通过 | HDR/alpha真实输出；data MRT明确Unsupported_not_rendered | 不以Native data拼成Filament data能力 |
| 五共同资产及作者更新 | 球阵、glTF normal、alpha fence、多灯、HDRI实际执行，edit/rebuild有新像素与新资源 | 同资产实际执行，方向误差修补后新run通过；原错误保留 | 是有限受控实验，不是全渲染质量/物理校准 |
| TAA+DOF与同帧显示 | TAA/DOF均Unsupported | 两效果真实同时启用，完成raw→一次CPU OCIO→Qt画布 | 已执行，不新增必须实现data才能完结的门 |
| Qt生命周期/覆盖 | 真实非前台visible/exposed/paint、资源重建/销毁 | 同样真实观察 | 不是直接swapchain或桌面像素截屏 |
| 兼容 | 实际GL4.1 core | 关键运行实际GL4.5 | Filament尚不符合本项目GL4.1兼容认证，不作J1必需依赖 |
| associated alpha/效果质量 | 只覆盖冻结控制，不外推全部透明 | 零alpha非零RGB等语义仍未认证 | 不清零原值或关闭关键效果取得通过 |

同帧证据为 `rt/ert-stage-cost-20261011-71ca8f04/runs/20261011-114432525-a3ac4196`：两端共1920完成帧、30渲染状态、10overlay、40真实非前台画布。Filament源182–192同时启用TAA/DOF；555–568将同一完成frame.raw交一次display.convert及canvas.present。未实现的数据附件是能力结果，不是整个流水线没运行。

实时阶段成本已记录首次准备/CPU作者更新/资源重建/完成帧wall/WDDM采样。Native初始化含glFinish；Filament准备及编辑仅API-return，gpuCompletionIncluded=false，不能横比排名。端到端延迟、瞬时峰值、全压力规模、最低设备及产品预算仍须产品阶段实测。

### 已观察的维护面

- 原生GL基础路线沿用现有Qt/GL4.1生命周期与完整affine消费，新增PBR、HDR附件及唯一色彩服务仍需产品实施和回归；不新增上游后端fork。实验代码不是产品代码，也没有两年TCO或工时估算。
- Filament官方预编译包与本机MSVC14.40存在四个STL缺符号，改固定源码构建；源构建诊断、WGL设备选择及等待/事件循环适配均有真实失败记录。隔离fork实际改变三个上游文件：Renderer.cpp两处HDR出口、PostProcessManager.cpp八处透明variant调用、dofMedian.mat透明RGBA/coverage分离。其余景深源码未修改；未来升级须重查这三文件及生成材质包，并重验TAA+DOF/alpha，不能把此次有限修复当稳定上游支持。
- Native Cycles需要固定CPU依赖/LFS物化和standalone前置头兼容；隔离cycles_standalone.cpp另有一行denoise_device初始化补丁。新IR适配、构建锁和loader部署均需维护，不能称完全未修改上游。Blender静态worker减少C++内核接线，却依赖固定Python适配/版本/进程与发行边界，产品中仍只可登记Bridge。两端一般动态消费、GPU设备、分发材料尚未闭合。

以上依据实验报告47–87、135–139行附近的实际构建和源码差异记录；只列已观察维护范围，不由增量构建wall推算公平成本或选择高级主后端。

### 固定最小色彩候选

新自有数学配置不复制Blender配置文本，不引用Look/LUT/ICC；以固定OCIO2.5库执行连续moncurve Linear Rec.709→Standard/sRGB。`ocio-minimal-standard-20261011-efa4923b/run-1791692756166` 的实际CPU/GPU/verifier均exit0；184368 RGB通过预冻门，旧Standard差0，alpha位/data字节保持，两真实GPU错误控制拒绝。GPU实际GL4.1、11draw、glError0，来源/程序/实际DLL路径核验。只有本Standard范围通过，AgX/Filmic/广色域不被删去，也未被认证。

配置SHA `580836D65890F3226EA7E38B67F0A1F55C4125149B3F311E6E3E7E98B8999927`。这是J1基础路线的转换证据，不代表后端原始关联alpha已正确。完整资产来源与发行义务单列在发行证据报告，不能由本候选推导产品许可。

## 3 E-OFF 的实际覆盖及候选分类

| R13样例 | 实际双端覆盖 | 保留限制 |
| --- | --- | --- |
| MR/SSS/闭合厚玻璃 | 固定球阵成功，Blender法线顺序修正前后证据保留 | RGB不严格一致，不认证一般材质质量 |
| 节点组/真位移 | 原生固定配方展开与Blender真实组；true/bump/control真实执行 | 不认证一般节点图、Catmull-Clark/adaptive dicing、位移后Normal |
| UV/Generated/Object及affine | seam、shear、负det和完整矩阵控制真实执行 | 不外推任意变形域、完整切线消费 |
| 实例/曲线/体积 | 共享作者/host Mesh、真实Hair/Curves-POLY、均匀Volume Absorption | 不认证device/BVH复用、Hair BSDF、VDB或所有体积 |
| 子帧/运动 | 线性及扩展运动积分控制已有实际输出/比较 | 不冒完整产品动画生产者 |
| AOV/Crypto/rank/多层 | FLOAT32数据、身份位模式、有限rank及Multiply真实执行 | 低rank缺名保留，不平均ID、不冒一般时间域身份 |
| 取消/进程丢失/fresh replay | 真实协作取消、授权loss、同输入新进程重做已执行 | 不是checkpoint续算，不认证产品Bundle/epoch/发布恢复 |

细节与原失败、独立复算、程序/输入/原图hash见实验报告的对应闭环段。以上不是待补“零执行”项目，后续生产者和全能力验收另有票据。

### 720p资源校准

新 `resource-calibration-20261011-8b712ef4/session-1791693230615-f064191f` 成功完成construct→Native一次→Blender一次→零渲染reader；1280×720/32spp/seed19/CPU2/原bounce及滤镜不变。两原EXR各3686400 FLOAT32 RGBA值有限，Sol独立ZIP解码后与raw逐位差0、复算RGB/alpha及资源统计一致；40保护源匹配，原exe+11DLL同字节副本实际从native-bin加载，四进程自行退出。

Native/Blender启动到观察退出wall11.3277859/9.5530621秒，仅direct-child采样WorkingSet约56.74/195.99MiB、Private committed约48.39/381.35MiB。原readerWorkingSet约270.88MiB单列，不把进程RSS相加称真实物理需求。进程树、去重shared、GPU/VRAM、瞬时峰值及恢复资源基线未认证；这些未知维度不能填零。两端RGBA差异只报告，不给公平性能排名或画质等价认证。

首次Name/name覆盖和次loader失败保留，策略改为固定PE导入闭包同字节部署后成功；没有修改原exe/质量/系统PATH。具体唯一缺DLL仍未证明。归档A196文件、B104文件均已实际冻结；正式旧前缀、编码/parser及隐藏差异检查通过，见本轮progress。

## 4 路线建议与未通过项

1. J1 基础：原生GL4.1，固定scene-linear Linear Rec.709/associated FLOAT32母版与独立data角色，预览只合成一次、执行一次固定Standard OCIO。新PBR/Sun合同采用W/m²与roughness²，不沿用比较器的归一化lux参考尺度，也不自动换算legacy灯。物理映射、roughness端点数值处理及产品预算必须在007事前声明，在010/015真实验收；实验近似GGX不冒完整物理认证。
2. Filament高级实时：保持未选；GL4.1兼容、alpha/data语义与质量缺口未闭合。不得在J1施工时暗中把它变为必需后端或关闭TAA/DOF获取“通过”。后续仍按完整原案处理高级能力。
3. 离线：Blender worker登记Bridge、固定Cycles C++登记Native实验；两者都没有产品作业接入。发行来源有固定证据，但资产条款/例外/分发义务映射尚不完整，不能宣称可发行Native。未选择产品许可证或移除GPL声明。
4. J1固定尺寸输出使用同一原生基础路线和冻结同帧状态，不以外部Blender渲染替代原生作者链。长期worker/持久Bundle/epoch/checkpoint/安全放宽仍须AUTH-JOB/AUTH-SEC专项授权；当前AUTH-DATA不包含这些行为。

## 5 完结与下一步门

未参与作者的独立Sol `j0_backend_closure_review` 已按R13/J1.1复核：规定实验已有执行及归档证据，005/006可按实验完结关闭，不新增data MRT/桌面截图/高级全算法门；NativeGL4.1+自有最小Standard足够进入007设计冻结。两项P2文档收口已落地：新增已观察维护面，发行报告只追加明确取代其旧Writing状态。发行/产品能力缺口保持未通过，不由实验签收取得能力分；本报告独审不代替007新合同审查。

007还须冻结最小typed身份/地址/时间/完整状态、精确文件owner、J1数值/图像门与三个资源profile的准入/保留/释放合同。观察样本不直接变成预算或最低设备认证；未测维度在产品验收中保留明确缺口，不冒全项通过。008依赖007，不抢先创建共享代码骨架。

当前已有AUTH-DATA（稳定ID/typed属性/求值/材质灯作者数据、V4→5另存、API0.2→0.3）以及E盘隔离实验授权，既有wire1/本机同用户管道/默认关闭/明确文件根/应用线程串行不变。专家按用户已接受的Sol6.1/max普通备用；请求参数可记录，provider路由不可独立核验，不声称fast。

## 6 证据与撤回

所有隔离包基于 `E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba`；已归档旧成功/失败不修改、不重渲染或自动清理。正式参考：`animation-rendering-j0-experiments.md`、`animation-rendering-j0-distribution-evidence.md`、J0三合同及`animation-rendering-j1.1.md`。

本报告仅记录与建议，无产品/CMake/系统接入。撤回时将本文件移入本任务证据目录作为撤回副本，执行台账/progress只追加撤回，保留其他已有改动；不提交推送或发布。
