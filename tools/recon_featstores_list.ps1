# READ-ONLY: list feature-management subkeys to characterize the store (no fids present).
$ErrorActionPreference = 'Continue'
foreach ($r in @('HKLM:\SYSTEM\CurrentControlSet\Control\FeatureManagement','HKLM:\SYSTEM\CurrentControlSet\Control\FeatureManagement\Overrides','HKLM:\SOFTWARE\Microsoft\WindowsSelfHost')) {
    Write-Output "=== $r ==="
    $k = Get-Item -LiteralPath $r
    foreach ($sn in $k.GetSubKeyNames()) {
        Write-Output ("  SUBKEY " + $sn)
    }
    Write-Output "  VALUES:"
    foreach ($vn in $k.GetValueNames()) {
        $val = $k.GetValue($vn,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        $kind = $k.GetValueKind($vn)
        if ($val -is [byte[]]) { Write-Output ("    '{0}' kind={1} len={2}" -f $vn,$kind,$val.Length) }
        else { Write-Output ("    '{0}' kind={1} value={2}" -f $vn,$kind,$val) }
    }
}
