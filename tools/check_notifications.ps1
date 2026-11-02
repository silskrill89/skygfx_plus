# Check notifications.quiethourssettings key structure for comparison
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"

# Check notifications.quiethourssettings
Write-Host "=== notifications.quiethourssettings ==="
reg query "$root\$$windows.data.notifications.quiethourssettings" 2>&1

Write-Host ""
Write-Host "=== notifications.quiethourssettings\Current ==="
reg query "$root\$$windows.data.notifications.quiethourssettings\Current" 2>&1

Write-Host ""
Write-Host "=== notifications.quiethourssettings\Current\s ==="
reg query "$root\$$windows.data.notifications.quiethourssettings\Current\s" 2>&1

# Also check the tabshell key more carefully
Write-Host ""
Write-Host "=== tabshell full ==="
reg query "$root\$$windows.data.tabshell" /s 2>&1