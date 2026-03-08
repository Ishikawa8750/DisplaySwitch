<script lang="ts">
  import { untrack } from "svelte";
  import MonitorCard from "./lib/MonitorCard.svelte";
  import SettingsPanel from "./lib/SettingsPanel.svelte";
  import ToastContainer from "./lib/ToastContainer.svelte";
  import ProfileBar from "./lib/ProfileBar.svelte";
  import StatusBar from "./lib/StatusBar.svelte";
  import TopologyView from "./lib/TopologyView.svelte";
  import TitleBar from "./lib/TitleBar.svelte";
  import type { DisplayInfo, AppConfig, Profile, PresetAction } from "./lib/types";
  import { loadConfig, saveConfig, saveActiveProfile, DEFAULT_CONFIG } from "./lib/configStore";
  import { applyTheme, watchSystemTheme } from "./lib/theme";
  import { toast } from "./lib/toast.svelte";
  import { t, setLocale, resolveLocale, onLocaleChange } from "./lib/i18n";
  import { getAmbientLight, startAutoBrightness, luxToBrightness } from "./lib/ambientLight";

  let displays: DisplayInfo[] = $state([]);
  let loading = $state(true);
  let error = $state("");
  let showSettings = $state(false);
  let appConfig = $state<AppConfig>(structuredClone(DEFAULT_CONFIG));

  // Platform detection for conditional title bar rendering
  let platform = $state<"windows" | "macos" | "linux">("windows");
  let isMaximized = $state(false);

  // Phase 7b: StatusBar / ALS / Topology state
  let ambientLux = $state(-1);
  let ambientAvailable = $state(false);
  let lastSyncTime = $state<Date | null>(null);
  let showTopology = $state(false);
  let _alsCleanup: (() => void) | null = null;

  // Sidebar active view
  let activeView = $state<"monitors" | "topology" | "profiles">("monitors");

  // Derived: active profile for StatusBar
  let activeProfile = $derived(
    appConfig.profiles.find((p) => p.id === appConfig.active_profile_id) ?? null
  );

  // i18n reactivity: force re-render when locale changes
  let _localeVersion = $state(0);

  /** Get the display ID for custom input name storage */
  function displayId(d: DisplayInfo): string {
    return (d.manufacturer_id || "") + "_" + (d.product_code || "");
  }

  /** Handle custom input name change from MonitorCard */
  async function handleInputNameChange(display: DisplayInfo, vcpCode: number, newName: string | null) {
    const id = displayId(display);
    if (!id || id === "_") return;

    const names = { ...(appConfig.custom_input_names[id] ?? {}) };
    if (newName === null) {
      delete names[String(vcpCode)];
    } else {
      names[String(vcpCode)] = newName;
    }

    const custom_input_names = { ...appConfig.custom_input_names };
    if (Object.keys(names).length === 0) {
      delete custom_input_names[id];
    } else {
      custom_input_names[id] = names;
    }

    appConfig = { ...appConfig, custom_input_names };
    await saveConfig(appConfig);
  }

  /** Check if running inside Tauri WebView */
  function isTauri(): boolean {
    return !!(window as any).__TAURI_INTERNALS__;
  }

  async function refresh() {
    loading = true;
    error = "";
    try {
      if (!isTauri()) {
        error = t("status.not_tauri");
        loading = false;
        return;
      }
      const { invoke } = await import("@tauri-apps/api/core");
      displays = await invoke<DisplayInfo[]>("scan_monitors");
      toast.success(t("toast.scan_complete", displays.length));
    } catch (e: any) {
      error = e?.message ?? String(e);
      toast.error(t("toast.error", error));
    }
    loading = false;
  }

  /** Lightweight brightness + input sync — reads values without full rescan */
  async function syncBrightness() {
    if (!isTauri() || displays.length === 0) return;
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      for (let i = 0; i < displays.length; i++) {
        const bri = await invoke<number>("get_brightness", { displayIndex: i });
        if (bri >= 0 && displays[i]) {
          displays[i] = { ...displays[i], brightness: bri };
        }
        if (!displays[i]?.is_internal) {
          const inp = await invoke<number>("get_input", { displayIndex: i });
          if (inp >= 0 && displays[i]) {
            displays[i] = { ...displays[i], current_input: inp };
          }
        }
      }
      lastSyncTime = new Date();
    } catch {
      // Silently ignore sync errors
    }
  }

  /** Register global hotkeys with the Rust backend */
  async function registerHotkeys(hotkeys: AppConfig["hotkeys"]) {
    if (!isTauri()) return;
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const shortcuts = hotkeys
        .filter((h) => h.shortcut && h.shortcut.length > 0)
        .map((h) => h.shortcut);
      await invoke("register_hotkeys", { shortcuts });
    } catch (e) {
      console.error("Failed to register hotkeys:", e);
    }
  }

  /** Apply actions (brightness + input) to displays */
  async function applyActions(actions: PresetAction[]) {
    if (!isTauri()) return;
    const { invoke } = await import("@tauri-apps/api/core");
    for (const act of actions) {
      try {
        if (act.brightness !== undefined && act.brightness >= 0) {
          await invoke("set_brightness", {
            displayIndex: act.display_index,
            level: act.brightness,
          });
          if (displays[act.display_index]) {
            displays[act.display_index] = {
              ...displays[act.display_index],
              brightness: act.brightness,
            };
          }
        }
        if (act.input_code !== undefined && act.input_code > 0) {
          const confirmed = await invoke<number>("set_input", {
            displayIndex: act.display_index,
            inputCode: act.input_code,
          });
          if (confirmed >= 0 && displays[act.display_index]) {
            displays[act.display_index] = {
              ...displays[act.display_index],
              current_input: confirmed,
            };
          }
        }
      } catch (e) {
        console.error(`Action failed for display ${act.display_index}:`, e);
      }
    }
  }

  /** Apply a preset — set brightness and input for each display */
  async function applyPreset(preset: AppConfig["presets"][number]) {
    await applyActions(preset.actions);
    toast.success(t("toast.preset_applied", preset.name));
  }

  /** Activate a scene profile */
  async function activateProfile(profile: Profile) {
    await applyActions(profile.actions);
    appConfig.active_profile_id = profile.id;
    await saveActiveProfile(profile.id);
    toast.info(t("toast.profile_activated", profile.name));
  }

  /** Handle hotkey action from the Rust backend */
  async function handleHotkeyAction(shortcutStr: string) {
    const cfg = untrack(() => appConfig);
    const binding = cfg.hotkeys.find((h) => h.shortcut === shortcutStr);
    if (!binding) return;

    switch (binding.action) {
      case "brightness_up":
      case "brightness_down": {
        const step = binding.value ?? 10;
        const { invoke } = await import("@tauri-apps/api/core");
        for (let i = 0; i < displays.length; i++) {
          if (displays[i].brightness >= 0) {
            const dir = binding.action === "brightness_up" ? 1 : -1;
            const newVal = Math.max(0, Math.min(100, displays[i].brightness + step * dir));
            try {
              await invoke("set_brightness", { displayIndex: i, level: newVal });
              displays[i] = { ...displays[i], brightness: newVal };
            } catch { /* ignore */ }
          }
        }
        toast.info(t("toast.brightness_changed", displays[0]?.brightness ?? 0));
        break;
      }
      case "refresh":
        refresh();
        break;
      case "apply_preset": {
        const presetIdx = binding.value ?? 0;
        const preset = cfg.presets[presetIdx];
        if (preset) await applyPreset(preset);
        break;
      }
      case "apply_profile": {
        const profileId = String(binding.value ?? "");
        const profile = cfg.profiles.find((p) => p.id === profileId);
        if (profile) await activateProfile(profile);
        break;
      }
      default:
        break;
    }
  }

  /** Handle config save from settings panel */
  async function onConfigSave(newConfig: AppConfig) {
    appConfig = newConfig;
    await saveConfig(newConfig);
    await registerHotkeys(newConfig.hotkeys);

    // Apply theme + locale changes from settings
    applyTheme(newConfig.theme);
    setLocale(resolveLocale(newConfig.locale));

    // Restart ALS if toggle changed
    restartALS(newConfig);

    toast.success(t("toast.config_saved"));
  }

  /** Start or stop ALS auto-brightness based on config */
  function restartALS(cfg: AppConfig) {
    if (_alsCleanup) { _alsCleanup(); _alsCleanup = null; }
    if (!cfg.als_enabled || !isTauri()) return;

    _alsCleanup = startAutoBrightness(cfg.als_interval_ms, async (targetBri) => {
      const als = await getAmbientLight();
      ambientLux = als.lux;
      ambientAvailable = als.available;

      if (!als.available) return;

      const { invoke } = await import("@tauri-apps/api/core");
      for (let i = 0; i < displays.length; i++) {
        if (displays[i].brightness >= 0) {
          try {
            await invoke("set_brightness", { displayIndex: i, level: targetBri });
            displays[i] = { ...displays[i], brightness: targetBri };
          } catch { /* ignore */ }
        }
      }
    });
  }

  /** Check scene triggers and auto-switch profile */
  function checkSceneTriggers() {
    const cfg = untrack(() => appConfig);
    const currentHour = new Date().getHours();
    const displayCount = displays.length;

    for (const profile of cfg.profiles) {
      if (!profile.trigger || profile.id === cfg.active_profile_id) continue;

      if (profile.trigger.type === "display_count" && displayCount === profile.trigger.count) {
        activateProfile(profile);
        return;
      }
      if (profile.trigger.type === "schedule") {
        const { start_hour, end_hour } = profile.trigger;
        const inRange = start_hour <= end_hour
          ? currentHour >= start_hour && currentHour < end_hour
          : currentHour >= start_hour || currentHour < end_hour;
        if (inRange) {
          activateProfile(profile);
          return;
        }
      }
    }
  }

  // ── One-time initialisation (NO reactive dependencies) ─────────────
  let _syncTimer: ReturnType<typeof setInterval> | null = null;
  let _unlistenHotplug: (() => void) | null = null;
  let _unlistenHotkey: (() => void) | null = null;
  let _unlistenMenuAction: (() => void) | null = null;
  let _unlistenSystemTheme: (() => void) | null = null;
  let _unlistenLocale: (() => void) | null = null;
  let _debounceTimer: ReturnType<typeof setTimeout> | null = null;
  let _initialised = false;

  $effect(() => {
    if (_initialised) return;
    _initialised = true;

    untrack(() => {
      _unlistenLocale = onLocaleChange(() => {
        _localeVersion++;
      });

      if (isTauri()) {
        // Detect platform for conditional title bar rendering
        import("@tauri-apps/api/core").then(({ invoke }) => {
          invoke<string>("get_platform").then((p) => {
            platform = p as typeof platform;
          });
        });

        // Track maximize state for Windows title bar icon
        import("@tauri-apps/api/window").then(({ getCurrentWindow }) => {
          const win = getCurrentWindow();
          win.isMaximized().then((m) => { isMaximized = m; });
          win.onResized(() => {
            win.isMaximized().then((m) => { isMaximized = m; });
          });
        });

        loadConfig().then((cfg) => {
          appConfig = cfg;
          registerHotkeys(cfg.hotkeys);
          applyTheme(cfg.theme);
          _unlistenSystemTheme = watchSystemTheme(() => {
            const currentCfg = untrack(() => appConfig);
            if (currentCfg.theme === "system") {
              applyTheme("system");
            }
          });
          setLocale(resolveLocale(cfg.locale));
          restartALS(cfg);
          getAmbientLight().then((als) => {
            ambientLux = als.lux;
            ambientAvailable = als.available;
          });
        });
      } else {
        applyTheme(DEFAULT_CONFIG.theme);
        setLocale(resolveLocale(DEFAULT_CONFIG.locale));
      }

      refresh();
      _syncTimer = setInterval(syncBrightness, DEFAULT_CONFIG.sync_interval_ms);

      if (isTauri()) {
        import("@tauri-apps/api/event").then(({ listen }) => {
          listen<void>("displays-changed", () => {
            if (_debounceTimer) clearTimeout(_debounceTimer);
            _debounceTimer = setTimeout(() => {
              refresh().then(() => checkSceneTriggers());
            }, 1500);
          }).then((fn) => {
            _unlistenHotplug = fn;
          });

          listen<string>("hotkey-action", (event) => {
            handleHotkeyAction(event.payload);
          }).then((fn) => {
            _unlistenHotkey = fn;
          });

          // macOS native menu actions
          listen<string>("menu-action", (event) => {
            switch (event.payload) {
              case "refresh": refresh(); break;
              case "settings": showSettings = !showSettings; break;
            }
          }).then((fn) => {
            _unlistenMenuAction = fn;
          });
        });
      }
    });

    return () => {
      if (_syncTimer) clearInterval(_syncTimer);
      if (_debounceTimer) clearTimeout(_debounceTimer);
      if (_unlistenHotplug) _unlistenHotplug();
      if (_unlistenHotkey) _unlistenHotkey();
      if (_unlistenMenuAction) _unlistenMenuAction();
      if (_unlistenSystemTheme) _unlistenSystemTheme();
      if (_unlistenLocale) _unlistenLocale();
      if (_alsCleanup) _alsCleanup();
    };
  });
</script>

<!-- Hidden i18n trigger -->
<span style="display:none">{_localeVersion}</span>

<!-- Windows custom title bar -->
{#if platform === "windows"}
  <TitleBar {isMaximized} />
{/if}

<div class="app-shell" class:has-titlebar={platform === "windows"}>
  <!-- ═══ Activity Bar (left icon strip, like VS Code) ═══ -->
  <nav class="activity-bar">
    <div class="activity-top">
      <button
        class="activity-btn"
        class:active={activeView === "monitors"}
        onclick={() => (activeView = "monitors")}
        title={t("app.title")}
      >
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7">
          <rect x="2" y="3" width="20" height="14" rx="2" />
          <line x1="8" y1="21" x2="16" y2="21" />
          <line x1="12" y1="17" x2="12" y2="21" />
        </svg>
      </button>
      <button
        class="activity-btn"
        class:active={activeView === "topology"}
        onclick={() => (activeView = "topology")}
        title={t("topology.title")}
      >
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7">
          <circle cx="5" cy="12" r="2.5" />
          <circle cx="19" cy="6" r="2.5" />
          <circle cx="19" cy="18" r="2.5" />
          <line x1="7.5" y1="12" x2="16.5" y2="6" />
          <line x1="7.5" y1="12" x2="16.5" y2="18" />
        </svg>
      </button>
      <button
        class="activity-btn"
        class:active={activeView === "profiles"}
        onclick={() => (activeView = "profiles")}
        title={t("profile.title")}
      >
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7">
          <rect x="3" y="3" width="7" height="7" rx="1.5" />
          <rect x="14" y="3" width="7" height="7" rx="1.5" />
          <rect x="3" y="14" width="7" height="7" rx="1.5" />
          <rect x="14" y="14" width="7" height="7" rx="1.5" />
        </svg>
      </button>
    </div>
    <div class="activity-bottom">
      <button
        class="activity-btn"
        onclick={refresh}
        disabled={loading}
        title={loading ? t("btn.scanning") : t("btn.refresh")}
      >
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"
          class:spinning={loading}>
          <path d="M1 4v6h6" />
          <path d="M3.51 15a9 9 0 1 0 2.13-9.36L1 10" />
        </svg>
      </button>
      <button
        class="activity-btn"
        class:active={showSettings}
        onclick={() => (showSettings = !showSettings)}
        title={t("btn.settings")}
      >
        <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7">
          <circle cx="12" cy="12" r="3" />
          <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z" />
        </svg>
      </button>
    </div>
  </nav>

  <!-- ═══ Main Content ═══ -->
  <div class="main-area">
    <!-- Panel Header -->
    <div class="panel-header">
      <span class="panel-title">
        {#if activeView === "monitors"}
          {t("app.title")}
        {:else if activeView === "topology"}
          {t("topology.title")}
        {:else}
          {t("profile.title")}
        {/if}
      </span>
      {#if activeView === "monitors" && appConfig.presets.length > 0}
        <div class="header-presets">
          {#each appConfig.presets as preset}
            <button class="preset-chip" onclick={() => applyPreset(preset)}>
              ⚡ {preset.name}
            </button>
          {/each}
        </div>
      {/if}
    </div>

    <!-- Content Area -->
    <div class="content-scroll">
      {#if activeView === "monitors"}
        {#if error}
          <div class="error-banner">{error}</div>
        {/if}

        <div class="cards-list">
          {#each displays as display, i (display.device_path)}
            <MonitorCard
              {display}
              customInputNames={appConfig.custom_input_names[displayId(display)] ?? undefined}
              onchange={(updates) => {
                displays[i] = { ...displays[i], ...updates };
              }}
              onInputNameChange={(code, name) => handleInputNameChange(display, code, name)}
            />
          {/each}
        </div>

        {#if !loading && displays.length === 0 && !error}
          <div class="empty-state">
            <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1" opacity="0.3">
              <rect x="2" y="3" width="20" height="14" rx="2" />
              <line x1="8" y1="21" x2="16" y2="21" />
              <line x1="12" y1="17" x2="12" y2="21" />
            </svg>
            <p>{t("status.no_displays")}</p>
          </div>
        {/if}

      {:else if activeView === "topology"}
        {#if displays.length > 0}
          <TopologyView {displays} />
        {:else}
          <div class="empty-state">
            <p>{t("status.no_displays")}</p>
          </div>
        {/if}

      {:else if activeView === "profiles"}
        <div class="profiles-view">
          {#if appConfig.profiles.length > 0}
            <ProfileBar
              profiles={appConfig.profiles}
              activeProfileId={appConfig.active_profile_id}
              onactivate={activateProfile}
            />
          {:else}
            <div class="empty-state">
              <p>{t("profile.add")}</p>
            </div>
          {/if}
        </div>
      {/if}
    </div>
  </div>
</div>

{#if showSettings}
  <SettingsPanel
    config={appConfig}
    {displays}
    onclose={() => (showSettings = false)}
    onsave={onConfigSave}
  />
{/if}

<ToastContainer />

<StatusBar
  {displays}
  {activeProfile}
  {ambientLux}
  {ambientAvailable}
  {lastSyncTime}
  {loading}
/>

<style>
  /* ═══ Global Design Tokens ═══ */
  :global(body) {
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", Roboto, "Helvetica Neue", sans-serif;
    font-size: 13px;
    line-height: 1.4;
    background: #13131a;
    color: #a9b1d6;
    transition: background 0.2s, color 0.2s;
    overflow: hidden;
  }

  :global(html.light body) {
    background: #f0f0f3;
    color: #343b58;
  }

  /* ═══ Trigger scrollbar — thin, only on hover ═══ */
  :global(*) {
    scrollbar-width: thin;
    scrollbar-color: transparent transparent;
  }
  :global(*:hover) {
    scrollbar-color: rgba(255,255,255,0.12) transparent;
  }
  :global(html.light *:hover) {
    scrollbar-color: rgba(0,0,0,0.15) transparent;
  }
  :global(::-webkit-scrollbar) {
    width: 8px;
    height: 8px;
  }
  :global(::-webkit-scrollbar-track) {
    background: transparent;
  }
  :global(::-webkit-scrollbar-thumb) {
    background: transparent;
    border-radius: 4px;
    transition: background 0.2s;
  }
  :global(*:hover::-webkit-scrollbar-thumb) {
    background: rgba(255,255,255,0.12);
  }
  :global(html.light *:hover::-webkit-scrollbar-thumb) {
    background: rgba(0,0,0,0.15);
  }

  /* ═══ App Shell — full-viewport flex ═══ */
  .app-shell {
    display: flex;
    height: 100vh;
    width: 100vw;
  }

  .app-shell.has-titlebar {
    height: calc(100vh - 31px); /* 30px titlebar + 1px border */
  }

  /* ═══ Activity Bar (left sidebar) ═══ */
  .activity-bar {
    width: 52px;
    min-width: 52px;
    background: rgba(255, 255, 255, 0.03);
    border-right: 1px solid rgba(255, 255, 255, 0.06);
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 8px 0;
    z-index: 100;
  }

  :global(html.light) .activity-bar {
    background: rgba(0, 0, 0, 0.03);
    border-right-color: rgba(0, 0, 0, 0.06);
  }

  .activity-top {
    display: flex;
    flex-direction: column;
    gap: 2px;
    flex: 1;
  }

  .activity-bottom {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding-bottom: 24px; /* space for statusbar */
  }

  .activity-btn {
    width: 40px;
    height: 40px;
    display: flex;
    align-items: center;
    justify-content: center;
    border: none;
    border-radius: 10px;
    background: transparent;
    color: rgba(169, 177, 214, 0.4);
    cursor: pointer;
    position: relative;
    transition: color 0.15s, background 0.15s;
    padding: 0;
    font-size: 0;
    margin: 1px 0;
  }

  .activity-btn:hover {
    color: rgba(169, 177, 214, 0.8);
    background: rgba(255, 255, 255, 0.05);
  }

  :global(html.light) .activity-btn {
    color: rgba(52, 59, 88, 0.4);
  }
  :global(html.light) .activity-btn:hover {
    color: rgba(52, 59, 88, 0.8);
    background: rgba(0, 0, 0, 0.05);
  }

  .activity-btn.active {
    color: #e1e4f0;
    background: rgba(122, 162, 247, 0.12);
  }

  .activity-btn.active::before {
    content: "";
    position: absolute;
    left: 0;
    top: 10px;
    bottom: 10px;
    width: 3px;
    background: #7aa2f7;
    border-radius: 0 2px 2px 0;
  }

  :global(html.light) .activity-btn.active {
    color: #1a1b2e;
    background: rgba(52, 84, 138, 0.1);
  }

  :global(html.light) .activity-btn.active::before {
    background: #34548a;
  }

  .activity-btn:disabled {
    opacity: 0.35;
    cursor: default;
  }

  @keyframes spin {
    from { transform: rotate(0deg); }
    to { transform: rotate(-360deg); }
  }
  .spinning {
    animation: spin 1s linear infinite;
  }

  /* ═══ Main Area ═══ */
  .main-area {
    flex: 1;
    display: flex;
    flex-direction: column;
    min-width: 0;
    overflow: hidden;
  }

  .panel-header {
    height: 40px;
    min-height: 40px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0 20px;
    background: transparent;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    user-select: none;
  }

  :global(html.light) .panel-header {
    background: transparent;
    border-bottom-color: rgba(0, 0, 0, 0.06);
  }

  .panel-title {
    font-size: 12px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.6px;
    color: rgba(169, 177, 214, 0.4);
  }

  :global(html.light) .panel-title {
    color: rgba(52, 59, 88, 0.4);
  }

  .header-presets {
    display: flex;
    gap: 6px;
  }

  .preset-chip {
    padding: 3px 12px;
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 8px;
    background: rgba(255, 255, 255, 0.05);
    color: #e0af68;
    cursor: pointer;
    font-size: 11px;
    font-weight: 500;
    transition: all 0.15s;
    font-family: inherit;
  }
  .preset-chip:hover {
    background: rgba(255, 255, 255, 0.08);
    border-color: rgba(224, 175, 104, 0.3);
  }

  :global(html.light) .preset-chip {
    background: rgba(0, 0, 0, 0.04);
    border-color: rgba(0, 0, 0, 0.08);
    color: #8f5e15;
  }
  :global(html.light) .preset-chip:hover {
    background: rgba(0, 0, 0, 0.07);
    border-color: rgba(143, 94, 21, 0.3);
  }

  /* ═══ Content scroll area ═══ */
  .content-scroll {
    flex: 1;
    overflow-y: auto;
    padding: 16px 20px;
    padding-bottom: 40px; /* statusbar clearance */
  }

  .cards-list {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .error-banner {
    padding: 10px 14px;
    margin-bottom: 10px;
    background: rgba(247, 118, 142, 0.08);
    border-left: 3px solid #f7768e;
    color: #f7768e;
    font-size: 12px;
    border-radius: 0 8px 8px 0;
  }

  :global(html.light) .error-banner {
    background: rgba(140, 67, 81, 0.06);
    border-left-color: #8c4351;
    color: #8c4351;
  }

  .empty-state {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 4rem 2rem;
    color: rgba(169, 177, 214, 0.35);
    gap: 1rem;
  }

  .empty-state p {
    margin: 0;
    font-size: 13px;
  }

  .profiles-view {
    padding: 8px 0;
  }
</style>
