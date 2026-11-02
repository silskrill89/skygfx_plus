# READ-ONLY CBS/packages via DISM (filtered for tab/shell/sets).
$ErrorActionPreference = 'Continue'
Write-Output "=== DISM /online /get-packages (filtered) ==="
try {
    $out = & dism /online /get-packages /English 2>&1
    $all = ($out | Out-String)
    Write-Output ("TOTAL_LINES=" + ($out.Count))
    $matchRe = [regex]'(?i)(tab|shell|sets|twinui|explorerframe|experience)'
    $hits = 0
    $inPkgs = $false
    foreach ($line in $out) {
        if ($line -match '^\s*Package Identity\s*:') { $inPkgs = $true }
        if ($matchRe.IsMatch($line)) {
            Write-Output ("PKGLINE " + $line)
            $hits++
        }
    }
    Write-Output ("MATCH_LINES=" + $hits)
} catch {
    Write-Output ("dism ERR " + $_.Exception.Message)
}

Write-Output "=== DISM feature summary (tab/shell/sets) ==="
try {
    $feat = & dism /online /get-features /English 2>&1
    foreach ($line in $feat) {
        if ($line -match '(?i)(tab|shell|sets|twinui|explorer)') {
            Write-Output ("FEATLINE " + $line)
        }
    }
} catch {
    Write-Output ("dism feature ERR " + $_.Exception.Message)
}
