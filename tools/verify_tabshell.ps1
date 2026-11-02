# Verify the tabshell structure we created
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"
$currentKey = "$root\$$windows.data.tabshell\Current"

try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\$$windows.data.tabshell\Current")
    if ($key) {
        $names = $key.GetValueNames()
        Write-Host "Current tabshell values: $($names -join ', ')"
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
        Write-Host "Failed to read back"
    }
} catch {
    Write-Host "Current key not accessible"
}