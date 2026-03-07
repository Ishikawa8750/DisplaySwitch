import os
os.environ["QT_API"] = "PySide6"
from PySide6.QtWidgets import QApplication
from qfluentwidgets import FluentWindow

app = QApplication([])
w = FluentWindow()
w.show()
print("Success")
