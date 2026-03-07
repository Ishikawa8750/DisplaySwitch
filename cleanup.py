import os

# Remove old .bat files that used the python script directly from root, 
# as user requested a clean "program" approach.
# But checking user request: "把目前的脚本版本存入一个独立的文件夹" -> DONE.
# "然后再开始编写程序" -> DONE (GUI).
# I will clean up the old bat files from the root to avoid confusion, 
# since they are now in script_version_v1.

if os.path.exists(r"f:\DisplaySwitch\DisplaySwitch\monitor_control.py"):
    os.remove(r"f:\DisplaySwitch\DisplaySwitch\monitor_control.py")
if os.path.exists(r"f:\DisplaySwitch\DisplaySwitch\get_capabilities.py"):
    os.remove(r"f:\DisplaySwitch\DisplaySwitch\get_capabilities.py")
if os.path.exists(r"f:\DisplaySwitch\DisplaySwitch\switch_to_mac.bat"):
    os.remove(r"f:\DisplaySwitch\DisplaySwitch\switch_to_mac.bat")
if os.path.exists(r"f:\DisplaySwitch\DisplaySwitch\switch_to_windows.bat"):
    os.remove(r"f:\DisplaySwitch\DisplaySwitch\switch_to_windows.bat")
if os.path.exists(r"f:\DisplaySwitch\DisplaySwitch\set_brightness.bat"):
    os.remove(r"f:\DisplaySwitch\DisplaySwitch\set_brightness.bat")
print("Cleanup complete.")
