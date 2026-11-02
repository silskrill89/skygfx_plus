# enable_tabshell.ps1 - Restore the Windows 10 BETA "Sets"/TabShell feature gates.
# SCOPE: ONLY the enumerated keys (TabShellAppSettings HKCU/HKLM, six FeatureManagement
# Overrides\4 gate subkeys, CloudStore tabshell hygiene). Nothing else is touched.
# Run stages: -Step all|1|2|3|4|5|6|7|8 (or preflight|strings|hkcu|hklm|cloudstore|restart|postverify|rollback)
param(
    [ValidateSet('all','1','2','3','4','5','6','7','8','preflight','strings','hkcu','hklm','cloudstore','restart','postverify','rollback')]
    [string]$Step = 'all'
)

$ErrorActionPreference = 'Continue'
$ToolsDir      = Split-Path -Parent $MyInvocation.MyCommand.Path
$GateBeforeFile = Join-Path $ToolsDir 'tabshell_gate_before.txt'
$GateAfterFile  = Join-Path $ToolsDir 'tabshell_gate_after.txt'
$StringsFile    = Join-Path $ToolsDir 'tabshell_appsettings_strings.txt'
$ElevScript     = Join-Path $ToolsDir 'enable_tabshell_ELEVATED.ps1'
$ElevResultFile = Join-Path $ToolsDir 'tabshell_elevated_result.txt'

# '$$' built from char codes so nothing in this file can be mangled by interpolation.
$cc = [char]0x24
$dq = $cc.ToString() + $cc.ToString()

$OverBase = 'SYSTEM\CurrentControlSet\Control\FeatureManagement\Overrides'
$Buckets  = @('0','4','8','9','15')

# fid -> obfuscated subkey name (algorithm verified against real published keys)
$Gates = @(
    @{ Fid = 10727725; Name = 'TabShell';                                Sub = '1527625864' },
    @{ Fid = 11895592; Name = 'TabShellWinRtApi';                        Sub = '1365873800' },
    @{ Fid = 13849566; Name = 'TabShellVisibility';                      Sub = '3167312009' },
    @{ Fid = 14291807; Name = 'TabShellVisibilityOverrideToOnForGates';  Sub = '3192474248' },
    @{ Fid = 17093654; Name = 'TabSettingStateRepoIntegration';          Sub = '768013962' },
    @{ Fid = 17057765; Name = 'TabShellAutoTabbingPreference';           Sub = '3403740811' }
)

# Expected signature of the fabricated BOGUS CloudStore key (step 5 delete-guard)
$BogusDataHex = '02-00-00-00-00-00-00-00-00-00-00-00-01-00-00-00-00-00-00-00'
# The VALID 21-byte "Sets ON" record body (FILETIME at 0-indexed bytes 4..11)
$GoodDataHex  = '02-00-00-00-51-1A-48-FB-C1-CD-D3-01-00-00-00-00-43-42-01-00-00'

function HexDump([byte[]]$b) {
    if ($null -eq $b) { return '(null)' }
    return (($b | ForEach-Object { $_.ToString('X2') }) -join '-')
}
function Ok($m) { Write-Output ('[OK] ' + $m) }
function Info($m) { Write-Output ('[*] ' + $m) }

# ---------------------------------------------------------------- STEP 1
function Step1-Preflight {
    Write-Output '===== STEP 1: PRE-FLIGHT SNAPSHOT ====='
    $out = New-Object System.Collections.Generic.List[string]
    [void]$out.Add(('PRE-FLIGHT SNAPSHOT {0}' -f (Get-Date -Format o)))
    $lm = [Microsoft.Win32.Registry]::LocalMachine
    foreach ($b in $Buckets) {
        $p = $OverBase + '\' + $b
        [void]$out.Add(('=== HKLM\{0} ===' -f $p))
        $k = $lm.OpenSubKey($p)
        if ($null -eq $k) { [void]$out.Add('  (bucket key ABSENT)'); continue }
        [void]$out.Add(('  bucket value count={0} subkey count={1}' -f $k.GetValueNames().Count, $k.SubKeyCount))
        foreach ($vn in $k.GetValueNames()) {
            $kind = $k.GetValueKind($vn)
            $val  = $k.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            $vs = if ($val -is [byte[]]) { HexDump $val } else { [string]$val }
            $nm = if ($vn -eq '') { '(Default)' } else { $vn }
            [void]$out.Add(('  VALUE {0} = {1}:{2}' -f $nm, $kind, $vs))
        }
        foreach ($sn in $k.GetSubKeyNames()) {
            $s = $k.OpenSubKey($sn)
            if ($null -eq $s) { continue }
            $parts = @()
            foreach ($vn in $s.GetValueNames()) {
                $kind = $s.GetValueKind($vn)
                $val  = $s.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                $vs = if ($val -is [byte[]]) { HexDump $val } else { [string]$val }
                $nm = if ($vn -eq '') { '(Default)' } else { $vn }
                $parts += ('{0}={1}:{2}' -f $nm, $kind, $vs)
            }
            [void]$out.Add(('  {0} :: {1}' -f $sn, ($parts -join ' | ')))
            $s.Close()
        }
        $k.Close()
    }

    [void]$out.Add('=== SIX OBFUSCATED NAME PRESENCE CHECK (buckets 0/4/8/9/15) ===')
    foreach ($g in $Gates) {
        $found = @()
        foreach ($b in $Buckets) {
            $k = $lm.OpenSubKey($OverBase + '\' + $b)
            if ($null -ne $k) {
                if (@($k.GetSubKeyNames()) -contains $g.Sub) { $found += $b }
                $k.Close()
            }
        }
        $state = if ($found.Count -eq 0) { 'ABSENT (expected pre-state)' } else { ('PRESENT in bucket(s): ' + ($found -join ',')) }
        $line = ('  fid {0} {1} -> subkey {2} : {3}' -f $g.Fid, $g.Name, $g.Sub, $state)
        [void]$out.Add($line)
        Write-Output $line
    }

    # 3-5 examples of EXISTING Overrides\4 format (console + file)
    [void]$out.Add('=== FORMAT EXAMPLES: existing Overrides\4 subkeys ===')
    $k4 = $lm.OpenSubKey($OverBase + '\4')
    if ($null -eq $k4) {
        [void]$out.Add('  (Overrides\4 ABSENT)')
        Write-Output 'FORMAT EXAMPLES: Overrides\4 ABSENT'
    } else {
        $names = @($k4.GetSubKeyNames())
        Info (('Overrides\4 currently has {0} subkeys' -f $names.Count))
        $examples = @()
        foreach ($sn in $names) {
            $s = $k4.OpenSubKey($sn)
            $parts = @()
            foreach ($vn in $s.GetValueNames()) {
                $kind = $s.GetValueKind($vn)
                $nm = if ($vn -eq '') { '(Default)' } else { $vn }
                $parts += ('{0}({1})' -f $nm, $kind)
            }
            $s.Close()
            $examples += ('  {0} :: value names/kinds: {1}' -f $sn, ($parts -join ', '))
            if ($examples.Count -ge 5) { break }
        }
        foreach ($e in $examples) {
            [void]$out.Add($e)
            Write-Output $e
        }
        $k4.Close()
    }

    Set-Content -LiteralPath $GateBeforeFile -Value $out -Encoding UTF8
    Ok (('pre-flight snapshot written to {0}' -f $GateBeforeFile))
}

# ---------------------------------------------------------------- STEP 2
function Step2-Strings {
    Write-Output '===== STEP 2: TabShellAppSettings / AllowSets STRING DISCOVERY ====='
    $dll = 'C:\Windows\System32\twinui.pcshell.dll'
    $rs  = Join-Path $ToolsDir 'recon_strings.ps1'
    Info ('scanning {0} via {1}' -f $dll, $rs)
    # UTF-16 runs containing the targets + the ~40 runs immediately around them (20 before + 20 after)
    & $rs -Path $dll -Subs @('TabShellAppSettings','AllowSets') -Neighborhood 20 -OutFile $StringsFile
    Ok (('findings recorded in {0}' -f $StringsFile))
}

# ---------------------------------------------------------------- STEP 3
function Step3-HKCU {
    Write-Output '===== STEP 3: HKCU TabShellAppSettings (no elevation needed) ====='
    $rel = 'Software\Microsoft\Windows\CurrentVersion\TabShellAppSettings'
    Write-Output ('HKCU path: HKCU\' + $rel)
    # Type: REG_DWORD 1 (default inference from a boolean-ish settings value name;
    # step 2 string evidence may refine this - re-run step 3 if so)
    $k = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($rel)
    $k.SetValue('AllowSets', 1, [Microsoft.Win32.RegistryValueKind]::DWord)
    $k.Close()
    Ok 'HKCU CreateSubKey + SetValue AllowSets=1 (DWord) returned without error'
    # readback
    $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($rel)
    if ($null -eq $k) { Write-Output 'READBACK FAIL: key vanished'; return }
    $v    = $k.GetValue('AllowSets', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    $kind = $k.GetValueKind('AllowSets')
    $subs = @($k.GetSubKeyNames())
    $k.Close()
    Write-Output ('READBACK HKCU\...\TabShellAppSettings\AllowSets = {0} (kind={1}) subkeys=[{2}]' -f $v, $kind, ($subs -join ','))
    if ($v -eq 1 -and [string]$kind -eq 'DWord') { Ok 'HKCU VERIFIED' } else { Write-Output 'HKCU VERIFICATION MISMATCH' }
}

# ---------------------------------------------------------------- STEP 4
function Write-HKLM-Stuff {
    $lm = [Microsoft.Win32.Registry]::LocalMachine
    $rel = 'SOFTWARE\Microsoft\Windows\CurrentVersion\TabShellAppSettings'
    Write-Output ('HKLM path: HKLM\' + $rel)
    $k = $lm.CreateSubKey($rel)
    $k.SetValue('AllowSets', 1, [Microsoft.Win32.RegistryValueKind]::DWord)
    $k.Close()
    Ok 'HKLM TabShellAppSettings\AllowSets = 1 (DWord) written'

    $ovRel = $OverBase + '\4'
    Write-Output ('HKLM gate store: HKLM\' + $ovRel)
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
        Ok ('gate {0} (fid {1} {2}) written: EnabledState=2 EnabledStateOptions=0 Variant=1 VariantPayload=0 VariantPayloadKind=0' -f $g.Sub, $g.Fid, $g.Name)
    }
    $ov.Close()
}

function Step4-HKLM {
    Write-Output '===== STEP 4: HKLM WRITES (elevation may be required) ====='
    $ok = $false
    try {
        Write-HKLM-Stuff
        $ok = $true
        Ok 'HKLM writes completed IN-PROCESS'
    } catch [System.UnauthorizedAccessException] {
        Write-Output ('IN-PROCESS DENIED (UnauthorizedAccessException): ' + $_.Exception.Message)
    } catch [System.Security.SecurityException] {
        Write-Output ('IN-PROCESS DENIED (SecurityException): ' + $_.Exception.Message)
    } catch {
        Write-Output ('IN-PROCESS ERROR: {0} : {1}' -f $_.Exception.GetType().FullName, $_.Exception.Message)
    }

    if ($ok) {
        Info 'elevation not needed; capturing gate after-state'
        $k4 = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($OverBase + '\4')
        $lines = New-Object System.Collections.Generic.List[string]
        [void]$lines.Add(('GATE AFTER (in-process write) {0}' -f (Get-Date -Format o)))
        foreach ($g in $Gates) {
            $s = $k4.OpenSubKey($g.Sub)
            if ($null -eq $s) { [void]$lines.Add(('  {0} MISSING' -f $g.Sub)); continue }
            $es = $s.GetValue('EnabledState', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            [void]$lines.Add(('  {0} (fid {1} {2}) EnabledState={3}' -f $g.Sub, $g.Fid, $g.Name, $es))
            $s.Close()
        }
        $k4.Close()
        Set-Content -LiteralPath $GateAfterFile -Value $lines -Encoding UTF8
        Ok (('gate after-state written to {0}' -f $GateAfterFile))
        return
    }

    # Access denied in-process -> one single elevation attempt, never looped.
    if (-not (Test-Path -LiteralPath $ElevScript)) {
        Write-Output ('ELEVATION_REQUIRED (missing helper script {0})' -f $ElevScript)
        return
    }
    Info ('launching elevated helper ONCE: ' + $ElevScript)
    try {
        $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $ElevScript + '"'))
        $p = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $argList -Wait -PassThru
        Write-Output ('ELEVATED PROCESS EXIT CODE: ' + $p.ExitCode)
    } catch {
        Write-Output ('ELEVATION LAUNCH FAILED: {0} : {1}' -f $_.Exception.GetType().FullName, $_.Exception.Message)
        Write-Output 'ELEVATION_REQUIRED'
    }
    if (Test-Path -LiteralPath $ElevResultFile) {
        Write-Output ('--- {0} ---' -f $ElevResultFile)
        Get-Content -LiteralPath $ElevResultFile | ForEach-Object { Write-Output $_ }
    } else {
        Write-Output 'ELEVATION_REQUIRED (no result file produced - UAC declined or launch failed)'
    }
}

# ---------------------------------------------------------------- STEP 5
function Step5-CloudStore {
    Write-Output '===== STEP 5: CLOUDSTORE HYGIENE ====='
    $base = 'Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount'
    $bogusName = 'windows.data.tabshell'                       # NO '$' prefix at all
    $goodName  = $dq + 'windows.data.tabshell'                 # the VALID '$$windows.data.tabshell'
    Write-Output ('Bogus path (delete-candidate): HKCU\' + $base + '\' + $bogusName)
    Write-Output ('Valid path (never touch)    : HKCU\' + $base + '\' + $goodName + '\Current')
    Write-Output ('(good name built from char codes: 0x24 0x24 + "windows.data.tabshell")')

    $cu = [Microsoft.Win32.Registry]::CurrentUser

    # --- 5a. BOGUS key: print signature, delete ONLY on exact match ---
    $bogusRel = $base + '\' + $bogusName
    $bk = $cu.OpenSubKey($bogusRel, $true)
    if ($null -eq $bk) {
        Write-Output 'BOGUS_KEPT_SIGNATURE_MISMATCH (key not present - nothing to delete)'
    } else {
        Write-Output ('--- BOGUS KEY DUMP: HKCU\' + $bogusRel + ' ---')
        $names = @($bk.GetValueNames())
        $sig = @{}
        foreach ($vn in $names) {
            $kind = $bk.GetValueKind($vn)
            $val  = $bk.GetValue($vn, $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            $vs = if ($val -is [byte[]]) { HexDump $val } else { [string]$val }
            $sig[$vn] = @{ Kind = [string]$kind; Val = $val; Vs = $vs }
            Write-Output ('  name={0} kind={1} value={2}' -f $vn, $kind, $vs)
        }
        $bk.Close()

        # signature: exactly {TestTabSetting, Version, Timestamp, Data}
        $match = $true
        $expectNames = @('TestTabSetting','Version','Timestamp','Data')
        if ($names.Count -ne $expectNames.Count) { $match = $false; Write-Output ('  SIG FAIL: value count {0} != 4' -f $names.Count) }
        foreach ($en in $expectNames) { if ($names -notcontains $en) { $match = $false; Write-Output ('  SIG FAIL: missing value ' + $en) } }
        if ($sig.ContainsKey('TestTabSetting')) {
            if ($sig['TestTabSetting'].Kind -ne 'String' -or $sig['TestTabSetting'].Val -ne 'SampleValue') { $match = $false; Write-Output '  SIG FAIL: TestTabSetting != String "SampleValue"' }
        }
        if ($sig.ContainsKey('Version')) {
            if ($sig['Version'].Kind -ne 'DWord' -or [int]$sig['Version'].Val -ne 2) { $match = $false; Write-Output '  SIG FAIL: Version != DWord 2' }
        }
        if ($sig.ContainsKey('Timestamp')) {
            if ($sig['Timestamp'].Kind -ne 'QWord') { $match = $false; Write-Output '  SIG FAIL: Timestamp kind != QWord' }
        }
        if ($sig.ContainsKey('Data')) {
            $d = $sig['Data'].Val
            $hex = if ($d -is [byte[]]) { HexDump $d } else { '(not binary)' }
            if ($sig['Data'].Kind -ne 'Binary' -or -not ($d -is [byte[]]) -or $d.Length -ne 20 -or $hex -ne $BogusDataHex) {
                $match = $false; Write-Output ('  SIG FAIL: Data != Binary 20 bytes {0}' -f $BogusDataHex)
            }
        }

        if ($match) {
            $parent = $cu.OpenSubKey($base, $true)
            $parent.DeleteSubKeyTree($bogusName, $false)
            $parent.Close()
            Write-Output 'BOGUS_DELETED'
            Ok ('deleted HKCU\' + $bogusRel + ' (signature matched exactly)')
        } else {
            Write-Output 'BOGUS_KEPT_SIGNATURE_MISMATCH'
            Info 'bogus key kept - its contents do not match the fabricated signature; delete manually if desired'
        }
    }

    # --- 5b. VALID '$$windows.data.tabshell\Current\Data' verify + optional FILETIME refresh ---
    $goodRel = $base + '\' + $goodName + '\Current'
    $gk = $cu.OpenSubKey($goodRel, $true)
    if ($null -eq $gk) {
        Write-Output ('GOOD KEY MISSING: HKCU\' + $goodRel)
        return
    }
    $data = $gk.GetValue('Data', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
    $kind = $gk.GetValueKind('Data')
    Write-Output ('GOOD KEY Data: kind={0} len={1} hex={2}' -f $kind, $(if ($data -is [byte[]]) { $data.Length } else { -1 }), (HexDump $data))

    $valid = ($data -is [byte[]]) -and ($data.Length -eq 21)
    if (-not $valid) {
        Write-Output 'GOOD DATA UNEXPECTED SHAPE (not 21 bytes) - left untouched'
        $gk.Close(); return
    }

    $oldFt = [BitConverter]::ToInt64($data, 4)
    $oldDt = [DateTime]::FromFileTimeUtc($oldFt)
    Write-Output ('GOOD DATA embedded FILETIME = {0} ({1:u})' -f $oldFt, $oldDt)

    $nowFt = [DateTime]::UtcNow.ToFileTime()
    if ($nowFt -gt $oldFt) {
        # stale timestamp -> rewrite same body with fresh FILETIME (monotonically newer, +2s margin)
        $newFt = $nowFt + 20000000   # 2 s in 100-ns units
        if ($newFt -le $oldFt) { $newFt = $oldFt + 20000000 }
        $newData = New-Object byte[] 21
        [Array]::Copy($data, $newData, 21)
        $ftBytes = [BitConverter]::GetBytes([long]$newFt)
        for ($i = 0; $i -lt 8; $i++) { $newData[4 + $i] = $ftBytes[$i] }
        $gk.SetValue('Data', $newData, [Microsoft.Win32.RegistryValueKind]::Binary)
        $back = $gk.GetValue('Data', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $gk.Close()
        Write-Output ('DATA refreshed hex=' + (HexDump $back))
        Write-Output ('DATA refreshed FILETIME = {0} ({1:u})' -f [BitConverter]::ToInt64($back, 4), [DateTime]::FromFileTimeUtc([BitConverter]::ToInt64($back, 4)))
        Write-Output 'DATA_REFRESHED'
    } else {
        $gk.Close()
        Write-Output 'DATA_OK (embedded FILETIME is not older than now - record left as-is)'
    }
}

# ---------------------------------------------------------------- STEP 6
function Step6-Restart {
    Write-Output '===== STEP 6: RESTART EXPLORER ====='
    & taskkill.exe /IM explorer.exe /F 2>&1 | ForEach-Object { Write-Output $_ }
    Start-Sleep -Seconds 2
    Start-Process explorer.exe
    Ok 'explorer.exe restart issued'
    Start-Sleep -Seconds 10
    $p = Get-Process explorer -ErrorAction SilentlyContinue
    if ($p) {
        foreach ($proc in $p) { Write-Output ('explorer.exe RUNNING pid={0} start={1}' -f $proc.Id, $proc.StartTime) }
    } else {
        Write-Output 'explorer.exe NOT RUNNING after restart window'
    }
}

# ---------------------------------------------------------------- STEP 7
function Step7-PostVerify {
    Write-Output '===== STEP 7: POST-VERIFY ====='
    foreach ($pair in @(@('HKCU','Software\Microsoft\Windows\CurrentVersion\TabShellAppSettings'), @('HKLM','SOFTWARE\Microsoft\Windows\CurrentVersion\TabShellAppSettings'))) {
        $hive = if ($pair[0] -eq 'HKCU') { [Microsoft.Win32.Registry]::CurrentUser } else { [Microsoft.Win32.Registry]::LocalMachine }
        $k = $hive.OpenSubKey($pair[1])
        if ($null -eq $k) {
            Write-Output ('{0}\...\TabShellAppSettings : KEY ABSENT' -f $pair[0])
        } else {
            $v = $k.GetValue('AllowSets', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            $kind = $k.GetValueKind('AllowSets')
            $k.Close()
            Write-Output ('{0}\...\TabShellAppSettings\AllowSets = {1} (kind={2})' -f $pair[0], $v, $kind)
        }
    }

    Write-Output '--- six Overrides\4 gate subkeys ---'
    $k4 = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($OverBase + '\4')
    if ($null -eq $k4) {
        Write-Output ('Overrides\4 ABSENT')
    } else {
        foreach ($g in $Gates) {
            $s = $k4.OpenSubKey($g.Sub)
            if ($null -eq $s) {
                Write-Output ('  {0} (fid {1} {2}) : ABSENT' -f $g.Sub, $g.Fid, $g.Name)
            } else {
                $es = $s.GetValue('EnabledState', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                $eskind = $s.GetValueKind('EnabledState')
                $s.Close()
                Write-Output ('  {0} (fid {1} {2}) : EnabledState={3} (kind={4})' -f $g.Sub, $g.Fid, $g.Name, $es, $eskind)
            }
        }
        $k4.Close()
    }

    Write-Output '--- Settings multitasking page ---'
    try {
        Start-Process 'ms-settings:multitasking'
        Start-Sleep -Seconds 6
        $sp = Get-Process SystemSettings -ErrorAction SilentlyContinue
        if ($sp) {
            foreach ($proc in $sp) {
                Write-Output ('SystemSettings RUNNING pid={0} MainWindowTitle="{1}"' -f $proc.Id, $proc.MainWindowTitle)
            }
            Try-UiDump
        } else {
            Write-Output 'SystemSettings process NOT FOUND'
        }
    } catch {
        Write-Output ('Settings launch failed: ' + $_.Exception.Message)
    }
}

function Try-UiDump {
    Write-Output '--- UIA dump of Settings window (best effort, looking for Sets group) ---'
    try {
        Add-Type -AssemblyName UIAutomationClient -ErrorAction Stop
        Add-Type -AssemblyName UIAutomationTypes -ErrorAction Stop
        $sp = Get-Process SystemSettings -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $sp) { Write-Output 'UIA SKIP: no SystemSettings process'; return }
        $root = [System.Windows.Automation.AutomationElement]::FromHandle($sp.MainWindowHandle)
        $cond = [System.Windows.Automation.Condition]::TrueCondition
        $all = $root.FindAll([System.Windows.Automation.TreeScope]::Descendant, $cond)
        Write-Output ('UIA: {0} descendant elements found' -f $all.Count)
        $hits = 0
        foreach ($e in $all) {
            $name = $e.Current.Name
            if ($null -eq $name -or $name -eq '') { continue }
            if ($name -match '(?i)set|tab') {
                $ct = $e.Current.ControlType.ProgrammaticName
                $r  = $e.Current.BoundingRectangle
                Write-Output ('  UIA name="{0}" type={1} rect=({2},{3},{4}x{5})' -f $name, $ct, [int]$r.X, [int]$r.Y, [int]$r.Width, [int]$r.Height)
                $hits++
                if ($hits -ge 60) { Write-Output '  UIA: hit list truncated at 60'; break }
            }
        }
        if ($hits -eq 0) { Write-Output 'UIA: no elements with "set"/"tab" in name' }
    } catch {
        Write-Output ('UIA dump failed (non-fatal): {0} : {1}' -f $_.Exception.GetType().FullName, $_.Exception.Message)
    }
}

# ---------------------------------------------------------------- STEP 8
function Step8-Rollback {
    Write-Output '===== STEP 8: ROLLBACK (PRINT ONLY - NOT EXECUTED) ====='
    Write-Output '--- run these to undo everything (elevated where noted) ---'
    foreach ($g in $Gates) {
        Write-Output ('reg.exe delete "HKLM\SYSTEM\CurrentControlSet\Control\FeatureManagement\Overrides\4\{0}" /f    REM fid {1} {2}' -f $g.Sub, $g.Fid, $g.Name)
    }
    Write-Output 'reg.exe delete "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\TabShellAppSettings" /f'
    Write-Output 'reg.exe delete "HKCU\Software\Microsoft\Windows\CurrentVersion\TabShellAppSettings" /f'
    Write-Output ''
    Write-Output '--- CloudStore restore: rewrite the original 21-byte Data record (original FILETIME body) ---'
    Write-Output '# save as restore_cloudstore.ps1 and run via powershell -NoProfile -ExecutionPolicy Bypass -File'
    Write-Output ('$cc = [char]0x24')
    Write-Output ('$base = ''Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount''')
    Write-Output ('$rel = $base + ''\'' + $cc.ToString() + $cc.ToString() + ''windows.data.tabshell\Current''')
    Write-Output ('$orig = [byte[]](0x02,0x00,0x00,0x00, 0x51,0x1A,0x48,0xFB,0xC1,0xCD,0xD3,0x01, 0x00,0x00,0x00,0x00, 0x43,0x42,0x01,0x00,0x00)')
    Write-Output ('$k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($rel, $true)')
    Write-Output ('$k.SetValue(''Data'', $orig, [Microsoft.Win32.RegistryValueKind]::Binary)')
    Write-Output ('$k.Close()')
    Write-Output ''
    Write-Output ('BOGUS key (already deleted in step 5; recreate only if you must) originally contained:')
    Write-Output ('  HKCU\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\windows.data.tabshell')
    Write-Output ('    TestTabSetting = REG_SZ "SampleValue"')
    Write-Output ('    Version        = REG_DWORD 2')
    Write-Output ('    Timestamp      = REG_QWORD <fabricated>')
    Write-Output ('    Data           = REG_BINARY {0}' -f $BogusDataHex)
    Write-Output ''

    Write-Output '--- ReconcileFeatures scheduled task status (DO NOT disable; it can revert gate writes) ---'
    $task = $null
    try { $task = Get-ScheduledTask -TaskPath '\Microsoft\Windows\Flighting\' -TaskName 'FeatureConfig' -ErrorAction Stop } catch {}
    if ($null -eq $task) {
        try {
            $task = Get-ScheduledTask -ErrorAction SilentlyContinue | Where-Object { $_.TaskName -match '(?i)FeatureConfig|ReconcileFeatures' } | Select-Object -First 1
        } catch {}
    }
    if ($null -eq $task) {
        Write-Output 'TASK NOT FOUND: \Microsoft\Windows\Flighting\FeatureConfig (searched by name too)'
        Write-Output 'schtasks fallback:'
        & schtasks.exe /Query /TN '\Microsoft\Windows\Flighting\FeatureConfig' /V /FO LIST 2>&1 | ForEach-Object { Write-Output $_ }
    } else {
        Write-Output ('TASK FOUND: {0}{1}' -f $task.TaskPath, $task.TaskName)
        Write-Output ('  State      : {0}' -f $task.State)
        try {
            $info = Get-ScheduledTaskInfo -TaskPath $task.TaskPath -TaskName $task.TaskName -ErrorAction Stop
            Write-Output ('  LastRunTime: {0}' -f $info.LastRunTime)
            Write-Output ('  LastResult : {0}' -f $info.LastTaskResult)
            Write-Output ('  NextRunTime: {0}' -f $info.NextRunTime)
            Write-Output ('  NumMissed  : {0}' -f $info.NumberOfMissedRuns)
        } catch {
            Write-Output ('  (task info unavailable: ' + $_.Exception.Message + ')')
        }
        try {
            $triggers = $task.Triggers
            foreach ($t in $triggers) {
                Write-Output ('  Trigger    : {0} enabled={1} startBoundary={2} repetition={3}' -f $t.CimClass.CimClassName, $t.Enabled, $t.StartBoundary, $t.Repetition.Interval)
            }
        } catch {}
        & schtasks.exe /Query /TN ($task.TaskPath + $task.TaskName) /V /FO LIST 2>&1 | Select-Object -First 40 | ForEach-Object { Write-Output $_ }
    }
}

# ---------------------------------------------------------------- dispatch
switch ($Step) {
    'all' {
        Step1-Preflight
        Step2-Strings
        Step3-HKCU
        Step4-HKLM
        Step5-CloudStore
        Step6-Restart
        Step7-PostVerify
        Step8-Rollback
    }
    '1'         { Step1-Preflight }
    'preflight' { Step1-Preflight }
    '2'         { Step2-Strings }
    'strings'   { Step2-Strings }
    '3'         { Step3-HKCU }
    'hkcu'      { Step3-HKCU }
    '4'         { Step4-HKLM }
    'hklm'      { Step4-HKLM }
    '5'         { Step5-CloudStore }
    'cloudstore'{ Step5-CloudStore }
    '6'         { Step6-Restart }
    'restart'   { Step6-Restart }
    '7'         { Step7-PostVerify }
    'postverify'{ Step7-PostVerify }
    '8'         { Step8-Rollback }
    'rollback'  { Step8-Rollback }
}
Write-Output ('===== enable_tabshell done (step={0}) =====' -f $Step)
