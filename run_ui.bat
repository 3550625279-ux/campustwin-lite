@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
set "exe=build\campustwin.exe"
if not exist "%exe%" set "exe=build\Release\campustwin.exe"
if not exist "%exe%" (
    echo [ERROR] Build campustwin.exe first. See README.md.
    pause
    exit /b 1
)
"%exe%" --ui
if errorlevel 1 pause
