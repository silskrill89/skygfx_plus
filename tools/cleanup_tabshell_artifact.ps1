# cleanup_tabshell_artifact.ps1
# Bounded cleanup: remove ONLY the 'TestTabSetting' value from the tabshell
# CloudStore key. Snapshots BEFORE/AFTER. Touches nothing else.
#
# MUST be run as:  powershell -NoProfile -ExecutionPolicy Bypass -File tools\cleanup_tabshell_artifact.ps1
# Do NOT pass this logic through bash -Command (mangles $$ and backslashes).

$ErrorActionPreference = 'Continue'

# ---------------------------------------------------------------------------
# 1. Registry hive via explicit .NET API
# ---------------------------------------------------------------------------
$hive = [Microsoft.Win32.Registry]::CurrentUser

# ---------------------------------------------------------------------------
# 2. Build the $$-prefixed path safely (avoid PowerShell $$ interpolation)
#    $$  is the automatic "last token" variable in PowerShell, so it must be
#    constructed from char codes and concatenated, never typed literally in a
#    double-quoted string.
# ---------------------------------------------------------------------------
$D  = [string][char]0x24                 # '$'
$DD = $D + $D                            # '$$'
$leaf = $DD + 'windows.data.tabshell'    # '$$windows.data.tabshell'
$basePath    = 'Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount'
$parentPath  = $basePath + '\' + $leaf
$currentPath = $parentPath + '\Current'

# Output locations (next to this script)
$outDir = $PSScriptRoot
if (-not $outDir) { $outDir = (Get-Location).Path }
$beforeFile = Join-Path $outDir 'tabshell_state_before.txt'
$afterFile  = Join-Path $outDir 'tabshell_state_after.txt'

Write-Host '=== TABSHELL ARTIFACT CLEANUP ==='
Write-Host ('Hive             : ' + $hive.Name)
Write-Host ('Parent subkey    : ' + $parentPath)
Write-Host ('Target subkey    : ' + $currentPath)
Write-Host ('Snapshot BEFORE  : ' + $beforeFile)
Write-Host ('Snapshot AFTER   : ' + $afterFile)
Write-Host ''

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Get-ValueInfo {
    param(
        [Microsoft.Win32.RegistryKey]$Key,
        [string]$Name
    )
    $kind = $Key.GetValueKind($Name)
    $raw  = $Key.GetValue($Name, $null,
                          [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    $hex  = ''
    $text = ''
    $kindStr = $kind.ToString()

    if ($kindStr -eq 'Binary') {
        if ($null -ne $raw) {
            $hex = [System.BitConverter]::ToString([byte[]]$raw)
        }
        $text = ('<' + ([string]($raw.Length)) + ' bytes>')
    }
    elseif ($kindStr -eq 'DWord') {
        $hex  = [System.BitConverter]::ToString([System.BitConverter]::GetBytes([int]$raw))
        $text = ('dec=' + [string]$raw + ' hex=0x' + ('{0:X8}' -f ([uint32]$raw)))
    }
    elseif ($kindStr -eq 'QWord') {
        $hex  = [System.BitConverter]::ToString([System.BitConverter]::GetBytes([long]$raw))
        $text = ('dec=' + [string]$raw + ' hex=0x' + ('{0:X16}' -f ([uint64]$raw)))
    }
    elseif ($kindStr -eq 'MultiString') {
        $text = ($raw -join ' | ')
        $hex  = [System.BitConverter]::ToString([System.Text.Encoding]::Unicode.GetBytes($text))
    }
    else {
        $text = [string]$raw
        $hex  = [System.BitConverter]::ToString([System.Text.Encoding]::Unicode.GetBytes($text))
    }

    return @{ Name = $Name; Kind = $kindStr; Hex = $hex; Text = $text }
}

function Get-SnapshotLines {
    param(
        [Microsoft.Win32.RegistryKey]$Key,
        [string]$Title
    )
    $lines = New-Object System.Collections.ArrayList
    [void]$lines.Add('=== ' + $Title + ' ===')
    [void]$lines.Add('Timestamp : ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
    [void]$lines.Add('Hive      : ' + [Microsoft.Win32.Registry]::CurrentUser.Name)
    [void]$lines.Add('Path      : ' + $Key.Name)

    $names = @($Key.GetValueNames() | Sort-Object)
    [void]$lines.Add('ValueCount: ' + $names.Count)
    [void]$lines.Add('--- VALUES (Name | Kind | Hex | Text) ---')
    foreach ($n in $names) {
        $info = Get-ValueInfo -Key $Key -Name $n
        [void]$lines.Add($info.Name + ' | ' + $info.Kind + ' | ' + $info.Hex + ' | ' + $info.Text)
    }
    [void]$lines.Add('--- END VALUES ---')
    return ,$lines
}

function Write-Snapshot {
    param(
        [System.Collections.ArrayList]$Lines,
        [string]$FilePath
    )
    # Print to console
    foreach ($l in $Lines) { Write-Host $l }
    # Write to file (UTF8 so the hex/ASCII is unambiguous)
    [System.IO.File]::WriteAllLines($FilePath, [string[]]$Lines.ToArray(), [System.Text.Encoding]::UTF8)
    Write-Host ('[wrote ' + $FilePath + ']')
}

# ---------------------------------------------------------------------------
# 3. Open the target subkey (writable). If missing -> KEY_ABSENT, exit 0.
# ---------------------------------------------------------------------------
$currentKey = $null
try {
    $currentKey = $hive.OpenSubKey($currentPath, $true)
}
catch {
    Write-Host ('OpenSubKey threw: ' + $_.Exception.Message)
}

if ($null -eq $currentKey) {
    Write-Host 'KEY_ABSENT'
    # Diagnostic only (read-only, touches nothing): does the key exist at all?
    $probe = $null
    try { $probe = $hive.OpenSubKey($currentPath, $false) } catch { }
    if ($null -eq $probe) {
        Write-Host 'Diagnostic: subkey does not exist at all.'
    }
    else {
        Write-Host 'Diagnostic: subkey EXISTS but could not be opened for write.'
        Write-Host ('Diagnostic: values present -> ' + ((@($probe.GetValueNames()) -join ', ')))
        $probe.Close()
    }
    exit 0
}

Write-Host ('Opened writable: ' + $currentKey.Name)
Write-Host ''

# ---------------------------------------------------------------------------
# 4. Snapshot BEFORE
# ---------------------------------------------------------------------------
$beforeNames = @($currentKey.GetValueNames() | Sort-Object)
$beforeLines = Get-SnapshotLines -Key $currentKey -Title 'TABSHELL STATE BEFORE'
Write-Snapshot -Lines $beforeLines -FilePath $beforeFile
Write-Host ''

# ---------------------------------------------------------------------------
# 5. Delete ONLY 'TestTabSetting' (if present)
# ---------------------------------------------------------------------------
$target = 'TestTabSetting'
if ($beforeNames -contains $target) {
    try {
        $currentKey.DeleteValue($target)
        Write-Host 'DELETED TestTabSetting'
    }
    catch {
        Write-Host ('DELETE FAILED for TestTabSetting: ' + $_.Exception.Message)
    }
}
else {
    Write-Host 'TestTabSetting NOT PRESENT'
}
Write-Host ''

# ---------------------------------------------------------------------------
# 6. Snapshot AFTER
# ---------------------------------------------------------------------------
$afterNames = @($currentKey.GetValueNames() | Sort-Object)
$afterLines = Get-SnapshotLines -Key $currentKey -Title 'TABSHELL STATE AFTER'
Write-Snapshot -Lines $afterLines -FilePath $afterFile
Write-Host ''

# ---------------------------------------------------------------------------
# 7. Removed / added value names
# ---------------------------------------------------------------------------
$removed = @($beforeNames | Where-Object { $afterNames -notcontains $_ })
$added   = @($afterNames  | Where-Object { $beforeNames -notcontains $_ })

Write-Host '=== VALUE NAME DIFF ==='
Write-Host ('Removed : ' + $(if ($removed.Count -gt 0) { $removed -join ', ' } else { '(none)' }))
Write-Host ('Added   : ' + $(if ($added.Count   -gt 0) { $added   -join ', ' } else { '(none)' }))
Write-Host ''

# ---------------------------------------------------------------------------
# 8. Subkeys of the parent '$$windows.data.tabshell'
# ---------------------------------------------------------------------------
Write-Host '=== PARENT SUBKEYS ==='
$parentKey = $null
try {
    $parentKey = $hive.OpenSubKey($parentPath, $false)
}
catch {
    Write-Host ('OpenSubKey(parent) threw: ' + $_.Exception.Message)
}

if ($null -eq $parentKey) {
    Write-Host ('PARENT_ABSENT: ' + $parentPath)
}
else {
    Write-Host ('Parent: ' + $parentKey.Name)
    $subs = @($parentKey.GetSubKeyNames() | Sort-Object)
    if ($subs.Count -eq 0) {
        Write-Host '(no subkeys)'
    }
    else {
        foreach ($s in $subs) { Write-Host ('  - ' + $s) }
    }
    Write-Host ('SubKeyCount: ' + $subs.Count)
    Write-Host ('Has Current       : ' + ($subs -contains 'Current'))
    Write-Host ('Has CurrentStatus : ' + ($subs -contains 'CurrentStatus'))
    Write-Host ('Has Previous      : ' + ($subs -contains 'Previous'))
}
Write-Host ''

# ---------------------------------------------------------------------------
# 9. Close handles, exit 0
# ---------------------------------------------------------------------------
if ($null -ne $currentKey) { $currentKey.Close() }
if ($null -ne $parentKey)  { $parentKey.Close()  }
Write-Host '=== DONE (handles closed) ==='
exit 0
