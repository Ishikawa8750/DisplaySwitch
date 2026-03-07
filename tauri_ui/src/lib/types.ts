/** Display information from the Rust/C++ backend */
export interface GPUInfo {
  name: string;
  vendor_name: string;
  dedicated_vram_bytes: number;
  driver_version: string;
  formatted_name: string;
}

export interface BandwidthInfo {
  max_bandwidth_gbps: number;
  bandwidth_str: string;
  can_support_4k60: boolean;
  can_support_4k120: boolean;
  can_support_8k60: boolean;
}

export interface DisplayInfo {
  name: string;
  device_path: string;
  manufacturer_id: string;
  product_code: string;
  is_internal: boolean;

  gpu: GPUInfo;

  connection_type: string;
  refresh_rate: number;
  hdmi_version: string | null;
  hdmi_frl_rate: string | null;
  max_tmds_clock_mhz: number;

  supports_hdr: boolean;
  hdr_formats: string[];

  screen_width_mm: number;
  screen_height_mm: number;

  resolution_width: number;
  resolution_height: number;
  resolution_str: string;
  bits_per_pixel: number;

  bandwidth: BandwidthInfo;

  current_input: number;
  supported_inputs: number[];
  brightness: number;
  display_index: number;
}

// ─── Phase 6: Config / Preset / Hotkey types ────────────────────────────

/** A preset stores a named combo of display settings */
export interface Preset {
  id: string;
  name: string;
  /** Display index → settings to apply */
  actions: PresetAction[];
}

export interface PresetAction {
  display_index: number;
  display_name: string;
  brightness?: number;     // 0-100, undefined = don't change
  input_code?: number;     // VCP 0x60 input code, undefined = don't change
}

/** Hotkey binding: maps a keyboard shortcut to an action */
export interface HotkeyBinding {
  id: string;
  shortcut: string;        // e.g. "Ctrl+Alt+1", "Ctrl+Alt+Up"
  action: HotkeyActionType;
  value?: number;           // display index or preset index
  label: string;            // human-readable description
}

export type HotkeyActionType =
  | "brightness_up"
  | "brightness_down"
  | "switch_input"
  | "apply_preset"
  | "apply_profile"
  | "refresh";

// ─── Phase 7: Profile / Theme / i18n types ──────────────────────────────

import type { ThemeMode } from "./theme";
import type { LocaleMode } from "./i18n";

/** Scene profile — groups presets + hotkey overrides for a scenario */
export interface Profile {
  id: string;
  name: string;
  description?: string;
  icon?: string;
  category: "work" | "gaming" | "presentation" | "movie" | "custom";
  /** Per-display settings to apply when activated */
  actions: PresetAction[];
  /** Optional: hotkey overrides for this profile */
  hotkeys?: HotkeyBinding[];
  /** Optional: auto-switch trigger */
  trigger?: ProfileTrigger;
}

/** Trigger for automatic profile switching */
export type ProfileTrigger =
  | { type: "display_count"; count: number }
  | { type: "schedule"; start_hour: number; end_hour: number };

/** Full app config stored via tauri-plugin-store */
export interface AppConfig {
  autostart: boolean;
  sync_interval_ms: number;      // brightness sync interval (default 5000)
  theme: ThemeMode;               // "dark" | "light" | "system"
  locale: LocaleMode;             // "en" | "zh" | "auto"
  hotkeys: HotkeyBinding[];
  presets: Preset[];
  profiles: Profile[];
  active_profile_id: string | null;
  // Phase 7b: Ambient Light Sensor
  als_enabled: boolean;           // auto-brightness via ALS
  als_interval_ms: number;        // ALS polling interval (default 10000)
}
