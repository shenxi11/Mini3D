# 镜像与细分 API

合同API0.1.0：api/schema/m5-modifiers.schema.json及共享样例。M5-03已在本机/官方SDK范围验收，详见[验收证据](validation/api-mcp-m5-modifiers-20261006.md)；运行期能力以system.describe为准，静态目录不代表当前实例已授权或可执行。

## 请求与状态

三个方法均提供document、expectedDocumentRevision、entityId、meshId、expectedTopologyRevision、expectedGeometryRevision。目标是明确的可编辑源网格；无待确认交互的Object模式才能写入，允许隐藏明确目标，不抢选择。先预算再求值，失败/no_change保留redo、clean、选区与版本。

mesh.getSummary补充完整modifiers：固定order=[mirror,subdivision]，mirror/subdivision分别为完整参数或null。order表示固定求值顺序，不代表两个修改器一定都存在或启用。统计继续区分真实source和最终evaluated。

| 方法 | 参数 | 行为 |
| --- | --- | --- |
| modifier.setMirror | options完整镜像参数或null | null移除；完整设置axis=x/y/z、enabled、merge、clipping、threshold；同值no_change |
| modifier.setSubdivision | options完整细分参数或null | null移除；enabled和levels=1或2；停用也校验级数；同值no_change |
| modifier.apply | modifier=mirror或subdivision | 应用必须存在的参数；不存在返回UNSUPPORTED_OPERATION；已存在但停用仍可应用 |

镜像只沿一个局部轴/原点平面，threshold是有限、非负double，0合法；不无依据限制为float。Merge与交互Clipping独立，API设置不自动剖开源，也不建立GUI夹持会话。细分遵守现有四边流形约束，不支持折痕或自适应级数。

## 应用与历史

- 应用mirror只烘焙镜像求值为新source，移除mirror，保留后面的subdivision。
- 应用subdivision烘焙当前整条最终求值链，移除mirror和subdivision。
- 停用参数仍被应用/移除，沿用本体Core语义，不把停用误当不存在。

返回flat command+meshIdentity、完整modifiers和requeryRequired。set为false；apply为true，始终重取源分页及稳定ID，不靠旧ID或容器位置猜烘焙结果。meshId绑定保留，源/求值内容和版本按Core保守安装规则前进；Undo/Redo回放准确准备快照，不重跑修改器。单次参数改动或应用对应一条共享历史。

API预算包含源、镜像/细分展开和候选内存，求值前保守核算、求值后核算实际容量；GUI可以编辑的更大模型不等于API允许展开。失败不留下半个修改器或部分源变化。
