# One elevated step to load a different EF Link build in both test clients:
# close both games and launchers, register the package in the host and guest EFML
# homes, enable only that EF Link module, then open the main launcher.
# A module is never swapped into a running game (hot swaps crashed both clients).
#
#   switch-module.ps1 -Package <package folder>          (module id = folder name)
#
# Run elevated (the games run elevated). Afterwards: start the host game, quit the
# main launcher, run launch-second.ps1 elevated, start the guest game.
param([Parameter(Mandatory=$true)][string]$Package,
      [string]$Efml='D:\AI\2026\EF\EFML\build\official-launcher\runtime\efml.exe',
      [string]$GuestHome='D:\AI\2026\EF\duo\client2\EFML',
      [string]$Launcher='E:\Program Files\Hypergryph Launcher\Launcher.exe')
$ErrorActionPreference='Stop'
$Package=(Resolve-Path -LiteralPath $Package).Path
$id=(Get-Content -LiteralPath (Join-Path $Package 'module.json') -Raw -Encoding UTF8 | ConvertFrom-Json).id
if($id -notmatch '^eflink\.') {throw "not an EF Link package: $id"}
# Stop-Process, not taskkill: in Windows PowerShell 5.1 a native command's stderr
# ("process not found") becomes a terminating error under ErrorActionPreference=Stop.
Get-Process Endfield,Games -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
$deadline=(Get-Date).AddSeconds(60)
while((Get-Process Endfield -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {Start-Sleep -Seconds 1}
if(Get-Process Endfield -ErrorAction SilentlyContinue) {throw 'game did not exit; not touching module indexes'}
foreach($homeDir in @($null,$GuestHome)) {
    if($homeDir) {$env:EFML_HOME=$homeDir;$index=Join-Path $homeDir 'modules\index.json'}
    else {Remove-Item Env:EFML_HOME -ErrorAction SilentlyContinue;$index=Join-Path $env:LOCALAPPDATA 'EFML\modules\index.json'}
    $known=@((Get-Content -LiteralPath $index -Raw -Encoding UTF8 | ConvertFrom-Json).modules)
    if(-not ($known | Where-Object id -eq $id)) {& $Efml add --dev $Package | Out-Host}
    foreach($m in $known | Where-Object {$_.enabled -and $_.id -like 'eflink.*' -and $_.id -ne $id}) {& $Efml disable $m.id | Out-Host}
    & $Efml enable $id | Out-Host
}
Remove-Item Env:EFML_HOME -ErrorAction SilentlyContinue
Start-Process -FilePath $Launcher -WorkingDirectory (Split-Path $Launcher)
"loaded $id in host and guest; main launcher started"
