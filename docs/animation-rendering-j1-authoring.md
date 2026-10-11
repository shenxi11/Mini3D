# J1 共享作者服务实施与验收

2026-10-11 · EXEC-012/A 已完成；B 后续接线与验收见 [API 0.3 说明](animation-rendering-j1-api.md)。

## 当前可用范围

共享业务层现已支持创建/完整更新无纹理 PBR 材料、为对象分配单个材质槽、原子创建与更新 Sun，以及 Position XYZ、roughness、Sun irradiance 的 typed 录键/删键/分页读取/独立取样。所有修改进入既有唯一撤销栈，GUI 与 API 后续均复用这些 Explicit 入口，不直接改 Core setter。

首个材质槽 Undo 后仍保留已发行水位，Redo 恢复原槽 ID；单槽换材质保留槽 ID，多槽明确不支持。键替换保留 KeyId，删除返回被删身份，未命中返回 no_change。材料 definitionVersion 创建为 1、参数更新保持原值。已登记但本切片未开放的旋转/缩放写入及 Bezier 明确 UnsupportedOperation，不替换成 Linear。

本节 A 交付时外部仍为 API 0.2；后续 B 已接通材料、Sun 与 typed 属性的 codec/bridge/MCP，统一为 API 0.3.0/wire1。其验收证据见 API 0.3 说明，不能据此宣称 GUI 创作、图片输出或 J1 联合作品已经验收。

## 时钟与独立取样

真实播放使用同一个有理帧率 helper；有理 override 优先于旧整数 fps。新文档 InclusiveFrameStep 的末帧驻留一帧后回绕，从 end 恢复不重启；旧稿 LegacyHalfOpen 保持旧行为。旧全字段 setSettings 同步已有 override 为 fps/1，避免速度更新被覆盖。

typed 取样一次求值完整有效状态，使用独立 Sample 来源，不安装 live pose、不改变当前帧、选择、会话或历史。24→1→12.5→1 与三类参数中点均走真实入口验证；typed 地址数 2048 与 legacy entity sample 256 分别保留，不扩大预算。

## 实际修补与独立审查

独立审查发现一个 P2：PreviewPaused 作者操作在真实超时后可能遗留 pending replay/command；随后合法 loop 变化会使同帧 no_change 误报 RevisionConflict。四处局部作用域清理守卫修复失败路径，成功入栈前解除；四种真实作者变更的回归已通过。

构建保留原始失败：测试误调用不存在的 setName；局部 slots 被 Qt 宏擦除；测试 helper 的 const 与 selection() 不匹配、INFO 三元表达式与流运算符优先级冲突。分别作局部修正，不增加生产 const 接口或改变断言。独审定向复核这些修正未掩盖 P2 流程，未发现新增 P0/P1/P2。

## 验收证据

最终整包来源为 slices/author-review-static-fadf5737dd4641b7893003b3dd0be105/combined-final-manifest.json，SHA256 B229D269A435D1AB685F1E37EE69238485C892BC5C13413454A4E9D293AE2EF4。slices 指 E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/product-slices-20261011-3f1dbe27；旧失败及前后像保留，没有重写。

| 实际操作 | 结果 | slices/runs 回执 |
| --- | --- | --- |
| Editor 重建 | actual exit 0 | 20261011-182125064-BuildEditor-e932601a |
| P2 四场景定向回归 | 1 例、289 断言通过 | 20261011-182235995-TestEditor-842e5a71 |
| 作者服务与真实时钟 | 19 例、5130 断言通过 | 20261011-182237552-TestEditor-b83bff0e |
| 既有动画/历史/对象/批次 | 135 例、15585 断言通过 | 20261011-182256424-TestEditor-9e0764cf |
| Core 场景/动画/分配/持久化 | 124 例、69358 断言通过 | 20261011-182333725-TestUnit-d00a1bd9 |
| Asset 重建 | actual exit 0 | 20261011-182344043-BuildAsset-9f400384 |
| Asset/动画文档 | 21 过、1 skipped；980 断言通过 | 20261011-182429881-TestAsset-c2f555cc |
| 文件/分配/兼容/设备/文档 | 47 过、1 skipped；16110 断言通过 | 20261011-182538968-TestEditor-2c934c4c |

各回执源漂移为空，测试执行前后 binary SHA 一致。135 例过滤器中的 allocation-publication 无匹配，不计覆盖；实际分配回归由 j1-allocation 及对象/批次/文件组覆盖。两项专用 junction 夹具 skipped 均不计通过。不同组有重叠用例，不将数字相加冒充唯一用例总数。

Core 测试 d00a1bd9 与 Asset 构建 9f400384 曾因 root 未等返回的 session 结束而短暂重叠。两者不改产品源码、没有 GPU/UI 或性能采集，实际源及二进制指纹相符；如实保留此执行偏差，不宣称本轮全程串行或性能验收通过。之后动态步骤恢复串行。

## 后续与撤回

EXEC-012/B 后续已接入冻结合同中的 11 方法、API 0.3.0/wire1、权威 catalog/schema 和机械派生 MCP，证据独立记录。013 GUI、014 PNG/FLOAT32 EXR、015 联合作品各自验收，不由 A/B 业务层测试代签。

回滚点是 E:/CodexTemp/20261011/mini3d-j1-author-a-e17d30bb 的 before-manifest.json 与完整 dirty 前像，及 slices 下 slot-name-repair-52764ca1、root-slot-macro-repair-96b014de、root-author-test-repair-28a1fbe9 和作者证据根 preview-release-repair-94bcc681 的后继前像。先核当前 SHA 和后续引用，再对拥有路径作定向逆补丁；新增文件移存唯一 withdrawn 路径。不得 reset、覆盖后来 dirty 或降低运行期已发行身份水位；日志只追加撤回。

本轮没有系统安装、新后台线程/作业、安全边界变更、Git 提交推送或发布。临时和构建证据均在 E 盘。模型请求 Sol 6.1/max；provider 最终路由未独立核验，不称 fast 生效。之前进度中“约81GiB”应为约81.86GB（76.2GiB），仅在此追加勘误，不回写历史。
