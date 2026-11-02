param(
    [string]$Path = 'C:\Windows\System32\twinui.pcshell.dll',
    [string[]]$Subs = @('TabShell','tabshell','TabView','IsTabShellSettingsEnabled','Sets'),
    [string]$OutFile = '',
    [int]$Neighborhood = 0
)
# READ-ONLY: extract actual strings containing target substrings from a binary.
# Extended: -Subs custom substrings, -OutFile capture, -Neighborhood N prints the
# N runs immediately before/after each matching UTF-16/ASCII run (N=20 -> ~40 around).
$ErrorActionPreference = 'Continue'

$script:lines = New-Object System.Collections.Generic.List[string]
function Emit($m) {
    $s = [string]$m
    Write-Output $s
    [void]$script:lines.Add($s)
}
function Cap($s, $n) {
    if ($null -eq $s) { return '' }
    if ($s.Length -le $n) { return $s }
    return $s.Substring(0, $n) + ('...(truncated,total {0} chars)' -f $s.Length)
}

if (-not (Test-Path -LiteralPath $Path)) { Emit "MISSING $Path"; exit }

$bytes = [System.IO.File]::ReadAllBytes($Path)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
$uni   = [System.Text.Encoding]::Unicode.GetString($bytes)

function Get-Strings($text, $tag, $subs, $unit) {
    foreach ($s in $subs) {
        $idx = 0
        while ($true) {
            $idx = $text.IndexOf($s, $idx, [System.StringComparison]::Ordinal)
            if ($idx -lt 0) { break }
            $start = $idx; $end = $idx + $s.Length
            $lo = $start
            while ($lo -gt 0 -and [int][char]$text[$lo-1] -ge 32 -and [int][char]$text[$lo-1] -le 126) { $lo-- }
            $hi = $end
            while ($hi -lt $text.Length -and [int][char]$text[$hi] -ge 32 -and [int][char]$text[$hi] -le 126) { $hi++ }
            $run = $text.Substring($lo, $hi-$lo)
            if ($run.Length -ge $s.Length) {
                Emit ("[{0}] @{1} => {2}" -f $tag, ($idx * $unit), (Cap $run 2000))
            }
            $idx++
        }
    }
}

function Get-Neighborhood($text, $tag, $unit, $subs, $n) {
    $rx = New-Object System.Text.RegularExpressions.Regex('[\x20-\x7E]{4,}')
    $ms = $rx.Matches($text)
    $hitIdx = New-Object System.Collections.Generic.List[int]
    for ($i = 0; $i -lt $ms.Count; $i++) {
        $s = $ms[$i].Value
        foreach ($sub in $subs) {
            if ($s.IndexOf($sub, [System.StringComparison]::Ordinal) -ge 0) { [void]$hitIdx.Add($i); break }
        }
    }
    Emit ("[{0}] total printable runs={1}, runs containing target subs={2}" -f $tag, $ms.Count, $hitIdx.Count)
    Emit ("[{0}] MATCHING RUNS (byte offset => full run):" -f $tag)
    if ($hitIdx.Count -eq 0) { Emit ("[{0}]   (none)" -f $tag) }
    foreach ($i in $hitIdx) {
        Emit ("[{0}]   @{1} => {2}" -f $tag, ($ms[$i].Index * $unit), (Cap $ms[$i].Value 2000))
    }
    Emit ("[{0}] NEIGHBORHOOD (+/- {1} runs around each match):" -f $tag, $n)
    $win = New-Object 'System.Collections.Generic.SortedSet[int]'
    foreach ($i in $hitIdx) {
        $lo = [Math]::Max(0, $i - $n); $hi = [Math]::Min($ms.Count - 1, $i + $n)
        for ($j = $lo; $j -le $hi; $j++) { [void]$win.Add($j) }
    }
    $last = -2
    foreach ($j in $win) {
        if ($j -ne ($last + 1)) { Emit ("[{0}]   --- context break ---" -f $tag) }
        Emit ("[{0}]   @{1} {2}" -f $tag, ($ms[$j].Index * $unit), (Cap $ms[$j].Value 400))
        $last = $j
    }
}

if ($Neighborhood -gt 0) {
    Emit ("=== RUN-NEIGHBORHOOD SCAN of {0} ===" -f $Path)
    Emit ("=== targets: {0} ===" -f ($subs -join ', '))
    Emit '=== UTF-16LE (byte offset = decoded index * 2) ==='
    Get-Neighborhood $uni 'U' 2 $Subs $Neighborhood
    Emit '=== ASCII (byte offset = decoded index * 1) ==='
    Get-Neighborhood $ascii 'A' 1 $Subs $Neighborhood
} else {
    Emit "=== ASCII strings in $Path ==="
    Get-Strings $ascii 'A' $Subs 1
    Emit "=== UTF-16LE strings in $Path ==="
    Get-Strings $uni 'U' $Subs 2
}

if ($OutFile -ne '') {
    Set-Content -LiteralPath $OutFile -Value $script:lines -Encoding UTF8
    Emit ("WROTE {0} ({1} lines)" -f $OutFile, $script:lines.Count)
}
