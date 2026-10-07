# 模块名: mini3d_core 构建定义
# 功能概述: 编译当前已实现的纯数学包围盒和射线模块。
# 对外接口: mini3d_core
# 依赖关系: GLM、mini3d_project_options
# 输入输出: 输入数学源码，输出供 Renderer 与单元测试共享的静态库。
# 异常与错误: 由 CMake/编译器报告。
# 维护说明: 禁止引入 Qt 或 OpenGL 依赖。
add_library(mini3d_core STATIC Aabb.cpp Aabb.h Ray.cpp Ray.h
    EntityId.h AssetId.h MeshData.h Transform.h Transform.cpp SceneNode.h SceneCollection.h Scene.h Scene.cpp
    Appearance.h SceneSerializer.h SceneSerializer.cpp
    modeling/EditableMesh.h modeling/EditableMesh.cpp
    modeling/MeshValidation.h modeling/MeshValidation.cpp
    modeling/MeshTopology.h modeling/MeshTopology.cpp
    modeling/MeshDerivation.h modeling/MeshDerivation.cpp
    modeling/VertexTransform.h modeling/VertexTransform.cpp
    modeling/ExtrudeRegion.h modeling/ExtrudeRegion.cpp
    modeling/InsetFace.h modeling/InsetFace.cpp
    modeling/LoopCut.h modeling/LoopCut.cpp
    modeling/DeleteComponents.h modeling/DeleteComponents.cpp
    modeling/FillFace.h modeling/FillFace.cpp)
target_sources(mini3d_core PRIVATE Animation.h Animation.cpp EvaluatedPose.h EvaluatedPose.cpp)
target_sources(mini3d_core PRIVATE NativeScenePreflight.h NativeScenePreflight.cpp)
target_sources(mini3d_core PRIVATE modeling/Mirror.h modeling/Mirror.cpp)
target_sources(mini3d_core PRIVATE modeling/ObjExporter.h modeling/ObjExporter.cpp)
target_sources(mini3d_core PRIVATE modeling/TopologySelection.h modeling/TopologySelection.cpp)
target_sources(mini3d_core PRIVATE modeling/ProportionalTransform.h modeling/ProportionalTransform.cpp)
target_sources(mini3d_core PRIVATE modeling/BevelEdge.h modeling/BevelEdge.cpp)
target_sources(mini3d_core PRIVATE modeling/Subdivision.h modeling/Subdivision.cpp)
target_include_directories(mini3d_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mini3d_core PUBLIC glm::glm PRIVATE mini3d_project_options nlohmann_json::nlohmann_json)
