@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TASK_VS=%%I"
if not defined TASK_VS exit /b 2
call "%TASK_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 2
if not exist build mkdir build
set "TASK_ID=%~1"
if "%TASK_ID%"=="" set "TASK_ID=eflink.core"
pushd build
set "TASK_FLAGS=/nologo /std:c++20 /EHa /W4 /O2 /MT /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\include /I..\src /I..\vendor\imgui"
if "%~2"=="reuse-gui" goto module
if "%~2"=="reuse-imgui" goto gui
cl %TASK_FLAGS% /c ..\src\link_gui.cpp ..\vendor\imgui\imgui.cpp ..\vendor\imgui\imgui_draw.cpp ..\vendor\imgui\imgui_tables.cpp ..\vendor\imgui\imgui_widgets.cpp ..\vendor\imgui\backends\imgui_impl_win32.cpp ..\vendor\imgui\backends\imgui_impl_dx11.cpp
if errorlevel 1 exit /b 1
goto module
:gui
cl %TASK_FLAGS% /c ..\src\link_gui.cpp
if errorlevel 1 exit /b 1
:module
set "TASK_GUI=link_gui.obj imgui.obj imgui_draw.obj imgui_tables.obj imgui_widgets.obj imgui_impl_win32.obj imgui_impl_dx11.obj"
cl %TASK_FLAGS% /DEFLINK_MODULE_ID=\"%TASK_ID%\" /LD ..\src\link_module.cpp %TASK_GUI% /Fe:%TASK_ID%.dll /link ws2_32.lib iphlpapi.lib user32.lib gdi32.lib imm32.lib dwmapi.lib comctl32.lib d3d11.lib dxgi.lib d3dcompiler.lib dcomp.lib /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b 1
cl %TASK_FLAGS% ..\src\link_preview.cpp /Fe:EFLink.Preview.exe /link user32.lib
if errorlevel 1 exit /b 1
cl %TASK_FLAGS% /LD ..\examples\echo\echo.cpp /Fe:eflink.example.echo.dll /link /DYNAMICBASE /NXCOMPAT
if errorlevel 1 exit /b 1
for %%T in (link_net_test link_api_test link_protocol_test link_session_test link_input_ownership_test link_map_consent_test link_native_op_policy_test link_official_view_test) do (
    cl %TASK_FLAGS% ..\tests\%%T.cpp /Fe:%%T.exe /link ws2_32.lib iphlpapi.lib
    if errorlevel 1 exit /b 1
)
link_protocol_test.exe
if not "%errorlevel%"=="0" exit /b 1
link_net_test.exe
if not "%errorlevel%"=="0" exit /b 1
link_session_test.exe
if not "%errorlevel%"=="0" exit /b 1
link_input_ownership_test.exe
if not "%errorlevel%"=="0" exit /b 1
link_map_consent_test.exe
if not "%errorlevel%"=="0" exit /b 1
link_native_op_policy_test.exe
if not "%errorlevel%"=="0" exit /b 1
link_official_view_test.exe
if not "%errorlevel%"=="0" exit /b 1
link_api_test.exe %TASK_ID%.dll %TASK_ID%
exit /b %errorlevel%
