@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_VS=%%I"
if not defined TASK_VS exit /b 2
call "%TASK_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 2
pushd build
cl /nologo /std:c++20 /EHa /W4 /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\include /I..\src /I..\vendor\imgui ..\tests\link_ui_test.cpp imgui.obj imgui_draw.obj imgui_tables.obj imgui_widgets.obj /Fe:link_ui_test.exe /link user32.lib imm32.lib
if errorlevel 1 exit /b 1
link_ui_test.exe
exit /b %errorlevel%
