# 模块名: Test-Package
# 功能概述: 将候选包复制到全新路径，隔离开发环境 PATH 后验证真实启动和场景加载。
# 对外接口: PackageDirectory、OutputDirectory。
# 依赖关系: pwsh、Windows Process API、应用显式截图验收入口。
# 输入输出: 包目录到独立副本、PNG、模块路径和日志。
# 异常与错误: 清单不符、超时、错误退出码或 DLL 越界即失败。
# 维护说明: 只结束本脚本创建的超时进程，不处理用户已有编辑器。
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Validation output exists; choose a new directory' }
foreach ($entry in Get-Content -Raw -LiteralPath (Join-Path $package 'manifest.json') | ConvertFrom-Json) {
    if ((Get-FileHash -LiteralPath (Join-Path $package $entry.Path)).Hash -ne $entry.SHA256) {
        throw "Package hash mismatch: $($entry.Path)"
    }
}
New-Item -ItemType Directory -Path $output | Out-Null
$relocated = Join-Path $output 'relocated app'
Copy-Item -LiteralPath $package -Destination $relocated -Recurse
$capturedModules = @()
foreach ($case in @('demo','scene','missing')) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $relocated 'Mini3DStudio.exe'
    $start.WorkingDirectory = $output
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Environment['PATH'] = "$env:SystemRoot/System32;$env:SystemRoot"
    foreach ($key in @('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML2_IMPORT_PATH','QT_QPA_PLATFORM','QT_OPENGL','QT_SCALE_FACTOR')) {
        $start.Environment.Remove($key) | Out-Null
    }
    $capture = Join-Path $output "$case.png"
    $start.Environment['MINI3D_VALIDATION_CAPTURE'] = $capture
    $uiCapture = Join-Path $output "$case-ui.png"
    $start.Environment['MINI3D_VALIDATION_UI_CAPTURE'] = $uiCapture
    $start.Environment.Remove('MINI3D_VALIDATION_SCENE') | Out-Null
    if ($case -eq 'scene') {
        $start.Environment['MINI3D_VALIDATION_SCENE'] = Join-Path $relocated 'assets/scenes/showcase.m3dscene'
    } elseif ($case -eq 'missing') {
        $start.Environment['MINI3D_VALIDATION_SCENE'] = Join-Path $relocated 'missing.m3dscene'
    }
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    try {
        if ($case -ne 'missing') {
            Start-Sleep -Milliseconds 700
            if (-not $process.HasExited) {
                $capturedModules += @($process.Modules | Select-Object ModuleName,FileName)
            }
        }
        if (-not $process.WaitForExit(15000)) {
            $process.Kill()
            throw "Package $case timed out (owned process stopped)"
        }
        ($stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()) |
            Set-Content -LiteralPath (Join-Path $output "$case.log") -Encoding utf8
        $expected = if ($case -eq 'missing') { 4 } else { 0 }
        if ($process.ExitCode -ne $expected) { throw "Package $case exit $($process.ExitCode), expected $expected" }
        if ($case -ne 'missing' -and -not (Test-Path -LiteralPath $capture)) { throw "No capture: $case" }
        if ($case -ne 'missing' -and -not (Test-Path -LiteralPath $uiCapture)) { throw "No UI capture: $case" }
        if ($case -eq 'missing' -and (Test-Path -LiteralPath $capture)) { throw 'Missing scene unexpectedly rendered' }
    } finally { $process.Dispose() }
}
$demoHash = (Get-FileHash -LiteralPath (Join-Path $output 'demo.png')).Hash
$sceneHash = (Get-FileHash -LiteralPath (Join-Path $output 'scene.png')).Hash
if ($demoHash -eq $sceneHash) { throw 'Loading showcase did not change the framebuffer' }
$requiredModules = @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6OpenGL.dll','Qt6OpenGLWidgets.dll',
    'qwindows.dll','fastgltf.dll','simdjson.dll','msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')
foreach ($name in $requiredModules) {
    $loaded = @($capturedModules | Where-Object ModuleName -IEQ $name)
    if ($loaded.Count -eq 0) { throw "Module not observed: $name" }
    foreach ($module in $loaded) {
        if (-not $module.FileName.StartsWith($relocated + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw "Module escaped package: $($module.FileName)"
        }
    }
}
$capturedModules | Sort-Object FileName -Unique | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'modules.json') -Encoding utf8
"Passed: manifest, relocated demo/scene, missing-file rejection, $($requiredModules.Count) packaged dependencies."
