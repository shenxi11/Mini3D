# 文件与文档 API

M5-04已在本机/官方SDK范围验收，详见[验收证据](validation/api-mcp-m5-files-20261006.md)。公共合同为api/schema/m5-files.schema.json与共享9合法/19非法样例，document.open沿用m1合同并增加显式discard。运行期以system.describe为准。

## 操作边界

| 方法 | 语义 |
| --- | --- |
| file.save | 只保存当前document.current.path；没有路径或旧格式首次升级必须另存。overwrite默认false，现有目标须显式true。成功更新保存点，不清历史 |
| file.saveAs | 沿用新路径语义，始终禁止覆盖；不接受overwrite字段 |
| document.open | ifDirty默认reject，显式discard仅在全部读取/准备/最终守卫成功后才替换当前文档；失败保留旧文档/路径/历史 |
| document.new | 同一ifDirty规则；打开空的未保存文档，返回opened/空路径；不是启动示例，不可撤销，旧句柄失效 |
| file.importGltf | path/name/parentId/完整local transform指定包装根；静态glTF/GLB完整子树先准备再发布；一次历史、不抢选择；返回全部前序created IDs/rootEntityId/warnings |
| file.exportObj | path/entityId/mode=source或evaluated；固定世界空间、显式对象、不读取GUI选区；静态/原生对象只有一种几何。overwrite默认false；成功saved只表示OBJ发布，不改变工程路径/保存点/历史/版本 |

外部读写只限启动批准根；权限和请求参数分开，overwrite=true不会扩大根。新文件发布必须原子拒绝竞争中新出现的目标；明确覆盖时同目录完整暂存、提交前重检根/祖先/普通目标与最终许可，失败不改变原字节。拒绝UNC、设备、ADS、重解析点以及非普通路径；不声称防御已控制当前用户权限的恶意进程所有TOCTOU攻击。

权限按方法声明，不是全局文档只读开关：file.read明确允许document.open读取并替换文档，以及file.importGltf读取并向当前场景导入；即便嵌入方不授予scene.write，这两种显式文件操作仍受file.read及批准读根控制。entity.create/update、document.new等仍要求scene.write。需要完全只读观察时，不授予file.read、scene.write或viewport.control；不要把file.read-only称为场景不可修改模式。

import/open的模型及buffer/image依赖都必须在实际读取前授权；RPC大小与磁盘预算不同。一次外部open/import的文件大小累计上限64MiB、最多128个唯一规范化授权路径，包含顶层和依赖；重复授权不重复计数，但再次核对当前大小。此预算不是图像解压后内存或全部解析临时峰值保证。import最多2048节点（包装根和多primitive子节点都计算），发布候选按64MiB核算；OBJ文本最多64MiB。超限返回LIMIT_EXCEEDED，不放宽GUI自身限制。

## 原子性与缓存

导入失败不得出现可见节点、半个父子树、历史/redo/clean/选区/版本变化或泄露失败ID。允许不可见资源缓存和内部高水位空洞；AssetManager成功解析后可能先缓存，后续Scene准备失败不宣称内部完全零写入。Undo/Redo只安装/收回准备子树，不重读磁盘或重跑导入，也不能整体替换Scene损坏旧历史。

新子树准备使用与运行期相同的localMatrix及逆向父链乘法顺序（含既有外部祖先）；不能把浮点矩阵的理论结合律当作数值安全证明。局部TRS均合法但运行期世界矩阵溢出的整组候选会被拒绝，不能发布后再发现非法值。

open/new准备新场景、资源、路径与运行期身份再检查最终守卫；成功清理旧历史、选择与预览并更换documentId。discard是调用者明确承担的丢弃意图，没有默认保存/对话框或隐式重试。

## 内部前置接口（唯一所有者冻结）

- Core：Scene::SubtreeNodeOptions{name,parentIndex,transform,surface,visible,meshRenderer}；root index0的parentIndex=nullopt，其他索引必须引用较早节点。Scene::prepareNewSubtree(options,parent,error,maximumEntities=2048)返回既有PreparedSubtree；其映射keys为输入1-based索引，values为新ID，仅供内部按输入顺序取值，不是已有实体复制映射。Core不读文件或解析资源。复用install/removePreparedSubtree，全组预检后无失败移动安装。
- 路径：FilePathPolicy::authorizeWrite(path,error)接受批准写根下新路径或已存在普通文件；authorizeNewFile保持不变。EditorApiService::setFileAccessPolicies(read,newOnly,replace={})新增独立第三策略；旧二参数不会自动取得覆盖权限。Bridge装配第三策略归主代理。
- OBJ：ObjDocument::WriteMode{ReplaceExisting,NewOnly}；write(path,text,error,beforeCommit={},mode=ReplaceExisting,overwriteDenied=nullptr)保持旧GUI签名兼容；NewOnly复用既有场景发布的原子不替换方式，不用QSaveFile冒充NewOnly。
- API：FileRequest补IfDirty enum（默认Reject），新增FileSaveRequest/ImportGltfRequest/ExportObjRequest/DocumentNewRequest及对应结果；方法名save/importGltf/exportObj/newDocument。VM/DTO/codec/service同一作者串行，Core/路径/OBJ前置接口冻结后才消费。

当前未提交/推送/发布、未修改用户MCP配置。本机/SDK证据不代表目标Codex宿主、跨用户或新机器已实测；未运行条件在验收证据中单列。
