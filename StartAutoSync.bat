@echo off
REM Auto Time Sync for RP2350 LED Clock
REM Run this batch file to start the auto sync service

cd /d "%~dp0"

echo ====================================
echo    RP2350 Clock Auto Sync Service
echo ====================================
echo.
echo Starting background service...
echo.

PowerShell -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0TimeSyncService.ps1"

echo Service stopped.
pause
