# Now that we can write, let's try to reconstruct the tabshell data structure
# Based on typical Windows CloudStore tabshell schema
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"
$currentKey = "$root\$$windows.data.tabshell\Current"

# Try to write a more complete tabshell structure
# Based on typical tabshell data: version, timestamp, tile collections, app list, etc.

# First, let's check what the schema might look like by examining related keys
Write-Host "=== Checking related CloudStore keys for structure hints ==="

# Check if there's any data in the parent tabshell key
$parentKey = "$root\$$windows.data.tabshell"
try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\$$windows.data.tabshell")
    if ($key) {
        $names = $key.GetValueNames()
        Write-Host "Parent tabshell values: $($names -join ', ')"
        foreach ($name in $names) {
            $val = $key.GetValue($name)
            Write-Host "  $name = $val"
        }
        $key.Close()
    }
} catch {
    Write-Host "Parent key read failed: " + $_.Exception.Message
}

# Check Current key values
try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\$$windows.data.tabshell\Current")
    if ($key) {
        $names = $key.GetValueNames()
        Write-Host "Current tabshell values: $($names -join ', ')"
        foreach ($name in $names) {
            $val = $key.GetValue($name)
            Write-Host "  $name = $val"
        }
        $key.Close()
    }
} catch {
    Write-Host "Current key read failed: " + $_.Exception.Message
}

# Now let's try to write a reconstructed tabshell structure
# Based on typical CloudStore tabshell data format:
# - Version (DWORD)
# - Timestamp (QWORD - FILETIME)
# - Data (BINARY - the actual tab configuration)

Write-Host ""
Write-Host "=== Writing reconstructed tabshell structure ==="

try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\$$windows.data.tabshell\Current")
    
    # Version - typically 1 or 2
    $key.SetValue("Version", 2, [Microsoft.Win32.RegistryValueKind]::DWord)
    Write-Host "Wrote Version = 2"
    
    # Timestamp - current FILETIME
    $timestamp = [DateTime]::UtcNow.ToFileTimeUtc()
    $key.SetValue("Timestamp", $timestamp, [Microsoft.Win32.RegistryValueKind]::QWord)
    Write-Host "Wrote Timestamp = $timestamp"
    
    # Data - binary blob representing tab configuration
    # This would typically contain: tile collections, app list, settings
    # For now, write a minimal valid structure
    $data = @(
        0x02, 0x00, 0x00, 0x00,  # Version 2
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # Placeholder timestamp
        0x01, 0x00, 0x00, 0x00,  # Number of tile collections (1)
        0x00, 0x00, 0x00, 0x00   # Padding
    )
    $key.SetValue("Data", $data, [Microsoft.Win32.RegistryValueKind]::Binary)
    Write-Host "Wrote Data (binary) = " + [BitConverter]::ToString($data).Replace("-", "")
    
    $key.Close()
    Write-Host "Reconstructed tabshell structure written successfully"
} catch {
    Write-Host "Write failed: " + $_.Exception.Message
}

# Read back to verify
try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\$$windows.data.tabshell\Current")
    if ($key) {
        $names = $key.GetValueNames()
        Write-Host "Current tabshell values after write: $($names -join ', ')"
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
    Write-Host "Read back failed: " + $_.Exception.Message
}