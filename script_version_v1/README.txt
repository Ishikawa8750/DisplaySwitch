Display Control Tools for Windows
=================================

This folder contains tools to control your external monitor (BenQ EW3270U) directly from Windows.

Requirements:
- Python 3.x (already installed on your system)
- No additional libraries required (uses built-in ctypes)

Usage:
1. Switch to Mac (USB-C):
   Run `switch_to_mac.bat` (Double click or create a shortcut)

2. Switch to Windows (HDMI2):
   Run `switch_to_windows.bat`

3. Change Brightness:
   Run `set_brightness.bat` and enter a value (0-100), or run from command line: `set_brightness.bat 50`

Troubleshooting:
- If switching to Mac fails, edit `monitor_control.py` and change `INPUT_DP_USBC` to 16.
- Run `python monitor_control.py scan` to see current monitor status and VCP codes.

Files:
- monitor_control.py: Main script logic.
- switch_to_mac.bat: Windows Batch file for one-click switch.
- switch_to_windows.bat: Windows Batch file for one-click switch.
- set_brightness.bat: Windows Batch file for brightness.
- get_capabilities.py: Diagnostic tool to read monitor capabilities string.
