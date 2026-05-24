# AutoTimeSync.ps1
# 自动同步时间到RP2350 LED时钟 - TinyUSB方案
param(
    [string]$PortName,
    [int]$BaudRate = 115200,
    [int]$SyncIntervalMinutes = 30,
    [switch]$KeepRunning
)

Write-Host "====================================" -ForegroundColor Cyan
Write-Host "   RP2350 LED时钟自动同步工具" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan
Write-Host ""

function Find-PicoPort {
    Write-Host "正在查找可用串口..." -ForegroundColor Yellow
    try {
        $ports = [System.IO.Ports.SerialPort]::GetPortNames()
        if ($ports.Count -eq 0) {
            Write-Host "未找到可用串口！请确保RP2350已连接" -ForegroundColor Red
            return $null
        }
        
        Write-Host "找到以下串口:" -ForegroundColor Green
        for ($i = 0; $i -lt $ports.Count; $i++) {
            Write-Host "  [$($i+1)] $($ports[$i])" -ForegroundColor White
        }
        
        if ($ports.Count -eq 1) {
            Write-Host "自动选择: $($ports[0])" -ForegroundColor Cyan
            return $ports[0]
        }
        
        $selected = Read-Host "请选择串口号 (1-$($ports.Count))"
        $index = [int]$selected - 1
        if ($index -ge 0 -and $index -lt $ports.Count) {
            return $ports[$index]
        }
        return $null
    }
    catch {
        Write-Host "错误: $_" -ForegroundColor Red
        return $null
    }
}

function Send-TimeToPico {
    param([string]$port, [int]$baud)
    try {
        $serial = New-Object System.IO.Ports.SerialPort $port, $baud, "None", 8, "One"
        $serial.ReadTimeout = 1000
        $serial.WriteTimeout = 1000
        $serial.Open()
        
        $currentTime = Get-Date
        $timeStr = "SETTIME " + $currentTime.ToString("HH:mm")
        
        $timestamp = $currentTime.ToString("yyyy-MM-dd HH:mm:ss")
        Write-Host "[$timestamp] 发送: $timeStr" -ForegroundColor Green
        
        $serial.WriteLine($timeStr)
        Start-Sleep -Milliseconds 200
        $serial.Close()
        return $true
    }
    catch {
        Write-Host "错误: 无法发送时间到串口 - $_" -ForegroundColor Red
        return $false
    }
}

# 主程序
if ([string]::IsNullOrEmpty($PortName)) {
    $PortName = Find-PicoPort
    if ([string]::IsNullOrEmpty($PortName)) {
        Write-Host "未选择串口，程序退出" -ForegroundColor Red
        exit 1
    }
}

Write-Host ""
Write-Host "使用串口: $PortName @ $BaudRate" -ForegroundColor Cyan

if ($KeepRunning) {
    Write-Host "模式: 持续运行，每 $SyncIntervalMinutes 分钟同步一次" -ForegroundColor Cyan
    Write-Host "按 Ctrl+C 停止运行" -ForegroundColor Yellow
    Write-Host ""
    
    while ($true) {
        Send-TimeToPico -port $PortName -baud $BaudRate
        
        $nextSync = (Get-Date).AddMinutes($SyncIntervalMinutes)
        Write-Host "下次同步: $($nextSync.ToString('HH:mm:ss'))" -ForegroundColor Gray
        Write-Host ""
        
        Start-Sleep -Seconds ($SyncIntervalMinutes * 60)
    }
}
else {
    Write-Host "模式: 单次同步" -ForegroundColor Cyan
    Write-Host ""
    
    Send-TimeToPico -port $PortName -baud $BaudRate
    
    Write-Host ""
    Write-Host "同步完成！" -ForegroundColor Green
}
