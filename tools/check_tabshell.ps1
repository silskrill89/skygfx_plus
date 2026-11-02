# Check tabshell Current key properties
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"
$key = "$root\$$windows.data.tabshell\Current"

# List all properties
$props = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
if ($props) {
    Write-Host "Properties found:"
    $props.PSObject.Properties | ForEach-Object {
        Write-Host "  $($_.Name) = $($_.Value)"
    }
} else {
    Write-Host "Key not found or no properties"
}

# Also check the parent key
$parentKey = "$root\$$windows.data.tabshell"
$parentProps = Get-ItemProperty -Path $parentKey -ErrorAction SilentlyContinue
if ($parentProps) {
    Write-Host "Parent properties:"
    $parentProps.PSObject.Properties | ForEach-Object {
        Write-Host "  $($_.Name) = $($_.Value)"
    }
} else {
    Write-Host "Parent key not found"
}