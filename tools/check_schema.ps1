# Check SystemMetaData and Schema for TabShell structure
$ErrorActionPreference = "SilentlyContinue"

$root = "HKCU\Software\Microsoft\Windows\CurrentVersion\CloudStore"

# Check SystemMetaData
Write-Host "=== SystemMetaData ==="
reg query "$root\SystemMetaData" 2>&1

# Check Schema
Write-Host ""
Write-Host "=== Schema ==="
reg query "$root\Schema" 2>&1

# Check Schema\Windows.Data.TabShell
Write-Host ""
Write-Host "=== Schema\Windows.Data.TabShell ==="
reg query "$root\Schema\Windows.Data.TabShell" 2>&1

# Check StoreInit
Write-Host ""
Write-Host "=== StoreInit ==="
reg query "$root\StoreInit" 2>&1

# Check Version
Write-Host ""
Write-Host "=== Version ==="
reg query "$root\Version" 2>&1