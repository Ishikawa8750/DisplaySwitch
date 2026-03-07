@echo off
pushd "%~dp0"
if "%1"=="" (
    set /p "level=Enter brightness level (0-100): "
) else (
    set "level=%1"
)
python monitor_control.py brightness %level%
pause
