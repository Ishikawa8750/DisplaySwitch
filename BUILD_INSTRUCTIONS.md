# Building DisplaySwitch

## Windows
1.  **Install Requirements**
    Run `pip install -r requirements.txt`. 
    Ensure you have `PySide6-Fluent-Widgets` installed (NOT `PyQt-Fluent-Widgets`).
    `pip install PySide6-Fluent-Widgets`

2.  **Build Exe**
    Open a terminal in the `DisplaySwitch\DisplaySwitch` folder.
    Run the following command:
    ```powershell
    pyinstaller --noconfirm --noconsole --onefile --additional-hooks-dir=. --name DisplaySwitch_Fluent app_fluent.py
    ```
    
    The executable will be in the `dist` folder.

## macOS (Apple Silicon)
1.  **Prerequisites**
    - Python 3.10+
    - `m1ddc` binary (either installed via `brew install m1ddc` or downloaded manually).

2.  **Install Requirements**
    ```bash
    pip install -r requirements.txt
    pip install pyinstaller
    ```

3.  **Build App Bundle**
    To create a standalone `.app` that includes the `m1ddc` tool:
    
    First, ensure the `m1ddc` executable is in the same folder as `app_fluent.py`.
    
    Then run:
    ```bash
    pyinstaller --noconfirm --windowed --onefile --additional-hooks-dir=. --add-binary "m1ddc:." --name DisplaySwitch_Fluent app_fluent.py
    ```
    
    This creates `dist/DisplaySwitch_Fluent.app`.
    
    The `--add-binary "m1ddc:."` flag tells PyInstaller to bundle the `m1ddc` tool inside the app bundle, and our code `backend.py` is written to look for it there.

4.  **Create DMG (Optional)**
    You can use tools like `create-dmg` to wrap the `.app` into a `.dmg` for distribution.
    ```bash
    brew install create-dmg
    create-dmg dist/DisplaySwitch_Fluent.app
    ```

## Development Notes
- The GUI is built with **PySide6** and **PyQt-Fluent-Widgets** (using the PySide6 backend).
- `app_fluent.py` is the main entry point.
- `monitor_control/backend.py` contains the cross-platform logic.
- `hook-qfluentwidgets.py` is a custom PyInstaller hook to ensure UI assets are included.
