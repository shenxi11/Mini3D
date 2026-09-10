# 模块名: mini3d_assets
# 功能概述: 编译 CPU 导入与资源管理，不链接 OpenGL。
# 对外接口: mini3d_assets
# 依赖关系: Core、Qt Gui、fastgltf
# 输入输出: CPU 资源静态库
# 异常与错误: 由 CMake/编译器报告。
# 维护说明: 第三方解析类型不进入公共接口。
add_library(mini3d_assets STATIC
    MeshAsset.h TextureAsset.h MaterialAsset.h
    GltfImporter.h GltfImporter.cpp AssetManager.h AssetManager.cpp
    SceneDocument.h SceneDocument.cpp)
target_include_directories(mini3d_assets PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(mini3d_assets PUBLIC mini3d_core Qt6::Gui
    PRIVATE fastgltf::fastgltf mini3d_project_options)
