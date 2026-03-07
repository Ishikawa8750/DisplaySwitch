@echo off
pushd "%~dp0"
if not exist "venv" (
    echo Virtual environment not found. Please run setup_env.bat first.
    pause
    exit /b
)
start "" venv\Scripts\pythonw.exe main.py
