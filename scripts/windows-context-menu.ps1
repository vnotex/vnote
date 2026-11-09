# Windows PowerShell 5.1. Registers only VNote's two per-user Explorer verbs.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Add', 'Remove')]
    [string]$Action,
    [string]$VNotePath
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

try {
    [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)

    if ($Action -eq 'Add') {
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

    # .NET registry APIs treat '*' literally, unlike PowerShell's registry provider.
    $shellPath = 'Software\Classes\*\shell'
    $shellKey = $null
    try {
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
            $shellKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($shellPath)
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
        } else {
            # Removal neither creates keys nor requires the previously registered executable.
            $shellKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($shellPath, $true)
            if ($null -ne $shellKey) {
                $shellKey.DeleteSubKeyTree('VNote.Read', $false)
                $shellKey.DeleteSubKeyTree('VNote.Edit', $false)
            }
        }
    } finally {
        if ($null -ne $shellKey) {
            $shellKey.Dispose()
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
