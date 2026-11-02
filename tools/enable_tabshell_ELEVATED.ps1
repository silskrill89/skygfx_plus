# enable_tabshell_ELEVATED.ps1 - self-contained elevated helper for enable_tabshell.ps1 step 4.
# Writes ONLY: HKLM\...\TabShellAppSettings\AllowSets and the six
# HKLM\SYSTEM\CurrentControlSet\Control\FeatureManagement\Overrides\4\<obfuscated> gate subkeys.
# Prints progress to stdout and writes tools/tabshell_elevated_result.txt + tools/tabshell_gate_after.txt.
$ErrorActionPreference = 'Continue'
$ToolsDir       = Split-Path -Parent $MyInvocation.MyCommand.Path
$ResultFile     = Join-Path $ToolsDir 'tabshell_elevated_result.txt'
$GateAfterFile  = Join-Path $ToolsDir 'tabshell_gate_after.txt'

$OverBase = 'SYSTEM\CurrentControlSet\Control\FeatureManagement\Overrides'
$Gates = @(
    @{ Fid = 10727725; Name = 'TabShell';                                Sub = '1527625864' },
    @{ Fid = 11895592; Name = 'TabShellWinRtApi';                        Sub = '1365873800' },
    @{ Fid = 13849566; Name = 'TabShellVisibility';                      Sub = '3167312009' },
    @{ Fid = 14291807; Name = 'TabShellVisibilityOverrideToOnForGates';  Sub = '3192474248' },
    @{ Fid = 17093654; Name = 'TabSettingStateRepoIntegration';          Sub = '768013962' },
    @{ Fid = 17057765; Name = 'TabShellAutoTabbingPreference';           Sub = '3403740811' }
)

$log = New-Object System.Collections.Generic.List[string]
function Say($m) {
    $s = [string]$m
    Write-Output $s
    [void]$script:log.Add($s)
}
function Ok($m) { Say ('[OK] ' + $m) }

Say ('enable_tabshell_ELEVATED started {0}' -f (Get-Date -Format o))
$ident = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$prin  = New-Object System.Security.Principal.WindowsPrincipal($ident)
$isElev = $prin.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
Say ('identity={0} elevated={1}' -f $ident.Name, $isElev)
if (-not $isElev) {
    Say 'ELEVATION_REQUIRED (helper is not running elevated)'
    Set-Content -LiteralPath $ResultFile -Value $script:log -Encoding UTF8
    exit 2
}

# --- BEFORE snapshot of the gate store ---
$lm = [Microsoft.Win32.Registry]::LocalMachine
$before = New-Object System.Collections.Generic.List[string]
[void]$before.Add(('GATE BEFORE {0}' -f (Get-Date -Format o)))
foreach ($b in @('0','4','8','9','15')) {
    $k = $lm.OpenSubKey($OverBase + '\' + $b)
    if ($null -eq $k) { [void]$before.Add(('HKLM\{0}\{1} : bucket ABSENT' -f $OverBase, $b)); continue }
    [void]$before.Add(('HKLM\{0}\{1} : subkey count={2}' -f $OverBase, $b, $k.SubKeyCount))
    if ($b -eq '4') {
        foreach ($g in $Gates) {
            $present = (@($k.GetSubKeyNames()) -contains $g.Sub)
            [void]$before.Add(('  pre {0} (fid {1} {2}) present={3}' -f $g.Sub, $g.Fid, $g.Name, $present))
        }
    }
    $k.Close()
}
foreach ($l in $before) { Say $l }

# --- writes ---
$exit = 0
try {
    $rel = 'SOFTWARE\Microsoft\Windows\CurrentVersion\TabShellAppSettings'
    $k = $lm.CreateSubKey($rel)
    $k.SetValue('AllowSets', 1, [Microsoft.Win32.RegistryValueKind]::DWord)
    $k.Close()
    Ok ('HKLM\' + $rel + '\AllowSets = 1 (DWord)')

    $ovRel = $OverBase + '\4'
    $ov = $lm.OpenSubKey($ovRel, $true)
    if ($null -eq $ov) { $ov = $lm.CreateSubKey($ovRel) }
    foreach ($g in $Gates) {
        $sk = $ov.CreateSubKey($g.Sub)
        $sk.SetValue('EnabledState',        2, [Microsoft.Win32.RegistryValueKind]::DWord)
        $sk.SetValue('EnabledStateOptions', 0, [Microsoft.Win32.RegistryValueKind]::DWord)
        $sk.SetValue('Variant',             1, [Microsoft.Win32.RegistryValueKind]::DWord)
        $sk.SetValue('VariantPayload',      0, [Microsoft.Win32.RegistryValueKind]::DWord)
        $sk.SetValue('VariantPayloadKind',  0, [Microsoft.Win32.RegistryValueKind]::DWord)
        $sk.Close()
        Ok ('HKLM\' + $ovRel + '\' + $g.Sub + (' (fid {0} {1}) EnabledState=2 EnabledStateOptions=0 Variant=1 VariantPayload=0 VariantPayloadKind=0' -f $g.Fid, $g.Name))
    }
    $ov.Close()
    Say 'HKLM_OK'
} catch {
    Say ('HKLM_WRITE_FAILED: {0} : {1}' -f $_.Exception.GetType().FullName, $_.Exception.Message)
    $exit = 1
}

# --- AFTER snapshot ---
$after = New-Object System.Collections.Generic.List[string]
[void]$after.Add(('GATE AFTER {0}' -f (Get-Date -Format o)))
foreach ($b in @('0','4','8','9','15')) {
    $k = $lm.OpenSubKey($OverBase + '\' + $b)
    if ($null -eq $k) { [void]$after.Add(('HKLM\{0}\{1} : bucket ABSENT' -f $OverBase, $b)); continue }
    [void]$after.Add(('HKLM\{0}\{1} : subkey count={2}' -f $OverBase, $b, $k.SubKeyCount))
    if ($b -eq '4') {
        foreach ($g in $Gates) {
            $s = $k.OpenSubKey($g.Sub)
            if ($null -eq $s) { [void]$after.Add(('  post {0} (fid {1} {2}) ABSENT' -f $g.Sub, $g.Fid, $g.Name)); continue }
            $vals = @()
            foreach ($vn in $s.GetValueNames()) {
                $vals += ('{0}={1}' -f $vn, $s.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames))
            }
            $s.Close()
            [void]$after.Add(('  post {0} (fid {1} {2}) {3}' -f $g.Sub, $g.Fid, $g.Name, ($vals -join ' ')))
        }
    }
    $k.Close()
}

Set-Content -LiteralPath $GateAfterFile -Value ($before + $after) -Encoding UTF8
Say ('gate before/after written to {0}' -f $GateAfterFile)
Say ('enable_tabshell_ELEVATED finished {0}' -f (Get-Date -Format o))
Set-Content -LiteralPath $ResultFile -Value $script:log -Encoding UTF8
exit $exit
