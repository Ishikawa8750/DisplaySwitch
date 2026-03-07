@echo off
pushd "%~dp0"
echo Switching to Mac (USB-C)...
python monitor_control.py switch mac
pause
