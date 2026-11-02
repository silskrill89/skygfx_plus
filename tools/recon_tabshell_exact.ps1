# READ-ONLY: exact contents of the real $$windows.data.tabshell key + WindowTabManager.
$ErrorActionPreference = 'Continue'
function HexDump([byte[]]$b) { if ($null -eq $b) { return '(null)' }; ($b | ForEach-Object { $_.ToString('X2') }) -join ' ' }

function DumpExact($root, $path) {
    $k = $root.OpenSubKey($path)
    if (-not $k) { Write-Output "ABSENT: $path"; return }
    Write-Output "KEY: $path"
    Write-Output ("  SUBKEYS: [" + (($k.GetSubKeyNames()) -join ', ') + "]")
    foreach ($vn in $k.GetValueNames()) {
        $kind = $k.GetValueKind($vn)
        $val = $k.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($val -is [byte[]]) { Write-Output ("  VALUE '{0}' kind={1} len={2} HEX={3}" -f $vn,$kind,$val.Length,(HexDump $val)) }
        else { Write-Output ("  VALUE '{0}' kind={1} value={2}" -f $vn,$kind,$val) }
    }
    foreach ($sn in $k.GetSubKeyNames()) {
        $sub = $k.OpenSubKey($sn)
        Write-Output ("  -- SUBKEY '{0}' --" -f $sn)
        foreach ($vn in $sub.GetValueNames()) {
            $kind = $sub.GetValueKind($vn)
            $val = $sub.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            if ($val -is [byte[]]) { Write-Output ("     VALUE '{0}' kind={1} len={2} HEX={3}" -f $vn,$kind,$val.Length,(HexDump $val)) }
            else { Write-Output ("     VALUE '{0}' kind={1} value={2}" -f $vn,$kind,$val) }
        }
        $sub.Close()
    }
    $k.Close()
}

$base = 'SOFTWARE\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount'
$cu = [Microsoft.Win32.Registry]::CurrentUser
Write-Output "=== REAL: \$\$windows.data.tabshell ==="
DumpExact $cu "$base\`$`$windows.data.tabshell"
Write-Output ""
Write-Output "=== FABRICATED: windows.data.tabshell (no \$\$) ==="
DumpExact $cu "$base\windows.data.tabshell"
Write-Output ""
Write-Output "=== WindowTabManager (HKLM) ==="
$lm = [Microsoft.Win32.Registry]::LocalMachine
DumpExact $lm 'SOFTWARE\Microsoft\Windows\CurrentVersion\WindowTabManager'
