@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_VS=%%I"
if not defined TASK_VS exit /b 2
call "%TASK_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 2
if not exist build\authority mkdir build\authority
pushd build\authority
cl /nologo /std:c++20 /EHa /W4 /O2 /MT /utf-8 /DNOMINMAX /I..\..\include /I..\..\src ..\..\tests\link_world_authority_test.cpp /Fe:link_world_authority_test.exe
if errorlevel 1 exit /b 1
link_world_authority_test.exe
exit /b %errorlevel%
