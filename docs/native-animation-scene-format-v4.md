# 原生对象动画：格式4与正式历史

2026-10-07，R2实现说明。完整动画产品仍需R3姿态/GUI、R4 API/MCP与R5验收；本说明不代表界面已经可以播放。

## 保存、旧稿与失败保护

Mini3D统一写`Mini3DScene`格式4，读格式1–4。读入旧1/2/3时不会自动生成关键帧、转换网格或覆盖原文件；首次升级只能另存到不同路径，界面建议`原文件名-v4.m3dscene`。Windows路径比较忽略大小写；取消或失败保持原文件、文档路径、选区、历史和保存点。

普通保存继续使用QSaveFile原子替换；另存NewOnly在最后竞争检查也不覆盖已有目标。落盘前按实际UTF-8编码限制64MiB，因此格式4保存器不会生成本程序无法重开的超限文件。旧格式的正常读取不施加新增格式4深度/字节上限，受控文件API已有读取限额仍独立生效。

加载始终先完成临时Scene、全部资源、网格/修改器和动画绑定验证，成功后才发布。损坏或超预算输入不会替换当前Scene、资源、选择或历史。文件读入使用同一QFile句柄；预检后大小/EOF变化会被拒绝，读取上限不依赖句柄能否冻结文件内容。

## 动画字段

格式4必需顶层`animation`对象，静态工程也写默认设置和空`tracks`。格式1/2/3若携带`animation`直接拒绝，不静默丢失动画。原`editorCamera`、`lighting`、`assets`、`entities`、`editableMeshes`、`collections`与`editorState`语义保持；原顶层未知字段兼容性不扩大为全局严格字段规则。

```json
"animation": {
  "fps": 24,
  "startFrame": 1,
  "endFrame": 250,
  "tracks": [{
    "entityId": 12,
    "channel": "rotationEulerXYZDegrees",
    "keys": [
      {"frame": 1, "value": [0, 0, 0], "interpolation": "linear"},
      {"frame": 49, "value": [0, 720.000000001, 0], "interpolation": "linear"}
    ]
  }]
}
```

上例是原生文件字段片段，不是API请求；原生EntityId是uint64整数，API仍使用无损十进制字符串。不要通过JavaScript Number中转大ID。

| 字段 | 规则 |
| --- | --- |
| fps | 整数1–120，默认24 |
| startFrame/endFrame | 整数1–100000，start≤end；范围调整不删键 |
| entityId/channel | 必须绑定现存实体；同一实体同一通道最多一轨 |
| channel | position、rotationEulerXYZDegrees或scale，均为完整XYZ组 |
| frame | 整数1–100000，原数组严格递增，不排序掩盖错误 |
| value | 三个有限double；不经过基础float TRS保存 |
| interpolation | constant或linear，属于左键到下一键；最后一键仍保存原插值 |

动画/轨道/键对象禁止未知字段、缺字段与重复字段。整数要求JSON整数词法token，`24.0`或`1e0`不算整数。结构schema无法表达这一区别、绑定唯一性、帧序或数值TRS可用性，不能以schema通过替代程序语义验证。

Position/Scale必须能转换为有限float。Scale每分量绝对值至少0.001；Linear相邻键同号，Constant可跳号。Euler保存连续度数且绝对值不超过3.6×10^9，0/360/720不按最终旋转视为相同。只有运行求值才约减角度并转float；宽ID与原始double精确往返。

合法空轨先检查实体、通道与重复绑定，再规范化移除；空轨不能用来隐藏坏引用或重复项。

## 分配前预检与预算

先固定内存完整扫描识别真正的顶层`version`，支持字段在尾部及转义键名；嵌套/字符串中的伪version不会改变路由。出现任何合法整数4且重复version会拒绝。格式4再进行有界SAX结构预检，最后才分配DOM与正式动画；预检不是用头部正则猜版本。

| 预算 | 上限 |
| --- | --- |
| 格式4源文件/实际UTF-8输出 | 64MiB |
| JSON容器深度，根为1 | 64（65拒绝） |
| 轨道 | 3000 |
| 单轨键 | 10000 |
| 总键 | 100000 |
| 动画完整数值Pose | 10000实体 |
| 定义编辑候选姿态预检 | 40000节点访问 |

预检取当前时刻及变化键/新邻接端点，时间去重后先核`节点数×时刻数`，不扫描所有未变化曲线的旧键。有限时刻预检不证明整个连续区间都合法，R3运行姿态仍须检查。普通静态GUI复制/删除不会继承API2048实体或动画Pose的全局限制。

## 正式内容与撤销

Scene持有不可变正式动画；基础TRS、正式关键帧和临时姿态分离。完整候选先验证再进入唯一QUndoStack，一次编辑一条历史；原始double/插值精确no_change在push前返回，不剪redo、不改变保存点，仍检查最后提交许可。

GUI/API复制共用prepared子树：新ID映射在push前固定，动画独立复制，Undo/Redo不重分ID。GUI选中副本，API不抢当前选择。删除同样带曲线/集合成员快照，Undo恢复原ID、曲线、成员与选区。实际换父时自身或后代有直接轨道会拒绝；同父no_change仍允许，无轨子树保持原keepLocal语义。

内容和历史先安装、版本推进后发布通知。选择先静默变为有效ID，再结束树reset，之后通知选区消费者；结构/文档/历史观察者不读取已删除的选中对象。

## 验证与边界

对应CPU标签`[animation],[scene-animation]`、真实文件`[animation-document]`、Qt历史`[animation-history]`。实际运行与双线审计以[本机审计记录](native-animation-audit-20261007.md)和`progress.md`最终追加为准，不把计划用例算作通过。

[格式4结构schema](scene-format-v4.schema.json)复用[格式3](scene-format-v3.schema.json)字段定义，未覆盖旧schema。R2当前数值预检暂用第1帧；R3将接会话时间、保留候选姿态、缓存实际几何bounds和统一显示身份。API仍0.1.0直到R4成套迁移；本阶段不提供动画MCP方法。
