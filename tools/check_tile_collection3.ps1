# Check one of the tile collection keys to understand structure
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"

# Check a curated tile collection key - use reg query directly
$guid = "b05253d7-73fb-4aff-9206-5e05a75f6e81"
$tileKey = "$root\$de${guid}$$windows.data.curatedtilecollection.tilecollectioncontainer"

Write-Host "=== Checking tile collection key ==="
Write-Host "Key: $tileKey"

# Use reg query directly
Write-Host ""
Write-Host "=== reg query tile collection ==="
reg query "$tileKey" 2>&1

Write-Host ""
Write-Host "=== reg query tile collection Current ==="
reg query "$tileKey\Current" 2>&1

Write-Host ""
Write-Host "=== reg query tile collection Current /s ==="
reg query "$tileKey\Current" /s 2>&1

# Also check the tabshell Current key we created
Write-Host ""
Write-Host "=== tabshell Current (reg query) ==="
reg query "$root\$$windows.data.tabshell\Current" 2>&1