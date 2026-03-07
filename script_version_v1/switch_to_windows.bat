@echo off
pushd "%~dp0"
echo Switching to Windows (HDMI2)...
python monitor_control.py switch windows
pause
