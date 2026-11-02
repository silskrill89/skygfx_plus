# Try to write to tabshell Current blob with test data
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"
$key = "$root\$$windows.data.tabshell\Current"

# Try to set a property
Try {
    Set-ItemProperty -Path $key -Property "TestProp" -Value "test_value" -Force
    Write-Host "Successfully wrote TestProp = test_value"
} Catch {
    Write-Host "Failed to write: " + $_.Exception.Message
}

# Also try to read back
if (Test-Path $key) {
    $prop = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
    if ($prop) {
        Write-Host "Read back: $($prop.PSObject.Properties | ForEach-Object { $_.Name + " = " + $_.Value })"
    }
} else {
    Write-Host "Key not found after write attempt"
}