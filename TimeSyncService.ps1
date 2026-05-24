# TimeSyncService.ps1
# RP2350 LED Clock Auto Time Sync Service
# Runs in background, listens for GETTIME request and responds automatically

param(
    [switch]$Start,
    [switch]$Stop
)

$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$pidFile = Join-Path $scriptPath "timesync.pid"
$logFile = Join-Path $scriptPath "timesync.log"

# Function to log messages
function Log-Message {
    param([string]$message)
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logEntry = "[$timestamp] $message"
    Write-Host $logEntry
    Add-Content -Path $logFile -Value $logEntry -ErrorAction SilentlyContinue
}

# Stop existing service if requested
if ($Stop) {
    if (Test-Path $pidFile) {
        $pidValue = Get-Content $pidFile -ErrorAction SilentlyContinue
        if ($pidValue) {
            try {
                $process = Get-Process -Id $pidValue -ErrorAction SilentlyContinue
                if ($process) {
                    Stop-Process -Id $pidValue -Force -ErrorAction SilentlyContinue
                    Log-Message "Stopped existing service (PID: $pidValue)"
                }
            } catch {
                # Do nothing
            }
        }
        Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
    }
    exit 0
}

# If -Start is not used, run in foreground with status display
if (-not $Start) {
    Log-Message "===================================="
    Log-Message "   RP2350 Clock Auto Sync Service"
    Log-Message "===================================="
    Log-Message ""
    Log-Message "Monitoring serial ports for GETTIME requests..."
    Log-Message "Press Ctrl+C to stop"
    Log-Message ""
}

# Save PID if running as service
$pid | Out-File -FilePath $pidFile -Force

try {
    while ($true) {
        # Find all available serial ports
        $ports = [System.IO.Ports.SerialPort]::GetPortNames()
        
        foreach ($portName in $ports) {
            try {
                $serial = New-Object System.IO.Ports.SerialPort
                $serial.PortName = $portName
                $serial.BaudRate = 115200
                $serial.Parity = "None"
                $serial.DataBits = 8
                $serial.StopBits = "One"
                $serial.ReadTimeout = 500
                $serial.WriteTimeout = 500
                $serial.DtrEnable = $true
                $serial.RtsEnable = $true
                
                $serial.Open()
                
                # Try to read any pending data
                try {
                    $line = $serial.ReadLine()
                    if ($line -and $line.Trim() -eq "GETTIME") {
                        Log-Message "Received GETTIME from $portName"
                        
                        # Send current time back
                        $currentTime = Get-Date
                        $timeStr = "SETTIME " + $currentTime.ToString("HH:mm")
                        $serial.WriteLine($timeStr)
                        
                        Log-Message "Sent time: $timeStr"
                    }
                } catch {
                    # Timeout or read error, ignore
                }
                
                $serial.Close()
            } catch {
                # Port busy or error, skip to next
            }
        }
        
        # Short delay before checking again
        Start-Sleep -Milliseconds 100
    }
} finally {
    # Cleanup
    if (Test-Path $pidFile) {
        Remove-Item $pidFile -Force -ErrorAction SilentlyContinue
    }
}
