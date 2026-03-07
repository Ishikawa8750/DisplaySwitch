import customtkinter as ctk
import sys
import threading
import os
import platform
import subprocess

# Set appearance mode
ctk.set_appearance_mode("System")
ctk.set_default_color_theme("blue")

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
        return None

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
        return cur

    def switch_to_input(self, input_code):
        return self.set_vcp(self.VCP_INPUT_SOURCE, input_code)


# Mac Implementation (Apple Silicon) using external CLI 'm1ddc'
class MacMonitorController(MonitorController):
    def __init__(self):
        # Check for m1ddc availability
        self.cmd_tool = "m1ddc"
        self.available = self._check_tool()

    def _check_tool(self):
        try:
            # Check if m1ddc is in path or installed via brew
            # Common paths for brew binaries on Apple Silicon
            paths = ["/opt/homebrew/bin/m1ddc", "/usr/local/bin/m1ddc", "m1ddc"]
            for p in paths:
                if self._verify_command(p):
                    self.cmd_tool = p
                    return True
            return False
        except:
            return False

    def _verify_command(self, cmd):
        # Just checking if we can call it (e.g. usage/help)
        try:
            subprocess.run([cmd, "display", "list"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return True
        except FileNotFoundError:
            return False
        except Exception:
            return False

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

    def _run_cmd(self, args):
        if not self.available:
            print("Standard tool 'm1ddc' not found. Please install: brew install m1ddc")
            return False
        try:
            # Example: m1ddc set input 15
            subprocess.run([self.cmd_tool] + args, check=True) # blocks
            return True
        except Exception as e:
            print(f"Command failed: {e}")
            return False

    def set_brightness(self, level):
        # m1ddc set luminance <value>
        # brightness/luminance
        level = max(0, min(100, int(level)))
        return self._run_cmd(["set", "luminance", str(level)])

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
        return self._run_cmd(["set", "input", str(input_code)])


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

class App(ctk.CTk):
    def __init__(self):
        super().__init__()

        self.controller = get_controller()
        self.os_name = platform.system()

        self.title(f"Display Switch ({self.os_name})")
        self.geometry("400x400")
        self.resizable(False, False)

        # Layout
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(6, weight=1)

        # Header
        self.header_label = ctk.CTkLabel(self, text="Monitor Control Center", font=("Roboto", 20, "bold"))
        self.header_label.grid(row=0, column=0, padx=20, pady=(20, 5))
        
        # Sub-header system info
        sys_info = f"System: {self.os_name} {platform.release()}"
        self.sys_label = ctk.CTkLabel(self, text=sys_info, text_color="gray", font=("Roboto", 12))
        self.sys_label.grid(row=1, column=0, padx=20, pady=(0, 10))

        # Initial scan
        monitor_count = self.controller.scan_monitors()
        status_text = f"Connected Controllable Monitors: {monitor_count}"
        if self.os_name == "Darwin" and isinstance(self.controller, MacMonitorController) and not self.controller.available:
            status_text = "Error: 'm1ddc' not found.\nPlease install: brew install m1ddc"
        
        self.status_label = ctk.CTkLabel(self, text=status_text, text_color="gray" if "Error" not in status_text else "red")
        self.status_label.grid(row=2, column=0, padx=20, pady=(0, 20))

        # Switch Buttons Frame
        self.switch_frame = ctk.CTkFrame(self)
        self.switch_frame.grid(row=3, column=0, padx=20, pady=10, sticky="ew")
        self.switch_frame.grid_columnconfigure((0, 1), weight=1)

        self.btn_mac = ctk.CTkButton(self.switch_frame, text="Change to Mac\n(USB-C)", 
                                     command=self.on_switch_mac, fg_color="#3B8ED0", hover_color="#36719F", height=50)
        self.btn_mac.grid(row=0, column=0, padx=10, pady=20, sticky="ew")

        self.btn_windows = ctk.CTkButton(self.switch_frame, text="Change to PC\n(HDMI)", 
                                         command=self.on_switch_windows, fg_color="#3B8ED0", hover_color="#36719F", height=50)
        self.btn_windows.grid(row=0, column=1, padx=10, pady=20, sticky="ew")

        # Brightness Control Frame
        self.brightness_frame = ctk.CTkFrame(self)
        self.brightness_frame.grid(row=4, column=0, padx=20, pady=10, sticky="ew")
        self.brightness_frame.grid_columnconfigure(1, weight=1)

        self.brightness_label = ctk.CTkLabel(self.brightness_frame, text="Brightness:")
        self.brightness_label.grid(row=0, column=0, padx=(15, 5), pady=15)
        
        # Initial Brightness
        init_brightness = self.controller.get_brightness()
        if init_brightness is None: init_brightness = 50

        self.slider = ctk.CTkSlider(self.brightness_frame, from_=0, to=100, command=self.on_brightness_change)
        self.slider.set(init_brightness)
        self.slider.grid(row=0, column=1, padx=(5, 5), pady=15, sticky="ew")
        
        self.brightness_value_label = ctk.CTkLabel(self.brightness_frame, text=f"{init_brightness}%", width=30)
        self.brightness_value_label.grid(row=0, column=2, padx=(5, 15), pady=15)

        # Rescan Button
        self.btn_rescan = ctk.CTkButton(self, text="Refresh Status", command=self.on_rescan, 
                                        fg_color="transparent", border_width=1, text_color=("gray10", "#DCE4EE"))
        self.btn_rescan.grid(row=5, column=0, padx=20, pady=10)

    def on_switch_mac(self):
        self.status_label.configure(text="Sending signal: Switch to Mac...", text_color="orange")
        threading.Thread(target=self._switch_mac_thread).start()

    def _switch_mac_thread(self):
        self.controller.switch_to_mac()
        self.after(0, lambda: self.status_label.configure(text="Signal Sent: Mac (Input 15)", text_color="gray"))

    def on_switch_windows(self):
        self.status_label.configure(text="Sending signal: Switch to PC...", text_color="orange")
        threading.Thread(target=self._switch_windows_thread).start()
    
    def _switch_windows_thread(self):
        self.controller.switch_to_windows()
        self.after(0, lambda: self.status_label.configure(text="Signal Sent: Windows (Input 17)", text_color="gray"))

    def on_brightness_change(self, value):
        val = int(value)
        self.brightness_value_label.configure(text=f"{val}%")
        threading.Thread(target=lambda: self.controller.set_brightness(val)).start()

    def on_rescan(self):
        count = self.controller.scan_monitors()
        status = f"Connected Controlled Monitors: {count}"
        if self.os_name == "Darwin" and isinstance(self.controller, MacMonitorController) and not self.controller.available:
            status = "Error: 'm1ddc' not found.\nPlease install: brew install m1ddc"
        
        self.status_label.configure(text=status, text_color="gray" if "Error" not in status else "red")
        
        b = self.controller.get_brightness()
        if b is not None:
            self.slider.set(b)
            self.brightness_value_label.configure(text=f"{b}%")

if __name__ == "__main__":
    app = App()
    app.mainloop()