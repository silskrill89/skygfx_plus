# READ-ONLY: check Overrides buckets (value names) + WindowsSelfHost\FIDs for the fids.
$ErrorActionPreference = 'Continue'
$fids = @('10727725','11895592')
function HexDump([byte[]]$b) { ($b | ForEach-Object { $_.ToString('X2') }) -join ' ' }

Write-Output "=== Overrides buckets: value names + subkeys ==="
foreach ($b in @('0','4','8','9','15')) {
    $p = "HKLM:\SYSTEM\CurrentControlSet\Control\FeatureManagement\Overrides\$b"
    if (-not (Test-Path -LiteralPath $p)) { Write-Output "ABSENT $p"; continue }
    $k = Get-Item -LiteralPath $p
    Write-Output "BUCKET $p  subkeys: $(($k.GetSubKeyNames()) -join ',')"
    foreach ($vn in $k.GetValueNames()) {
        $val = $k.GetValue($vn,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $kind = $k.GetValueKind($vn)
        $mark = ''
        foreach ($f in $fids) { if ($vn -eq $f -or ([string]$val) -match $f) { $mark = '  <<<FID' } }
        if ($val -is [byte[]]) { Write-Output ("  val '{0}' kind={1} len={2}{3}" -f $vn,$kind,$val.Length,$mark) }
        else { Write-Output ("  val '{0}' kind={1} value={2}{3}" -f $vn,$kind,$val,$mark) }
    }
    # recurse one level into subkeys, match fid names
    foreach ($sn in $k.GetSubKeyNames()) {
        foreach ($f in $fids) {
            if ($sn -eq $f -or $sn -like "*$f*") {
                Write-Output ("  SUBKEY-FID {0}\{1}" -f $p,$sn)
                $s = $k.OpenSubKey($sn)
                foreach ($vn in $s.GetValueNames()) { Write-Output ("     '{0}' = {1}" -f $vn, $s.GetValue($vn)) }
                $s.Close()
            }
        }
    }
}

Write-Output "=== WindowsSelfHost\FIDs ==="
$fp = 'HKLM:\SOFTWARE\Microsoft\WindowsSelfHost\FIDs'
if (Test-Path -LiteralPath $fp) {
    $k = Get-Item -LiteralPath $fp
    Write-Output "SUBKEYS: $(($k.GetSubKeyNames()) -join ', ')"
    foreach ($vn in $k.GetValueNames()) {
        $val = $k.GetValue($vn,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $kind = $k.GetValueKind($vn)
        $mark = ''
        foreach ($f in $fids) { if ($vn -eq $f -or ([string]$val) -match $f) { $mark = '  <<<FID' } }
        if ($val -is [byte[]]) { Write-Output ("  '{0}' kind={1} len={2}{3}" -f $vn,$kind,$val.Length,$mark) }
        else { Write-Output ("  '{0}' kind={1} value={2}{3}" -f $vn,$kind,$val,$mark) }
    }
} else { Write-Output "ABSENT $fp" }

Write-Output "=== WindowsSelfHost\OneSettings / Applicability (fids?) ==="
foreach ($p in @('HKLM:\SOFTWARE\Microsoft\WindowsSelfHost\OneSettings','HKLM:\SOFTWARE\Microsoft\WindowsSelfHost\Applicability','HKLM:\SOFTWARE\Microsoft\WindowsSelfHost\ClientState')) {
    if (Test-Path -LiteralPath $p) {
        $k = Get-Item -LiteralPath $p
        Write-Output "$p subkeys: $(($k.GetSubKeyNames()) -join ', ')"
        foreach ($vn in $k.GetValueNames()) {
            $val = $k.GetValue($vn,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            foreach ($f in $fids) { if (([string]$val) -match $f -or $vn -eq $f) { Write-Output ("  FIDHIT '{0}' = {1}" -f $vn,$val) } }
        }
    }
}
