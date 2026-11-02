# Check one of the tile collection keys to understand structure
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"

# Check a curated tile collection key
$tileKey = "$root\$de${b05253d7-73fb-4aff-9206-5e05a75f6e81}$$windows.data.curatedtilecollection.tilecollectioncontainer"

Write-Host "=== Checking tile collection key ==="
Write-Host "Key: $tileKey"

try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($tileKey)
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
    } else {
        Write-Host "Key not found"
    }
} catch {
    Write-Host "Failed to read tile collection: " + $_.Exception.Message
}

# Check if it has a Current subkey
$currentTileKey = "$tileKey\Current"
Write-Host ""
Write-Host "=== Checking Current subkey ==="
Write-Host "Key: $currentTileKey"

try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($currentTileKey)
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
    } else {
        Write-Host "Current subkey not found"
    }
} catch {
    Write-Host "Failed to read Current subkey: " + $_.Exception.Message
}