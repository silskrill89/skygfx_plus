# Read tabshell Current Data blob
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"
$key = "$root\$$windows.data.tabshell\Current"

# Try to read raw binary
$result = reg query "$key" /v Data 2>&1
if ($result) {
    Write-Host "Raw data (hex):"
    $result | ForEach-Object { Write-Host -NoNewline -ForegroundColor Yellow -Format "  {0:X2}" -ArgumentObject $_ }
} else {
    Write-Host "Empty or inaccessible"
}