# DisplaySwitch (Cross-Platform)

A modern GUI tool to switch monitor inputs and control brightness on Windows and macOS (Apple Silicon compatible).

## Prerequisites

### Windows

- Python 3.x
- No extra system tools required.

### macOS (Apple Silicon / M1 / M2)

- Python 3.x
- **m1ddc**: This tool is required to send DDC commands on Apple Silicon.
  - Install via Homebrew: `brew install m1ddc`
  - Or manually download from: <https://github.com/waydabber/m1ddc>

## Setup

1. **Install Dependencies**:

    ```bash
    pip install -r requirements.txt
    ```

2. **Run the Application**:

    ```bash
    python main.py
    ```

## Usage

- **Switch Input**: Click the buttons to switch between Mac (USB-C) and Windows (HDMI).
- **Brightness**: Use the slider to adjust the external monitor's brightness.

## Configuration

The Input Source values are set to:

- Mac (USB-C/DP): 15
- Windows (HDMI2): 17

(These defaults are based on your specific BenQ monitor scan. Edit `main.py` if needed.)

## Troubleshooting (Mac)

- Ensure your Mac has proper permissions to run `m1ddc` (sometimes requires running this app via Terminal with correct permissions).

