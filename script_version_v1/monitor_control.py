import ctypes
from ctypes import wintypes
import argparse
import sys

# Constants
VCP_INPUT_SOURCE = 0x60
VCP_BRIGHTNESS = 0x10

# Known Input Source Codes for BenQ EW3270U (Approximate based on scan)
INPUT_DP_USBC = 15      # 0x0F (DisplayPort / USB-C Alt Mode)
INPUT_USBC_ALT = 16     # 0x10 (Possible specific USB-C)
INPUT_HDMI1 = 17        # 0x11
INPUT_HDMI2 = 18        # 0x12

# Note: Scan showed current=17 (0x11). 
# User says they are on HDMI2.
# So we assume:
# Windows (Current) = 17
# Mac (USB-C) = 15 (Standard DP) or 16.

class PHYSICAL_MONITOR(ctypes.Structure):
    _fields_ = [('hPhysicalMonitor', wintypes.HANDLE),
                ('szPhysicalMonitorDescription', wintypes.WCHAR * 128)]

def get_physical_monitors():
    user32 = ctypes.windll.user32
    dxva2 = ctypes.windll.dxva2
    
    monitors_handles = [] # Changed name to avoid conflict
    
    def monitor_enum_proc(hMonitor, hdcMonitor, lprcMonitor, dwData):
        pdwNumberOfPhysicalMonitors = wintypes.DWORD()
        if dxva2.GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, ctypes.byref(pdwNumberOfPhysicalMonitors)):
            physical_monitors = (PHYSICAL_MONITOR * pdwNumberOfPhysicalMonitors.value)()
            if dxva2.GetPhysicalMonitorsFromHMONITOR(hMonitor, pdwNumberOfPhysicalMonitors, physical_monitors):
                for pm in physical_monitors:
                    monitors_handles.append(pm.hPhysicalMonitor) # Helper list
                    # We print to confirm discovery
                    print(f"Found Physical Monitor Handle: {pm.hPhysicalMonitor}")
        return True

    MONITORENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HANDLE, wintypes.HANDLE,
                                         ctypes.POINTER(wintypes.RECT), wintypes.LPARAM)
    
    if not user32.EnumDisplayMonitors(None, None, MONITORENUMPROC(monitor_enum_proc), 0):
        print("EnumDisplayMonitors failed")
    
    return monitors_handles

def set_vcp(handle, code, value):
    dxva2 = ctypes.windll.dxva2
    # Define argtypes for safety
    dxva2.SetVCPFeature.argtypes = [wintypes.HANDLE, wintypes.BYTE, wintypes.DWORD]
    dxva2.SetVCPFeature.restype = wintypes.BOOL
    
    if not dxva2.SetVCPFeature(handle, code, value):
        err = ctypes.GetLastError()
        print(f"Failed to set VCP code 0x{code:02X} to {value} (Error: {err})")
    else:
        print(f"Successfully set VCP code 0x{code:02X} to {value}")

def get_vcp(handle, code):
    dxva2 = ctypes.windll.dxva2
    # Define argtypes
    dxva2.GetVCPFeatureAndVCPFeatureReply.argtypes = [wintypes.HANDLE, wintypes.BYTE, ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD)]
    dxva2.GetVCPFeatureAndVCPFeatureReply.restype = wintypes.BOOL
    
    current = wintypes.DWORD()
    maximum = wintypes.DWORD()
    try:
        # Note: The 3rd arg is LKP_VCP_CODE_TYPE (NULL is allowed for getting value)
        if dxva2.GetVCPFeatureAndVCPFeatureReply(handle, code, None, ctypes.byref(current), ctypes.byref(maximum)):
            return current.value, maximum.value
    except Exception as e:
        print(f"Error reading VCP: {e}")
    return None, None

def main():
    parser = argparse.ArgumentParser(description="Monitor Control for BenQ EW3270U")
    subparsers = parser.add_subparsers(dest="command")

    # Switch command
    switch_parser = subparsers.add_parser("switch", help="Switch Input Source")
    switch_parser.add_argument("source", help="Target Source (mac, windows, hdmi1, hdmi2, dp, usbc, or value)")

    # Brightness command
    bright_parser = subparsers.add_parser("brightness", help="Set Brightness")
    bright_parser.add_argument("level", type=int, help="Brightness Level (0-100)")

    # Scan command (debug)
    subparsers.add_parser("scan", help="Scan current values")

    args = parser.parse_args()
    
    monitors = get_physical_monitors()
    if not monitors:
        print("No supported monitors found.")
        return

    # Find target monitor (Success on VCP 0x60 read)
    target_monitor = None
    for h in monitors:
        v, _ = get_vcp(h, VCP_INPUT_SOURCE)
        if v is not None:
            target_monitor = h
            print(f"Using Monitor Handle: {h} (Current Source: {v})")
            break
            
    if not target_monitor:
        print("Could not find a monitor supporting VCP Input Source.")
        return

    val = 0
    if args.command == "switch":
        s = args.source.lower()
        val = 0
        
        if s == "mac" or s == "usbc":
            val = INPUT_DP_USBC # 15
        elif s == "windows":
            val = 17 # Use scanned value for Windows
        elif s == "hdmi2":
            val = 18 # Standard
        elif s == "hdmi1":
            val = 17 # Standard
        elif s == "dp":
            val = 15
        elif s.isdigit():
            val = int(s)
            
        print(f"Switching to {args.source} (Value: {val})...")
        set_vcp(target_monitor, VCP_INPUT_SOURCE, val)

    elif args.command == "brightness":
        level = max(0, min(100, args.level))
        print(f"Setting brightness to {level}...")
        set_vcp(target_monitor, VCP_BRIGHTNESS, level)

    elif args.command == "scan":
        print("Scanning monitors...")
        for h in monitors:
            v60, _ = get_vcp(h, VCP_INPUT_SOURCE)
            v10, m10 = get_vcp(h, VCP_BRIGHTNESS)
            print(f"Monitor Handle {h}: Source={v60}, Brightness={v10}/{m10}")

if __name__ == "__main__":
    main()