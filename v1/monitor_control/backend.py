import platform
import subprocess

# Abstract Base Controller
class MonitorController:
    # Common VCP Codes & Values
    VCP_INPUT_SOURCE = 0x60
    VCP_BRIGHTNESS = 0x10
    
    # Input Codes (BenQ Specific from scan)
    INPUT_DP_USBC = 15  # Mac (DP/USB-C)
    INPUT_HDMI2 = 17    # Windows (HDMI2)

    def scan_monitors(self):
        """Scans and returns the count of controllable monitors."""
        return 0

    def set_brightness(self, level):
        """Sets brightness (0-100)."""
        pass

    def get_brightness(self):
        """Gets current brightness (0-100) or None."""
        return 50 # Default

    def switch_to_input(self, input_code):
        """Switches input source to given code."""
        pass
        
    def switch_to_mac(self):
        return self.switch_to_input(self.INPUT_DP_USBC)

    def switch_to_windows(self):
        return self.switch_to_input(self.INPUT_HDMI2)


# Windows Implementation using ctypes (Native API)
class WindowsMonitorController(MonitorController):
    def __init__(self):
        import ctypes
        from ctypes import wintypes
        self.ctypes = ctypes
        self.wintypes = wintypes
        self._user32 = ctypes.windll.user32
        self._dxva2 = ctypes.windll.dxva2
        self.monitors = []

    def scan_monitors(self):
        self.monitors = []
        
        class PHYSICAL_MONITOR(self.ctypes.Structure):
            _fields_ = [('hPhysicalMonitor', self.wintypes.HANDLE),
                        ('szPhysicalMonitorDescription', self.wintypes.WCHAR * 128)]

        def monitor_enum_proc(hMonitor, hdcMonitor, lprcMonitor, dwData):
            pdwNumberOfPhysicalMonitors = self.wintypes.DWORD()
            bSuccess = self._dxva2.GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, self.ctypes.byref(pdwNumberOfPhysicalMonitors))
            if bSuccess:
                cPhysicalMonitors = pdwNumberOfPhysicalMonitors.value
                lpPhysicalMonitors = (PHYSICAL_MONITOR * cPhysicalMonitors)()
                bSuccess = self._dxva2.GetPhysicalMonitorsFromHMONITOR(hMonitor, cPhysicalMonitors, lpPhysicalMonitors)
                if bSuccess:
                    for item in lpPhysicalMonitors:
                        self.monitors.append(item.hPhysicalMonitor)
            return True

        MONITORENUMPROC = self.ctypes.WINFUNCTYPE(self.wintypes.BOOL, self.wintypes.HANDLE, self.wintypes.HANDLE,
                                             self.ctypes.POINTER(self.wintypes.RECT), self.wintypes.LPARAM)
        
        self._user32.EnumDisplayMonitors(None, None, MONITORENUMPROC(monitor_enum_proc), 0)
        return len(self.monitors)

    def set_vcp(self, code, value):
        success_count = 0
        for h in self.monitors:
            # Re-confirm argtypes just in case
            self._dxva2.SetVCPFeature.argtypes = [self.wintypes.HANDLE, self.wintypes.BYTE, self.wintypes.DWORD]
            self._dxva2.SetVCPFeature.restype = self.wintypes.BOOL
            if self._dxva2.SetVCPFeature(h, code, value):
                success_count += 1
        return success_count

    def get_vcp(self, code):
        for h in self.monitors:
            current = self.wintypes.DWORD()
            maximum = self.wintypes.DWORD()
            try:
                if self._dxva2.GetVCPFeatureAndVCPFeatureReply(h, code, None, self.ctypes.byref(current), self.ctypes.byref(maximum)):
                    return current.value, maximum.value
            except:
                pass
        return None, None

    def set_brightness(self, level):
        level = max(0, min(100, int(level)))
        return self.set_vcp(self.VCP_BRIGHTNESS, level)

    def get_brightness(self):
        cur, _ = self.get_vcp(self.VCP_BRIGHTNESS)
        return cur if cur is not None else 50

    def switch_to_input(self, input_code):
        return self.set_vcp(self.VCP_INPUT_SOURCE, input_code)


# Mac Implementation (Apple Silicon) using external CLI 'm1ddc'
class MacMonitorController(MonitorController):
    def __init__(self):
        # Check for m1ddc availability
        self.cmd_tool = self._find_m1ddc_tool()
        self.available = True # Assume available if we found a path or name

    def _find_m1ddc_tool(self):
        import shutil
        import sys
        import os

        tool_name = "m1ddc"

        # Check if bundled with PyInstaller
        if getattr(sys, 'frozen', False):
            bundled_path = os.path.join(sys._MEIPASS, tool_name)
            if os.path.exists(bundled_path):
                return bundled_path
            
            # Check adjacent to executable (for non-onefile builds or manually placed)
            exe_path = os.path.dirname(sys.executable)
            adjacent_path = os.path.join(exe_path, tool_name)
            if os.path.exists(adjacent_path):
                return adjacent_path

        # Check current working directory
        cwd_path = os.path.join(os.getcwd(), tool_name)
        if os.path.exists(cwd_path):
            return cwd_path
            
        # Check system PATH
        path_tool = shutil.which(tool_name)
        if path_tool:
            return path_tool
            
        return tool_name # Fallback to name in hope it works

    def scan_monitors(self):
        if not self.available:
            return 0
        try:
            # m1ddc display list
            # Output format is custom, just counting lines roughly or assuming 1 for external
            result = subprocess.run([self.cmd_tool, "display", "list"], capture_output=True, text=True)
            if result.returncode == 0:
                # Naive count: lines in output that look like displays
                lines = [l for l in result.stdout.splitlines() if "Display" in l or "[" in l]
                return len(lines) if lines else 0 # m1ddc counts internal too usually 0 is internal, 1 is external
            return 0
        except:
            return 0

    def _run_m1ddc(self, args):
        """Helper to run m1ddc command."""
        try:
            # Assumes m1ddc is in PATH or bundled
            cmd = [self.cmd_tool] + args
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            return result
        except Exception as e:
            print(f"Command failed: {e}")
            return None

    def set_brightness(self, level):
        # m1ddc set luminance <value>
        # brightness/luminance
        level = max(0, min(100, int(level)))
        return self._run_m1ddc(["set", "luminance", str(level)])

    def get_brightness(self):
        # m1ddc get luminance
        if not self.available: return None
        try:
            res = subprocess.run([self.cmd_tool, "get", "luminance"], capture_output=True, text=True)
            val = res.stdout.strip()
            if val.isdigit():
                return int(val)
        except:
            pass
        return None

    def switch_to_input(self, input_code):
        # m1ddc set input <value>
        return self._run_m1ddc(["set", "input", str(input_code)])


# Factory
def get_controller():
    system = platform.system()
    if system == "Windows":
        return WindowsMonitorController()
    elif system == "Darwin":
        return MacMonitorController()
    else:
        # Fallback dummy
        return MonitorController()
