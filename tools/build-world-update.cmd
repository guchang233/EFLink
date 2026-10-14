@echo off
setlocal
cd /d "%~dp0.."
if "%~1"=="" exit /b 2
pwsh -NoProfile -File tools\build-cooperation-stage1.ps1 -ReuseGui -ModuleId "%~1"
exit /b %errorlevel%
