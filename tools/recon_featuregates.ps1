# READ-ONLY targeted feature-gate scan. Fast: subkey-name match + bounded value scan in feature stores only.
$ErrorActionPreference = 'Continue'
$fids = @('10727725','11895592')
$terms = @('TabShell','tabshell','Sets','AllowSets','IsTabShellSettingsEnabled')

function HexDump([byte[]]$b) { ($b | ForEach-Object { $_.ToString('X2') }) -join ' ' }

function Dump-Values($key, $path) {
    foreach ($vn in $key.GetValueNames()) {
        $kind = $key.GetValueKind($vn)
        $val = $key.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ($val -is [byte[]]) { Write-Output ("    VALUE '{0}' kind={1} len={2} HEX={3}" -f $vn,$kind,$val.Length,(HexDump $val)) }
        else { Write-Output ("    VALUE '{0}' kind={1} value={2}" -f $vn,$kind,$val) }
    }
}

# 1) Feature-store roots: enumerate subkey names, match fids/terms
$roots = @(
    'HKLM:\SYSTEM\CurrentControlSet\Control\FeatureManagement',
    'HKLM:\SYSTEM\CurrentControlSet\Control\FeatureManagement\Overrides',
    'HKLM:\SYSTEM\CurrentControlSet\Control\FeatureManagement\Features',
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\FlightedFeatures',
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\FeatureManagement',
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\FeatureOverride',
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\FeatureStaging',
    'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FeatureOverrides',
    'HKLM:\SOFTWARE\Microsoft\PolicyManager\default\Features',
    'HKLM:\SOFTWARE\Microsoft\WindowsSelfHost'
)
Write-Output "=== FEATURE STORE SUBKEY SCAN ==="
foreach ($r in $roots) {
    if (-not (Test-Path -LiteralPath $r)) { Write-Output "ABSENT  $r"; continue }
    $k = Get-Item -LiteralPath $r
    Write-Output "PRESENT $r  (subkeys: $($k.GetSubKeyNames().Count))"
    foreach ($sn in $k.GetSubKeyNames()) {
        $hit = $false
        foreach ($f in $fids) { if ($sn -eq $f -or $sn -like "*$f*") { $hit=$true } }
        foreach ($t in $terms) { if ($sn.IndexOf($t,[StringComparison]::OrdinalIgnoreCase) -ge 0) { $hit=$true } }
        if ($hit) {
            Write-Output ("  SUBKEY-HIT {0}\{1}" -f $r,$sn)
            $sub = $k.OpenSubKey($sn); Dump-Values $sub "$r\$sn"; $sub.Close()
        }
    }
}

# 2) Direct fid key lookup in FeatureManagement Overrides / Features
Write-Output "=== DIRECT FID KEY LOOKUP ==="
foreach ($base in @('HKLM:\SYSTEM\CurrentControlSet\Control\FeatureManagement\Overrides','HKLM:\SYSTEM\CurrentControlSet\Control\FeatureManagement\Features','HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\FlightedFeatures')) {
    foreach ($f in $fids) {
        $p = "$base\$f"
        if (Test-Path -LiteralPath $p) {
            $k = Get-Item -LiteralPath $p
            Write-Output "FOUND $p"
            Dump-Values $k $p
            foreach ($sn in $k.GetSubKeyNames()) { $s=$k.OpenSubKey($sn); Write-Output ("  sub {0}:" -f $sn); Dump-Values $s "$p\$sn"; $s.Close() }
        } else {
            Write-Output "ABSENT  $p"
        }
    }
}

# 3) Bounded value scan inside FeatureManagement + FlightedFeatures trees for fids/tabshell/sets
Write-Output "=== VALUE SCAN in feature stores ==="
function ScanTree($path, $depth) {
    if ($depth -lt 0) { return }
    try { $k = Get-Item -LiteralPath $path -ErrorAction Stop } catch { return }
    foreach ($vn in $k.GetValueNames()) {
        $val = $k.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $vs = if ($val -is [byte[]]) { HexDump $val } else { [string]$val }
        foreach ($f in $fids) { if ($vs -match $f) { Write-Output ("  VAL-FID {0} name={1} value={2}" -f $path,$vn,$vs) } }
        foreach ($t in $terms) { if ($vs.IndexOf($t,[StringComparison]::OrdinalIgnoreCase) -ge 0) { Write-Output ("  VAL-TERM {0} name={1} value={2}" -f $path,$vn,$vs) } }
    }
    foreach ($sn in $k.GetSubKeyNames()) { ScanTree (Join-Path $path $sn) ($depth-1) }
}
foreach ($t in @('HKLM:\SYSTEM\CurrentControlSet\Control\FeatureManagement','HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\FlightedFeatures')) {
    Write-Output "-- tree $t --"
    ScanTree $t 4
}
