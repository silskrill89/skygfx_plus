# READ-ONLY runtime host status.
$ErrorActionPreference = 'Continue'
Write-Output "=== RUNNING PROCESSES (filtered) ==="
$patterns = @('explorer','ShellExperienceHost','StartMenuExperienceHost','SearchHost','tab','twinui','SystemSettings','sihost','SearchUI','SearchApp','ctfmon','ShellExperience')
try {
    $procs = Get-Process -ErrorAction Stop
    $found = $procs | Where-Object {
        $n = $_.ProcessName
        foreach ($p in $patterns) { if ($n -like "*$p*") { return $true } }
        return $false
    }
    if ($found) {
        foreach ($pr in ($found | Sort-Object ProcessName)) {
            Write-Output ("PROC {0} pid={1} mem={2}KB" -f $pr.ProcessName, $pr.Id, [math]::Round($pr.WorkingSet64/1KB))
        }
    } else {
        Write-Output "(no matching processes)"
    }
} catch {
    Write-Output ("Get-Process ERR " + $_.Exception.Message)
}

Write-Output "=== SPECIFIC HOST CHECK ==="
foreach ($n in @('explorer','ShellExperienceHost','StartMenuExperienceHost','SearchHost','SearchUI','SearchApp','SystemSettings')) {
    $p = Get-Process -Name $n -ErrorAction SilentlyContinue
    if ($p) {
        foreach ($pp in $p) { Write-Output ("RUNNING {0} pid={1} path={2}" -f $n, $pp.Id, $pp.Path) }
    } else {
        Write-Output ("NOT-RUNNING {0}" -f $n)
    }
}

Write-Output "=== tasklist raw filter ==="
try {
    $tl = & tasklist /fo csv /nh 2>&1
    foreach ($line in $tl) {
        foreach ($p in @('explorer','ShellExperienceHost','StartMenu','SearchHost','tab')) {
            if ($line -like "*$p*") { Write-Output ("TASKLIST " + $line); break }
        }
    }
} catch {
    Write-Output ("tasklist ERR " + $_.Exception.Message)
}
