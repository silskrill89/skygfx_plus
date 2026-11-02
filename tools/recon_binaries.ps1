# READ-ONLY binary inventory + byte-scan for TabShell/Sets strings.
$ErrorActionPreference = 'Continue'

function Count-Occurrences([string]$haystack, [string]$needle) {
    if ([string]::IsNullOrEmpty($needle)) { return 0 }
    $count = 0
    $idx = 0
    while ($true) {
        $idx = $haystack.IndexOf($needle, $idx, [System.StringComparison]::Ordinal)
        if ($idx -lt 0) { break }
        $count++
        $idx++
    }
    return $count
}

function Scan-File([string]$path) {
    $item = Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
    if (-not $item) {
        return [pscustomobject]@{ File=$path; Size='(missing)'; A_TabShell=''; A_Sets=''; A_IsTab=''; A_TabView=''; A_lcTabShell=''; U_TabShell=''; U_Sets=''; U_IsTab=''; U_TabView=''; U_lcTabShell=''; Err='missing' }
    }
    try {
        $bytes = [System.IO.File]::ReadAllBytes($path)
        $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
        $uni   = [System.Text.Encoding]::Unicode.GetString($bytes)
        return [pscustomobject]@{
            File = $path
            Size = $item.Length
            A_TabShell = (Count-Occurrences $ascii 'TabShell')
            A_Sets     = (Count-Occurrences $ascii 'Sets')
            A_IsTab    = (Count-Occurrences $ascii 'IsTabShellSettingsEnabled')
            A_TabView  = (Count-Occurrences $ascii 'TabView')
            A_lcTabShell = (Count-Occurrences $ascii 'tabshell')
            U_TabShell = (Count-Occurrences $uni 'TabShell')
            U_Sets     = (Count-Occurrences $uni 'Sets')
            U_IsTab    = (Count-Occurrences $uni 'IsTabShellSettingsEnabled')
            U_TabView  = (Count-Occurrences $uni 'TabView')
            U_lcTabShell = (Count-Occurrences $uni 'tabshell')
            Err = ''
        }
    } catch {
        return [pscustomobject]@{ File=$path; Size=$item.Length; A_TabShell=''; A_Sets=''; A_IsTab=''; A_TabView=''; A_lcTabShell=''; U_TabShell=''; U_Sets=''; U_IsTab=''; U_TabView=''; U_lcTabShell=''; Err=$_.Exception.Message }
    }
}

$candidates = New-Object System.Collections.Generic.List[string]
$sys32 = 'C:\Windows\System32'
foreach ($f in @('explorerframe.dll','twinui.pcshell.dll','twinui.dll','ShellExperienceHost.exe','Windows.UI.Shell.dll','SystemSettings.exe','shell32.dll','shlwapi.dll','ShellCommon.dll','Windows.Internal.ShellCommon.dll')) {
    $candidates.Add((Join-Path $sys32 $f))
}
Get-ChildItem -LiteralPath $sys32 -Filter 'SystemSettings*.dll' -ErrorAction SilentlyContinue | ForEach-Object { $candidates.Add($_.FullName) }

$saRoot = 'C:\Windows\SystemApps'
Get-ChildItem -LiteralPath $saRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like '*ShellExperienceHost*' } |
    ForEach-Object {
        $candidates.Add($_.FullName)
        Get-ChildItem -LiteralPath $_.FullName -Recurse -File -Include @('*.dll','*.exe') -ErrorAction SilentlyContinue | ForEach-Object { $candidates.Add($_.FullName) }
    }

$uniq = $candidates | Sort-Object -Unique
Write-Output ("CANDIDATE_COUNT=" + $uniq.Count)
Write-Output "FILE|SIZE|A_TabShell|A_Sets|A_IsTab|A_TabView|A_lcTabshell|U_TabShell|U_Sets|U_IsTab|U_TabView|U_lcTabshell|ERR"
foreach ($p in $uniq) {
    $r = Scan-File $p
    Write-Output ("{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}|{10}|{11}|{12}" -f $r.File,$r.Size,$r.A_TabShell,$r.A_Sets,$r.A_IsTab,$r.A_TabView,$r.A_lcTabShell,$r.U_TabShell,$r.U_Sets,$r.U_IsTab,$r.U_TabView,$r.U_lcTabShell,$r.Err)
}
