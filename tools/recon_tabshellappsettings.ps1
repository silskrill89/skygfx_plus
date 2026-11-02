# READ-ONLY dump of TabShellAppSettings + related feature keys found in twinui.pcshell.dll strings.
$ErrorActionPreference = 'Continue'

function HexDump([byte[]]$b) { if ($null -eq $b) { return '(null)' }; ($b | ForEach-Object { $_.ToString('X2') }) -join ' ' }

function Dump-Key($key, $path) {
    Write-Output ("KEY {0}" -f $path)
    Write-Output ("  SUBKEYS: " + (($key.GetSubKeyNames()) -join ', '))
    foreach ($vn in $key.GetValueNames()) {
        $kind = $key.GetValueKind($vn)
        $val = $key.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($val -is [byte[]]) {
            Write-Output ("  VALUE name='{0}' kind={1} len={2}" -f $vn,$kind,$val.Length)
            Write-Output ("    HEX=" + (HexDump $val))
        } else {
            Write-Output ("  VALUE name='{0}' kind={1} value={2}" -f $vn,$kind,$val)
        }
    }
    foreach ($sn in $key.GetSubKeyNames()) {
        try { $sub = $key.OpenSubKey($sn); Dump-Key $sub "$path\$sn"; $sub.Close() } catch { Write-Output ("  SUB ERR " + $_.Exception.Message) }
    }
}

$paths = @(
    'SOFTWARE\Microsoft\Windows\CurrentVersion\TabShellAppSettings',
    'SOFTWARE\Microsoft\Windows\CurrentVersion\TabShell',
    'SOFTWARE\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount'
)
foreach ($hive in @([Microsoft.Win32.Registry]::CurrentUser, [Microsoft.Win32.Registry]::LocalMachine)) {
    $hiveName = $hive.Name
    foreach ($p in $paths) {
        try {
            $k = $hive.OpenSubKey($p)
            if ($k) {
                Write-Output "=== $hiveName\$p ==="
                Dump-Key $k "$hiveName\$p"
                $k.Close()
            } else {
                Write-Output ("ABSENT $hiveName\$p")
            }
        } catch {
            Write-Output ("ERR $hiveName\$p : " + $_.Exception.Message)
        }
    }
}

# Search both hives for any subkey named like TabShell/AllowSets under CurrentVersion
Write-Output "=== NAME SEARCH under CurrentVersion (both hives) ==="
foreach ($hive in @([Microsoft.Win32.Registry]::CurrentUser, [Microsoft.Win32.Registry]::LocalMachine)) {
    foreach ($rp in @('SOFTWARE\Microsoft\Windows\CurrentVersion','SOFTWARE\Microsoft\Windows NT\CurrentVersion')) {
        try {
            $r = $hive.OpenSubKey($rp)
            if ($r) {
                foreach ($sn in $r.GetSubKeyNames()) {
                    if ($sn -match '(?i)(tab|sets|shell)') {
                        Write-Output ("FOUND {0}\{1}\{2}" -f $hive.Name, $rp, $sn)
                    }
                }
                $r.Close()
            }
        } catch {}
    }
}
