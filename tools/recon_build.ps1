# READ-ONLY build identity from HKLM.
$ErrorActionPreference = 'Continue'
$cv = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
Write-Output "=== BUILD IDENTITY ($cv) ==="
foreach ($n in @('CurrentBuild','CurrentBuildNumber','UBR','ProductName','DisplayVersion','ReleaseId','BuildLabEx','BuildLab','CompositionEditionID','InstallationType','EditionID','RegisteredOwner')) {
    try {
        $v = (Get-Item -LiteralPath $cv -ErrorAction Stop).GetValue($n)
        Write-Output ("{0}={1}" -f $n, $v)
    } catch {
        Write-Output ("{0}=<ERR {1}>" -f $n, $_.Exception.Message)
    }
}
Write-Output "=== UBR as raw DWORD ==="
try {
    $k = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion')
    foreach ($n in @('CurrentBuild','CurrentBuildNumber','UBR','BuildLabEx')) {
        $vk = $k.GetValueKind($n)
        $vv = $k.GetValue($n)
        Write-Output ("{0} kind={1} value={2}" -f $n, $vk, $vv)
    }
    $k.Close()
} catch {
    Write-Output ("ERR " + $_.Exception.Message)
}
