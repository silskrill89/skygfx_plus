# Try to reload Windows Settings to verify tabshell data affects dropdowns
$ErrorActionPreference = "SilentlyContinue"

# First, let's verify the tabshell data we created
$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"
$currentKey = "$root\$$windows.data.tabshell\Current"

Write-Host "=== Current tabshell structure ==="
try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\$$windows.data.tabshell\Current")
    if ($key) {
        $names = $key.GetValueNames()
        Write-Host "Values: $($names -join ', ')"
        foreach ($name in $names) {
            $val = $key.GetValue($name)
            if ($name -eq "Data") {
                Write-Host "  $name = " + [BitConverter]::ToString($val).Replace("-", "")
            } else {
                Write-Host "  $name = $val"
            }
        }
        $key.Close()
    }
} catch {
    Write-Host "Failed to read: " + $_.Exception.Message
}

# Try to trigger a Settings refresh
Write-Host ""
Write-Host "=== Attempting Settings refresh ==="

# Method 1: Restart Settings process
Write-Host "Method 1: Restarting Settings..."
& taskkill /IM "Settings.exe" /F 2>$null
Start-Process "Settings.exe" 2>$null

# Wait a moment for Settings to start
Start-Sleep -Seconds 3

# Method 2: Use ms-settings command
Write-Host ""
Write-Host "Method 2: Opening specific Settings page..."
Start-Process "ms-settings:" 2>$null

Start-Sleep -Seconds 5

# Method 3: Try to open the System tab where tabshell settings might be
Write-Host ""
Write-Host "Method 3: Opening System settings..."
Start-Process "ms-settings:system" 2>$null

Start-Sleep -Seconds 5

# Check if the tabshell data is still there
Write-Host ""
Write-Host "=== Verifying tabshell data after Settings reload ==="
try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\$$windows.data.tabshell\Current")
    if ($key) {
        $names = $key.GetValueNames()
        Write-Host "Values after reload: $($names -join ', ')"
        foreach ($name in $names) {
            $val = $key.GetValue($name)
            if ($name -eq "Data") {
                Write-Host "  $name = " + [BitConverter]::ToString($val).Replace("-", "")
            } else {
                Write-Host "  $name = $val"
            }
        }
        $key.Close()
    }
} catch {
    Write-Host "Failed to read after reload: " + $_.Exception.Message
}