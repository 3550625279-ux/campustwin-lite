@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
if not exist "build\campustwin.exe" (
    echo [ERROR] build\campustwin.exe not found. Please run: cmake -B build ^&^& cmake --build build
    pause
    exit /b 1
)
"build\campustwin.exe" --ui
if errorlevel 1 pause
