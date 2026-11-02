# Try writing to tabshell Current with different approach
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"
$currentKey = "$root\$$windows.data.tabshell\Current"

# Try using reg.exe directly
Write-Host "=== Using reg.exe to write ==="
& cmd /c "reg add `"$currentKey`" /v TestTabSetting /t REG_SZ /d SampleValue /f" 2>&1

# Try reading back
Write-Host ""
Write-Host "=== Reading back ==="
$result = & cmd /c "reg query `"$currentKey`" /v TestTabSetting" 2>&1
Write-Host $result

# Try using PowerShell with explicit registry access
Write-Host ""
Write-Host "=== Using PowerShell explicit ==="
try {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\$$windows.data.tabshell\Current")
    $key.SetValue("TestTabSetting", "SampleValue", [Microsoft.Win32.RegistryValueKind]::String)
    $key.Close()
    Write-Host "Successfully wrote via explicit registry access"
} catch {
    Write-Host "Explicit registry access failed: " + $_.Exception.Message
}

# Read back
try {
    $key2 = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey("Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount\$$windows.data.tabshell\Current")
    if ($key2) {
        $val = $key2.GetValue("TestTabSetting")
        Write-Host "Read back value: $val"
        $key2.Close()
    }
} catch {
    Write-Host "Read failed: " + $_.Exception.Message
}