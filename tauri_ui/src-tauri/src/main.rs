//! DisplaySwitch Tauri Backend — Phase 6
//!
//! Direct FFI to displayswitch_ffi.dll (C++ native core).
//! + Hot-plug detection (WM_DISPLAYCHANGE)
//! + Periodic brightness sync
//! + WMI internal display brightness (via C++ core)
//! + Global hotkeys (Phase 6)
//! + Autostart (Phase 6)
//! + Config/preset persistence (Phase 6)

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use libloading::{Library, Symbol};
use serde::{Deserialize, Serialize};
use std::ffi::{c_char, c_int, c_void, CStr};
use std::path::PathBuf;
use std::sync::Arc;
use std::sync::Mutex;
use tauri::{
    image::Image,
    menu::{Menu, MenuItem},
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    Emitter, Manager, WindowEvent,
};

#[cfg(target_os = "macos")]
use tauri::menu::Submenu;
use tauri_plugin_global_shortcut::{GlobalShortcutExt, Shortcut, ShortcutState};
use tauri_plugin_updater::UpdaterExt;

#[cfg(target_os = "windows")]
use std::os::windows::process::CommandExt;

// ─── C API types (mirror c_api.h) ───────────────────────────────────────

#[repr(C)]
struct DsGPUInfoC {
    name: *const c_char,
    vendor_name: *const c_char,
    dedicated_vram_bytes: u64,
    driver_version: *const c_char,
    formatted_name: *const c_char,
}

#[repr(C)]
struct DsBandwidthInfoC {
    max_bandwidth_gbps: f64,
    bandwidth_str: *const c_char,
    can_support_4k60: c_int,
    can_support_4k120: c_int,
    can_support_8k60: c_int,
}

#[repr(C)]
struct DsDisplayInfoC {
    name: *const c_char,
    device_path: *const c_char,
    manufacturer_id: *const c_char,
    product_code: *const c_char,
    is_internal: c_int,

    gpu: DsGPUInfoC,

    connection_type: *const c_char,
    refresh_rate: f64,
    hdmi_version: *const c_char,
    hdmi_frl_rate: *const c_char,
    max_tmds_clock_mhz: u16,

    supports_hdr: c_int,
    hdr_formats: *const c_char,

    screen_width_mm: u16,
    screen_height_mm: u16,

    resolution_width: u16,
    resolution_height: u16,
    resolution_str: *const c_char,
    bits_per_pixel: u8,

    bandwidth: DsBandwidthInfoC,

    current_input: c_int,
    supported_inputs: *const c_int,
    supported_inputs_count: c_int,
    brightness: c_int,
}

// ─── Serde types for Tauri frontend ─────────────────────────────────────

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GpuInfo {
    pub name: String,
    pub vendor_name: String,
    pub dedicated_vram_bytes: u64,
    pub driver_version: String,
    pub formatted_name: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BandwidthInfo {
    pub max_bandwidth_gbps: f64,
    pub bandwidth_str: String,
    pub can_support_4k60: bool,
    pub can_support_4k120: bool,
    pub can_support_8k60: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DisplayInfo {
    pub name: String,
    pub device_path: String,
    pub manufacturer_id: String,
    pub product_code: String,
    pub is_internal: bool,
    pub gpu: GpuInfo,
    pub connection_type: String,
    pub refresh_rate: f64,
    pub hdmi_version: Option<String>,
    pub hdmi_frl_rate: Option<String>,
    pub max_tmds_clock_mhz: u16,
    pub supports_hdr: bool,
    pub hdr_formats: Vec<String>,
    pub screen_width_mm: u16,
    pub screen_height_mm: u16,
    pub resolution_width: u16,
    pub resolution_height: u16,
    pub resolution_str: String,
    pub bits_per_pixel: u8,
    pub bandwidth: BandwidthInfo,
    pub current_input: i32,
    pub supported_inputs: Vec<i32>,
    pub brightness: i32,
    pub display_index: usize,
}

// ─── Safe FFI wrapper ───────────────────────────────────────────────────

unsafe fn cstr_to_string(p: *const c_char) -> String {
    if p.is_null() {
        String::new()
    } else {
        CStr::from_ptr(p).to_string_lossy().into_owned()
    }
}

unsafe fn cstr_to_option(p: *const c_char) -> Option<String> {
    if p.is_null() {
        None
    } else {
        let s = CStr::from_ptr(p).to_string_lossy().into_owned();
        if s.is_empty() {
            None
        } else {
            Some(s)
        }
    }
}

unsafe fn convert_display(c: &DsDisplayInfoC, index: usize) -> DisplayInfo {
    let hdr_str = cstr_to_string(c.hdr_formats);
    let hdr_formats: Vec<String> = if hdr_str.is_empty() {
        vec![]
    } else {
        hdr_str.split(", ").map(|s| s.to_string()).collect()
    };

    let mut supported_inputs = Vec::new();
    if !c.supported_inputs.is_null() && c.supported_inputs_count > 0 {
        let slice =
            std::slice::from_raw_parts(c.supported_inputs, c.supported_inputs_count as usize);
        supported_inputs = slice.iter().copied().collect();
    }

    DisplayInfo {
        name: cstr_to_string(c.name),
        device_path: cstr_to_string(c.device_path),
        manufacturer_id: cstr_to_string(c.manufacturer_id),
        product_code: cstr_to_string(c.product_code),
        is_internal: c.is_internal != 0,
        gpu: GpuInfo {
            name: cstr_to_string(c.gpu.name),
            vendor_name: cstr_to_string(c.gpu.vendor_name),
            dedicated_vram_bytes: c.gpu.dedicated_vram_bytes,
            driver_version: cstr_to_string(c.gpu.driver_version),
            formatted_name: cstr_to_string(c.gpu.formatted_name),
        },
        connection_type: cstr_to_string(c.connection_type),
        refresh_rate: c.refresh_rate,
        hdmi_version: cstr_to_option(c.hdmi_version),
        hdmi_frl_rate: cstr_to_option(c.hdmi_frl_rate),
        max_tmds_clock_mhz: c.max_tmds_clock_mhz,
        supports_hdr: c.supports_hdr != 0,
        hdr_formats,
        screen_width_mm: c.screen_width_mm,
        screen_height_mm: c.screen_height_mm,
        resolution_width: c.resolution_width,
        resolution_height: c.resolution_height,
        resolution_str: cstr_to_string(c.resolution_str),
        bits_per_pixel: c.bits_per_pixel,
        bandwidth: BandwidthInfo {
            max_bandwidth_gbps: c.bandwidth.max_bandwidth_gbps,
            bandwidth_str: cstr_to_string(c.bandwidth.bandwidth_str),
            can_support_4k60: c.bandwidth.can_support_4k60 != 0,
            can_support_4k120: c.bandwidth.can_support_4k120 != 0,
            can_support_8k60: c.bandwidth.can_support_8k60 != 0,
        },
        current_input: c.current_input,
        supported_inputs,
        brightness: c.brightness,
        display_index: index,
    }
}

// ─── NativeCore: manages DLL + detector lifetime ────────────────────────

struct NativeCore {
    _lib: Library,
    detector: *mut c_void,
    fn_scan: unsafe extern "C" fn(*mut c_void, *mut c_int) -> *mut DsDisplayInfoC,
    fn_free_displays: unsafe extern "C" fn(*mut DsDisplayInfoC, c_int),
    fn_set_brightness: unsafe extern "C" fn(*mut c_void, c_int, c_int) -> c_int,
    fn_get_brightness: unsafe extern "C" fn(*mut c_void, c_int) -> c_int,
    fn_set_input: unsafe extern "C" fn(*mut c_void, c_int, c_int) -> c_int,
    fn_get_input: unsafe extern "C" fn(*mut c_void, c_int) -> c_int,
    fn_last_error: unsafe extern "C" fn() -> *const c_char,
    fn_destroy: unsafe extern "C" fn(*mut c_void),
}

unsafe impl Send for NativeCore {}
unsafe impl Sync for NativeCore {}

impl NativeCore {
    fn new() -> Result<Self, String> {
        let dll_path = Self::find_dll()?;
        eprintln!("[NativeCore] Loading DLL: {}", dll_path.display());

        let lib = unsafe {
            Library::new(&dll_path)
                .map_err(|e| format!("Failed to load {}: {}", dll_path.display(), e))?
        };

        unsafe {
            let fn_create: Symbol<unsafe extern "C" fn() -> *mut c_void> = lib
                .get(b"ds_create_detector")
                .map_err(|e| format!("Symbol not found: {}", e))?;

            let detector = fn_create();
            if detector.is_null() {
                let fn_error: Symbol<unsafe extern "C" fn() -> *const c_char> =
                    lib.get(b"ds_last_error").map_err(|e| format!("{}", e))?;
                let err = cstr_to_string(fn_error());
                return Err(format!("ds_create_detector failed: {}", err));
            }

            // Dereference all Symbols to raw fn pointers BEFORE moving lib
            let fn_scan = *lib
                .get::<unsafe extern "C" fn(*mut c_void, *mut c_int) -> *mut DsDisplayInfoC>(
                    b"ds_scan",
                )
                .map_err(|e| format!("{}", e))?;
            let fn_free = *lib
                .get::<unsafe extern "C" fn(*mut DsDisplayInfoC, c_int)>(b"ds_free_displays")
                .map_err(|e| format!("{}", e))?;
            let fn_bri = *lib
                .get::<unsafe extern "C" fn(*mut c_void, c_int, c_int) -> c_int>(
                    b"ds_set_brightness",
                )
                .map_err(|e| format!("{}", e))?;
            let fn_get_bri = *lib
                .get::<unsafe extern "C" fn(*mut c_void, c_int) -> c_int>(b"ds_get_brightness")
                .map_err(|e| format!("{}", e))?;
            let fn_inp = *lib
                .get::<unsafe extern "C" fn(*mut c_void, c_int, c_int) -> c_int>(b"ds_set_input")
                .map_err(|e| format!("{}", e))?;
            let fn_get_inp = *lib
                .get::<unsafe extern "C" fn(*mut c_void, c_int) -> c_int>(b"ds_get_input")
                .map_err(|e| format!("{}", e))?;
            let fn_err = *lib
                .get::<unsafe extern "C" fn() -> *const c_char>(b"ds_last_error")
                .map_err(|e| format!("{}", e))?;
            let fn_destroy = *lib
                .get::<unsafe extern "C" fn(*mut c_void)>(b"ds_destroy_detector")
                .map_err(|e| format!("{}", e))?;

            eprintln!("[NativeCore] Detector created successfully");

            Ok(NativeCore {
                _lib: lib,
                detector,
                fn_scan,
                fn_free_displays: fn_free,
                fn_set_brightness: fn_bri,
                fn_get_brightness: fn_get_bri,
                fn_set_input: fn_inp,
                fn_get_input: fn_get_inp,
                fn_last_error: fn_err,
                fn_destroy,
            })
        }
    }

    fn find_dll() -> Result<PathBuf, String> {
        #[cfg(target_os = "windows")]
        let lib_name = "displayswitch_ffi.dll";
        #[cfg(target_os = "macos")]
        let lib_name = "libdisplayswitch_ffi.dylib";
        #[cfg(target_os = "linux")]
        let lib_name = "libdisplayswitch_ffi.so";

        let candidates = [
            // 1. Same directory as executable (bundled app)
            std::env::current_exe()
                .ok()
                .and_then(|e| e.parent().map(|p| p.join(lib_name))),
            // 2. macOS .app bundle Resources/
            #[cfg(target_os = "macos")]
            std::env::current_exe()
                .ok()
                .and_then(|e| e.parent().and_then(|p| p.parent()).map(|p| p.join("Resources").join(lib_name))),
            #[cfg(not(target_os = "macos"))]
            None,
            // 3. src-tauri/ (dev mode, CMake post-build copy)
            Some(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(lib_name)),
            // 4. core_native/build/Release/
            Some(
                PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                    .parent()
                    .unwrap()
                    .parent()
                    .unwrap()
                    .join("core_native")
                    .join("build")
                    .join("Release")
                    .join(lib_name),
            ),
        ];

        for candidate in candidates.iter().flatten() {
            if candidate.exists() {
                return Ok(candidate.clone());
            }
        }

        Err(format!("{} not found. Build core_native first.", lib_name))
    }

    fn scan(&self) -> Result<Vec<DisplayInfo>, String> {
        unsafe {
            let mut count: c_int = 0;
            let ptr = (self.fn_scan)(self.detector, &mut count);
            if ptr.is_null() {
                return Ok(vec![]);
            }

            let slice = std::slice::from_raw_parts(ptr, count as usize);
            let result: Vec<DisplayInfo> = slice
                .iter()
                .enumerate()
                .map(|(i, c)| convert_display(c, i))
                .collect();

            (self.fn_free_displays)(ptr, count);
            Ok(result)
        }
    }

    fn last_error(&self) -> String {
        unsafe { cstr_to_string((self.fn_last_error)()) }
    }

    fn set_brightness(&self, idx: i32, level: i32) -> Result<(), String> {
        unsafe {
            let rc = (self.fn_set_brightness)(self.detector, idx, level);
            if rc == 0 {
                Ok(())
            } else {
                let detail = self.last_error();
                Err(format!(
                    "DDC/CI set_brightness failed: {}",
                    if detail.is_empty() {
                        "unknown error"
                    } else {
                        &detail
                    }
                ))
            }
        }
    }

    fn set_input(&self, idx: i32, code: i32) -> Result<(), String> {
        unsafe {
            let rc = (self.fn_set_input)(self.detector, idx, code);
            if rc == 0 {
                Ok(())
            } else {
                let detail = self.last_error();
                Err(format!(
                    "DDC/CI set_input failed: {}",
                    if detail.is_empty() {
                        "unknown error"
                    } else {
                        &detail
                    }
                ))
            }
        }
    }

    fn get_input(&self, idx: i32) -> i32 {
        unsafe { (self.fn_get_input)(self.detector, idx) }
    }
}

impl Drop for NativeCore {
    fn drop(&mut self) {
        if !self.detector.is_null() {
            unsafe { (self.fn_destroy)(self.detector) };
            self.detector = std::ptr::null_mut();
        }
    }
}

// ─── Tauri Commands ─────────────────────────────────────────────────────

#[tauri::command]
fn scan_monitors(state: tauri::State<'_, Mutex<Option<NativeCore>>>) -> Result<Vec<DisplayInfo>, String> {
    let guard = state.lock().map_err(|e| format!("Lock: {}", e))?;
    let core = guard.as_ref().ok_or("Native core not available – display library failed to load")?;
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| core.scan())) {
        Ok(result) => result,
        Err(_) => Err("Internal error: scan panicked".to_string()),
    }
}

#[tauri::command]
fn set_brightness(
    state: tauri::State<'_, Mutex<Option<NativeCore>>>,
    display_index: i32,
    level: i32,
) -> Result<(), String> {
    let guard = state.lock().map_err(|e| format!("Lock: {}", e))?;
    let core = guard.as_ref().ok_or("Native core not available")?;
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        core.set_brightness(display_index, level)
    })) {
        Ok(result) => result,
        Err(_) => Err("Internal error: set_brightness panicked".to_string()),
    }
}

#[tauri::command]
fn set_input(
    state: tauri::State<'_, Mutex<Option<NativeCore>>>,
    display_index: i32,
    input_code: i32,
) -> Result<i32, String> {
    // Step 1: send the set_input command (hold lock briefly)
    {
        let guard = state.lock().map_err(|e| format!("Lock: {}", e))?;
        let core = guard.as_ref().ok_or("Native core not available")?;
        core.set_input(display_index, input_code)?;
    } // <-- mutex released here

    // Step 2: wait outside the lock so other commands aren't blocked
    std::thread::sleep(std::time::Duration::from_millis(300));

    // Step 3: re-acquire lock to read back the confirmed input
    let guard = state.lock().map_err(|e| format!("Lock: {}", e))?;
    let core = guard.as_ref().ok_or("Native core not available")?;
    let confirmed = core.get_input(display_index);
    Ok(confirmed)
}

/// Get brightness for a single display without full rescan (fast path).
#[tauri::command]
fn get_brightness(
    state: tauri::State<'_, Mutex<Option<NativeCore>>>,
    display_index: i32,
) -> Result<i32, String> {
    let guard = match state.lock() {
        Ok(c) => c,
        Err(_) => return Ok(-1),
    };
    let core = match guard.as_ref() {
        Some(c) => c,
        None => return Ok(-1),
    };
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| unsafe {
        (core.fn_get_brightness)(core.detector, display_index)
    })) {
        Ok(val) => Ok(val),
        Err(_) => Ok(-1),
    }
}

/// Get current input source for a single display without full rescan.
#[tauri::command]
fn get_input(
    state: tauri::State<'_, Mutex<Option<NativeCore>>>,
    display_index: i32,
) -> Result<i32, String> {
    let guard = match state.lock() {
        Ok(c) => c,
        Err(_) => return Ok(-1),
    };
    let core = match guard.as_ref() {
        Some(c) => c,
        None => return Ok(-1),
    };
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        core.get_input(display_index)
    })) {
        Ok(val) => Ok(val),
        Err(_) => Ok(-1),
    }
}

/// Register global hotkeys from frontend config.
/// `shortcuts` is a list of shortcut strings like "Ctrl+Alt+1", "Ctrl+Alt+Up" etc.
///
/// The Builder's `with_handler` callback (in main) handles the actual key events
/// and emits "hotkey-action". Here we only **register** shortcuts (no per-key handler)
/// so the global handler fires for them.
#[tauri::command]
fn register_hotkeys(
    app: tauri::AppHandle,
    shortcuts: Vec<String>,
) -> Result<(), String> {
    // First unregister all existing shortcuts
    if let Err(e) = app.global_shortcut().unregister_all() {
        eprintln!("[Hotkey] Failed to unregister all: {}", e);
    }

    for shortcut_str in &shortcuts {
        match shortcut_str.parse::<Shortcut>() {
            Ok(shortcut) => {
                // Use `register` — registers the shortcut so the global
                // `with_handler` callback fires for it. No per-shortcut handler
                // needed (avoids double-handler issues).
                if let Err(e) = app.global_shortcut().register(shortcut) {
                    eprintln!("[Hotkey] Failed to register '{}': {}", shortcut_str, e);
                } else {
                    eprintln!("[Hotkey] Registered: {}", shortcut_str);
                }
            }
            Err(e) => {
                eprintln!("[Hotkey] Invalid shortcut '{}': {}", shortcut_str, e);
            }
        }
    }
    Ok(())
}

/// Unregister all global hotkeys.
#[tauri::command]
fn unregister_all_hotkeys(app: tauri::AppHandle) -> Result<(), String> {
    app.global_shortcut()
        .unregister_all()
        .map_err(|e| format!("Failed to unregister hotkeys: {}", e))
}

// ─── Ambient Light Sensor (Windows Sensor API via PowerShell) ───────────

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ALSStatus {
    pub available: bool,
    pub lux: f64,
    pub recommended_brightness: i32,
}

/// Query the Windows ambient light sensor via PowerShell.
/// Uses Windows.Devices.Sensors.LightSensor WinRT API.
#[tauri::command]
fn get_ambient_light() -> ALSStatus {
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        als_read_sensor()
    })) {
        Ok(status) => status,
        Err(_) => ALSStatus {
            available: false,
            lux: -1.0,
            recommended_brightness: -1,
        },
    }
}

#[cfg(target_os = "windows")]
fn als_read_sensor() -> ALSStatus {
    use std::process::Command;

    let ps_script = r#"
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Devices.Sensors.LightSensor, Windows.Devices.Sensors, ContentType=WindowsRuntime]
$sensor = [Windows.Devices.Sensors.LightSensor]::GetDefault()
if ($sensor) {
    $reading = $sensor.GetCurrentReading()
    @{ Available = $true; Lux = $reading.IlluminanceInLux } | ConvertTo-Json -Compress
} else {
    @{ Available = $false; Lux = -1 } | ConvertTo-Json -Compress
}
"#;

    let output = Command::new("powershell")
        .args(["-NoProfile", "-NonInteractive", "-Command", ps_script])
        .creation_flags(0x08000000) // CREATE_NO_WINDOW
        .output();

    match output {
        Ok(out) if out.status.success() => {
            let stdout = String::from_utf8_lossy(&out.stdout);
            if let Ok(val) = serde_json::from_str::<serde_json::Value>(stdout.trim()) {
                let available = val["Available"].as_bool().unwrap_or(false);
                let lux = val["Lux"].as_f64().unwrap_or(-1.0);
                let recommended = if available && lux >= 0.0 {
                    lux_to_brightness(lux)
                } else {
                    -1
                };
                ALSStatus {
                    available,
                    lux,
                    recommended_brightness: recommended,
                }
            } else {
                ALSStatus {
                    available: false,
                    lux: -1.0,
                    recommended_brightness: -1,
                }
            }
        }
        _ => ALSStatus {
            available: false,
            lux: -1.0,
            recommended_brightness: -1,
        },
    }
}

#[cfg(not(target_os = "windows"))]
fn als_read_sensor() -> ALSStatus {
    ALSStatus {
        available: false,
        lux: -1.0,
        recommended_brightness: -1,
    }
}

/// Logarithmic lux → brightness mapping (matches Python version).
fn lux_to_brightness(lux: f64) -> i32 {
    if lux <= 0.0 {
        return 10;
    }
    let log_lux = lux.max(1.0).log10(); // 0 to ~4
    let brightness = 10.0 + (log_lux / 4.0) * 90.0;
    (brightness.round() as i32).max(10).min(100)
}

/// Check for application updates using tauri-plugin-updater.
#[tauri::command]
async fn check_for_update(app: tauri::AppHandle) -> Result<Option<String>, String> {
    match app.updater().map_err(|e| e.to_string())?.check().await {
        Ok(Some(update)) => Ok(Some(update.version.clone())),
        Ok(None) => Ok(None),
        Err(e) => Err(format!("Update check failed: {}", e)),
    }
}

/// Return the current platform for frontend conditional rendering.
#[tauri::command]
fn get_platform() -> String {
    if cfg!(target_os = "windows") {
        "windows".to_string()
    } else if cfg!(target_os = "macos") {
        "macos".to_string()
    } else {
        "linux".to_string()
    }
}

/// Window control commands for custom title bar (Windows).
#[tauri::command]
fn window_minimize(window: tauri::Window) -> Result<(), String> {
    window.minimize().map_err(|e| e.to_string())
}

#[tauri::command]
fn window_toggle_maximize(window: tauri::Window) -> Result<(), String> {
    if window.is_maximized().unwrap_or(false) {
        window.unmaximize().map_err(|e| e.to_string())
    } else {
        window.maximize().map_err(|e| e.to_string())
    }
}

#[tauri::command]
fn window_close(window: tauri::Window) -> Result<(), String> {
    window.hide().map_err(|e| e.to_string())
}

// ─── Hot-plug detection (Windows WM_DISPLAYCHANGE) ──────────────────────

#[cfg(target_os = "windows")]
mod hotplug {
    use std::sync::Arc;
    use tauri::{AppHandle, Emitter};

    /// Spawn a background thread with a **real** hidden window (not message-only)
    /// that listens for `WM_DISPLAYCHANGE` and `WM_DEVICECHANGE`.
    ///
    /// IMPORTANT: Message-only windows (HWND_MESSAGE) do NOT receive broadcast
    /// messages like WM_DISPLAYCHANGE. We must use a real top-level window
    /// with zero size, hidden via ShowWindow(SW_HIDE).
    pub fn start_display_watcher(app_handle: Arc<AppHandle>) {
        std::thread::spawn(move || {
            unsafe { run_message_loop(app_handle) };
        });
    }

    unsafe fn run_message_loop(app_handle: Arc<AppHandle>) {
        use std::ffi::c_void;

        // Win32 types
        #[repr(C)]
        struct WNDCLASSEXW {
            cb_size: u32,
            style: u32,
            lpfn_wnd_proc: unsafe extern "system" fn(isize, u32, usize, isize) -> isize,
            cb_cls_extra: i32,
            cb_wnd_extra: i32,
            h_instance: isize,
            h_icon: isize,
            h_cursor: isize,
            hbr_background: isize,
            lpsz_menu_name: *const u16,
            lpsz_class_name: *const u16,
            h_icon_sm: isize,
        }

        #[repr(C)]
        struct MSG {
            hwnd: isize,
            message: u32,
            w_param: usize,
            l_param: isize,
            time: u32,
            pt_x: i32,
            pt_y: i32,
        }

        #[repr(C)]
        struct GUID {
            data1: u32,
            data2: u16,
            data3: u16,
            data4: [u8; 8],
        }

        #[repr(C)]
        struct DEV_BROADCAST_DEVICEINTERFACE_W {
            dbcc_size: u32,
            dbcc_devicetype: u32,
            dbcc_reserved: u32,
            dbcc_classguid: GUID,
            dbcc_name: [u16; 1], // variable-length
        }

        #[link(name = "user32")]
        extern "system" {
            fn RegisterClassExW(lpwcx: *const WNDCLASSEXW) -> u16;
            fn CreateWindowExW(
                ex_style: u32,
                class_name: *const u16,
                window_name: *const u16,
                style: u32,
                x: i32,
                y: i32,
                w: i32,
                h: i32,
                parent: isize,
                menu: isize,
                instance: isize,
                param: *mut c_void,
            ) -> isize;
            fn ShowWindow(hwnd: isize, cmd: i32) -> i32;
            fn GetMessageW(msg: *mut MSG, hwnd: isize, filter_min: u32, filter_max: u32) -> i32;
            fn TranslateMessage(msg: *const MSG) -> i32;
            fn DispatchMessageW(msg: *const MSG) -> isize;
            fn DefWindowProcW(hwnd: isize, msg: u32, w_param: usize, l_param: isize) -> isize;
            fn SetWindowLongPtrW(hwnd: isize, index: i32, new_long: isize) -> isize;
            fn GetWindowLongPtrW(hwnd: isize, index: i32) -> isize;
            fn GetModuleHandleW(name: *const u16) -> isize;
            fn RegisterDeviceNotificationW(
                recipient: isize,
                filter: *const c_void,
                flags: u32,
            ) -> isize;
        }

        const WM_DISPLAYCHANGE: u32 = 0x007E;
        const WM_DEVICECHANGE: u32 = 0x0219;
        const DBT_DEVNODES_CHANGED: usize = 0x0007;
        const DBT_DEVICEARRIVAL: usize = 0x8000;
        const DBT_DEVICEREMOVECOMPLETE: usize = 0x8004;
        const GWLP_USERDATA: i32 = -21;
        const SW_HIDE: i32 = 0;
        const WS_POPUP: u32 = 0x80000000;
        const DEVICE_NOTIFY_WINDOW_HANDLE: u32 = 0x00000000;
        const DBT_DEVTYP_DEVICEINTERFACE: u32 = 0x00000005;

        // GUID_DEVINTERFACE_MONITOR = {E6F07B5F-EE97-4a90-B076-33F57BF4EAA7}
        let guid_monitor = GUID {
            data1: 0xE6F07B5F,
            data2: 0xEE97,
            data3: 0x4A90,
            data4: [0xB0, 0x76, 0x33, 0xF5, 0x7B, 0xF4, 0xEA, 0xA7],
        };

        // Store the app handle pointer so we can attach it to the window
        let app_ptr = Box::into_raw(Box::new(app_handle));

        unsafe extern "system" fn wnd_proc(
            hwnd: isize,
            msg: u32,
            w_param: usize,
            l_param: isize,
        ) -> isize {
            match msg {
                WM_DISPLAYCHANGE => {
                    eprintln!("[Hotplug] WM_DISPLAYCHANGE received");
                    let ptr = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
                    if ptr != 0 {
                        let app: &Arc<AppHandle> = &*(ptr as *const Arc<AppHandle>);
                        let _ = app.emit("displays-changed", "displaychange");
                    }
                    0
                }
                WM_DEVICECHANGE => {
                    // Only react to relevant sub-events
                    match w_param {
                        DBT_DEVNODES_CHANGED | DBT_DEVICEARRIVAL | DBT_DEVICEREMOVECOMPLETE => {
                            eprintln!("[Hotplug] WM_DEVICECHANGE (0x{:04X}) received", w_param);
                            let ptr = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
                            if ptr != 0 {
                                let app: &Arc<AppHandle> = &*(ptr as *const Arc<AppHandle>);
                                let _ = app.emit("displays-changed", "devicechange");
                            }
                        }
                        _ => {}
                    }
                    DefWindowProcW(hwnd, msg, w_param, l_param)
                }
                _ => DefWindowProcW(hwnd, msg, w_param, l_param),
            }
        }

        let class_name: Vec<u16> = "DSHotplugWatcher\0".encode_utf16().collect();
        let h_instance = GetModuleHandleW(std::ptr::null());

        let wc = WNDCLASSEXW {
            cb_size: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: 0,
            lpfn_wnd_proc: wnd_proc,
            cb_cls_extra: 0,
            cb_wnd_extra: 0,
            h_instance,
            h_icon: 0,
            h_cursor: 0,
            hbr_background: 0,
            lpsz_menu_name: std::ptr::null(),
            lpsz_class_name: class_name.as_ptr(),
            h_icon_sm: 0,
        };

        RegisterClassExW(&wc);

        // Create a real hidden window (NOT message-only!) — broadcast messages
        // like WM_DISPLAYCHANGE are NOT delivered to HWND_MESSAGE windows.
        let hwnd = CreateWindowExW(
            0,
            class_name.as_ptr(),
            class_name.as_ptr(),
            WS_POPUP, // minimal popup window
            0,
            0,
            0,
            0, // zero size
            0,
            0,
            h_instance,
            std::ptr::null_mut(), // parent=0, NOT HWND_MESSAGE
        );

        if hwnd == 0 {
            eprintln!("[Hotplug] Failed to create hidden window");
            return;
        }

        // Hide it (invisible, no taskbar entry)
        ShowWindow(hwnd, SW_HIDE);

        // Attach app handle to window user data
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, app_ptr as isize);

        // Register for monitor device interface notifications
        let mut filter = DEV_BROADCAST_DEVICEINTERFACE_W {
            dbcc_size: std::mem::size_of::<DEV_BROADCAST_DEVICEINTERFACE_W>() as u32,
            dbcc_devicetype: DBT_DEVTYP_DEVICEINTERFACE,
            dbcc_reserved: 0,
            dbcc_classguid: guid_monitor,
            dbcc_name: [0],
        };

        let notify_handle = RegisterDeviceNotificationW(
            hwnd,
            &mut filter as *mut _ as *mut c_void,
            DEVICE_NOTIFY_WINDOW_HANDLE,
        );

        if notify_handle == 0 {
            eprintln!(
                "[Hotplug] RegisterDeviceNotification failed, falling back to broadcast only"
            );
        } else {
            eprintln!("[Hotplug] RegisterDeviceNotification OK (monitor interface)");
        }

        eprintln!("[Hotplug] Display change watcher started (hidden window)");

        // Message loop — runs forever
        let mut msg: MSG = std::mem::zeroed();
        while GetMessageW(&mut msg, 0, 0, 0) > 0 {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Cleanup (unreachable in practice)
        let _ = Box::from_raw(app_ptr);
    }
}

// ─── main ───────────────────────────────────────────────────────────────

fn main() {
    let core = match NativeCore::new() {
        Ok(c) => {
            eprintln!("[NativeCore] Loaded successfully");
            Some(c)
        }
        Err(e) => {
            eprintln!("[NativeCore] Failed to load: {}. Running in UI-only mode.", e);
            None
        }
    };

    tauri::Builder::default()
        .plugin(tauri_plugin_store::Builder::new().build())
        .plugin(
            tauri_plugin_autostart::Builder::new()
                .args(["--autostarted"])
                .build(),
        )
        .plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_handler(|app, shortcut, event| {
                    if event.state() == ShortcutState::Pressed {
                        let shortcut_str = shortcut.to_string();
                        eprintln!("[Hotkey] Pressed: {}", shortcut_str);
                        let _ = app.emit("hotkey-action", shortcut_str);
                    }
                })
                .build(),
        )
        .plugin(tauri_plugin_shell::init())
        .manage(Mutex::new(core))
        .plugin(tauri_plugin_updater::Builder::new().build())
        .invoke_handler(tauri::generate_handler![
            scan_monitors,
            set_brightness,
            set_input,
            get_brightness,
            get_input,
            register_hotkeys,
            unregister_all_hotkeys,
            get_ambient_light,
            check_for_update,
            get_platform,
            window_minimize,
            window_toggle_maximize,
            window_close,
        ])
        .setup(|app| {
            // ── macOS: native decorations + overlay title bar + app menu ─
            #[cfg(target_os = "macos")]
            {
                use tauri::WebviewWindowBuilder;

                // Re-enable decorations on macOS (disabled in tauri.conf.json for Windows)
                if let Some(window) = app.get_webview_window("main") {
                    let _ = window.set_decorations(true);
                }

                // macOS native menu bar
                let app_menu = Submenu::with_items(
                    app,
                    "DisplaySwitch",
                    true,
                    &[
                        &MenuItem::with_id(app, "about", "About DisplaySwitch", true, None::<&str>)?,
                        &MenuItem::with_id(app, "check_update", "Check for Updates…", true, None::<&str>)?,
                        &MenuItem::with_id(app, "quit_menu", "Quit DisplaySwitch", true, Some("CmdOrCtrl+Q"))?,
                    ],
                )?;

                let view_menu = Submenu::with_items(
                    app,
                    "View",
                    true,
                    &[
                        &MenuItem::with_id(app, "refresh_menu", "Refresh Displays", true, Some("CmdOrCtrl+R"))?,
                        &MenuItem::with_id(app, "settings_menu", "Settings…", true, Some("CmdOrCtrl+,"))?,
                    ],
                )?;

                let window_menu = Submenu::with_items(
                    app,
                    "Window",
                    true,
                    &[
                        &MenuItem::with_id(app, "minimize_menu", "Minimize", true, Some("CmdOrCtrl+M"))?,
                        &MenuItem::with_id(app, "close_menu", "Close", true, Some("CmdOrCtrl+W"))?,
                    ],
                )?;

                let menu_bar = Menu::with_items(app, &[&app_menu, &view_menu, &window_menu])?;
                app.set_menu(menu_bar)?;

                app.on_menu_event(move |app, event| {
                    match event.id().as_ref() {
                        "quit_menu" => app.exit(0),
                        "refresh_menu" => { let _ = app.emit("menu-action", "refresh"); },
                        "settings_menu" => { let _ = app.emit("menu-action", "settings"); },
                        "check_update" => { let _ = app.emit("menu-action", "check_update"); },
                        "minimize_menu" => {
                            if let Some(w) = app.get_webview_window("main") { let _ = w.minimize(); }
                        },
                        "close_menu" => {
                            if let Some(w) = app.get_webview_window("main") { let _ = w.hide(); }
                        },
                        _ => {}
                    }
                });
            }

            // ── System tray ─────────────────────────────────────────
            let show_item = MenuItem::with_id(app, "show", "Show Window", true, None::<&str>)?;
            let quit_item = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show_item, &quit_item])?;

            let icon = Image::from_bytes(include_bytes!("../icons/32x32.png"))
                .expect("Failed to load tray icon");

            let _tray = TrayIconBuilder::new()
                .icon(icon)
                .menu(&menu)
                .tooltip("DisplaySwitch")
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => {
                        if let Some(w) = app.get_webview_window("main") {
                            let _ = w.show();
                            let _ = w.unminimize();
                            let _ = w.set_focus();
                        }
                    }
                    "quit" => {
                        app.exit(0);
                    }
                    _ => {}
                })
                .on_tray_icon_event(|tray, event| {
                    if let TrayIconEvent::Click {
                        button: MouseButton::Left,
                        button_state: MouseButtonState::Up,
                        ..
                    } = event
                    {
                        let app = tray.app_handle();
                        if let Some(w) = app.get_webview_window("main") {
                            let _ = w.show();
                            let _ = w.unminimize();
                            let _ = w.set_focus();
                        }
                    }
                })
                .build(app)?;

            // ── Hot-plug detection ──────────────────────────────────
            #[cfg(target_os = "windows")]
            {
                let app_handle = Arc::new(app.handle().clone());
                hotplug::start_display_watcher(app_handle);
            }

            Ok(())
        })
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
