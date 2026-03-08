# DisplaySwitch

<p align="center">
  <strong>Cross-platform display management — brightness, input switching, and topology visualization</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Tauri-2.10-blue?logo=tauri" alt="Tauri 2" />
  <img src="https://img.shields.io/badge/Svelte-5-orange?logo=svelte" alt="Svelte 5" />
  <img src="https://img.shields.io/badge/Rust-1.94-red?logo=rust" alt="Rust" />
  <img src="https://img.shields.io/badge/C++-20-blueviolet?logo=cplusplus" alt="C++20" />
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20macOS-green" alt="Platform" />
</p>

---

## Features

| Category | Details |
|---|---|
| **Display Detection** | CCD API + Registry EDID + CEA-861 parsing (HDMI VSDB / HF-VSDB / HDR metadata) |
| **Brightness Control** | DDC/CI VCP 0x10 for external monitors, WMI for laptop internal displays |
| **Input Switching** | DDC/CI VCP 0x60 with full input enumeration (VGA / DVI / DP / HDMI / USB-C) |
| **Custom Input Names** | Rename input sources via RenameDialog modal, persistently stored per monitor |
| **GPU Info** | WMI-based detection — model, VRAM, driver version |
| **Hot-Plug** | Real-time display connect / disconnect via `WM_DISPLAYCHANGE` + `WM_DEVICECHANGE` |
| **Topology View** | Adaptive SVG visualization: GPU → connection → display with bandwidth info |
| **Global Hotkeys** | System-wide keyboard shortcuts for brightness, refresh, presets (even when minimized) |
| **Scene Profiles** | Work / Gaming / Movie profiles with auto-trigger by display count or time schedule |
| **Presets** | Quick-apply brightness + input combinations with one click |
| **Auto-Brightness** | Ambient Light Sensor integration with logarithmic brightness curve |
| **Theme** | Dark (Tokyo Night) / Light / System-follow modes |
| **i18n** | English + 中文, auto-detect system locale |
| **Auto-Start** | Launch on system boot (Windows Registry / macOS LaunchAgent) |
| **Auto-Update** | GitHub Releases update check via `tauri-plugin-updater` |
| **System Tray** | Minimize to tray, tray menu, click to restore |
| **macOS Transparent Popup** | Native macOS private API for fully transparent tray popup (no background rectangle) |
| **Rename Dialog** | Modal UI for renaming displays and input sources with individual reset buttons |

## Architecture

```
┌────────────────────────────────────────────────────┐
│  Svelte 5 Frontend (Vite 6)                        │
│  App.svelte → MonitorCard / TopologyView / ...     │
├────────────────────────────────────────────────────┤
│  Tauri 2 Rust Backend                              │
│  Commands: scan_monitors, set_brightness, ...      │
│  libloading FFI → displayswitch_ffi.dll            │
├────────────────────────────────────────────────────┤
│  C++20 Native Core                                 │
│  CCD / DDC/CI / EDID / WMI / VCP Controller       │
└────────────────────────────────────────────────────┘
```

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| **Rust** | 1.94+ | `rustup install stable` |
| **Node.js** | 18+ | For Svelte / Vite frontend build |
| **Visual Studio 2022** | 17.x | MSVC C++20 + Windows SDK (Windows only) |
| **CMake** | 3.22+ | For C++ native core build |

## Build

### 1. Build C++ native core

```bash
cd core_native
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This produces `displayswitch_ffi.dll` (Windows) which is loaded at runtime by the Rust backend.

### 2. Build & run the Tauri app

```bash
cd tauri_ui
npm install
npx tauri dev      # development with HMR
npx tauri build    # production build → installer
```

The build produces platform-specific installers (`.msi` / `.nsis` on Windows, `.dmg` / `.app` on macOS).

## Project Structure

```
DisplaySwitch/
├── core_native/                 # C++20 native library
│   ├── include/displayswitch/   # Public headers (c_api.h, edid_parser.h, ...)
│   ├── src/                     # Implementation
│   │   ├── platform/windows/    # CCD, DDC/CI, WMI, EDID registry
│   │   └── platform/macos/     # IOKit stubs (planned)
│   └── tests/                   # Unit tests (EDID parser)
├── tauri_ui/                    # Tauri desktop application
│   ├── src/                     # Svelte 5 frontend
│   │   ├── App.svelte           # Main application shell
│   │   └── lib/                 # Components & utilities
│   │       ├── MonitorCard.svelte
│   │       ├── TopologyView.svelte
│   │       ├── SettingsPanel.svelte
│   │       ├── TitleBar.svelte
│   │       ├── StatusBar.svelte
│   │       ├── ProfileBar.svelte
│   │       ├── RenameDialog.svelte
│   │       ├── configStore.ts
│   │       ├── i18n.ts
│   │       ├── theme.ts
│   │       └── ambientLight.ts
│   └── src-tauri/               # Rust backend
│       ├── src/main.rs          # Tauri commands + FFI bridge
│       └── Cargo.toml           # Dependencies
├── .github/workflows/           # CI/CD (Windows + macOS)
└── ROADMAP.md                   # Development roadmap
```

## CI/CD

GitHub Actions builds on every push to `main`:

- **Windows** — MSVC x64
- **macOS arm64** — Apple Silicon
- **macOS x86_64** — Intel

## License

MIT

