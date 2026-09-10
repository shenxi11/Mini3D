# Mini3D Studio V1.0 起步方案与开发设计文档

> 轻量级三维场景编辑器｜C++20 · Qt 6 · OpenGL · glTF · CMake

- 文档版本：v0.1
- 编制日期：2026-08-30
- 计划周期：8 周（在职每周约 7—9 个有效小时）
- 目标平台：Windows x64 / MSVC 2022

## 执行摘要

本项目不是复刻完整 Blender，而是从零实现一个可交付的三维场景编辑器 V1。核心闭环为：创建场景、导入静态 GLB、管理对象层级、选择对象、修改 Transform、基础材质与灯光、撤销重做、保存与重新打开。

**三条红线：**不引入完整游戏引擎；不在 V1 做网格建模/动画/物理；不让 AI 替代对 Scene Graph、OpenGL 生命周期、Picking、Gizmo 和 Undo/Redo 的理解。

## 目录

- 1. 项目概述
- 2. V1 功能范围
- 3. 技术选型与环境基线
- 4. 总体架构设计
- 5. 仓库结构与工程规范
- 6. Core 数据模型
- 7. OpenGL 渲染系统
- 8. 资源系统与 glTF 导入
- 9. 编辑器交互设计
- 10. 场景文件与 Undo/Redo
- 11. 开发环境与首次构建
- 12. 第一周起步执行方案
- 13. 八周里程碑计划
- 14. 测试、质量与性能
- 15. 风险与应对
- 16. AI 辅助开发工作流
- 17. Git、Issue 与交付流程
- 18. 作品集与求职交付
- 19. V1.0 最终验收清单
- 20. V1.1 / V2 演进方向
- 附录 A：首批类与文件清单
- 附录 B：初始 Backlog
- 附录 C：核心代码审查清单
- 附录 D：官方参考资料

# 1. 项目概述

## 1.1 项目定位

Mini3D Studio V1 是一款面向个人作品集与 C++/工业三维岗位求职的轻量级三维场景编辑器。它借鉴 Blender、Unity Editor 等专业工具的界面和工作流，但第一版不承担网格建模、雕刻、动画、物理和离线渲染等大型能力。

> **一句话目标：** 在 8 周内，从零完成“创建场景—导入 GLB—管理层级—选择对象—修改变换—设置基础材质与灯光—撤销重做—保存并重新打开”的完整闭环。

## 1.2 目标用户与使用场景

目标用户是需要快速搭建三维展示场景的开发人员、工业可视化工程师或技术面试评审者。V1 的演示场景固定为“工业机器人展示场景”：用户导入机器人 GLB 模型，添加地面和方向光，通过场景树组织层级，使用 Inspector 与移动 Gizmo 调整对象，最后保存项目并重新打开。

1. 启动 Mini3D Studio，新建空场景。
2. 创建 Plane 作为地面，创建 Directional Light。
3. 导入 Robot.glb，模型及节点层级出现在 Scene Tree。
4. 在三维视口或 Scene Tree 中选中 Robot，Inspector 显示 Position、Rotation、Scale。
5. 通过数值输入或 Move Gizmo 调整位置，修改基础颜色与纹理。
6. 使用 Ctrl+Z / Ctrl+Y 验证撤销与重做。
7. 保存为 Factory.m3dscene，关闭软件后重新打开，恢复对象层级、变换、资源引用和编辑器相机。

## 1.3 V1 成功标准

- [ ] 形成一个可独立安装、可演示、可重复构建的 Windows x64 应用。
- [ ] 核心三维能力由项目自行实现，而不是调用 Qt3D、Qt Quick 3D 或完整游戏引擎。
- [ ] 代码体现 Scene Graph、资源管理、OpenGL 资源生命周期、Picking、Command 和序列化等工程能力。
- [ ] 项目仓库包含构建说明、架构图、操作 GIF、演示视频、测试结果和版本标签。
- [ ] 项目负责人能够脱离 AI 解释核心数据结构、渲染流程、坐标变换、拾取算法、撤销栈和场景格式。

# 2. V1 功能范围

## 2.1 必须交付的 P0 功能

| 模块 | P0 功能 | 验收结果 |
| --- | --- | --- |
| 编辑器框架 | 菜单、工具栏、Scene Tree、Inspector、Assets/Console、状态栏 | 窗口布局可停靠并可恢复 |
| 三维视口 | 透视相机、Orbit、Pan、Zoom、Fit、地面网格、坐标轴 | 能够流畅观察场景 |
| 场景对象 | Cube、Sphere、Plane、Empty、Camera、Directional Light | 创建后进入场景树 |
| Scene Graph | 父子层级、Local/World Transform、显示隐藏、重命名、删除、复制 | 父节点变化可正确传递 |
| 模型导入 | 静态 glTF/GLB：位置、法线、UV、索引、节点层级、基础颜色和纹理 | 官方示例模型可导入 |
| 选择与属性 | 树选择、视口选择、Selection 同步、Transform Inspector | 选中状态双向一致 |
| 交互变换 | 数值编辑 + Move Gizmo；Rotate/Scale 至少支持 Inspector | 可完成场景摆放 |
| 基础渲染 | 深度测试、背面剔除、Base Color/Texture、方向光、环境光 | 模型具备可读的明暗 |
| 项目文件 | 新建、打开、保存、另存为、未保存标记 | 重开后恢复场景 |
| 撤销重做 | 创建、删除、重命名、Transform | Ctrl+Z/Ctrl+Y 正常 |
| 质量保障 | 日志、Debug Context、核心单元测试、错误提示 | 异常输入不直接崩溃 |

## 2.2 可延后的 P1 功能

- Rotate Gizmo 与 Scale Gizmo；第一版卡期时，保留 Inspector 数值编辑即可。
- Stencil 或后处理描边；P0 可以用 AABB 包围盒和高亮颜色代替。
- Asset Browser 拖拽导入；P0 可使用 File → Import。
- 简单阴影、Gamma 完整流程、材质参数编辑。
- 多选、框选、对齐和吸附。

## 2.3 明确不做

| 不做的能力 | 原因 |
| --- | --- |
| 点/边/面编辑、挤出、倒角 | 会把场景编辑器扩张为网格建模器 |
| 雕刻、布尔、Modifier | 每一项都足以成为独立大型项目 |
| 骨骼动画、时间轴、IK | 会同时扩展资源、场景、渲染和编辑器状态 |
| 物理、粒子、流体、布料 | 与 V1 的职业展示目标无关 |
| 完整 PBR、HDR、Bloom、SSAO | 优先保证编辑器闭环与工程结构 |
| Vulkan、多线程渲染、Render Graph | 第一版只保留单 OpenGL 后端和单线程 Context |
| Qt3D、Qt Quick 3D、完整游戏引擎 | 会掩盖核心 Renderer 与 Scene 能力 |
| CAD/CAE、VTK、OpenCASCADE | 作为 V1.1/V2 的工业插件方向 |

# 3. 技术选型与环境基线

## 3.1 最终技术栈

| 层级 | 选型 | 用途与边界 |
| --- | --- | --- |
| 语言 | C++20 | 核心数据结构、资源、渲染和编辑器业务 |
| 桌面 UI | Qt 6 Widgets | QMainWindow、QDockWidget、QTreeView、Inspector、文件操作 |
| 三维窗口 | QOpenGLWidget | 在 Qt Widgets 中承载 OpenGL Context |
| 图形 API | OpenGL 4.1 Core + GLSL 410 | 自行实现 Mesh、Shader、Camera、Picking 和 Gizmo |
| OpenGL 函数 | QOpenGLFunctions_4_1_Core | 避免额外引入 GLAD/GLEW |
| 数学 | GLM | 向量、矩阵、四元数、投影、Ray 和 AABB |
| 模型格式 | glTF 2.0 / GLB | V1 唯一外部模型格式 |
| 模型解析 | fastgltf | 解析静态 glTF/GLB 数据 |
| 图片解码 | QImage | PNG/JPEG 外部或内嵌纹理 |
| 场景序列化 | nlohmann/json | 保持 Core 层不依赖 Qt JSON |
| 日志 | spdlog | 控制台、文件以及后续编辑器 Console |
| 测试 | Catch2 | 纯 C++ Core 与数学逻辑单元测试 |
| 构建 | CMake 3.28+ | 多 target、Presets、Debug/Release |
| 依赖管理 | vcpkg Manifest | 按项目声明和恢复依赖 |
| 编译器 | MSVC 2022 x64 | Windows 主平台工具链 |
| 版本控制 | Git | Issue、分支、提交、Tag 和 Release |

## 3.2 推荐版本策略

- 项目基线建议使用 Qt 6.11.2 + MSVC 2022 x64；若本机已经安装 Qt 6.8 LTS，可继续使用，但不要依赖 6.11 独占 API。
- CMake 最低写 3.28，开发机可安装更高版本；仓库通过 CMakePresets.json 固定常用配置。
- 第三方库不在文档中手工锁死小版本，首次成功构建后将 vcpkg builtin-baseline 固定到仓库。
- 第一版只支持 Windows x64；代码结构保留跨平台能力，但不在 V1 同时验证 Linux/macOS。

## 3.3 为什么不选择其他方案

| 备选 | 暂不选择的原因 |
| --- | --- |
| QML / Qt Quick | 专业桌面 Dock、树、属性面板并非第一版最省成本的路线；还会增加 Qt Quick 与自定义 OpenGL 渲染协调 |
| Dear ImGui | 适合内部工具，但文件对话框、中文输入、复杂桌面布局和 Model/View 不如 Qt Widgets 完整 |
| Vulkan | 图形基础设施成本过高，会延迟 Picking、Gizmo、Scene 和持久化等核心功能 |
| Assimp | 格式多但依赖和兼容面更大；V1 只需把 glTF 做正确 |
| EnTT/ECS | 当前实体规模小，先自己完成 ID + Scene Graph，避免绕开数据结构学习 |
| Eigen | 图形数学统一使用 GLM；未来 CAE/数值模块再引入 Eigen |
| VTK | 擅长科学可视化，不适合作为通用场景编辑器的核心渲染框架 |

# 4. 总体架构设计

*图 1  Mini3D Studio 总体架构与单向依赖（Word 版含图示）*

## 4.1 架构原则

1. 依赖单向：Editor → Application/Renderer/Assets → Core，低层不得反向引用 UI。
2. Core 尽量保持纯 C++：不依赖 Qt Widgets，不保存 OpenGL 句柄，不认识具体窗口。
3. Scene 使用稳定 ID 表达实体关系，不让 UI、Command 和序列化长期保存裸指针。
4. CPU MeshAsset 与 GPU GpuMesh 分离，同一资源可被多个场景对象复用。
5. OpenGL 资源只在拥有有效 Context 的线程创建、使用和销毁。
6. 第一版采用单线程渲染；异步导入只在确认主流程稳定后加入。
7. 所有用户操作尽量转成 Command，便于 Undo/Redo、脏状态管理和测试。

## 4.2 模块职责

| 模块/Target | 主要职责 | 禁止事项 |
| --- | --- | --- |
| mini3d_core | Scene、Entity、Transform、组件、ID、世界矩阵、场景序列化数据 | 禁止调用 QWidget/OpenGL |
| mini3d_assets | glTF 解析、MeshAsset、TextureAsset、AssetId、缓存、路径规范化 | 禁止直接绘制 |
| mini3d_renderer_gl | QOpenGLWidget 适配、Shader、GpuMesh、Camera、Grid、Selection、Gizmo | 禁止修改 Scene 业务状态 |
| mini3d_editor | MainWindow、SceneModel、Inspector、SelectionModel、Commands、文件菜单 | 不直接管理 VAO/VBO/EBO |
| mini3d_tests | Core、Scene Graph、Ray/AABB、序列化与 Command 逻辑测试 | 不依赖真实窗口 |

## 4.3 运行时数据流

*图 2  glTF 导入、资源缓存与渲染数据流（Word 版含图示）*

glTF 解析器只负责把文件数据转换为项目内部 MeshAsset、MaterialAsset 和节点描述；Scene 通过 AssetId 引用资源；Renderer 在首次绘制时把 CPU 资源上传为 GpuMesh 并缓存。场景保存时只保存逻辑数据和资源相对路径，不保存 OpenGL 句柄。

# 5. 仓库结构与工程规范

## 5.1 推荐目录

```text
Mini3DStudio/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ CMakeUserPresets.json.example
├─ vcpkg.json
├─ .clang-format
├─ .gitignore
├─ LICENSE
├─ README.md
├─ assets/
│  ├─ shaders/
│  ├─ icons/
│  └─ samples/
├─ docs/
│  ├─ architecture.md
│  ├─ scope.md
│  ├─ roadmap.md
│  └─ devlog/
├─ src/
│  ├─ core/
│  ├─ assets/
│  ├─ renderer_gl/
│  └─ editor/
└─ tests/
```

## 5.2 CMake Target 依赖

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

## 5.3 命名与代码风格

| 对象 | 约定 | 示例 |
| --- | --- | --- |
| 命名空间 | 小写层级 | mini3d::core |
| 类型/类/枚举 | PascalCase | SceneNode、AssetManager |
| 函数/局部变量 | lowerCamelCase | worldMatrix、selectedEntity |
| 私有成员 | 尾随下划线 | renderer_、scene_ |
| 常量 | kPascalCase | kInvalidEntity |
| 文件 | 与主类型同名 | ViewportWidget.h/.cpp |
| 分支 | 类型/主题 | feature/scene-graph |
| 提交 | Conventional 风格 | feat: add orbit camera |

- 启用 /W4；先不强制 /WX，稳定后可在 CI 中把自有代码警告视为错误。
- 所有权必须在接口处说明：unique_ptr 表示唯一拥有，shared_ptr 只用于真实共享资源，观察关系使用 ID 或非拥有引用。
- 禁止在 MainWindow 和 ViewportWidget 中堆积业务逻辑；超过一个明确职责就拆类。
- 每个 public 类至少写一段用途说明；复杂数学函数写输入空间、输出空间和坐标约定。

# 6. Core 数据模型

## 6.1 标识符与无效值

```cpp
namespace mini3d::core {
using EntityId = std::uint64_t;
using AssetId  = std::uint64_t;

inline constexpr EntityId kInvalidEntity = 0;
inline constexpr AssetId  kInvalidAsset  = 0;
}
```

实体、父子关系、Selection、Undo Command 和序列化统一使用 EntityId。UI 只在需要时通过 Scene::find() 获取临时对象，不长期保存对象地址。

## 6.2 Transform

```cpp
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    [[nodiscard]] glm::mat4 localMatrix() const;
};
```

- 内部旋转使用四元数，Inspector 显示和输入欧拉角。
- 统一右手坐标系；Y 轴向上；角度 UI 使用度，内部 GLM 接口使用弧度。
- WorldMatrix = ParentWorldMatrix × LocalMatrix。
- 后续优化可增加 dirty 标记；V1 初期允许按需递归计算。

## 6.3 Entity 与组件组合

```cpp
struct MeshRendererComponent {
    AssetId mesh{kInvalidAsset};
    AssetId material{kInvalidAsset};
};

struct LightComponent {
    glm::vec3 color{1.0f};
    float intensity{1.0f};
};

struct SceneNode {
    EntityId id{kInvalidEntity};
    std::string name;
    EntityId parent{kInvalidEntity};
    std::vector<EntityId> children;
    Transform transform;
    bool visible{true};

    std::optional<MeshRendererComponent> meshRenderer;
    std::optional<LightComponent> light;
};
```

## 6.4 Scene 接口

```cpp
class Scene {
public:
    EntityId createEntity(std::string name);
    bool removeEntity(EntityId id);
    bool setParent(EntityId child, EntityId parent);

    SceneNode* find(EntityId id);
    const SceneNode* find(EntityId id) const;

    [[nodiscard]] glm::mat4 worldMatrix(EntityId id) const;
    [[nodiscard]] std::vector<EntityId> roots() const;

private:
    EntityId nextId_{1};
    std::unordered_map<EntityId, SceneNode> entities_;
};
```

> **setParent 必须验证：** 禁止把实体设为自己的父节点；禁止形成祖先循环；换父节点时要同步旧父节点和新父节点的 children；删除节点时必须明确“级联删除”或“提升子节点”策略。V1 推荐级联删除，并由 DeleteEntityCommand 保存完整子树快照。

## 6.5 所有权设计

| 对象 | 拥有者 | 引用方式 |
| --- | --- | --- |
| SceneNode | Scene 容器按值拥有 | EntityId |
| MeshAsset / TextureAsset | AssetManager | shared_ptr<const Asset> 或 AssetId |
| GpuMesh / GpuTexture | Renderer 缓存 | unique_ptr / 值对象 |
| Selection | Editor SelectionModel | EntityId |
| QUndoCommand | QUndoStack | Qt 负责生命周期 |
| Qt Widget | Qt 父子对象机制 | QObject 指针仅限 UI 层 |

# 7. OpenGL 渲染系统

## 7.1 QOpenGLWidget 生命周期

| 阶段 | 职责 | 注意事项 |
| --- | --- | --- |
| initializeGL | 初始化函数表、Debug Logger、Renderer、基础 Shader、Grid | 此时 Context 有效 |
| resizeGL | 更新 Viewport 尺寸与投影矩阵 | 处理高度为 0 的情况 |
| paintGL | 清屏、绘制 Grid/Scene/Selection/Gizmo | 禁止做阻塞 I/O |
| 析构 | makeCurrent → 清理 GPU 资源 → doneCurrent | 不能在无 Context 时删除 GL 对象 |

## 7.2 Renderer 类划分

| 类 | 职责 |
| --- | --- |
| Renderer | 组织一帧渲染、管理状态和资源缓存 |
| ShaderProgram | 编译、链接、Uniform 设置、错误日志 |
| GpuMesh | VAO/VBO/EBO 与索引数量，RAII 释放 |
| GpuTexture | 纹理上传、参数与生命周期 |
| EditorCamera | Orbit/Pan/Zoom、View/Projection、屏幕射线 |
| GridRenderer | 无限/有限网格和主轴线 |
| SelectionRenderer | AABB 或描边高亮 |
| GizmoRenderer | 绘制与拾取 Move Gizmo |

## 7.3 每帧流程

```text
处理输入 → 更新相机 → 清屏
          ↓
绘制 Grid / Axis
          ↓
遍历可见 Entity，计算 WorldMatrix
          ↓
AssetId 查找/创建 GpuMesh
          ↓
绑定 Shader / Material / Texture 并 Draw
          ↓
绘制 Selection AABB
          ↓
绘制 Move Gizmo
```

## 7.4 第一版 Shader

| Shader | 用途 |
| --- | --- |
| mesh_lit.vert/.frag | 基础网格、法线、方向光、Base Color/Texture |
| grid.vert/.frag | 地面网格与主轴 |
| line.vert/.frag | AABB、坐标轴和 Gizmo 线框 |
| selection.vert/.frag | 选中对象的高亮或描边（P1） |

## 7.5 相机与输入

| 输入 | 行为 |
| --- | --- |
| 中键拖动 | Orbit 围绕 target 旋转 |
| Shift + 中键 | Pan 平移 target 与相机 |
| 滚轮 | Zoom / Dolly |
| F | 聚焦当前对象 AABB |
| 左键 | 选择对象或 Gizmo 轴 |
| W / E / R | 移动 / 旋转 / 缩放工具（V1 必做 W） |

## 7.6 OpenGL 调试与状态控制

- Debug 构建请求 Debug Context，并接入 QOpenGLDebugLogger。
- 每个 Shader 编译和链接失败必须输出源码文件名与驱动日志。
- 渲染入口显式设置 Depth Test、Cull Face、Blend 等状态，避免状态泄漏。
- Renderer 内部维护最小状态缓存；V1 不做过度优化。
- 禁止在非 Viewport Context 的线程创建或销毁 VAO/VBO/Texture。

# 8. 资源系统与 glTF 导入

## 8.1 V1 支持的 glTF 子集

| 类别 | 支持 | 暂不支持 |
| --- | --- | --- |
| 几何 | POSITION、NORMAL、TEXCOORD_0、indices | Morph Target、稀疏 Accessor 特殊优化 |
| 节点 | Node 层级、TRS/Matrix | 复杂 Scene 切换 |
| 材质 | baseColorFactor、baseColorTexture | 完整 Metallic-Roughness、扩展材质 |
| 纹理 | PNG/JPEG，外部文件或 GLB 内嵌 | KTX2/BasisU |
| 动画 | 无 | Skin、Animation、IK |
| 压缩 | 无 | Draco、Meshopt 扩展 |

## 8.2 导入流程

1. 规范化源文件路径，并建立 ImportContext。
2. fastgltf 解析 JSON/GLB、Buffer、Accessor、Image 和 Node。
3. 把顶点和索引转换为项目内部 MeshAsset，不让 fastgltf 类型泄漏到 Core。
4. 使用 QImage 从文件或内存数据解码纹理，统一翻转和颜色空间规则。
5. 为 Mesh、Texture、Material 生成 AssetId，并写入 AssetManager 缓存。
6. 根据 glTF Node 创建 SceneNode，保留父子层级与局部 Transform。
7. 导入完成后由 Editor 执行一个 ImportAssetCommand，便于撤销。

## 8.3 资源缓存

```cpp
class AssetManager {
public:
    ImportResult importGltf(const std::filesystem::path& path);

    std::shared_ptr<const MeshAsset> mesh(AssetId id) const;
    std::shared_ptr<const TextureAsset> texture(AssetId id) const;

private:
    AssetId nextId_{1};
    std::unordered_map<AssetId, std::shared_ptr<MeshAsset>> meshes_;
    std::unordered_map<AssetId, std::shared_ptr<TextureAsset>> textures_;
    std::unordered_map<std::filesystem::path, AssetId> sourceCache_;
};
```

## 8.4 错误处理

- 文件不存在、格式不支持、Accessor 越界、纹理解码失败时返回明确错误，不允许直接 assert 终止 Release。
- 单个材质或纹理失败时可降级为默认材质；几何数据不完整时终止该 Mesh 导入。
- 所有导入日志包含源文件、节点或 Mesh 名称以及失败阶段。
- 首批测试资产使用 Khronos glTF Sample Assets 中的 Box、BoxTextured、Duck 等静态模型。

# 9. 编辑器交互设计

*图 3  V1 编辑器界面线框（Word 版含图示）*

## 9.1 Scene Tree

- 使用 QTreeView + 自定义 QAbstractItemModel 映射 Scene，不把 Scene 数据复制到 QTreeWidgetItem。
- 支持选择、重命名、显示隐藏、删除、复制和拖动换父节点（换父可放 P1）。
- Tree 的 QModelIndex 内只保存 EntityId 或可恢复标识，不长期保存 SceneNode 裸指针。
- Scene 变更后由模型发送精确通知；初期可安全 reset，稳定后再优化增量更新。

## 9.2 Inspector

Inspector 根据 SelectionModel 中的 EntityId 读取当前对象，显示名称、可见性、Transform 和可选组件。数值编辑必须避免“UI 刷新再次触发业务写入”的循环，可使用更新保护标记或 QSignalBlocker。

## 9.3 SelectionModel

```text
Viewport 点击 / Scene Tree 点击
            ↓
      SelectionModel
            ↓
 ┌──────────┴──────────┐
 Scene Tree 高亮     Inspector 刷新
            ↓
      Viewport 高亮
```

## 9.4 Picking

1. 把鼠标像素坐标转换为 NDC。
2. 使用 inverseProjection 和 inverseView 生成世界空间 Ray。
3. 遍历可选择对象，将对象局部 AABB 变换到世界空间或把 Ray 变换到局部空间。
4. 执行 Ray/AABB 相交，选取最近的正向命中对象。
5. 后续需要精确选择时再增加 Ray/Triangle 或 GPU ID Buffer。

## 9.5 Move Gizmo 最小实现

- 绘制 X/Y/Z 三根轴，并为每根轴构建独立可拾取几何。
- 鼠标按下时确定轴、初始 Transform、拖动平面与初始交点。
- 鼠标移动时计算当前交点在轴方向上的投影差，实时更新对象位置。
- 鼠标释放时把 before/after 压入一条 TransformEntityCommand。
- V1 先实现 World Space；Local Space、吸附和多选延后。

## 9.6 快捷键

| 快捷键 | 功能 |
| --- | --- |
| Ctrl+N | 新建场景 |
| Ctrl+O | 打开场景 |
| Ctrl+S | 保存 |
| Ctrl+Shift+S | 另存为 |
| Ctrl+Z / Ctrl+Y | 撤销 / 重做 |
| Ctrl+D | 复制对象 |
| Delete | 删除对象 |
| F | 聚焦选中对象 |
| W / E / R | 移动 / 旋转 / 缩放工具 |
| Esc | 取消当前交互 |
| 1 / 3 / 7 | 前/侧/顶视图（P1） |

# 10. 场景文件与 Undo/Redo

## 10.1 场景格式

场景扩展名使用 .m3dscene，内部为 UTF-8 JSON。场景文件只保存编辑器需要恢复的逻辑状态和资源相对路径。

```json
{
  "format": "Mini3DScene",
  "version": 1,
  "editorCamera": {
    "position": [5.0, 4.0, 8.0],
    "target": [0.0, 0.0, 0.0]
  },
  "assets": [
    {"id": 1, "type": "gltf", "path": "Assets/Robot.glb"}
  ],
  "entities": [
    {
      "id": 10,
      "name": "Robot",
      "parent": 0,
      "visible": true,
      "transform": {
        "position": [0.0, 0.0, 0.0],
        "rotation": [1.0, 0.0, 0.0, 0.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "meshAsset": 1
    }
  ]
}
```

## 10.2 版本兼容

- 顶层必须包含 format 和 version。
- Reader 根据版本执行迁移；Writer 永远写当前版本。
- 未知字段忽略，必需字段缺失给出错误；禁止默默生成不可预测状态。
- 资源路径保存为项目目录相对路径；打开场景时处理路径不存在。

## 10.3 Command 设计

| Command | 保存的数据 |
| --- | --- |
| CreateEntityCommand | 新实体/子树快照与创建位置 |
| DeleteEntityCommand | 被删除子树完整快照及原父节点 |
| RenameEntityCommand | EntityId、beforeName、afterName |
| TransformEntityCommand | EntityId、beforeTransform、afterTransform |
| ReparentEntityCommand | EntityId、beforeParent、afterParent |
| ImportAssetCommand | 资源导入结果与创建的场景子树 |

> **拖动命令合并：** Gizmo 拖动期间只做实时预览；mouseRelease 时提交一条 TransformEntityCommand。Inspector 连续微调可按时间窗口或字段类型合并，避免 Undo 栈产生大量细碎记录。

## 10.4 文档脏状态

QUndoStack 的 clean index 与当前场景保存点绑定。任何可撤销操作使标题显示“*”；保存成功后 setClean()；关闭有未保存内容时弹出保存、丢弃、取消三选一。

# 11. 开发环境与首次构建

## 11.1 前置安装

1. 安装 Visual Studio 2022，勾选“使用 C++ 的桌面开发”和 Windows SDK。
2. 安装 Qt 6.11.2 MSVC 2022 64-bit（或 Qt 6.8 LTS 对应套件）。
3. 安装 Git、CMake 3.28+；Visual Studio 自带版本不足时单独安装。
4. 克隆并引导 vcpkg，设置环境变量 VCPKG_ROOT。
5. 可选安装 Qt Creator；主要调试环境推荐 Visual Studio 2022。

## 11.2 vcpkg.json

```json
{
  "name": "mini3d-studio",
  "version-string": "0.1.0",
  "dependencies": [
    "glm",
    "fastgltf",
    "nlohmann-json",
    "spdlog",
    "catch2"
  ]
}
```

第一次完整构建成功后执行版本基线更新并提交 builtin-baseline，保证后续环境可重复。Qt 建议通过官方安装器单独安装，不与其他依赖混用多个包管理器。

## 11.3 根 CMakeLists.txt 起点

```cmake
cmake_minimum_required(VERSION 3.28)

project(Mini3DStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets OpenGL OpenGLWidgets)
find_package(glm CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

add_subdirectory(src/core)
add_subdirectory(src/assets)
add_subdirectory(src/renderer_gl)
add_subdirectory(src/editor)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

## 11.4 CMakePresets.json 起点

```json
{
  "version": 6,
  "cmakeMinimumRequired": {"major": 3, "minor": 28, "patch": 0},
  "configurePresets": [
    {
      "name": "windows-msvc",
      "displayName": "Windows MSVC 2022",
      "generator": "Visual Studio 17 2022",
      "architecture": "x64",
      "binaryDir": "${sourceDir}/out/build/${presetName}",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
        "CMAKE_PREFIX_PATH": "$env{QT_ROOT}"
      }
    }
  ],
  "buildPresets": [
    {"name": "debug", "configurePreset": "windows-msvc", "configuration": "Debug"},
    {"name": "release", "configurePreset": "windows-msvc", "configuration": "Release"}
  ],
  "testPresets": [
    {"name": "test-debug", "configurePreset": "windows-msvc", "configuration": "Debug", "output": {"outputOnFailure": true}}
  ]
}
```

## 11.5 环境变量与命令

```powershell
$env:VCPKG_ROOT = "D:/dev/vcpkg"
$env:QT_ROOT = "C:/Qt/6.11.2/msvc2022_64"

cmake --preset windows-msvc
cmake --build --preset debug
ctest --preset test-debug
```

## 11.6 常见起步问题

| 问题 | 排查顺序 |
| --- | --- |
| find_package(Qt6) 失败 | 确认 QT_ROOT 指向包含 lib/cmake/Qt6 的套件目录；不要指向 Qt 总目录 |
| vcpkg 工具链未生效 | 确认 VCPKG_ROOT、CMAKE_TOOLCHAIN_FILE 在第一次 configure 前设置；删除旧 build 目录重配 |
| QOpenGLWidget 黑屏 | 检查 SurfaceFormat、initializeOpenGLFunctions、Shader 日志、Viewport 尺寸、VAO/VBO、Context 当前状态 |
| Debug 能跑 Release 崩溃 | 检查未初始化变量、对象生命周期、资源路径、断言依赖和 GL Context 析构 |
| 纹理上下颠倒 | 明确 UV 原点与 QImage 数据方向，只在导入层统一处理一次 |
| 模型太暗/全黑 | 检查法线、光方向、法线矩阵、颜色空间和默认材质 |

# 12. 第一周起步执行方案

## 12.1 每日任务

| 天 | 目标 | 交付物/提交 |
| --- | --- | --- |
| Day 1 | 创建仓库、README、scope/roadmap、根 CMake、vcpkg Manifest | chore: initialize repository and build configuration |
| Day 2 | QMainWindow、左右 Dock、底部 Console、中央占位 Viewport | feat: add editor shell and dock layout |
| Day 3 | QOpenGLWidget、OpenGL 4.1 Core、Debug Logger、清屏 | feat: initialize OpenGL viewport |
| Day 4 | ShaderProgram、三角形、Shader 错误日志 | feat: render first OpenGL triangle |
| Day 5 | GpuMesh、Cube、Depth Test、背面剔除 | feat: add cube mesh and depth rendering |
| Day 6 | EditorCamera、Orbit/Pan/Zoom、Resize | feat: add orbit camera controls |
| Day 7 | Grid、Axis、重构、首周测试、截图、Tag | release: v0.1.0-alpha editor viewport |

## 12.2 首周完成定义

- [ ] 全新克隆后，按照 README 可在一台干净开发机完成 Configure 和 Build。
- [ ] 窗口包含 Scene、Inspector、Console 三个区域和中央 Viewport。
- [ ] Viewport 可显示 Cube、地面网格和 XYZ 轴。
- [ ] 中键 Orbit、Shift+中键 Pan、滚轮 Zoom 可用。
- [ ] 调整窗口大小不变形、不崩溃。
- [ ] Shader 编译失败能在日志中看到完整错误。
- [ ] Debug 退出时不出现明显 OpenGL 资源泄漏或 Context 错误。
- [ ] README 放置一张首周截图，发布 v0.1.0-alpha 标签。

## 12.3 首周禁止事项

- 不接 glTF，不做 Scene Graph，不做 Gizmo。
- 不在第一天生成二十个空类；只有明确被当前里程碑使用的类才创建。
- 不花大量时间做图标、主题皮肤和动画。
- 不把所有 OpenGL 代码直接写进 MainWindow。

# 13. 八周里程碑计划

| 周 | 主要目标 | 演示结果 | 可裁剪项 |
| --- | --- | --- | --- |
| 第1周 | 编辑器壳、OpenGL、Camera、Grid、Cube | 像专业工具一样观察基础物体 | 无 |
| 第2周 | Renderer 抽象、基础几何、Shader、资源 RAII | Cube/Sphere/Plane 稳定绘制 | 高级材质 |
| 第3周 | Scene、EntityId、Scene Graph、Scene Tree、Inspector | 父子层级与 Transform 双向同步 | 拖动换父 |
| 第4周 | fastgltf、AssetManager、静态 GLB 导入 | Duck/Robot 模型进入树和视口 | 复杂材质/扩展 |
| 第5周 | SelectionModel、Ray/AABB、AABB 高亮、Fit | 点击模型并编辑 Transform | 轮廓描边 |
| 第6周 | Move Gizmo、TransformCommand、复制删除 | 视口拖动对象并撤销 | Rotate/Scale Gizmo |
| 第7周 | 方向光、基础材质、场景保存加载、Undo 栈 | 保存—关闭—打开完整恢复 | 最近文件/拖拽导入 |
| 第8周 | 测试、异常、性能记录、打包、README、视频 | v1.0.0 可投递版本 | 阴影/主题美化 |

## 13.1 卡期裁剪顺序

1. 先砍 Rotate/Scale Gizmo，保留 Inspector。
2. 再砍 Outline，使用 AABB 高亮。
3. 再砍 Asset Browser 拖拽，保留菜单导入。
4. 再砍阴影与高级材质，保留基础方向光。
5. 绝不砍 Scene Graph、glTF 导入、Picking、Move Gizmo、保存加载和 Undo；这些构成项目核心证明。

## 13.2 在职节奏建议

按每周 7—9 个有效小时规划：工作日前 4 次 × 1 小时完成小任务，周末一个 3—4 小时完整时段完成整合、调试和录屏。项目忙导致某周投入不足时，不追赶新功能，优先保证当前里程碑可运行。

# 14. 测试、质量与性能

## 14.1 单元测试重点

| 测试对象 | 关键用例 |
| --- | --- |
| Transform | 平移/旋转/缩放矩阵；四元数单位化；父子组合 |
| Scene Graph | 创建、删除、换父；阻止循环；级联删除；Roots |
| Ray/AABB | 命中、错过、从内部出发、平行轴、负方向 |
| 序列化 | 保存后读取等价；未知字段；缺失资源；版本迁移 |
| Asset Cache | 相同规范路径不重复创建；不存在资源返回错误 |
| Commands | redo/undo 对称；Transform 拖动只产生一次历史 |
| Path | 绝对/相对、分隔符、大小写与项目迁移 |

## 14.2 手工回归清单

- [ ] 空场景打开、保存、关闭均不崩溃。
- [ ] 导入正确 GLB、缺失文件、损坏文件和不支持扩展时提示清楚。
- [ ] 创建 100 个 Cube 后选择、删除、撤销仍正确。
- [ ] 父节点带非均匀缩放时，子节点显示符合约定并记录限制。
- [ ] Viewport 连续缩放、最大化、拖到另一显示器后仍可绘制。
- [ ] 保存后移动项目目录，使用相对路径的资源仍能恢复。
- [ ] 连续 Undo 到初始状态，再 Redo 到末尾，不出现 ID 冲突。
- [ ] Release 构建和打包机器上可运行，不依赖开发机绝对路径。

## 14.3 性能记录

| 指标 | 记录方式 |
| --- | --- |
| 启动时间 | Release 从启动到空场景可交互 |
| 模型导入 | 记录文件大小、三角形数、耗时和峰值内存 |
| 帧率 | 1920×1080 下 10万/50万/100万三角形 |
| Draw Calls | 统计一帧可见对象与 Draw 次数 |
| GPU 上传 | 首次显示 Mesh 的上传耗时 |
| 场景保存/打开 | 实体数量与耗时 |
| 稳定性 | 连续操作 30 分钟、反复导入/删除/Undo |

> **性能描述规则：** 简历和 README 不写“性能显著提升”这类空结论。必须写参考机器、模型规模、分辨率、平均/最低 FPS、加载耗时以及优化前后数据。

# 15. 风险与应对

| 风险 | 表现 | 应对 |
| --- | --- | --- |
| 范围失控 | 不断加入 PBR、动画、CAD、Vulkan | scope.md 固定 P0/P1，任何新增能力进入 V1.1 |
| OpenGL Context 错误 | 退出崩溃、黑屏、资源泄漏 | GPU 对象集中由 Renderer 管理；析构前 makeCurrent |
| Gizmo 卡住 | 第6周长期无法完成 | 先交 Move World Space；使用 AABB 拾取；Rotate/Scale 延后 |
| glTF 兼容面过大 | 某些复杂模型始终失败 | 只承诺明确子集；用官方固定样例做回归 |
| Scene 与 UI 耦合 | 树、视口、Inspector 状态混乱 | 引入 SelectionModel 和 Scene 事件；UI 不直接互相调用 |
| AI 生成代码失控 | 代码能跑但无法解释 | 每个核心模块先写接口/不变量，再让 AI 实现局部；合并前口述解释 |
| 工作加班打断 | 计划连续中断 | 拆成 30—60 分钟 Issue；每周保证一个可运行 Tag |
| 依赖版本漂移 | 换机器无法构建 | 固定 vcpkg baseline、Qt 基线、Presets 和构建说明 |

# 16. AI 辅助开发工作流

## 16.1 AI 可以承担

- Qt/CMake/OpenGL API 样板和错误信息检索。
- 为已确定接口补实现、测试用例和边界检查。
- 对崩溃堆栈、Shader 日志和编译链接错误提出排查顺序。
- 生成 README 草稿、Issue 拆分、代码审查清单和重构建议。
- 解释第三方库文档，但最终以官方文档和本地验证为准。

## 16.2 必须由开发者掌握

- [ ] Scene Graph 为什么用 EntityId，父子关系如何保证不形成环。
- [ ] Local/World Matrix、View/Projection、屏幕射线和 Ray/AABB 的完整数学链路。
- [ ] VAO/VBO/EBO、Shader、Texture 在哪个 Context 创建和销毁。
- [ ] MeshAsset 与 GpuMesh 为什么分离，资源缓存由谁拥有。
- [ ] 一次 Gizmo 拖动为何只产生一条 Command。
- [ ] Scene JSON 如何做版本、ID 恢复、相对路径和错误处理。

## 16.3 单任务提示词模板

```text
目标：实现 Ray/AABB 相交函数。
上下文：右手坐标系，Ray 已归一化；AABB 在对象局部空间。
现有接口：bool intersectRayAabb(const Ray&, const Aabb&, float& distance)。
约束：C++20、无额外依赖、不得修改公开接口。
验收：命中/错过/射线从盒内出发/平行轴/负方向均有 Catch2 测试。
先输出算法和边界条件，再给实现；不要生成项目其他文件。
```

## 16.4 每日技术沉淀

每完成一个核心 Issue，在 docs/devlog/YYYY-MM-DD.md 记录：问题、错误现象、根因、最终设计、验证方式、AI 提供但未采用的方案。面试前这些记录可以直接转成项目故事和故障排查案例。

# 17. Git、Issue 与交付流程

## 17.1 分支和提交

- 个人项目采用 main + 短生命周期 feature 分支；每个 Issue 一个分支，完成后合并。
- 提交前必须通过格式化、编译和相关测试；一个提交只表达一个意图。
- 里程碑结束打 Tag：v0.1.0-alpha、v0.3.0-scene、v0.5.0-import、v1.0.0。

| 前缀 | 使用场景 | 示例 |
| --- | --- | --- |
| feat | 新功能 | feat: add scene hierarchy model |
| fix | 缺陷修复 | fix: release GL buffers with active context |
| refactor | 不改变行为的重构 | refactor: split camera from viewport widget |
| test | 测试 | test: cover cyclic reparent validation |
| docs | 文档 | docs: add glTF supported subset |
| build | 构建依赖 | build: add fastgltf through vcpkg |
| chore | 其他维护 | chore: update clang-format rules |

## 17.2 Issue 模板

```markdown
## 目标
一句话说明用户可见或工程结果。

## 设计边界
- 允许修改的模块：
- 不允许修改的接口：

## 验收标准
- [ ] Debug / Release 构建通过
- [ ] 单元测试或手工用例通过
- [ ] 错误路径已验证
- [ ] 文档 / 截图已更新

## 技术说明
对象所有权、线程/Context、坐标空间和潜在风险。
```

## 17.3 Definition of Done

- [ ] 功能符合 Issue 验收标准。
- [ ] Debug 和 Release 均可构建。
- [ ] 新增 Core 逻辑有测试，UI/渲染功能有手工回归记录。
- [ ] 无明显新编译警告，Shader 和运行日志无未处理错误。
- [ ] AI 生成代码已逐段审阅，开发者能够解释关键实现。
- [ ] README、architecture 或 devlog 已同步更新。
- [ ] 提交信息清楚，未混入无关格式化和大范围改动。

# 18. 作品集与求职交付

## 18.1 仓库必须包含

- [ ] README：定位、核心功能、截图、GIF、构建步骤、架构图、技术难点。
- [ ] docs/scope.md、architecture.md、roadmap.md 与 devlog。
- [ ] Windows Release 压缩包和 v1.0.0 Release。
- [ ] 2—3 分钟演示视频：导入、选择、层级、Gizmo、Undo、保存重开。
- [ ] 至少一组性能数据和一组异常输入演示。
- [ ] 第三方依赖和样例模型许可证说明。

## 18.2 演示视频脚本

1. 10 秒介绍项目定位和技术栈。
2. 20 秒展示 Dock 布局、相机、Grid 和基础物体。
3. 30 秒导入 Robot.glb，并展示 glTF 层级进入 Scene Tree。
4. 30 秒在 Viewport 选择模型、Inspector 改数值、Move Gizmo 拖动。
5. 20 秒创建父子层级并移动父节点。
6. 20 秒执行删除、Undo、Redo。
7. 20 秒保存场景、关闭、重新打开并恢复状态。
8. 20 秒展示架构图、单元测试和性能记录。

## 18.3 简历描述参考

完成 V1 后，可在简历中使用以下表述，并根据真实完成度删改：

- 基于 C++20、Qt6 与 OpenGL 从零开发轻量级三维场景编辑器，完成可停靠式桌面界面、三维视口、场景树与属性面板。
- 设计基于 EntityId 的 Scene Graph，实现父子层级、Local/World Transform、对象管理与场景序列化。
- 实现 glTF/GLB 静态模型导入与 CPU/GPU 资源缓存，支持位置、法线、UV、索引、基础材质和纹理。
- 基于屏幕射线与 AABB 相交实现对象拾取，并完成 Transform Inspector 与三维移动 Gizmo。
- 基于 Command 模式和 QUndoStack 实现创建、删除和变换操作的撤销重做，并记录可复现构建与性能测试。

# 19. V1.0 最终验收清单

- [ ] 全新环境按 README 能完成依赖恢复、Configure、Build 和 Run。
- [ ] 窗口具备菜单、工具栏、Scene Tree、Viewport、Inspector、Console 和状态栏。
- [ ] 相机支持 Orbit、Pan、Zoom、Fit；Viewport 有 Grid 和坐标轴。
- [ ] 可创建 Cube、Sphere、Plane、Empty、Camera 和 Directional Light。
- [ ] Scene Graph 的父子 Transform 正确，不能形成循环。
- [ ] 可导入静态 .gltf/.glb，至少通过 Box、BoxTextured、Duck 与一个工业模型。
- [ ] Scene Tree 和 Viewport 选择双向同步；选中对象有 AABB 或高亮反馈。
- [ ] Inspector 可编辑 Position、Rotation、Scale；Move Gizmo 可拖动 X/Y/Z。
- [ ] 支持显示隐藏、重命名、复制和删除。
- [ ] 支持基础颜色、纹理、方向光和环境光。
- [ ] 支持新建、打开、保存、另存为和未保存提示。
- [ ] 支持创建、删除、重命名和 Transform 的 Undo/Redo。
- [ ] 损坏模型、缺失纹理、缺失场景资源等错误有清晰提示且不崩溃。
- [ ] Core 测试通过，Release 包可在无开发环境机器运行。
- [ ] GitHub/Git 仓库有 v1.0.0 Tag、README、截图、GIF、演示视频和许可证说明。

# 20. V1.1 / V2 演进方向

| 版本 | 优先能力 | 职业方向 |
| --- | --- | --- |
| V1.1 | Rotate/Scale Gizmo、Stencil Outline、阴影、简单 PBR、拖拽导入、多视图 | 图形/编辑器能力深化 |
| V1.2 | OBJ/STL、资源热重载、场景实例、性能分析面板 | 通用三维工具 |
| V2-Industrial | 测量、剖切、标注、点云、ROI、标量场插件 | 工业三维/数字孪生 |
| V2-CAD | OpenCASCADE、STEP/IGES、拓扑选择 | CAD/CAE 前处理 |
| V2-CAE | VTK 结果云图、网格和结果场插件 | CAE 后处理 |
| V2-Backend | 抽象 Renderer API，增加 Vulkan/QRhi 后端 | 图形引擎方向 |

> **推荐的下一步：** V1 完成后，优先增加“工业测量 + 剖切 + 点云/标量场”中的一个，而不是继续模仿 Blender 的雕刻和动画。这样更能衔接 C++、Qt、OpenGL 与工业软件岗位。

# 附录 A：首批类与文件清单

| 阶段 | 建议文件 |
| --- | --- |
| 第1周 | MainWindow、ViewportWidget、Renderer、ShaderProgram、GpuMesh、EditorCamera、GridRenderer |
| 第2周 | PrimitiveFactory、GpuTexture、Material、Aabb、Ray |
| 第3周 | EntityId、Transform、SceneNode、Scene、SceneTreeModel、SelectionModel、TransformInspector |
| 第4周 | AssetId、MeshAsset、TextureAsset、MaterialAsset、AssetManager、GltfImporter |
| 第5周 | RayCaster、SelectionRenderer、FocusController |
| 第6周 | GizmoRenderer、GizmoController、TransformEntityCommand |
| 第7周 | SceneSerializer、Document、Create/Delete/RenameCommand、RecentFiles |
| 第8周 | Packaging、PerformanceRecorder、RegressionChecklist |

# 附录 B：初始 Backlog

| ID | 任务 | 优先级 | 依赖 |
| --- | --- | --- | --- |
| INF-001 | 创建仓库、许可证、README 和 docs | P0 | 无 |
| INF-002 | 配置 CMakePresets 与 vcpkg Manifest | P0 | INF-001 |
| EDT-001 | 创建 QMainWindow 与 Dock 布局 | P0 | INF-002 |
| RND-001 | 初始化 QOpenGLWidget 与 Debug Context | P0 | EDT-001 |
| RND-002 | 实现 ShaderProgram 与三角形 | P0 | RND-001 |
| RND-003 | 实现 GpuMesh 与 Cube | P0 | RND-002 |
| RND-004 | 实现 EditorCamera 与 Grid | P0 | RND-003 |
| CORE-001 | 实现 EntityId、Transform、Scene | P0 | RND-004 |
| EDT-002 | 实现 SceneTreeModel 与 SelectionModel | P0 | CORE-001 |
| EDT-003 | 实现 Transform Inspector | P0 | EDT-002 |
| AST-001 | 实现 MeshAsset 与 AssetManager | P0 | CORE-001 |
| AST-002 | 接入 fastgltf 并导入 Box | P0 | AST-001 |
| AST-003 | 导入 Node 层级、纹理和材质 | P0 | AST-002 |
| INT-001 | 实现 Ray/AABB Picking | P0 | EDT-002,RND-004 |
| INT-002 | 实现 Selection AABB | P0 | INT-001 |
| INT-003 | 实现 Move Gizmo | P0 | INT-001 |
| CMD-001 | 接入 QUndoStack 与 TransformCommand | P0 | INT-003 |
| IO-001 | 实现 .m3dscene 保存/加载 | P0 | CORE-001,AST-001 |
| QA-001 | 核心测试与异常输入回归 | P0 | 全模块 |
| REL-001 | Release 打包、README、GIF 和视频 | P0 | QA-001 |

# 附录 C：核心代码审查清单

- [ ] 这个类的唯一职责是什么？是否应该拆分？
- [ ] 对象由谁拥有？是否存在 shared_ptr 滥用、循环引用或悬空指针？
- [ ] OpenGL 调用发生时 Context 是否有效？资源销毁顺序是否明确？
- [ ] 接口中的坐标属于 Local、World、View、Clip 还是 Screen？单位是什么？
- [ ] 错误输入如何返回？Release 是否仍然安全？
- [ ] 是否把第三方类型泄漏到 Core 公共接口？
- [ ] Scene 修改是否通过 Command 或明确的服务入口？
- [ ] 是否存在 UI 信号回写循环或选择状态多源问题？
- [ ] 是否有最小测试或可重复的手工验证步骤？
- [ ] AI 生成的每一段代码是否经过本地编译、调试和解释？

# 附录 D：官方参考资料

| 编号 | 资料 | 用途 |
| --- | --- | --- |
| R1 | Qt 6.11.2 Released | 项目 Qt 基线版本 |
| R2 | Qt 6 Supported Platforms / Qt for Windows | Windows 与 MSVC 2022 支持矩阵 |
| R3 | QOpenGLWidget 文档 | Qt Widgets 中的 OpenGL 生命周期 |
| R4 | QMainWindow / QDockWidget / QTreeView / QUndoStack 文档 | 编辑器 UI 与撤销栈 |
| R5 | glTF 2.0 Specification | 模型格式规范 |
| R6 | fastgltf Repository / vcpkg port | glTF 解析库与依赖安装 |
| R7 | GLM Repository | 图形数学库 |
| R8 | nlohmann/json、spdlog、Catch2 Repository | 序列化、日志和测试 |
| R9 | vcpkg Manifest Mode | 项目依赖声明和基线锁定 |
| R10 | CMake Presets Manual | 共享 Configure/Build/Test 配置 |

参考链接已在 Word 文档末尾以可点击超链接列出；实际开发时优先查官方文档与库仓库，不以过时博客代码作为唯一依据。

## 参考链接

- [Qt 6.11.2 Released](https://www.qt.io/blog/qt-6.11.2-released)
- [Qt Supported Platforms](https://doc.qt.io/qt-6/supported-platforms.html)
- [QOpenGLWidget](https://doc.qt.io/qt-6/qopenglwidget.html)
- [QMainWindow](https://doc.qt.io/qt-6/qmainwindow.html)
- [QDockWidget](https://doc.qt.io/qt-6/qdockwidget.html)
- [QTreeView](https://doc.qt.io/qt-6/qtreeview.html)
- [QUndoStack](https://doc.qt.io/qt-6/qundostack.html)
- [glTF 2.0 Specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
- [fastgltf](https://github.com/spnda/fastgltf)
- [GLM](https://github.com/g-truc/glm)
- [nlohmann/json](https://github.com/nlohmann/json)
- [spdlog](https://github.com/gabime/spdlog)
- [Catch2](https://github.com/catchorg/Catch2)
- [vcpkg Manifest Mode](https://learn.microsoft.com/en-us/vcpkg/concepts/manifest-mode)
- [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
