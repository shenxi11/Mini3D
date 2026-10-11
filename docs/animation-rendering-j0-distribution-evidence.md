# J0 后端与色彩资产发行证据

2026-10-11 · 来源核验，不是法律认证或产品许可证变更。

## 结论

现有证据能确认实际使用的后端、OCIO库及配置来源，但还不足以签发行通过。不得把许可文件缺失理解成“资源没有许可”，也不得把库级BSD条款直接当成配置/LUT资产的授权。固定Blender源码的根COPYING存在GPL声明，本轮没有排除其可能适用；具体资产覆盖、义务及产品分发路线仍需有明确映射和专项判断。

因此EXEC-005/006保持Writing、EXEC-007未Ready，产品Verified=0。本文件不选择产品许可证、不安装或打包依赖、不提交推送或发布；已有AUTH-DATA不能替代这些决策和授权。

## 已核的精确来源

| 对象 | 实际证据 | 可以支持的结论 | 不能支持的结论 |
| --- | --- | --- | --- |
| Blender5.2.2 | 固定commit `d13f752e3b9c4f8c261cda552b1021f8bcc0382c`，根COPYING与release/license | Blender整体GPL及第三方许可清单存在 | 自动解决全部第三方资产例外、产品授权或分发义务 |
| OCIO2.5.0库 | 固定DLL SHA及Blender第三方清单BSD-3-Clause分组 | 当前实验库身份及库级条款来源 | 配置、AgX、Filmic、ICC和LUT全部按同一条款分发 |
| 实际config.ocio | 便携版本68686bytes与源码67136bytes，去CRLF后内容一致；源码Git blob匹配官方固定目录 | 实际运行配置可绑定到固定源码 | 仅凭来源链接或运行Standard成功签资产发行通过 |
| Filament1.77.3 | 固定源码/包两份Apache-2.0 LICENSE | 主库许可文本可定位，保留许可/归属/修改声明及适用NOTICE是分发检查项 | 已完整盘点gltfio、Draco等全部依赖及fork发行材料 |
| Mini3D | 根目录一级未找到LICENSE/COPYING/NOTICE文件 | 当前查证范围尚无产品级落点 | 项目无许可、公众可随意再分发或代理可自行选许可证 |

root于2026-10-11实际通过GitHub只读连接器查询[固定色彩目录](https://github.com/blender/blender/tree/d13f752e3b9c4f8c261cda552b1021f8bcc0382c/release/datafiles/colormanagement)，目录仅列config.ocio、filmic、icc、luts。[固定ocio-license.txt路径](https://raw.githubusercontent.com/blender/blender/d13f752e3b9c4f8c261cda552b1021f8bcc0382c/release/datafiles/colormanagement/ocio-license.txt)返回404，原回执保留；只证明该固定路径未取得文件，不重复重试或推广到全部版本/全部可能授权来源。

同次读取[固定根COPYING](https://github.com/blender/blender/blob/d13f752e3b9c4f8c261cda552b1021f8bcc0382c/COPYING)确认GNU GPL总声明。它是必须保留的事实，不能因配置没有独立文件就先断言GPL不适用。配置头引用 `See ocio-license.txt for details.`；外置许可指针与总声明之间的覆盖和例外仍待澄清。

独立Luna另只读取证便携license.md/licenses.json、配置头与[EaryChow/AgX](https://github.com/EaryChow/AgX)/[sobotka/AgX](https://github.com/sobotka/AgX)指定根目录，没有取得点名实际配置/LUT的明确条款；仓库API的license=null只代表检测结果，不证明无许可。root未重新查询两AgX仓库，也不把这些可变默认分支当成固定Blender资产逐字节来源。Luna请求gpt-5.6-luna/max，provider最终路由未独立核验。

## 哈希与实际核验

证据包：`E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/distribution/license-evidence-20261011-4d90e729`。

root的`verify-local-evidence.ps1`实际退出0、状态PASS-SOURCE-EVIDENCE-NOT-DISTRIBUTION；七份本机文件固定SHA验证、官方目录/原404/根COPYING回执检查通过。源码config的Git blob以真实原字节和Git blob头独算为 `B30A227EE8662BFAD897E27FDB06649ACEDF4DFE`，不是调用Git或根据文件名推断。

- 便携config：`C39C242CCFBB6B667B804AFA281652727148874BCD7FFBC01C777341BC597487`。
- 源码config：`F10891FC7D34E78E71B535E3F92B3C612177320F990641283247CB84BB8D62AD`；仅内存CRLF归一比较成立，未转码或改写原文件。
- 便携license.md：`A17C575B02C85FE5C2A8153A57B41A37661F7D09F18B22857185AAECD1504AB1`；licenses.json：`4D7C8EF6824D2DD35B0058D57449F630271E3E4674E2954E8958B712F7EABE9A`，共27许可模板，不是资产到条款清单。
- Blender源码license.md：`CDAC130F03722DFF2953D7DA27035289BFF8533D0BB3029D23E22FFE95A0BF7E`。
- Filament源码LICENSE：`02D70B593DD8E9355DD8E1CA8D13C303BEE5BCAF3065B66FF6E4F534E1E6A2C1`；包LICENSE：`F4FE379E2FB014A378B89F5D3F63BFD3FDEE9D6E514DED6B6723A1762BE65D9E`。
- 实际OpenColorIO_2_5.dll：`55E9907D952E2596FF3D6E4CAE5DB29AA232E8B9B369183CC7036B796D8BCB05`。
- root原三只读GitHub回执：`D65DF418B0712D671D21E6E9B82B611DDF51CC3A87F50EDB75B9C76370A1B5C5`。

本轮没有渲染、构建、安装或资产代码执行，没有修改任何原许可/配置/产品文件；只保存来源回执、核验代码和本文件。严格区分“来源核验通过”与“可以发行”。

## 后续最小闭环

1. 明确本体、内部Cycles worker与Blender Bridge的实际分发构成，保留Native/Bridge区分；由拥有权利的负责人决定产品许可证，不由代理默认指定。
2. 对确实随产品分发的config/ICC/LUT建立固定commit、原字节SHA、权利来源、例外及所需署名/条款/源码材料清单；取得外置文件或其他足以覆盖实际资产的明确依据。根GPL可能覆盖的路径也应纳入判断，不能只寻找一个文件名。
3. J1实际Standard/sRGB processor当前未引用外部LUT，这仅是运行事实，不等于整份config没有权利义务。若采用自有最小OCIO配置作为新候选，需先独立校验数值/负值/HDR/alpha/CPU-GPU一致性并记录路线裁决；不能直接替换现有证据，也不能从后续范围删掉AgX/Filmic。
4. 完整依赖及修改通知核验后，按原预算/最低设备/功能组合门形成候选报告。不得用本文件替代这些尚未通过的门。

撤回方式：停止本包核验脚本即可。正式源文件可在保护后续修改的前提下定向移回本包保留的文档前像，新文档可移至本包作为撤回副本；progress与执行日志仅追加撤回，不删除历史。所有小证据留E盘，不自动删除。

## 后续状态裁决（2026-10-11）

上文“因此005/006保持Writing、007未Ready”是本报告编写时的状态，现由后端完结报告及独立Sol审查明确取代：规定E-RT/E-OFF实验已执行并保留失败/不支持证据，005/006按实验票签收；高级候选能力与发行/依赖认证另列未通过。许可缺口本身不等于实验漏跑，也不再单独阻止007限定基础合同冻结。

自有最小Standard配置已在独占新包完成CPU/GPU/负值/HDR/alpha/data对照及独审，不复制本报告的Blender配置/LUT；SHA580836D65890F3226EA7E38B67F0A1F55C4125149B3F311E6E3E7E98B8999927固定。本轮用户另批准固定OCIO2.5与自有配置本地产品接入、同应用线程PNG/FLOAT32 EXR离屏，不涵盖发行许可、后台worker或安全放宽。原GPL/许可来源与未完成资产义务映射仍保留，产品Verified=0。
