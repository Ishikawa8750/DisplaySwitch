# DisplaySwitch Universal - 实施路线图

> **最后更新**: Phase 7 UI增强 + 高级功能完成  
> **Python版本**: ✅ 完成 → `python_version/`  
> **C++ 核心**: ✅ 编译通过 + 5/5 测试通过 + FFI DLL + WMI 亮度 → `core_native/`  
> **Tauri 前端**: ✅ C++ FFI 直连 + 系统托盘 + 热插拔 + 亮度同步 + 全局热键 + 自启动 + 配置持久化 + 主题/i18n/场景/ALS/拓扑/更新 → `tauri_ui/`

---

## 🔍 当前状态

### ✅ Phase 1 & Phase 2 — 已完成 (Python 版)

- 注册表 EDID 完整读取 + CEA-861 解析（HDMI VSDB / HF-VSDB / HDR）
- WMI GPU 检测 + 适配器匹配（修复了 EnumDisplayDevices 枚举逻辑）
- 连接协议细节（HDMI 2.0 / eDP / DP，带宽计算）
- 环境光传感器 (Windows Sensor API)
- SQLite 配置持久化（预设 / 场景配置 / 偏好）
- 完整 PySide6 + qfluentwidgets UI（MonitorCard / AmbientLightCard / SceneProfileCard）
- **7/7 测试全部通过**
- 已打包至 `python_version/`，附 `VERSION.md`

### ✅ Phase 3 — 验证完成 (C++ + Tauri)

- **C++ core_native**: CMake 编译通过（STATIC lib + PyBind11 .pyd + 测试 exe）
  - 编译器: MSVC 19.44 (VS 2022 Professional 17.14.27)
  - Windows SDK: 10.0.26100.0, C++20
  - **5/5 单元测试全部通过** ✅
- **Tauri + Svelte 前端**: 392+ Rust crates 编译通过，应用窗口成功启动
  - Rust 1.94.0 + Tauri 2.10.3 + Svelte 5 + Vite 6.4.1

### ✅ Phase 4 — C++ FFI + 功能增强 (当前)

- **C++ FFI DLL** (`displayswitch_ffi.dll`) — 平坦 C API，opaque handle 模式
- **Rust libloading** — 替换 Python 子进程，零运行时依赖
- **亮度控制修复** — 统一使用 Low-Level VCP API (VCP 0x10)
  - 根因: High-Level API (`SetMonitorBrightness`) 与 Low-Level API (`SetVCPFeature`) 混用导致失败
  - 修复: 所有 DDC/CI 操作统一使用 Low-Level API
- **输入源切换** — 枚举 supported_inputs 列表，前端显示切换按钮
- **物理监视器句柄** — 修复重扫描时的句柄泄漏 (`close()` → `scan()`)
- **前端增强**:
  - 亮度滑块 150ms throttle（防止 DDC/CI 总线过载）
  - 错误状态提示（红色 toast）
  - DDC/CI 不支持时显示 "Not available"
  - 输入源名称完整映射（VGA/DVI/DP/HDMI/USB-C）
- **系统托盘** — 关闭窗口最小化到托盘，托盘菜单 Show/Quit，双击恢复
- **Rust 端错误诊断** — 加载 `ds_last_error()` 符号，返回详细 DDC/CI 错误信息

## ✅ Phase 1: EDID + GPU 增强 — 已完成

> 全部功能已集成到 `python_version/core/` 中

- **edid_reader.py** — 注册表直读完整 256 字节 EDID  
  - CEA-861 扩展块、HDMI VSDB、HF-VSDB（FRL）、HDR 静态元数据
- **gpu_detector.py** — WMI `Win32_VideoController` + 适配器名匹配  
  - 完整 GPU 型号 + VRAM + 驱动版本
- **控制器修复** — `EnumDisplayDevicesW(None, idx)` 循环枚举适配器  
  - 内部显示器 EDID 回退命名 ("AUO 0xE495")

## ✅ Phase 2: 连接细节 + 自适应亮度 + 配置 — 已完成

- **connection_detail.py** — DEVMODE 分辨率、带宽计算、USB-C 检测
- **ambient_light.py** — Windows Sensor API (PowerShell 桥接)
- **config_store.py** — SQLite 持久化（monitors / presets / profiles / preferences）
- **main.py** — 完整 UI (MonitorCard 信息网格 + 亮度滑块 + 输入切换 + 预设管理)
- **测试**: 7/7 通过（EDID / GPU / Controller / ConnectionDetail / AmbientLight / ConfigStore / ExtendedDisplayRef）

## ✅ Phase 3: C++ 核心 + Tauri 前端 — 验证完成

### 3.1 C++ core_native（✅ 编译 + 测试通过）

```text
core_native/
├── include/displayswitch/
│   ├── edid_parser.h           # EDIDInfo / VideoMode / Connector / HDRMetadata
│   └── display_detector.h      # GPUInfo / BandwidthInfo / DisplayInfo / 抽象接口
├── src/
│   ├── edid_parser.cpp         # 完整 EDID 1.4 解析（CEA-861 / HDMI VSDB / HF-VSDB / HDR）
│   ├── display_detector.cpp    # calculate_bandwidth()
│   ├── gpu_info.cpp            # GPUInfo::formatted_name()
│   ├── vcp_controller.cpp      # stub
│   ├── platform/windows/
│   │   ├── display_detector_win.cpp  # EnumDisplayMonitors + Registry EDID + CCD + DDC/CI
│   │   ├── gpu_info_win.cpp          # WMI COM (Win32_VideoController)
│   │   └── vcp_controller_win.cpp    # read_vcp / write_vcp
│   ├── platform/macos/              # stubs
│   └── bindings/python_bindings.cpp  # PyBind11 完整绑定
├── tests/
│   ├── test_edid_parser.cpp    # 5 个单元测试
│   └── CMakeLists.txt
└── CMakeLists.txt              # v2.0.0, FetchContent pybind11
```

**构建产物**:

- ✅ `core_native/build/Release/displayswitch_core.lib` — C++20 静态库
- ✅ `core_native/build/Release/displayswitch_native.cp313-win_amd64.pyd` — Python 绑定
- ✅ `core_native/build/tests/Release/test_edid_parser.exe` — 5/5 测试通过

### 3.2 Tauri + Svelte 前端（✅ 编译 + 启动成功）

```text
tauri_ui/
├── package.json                # Svelte 5 + Tauri 2 + Vite 6
├── vite.config.ts / tsconfig.json / index.html
├── src/
│   ├── main.ts                 # Svelte 5 挂载
│   ├── App.svelte              # 主应用（scan_monitors invoke, 加载/错误状态）
│   └── lib/
│       ├── types.ts            # TS 接口（匹配 C++ 结构体）
│       └── MonitorCard.svelte  # 显示器卡片（信息网格 + 亮度 + 输入切换）
└── src-tauri/
    ├── Cargo.toml              # tauri 2, serde, serde_json
    ├── build.rs
    ├── tauri.conf.json         # 窗口 960×740
    └── src/main.rs             # Rust 后端 — scan_monitors / set_brightness / set_input
```

**构建状态**:

- ✅ Rust 1.94.0 + 392 crates 编译通过（32.69s）
- ✅ `displayswitch-app.exe` 启动成功，Tauri 窗口正常打开
- ✅ Vite HMR 热更新正常工作

### 3.3 macOS 支持（已有 stub）

- `core_native/src/platform/macos/` 包含 `.mm` 空实现
- 未来集成 IOKit / CoreGraphics / m1ddc

## ✅ Phase 4: C++ FFI + 功能增强 — 已完成

### 4.1 C API FFI 层（✅ 完成）

```text
core_native/
├── include/displayswitch/
│   └── c_api.h                 # 平坦 C API (DsDetector* 不透明句柄)
└── src/
    └── c_api.cpp               # 包装 C++ DisplayDetector，线程安全 mutex
```

**DLL 导出函数**:

- `ds_create_detector()` / `ds_destroy_detector()` — 生命周期管理
- `ds_scan()` / `ds_free_displays()` — 显示器枚举（含亮度、输入源读取）
- `ds_set_brightness()` / `ds_get_brightness()` — DDC/CI VCP 0x10
- `ds_set_input()` / `ds_get_input()` — DDC/CI VCP 0x60
- `ds_last_error()` — 详细错误信息

### 4.2 关键 Bug 修复（✅ 完成）

- **VCP API 混用 bug**: `set_brightness`/`get_brightness` 从 High-Level API 改为 Low-Level VCP 0x10
- **supported_inputs 空列表**: 扫描时通过 VCP 0x60 max value 枚举支持的输入源
- **物理监视器句柄泄漏**: `scan()` 前调用 `close()` 释放旧句柄 + 刷新 CCD 映射
- **亮度滑块无节流**: 前端添加 150ms throttle 防止 DDC/CI 总线过载

### 4.3 Rust FFI (libloading)（✅ 完成）

```text
tauri_ui/src-tauri/
├── Cargo.toml                  # tauri 2 [tray-icon, image-png] + libloading 0.8
├── displayswitch_ffi.dll       # CMake 自动拷贝
└── src/main.rs                 # NativeCore: 动态加载 DLL，缓存函数指针
```

### 4.4 系统托盘（✅ 完成）

- 关闭窗口 → 最小化到系统托盘（不退出）
- 托盘右键菜单: Show Window / Quit
- 点击托盘图标恢复窗口

### 4.5 前端增强（✅ 完成）

- DDC/CI 不可用时显示 "Not available" 提示
- 错误状态 toast 提示（红色背景）
- 输入源名称完整映射（0x01=VGA, 0x03=DVI, 0x0F=DP, 0x11=HDMI, 0x1B=USB-C）
- `display_index` 替代 `device_path` 用于控制命令

## 🎯 下一步行动

### ✅ Phase 5: 高级功能 — 已完成

#### 5.1 笔记本内屏亮度控制（✅ WMI）

- **WMI 亮度模块** (`wmi_brightness_win.cpp`) — 使用 `WmiMonitorBrightness` / `WmiMonitorBrightnessMethods`
- 内屏（eDP/LVDS）不支持 DDC/CI → 改用 WMI COM 接口
- `wmi_get_brightness()` / `wmi_set_brightness()` — 读写内屏亮度 (0-100)
- 自动集成到 `WindowsDisplayDetector::set_brightness()` / `get_brightness()` — 内屏走 WMI，外屏走 DDC/CI
- 扫描时自动缓存内屏亮度 (`cached_brightness = wmi_get_brightness()`)
- 前端自动显示亮度滑块（`brightness >= 0`）

#### 5.2 热插拔检测（✅ WM_DISPLAYCHANGE）

- Rust 后端创建隐藏 message-only 窗口监听 `WM_DISPLAYCHANGE` + `WM_DEVICECHANGE`
- 显示器连接/断开时自动 emit `displays-changed` 事件到前端
- 前端收到事件后 1.5s 防抖自动重新扫描（避免连续消息重复触发）

#### 5.3 自动亮度同步（✅ 定时轮询）

- 前端每 5 秒调用 `get_brightness` 轮询各显示器亮度
- 使用 C API `ds_get_brightness()` 快速读取（不触发全量扫描）
- Rust 端新增 `get_brightness` Tauri command（直接调用 FFI 函数指针）
- `brightnessOverride` 自动清除机制（拖动停止 3 秒后同步后端值）

#### 5.4 新增文件

```text
core_native/
├── include/displayswitch/
│   └── wmi_brightness.h              # WMI 亮度控制头文件
└── src/platform/windows/
    └── wmi_brightness_win.cpp         # WMI COM 实现 (root\wmi)
```

### ✅ Phase 6: 高级功能 — 已完成

#### 6.1 全局热键（✅ tauri-plugin-global-shortcut）

- **Rust 后端**: `register_hotkeys` / `unregister_all_hotkeys` Tauri commands
- **global-hotkey v0.7.0** — 跨平台全局键盘快捷键（窗口隐藏时仍然生效）
- 默认热键: Ctrl+Alt+Up (亮度+10%), Ctrl+Alt+Down (亮度-10%), Ctrl+Alt+R (刷新)
- Rust handler 通过 `app.emit("hotkey-action", shortcut_str)` 发送事件到前端
- 前端根据配置匹配 shortcut → action，执行对应操作
- 设置面板支持自定义录制快捷键（按键录制模式）

#### 6.2 自动启动（✅ tauri-plugin-autostart）

- **auto-launch v0.5.0** — Windows 注册表 `Run` 键自动启动
- 前端设置面板一键 Toggle（实时检测当前状态）
- 启动参数: `--autostarted` 标识自动启动场景
- macOS 兼容 (LaunchAgent)

#### 6.3 配置/预设持久化（✅ tauri-plugin-store）

- **tauri-plugin-store v2.4.2** — 本地 JSON 文件持久化 (`config.json`)
- `AppConfig` 结构: autostart / sync\_interval\_ms / hotkeys / presets
- `HotkeyBinding`: shortcut + action type + value + label
- `Preset`: 命名预设，每个显示器可配置 brightness + input\_code
- `configStore.ts`: loadConfig / saveConfig / updateConfig / savePresets / saveHotkeys
- 默认配置合并机制（新版本添加字段时自动补全）

#### 6.4 设置面板 UI（✅ SettingsPanel.svelte）

- 三标签页: General / Hotkeys / Presets
- **General**: 自启动 Toggle + 同步间隔选择 (2s/5s/10s/30s)
- **Hotkeys**: 快捷键列表 + 按键录制 + 动作选择 + 增删
- **Presets**: 预设管理 + 展开编辑各显示器设置 + 一键应用
- Header 预设快速按钮（⚡ 图标）
- 深色主题一致风格

#### 6.5 新增/修改文件

```text
tauri_ui/
├── src/
│   ├── App.svelte                    # 集成设置面板 + 热键监听 + 预设应用
│   └── lib/
│       ├── types.ts                  # +AppConfig/Preset/HotkeyBinding 接口
│       ├── configStore.ts            # NEW: 配置持久化模块
│       └── SettingsPanel.svelte      # NEW: 设置面板组件
└── src-tauri/
    ├── Cargo.toml                    # +tauri-plugin-{global-shortcut,autostart,store}
    ├── src/main.rs                   # +register_hotkeys/unregister commands + 插件初始化
    └── capabilities/
        ├── default.json              # +global-shortcut:default, autostart:default
        └── desktop.json              # NEW: 桌面平台专用 capabilities
```

### ✅ Phase 7: UI 增强 + 高级功能 — 已完成

#### 7.1 主题 / Toast / i18n / 场景配置（✅ Phase 7a）

- **主题系统** (`theme.ts`) — dark / light / system 三模式切换
  - `applyTheme()` 切换 `<html>` class，`watchSystemTheme()` 监听系统主题变化
  - 所有组件支持 `:global(html.light)` CSS 覆写
- **Toast 通知** (`toast.svelte.ts` + `ToastContainer.svelte`) — 全局消息提示
  - `toast.success()` / `.error()` / `.info()` 响应式通知栏
  - 自动消失 (3s)，支持多条堆叠
- **国际化** (`i18n.ts`) — en / zh / auto 三语模式
  - 80+ 翻译键，支持 `{0}` 占位符
  - `setLocale()` + `onLocaleChange()` + `_localeVersion` 响应式更新
- **场景配置** (`ProfileBar.svelte`) — 工作/游戏/演示/影音/自定义场景
  - 一键切换场景（应用亮度+输入源组合）
  - `Profile` 类型 + `configStore` 持久化

#### 7.2 状态栏（✅ Phase 7b）

- **StatusBar.svelte** — 底部固定状态栏
  - 显示器数量（内屏+外屏分类）、当前场景、ALS lux、最后同步时间、版本号
  - 同步状态脉冲动画，深色/浅色主题适配

#### 7.3 环境光自适应亮度（✅ Phase 7b）

- **Rust 后端** (`get_ambient_light` command) — PowerShell → Windows.Devices.Sensors.LightSensor WinRT
  - `CREATE_NO_WINDOW` (0x08000000) 隐藏 PowerShell 窗口
  - JSON 输出解析 → `ALSStatus { available, lux, recommended_brightness }`
- **前端** (`ambientLight.ts`) — `startAutoBrightness()` 循环自动调节
  - 对数亮度曲线: 0-10lux→10-25%, 10-100→25-50%, 100-500→50-75%, 500-10000→75-100%
  - 配置: `als_enabled` + `als_interval_ms` (默认 10s)
  - SettingsPanel General 标签页增加 ALS 开关 + 轮询间隔选择

#### 7.4 显示拓扑可视化（✅ Phase 7b）

- **TopologyView.svelte** — 纯 SVG 拓扑图（无 D3.js 依赖）
  - GPU → 连接线 → 显示器节点，按 GPU 分组
  - 颜色编码: Thunderbolt/USB-C=橙色, DP=绿色, HDMI=蓝色, eDP=紫色
  - 显示分辨率/刷新率/亮度，虚线=外接
  - Header 拓扑切换按钮 🔗

#### 7.5 自动更新（✅ Phase 7b）

- **tauri-plugin-updater v2** — GitHub Releases JSON 端点
  - `check_for_update` Tauri command → 检测新版本
  - SettingsPanel "检查更新" 按钮 + 状态提示
  - capabilities `updater:default` 权限
  - tauri.conf.json updater endpoint 配置

#### 7.6 场景自动切换（✅ Phase 7b）

- **ProfileTrigger** 类型: `display_count` (显示器数量) / `schedule` (时段)
  - 热插拔事件后自动检查 display_count 触发
  - 定时轮询时段触发（start_hour / end_hour 支持跨午夜）
  - 自动跳过已激活的场景

#### 7.7 新增/修改文件

```text
tauri_ui/
├── src/
│   ├── App.svelte                    # +StatusBar/TopologyView/ALS 集成 + 场景自动切换
│   └── lib/
│       ├── types.ts                  # +ProfileTrigger + als_enabled/als_interval_ms
│       ├── configStore.ts            # +ALS 默认值 + 合并逻辑
│       ├── i18n.ts                   # +StatusBar/ALS/Topology/Updater 翻译键 (30+)
│       ├── StatusBar.svelte          # NEW: 底部状态栏
│       ├── TopologyView.svelte       # NEW: SVG 拓扑可视化
│       ├── ambientLight.ts           # NEW: ALS 前端模块
│       ├── theme.ts                  # Phase 7a
│       ├── toast.svelte.ts           # Phase 7a
│       ├── ToastContainer.svelte     # Phase 7a
│       └── ProfileBar.svelte         # Phase 7a
└── src-tauri/
    ├── Cargo.toml                    # +tauri-plugin-updater v2
    ├── src/main.rs                   # +get_ambient_light/check_for_update commands
    ├── tauri.conf.json               # +plugins.updater 端点配置
    └── capabilities/default.json     # +updater:default
```

### Phase 8: 未来计划（待开始）
- [ ] macOS 完整支持 (IOKit / m1ddc / CoreGraphics)
- [ ] Linux 支持 (xrandr / DDC/CI i2c-dev)
- [ ] 多显示器拖拽排列（虚拟布局编辑器）
- [ ] 色彩配置管理 (ICC Profile)
- [ ] HDR 开关控制
- [ ] 显示器 OSD 菜单远程控制 (VCP 扩展)

### 已安装的工具链

| 工具 | 版本 | 路径 |
| ------ | ------ | ------ |
| Rust | 1.94.0 stable | `%USERPROFILE%\.cargo\bin` |
| VS 2022 Professional | 17.14.27 | `C:\Program Files\Microsoft Visual Studio\2022\Professional` |
| MSVC | 19.44.35223.0 | (VS 2022 内置) |
| CMake | 3.31.6 | (VS 2022 内置) |
| Windows SDK | 10.0.26100.0 | (VS 安装) |
| Node.js | v24.11.0 | (系统 PATH) |
| Python | 3.13.12 | (MS Store) |
