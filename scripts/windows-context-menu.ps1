# Windows PowerShell 5.1. Registers only VNote's two per-user Explorer verbs.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Add', 'Remove')]
    [string]$Action,
    [string]$VNotePath,
    [AllowEmptyString()]
    [string]$Suffixes
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

function Remove-VNoteVerbs([string]$ShellPath) {
    # Inspect first so unrelated shell keys never need writable access.
    $existing = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($ShellPath)
    if ($null -eq $existing) { return }
    try {
        $names = $existing.GetSubKeyNames()
        $hasVerbs = $names -contains 'VNote.Read' -or $names -contains 'VNote.Edit'
    } finally {
        $existing.Dispose()
    }
    if (-not $hasVerbs) { return }
    $shellKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($ShellPath, $true)
    if ($null -eq $shellKey) { return }
    try {
        $shellKey.DeleteSubKeyTree('VNote.Read', $false)
        $shellKey.DeleteSubKeyTree('VNote.Edit', $false)
    } finally {
        $shellKey.Dispose()
    }
}

try {
    [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)

    $extensions = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    if ($Action -eq 'Add') {
        if (-not $PSBoundParameters.ContainsKey('Suffixes')) {
            throw 'Add requires -Suffixes with a semicolon-separated list of supported file suffixes.'
        }
        foreach ($value in $Suffixes.Split(';')) {
            $suffix = $value.Trim().ToLowerInvariant()
            if ($suffix.Length -eq 0) { continue }
            if ($suffix.StartsWith('.')) { $suffix = $suffix.Substring(1) }
            if ($suffix.Length -eq 0 -or $suffix.Length -gt 254 -or
                $suffix -match '[.\s\\/:*?"<>|;\x00-\x1f]') {
                throw "Invalid file suffix: $value"
            }
            [void]$extensions.Add('.' + $suffix)
        }
        # Windows PowerShell 5.1 has no script root during parameter-default evaluation.
        if (-not $PSBoundParameters.ContainsKey('VNotePath')) {
            $VNotePath = Join-Path $PSScriptRoot 'vnote.exe'
        }
        # Validate before opening any writable registry key. Never evaluate this path as code.
        if ([string]::IsNullOrWhiteSpace($VNotePath) -or $VNotePath.Contains('"')) {
            throw 'VNotePath must name an existing executable and must not contain double quotes.'
        }
        $executable = [System.IO.Path]::GetFullPath($VNotePath)
        if (-not [System.IO.File]::Exists($executable) -or
            -not [string]::Equals([System.IO.Path]::GetExtension($executable), '.exe',
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "VNote executable not found: $executable"
        }
    }

    # Compile before mutation so unavailable interop cannot leave a partially changed menu.
    if ($null -eq ('VNote.ContextMenuNativeMethods' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace VNote {
    public static class ContextMenuNativeMethods {
        [DllImport("shell32.dll")]
        public static extern void SHChangeNotify(uint eventId, uint flags, IntPtr item1, IntPtr item2);
    }
}
'@
    }

    # .NET APIs treat the legacy '*' key literally. Never use provider wildcard paths.
    Remove-VNoteVerbs 'Software\Classes\*\shell'
    $associationsPath = 'Software\Classes\SystemFileAssociations'
    $associations = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($associationsPath)
    if ($null -ne $associations) {
        try {
            foreach ($extension in $associations.GetSubKeyNames()) {
                if ($extension.StartsWith('.') -and
                    ($Action -eq 'Remove' -or -not $extensions.Contains($extension))) {
                    Remove-VNoteVerbs ($associationsPath + '\' + $extension + '\shell')
                }
            }
        } finally {
            $associations.Dispose()
        }
    }

    if ($Action -eq 'Add') {
        $verbs = @(
            @{
                Key = 'VNote.Read'
                Label = 'Read in VNote'
                Command = '"{0}" --read-mode --detached-view -- "%1"' -f $executable
            },
            @{
                Key = 'VNote.Edit'
                Label = 'Edit in VNote'
                Command = '"{0}" --edit-mode -- "%1"' -f $executable
            }
        )
        foreach ($extension in $extensions) {
            $shellKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey(
                $associationsPath + '\' + $extension + '\shell')
            try {
                foreach ($verb in $verbs) {
                    $verbKey = $shellKey.CreateSubKey($verb.Key)
                    try {
                        $verbKey.SetValue('', $verb.Label, [Microsoft.Win32.RegistryValueKind]::String)
                        $verbKey.SetValue('Icon', ('"{0}",0' -f $executable),
                            [Microsoft.Win32.RegistryValueKind]::String)
                        $commandKey = $verbKey.CreateSubKey('command')
                        try {
                            $commandKey.SetValue('', $verb.Command,
                                [Microsoft.Win32.RegistryValueKind]::String)
                        } finally {
                            $commandKey.Dispose()
                        }
                    } finally {
                        $verbKey.Dispose()
                    }
                }
            } finally {
                $shellKey.Dispose()
            }
        }
    }

    # SHCNE_ASSOCCHANGED | SHCNF_IDLIST; flush asynchronously instead of waiting for Explorer.
    [VNote.ContextMenuNativeMethods]::SHChangeNotify(0x08000000, 0x2000,
        [IntPtr]::Zero, [IntPtr]::Zero)
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
