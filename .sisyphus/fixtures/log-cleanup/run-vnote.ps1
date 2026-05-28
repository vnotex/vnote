param([Parameter(Mandatory)][string]$EvidencePath,
      [string]$QtLoggingRules="", [string]$VnoteLogRules="",
      [string]$VxcoreLogLevel="", [int]$WaitSeconds=10)
$fixRoot = "$PSScriptRoot"
# Reset to pristine fixture (delete any per-run state created last time)
Remove-Item -Recurse -Force "$fixRoot/runtime" -ErrorAction SilentlyContinue
Copy-Item -Recurse "$fixRoot/appdata" "$fixRoot/runtime/appdata"
Copy-Item -Recurse "$fixRoot/localappdata" "$fixRoot/runtime/localappdata"
$env:APPDATA = "$fixRoot/runtime/appdata"
$env:LOCALAPPDATA = "$fixRoot/runtime/localappdata"
# Auto-detect MSVC multi-config (Debug/) vs flat layout
$buildRoot = "$PSScriptRoot/../../../build-debug"
$vtextDirs = @("$buildRoot/libs/vtextedit/src/Debug", "$buildRoot/libs/vtextedit/src")
$exeCands  = @("$buildRoot/src/Debug/vnote.exe", "$buildRoot/src/vnote.exe")
$vtextDir  = ($vtextDirs | Where-Object { Test-Path $_ } | Select-Object -First 1)
$exe       = ($exeCands  | Where-Object { Test-Path $_ } | Select-Object -First 1)
if (-not $exe)      { Write-Error "vnote.exe not found under $buildRoot/src"; exit 1 }
if (-not $vtextDir) { Write-Error "VTextEdit.dll dir not found under $buildRoot/libs/vtextedit/src"; exit 1 }
$env:PATH = "C:/Qt/6.9.3/msvc2022_64/bin;$vtextDir;" + $env:PATH
if ($QtLoggingRules)  { $env:QT_LOGGING_RULES  = $QtLoggingRules }  else { Remove-Item Env:QT_LOGGING_RULES  -ErrorAction SilentlyContinue }
if ($VnoteLogRules)   { $env:VNOTE_LOG_RULES   = $VnoteLogRules }   else { Remove-Item Env:VNOTE_LOG_RULES   -ErrorAction SilentlyContinue }
if ($VxcoreLogLevel)  { $env:VXCORE_LOG_LEVEL  = $VxcoreLogLevel }  else { Remove-Item Env:VXCORE_LOG_LEVEL  -ErrorAction SilentlyContinue }
$proc = Start-Process -FilePath $exe -RedirectStandardError $EvidencePath -PassThru -WindowStyle Hidden
Start-Sleep -Seconds $WaitSeconds
# Loader-trap guard per AGENTS.md
$loaded = $false
try { $proc.Refresh(); $loaded = ($proc.Modules | Where-Object { $_.ModuleName -ieq "VTextEdit.dll" }).Count -gt 0 } catch {}
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
if (-not $loaded) { Write-Error "loader trap: VTextEdit.dll never loaded"; exit 1 }
exit 0
