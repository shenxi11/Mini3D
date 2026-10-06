# 模块名: Package-Mcp
# 功能概述: 在独占工作目录编译 adapter，按冻结 lockfile 生成可直接启动的 MCP 候选目录。
# 对外接口: OutputDirectory、WorkDirectory、NodePath、NpmPath。
# 依赖关系: 外部 Node 24.13–24.x、npm 11.6.2、pwsh 7.6 与仓库冻结合同。
# 输入输出: 源码快照到 mcp/dist、生产依赖、api/schema、兼容记录与 SHA256 清单。
# 异常与错误: 输入不符、目标已存在、路径链接或子进程失败即停止并保留现场。
# 维护说明: 不下载 Node，不修改仓库 node_modules/dist，不复制实例描述、凭据或用户配置。
#requires -Version 7.6
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$WorkDirectory,
    [Parameter(Mandatory)][string]$NodePath,
    [Parameter(Mandatory)][string]$NpmPath
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Minor -ne 6) { throw 'Use PowerShell 7.6' }
$workspace = Split-Path -Parent $PSScriptRoot
$pwsh = Join-Path $PSHOME 'pwsh.exe'

# 核对新目录及其已存在的祖先；不通过链接或盘符别名写入其他存储。
function Get-NewPlainDirectory([string]$Path) {
    if (-not [IO.Path]::IsPathFullyQualified($Path)) { throw 'Use an absolute directory path' }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $root = [IO.Path]::GetPathRoot($full)
    if ($full.Length -le $root.Length -or (Test-Path -LiteralPath $full)) { throw "Output already exists or is a drive root: $full" }
    $drive = [IO.DriveInfo]::new($root)
    if ($drive.DriveType -ne [IO.DriveType]::Fixed -or $drive.AvailableFreeSpace -lt 1GB) { throw "Require a fixed drive with at least 1 GiB free: $root" }
    for ($ancestor = Split-Path -Parent $full; $ancestor; $ancestor = Split-Path -Parent $ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Unsafe directory ancestor: $ancestor" }
        }
    }
    return $full
}

$stage = Get-NewPlainDirectory $OutputDirectory
$work = Get-NewPlainDirectory $WorkDirectory
if ([IO.Path]::GetPathRoot($work) -notin @('E:\', 'D:\')) { throw 'WorkDirectory must be on verified E: or D: storage' }
foreach ($pair in @(@($stage, $work), @($work, $stage), @($work, $workspace))) {
    if ($pair[0] -eq $pair[1] -or $pair[0].StartsWith($pair[1].TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Package, work and repository directories must not overlap'
    }
}
$node = (Resolve-Path -LiteralPath $NodePath).Path
$npmLauncher = (Resolve-Path -LiteralPath $NpmPath).Path
$npmCli = Join-Path (Split-Path -Parent $npmLauncher) 'node_modules/npm/bin/npm-cli.js'
if (-not (Test-Path -LiteralPath $npmCli -PathType Leaf)) { throw 'NpmPath must locate an installed npm launcher with node_modules/npm/bin/npm-cli.js' }
$nodeVersion = & $node --version
if ($LASTEXITCODE -ne 0 -or $nodeVersion -notmatch '^v24\.(\d+)\.\d+$' -or [int]$Matches[1] -lt 13) { throw 'Require external Node >=24.13.0 <25' }
$packagePath = Join-Path $workspace 'mcp/package.json'
$lockPath = Join-Path $workspace 'mcp/package-lock.json'
$package = Get-Content -LiteralPath $packagePath -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable
$catalog = Get-Content -LiteralPath (Join-Path $workspace 'api/schema/methods.json') -Raw -Encoding utf8 | ConvertFrom-Json
if ($package.packageManager -ne 'npm@11.6.2' -or $package.engines.node -ne '>=24.13.0 <25' -or
    $package.dependencies['@modelcontextprotocol/server'] -ne '2.3.1' -or $package.devDependencies.typescript -ne '7.0.2' -or
    $lock.lockfileVersion -ne 3 -or $lock.version -ne $package.version -or $catalog.apiVersion -ne '0.1.0' -or
    $catalog.wireVersion -ne 1 -or $catalog.methods.Count -ne 47) { throw 'Frozen package/API compatibility changed; review it before packaging' }
$inputs = @('mcp/package.json', 'mcp/package-lock.json', 'mcp/tsconfig.json')
$inputs += @(Get-ChildItem -LiteralPath (Join-Path $workspace 'mcp/src') -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($workspace, $_.FullName).Replace('\', '/') })
$inputs += @(Get-ChildItem -LiteralPath (Join-Path $workspace 'api/schema') -File | Where-Object { $_.Name -eq 'methods.json' -or $_.Name.EndsWith('.schema.json') } | ForEach-Object { 'api/schema/' + $_.Name })
foreach ($relative in $inputs) {
    $inputFile = Get-Item -LiteralPath (Join-Path $workspace $relative)
    if ($inputFile.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked input is unsupported: $relative" }
}
New-Item -ItemType Directory -Path $work | Out-Null
$temp = New-Item -ItemType Directory -Path (Join-Path $work 'temp')
$logs = New-Item -ItemType Directory -Path (Join-Path $work 'logs')
$build = New-Item -ItemType Directory -Path (Join-Path $work 'snapshot')
$cache = Join-Path $work 'npm-cache'
$probe = Join-Path $temp.FullName 'writable.probe'
[IO.File]::WriteAllText($probe, 'probe', [Text.UTF8Encoding]::new($false))
Remove-Item -LiteralPath $probe
$inputManifest = foreach ($relative in $inputs | Sort-Object) {
    $destination = Join-Path $build.FullName $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $workspace $relative) -Destination $destination
    [pscustomobject]@{ Path = $relative; Bytes = (Get-Item -LiteralPath $destination).Length; SHA256 = (Get-FileHash -LiteralPath $destination).Hash }
}

# 子进程固定 Node、缓存、临时目录和 npm 配置；日志仅写入本任务工作目录。
function Invoke-BuildProcess([string[]]$Arguments, [string]$Directory, [string]$Label) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $node
    $start.WorkingDirectory = $Directory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    foreach ($key in @($start.Environment.Keys)) {
        if ($key.StartsWith('npm_config_', [StringComparison]::OrdinalIgnoreCase) -or $key -in @('NODE_PATH', 'NODE_OPTIONS')) { $start.Environment.Remove($key) | Out-Null }
    }
    $start.Environment['PATH'] = (Split-Path -Parent $node) + ';' + $PSHOME + ';' + $env:SystemRoot + '/System32;' + $env:SystemRoot
    foreach ($key in @('TMPDIR', 'TEMP', 'TMP')) { $start.Environment[$key] = $temp.FullName }
    $start.Environment['npm_config_cache'] = $cache
    $start.Environment['npm_config_userconfig'] = Join-Path $work 'unused-user.npmrc'
    $start.Environment['npm_config_globalconfig'] = Join-Path $work 'unused-global.npmrc'
    $start.Environment['npm_config_script_shell'] = $pwsh
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    try {
        if (-not $process.WaitForExit(180000)) { $process.Kill($true); throw "$Label timed out; owned process stopped" }
        $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $logs.FullName ($Label + '.log')), $text, [Text.UTF8Encoding]::new($false))
        if ($process.ExitCode -ne 0) { throw "$Label failed with exit $($process.ExitCode); inspect $($logs.FullName)" }
        return $text.Trim()
    } finally { $process.Dispose() }
}

$npmVersion = Invoke-BuildProcess @($npmCli, '--version') $work 'npm-version'
if ($npmVersion -ne '11.6.2') { throw 'Require npm 11.6.2 for the frozen lockfile build' }
$buildMcp = Join-Path $build.FullName 'mcp'
Invoke-BuildProcess @($npmCli, 'ci', '--ignore-scripts', '--include=dev', '--no-audit', '--no-fund', '--registry=https://registry.npmjs.org') $buildMcp 'build-ci' | Write-Host
Invoke-BuildProcess @((Join-Path $buildMcp 'node_modules/typescript/bin/tsc'), '-p', 'tsconfig.json') $buildMcp 'compile' | Write-Host
New-Item -ItemType Directory -Path $stage | Out-Null
$stageMcp = New-Item -ItemType Directory -Path (Join-Path $stage 'mcp')
Copy-Item -LiteralPath (Join-Path $buildMcp 'dist') -Destination $stageMcp.FullName -Recurse
foreach ($relative in @('package.json', 'package-lock.json')) { Copy-Item -LiteralPath (Join-Path $buildMcp $relative) -Destination $stageMcp.FullName }
$stageApi = New-Item -ItemType Directory -Path (Join-Path $stage 'api')
Copy-Item -LiteralPath (Join-Path $build.FullName 'api/schema') -Destination $stageApi.FullName -Recurse
Invoke-BuildProcess @($npmCli, 'ci', '--omit=dev', '--ignore-scripts', '--no-audit', '--no-fund', '--registry=https://registry.npmjs.org') $stageMcp.FullName 'production-ci' | Write-Host
# 冻结 tarball 实际包含这些开发材料；只从本轮新建候选中剔除，不修改安装源。
$excluded = @(
    'node_modules/fast-uri/.github', 'node_modules/fast-uri/test', 'node_modules/fast-uri/.gitattributes',
    'node_modules/fast-uri/eslint.config.js', 'node_modules/fast-uri/tsconfig.json',
    'node_modules/json-schema-traverse/.github', 'node_modules/json-schema-traverse/spec', 'node_modules/json-schema-traverse/.eslintrc.yml',
    'node_modules/zod/src/v3/tests', 'node_modules/zod/src/v4/classic/tests',
    'node_modules/zod/src/v4/core/tests', 'node_modules/zod/src/v4/mini/tests', 'node_modules/ajv/.runkit_example.js'
)
foreach ($relative in $excluded) {
    $target = [IO.Path]::GetFullPath((Join-Path $stageMcp.FullName $relative))
    if (-not $target.StartsWith($stageMcp.FullName + '\node_modules\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Pruning target escaped staged dependencies' }
    if (Test-Path -LiteralPath $target) {
        $item = Get-Item -LiteralPath $target -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked pruning target: $relative" }
        Remove-Item -LiteralPath $target -Recurse
    }
}
$dependencies = foreach ($entry in $lock.packages.GetEnumerator() | Where-Object { $_.Key -and $_.Value.dev -ne $true } | Sort-Object Key) {
    $installed = Get-Content -LiteralPath (Join-Path $stageMcp.FullName ($entry.Key + '/package.json')) -Raw -Encoding utf8 | ConvertFrom-Json
    if ($installed.version -ne $entry.Value.version) { throw "Installed dependency differs from lockfile: $($entry.Key)" }
    [pscustomobject]@{ Path = 'mcp/' + $entry.Key; Name = $installed.name; Version = $installed.version; Integrity = $entry.Value.integrity; Resolved = $entry.Value.resolved }
}
$compatibility = [ordered]@{
    manifestVersion = 1
    package = @{ name = $package.name; version = $package.version; entryPoint = 'mcp/dist/main.js' }
    api = @{ version = $catalog.apiVersion; wireVersion = $catalog.wireVersion; methodCount = $catalog.methods.Count; methodsSha256 = (Get-FileHash -LiteralPath (Join-Path $stage 'api/schema/methods.json')).Hash }
    runtime = @{ node = $package.engines.node; deployment = 'external'; bundled = $false; npmRequiredAtRuntime = $false; installAtRuntime = $false; descriptor = 'explicit protected absolute path; never bundled' }
    mcp = @{ sdkServer = '2.3.1'; sdkCore = '2.3.1'; sdkClientForValidation = '2.3.1'; transport = 'stdio'; protocolEras = @('legacy', '2026-07-28') }
    build = @{ node = $nodeVersion.TrimStart('v'); nodeSha256 = (Get-FileHash -LiteralPath $node).Hash; npm = $npmVersion; typescript = '7.0.2'; pwsh = $PSVersionTable.PSVersion.ToString(); productionInstall = 'npm ci --omit=dev --ignore-scripts'; lockfileSha256 = (Get-FileHash -LiteralPath (Join-Path $stageMcp.FullName 'package-lock.json')).Hash; packageScriptSha256 = (Get-FileHash -LiteralPath $PSCommandPath).Hash }
    inputs = @($inputManifest)
    productionDependencies = @($dependencies)
    excludedNonRuntimePaths = @($excluded | ForEach-Object { 'mcp/' + $_ })
}
$compatibility | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stage 'compatibility.json') -Encoding utf8
$manifest = @(Get-ChildItem -LiteralPath $stage -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
    [pscustomobject]@{ Path = [IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\', '/'); Bytes = $_.Length; SHA256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
})
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $stage 'manifest.json') -Encoding utf8
[pscustomobject]@{ PackageDirectory = $stage; WorkDirectory = $work; ClientDirectory = $buildMcp; Files = $manifest.Count; Bytes = ($manifest | Measure-Object Bytes -Sum).Sum; ManifestSHA256 = (Get-FileHash -LiteralPath (Join-Path $stage 'manifest.json')).Hash; CompatibilitySHA256 = (Get-FileHash -LiteralPath (Join-Path $stage 'compatibility.json')).Hash } | ConvertTo-Json
