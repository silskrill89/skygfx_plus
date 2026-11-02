# Attempt to initialize the tabshell by creating the key structure
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\Cache\DefaultAccount"

# Create the tabshell key if it doesn't exist
$tabshellKey = "$root\$$windows.data.tabshell"
if (-not (Test-Path $tabshellKey)) {
    try {
        New-Item -Path $tabshellKey -Type "Registry" -Force | Out-Null
        Write-Host "Created tabshell key: $tabshellKey"
    } catch {
        Write-Host "Failed to create tabshell key: " + $tabshellKey
    }
}

# Try to create the Current subkey
$currentKey = "$tabshellKey\Current"
if (-not (Test-Path $currentKey)) {
    try {
        New-Item -Path $currentKey -Type "Registry" -Force | Out-Null
        Write-Host "Created tabshell Current key: $currentKey"
    } catch {
        Write-Host "Failed to create tabshell Current key: " + $currentKey
    }
}

# Try to set a sample property to verify the key is writable
$sampleProp = "TestTabSetting"
try {
    Set-ItemProperty -Path $currentKey -Property $sampleProp -Value "SampleValue" -Force
    Write-Host "Successfully wrote $sampleProp = SampleValue to tabshell Current"
} catch {
    Write-Host "Failed to write sample property: " + $sampleProp
}

# Now try to read it back
if (Test-Path $currentKey) {
    $prop = Get-ItemProperty -Path $currentKey -ErrorAction SilentlyContinue
    if ($prop) {
        Write-Host "Read back tabshell Current: $($prop.PSObject.Properties | ForEach-Object { $_.Name + " = " + $_.Value })"
    }
} else {
    Write-Host "Current key still not accessible after initialization"
}
