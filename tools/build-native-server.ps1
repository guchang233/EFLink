param([string]$Compiler = '', [ValidateSet('test', 'fixtures', 'authority', 'all')][string]$Step = 'test')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
if (-not $Compiler) { $Compiler = Join-Path $taskRoot 'build\toolchains\zig-x86_64-windows-0.17.0\zig.exe' }
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw 'Zig 0.17.0 compiler is required.' }
$taskPreviousCache = $env:ZIG_GLOBAL_CACHE_DIR
Push-Location (Join-Path $taskRoot 'server')
try {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $taskRoot 'build\zig-global-cache'
    [string[]]$taskSteps = if ($Step -eq 'all') { @('test', 'fixtures', 'authority') } else { @($Step) }
    & $Compiler build @taskSteps '-Doptimize=safe' --cache-dir (Join-Path $taskRoot 'build\server-cache') --prefix (Join-Path $taskRoot 'build\native-server') --summary all
    if ($LASTEXITCODE -ne 0) { throw "Native server verification failed ($LASTEXITCODE)." }
} finally {
    $env:ZIG_GLOBAL_CACHE_DIR = $taskPreviousCache
    Pop-Location
}
