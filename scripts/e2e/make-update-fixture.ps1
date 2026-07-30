# Builds a synthetic VNote install tree that satisfies every layout assertion in
# scripts/gen-update-package.ps1, so the generator can be exercised end to end
# without a 20-minute Release build + windeployqt.
#
# The point is to validate the GENERATOR (manifest, hashing, deterministic zip,
# layout assertions, delta computation), not Qt itself, so the payloads are
# small stand-ins with the right NAMES and LAYOUT.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ValidateSet('A', 'B')][string]$Generation,
    [ValidateSet('win64', 'win64-windows7')][string]$Variant = 'win64'
)

$ErrorActionPreference = 'Stop'

if (Test-Path -LiteralPath $Root) { Remove-Item -Recurse -Force -LiteralPath $Root }
New-Item -ItemType Directory -Force -Path $Root | Out-Null

function Put {
    param([string]$RelPath, [string]$Content)
    $full = Join-Path $Root $RelPath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $full) | Out-Null
    # LF, no BOM: byte-stable across generations so unchanged files hash equal.
    [System.IO.File]::WriteAllText($full, ($Content -replace "`r`n", "`n"),
        (New-Object System.Text.UTF8Encoding($false)))
}

$qtMajor = if ($Variant -eq 'win64') { '6' } else { '5' }

# --- Files that NEVER change between A and B (must not enter the delta) -----
Put 'platforms/qwindows.dll'                      'stable-platform-plugin'
Put "Qt${qtMajor}Gui.dll"                         'stable-qt-gui'
Put 'styles/qwindowsvistastyle.dll'               'stable-style'
Put 'imageformats/qjpeg.dll'                      'stable-imageformat'
Put 'resources/qtwebengine_devtools_resources.pak' 'stable-devtools'
Put 'translations/qtwebengine_locales/en-US.pak'  'stable-locale-en'
Put 'translations/qtwebengine_locales/zh-CN.pak'  'stable-locale-zh'
Put 'QtWebEngineProcess.exe'                      'stable-webengine-process'

# --- Files that DO change between A and B ----------------------------------
Put 'vnote.exe'                       "vnote-executable-generation-$Generation"
Put "Qt${qtMajor}Core.dll"            "qt-core-generation-$Generation"
Put 'resources/icudtl.dat'            "icu-generation-$Generation"
Put 'translations/vnote_zh_CN.qm'     "zh-translation-generation-$Generation"
Put 'translations/vnote_ja.qm'        "ja-translation-generation-$Generation"
Put 'vnote_extra.rcc'                 "extra-resources-generation-$Generation"

if ($Generation -eq 'A') {
    # Removed in B: exercises deletion AND a directory that becomes empty.
    Put 'obsolete/legacy_helper.dll'  'this-file-disappears-in-B'
    # Becomes a DIRECTORY in B: file -> directory transition.
    Put 'plugins'                     'in-A-this-path-is-a-FILE'
    # A file whose content is changed in B and reverted in C (three-hop test).
    Put 'flipflop.dll'                'flip-original'
}
else {
    # New in B: exercises Add plus a newly created directory.
    Put 'newfeature/brand_new.dll'    'this-file-is-new-in-B'
    # file -> directory transition.
    Put 'plugins/real_plugin.dll'     'in-B-plugins-is-a-DIRECTORY'
    Put 'flipflop.dll'                'flip-changed-in-B'
}

$count = (Get-ChildItem -Recurse -File -LiteralPath $Root | Measure-Object).Count
Write-Host "fixture $Generation ($Variant): $count files under $Root"
