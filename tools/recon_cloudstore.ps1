# READ-ONLY CloudStore tabshell state dump via .NET Registry API (handles $$ keys).
$ErrorActionPreference = 'Continue'

function HexDump([byte[]]$b) {
    if ($null -eq $b) { return '(null)' }
    ($b | ForEach-Object { $_.ToString('X2') }) -join ' '
}

function Dump-Key($key, $path, $depth) {
    foreach ($vn in $key.GetValueNames()) {
        $kind = $key.GetValueKind($vn)
        $val = $key.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($kind -eq [Microsoft.Win32.RegistryValueKind]::Binary -or $kind -eq [Microsoft.Win32.RegistryValueKind]::None -or $kind -eq [Microsoft.Win32.RegistryValueKind]::None) {
            Write-Output ("  VALUE name='{0}' kind={1} len={2}" -f $vn, $kind, $(if ($val -is [byte[]]) { $val.Length } else { 'n/a' }))
            if ($val -is [byte[]]) { Write-Output ("    HEX=" + (HexDump $val)) }
            else { Write-Output ("    DATA=" + $val) }
        } elseif ($kind -eq [Microsoft.Win32.RegistryValueKind]::QWord -or $kind -eq [Microsoft.Win32.RegistryValueKind]::DWord) {
            Write-Output ("  VALUE name='{0}' kind={1} value={2}" -f $vn, $kind, $val)
        } elseif ($kind -eq [Microsoft.Win32.RegistryValueKind]::MultiString) {
            Write-Output ("  VALUE name='{0}' kind={1} value={2}" -f $vn, $kind, ($val -join ' ; '))
        } else {
            Write-Output ("  VALUE name='{0}' kind={1} value={2}" -f $vn, $kind, $val)
            if ($val -is [byte[]]) { Write-Output ("    HEX=" + (HexDump $val)) }
        }
    }
    if ($depth -gt 0) {
        foreach ($sn in $key.GetSubKeyNames()) {
            Write-Output ("  SUBKEY {0}" -f $sn)
            try {
                $sub = $key.OpenSubKey($sn)
                Dump-Key $sub "$path\$sn" ($depth - 1)
                $sub.Close()
            } catch { Write-Output ("    ERR " + $_.Exception.Message) }
        }
    }
}

$base = 'SOFTWARE\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount'
Write-Output "=== CloudStore tabshell (CurrentUser) ==="
Write-Output ("BASE=" + $base)
try {
    $cu = [Microsoft.Win32.Registry]::CurrentUser
    $parent = $cu.OpenSubKey($base)
    if (-not $parent) {
        Write-Output "PARENT MISSING: $base"
        # try to locate any CloudStore path
        foreach ($alt in @('SOFTWARE\Microsoft\Windows\CurrentVersion\CloudStore','SOFTWARE\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache')) {
            $a = $cu.OpenSubKey($alt)
            if ($a) { Write-Output ("ALT PRESENT {0} subkeys={1}" -f $alt, (($a.GetSubKeyNames()) -join ', ')); $a.Close() }
            else { Write-Output ("ALT ABSENT {0}" -f $alt) }
        }
    } else {
        Write-Output "=== DefaultAccount subkeys ==="
        foreach ($sn in $parent.GetSubKeyNames()) {
            if ($sn -like '*tab*' -or $sn -like '*Tab*' -or $sn -like '*$$*') {
                Write-Output ("SUBKEY {0}" -f $sn)
            }
        }
        # Focus on the tabshell key
        $targetNames = $parent.GetSubKeyNames() | Where-Object { $_ -like '*tabshell*' -or $_ -like '*TabShell*' }
        foreach ($tn in $targetNames) {
            Write-Output "=== TARGET KEY: $tn ==="
            $tk = $parent.OpenSubKey($tn)
            Write-Output ("  immediate subkeys: " + (($tk.GetSubKeyNames()) -join ', '))
            Dump-Key $tk "$base\$tn" 2
            $tk.Close()
        }
        if (-not $targetNames) {
            Write-Output "(no tabshell subkey found; dumping first-level value names only)"
        }
        $parent.Close()
    }
} catch {
    Write-Output ("ERR " + $_.Exception.Message)
}
