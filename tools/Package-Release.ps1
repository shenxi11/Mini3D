# 模块名: Package-Release
# 功能概述: 将已构建的 Release 应用和依赖部署为本地候选 ZIP。
# 对外接口: BuildDirectory、QtBinDirectory、MsvcRuntimeDirectory、OutputDirectory。
# 依赖关系: pwsh、windeployqt、已构建应用、vcpkg 许可与 Qt SBOM。
# 输入输出: Release 产物到新目录、SHA256 清单和 ZIP。
# 异常与错误: 路径缺失、目标已存在或部署失败即停止，保留现场。
# 维护说明: 不下载、不发布、不覆盖已有包，不包含测试/调试产物和本机配置。
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$QtBinDirectory,
    [Parameter(Mandatory)][string]$MsvcRuntimeDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$qtBin = (Resolve-Path -LiteralPath $QtBinDirectory).Path
$runtime = (Resolve-Path -LiteralPath $MsvcRuntimeDirectory).Path
$stage = [IO.Path]::GetFullPath($OutputDirectory)
$zip = $stage + '.zip'
if ((Test-Path -LiteralPath $stage) -or (Test-Path -LiteralPath $zip)) {
    throw 'Output already exists; choose a new package directory'
}
$release = Join-Path $buildRoot 'src/editor/Release'
$app = Join-Path $release 'Mini3DStudio.exe'
$deploy = Join-Path $qtBin 'windeployqt.exe'
foreach ($required in @($app, $deploy, (Join-Path $release 'fastgltf.dll'), (Join-Path $release 'simdjson.dll'),
    (Join-Path $runtime 'msvcp140.dll'), (Join-Path $runtime 'vcruntime140.dll'), (Join-Path $runtime 'vcruntime140_1.dll'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing: $required" }
}
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $app -Destination $stage
foreach ($dll in Get-ChildItem -LiteralPath $release -Filter '*.dll') {
    Copy-Item -LiteralPath $dll.FullName -Destination $stage
}
# qtbase_zh_CN.qm 已嵌入编辑器，无需复制外部 translations 目录。
& $deploy --release --no-translations --no-compiler-runtime --no-system-dxc-compiler --skip-plugin-types generic --dir $stage (Join-Path $stage 'Mini3DStudio.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed; staging files retained for diagnosis' }
foreach ($dll in Get-ChildItem -LiteralPath $runtime -Filter '*.dll') {
    Copy-Item -LiteralPath $dll.FullName -Destination $stage
}
Copy-Item -LiteralPath (Join-Path $workspace 'assets') -Destination $stage -Recurse
$docs = New-Item -ItemType Directory -Path (Join-Path $stage 'docs')
foreach ($file in @('third-party-notices.md','sample-assets.md','week7.md','week8.md','performance-week8.json','demo.md','camera-light.md','localization.md')) {
    Copy-Item -LiteralPath (Join-Path $workspace "docs/$file") -Destination $docs.FullName
}
Copy-Item -LiteralPath (Join-Path $workspace 'docs/licenses') -Destination $docs.FullName -Recurse
Copy-Item -LiteralPath (Join-Path $workspace 'docs/images') -Destination $docs.FullName -Recurse
Copy-Item -LiteralPath (Join-Path $workspace 'docs/media') -Destination $docs.FullName -Recurse
$notices = New-Item -ItemType Directory -Path (Join-Path $docs.FullName 'third-party')
foreach ($port in @('fastgltf','simdjson','glm','nlohmann-json','spdlog','fmt')) {
    Copy-Item -LiteralPath (Join-Path $buildRoot "vcpkg_installed/x64-windows/share/$port/copyright") -Destination (Join-Path $notices.FullName "$port.txt")
}
Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $qtBin) 'sbom') -Destination (Join-Path $notices.FullName 'qt-sbom') -Recurse
Copy-Item -LiteralPath (Join-Path $workspace 'docs/package-readme.md') -Destination (Join-Path $stage 'README.md')
$manifest = foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
    [pscustomobject]@{ Path=[IO.Path]::GetRelativePath($stage,$file.FullName); Bytes=$file.Length; SHA256=(Get-FileHash -LiteralPath $file.FullName).Hash }
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $stage 'manifest.json') -Encoding utf8
Compress-Archive -LiteralPath $stage -DestinationPath $zip
Get-FileHash -LiteralPath $zip
