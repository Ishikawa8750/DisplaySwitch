# PyInstaller hook for PyQt-Fluent-Widgets
from PyInstaller.utils.hooks import collect_data_files

datas = collect_data_files('qfluentwidgets')
