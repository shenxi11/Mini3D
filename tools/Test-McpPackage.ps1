# 模块名: Test-McpPackage
# 功能概述: 只读验证独立 MCP 候选的完整清单、生产依赖、合同及可选官方 SDK 本机连接。
# 对外接口: PackageDirectory、NodePath、ValidationDirectory、可选 DescriptorPath、ClientDirectory。
# 依赖关系: 外部 Node 24.13–24.x、pwsh 7.6；真实连接另需官方 client 2.3.1 的已安装目录。
# 输入输出: 固定候选到独占验证目录中的报告、日志与可选真实视口 PNG。
# 异常与错误: SHA 不符、多余文件、开发材料、合同或 stdio 调用失败立即停止。
# 维护说明: 不安装依赖、不修改候选、不创建场景；只关闭自有 adapter，不关闭本体或用户窗口。
#requires -Version 7.6
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [Parameter(Mandatory)][string]$NodePath,
    [Parameter(Mandatory)][string]$ValidationDirectory,
    [string]$DescriptorPath = '',
    [string]$ClientDirectory = ''
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Minor -ne 6) { throw 'Use PowerShell 7.6' }

# 路径仅用于读取或新建本次验证目录；拒绝已有路径链接及宽泛根目录。
function Get-PlainDirectory([string]$Path) {
    if (-not [IO.Path]::IsPathFullyQualified($Path)) { throw 'Use absolute directory paths' }
    $full = [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ($full.Length -le [IO.Path]::GetPathRoot($full).Length) { throw 'Drive roots are not validation targets' }
    for ($ancestor = $full; $ancestor; $ancestor = Split-Path -Parent $ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Unsafe directory: $ancestor" }
        }
    }
    return $full
}
$package = Get-PlainDirectory $PackageDirectory
$output = Get-PlainDirectory $ValidationDirectory
if (-not (Test-Path -LiteralPath $package -PathType Container)) { throw 'Package directory is missing' }
if ((Test-Path -LiteralPath $output) -or $output.StartsWith($package + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $package.StartsWith($output + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Use a new validation directory outside the package' }
$drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($output))
if ($drive.Name -notin @('E:\', 'D:\') -or $drive.DriveType -ne [IO.DriveType]::Fixed -or $drive.AvailableFreeSpace -lt 100MB) { throw 'Require verified E: or D: validation storage with at least 100 MiB free' }
$node = (Resolve-Path -LiteralPath $NodePath).Path
if ($DescriptorPath) {
    if (-not [IO.Path]::IsPathFullyQualified($DescriptorPath)) { throw 'DescriptorPath must be an explicit absolute file path' }
    $DescriptorPath = (Resolve-Path -LiteralPath $DescriptorPath).Path
    if (-not $ClientDirectory) { $ClientDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'mcp' }
    $ClientDirectory = Get-PlainDirectory $ClientDirectory
}
$manifestPath = Join-Path $package 'manifest.json'
$compatibilityPath = Join-Path $package 'compatibility.json'
$manifest = @(Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8 | ConvertFrom-Json)
$compatibility = Get-Content -LiteralPath $compatibilityPath -Raw -Encoding utf8 | ConvertFrom-Json
if ($compatibility.manifestVersion -ne 1 -or $compatibility.package.entryPoint -ne 'mcp/dist/main.js' -or
    $compatibility.api.version -ne '0.1.0' -or $compatibility.api.wireVersion -ne 1 -or $compatibility.api.methodCount -ne 47 -or
    $compatibility.runtime.node -ne '>=24.13.0 <25' -or $compatibility.runtime.deployment -ne 'external' -or
    $compatibility.runtime.bundled -ne $false -or $compatibility.runtime.npmRequiredAtRuntime -ne $false -or
    $compatibility.runtime.installAtRuntime -ne $false -or $compatibility.mcp.sdkServer -ne '2.3.1' -or
    $compatibility.mcp.sdkCore -ne '2.3.1' -or $compatibility.mcp.sdkClientForValidation -ne '2.3.1' -or
    (@($compatibility.mcp.protocolEras) -join ',') -ne 'legacy,2026-07-28') { throw 'Compatibility manifest does not describe the frozen external-runtime package' }
$recorded = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $manifest) {
    $target = [IO.Path]::GetFullPath((Join-Path $package $entry.Path))
    if (-not $target.StartsWith($package + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $entry.Path -eq 'manifest.json' -or -not $recorded.Add($entry.Path)) { throw 'Invalid or duplicate manifest path' }
    $file = Get-Item -LiteralPath $target -Force
    if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        $file.Length -ne $entry.Bytes -or (Get-FileHash -LiteralPath $target).Hash -ne $entry.SHA256) { throw "Package integrity mismatch: $($entry.Path)" }
}
$allItems = @(Get-ChildItem -LiteralPath $package -Recurse -Force)
foreach ($item in $allItems) {
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked package content is unsupported' }
    $relative = [IO.Path]::GetRelativePath($package, $item.FullName).Replace('\', '/')
    if (-not $item.PSIsContainer -and $relative -ne 'manifest.json' -and -not $recorded.Contains($relative)) { throw "Unlisted package file: $relative" }
    if ($relative -match '(^|/)(tests?|__tests__|specs?|\.github|@types|@typescript|typescript)(/|$)' -or
        $item.Name -match '^(\.npmrc|\.env(?:\..*)?|\.eslintrc(?:\..*)?|eslint\.config\..*|tsconfig(?:\..*)?\.json|(?:descriptor|instance|secret)(?:[-._].*)?\.json|node\.exe|npm(?:\.cmd|\.ps1)?)$') { throw "Non-runtime or private package content: $relative" }
    if (-not $item.PSIsContainer -and $relative -ne 'manifest.json' -and $relative -ne 'compatibility.json' -and
        $relative -notmatch '^mcp/(package(?:-lock)?\.json|dist/[^/]+\.js|node_modules/.+)$' -and
        $relative -notmatch '^api/schema/(methods\.json|[^/]+\.schema\.json)$') { throw "Unexpected package layout: $relative" }
}
if (@($allItems | Where-Object { -not $_.PSIsContainer -and $_.FullName -ne $manifestPath }).Count -ne $manifest.Count) { throw 'Manifest file count differs from package' }
$lockPath = Join-Path $package 'mcp/package-lock.json'
$lock = Get-Content -LiteralPath $lockPath -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable
$packageJson = Get-Content -LiteralPath (Join-Path $package 'mcp/package.json') -Raw -Encoding utf8 | ConvertFrom-Json
if ($packageJson.name -ne $compatibility.package.name -or $packageJson.version -ne $compatibility.package.version -or
    $packageJson.engines.node -ne $compatibility.runtime.node -or $packageJson.scripts.start -ne 'node dist/main.js' -or
    $packageJson.dependencies.'@modelcontextprotocol/server' -ne '2.3.1' -or $lock.lockfileVersion -ne 3 -or
    (Get-FileHash -LiteralPath $lockPath).Hash -ne $compatibility.build.lockfileSha256 -or
    (Get-FileHash -LiteralPath (Join-Path $package 'api/schema/methods.json')).Hash -ne $compatibility.api.methodsSha256) { throw 'Packaged manifest, lockfile or API input differs from compatibility record' }
$production = @($lock.packages.GetEnumerator() | Where-Object { $_.Key -and $_.Value.dev -ne $true } | Sort-Object Key)
$installedLock = Get-Content -LiteralPath (Join-Path $package 'mcp/node_modules/.package-lock.json') -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable
if ((@($installedLock.packages.Keys | Sort-Object) -join ',') -ne (@($production.Key | Sort-Object) -join ',') -or
    @($compatibility.productionDependencies).Count -ne $production.Count) { throw 'Production dependency closure differs from lockfile' }
foreach ($entry in $production) {
    $installed = Get-Content -LiteralPath (Join-Path $package ('mcp/' + $entry.Key + '/package.json')) -Raw -Encoding utf8 | ConvertFrom-Json
    $record = @($compatibility.productionDependencies | Where-Object Path -EQ ('mcp/' + $entry.Key))
    if ($record.Count -ne 1 -or $record[0].Version -ne $entry.Value.version -or $record[0].Integrity -ne $entry.Value.integrity -or
        $record[0].Resolved -ne $entry.Value.resolved -or $record[0].Name -ne $installed.name -or
        $installed.version -ne $entry.Value.version -or $installedLock.packages[$entry.Key].version -ne $entry.Value.version) { throw "Dependency lock mismatch: $($entry.Key)" }
}
foreach ($entry in $lock.packages.GetEnumerator() | Where-Object { $_.Value.dev -eq $true }) {
    if (Test-Path -LiteralPath (Join-Path $package ('mcp/' + $entry.Key))) { throw "Development dependency was packaged: $($entry.Key)" }
}
New-Item -ItemType Directory -Path $output | Out-Null
$temp = New-Item -ItemType Directory -Path (Join-Path $output 'temp')
$scriptPath = Join-Path $output 'validate-package.mjs'
# 验证脚本仅生成在独占目录；导入候选自身模块，不依赖开发目录 NODE_PATH。
$validationScript = @'
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';

const [root, output, clientRoot, descriptor] = process.argv.slice(2);
let secret = '';
const sanitize = text => [descriptor, secret].filter(Boolean).reduce((value, item) => value.replaceAll(item, '[private]'), String(text));
async function main() {
  const [major, minor] = process.versions.node.split('.').map(Number);
  assert(major === 24 && minor >= 13, 'Require external Node >=24.13.0 <25');
  const entry = path.join(root, 'mcp/dist/main.js');
  const schema = await import(pathToFileURL(path.join(root, 'mcp/dist/schema.js')).href);
  const tools = await import(pathToFileURL(path.join(root, 'mcp/dist/tools.js')).href);
  assert.equal(schema.apiVersion, '0.1.0'); assert.equal(schema.wireVersion, 1); assert.equal(schema.methods.length, 47);
  for (const method of schema.methods) {
    schema.validator.getValidator(schema.toolInput(method));
    schema.validator.getValidator(schema.toolOutput(method));
  }
  const missingDescriptor = spawn(process.execPath, [entry], { cwd: output, env: { ...process.env }, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  let stdout = '', stderr = '';
  missingDescriptor.stdout.on('data', bytes => { stdout += bytes.toString('utf8'); });
  missingDescriptor.stderr.on('data', bytes => { stderr += bytes.toString('utf8'); });
  const exitCode = await new Promise((resolve, reject) => { missingDescriptor.once('error', reject); missingDescriptor.once('exit', resolve); });
  assert.equal(exitCode, 1, 'Entry must refuse missing explicit descriptor'); assert.equal(stdout, ''); assert(stderr.length > 0);
  writeFileSync(path.join(output, 'missing-descriptor.log'), stderr, 'utf8');
  const report = { node: process.versions.node, schemaMethods: schema.methods.length, schemaValidation: 'all 47 input/output schemas compiled', entryPoint: 'mcp/dist/main.js', missingDescriptorExit: exitCode, officialSdk: 'not run: no explicit DescriptorPath', connections: [] };
  if (descriptor) {
    const bridge = await import(pathToFileURL(path.join(root, 'mcp/dist/bridge.js')).href);
    secret = (await bridge.readDescriptor(descriptor)).secret;
    const sdkRoot = path.join(clientRoot, 'node_modules/@modelcontextprotocol/client');
    const sdk = JSON.parse(readFileSync(path.join(sdkRoot, 'package.json'), 'utf8'));
    assert.equal(sdk.version, '2.3.1', 'Official validation client must match the frozen SDK');
    const { Client } = await import(pathToFileURL(path.join(sdkRoot, sdk.exports['.'].import.default)).href);
    const { StdioClientTransport } = await import(pathToFileURL(path.join(sdkRoot, sdk.exports['./stdio'].import.default)).href);
    for (const [era, mode] of [['legacy', 'legacy'], ['modern', { pin: '2026-07-28' }]]) {
      const transport = new StdioClientTransport({ command: process.execPath, args: [entry, '--descriptor', descriptor], cwd: output, env: { ...process.env }, stderr: 'pipe' });
      const client = new Client({ name: 'mini3d-package-validation', version: '0.1.0' }, { jsonSchemaValidator: schema.validator, versionNegotiation: { mode, probe: { timeoutMs: 5000, maxRetries: 0 } } });
      const protocolErrors = []; let adapterStderr = '';
      client.onerror = error => protocolErrors.push(error);
      transport.stderr?.on('data', bytes => { adapterStderr += bytes.toString('utf8'); });
      const call = async (method, args) => {
        const result = await client.callTool({ name: tools.toolName(method), arguments: args }, undefined, { timeout: 15000 });
        if (result.isError) throw new Error(`${method} failed: ${result.structuredContent?.error?.code ?? 'MCP error'}`);
        return result;
      };
      try {
        await client.connect(transport, { timeout: 5000 });
        assert.equal(client.getProtocolEra(), era);
        if (era === 'modern') assert.equal(client.getNegotiatedProtocolVersion(), '2026-07-28');
        const listed = await client.listTools();
        assert.deepEqual(listed.tools.map(tool => tool.name).sort(), schema.methods.map(method => tools.toolName(method.name)).sort());
        for (const method of schema.methods) {
          const tool = listed.tools.find(item => item.name === tools.toolName(method.name));
          assert.deepEqual(tool.inputSchema, schema.toolInput(method)); assert.deepEqual(tool.outputSchema, schema.toolOutput(method));
        }
        const describe = (await call('system.describe', {})).structuredContent;
        assert.equal(describe.apiVersion, '0.1.0'); assert.equal(describe.wireVersion, 1);
        assert.deepEqual(describe.methods.map(method => method.name).sort(), schema.methods.map(method => method.name).sort());
        const current = (await call('document.current', {})).structuredContent;
        const view = (await call('viewport.getState', { document: current.document })).structuredContent;
        const capture = await call('viewport.capture', { document: view.document, expectedDocumentRevision: view.documentRevision, expectedViewportRevision: view.viewportRevision, longestEdge: 800 });
        const image = capture.content.find(item => item.type === 'image');
        assert(image && image.mimeType === 'image/png', 'Capture must expose MCP image content');
        const bytes = Buffer.from(image.data, 'base64'), metadata = capture.structuredContent;
        assert.equal(bytes.subarray(0, 8).toString('hex'), '89504e470d0a1a0a'); assert.equal(bytes.length, metadata.byteLength);
        assert.equal(createHash('sha256').update(bytes).digest('hex'), metadata.sha256);
        assert.equal(bytes.readUInt32BE(16), metadata.outputPixelSize.width); assert.equal(bytes.readUInt32BE(20), metadata.outputPixelSize.height);
        assert.equal(Object.hasOwn(metadata, 'pngBase64'), false);
        assert.equal(capture.content.some(item => item.type === 'text' && item.text.includes(image.data)), false);
        writeFileSync(path.join(output, `capture-${era}.png`), bytes);
        report.connections.push({ era, protocolVersion: client.getNegotiatedProtocolVersion(), tools: listed.tools.length, describeMethods: describe.methods.length, image: { file: `capture-${era}.png`, sha256: metadata.sha256, byteLength: bytes.length, frameId: metadata.frameId, contextGeneration: metadata.contextGeneration, size: metadata.outputPixelSize } });
      } finally { await client.close(); await transport.close(); }
      assert.equal(protocolErrors.length, 0, 'Official SDK reported a protocol error');
      assert(!adapterStderr.includes(secret) && !adapterStderr.includes(descriptor), 'Adapter stderr exposed private descriptor material');
      writeFileSync(path.join(output, `adapter-${era}.log`), adapterStderr, 'utf8');
    }
    report.officialSdk = '2.3.1; legacy and 2026-07-28 describe/tools/current/view/capture passed';
  }
  writeFileSync(path.join(output, 'node-report.json'), JSON.stringify(report, null, 2) + '\n', 'utf8');
  console.log(JSON.stringify(report));
}
main().catch(error => { console.error(sanitize(error.stack ?? error)); process.exitCode = 1; });
'@
[IO.File]::WriteAllText($scriptPath, $validationScript, [Text.UTF8Encoding]::new($false))
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $node
$start.WorkingDirectory = $output
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @($scriptPath, $package, $output, $ClientDirectory, $DescriptorPath)) { $start.ArgumentList.Add($argument) }
$start.Environment['PATH'] = $env:SystemRoot + '/System32;' + $env:SystemRoot
foreach ($key in @('NODE_PATH', 'NODE_OPTIONS')) { $start.Environment.Remove($key) | Out-Null }
foreach ($key in @('TMPDIR', 'TEMP', 'TMP')) { $start.Environment[$key] = $temp.FullName }
$process = [Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
try {
    if (-not $process.WaitForExit(120000)) { $process.Kill($true); throw 'Package validation timed out; owned adapter process tree stopped' }
    $log = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $output 'validation.log'), $log, [Text.UTF8Encoding]::new($false))
    if ($process.ExitCode -ne 0) { throw "Node package validation failed with exit $($process.ExitCode); inspect $output/validation.log" }
} finally { $process.Dispose() }
$report = [ordered]@{
    packageDirectory = $package
    files = $manifest.Count
    bytes = ($manifest | Measure-Object Bytes -Sum).Sum
    manifestSha256 = (Get-FileHash -LiteralPath $manifestPath).Hash
    compatibilitySha256 = (Get-FileHash -LiteralPath $compatibilityPath).Hash
    productionDependencies = $production.Count
    integrity = 'all listed files and exact production lock closure passed; no dev/test/private material'
    runtime = Get-Content -LiteralPath (Join-Path $output 'node-report.json') -Raw -Encoding utf8 | ConvertFrom-Json
    limitation = if ($DescriptorPath) { 'This verifies the explicitly selected instance; relocated editor/Qt Network provenance and target host rendering require separate integration evidence.' } else { 'No explicit descriptor selected: real Mini3D pipe/capture and host rendering were not verified.' }
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'report.json') -Encoding utf8
$report | ConvertTo-Json -Depth 8
