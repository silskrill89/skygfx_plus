# Check all CloudStore keys for structure hints
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore"

# Check all subkeys under Store\Cache\DefaultAccount
Write-Host "=== All caches under DefaultAccount ==="
$cacheRoot = "$root\Store\Cache\DefaultAccount"
try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount")
    if ($key) {
        $subkeys = $key.GetSubKeyNames()
        foreach ($sk in $subkeys) {
            Write-Host "  $sk"
        }
        $key.Close()
    }
} catch {
    Write-Host "Failed to list caches: " + $_.Exception.Message
}

# Check Schema subkeys
Write-Host ""
Write-Host "=== Schema subkeys ==="
try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Schema")
    if ($key) {
        $subkeys = $key.GetSubKeyNames()
        foreach ($sk in $subkeys) {
            Write-Host "  $sk"
        }
        $key.Close()
    }
} catch {
    Write-Host "Failed to list Schema: " + $_.Exception.Message
}

# Check SystemMetaData\DefaultAccount
Write-Host ""
Write-Host "=== SystemMetaData\DefaultAccount ==="
try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\SystemMetaData\DefaultAccount")
    if ($key) {
        $names = $key.GetValueNames()
        foreach ($name in $names) {
            $val = $key.GetValue($name)
            Write-Host "  $name = $val"
        }
        $key.Close()
    }
} catch {
    Write-Host "Failed to read SystemMetaData\DefaultAccount: " + $_.Exception.Message
}