# Check tabshell key existence and properties via reg query
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"
$key = "$root\$$windows.data.tabshell"

# List subkeys
Write-Host "=== Subkeys of $$windows.data.tabshell ==="
reg query "$key" 2>&1

# List Current subkey
Write-Host ""
Write-Host "=== $$windows.data.tabshell\Current ==="
reg query "$key\Current" 2>&1

# List all values in Current
Write-Host ""
Write-Host "=== Values in Current ==="
reg query "$key\Current" /s 2>&1