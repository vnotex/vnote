@echo off
REM Initialization script for VNote development environment on Windows
REM This script calls init.sh using Git Bash

setlocal

echo ========================================
echo Initializing VNote development environment
echo ========================================
echo.

REM Check if Git Bash is available
where bash.exe >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Git Bash not found in PATH
    echo.
    echo Please ensure Git for Windows is installed and bash.exe is in your PATH.
    echo You can download Git for Windows from: https://git-scm.com/download/win
    echo.
    echo Alternatively, run the init.sh script directly from Git Bash:
    echo   bash scripts/init.sh
    echo.
    pause
    exit /b 1
)

REM Get the directory where this script is located
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

REM Convert Windows path to Unix-style path for Git Bash
REM Change C:\path\to\file to /c/path/to/file
set UNIX_PATH=%SCRIPT_DIR:\=/%
set UNIX_PATH=%UNIX_PATH::=%
set UNIX_PATH=/%UNIX_PATH%

REM Run the bash script
bash.exe "%UNIX_PATH%/init.sh"

if %errorlevel% neq 0 (
    echo.
    echo Error: Initialization failed
    pause
    exit /b 1
)

echo.
echo You can now start developing VNote!
echo.
pause
