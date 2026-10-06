# 模块名: API/MCP 性能测量执行入口
# 功能概述: 验证 E/D 临时目录，构建独立 Release 探针并串行采集正式/插桩结果。
# 对外接口: run.ps1 -Cases -Samples -Warmup -CaptureSamples -WindowSeconds -WindowCount。
# 依赖关系: PowerShell 7.6、现成 Release 库、CMake/Qt/Node。
# 输入输出: 冻结生产库到独占 scratch 的构建、源码哈希与成对测量报告。
# 异常与错误: 原生命令非零或不安全路径直接失败，不降级 shell。
# 维护说明: 不提交推送、不修改生产代码、不连接用户窗口；GUI 可见启动。
[CmdletBinding()]
param(
    [string]$Repository = 'E:/Mini3D',
    [string]$SourceBuild = 'E:/Mini3D/out/build/windows-msvc-local',
    [string]$ScratchRoot = '',
    [string]$ExistingProbeBuild = '',
    [string]$Cmake = 'E:/cmake-3.31.0-rc1-windows-x86_64/bin/cmake.exe',
    [string]$QtRoot = 'E:/Qt5.15/6.8.3/msvc2022_64',
    [string]$Node = 'D:/NodeJs/node.exe',
    [string]$Cases = 'mesh:1000,mesh:10000,mesh:100000,entities:1000,entities:10000',
    [int]$Samples = 20,
    [int]$Warmup = 3,
    [int]$CaptureSamples = 10,
    [ValidateSet(960, 1600)][int]$CaptureLongestEdge = 960,
    [int]$WindowSeconds = 5,
    [int]$WindowCount = 5
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -ne 7 -or $PSVersionTable.PSVersion.Minor -ne 6) {
    throw 'This entry point requires PowerShell 7.6.'
}
function Test-SafeStorage([string]$Candidate) {
    $absolute = [IO.Path]::GetFullPath($Candidate)
    $driveRoot = [IO.Path]::GetPathRoot($absolute)
    $cursor = $absolute
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse point is not allowed for performance output: $cursor"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($cursor.TrimEnd('\', '/'))
        if (!$parent -or $parent -eq $cursor) { break }
        $cursor = $parent
    }
    $drive = Get-PSDrive -Name ($driveRoot.Substring(0, 1))
    if ($drive.Free -lt 2GB) { throw "Insufficient performance scratch space: $driveRoot" }
    if ($driveRoot -notin @('E:\', 'D:\') -and
        !$absolute.StartsWith('C:\Users\SL\AppData\Local\Temp\CodexTemp\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unexpected scratch destination: $absolute"
    }
    return $absolute
}
function Invoke-Native([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    $nativeExit = $LASTEXITCODE
    if ($nativeExit -ne 0) { throw "$Executable failed: $nativeExit" }
}
if (!$ScratchRoot) {
    $date = Get-Date -Format 'yyyyMMdd'
    $suffix = [Guid]::NewGuid().ToString('N').Substring(0, 8)
    foreach ($candidateRoot in @('E:/CodexTemp', 'D:/CodexTemp', 'C:/Users/SL/AppData/Local/Temp/CodexTemp')) {
        try {
            $candidate = Join-Path $candidateRoot "$date/m7-performance-$suffix"
            $verified = Test-SafeStorage $candidate
            New-Item -Path $verified -ItemType Directory | Out-Null
            [IO.File]::WriteAllText((Join-Path $verified 'storage-probe.txt'), 'M7 scratch write verified', [Text.UTF8Encoding]::new($false))
            $ScratchRoot = Test-SafeStorage $verified
            break
        } catch {
            Write-Information "Scratch candidate unavailable: $candidateRoot; $($_.Exception.Message)" -InformationAction Continue
        }
    }
    if (!$ScratchRoot) { throw 'No verified performance scratch destination.' }
} else {
    $ScratchRoot = Test-SafeStorage $ScratchRoot
    if (!(Test-Path -LiteralPath $ScratchRoot)) { New-Item -Path $ScratchRoot -ItemType Directory | Out-Null }
}
$runRoot = Join-Path $ScratchRoot ('run-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -Path $runRoot -ItemType Directory | Out-Null
$taskTemp = Join-Path $runRoot 'tmp'
New-Item -Path $taskTemp -ItemType Directory | Out-Null
$previous = @{}
foreach ($name in @('TMPDIR', 'TEMP', 'TMP', 'PATH', 'QT_QPA_PLATFORM', 'QT_FORCE_STDERR_LOGGING')) {
    $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:TMPDIR = $taskTemp
    $env:TEMP = $taskTemp
    $env:TMP = $taskTemp
    $env:PATH = "$QtRoot/bin;$SourceBuild/vcpkg_installed/x64-windows/bin;" + $env:PATH
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_FORCE_STDERR_LOGGING = '1'
    $source = Join-Path $Repository 'tools/api-mcp/performance'
    $productionLibraries = @(
        'src/editor/Release/mini3d_editor_ui.lib',
        'src/editor/mini3d_editor_ui_resources_1.dir/Release/mini3d_editor_ui_resources_1.lib',
        'src/renderer_gl/Release/mini3d_renderer_gl.lib',
        'src/renderer_gl/mini3d_renderer_gl_resources_1.dir/Release/mini3d_renderer_gl_resources_1.lib',
        'src/assets/Release/mini3d_assets.lib', 'src/core/Release/mini3d_core.lib'
    )
    $libraryMetadata = foreach ($relative in $productionLibraries) {
        $library = Get-Item -LiteralPath (Join-Path $SourceBuild $relative)
        [ordered]@{ path = $library.FullName; bytes = $library.Length; modifiedUtc = $library.LastWriteTimeUtc.ToString('o');
            sha256 = (Get-FileHash -LiteralPath $library.FullName -Algorithm SHA256).Hash }
    }
    if (!$ExistingProbeBuild) {
        $generated = Join-Path $runRoot 'generated'
        Invoke-Native $Node @((Join-Path $source 'instrument.mjs'), $Repository, $generated)
        $build = Join-Path $runRoot 'build'
        $prefix = "$QtRoot;$SourceBuild/vcpkg_installed/x64-windows"
        Invoke-Native $Cmake @('-S', $source, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64',
            "-DCMAKE_PREFIX_PATH=$prefix", "-DMINI3D_ROOT=$Repository", "-DMINI3D_BUILD=$SourceBuild",
            "-DPERFORMANCE_GENERATED=$generated")
        Invoke-Native $Cmake @('--build', $build, '--config', 'Release', '--parallel', '2', '--target',
            'mini3d_api_performance', 'mini3d_api_performance_instrumented')
    } else {
        $build = Test-SafeStorage $ExistingProbeBuild
        $generated = Join-Path (Split-Path $build -Parent) 'generated'
    }
    $head = & git -C $Repository rev-parse HEAD
    $nativeExit = $LASTEXITCODE
    if ($nativeExit -ne 0) { throw 'Git metadata failed.' }
    $dirty = & git -C $Repository status --porcelain
    $nativeExit = $LASTEXITCODE
    if ($nativeExit -ne 0) { throw 'Git state metadata failed.' }
    $nodeVersion = & $Node --version
    $nativeExit = $LASTEXITCODE
    if ($nativeExit -ne 0) { throw 'Node version failed.' }
    $metadata = [ordered]@{
        startedUtc = [DateTime]::UtcNow.ToString('o'); repository = $Repository; commit = $head;
        dirtyPaths = $dirty; powershell = $PSVersionTable.PSVersion.ToString(); node = $nodeVersion;
        sourceBuild = $SourceBuild; probeBuild = $build; scratch = $runRoot;
        configuration = 'Release /O2 /MD'; libraries = $libraryMetadata;
        instrumentationManifest = (Join-Path $generated 'instrumentation.json');
        sampleCount = $Samples; warmups = $Warmup; captureSampleCount = $CaptureSamples;
        captureLongestEdge = $CaptureLongestEdge;
        memoryWindows = $WindowCount; memoryWindowSeconds = $WindowSeconds;
        modelRequested = 'gpt-6.1-sol/max'; providerModelVerified = $false
    }
    [IO.File]::WriteAllText((Join-Path $runRoot 'build-metadata.json'), ($metadata | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
    foreach ($kind in @('official', 'instrumented')) {
        $executable = if ($kind -eq 'official') { 'mini3d_api_performance.exe' } else { 'mini3d_api_performance_instrumented.exe' }
        Invoke-Native $Node @((Join-Path $source 'measure.mjs'), '--probe', (Join-Path $build "Release/$executable"),
            '--output', (Join-Path $runRoot $kind), '--cases', $Cases, '--samples', [string]$Samples,
            '--warmup', [string]$Warmup, '--capture-samples', [string]$CaptureSamples,
            '--capture-longest-edge', [string]$CaptureLongestEdge,
            '--window-seconds', [string]$WindowSeconds, '--window-count', [string]$WindowCount)
    }
    Write-Output ([ordered]@{ completed = $true; scratch = $runRoot; probeBuild = $build } | ConvertTo-Json -Compress)
} finally {
    foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
