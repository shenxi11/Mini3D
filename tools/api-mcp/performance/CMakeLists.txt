# 模块名: API/MCP 性能独立构建
# 功能概述: 复用冻结 Release 库，单独构建正式实现和插桩测量探针。
# 对外接口: mini3d_api_performance、mini3d_api_performance_instrumented。
# 依赖关系: 已构建 Mini3D 静态库、Qt 6、vcpkg 依赖。
# 输入输出: 测量源码及测试副本到独占目录中的可执行程序。
# 异常与错误: 缺少库或插桩副本时配置失败。
# 维护说明: 不接入主体默认 build，不修改生产源码。
cmake_minimum_required(VERSION 3.28)
project(Mini3DApiPerformance LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(MINI3D_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "Mini3D repository")
set(MINI3D_BUILD "${MINI3D_ROOT}/out/build/windows-msvc-local" CACHE PATH "Existing Release build")
set(PERFORMANCE_GENERATED "" CACHE PATH "Instrumented test-only source directory")
find_package(Qt6 REQUIRED COMPONENTS Widgets Network OpenGL OpenGLWidgets)
find_package(glm CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
foreach(target IN ITEMS mini3d_api_performance mini3d_api_performance_instrumented)
    add_executable(${target} PerformanceProbe.cpp)
    target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus)
    target_include_directories(${target} PRIVATE
        "${MINI3D_ROOT}/src" "${MINI3D_BUILD}/generated" "${CMAKE_CURRENT_LIST_DIR}")
    target_link_libraries(${target} PRIVATE
        "${MINI3D_BUILD}/src/editor/Release/mini3d_editor_ui.lib"
        "${MINI3D_BUILD}/src/editor/mini3d_editor_ui_resources_1.dir/Release/mini3d_editor_ui_resources_1.lib"
        "${MINI3D_BUILD}/src/renderer_gl/Release/mini3d_renderer_gl.lib"
        "${MINI3D_BUILD}/src/renderer_gl/mini3d_renderer_gl_resources_1.dir/Release/mini3d_renderer_gl_resources_1.lib"
        "${MINI3D_BUILD}/src/assets/Release/mini3d_assets.lib"
        "${MINI3D_BUILD}/src/core/Release/mini3d_core.lib"
        Qt6::Widgets Qt6::Network Qt6::OpenGL Qt6::OpenGLWidgets
        glm::glm fastgltf::fastgltf spdlog::spdlog advapi32 bcrypt psapi)
endforeach()
if(NOT EXISTS "${PERFORMANCE_GENERATED}/LocalAutomationBridge.cpp")
    message(FATAL_ERROR "Run instrument.mjs into the verified scratch directory first")
endif()
target_sources(mini3d_api_performance_instrumented PRIVATE
    "${PERFORMANCE_GENERATED}/LocalAutomationBridge.cpp"
    "${PERFORMANCE_GENERATED}/AutomationFrame.cpp"
    "${PERFORMANCE_GENERATED}/ApiJsonCodec.cpp"
    "${PERFORMANCE_GENERATED}/ObservationService.cpp")
target_include_directories(mini3d_api_performance_instrumented PRIVATE
    "${MINI3D_ROOT}/src/editor/automation" "${MINI3D_ROOT}/src/editor/api"
    "${MINI3D_ROOT}/src/editor/observation")
target_compile_definitions(mini3d_api_performance_instrumented PRIVATE PERFORMANCE_INSTRUMENTED=1)
