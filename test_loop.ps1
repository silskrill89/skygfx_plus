# Auto-test loop: launch GTA SA, skip to new game, detect crash, retry
# Usage: Run from PowerShell in the skygfx_plus directory

$gameDir = "E:\games\San Andreas Retro Revised"
$gameExe = "$gameDir\gta_sa.exe"
$logFile = "E:\games\San Andreas Retro Revised\skygfx_dbg.log"
$maxRuns = 20
$launchDelay = 5  # seconds between launches

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public class Win32 {
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte bKey, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    
    [DllImport("user32.dll")]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

    public const byte VK_RETURN = 0x0D;
    public const byte VK_ESCAPE = 0x1B;
    public const byte VK_SPACE = 0x20;
    public const uint KEYEVENTF_KEYUP = 0x0002;
}
"@

function Send-Key([byte]$vk, [int]$delayMs = 50) {
    [Win32]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $delayMs
    [Win32]::keybd_event($vk, 0, [Win32]::KEYEVENTF_KEYUP, [UIntPtr]::Zero)
}

function Clear-Log {
    if (Test-Path $logFile) {
        Set-Content -Path $logFile -Value "" -Force
    }
}

function Wait-For-Window {
    param([string]$title, [int]$timeoutSec = 30)
    $elapsed = 0
    while ($elapsed -lt $timeoutSec) {
        $h = [Win32]::FindWindow($null, $title)
        if ($h -ne [IntPtr]::Zero) { return $true }
        Start-Sleep -Seconds 1
        $elapsed++
    }
    return $false
}

function Navigate-To-NewGame {
    param([int]$waitSec = 2)
    
    Write-Host "  Waiting for game window..."
    Start-Sleep -Seconds $waitSec
    
    # Find and focus the window
    $hWnd = [Win32]::FindWindow($null, $null)
    if ($hWnd -ne [IntPtr]::Zero) {
        [Win32]::SetForegroundWindow($hWnd)
        Start-Sleep -Milliseconds 500
    }
    
    # GTA SA startup sequence:
    # 1. Press Escape to skip intro videos (multiple presses)
    Write-Host "  Skipping intro videos..."
    for ($i = 0; $i -lt 3; $i++) {
        Send-Key ([Win32]::VK_ESCAPE)
        Start-Sleep -Milliseconds 500
    }
    
    # 2. Wait for main menu
    Start-Sleep -Seconds 2
    
    # 3. Press Enter to select "Start Game"
    Write-Host "  Selecting Start Game..."
    Send-Key ([Win32]::VK_RETURN)
    Start-Sleep -Seconds 1
    
    # 4. Press Enter to select "New Game"
    Write-Host "  Selecting New Game..."
    Send-Key ([Win32]::VK_RETURN)
    Start-Sleep -Seconds 1
    
    # 5. Now the game loads - wait and monitor
    Write-Host "  Game loading..."
}

# Main loop
Write-Host "========================================="
Write-Host "  GTA SA Auto-Test Loop"
Write-Host "  Max runs: $maxRuns"
Write-Host "========================================="

for ($run = 1; $run -le $maxRuns; $run++) {
    Write-Host ""
    Write-Host "--- Run $run / $maxRuns ---"
    
    # Clear log
    Clear-Log
    
    # Kill any existing gta_sa process
    Get-Process gta_sa -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2
    
    # Launch game
    Write-Host "  Launching game..."
    $proc = Start-Process -FilePath $gameExe -WorkingDirectory $gameDir -PassThru
    
    # Navigate menus
    Start-Sleep -Seconds 3
    Navigate-To-NewGame -waitSec 2
    
    # Monitor for crash (wait up to 60 seconds)
    $monitorTime = 0
    $maxMonitor = 60
    $crashed = $false
    
    while ($monitorTime -lt $maxMonitor) {
        Start-Sleep -Seconds 2
        $monitorTime += 2
        
        # Check if process still running
        $running = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
        if ($null -eq $running) {
            Write-Host "  Process exited after ${monitorTime}s"
            $crashed = $true
            break
        }
        
        # Check log for crash/watchdog
        if (Test-Path $logFile) {
            $logContent = Get-Content $logFile -Raw -ErrorAction SilentlyContinue
            if ($logContent -match "CRASH.*code=0x[0-9A-F]+.*addr=0x[0-9A-F]+") {
                Write-Host "  CRASH detected in log!"
            }
            if ($logContent -match "WATCHDOG: hung at") {
                Write-Host "  HANG detected by watchdog!"
                # Don't break - keep monitoring, might recover
            }
            if ($logContent -match "original returned successfully") {
                Write-Host "  Game initialized successfully!"
                $crashed = $false
                break
            }
        }
    }
    
    # Kill game for next run
    Get-Process gta_sa -ErrorAction SilentlyContinue | Stop-Process -Force
    
    # Report results
    if ($crashed) {
        Write-Host "  RESULT: CRASH/HANG after ${monitorTime}s"
    } else {
        Write-Host "  RESULT: OK (${monitorTime}s monitored)"
    }
    
    # Show last few lines of log
    if (Test-Path $logFile) {
        Write-Host "  --- Last log entries ---"
        Get-Content $logFile -Tail 5 | ForEach-Object { Write-Host "  $_" }
    }
    
    Start-Sleep -Seconds $launchDelay
}

Write-Host ""
Write-Host "========================================="
Write-Host "  Test loop complete ($maxRuns runs)"
Write-Host "========================================="
