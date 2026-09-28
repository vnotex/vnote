# Windows PowerShell 5.1. The deployed updater uses only its bundled verifier.
# Dot-sourcing defines the updater functions; it never starts an update.
[CmdletBinding()]
param(
    [string]$Source,
    [string]$Version,
    [string]$CurrentVersion,
    [string]$Variant,
    [string]$InstallDir,
    [int]$ParentProcessId,
    [string]$PipeName,
    [string]$Token
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$script:VNoteOrigins = @{
    github = @{
        ApiBaseUrl = 'https://api.github.com/repos/vnotex/vnote/releases/tags/'
        Scheme = 'https'
        Port = 443
        Hosts = @('api.github.com', 'github.com', 'codeload.github.com')
        HostSuffixes = @('.githubusercontent.com')
        Accept = 'application/vnd.github+json, */*'
    }
    gitee = @{
        ApiBaseUrl = 'https://gitee.com/api/v5/repos/vnotex/vnote/releases/tags/'
        Scheme = 'https'
        Port = 443
        Hosts = @('gitee.com')
        HostSuffixes = @('.gitee.com')
        Accept = 'application/json, */*'
    }
}
$script:VNoteTrustedKeys = @(
    'RWTozVZS1n5PM5euO7/ieR6o6daenLdTCK0EIhYnf0ACb47j6usoRtnJ',
    'RWRmwoKaT9dqtSFII74FcPaet3Ork43BpcJx/SOnuiX3JR6wG9864WjO'
)
$script:VNoteStageRoot = $null
$script:VNoteInstallRoot = $null
$script:VNotePreparationGuard = $null
# Tests may change these constants in a dot-sourced scope, never via CLI or environment.
$script:VNoteRequestTimeoutMilliseconds = 30000
$script:VNoteReadTimeoutMilliseconds = 30000
$script:VNoteMetadataDeadlineMilliseconds = 30000
$script:VNoteZipDeadlineMilliseconds = 1800000

function Get-VNoteProperty {
    param(
        [Parameter(Mandatory = $true)][AllowNull()]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if ($null -eq $Object -or $Object -isnot [System.Management.Automation.PSCustomObject]) {
        throw "Expected a JSON object containing '$Name'."
    }
    foreach ($property in $Object.PSObject.Properties) {
        if ($property.Name -ceq $Name) {
            # Keep JSON arrays as arrays, including arrays with a single element.
            return ,$property.Value
        }
    }
    throw "Required JSON property '$Name' is missing."
}

function Assert-VNoteInteger {
    param(
        [Parameter(Mandatory = $true)][AllowNull()]$Value,
        [Parameter(Mandatory = $true)][string]$Name,
        [int64]$Minimum = 0,
        [int64]$Maximum = [int64]::MaxValue
    )

    $isInteger = $Value -is [byte] -or $Value -is [sbyte] -or
        $Value -is [int16] -or $Value -is [uint16] -or
        $Value -is [int32] -or $Value -is [uint32] -or
        $Value -is [int64] -or $Value -is [uint64]
    if (-not $isInteger -or [decimal]$Value -lt [decimal]$Minimum -or
        [decimal]$Value -gt [decimal]$Maximum) {
        throw "'$Name' must be an integer between $Minimum and $Maximum."
    }
}

function Assert-VNoteVersion {
    param([Parameter(Mandatory = $true)][AllowNull()]$Version)

    if ($Version -isnot [string] -or $Version -cnotmatch '\A[0-9]+\.[0-9]+\.[0-9]+\z') {
        throw "Automatic update requires a numeric stable version (major.minor.patch). Use Check Release to update manually."
    }
}

function Compare-VNoteVersion {
    param(
        [Parameter(Mandatory = $true)]$Left,
        [Parameter(Mandatory = $true)]$Right
    )

    Assert-VNoteVersion -Version $Left
    Assert-VNoteVersion -Version $Right
    $leftParts = $Left.Split('.')
    $rightParts = $Right.Split('.')
    for ($index = 0; $index -lt 3; ++$index) {
        # Compare arbitrary-length decimal components without Int32/Version overflow.
        $leftPart = $leftParts[$index].TrimStart([char]'0')
        $rightPart = $rightParts[$index].TrimStart([char]'0')
        if ($leftPart.Length -lt $rightPart.Length) { return -1 }
        if ($leftPart.Length -gt $rightPart.Length) { return 1 }
        $comparison = [string]::CompareOrdinal($leftPart, $rightPart)
        if ($comparison -lt 0) { return -1 }
        if ($comparison -gt 0) { return 1 }
    }
    return 0
}

function Get-VNoteUri {
    param([Parameter(Mandatory = $true)][AllowNull()]$Url)

    $uri = $null
    if ($Url -isnot [string] -or [string]::IsNullOrWhiteSpace($Url) -or
        $Url -match '[\s\\]' -or $Url -notmatch '\A[a-zA-Z][a-zA-Z0-9+.-]*://' -or
        -not [Uri]::TryCreate($Url, [UriKind]::Absolute, [ref]$uri) -or
        -not $uri.IsWellFormedOriginalString() -or [string]::IsNullOrEmpty($uri.Host)) {
        throw 'The release contains an empty or malformed download URL.'
    }
    if ($uri.UserInfo.Length -ne 0 -or $Url -match '\A[^:]+://[^/?#]*@' -or
        $uri.Fragment.Length -ne 0) {
        throw 'Update URLs must not contain user information or fragments.'
    }
    return $uri
}

function Assert-VNoteProductionUrl {
    param(
        [Parameter(Mandatory = $true)]$Url,
        [Parameter(Mandatory = $true)][ValidateSet('github', 'gitee')][string]$Source
    )

    $uri = Get-VNoteUri -Url $Url
    if ($uri.Scheme -cne 'https' -or $uri.Port -ne 443) {
        throw 'Update downloads require HTTPS on port 443.'
    }
    $hostName = $uri.DnsSafeHost.ToLowerInvariant()
    $allowed = if ($Source -eq 'github') {
        $hostName -in @('api.github.com', 'github.com', 'codeload.github.com') -or
            $hostName.EndsWith('.githubusercontent.com', [StringComparison]::Ordinal)
    }
    else {
        $hostName -eq 'gitee.com' -or $hostName.EndsWith('.gitee.com', [StringComparison]::Ordinal)
    }
    if (-not $allowed) {
        throw "Refusing a download outside the configured $Source source: $hostName"
    }
    return $uri
}

function Assert-VNoteUrl {
    param(
        [Parameter(Mandatory = $true)]$Url,
        [Parameter(Mandatory = $true)][ValidateSet('github', 'gitee')][string]$Source
    )

    $uri = Get-VNoteUri -Url $Url
    $origin = $script:VNoteOrigins[$Source]
    $hostName = $uri.DnsSafeHost.ToLowerInvariant()
    $exactHost = $false
    foreach ($allowedHost in $origin.Hosts) {
        if ([string]::Equals($hostName, $allowedHost, [StringComparison]::OrdinalIgnoreCase)) {
            $exactHost = $true
            break
        }
    }
    $allowedHostName = $exactHost
    foreach ($suffix in $origin.HostSuffixes) {
        if ($hostName.EndsWith($suffix, [StringComparison]::OrdinalIgnoreCase)) {
            $allowedHostName = $true
            break
        }
    }
    if ($uri.Scheme -cne $origin.Scheme -or $uri.Port -ne $origin.Port -or -not $allowedHostName) {
        throw "Refusing an unexpected URL origin for the configured $Source source."
    }
    # Only a dot-sourced fixture table can nominate an exact loopback origin.
    # Changing the table can never weaken production remote-host/TLS policy.
    if (-not ($uri.IsLoopback -and $exactHost -and $uri.Scheme -in @('http', 'https'))) {
        $null = Assert-VNoteProductionUrl -Url $Url -Source $Source
    }
    return $uri
}

function Wait-VNoteTask {
    param(
        [Parameter(Mandatory = $true)][Threading.Tasks.Task]$Task,
        [Parameter(Mandatory = $true)][int]$TimeoutMilliseconds
    )
    try { return $Task.Wait($TimeoutMilliseconds) }
    catch {
        # Report the real IO failure rather than AggregateException's wrapper.
        if ($Task.IsCompleted) { $null = $Task.GetAwaiter().GetResult() }
        throw
    }
}

function Receive-VNoteFile {
    param(
        [Parameter(Mandatory = $true)]$Url,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][ValidateSet('github', 'gitee')][string]$Source,
        [Parameter(Mandatory = $true)]$MaxBytes
    )

    Assert-VNoteInteger -Value $MaxBytes -Name 'download byte limit' -Minimum 1 -Maximum 4294967296
    $uri = Assert-VNoteUrl -Url $Url -Source $Source
    if ([string]::IsNullOrWhiteSpace($script:VNoteStageRoot) -or
        -not [IO.Path]::IsPathRooted($script:VNoteStageRoot) -or
        -not [IO.Path]::IsPathRooted($Destination)) {
        throw 'Download staging paths must be absolute paths in the private attempt directory.'
    }
    $stageRoot = [IO.Path]::GetFullPath($script:VNoteStageRoot).TrimEnd([char[]]@('\', '/'))
    $destinationPath = [IO.Path]::GetFullPath($Destination)
    $stagePrefix = $stageRoot + [IO.Path]::DirectorySeparatorChar
    if ($stageRoot.Length -le [IO.Path]::GetPathRoot($stageRoot).Length) {
        throw 'The private attempt directory must not be a filesystem root.'
    }
    if (-not $destinationPath.StartsWith($stagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to write a download outside the private attempt directory.'
    }
    Assert-VNoteSafePath -Path ($destinationPath.Substring($stagePrefix.Length).Replace('\', '/'))
    Assert-VNoteNoReparse -Path $stageRoot
    Assert-VNoteNoReparse -Path $destinationPath
    if (-not [IO.Directory]::Exists($stageRoot) -or
        -not [IO.Directory]::Exists([IO.Path]::GetDirectoryName($destinationPath))) {
        throw 'The private download directory does not exist.'
    }

    $isZip = $uri.AbsolutePath.EndsWith('.zip', [StringComparison]::OrdinalIgnoreCase) -or
        $destinationPath.EndsWith('.zip', [StringComparison]::OrdinalIgnoreCase)
    $isSignature = $uri.AbsolutePath.EndsWith('.minisig', [StringComparison]::OrdinalIgnoreCase) -or
        $destinationPath.EndsWith('.minisig', [StringComparison]::OrdinalIgnoreCase)
    $limit = [int64]$MaxBytes
    $deadline = $script:VNoteZipDeadlineMilliseconds
    if (-not $isZip) {
        $limit = [Math]::Min($limit, [int64](16 * 1024 * 1024))
        if ($isSignature) { $limit = [Math]::Min($limit, [int64](64 * 1024)) }
        $deadline = $script:VNoteMetadataDeadlineMilliseconds
    }
    [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $request = $null
    $response = $null
    $inputStream = $null
    $outputStream = $null
    $created = $false
    $complete = $false
    try {
        if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
        # CreateNew is important: a failed request must never remove someone else's file.
        $outputStream = [IO.File]::Open($destinationPath, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::None)
        $created = $true
        Write-Host ("Downloading {0} from {1}..." -f [IO.Path]::GetFileName($destinationPath), $uri.Host)
        for ($hop = 0; $hop -le 5; ++$hop) {
            if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
            $uri = Assert-VNoteUrl -Url $uri.AbsoluteUri -Source $Source
            $remaining = [int64]$deadline - $clock.ElapsedMilliseconds
            if ($remaining -le 0) { throw 'The update download exceeded its overall deadline.' }
            $timeout = [int][Math]::Min($remaining, $script:VNoteRequestTimeoutMilliseconds)
            $request = [Net.HttpWebRequest]::Create($uri)
            $request.Method = 'GET'
            $request.AllowAutoRedirect = $false
            $request.UserAgent = 'VNote-Updater'
            $request.Accept = $script:VNoteOrigins[$Source].Accept
            $request.Timeout = $timeout
            $request.ReadWriteTimeout = $script:VNoteReadTimeoutMilliseconds
            $request.AutomaticDecompression = [Net.DecompressionMethods]::None
            $request.CachePolicy = New-Object System.Net.Cache.RequestCachePolicy ([Net.Cache.RequestCacheLevel]::NoCacheNoStore)
            $pendingResponse = $request.GetResponseAsync()
            if (-not (Wait-VNoteTask -Task $pendingResponse -TimeoutMilliseconds $timeout)) {
                $request.Abort()
                throw 'The update request timed out before response headers arrived.'
            }
            $response = [Net.HttpWebResponse]($pendingResponse.GetAwaiter().GetResult())

            $status = [int]$response.StatusCode
            if ($status -in @(301, 302, 303, 307, 308)) {
                if ($hop -eq 5) { throw 'The update download exceeded five redirects.' }
                $location = $response.Headers['Location']
                if ([string]::IsNullOrWhiteSpace($location) -or $location -match '[\s\\]' -or
                    $location -match '\A(?:[^:]+:)?//[^/?#]*@') {
                    throw 'The update server returned an empty or malformed redirect.'
                }
                $nextUri = $null
                if (-not [Uri]::TryCreate($uri, $location, [ref]$nextUri)) {
                    throw 'The update server returned a malformed redirect.'
                }
                # Resolve relative locations without removing signed query parameters.
                $uri = Assert-VNoteUrl -Url $nextUri.AbsoluteUri -Source $Source
                $response.Close()
                $response = $null
                continue
            }
            if ($status -ne 200) { throw "The update server returned unexpected HTTP status $status." }
            if ($response.ContentLength -gt $limit -or
                ($isZip -and $response.ContentLength -ge 0 -and $response.ContentLength -ne $limit)) {
                throw 'The download length does not match its permitted or signed byte count.'
            }
            $inputStream = $response.GetResponseStream()
            $buffer = New-Object byte[] 65536
            $received = [int64]0
            $lastProgress = [int64]-1000
            while ($true) {
                if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
                $remaining = [int64]$deadline - $clock.ElapsedMilliseconds
                if ($remaining -le 0) { throw 'The update download exceeded its overall deadline.' }
                $timeout = [int][Math]::Min($remaining, $script:VNoteReadTimeoutMilliseconds)
                # Async waits enforce an overall deadline even for a slow trickle stream.
                $pendingRead = $inputStream.ReadAsync($buffer, 0, $buffer.Length)
                if (-not (Wait-VNoteTask -Task $pendingRead -TimeoutMilliseconds $timeout)) {
                    $request.Abort()
                    throw 'The update download stalled or exceeded its overall deadline.'
                }
                $count = $pendingRead.GetAwaiter().GetResult()
                if ($clock.ElapsedMilliseconds -ge $deadline) {
                    throw 'The update download exceeded its overall deadline.'
                }
                if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
                if ($count -eq 0) { break }
                if ($count -gt $limit - $received) {
                    $request.Abort()
                    throw 'The update download exceeded its permitted or signed byte count.'
                }
                $outputStream.Write($buffer, 0, $count)
                $received += $count
                if ($clock.ElapsedMilliseconds - $lastProgress -ge 250) {
                    $lastProgress = $clock.ElapsedMilliseconds
                    Write-Progress -Id 1 -Activity 'Downloading VNote update' -Status "$received bytes received" `
                        -PercentComplete ([int][Math]::Min(100, 100.0 * $received / $limit))
                }
            }
            if (($isZip -and $received -ne $limit) -or
                ($response.ContentLength -ge 0 -and $received -ne $response.ContentLength)) {
                throw 'The update download ended before the expected byte count was received.'
            }
            $outputStream.Flush()
            $complete = $true
            return
        }
        throw 'The update download exceeded five redirects.'
    }
    finally {
        if ($null -ne $inputStream) { $inputStream.Dispose() }
        if ($null -ne $response) { $response.Close() }
        if ($null -ne $request) { $request.Abort() }
        if ($null -ne $outputStream) { $outputStream.Dispose() }
        $clock.Stop()
        Write-Progress -Id 1 -Activity 'Downloading VNote update' -Completed
        if ($created -and -not $complete) { [IO.File]::Delete($destinationPath) }
    }
}

function Assert-VNoteRelease {
    param(
        [Parameter(Mandatory = $true)]$Release,
        [Parameter(Mandatory = $true)]$Version,
        [string]$Source = ''
    )

    Assert-VNoteVersion -Version $Version
    $tag = Get-VNoteProperty -Object $Release -Name 'tag_name'
    if ($tag -isnot [string]) { throw 'The release tag is not a string.' }
    if ($tag.StartsWith('v', [StringComparison]::Ordinal)) { $tag = $tag.Substring(1) }
    Assert-VNoteVersion -Version $tag
    if ($tag -cne $Version) { throw 'The release API returned a different version than the offered update.' }
    $prerelease = Get-VNoteProperty -Object $Release -Name 'prerelease'
    if ($prerelease -isnot [bool] -or $prerelease) { throw 'Prerelease or malformed releases cannot be installed automatically.' }
    $draftProperty = $null
    foreach ($property in $Release.PSObject.Properties) {
        if ($property.Name -ceq 'draft') { $draftProperty = $property; break }
    }
    # Gitee does not expose a draft field; GitHub must explicitly report false.
    if ($null -eq $draftProperty) {
        if ($Source -eq 'github') { throw "The GitHub release is missing its 'draft' flag." }
    }
    elseif ($draftProperty.Value -isnot [bool] -or $draftProperty.Value) {
        throw 'Draft or malformed releases cannot be installed automatically.'
    }
    $assets = Get-VNoteProperty -Object $Release -Name 'assets'
    if ($assets -isnot [array] -or $assets.Count -eq 0) { throw 'The release has no asset array.' }
}

function Get-VNoteRelease {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('github', 'gitee')][string]$Source,
        [Parameter(Mandatory = $true)]$Version
    )

    Assert-VNoteVersion -Version $Version
    if ([string]::IsNullOrWhiteSpace($script:VNoteStageRoot)) { throw 'The private attempt directory has not been initialized.' }
    $apiUrl = $script:VNoteOrigins[$Source].ApiBaseUrl + 'v' + $Version
    $apiPath = Join-Path $script:VNoteStageRoot ('release-' + [Guid]::NewGuid().ToString('N') + '.json')
    $downloaded = $false
    try {
        Receive-VNoteFile -Url $apiUrl -Destination $apiPath -Source $Source -MaxBytes (16 * 1024 * 1024)
        $downloaded = $true
        $release = ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($apiPath, [Text.Encoding]::UTF8)) -ErrorAction Stop
        Assert-VNoteRelease -Release $release -Version $Version -Source $Source
        # Validate source policy for the only records asset selection may consume.
        $names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::Ordinal)
        foreach ($releaseVariant in @('win64', 'win64-windows7')) {
            $baseName = "VNote-$Version-$releaseVariant"
            $null = $names.Add("$baseName.zip")
            $null = $names.Add("$baseName.manifest.json")
            $null = $names.Add("$baseName.manifest.json.minisig")
        }
        foreach ($asset in $release.assets) {
            $name = Get-VNoteProperty -Object $asset -Name 'name'
            if ($name -isnot [string]) { throw 'A release asset name is not a string.' }
            if ($names.Contains($name)) {
                $null = Assert-VNoteUrl -Url (Get-VNoteProperty -Object $asset -Name 'browser_download_url') -Source $Source
            }
        }
        return $release
    }
    finally {
        if ($downloaded) { [IO.File]::Delete($apiPath) }
    }
}

function Select-VNoteReleaseAssets {
    param(
        [Parameter(Mandatory = $true)]$Release,
        [Parameter(Mandatory = $true)]$Version,
        [Parameter(Mandatory = $true)][ValidateSet('win64', 'win64-windows7')][string]$Variant
    )

    Assert-VNoteRelease -Release $Release -Version $Version
    $baseName = "VNote-$Version-$Variant"
    $required = @{
        Zip = "$baseName.zip"
        Manifest = "$baseName.manifest.json"
        Signature = "$baseName.manifest.json.minisig"
    }
    $selected = @{}
    foreach ($kind in $required.Keys) {
        $match = $null
        foreach ($asset in $Release.assets) {
            $name = Get-VNoteProperty -Object $asset -Name 'name'
            if ($name -isnot [string]) { throw 'A release asset name is not a string.' }
            if ($name -ceq $required[$kind]) {
                if ($null -ne $match) { throw "The release has duplicate '$($required[$kind])' assets." }
                $match = $asset
            }
        }
        if ($null -eq $match) {
            throw "The configured source is missing '$($required[$kind])'. Use Check Release to update manually."
        }
        $url = Get-VNoteProperty -Object $match -Name 'browser_download_url'
        $null = Get-VNoteUri -Url $url
        # Get-VNoteRelease and Receive-VNoteFile additionally enforce the selected source.
        $permitted = $false
        foreach ($candidateSource in @('github', 'gitee')) {
            try {
                $null = Assert-VNoteUrl -Url $url -Source $candidateSource
                $permitted = $true
                break
            }
            catch { }
        }
        if (-not $permitted) { throw "The '$($required[$kind])' asset has an untrusted download URL." }
        $selected[$kind] = $url
    }
    return [PSCustomObject]@{
        Zip = $selected.Zip
        Manifest = $selected.Manifest
        Signature = $selected.Signature
        ZipName = $required.Zip
    }
}

function Assert-VNoteManifestFiles {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)]$Version,
        [Parameter(Mandatory = $true)][ValidateSet('win64', 'win64-windows7')][string]$Variant
    )

    Assert-VNoteVersion -Version $Version
    $schema = Get-VNoteProperty -Object $Manifest -Name 'schema'
    Assert-VNoteInteger -Value $schema -Name 'schema' -Minimum 1 -Maximum 1
    $identity = @{
        product = 'VNote'
        channel = 'stable'
        platform = 'windows-x64'
        version = $Version
        variant = $Variant
    }
    foreach ($name in $identity.Keys) {
        $value = Get-VNoteProperty -Object $Manifest -Name $name
        if ($value -isnot [string] -or $value -cne $identity[$name]) {
            throw "The signed manifest has an unexpected '$name' value."
        }
    }
    foreach ($name in @('commit', 'generatedAt')) {
        $value = Get-VNoteProperty -Object $Manifest -Name $name
        if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value)) {
            throw "The manifest has an invalid '$name' value."
        }
    }
    $files = Get-VNoteProperty -Object $Manifest -Name 'files'
    if ($files -isnot [array] -or $files.Count -eq 0) { throw 'The signed manifest must contain a nonempty files array.' }
    $paths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $totalSize = [int64]0
    $oppositeQt = if ($Variant -eq 'win64') { '(^|/)Qt5[^/]*\.dll$' } else { '(^|/)Qt6[^/]*\.dll$' }
    foreach ($file in $files) {
        $path = Get-VNoteProperty -Object $file -Name 'path'
        if ($path -isnot [string]) { throw 'A signed manifest path is not a string.' }
        Assert-VNoteSafePath -Path $path
        if ($path -ieq 'manifest.json' -or $path.StartsWith('manifest.json/', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The manifest file must not describe or replace itself.'
        }
        if (-not $paths.Add($path)) { throw "The manifest contains a case-insensitive duplicate path: $path" }
        if ($path -match $oppositeQt) { throw "The manifest contains a DLL for the wrong Qt major: $path" }
        $size = Get-VNoteProperty -Object $file -Name 'size'
        Assert-VNoteInteger -Value $size -Name "size of $path"
        if ([int64]$size -gt [int64]::MaxValue - $totalSize) { throw 'The signed extraction size overflows the supported bound.' }
        $totalSize += [int64]$size
        $hash = Get-VNoteProperty -Object $file -Name 'sha256'
        if ($hash -isnot [string] -or $hash -cnotmatch '\A[0-9a-fA-F]{64}\z') {
            throw "The manifest has an invalid SHA-256 for '$path'."
        }
    }
    foreach ($path in $paths) {
        $separator = $path.IndexOf('/')
        while ($separator -ge 0) {
            if ($paths.Contains($path.Substring(0, $separator))) {
                throw "The manifest has a file/directory prefix conflict: $path"
            }
            $separator = $path.IndexOf('/', $separator + 1)
        }
    }
    $qtCore = if ($Variant -eq 'win64') { 'Qt6Core.dll' } else { 'Qt5Core.dll' }
    if (-not $paths.Contains('vnote.exe') -or -not $paths.Contains($qtCore)) {
        throw "The manifest must declare root vnote.exe and $qtCore."
    }
}

function Assert-VNoteManifest {
    param(
        [Parameter(Mandatory = $true)]$Manifest,
        [Parameter(Mandatory = $true)]$Version,
        [Parameter(Mandatory = $true)]$CurrentVersion,
        [Parameter(Mandatory = $true)][ValidateSet('win64', 'win64-windows7')][string]$Variant
    )

    Assert-VNoteVersion -Version $CurrentVersion
    if ((Compare-VNoteVersion -Left $Version -Right $CurrentVersion) -le 0) {
        throw 'The offered update is not newer than the running VNote version. Use Check Release to update manually.'
    }
    Assert-VNoteManifestFiles -Manifest $Manifest -Version $Version -Variant $Variant
    $package = Get-VNoteProperty -Object $Manifest -Name 'fullPackage'
    $asset = Get-VNoteProperty -Object $package -Name 'asset'
    if ($asset -isnot [string] -or $asset -cne "VNote-$Version-$Variant.zip") {
        throw 'The signed full-package asset name does not match the selected update ZIP.'
    }
    Assert-VNoteInteger -Value (Get-VNoteProperty -Object $package -Name 'size') `
        -Name 'fullPackage.size' -Minimum 1 -Maximum 4294967296
    $hash = Get-VNoteProperty -Object $package -Name 'sha256'
    if ($hash -isnot [string] -or $hash -cnotmatch '\A[0-9a-fA-F]{64}\z') {
        throw 'The signed full package has an invalid SHA-256.'
    }
    # Delta metadata is intentionally neither consumed nor used as a fallback.
    return $Manifest
}

function Read-VerifiedVNoteManifest {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$SignaturePath,
        [Parameter(Mandatory = $true)][string]$MinisignPath,
        [Parameter(Mandatory = $true)]$Version,
        [Parameter(Mandatory = $true)]$CurrentVersion,
        [Parameter(Mandatory = $true)][ValidateSet('win64', 'win64-windows7')][string]$Variant,
        [Parameter(Mandatory = $true)][string[]]$TrustedKeys
    )

    Assert-VNoteVersion -Version $Version
    Assert-VNoteVersion -Version $CurrentVersion
    if ((Compare-VNoteVersion -Left $Version -Right $CurrentVersion) -le 0) {
        throw 'The offered update is not newer than the running VNote version. Use Check Release to update manually.'
    }
    if ($TrustedKeys.Count -eq 0) { throw 'No trusted VNote signing keys are available.' }
    foreach ($filePath in @($Path, $SignaturePath, $MinisignPath)) {
        if (-not [IO.Path]::IsPathRooted($filePath) -or -not [IO.File]::Exists($filePath)) {
            throw "Automatic update is unavailable. Use Check Release to update manually. Missing required file: $filePath"
        }
        Assert-VNoteNoReparse -Path $filePath
    }
    if ([IO.Path]::GetFileName($MinisignPath) -ine 'minisign.exe') {
        throw 'Automatic update is unavailable. Use Check Release to update manually. The bundled minisign.exe verifier is required.'
    }
    if ([string]::IsNullOrWhiteSpace($script:VNoteInstallRoot) -or
        -not [IO.Path]::IsPathRooted($script:VNoteInstallRoot) -or
        -not [IO.Directory]::Exists($script:VNoteInstallRoot)) {
        throw 'Automatic update is unavailable. Use Check Release to update manually. The installation root for bundled verifier dependencies is missing.'
    }
    Assert-VNoteNoReparse -Path $script:VNoteInstallRoot
    $manifestStream = $null
    $signatureStream = $null
    $reader = $null
    $savedPath = $env:PATH
    $savedPreference = $ErrorActionPreference
    try {
        # Keep these exact bytes immutable across verification and JSON parsing.
        $manifestStream = [IO.File]::Open([IO.Path]::GetFullPath($Path), [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $signatureStream = [IO.File]::Open([IO.Path]::GetFullPath($SignaturePath), [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::Read)
        if ($manifestStream.Length -le 0 -or $manifestStream.Length -gt 16 * 1024 * 1024 -or
            $signatureStream.Length -le 0 -or $signatureStream.Length -gt 64 * 1024) {
            throw 'The release manifest or signature is empty or exceeds its permitted byte limit.'
        }
        $env:PATH = [IO.Path]::GetFullPath($script:VNoteInstallRoot) + ';' + $savedPath
        $verified = $false
        Write-Host 'Authenticating the signed release manifest...'
        foreach ($key in $TrustedKeys) {
            if ([string]::IsNullOrWhiteSpace($key)) { throw 'A trusted VNote signing key is empty.' }
            if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
            $arguments = @('-V', '-P', $key, '-m', [IO.Path]::GetFullPath($Path), '-x', [IO.Path]::GetFullPath($SignaturePath))
            try {
                # Windows PowerShell turns redirected native stderr into ErrorRecords.
                # A key-rotation mismatch is expected; it must not prevent trying key two.
                $ErrorActionPreference = 'Continue'
                $global:LASTEXITCODE = -1
                $null = & ([IO.Path]::GetFullPath($MinisignPath)) @arguments 2>&1
                $nativeExitCode = $global:LASTEXITCODE
            }
            finally { $ErrorActionPreference = $savedPreference }
            if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
            if ($nativeExitCode -eq 0) {
                # -V verifies both the message and the trusted-comment signature.
                $verified = $true
                break
            }
        }
        if (-not $verified) {
            throw 'The release manifest signature is invalid or was not signed by a trusted VNote key. Nothing will be installed.'
        }
        $manifestStream.Position = 0
        $reader = New-Object IO.StreamReader ($manifestStream, [Text.Encoding]::UTF8, $true)
        $manifest = ConvertFrom-Json -InputObject ($reader.ReadToEnd()) -ErrorAction Stop
        return (Assert-VNoteManifest -Manifest $manifest -Version $Version -CurrentVersion $CurrentVersion -Variant $Variant)
    }
    finally {
        $ErrorActionPreference = $savedPreference
        $env:PATH = $savedPath
        if ($null -ne $reader) { $reader.Dispose() }
        if ($null -ne $signatureStream) { $signatureStream.Dispose() }
        if ($null -ne $manifestStream) { $manifestStream.Dispose() }
    }
}

function Assert-VNoteSafePath {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'Empty path in package.' }
    if ($Path.StartsWith('/') -or $Path.Contains('\')) {
        throw "Absolute or backslash path in package: $Path"
    }
    foreach ($segment in $Path.Split('/')) {
        if ($segment -eq '' -or $segment -eq '.' -or $segment -eq '..') {
            throw "Empty or relative path segment in package: $Path"
        }
        if ($segment.EndsWith('.') -or $segment.EndsWith(' ') -or $segment.StartsWith(' ')) {
            throw "Path segment aliases another Windows name: $Path"
        }
        if ($segment -match '[<>:"|?*\x00-\x1f]') {
            throw "Path segment contains a character Windows forbids: $Path"
        }
        $stem = ($segment -split '\.', 2)[0].TrimEnd(' ').ToUpperInvariant()
        if ($stem -match '^(CON|PRN|AUX|NUL|CLOCK\$|CONIN\$|CONOUT\$|(COM|LPT)[1-9\u00b9\u00b2\u00b3])$') {
            throw "Reserved Windows device name in package: $Path"
        }
    }
    $rootComponent = $Path.Split('/')[0]
    if ($rootComponent -iin @('config', '.vnote-update', '.vnote-old')) {
        throw "Package targets portable data or a reserved updater directory: $Path"
    }
}

function Assert-VNoteNoReparse {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath.StartsWith('\\?\') -or $fullPath.StartsWith('\\.\')) {
        throw "Device-namespace paths are not supported by the updater: $Path"
    }
    $root = [System.IO.Path]::GetPathRoot($fullPath)
    if ($fullPath.Length -gt $root.Length) { $fullPath = $fullPath.TrimEnd([char[]]'\/') }
    $ancestors = [System.Collections.Generic.List[string]]::new()
    $current = $fullPath
    while ($null -ne $current) {
        $ancestors.Add($current)
        if ([string]::Equals($current, $root, [System.StringComparison]::OrdinalIgnoreCase)) { break }
        $parent = [System.IO.Directory]::GetParent($current)
        if ($null -eq $parent) { break }
        $current = $parent.FullName
    }
    # Check from the volume root down, never looking through an unchecked ancestor.
    for ($index = $ancestors.Count - 1; $index -ge 0; --$index) {
        try {
            $attributes = [System.IO.File]::GetAttributes($ancestors[$index])
        } catch [System.IO.FileNotFoundException] {
            continue
        } catch [System.IO.DirectoryNotFoundException] {
            continue
        }
        if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Reparse points are not supported by the updater: $($ancestors[$index])"
        }
    }
}

function Get-VNoteTree {
    param([Parameter(Mandatory = $true)][string]$Root)

    $rootPath = [System.IO.Path]::GetFullPath($Root)
    Assert-VNoteNoReparse -Path $rootPath
    if (-not [System.IO.Directory]::Exists($rootPath)) {
        throw "The directory does not exist: $rootPath"
    }
    $pending = [System.Collections.Generic.Stack[System.IO.DirectoryInfo]]::new()
    $pending.Push([System.IO.DirectoryInfo]::new($rootPath))
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        Assert-VNoteNoReparse -Path $directory.FullName
        # The .NET enumerator includes hidden/system entries and does not recurse.
        foreach ($child in $directory.EnumerateFileSystemInfos()) {
            $child.Refresh()
            if (($child.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse points are not supported by the updater: $($child.FullName)"
            }
            if (-not $child.Exists) { throw "The directory changed while being read: $($child.FullName)" }
            $child
            if (($child.Attributes -band [System.IO.FileAttributes]::Directory) -ne 0) {
                $pending.Push([System.IO.DirectoryInfo]$child)
            }
        }
    }
}

function Expand-VerifiedVNotePackage {
    param(
        [Parameter(Mandatory = $true)][string]$ArchivePath,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)]$Manifest
    )

    if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
    $version = Get-VNoteProperty -Object $Manifest -Name 'version'
    $variant = Get-VNoteProperty -Object $Manifest -Name 'variant'
    $null = Assert-VNoteManifestFiles -Manifest $Manifest -Version $version -Variant $variant
    $fullPackage = Get-VNoteProperty -Object $Manifest -Name 'fullPackage'
    $asset = Get-VNoteProperty -Object $fullPackage -Name 'asset'
    $archiveSize = Get-VNoteProperty -Object $fullPackage -Name 'size'
    $archiveHash = Get-VNoteProperty -Object $fullPackage -Name 'sha256'
    $null = Assert-VNoteInteger -Value $archiveSize -Name 'fullPackage.size' -Minimum 1 -Maximum 4294967296
    if ($asset -isnot [string] -or $asset -cne "VNote-$version-$variant.zip") {
        throw 'The signed full-package asset does not match the requested package.'
    }
    if ($archiveHash -isnot [string] -or $archiveHash -notmatch '\A[0-9a-fA-F]{64}\z') {
        throw 'The signed full-package SHA-256 is invalid.'
    }
    $archiveSize = [long]$archiveSize
    $archiveFullPath = [System.IO.Path]::GetFullPath($ArchivePath)
    $destinationFullPath = [System.IO.Path]::GetFullPath($Destination).TrimEnd([char[]]'\/')
    if ([string]::IsNullOrWhiteSpace($script:VNoteStageRoot)) {
        throw 'A private attempt staging directory is required before extraction.'
    }
    $stagePath = [System.IO.Path]::GetFullPath($script:VNoteStageRoot).TrimEnd([char[]]'\/')
    $stagePrefix = $stagePath + [System.IO.Path]::DirectorySeparatorChar
    $destinationPrefix = $destinationFullPath + [System.IO.Path]::DirectorySeparatorChar
    if (-not $destinationFullPath.StartsWith($stagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'The extraction destination must be strictly inside the private attempt staging directory.'
    }
    Assert-VNoteNoReparse -Path $archiveFullPath
    Assert-VNoteNoReparse -Path $destinationFullPath
    if (-not [System.IO.Directory]::Exists($stagePath) -or
        -not [System.IO.Directory]::Exists($destinationFullPath)) {
        throw 'The private extraction destination must already exist.'
    }
    if ([System.IO.Directory]::GetFileSystemEntries($destinationFullPath).Length -ne 0) {
        throw 'The extraction destination must be empty.'
    }

    $signedFiles = Get-VNoteProperty -Object $Manifest -Name 'files'
    $declaredFiles = [System.Collections.Generic.Dictionary[string, object]]::new([System.StringComparer]::OrdinalIgnoreCase)
    [long]$signedPayloadSize = 0
    foreach ($file in $signedFiles) {
        [long]$size = $file.size
        if ($size -gt [long]::MaxValue - $signedPayloadSize) {
            throw 'The signed file sizes exceed the supported extraction bound.'
        }
        $signedPayloadSize += $size
        $declaredFiles.Add($file.path, $file)
    }

    Add-Type -AssemblyName System.IO.Compression -ErrorAction Stop
    $archiveStream = $null
    $zip = $null
    $inPackageBytes = $null
    $createdFiles = [System.Collections.Generic.List[string]]::new()
    $createdDirectories = [System.Collections.Generic.List[string]]::new()
    try {
        # Retain this handle through hashing and extraction: no writer or rename can
        # replace the ZIP between authentication and consumption.
        $archiveStream = [System.IO.FileStream]::new($archiveFullPath, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
        if ($archiveStream.Length -ne $archiveSize) { throw 'The ZIP length differs from its signed size.' }
        if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
        $actualArchiveHash = (Get-FileHash -InputStream $archiveStream -Algorithm SHA256 -ErrorAction Stop).Hash
        if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
        if ($actualArchiveHash -ine $archiveHash) { throw 'The ZIP SHA-256 differs from its signed hash.' }
        $archiveStream.Position = 0
        $zip = [System.IO.Compression.ZipArchive]::new($archiveStream, [System.IO.Compression.ZipArchiveMode]::Read, $true)
        $wrapper = "VNote-$version-$variant"
        $wrapperPrefix = $wrapper + '/'
        $seenPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
        $explicitDirectories = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
        $requiredDirectories = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
        $archiveFiles = [System.Collections.Generic.Dictionary[string, object]]::new([System.StringComparer]::OrdinalIgnoreCase)
        $entriesToExtract = [System.Collections.Generic.List[object]]::new()
        $inPackageEntry = $null
        [long]$listedPayloadSize = 0
        [long]$manifestLimit = 16777216

        # Only metadata is examined here. No destination contents exist until every
        # entry, every declared payload file, and the embedded manifest are valid.
        foreach ($entry in $zip.Entries) {
            if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
            $entryName = $entry.FullName
            $isDirectory = $entryName.EndsWith('/', [System.StringComparison]::Ordinal)
            $plainName = $entryName
            if ($isDirectory) { $plainName = $entryName.Substring(0, $entryName.Length - 1) }
            Assert-VNoteSafePath -Path $plainName
            [long]$attributes = [long]$entry.ExternalAttributes -band 4294967295
            $unixType = ($attributes -shr 16) -band 61440
            if (($attributes -band 1024) -ne 0 -or $unixType -notin @(0, 16384, 32768)) {
                throw "The ZIP contains a link, reparse point, or special file: $entryName"
            }
            if (($isDirectory -and ($unixType -eq 32768 -or $entry.Length -ne 0)) -or
                (-not $isDirectory -and ($unixType -eq 16384 -or ($attributes -band 16) -ne 0))) {
                throw "The ZIP has inconsistent file/directory metadata: $entryName"
            }
            if ($plainName -ceq $wrapper -and $isDirectory) {
                if (-not $seenPaths.Add('')) { throw "The ZIP repeats its wrapper directory: $entryName" }
                continue
            }
            if (-not $entryName.StartsWith($wrapperPrefix, [System.StringComparison]::Ordinal)) {
                throw "The ZIP entry is outside the exact expected wrapper '$wrapper': $entryName"
            }
            $relative = $plainName.Substring($wrapperPrefix.Length)
            Assert-VNoteSafePath -Path $relative
            if (-not $seenPaths.Add($relative)) { throw "The ZIP contains duplicate case-insensitive paths: $relative" }
            $targetPath = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($destinationFullPath,
                $relative.Replace([char]'/', [System.IO.Path]::DirectorySeparatorChar)))
            if (-not $targetPath.StartsWith($destinationPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "The ZIP entry escapes the extraction destination: $entryName"
            }
            if ($isDirectory) {
                [void]$explicitDirectories.Add($relative)
                continue
            }
            if ($relative -ieq 'manifest.json') {
                if ($entry.Length -le 0 -or $entry.Length -gt $manifestLimit) {
                    throw 'The in-package manifest exceeds its size bound or is empty.'
                }
                $inPackageEntry = $entry
                [long]$expectedSize = $entry.Length
            } else {
                if (-not $declaredFiles.ContainsKey($relative)) {
                    throw "The ZIP contains an undeclared payload file: $relative"
                }
                [long]$expectedSize = $declaredFiles[$relative].size
                if ($entry.Length -ne $expectedSize) { throw "The ZIP entry differs from its signed file size: $relative" }
                if ($expectedSize -gt $signedPayloadSize - $listedPayloadSize) {
                    throw 'The ZIP entries exceed the signed extraction bound.'
                }
                $listedPayloadSize += $expectedSize
            }
            $record = [pscustomobject]@{
                Entry = $entry
                Path = $relative
                Destination = $targetPath
                Size = $expectedSize
                IsManifest = ($relative -ieq 'manifest.json')
            }
            $archiveFiles.Add($relative, $record)
            $entriesToExtract.Add($record)
            $slash = $relative.LastIndexOf('/')
            while ($slash -gt 0) {
                [void]$requiredDirectories.Add($relative.Substring(0, $slash))
                $slash = $relative.LastIndexOf('/', $slash - 1)
            }
        }
        if ($null -eq $inPackageEntry) { throw 'The ZIP is missing its in-package manifest.json.' }
        foreach ($path in $declaredFiles.Keys) {
            if (-not $archiveFiles.ContainsKey($path)) { throw "The ZIP is missing a signed payload file: $path" }
        }
        if ($listedPayloadSize -ne $signedPayloadSize) { throw 'The ZIP does not contain the complete signed payload.' }
        foreach ($path in $requiredDirectories) {
            if ($archiveFiles.ContainsKey($path)) { throw "The ZIP has a file/directory prefix conflict: $path" }
        }
        foreach ($path in $explicitDirectories) {
            if (-not $requiredDirectories.Contains($path)) {
                throw "The ZIP contains a directory that is not a parent of a valid file: $path"
            }
        }

        $buffer = New-Object byte[] 65536
        $inPackageBytes = [System.IO.MemoryStream]::new()
        $manifestStream = $inPackageEntry.Open()
        try {
            while ($true) {
                if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
                $readCount = $manifestStream.Read($buffer, 0, $buffer.Length)
                if ($readCount -eq 0) { break }
                if ($readCount -gt $inPackageEntry.Length - $inPackageBytes.Length -or
                    $readCount -gt $manifestLimit - $inPackageBytes.Length) {
                    throw 'The in-package manifest stream exceeds its size bound.'
                }
                $inPackageBytes.Write($buffer, 0, $readCount)
            }
            if ($inPackageBytes.Length -ne $inPackageEntry.Length) { throw 'The in-package manifest stream is truncated.' }
        } finally {
            $manifestStream.Dispose()
        }
        if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
        $utf8 = [System.Text.UTF8Encoding]::new($false, $true)
        $manifestText = $utf8.GetString($inPackageBytes.GetBuffer(), 0, [int]$inPackageBytes.Length)
        if ($manifestText.Length -gt 0 -and $manifestText[0] -eq [char]0xfeff) {
            $manifestText = $manifestText.Substring(1)
        }
        $inPackageManifest = ConvertFrom-Json -InputObject $manifestText -ErrorAction Stop
        $null = Assert-VNoteManifestFiles -Manifest $inPackageManifest -Version $version -Variant $variant
        if ($inPackageManifest.PSObject.Properties.Name -icontains 'fullPackage' -or
            $inPackageManifest.PSObject.Properties.Name -icontains 'delta') {
            throw 'The in-package manifest must not contain release archive references.'
        }
        foreach ($name in @('schema', 'product', 'channel', 'version', 'variant', 'platform', 'commit', 'generatedAt')) {
            $expected = Get-VNoteProperty -Object $Manifest -Name $name
            $actual = Get-VNoteProperty -Object $inPackageManifest -Name $name
            if ($actual -cne $expected) { throw "The in-package manifest differs from the signed identity field: $name" }
        }
        $inPackageFiles = Get-VNoteProperty -Object $inPackageManifest -Name 'files'
        if ($inPackageFiles.Count -ne $declaredFiles.Count) { throw 'The in-package manifest file table differs from the signed table.' }
        foreach ($file in $inPackageFiles) {
            if (-not $declaredFiles.ContainsKey($file.path)) {
                throw "The in-package manifest declares an unsigned file: $($file.path)"
            }
            $signedFile = $declaredFiles[$file.path]
            if ([long]$file.size -ne [long]$signedFile.size -or $file.sha256 -ine $signedFile.sha256) {
                throw "The in-package manifest differs from the signed file table: $($file.path)"
            }
        }
        if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }

        Assert-VNoteNoReparse -Path $destinationFullPath
        if ([System.IO.Directory]::GetFileSystemEntries($destinationFullPath).Length -ne 0) {
            throw 'The extraction destination changed during package verification.'
        }
        foreach ($path in ($requiredDirectories | Sort-Object -Property Length)) {
            if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
            $directoryPath = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($destinationFullPath,
                $path.Replace([char]'/', [System.IO.Path]::DirectorySeparatorChar)))
            if (-not $directoryPath.StartsWith($destinationPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "The ZIP directory escapes the extraction destination: $path"
            }
            Assert-VNoteNoReparse -Path $directoryPath
            if ([System.IO.File]::Exists($directoryPath) -or [System.IO.Directory]::Exists($directoryPath)) {
                throw "The extraction destination already contains a directory or file: $directoryPath"
            }
            $null = [System.IO.Directory]::CreateDirectory($directoryPath)
            $createdDirectories.Add($directoryPath)
        }
        [long]$payloadWritten = 0
        foreach ($record in $entriesToExtract) {
            if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
            Assert-VNoteNoReparse -Path $record.Destination
            $outputStream = $null
            $inputStream = $null
            try {
                $outputStream = [System.IO.FileStream]::new($record.Destination, [System.IO.FileMode]::CreateNew,
                    [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
                $createdFiles.Add($record.Destination)
                if ($record.IsManifest) {
                    $inPackageBytes.Position = 0
                    $inputStream = $inPackageBytes
                } else {
                    $inputStream = $record.Entry.Open()
                }
                [long]$written = 0
                while ($true) {
                    if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
                    $readCount = $inputStream.Read($buffer, 0, $buffer.Length)
                    if ($readCount -eq 0) { break }
                    if ($readCount -gt $record.Size - $written) {
                        throw "The ZIP stream exceeds its declared size: $($record.Path)"
                    }
                    if (-not $record.IsManifest -and $readCount -gt $signedPayloadSize - $payloadWritten) {
                        throw 'The ZIP stream exceeds the signed total extraction bound.'
                    }
                    $outputStream.Write($buffer, 0, $readCount)
                    $written += $readCount
                    if (-not $record.IsManifest) { $payloadWritten += $readCount }
                }
                if ($written -ne $record.Size) { throw "The ZIP stream is truncated: $($record.Path)" }
                $outputStream.Flush()
                if (-not $record.IsManifest) {
                    if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
                    $outputStream.Position = 0
                    $actualHash = (Get-FileHash -InputStream $outputStream -Algorithm SHA256 -ErrorAction Stop).Hash
                    if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
                    if ($actualHash -ine $declaredFiles[$record.Path].sha256) {
                        throw "The extracted file SHA-256 differs from its signed hash: $($record.Path)"
                    }
                }
            } finally {
                if ($null -ne $inputStream -and -not $record.IsManifest) { $inputStream.Dispose() }
                if ($null -ne $outputStream) { $outputStream.Dispose() }
            }
        }
        if ($payloadWritten -ne $signedPayloadSize) { throw 'The extracted payload is incomplete.' }
        if ($null -ne $script:VNotePreparationGuard) { $null = & $script:VNotePreparationGuard }
        return $destinationFullPath
    } catch {
        $failure = $_
        $cleanupFailures = [System.Collections.Generic.List[string]]::new()
        # Never recursively delete the caller's destination. Only successful
        # CreateNew files and our created directories are eligible for cleanup.
        for ($index = $createdFiles.Count - 1; $index -ge 0; --$index) {
            try {
                Assert-VNoteNoReparse -Path $createdFiles[$index]
                [System.IO.File]::Delete($createdFiles[$index])
            } catch {
                $cleanupFailures.Add($createdFiles[$index])
            }
        }
        for ($index = $createdDirectories.Count - 1; $index -ge 0; --$index) {
            try {
                Assert-VNoteNoReparse -Path $createdDirectories[$index]
                if ([System.IO.Directory]::Exists($createdDirectories[$index])) {
                    [System.IO.Directory]::Delete($createdDirectories[$index], $false)
                }
            } catch {
                $cleanupFailures.Add($createdDirectories[$index])
            }
        }
        if ($cleanupFailures.Count -ne 0) {
            throw "$($failure.Exception.Message) Temporary extraction contents could not be removed: $($cleanupFailures -join ', ')"
        }
        throw $failure
    } finally {
        if ($null -ne $inPackageBytes) { $inPackageBytes.Dispose() }
        if ($null -ne $zip) { $zip.Dispose() }
        if ($null -ne $archiveStream) { $archiveStream.Dispose() }
    }
}

# A launch-scoped pipe is the only authority to request shutdown. Process EOF alone
# never authorizes installation; retain the original process handle until it exits.
$script:VNoteStartupTimeoutMilliseconds = 15000
$script:VNoteCloseTimeoutMilliseconds = 300000
$script:VNoteExitTimeoutMilliseconds = 120000
$script:VNoteLockTimeoutMilliseconds = 30000
$script:VNoteRelaunchObservationMilliseconds = 5000

function Initialize-VNoteNative {
    if ('VNoteUpdate.Native' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace VNoteUpdate {
    public static class Native {
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint pid);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool QueryFullProcessImageName(IntPtr process, uint flags,
                                                            StringBuilder path, ref uint size);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CreateDirectory(string path, IntPtr security);
        public static string ProcessPath(IntPtr process) {
            uint length = 32768;
            var path = new StringBuilder((int)length);
            if (!QueryFullProcessImageName(process, 0, path, ref length))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return path.ToString();
        }
        public static void CreateDirectoryExclusive(string path) {
            if (!CreateDirectory(path, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error());
        }
    }
}
'@ -ErrorAction Stop
}

function Get-VNoteProcessPath {
    param([Parameter(Mandatory = $true)][Diagnostics.Process]$Process)
    Initialize-VNoteNative
    # Accessing Handle retains the process object, not merely a reusable PID.
    return [IO.Path]::GetFullPath([VNoteUpdate.Native]::ProcessPath($Process.Handle))
}

function Start-VNotePipeRead {
    param([Parameter(Mandatory = $true)]$Session)
    if ($null -ne $Session.PendingRead) { throw 'A pipe read is already pending.' }
    $Session.PendingRead = $Session.Pipe.ReadAsync($Session.ReadBuffer, 0, 1)
}

function Read-VNotePipeFrame {
    param(
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][int]$TimeoutMilliseconds
    )
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $bytes = [Collections.Generic.List[byte]]::new()
    while ($true) {
        if ($null -eq $Session.PendingRead) { Start-VNotePipeRead -Session $Session }
        $remaining = [long]$TimeoutMilliseconds - $clock.ElapsedMilliseconds
        if ($remaining -le 0 -or -not (Wait-VNoteTask -Task $Session.PendingRead -TimeoutMilliseconds ([int]$remaining))) {
            throw 'Timed out waiting for VNote. Nothing will be installed; reopen VNote if it has closed.'
        }
        $pending = $Session.PendingRead
        $Session.PendingRead = $null
        $count = $pending.GetAwaiter().GetResult()
        if ($count -ne 1) {
            throw 'VNote disconnected without a complete authorization frame. Nothing will be installed; reopen VNote if it has closed.'
        }
        $value = $Session.ReadBuffer[0]
        if ($value -eq 10) { return [Text.Encoding]::ASCII.GetString($bytes.ToArray()) }
        if ($value -lt 32 -or $value -gt 126 -or $bytes.Count -ge 255) {
            throw 'VNote sent an invalid or oversized update protocol frame.'
        }
        $bytes.Add($value)
    }
}

function Write-VNotePipeFrame {
    param(
        [Parameter(Mandatory = $true)]$Session,
        [Parameter(Mandatory = $true)][string]$Frame
    )
    if ($Frame.Length -gt 255 -or $Frame -match '[^\x20-\x7e]') {
        throw 'Invalid outgoing update protocol frame.'
    }
    $bytes = [Text.Encoding]::ASCII.GetBytes($Frame + "`n")
    $Session.PendingWrite = $Session.Pipe.WriteAsync($bytes, 0, $bytes.Length)
    if (-not (Wait-VNoteTask -Task $Session.PendingWrite -TimeoutMilliseconds $script:VNoteStartupTimeoutMilliseconds)) {
        throw 'VNote did not receive the update request within the deadline.'
    }
    $null = $Session.PendingWrite.GetAwaiter().GetResult()
    $Session.PendingWrite = $null
}

function Close-VNoteParentSession {
    param($Session)
    if ($null -eq $Session) { return }
    try {
        if ($null -ne $Session.Pipe) { $Session.Pipe.Dispose() }
    } finally {
        # Task-based IO owns its completion synchronization. Closing an APM wait
        # handle before cancelled IO completes crashes .NET's PipeStream callback.
        foreach ($operation in @($Session.PendingRead, $Session.PendingWrite)) {
            if ($null -ne $operation) {
                try { $null = Wait-VNoteTask -Task $operation -TimeoutMilliseconds 1000 }
                catch { } # Disposal is expected to cancel the pending IO.
            }
        }
        $Session.PendingRead = $null
        $Session.PendingWrite = $null
        if ($null -ne $Session.ParentProcess) { $Session.ParentProcess.Dispose() }
    }
}

function Connect-VNoteParent {
    param(
        [Parameter(Mandatory = $true)][string]$InstallDir,
        [Parameter(Mandatory = $true)][int]$ParentProcessId,
        [Parameter(Mandatory = $true)][string]$PipeName,
        [Parameter(Mandatory = $true)][string]$Token
    )
    if ($ParentProcessId -le 0 -or $PipeName -cnotmatch '\Avnote-update-[0-9a-fA-F-]{32,36}\z' -or
        $Token -cnotmatch '\A[0-9a-fA-F-]{32,36}\z') {
        throw 'The launch-scoped VNote process, pipe, or token is invalid.'
    }
    Initialize-VNoteNative
    Assert-VNoteNoReparse -Path $InstallDir
    $session = [pscustomobject]@{
        Pipe = $null; ParentProcess = $null; ReadBuffer = (New-Object byte[] 1)
        PendingRead = $null; PendingWrite = $null; Token = $Token; Authenticated = $false
        ReadySent = $false; Accepted = $false
    }
    try {
        $session.ParentProcess = [Diagnostics.Process]::GetProcessById($ParentProcessId)
        $expected = [IO.Path]::GetFullPath([IO.Path]::Combine($InstallDir, 'vnote.exe'))
        if (-not [string]::Equals((Get-VNoteProcessPath -Process $session.ParentProcess), $expected,
                [StringComparison]::OrdinalIgnoreCase) -or $session.ParentProcess.HasExited) {
            throw 'The original VNote process does not belong to this installation.'
        }
        $session.Pipe = [IO.Pipes.NamedPipeClientStream]::new('.', $PipeName,
            [IO.Pipes.PipeDirection]::InOut, [IO.Pipes.PipeOptions]::Asynchronous)
        $session.Pipe.Connect($script:VNoteStartupTimeoutMilliseconds)
        [uint32]$serverPid = 0
        if (-not [VNoteUpdate.Native]::GetNamedPipeServerProcessId($session.Pipe.SafePipeHandle, [ref]$serverPid) -or
            $serverPid -ne $ParentProcessId -or $session.ParentProcess.HasExited) {
            throw 'The update pipe is not owned by the original VNote process.'
        }
        Write-VNotePipeFrame -Session $session -Frame "HELLO $Token"
        if ((Read-VNotePipeFrame -Session $session -TimeoutMilliseconds $script:VNoteStartupTimeoutMilliseconds) -cne 'OK') {
            throw 'VNote refused to authenticate the update helper.'
        }
        $session.Authenticated = $true
        # A pending async read observes EOF during preparation without unsupported
        # NamedPipeClientStream.ReadTimeout or polling a possibly reused PID.
        Start-VNotePipeRead -Session $session
        return $session
    } catch {
        Close-VNoteParentSession -Session $session
        throw
    }
}

function Assert-VNoteParentPreparing {
    param([Parameter(Mandatory = $true)]$Session)
    if (-not $Session.Authenticated -or $Session.ReadySent -or
        $Session.ParentProcess.HasExited -or $null -eq $Session.PendingRead -or
        $Session.PendingRead.IsCompleted) {
        throw 'VNote closed or the update connection was lost during preparation. Nothing will be installed.'
    }
}

function Request-VNoteShutdown {
    param([Parameter(Mandatory = $true)]$Session)
    Assert-VNoteParentPreparing -Session $Session
    $Session.ReadySent = $true
    Write-Host 'Update verified. Waiting for VNote to save and close...'
    Write-VNotePipeFrame -Session $Session -Frame "READY $($Session.Token)"
    $reply = Read-VNotePipeFrame -Session $Session -TimeoutMilliseconds $script:VNoteCloseTimeoutMilliseconds
    if ($reply -cne 'ACCEPTED') {
        throw 'VNote did not accept shutdown. Nothing was installed; use Update Now to retry.'
    }
    $Session.Accepted = $true
}

function Wait-VNoteParentExit {
    param([Parameter(Mandatory = $true)]$Session)
    if (-not $Session.Accepted) { throw 'Explicit ACCEPTED authorization is required before installation.' }
    if (-not $Session.ParentProcess.WaitForExit($script:VNoteExitTimeoutMilliseconds)) {
        throw 'VNote has not finished shutting down. Nothing was installed; no process was terminated.'
    }
}

$script:VNoteScriptPath = $PSCommandPath

function Enter-VNoteUpdateMutex {
    param([Parameter(Mandatory = $true)][string]$InstallDir)
    $root = Get-VNoteInstallRoot -InstallDir $InstallDir
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = [BitConverter]::ToString($sha.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant()))).Replace('-', '')
    } finally { $sha.Dispose() }
    $mutex = [Threading.Mutex]::new($false, ('Local\VNote.Update.' + $hash))
    $acquired = $false
    try {
        try { $acquired = $mutex.WaitOne(0) }
        catch [Threading.AbandonedMutexException] { $acquired = $true }
        if (-not $acquired) { throw 'Another updater is already working on this VNote installation.' }
        return $mutex
    } catch {
        $mutex.Dispose()
        throw
    }
}

function Start-VNoteAfterUpdate {
    param(
        [Parameter(Mandatory = $true)][string]$InstallDir,
        [Parameter(Mandatory = $true)][string]$BackupPath
    )
    $process = $null
    try {
        $root = Get-VNoteInstallRoot -InstallDir $InstallDir
        Assert-VNoteNoTargetProcesses -InstallDir $root
        $executable = [IO.Path]::Combine($root, 'vnote.exe')
        # No shell verbs, elevation, token changes, or replayed file arguments.
        $process = Start-Process -FilePath $executable -WorkingDirectory $root -PassThru -ErrorAction Stop
        $null = $process.Handle
        if ($process.WaitForExit($script:VNoteRelaunchObservationMilliseconds) -or
            -not [string]::Equals((Get-VNoteProcessPath -Process $process), $executable,
                [StringComparison]::OrdinalIgnoreCase) -or $process.HasExited) {
            throw 'The launched process exited immediately or did not retain the updated executable identity.'
        }
        Write-Host "Update installed; VNote started (PID $($process.Id))."
        Write-Host "Recovery backup retained at: $BackupPath"
        return $process
    } catch {
        if ($null -ne $process) { $process.Dispose() }
        throw ("Update installed, but VNote could not be reopened. $($_.Exception.Message) " +
            "Installation: $InstallDir. Recovery backup: $BackupPath. " +
            'Check for an existing VNote instance, then open vnote.exe manually. The backup was not removed.')
    }
}

function Remove-VNoteCopiedHelper {
    param([Parameter(Mandatory = $true)][string]$ScratchDir)
    $scratch = [IO.Path]::GetFullPath($ScratchDir).TrimEnd([char[]]'\/')
    $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([char[]]'\/')
    # Never remove an installed or repository script when dot-sourced/manual.
    if (-not $scratch.StartsWith(($temp + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($scratch) -cnotmatch '\Avnote-update-[A-Za-z0-9]{6,}\z') { return }
    Assert-VNoteNoReparse -Path $scratch
    Set-Location -LiteralPath $temp
    [Environment]::CurrentDirectory = $temp
    foreach ($name in @('update-vnote.ps1', 'minisign.exe', 'LICENSE.minisign')) {
        $path = [IO.Path]::Combine($scratch, $name)
        Assert-VNoteNoReparse -Path $path
        if ([IO.File]::Exists($path)) { [IO.File]::Delete($path) }
    }
    # Nonrecursive: a foreign or still-busy file leaves this directory intact.
    [IO.Directory]::Delete($scratch, $false)
}

function Assert-VNoteRuntimePrerequisites {
    param([Parameter(Mandatory = $true)][string]$MinisignPath)
    $missing = ''
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { $missing = 'Windows is required.' }
    elseif (-not [Environment]::Is64BitProcess) { $missing = 'A 64-bit PowerShell process is required.' }
    elseif ($PSVersionTable.PSVersion -lt [version]'5.1') { $missing = 'Windows PowerShell 5.1 or later is required.' }
    elseif (-not [IO.File]::Exists($MinisignPath)) { $missing = "The bundled minisign.exe verifier is missing: $MinisignPath" }
    if ($missing) { throw "Automatic update is unavailable. Use Check Release to update manually. $missing" }
    try {
        Add-Type -AssemblyName System.IO.Compression -ErrorAction Stop
        Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction Stop
        $null = [IO.Compression.ZipArchive]
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Initialize-VNoteNative
        Assert-VNoteNoReparse -Path $MinisignPath
        $savedPath = $env:PATH
        $savedPreference = $ErrorActionPreference
        try {
            $env:PATH = $script:VNoteInstallRoot + ';' + $savedPath
            $ErrorActionPreference = 'Continue'
            $global:LASTEXITCODE = -1
            $null = & $MinisignPath '-v' 2>&1
            if ($global:LASTEXITCODE -ne 0) { throw 'The bundled minisign.exe could not run with the installed runtime DLLs.' }
        } finally {
            $env:PATH = $savedPath
            $ErrorActionPreference = $savedPreference
        }
    } catch {
        throw "Automatic update is unavailable. Use Check Release to update manually. $($_.Exception.Message)"
    }
}

function Invoke-VNoteUpdate {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][ValidateSet('github', 'gitee')][string]$Source,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$CurrentVersion,
        [Parameter(Mandatory = $true)][ValidateSet('win64', 'win64-windows7')][string]$Variant,
        [Parameter(Mandatory = $true)][string]$InstallDir,
        [Parameter(Mandatory = $true)][int]$ParentProcessId,
        [Parameter(Mandatory = $true)][string]$PipeName,
        [Parameter(Mandatory = $true)][string]$Token
    )
    $session = $null
    $mutex = $null
    $stage = $null
    $started = $null
    $backup = $null
    $failure = $null
    $scratch = [IO.Path]::GetDirectoryName($script:VNoteScriptPath)
    try {
        Write-Host "Preparing VNote $Version from $Source. VNote will remain open until the update is verified."
        Assert-VNoteVersion -Version $Version
        Assert-VNoteVersion -Version $CurrentVersion
        if ((Compare-VNoteVersion -Left $Version -Right $CurrentVersion) -le 0) {
            throw 'The offered update is not newer than the running VNote version. Use Check Release to update manually.'
        }
        $script:VNoteInstallRoot = $InstallDir
        $minisign = [IO.Path]::Combine($scratch, 'minisign.exe')
        Assert-VNoteRuntimePrerequisites -MinisignPath $minisign
        $root = Get-VNoteInstallRoot -InstallDir $InstallDir
        $script:VNoteInstallRoot = $root
        $session = Connect-VNoteParent -InstallDir $root -ParentProcessId $ParentProcessId -PipeName $PipeName -Token $Token
        $mutex = Enter-VNoteUpdateMutex -InstallDir $root
        $script:VNotePreparationGuard = { Assert-VNoteParentPreparing -Session $session }.GetNewClosure()
        Assert-VNoteParentPreparing -Session $session
        # The controller transfers scratch ownership before replying OK. Never
        # create downloads/extraction directories before that authenticated reply.
        $candidate = [IO.Path]::Combine($scratch, ('attempt-' + [guid]::NewGuid().ToString('N')))
        Assert-VNoteNoReparse -Path $candidate
        [VNoteUpdate.Native]::CreateDirectoryExclusive($candidate)
        $stage = $candidate
        $script:VNoteStageRoot = $stage
        $release = Get-VNoteRelease -Source $Source -Version $Version
        $assets = Select-VNoteReleaseAssets -Release $release -Version $Version -Variant $Variant
        $manifestPath = [IO.Path]::Combine($stage, 'release.manifest.json')
        $signaturePath = $manifestPath + '.minisig'
        Receive-VNoteFile -Url $assets.Manifest -Destination $manifestPath -Source $Source -MaxBytes 16777216
        Receive-VNoteFile -Url $assets.Signature -Destination $signaturePath -Source $Source -MaxBytes 65536
        $manifest = Read-VerifiedVNoteManifest -Path $manifestPath -SignaturePath $signaturePath -MinisignPath $minisign `
            -Version $Version -CurrentVersion $CurrentVersion -Variant $Variant -TrustedKeys $script:VNoteTrustedKeys
        Assert-VNoteInstallReady -InstallDir $root -Manifest $manifest -PreparingDownload
        $archivePath = [IO.Path]::Combine($stage, $assets.ZipName)
        Receive-VNoteFile -Url $assets.Zip -Destination $archivePath -Source $Source -MaxBytes $manifest.fullPackage.size
        Write-Host 'Verifying and extracting the full update package...'
        $packagePath = [IO.Path]::Combine($stage, 'package')
        [VNoteUpdate.Native]::CreateDirectoryExclusive($packagePath)
        $null = Expand-VerifiedVNotePackage -ArchivePath $archivePath -Destination $packagePath -Manifest $manifest
        Assert-VNoteInstallReady -InstallDir $root -PackageDir $packagePath -Manifest $manifest
        Assert-VNoteParentPreparing -Session $session
        $script:VNotePreparationGuard = $null
        Request-VNoteShutdown -Session $session
        Wait-VNoteParentExit -Session $session
        Wait-VNoteInstallUnlocked -InstallDir $root
        Write-Host 'VNote has fully exited. Preparing the replacement installation...'
        $backup = Install-VNotePackage -InstallDir $root -PackageDir $packagePath -Manifest $manifest
        $started = Start-VNoteAfterUpdate -InstallDir $root -BackupPath $backup
    } catch {
        $failure = $_.Exception.Message
    } finally {
        $script:VNotePreparationGuard = $null
        Close-VNoteParentSession -Session $session
        if ($null -ne $started) { $started.Dispose() }
        if ($null -ne $mutex) {
            try { $mutex.ReleaseMutex() } finally { $mutex.Dispose() }
        }
        if ($null -ne $stage) {
            try { Remove-VNoteOwnedStage -Path $stage }
            catch { Write-Host "Temporary update files retained at $stage`: $($_.Exception.Message)" }
        }
        try { Remove-VNoteCopiedHelper -ScratchDir $scratch }
        catch { Write-Host "Temporary updater files retained at $scratch`: $($_.Exception.Message)" }
        $script:VNoteStageRoot = $null
    }
    if ($null -ne $failure) {
        Write-Host "Automatic update failed: $failure" -ForegroundColor Red
        Write-Host 'Use Check Release to update manually. If VNote has closed, reopen it from the installation folder.'
        if ($null -ne $backup) { Write-Host "Recovery backup retained at: $backup" }
        # All locks and the pipe have been released: this visible prompt cannot
        # keep the application's attempt active or prevent an explicit retry.
        $null = Read-Host 'Press Enter to close this updater console'
        return 1
    }
    return 0
}

function Get-VNoteDirectoryIdentity {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    Assert-VNoteNoReparse -Path $fullPath
    if (-not [IO.Directory]::Exists($fullPath)) { throw "The directory does not exist: $fullPath" }
    if (-not ('VNoteUpdate.InstallNative' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace VNoteUpdate {
    public static class InstallNative {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFile(string path, uint access,
            uint share, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandle(SafeFileHandle handle,
            StringBuilder path, uint length, uint flags);
        private static string FinalPath(SafeFileHandle handle, uint flags) {
            var path = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandle(handle, path, (uint)path.Capacity, flags);
            if (length == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (length >= path.Capacity) throw new InvalidOperationException("The directory path is too long.");
            return path.ToString();
        }
        public static string[] DirectoryPaths(string path) {
            // No data access; retain identity while resolving DOS and volume names.
            using (var handle = CreateFile(path, 0, 7, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero)) {
                if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
                return new [] { FinalPath(handle, 0), FinalPath(handle, 1) };
            }
        }
    }
}
'@ -ErrorAction Stop
    }
    $paths = [VNoteUpdate.InstallNative]::DirectoryPaths($fullPath)
    if ($paths[0] -notmatch '\A\\\\\?\\[A-Za-z]:\\' -or
        -not $paths[1].StartsWith('\\?\Volume{', [StringComparison]::OrdinalIgnoreCase)) {
        throw "The updater requires an existing directory on a local volume: $Path"
    }
    $canonical = $paths[0].Substring(4)
    if ($canonical.Length -gt [IO.Path]::GetPathRoot($canonical).Length) {
        $canonical = $canonical.TrimEnd([char[]]'\/')
    }
    Assert-VNoteNoReparse -Path $canonical
    $volumeEnd = $paths[1].IndexOf('\', 4)
    if ($volumeEnd -lt 0) { throw "Cannot establish the directory's volume identity: $Path" }
    return [pscustomobject]@{ Path = $canonical; Volume = $paths[1].Substring(0, $volumeEnd + 1) }
}

function Get-VNoteInstallRoot {
    param([Parameter(Mandatory = $true)][string]$InstallDir)

    if ([string]::IsNullOrWhiteSpace($InstallDir) -or $InstallDir -notmatch '\A[A-Za-z]:[\\/]' -or
        $InstallDir.StartsWith('\\') -or $InstallDir.Substring(2).Contains(':')) {
        throw 'The VNote installation must be an existing, non-root directory on a local drive. Use Check Release to update manually.'
    }
    $fullPath = [IO.Path]::GetFullPath($InstallDir).TrimEnd([char[]]'\/')
    $driveRoot = [IO.Path]::GetPathRoot($fullPath)
    if ($fullPath.Length -le $driveRoot.Length) {
        throw 'Updating a drive root is not supported. Use Check Release to update manually.'
    }
    foreach ($component in $fullPath.Substring($driveRoot.Length).Split([char[]]'\/')) {
        if ($component.EndsWith('.') -or $component.EndsWith(' ')) {
            throw "The installation path contains a Windows name alias: $InstallDir"
        }
    }
    $drive = [IO.DriveInfo]::new($driveRoot)
    if (-not $drive.IsReady -or $drive.DriveType -in @([IO.DriveType]::Network,
            [IO.DriveType]::Unknown, [IO.DriveType]::NoRootDirectory)) {
        throw 'The VNote installation must be on an available local drive. Use Check Release to update manually.'
    }
    Assert-VNoteNoReparse -Path $fullPath
    $canonical = (Get-VNoteDirectoryIdentity -Path $fullPath).Path
    if ($canonical.Length -le [IO.Path]::GetPathRoot($canonical).Length) {
        throw 'Updating a drive root is not supported. Use Check Release to update manually.'
    }
    Assert-VNoteNoReparse -Path $canonical
    $executable = [IO.Path]::Combine($canonical, 'vnote.exe')
    Assert-VNoteNoReparse -Path $executable
    if (-not [IO.File]::Exists($executable)) {
        throw "The installation has no root vnote.exe: $canonical"
    }
    return $canonical
}

function Assert-VNoteDirectoryWritable {
    param([Parameter(Mandatory = $true)][string]$Directory)

    Initialize-VNoteNative
    Assert-VNoteNoReparse -Path $Directory
    $probe = [IO.Path]::Combine($Directory, '.vnote-write-probe-' + [Guid]::NewGuid().ToString('N'))
    $movedProbe = $probe + '-renamed'
    $ownedFile = $null
    $ownedDirectory = $null
    $stream = $null
    $failure = $null
    try {
        # File and directory ACLs can differ; exercise both without touching an
        # existing entry. CreateNew/CreateDirectoryExclusive never claim a collision.
        $stream = [IO.FileStream]::new($probe, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::None)
        $ownedFile = $probe
        $stream.WriteByte(0)
        $stream.Flush()
        $stream.Dispose()
        $stream = $null
        [IO.File]::Move($probe, $movedProbe)
        $ownedFile = $movedProbe
        [IO.File]::Delete($ownedFile)
        $ownedFile = $null
        [VNoteUpdate.Native]::CreateDirectoryExclusive($probe)
        $ownedDirectory = $probe
        [IO.Directory]::Move($probe, $movedProbe)
        $ownedDirectory = $movedProbe
        [IO.Directory]::Delete($ownedDirectory, $false)
        $ownedDirectory = $null
    } catch {
        $failure = $_
    } finally {
        if ($null -ne $stream) {
            try { $stream.Dispose() }
            catch { if ($null -eq $failure) { $failure = $_ } }
        }
        foreach ($ownedPath in @($ownedFile, $ownedDirectory)) {
            if ($null -eq $ownedPath) { continue }
            try {
                Assert-VNoteNoReparse -Path $ownedPath
                if ($ownedPath -eq $ownedFile) { [IO.File]::Delete($ownedPath) }
                else { [IO.Directory]::Delete($ownedPath, $false) }
            } catch {
                if ($null -eq $failure) { $failure = $_ }
                Write-Warning "Could not remove the updater's permission probe: $ownedPath. $($_.Exception.Message)"
            }
        }
    }
    if ($null -ne $failure) {
        throw "The VNote installation folder is not writable. Use Check Release to update this installation manually. Directory: $Directory. $($failure.Exception.Message)"
    }
}

function Assert-VNotePackageContents {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)]$Manifest,
        [switch]$AllowUnowned
    )

    $version = Get-VNoteProperty -Object $Manifest -Name 'version'
    $variant = Get-VNoteProperty -Object $Manifest -Name 'variant'
    Assert-VNoteManifestFiles -Manifest $Manifest -Version $version -Variant $variant
    $rootPath = (Get-VNoteDirectoryIdentity -Path $Root).Path
    Assert-VNoteNoReparse -Path $rootPath
    $prefix = $rootPath + [IO.Path]::DirectorySeparatorChar
    $declared = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    $parents = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $Manifest.files) {
        $declared.Add($file.path, $file)
        $separator = $file.path.LastIndexOf('/')
        while ($separator -gt 0) {
            [void]$parents.Add($file.path.Substring(0, $separator))
            $separator = $file.path.LastIndexOf('/', $separator - 1)
        }
    }
    # Even an overlay tree must be scanned completely: an unowned link is not safe
    # merely because the manifest never names it.
    foreach ($entry in (Get-VNoteTree -Root $rootPath)) {
        if ($AllowUnowned) { continue }
        $relative = $entry.FullName.Substring($prefix.Length).Replace('\', '/')
        if (($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
            if (-not $parents.Contains($relative)) { throw "The extracted package contains an undeclared directory: $relative" }
        } elseif ($relative -ine 'manifest.json' -and -not $declared.ContainsKey($relative)) {
            throw "The extracted package contains an undeclared file: $relative"
        }
    }
    foreach ($file in $Manifest.files) {
        $filePath = [IO.Path]::GetFullPath([IO.Path]::Combine($rootPath, $file.path.Replace('/', '\')))
        if (-not $filePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "A signed payload path escapes its directory: $($file.path)"
        }
        Assert-VNoteNoReparse -Path $filePath
        if (([IO.File]::GetAttributes($filePath) -band [IO.FileAttributes]::ReadOnly) -ne 0) {
            throw "A package-owned file is read-only: $filePath. Use Check Release to update manually."
        }
        $stream = $null
        try {
            $stream = [IO.FileStream]::new($filePath, [IO.FileMode]::Open,
                [IO.FileAccess]::Read, [IO.FileShare]::Read)
            if ($stream.Length -ne [long]$file.size -or
                (Get-FileHash -InputStream $stream -Algorithm SHA256 -ErrorAction Stop).Hash -ine $file.sha256) {
                throw "A package-owned file differs from the signed size or SHA-256: $($file.path)"
            }
        } finally {
            if ($null -ne $stream) { $stream.Dispose() }
        }
    }
    $manifestPath = [IO.Path]::Combine($rootPath, 'manifest.json')
    Assert-VNoteNoReparse -Path $manifestPath
    if (([IO.File]::GetAttributes($manifestPath) -band [IO.FileAttributes]::ReadOnly) -ne 0) {
        throw "The in-package manifest is read-only: $manifestPath. Use Check Release to update manually."
    }
    $stream = $null
    $reader = $null
    try {
        $stream = [IO.FileStream]::new($manifestPath, [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::Read)
        if ($stream.Length -le 0 -or $stream.Length -gt 16777216) {
            throw 'The in-package manifest is empty or exceeds its size bound.'
        }
        $reader = [IO.StreamReader]::new($stream, [Text.UTF8Encoding]::new($false, $true), $false, 65536, $true)
        $text = $reader.ReadToEnd()
        if ($text.Length -gt 0 -and $text[0] -eq [char]0xfeff) { $text = $text.Substring(1) }
        $embedded = ConvertFrom-Json -InputObject $text -ErrorAction Stop
    } finally {
        if ($null -ne $reader) { $reader.Dispose() }
        if ($null -ne $stream) { $stream.Dispose() }
    }
    Assert-VNoteManifestFiles -Manifest $embedded -Version $version -Variant $variant
    if ($embedded.PSObject.Properties.Name -icontains 'fullPackage' -or
        $embedded.PSObject.Properties.Name -icontains 'delta') {
        throw 'The in-package manifest must not contain release archive references.'
    }
    foreach ($name in @('schema', 'product', 'channel', 'version', 'variant', 'platform', 'commit', 'generatedAt')) {
        if ((Get-VNoteProperty -Object $embedded -Name $name) -cne
            (Get-VNoteProperty -Object $Manifest -Name $name)) {
            throw "The in-package manifest differs from the signed identity field: $name"
        }
    }
    if ($embedded.files.Count -ne $declared.Count) { throw 'The in-package manifest file table differs from the signed table.' }
    foreach ($file in $embedded.files) {
        if (-not $declared.ContainsKey($file.path)) { throw "The in-package manifest declares an unsigned file: $($file.path)" }
        $signedFile = $declared[$file.path]
        if ([long]$file.size -ne [long]$signedFile.size -or $file.sha256 -ine $signedFile.sha256) {
            throw "The in-package manifest differs from the signed file table: $($file.path)"
        }
    }
}

function Assert-VNoteInstallReady {
    param(
        [Parameter(Mandatory = $true)][string]$InstallDir,
        [Parameter(Mandatory = $true)]$Manifest,
        [string]$PackageDir,
        [switch]$PreparingDownload
    )

    $root = Get-VNoteInstallRoot -InstallDir $InstallDir
    Assert-VNoteManifestFiles -Manifest $Manifest -Version $Manifest.version -Variant $Manifest.variant
    [decimal]$oldSize = 0
    foreach ($entry in (Get-VNoteTree -Root $root)) {
        if (($entry.Attributes -band [IO.FileAttributes]::Directory) -eq 0) { $oldSize += [long]$entry.Length }
    }
    [decimal]$ownedSize = 0
    $payloadPaths = [Collections.Generic.List[string]]::new()
    foreach ($file in $Manifest.files) {
        $ownedSize += [long]$file.size
        $payloadPaths.Add($file.path)
    }
    $payloadPaths.Add('manifest.json')
    foreach ($relative in $payloadPaths) {
        $destination = [IO.Path]::Combine($root, $relative.Replace('/', '\'))
        Assert-VNoteNoReparse -Path $destination
        if ([IO.Directory]::Exists($destination)) {
            throw "A package file would replace an existing directory: $destination. Use Check Release to update manually."
        }
        if ([IO.File]::Exists($destination) -and
            ([IO.File]::GetAttributes($destination) -band [IO.FileAttributes]::ReadOnly) -ne 0) {
            throw "The VNote installation folder is not writable. Use Check Release to update this installation manually. Read-only payload: $destination"
        }
        $parent = [IO.Path]::GetDirectoryName($destination)
        while (-not [string]::Equals($parent, $root, [StringComparison]::OrdinalIgnoreCase)) {
            if ([IO.File]::Exists($parent)) {
                throw "A package directory would replace an existing file: $parent. Use Check Release to update manually."
            }
            $parent = [IO.Path]::GetDirectoryName($parent)
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($PackageDir)) {
        Assert-VNotePackageContents -Root $PackageDir -Manifest $Manifest
        $ownedSize += [IO.FileInfo]::new([IO.Path]::Combine($PackageDir, 'manifest.json')).Length
    } else {
        # The embedded manifest has a separately enforced 16 MiB cap. Its exact
        # size is not yet known when deciding whether it is safe to download.
        $ownedSize += 16777216
    }
    Assert-VNoteDirectoryWritable -Directory $root
    Assert-VNoteDirectoryWritable -Directory ([IO.Path]::GetDirectoryName($root))
    $installIdentity = Get-VNoteDirectoryIdentity -Path $root
    $installDrive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($installIdentity.Path))
    [decimal]$installRequired = $oldSize + $ownedSize + 67108864
    if ($PreparingDownload) {
        $archiveSize = Get-VNoteProperty -Object (Get-VNoteProperty -Object $Manifest -Name 'fullPackage') -Name 'size'
        Assert-VNoteInteger -Value $archiveSize -Name 'fullPackage.size' -Minimum 1 -Maximum 4294967296
        if ([string]::IsNullOrWhiteSpace($script:VNoteStageRoot) -or
            -not [IO.Path]::IsPathRooted($script:VNoteStageRoot)) {
            throw 'The private download directory is required for the free-space check.'
        }
        Assert-VNoteNoReparse -Path $script:VNoteStageRoot
        if (-not [IO.Directory]::Exists($script:VNoteStageRoot)) { throw 'The private download directory does not exist.' }
        $tempIdentity = Get-VNoteDirectoryIdentity -Path $script:VNoteStageRoot
        $tempDrive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($tempIdentity.Path))
        [decimal]$downloadRequired = [long]$archiveSize + $ownedSize
        if ([string]::Equals($installIdentity.Volume, $tempIdentity.Volume, [StringComparison]::OrdinalIgnoreCase)) {
            $installRequired += $downloadRequired
        } elseif (-not $tempDrive.IsReady -or [decimal]$tempDrive.AvailableFreeSpace -lt $downloadRequired + 67108864) {
            throw "Not enough free space on the download drive for the ZIP, extraction, and 64 MiB headroom: $($tempDrive.Name)"
        }
    }
    if ([decimal]$installDrive.AvailableFreeSpace -lt $installRequired) {
        throw "Not enough free space for a complete installation clone, new package files, and 64 MiB headroom: $($installDrive.Name). Required bytes: $installRequired"
    }
}

function Get-VNoteTargetProcesses {
    param([Parameter(Mandatory = $true)][string]$InstallDir)

    $root = [IO.Path]::GetFullPath($InstallDir).TrimEnd([char[]]'\/')
    $targets = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [void]$targets.Add([IO.Path]::Combine($root, 'vnote.exe'))
    [void]$targets.Add([IO.Path]::Combine($root, 'QtWebEngineProcess.exe'))
    $matches = [Collections.Generic.List[Diagnostics.Process]]::new()
    try {
        foreach ($name in @('vnote', 'QtWebEngineProcess')) {
            $candidates = [Diagnostics.Process]::GetProcessesByName($name)
            try {
                foreach ($candidate in $candidates) {
                    try {
                        if ($candidate.HasExited) { continue }
                        $imagePath = Get-VNoteProcessPath -Process $candidate
                    } catch {
                        $queryFailure = $_
                        $exited = $false
                        try { $exited = $candidate.HasExited } catch { }
                        if ($exited) { continue }
                        throw "Cannot establish the executable identity of a live $name process (PID $($candidate.Id)); refusing to update. $($queryFailure.Exception.Message)"
                    }
                    if ($targets.Contains($imagePath) -and -not $candidate.HasExited) {
                        $matches.Add($candidate)
                    }
                }
            } finally {
                foreach ($candidate in $candidates) {
                    if (-not $matches.Contains($candidate)) { $candidate.Dispose() }
                }
            }
        }
    } catch {
        foreach ($process in $matches) { $process.Dispose() }
        throw
    }
    # Do not emit retained handles until all candidates have been checked: a
    # failing pipeline assignment would otherwise lose earlier handles.
    return $matches.ToArray()
}

function Assert-VNoteNoTargetProcesses {
    param([Parameter(Mandatory = $true)][string]$InstallDir)

    $processes = @(Get-VNoteTargetProcesses -InstallDir $InstallDir)
    try {
        if ($processes.Count -ne 0) {
            $ids = ($processes | ForEach-Object { $_.Id }) -join ', '
            throw "A VNote or QtWebEngine process still uses this installation: $InstallDir (PID $ids). Close it before updating; no process will be terminated."
        }
    } finally {
        foreach ($process in $processes) { $process.Dispose() }
    }
}

function Assert-VNoteBinaryFilesUnlocked {
    param([Parameter(Mandatory = $true)][string]$InstallDir)

    $streams = [Collections.Generic.List[IO.FileStream]]::new()
    try {
        foreach ($entry in (Get-VNoteTree -Root $InstallDir)) {
            if (($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
                $entry.Extension -inotmatch '\A\.(exe|dll)\z') { continue }
            Assert-VNoteNoReparse -Path $entry.FullName
            # Read-only exclusive opens do not detect a mapped image section.
            # Request write access without writing, so lingering loader locks fail.
            $streams.Add([IO.FileStream]::new($entry.FullName, [IO.FileMode]::Open,
                [IO.FileAccess]::ReadWrite, [IO.FileShare]::None))
        }
    } finally {
        foreach ($stream in $streams) { $stream.Dispose() }
    }
}

function Wait-VNoteInstallUnlocked {
    param([Parameter(Mandatory = $true)][string]$InstallDir)

    $clock = [Diagnostics.Stopwatch]::StartNew()
    $root = Get-VNoteInstallRoot -InstallDir $InstallDir
    $lastFailure = 'The installation has not become available.'
    while ($true) {
        try {
            Assert-VNoteNoTargetProcesses -InstallDir $root
            Assert-VNoteBinaryFilesUnlocked -InstallDir $root
            if ($clock.ElapsedMilliseconds -le $script:VNoteLockTimeoutMilliseconds) { return }
            $lastFailure = 'The installation lock check exceeded its deadline.'
        } catch {
            $lastFailure = $_.Exception.Message
        }
        $remaining = $script:VNoteLockTimeoutMilliseconds - $clock.ElapsedMilliseconds
        if ($remaining -le 0) {
            throw "Timed out waiting for this installation's processes and executable/DLL locks. $lastFailure"
        }
        Start-Sleep -Milliseconds ([int][Math]::Min(100, $remaining))
    }
}

function Assert-VNoteIdenticalFile {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    Assert-VNoteNoReparse -Path $Source
    Assert-VNoteNoReparse -Path $Destination
    $sourceStream = $null
    $destinationStream = $null
    try {
        $sourceStream = [IO.FileStream]::new($Source, [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $destinationStream = [IO.FileStream]::new($Destination, [IO.FileMode]::Open,
            [IO.FileAccess]::Read, [IO.FileShare]::Read)
        if ($sourceStream.Length -ne $destinationStream.Length -or
            (Get-FileHash -InputStream $sourceStream -Algorithm SHA256 -ErrorAction Stop).Hash -ine
            (Get-FileHash -InputStream $destinationStream -Algorithm SHA256 -ErrorAction Stop).Hash) {
            throw "The copied portable data differs from the stopped installation: $Source"
        }
    } finally {
        if ($null -ne $sourceStream) { $sourceStream.Dispose() }
        if ($null -ne $destinationStream) { $destinationStream.Dispose() }
    }
}

function Assert-VNoteCopiedConfig {
    param(
        [Parameter(Mandatory = $true)][string]$OriginalDir,
        [Parameter(Mandatory = $true)][string]$ReplacementDir
    )

    $original = [IO.Path]::Combine($OriginalDir, 'config')
    $replacement = [IO.Path]::Combine($ReplacementDir, 'config')
    Assert-VNoteNoReparse -Path $original
    Assert-VNoteNoReparse -Path $replacement
    if ([IO.File]::Exists($original)) {
        Assert-VNoteIdenticalFile -Source $original -Destination $replacement
        return
    }
    if (-not [IO.Directory]::Exists($original)) {
        if ([IO.File]::Exists($replacement) -or [IO.Directory]::Exists($replacement)) {
            throw 'The staged installation unexpectedly contains portable config data.'
        }
        return
    }
    if (-not [IO.Directory]::Exists($replacement)) { throw 'The staged installation is missing the portable config directory.' }
    $sourcePrefix = $original + [IO.Path]::DirectorySeparatorChar
    $destinationPrefix = $replacement + [IO.Path]::DirectorySeparatorChar
    $entries = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in (Get-VNoteTree -Root $original)) {
        $entries.Add($entry.FullName.Substring($sourcePrefix.Length), $entry)
    }
    foreach ($entry in (Get-VNoteTree -Root $replacement)) {
        $relative = $entry.FullName.Substring($destinationPrefix.Length)
        if (-not $entries.ContainsKey($relative)) { throw "The staged portable data has an unexpected entry: $relative" }
        $source = $entries[$relative]
        $sourceIsDirectory = ($source.Attributes -band [IO.FileAttributes]::Directory) -ne 0
        $destinationIsDirectory = ($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0
        if ($sourceIsDirectory -ne $destinationIsDirectory) { throw "The copied portable data changed entry type: $relative" }
        if (-not $sourceIsDirectory) { Assert-VNoteIdenticalFile -Source $source.FullName -Destination $entry.FullName }
        [void]$entries.Remove($relative)
    }
    if ($entries.Count -ne 0) { throw 'The staged installation is missing portable config entries.' }
}

function Remove-VNoteOwnedStage {
    param([Parameter(Mandatory = $true)][string]$Path)

    # Called only for a directory this invocation created exclusively and never
    # successfully installed. Refuse links; delete children non-recursively.
    Assert-VNoteNoReparse -Path $Path
    $entries = @(Get-VNoteTree -Root $Path)
    for ($index = $entries.Count - 1; $index -ge 0; --$index) {
        $entry = $entries[$index]
        Assert-VNoteNoReparse -Path $entry.FullName
        $attributes = [IO.File]::GetAttributes($entry.FullName)
        if (($attributes -band [IO.FileAttributes]::ReadOnly) -ne 0) {
            [IO.File]::SetAttributes($entry.FullName, $attributes -band (-bnot [IO.FileAttributes]::ReadOnly))
        }
        if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) { [IO.Directory]::Delete($entry.FullName, $false) }
        else { [IO.File]::Delete($entry.FullName) }
    }
    $attributes = [IO.File]::GetAttributes($Path)
    if (($attributes -band [IO.FileAttributes]::ReadOnly) -ne 0) {
        [IO.File]::SetAttributes($Path, $attributes -band (-bnot [IO.FileAttributes]::ReadOnly))
    }
    [IO.Directory]::Delete($Path, $false)
}

function Move-VNoteDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    [IO.Directory]::Move($Source, $Destination)
}

function Install-VNotePackage {
    param(
        [Parameter(Mandatory = $true)][string]$InstallDir,
        [Parameter(Mandatory = $true)][string]$PackageDir,
        [Parameter(Mandatory = $true)]$Manifest
    )

    # The orchestrator calls this only after ACCEPTED and the retained original
    # process handle has signalled exit. Recheck the stopped tree, not a pre-quit
    # config snapshot, and never write replacement bytes into the original.
    $root = Get-VNoteInstallRoot -InstallDir $InstallDir
    $packageRoot = (Get-VNoteDirectoryIdentity -Path $PackageDir).Path
    $prefix = $root + [IO.Path]::DirectorySeparatorChar
    if ([string]::Equals($packageRoot, $root, [StringComparison]::OrdinalIgnoreCase) -or
        $packageRoot.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or
        $root.StartsWith($packageRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The extracted package and installation must be separate, non-nested directories.'
    }
    Assert-VNoteInstallReady -InstallDir $root -Manifest $Manifest -PackageDir $packageRoot
    Wait-VNoteInstallUnlocked -InstallDir $root
    Initialize-VNoteNative
    $staging = $root + '.vnote-new-' + [Guid]::NewGuid().ToString('N')
    $backup = $root + '.vnote-old-' + [Guid]::NewGuid().ToString('N')
    $stageOwned = $false
    try {
        Assert-VNoteNoReparse -Path $staging
        [VNoteUpdate.Native]::CreateDirectoryExclusive($staging)
        $stageOwned = $true
        $directoryMetadata = [Collections.Generic.List[object]]::new()
        $directoryMetadata.Add([pscustomobject]@{ Source = [IO.DirectoryInfo]::new($root); Destination = $staging })
        foreach ($entry in (Get-VNoteTree -Root $root)) {
            $relative = $entry.FullName.Substring($prefix.Length)
            $destination = [IO.Path]::GetFullPath([IO.Path]::Combine($staging, $relative))
            if (-not $destination.StartsWith($staging + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw "An installation entry escapes the clone directory: $($entry.FullName)"
            }
            Assert-VNoteNoReparse -Path $entry.FullName
            Assert-VNoteNoReparse -Path $destination
            if (($entry.Attributes -band [IO.FileAttributes]::Directory) -ne 0) {
                [VNoteUpdate.Native]::CreateDirectoryExclusive($destination)
                $directoryMetadata.Add([pscustomobject]@{ Source = $entry; Destination = $destination })
            } else {
                [IO.File]::Copy($entry.FullName, $destination, $false)
            }
        }
        $payloadPaths = [Collections.Generic.List[string]]::new()
        foreach ($file in $Manifest.files) { $payloadPaths.Add($file.path) }
        $payloadPaths.Add('manifest.json')
        foreach ($relative in $payloadPaths) {
            $components = $relative.Split('/')
            $parent = $staging
            for ($index = 0; $index -lt $components.Length - 1; ++$index) {
                $parent = [IO.Path]::Combine($parent, $components[$index])
                Assert-VNoteNoReparse -Path $parent
                if (-not [IO.Directory]::Exists($parent)) { [VNoteUpdate.Native]::CreateDirectoryExclusive($parent) }
            }
            $source = [IO.Path]::Combine($packageRoot, $relative.Replace('/', '\'))
            $destination = [IO.Path]::Combine($staging, $relative.Replace('/', '\'))
            Assert-VNoteNoReparse -Path $source
            Assert-VNoteNoReparse -Path $destination
            [IO.File]::Copy($source, $destination, $true)
        }
        Assert-VNotePackageContents -Root $staging -Manifest $Manifest -AllowUnowned
        Assert-VNoteCopiedConfig -OriginalDir $root -ReplacementDir $staging
        # Apply directory metadata after all child writes. File.Copy already
        # preserves hidden/system/read-only file attributes and file timestamps.
        for ($index = $directoryMetadata.Count - 1; $index -ge 0; --$index) {
            $record = $directoryMetadata[$index]
            Assert-VNoteNoReparse -Path $record.Source.FullName
            Assert-VNoteNoReparse -Path $record.Destination
            [IO.Directory]::SetCreationTimeUtc($record.Destination, $record.Source.CreationTimeUtc)
            [IO.Directory]::SetLastWriteTimeUtc($record.Destination, $record.Source.LastWriteTimeUtc)
            [IO.File]::SetAttributes($record.Destination, $record.Source.Attributes)
        }
        Wait-VNoteInstallUnlocked -InstallDir $root
        Write-Host "Original installation: $root"
        Write-Host "Replacement staging: $staging"
        Write-Host "Recovery backup: $backup"
        Write-Host 'If interrupted between renames, close VNote and rename the recovery backup to the original installation path.'
        if (-not [string]::Equals((Get-VNoteInstallRoot -InstallDir $root), $root, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The installation path changed before replacement.'
        }
        Assert-VNoteNoReparse -Path $backup
        Assert-VNoteNoTargetProcesses -InstallDir $root
        Assert-VNoteNoTargetProcesses -InstallDir $staging
        Assert-VNoteBinaryFilesUnlocked -InstallDir $root
        Move-VNoteDirectory -Source $root -Destination $backup
        try {
            Assert-VNoteNoReparse -Path $root
            Assert-VNoteNoReparse -Path $backup
            Assert-VNoteNoReparse -Path $staging
            Assert-VNoteNoTargetProcesses -InstallDir $root
            Assert-VNoteNoTargetProcesses -InstallDir $backup
            Assert-VNoteNoTargetProcesses -InstallDir $staging
            Assert-VNoteBinaryFilesUnlocked -InstallDir $backup
            Move-VNoteDirectory -Source $staging -Destination $root
            $stageOwned = $false
        } catch {
            $replacementFailure = $_
            $recoveryFailure = $null
            try {
                Assert-VNoteNoReparse -Path $root
                $null = Get-VNoteInstallRoot -InstallDir $backup
                Assert-VNoteNoTargetProcesses -InstallDir $root
                Assert-VNoteNoTargetProcesses -InstallDir $backup
                Assert-VNoteBinaryFilesUnlocked -InstallDir $backup
                # Directory.Move never overwrites a directory created by another
                # process. Such a collision leaves the complete backup intact.
                Move-VNoteDirectory -Source $backup -Destination $root
            } catch {
                $recoveryFailure = $_
            }
            if ($null -ne $recoveryFailure) {
                throw "The replacement rename failed and automatic recovery could not restore the original path. Original: $root. Complete recovery backup: $backup. Close all VNote processes using either path before manually restoring the backup. Replacement error: $($replacementFailure.Exception.Message). Recovery error: $($recoveryFailure.Exception.Message)"
            }
            throw "The replacement rename failed; the complete original installation was restored at $root. $($replacementFailure.Exception.Message)"
        }
        return $backup
    } finally {
        if ($stageOwned) {
            try { Remove-VNoteOwnedStage -Path $staging }
            catch { Write-Warning "Could not remove the updater-owned staging directory: $staging. $($_.Exception.Message)" }
        }
    }
}

# Binding is optional only while dot-sourcing. The deployed -File entry requires
# every named argument and never discovers an installation or configuration itself.
if ($MyInvocation.InvocationName -ne '.') {
    try {
        foreach ($required in @('Source', 'Version', 'CurrentVersion', 'Variant', 'InstallDir',
                'ParentProcessId', 'PipeName', 'Token')) {
            if (-not $PSBoundParameters.ContainsKey($required) -or
                [string]::IsNullOrWhiteSpace([string]$PSBoundParameters[$required])) {
                throw "Missing required updater parameter: -$required"
            }
        }
        $result = Invoke-VNoteUpdate @PSBoundParameters
        exit $result
    } catch {
        Write-Host "Automatic update is unavailable. Use Check Release to update manually. $($_.Exception.Message)" -ForegroundColor Red
        $null = Read-Host 'Press Enter to close this updater console'
        exit 1
    }
}
