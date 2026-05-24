# AutoTimeSync.ps1
# Auto sync time to RP2350 LED Clock - TinyUSB Version
param(
    [string]$PortName,
    [int]$BaudRate = 115200
)

Write-Host "====================================" -ForegroundColor Cyan
Write-Host "   RP2350 LED Clock Auto Sync Tool" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Write-Host ""

# Find available serial port
if ([string]::IsNullOrEmpty($PortName)) {
    Write-Host "Searching for available ports..." -ForegroundColor Yellow
    try {
        $ports = [System.IO.Ports.SerialPort]::GetPortNames()
        if ($ports.Count -eq 0) {
            Write-Host "No ports found! Please connect RP2350" -ForegroundColor Red
            exit 1
        }
        
        Write-Host "Found ports:" -ForegroundColor Green
        for ($i = 0; $i -lt $ports.Count; $i++) {
            Write-Host "  [$($i+1)] $($ports[$i])" -ForegroundColor White
        }
        
        if ($ports.Count -eq 1) {
            $PortName = $ports[0]
            Write-Host "Auto selected: $PortName" -ForegroundColor Cyan
        } else {
            $selected = Read-Host "Select port number (1-$($ports.Count))"
            $index = [int]$selected - 1
            if ($index -ge 0 -and $index -lt $ports.Count) {
                $PortName = $ports[$index]
            } else {
                Write-Host "Invalid selection" -ForegroundColor Red
                exit 1
            }
        }
    } catch {
        Write-Host "Error occurred" -ForegroundColor Red
        exit 1
    }
}

Write-Host ""
Write-Host "Using port: $PortName @ $BaudRate" -ForegroundColor Cyan
Write-Host ""

# Send time with retry logic
$maxRetries = 3
$success = $false

for ($retry = 1; $retry -le $maxRetries; $retry++) {
    Write-Host "Attempt $retry of $maxRetries..." -ForegroundColor Yellow
    
    try {
        $serial = New-Object System.IO.Ports.SerialPort
        $serial.PortName = $PortName
        $serial.BaudRate = $BaudRate
        $serial.Parity = "None"
        $serial.DataBits = 8
        $serial.StopBits = "One"
        $serial.ReadTimeout = 3000
        $serial.WriteTimeout = 3000
        $serial.DtrEnable = $true
        $serial.RtsEnable = $true
        
        $serial.Open()
        Start-Sleep -Milliseconds 500
        
        $currentTime = Get-Date
        $timeStr = "SETTIME " + $currentTime.ToString("HH:mm")
        
        $timestamp = $currentTime.ToString("yyyy-MM-dd HH:mm:ss")
        Write-Host "[$timestamp] Sending: $timeStr" -ForegroundColor Green
        
        $serial.WriteLine($timeStr)
        Start-Sleep -Milliseconds 500
        
        $serial.Close()
        
        Write-Host ""
        Write-Host "Sync complete! LED will flash green to confirm" -ForegroundColor Green
        $success = $true
        break
        
    } catch {
        $errorMsg = $_.Exception.Message
        Write-Host "Error on attempt $retry : $errorMsg" -ForegroundColor Red
        if ($serial -and $serial.IsOpen) {
            $serial.Close()
        }
        
        if ($retry -lt $maxRetries) {
            Write-Host "Waiting 2 seconds before retry..." -ForegroundColor Yellow
            Start-Sleep -Seconds 2
        }
    }
}

if (-not $success) {
    Write-Host ""
    Write-Host "Failed after $maxRetries attempts" -ForegroundColor Red
    Write-Host "Please check:" -ForegroundColor Yellow
    Write-Host "  - RP2350 is connected and powered" -ForegroundColor White
    Write-Host "  - Correct COM port selected" -ForegroundColor White
    Write-Host "  - No other program using COM4" -ForegroundColor White
    Write-Host "  - Try unplug and replug RP2350" -ForegroundColor White
    exit 1
}
