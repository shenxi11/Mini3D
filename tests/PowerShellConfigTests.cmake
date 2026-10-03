# 模块名: PowerShell 配置回归
# 功能概述: 独立验证旧缓存、旧安装提示、Store 别名和缺少安装时的行为。
# 对外接口: cmake -P tests/PowerShellConfigTests.cmake；CASE 供隔离子进程使用。
# 依赖关系: Windows、CMake 3.28+、PowerShell 7.6。
# 输入输出: 当前安装到四项配置断言，不编译业务代码、不改实际构建缓存。
# 异常与错误: 路径未刷新、探测失败或缺少安装未报错时终止。
# 维护说明: 使用独立 cmake -P 进程隔离环境；不修改系统 PATH。
cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED CASE)
    foreach(_case IN ITEMS stale alias explicit missing)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" "-DCASE=${_case}" -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _error
            TIMEOUT 30
        )
        if(_case STREQUAL "missing")
            if("${_result}" STREQUAL "0" OR NOT _error MATCHES "Mini3D requires PowerShell 7.6")
                message(FATAL_ERROR "Missing installation was not rejected: ${_result}\n${_output}${_error}")
            endif()
        elseif(NOT "${_result}" STREQUAL "0")
            message(FATAL_ERROR "${_case} failed: ${_result}\n${_output}${_error}")
        endif()
        message(STATUS "PowerShell config: ${_case} passed")
    endforeach()
    return()
endif()

set(_module "${CMAKE_CURRENT_LIST_DIR}/../cmake/Mini3DPowerShell.cmake")
set(_removed "${CMAKE_CURRENT_LIST_DIR}/removed-powershell-installation")
if(EXISTS "${_removed}")
    message(FATAL_ERROR "Test requires a nonexistent installation path")
endif()
set(Z_VCPKG_PWSH_PATH "${_removed}/pwsh.exe" CACHE FILEPATH "Old test cache" FORCE)
set(Z_VCPKG_POWERSHELL_PATH "${_removed}/pwsh.exe" CACHE INTERNAL "Old test cache" FORCE)

if(CASE STREQUAL "missing")
    set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
    set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)
    set(ENV{LOCALAPPDATA} "${_removed}")
    set(ENV{ProgramFiles} "${_removed}")
elseif(CASE STREQUAL "stale")
    set(MINI3D_LOCAL_PWSH_DIRECTORY "${_removed}")
    set(ENV{PATH} "${_removed};$ENV{PATH}")
elseif(CASE STREQUAL "alias")
    # 即使 IDE 进程没有 PowerShell PATH，也必须能从稳定别名找到真实安装。
    set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
    set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)
    set(ENV{ProgramFiles} "${_removed}")
elseif(CASE STREQUAL "explicit")
    include("${_module}")
    get_filename_component(MINI3D_LOCAL_PWSH_DIRECTORY "${Z_VCPKG_PWSH_PATH}" DIRECTORY)
    set(_expected "${Z_VCPKG_PWSH_PATH}")
    set(Z_VCPKG_PWSH_PATH "${_removed}/pwsh.exe" CACHE FILEPATH "Old test cache" FORCE)
    set(Z_VCPKG_POWERSHELL_PATH "${_removed}/pwsh.exe" CACHE INTERNAL "Old test cache" FORCE)
endif()

include("${_module}")
if(NOT EXISTS "${Z_VCPKG_PWSH_PATH}" OR NOT Z_VCPKG_PWSH_PATH STREQUAL Z_VCPKG_POWERSHELL_PATH)
    message(FATAL_ERROR "Both vcpkg cache entries must use the same existing executable")
endif()
if(Z_VCPKG_PWSH_PATH MATCHES "/Microsoft/WindowsApps/" OR Z_VCPKG_PWSH_PATH MATCHES "removed-powershell-installation")
    message(FATAL_ERROR "vcpkg must receive a real executable, not an alias or removed path")
endif()
if(DEFINED _expected AND NOT Z_VCPKG_PWSH_PATH STREQUAL _expected)
    message(FATAL_ERROR "Explicit installation hint was not respected")
endif()
