import sys
import platform
from PySide6.QtWidgets import QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel
from PySide6.QtCore import Qt
from PySide6.QtGui import QIcon

from qfluentwidgets import (
    FluentWindow, SubtitleLabel, BodyLabel, PrimaryPushButton, Slider, InfoBar,
    InfoBarPosition, setTheme, Theme, FluentIcon as FIF,
    CardWidget, PushButton, IconWidget, NavigationItemPosition
)

# Fix relative import when running as script
try:
    from monitor_control.backend import get_controller
except ImportError:
    # Fallback if running directly from file without package structure
    import sys
    import os
    sys.path.append(os.path.dirname(os.path.abspath(__file__)))
    from monitor_control.backend import get_controller


class MonitorControlInterface(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent=parent)
        self.controller = get_controller()
        self.os_name = platform.system()
        
        self.setup_ui()
        self.refresh_status()

    def setup_ui(self):
        self.main_layout = QVBoxLayout(self)
        self.main_layout.setContentsMargins(30, 30, 30, 30)
        self.main_layout.setSpacing(20)

        # Header
        self.header = SubtitleLabel("Monitor Control", self)
        
        # System Info Label
        sys_str = f"System: {self.os_name} {platform.release()}"
        self.sys_info = BodyLabel(sys_str, self)
        self.sys_info.setTextColor("#666666", "#aaaaaa") # Light/Dark mode colors

        self.main_layout.addWidget(self.header)
        self.main_layout.addWidget(self.sys_info)

        # Status Card (Simulated with simple layout for now, CardWidget usage refined)
        self.status_container = CardWidget(self)
        self.status_layout = QHBoxLayout(self.status_container)
        
        self.status_icon = IconWidget(FIF.INFO, self)
        self.status_icon.setFixedSize(16, 16)
        
        self.status_label = BodyLabel("Checking monitors...", self)
        
        self.status_layout.addWidget(self.status_icon)
        self.status_layout.addWidget(self.status_label)
        self.status_layout.addStretch(1)
        
        self.main_layout.addWidget(self.status_container)

        # Switch Section
        self.switch_label = BodyLabel("Input Source", self)
        self.switch_label.setTextColor("#333333", "#dddddd")
        self.main_layout.addWidget(self.switch_label)

        self.switch_layout = QHBoxLayout()
        self.switch_layout.setSpacing(15)

        self.btn_mac = PrimaryPushButton("Switch to Mac (USB-C)", self, FIF.IOT)
        self.btn_mac.clicked.connect(lambda: self.on_switch_mac())
        self.btn_mac.setFixedHeight(50)

        self.btn_win = PrimaryPushButton("Switch to PC (HDMI)", self, FIF.PROJECTOR)
        self.btn_win.clicked.connect(lambda: self.on_switch_win())
        self.btn_win.setFixedHeight(50)

        self.switch_layout.addWidget(self.btn_mac)
        self.switch_layout.addWidget(self.btn_win)
        self.main_layout.addLayout(self.switch_layout)

        # Brightness Section
        self.bright_label = BodyLabel("Brightness", self)
        self.main_layout.addWidget(self.bright_label)

        self.bright_layout = QHBoxLayout()
        
        self.slider = Slider(Qt.Horizontal, self)
        self.slider.setRange(0, 100)
        self.slider.setValue(50)
        self.slider.valueChanged.connect(self.on_brightness_change)
        
        self.val_label = BodyLabel("50%", self)
        self.val_label.setFixedWidth(40)
        
        self.bright_layout.addWidget(self.slider)
        self.bright_layout.addWidget(self.val_label)
        self.main_layout.addLayout(self.bright_layout)

        # Refresh Button
        self.btn_refresh = PushButton("Refresh Status", self, FIF.SYNC)
        self.btn_refresh.clicked.connect(self.refresh_status)
        self.main_layout.addWidget(self.btn_refresh, 0, Qt.AlignRight)

        self.main_layout.addStretch(1)

    def refresh_status(self):
        count = self.controller.scan_monitors()
        b_val = self.controller.get_brightness()
        
        status_text = f"Monitors: {count}"
        if self.os_name == "Darwin":
            # Check availability
             if hasattr(self.controller, 'available') and not self.controller.available:
                status_text = "Error: 'm1ddc' not found."
                self.status_icon.setIcon(FIF.CANCEL)
        
        self.status_label.setText(status_text)
        
        if b_val is not None:
            self.slider.setValue(b_val)
            self.val_label.setText(f"{b_val}%")

    def on_switch_mac(self):
        self.controller.switch_to_mac()
        self.show_success("Switched to Mac", "Input changed to USB-C/DP")

    def on_switch_win(self):
        self.controller.switch_to_windows()
        self.show_success("Switched to PC", "Input changed to HDMI")

    def on_brightness_change(self, value):
        self.val_label.setText(f"{value}%")
        self.controller.set_brightness(value)

    def show_success(self, title, content):
        InfoBar.success(
            title=title,
            content=content,
            orient=Qt.Horizontal,
            isClosable=True,
            position=InfoBarPosition.TOP_RIGHT,
            parent=self.window() # Use window() to find parent FluentWindow
        )

class MainWindow(FluentWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Display Switch")
        self.resize(600, 500)
        
        # Center
        screen = QApplication.primaryScreen().geometry()
        x = (screen.width() - self.width()) // 2
        y = (screen.height() - self.height()) // 2
        self.move(x, y)

        self.home_interface = MonitorControlInterface(self)
        self.home_interface.setObjectName("homeInterface")

        self.addSubInterface(self.home_interface, FIF.HOME, "Control Center")
        
if __name__ == '__main__':
    # Enable High DPI
    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    
    app = QApplication(sys.argv)
    
    setTheme(Theme.AUTO) 
    
    w = MainWindow()
    w.show()
    sys.exit(app.exec())

