# READ-ONLY: capture raw DISM output (may require elevation) + fallback CBS registry package enumeration.
$ErrorActionPreference = 'Continue'
Write-Output "=== RAW dism /online /get-packages ==="
try {
    $out = & dism /online /get-packages /English 2>&1
    foreach ($line in $out) { Write-Output ("DISM| " + $line) }
} catch { Write-Output ("dism ERR " + $_.Exception.Message) }

Write-Output ""
Write-Output "=== CBS registry packages matching tab/shell/sets (fallback) ==="
$base = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\Packages'
if (Test-Path -LiteralPath $base) {
    $k = Get-Item -LiteralPath $base
    $all = $k.GetSubKeyNames()
    Write-Output ("TOTAL_CBS_PACKAGES=" + $all.Count)
    foreach ($sn in $all) {
        if ($sn -match '(?i)(tab|shell|sets|twinui|explorerframe|experience)') {
            $sub = $k.OpenSubKey($sn)
            $state = $sub.GetValue('CurrentState')
            Write-Output ("CBS {0}  CurrentState={1}" -f $sn, $state)
            $sub.Close()
        }
    }
} else {
    Write-Output "CBS Packages key ABSENT"
}
