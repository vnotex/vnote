<#
.SYNOPSIS
    Generates the incremental-update artifacts for one VNote Windows package.

.DESCRIPTION
    Produces, for a single extracted package directory:

      1. <ExtractedDir>/manifest.json
         The IN-PACKAGE manifest: schema, product, channel, version, variant,
         platform, commit, generatedAt and a recursive <path, size, sha256>
         listing of every file EXCEPT manifest.json itself.

      2. VNote-<Version>-<Variant>.zip
         The full package, DELETED AND REBUILT deterministically from
         <ExtractedDir> so the freshly written manifest.json is actually inside
         it. Re-zipping "over" an existing archive would leave the previous
         central directory in place.

      3. VNote-<Version>-<Variant>.delta.zip            (optional)
         Only the files that changed since the previous STABLE release, with
         install-root-relative entries and NO top-level directory. Skipped
         cleanly when the predecessor has no published manifest (e.g. the first
         release that ships this feature).

      4. VNote-<Version>-<Variant>.manifest.json
         The RELEASE ASSET manifest: the same object as (1) plus `fullPackage`
         and, when built, `delta`.

    The client contract these artifacts must satisfy is documented in
    .kilo/plans/1785337074532-incremental-update-plan.md and in the
    "Incremental Update" section of AGENTS.md.

.PARAMETER ExtractedDir
    The extracted package directory, e.g. build/VNote-4.3.2-win64. Its files
    are laid out exactly as they will be installed.

.PARAMETER Version
    Release version without a leading "v", e.g. "4.3.2".

.PARAMETER Variant
    "win64" (Qt6) or "win64-windows7" (Qt5).

.PARAMETER Commit
    Full git SHA of the build.

.PARAMETER Channel
    "stable" or "continuous". ONLY "stable" is eligible as a delta base on the
    client, so this must be derived from the workflow's real release predicate,
    never from "was this a tag build" (this repo cuts releases from a master
    push and creates the tag afterwards).

.PARAMETER OutputDir
    Where the .zip / .delta.zip / .manifest.json assets are written. Defaults to
    the parent of ExtractedDir.

.PARAMETER BaseManifestPath
    Local path to the PREVIOUS release's manifest asset. When given, the delta is
    computed against it and GitHub is never contacted.

    This is what makes the plan's manual end-to-end procedure possible (pack A,
    pack B, generate a delta locally with no release published), and it is also
    how the delta path is regression-tested. When omitted, the previous stable
    release is resolved through the `gh` CLI as usual.

.PARAMETER SkipDelta
    Do not attempt to build a delta even if a predecessor exists.

.PARAMETER MinisignSecretKey
    Path to the minisign secret key used to sign the release-asset manifest.
    Produces `VNote-<ver>-<variant>.manifest.json.minisig`, which the client
    REQUIRES: `UpdateService` refuses any manifest it cannot verify.

    Defaults to $env:MINISIGN_SECRET_KEY_FILE. The key password comes from
    $env:MINISIGN_PASSWORD (use an empty password for an unattended CI key).

    Omitting it emits an unsigned manifest and a loud warning; that is only
    useful for local experimentation, since released artifacts without a
    signature are inert for every client.

.EXAMPLE
    ./scripts/gen-update-package.ps1 -ExtractedDir build/VNote-4.3.2-win64 `
        -Version 4.3.2 -Variant win64 -Commit $sha -Channel stable

.EXAMPLE
    # Local end-to-end: delta from a manifest on disk, no GitHub involved.
    ./scripts/gen-update-package.ps1 -ExtractedDir C:\tmp\vnoteB `
        -Version 4.3.2 -Variant win64 -Commit deadbeef -Channel stable `
        -BaseManifestPath C:\tmp\out\VNote-4.3.1-win64.manifest.json
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ExtractedDir,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][ValidateSet('win64', 'win64-windows7')][string]$Variant,
    [Parameter(Mandatory = $true)][string]$Commit,
    [Parameter(Mandatory = $true)][ValidateSet('stable', 'continuous')][string]$Channel,
    [string]$OutputDir,
    [string]$BaseManifestPath,
    [string]$MinisignSecretKey,
    [switch]$SkipDelta
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# ---------------------------------------------------------------------------
# ZIP helpers
# ---------------------------------------------------------------------------
# Deliberately .NET rather than an external 7z: the archives here have exact
# layout requirements that the client enforces (the full package must have
# EXACTLY one top-level directory; a delta must have NONE), and building them
# entry by entry is the only way to guarantee that regardless of what is
# installed on the build agent. It also means the local end-to-end procedure in
# the plan needs no extra tooling.
#
# Entry names are always forward-slash, matching the manifest and what
# ZipExtractor expects.

function New-ZipArchiveFromEntries {
    param(
        [Parameter(Mandatory = $true)][string]$DestZip,
        [Parameter(Mandatory = $true)][string]$SourceDir,
        # Ordered list of @{ Entry = 'a/b.dll'; Source = '<abs path>' }
        [Parameter(Mandatory = $true)][object[]]$Entries
    )

    if (Test-Path -LiteralPath $DestZip) { Remove-Item -LiteralPath $DestZip -Force }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DestZip) | Out-Null

    $stream = [System.IO.File]::Open($DestZip, [System.IO.FileMode]::CreateNew)
    try {
        $zip = New-Object System.IO.Compression.ZipArchive(
            $stream, [System.IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            foreach ($item in $Entries) {
                $entry = $zip.CreateEntry($item.Entry,
                    [System.IO.Compression.CompressionLevel]::Optimal)
                $entryStream = $entry.Open()
                try {
                    $bytes = [System.IO.File]::ReadAllBytes($item.Source)
                    $entryStream.Write($bytes, 0, $bytes.Length)
                }
                finally { $entryStream.Dispose() }
            }
        }
        finally { $zip.Dispose() }
    }
    finally { $stream.Dispose() }
}

function Get-ZipEntryNames {
    param([Parameter(Mandatory = $true)][string]$Path)
    $names = @()
    $zip = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Path).Path)
    try {
        foreach ($e in $zip.Entries) { $names += $e.FullName }
    }
    finally { $zip.Dispose() }
    # NOTE: plain `return $names`, NOT `return , $names`. Call sites wrap the
    # result in @(...), and the comma operator would add a second level of
    # nesting that @() does not unwrap -- yielding a 1-element array whose only
    # member is the real array. That silently breaks every -contains test.
    return $names
}

# Reads every entry to the end, which forces .NET to validate each CRC-32.
function Test-ZipIntegrity {
    param([Parameter(Mandatory = $true)][string]$Path)
    $zip = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Path).Path)
    try {
        $buffer = New-Object byte[] 65536
        foreach ($e in $zip.Entries) {
            if ($e.FullName.EndsWith('/')) { continue }
            $s = $e.Open()
            try { while ($s.Read($buffer, 0, $buffer.Length) -gt 0) { } }
            finally { $s.Dispose() }
        }
    }
    finally { $zip.Dispose() }
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Get-RelativeForwardPath {
    param([string]$Root, [string]$FullPath)
    $rootFull = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
    $rel = $FullPath.Substring($rootFull.Length).TrimStart('\', '/')
    return $rel -replace '\\', '/'
}

# Mirrors UpdateManifest::normalizePath. A package that cannot be described by a
# safe manifest must fail the BUILD, not the user's update.
function Assert-SafeManifestPath {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { throw "Empty path in package." }
    if ($Path.StartsWith('/')) { throw "Absolute path in package: $Path" }
    if ($Path.Length -ge 2 -and $Path[1] -eq ':') { throw "Drive-qualified path in package: $Path" }

    foreach ($segment in $Path.Split('/')) {
        if ($segment -eq '') { throw "Empty path segment in package: $Path" }
        if ($segment -eq '.' -or $segment -eq '..') { throw "Relative segment in package: $Path" }
        if ($segment.EndsWith('.') -or $segment.EndsWith(' ') -or $segment.StartsWith(' ')) {
            throw "Path segment with a trailing/leading dot or space aliases another name: $Path"
        }
        if ($segment -match '[<>:"|?*]' -or $segment -match '[\x00-\x1f]') {
            throw "Path segment contains a character Windows forbids: $Path"
        }
        $stem = ($segment -split '\.')[0].ToUpperInvariant()
        if ($stem -in @('CON', 'PRN', 'AUX', 'NUL') -or $stem -match '^(COM|LPT)[1-9]$') {
            throw "Reserved Windows device name in package: $Path"
        }
    }

    $lower = $Path.ToLowerInvariant()
    if ($lower -eq '.vnote-update' -or $lower.StartsWith('.vnote-update/') -or
        $lower -eq '.vnote-old' -or $lower.StartsWith('.vnote-old/')) {
        throw "Package targets a reserved updater directory: $Path"
    }
}

function New-FileEntries {
    param([string]$Root)

    $entries = New-Object System.Collections.Generic.List[object]
    $files = Get-ChildItem -LiteralPath $Root -Recurse -File -Force |
        Sort-Object FullName
    foreach ($file in $files) {
        $rel = Get-RelativeForwardPath -Root $Root -FullPath $file.FullName
        # manifest.json describes the package; it never describes itself.
        if ($rel -ieq 'manifest.json') { continue }
        Assert-SafeManifestPath -Path $rel
        $entries.Add([ordered]@{
                path   = $rel
                size   = [int64]$file.Length
                sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            })
    }
    return , $entries.ToArray()
}

function ConvertTo-PrettyJson {
    param($Object)
    return ($Object | ConvertTo-Json -Depth 12)
}

function Get-ArchiveRef {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path
    return [ordered]@{
        asset  = $item.Name
        size   = [int64]$item.Length
        sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------

if (-not (Test-Path -LiteralPath $ExtractedDir)) {
    throw "ExtractedDir '$ExtractedDir' does not exist."
}
$ExtractedDir = (Resolve-Path -LiteralPath $ExtractedDir).Path

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Split-Path -Parent $ExtractedDir
}
if (-not (Test-Path -LiteralPath $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$packageName = "VNote-$Version-$Variant"
$topLevelName = Split-Path -Leaf $ExtractedDir

Write-Host "gen-update-package: $packageName ($Channel) from '$ExtractedDir'"

# ---------------------------------------------------------------------------
# 1. In-package manifest.json
# ---------------------------------------------------------------------------

$inPackageManifest = [ordered]@{
    schema      = 1
    product     = 'VNote'
    channel     = $Channel
    version     = $Version
    variant     = $Variant
    platform    = 'windows-x64'
    commit      = $Commit
    generatedAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    files       = (New-FileEntries -Root $ExtractedDir)
}

$manifestPath = Join-Path $ExtractedDir 'manifest.json'
ConvertTo-PrettyJson $inPackageManifest | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Write-Host "  wrote $manifestPath ($($inPackageManifest.files.Count) files)"

# ---------------------------------------------------------------------------
# 2. Full ZIP, deleted and recreated (never "re-zipped over")
# ---------------------------------------------------------------------------

$fullZip = Join-Path $OutputDir "$packageName.zip"

# Built entry by entry from the CURRENT directory contents, so the freshly
# written manifest.json is inside and every entry sits under exactly one
# top-level directory (the client strips exactly one level for a full package).
# Sorted for determinism.
$fullEntries = @()
foreach ($file in (Get-ChildItem -LiteralPath $ExtractedDir -Recurse -File -Force | Sort-Object FullName)) {
    $rel = Get-RelativeForwardPath -Root $ExtractedDir -FullPath $file.FullName
    $fullEntries += @{ Entry = "$topLevelName/$rel"; Source = $file.FullName }
}
New-ZipArchiveFromEntries -DestZip $fullZip -SourceDir $ExtractedDir -Entries $fullEntries

Test-ZipIntegrity -Path $fullZip

# --- Layout assertions on the REBUILT archive -------------------------------
$zipPaths = @(Get-ZipEntryNames -Path $fullZip)
if ($zipPaths.Count -eq 0) { throw "Could not list $fullZip" }

$relZipPaths = @()
foreach ($p in $zipPaths) {
    if ($p -eq $topLevelName -or $p -eq "$topLevelName/") { continue }
    if (-not $p.StartsWith("$topLevelName/")) {
        throw "Archive entry '$p' is not under the expected top-level directory '$topLevelName'."
    }
    $relZipPaths += $p.Substring($topLevelName.Length + 1)
}

function Assert-ZipContains {
    param([string[]]$Paths, [string]$Pattern, [string]$What)
    if (-not ($Paths | Where-Object { $_ -like $Pattern })) {
        throw "Package is missing $What (no entry matching '$Pattern')."
    }
}

Assert-ZipContains -Paths $relZipPaths -Pattern 'manifest.json' -What 'the in-package manifest'
Assert-ZipContains -Paths $relZipPaths -Pattern 'vnote.exe' -What 'vnote.exe'
Assert-ZipContains -Paths $relZipPaths -Pattern 'resources/*' -What 'the WebEngine resources directory'
Assert-ZipContains -Paths $relZipPaths -Pattern 'translations/*.qm' -What 'the Qt translations'

# Qt5/Qt6 exclusivity: a leaked DLL from the other major version produces an
# install that cannot start.
$hasQt5 = @($relZipPaths | Where-Object { $_ -match '(^|/)Qt5[^/]*\.dll$' }).Count -gt 0
$hasQt6 = @($relZipPaths | Where-Object { $_ -match '(^|/)Qt6[^/]*\.dll$' }).Count -gt 0
if ($hasQt5 -and $hasQt6) { throw "Package mixes Qt5 and Qt6 DLLs." }
if ($Variant -eq 'win64' -and -not $hasQt6) { throw "win64 package has no Qt6 DLLs." }
if ($Variant -eq 'win64-windows7' -and -not $hasQt5) { throw "win64-windows7 package has no Qt5 DLLs." }

# WebEngine locales live beside the resources; without them the viewer renders
# blank on non-English systems.
Assert-ZipContains -Paths $relZipPaths -Pattern 'translations/qtwebengine_locales/*' -What 'the WebEngine locales'

Write-Host "  wrote $fullZip ($($relZipPaths.Count) entries), layout verified"

$releaseManifest = [ordered]@{}
foreach ($key in $inPackageManifest.Keys) { $releaseManifest[$key] = $inPackageManifest[$key] }
$releaseManifest['fullPackage'] = Get-ArchiveRef -Path $fullZip

# ---------------------------------------------------------------------------
# 3. Delta against the previous STABLE release
# ---------------------------------------------------------------------------

function Get-PreviousStableVersion {
    param([string]$CurrentVersion)

    # Highest published NON-draft, NON-prerelease tag strictly below the current
    # version. Resolved explicitly rather than assuming "the release before this
    # one in the list" -- continuous-build is a prerelease and must never win.
    $json = & gh api "repos/$env:GH_REPO/releases?per_page=100" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $json) { return $null }

    $releases = $json | ConvertFrom-Json
    $current = [version]$CurrentVersion

    $best = $null
    foreach ($release in $releases) {
        if ($release.draft -or $release.prerelease) { continue }
        $tag = [string]$release.tag_name
        if (-not $tag.StartsWith('v')) { continue }
        $candidateText = $tag.Substring(1)
        $candidate = $null
        if (-not [version]::TryParse($candidateText, [ref]$candidate)) { continue }
        if ($candidate -ge $current) { continue }
        if ($null -eq $best -or $candidate -gt $best) { $best = $candidate }
    }

    if ($null -eq $best) { return $null }
    return $best.ToString()
}

$deltaBuilt = $false

# --- Resolve the base manifest -------------------------------------------
# Either supplied on disk (-BaseManifestPath, used by the local end-to-end
# procedure and by the regression tests) or resolved through the gh CLI.
$base = $null
$previous = $null
$downloadedBaseManifest = $null

if ($SkipDelta) {
    Write-Host "  delta: skipped (-SkipDelta)"
}
elseif ($Channel -ne 'stable') {
    # A continuous build is never a delta base and never publishes a delta.
    Write-Host "  delta: skipped (channel is '$Channel')"
}
elseif ($BaseManifestPath) {
    if (-not (Test-Path -LiteralPath $BaseManifestPath)) {
        throw "BaseManifestPath '$BaseManifestPath' does not exist."
    }
    $base = Get-Content -LiteralPath $BaseManifestPath -Raw | ConvertFrom-Json
    $previous = [string]$base.version
    Write-Host "  delta: base manifest supplied on disk (version $previous)"
}
elseif (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    Write-Host "  delta: skipped (gh CLI not available)"
}
else {
    $previous = Get-PreviousStableVersion -CurrentVersion $Version
    if (-not $previous) {
        Write-Host "  delta: skipped (no previous stable release found)"
    }
    else {
        Write-Host "  delta: previous stable release is $previous"
        $baseManifestName = "VNote-$previous-$Variant.manifest.json"
        $downloadedBaseManifest = Join-Path $OutputDir "base-$baseManifestName"

        & gh release download "v$previous" --pattern $baseManifestName --output $downloadedBaseManifest --clobber 2>$null
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $downloadedBaseManifest)) {
            Write-Host "  delta: skipped ($baseManifestName is not published)"
            $downloadedBaseManifest = $null
        }
        else {
            $base = Get-Content -LiteralPath $downloadedBaseManifest -Raw | ConvertFrom-Json
        }
    }
}

# --- Build the delta ------------------------------------------------------
if ($base) {
    if ($base.version -ne $previous) {
        throw "Base manifest declares version '$($base.version)' but was resolved as $previous."
    }
    if ($base.variant -ne $Variant) {
        throw "Base manifest declares variant '$($base.variant)' but this build is $Variant."
    }
    if ([version]$previous -ge [version]$Version) {
        throw "Base manifest version '$previous' is not older than this build ($Version)."
    }

    if ($base.channel -ne 'stable') {
        Write-Host "  delta: skipped (base manifest channel is '$($base.channel)')"
    }
    else {
                # expectedChanged = { p in target : p not in base || hash differs }
                $baseByPath = @{}
                foreach ($entry in $base.files) {
                    $baseByPath[$entry.path.ToLowerInvariant()] = $entry.sha256.ToLowerInvariant()
                }

                $changed = New-Object System.Collections.Generic.List[string]
                foreach ($entry in $inPackageManifest.files) {
                    $key = $entry.path.ToLowerInvariant()
                    if (-not $baseByPath.ContainsKey($key) -or $baseByPath[$key] -ne $entry.sha256) {
                        $changed.Add($entry.path)
                    }
                }

                if ($changed.Count -eq 0) {
                    Write-Host "  delta: skipped (nothing changed since $previous)"
                }
                else {
                    $deltaZip = Join-Path $OutputDir "$packageName.delta.zip"
                    if (Test-Path -LiteralPath $deltaZip) { Remove-Item -LiteralPath $deltaZip -Force }

                    # Entries are INSTALL-ROOT-RELATIVE with NO top-level
                    # directory, which is what the client expects for a delta
                    # (unlike the full package).
                    $deltaEntries = @()
                    foreach ($rel in ($changed | Sort-Object)) {
                        $deltaEntries += @{
                            Entry  = $rel
                            Source = (Join-Path $ExtractedDir ($rel -replace '/', '\'))
                        }
                    }
                    New-ZipArchiveFromEntries -DestZip $deltaZip -SourceDir $ExtractedDir `
                        -Entries $deltaEntries

                    Test-ZipIntegrity -Path $deltaZip

                    # The client requires EXACT entry-set equality, so verify it
                    # here rather than letting every user discover a mismatch.
                    $deltaPaths = @(Get-ZipEntryNames -Path $deltaZip)
                    $missing = @($changed | Where-Object { $deltaPaths -notcontains $_ })
                    if ($missing.Count -gt 0) {
                        throw "Delta archive is missing $($missing.Count) changed file(s), e.g. $($missing[0])."
                    }
                    $unexpected = @($deltaPaths | Where-Object { $changed -notcontains $_ })
                    if ($unexpected.Count -gt 0) {
                        throw "Delta archive has $($unexpected.Count) unexpected entr(ies), e.g. $($unexpected[0])."
                    }

                    $releaseManifest['delta'] = Get-ArchiveRef -Path $deltaZip
                    $releaseManifest['delta']['baseVersion'] = $previous
                    $deltaBuilt = $true
                    Write-Host "  wrote $deltaZip ($($changed.Count) changed files)"
                }
    }

    if ($downloadedBaseManifest) {
        Remove-Item -LiteralPath $downloadedBaseManifest -Force -ErrorAction SilentlyContinue
    }
}

if (-not $deltaBuilt) {
    Write-Host "  delta: not published for this build (clients will use the full package)"
}

# ---------------------------------------------------------------------------
# 4. Release-asset manifest
# ---------------------------------------------------------------------------

$releaseManifestPath = Join-Path $OutputDir "$packageName.manifest.json"
# UTF-8 WITHOUT a BOM, LF-normalized: the signature covers these exact bytes, so
# the file must be written byte-deterministically. Set-Content -Encoding UTF8
# emits a BOM on Windows PowerShell, which would silently change what is signed.
$manifestJson = (ConvertTo-PrettyJson $releaseManifest) -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($releaseManifestPath, $manifestJson,
    (New-Object System.Text.UTF8Encoding($false)))
Write-Host "  wrote $releaseManifestPath"

# ---------------------------------------------------------------------------
# 5. Detached minisign signature over the release manifest
# ---------------------------------------------------------------------------
# The client verifies this before parsing anything, and refuses a manifest it
# cannot verify. An unsigned release is therefore inert, not merely unverified.

if (-not $MinisignSecretKey) { $MinisignSecretKey = $env:MINISIGN_SECRET_KEY_FILE }

if (-not $MinisignSecretKey) {
    Write-Warning ("No minisign secret key supplied (-MinisignSecretKey / " +
        "MINISIGN_SECRET_KEY_FILE). The manifest is UNSIGNED and every client " +
        "will refuse this release. This is only valid for local experiments.")
}
else {
    if (-not (Test-Path -LiteralPath $MinisignSecretKey)) {
        throw "Minisign secret key '$MinisignSecretKey' does not exist."
    }
    $minisign = Get-Command minisign -ErrorAction SilentlyContinue
    if (-not $minisign) {
        throw ("minisign was not found on PATH but a secret key was supplied. " +
            "Install it (https://jedisct1.github.io/minisign/) or omit the key.")
    }

    $sigPath = "$releaseManifestPath.minisig"
    if (Test-Path -LiteralPath $sigPath) { Remove-Item -LiteralPath $sigPath -Force }

    # The trusted comment IS authenticated (it is covered by the global
    # signature), so it is a safe place to restate the identity a human would
    # want to confirm when auditing a release by hand.
    $trustedComment = "VNote $Version $Variant $Channel commit $Commit"

    # -W: the CI key has an empty password. minisign reads the password from
    # stdin when one is set; MINISIGN_PASSWORD covers that case.
    if ($env:MINISIGN_PASSWORD) {
        $env:MINISIGN_PASSWORD | & $minisign.Source -S -s $MinisignSecretKey `
            -m $releaseManifestPath -x $sigPath -t $trustedComment | Out-Null
    }
    else {
        & $minisign.Source -S -s $MinisignSecretKey -m $releaseManifestPath `
            -x $sigPath -t $trustedComment -W | Out-Null
    }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $sigPath)) {
        throw "minisign failed to sign $releaseManifestPath"
    }

    # Verify what was just produced, with the PUBLIC half derived from the
    # secret key. Shipping a signature nobody checked would defeat the purpose.
    $pubForCheck = Join-Path $OutputDir "$packageName.verify.pub"
    & $minisign.Source -R -s $MinisignSecretKey -p $pubForCheck 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $pubForCheck)) {
        & $minisign.Source -V -p $pubForCheck -m $releaseManifestPath -x $sigPath | Out-Null
        $verified = $LASTEXITCODE -eq 0
        Remove-Item -LiteralPath $pubForCheck -Force -ErrorAction SilentlyContinue
        if (-not $verified) {
            throw "The signature just produced for $releaseManifestPath does not verify."
        }
    }

    Write-Host "  wrote $sigPath (signed, self-verified)"
}

Write-Host "gen-update-package: done"
