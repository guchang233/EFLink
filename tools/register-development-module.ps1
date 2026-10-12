param([Parameter(Mandatory=$true)][string]$ModuleId,[string]$DllPath,[string]$Efml='D:\EFML\build\native\efml.exe',[switch]$PrepareOnly)
$ErrorActionPreference='Stop'
if($ModuleId -notmatch '^eflink\.(coop\.|probe)[a-z0-9_.-]{1,64}$') {throw 'Use a unique EF Link development ID'}
$taskProject=Split-Path $PSScriptRoot
if(-not $DllPath) {$DllPath=Join-Path $taskProject "build\cooperation-stage1\$ModuleId.dll"}
$taskSource=(Resolve-Path -LiteralPath $DllPath).Path
$taskSourceHash=(Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash
if($ModuleId -match '^eflink\.coop\.stage1\.') {
    $taskBuildManifest=Join-Path (Split-Path $taskSource) "$ModuleId-build.json"
    if(-not (Test-Path -LiteralPath $taskBuildManifest)) {throw 'Stage 1 registration requires a verified build manifest'}
    $taskBuild=Get-Content -LiteralPath $taskBuildManifest -Raw | ConvertFrom-Json
    if($taskBuild.module -ne $ModuleId -or -not $taskBuild.tests_passed -or $taskBuild.dll_sha256 -ne $taskSourceHash) {throw 'Stage 1 DLL does not match its passed test build'}
}
$taskPackage=Join-Path $taskProject "build\dev\$ModuleId"
if(Test-Path -LiteralPath $taskPackage) {throw 'Development package already exists; choose a new ID rather than overwrite a loaded DLL'}
New-Item -ItemType Directory -Force -Path (Join-Path $taskPackage 'native\windows-x64') | Out-Null
Copy-Item -LiteralPath $taskSource -Destination (Join-Path $taskPackage "native\windows-x64\$ModuleId.dll")
$taskManifest=Get-Content -LiteralPath (Join-Path $taskProject 'module.json') -Raw | ConvertFrom-Json
$taskManifest.id=$ModuleId;$taskManifest.name='EF Link Stage 1 development'
$taskManifest.libraries.'windows-x64'="native/windows-x64/$ModuleId.dll"
$taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskPackage 'module.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $taskProject 'LICENSE') -Destination $taskPackage
if($taskBuildManifest) {Copy-Item -LiteralPath $taskBuildManifest -Destination (Join-Path $taskPackage 'build-evidence.json')}
if(-not $PrepareOnly) {
    & $Efml add --dev $taskPackage
    if($LASTEXITCODE -ne 0) {throw 'EFML development registration failed'}
} else {Write-Output ('Prepared offline package: '+$taskPackage)}
Write-Output ($taskSourceHash+' '+$ModuleId)
