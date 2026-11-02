# Fix binary data write and try again
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"
$currentKey = "$root\$$windows.data.tabshell\Current"

Write-Host "=== Writing reconstructed tabshell structure (fixed binary) ==="

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
    # Create proper byte array
    $data = [byte[]]@(
        0x02, 0x00, 0x00, 0x00,  # Version 2
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # Placeholder timestamp (8 bytes)
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