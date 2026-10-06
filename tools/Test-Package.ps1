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
$temporary = Join-Path $output 'tmp'
New-Item -ItemType Directory -Path $temporary | Out-Null
$relocated = Join-Path $output 'relocated app'
Copy-Item -LiteralPath $package -Destination $relocated -Recurse
foreach ($required in @('docs/Mini3D_使用手册.html','docs/blender-compatibility.md','docs/v2-performance.md',
    'docs/v2-acceptance.md','docs/validation/v2/final/regression/report.json',
    'docs/performance/v2/delivery/manifest.json',
    'assets/scenes/v2/shell.m3dscene','assets/scenes/v2/symmetric.m3dscene','assets/scenes/v2/subdivision.m3dscene')) {
    if (-not (Test-Path -LiteralPath (Join-Path $relocated $required) -PathType Leaf)) { throw "Missing V2 delivery: $required" }
}
$guide = Get-Content -LiteralPath (Join-Path $relocated 'docs/Mini3D_使用手册.html') -Raw -Encoding utf8
foreach ($match in [regex]::Matches($guide, '(?:href|src)="(?<path>images/[^"#]+)"')) {
    if (-not (Test-Path -LiteralPath (Join-Path $relocated ('docs/' + $match.Groups['path'].Value)) -PathType Leaf)) {
        throw "Missing guide image: $($match.Groups['path'].Value)"
    }
}
$capturedModules = @()
foreach ($case in @('demo','scene','shell','symmetric','subdivision','missing')) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $relocated 'Mini3DStudio.exe'
    $start.WorkingDirectory = $output
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # Qt GUI 日志须显式写入 stderr，并按已确认的本机 ANSI 编码读取中文路径。
    $start.StandardErrorEncoding = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.ANSICodePage)
    $start.Environment['QT_FORCE_STDERR_LOGGING'] = '1'
    foreach ($key in @('TMPDIR','TEMP','TMP')) { $start.Environment[$key] = $temporary }
    $start.Environment['MINI3D_VALIDATION_SETTINGS'] = $temporary
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
    } elseif ($case -in @('shell','symmetric','subdivision')) {
        $start.Environment['MINI3D_VALIDATION_SCENE'] = Join-Path $relocated ("assets/scenes/v2/$case.m3dscene")
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
        $log = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        $log |
            Set-Content -LiteralPath (Join-Path $output "$case.log") -Encoding utf8
        $expected = if ($case -eq 'missing') { 4 } else { 0 }
        if ($process.ExitCode -ne $expected) { throw "Package $case exit $($process.ExitCode), expected $expected" }
        if ($case -ne 'missing' -and -not (Test-Path -LiteralPath $capture)) { throw "No capture: $case" }
        if ($case -ne 'missing' -and -not (Test-Path -LiteralPath $uiCapture)) { throw "No UI capture: $case" }
        if ($case -eq 'missing' -and (Test-Path -LiteralPath $capture)) { throw 'Missing scene unexpectedly rendered' }
        if ($case -ne 'missing') {
            $expectedGuide = [IO.Path]::GetFullPath((Join-Path $relocated 'docs/Mini3D_使用手册.html'))
            if (-not $log.Contains(('MINI3D_VALIDATION_HELP ' + $expectedGuide), [StringComparison]::OrdinalIgnoreCase)) {
                throw "Help escaped relocated package: $case"
            }
        }
    } finally { $process.Dispose() }
}
$demoHash = (Get-FileHash -LiteralPath (Join-Path $output 'demo.png')).Hash
$sceneHash = (Get-FileHash -LiteralPath (Join-Path $output 'scene.png')).Hash
if ($demoHash -eq $sceneHash) { throw 'Loading showcase did not change the framebuffer' }
$requiredModules = @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Network.dll','Qt6OpenGL.dll','Qt6OpenGLWidgets.dll',
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
"Passed: manifest, relocated demo/showcase/3 V2 scenes, offline guide/images, missing-file rejection, $($requiredModules.Count) packaged dependencies."
