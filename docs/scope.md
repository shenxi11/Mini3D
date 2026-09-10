# Mini3D Studio V1 范围

## 产品目标

在 Windows x64 上完成“创建场景—导入静态 GLB—管理层级—选择对象—修改变换—
设置基础材质与灯光—撤销重做—保存并重新打开”的轻量级三维场景编辑闭环。

## P0 必须交付

- Qt 6 Widgets 可停靠编辑器框架：Scene Tree、Viewport、Inspector、Assets/Console。
- 透视相机、Orbit、Pan、Zoom、Fit、Grid 和坐标轴。
- Cube、Sphere、Plane、Empty、Camera 与 Directional Light。
- 基于稳定 EntityId 的 Scene Graph、父子 Transform、显示隐藏、重命名、复制和删除。
- 静态 glTF/GLB 的位置、法线、UV、索引、节点层级、基础颜色和纹理。
- Scene Tree 与 Viewport 选择同步、Transform Inspector 与 World Space Move Gizmo。
- 深度测试、背面剔除、基础纹理、方向光和环境光。
- `.m3dscene` 新建、打开、保存、另存为与未保存标记。
- 创建、删除、重命名和 Transform 的撤销重做。
- 日志、OpenGL Debug Context、Core 单元测试与可理解的错误提示。

## P1 可延后

- Rotate/Scale Gizmo。
- Stencil 或后处理描边。
- Asset Browser 拖拽导入。
- 阴影、完整 Gamma 流程与更多材质参数。
- 多选、框选、对齐和吸附。

## V1 明确不做

- 点、边、面建模，挤出、倒角、雕刻、布尔和 Modifier。
- 骨骼动画、时间轴、IK、物理、粒子、流体和布料。
- 完整 PBR、HDR、Bloom、SSAO、Vulkan、多线程渲染和 Render Graph。
- Qt3D、Qt Quick 3D、完整游戏引擎、VTK 与 OpenCASCADE。

任何新增能力先进入 V1.1/V2 讨论，不扩张当前 V1 的 P0 边界。

Camera/Directional Light P0 已按用户批准补齐为独立实体。相机支持透视参数与只读预览，
方向光保持单灯不叠加；多光源、设备图标/视锥绘制不在本轮实现内。
旧版文件兼容、选择方式和操作约定见 [专项说明](camera-light.md)。

## 用户批准的 V1.1 交互优化（2026-09-10）

原 P0/P1 划分保留作为历史基线；用户已批准并实施旋转/缩放手柄、世界/局部坐标、平面移动与步进吸附、视图快捷切换、资源管理器拖入模型、场景树拖放/右键/搜索六项。此处的文件拖入不是 Asset Browser，多选、框选、精确拾取和渲染特效仍未扩入本轮。详见 [交互优化说明](interaction-optimization.md)。
