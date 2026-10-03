# 模块名: Run-V2Acceptance
# 功能概述: 串行执行二期真实窗口验收并保存作品、日志和可追溯元数据。
# 对外接口: BuildDirectory、QtBinDirectory、OutputDirectory、Configuration、Scale、Filter。
# 依赖关系: 已构建 editor tests、Qt、Git、PowerShell 7.6。
# 输入输出: 新证据目录到逐配方实际 DPR/帧/OBJ 报告、作品/截图、日志和 SHA256。
# 异常与错误: 拒绝覆盖证据；测试失败保留现场并抛错，不自动重试。
# 维护说明: 不与其他 UI/GPU 测试并行；合成输入与 DPI 模拟不代替真实 IME/混合 DPI。
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$QtBinDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [ValidateSet('1','1.5','2')][string]$Scale = '1',
    [string]$Filter = '[v2-acceptance],[help-editor]'
)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path -Parent $PSScriptRoot
$buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
$qtBin = (Resolve-Path -LiteralPath $QtBinDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
$executable = Join-Path $buildRoot ("tests/$Configuration/mini3d_editor_tests.exe")
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing executable: $executable" }
if (Test-Path -LiteralPath $output) { throw 'Evidence exists; choose a new output directory' }
[IO.Directory]::CreateDirectory($output) | Out-Null
$artifacts = Join-Path $output 'artifacts'
[IO.Directory]::CreateDirectory($artifacts) | Out-Null
$buildSha = & git -C $workspace rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record Git HEAD' }
$dirty = @(& git -C $workspace status --porcelain).Count -ne 0
if ($LASTEXITCODE -ne 0) { throw 'Cannot record worktree state' }
$expectedDpr = [double]::Parse($Scale, [Globalization.CultureInfo]::InvariantCulture)
$preservedEnvironment = @{}
foreach ($name in @('PATH', 'QT_SCALE_FACTOR', 'MINI3D_TEST_V2_ARTIFACT_DIR',
        'MINI3D_TEST_V2_EXPECTED_DPR', 'MINI3D_TEST_V2_BUILD_SHA', 'MINI3D_TEST_V2_WORKTREE_DIRTY')) {
    $preservedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$originalNativeErrorPreference = $PSNativeCommandUseErrorActionPreference
try {
    $env:PATH = $qtBin + [IO.Path]::PathSeparator + $preservedEnvironment['PATH']
    $env:QT_SCALE_FACTOR = $Scale
    $env:MINI3D_TEST_V2_ARTIFACT_DIR = $artifacts
    $env:MINI3D_TEST_V2_EXPECTED_DPR = $Scale
    $env:MINI3D_TEST_V2_BUILD_SHA = $buildSha.Trim()
    $env:MINI3D_TEST_V2_WORKTREE_DIRTY = $dirty.ToString().ToLowerInvariant()
    $PSNativeCommandUseErrorActionPreference = $false
    $arguments = @($Filter, '--rng-seed', '20261002')
    & $executable @arguments 2>&1 | Tee-Object -FilePath (Join-Path $output 'editor-tests.log')
    $testExit = $LASTEXITCODE
    $evidenceIssues = [Collections.Generic.List[string]]::new()
    $caseReports = @(foreach ($file in (Get-ChildItem -LiteralPath $artifacts -Filter '*.json' -File |
            Sort-Object Name)) {
        try {
            $case = Get-Content -LiteralPath $file.FullName -Raw -Encoding utf8 | ConvertFrom-Json
        } catch {
            $evidenceIssues.Add("Invalid case JSON $($file.Name): $($_.Exception.Message)")
            continue
        }
        if ([string]::IsNullOrWhiteSpace($case.caseId) -or
                [string]::IsNullOrWhiteSpace($case.sourceObj)) {
            $evidenceIssues.Add("Missing case identity/source OBJ: $($file.Name)")
            continue
        }
        if ($case.buildSha -ne $buildSha.Trim() -or
                $case.worktreeDirty -ne $dirty.ToString().ToLowerInvariant()) {
            $evidenceIssues.Add("Build trace mismatch: $($file.Name)")
        }
        if ($null -eq $case.expectedDpr -or [Math]::Abs($case.expectedDpr - $expectedDpr) -ge 0.01 -or
                $null -eq $case.dpr.window -or $null -eq $case.dpr.viewport -or
                [Math]::Abs($case.dpr.window - $expectedDpr) -ge 0.01 -or
                [Math]::Abs($case.dpr.viewport - $expectedDpr) -ge 0.01 -or
                $case.actual.requestedDprObserved -ne $true -or
                $case.actual.framebufferMatchesViewportDpr -ne $true) {
            $evidenceIssues.Add("Requested DPR $expectedDpr not established: $($file.Name)")
        }
        $sourcePath = Join-Path $artifacts $case.sourceObj
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            $evidenceIssues.Add("Missing source OBJ: $($file.Name)")
        } elseif ((Get-FileHash -LiteralPath $sourcePath).Hash -ne $case.modelHash) {
            $evidenceIssues.Add("Source OBJ SHA256 mismatch: $($file.Name)")
        }
        $case
    })
    $requiredRecipes = @(1..12 | ForEach-Object { 'S{0:D2}' -f $_ })
    $missingRecipes = @($requiredRecipes | Where-Object { $_ -notin $caseReports.caseId })
    $fullRecipeFilter = $Filter -in @('[v2-acceptance],[help-editor]', '[v2-acceptance]')
    if ($fullRecipeFilter -and $missingRecipes.Count -ne 0) {
        $evidenceIssues.Add('Missing recipe reports: ' + ($missingRecipes -join ', '))
    }
    $observedDpr = @($caseReports | ForEach-Object {
        [ordered]@{ caseId = $_.caseId; screen = $_.screen; screenDpr = $_.dpr.screen
            windowDpr = $_.dpr.window; viewportDpr = $_.dpr.viewport
            requestedDprObserved = $_.actual.requestedDprObserved
            framebufferPixels = $_.resolution.framebufferPixels }
    })
    $files = @(Get-ChildItem -LiteralPath $artifacts -File | ForEach-Object {
        [ordered]@{ Path = $_.Name; Bytes = $_.Length; SHA256 = (Get-FileHash -LiteralPath $_.FullName).Hash }
    })
    $report = [ordered]@{
        caseId = 'v2-native-and-help-acceptance'
        buildSha = $buildSha.Trim()
        worktreeDirty = $dirty
        executableSHA256 = (Get-FileHash -LiteralPath $executable).Hash
        configuration = $Configuration
        referenceVersion = 'Blender v4.5.0 (fixed reference, not an external run)'
        keymap = 'Blender explicitly selected by native cases; F1 tested independently'
        requestedQtScaleFactor = $Scale
        expectedEffectiveDpr = $expectedDpr
        requestedNativeWindowLogical = @(1440,900)
        filter = $Filter
        randomSeed = 20261002
        expected = 'Selected assertions pass; actual window/viewport DPR reaches the requested value; source OBJ hashes and complete default S01-S12 evidence agree'
        actual = @{ exitCode = $testExit; files = $files; observedDpr = $observedDpr
            caseReports = $caseReports; evidenceIssues = @($evidenceIssues)
            recipeCoverage = @{ required = $requiredRecipes; observed = @($caseReports.caseId)
                missing = $missingRecipes; completeRecipeFilter = $fullRecipeFilter } }
        limitations = @('Qt synthetic input is not real Chinese IME evidence.',
            'Scale requests effective Qt DPR; absolute actual window/viewport DPR is verified, not only internal framebuffer consistency.',
            'QT_SCALE_FACTOR does not alter or prove native Windows scaling. A non-1 system base DPR is reported as the actual value and may fail the requested target.',
            'Real screen crossing and equal-DPR limitations are recorded in S12.json and editor-tests.log; equal-DPR screens do not verify mixed-DPI crossing.',
            'A custom filter may cover only selected recipes; recipeCoverage records any missing recipes.',
            'Model hashes are the delivered source/evaluated OBJ SHA256, not a .blend comparison.')
        status = $(if ($testExit -eq 0 -and $evidenceIssues.Count -eq 0) {
            'passed-with-limitations'
        } else { 'failed' })
    }
    $report | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $output 'report.json') -Encoding utf8
    if ($testExit -ne 0) { throw "Acceptance failed: $testExit. Read preserved evidence before retrying." }
    if ($evidenceIssues.Count -ne 0) {
        throw ('Acceptance evidence failed: ' + ($evidenceIssues -join '; ') + '. Read report.json.')
    }
} finally {
    foreach ($name in $preservedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $preservedEnvironment[$name], 'Process')
    }
    $PSNativeCommandUseErrorActionPreference = $originalNativeErrorPreference
}
