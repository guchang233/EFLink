# Builds a uniquely named Stage 1 diagnostic DLL and runs standalone tests.
# Does not launch a game, preview UI, server, activation tool or package script.
param([switch]$ReuseGui, [switch]$TestsOnly, [string]$ModuleId="eflink.coop.stage1.1")
if ($ModuleId -notmatch "^eflink\.[a-z0-9_.-]{1,64}$") { throw "Invalid development module ID" }
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path $PSScriptRoot
$taskOutput = Join-Path $taskProject 'build\cooperation-stage1'
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
$taskFlags += '/DEFLINK_NATIVE_DIAGNOSTICS=1'
$taskModuleHeader = Join-Path $taskOutput 'development-module-id.h'
function Get-TaskSourceFingerprint {
    $taskSourceRows = @('native-diagnostics=1',"module=$ModuleId")
    foreach ($taskRoot in @('src','include','vendor\imgui')) {
        foreach ($taskSourceFile in (Get-ChildItem -LiteralPath (Join-Path $taskProject $taskRoot) -Recurse -File | Where-Object Extension -In '.h','.cpp' | Sort-Object FullName)) {
            $taskRelative=$taskSourceFile.FullName.Substring($taskProject.Length).Replace('\','/')
            $taskSourceRows += $taskRelative+' '+(Get-FileHash -LiteralPath $taskSourceFile.FullName -Algorithm SHA256).Hash
        }
    }
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes(($taskSourceRows -join "`n")))).ToLowerInvariant()
}
$taskFingerprint=Get-TaskSourceFingerprint
$taskBuildId='dev/'+$taskFingerprint.Substring(0,32)
$taskManifestPath=Join-Path $taskOutput "$ModuleId-build.json"
if ($TestsOnly) {
    if (-not (Test-Path -LiteralPath $taskManifestPath) -or -not (Test-Path -LiteralPath (Join-Path $taskOutput "$ModuleId.dll"))) { throw 'TestsOnly requires the matching built DLL and manifest' }
    $taskPrevious=Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
    if ($taskPrevious.source_sha256 -ne $taskFingerprint) { throw 'Source differs from the DLL; perform a full development build' }
}
Set-Content -LiteralPath $taskModuleHeader -Value @(('#define EFLINK_MODULE_ID "'+$ModuleId+'"'),('#define EFLINK_BUILD_ID "'+$taskBuildId+'"')) -Encoding ascii
@{module=$ModuleId;build_id=$taskBuildId;source_sha256=$taskFingerprint;tests_passed=$false} | ConvertTo-Json | Set-Content -LiteralPath $taskManifestPath -Encoding utf8
$taskFlags += "/FI$taskModuleHeader"
function Invoke-TaskCompiler([string[]]$taskArguments) {
    & cl.exe @taskArguments
    if ($LASTEXITCODE -ne 0) { throw "C++ compilation failed: $LASTEXITCODE" }
}
Push-Location $taskOutput
try {
    if (-not $ReuseGui -and -not $TestsOnly) {
        Invoke-TaskCompiler ($taskFlags + @('/c',"$taskProject\src\link_gui.cpp","$taskProject\vendor\imgui\imgui.cpp","$taskProject\vendor\imgui\imgui_draw.cpp","$taskProject\vendor\imgui\imgui_tables.cpp","$taskProject\vendor\imgui\imgui_widgets.cpp","$taskProject\vendor\imgui\backends\imgui_impl_win32.cpp","$taskProject\vendor\imgui\backends\imgui_impl_dx11.cpp"))
    }
    if ($ReuseGui -and -not $TestsOnly) {
        # The panel is included by link_gui.cpp; only the third-party ImGui
        # objects are safe to reuse across development source changes.
        Invoke-TaskCompiler ($taskFlags + @('/c',"$taskProject\src\link_gui.cpp"))
    }
    $taskGui = @('link_gui.obj','imgui.obj','imgui_draw.obj','imgui_tables.obj','imgui_widgets.obj','imgui_impl_win32.obj','imgui_impl_dx11.obj')
    if (-not $TestsOnly) {
        Invoke-TaskCompiler ($taskFlags + @('/bigobj','/Z7','/LD',"$taskProject\src\link_module.cpp") + $taskGui + @("/Fe:$ModuleId.dll",'/link','ws2_32.lib','iphlpapi.lib','user32.lib','gdi32.lib','imm32.lib','dwmapi.lib','comctl32.lib','d3d11.lib','dxgi.lib','d3dcompiler.lib','dcomp.lib','/DEBUG:FULL',"/PDB:$ModuleId.pdb",'/DYNAMICBASE','/NXCOMPAT'))
        Invoke-TaskCompiler ($taskFlags + @('/LD',"$taskProject\examples\echo\echo.cpp",'/Fe:eflink.example.echo.dll','/link','/DYNAMICBASE','/NXCOMPAT'))
    }
    $taskTests = @('link_coop_contract_test','link_world_authority_test','link_native_bindings_test','link_native_value_abi_test','link_authority_appearance_test','link_authority_session_test','link_damage_scope_test','link_world_delta_test','link_appearance_test','link_session_test','link_protocol_test','link_net_test','link_input_ownership_test','link_map_consent_test','link_api_test')
    foreach ($taskTest in $taskTests) {
        Invoke-TaskCompiler ($taskFlags + @("$taskProject\tests\$taskTest.cpp","/Fe:$taskTest.exe",'/link','ws2_32.lib','iphlpapi.lib'))
        $taskArguments = if ($taskTest -eq 'link_api_test') { @((Join-Path $taskOutput "$ModuleId.dll"),$ModuleId) } else { @() }
        & (Join-Path $taskOutput "$taskTest.exe") @taskArguments
        if ($LASTEXITCODE -ne 0) { throw "Offline test failed: $taskTest" }
    }
    if ((Get-TaskSourceFingerprint) -ne $taskFingerprint) { throw 'Source changed during compilation; discard this build and rebuild' }
    @{module=$ModuleId;build_id=$taskBuildId;source_sha256=$taskFingerprint;tests_passed=$true;test_count=$taskTests.Count;
      dll_sha256=(Get-FileHash -LiteralPath (Join-Path $taskOutput "$ModuleId.dll") -Algorithm SHA256).Hash;
      verified_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath $taskManifestPath -Encoding utf8
    Write-Output 'All standalone Stage 1 checks passed. Source fingerprint verified. This script does not launch the game.'
} finally { Pop-Location }
