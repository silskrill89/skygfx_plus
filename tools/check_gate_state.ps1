# check_gate_state.ps1 -- READ-ONLY. Post-crash progress probe for the TabShell enablement lane.
# Answers: did the fixer lane actually write anything before the crash?
$ErrorActionPreference = 'Continue'
$out = @()
function P([string]$s) { $script:out += $s; Write-Host $s }

P "=== 1. TabShellAppSettings (the key the binary reads) ==="
foreach ($root in @(@('HKCU',[Microsoft.Win32.Registry]::CurrentUser), @('HKLM',[Microsoft.Win32.Registry]::LocalMachine))) {
    $tag = $root[0]; $hive = $root[1]
    $p = 'Software\Microsoft\Windows\CurrentVersion\TabShellAppSettings'
    $k = $hive.OpenSubKey($p, $false)
    if ($null -eq $k) { P "$tag\$p  -> KEY_ABSENT" }
    else {
        foreach ($vn in $k.GetValueNames()) {
            $vk = $k.GetValueKind($vn)
            $vv = $k.GetValue($vn)
            if ($vk -eq [Microsoft.Win32.RegistryValueKind]::Binary) { $vv = [System.BitConverter]::ToString($vv) }
            P "$tag\$p\$vn = [$vk] $vv"
        }
        if ($k.GetValueNames().Count -eq 0) { P "$tag\$p  -> KEY_PRESENT_NO_VALUES" }
        $k.Close()
    }
}

P ""
P "=== 2. Velocity feature gates (Overrides\4, obfuscated names) ==="
$want = @{
    '1527625864'='10727725 TabShell'
    '1365873800'='11895592 TabShellWinRtApi'
    '3167312009'='13849566 TabShellVisibility'
    '3192474248'='14291807 TabShellVisibilityOverrideToOnForGates'
    '768013962' ='17093654 TabSettingStateRepoIntegration'
    '3403740811'='17057765 TabShellAutoTabbingPreference'
}
$ov = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SYSTEM\CurrentControlSet\Control\FeatureManagement\Overrides', $false)
if ($null -eq $ov) { P "Overrides ROOT_ABSENT" }
else {
    P ("Overrides buckets present: " + (($ov.GetSubKeyNames() | Sort-Object) -join ','))
    foreach ($bn in $ov.GetSubKeyNames()) {
        $bk = $ov.OpenSubKey($bn, $false)
        foreach ($n in $bk.GetSubKeyNames()) {
            if ($want.ContainsKey($n)) {
                $sk = $bk.OpenSubKey($n, $false)
                $vals = @()
                foreach ($vn in $sk.GetValueNames()) { $vals += "$vn=$($sk.GetValue($vn))" }
                P ("  HIT Overrides\$bn\$n  (" + $want[$n] + ")  -> " + ($vals -join '  '))
                $sk.Close()
            }
        }
        $bk.Close()
    }
    P "(only matches for the 6 TabShell fids are printed above; blank = still absent)"
    $ov.Close()
}

P ""
P "=== 3. BOGUS fabricated CloudStore key (must be deleted if signature matches) ==="
$bsig = 'Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\windows.data.tabshell'
$bk2 = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($bsig, $false)
if ($null -eq $bk2) { P "BOGUS_KEY ABSENT (good)" }
else {
    P "BOGUS_KEY PRESENT -> $bsig"
    foreach ($skn in $bk2.GetSubKeyNames()) {
        $sk = $bk2.OpenSubKey($skn, $false)
        foreach ($vn in $sk.GetValueNames()) {
            $vk = $sk.GetValueKind($vn); $vv = $sk.GetValue($vn)
            if ($vk -eq [Microsoft.Win32.RegistryValueKind]::Binary) { $vv = [System.BitConverter]::ToString($vv) }
            P "   [$skn] $vn = [$vk] $vv"
        }
        $sk.Close()
    }
    $bk2.Close()
}

P ""
P "=== 4. VALID $$ tabshell key (should keep its 21-byte Data) ==="
$d4 = [string]::Concat([char]0x24,[char]0x24,'windows.data.tabshell')
$p4 = 'Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\' + $d4
$k4 = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($p4, $false)
if ($null -eq $k4) { P "VALID_KEY ABSENT (unexpected)" }
else {
    P ("subkeys: " + (($k4.GetSubKeyNames()) -join ','))
    foreach ($skn in $k4.GetSubKeyNames()) {
        $sk = $k4.OpenSubKey($skn, $false)
        foreach ($vn in $sk.GetValueNames()) {
            $vk = $sk.GetValueKind($vn); $vv = $sk.GetValue($vn)
            if ($vk -eq [Microsoft.Win32.RegistryValueKind]::Binary) { $vv = [System.BitConverter]::ToString($vv) }
            P "   [$skn] $vn = [$vk] $vv"
        }
        $sk.Close()
    }
    $k4.Close()
}

P ""
P "=== 5. ReconcileFeatures task (can silently revert our gates) ==="
$t = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion\Schedule\TaskCache\Tree\Microsoft\Windows\Flighting\FeatureConfig', $false)
if ($null -eq $t) { P "FeatureConfig task -> ABSENT" } else { P "FeatureConfig task -> PRESENT"; $t.Close() }

$out | Set-Content -Path (Join-Path $PSScriptRoot 'gate_state_now.txt') -Encoding UTF8
Write-Host ""
Write-Host "WROTE: $(Join-Path $PSScriptRoot 'gate_state_now.txt')"
