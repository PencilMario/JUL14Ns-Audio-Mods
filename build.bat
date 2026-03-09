@echo off
REM JUL14Ns Audio Mods - TeamSpeak 3 Plugin Build Script
REM This script builds the plugin and creates a .ts3_plugin package

setlocal enabledelayedexpansion
cd /d "%~dp0"

REM Check if PowerShell is available
where powershell >nul 2>nul
if errorlevel 1 (
    echo Error: PowerShell is not installed or not in PATH
    pause
    exit /b 1
)

REM Run the PowerShell build script
echo.
echo Building JUL14Ns Audio Mods Plugin...
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*

if errorlevel 1 (
    echo.
    echo Build failed! Press any key to exit...
    pause
    exit /b 1
) else (
    echo.
    echo Build completed successfully!
    pause
)
