#!/usr/bin/env pwsh
# Sync API caller inventory script

$OutputPath = ".sisyphus/evidence/sync-callers-baseline.txt"
$OutputDir = Split-Path -Parent $OutputPath

# Ensure output directory exists
if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

# C ABI functions to search for
$CABIFunctions = @(
    "vxcore_sync_enable",
    "vxcore_sync_set_credentials",
    "vxcore_sync_disable",
    "vxcore_sync_trigger",
    "vxcore_sync_is_ready",
    "vxcore_sync_get_last_sync_utc"
)

# C++ SyncManager methods to search for
$CppMethods = @(
    "SyncManager::EnableSync",
    "SyncManager::DisableSync",
    "SyncManager::TriggerSync",
    "SyncManager::SetCredentials",
    "SyncManager::GetSyncConfig"
)

# Output buffer
$Results = @()

# Helper function to search for a pattern
function Search-Pattern {
    param(
        [string]$Pattern,
        [string]$PatternName
    )
    
    $script:Results += "=== $PatternName ==="
    $script:Results += ""
    
    $Found = $false
    
    try {
        $Matches = Select-String -Path "libs/**/*.cpp", "libs/**/*.h", "libs/**/*.hpp", "src/**/*.cpp", "src/**/*.h", "src/**/*.hpp", "tests/**/*.cpp", "tests/**/*.h" `
            -Pattern ([regex]::Escape($Pattern)) `
            -ErrorAction SilentlyContinue
        
        if ($Matches) {
            $Found = $true
            foreach ($Match in $Matches) {
                $RelPath = $Match.Path -replace [regex]::Escape((Get-Location).Path + "\"), ""
                $script:Results += "$RelPath`:$($Match.LineNumber):$($Match.Line.Trim())"
            }
        }
    }
    catch {
        # Silently continue on errors
    }
    
    if (-not $Found) {
        $script:Results += "(no call sites found)"
    }
    
    $script:Results += ""
}

# Search for all C ABI functions
foreach ($Func in $CABIFunctions) {
    Search-Pattern -Pattern $Func -PatternName $Func
}

# Search for all C++ methods
foreach ($Method in $CppMethods) {
    Search-Pattern -Pattern $Method -PatternName $Method
}

# Write results to file
$Results | Set-Content -LiteralPath $OutputPath -Encoding UTF8

Write-Host "Sync API caller inventory complete."
Write-Host "Output: $OutputPath"
