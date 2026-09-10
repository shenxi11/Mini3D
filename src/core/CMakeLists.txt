# 模块名: mini3d_core 构建定义
# 功能概述: 编译当前已实现的纯数学包围盒和射线模块。
# 对外接口: mini3d_core
# 依赖关系: GLM、mini3d_project_options
# 输入输出: 输入数学源码，输出供 Renderer 与单元测试共享的静态库。
# 异常与错误: 由 CMake/编译器报告。
# 维护说明: 禁止引入 Qt 或 OpenGL 依赖。
add_library(mini3d_core STATIC Aabb.cpp Aabb.h Ray.cpp Ray.h
    EntityId.h AssetId.h MeshData.h Transform.h Transform.cpp SceneNode.h Scene.h Scene.cpp
    Appearance.h SceneSerializer.h SceneSerializer.cpp)
target_include_directories(mini3d_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mini3d_core PUBLIC glm::glm PRIVATE mini3d_project_options nlohmann_json::nlohmann_json)
