# Launch the local Light-Help mode 2 helpers sequentially. UAC elevation is
# handled by the caller; this script never interacts with official login.
param([Parameter(Mandatory=$true)][ValidatePattern('^eflink\.coop\.stage1\.[0-9]+$')][string]$ModuleId)
$ErrorActionPreference='Stop'
$taskProject=Split-Path $PSScriptRoot
$taskHelpers=Join-Path $taskProject 'test-environments\light-help'
$taskHelper=Join-Path $taskHelpers 'launch-one.ps1'
if(-not (Test-Path -LiteralPath $taskHelper)){throw 'Local Light-Help helper is unavailable'}
$taskAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if(-not $taskAdmin){throw 'Run the Light-Help launcher elevated'}
$taskExpectedGame=(Resolve-Path -LiteralPath 'D:\Arknights Endfield\Endfield.exe').Path
foreach($taskInstance in 0,1) {
    # Do not rely on LASTEXITCODE: a successful PowerShell helper that never
    # invokes a native executable can leave that variable null or stale.
    $taskStartedAfter=[DateTime]::UtcNow
    & $taskHelper -Instance $taskInstance -ModuleId $ModuleId
    $taskStatePath=Join-Path $taskHelpers "instance-$taskInstance-launch.json"
    if(-not (Test-Path -LiteralPath $taskStatePath)){throw "Missing launch result for instance $taskInstance"}
    $taskStateFile=Get-Item -LiteralPath $taskStatePath
    if($taskStateFile.LastWriteTimeUtc -lt $taskStartedAfter){throw "Stale launch result for instance $taskInstance"}
    $taskState=Get-Content -LiteralPath $taskStatePath -Raw | ConvertFrom-Json
    if($taskState.phase -ne 'window_and_efml_ready' -or -not $taskState.boot_restored){throw "Launch failed for instance $taskInstance"}
    $taskProcess=Get-Process -Id $taskState.pid -ErrorAction Stop
    if($taskProcess.HasExited -or $taskProcess.Path -ne $taskExpectedGame){throw "Launch result does not identify a live game for instance $taskInstance"}
    Write-Output "Instance $taskInstance game and EFML ready: PID $($taskProcess.Id)"
}
