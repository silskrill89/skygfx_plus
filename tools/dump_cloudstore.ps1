# Dump sibling CloudStore caches + tabshell Current Data blobs for schema comparison
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"

$siblings = @(
    "$$windows.data.tabshell",
    "$$windows.data.notifications.quiethourssettings",
    "$$windows.data.platform.partitioning.activepartitions",
    "$$windows.data.platform.partitioning.systempartitionindex"
)

foreach ($s in $siblings) {
    $key = "$root\$s\Current"
    try {
        $data = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if ($data -and $data.Data) {
            $hex = [BitConverter]::ToString($data.Data).Replace("-", "")
            Write-Host "=== $s ==="
            Write-Host "Data (hex): $hex"
            Write-Host "Data (raw bytes):"
            foreach ($b in $data.Data) {
                Write-Host ("  {0:X2}" -f $b) -NoNewline
            }
            Write-Host ""
            Write-Host ""
        } else {
            Write-Host "=== $s === NOT FOUND"
        }
    } catch {
        Write-Host "=== $s === ERROR: " + $_.Exception.Message
    }
}