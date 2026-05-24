# Set-TimeToRP2350.ps1
# PowerShell脚本用于向RP2350发送当前时间

param(
    [string]$PortName = "COM3",
    [int]$BaudRate = 115200
)

Write-Host "正在连接串口 $PortName (波特率: $BaudRate)..."

try {
    $port = new-Object System.IO.Ports.SerialPort $PortName, $BaudRate, "None", 8, "One"
    $port.Open()
    
    $currentTime = Get-Date
    $timeString = "SETTIME " + $currentTime.ToString("HH:mm")
    
    Write-Host "当前时间: " $currentTime.ToString("HH:mm:ss")
    Write-Host "发送命令: " $timeString
    
    $port.WriteLine($timeString)
    Start-Sleep -Milliseconds 100
    
    $port.Close()
    Write-Host "时间设置成功！LED会闪烁绿色2次表示确认。"
    
} catch {
    Write-Host "错误: $_" -ForegroundColor Red
    Write-Host "请检查："
    Write-Host " 1. 串口号是否正确 (尝试使用 'mode' 命令查看可用串口)"
    Write-Host " 2. RP2350 是否已连接"
    Write-Host " 3. 串口是否被其他程序占用"
}
