/**
 * Config Store — Phase 6 + Phase 7
 *
 * Persistent configuration using tauri-plugin-store.
 * Stores: autostart, hotkey bindings, presets, profiles, theme, locale.
 */

import type { AppConfig, HotkeyBinding, Preset, Profile } from "./types";

const STORE_FILE = "config.json";

/** Default config */
export const DEFAULT_CONFIG: AppConfig = {
  autostart: false,
  sync_interval_ms: 5000,
  theme: "dark",
  locale: "auto",
  hotkeys: [
    {
      id: "bri_up",
      shortcut: "Ctrl+Alt+Up",
      action: "brightness_up",
      value: 10,
      label: "Brightness +10%",
    },
    {
      id: "bri_down",
      shortcut: "Ctrl+Alt+Down",
      action: "brightness_down",
      value: 10,
      label: "Brightness -10%",
    },
    {
      id: "refresh",
      shortcut: "Ctrl+Alt+F5",
      action: "refresh",
      label: "Refresh displays",
    },
  ],
  presets: [],
  profiles: [],
  active_profile_id: null,
  als_enabled: false,
  als_interval_ms: 10000,
};

let storeInstance: any = null;

async function getStore() {
  if (storeInstance) return storeInstance;
  const { load } = await import("@tauri-apps/plugin-store");
  storeInstance = await load(STORE_FILE, { autoSave: true });
  return storeInstance;
}

/** Load full config, merging defaults for missing keys */
export async function loadConfig(): Promise<AppConfig> {
  try {
    const store = await getStore();
    const saved = await store.get<AppConfig>("config");
    if (!saved) return { ...DEFAULT_CONFIG };
    // Merge with defaults (in case new fields added in updates)
    return {
      ...DEFAULT_CONFIG,
      ...saved,
      hotkeys: saved.hotkeys ?? DEFAULT_CONFIG.hotkeys,
      presets: saved.presets ?? DEFAULT_CONFIG.presets,
      profiles: saved.profiles ?? DEFAULT_CONFIG.profiles,
      theme: saved.theme ?? DEFAULT_CONFIG.theme,
      locale: saved.locale ?? DEFAULT_CONFIG.locale,
      active_profile_id: saved.active_profile_id ?? null,
      als_enabled: saved.als_enabled ?? DEFAULT_CONFIG.als_enabled,
      als_interval_ms: saved.als_interval_ms ?? DEFAULT_CONFIG.als_interval_ms,
    };
  } catch (e) {
    console.error("Failed to load config:", e);
    return { ...DEFAULT_CONFIG };
  }
}

/** Save full config */
export async function saveConfig(config: AppConfig): Promise<void> {
  try {
    const store = await getStore();
    await store.set("config", config);
    await store.save();
  } catch (e) {
    console.error("Failed to save config:", e);
  }
}

/** Update a single field */
export async function updateConfig(
  partial: Partial<AppConfig>,
): Promise<AppConfig> {
  const config = await loadConfig();
  const updated = { ...config, ...partial };
  await saveConfig(updated);
  return updated;
}

/** Save presets */
export async function savePresets(presets: Preset[]): Promise<void> {
  await updateConfig({ presets });
}

/** Save hotkey bindings */
export async function saveHotkeys(hotkeys: HotkeyBinding[]): Promise<void> {
  await updateConfig({ hotkeys });
}

/** Save profiles */
export async function saveProfiles(profiles: Profile[]): Promise<void> {
  await updateConfig({ profiles });
}

/** Save active profile id */
export async function saveActiveProfile(id: string | null): Promise<void> {
  await updateConfig({ active_profile_id: id });
}
