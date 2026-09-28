# Windows PowerShell 5.1 regression runner. No Pester, network service or signing
# secret is required. All child modes belong to this test, never the deployed helper.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$MinisignPath,
    [ValidateSet('Run', 'Http', 'Mutex', 'Update')][string]$ChildMode = 'Run',
    [string]$FixtureConfig
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'
$script:TestScript = $PSCommandPath
$script:UpdaterScript = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../update-vnote.ps1'))
$script:Utf8 = New-Object Text.UTF8Encoding($false)

function Write-FixtureText {
    param([string]$Path, [string]$Text)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    # Publish atomically: the HTTP child must never parse half-written routes.
    $temporary = $Path + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temporary, $Text, $script:Utf8)
    if ([IO.File]::Exists($Path)) { [IO.File]::Replace($temporary, $Path, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $Path) }
}

function Write-FixtureJson {
    param([string]$Path, $Value)
    Write-FixtureText $Path ($Value | ConvertTo-Json -Depth 30)
}

function Read-FixtureJson {
    param([string]$Path)
    return (ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($Path)))
}

function Invoke-HttpFixture {
    param($Config)
    # TcpListener avoids HttpListener URL ACLs and binds only a private loopback port.
    $listener = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try {
        Write-FixtureJson $Config.Ready @{ Port = $listener.LocalEndpoint.Port }
        while ($true) {
            $client = $listener.AcceptTcpClient()
            try {
                $stream = $client.GetStream()
                $stream.ReadTimeout = 5000
                $stream.WriteTimeout = 5000
                $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::ASCII, $false, 1024, $true)
                try {
                    $request = $reader.ReadLine()
                    if ([string]::IsNullOrEmpty($request)) { continue }
                    $target = ($request -split ' ')[1]
                    $headerBytes = $request.Length
                    do {
                        $line = $reader.ReadLine()
                        if ($null -eq $line) { throw 'Incomplete fixture request headers.' }
                        $headerBytes += $line.Length
                        if ($headerBytes -gt 16384) { throw 'Oversized fixture request headers.' }
                    } while ($line.Length -gt 0)
                    [IO.File]::AppendAllText($Config.Log, $target + "`n", $script:Utf8)
                    $routes = Read-FixtureJson $Config.Routes
                    $property = $routes.PSObject.Properties[$target]
                    if ($null -eq $property) {
                        $headers = "HTTP/1.1 404 Not Found`r`nContent-Length: 0`r`nConnection: close`r`n`r`n"
                        $body = New-Object byte[] 0
                    }
                    else {
                        $route = $property.Value
                        $body = if ($route.File) { [IO.File]::ReadAllBytes($route.File) } else { New-Object byte[] 0 }
                        $headers = "HTTP/1.1 $($route.Status) Fixture`r`nConnection: close`r`n"
                        if ($route.Location) { $headers += "Location: $($route.Location)`r`n" }
                        if (-not $route.OmitLength) { $headers += "Content-Length: $($body.Length)`r`n" }
                        $headers += "`r`n"
                    }
                    $header = [Text.Encoding]::ASCII.GetBytes($headers)
                    $stream.Write($header, 0, $header.Length)
                    if ($null -ne $property -and $property.Value.Truncate) {
                        $stream.Write($body, 0, [int][Math]::Max(1, [Math]::Floor($body.Length / 2)))
                    }
                    elseif ($body.Length -gt 0) {
                        $stream.Write($body, 0, $body.Length)
                    }
                    $stream.Flush()
                }
                finally { $reader.Dispose() }
            }
            finally { $client.Close() }
        }
    }
    finally { $listener.Stop() }
}

if ($ChildMode -ne 'Run') {
    try {
        $config = Read-FixtureJson $FixtureConfig
        if ($ChildMode -eq 'Http') { Invoke-HttpFixture $config; exit 0 }
        . $config.Updater
        if ($ChildMode -eq 'Mutex') {
            $mutex = $null
            try {
                $mutex = Enter-VNoteUpdateMutex -InstallDir $config.InstallDir
                Write-FixtureText $config.Result 'entered'
            }
            catch {
                Write-FixtureText $config.Result ('refused: ' + $_.Exception.Message)
                exit 7
            }
            finally {
                if ($null -ne $mutex) { $mutex.ReleaseMutex(); $mutex.Dispose() }
            }
            exit 0
        }
        # The real orchestrator still validates the actual peer PID/path and every
        # signature. Only this dot-sourced test scope trusts the ephemeral fixture.
        foreach ($sourceName in @('github', 'gitee')) {
            $script:VNoteOrigins[$sourceName] = @{
                ApiBaseUrl = "$($config.BaseUrl)/$sourceName/tags/"
                Scheme = 'http'; Port = [int]$config.Port
                Hosts = @('127.0.0.1'); HostSuffixes = @(); Accept = 'application/json'
            }
        }
        $script:VNoteTrustedKeys = @($config.PublicKey)
        $code = Invoke-VNoteUpdate -Source $config.Source -Version '9.1.0' -CurrentVersion '9.0.0' `
            -Variant $config.Variant -InstallDir $config.InstallDir -ParentProcessId $config.ParentProcessId `
            -PipeName $config.PipeName -Token $config.Token
        exit ([int]$code)
    }
    catch { [Console]::Error.WriteLine($_.ToString()); exit 99 }
}

if ($PSVersionTable.PSEdition -ne 'Desktop' -or $PSVersionTable.PSVersion -lt [Version]'5.1' -or
    -not [Environment]::Is64BitProcess) {
    throw 'Run this regression with 64-bit Windows PowerShell 5.1.'
}
$MinisignPath = [IO.Path]::GetFullPath($MinisignPath)
if (-not [IO.File]::Exists($MinisignPath)) {
    throw 'Prepare prepare_win_updater first; -MinisignPath must name its real minisign.exe.'
}
. $script:UpdaterScript
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$script:Children = New-Object 'Collections.Generic.List[object]'
$script:RelaunchRoots = New-Object 'Collections.Generic.List[string]'
$script:AclRestores = New-Object 'Collections.Generic.List[object]'
$script:Junctions = New-Object 'Collections.Generic.List[string]'
$script:Passed = 0
$script:Clock = [Diagnostics.Stopwatch]::StartNew()
$script:StartedAt = [DateTime]::Now
$script:Root = Join-Path ([IO.Path]::GetTempPath()) ('VNote updater ' + [char]0x66f4 + [char]0x65b0 + ' ' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($script:Root) | Out-Null
$script:PowerShell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$script:Routes = @{}
$script:RoutesPath = Join-Path $script:Root 'http routes.json'
$script:SavedPath = $env:PATH
$script:SavedPassword = $env:MINISIGN_PASSWORD
$env:PATH = [IO.Path]::GetDirectoryName($MinisignPath) + ';' + $env:PATH
$env:MINISIGN_PASSWORD = $null
$success = $false
$failure = $null

function Assert-Fixture {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "ASSERT: $Message" }
}

function Pass-Fixture {
    param([string]$Name)
    ++$script:Passed
    Write-Host "PASS $Name"
    if ($script:Clock.Elapsed.TotalSeconds -gt 160) { throw 'Regression exceeded its 160-second work budget (CTest limit: 180 seconds).' }
}

function Expect-FixtureFailure {
    param([scriptblock]$Action, [string]$Name)
    $message = $null
    try { $null = & $Action }
    catch { $message = $_.Exception.Message }
    Assert-Fixture ($null -ne $message) "$Name unexpectedly succeeded"
    return $message
}

function Quote-FixtureArgument {
    param([string]$Value)
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Start-FixtureProcess {
    param([string]$Executable, [string[]]$Arguments, [string]$Label, [string]$WorkingDirectory = $script:Root)
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = (($Arguments | ForEach-Object { Quote-FixtureArgument $_ }) -join ' ')
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    Assert-Fixture ($process.Start()) "Could not start $Label"
    $child = [pscustomobject]@{
        Process = $process; Label = $Label
        Output = $process.StandardOutput.ReadToEndAsync()
        Error = $process.StandardError.ReadToEndAsync()
    }
    $script:Children.Add($child)
    # A failed Invoke releases all resources before Read-Host. Feed that prompt
    # without -NonInteractive (which changes Read-Host into another exception).
    $process.StandardInput.WriteLine('')
    $process.StandardInput.Close()
    return $child
}

function Start-FixtureMode {
    param([string]$Mode, $Config, [string]$Label)
    $configPath = Join-Path $script:Root ($Label + '-' + [Guid]::NewGuid().ToString('N') + '.json')
    Write-FixtureJson $configPath $Config
    return (Start-FixtureProcess $script:PowerShell @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', $script:TestScript, '-MinisignPath', $MinisignPath, '-ChildMode', $Mode, '-FixtureConfig', $configPath) $Label)
}

function Wait-FixtureExit {
    param($Child, [int]$ExpectedCode, [int]$Milliseconds = 20000)
    Assert-Fixture ($Child.Process.WaitForExit($Milliseconds)) "$($Child.Label) did not exit"
    $stdout = $Child.Output.GetAwaiter().GetResult()
    $stderr = $Child.Error.GetAwaiter().GetResult()
    Write-FixtureText (Join-Path $script:Root ($Child.Label + '.stdout.txt')) $stdout
    Write-FixtureText (Join-Path $script:Root ($Child.Label + '.stderr.txt')) $stderr
    Assert-Fixture ($Child.Process.ExitCode -eq $ExpectedCode) "$($Child.Label) exit $($Child.Process.ExitCode), expected $ExpectedCode`n$stdout`n$stderr"
    return $stdout
}

function Stop-FixtureChild {
    param($Child)
    # Only retained processes created by this runner (or PID+path verified from
    # their generation record) can reach this function. Never kill by name.
    Assert-Fixture ($Child.Process.Id -ne $PID) "Refusing to stop the regression runner itself"
    if (-not $Child.Process.HasExited) {
        try { $Child.Process.Kill() }
        catch { if (-not $Child.Process.HasExited) { throw } }
        Assert-Fixture ($Child.Process.WaitForExit(5000)) "Could not stop fixture $($Child.Label)"
    }
}

function Stop-FixtureRecordedRelaunch {
    param([string]$InstallDir)
    $recordPath = Join-Path $InstallDir '.fixture-launched'
    if (-not [IO.File]::Exists($recordPath)) { return }
    $record = [IO.File]::ReadAllText($recordPath).Split('|')
    Assert-Fixture ($record.Length -eq 2 -and $record[0] -ceq 'B') 'Invalid fixture generation record during cleanup'
    $process = $null
    try { $process = [Diagnostics.Process]::GetProcessById([int]$record[1]) }
    catch [ArgumentException] { return } # This known fixture already exited.
    try {
        if ($process.HasExited) { return }
        Assert-Fixture ($process.StartTime -ge $script:StartedAt) 'Refusing to clean up a reused fixture PID'
        Assert-Fixture ((Get-VNoteProcessPath -Process $process) -ieq (Join-Path $InstallDir 'vnote.exe')) 'Refusing to clean up a fixture PID with a different executable'
        Stop-FixtureChild ([pscustomobject]@{ Process = $process; Label = 'recorded generation B' })
    }
    finally { $process.Dispose() }
}

function Wait-FixtureFile {
    param([string]$Path, $Child, [string]$Contains = '', [int]$Milliseconds = 10000)
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.ElapsedMilliseconds -lt $Milliseconds) {
        if ([IO.File]::Exists($Path)) {
            # Parent transcripts are appended while this readiness poll runs.
            $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
            $reader = [IO.StreamReader]::new($stream)
            try { $text = $reader.ReadToEnd() }
            finally { $reader.Dispose() }
            if ($Contains -eq '' -or $text.Contains($Contains)) { return $text }
        }
        if ($null -ne $Child -and $Child.Process.HasExited) {
            throw "$($Child.Label) exited before producing $Path"
        }
        Start-Sleep -Milliseconds 20
    }
    throw "Timed out waiting for fixture evidence: $Path"
}

function New-FixtureDirectory {
    param([string]$Label)
    $path = Join-Path $script:Root ($Label + ' ' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($path) | Out-Null
    return $path
}

function Get-FixtureSnapshot {
    param([string]$Root)
    $prefix = $Root.TrimEnd('\') + '\'
    $rows = @(Get-ChildItem -LiteralPath $Root -Recurse -Force | ForEach-Object {
        $relative = $_.FullName.Substring($prefix.Length).Replace('\', '/')
        if ($_.PSIsContainer) { 'D|' + $relative }
        else { 'F|' + $relative + '|' + $_.Length + '|' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    } | Sort-Object)
    return [string]::Join("`n", [string[]]$rows)
}

function Assert-FixtureSnapshot {
    param([string]$Root, [string]$Snapshot, [string]$Name)
    Assert-Fixture ((Get-FixtureSnapshot $Root) -ceq $Snapshot) "$Name changed the complete tree at $Root"
}

function Copy-FixtureTree {
    param([string]$Source, [string]$Destination)
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | Copy-Item -Destination $Destination -Recurse -Force
}

function New-FixtureInstall {
    param([string]$Variant = 'win64', [switch]$Conflict)
    $directory = New-FixtureDirectory 'installed A'
    if ($Conflict) {
        & (Join-Path $PSScriptRoot 'make-update-fixture.ps1') -Root $directory -Generation A -Variant $Variant
    }
    else {
        Copy-FixtureTree $script:Packages[$Variant].Tree $directory
        [IO.File]::Delete((Join-Path $directory 'manifest.json'))
    }
    [IO.File]::Copy($script:ExeA, (Join-Path $directory 'vnote.exe'), $true)
    $qt = if ($Variant -eq 'win64') { 'Qt6Core.dll' } else { 'Qt5Core.dll' }
    Write-FixtureText (Join-Path $directory $qt) 'generation A core'
    Write-FixtureText (Join-Path $directory 'config/app/sentinel.bin') "portable app`0A"
    Write-FixtureText (Join-Path $directory 'config/local/sentinel.bin') "portable local`0A"
    Write-FixtureText (Join-Path $directory 'unowned file.bin') "unowned`0keep me"
    Write-FixtureText (Join-Path $directory '.hidden-sentinel') 'hidden data'
    [IO.File]::SetAttributes((Join-Path $directory '.hidden-sentinel'), [IO.FileAttributes]::Hidden)
    [IO.Directory]::CreateDirectory((Join-Path $directory 'empty portable directory')) | Out-Null
    return $directory
}

function Add-FixtureRoute {
    param([string]$Path, [string]$File = '', [int]$Status = 200, [string]$Location = '',
        [bool]$Truncate = $false, [bool]$OmitLength = $false)
    $script:Routes[$Path] = @{ File = $File; Status = $Status; Location = $Location; Truncate = $Truncate; OmitLength = $OmitLength }
    Write-FixtureJson $script:RoutesPath $script:Routes
}

function Publish-FixtureArtifacts {
    param($Artifacts, [string]$Prefix)
    $result = @{}
    foreach ($kind in @('Zip', 'Manifest', 'Signature')) {
        $suffix = switch ($kind) { Zip { '.zip' } Manifest { '.manifest.json' } Signature { '.manifest.json.minisig' } }
        $path = '/' + $Prefix + '/package' + $suffix
        Add-FixtureRoute $path $Artifacts.$kind
        $result[$kind] = $script:BaseUrl + $path
    }
    return [pscustomobject]$result
}

function New-FixtureRelease {
    param([string]$Source, $Urls = $script:PackageUrls, [string]$OnlyVariant = '')
    $assets = New-Object 'Collections.Generic.List[object]'
    # Deliberately put misleading source/delta archives before either full ZIP.
    $assets.Add(@{ name = 'source-code.zip'; browser_download_url = $script:BaseUrl + '/not-a-package.zip' })
    $assets.Add(@{ name = 'VNote-9.1.0-win64.delta.zip'; browser_download_url = $script:BaseUrl + '/not-a-package.zip' })
    foreach ($variantName in @('win64-windows7', 'win64')) {
        if ($OnlyVariant -and $OnlyVariant -ne $variantName) { continue }
        foreach ($kind in @('Signature', 'Zip', 'Manifest')) {
            $suffix = switch ($kind) { Zip { '.zip' } Manifest { '.manifest.json' } Signature { '.manifest.json.minisig' } }
            $assets.Add(@{ name = "VNote-9.1.0-$variantName$suffix"; browser_download_url = $Urls[$variantName].$kind })
        }
    }
    $release = @{ tag_name = 'v9.1.0'; prerelease = $false; assets = $assets.ToArray() }
    if ($Source -eq 'github') { $release.draft = $false }
    return (ConvertFrom-Json -InputObject ($release | ConvertTo-Json -Depth 10))
}

function Publish-FixtureRelease {
    param([string]$Source, $Release)
    $path = Join-Path $script:Root ($Source + '-release.json')
    Write-FixtureJson $path $Release
    Add-FixtureRoute "/$Source/tags/v9.1.0" $path
}

function New-FixtureAttempt {
    $stage = New-FixtureDirectory 'private stage'
    $script:VNoteStageRoot = $stage
    return $stage
}

function Read-FixtureManifest {
    param($Artifacts, [string]$Variant = 'win64', [string[]]$Keys = @($script:PublicKey))
    return (Read-VerifiedVNoteManifest -Path $Artifacts.Manifest -SignaturePath $Artifacts.Signature `
        -MinisignPath $MinisignPath -Version '9.1.0' -CurrentVersion '9.0.0' -Variant $Variant -TrustedKeys $Keys)
}

function Expand-FixturePackage {
    param($Artifacts, [string]$Variant = 'win64')
    $manifest = Read-FixtureManifest $Artifacts $Variant
    $stage = New-FixtureAttempt
    $destination = Join-Path $stage 'extracted'
    [IO.Directory]::CreateDirectory($destination) | Out-Null
    $package = Expand-VerifiedVNotePackage -ArchivePath $Artifacts.Zip -Destination $destination -Manifest $manifest
    return [pscustomobject]@{ Manifest = $manifest; Directory = $package }
}

function Receive-FixturePackage {
    param([string]$Source, [string]$Variant)
    $stage = New-FixtureAttempt
    $release = Get-VNoteRelease -Source $Source -Version '9.1.0'
    $assets = Select-VNoteReleaseAssets -Release $release -Version '9.1.0' -Variant $Variant
    $manifestPath = Join-Path $stage 'release.manifest.json'
    $signaturePath = $manifestPath + '.minisig'
    Receive-VNoteFile -Url $assets.Manifest -Destination $manifestPath -Source $Source -MaxBytes 16777216
    Receive-VNoteFile -Url $assets.Signature -Destination $signaturePath -Source $Source -MaxBytes 65536
    $manifest = Read-FixtureManifest ([pscustomobject]@{ Manifest = $manifestPath; Signature = $signaturePath }) $Variant
    Assert-Fixture ($manifest.fullPackage.asset -ceq $assets.ZipName) 'Selected a different full ZIP than the signed asset'
    $zipPath = Join-Path $stage $assets.ZipName
    Receive-VNoteFile -Url $assets.Zip -Destination $zipPath -Source $Source -MaxBytes $manifest.fullPackage.size
    $destination = Join-Path $stage 'extracted'
    [IO.Directory]::CreateDirectory($destination) | Out-Null
    $package = Expand-VerifiedVNotePackage -ArchivePath $zipPath -Destination $destination -Manifest $manifest
    return [pscustomobject]@{ Manifest = $manifest; Directory = $package }
}

function Copy-FixtureArtifacts {
    param([string]$Name)
    $directory = New-FixtureDirectory $Name
    $original = $script:Packages.win64
    $result = @{}
    foreach ($kind in @('Zip', 'Manifest', 'Signature')) {
        $path = Join-Path $directory ([IO.Path]::GetFileName($original.$kind))
        [IO.File]::Copy($original.$kind, $path)
        $result[$kind] = $path
    }
    return [pscustomobject]$result
}

function Sign-FixtureManifest {
    param($Artifacts, $Manifest)
    Write-FixtureJson $Artifacts.Manifest $Manifest
    [IO.File]::Delete($Artifacts.Signature)
    Push-Location -LiteralPath ([IO.Path]::GetDirectoryName($Artifacts.Manifest))
    try {
        $key = Resolve-Path -LiteralPath $script:SecretKey -Relative
        & $MinisignPath -S -W -s $key -m ([IO.Path]::GetFileName($Artifacts.Manifest)) `
            -x ([IO.Path]::GetFileName($Artifacts.Signature)) -t 'VNote regression fixture' | Out-Null
        Assert-Fixture ($LASTEXITCODE -eq 0) 'Signing the mutated fixture failed'
    }
    finally { Pop-Location }
}

function Sign-FixtureArchive {
    param($Artifacts)
    $manifest = Read-FixtureJson $Artifacts.Manifest
    $manifest.fullPackage.size = [int64](Get-Item -LiteralPath $Artifacts.Zip).Length
    $manifest.fullPackage.sha256 = (Get-FileHash -LiteralPath $Artifacts.Zip -Algorithm SHA256).Hash.ToLowerInvariant()
    Sign-FixtureManifest $Artifacts $manifest
}

function Add-FixtureZipEntry {
    param($Artifacts, [string]$Name, [string]$Content = 'hostile payload', [int]$Attributes = 0)
    $zip = [IO.Compression.ZipFile]::Open($Artifacts.Zip, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $entry = $zip.CreateEntry($Name)
        $entry.ExternalAttributes = $Attributes
        $stream = $entry.Open()
        try {
            $bytes = $script:Utf8.GetBytes($Content)
            $stream.Write($bytes, 0, $bytes.Length)
        }
        finally { $stream.Dispose() }
    }
    finally { $zip.Dispose() }
    Sign-FixtureArchive $Artifacts
}

function Start-FixtureParent {
    param([string]$InstallDir, [string]$Mode = 'accept', [int]$HelperPid = $PID)
    $id = [Guid]::NewGuid().ToString('N')
    $log = Join-Path $script:Root ($id + '-parent.log')
    $helperFile = Join-Path $script:Root ($id + '-helper.pid')
    if ($HelperPid -gt 0) { Write-FixtureText $helperFile ([string]$HelperPid) }
    $pipe = 'vnote-update-' + $id
    $token = [Guid]::NewGuid().ToString('N')
    $child = Start-FixtureProcess (Join-Path $InstallDir 'vnote.exe') @('--parent', $pipe, $token, $log, $Mode, $helperFile) ('parent-' + $id) $InstallDir
    $null = Wait-FixtureFile $log $child 'LISTENING'
    return [pscustomobject]@{ Child = $child; Pipe = $pipe; Token = $token; Log = $log; HelperFile = $helperFile }
}

function Connect-FixtureParent {
    param([string]$InstallDir, $Parent)
    return (Connect-VNoteParent -InstallDir $InstallDir -ParentProcessId $Parent.Child.Process.Id -PipeName $Parent.Pipe -Token $Parent.Token)
}

function Assert-NoFixtureReady {
    param($Parent)
    Assert-Fixture (-not ([IO.File]::ReadAllText($Parent.Log).Contains("READY`n"))) 'Refused preparation nevertheless requested shutdown'
    Assert-Fixture (-not $Parent.Child.Process.HasExited) 'Refused preparation stopped the original VNote process'
}

function Expect-FixturePreparationFailure {
    param([string]$Name, [scriptblock]$Preparation)
    $message = Expect-FixtureFailure {
        $null = & $Preparation
        # Deliberately continue into the real handshake if a broken validator
        # accepts the fixture. The peer transcript then makes that regression fail.
        Request-VNoteShutdown -Session $script:NegativeSession
    } $Name
    Assert-NoFixtureReady $script:NegativeParent
    Assert-FixtureSnapshot $script:NegativeInstall $script:NegativeSnapshot $Name
    Assert-FixtureSnapshot $script:Outside $script:OutsideSnapshot "$Name outside destination"
    Pass-Fixture $Name
}

function Assert-FixtureOverlay {
    param([string]$InstallDir, [string]$Backup, [string]$Before, $Manifest)
    Assert-FixtureSnapshot $Backup $Before 'Recovery backup'
    foreach ($file in $Manifest.files) {
        $path = Join-Path $InstallDir $file.path
        Assert-Fixture ((Get-Item -LiteralPath $path).Length -eq $file.size) "Installed size mismatch: $($file.path)"
        Assert-Fixture ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ieq $file.sha256) "Installed hash mismatch: $($file.path)"
    }
    foreach ($relative in @('config/app/sentinel.bin', 'config/local/sentinel.bin', 'unowned file.bin', '.hidden-sentinel')) {
        Assert-Fixture ((Get-FileHash -LiteralPath (Join-Path $InstallDir $relative)).Hash -eq
            (Get-FileHash -LiteralPath (Join-Path $Backup $relative)).Hash) "Overlay altered portable/unowned bytes: $relative"
    }
    Assert-Fixture ([IO.Directory]::Exists((Join-Path $InstallDir 'empty portable directory'))) 'Overlay lost an empty unowned directory'
    Assert-Fixture (([IO.File]::GetAttributes((Join-Path $InstallDir '.hidden-sentinel')) -band [IO.FileAttributes]::Hidden) -ne 0) 'Overlay lost hidden-file attributes'
}

# These are real console executables, not text payloads pretending to be VNote.
# The generation marker proves Main ran, unlike a mere live PID/loader dialog.
$fixtureProgram = @'
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.Win32.SafeHandles;
public static class Fixture__GENERATION__ {
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetNamedPipeClientProcessId(SafePipeHandle pipe, out uint pid);
    static string ReadFrame(Stream stream) {
        var text = new StringBuilder();
        for (int index=0; index<256; ++index) {
            int b=stream.ReadByte();
            if (b<0) throw new IOException("client disconnected");
            if (b==10) return text.ToString();
            if (b<32 || b>126) throw new IOException("invalid frame");
            text.Append((char)b);
        }
        throw new IOException("oversized frame");
    }
    static void Send(Stream stream, string text) {
        byte[] bytes=Encoding.ASCII.GetBytes(text);
        stream.Write(bytes,0,bytes.Length); stream.Flush();
    }
    public static int Main(string[] args) {
        try {
            string root=AppDomain.CurrentDomain.BaseDirectory;
            if (args.Length==0) {
                File.WriteAllText(Path.Combine(root,".fixture-launched"), "__GENERATION__|"+Process.GetCurrentProcess().Id);
                string mode=Path.Combine(root,".fixture-relaunch-mode");
                if (File.Exists(mode)) {
                    string value=File.ReadAllText(mode);
                    if (value=="exit") return 0;
                    if (value=="handoff") {
                        string endpoint=File.ReadAllText(Path.Combine(root,".fixture-handoff-pipe"));
                        using (var client=new NamedPipeClientStream(".",endpoint,PipeDirection.InOut)) {
                            client.Connect(5000); Send(client,"ACTIVATE\n");
                            if (ReadFrame(client)!="ACK") return 26;
                        }
                        return 0;
                    }
                }
                Thread.Sleep(120000); return 0;
            }
            if (args.Length==3 && args[0]=="--handoff-peer") {
                using (var server=new NamedPipeServerStream(args[1],PipeDirection.InOut)) {
                    File.AppendAllText(args[2],"LISTENING\n");
                    server.WaitForConnection();
                    if (ReadFrame(server)!="ACTIVATE") return 27;
                    File.AppendAllText(args[2],"HANDOFF\n"); Send(server,"ACK\n");
                    Thread.Sleep(120000); return 0;
                }
            }
            if (args.Length!=6 || args[0]!="--parent") return 21;
            string log=args[3], behavior=args[4];
            using (var pipe=new NamedPipeServerStream(args[1],PipeDirection.InOut,1,
                       PipeTransmissionMode.Byte,PipeOptions.Asynchronous)) {
                File.AppendAllText(log,"LISTENING\n");
                pipe.WaitForConnection();
                var watch=Stopwatch.StartNew();
                while (!File.Exists(args[5]) && watch.ElapsedMilliseconds<5000) Thread.Sleep(5);
                uint actual;
                if (!GetNamedPipeClientProcessId(pipe.SafePipeHandle,out actual) ||
                    actual!=UInt32.Parse(File.ReadAllText(args[5]))) return 22;
                File.AppendAllText(log,"CLIENT "+actual+"\n");
                if (ReadFrame(pipe)!="HELLO "+args[2]) return 23;
                File.AppendAllText(log,"HELLO\n");
                Send(pipe,"O"); Send(pipe,"K\n");
                if (behavior=="eof-preparing") return 0;
                string ready;
                try { ready=ReadFrame(pipe); }
                catch (IOException) {
                    // Failed preparation disconnects the helper, not the app.
                    File.AppendAllText(log,"HELPER DISCONNECTED\n");
                    Thread.Sleep(120000); return 0;
                }
                if (ready!="READY "+args[2]) return 24;
                File.AppendAllText(log,"READY\n");
                if (behavior=="cancel") {
                    Send(pipe,"CANCELLED\n"); Thread.Sleep(120000); return 0;
                }
                if (behavior=="eof") { Send(pipe,"ACCEPT"); return 0; }
                if (behavior=="no-response") { Thread.Sleep(120000); return 0; }
                Send(pipe,"ACCEPTED\n");
                pipe.WaitForPipeDrain();
                if (behavior=="exit-timeout") { Thread.Sleep(120000); return 0; }
                File.WriteAllText(Path.Combine(root,"config","local","saved-on-close.bin"),"saved after ACCEPTED");
                File.AppendAllText(log,"SAVED\n");
                return 0;
            }
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 25; }
    }
}
'@

try {
    Write-Host "Fixture root: $script:Root"
    $script:Outside = New-FixtureDirectory 'outside extraction'
    Write-FixtureText (Join-Path $script:Outside 'sentinel') 'outside destination must remain unchanged'
    $script:OutsideSnapshot = Get-FixtureSnapshot $script:Outside
    $script:SecretKey = Join-Path $script:Root 'ephemeral.key'
    $publicFile = Join-Path $script:Root 'ephemeral.pub'
    # minisign uses narrow file APIs: retain the Unicode root as its CWD, not argv.
    Push-Location -LiteralPath $script:Root
    try {
        & $MinisignPath -G -W -s 'ephemeral.key' -p 'ephemeral.pub' | Out-Null
        Assert-Fixture ($LASTEXITCODE -eq 0) 'Could not generate an ephemeral signing key'
        & $MinisignPath -G -W -s 'wrong.key' -p 'wrong.pub' | Out-Null
        Assert-Fixture ($LASTEXITCODE -eq 0) 'Could not generate an unrelated signing key'
    }
    finally { Pop-Location }
    $script:PublicKey = [IO.File]::ReadAllLines($publicFile)[1]
    $wrongKey = [IO.File]::ReadAllLines((Join-Path $script:Root 'wrong.pub'))[1]
    $compiledA = New-FixtureDirectory 'compiled A'
    $compiledB = New-FixtureDirectory 'compiled B'
    $script:ExeA = Join-Path $compiledA 'vnote.exe'
    $exeB = Join-Path $compiledB 'vnote.exe'
    Add-Type -TypeDefinition ($fixtureProgram.Replace('__GENERATION__', 'A')) -OutputAssembly $script:ExeA -OutputType ConsoleApplication
    Add-Type -TypeDefinition ($fixtureProgram.Replace('__GENERATION__', 'B')) -OutputAssembly $exeB -OutputType ConsoleApplication
    $script:Packages = @{}
    foreach ($variantName in @('win64', 'win64-windows7')) {
        $packageParent = New-FixtureDirectory ('package ' + $variantName)
        $name = 'VNote-9.1.0-' + $variantName
        $tree = Join-Path $packageParent $name
        & (Join-Path $PSScriptRoot 'make-update-fixture.ps1') -Root $tree -Generation B -Variant $variantName
        [IO.File]::Copy($exeB, (Join-Path $tree 'vnote.exe'), $true)
        & (Join-Path $PSScriptRoot '../gen-update-package.ps1') -ExtractedDir $tree -Version '9.1.0' `
            -Variant $variantName -Commit ('a' * 40) -Channel stable -OutputDir $packageParent `
            -MinisignSecretKey $script:SecretKey -SkipDelta
        $manifest = Join-Path $packageParent ($name + '.manifest.json')
        $script:Packages[$variantName] = [pscustomobject]@{
            Tree = $tree; Zip = (Join-Path $packageParent ($name + '.zip')); Manifest = $manifest; Signature = $manifest + '.minisig'
        }
    }
    $script:VNoteInstallRoot = $script:Packages.win64.Tree
    $httpReady = Join-Path $script:Root 'http ready.json'
    $httpLog = Join-Path $script:Root 'http requests.txt'
    Write-FixtureJson $script:RoutesPath @{}
    $server = Start-FixtureMode Http @{ Routes = $script:RoutesPath; Ready = $httpReady; Log = $httpLog } 'http'
    $null = Wait-FixtureFile $httpReady $server
    $port = [int](Read-FixtureJson $httpReady).Port
    $script:BaseUrl = 'http://127.0.0.1:' + $port
    foreach ($sourceName in @('github', 'gitee')) {
        $script:VNoteOrigins[$sourceName] = @{
            ApiBaseUrl = "$script:BaseUrl/$sourceName/tags/"; Scheme = 'http'; Port = $port
            Hosts = @('127.0.0.1'); HostSuffixes = @(); Accept = 'application/json'
        }
    }
    $script:PackageUrls = @{}
    foreach ($variantName in @('win64', 'win64-windows7')) {
        $script:PackageUrls[$variantName] = Publish-FixtureArtifacts $script:Packages[$variantName] $variantName
    }
    foreach ($sourceName in @('github', 'gitee')) { Publish-FixtureRelease $sourceName (New-FixtureRelease $sourceName) }

    foreach ($sourceName in @('github', 'gitee')) {
        foreach ($variantName in @('win64', 'win64-windows7')) {
            $prepared = Receive-FixturePackage $sourceName $variantName
            $install = New-FixtureInstall $variantName
            $before = Get-FixtureSnapshot $install
            Assert-VNoteInstallReady -InstallDir $install -Manifest $prepared.Manifest -PackageDir $prepared.Directory -PreparingDownload
            $backup = Install-VNotePackage -InstallDir $install -PackageDir $prepared.Directory -Manifest $prepared.Manifest
            Assert-FixtureOverlay $install $backup $before $prepared.Manifest
            Pass-Fixture "$sourceName-shaped shuffled API / $variantName signed overlay and complete backup"
        }
    }

    # A shared *real* authenticated peer proves every preparation rejection below
    # leaves both the original process and its installed bytes alone.
    $script:NegativeInstall = New-FixtureInstall
    $script:NegativeParent = Start-FixtureParent $script:NegativeInstall
    $script:NegativeSession = Connect-FixtureParent $script:NegativeInstall $script:NegativeParent
    $script:NegativeSnapshot = Get-FixtureSnapshot $script:NegativeInstall
    $script:VNotePreparationGuard = { Assert-VNoteParentPreparing -Session $script:NegativeSession }
    foreach ($fault in @('missing', 'duplicate', 'wrong-name', 'wrong-tag', 'wrong-variant', 'empty-url', 'draft', 'prerelease')) {
        $release = New-FixtureRelease github
        switch ($fault) {
            missing { $release.assets = @($release.assets | Where-Object { $_.name -cne 'VNote-9.1.0-win64.manifest.json.minisig' }) }
            duplicate { $release.assets += @($release.assets | Where-Object { $_.name -ceq 'VNote-9.1.0-win64.zip' }) }
            wrong-name { ($release.assets | Where-Object { $_.name -ceq 'VNote-9.1.0-win64.zip' }).name = 'VNote-9.2.0-win64.zip' }
            wrong-tag { $release.tag_name = 'v9.2.0' }
            wrong-variant { $release = New-FixtureRelease github -OnlyVariant 'win64-windows7' }
            empty-url { ($release.assets | Where-Object { $_.name -ceq 'VNote-9.1.0-win64.zip' }).browser_download_url = '' }
            draft { $release.draft = $true }
            prerelease { $release.prerelease = $true }
        }
        Publish-FixtureRelease github $release
        Expect-FixturePreparationFailure "API $fault refuses before READY" { Receive-FixturePackage github win64 }
    }
    Publish-FixtureRelease github (New-FixtureRelease github)
    $tampered = Copy-FixtureArtifacts 'one byte manifest tamper'
    $bytes = [IO.File]::ReadAllBytes($tampered.Manifest)
    $bytes[0] = $bytes[0] -bxor 1
    [IO.File]::WriteAllBytes($tampered.Manifest, $bytes)
    Expect-FixturePreparationFailure 'one-byte signed manifest tamper' { Expand-FixturePackage $tampered }
    $badComment = Copy-FixtureArtifacts 'trusted comment tamper'
    $comment = [IO.File]::ReadAllText($badComment.Signature)
    Write-FixtureText $badComment.Signature ([regex]::Replace($comment, '(?m)^trusted comment:.*', 'trusted comment: attacker changed this'))
    Expect-FixturePreparationFailure 'trusted-comment signature tamper' { Expand-FixturePackage $badComment }
    Expect-FixturePreparationFailure 'untrusted signing key' { Read-FixtureManifest $script:Packages.win64 win64 @($wrongKey) }
    Expect-FixturePreparationFailure 'signed variant mismatch' { Read-FixtureManifest $script:Packages.win64 'win64-windows7' }
    $hashMismatch = Copy-FixtureArtifacts 'zip hash mismatch'
    $bytes = [IO.File]::ReadAllBytes($hashMismatch.Zip)
    $bytes[20] = $bytes[20] -bxor 1
    [IO.File]::WriteAllBytes($hashMismatch.Zip, $bytes)
    Expect-FixturePreparationFailure 'ZIP hash mismatch' { Expand-FixturePackage $hashMismatch }
    $lengthMismatch = Copy-FixtureArtifacts 'zip length mismatch'
    $stream = [IO.File]::OpenWrite($lengthMismatch.Zip)
    try { $stream.SetLength($stream.Length - 1) } finally { $stream.Dispose() }
    Expect-FixturePreparationFailure 'ZIP signed length mismatch' { Expand-FixturePackage $lengthMismatch }
    Add-FixtureRoute '/truncated.zip' $script:Packages.win64.Zip -Truncate $true
    $truncatedPath = Join-Path (New-FixtureAttempt) 'truncated.zip'
    Expect-FixturePreparationFailure 'HTTP truncated stream' {
        Receive-VNoteFile -Url "$script:BaseUrl/truncated.zip" -Destination $truncatedPath -Source github `
            -MaxBytes ([int64](Get-Item -LiteralPath $script:Packages.win64.Zip).Length)
    }
    Assert-Fixture (-not [IO.File]::Exists($truncatedPath)) 'Truncated download was retained'
    Add-FixtureRoute '/unbounded.zip' $script:Packages.win64.Zip -OmitLength $true
    $overrunPath = Join-Path (New-FixtureAttempt) 'overrun.zip'
    Expect-FixturePreparationFailure 'HTTP streaming signed-size overrun' {
        Receive-VNoteFile -Url "$script:BaseUrl/unbounded.zip" -Destination $overrunPath -Source github -MaxBytes 64
    }
    Assert-Fixture (-not [IO.File]::Exists($overrunPath)) 'Over-limit download was retained'
    $hostileEntries = @(
        @{ Name = 'undeclared executable'; Path = 'VNote-9.1.0-win64/evil.exe'; Attributes = 0 },
        @{ Name = 'traversal'; Path = 'VNote-9.1.0-win64/../../escape.bin'; Attributes = 0 },
        @{ Name = 'ADS'; Path = 'VNote-9.1.0-win64/vnote.exe:payload'; Attributes = 0 },
        @{ Name = 'reserved device'; Path = 'VNote-9.1.0-win64/CON.txt'; Attributes = 0 },
        @{ Name = 'duplicate case'; Path = 'VNote-9.1.0-win64/VNOTE.EXE'; Attributes = 0 },
        @{ Name = 'wrong wrapper'; Path = 'another-wrapper/escape.bin'; Attributes = 0 },
        @{ Name = 'absolute path'; Path = (Join-Path $script:Outside 'escaped.exe').Replace('\', '/').Substring(2); Attributes = 0 },
        @{ Name = 'backslash traversal'; Path = 'VNote-9.1.0-win64/..\escape.bin'; Attributes = 0 },
        @{ Name = 'trailing-dot alias'; Path = 'VNote-9.1.0-win64/payload.'; Attributes = 0 },
        @{ Name = 'reparse metadata'; Path = 'VNote-9.1.0-win64/link'; Attributes = 1024 },
        @{ Name = 'Unix symlink'; Path = 'VNote-9.1.0-win64/symlink'; Attributes = [BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]::Parse('A1FF0000', [Globalization.NumberStyles]::HexNumber)), 0) },
        @{ Name = 'portable config'; Path = 'VNote-9.1.0-win64/config/app/sentinel.bin'; Attributes = 0 },
        @{ Name = 'file-directory collision'; Path = 'VNote-9.1.0-win64/vnote.exe/child'; Attributes = 0 }
    )
    foreach ($hostile in $hostileEntries) {
        $artifacts = Copy-FixtureArtifacts ('hostile ' + $hostile.Name)
        Add-FixtureZipEntry $artifacts $hostile.Path -Attributes $hostile.Attributes
        Expect-FixturePreparationFailure ("signed hostile ZIP: " + $hostile.Name) { Expand-FixturePackage $artifacts }
        Assert-Fixture (-not [IO.File]::Exists((Join-Path $script:Root 'escape.bin'))) 'ZIP traversal escaped its extraction directory'
    }
    $configManifest = Copy-FixtureArtifacts 'signed config manifest'
    $manifest = Read-FixtureJson $configManifest.Manifest
    $manifest.files += [pscustomobject]@{ path = 'config/app/sentinel.bin'; size = 1; sha256 = ('0' * 64) }
    Sign-FixtureManifest $configManifest $manifest
    Expect-FixturePreparationFailure 'signed manifest cannot own portable config' { Expand-FixturePackage $configManifest }
    $fileHash = Copy-FixtureArtifacts 'file hash mismatch'
    $zip = [IO.Compression.ZipFile]::Open($fileHash.Zip, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $entry = $zip.GetEntry('VNote-9.1.0-win64/Qt6Core.dll')
        $stream = $entry.Open()
        try { $first = $stream.ReadByte(); $stream.Position = 0; $stream.WriteByte([byte]($first -bxor 1)) }
        finally { $stream.Dispose() }
    }
    finally { $zip.Dispose() }
    Sign-FixtureArchive $fileHash
    Expect-FixturePreparationFailure 'signed ZIP with wrong per-file hash' { Expand-FixturePackage $fileHash }
    $valid = Expand-FixturePackage $script:Packages.win64
    Expect-FixturePreparationFailure 'extraction destination outside private stage' {
        Expand-VerifiedVNotePackage -ArchivePath $script:Packages.win64.Zip -Destination $script:Outside -Manifest $valid.Manifest
    }
    $conflictInstall = New-FixtureInstall -Conflict
    $conflictBefore = Get-FixtureSnapshot $conflictInstall
    Expect-FixturePreparationFailure 'fixture A plugins file versus fixture B directory' {
        Assert-VNoteInstallReady -InstallDir $conflictInstall -Manifest $valid.Manifest -PackageDir $valid.Directory
    }
    Assert-FixtureSnapshot $conflictInstall $conflictBefore 'Structural collision refusal'
    $readonly = Join-Path $script:NegativeInstall 'Qt6Core.dll'
    [IO.File]::SetAttributes($readonly, [IO.FileAttributes]::ReadOnly)
    try {
        Expect-FixturePreparationFailure 'read-only owned payload' {
            Assert-VNoteInstallReady -InstallDir $script:NegativeInstall -Manifest $valid.Manifest -PackageDir $valid.Directory
        }
    }
    finally { [IO.File]::SetAttributes($readonly, [IO.FileAttributes]::Normal) }
    $denied = New-FixtureInstall
    $deniedBefore = Get-FixtureSnapshot $denied
    $originalAcl = Get-Acl -LiteralPath $denied
    $script:AclRestores.Add([pscustomobject]@{ Path = $denied; Acl = $originalAcl })
    $deniedAcl = Get-Acl -LiteralPath $denied
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User
    $rights = [Security.AccessControl.FileSystemRights]::CreateFiles -bor [Security.AccessControl.FileSystemRights]::CreateDirectories -bor [Security.AccessControl.FileSystemRights]::Delete
    $deny = New-Object Security.AccessControl.FileSystemAccessRule($sid, $rights, [Security.AccessControl.AccessControlType]::Deny)
    $deniedAcl.AddAccessRule($deny)
    Set-Acl -LiteralPath $denied -AclObject $deniedAcl
    try {
        $null = Expect-FixtureFailure {
            $probe = [IO.File]::Open((Join-Path $denied 'actual-denied-write'), [IO.FileMode]::CreateNew)
            $probe.Dispose()
        } 'ACL fixture really denies writes'
        Expect-FixturePreparationFailure 'real denied-write directory ACL' {
            Assert-VNoteInstallReady -InstallDir $denied -Manifest $valid.Manifest -PackageDir $valid.Directory
        }
    }
    finally { Set-Acl -LiteralPath $denied -AclObject $originalAcl }
    Assert-FixtureSnapshot $denied $deniedBefore 'Denied-write installation'
    $junction = Join-Path $script:NegativeInstall 'linked user directory'
    New-Item -ItemType Junction -Path $junction -Target $script:Outside | Out-Null
    $script:Junctions.Add($junction)
    try {
        $null = Expect-FixtureFailure {
            Assert-VNoteInstallReady -InstallDir $script:NegativeInstall -Manifest $valid.Manifest -PackageDir $valid.Directory
        } 'Install subtree reparse point'
        $null = Expect-FixtureFailure { Get-VNoteInstallRoot -InstallDir $junction } 'Install root reparse point'
        Assert-NoFixtureReady $script:NegativeParent
        Assert-FixtureSnapshot $script:Outside $script:OutsideSnapshot 'Junction target'
    }
    finally { [IO.Directory]::Delete($junction) }
    Assert-FixtureSnapshot $script:NegativeInstall $script:NegativeSnapshot 'Reparse refusal'
    Pass-Fixture 'install-root/subtree junctions never followed'
    $script:VNotePreparationGuard = $null
    Close-VNoteParentSession -Session $script:NegativeSession
    $script:NegativeSession = $null
    Stop-FixtureChild $script:NegativeParent.Child

    $lockedInstall = New-FixtureInstall
    $lockedBefore = Get-FixtureSnapshot $lockedInstall
    $lock = [IO.File]::Open((Join-Path $lockedInstall 'Qt6Core.dll'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    $savedLockTimeout = $script:VNoteLockTimeoutMilliseconds
    try {
        $script:VNoteLockTimeoutMilliseconds = 150
        $null = Expect-FixtureFailure { Wait-VNoteInstallUnlocked -InstallDir $lockedInstall } 'exclusive DLL lock'
        $null = Expect-FixtureFailure {
            Install-VNotePackage -InstallDir $lockedInstall -PackageDir $valid.Directory -Manifest $valid.Manifest
        } 'locked installation cannot partially replace'
    }
    finally { $script:VNoteLockTimeoutMilliseconds = $savedLockTimeout; $lock.Dispose() }
    Assert-FixtureSnapshot $lockedInstall $lockedBefore 'Exclusive file lock'
    Pass-Fixture 'exclusive loader lock leaves original tree intact'

    # A mapped image can have no open file handle: Read+ShareNone still succeeds.
    # The production probe must request write access without changing any bytes.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class VNoteMappedImageFixture {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr module);
}
'@
    $mappedInstall = New-FixtureInstall
    $mappedExe = Join-Path $mappedInstall 'mapped-image.exe'
    [IO.File]::Copy($MinisignPath, $mappedExe)
    $mappedBefore = Get-FixtureSnapshot $mappedInstall
    $module = [VNoteMappedImageFixture]::LoadLibraryEx($mappedExe, [IntPtr]::Zero, 1)
    Assert-Fixture ($module -ne [IntPtr]::Zero) 'Could not create a real mapped-image fixture'
    $savedLockTimeout = $script:VNoteLockTimeoutMilliseconds
    try {
        $script:VNoteLockTimeoutMilliseconds = 150
        $null = Expect-FixtureFailure { Wait-VNoteInstallUnlocked -InstallDir $mappedInstall } 'mapped executable image'
    }
    finally {
        $script:VNoteLockTimeoutMilliseconds = $savedLockTimeout
        [void][VNoteMappedImageFixture]::FreeLibrary($module)
    }
    Wait-VNoteInstallUnlocked -InstallDir $mappedInstall
    Assert-FixtureSnapshot $mappedInstall $mappedBefore 'Mapped image lock'
    Pass-Fixture 'mapped image blocks replacement until unloaded without altering bytes'
    $rollbackInstall = New-FixtureInstall
    $rollbackBefore = Get-FixtureSnapshot $rollbackInstall
    $originalMove = (Get-Item Function:Move-VNoteDirectory).ScriptBlock
    try {
        $script:FixtureRenameCalls = 0
        function script:Move-VNoteDirectory {
            param([string]$Source, [string]$Destination)
            ++$script:FixtureRenameCalls
            if ($script:FixtureRenameCalls -eq 2) { throw 'Injected second-rename failure' }
            [IO.Directory]::Move($Source, $Destination)
        }
        $message = Expect-FixtureFailure {
            Install-VNotePackage -InstallDir $rollbackInstall -PackageDir $valid.Directory -Manifest $valid.Manifest
        } 'second directory rename'
        Assert-Fixture ($message.Contains('Injected second-rename failure')) 'Rollback fixture failed before the injected rename error'
    }
    finally { Set-Item Function:script:Move-VNoteDirectory -Value $originalMove }
    Assert-FixtureSnapshot $rollbackInstall $rollbackBefore 'Second-rename rollback'
    Pass-Fixture 'second-rename failure restores complete original tree'

    foreach ($mode in @('cancel', 'eof', 'eof-preparing', 'no-response', 'exit-timeout')) {
        $install = New-FixtureInstall
        $before = Get-FixtureSnapshot $install
        $parent = Start-FixtureParent $install $mode
        $session = $null
        $savedClose = $script:VNoteCloseTimeoutMilliseconds
        $savedExit = $script:VNoteExitTimeoutMilliseconds
        try {
            $session = Connect-FixtureParent $install $parent
            $script:VNoteCloseTimeoutMilliseconds = 150
            $script:VNoteExitTimeoutMilliseconds = 150
            if ($mode -eq 'eof-preparing') {
                Assert-Fixture ($parent.Child.Process.WaitForExit(5000)) 'EOF fixture did not exit'
                $null = Expect-FixtureFailure { Assert-VNoteParentPreparing -Session $session } 'parent EOF during preparation'
                Assert-Fixture (-not $session.Accepted) 'EOF authorized an installation'
            }
            else {
                $null = Expect-FixtureFailure {
                    Request-VNoteShutdown -Session $session
                    Wait-VNoteParentExit -Session $session
                    Install-VNotePackage -InstallDir $install -PackageDir $valid.Directory -Manifest $valid.Manifest
                } ("parent " + $mode)
            }
            Assert-FixtureSnapshot $install $before ("Parent " + $mode)
            if ($mode -in @('cancel', 'no-response', 'exit-timeout')) {
                Assert-Fixture (-not $parent.Child.Process.HasExited) 'Updater terminated a refusing or draining parent'
            }
            Pass-Fixture "parent $mode cannot authorize replacement"
        }
        finally {
            $script:VNoteCloseTimeoutMilliseconds = $savedClose
            $script:VNoteExitTimeoutMilliseconds = $savedExit
            Close-VNoteParentSession -Session $session
            Stop-FixtureChild $parent.Child
        }
    }

    $mutexInstall = New-FixtureInstall
    $otherInstall = New-FixtureInstall
    $otherBefore = Get-FixtureSnapshot $otherInstall
    $otherParent = Start-FixtureParent $otherInstall 'cancel'
    $mutex = Enter-VNoteUpdateMutex -InstallDir $mutexInstall
    try {
        $sameResult = Join-Path $script:Root 'same mutex.txt'
        $otherResult = Join-Path $script:Root 'other mutex.txt'
        $same = Start-FixtureMode Mutex @{ Updater = $script:UpdaterScript; InstallDir = $mutexInstall.ToUpperInvariant(); Result = $sameResult } 'same-mutex'
        $different = Start-FixtureMode Mutex @{ Updater = $script:UpdaterScript; InstallDir = $otherInstall; Result = $otherResult } 'different-mutex'
        $null = Wait-FixtureExit $same 7
        $null = Wait-FixtureExit $different 0
        Assert-Fixture ([IO.File]::ReadAllText($sameResult).StartsWith('refused:')) 'Concurrent same-install updater entered'
        Assert-Fixture ([IO.File]::ReadAllText($otherResult) -ceq 'entered') 'Different installation mutex was blocked'
        Assert-VNoteNoTargetProcesses -InstallDir $mutexInstall
        Assert-Fixture (-not $otherParent.Child.Process.HasExited) 'Another installation was affected'
        Assert-FixtureSnapshot $otherInstall $otherBefore 'Other installation'
    }
    finally { $mutex.ReleaseMutex(); $mutex.Dispose() }
    $retry = Start-FixtureMode Mutex @{ Updater = $script:UpdaterScript; InstallDir = $mutexInstall; Result = $sameResult } 'mutex-retry'
    $null = Wait-FixtureExit $retry 0
    Stop-FixtureChild $otherParent.Child
    Pass-Fixture 'real cross-process mutex exclusion, case folding, other-install isolation and retry'

    # Policy checks do not use the loopback exception: the production function is
    # hardcoded HTTPS/443 and source-scoped even after fixture table substitution.
    foreach ($case in @(
        @('github', 'http://github.com/asset'), @('github', 'https://github.com:444/asset'),
        @('github', 'https://githubusercontent.com.evil.invalid/asset'),
        @('github', 'https://evilgithubusercontent.com/asset'), @('github', 'https://gitee.com/asset'),
        @('gitee', 'https://github.com/asset'), @('gitee', 'https://gitee.com.evil.invalid/asset'),
        @('github', 'https://user@github.com/asset'), @('github', 'https://github.com/asset#fragment')
    )) {
        $null = Expect-FixtureFailure { Assert-VNoteProductionUrl -Source $case[0] -Url $case[1] } ('source policy ' + $case[1])
    }
    foreach ($case in @(@('github', 'https://release-assets.githubusercontent.com/asset?signature=a%2Fb'), @('gitee', 'https://foruda.gitee.com/asset'))) {
        $uri = Assert-VNoteProductionUrl -Source $case[0] -Url $case[1]
        Assert-Fixture ($uri.AbsoluteUri -ceq $case[1]) 'Production source policy changed a signed URL'
    }
    Add-FixtureRoute '/redirect.zip' -Status 302 -Location '/signed.zip?token=a%2Fb&part=1'
    Add-FixtureRoute '/signed.zip?token=a%2Fb&part=1' $script:Packages.win64.Zip
    $redirected = Join-Path (New-FixtureAttempt) 'redirected.zip'
    Receive-VNoteFile -Url "$script:BaseUrl/redirect.zip" -Destination $redirected -Source github -MaxBytes $valid.Manifest.fullPackage.size
    Assert-Fixture ((Get-FileHash -LiteralPath $redirected).Hash -ieq $valid.Manifest.fullPackage.sha256) 'Relative signed-query redirect changed bytes'
    Assert-Fixture ([IO.File]::ReadAllText($httpLog).Contains('/signed.zip?token=a%2Fb&part=1')) 'Redirect discarded its signed query'
    Add-FixtureRoute '/loop.zip' -Status 302 -Location '/loop.zip'
    $null = Expect-FixtureFailure {
        Receive-VNoteFile -Url "$script:BaseUrl/loop.zip" -Destination (Join-Path (New-FixtureAttempt) 'loop.zip') -Source github -MaxBytes $valid.Manifest.fullPackage.size
    } 'bounded redirect loop'
    foreach ($remote in @('http://github.com/asset.zip', 'https://gitee.com/asset.zip', 'https://github.com.evil.invalid/asset.zip')) {
        Add-FixtureRoute '/escape.zip' -Status 302 -Location $remote
        $null = Expect-FixtureFailure {
            Receive-VNoteFile -Url "$script:BaseUrl/escape.zip" -Destination (Join-Path (New-FixtureAttempt) 'escape.zip') -Source github -MaxBytes $valid.Manifest.fullPackage.size
        } 'redirect origin escape'
    }
    Pass-Fixture 'production origin policy, signed-query redirect, hop bound and redirect escape refusal'

    # Full external-helper success: copy precisely the deployed helper files, then
    # dot-source that copy in a child. Its own Invoke performs HELLO, preparation,
    # READY/ACCEPTED, retained-handle wait, swap and five-second observed relaunch.
    $helperDirectory = New-FixtureDirectory 'copied deployed helper'
    $helperScript = Join-Path $helperDirectory 'update-vnote.ps1'
    [IO.File]::Copy($script:UpdaterScript, $helperScript)
    [IO.File]::Copy($MinisignPath, (Join-Path $helperDirectory 'minisign.exe'))
    $license = Join-Path ([IO.Path]::GetDirectoryName($MinisignPath)) 'LICENSE.minisign'
    Assert-Fixture ([IO.File]::Exists($license)) 'Prepared verifier license is missing'
    [IO.File]::Copy($license, (Join-Path $helperDirectory 'LICENSE.minisign'))
    $lifecycleInstall = New-FixtureInstall
    $lifecycleBefore = Get-FixtureSnapshot $lifecycleInstall
    $script:RelaunchRoots.Add($lifecycleInstall)
    $stoppedExpected = New-FixtureDirectory 'expected stopped installation'
    Copy-FixtureTree $lifecycleInstall $stoppedExpected
    Write-FixtureText (Join-Path $stoppedExpected 'config/local/saved-on-close.bin') 'saved after ACCEPTED'
    $stoppedSnapshot = Get-FixtureSnapshot $stoppedExpected
    $badUrls = @{} + $script:PackageUrls
    $badUrls.win64 = Publish-FixtureArtifacts $tampered 'tampered-lifecycle'
    Publish-FixtureRelease github (New-FixtureRelease github $badUrls)
    $parent = Start-FixtureParent $lifecycleInstall 'accept' 0
    $attempt = @{
        Updater = $helperScript; BaseUrl = $script:BaseUrl; Port = $port; PublicKey = $script:PublicKey
        Source = 'github'; Variant = 'win64'; InstallDir = $lifecycleInstall
        ParentProcessId = $parent.Child.Process.Id; PipeName = $parent.Pipe; Token = $parent.Token
    }
    $helper = Start-FixtureMode Update $attempt 'failed-real-updater'
    Write-FixtureText $parent.HelperFile ([string]$helper.Process.Id)
    $null = Wait-FixtureExit $helper 1
    Assert-NoFixtureReady $parent
    Assert-FixtureSnapshot $lifecycleInstall $lifecycleBefore 'Real updater signature rejection'
    Stop-FixtureChild $parent.Child
    # A rejected helper releases its mutex before the visible failure prompt.
    $releasedMutex = Enter-VNoteUpdateMutex -InstallDir $lifecycleInstall
    $releasedMutex.ReleaseMutex(); $releasedMutex.Dispose()
    Publish-FixtureRelease github (New-FixtureRelease github)
    $parent = Start-FixtureParent $lifecycleInstall 'accept' 0
    $attempt.ParentProcessId = $parent.Child.Process.Id
    $attempt.PipeName = $parent.Pipe
    $attempt.Token = $parent.Token
    # Invoke owns and may delete the copied helper directory, so retries receive
    # another independent copy exactly as the controller would create.
    $retryDirectory = New-FixtureDirectory 'copied retry helper'
    $attempt.Updater = Join-Path $retryDirectory 'update-vnote.ps1'
    [IO.File]::Copy($script:UpdaterScript, $attempt.Updater)
    [IO.File]::Copy($MinisignPath, (Join-Path $retryDirectory 'minisign.exe'))
    [IO.File]::Copy($license, (Join-Path $retryDirectory 'LICENSE.minisign'))
    $helper = Start-FixtureMode Update $attempt 'successful-real-updater'
    Write-FixtureText $parent.HelperFile ([string]$helper.Process.Id)
    $null = Wait-FixtureExit $helper 0 30000
    Assert-Fixture ($parent.Child.Process.HasExited) 'Original VNote did not fully exit'
    $transcript = [IO.File]::ReadAllText($parent.Log)
    Assert-Fixture ($transcript.Contains("CLIENT $($helper.Process.Id)`n") -and $transcript.Contains("READY`nSAVED`n")) 'Real peer did not accept and drain its saved data'
    $record = [IO.File]::ReadAllText((Join-Path $lifecycleInstall '.fixture-launched')).Split('|')
    Assert-Fixture ($record[0] -ceq 'B') 'Replacement did not run generation B Main'
    $relaunched = [Diagnostics.Process]::GetProcessById([int]$record[1])
    Assert-Fixture ((Get-VNoteProcessPath -Process $relaunched) -ieq (Join-Path $lifecycleInstall 'vnote.exe')) 'Relaunch record points outside the updated install'
    $retainedChild = [pscustomobject]@{ Process = $relaunched; Label = 'observed generation B'; Output = $null; Error = $null }
    $script:Children.Add($retainedChild)
    Assert-Fixture (-not $relaunched.HasExited) 'Generation B exited despite successful observation'
    $backups = @([IO.Directory]::GetDirectories([IO.Path]::GetDirectoryName($lifecycleInstall), [IO.Path]::GetFileName($lifecycleInstall) + '.vnote-old-*'))
    Assert-Fixture ($backups.Count -eq 1) 'Full update did not retain exactly one recovery backup'
    $savedPath = 'config/local/saved-on-close.bin'
    Assert-Fixture ([IO.File]::ReadAllText((Join-Path $lifecycleInstall $savedPath)) -ceq 'saved after ACCEPTED') 'Clone preceded the final portable save'
    Assert-Fixture ([IO.File]::ReadAllText((Join-Path $backups[0] $savedPath)) -ceq 'saved after ACCEPTED') 'Backup lacks stopped-session portable data'
    Assert-FixtureOverlay $lifecycleInstall $backups[0] $stoppedSnapshot $valid.Manifest
    Stop-FixtureChild $retainedChild
    Pass-Fixture 'real Invoke failure/reset/retry, graceful exit, stopped config clone and observed generation B relaunch'

    foreach ($mode in @('exit', 'handoff')) {
        $install = New-FixtureInstall
        $script:RelaunchRoots.Add($install)
        Write-FixtureText (Join-Path $install '.fixture-relaunch-mode') $mode
        $handoffPeer = $null
        if ($mode -eq 'handoff') {
            $handoffPipe = 'vnote-fixture-handoff-' + [Guid]::NewGuid().ToString('N')
            $handoffLog = Join-Path $script:Root 'handoff-peer.log'
            $handoffPeer = Start-FixtureProcess $script:ExeA @('--handoff-peer', $handoffPipe, $handoffLog) 'handoff-peer'
            $null = Wait-FixtureFile $handoffLog $handoffPeer 'LISTENING'
            Write-FixtureText (Join-Path $install '.fixture-handoff-pipe') $handoffPipe
        }
        $before = Get-FixtureSnapshot $install
        $backup = Install-VNotePackage -InstallDir $install -PackageDir $valid.Directory -Manifest $valid.Manifest
        $message = Expect-FixtureFailure { Start-VNoteAfterUpdate -InstallDir $install -BackupPath $backup } ("relaunch " + $mode)
        Assert-Fixture ($message -match 'installed' -and $message -match 'reopen') 'Failed relaunch was not reported as installed-but-not-reopened'
        Assert-Fixture ([IO.File]::ReadAllText((Join-Path $install '.fixture-launched')).StartsWith('B|')) 'Early-exit fixture never reached generation B Main'
        Assert-FixtureOverlay $install $backup $before $valid.Manifest
        if ($null -ne $handoffPeer) {
            Assert-Fixture ([IO.File]::ReadAllText($handoffLog).Contains("HANDOFF`n")) 'Secondary instance did not actually hand off to the existing peer'
            Assert-Fixture (-not $handoffPeer.Process.HasExited) 'Updater terminated the handoff recipient'
            Stop-FixtureChild $handoffPeer
        }
        Pass-Fixture "relaunch $mode reports partial success and retains complete backup"
    }
    $success = $true
    Write-Host ("PASS {0} updater regressions in {1:N1}s" -f $script:Passed, $script:Clock.Elapsed.TotalSeconds)
}
catch {
    $failure = $_
    [Console]::Error.WriteLine($_.ToString())
    [Console]::Error.WriteLine($_.ScriptStackTrace)
}
finally {
    $cleanupFailed = $false
    $script:VNotePreparationGuard = $null
    # Restore ACLs before any recursive cleanup, and remove junctions themselves,
    # never their targets. Preserve all evidence if either operation fails.
    foreach ($entry in $script:AclRestores) {
        try { Set-Acl -LiteralPath $entry.Path -AclObject $entry.Acl }
        catch { $cleanupFailed = $true; [Console]::Error.WriteLine("ACL restore failed: $($entry.Path): $_") }
    }
    if (Get-Variable -Name NegativeSession -Scope Script -ErrorAction SilentlyContinue) {
        try { Close-VNoteParentSession -Session $script:NegativeSession }
        catch { $cleanupFailed = $true; [Console]::Error.WriteLine("Pipe cleanup failed: $_") }
    }
    foreach ($child in $script:Children) {
        try { Stop-FixtureChild $child; $child.Process.Dispose() }
        catch { $cleanupFailed = $true; [Console]::Error.WriteLine("Child cleanup failed ($($child.Label)): $_") }
    }
    foreach ($install in $script:RelaunchRoots) {
        try { Stop-FixtureRecordedRelaunch $install }
        catch { $cleanupFailed = $true; [Console]::Error.WriteLine("Relaunch cleanup failed: $install : $_") }
    }
    foreach ($junction in $script:Junctions) {
        try { if ([IO.Directory]::Exists($junction)) { [IO.Directory]::Delete($junction) } }
        catch { $cleanupFailed = $true; [Console]::Error.WriteLine("Junction cleanup failed: $junction : $_") }
    }
    $env:PATH = $script:SavedPath
    $env:MINISIGN_PASSWORD = $script:SavedPassword
    if ($success -and -not $cleanupFailed) {
        try { Remove-Item -LiteralPath $script:Root -Recurse -Force }
        catch { $cleanupFailed = $true; [Console]::Error.WriteLine("Fixture cleanup failed: $_") }
    }
    if (-not $success -or $cleanupFailed) {
        [Console]::Error.WriteLine("Retained regression evidence: $script:Root")
    }
    if ($cleanupFailed) { $success = $false }
}
if (-not $success) { exit 1 }
exit 0
