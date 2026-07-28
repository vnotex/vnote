@echo off
REM Initialization script for VNote development environment on Windows
REM This script calls init.sh using Git Bash

setlocal

echo ========================================
echo Initializing VNote development environment
echo ========================================
echo.

REM Locate Git Bash explicitly. Do NOT use a bare "where bash.exe": on Windows
REM that can resolve to the WSL launcher (C:\Windows\System32\bash.exe), which
REM expects /mnt/c-style paths and would break the /c-style path passed below.
set "GIT_BASH="

REM Prefer deriving Git Bash from the installed git.exe (Git for Windows layout:
REM <root>\cmd\git.exe with bash at <root>\bin\bash.exe).
for /f "delims=" %%I in ('where git.exe 2^>nul') do (
    if not defined GIT_BASH (
        for %%R in ("%%~dpI..") do set "GIT_ROOT=%%~fR"
    )
)
if defined GIT_ROOT (
    if exist "%GIT_ROOT%\bin\bash.exe" set "GIT_BASH=%GIT_ROOT%\bin\bash.exe"
)

REM Fall back to the standard Git for Windows install locations.
if not defined GIT_BASH (
    if exist "%ProgramFiles%\Git\bin\bash.exe" set "GIT_BASH=%ProgramFiles%\Git\bin\bash.exe"
)
if not defined GIT_BASH (
    if exist "%ProgramFiles(x86)%\Git\bin\bash.exe" set "GIT_BASH=%ProgramFiles(x86)%\Git\bin\bash.exe"
)
if not defined GIT_BASH (
    if exist "%LocalAppData%\Programs\Git\bin\bash.exe" set "GIT_BASH=%LocalAppData%\Programs\Git\bin\bash.exe"
)

if not defined GIT_BASH (
    echo Error: Git Bash not found
    echo.
    echo Please ensure Git for Windows is installed.
    echo You can download Git for Windows from: https://git-scm.com/download/win
    echo.
    echo This script requires Git Bash and does NOT use WSL.
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

REM Run the bash script with Git Bash (never WSL)
"%GIT_BASH%" "%UNIX_PATH%/init.sh"

if %errorlevel% neq 0 (
    echo.
    echo Error: Initialization failed
    pause
    exit /b 1
)

REM Verify libgit2 nested submodule pulled correctly (required by GitSyncBackend)
set "LIBGIT2_VERSION_HEADER=libs\vxcore\third_party\libgit2\include\git2\version.h"
if not exist "%LIBGIT2_VERSION_HEADER%" (
  echo ERROR: libgit2 submodule pulled but expected header not found at %LIBGIT2_VERSION_HEADER%. Check libs/vxcore/.gitmodules entry for libgit2. 1>&2
  exit /b 1
)
findstr "LIBGIT2_VERSION" "%LIBGIT2_VERSION_HEADER%"

echo.
echo You can now start developing VNote!
echo.
pause
