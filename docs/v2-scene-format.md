# 格式 3 与可编辑网格文档接入

> 本文保留二期格式3的历史合同。原生动画R2起正式写格式4、读1–4；旧1/2/3首次升级须另存到新路径，建议名改为`-v4.m3dscene`。当前新增字段、读写预算和迁移规则以[原生动画格式4](native-animation-scene-format-v4.md)为准，原网格与资源语义不变。

## 当前交付边界

2026-09-12，EDIT-01 接通场景真源、唯一历史、GPU 缓存与最小格式 3。
当前转换入口是 `SceneViewModel::makeEditable`，仅支持可见原生 Cube 或已绑定的可编辑对象。
Tab 模式切换、组件选择和交互式组件 G/R/S 分别由 EDIT-02/PICK-01/EDIT-03 接入，不能以本项
自动化调用 ViewModel 的测试宣称这些 UI 功能已经完成。

## 所有权和历史

- Scene 持有 MeshId → 不可变真源/派生内容，SceneNode 只存 MeshId；与 primitive、静态 meshAsset
  互斥。相机/灯不能绑定可编辑几何，同一可编辑 MeshId 不允许被多个节点共享写入。
- 首次转换创建独立身份；复制可编辑子树为各几何分配新 MeshId，不改变另一实例。不可变内容
  可以共享内存，实际编辑安装新内容。删除保留历史所需资源，保存只输出仍绑定的网格。
- 候选先完成流形、属性、引用和三角化校验；成功后预留身份/表槽位。当前节点及 revision
  在提交前不变。命令持有 before/after，Undo/Redo 只安装快照，不重跑算法，不新建历史栈。
- 三类 revision 由 Scene 单调生成，目前保守地一起递增，不做几何/拓扑分类优化。
  它们不写文件，撤销不回退；新文档重置身份域并清理 GPU 缓存。GPU 更新只在有效 Context。
- `replaceEditableMesh` 相同内容不入历史；验证失败不影响内容/历史/保存点。内存分配异常沿用
  既有 Core 策略，完整分配失败注入与 F9 事务验收仍属于 HISTORY-01，尚未宣称通过。

## 文件结构

写 `format=Mini3DScene`、`version=3`，读 1/2/3；保留原 editorCamera、lighting、assets、entities
含义以及 Y-up、局部 TRS、WXYZ、glTF 相对路径和 meshIndex。静态资源仍用原 AssetManager 重映射。

新增必需顶层数组 `editableMeshes`，无可编辑几何时写空数组。每项：

| 字段 | 含义 |
| --- | --- |
| id | 文档内独立 MeshId，uint64 非零；0 与最大值保留，读入后分配高水位继续前进 |
| vertices[] | `{id, position:[x,y,z]}`；源 VertexId 与位置 |
| faces[] | `{id, material, corners:[…]}`；源多边形，不写渲染三角形 |
| corners[] | `{id, vertex, uv:[u,v], color:[r,g,b], normal?:[x,y,z]}`；独立 CornerId 与属性 |
| entities[].editableMesh | 引用上述网格，要求 primitive=Empty，且没有 meshAsset/camera/light |
| editableMeshes[].modifiers[] | 固定顺序：可选Mirror，然后可选Subdivision；新文件总是写数组，无修改器时为空 |
| editableMeshes[].modifier? | 兼容读取旧格式3单Mirror字段；不得与modifiers同时出现，新文件不再写此字段 |

当前 `material` 仅接受 0（对象默认材质）；外部逐面材质映射尚未接入，非零明确拒绝，
不静默丢掉纹理。VertexId/FaceId/CornerId 保留64位精度；不能经过 JavaScript Number 中转。
不保存派生三角索引、GPU 句柄、revision、选择与 Undo 历史。

[结构 schema](scene-format-v3.schema.json) 仅验证字段形状；最终以 Core 语义校验为准，额外校验
ID 分域唯一、引用、网格绑定互斥且独占、保留编号、有限 float、面环/流形/投影三角化和组件参数。
Schema 通过不表示拓扑或资源可用，缺资源仍拒绝。省略两个修改器字段表示无修改器；
Mirror 阈值须有限且非负，载入时同时验证完整求值，失败不替换当前文档。停用 Mirror 后应用
会将恒等求值（原真源）保留为源并移除参数。FILE-01 的完整字段与失败保护见
[保存兼容性](v2-file-compatibility.md)，最终二期验收仍与阶段自检区分。

Subdivision字段为 `{type:"Subdivision",enabled:boolean,levels:1|2}`。只允许空链、单Mirror、
单Subdivision或Mirror→Subdivision；反序、重复、未知算子和两个旧/新字段并存均拒绝。
停用细分不求值，但参数仍严格校验。启用细分要求流形四边网格且无孤立点。
应用Mirror保留后续细分；应用细分烘焙完整当前求值链并删除两算子。求值网格、来源映射、
缓存和选择仍不保存，载入时全部重新离线验证后才发布。

## CURSOR-01：游标辅助状态（2026-09-17）

沿用已批准的格式 3 扩展，新增可选顶层 `editorState`。FILE-01 在原游标结构旁增加固定轴声明：

```json
"editorState": { "upAxis": "Y", "cursor3D": { "position": [0, 0, 0], "visible": true } }
```

坐标为右手 Y-up 世界位置，接受三个有限 float；`visible` 必须为布尔。
旧格式 1/2 及没有该字段的旧格式 3 默认原点、可见；格式 1/2 不解释 cursor3D。
省略 upAxis 默认 Y-up；若任一支持版本显式提供 upAxis，必须是字符串 "Y"，否则原子拒绝。
格式 3 中省略 `editorState` 或 `cursor3D` 可读；若提供 cursor3D 则两个子字段都必需，
错误类型、缺项、非有限/溢出数字均使读取失败，不替换当前文档。
新写出的文件总是包含此最小状态，不保存游标朝向、鼠标放置模式或操作历史。

游标不属于 SceneNode；定位及其显示偏好不标脏、不触发未保存提示，不加入 QUndoStack。
仅在下次显式保存时落盘：没有星号不代表本次游标位置已经保存。新建复位，打开恢复；
旧格式首次升级仍须另存。操作和验收见 [3D 游标](v2-cursor.md)。

## 迁移与故障保护

基础集合使用可选顶层 `collections` 数组，每项为
`{id, name, visible, members:[EntityId,...]}`。新文件总是写数组，旧格式3省略时为空。
格式1/2仅允许省略或空数组，不接受携带集合的旧版本文件。集合ID独立于EntityId，
非零且不能使用uint64最大值，撤销/删除不回退分配高水位。
所有成员必须存在、集合内不重复，且每个对象最多属于一个集合；非法名称、重复身份、
缺失或重复成员使整个读取失败。集合不是实体父节点，不改变局部TRS或父子树。
最终可见性叠加对象/祖先的visible和所属集合visible，删除集合只解除归属。
详见 [集合管理](v2-collections.md)。

旧版读入后仍是原来的静态对象，不自动转换网格；运行时记录来源版本。首次 Ctrl+S/另存为
均展示另存对话框，建议 `原文件名-v3.m3dscene`。取消保持原状；即使直接调用保存 API，解析后
路径仍与旧原件相同也拒绝，Windows 比较忽略大小写。首次成功保存新路径后解除这个限制。

继续使用 QSaveFile，不启用直接覆盖回退。加载先构造临时 Scene/Assets，坏 JSON、非法网格、
坏绑定、缺资源或设备参数错误均不替换当前文档/资源/选择/历史。对外函数不保存 Undo 栈。
格式 3 文件不能交给旧 V1 候选程序读取；原候选包与用户输入资料没有更新或覆盖。

## 验证入口

CPU 标签 `[editable-scene],[serializer]`；编辑器/文件/GPU 标签 `[editable-document]`。
覆盖64位 ID/属性往返、16种损坏夹具、1/2迁移、独立副本、无变化/失败历史、保存重开、
真实帧缓冲与包围盒、Undo/Redo 缓存更新、Context 身份，以及旧文件保护与保存对话框建议名。
完整两配置结果见 progress.md 的 EDIT-01 记录；此处不替代后续真实组件输入验收。

EDIT-01 最终 Debug/Release 全 build 成功，CTest 各4/4（43.60s / 15.98s）；CPU 专项各
8用例210断言，文件/GPU专项各4用例124断言（含截图/工程输出），200% 专项4用例121断言。
证据 `out/validation/v2-edit01-{debug,release}.xml`、同名 `.m3dscene`/`.png`，及
`v2-edit01-dpi2.png`。结构 schema 正样本/反例均验证；CPU 测试依赖仍无 Qt/GL DLL。
