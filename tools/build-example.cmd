@echo off
setlocal
cd /d "%~dp0"
if exist "..\examples\echo\echo.cpp" cd /d "%~dp0.."
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_VS=%%I"
if not defined TASK_VS exit /b 2
call "%TASK_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 2
if not exist build mkdir build
cl /nologo /std:c++20 /W4 /O2 /MT /utf-8 /Iinclude /LD examples\echo\echo.cpp /Fo:build\echo.obj /Fe:build\eflink.example.echo.dll /link /IMPLIB:build\eflink.example.echo.lib /DYNAMICBASE /NXCOMPAT
exit /b %errorlevel%
