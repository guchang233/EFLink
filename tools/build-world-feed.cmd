@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_VS=%%I"
if not defined TASK_VS exit /b 2
call "%TASK_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 2
if not exist build mkdir build
pushd build
set "TASK_FEED_ID=%~1"
if "%TASK_FEED_ID%"=="" set "TASK_FEED_ID=eflink.worldfeed1"
cl /nologo /std:c++20 /EHa /W4 /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DEFLINK_INJECTOR_ID=\"%TASK_FEED_ID%\" /I..\include /I..\src /LD ..\src\authority\link_world_injector.cpp /Fd:%TASK_FEED_ID%.pdb /Fe:%TASK_FEED_ID%.dll /link /DYNAMICBASE /NXCOMPAT
exit /b %errorlevel%
