# Builds a separate development DLL and runs only standalone/offline tests.
# Does not launch a game, preview UI, server, activation tool or package script.
param([switch]$ReuseGui)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path $PSScriptRoot
$taskOutput = Join-Path $taskProject 'build\appearance-preliminary'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVs) { throw 'Visual Studio C++ tools unavailable' }
$taskVc = (Get-ChildItem -LiteralPath (Join-Path $taskVs 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$taskSdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$taskSdkVersion = (Get-ChildItem -LiteralPath (Join-Path $taskSdk 'Lib') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$env:PATH = (Join-Path $taskVc 'bin\Hostx64\x64') + ';' + $env:PATH
$env:INCLUDE = (@((Join-Path $taskVc 'include')) + @(@('ucrt','shared','um','winrt') | ForEach-Object { Join-Path $taskSdk "Include\$taskSdkVersion\$_" })) -join ';'
$env:LIB = (@((Join-Path $taskVc 'lib\x64'),(Join-Path $taskSdk "Lib\$taskSdkVersion\ucrt\x64"),(Join-Path $taskSdk "Lib\$taskSdkVersion\um\x64"))) -join ';'
$taskFlags = @('/nologo','/std:c++20','/EHa','/W4','/O2','/MT','/utf-8','/DUNICODE','/D_UNICODE','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',"/I$taskProject\include","/I$taskProject\src","/I$taskProject\vendor\imgui")
function Invoke-TaskCompiler([string[]]$taskArguments) {
    & cl.exe @taskArguments
    if ($LASTEXITCODE -ne 0) { throw "C++ compilation failed: $LASTEXITCODE" }
}
Push-Location $taskOutput
try {
    if (-not $ReuseGui) {
        Invoke-TaskCompiler ($taskFlags + @('/c',"$taskProject\src\link_gui.cpp","$taskProject\vendor\imgui\imgui.cpp","$taskProject\vendor\imgui\imgui_draw.cpp","$taskProject\vendor\imgui\imgui_tables.cpp","$taskProject\vendor\imgui\imgui_widgets.cpp","$taskProject\vendor\imgui\backends\imgui_impl_win32.cpp","$taskProject\vendor\imgui\backends\imgui_impl_dx11.cpp"))
    }
    $taskGui = @('link_gui.obj','imgui.obj','imgui_draw.obj','imgui_tables.obj','imgui_widgets.obj','imgui_impl_win32.obj','imgui_impl_dx11.obj')
    Invoke-TaskCompiler ($taskFlags + @('/LD',"$taskProject\src\link_module.cpp") + $taskGui + @('/Fe:eflink.core.dll','/link','ws2_32.lib','iphlpapi.lib','user32.lib','gdi32.lib','imm32.lib','dwmapi.lib','comctl32.lib','d3d11.lib','dxgi.lib','d3dcompiler.lib','dcomp.lib','/DYNAMICBASE','/NXCOMPAT'))
    Invoke-TaskCompiler ($taskFlags + @('/LD',"$taskProject\examples\echo\echo.cpp",'/Fe:eflink.example.echo.dll','/link','/DYNAMICBASE','/NXCOMPAT'))
    $taskTests = @('link_appearance_test','link_session_test','link_protocol_test','link_net_test','link_input_ownership_test','link_map_consent_test','link_native_op_policy_test','link_official_view_test','link_official_monster_test','link_api_test')
    foreach ($taskTest in $taskTests) {
        Invoke-TaskCompiler ($taskFlags + @("$taskProject\tests\$taskTest.cpp","/Fe:$taskTest.exe",'/link','ws2_32.lib','iphlpapi.lib'))
        $taskArguments = if ($taskTest -eq 'link_api_test') { @((Join-Path $taskOutput 'eflink.core.dll'),'eflink.core') } else { @() }
        & (Join-Path $taskOutput "$taskTest.exe") @taskArguments
        if ($LASTEXITCODE -ne 0) { throw "Offline test failed: $taskTest" }
    }
    Write-Output 'All offline appearance checks and existing standalone tests passed. No game was launched.'
} finally { Pop-Location }
