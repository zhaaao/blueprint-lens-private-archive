# Build the BlueprintLens editor target, refusing to start while a running
# Editor holds the plugin DLL.
#
# Why this wrapper exists: UBT does not report a locked output DLL as a link
# error. It dies with an unhandled managed exception, 0xe0434352, inside a modal
# "dotnet.exe - Application Error" box. The box blocks the process, so a
# background build hangs instead of failing, and the only symptom is
# a log that stops right after "Running UnrealBuildTool: dotnet". Failing fast
# here turns that into one readable line.

param(
    [string]$UnrealRoot = "C:\Program Files\Epic Games\UE_5.8",
    [string]$Project = "",
    [string]$Target = "BlueprintLensProbeEditor",
    [switch]$KillEditor,
    [switch]$PrintResolvedPaths
)

$ErrorActionPreference = "Stop"

$Root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($Project)) {
    $Project = Join-Path $Root 'unreal\BlueprintLensProbe\BlueprintLensProbe.uproject'
}
$Project = [System.IO.Path]::GetFullPath($Project)
$ProjectDirectory = Split-Path -Parent $Project

$LockedOutputs = @(
    (Join-Path $ProjectDirectory 'Plugins\BlueprintLensExporter\Binaries\Win64\UnrealEditor-BlueprintLensEditor.dll'),
    (Join-Path $ProjectDirectory 'Plugins\BlueprintLensExporter\Binaries\Win64\UnrealEditor-BlueprintLensExporter.dll')
)

if ($PrintResolvedPaths) {
    Write-Output "PROJECT=$Project"
    foreach ($Path in $LockedOutputs) {
        Write-Output "LOCKED_OUTPUT=$Path"
    }
    exit 0
}

function Get-LockedOutput {
    foreach ($Path in $LockedOutputs) {
        if (-not (Test-Path $Path)) { continue }
        try {
            $Stream = [System.IO.File]::Open($Path, 'Open', 'Write', 'None')
            $Stream.Close()
        }
        catch {
            return $Path
        }
    }
    return $null
}

$Blockers = @(
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ProcessName -in @("UnrealEditor", "UnrealEditor-Cmd", "CrashReportClientEditor") }
)

if ($Blockers.Count -gt 0 -and $KillEditor) {
    Write-Output "BUILD_GATE closing $($Blockers.Count) Unreal process(es) because -KillEditor was passed"
    $Blockers | Stop-Process -Force
    Start-Sleep -Seconds 6
    $Blockers = @(
        Get-Process -ErrorAction SilentlyContinue |
            Where-Object { $_.ProcessName -in @("UnrealEditor", "UnrealEditor-Cmd", "CrashReportClientEditor") }
    )
}

$Locked = Get-LockedOutput

# One crashed build leaves a cmd -> dotnet -> dotnet chain alive behind a modal
# error box. It keeps Build.bat's %TEMP% lock, so every later build waits on it
# instead of running, and killing the MCP task does not kill the chain. The lock
# FILE always exists; only a live UBT process means the lock is actually held.
$OrphanUbt = @(
    Get-CimInstance Win32_Process -Filter "Name='dotnet.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.CommandLine -like '*UnrealBuildTool*' }
)

if ($OrphanUbt.Count -gt 0) {
    Write-Output "BUILD_GATE_FAILED"
    foreach ($Process in $OrphanUbt) {
        Write-Output "  orphaned UnrealBuildTool: pid=$($Process.ProcessId) started=$($Process.CreationDate)"
    }
    Write-Output "  A previous build is still alive, most likely stalled behind a modal"
    Write-Output "  dotnet.exe error dialog, and it holds Build.bat's lock. Every build"
    Write-Output "  after it waits rather than runs. End that process chain, then build"
    Write-Output "  again. If this was an external build, it may have hit a sandbox denial:"
    Write-Output "  grant writable_roots for UnrealBuildTool, the engine tree and"
    Write-Output "  ProgramData\Epic rather than widening the sandbox."
    exit 3
}

if ($Blockers.Count -gt 0 -or $Locked) {
    Write-Output "BUILD_GATE_FAILED"
    foreach ($Process in $Blockers) {
        Write-Output "  running: $($Process.ProcessName) pid=$($Process.Id)"
    }
    if ($Locked) {
        Write-Output "  locked output: $Locked"
    }
    Write-Output "  A running Editor holds the plugin DLL. UBT would die with an"
    Write-Output "  unhandled managed exception (0xe0434352) in a modal dialog that"
    Write-Output "  blocks the process instead of failing. Close the Editor, or pass"
    Write-Output "  -KillEditor, then build again."
    exit 2
}

# Skips this machine's all-platform SDK probe only; it does not change the
# BlueprintLens build or test scope.
$env:UE_SKIP_UBT_SDK_SETUP = '1'

& "$UnrealRoot\Engine\Build\BatchFiles\Build.bat" `
    $Target Win64 Development `
    "-Project=$Project" `
    -WaitMutex -Progress -MaxParallelActions=1
$BuildExit = $LASTEXITCODE

Write-Output "BUILD_EXIT=$BuildExit"
exit $BuildExit
