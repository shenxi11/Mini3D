# 显式源网格编辑 API

合同API0.1.0：`api/schema/m5-modeling.schema.json`及共享样例。M5-02本体/官方SDK闭环已通过，运行期能力仍以`system.describe`为准；证据见[建模验收](validation/api-mcp-m5-modeling-20261006.md)。

## 共同前提

不进入GUI编辑模式、不读取当前组件选区。API写入只在无待确认交互的Object模式执行；允许明确ID的隐藏对象。成功不抢选择。除makeEditable外，必须提供document、expectedDocumentRevision、entityId、meshId、expectedTopologyRevision和expectedGeometryRevision；每次用返回的新版本，不自行加一。

先验证源身份/预算、准备合法几何、返回结果和历史命令，最终守卫后一次提交。目标及版本验证后，共享预检在组件索引或拓扑分析前拒绝超预算源，包含零位移挤出；不因GUI能编辑更大源就解除API预算。Undo/Redo回放准备好的身份；失败/no_change不改变历史、redo、保存点或版本。输入/候选数量和内存沿用M2预算。

沿用Core保守版本策略：每次安装几何快照三类mesh revision都前进，Undo/Redo也不回退。拓扑ID保持不变不等于topologyRevision不变；始终重取返回版本。

## 组件和边身份

- domain=vertices仅提供vertexIds；domain=edges仅提供edges；domain=faces仅提供faceIds。每次仅一个域，最多10000目标，不允许重复。fillFace不支持faces。
- 边为两个稳定VertexId组成的数组，例如`["12","25"]`。端点无方向，内部按数值升序；反向输入等价，但同批反向重复仍拒绝。相同/不存在端点、非真实源边均拒绝。
- 点/面身份从mesh.readSourcePage获得；边端点从面环相邻顶点获得，不能使用渲染顶点/三角形下标。

## 六个方法

| 方法 | 显式参数 | 结果及现有限制 |
| --- | --- | --- |
| mesh.makeEditable | 文档/内容版本、entityId | 仅原生Cube转换，已editable为no_change；返回当前mesh身份。其他静态几何、设备不支持。Undo恢复原primitive，Redo恢复同一准备mesh |
| mesh.transformComponents | 域/稳定ID、transform | 返回实际移动的affectedVertexIds；不改变拓扑ID。完整源变换后重新求值既有修改器 |
| mesh.bevelEdge | edge、width | 单条外凸流形源边、单段；局部垂距宽度严格大于0且小于内核分析上限；返回bevelFaceId |
| mesh.loopCut | seedEdge、可选slide默认0 | 规则四边面带单环切，slide严格(-1,1)；0为中点，也改变拓扑；返回cutEdges |
| mesh.deleteComponents | 域/稳定ID | 删除关联面并清孤立点，不是溶解；返回deletedVertexIds/deletedFaceIds/deletedCornerIds/deletedEdges精确差集；允许空结果沿内核规则 |
| mesh.fillFace | 点或边域/稳定ID | 必须完整单个共面边界闭环，不按点击顺序连接散点，不修多孔；返回新faceId |

所有身份数组按数值升序，边键先规范化再按端点排序；新面身份来自内核结果，不猜数组末尾。源变化后旧版本一律拒绝。

## 组件变换语义

transform完整提供space（local/world）、pivot、translation、rotationQuaternion `[x,y,z,w]` 和scale。四元数非零并由应用归一化；缩放各分量绝对值至少0.001，可为负。坐标右手Y向上、沿用场景单位。

同一空间增量为`D=T(pivot+translation)*R*S*T(-pivot)`。world直接作用于顶点世界位置；local在源坐标作用，经实际对象及父节点矩阵W转为`W*D*inverse(W)`。不把含非均匀缩放/剪切的世界矩阵强行分解为TRS。枢轴与位移均在所声明空间；不可逆或非法矩阵明确拒绝。

例如绕世界原点旋转90°并向上移1：space=world、pivot=[0,0,0]、translation=[0,1,0]、rotationQuaternion=[0,0,0.7071067811865476,0.7071067811865476]、scale=[1,1,1]。

不读取GUI吸附、比例编辑、枢轴偏好或交互Mirror clipping会话；明确源变换后依照持久修改器求值。不提供任意矩阵/任意脚本执行，也不扩展现有倒角/环切/补面算法边界。
