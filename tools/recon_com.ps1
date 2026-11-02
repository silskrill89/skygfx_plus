# READ-ONLY COM registration bounded search for TabShell / Sets.
$ErrorActionPreference = 'Continue'
$targets = @('TabShell','tabshell','Sets')

function Walk-Com($path, $depth) {
    if ($visited -ge $maxVisit) { return }
    $script:visited++
    $key = $null
    try { $key = Get-Item -LiteralPath $path -ErrorAction Stop } catch { return }
    $leaf = Split-Path $path -Leaf
    foreach ($t in $targets) {
        if ($leaf.IndexOf($t, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            Write-Output ("KEYHIT {0}" -f $path)
            $script:hitCount++
            $script:examples.Add($path)
            # print default value
            try {
                $def = $key.GetValue('')
                Write-Output ("  default={0}" -f $def)
            } catch {}
            break
        }
    }
    # also scan value data
    foreach ($vn in $key.GetValueNames()) {
        $vd = [string]$key.GetValue($vn)
        foreach ($t in $targets) {
            if ($vd -and $vd.IndexOf($t, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                Write-Output ("VALHIT {0} name={1} value={2}" -f $path,$vn,$vd)
                $script:hitCount++
                if ($script:examples.Count -lt 5) { $script:examples.Add($path) }
                break
            }
        }
    }
    if ($depth -le 0) { return }
    foreach ($sn in $key.GetSubKeyNames()) {
        Walk-Com (Join-Path $path $sn) ($depth - 1)
    }
}

foreach ($root in @('HKLM:\SOFTWARE\Classes\CLSID','HKLM:\SOFTWARE\Classes\Interface')) {
    $visited = 0
    $maxVisit = 60000
    $hitCount = 0
    $examples = New-Object System.Collections.Generic.List[string]
    Write-Output "=== COM SCAN $root ==="
    if (Test-Path -LiteralPath $root) {
        Walk-Com $root 2
        Write-Output ("ROOT={0} VISITED={1} HITS={2}" -f $root,$visited,$hitCount)
        Write-Output ("EXAMPLES: " + (($examples | Select-Object -Unique -First 5) -join ' | '))
    } else {
        Write-Output "(root missing)"
    }
}
