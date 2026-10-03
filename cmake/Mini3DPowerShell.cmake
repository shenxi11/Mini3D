# 模块名: Mini3D PowerShell 定位
# 功能概述: 在 project() 前解析可执行的 PowerShell 7.6，并刷新 vcpkg 的两项缓存。
# 对外接口: 可选 MINI3D_LOCAL_PWSH_DIRECTORY；输出 Z_VCPKG_PWSH_PATH、Z_VCPKG_POWERSHELL_PATH。
# 依赖关系: 已安装的 PowerShell 7.6；支持 Microsoft Store 应用执行别名。
# 输入输出: 安装提示及当前环境到真实可执行文件路径和当前进程 PATH。
# 异常与错误: 找不到或无法运行要求版本时终止配置，不下载、不降级。
# 维护说明: 每次配置重新发现；不将 Store 版本目录作为长期配置保存。

# 在独立作用域中查找，避免上一次 include 的临时变量绕过重新探测。
function(mini3d_configure_powershell)
    find_program(
        _mini3d_pwsh
        NAMES pwsh
        HINTS
            "${MINI3D_LOCAL_PWSH_DIRECTORY}"
            "$ENV{LOCALAPPDATA}/Microsoft/WindowsApps"
            "$ENV{ProgramFiles}/PowerShell/7"
        NO_CACHE
    )
    if(NOT _mini3d_pwsh)
        message(FATAL_ERROR
            "Mini3D requires PowerShell 7.6. Enable the pwsh app execution alias, "
            "add pwsh to PATH, or set MINI3D_LOCAL_PWSH_DIRECTORY, then run CMake again."
        )
    endif()

    # 运行别名以获得实际进程路径；不能把 WindowsApps 的别名文件交给 vcpkg。
    execute_process(
        COMMAND "${_mini3d_pwsh}" -NoLogo -NoProfile -NonInteractive -Command
            [=[if ($PSVersionTable.PSVersion.Major -ne 7 -or $PSVersionTable.PSVersion.Minor -ne 6) { [Console]::Error.WriteLine('Mini3D requires PowerShell 7.6'); exit 1 }; [Environment]::ProcessPath]=]
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _resolved
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE
        TIMEOUT 15
    )
    if(NOT "${_result}" STREQUAL "0" OR NOT EXISTS "${_resolved}")
        message(FATAL_ERROR
            "Cannot run PowerShell 7.6 at ${_mini3d_pwsh}: ${_result}\n${_error}"
        )
    endif()

    file(TO_CMAKE_PATH "${_resolved}" _resolved)
    get_filename_component(_directory "${_resolved}" DIRECTORY)
    set(ENV{PATH} "${_directory};$ENV{PATH}")
    set(Z_VCPKG_PWSH_PATH "${_resolved}" CACHE FILEPATH "Mini3D local PowerShell executable" FORCE)
    set(Z_VCPKG_POWERSHELL_PATH "${_resolved}" CACHE INTERNAL "The path to the PowerShell implementation to use." FORCE)
    message(STATUS "Mini3D PowerShell: ${_resolved}")
endfunction()

mini3d_configure_powershell()
