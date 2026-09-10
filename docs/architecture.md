# Mini3D Studio V1 架构约束

## 目标

V1 采用单线程 OpenGL 后端与单向模块依赖，优先证明 Scene Graph、资源管理、
OpenGL 生命周期、Picking、Gizmo、Command 和序列化能力，不引入完整游戏引擎。

## Target 依赖

```text
mini3d_editor
 ├─ mini3d_renderer_gl
 ├─ mini3d_assets
 └─ mini3d_core

mini3d_renderer_gl
 ├─ mini3d_assets
 └─ mini3d_core

mini3d_assets
 └─ mini3d_core

mini3d_tests
 └─ mini3d_core
```

依赖只能沿图中方向发生。Core 保持纯 C++，不得依赖 Qt Widgets 或 OpenGL；Editor
不得直接拥有 VAO、VBO、EBO 等 GPU 资源。

## 模块职责

| Target | 职责 | 关键边界 |
| --- | --- | --- |
| `mini3d_core` | AABB/Ray、稳定 ID、MeshData、Transform、Scene、外观和场景 JSON | 纯 C++/GLM/JSON，不依赖 Qt 或 OpenGL |
| `mini3d_assets` | glTF 解析、CPU 缓存、场景文件和相对资源重建 | Qt 用于文件/图片处理，不执行绘制 |
| `mini3d_renderer_gl` | QOpenGLWidget 适配、相机、基础几何、Shader 与 GPU 资源 | 不修改场景业务状态 |
| `mini3d_editor` | 主窗口、场景树、Inspector、Selection 与 Command | 不直接管理 GPU 生命周期 |
| `mini3d_editor_ui` | 当前 SceneViewModel、树模型、选择模型、Inspector 和主窗口 | 应用与 UI 测试共用；不写 GPU 状态 |
| `mini3d_tests` | Core、相机、几何及 Gizmo 数学测试 | 不依赖 Qt 或真实窗口 |
| `mini3d_asset_tests` | 静态导入、资源缓存、纹理解码与 CPU 场景拾取测试 | 依赖 Qt Gui，但不创建窗口或 GL Context |
| `mini3d_gpu_tests` | 纹理、材质、选中线框与 GPU 生命周期集成验收 | 依赖 Renderer 与真实驱动，使用离屏 Surface/FBO |
| `mini3d_editor_tests` | 场景树契约、属性/命令、拖动与实际渲染联动 | Qt Test 驱动真实窗口；不混入纯 CPU 测试 |

## 数据与生命周期约束

1. Scene、Selection、Command 和序列化统一通过稳定 `EntityId` 关联，不长期保存
   `SceneNode` 裸指针。
2. CPU 侧 `MeshAsset` 与 GPU 侧 `GpuMesh` 分离，Scene 只保存 `AssetId`。
3. OpenGL 对象只能在有效 Viewport Context 中创建、使用和销毁。
4. V1 使用右手坐标系、Y 轴向上；世界矩阵按 `ParentWorld × Local` 计算。
5. 用户修改最终进入 Command；一次 Gizmo 拖动只提交一条历史记录。

## 当前落点

当前已建立 `mini3d_core`、`mini3d_assets`、`mini3d_renderer_gl` 与 `mini3d_editor` Target。应用在创建 `QApplication`
前统一请求 OpenGL 4.1 Core 格式；`ViewportWidget` 只管理 Context、调试日志和 Qt 帧
回调，并把中键、Shift+中键和滚轮输入翻译为相机意图；`Renderer` 组织每帧状态与绘制，
持有纯 GLM `EditorCamera`，先通过 `GridRenderer` 绘制有限 XZ 网格与 XYZ 轴，再通过
三个 `GpuMesh` 绘制 Cube、Sphere 和 Plane。纯 CPU `PrimitiveFactory` 只生成位置、
单位法线、颜色、UV 和索引，不创建 GPU 句柄或场景身份。`ShaderProgram` 管理 GLSL Program，
`GpuMesh` 独占 VAO/VBO/EBO，`GridRenderer` 独占线段 VAO/VBO；两类几何不混用绘制拓扑。
Resize、Orbit、Pan 和 Zoom 只更新 CPU 相机状态，GPU 资源仍在 Viewport Context 有效期间
按依赖逆序释放。`MainWindow` 仅装配 Viewport 与 Scene、Inspector、Console Dock，不
直接持有 GPU 句柄；SceneViewModel 负责统一场景写入，SceneTreeModel 映射节点，
SelectionModel 保存选择 ID，TransformInspector 刷新和提交数值意图。

Renderer 新增值成员 `GpuTexture`，拥有棋盘格和白色默认纹理；`Material` 只借用纹理，
通过 baseColor、顶点色开关和纹理开关逐物体配置 Shader。纯色绘制也绑定有效白纹理。
`MeshData::bounds()` 调用 Core 的 Aabb 计算局部盒，Core 还提供八角点仿射包围和 Ray
相交函数。SceneNode 保留内置 PrimitiveKind，并以可选 MeshRendererComponent 保存
导入网格/材质的 AssetId，不保存 GPU 句柄。MeshData 定义在 Core，Renderer 旧路径仅保留
类型别名。纹理契约见 [第二周验收](week2.md) 和 [第四周导入](week4.md)。

第三周起，Viewport 与 ViewModel 共享 Scene 生命周期，Viewport 只持有只读引用；
Renderer 不再固定物体位置，而是递归读取 Scene 计算 World 并复用内置 Mesh。
UI 通知只请求重绘，GPU 调用仍限定在 Viewport Context 生命周期内。
具体数据流、换父语义和测试见 [第三周验收](week3.md)。

`mini3d_tests` 直接编译 `EditorCamera` 与 `PrimitiveFactory` 的纯 GLM 实现，链接 Core、
Catch2、GLM 和编译选项，不链接 Qt Widgets、OpenGL 或 `mini3d_renderer_gl`。
`mini3d_gpu_tests` 单独链接 Renderer 和 Catch2，在真实 Context 中验证纹理和材质，
不影响 CPU 测试的无窗口边界。Assets Target 的 Qt Gui 与 fastgltf 依赖不反向传入 Core。
实际应用入口链接 mini3d_editor_ui，后者链接 Renderer/Core；mini3d_editor_tests 使用
同一个 UI 库和 Qt Test 验证真实控件与帧缓冲。

第四周起，File 菜单将文件路径交给 SceneViewModel；AssetManager 先完成 CPU 解码和验证，
成功后 ViewModel 才一次性通知树结构变化并实例化节点。重复路径复用首次成功的 CPU 资源，
但创建独立 EntityId。解析失败不修改现有场景和选择。Viewport 共享只读 AssetManager，
Renderer 在有效 Context 的绘制阶段按 AssetId 懒上传并缓存 GpuMesh/GpuTexture；
换资源库或销毁 Context 时清理缓存，上传失败不会逐帧重试刷屏。
当前导入同步执行；缓存随编辑器生命周期保留，不提供后台加载、热重载或资源卸载。

第五周选择通路：Viewport 将无修饰左键点击转换为世界射线，发出 pickRequested；
MainWindow 只接线，SceneViewModel 调用无 GL 操作的 RayCaster 并写入唯一 SelectionModel。
树、Inspector 和 Viewport 都订阅同一选择 ID；Viewport 仅保留绘制用镜像，不独立决定选择。
RayCaster 与 PrimitiveFactory 一样位于 renderer_gl 源目录，但只读取 Core/CPU Assets。
无窗口资源测试直接编译该实现，不链接 Renderer/OpenGL；Core 不新增 Qt 依赖。

Renderer 在实体绘制后由 SelectionRenderer 绘制世界 AABB 覆盖层。SelectionRenderer 独占
固定十二条边的 VAO/VBO 和 Shader，复用 grid Shader 资源，不修改模型材质或深度缓冲，
绘制后恢复深度测试/写入状态。它随 Renderer 在有效 Context 内销毁，不逐帧创建资源。
可见子树包围盒同时供高亮与聚焦使用。F/View 菜单仅更新相机，不写 Scene；
Focus 数学直接放入 EditorCamera，未为一次调用另加独立 FocusController。
坐标、选择层级与限制见 [第五周说明](week5.md)。

第六周移动通路：GizmoController 只计算轴拾取和冻结平面投影，不写 Scene；Viewport
发出开始/预览/结束意图，SceneViewModel 保存初始 TRS，预览更新场景但不入栈。
提交产生一个 TransformEntityCommand，取消恢复初始 TRS。世界位移通过逆 ParentWorld
转换成局部位移，不分解父矩阵；父旋转/负非均匀缩放不改变世界轴约束。
GizmoRenderer 复用一次上传的箭头网格，在选中线框之后覆盖绘制三轴并恢复深度/剔除状态。

SceneViewModel 唯一拥有 QUndoStack，命令按 EntityId 保存值，不持有节点地址。
SubtreeCommand 使用 Scene 生成的封装内存快照：删除恢复原 ID/兄弟顺序，复制首次生成新 ID，
redo 复用该 ID。快照保存节点和 AssetId，不拷贝 GPU 对象、不重新导入资源。
第六周中未纳入命令的操作曾清空历史；第七周已补齐，只有打开/新建重置文档历史。
Core 快照与 Gizmo 数学不依赖 Qt；Qt 历史测试位于编辑器测试目标。
操作、生命周期取消条件和限制见 [第六周说明](week6.md)。

第七周文档通路：MainWindow 只处理对话框、关闭确认和标题，SceneViewModel 管理
文件路径、QUndoStack 保存点及观察相机；SceneDocument 负责 QSaveFile 原子写入和
相对路径解析，SceneSerializer 在 Core 内验证 UTF-8 JSON，不执行 IO。
打开先在临时 Scene/AssetManager 中完成全部解析，成功后才发布；保持共享 Scene 对象身份，
替换资源库时通知 Viewport 清除旧 GPU 缓存，避免新旧 AssetId 碰撞。新建同样重置资源库。

AppearanceInspector 只提交材质/光照意图，EditCommand 按值回放；SurfaceStyle 属于实例，
不修改共享导入材质，Renderer 将实例乘色/开关与源材质叠加。Lighting 属于 Scene，
第七周时尚无灯实体。Viewport 导航发出 CameraState，文档重置单向恢复视口，不回发导航通知。
相机与保存值差异独立计入 dirty，但不入对象撤销栈；单纯选择不标脏。
ViewModel 析构时断开历史通知，避免 QUndoStack 销毁向正在析构的窗口回调。
文档格式、资源边界和验证入口见 [第七周说明](week7.md)。

第八周只增加可选 tools：PerformanceProbe 使用现有 Renderer/SceneViewModel 记录基线，
DemoCapture 使用真实主窗口和 Qt Test 回放；均通过 MINI3D_BUILD_TOOLS 显式开启。
打包脚本收集已有 Release 产物，Test-Package 用独立副本及净 PATH 检查实际 DLL 来源。
这些工具不加入生产 UI 状态，也不改变 `.m3dscene` version 1；main 的指定场景入口仅在
显式截图验证模式生效。发布范围及外部验收缺口见 [第八周说明](week8.md)。

2026-09-10 P0 补齐：SceneNode 新增可选 CameraComponent / LightComponent，Scene 维护
组件校验、世界旋转、预览 VP 和单方向灯选择。位置服从完整层级 TRS，设备朝向只组合旋转。
SceneViewModel 创建/编辑组件仍经 QUndoStack；只读相机预览 ID 为瞬时 UI 状态，不入历史，
不标脏、不持久化，隐藏/删除/文档切换时退出。Inspector 发意图，MainWindow 只负责接线。
Renderer 使用组件相机 VP / 有效灯光；Viewport 预览时暂停鼠标导航、拾取、移动和 F 聚焦，
并隐藏网格/高亮/手柄；退出恢复未改动的 EditorCamera。树和 Inspector 仍可编辑，变化实时预览。
SceneSerializer 写 version 2、读 1/2；SceneDocument 原子 IO 和资源恢复路径未改动。
格式及使用约定见 [Camera/Light 说明](camera-light.md)。

2026-09-10 汉化：MainWindow 创建控件前调用 ChineseUi，翻译器由应用持有，官方 Qt 中文 qm
通过当前 Qt 套件查询并内嵌静态 UI 库。文件入口使用 Qt 非原生对话框；业务文案默认中文，
未增加语言切换或改变数值区域格式。标签与 objectName 分离，Core/资产层仍不依赖 UI 翻译器。
用户名称、glTF 标识和场景键保持原样；错误中文上下文保留第三方诊断。详见 [汉化说明](localization.md)。
