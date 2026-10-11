# 模块名: mini3d_renderer_gl 构建定义
# 功能概述: 声明 OpenGL Viewport 模块及其 Qt 图形依赖。
# 对外接口: mini3d_renderer_gl 静态库 Target
# 依赖关系: Qt6::OpenGL、Qt6::OpenGLWidgets、mini3d_project_options
# 输入输出: 输入 Viewport 源码，输出供编辑器链接的 OpenGL 渲染库。
# 异常与错误: 依赖或源码缺失时由 CMake 在 Configure/Build 阶段报告。
# 维护说明: 只加入当前里程碑实际使用的渲染源码。

find_package(OpenColorIO 2.5.0 EXACT CONFIG REQUIRED GLOBAL)
# 此固定共享 OCIO 构建的运行时依赖；按配置部署，不把 Debug 映射到 Release。
find_package(Imath 3.2.2 EXACT CONFIG REQUIRED GLOBAL)
find_package(openjph 0.25.2 EXACT CONFIG REQUIRED GLOBAL)
find_package(OpenEXR 3.4.10 EXACT CONFIG REQUIRED GLOBAL)
find_package(ZLIB 1.3.1 EXACT REQUIRED)

qt_add_library(
    mini3d_renderer_gl
    STATIC
    EditorCamera.cpp
    EditorCamera.h
    GpuMesh.cpp
    GpuMesh.h
    GpuTexture.cpp
    GpuTexture.h
    Material.h
    GridRenderer.cpp
    GridRenderer.h
    MeshData.h
    PrimitiveFactory.cpp
    PrimitiveFactory.h
    RayCaster.cpp
    RayCaster.h
    InstalledPose.cpp InstalledPose.h
    FrozenStateRenderer.cpp FrozenStateRenderer.h
    OcioDisplayTransform.cpp OcioDisplayTransform.h
    ImageFrame.cpp ImageFrame.h
    ImageFrameOutput.cpp ImageFrameOutput.h
    SelectionRenderer.cpp
    SelectionRenderer.h
    ComponentOverlayRenderer.cpp ComponentOverlayRenderer.h
    GizmoController.cpp GizmoController.h
    GizmoRenderer.cpp GizmoRenderer.h
    Renderer.cpp
    Renderer.h
    ShaderProgram.cpp
    ShaderProgram.h
    ViewportWidget.cpp
    ViewportWidget.h
    ViewNavigationWidget.cpp
    ViewNavigationWidget.h
)

set(MINI3D_SHADER_DIRECTORY "${PROJECT_SOURCE_DIR}/assets/shaders")
set_target_properties(mini3d_renderer_gl PROPERTIES AUTOMOC ON)
qt_add_resources(mini3d_renderer_gl renderer_color_config
    PREFIX "/mini3d/color"
    BASE "${PROJECT_SOURCE_DIR}/assets/color"
    FILES "${PROJECT_SOURCE_DIR}/assets/color/mini3d-standard.ocio")
qt_add_resources(
    mini3d_renderer_gl
    renderer_shaders
    PREFIX
        "/mini3d/shaders"
    BASE
        "${MINI3D_SHADER_DIRECTORY}"
    FILES
        "${MINI3D_SHADER_DIRECTORY}/grid.frag"
        "${MINI3D_SHADER_DIRECTORY}/infinite_grid.vert"
        "${MINI3D_SHADER_DIRECTORY}/infinite_grid.frag"
        "${MINI3D_SHADER_DIRECTORY}/gizmo.vert"
        "${MINI3D_SHADER_DIRECTORY}/gizmo.frag"
        "${MINI3D_SHADER_DIRECTORY}/grid.vert"
        "${MINI3D_SHADER_DIRECTORY}/component.vert"
        "${MINI3D_SHADER_DIRECTORY}/component.frag"
        "${MINI3D_SHADER_DIRECTORY}/mesh.frag"
        "${MINI3D_SHADER_DIRECTORY}/mesh.vert"
)

target_include_directories(mini3d_renderer_gl PUBLIC "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(
    mini3d_renderer_gl
    PUBLIC
        mini3d_assets
        mini3d_core
        glm::glm
        Qt6::OpenGL
        Qt6::OpenGLWidgets
    PRIVATE
        mini3d_project_options
        OpenColorIO::OpenColorIO
        OpenEXR::OpenEXR
        ZLIB::ZLIB
)
