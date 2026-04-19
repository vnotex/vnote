# pack-win.ps1 — Build and pack VNote release ZIP on Windows.
# Mirrors the CI pipeline from .github/workflows/ci-win.yml
#
# Auto-detects Qt and Visual Studio installations. All defaults can be
# overridden via parameters.
#
# Usage:
#   .\scripts\pack-win.ps1                    # auto-detect everything
#   .\scripts\pack-win.ps1 -Clean             # wipe build dir first
#   .\scripts\pack-win.ps1 -BuildOnly         # build but skip pack
#   .\scripts\pack-win.ps1 -QtDir "C:\Qt\6.8.3\msvc2022_64"
#   .\scripts\pack-win.ps1 -VcVars "...\vcvars64.bat"

param(
  [string]$QtDir     = "",
  [string]$VcVars    = "",
  [string]$BuildDir  = "",
  [string]$BuildType = "Release",
  [switch]$Clean,
  [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path

# ── Auto-detect helpers ──────────────────────────────────────────────

function Find-QtDir {
  # 1. Explicit parameter (handled by caller)
  # 2. Qt6_DIR env var  (points to <kit>/lib/cmake/Qt6)
  if ($env:Qt6_DIR -and (Test-Path $env:Qt6_DIR)) {
    $kit = (Resolve-Path "$env:Qt6_DIR\..\..\..").Path
    if (Test-Path "$kit\bin\qmake.exe") {
      Write-Host "[detect] Qt from env Qt6_DIR: $kit" -ForegroundColor DarkGray
      return $kit
    }
  }
  # 3. CMAKE_PREFIX_PATH env var
  if ($env:CMAKE_PREFIX_PATH) {
    foreach ($p in $env:CMAKE_PREFIX_PATH -split ";") {
      if ((Test-Path "$p\bin\qmake.exe") -and ($p -match 'msvc')) {
        Write-Host "[detect] Qt from env CMAKE_PREFIX_PATH: $p" -ForegroundColor DarkGray
        return $p
      }
    }
  }
  # 4. Scan C:\Qt for highest-versioned msvc2022_64 kit
  $roots = @("C:\Qt", "D:\Qt", "$env:USERPROFILE\Qt")
  foreach ($root in $roots) {
    if (-not (Test-Path $root)) { continue }
    $kits = Get-ChildItem "$root\*\msvc2022_64" -Directory -ErrorAction SilentlyContinue |
      Where-Object { Test-Path "$($_.FullName)\bin\qmake.exe" } |
      Sort-Object { [version]($_.Parent.Name) } -Descending
    if ($kits) {
      $kit = $kits[0].FullName
      Write-Host "[detect] Qt from disk scan: $kit" -ForegroundColor DarkGray
      return $kit
    }
  }
  return $null
}

function Find-VcVars {
  $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { return $null }

  $vsPath = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
  if (-not $vsPath) { return $null }

  $bat = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
  if (Test-Path $bat) {
    Write-Host "[detect] VS from vswhere: $bat" -ForegroundColor DarkGray
    return $bat
  }
  return $null
}

# ── Resolve parameters ───────────────────────────────────────────────

if (-not $QtDir)    { $QtDir    = Find-QtDir }
if (-not $VcVars)   { $VcVars   = Find-VcVars }
if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot "build" }

# ── Validate ─────────────────────────────────────────────────────────

$fail = $false
if (-not $QtDir -or -not (Test-Path $QtDir)) {
  Write-Host "ERROR: Qt MSVC kit not found. Pass -QtDir or set Qt6_DIR env var." -ForegroundColor Red
  $fail = $true
}
if (-not $VcVars -or -not (Test-Path $VcVars)) {
  Write-Host "ERROR: vcvars64.bat not found. Pass -VcVars or install Visual Studio with C++ workload." -ForegroundColor Red
  $fail = $true
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
  Write-Host "ERROR: ninja not found in PATH. Install via: scoop install ninja" -ForegroundColor Red
  $fail = $true
}
if ($fail) { exit 1 }

Write-Host ""
Write-Host "[pack] Configuration:" -ForegroundColor Cyan
Write-Host "  Qt:        $QtDir"
Write-Host "  VS:        $VcVars"
Write-Host "  Build dir: $BuildDir"
Write-Host "  Type:      $BuildType"
Write-Host ""

# ── Clean ────────────────────────────────────────────────────────────

if ($Clean -and (Test-Path $BuildDir)) {
  Write-Host "[pack] Removing $BuildDir ..." -ForegroundColor Yellow
  Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path $BuildDir)) {
  New-Item -ItemType Directory -Force $BuildDir | Out-Null
}

# ── Build script ─────────────────────────────────────────────────────
# Everything runs inside a single cmd session so vcvars env is inherited.

$QtBinDir    = "$QtDir\bin"
$CmakePrefix = "$QtDir\lib\cmake\Qt6"

# Build targets:
#   1. cmake configure + build
#   2. 'deploy' target  = windeployqt + lrelease (translations)
#   3. cpack -G ZIP     = only ZIP, skip WiX (avoids WiX-not-installed error)
$packSteps = ""
if (-not $BuildOnly) {
  $packSteps = @"

echo [pack] Running windeployqt ...
cmake --build . --target deploy
if errorlevel 1 exit /b 1

echo [pack] Creating ZIP via CPack ...
cpack -G ZIP --config BundleConfig.cmake --verbose
if errorlevel 1 exit /b 1
"@
}

$script = @"
@echo off
call "$VcVars"
if errorlevel 1 exit /b 1

set "PATH=$QtBinDir;%PATH%"
set "Qt6_DIR=$CmakePrefix"
set "CMAKE_PREFIX_PATH=$QtDir"

cd /d "$BuildDir"

echo [pack] Configuring ...
cmake -GNinja -DCMAKE_BUILD_TYPE=$BuildType -DCMAKE_PREFIX_PATH="$QtDir" "$RepoRoot"
if errorlevel 1 exit /b 1

echo [pack] Building ...
cmake --build . --config $BuildType
if errorlevel 1 exit /b 1
$packSteps
"@

$scriptPath = Join-Path $BuildDir "_pack_build.cmd"
Set-Content -Path $scriptPath -Value $script -Encoding ASCII

Write-Host "[pack] Building ..." -ForegroundColor Cyan
cmd.exe /c $scriptPath
if ($LASTEXITCODE -ne 0) {
  Write-Error "[pack] Failed with exit code $LASTEXITCODE"
  exit $LASTEXITCODE
}

# ── Report ───────────────────────────────────────────────────────────

if (-not $BuildOnly) {
  $zips = Get-ChildItem "$BuildDir\VNote*.zip" -ErrorAction SilentlyContinue
  if ($zips) {
    Write-Host ""
    Write-Host "[pack] Done! Output:" -ForegroundColor Green
    foreach ($z in $zips) {
      $sizeMB = [math]::Round($z.Length / 1MB, 1)
      Write-Host "  $($z.FullName)  ($sizeMB MB)" -ForegroundColor Green
    }
  } else {
    Write-Warning "[pack] Build succeeded but no VNote*.zip found in $BuildDir"
  }
}
