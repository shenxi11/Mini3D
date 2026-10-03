# 验收样例资源、来源与许可

## 固定 glTF 验收样例

以下文件于 2026-09-08 从 KhronosGroup/glTF-Sample-Assets 的 main 分支下载，未经修改，
仅作为导入与渲染验收素材。来源模型仓库与应用代码的许可互相独立。

| 文件 | 来源 / 署名 | 许可 |
| --- | --- | --- |
| assets/samples/Box.glb | [Box](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Box)，© 2017 Cesium | [CC BY 4.0](licenses/CC-BY-4.0.txt) |
| assets/samples/BoxTextured.glb | [BoxTextured](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/BoxTextured)，© 2017 Cesium；Logo © 2015 Cesium | [CC BY 4.0](licenses/CC-BY-4.0.txt)，另遵守 [Cesium 标志说明](licenses/LicenseRef-LegalMark-Cesium.txt) |
| assets/samples/Duck.glb | [Duck](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Duck)，© 2006 Sony | [SCEA Shared Source License 1.0](licenses/SCEA.txt) |

BoxTextured 中的 Cesium 标志不代表 Mini3D 获得该商标的独立使用或背书权。

SHA-256：

```text
Box.glb         ED52F7192B8311D700AC0CE80644E3852CD01537E4D62241B9ACBA023DA3D54E
BoxTextured.glb B510ECA2E2EF33F62F9ED57D6E7CE2D10EBB2BDEBC4A8E59D347719BA81ABDF4
Duck.glb        65BF938F54D6073E619E76E007820BBF980CDC3DC0DAEC0D94830FFC4AE54AB5
```

## 二期原生作品

2026-10-02 增加三份可打开、继续编辑的格式 3 场景，位于 `assets/scenes/v2/`。
它们由本项目原生 Cube 与真实 C++ 验收用例制作，不是 glTF 导入样例、Blender 资产
或 HTML 效果稿。场景、源 OBJ、求值 OBJ 的准确来源和 SHA256 见
[作品清单](../assets/scenes/v2/manifest.json)，原始配方和截图见 [二期验收](v2-acceptance.md)。

| 场景 | 制作来源与用途 | 源网格 / 求值网格 | 配套 OBJ |
| --- | --- | --- | --- |
| [shell.m3dscene](../assets/scenes/v2/shell.m3dscene) | 原生 Cube 内插、挤出、规则环切；取消、确认/撤销循环后重做并保存重开 | 源 20 点 18 面，无修改器 | `shell-source.obj` |
| [symmetric.m3dscene](../assets/scenes/v2/symmetric.m3dscene) | 原生 Cube 程序化半壳，X Mirror / Merge / Clipping；应用后撤销 | 源 8 点 5 面；求值 12 点 10 面 | `symmetric-source.obj`、`symmetric-evaluated.obj` |
| [subdivision.m3dscene](../assets/scenes/v2/subdivision.m3dscene) | 同一半壳，固定 Mirror → 二级有限 Catmull–Clark；源笼与只读求值分离 | 源 8 点 5 面；求值 162 点 160 面 | `subdivision-source.obj`、`subdivision-evaluated.obj` |

使用当前源码构建的程序，通过“文件 → 打开场景”选择 `.m3dscene`，先另存副本再编辑。
选中网格对象后 Tab 进入 Edit，继续编辑源笼；修改器结果不可当作可写源元素。
OBJ 是复核/交换文件，不是工程入口。默认导出为 Y-up、单位不变，不附材质或纹理，
详见 [OBJ 导出](v2-obj-export.md)。旧第八周候选程序不能读取格式 3。

半壳和中心缝点选择由公开模型夹具构造，不能当作 UI Bisect 或鼠标选缝已实现的证据。
保存、重开及 OBJ 输出使用公开模型 API，配方中的实际输入项另行记录；不能据此宣称
文件对话框或 30 分钟人工耐久已验收。外壳用例执行 20 轮取消、20 轮确认/撤销，
之后一次重做、保存和重开，不是每轮都保存重开。

本机 Blender 5.1.1 实际导入这 5 个源/求值 OBJ，并复核 Mirror 与有限二级细分；
这不证明固定参考 4.5.0 已实测，也不代表 Blender 默认极限曲面、材质或全部功能等价。
具体数值与限制见 [兼容边界](blender-compatibility.md) 和 [二期验收](v2-acceptance.md)。

这些原生作品不引用上表第三方模型。项目许可仍未确定，不能据此推定获得公开复用或
分发许可；正式发布仍须完成许可核查。
