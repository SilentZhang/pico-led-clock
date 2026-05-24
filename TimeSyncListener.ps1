# TimeSyncListener.ps1
# Simple listener for RP2350 clock time requests

Write-Host "====================================" -ForegroundColor Cyan
Write-Host "   RP2350 Clock Time Sync Listener" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Listening for GETTIME requests..." -ForegroundColor Yellow
Write-Host "Press Ctrl+C to stop" -ForegroundColor Gray
Write-Host ""

$lastPort = ""

while ($true) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    
    foreach ($portName in $ports) {
        try {
            $serial = New-Object System.IO.Ports.SerialPort
            $serial.PortName = $portName
            $serial.BaudRate = 115200
            $serial.Parity = "None"
            $serial.DataBits = 8
            $serial.StopBits = "One"
            $serial.ReadTimeout = 200
            $serial.WriteTimeout = 200
            $serial.DtrEnable = $true
            $serial.RtsEnable = $true
            
            $serial.Open()
            
            try {
                $line = $serial.ReadLine()
                if ($line -and $line.Trim() -eq "GETTIME") {
                    $currentTime = Get-Date
                    $timeStr = "SETTIME " + $currentTime.ToString("HH:mm")
                    $timestamp = $currentTime.ToString("yyyy-MM-dd HH:mm:ss")
                    Write-Host "[$timestamp] $portName -> $timeStr" -ForegroundColor Green
                    $serial.WriteLine($timeStr)
                    Start-Sleep -Milliseconds 100
                }
            } catch {
                # Timeout or error, ignore
            }
            
            $serial.Close()
        } catch {
            # Port busy or error, skip
        }
    }
    
    Start-Sleep -Milliseconds 50
}
