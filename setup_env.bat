@echo off
pushd "%~dp0"

echo Creating virtual environment...
python -m venv venv

echo Upgrading pip...
venv\Scripts\python.exe -m pip install --upgrade pip

echo Installing requirements...
venv\Scripts\pip install -r requirements.txt

echo.
echo Environment setup complete!
echo You can now run "run_display_switch.bat"
pause
