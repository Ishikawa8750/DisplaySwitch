import ctypes
from ctypes import wintypes
import sys

# Define necessary structures
class PHYSICAL_MONITOR(ctypes.Structure):
    _fields_ = [('hPhysicalMonitor', wintypes.HANDLE),
                ('szPhysicalMonitorDescription', wintypes.WCHAR * 128)]

def get_capabilities():
    user32 = ctypes.windll.user32
    dxva2 = ctypes.windll.dxva2
    
    monitors_info = []

    def monitor_enum_proc(hMonitor, hdcMonitor, lprcMonitor, dwData):
        monitors_info.append(hMonitor)
        return True

    MONITORENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HANDLE, wintypes.HANDLE,
                                         ctypes.POINTER(wintypes.RECT), wintypes.LPARAM)
    
    if not user32.EnumDisplayMonitors(None, None, MONITORENUMPROC(monitor_enum_proc), 0):
        print("Failed to enumerate monitors")
        return

    print(f"Found {len(monitors_info)} monitor(s).")

    for hMonitor in monitors_info:
        pdwNumberOfPhysicalMonitors = wintypes.DWORD()
        if not dxva2.GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, ctypes.byref(pdwNumberOfPhysicalMonitors)):
            continue

        physical_monitors = (PHYSICAL_MONITOR * pdwNumberOfPhysicalMonitors.value)()
        if not dxva2.GetPhysicalMonitorsFromHMONITOR(hMonitor, pdwNumberOfPhysicalMonitors, physical_monitors):
            continue

        for pm in physical_monitors:
            print(f"\nScanning Monitor Handle: {pm.hPhysicalMonitor}")
            
            # Get VCP 60 current value (Input Source)
            pdwCurrentValue = wintypes.DWORD()
            pdwMaximumValue = wintypes.DWORD()
            
            if dxva2.GetVCPFeatureAndVCPFeatureReply(pm.hPhysicalMonitor, 0x60, None, ctypes.byref(pdwCurrentValue), ctypes.byref(pdwMaximumValue)):
                 print(f"  Current Input Source (0x60): {pdwCurrentValue.value} (0x{pdwCurrentValue.value:02X})")
            
            # Get Capabilities String
            pdwCapabilitiesStringLengthInCharacters = wintypes.DWORD()
            if dxva2.GetCapabilitiesStringLength(pm.hPhysicalMonitor, ctypes.byref(pdwCapabilitiesStringLengthInCharacters)):
                length = pdwCapabilitiesStringLengthInCharacters.value
                pszASCIICapabilitiesString = ctypes.create_string_buffer(length)
                if dxva2.CapabilitiesRequestAndCapabilitiesReply(pm.hPhysicalMonitor, pszASCIICapabilitiesString, length):
                    cap_str = pszASCIICapabilitiesString.value.decode('ascii', errors='ignore')
                    print(f"  Capabilities String: {cap_str}")
                    # Try to parse valid inputs from string
                    # e.g., vcp(60(0F 11 12))
                    start = cap_str.find("60(")
                    if start != -1:
                        sub = cap_str[start+3:]
                        end = sub.find(")")
                        if end != -1:
                            input_codes = sub[:end]
                            print(f"  Supported Input Codes: {input_codes}")
                else:
                    print("  Failed to read Capabilities String")
            else:
                print("  Failed to get Capabilities String Length")

            dxva2.DestroyPhysicalMonitors(pdwNumberOfPhysicalMonitors, physical_monitors)

if __name__ == "__main__":
    get_capabilities()
