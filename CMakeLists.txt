cmake_minimum_required(VERSION 3.28)

include("${CMAKE_CURRENT_SOURCE_DIR}/CMakeLocalConfig.cmake" OPTIONAL)

project(Mini3DStudio VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Mini3D Studio V1 currently supports Windows x64 only.")
endif()

if(NOT MSVC OR MSVC_VERSION LESS 1930)
    message(FATAL_ERROR "Mini3D Studio V1 requires the MSVC 2022 toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MINI3D_ENABLE_FORMAT_TARGET "Enable the clang-format helper target" ON)

find_package(Qt6 6.8 CONFIG REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(glm CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.8 CONFIG REQUIRED COMPONENTS Test)
    find_package(Catch2 CONFIG REQUIRED)
endif()

add_library(mini3d_project_options INTERFACE)
target_compile_features(mini3d_project_options INTERFACE cxx_std_20)
target_compile_options(
    mini3d_project_options
    INTERFACE
        /W4
        /utf-8
        /permissive-
        /Zc:__cplusplus
)

add_subdirectory(src/core)
add_subdirectory(src/assets)
add_subdirectory(src/renderer_gl)
add_subdirectory(src/editor)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

option(MINI3D_BUILD_TOOLS "Build opt-in release performance tools" OFF)
if(MINI3D_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(MINI3D_ENABLE_FORMAT_TARGET)
    find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
    if(CLANG_FORMAT_EXECUTABLE)
        file(
            GLOB_RECURSE MINI3D_FORMAT_SOURCES
            CONFIGURE_DEPENDS
            "${CMAKE_SOURCE_DIR}/src/*.cpp"
            "${CMAKE_SOURCE_DIR}/src/*.cc"
            "${CMAKE_SOURCE_DIR}/src/*.cxx"
            "${CMAKE_SOURCE_DIR}/src/*.h"
            "${CMAKE_SOURCE_DIR}/src/*.hpp"
            "${CMAKE_SOURCE_DIR}/tests/*.cpp"
        )

        if(MINI3D_FORMAT_SOURCES)
            add_custom_target(
                format
                COMMAND ${CLANG_FORMAT_EXECUTABLE} -i --style=file ${MINI3D_FORMAT_SOURCES}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                COMMENT "Formatting Mini3D Studio sources"
                VERBATIM
            )
        endif()
    endif()
endif()
