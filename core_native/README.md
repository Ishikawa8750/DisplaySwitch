# DisplaySwitch Native Core

High-performance display detection and control library written in C++.

## Features

- Complete EDID 1.4/2.0 parsing
- Physical connector enumeration
- HDMI/DisplayPort version detection
- GPU detailed information via DXGI
- DDC/CI VCP control
- Thunderbolt topology detection

## Build Requirements

### Windows

- Visual Studio 2022 or later
- CMake 3.20+
- Windows SDK 10.0.22000.0+

### macOS

- Xcode 14+
- CMake 3.20+

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

## Python Bindings

```bash
pip install .
```

## Usage

```python
from displayswitch_native import DisplayDetector

detector = DisplayDetector()
displays = detector.scan()

for display in displays:
    print(f"Monitor: {display.name}")
    print(f"Connection: {display.connection_detail}")
    print(f"Resolution: {display.width}x{display.height}@{display.refresh_rate}Hz")
    print(f"Physical Ports: {len(display.connectors)}")
    for connector in display.connectors:
        print(f"  - {connector.type} Port {connector.index}")
```
