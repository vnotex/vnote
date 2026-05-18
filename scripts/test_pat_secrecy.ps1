# W5.T3 — Credential secrecy audit (D15)
#
# Validates that the literal PAT used in tests (`test_pat_12345`), GitHub PAT
# prefixes (`ghp_`, `github_pat_`), and PAT-named JSON keys are NEVER present
# in production source, test logs, or notebook config JSON written by tests.
#
# References:
#   .sisyphus/plans/sync-completion-flow-overhaul.md (W5.T3 "Credential secrecy
#   grep test (D15)")
#
# Evidence files produced (always written):
#   .sisyphus/evidence/task-20-no-pat-in-logs.txt
#   .sisyphus/evidence/task-20-source-clean.txt
#   .sisyphus/evidence/task-20-json-clean.txt
#
# Exit codes:
#   0  - all three checks passed
#   1  - at least one check found PAT leakage
#
# Usage:
#   pwsh -File scripts/test_pat_secrecy.ps1 [-RepoRoot <path>]
#
# Re-runnable. Idempotent. Reads only — never modifies sources.

[CmdletBinding()]
param(
  [string]$RepoRoot = (Resolve-Path "$PSScriptRoot/..").Path,
  [string]$EvidenceDir = (Join-Path (Resolve-Path "$PSScriptRoot/..").Path ".sisyphus/evidence"),
  [string]$LogFile = $null
)

$ErrorActionPreference = 'Continue'

if (-not (Test-Path -LiteralPath $EvidenceDir)) {
  New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
}

$srcCleanEvidence = Join-Path $EvidenceDir "task-20-source-clean.txt"
$jsonCleanEvidence = Join-Path $EvidenceDir "task-20-json-clean.txt"
$logCleanEvidence = Join-Path $EvidenceDir "task-20-no-pat-in-logs.txt"

$failures = @()

# ---------------------------------------------------------------------------
# Check 1: production source tree (src/) is free of real PAT literals.
# Tests are allowed (the matrix test uses test_pat_12345 deliberately).
# Template/placeholder patterns like 'ghp_xxxx...' (>=10 consecutive 'x' or
# 'X' characters) and 'ghp_0000...' are EXCLUDED — these are UI hints and
# example strings shown to the user, not real credentials.
# ---------------------------------------------------------------------------
Write-Output ""
Write-Output "[Check 1] Scanning src/ for hardcoded PAT literals..."
$srcMatches = @()
$srcRoot = Join-Path $RepoRoot "src"
if (Test-Path -LiteralPath $srcRoot) {
  $srcFiles = Get-ChildItem -LiteralPath $srcRoot -Recurse -File -Include *.cpp,*.h,*.hpp
  foreach ($f in $srcFiles) {
    $m = Select-String -LiteralPath $f.FullName -Pattern "test_pat_12345","test_pat","ghp_[A-Za-z0-9]","github_pat_" -CaseSensitive:$false
    foreach ($hit in $m) {
      $line = $hit.Line
      # Exclude obvious placeholder/template patterns shown to users in UI
      # (e.g., 'ghp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' as a hint string).
      if ($line -match 'ghp_(x{8,}|X{8,}|0{8,})') { continue }
      if ($line -match 'github_pat_(x{8,}|X{8,}|0{8,})') { continue }
      $srcMatches += $hit
    }
  }
}
$srcReport = @()
$srcReport += "Check 1: Production source tree (src/**/*.{cpp,h,hpp}) scan for PAT literals."
$srcReport += "Patterns: 'test_pat_12345', 'test_pat', 'ghp_[A-Za-z0-9]', 'github_pat_'"
$srcReport += "Run at: $(Get-Date -Format 'o')"
$srcReport += "RepoRoot: $RepoRoot"
$srcReport += ""
if ($srcMatches.Count -eq 0) {
  $srcReport += "RESULT: PASS — 0 matches in production source tree."
  Write-Output "  PASS — src/ tree is clean of PAT literals."
} else {
  $srcReport += "RESULT: FAIL — $($srcMatches.Count) match(es) found:"
  foreach ($m in $srcMatches) {
    $srcReport += ("  {0}:{1}: {2}" -f $m.Path, $m.LineNumber, $m.Line.Trim())
  }
  Write-Output "  FAIL — $($srcMatches.Count) match(es) in src/ tree."
  $failures += "src-clean"
}
$srcReport | Out-File -FilePath $srcCleanEvidence -Encoding utf8
Write-Output "  Evidence: $srcCleanEvidence"

# ---------------------------------------------------------------------------
# Check 2: notebook JSON in vxcore test data dir must not contain PAT values
# or PAT-named field keys.
# ---------------------------------------------------------------------------
Write-Output ""
Write-Output "[Check 2] Scanning vxcore test data for PAT in JSON..."
$tempBase = [System.IO.Path]::GetTempPath()
$jsonMatches = @()
$jsonFilesScanned = 0
$candidatePaths = @(
  (Join-Path $tempBase "vxcore_test_data"),
  (Join-Path $tempBase "vxcore_test"),
  (Join-Path $tempBase "vnote-tests")
)
foreach ($p in $candidatePaths) {
  if (Test-Path -LiteralPath $p) {
    $jsonFiles = Get-ChildItem -LiteralPath $p -Recurse -File -Filter "*.json" -ErrorAction SilentlyContinue
    foreach ($jf in $jsonFiles) {
      $jsonFilesScanned++
      # 2a literal PAT value
      $lit = Select-String -LiteralPath $jf.FullName -Pattern "test_pat_12345" -SimpleMatch
      # 2b PAT-named JSON keys (avoid matching the word "path")
      $keys = Select-String -LiteralPath $jf.FullName -Pattern '"(syncPat|credential|token|password|secret)"\s*:'
      # 2c GitHub PAT prefixes
      $prefix = Select-String -LiteralPath $jf.FullName -Pattern "ghp_[A-Za-z0-9]","github_pat_"
      foreach ($m in $lit) { $jsonMatches += $m }
      foreach ($m in $keys) { $jsonMatches += $m }
      foreach ($m in $prefix) { $jsonMatches += $m }
    }
  }
}
$jsonReport = @()
$jsonReport += "Check 2: vxcore test data JSON files scan for PAT leakage."
$jsonReport += 'Patterns: test_pat_12345 (value); "(syncPat|credential|token|password|secret)": (keys); ghp_/github_pat_ (prefixes)'
$jsonReport += "Run at: $(Get-Date -Format 'o')"
$jsonReport += "Files scanned: $jsonFilesScanned"
$jsonReport += "Search roots tried: $($candidatePaths -join ', ')"
$jsonReport += ""
if ($jsonMatches.Count -eq 0) {
  if ($jsonFilesScanned -eq 0) {
    $jsonReport += "RESULT: PASS (vacuous) — no vxcore test JSON files present. Run state-machine tests first to populate."
    Write-Output "  PASS (vacuous) — no JSON files to scan."
  } else {
    $jsonReport += "RESULT: PASS — 0 matches across $jsonFilesScanned JSON file(s)."
    Write-Output "  PASS — JSON tree is clean of PAT leakage."
  }
} else {
  $jsonReport += "RESULT: FAIL — $($jsonMatches.Count) match(es) found:"
  foreach ($m in $jsonMatches) {
    $jsonReport += ("  {0}:{1}: {2}" -f $m.Path, $m.LineNumber, $m.Line.Trim())
  }
  Write-Output "  FAIL — $($jsonMatches.Count) match(es) in test JSON."
  $failures += "json-clean"
}
$jsonReport | Out-File -FilePath $jsonCleanEvidence -Encoding utf8
Write-Output "  Evidence: $jsonCleanEvidence"

# ---------------------------------------------------------------------------
# Check 3: captured-log evidence files in .sisyphus/evidence/ must not contain
# PAT values that would only appear if a service accidentally logged them.
# Scope is limited to files matching log/output naming patterns (*.log,
# *qtest*, *ctest*, *stdout*, *stderr*, *.out) so that markdown plans and
# design docs (which legitimately REFERENCE the literal PAT as documentation)
# do not trigger false positives.
# ---------------------------------------------------------------------------
Write-Output ""
Write-Output "[Check 3] Scanning captured log files in .sisyphus/evidence/ for PAT leakage..."
$logMatches = @()
$evidenceFilesScanned = 0
if (Test-Path -LiteralPath $EvidenceDir) {
  $logFiles = Get-ChildItem -LiteralPath $EvidenceDir -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object {
      $_.Name -ne "task-20-no-pat-in-logs.txt" -and
      $_.Name -ne "task-20-source-clean.txt" -and
      $_.Name -ne "task-20-json-clean.txt" -and
      ($_.Extension -in @(".log", ".out") -or
       $_.BaseName -match "(qtest|ctest|stdout|stderr|test[-_]?run|test-log|smoke)")
    }
  foreach ($lf in $logFiles) {
    $evidenceFilesScanned++
    $m = Select-String -LiteralPath $lf.FullName -Pattern "test_pat_12345" -SimpleMatch
    if ($m) { $logMatches += $m }
    $m2 = Select-String -LiteralPath $lf.FullName -Pattern "ghp_[A-Za-z0-9]","github_pat_"
    foreach ($hit in $m2) {
      $line = $hit.Line
      if ($line -match 'ghp_(x{8,}|X{8,}|0{8,})') { continue }
      $logMatches += $hit
    }
  }
}
# Optionally extend to an external log captured during a test run via -LogFile.
if ($LogFile -and (Test-Path -LiteralPath $LogFile)) {
  $evidenceFilesScanned++
  $m = Select-String -LiteralPath $LogFile -Pattern "test_pat_12345" -SimpleMatch
  if ($m) { $logMatches += $m }
}
$logReport = @()
$logReport += "Check 3: .sisyphus/evidence/ + optional -LogFile scan for PAT leakage."
$logReport += "Patterns: 'test_pat_12345', 'ghp_/github_pat_' prefixes"
$logReport += "Run at: $(Get-Date -Format 'o')"
$logReport += "Files scanned: $evidenceFilesScanned"
if ($LogFile) { $logReport += "Extra LogFile: $LogFile" }
$logReport += ""
if ($logMatches.Count -eq 0) {
  $logReport += "RESULT: PASS — 0 matches in evidence files."
  Write-Output "  PASS — evidence tree is clean of PAT leakage."
} else {
  $logReport += "RESULT: FAIL — $($logMatches.Count) match(es) found:"
  foreach ($m in $logMatches) {
    $logReport += ("  {0}:{1}: {2}" -f $m.Path, $m.LineNumber, $m.Line.Trim())
  }
  Write-Output "  FAIL — $($logMatches.Count) match(es) in evidence files."
  $failures += "log-clean"
}
$logReport | Out-File -FilePath $logCleanEvidence -Encoding utf8
Write-Output "  Evidence: $logCleanEvidence"

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Output ""
Write-Output "================================================================"
Write-Output "PAT secrecy audit summary"
Write-Output "================================================================"
if ($failures.Count -eq 0) {
  Write-Output "RESULT: PASS — all three secrecy checks clean."
  exit 0
} else {
  Write-Output "RESULT: FAIL — failed check(s): $($failures -join ', ')"
  exit 1
}
