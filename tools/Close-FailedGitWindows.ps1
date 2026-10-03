# 模块名: Close-FailedGitWindows
# 功能概述: 仅清理指定 Git 命令启动失败后遗留的单标签 Windows Terminal 窗口。
# 对外接口: 默认只检查；-Close 发送窗口关闭；-ProtectedWindowHandle 固定保护；-WatchMinutes 限时兜底；-SelfTest 自检。
# 依赖关系: PowerShell 7.6、Win32、Windows UI Automation。
# 输入输出: 精确标题/进程/标签/错误正文到关闭请求与本机诊断日志，不记录正常终端正文。
# 异常与错误: 当前会话、前台、读取失败或多标签窗口一律保留；不结束后台，不修改系统终端配置。
# 维护说明: 只针对本次 0x800700e8 remote get-url origin 故障；限时任务无开机自启。
param(
    [switch]$Close,
    [ValidateRange(0,240)][int]$WatchMinutes = 0,
    [string]$LogDirectory = '',
    [long[]]$ProtectedWindowHandle = @(),
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'

function Test-FailedGitWindow {
    param([string]$Title, [string]$ProcessName, [int]$TabCount, [string]$Text,
        [long]$WindowHandle = 0, [long[]]$ProtectedHandles = @(), [long]$ForegroundHandle = 0)
    # 窗口保护优先于错误匹配，即使受保护窗口显示完整失败命令也不得关闭。
    if ($WindowHandle -ne 0 -and ($WindowHandle -in $ProtectedHandles -or $WindowHandle -eq $ForegroundHandle)) { return $false }
    $normalized = $Title.Replace('/', '\').Replace('\\', '\').Trim('"')
    if ($ProcessName -notin @('WindowsTerminal','WindowsTerminalPreview') -or
        $normalized -ine 'D:\Git\cmd\git.exe' -or $TabCount -ne 1) { return $false }
    $textValue = [regex]::Replace($Text, '\s+', ' ')
    $hasError = $textValue.Contains('2147942632') -or $textValue.Contains('0x800700e8', [StringComparison]::OrdinalIgnoreCase)
    return $hasError -and $textValue.Contains('safe.bareRepository=explicit') -and
        $textValue.Contains('core.hooksPath=NUL') -and $textValue.Contains('core.fsmonitor=false') -and
        $textValue.Contains('remote get-url origin')
}

if ($SelfTest) {
    $failureText = '[出现错误 2147942632 (0x800700e8) 启动 "git" -c safe.bareRepository=explicit -c core.hooksPath=NUL -c core.fsmonitor=false remote get-url origin 时]'
    $testCases = @(
        @{Title='D:\Git\cmd\git.exe'; ProcessName='WindowsTerminal'; TabCount=1; Text=$failureText; Expected=$true},
        @{Title='D:\\Git\\cmd\\git.exe'; ProcessName='WindowsTerminal'; TabCount=1; Text=$failureText; Expected=$true},
        @{Title='D:\Git\cmd\git.exe'; ProcessName='WindowsTerminal'; TabCount=2; Text=$failureText; Expected=$false},
        @{Title='Mini3D | normal terminal'; ProcessName='WindowsTerminal'; TabCount=1; Text=$failureText; Expected=$false},
        @{Title='D:\Git\cmd\git.exe'; ProcessName='git'; TabCount=1; Text=$failureText; Expected=$false},
        @{Title='D:\Git\cmd\git.exe'; ProcessName='WindowsTerminal'; TabCount=1; Text='git remote get-url origin succeeded'; Expected=$false},
        @{Title='D:\Git\cmd\git.exe'; ProcessName='WindowsTerminal'; TabCount=1; Text='0x800700e8 git status'; Expected=$false}
    )
    foreach ($case in $testCases) {
        $parameters = @{Title=$case.Title; ProcessName=$case.ProcessName; TabCount=$case.TabCount; Text=$case.Text}
        if ((Test-FailedGitWindow @parameters) -ne $case.Expected) { throw 'Guard classifier self-test failed' }
    }
    $protectionCases = @(
        @{WindowHandle=101L; ProtectedHandles=@(101L); ForegroundHandle=0L; Expected=$false},
        @{WindowHandle=101L; ProtectedHandles=@(42L,101L,203L); ForegroundHandle=0L; Expected=$false},
        @{WindowHandle=101L; ProtectedHandles=@(); ForegroundHandle=101L; Expected=$false},
        @{WindowHandle=101L; ProtectedHandles=@(42L); ForegroundHandle=203L; Expected=$true}
    )
    foreach ($case in $protectionCases) {
        $parameters = @{Title='D:\Git\cmd\git.exe'; ProcessName='WindowsTerminal'; TabCount=1; Text=$failureText;
            WindowHandle=$case.WindowHandle; ProtectedHandles=$case.ProtectedHandles; ForegroundHandle=$case.ForegroundHandle}
        if ((Test-FailedGitWindow @parameters) -ne $case.Expected) { throw 'Current-window protection self-test failed' }
    }
    "Passed: $($testCases.Count) classifier checks and $($protectionCases.Count) protection checks; no window was closed."
    return
}
if ($PSVersionTable.PSVersion.Major -ne 7 -or $PSVersionTable.PSVersion.Minor -ne 6) { throw 'Use PowerShell 7.6' }
if ($WatchMinutes -gt 0 -and -not $Close) { throw 'Watch mode requires explicit -Close' }
foreach ($assembly in @('UIAutomationTypes.dll','UIAutomationClient.dll')) {
    Add-Type -Path (Join-Path $env:WINDIR ('Microsoft.NET/Framework64/v4.0.30319/WPF/' + $assembly))
}
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class Mini3DFailedGitWindows {
    public delegate bool EnumCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumCallback callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr first, IntPtr second);
    public sealed class Entry { public long Handle; public uint ProcessId; public string Title; }
    public static Entry Read(IntPtr window) {
        var title = new StringBuilder(4096);
        GetWindowText(window, title, title.Capacity);
        uint processId;
        GetWindowThreadProcessId(window, out processId);
        return new Entry { Handle=window.ToInt64(), ProcessId=processId, Title=title.ToString() };
    }
    public static List<Entry> Windows() {
        var entries = new List<Entry>();
        EnumWindows((window, parameter) => {
            if (IsWindowVisible(window)) entries.Add(Read(window));
            return true;
        }, IntPtr.Zero);
        return entries;
    }
}
'@
if ($LogDirectory.Length -eq 0) {
    $LogDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) ('out/validation/git-window-guard-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$logRoot = [IO.Path]::GetFullPath($LogDirectory)
[IO.Directory]::CreateDirectory($logRoot) | Out-Null
$stopFile = Join-Path $logRoot 'stop.request'
$mutex = [Threading.Mutex]::new($false, 'Local\Mini3D_FailedGitWindowGuard')
$ownsMutex = $false
try {
    $ownsMutex = $mutex.WaitOne(0)
    if (-not $ownsMutex) { throw 'A failed-Git-window guard is already running' }
    $deadline = [DateTime]::UtcNow.AddMinutes($WatchMinutes)
    $closedTotal = 0
    $protectedHandles = [Collections.Generic.HashSet[long]]::new()
    foreach ($handle in @($ProtectedWindowHandle) + @(
        [Mini3DFailedGitWindows]::GetForegroundWindow().ToInt64(),
        [Mini3DFailedGitWindows]::GetConsoleWindow().ToInt64())) {
        if ($handle -gt 0) { [void]$protectedHandles.Add($handle) }
    }
    # 固定保存启动时的正常终端句柄，避免当前会话后来改成 Git 标题而被误关。
    foreach ($window in [Mini3DFailedGitWindows]::Windows()) {
        $process = Get-Process -Id $window.ProcessId -ErrorAction SilentlyContinue
        $title = $window.Title.Replace('/', '\').Replace('\\', '\').Trim('"')
        if ($process -and $process.ProcessName -in @('WindowsTerminal','WindowsTerminalPreview') -and
            $title -ine 'D:\Git\cmd\git.exe') { [void]$protectedHandles.Add($window.Handle) }
    }
    do {
        $checked = 0
        $matched = 0
        $closed = 0
        $skipped = 0
        $protectedSkipped = 0
        foreach ($window in [Mini3DFailedGitWindows]::Windows()) {
            $title = $window.Title.Replace('/', '\').Replace('\\', '\').Trim('"')
            if ($title -ine 'D:\Git\cmd\git.exe') { continue }
            $process = Get-Process -Id $window.ProcessId -ErrorAction SilentlyContinue
            if (-not $process -or $process.ProcessName -notin @('WindowsTerminal','WindowsTerminalPreview')) { continue }
            ++$checked
            if ($protectedHandles.Contains($window.Handle) -or
                $window.Handle -eq [Mini3DFailedGitWindows]::GetForegroundWindow().ToInt64()) {
                ++$skipped; ++$protectedSkipped; continue
            }
            try {
                $root = [System.Windows.Automation.AutomationElement]::FromHandle([IntPtr]$window.Handle)
                $nodes = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
                $tabCount = 0
                $text = [Text.StringBuilder]::new()
                foreach ($node in $nodes) {
                    if ($node.Current.ControlType -eq [System.Windows.Automation.ControlType]::TabItem) { ++$tabCount }
                    $pattern = $null
                    if ($node.TryGetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern, [ref]$pattern)) {
                        [void]$text.AppendLine($pattern.DocumentRange.GetText(8192))
                    }
                }
                if (-not (Test-FailedGitWindow $window.Title $process.ProcessName $tabCount $text.ToString() $window.Handle @($protectedHandles) ([Mini3DFailedGitWindows]::GetForegroundWindow().ToInt64()))) {
                    ++$skipped
                    continue
                }
                ++$matched
                if ($Close) {
                    $current = [Mini3DFailedGitWindows]::Read([IntPtr]$window.Handle)
                    if ($current.ProcessId -ne $window.ProcessId -or $current.Title -cne $window.Title) { ++$skipped; continue }
                    # 发送关闭前再次核对；用户切入目标窗口或请求停止后，不得继续关闭。
                    if ($protectedHandles.Contains($window.Handle) -or
                        $window.Handle -eq [Mini3DFailedGitWindows]::GetForegroundWindow().ToInt64() -or
                        (Test-Path -LiteralPath $stopFile)) { ++$skipped; ++$protectedSkipped; continue }
                    if (-not [Mini3DFailedGitWindows]::PostMessage([IntPtr]$window.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) { throw 'WM_CLOSE failed' }
                    for ($attempt = 0; $attempt -lt 10 -and [Mini3DFailedGitWindows]::IsWindow([IntPtr]$window.Handle); ++$attempt) {
                        Start-Sleep -Milliseconds 100
                    }
                    $wasClosed = -not [Mini3DFailedGitWindows]::IsWindow([IntPtr]$window.Handle)
                    if ($wasClosed) { ++$closed; ++$closedTotal }
                    [ordered]@{time=[DateTime]::UtcNow.ToString('o'); handle=$window.Handle; title=$window.Title; closed=$wasClosed} |
                        ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $logRoot 'events.jsonl') -Encoding utf8
                }
            } catch {
                ++$skipped
                [ordered]@{time=[DateTime]::UtcNow.ToString('o'); handle=$window.Handle; skipped=$true; reason=$_.Exception.Message} |
                    ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $logRoot 'events.jsonl') -Encoding utf8
            }
        }
        $state = [ordered]@{pid=$PID; checked=$checked; matched=$matched; closed=$closed; skipped=$skipped; closedTotal=$closedTotal;
            protectedWindowHandles=@($protectedHandles); protectedSkipped=$protectedSkipped;
            updatedUtc=[DateTime]::UtcNow.ToString('o'); deadlineUtc=$deadline.ToString('o'); stopFile=$stopFile; running=($WatchMinutes -gt 0)}
        $state | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logRoot 'state.json') -Encoding utf8
        if ($WatchMinutes -eq 0) { $state | ConvertTo-Json; break }
        if ([DateTime]::UtcNow -lt $deadline -and -not (Test-Path -LiteralPath $stopFile)) { Start-Sleep -Seconds 5 }
    } while ([DateTime]::UtcNow -lt $deadline -and -not (Test-Path -LiteralPath $stopFile))
    $state.running = $false
    $state | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logRoot 'state.json') -Encoding utf8
} finally {
    if ($ownsMutex) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
