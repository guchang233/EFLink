@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_VS=%%I"
if not defined TASK_VS exit /b 2
call "%TASK_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 2
pushd build
set "TASK_FLAGS=/nologo /std:c++20 /EHa /W4 /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\include /I..\src"
for %%T in (link_net_test link_protocol_test) do (
    cl %TASK_FLAGS% ..\tests\%%T.cpp /Fd:%%T.pdb /Fe:%%T.exe /link ws2_32.lib iphlpapi.lib
    if errorlevel 1 exit /b 1
    %%T.exe
    if errorlevel 1 exit /b 1
)
exit /b 0
