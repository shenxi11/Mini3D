# J1 API 0.3 与 MCP 接入说明

2026-10-11 · EXEC-012/B 实施与验收

## 可用范围

格式 5、共享作者服务及外部接线现已完成。API 与 MCP 包版本统一为 0.3.0，wire 仍为 1；权威 catalog 共 72 个方法，未接视口观察时 C++ describe 返回 68 个方法。是否可调用仍以运行实例的 system.describe、权限和文件根为准，不能仅凭目录条目推断可用。

本轮只新增 11 个 J1 方法，仍复用 SceneViewModel Explicit 命令和唯一历史栈：

| 业务 | 方法 |
| --- | --- |
| 无纹理 PBR 材料 | material.create、material.get、material.update |
| 单材质槽 | entity.assignMaterial |
| Sun | light.createSun、light.updateSun |
| typed 属性键 | animation.upsertPropertyKeyframe、animation.deletePropertyKeyframe、animation.readPropertyKeyframes |
| 独立属性取样 | animation.sampleProperties |
| 有理帧率 | animation.setRationalSettings |

旧 14 个动画方法保留。没有新增渲染 wire 方法、第二套命令服务、后台线程或持久作业；同用户本机管道、默认关闭、拒绝提升权限、显式文件根和应用线程串行保持不变。

## 调用约定

先通过 document.current 取得当前 DocumentHandle、documentRevision 和持久 documentUuid。写入请求必须携带当前 document 及 expectedDocumentRevision；成功后使用结果里的最新状态继续操作。documentUuid 属于作者地址，不替代运行期文档句柄。

材料必须提供完整 parameters：baseColor、roughness、metallic、coverage；所有分量为有限 binary64，颜色/单位参数按共享值域校验，不隐式转换颜色。Sun 必须提供完整 irradiance/color，irradiance 单位 W/m²。多材质槽操作明确不支持；只有一个槽时换材料保留槽 ID。

typed 地址例：

```json
{
  "documentUuid": "从 document.current 取得的规范非 nil UUID",
  "owner": {"kind": "entity", "entityId": "7"},
  "property": "transform.position",
  "component": "x"
}
```

材料 roughness 使用 owner.kind=material/materialId、property=material.pbr.roughness、component=scalar；Sun 强度使用 entity owner、property=light.sun.irradiance、component=scalar。所有 uint64 身份和版本为规范十进制字符串，包括大于 2^53 的 ID；子帧与作者值保留 binary64，不加 epsilon 合并相邻时间。

录键支持 constant/linear。已登记的旋转/缩放写入或 Bezier 返回 UNSUPPORTED_OPERATION，不自动换成其他插值。linkedEdit 默认 rejectShared，可显式 editLinked；共享曲线语义沿原合同，不在适配器端拆分或修改请求。

新增材料、槽及键写入结果包含完整 command，键结果另含稳定身份。同时间替换保留 KeyId，no_change 不消耗身份、不清 redo；删未命中的键返回 no_change/keyIdentity=null。桥接账本从嵌套 command 读取 committedDocumentRevision，旧顶层结果仍兼容；重放不重复执行。

分页以 afterTime 精确续页，并冻结同一 documentRevision。typed 取样地址上限 2048，旧动画实体取样上限 256 不变。sampleProperties 独立求值，不安装 live pose、不改变游标、会话、选择或历史。

有理帧率设置要求非 null rationalFps={numerator,denominator}，字段支持 uint32；有效速率、约分与帧范围仍由共享服务校验。InclusiveFrameStep 和 LegacyHalfOpen 两种策略保留；设置帧率不移动已有键。旧全字段 setSettings 显式同步已有 rational override，避免旧值掩盖播放速度修改。

完整请求/结果以 [施工合同](animation-rendering-j1-persistence-api.md) 和 api/schema/j1.schema.json 为准。MCP 工具由权威 catalog 机械生成；本文不新增配置项，也不修改用户的 MCP 配置。

## 实际验证与限制

最终 C++ 77 路径清单位于 E:/CodexTemp/mini3d-j11-execution-20261009-c94e14ba/product-slices-20261011-3f1dbe27/b-catalog72-final-manifest-f8a61bf2.json，SHA256 40CBD588D0EBAC2509CCC71BF21427817584434027196CF4AD6A9BB3C00F0459。

- 最终 Editor 构建实际 exit 0；八类 API/桥/批次/对象/作者/分配合并回归：122 例中 120 通过、2 个特殊磁盘夹具 skipped，20775 断言通过，没有空标签匹配。回执为 slices/runs/20261011-191029235-BuildEditor-8715061e 和 20261011-191106441-TestEditor-dbf7ce74。
- 大身份最小复现修后 1 例/31 断言通过；新增 J1 API/真实桥 17 例/3391 断言通过。它们包含在后续合并回归中，不累加成唯一用例总数。
- E 盘隔离 MCP 副本 Build、Typecheck、Test 实际 exit 0；56 例通过、无 skipped。两代官方 SDK 覆盖 72 工具及 11 新方法的传输、结果、CAS/Unsupported/no_change；mock 不代替真实 Core 求值或真实作者历史验证。回执位于 slices/mcp-b-validation-65fc13ad/runs/20261011-190838730-Build-323bd546、20261011-190847511-Typecheck-e0cc4999、20261011-190855917-Test-8c2ee468。
- 独立源码审查未发现本范围新增未解决的 P0/P1/P2。原编译失败、递归测试、JSON 悬空引用和旧 61 方法断言失败均保留，分别作精确测试修补，没有放宽生产守卫或大身份/精度断言。

两 skipped 分别因本机没有现成 mapped network drive 和可访问 FAT/exFAT 卷，不计通过。MCP 没有 npm install、写仓库 dist 或添加全局配置。GUI 三类参数创作、正式 PNG/FLOAT32 EXR 输出和联合作品仍待 EXEC-013/014/015 各自验收；不能用本票代签。

## 撤回与后续

撤回点：B C++ 前像 E:/CodexTemp/20261011/mini3d-j1-api-b-a92f541c/before-manifest.json、Schema/MCP 前像 E:/CodexTemp/20261011/mini3d-j1-b-schema-38cd0b63/before-manifest.json，以及 slices/before 的三份 J1ApiTests 前像和 BatchApiTests.before-catalog72-f8a61bf2.cpp。先核后继与引用，再定向逆补丁；新增文件移存独占 withdrawn，不能 reset 或覆盖其他 dirty。正式日志只追加撤回。

下一步按已批准计划实施 EXEC-013 最小 GUI 与 EXEC-014 同线程固定尺寸离屏输出。子代理按 Sol 6.1/max 实施/独审；请求配置被工具接受，provider 最终路由未独立核验，不能声称 fast 生效。没有系统安装、Git 提交推送或发布。
