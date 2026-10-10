@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_VS=%%I"
if not defined TASK_VS exit /b 2
call "%TASK_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 2
if "%~1"=="" exit /b 2
pushd build
set "TASK_FLAGS=/nologo /std:c++20 /EHa /W4 /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DEFLINK_NATIVE_DIAGNOSTICS=1 /I..\include /I..\src /I..\vendor\imgui"
cl %TASK_FLAGS% ..\tests\link_official_view_test.cpp /Fe:link_official_view_test.exe
if errorlevel 1 exit /b 1
link_official_view_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl %TASK_FLAGS% ..\tests\link_native_op_policy_test.cpp /Fe:link_native_op_policy_test.exe
if errorlevel 1 exit /b 1
link_native_op_policy_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl %TASK_FLAGS% ..\tests\link_map_consent_test.cpp /Fe:link_map_consent_test.exe
if errorlevel 1 exit /b 1
link_map_consent_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl %TASK_FLAGS% ..\tests\link_session_test.cpp /Fe:link_session_test.exe /link ws2_32.lib iphlpapi.lib
if errorlevel 1 exit /b 1
link_session_test.exe
if not "%errorlevel%"=="0" exit /b 1
cl %TASK_FLAGS% /c ..\src\link_gui.cpp
if errorlevel 1 exit /b 1
cl %TASK_FLAGS% /DEFLINK_MODULE_ID=\"%~1\" /LD ..\src\link_module.cpp link_gui.obj imgui.obj imgui_draw.obj imgui_tables.obj imgui_widgets.obj imgui_impl_win32.obj imgui_impl_dx11.obj /Fe:%~1.dll /link ws2_32.lib iphlpapi.lib user32.lib gdi32.lib imm32.lib dwmapi.lib comctl32.lib d3d11.lib dxgi.lib d3dcompiler.lib /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b 1
link_api_test.exe %~1.dll %~1
exit /b %errorlevel%
