param([Parameter(Mandatory=$true)][string]$ModuleId)
$ErrorActionPreference='Stop'
if($ModuleId -notmatch '^eflink\.coop\.stage1\.[0-9]+$') {throw 'Use a Stage 1 development module ID'}
$taskProject=Split-Path $PSScriptRoot
$taskBuild=Join-Path $taskProject 'build\cooperation-stage1'
$taskManifestPath=Join-Path $taskBuild "$ModuleId-build.json"
$taskManifest=Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
$taskDll=Join-Path $taskBuild "$ModuleId.dll"
if(-not $taskManifest.tests_passed -or $taskManifest.module -ne $ModuleId -or (Get-FileHash -LiteralPath $taskDll -Algorithm SHA256).Hash -ne $taskManifest.dll_sha256) {throw 'Build and passed test evidence do not match'}
$taskRows=@('native-diagnostics=1',"module=$ModuleId")
foreach($taskRoot in @('src','include','vendor\imgui')) {
    foreach($taskFile in (Get-ChildItem -LiteralPath (Join-Path $taskProject $taskRoot) -Recurse -File | Where-Object Extension -In '.h','.cpp' | Sort-Object FullName)) {
        $taskRows += $taskFile.FullName.Substring($taskProject.Length).Replace('\','/')+' '+(Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash
    }
}
$taskHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes(($taskRows -join "`n")))).ToLowerInvariant()
if($taskHash -ne $taskManifest.source_sha256) {throw 'Current source differs from the verified build'}
$taskSnapshot=Join-Path $taskBuild "$ModuleId-recovery"
$taskArchive="$taskSnapshot.zip"
if((Test-Path -LiteralPath $taskSnapshot) -or (Test-Path -LiteralPath $taskArchive)) {throw 'A recovery snapshot already exists; it is immutable'}
New-Item -ItemType Directory -Path $taskSnapshot | Out-Null
foreach($taskRoot in @('src','include','tests','docs','vendor')) {Copy-Item -LiteralPath (Join-Path $taskProject $taskRoot) -Destination $taskSnapshot -Recurse}
$taskTools=Join-Path $taskSnapshot 'tools'
New-Item -ItemType Directory -Path $taskTools | Out-Null
Get-ChildItem -LiteralPath $PSScriptRoot -File | Copy-Item -Destination $taskTools
$taskNotices=Join-Path $PSScriptRoot 'third_party'
if(Test-Path -LiteralPath $taskNotices) {Copy-Item -LiteralPath $taskNotices -Destination $taskTools -Recurse}
foreach($taskFile in @('module.json','LICENSE','README.md')) {Copy-Item -LiteralPath (Join-Path $taskProject $taskFile) -Destination $taskSnapshot}
Copy-Item -LiteralPath $taskDll -Destination $taskSnapshot
$taskSymbols=Join-Path $taskBuild "$ModuleId.pdb"
if(Test-Path -LiteralPath $taskSymbols) {Copy-Item -LiteralPath $taskSymbols -Destination $taskSnapshot}
Copy-Item -LiteralPath $taskManifestPath -Destination $taskSnapshot
Copy-Item -LiteralPath (Join-Path $taskBuild ('build-and-tests-'+($ModuleId.Split('.')[-1])+'.log')) -Destination $taskSnapshot
Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $taskSnapshot | ForEach-Object FullName) -DestinationPath $taskArchive -CompressionLevel Optimal
Write-Output $taskArchive
Write-Output ((Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash)
