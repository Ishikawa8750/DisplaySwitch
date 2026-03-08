<script lang="ts">
  import type { AppConfig, HotkeyBinding, Preset, PresetAction, DisplayInfo, HotkeyActionType } from "./types";
  import { t } from "./i18n";

  let {
    config: initialConfig,
    displays,
    onclose,
    onsave,
  }: {
    config: AppConfig;
    displays: DisplayInfo[];
    onclose: () => void;
    onsave: (config: AppConfig) => void;
  } = $props();

  // Local mutable copy (deep clone the prop to avoid mutating parent state)
  let localConfig = $state<AppConfig>(JSON.parse(JSON.stringify(initialConfig)));
  let activeTab = $state<"general" | "hotkeys" | "presets">("general");
  let autostartLoading = $state(false);
  let statusMsg = $state("");
  let updateChecking = $state(false);
  let updateStatusMsg = $state("");

  // ── Check for Updates ──────────────────────────────────────────────────
  async function checkForUpdate() {
    updateChecking = true;
    updateStatusMsg = "";
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const version = await invoke<string | null>("check_for_update");
      if (version) {
        updateStatusMsg = t("updater.available", version);
      } else {
        updateStatusMsg = t("updater.up_to_date");
      }
    } catch (e: any) {
      updateStatusMsg = `⚠ ${e?.message ?? e}`;
    }
    updateChecking = false;
    setTimeout(() => (updateStatusMsg = ""), 8000);
  }

  // ── Autostart ──────────────────────────────────────────────────────────
  async function toggleAutostart() {
    autostartLoading = true;
    try {
      const autostart = await import("@tauri-apps/plugin-autostart");
      if (localConfig.autostart) {
        await autostart.disable();
        localConfig.autostart = false;
      } else {
        await autostart.enable();
        localConfig.autostart = true;
      }
      statusMsg = localConfig.autostart ? "✓ Autostart enabled" : "✓ Autostart disabled";
    } catch (e: any) {
      statusMsg = `⚠ Autostart: ${e?.message ?? e}`;
    }
    autostartLoading = false;
    setTimeout(() => (statusMsg = ""), 3000);
  }

  // ── Check actual autostart state on mount ──────────────────────────────
  $effect(() => {
    import("@tauri-apps/plugin-autostart").then(async (mod) => {
      try {
        localConfig.autostart = await mod.isEnabled();
      } catch {
        // Ignore
      }
    });
  });

  // ── Hotkeys ────────────────────────────────────────────────────────────
  let editingHotkeyId = $state<string | null>(null);
  let recordingShortcut = $state(false);
  let recordedKeys = $state("");

  function startRecording(id: string) {
    editingHotkeyId = id;
    recordingShortcut = true;
    recordedKeys = "Press keys…";
  }

  function handleKeyDown(e: KeyboardEvent) {
    if (!recordingShortcut || !editingHotkeyId) return;
    e.preventDefault();
    e.stopPropagation();

    // Build shortcut string from modifiers + key
    const parts: string[] = [];
    if (e.ctrlKey) parts.push("Ctrl");
    if (e.altKey) parts.push("Alt");
    if (e.shiftKey) parts.push("Shift");
    if (e.metaKey) parts.push("Super");

    // Ignore standalone modifier keys
    const key = e.key;
    if (["Control", "Alt", "Shift", "Meta"].includes(key)) {
      recordedKeys = parts.join("+") + "+…";
      return;
    }

    // Map special keys
    let keyName = key;
    if (key === "ArrowUp") keyName = "Up";
    else if (key === "ArrowDown") keyName = "Down";
    else if (key === "ArrowLeft") keyName = "Left";
    else if (key === "ArrowRight") keyName = "Right";
    else if (key.length === 1) keyName = key.toUpperCase();

    parts.push(keyName);
    const shortcut = parts.join("+");

    // Apply to binding
    const idx = localConfig.hotkeys.findIndex((h) => h.id === editingHotkeyId);
    if (idx >= 0) {
      localConfig.hotkeys[idx] = { ...localConfig.hotkeys[idx], shortcut };
    }

    recordingShortcut = false;
    editingHotkeyId = null;
    recordedKeys = "";
  }

  function removeHotkey(id: string) {
    localConfig.hotkeys = localConfig.hotkeys.filter((h) => h.id !== id);
  }

  function addHotkey() {
    const id = "hk_" + Date.now();
    localConfig.hotkeys = [
      ...localConfig.hotkeys,
      {
        id,
        shortcut: "",
        action: "brightness_up" as HotkeyActionType,
        value: 10,
        label: "New Hotkey",
      },
    ];
  }

  // ── Presets ────────────────────────────────────────────────────────────
  let editingPresetId = $state<string | null>(null);

  function addPreset() {
    const id = "preset_" + Date.now();
    const actions: PresetAction[] = displays.map((d) => ({
      display_index: d.display_index,
      display_name: d.name,
      brightness: d.brightness >= 0 ? d.brightness : undefined,
      input_code: !d.is_internal && d.current_input > 0 ? d.current_input : undefined,
    }));
    localConfig.presets = [
      ...localConfig.presets,
      { id, name: "New Preset", actions },
    ];
    editingPresetId = id;
  }

  function removePreset(id: string) {
    localConfig.presets = localConfig.presets.filter((p) => p.id !== id);
    if (editingPresetId === id) editingPresetId = null;
  }

  function saveAndClose() {
    onsave(localConfig);
    onclose();
  }

  const actionLabels: Record<HotkeyActionType, string> = {
    brightness_up: t("hotkey.action.brightness_up"),
    brightness_down: t("hotkey.action.brightness_down"),
    switch_input: t("hotkey.action.switch_input"),
    apply_preset: t("hotkey.action.apply_preset"),
    apply_profile: t("profile.activate"),
    refresh: t("hotkey.action.refresh"),
  };

  const inputNames: Record<number, string> = {
    1: "VGA-1", 2: "VGA-2", 3: "DVI-1", 4: "DVI-2",
    15: "DP-1", 16: "DP-2", 17: "HDMI-1", 18: "HDMI-2", 27: "USB-C",
  };
</script>

<!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
<!-- svelte-ignore a11y_interactive_supports_focus -->
<div class="overlay" role="dialog" aria-modal="true" tabindex="-1" onkeydown={handleKeyDown}>
  <div class="panel">
    <header>
      <h2>⚙ {t("settings.title")}</h2>
      <button class="close-btn" onclick={onclose}>✕</button>
    </header>

    <!-- Tabs -->
    <nav class="tabs">
      <button class:active={activeTab === "general"} onclick={() => (activeTab = "general")}>
        {t("settings.tab.general")}
      </button>
      <button class:active={activeTab === "hotkeys"} onclick={() => (activeTab = "hotkeys")}>
        {t("settings.tab.hotkeys")}
      </button>
      <button class:active={activeTab === "presets"} onclick={() => (activeTab = "presets")}>
        {t("settings.tab.presets")}
      </button>
    </nav>

    <div class="content">
      <!-- ═══ General Tab ═══ -->
      {#if activeTab === "general"}
        <section>
          <div class="setting-row">
            <div>
              <strong>{t("settings.autostart")}</strong>
              <p class="hint">Launch DisplaySwitch when you log in</p>
            </div>
            <button
              class="toggle"
              class:on={localConfig.autostart}
              onclick={toggleAutostart}
              disabled={autostartLoading}
            >
              {localConfig.autostart ? "ON" : "OFF"}
            </button>
          </div>

          <div class="setting-row">
            <div>
              <strong>{t("settings.sync_interval")}</strong>
              <p class="hint">Brightness polling interval (ms)</p>
            </div>
            <select
              value={String(localConfig.sync_interval_ms)}
              onchange={(e) => {
                localConfig.sync_interval_ms = Number(e.currentTarget.value);
              }}
            >
              <option value="2000">2s</option>
              <option value="5000">5s (default)</option>
              <option value="10000">10s</option>
              <option value="30000">30s</option>
            </select>
          </div>

          <div class="setting-row">
            <div>
              <strong>{t("settings.theme")}</strong>
              <p class="hint">Dark / Light / System</p>
            </div>
            <select
              value={localConfig.theme}
              onchange={(e) => {
                localConfig.theme = e.currentTarget.value as AppConfig["theme"];
              }}
            >
              <option value="dark">{t("settings.theme.dark")}</option>
              <option value="light">{t("settings.theme.light")}</option>
              <option value="system">{t("settings.theme.system")}</option>
            </select>
          </div>

          <div class="setting-row">
            <div>
              <strong>{t("settings.language")}</strong>
              <p class="hint">Interface language</p>
            </div>
            <select
              value={localConfig.locale}
              onchange={(e) => {
                localConfig.locale = e.currentTarget.value as AppConfig["locale"];
              }}
            >
              <option value="auto">{t("settings.lang.auto")}</option>
              <option value="en">{t("settings.lang.en")}</option>
              <option value="zh">{t("settings.lang.zh")}</option>
            </select>
          </div>

          <!-- ALS Auto Brightness -->
          <div class="setting-row">
            <div>
              <strong>{t("als.auto_brightness")}</strong>
              <p class="hint">{localConfig.als_enabled ? t("als.enabled") : t("als.disabled")}</p>
            </div>
            <button
              class="toggle"
              class:on={localConfig.als_enabled}
              onclick={() => { localConfig.als_enabled = !localConfig.als_enabled; }}
            >
              {localConfig.als_enabled ? "ON" : "OFF"}
            </button>
          </div>

          {#if localConfig.als_enabled}
            <div class="setting-row">
              <div>
                <strong>{t("als.interval")}</strong>
                <p class="hint">ms</p>
              </div>
              <select
                value={String(localConfig.als_interval_ms)}
                onchange={(e) => {
                  localConfig.als_interval_ms = Number(e.currentTarget.value);
                }}
              >
                <option value="5000">5s</option>
                <option value="10000">10s (default)</option>
                <option value="30000">30s</option>
                <option value="60000">60s</option>
              </select>
            </div>
          {/if}

          <!-- Check for Updates -->
          <div class="setting-row">
            <div>
              <strong>{t("updater.check")}</strong>
              <p class="hint">{updateStatusMsg}</p>
            </div>
            <button onclick={checkForUpdate} disabled={updateChecking}>
              {updateChecking ? t("updater.checking") : t("updater.check")}
            </button>
          </div>
        </section>

      <!-- ═══ Hotkeys Tab ═══ -->
      {:else if activeTab === "hotkeys"}
        <section>
          <p class="hint" style="margin-bottom: 0.8rem">
            Global keyboard shortcuts work even when the window is hidden.
          </p>

          {#each localConfig.hotkeys as hk, i (hk.id)}
            <div class="hotkey-row">
              <div class="hk-shortcut">
                <button
                  class="shortcut-btn"
                  class:recording={editingHotkeyId === hk.id}
                  onclick={() => startRecording(hk.id)}
                >
                  {editingHotkeyId === hk.id ? recordedKeys : (hk.shortcut || "Click to set")}
                </button>
              </div>
              <div class="hk-action">
                <select
                  value={hk.action}
                  onchange={(e) => {
                    localConfig.hotkeys[i] = {
                      ...hk,
                      action: e.currentTarget.value as HotkeyActionType,
                    };
                  }}
                >
                  {#each Object.entries(actionLabels) as [key, label]}
                    <option value={key}>{label}</option>
                  {/each}
                </select>
              </div>
              <div class="hk-value">
                {#if hk.action === "brightness_up" || hk.action === "brightness_down"}
                  <input
                    type="number"
                    min="1"
                    max="50"
                    value={hk.value ?? 10}
                    onchange={(e) => {
                      localConfig.hotkeys[i] = {
                        ...hk,
                        value: Number(e.currentTarget.value),
                      };
                    }}
                  />
                  <span class="unit">%</span>
                {:else if hk.action === "apply_preset"}
                  <select
                    value={String(hk.value ?? 0)}
                    onchange={(e) => {
                      localConfig.hotkeys[i] = {
                        ...hk,
                        value: Number(e.currentTarget.value),
                      };
                    }}
                  >
                    {#each localConfig.presets as preset, pi}
                      <option value={String(pi)}>{preset.name}</option>
                    {/each}
                    {#if localConfig.presets.length === 0}
                      <option value="0">(no presets)</option>
                    {/if}
                  </select>
                {/if}
              </div>
              <button class="remove-btn" onclick={() => removeHotkey(hk.id)}>✕</button>
            </div>
          {/each}

          <button class="add-btn" onclick={addHotkey}>{t("hotkey.add")}</button>
        </section>

      <!-- ═══ Presets Tab ═══ -->
      {:else if activeTab === "presets"}
        <section>
          <p class="hint" style="margin-bottom: 0.8rem">
            Presets save brightness & input source combos to apply with one click or hotkey.
          </p>

          {#each localConfig.presets as preset, pi (preset.id)}
            <div class="preset-card">
              <div class="preset-header">
                <input
                  class="preset-name"
                  type="text"
                  value={preset.name}
                  onchange={(e) => {
                    localConfig.presets[pi] = { ...preset, name: e.currentTarget.value };
                  }}
                />
                <button class="remove-btn" onclick={() => removePreset(preset.id)}>✕</button>
              </div>

              {#if editingPresetId === preset.id}
                <div class="preset-actions">
                  {#each preset.actions as act, ai}
                    <div class="action-row">
                      <span class="act-display">{act.display_name}</span>
                      <label>
                        Brightness
                        <input
                          type="number"
                          min="0"
                          max="100"
                          value={act.brightness ?? ""}
                          placeholder="—"
                          onchange={(e) => {
                            const val = e.currentTarget.value;
                            const newAct = { ...act, brightness: val ? Number(val) : undefined };
                            const newActions = [...preset.actions];
                            newActions[ai] = newAct;
                            localConfig.presets[pi] = { ...preset, actions: newActions };
                          }}
                        />
                      </label>
                      <label>
                        Input
                        <select
                          value={String(act.input_code ?? "")}
                          onchange={(e) => {
                            const val = e.currentTarget.value;
                            const newAct = { ...act, input_code: val ? Number(val) : undefined };
                            const newActions = [...preset.actions];
                            newActions[ai] = newAct;
                            localConfig.presets[pi] = { ...preset, actions: newActions };
                          }}
                        >
                          <option value="">—</option>
                          {#each Object.entries(inputNames) as [code, name]}
                            <option value={code}>{name}</option>
                          {/each}
                        </select>
                      </label>
                    </div>
                  {/each}
                </div>
                <button class="collapse-btn" onclick={() => (editingPresetId = null)}>
                  ▲ Collapse
                </button>
              {:else}
                <button class="collapse-btn" onclick={() => (editingPresetId = preset.id)}>
                  ▼ Edit actions ({preset.actions.length} displays)
                </button>
              {/if}
            </div>
          {/each}

          <button class="add-btn" onclick={addPreset}>{t("preset.add")}</button>
        </section>
      {/if}
    </div>

    {#if statusMsg}
      <div class="status">{statusMsg}</div>
    {/if}

    <footer>
      <button class="cancel-btn" onclick={onclose}>{t("settings.cancel")}</button>
      <button class="save-btn" onclick={saveAndClose}>{t("settings.save")}</button>
    </footer>
  </div>
</div>

<style>
  .overlay {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.5);
    backdrop-filter: blur(4px);
    -webkit-backdrop-filter: blur(4px);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 1000;
  }

  .panel {
    background: rgba(22, 22, 30, 0.96);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 14px;
    width: 560px;
    max-height: 80vh;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    box-shadow: 0 20px 60px rgba(0, 0, 0, 0.5);
    backdrop-filter: blur(40px);
    -webkit-backdrop-filter: blur(40px);
  }

  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 20px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
  }

  h2 {
    margin: 0;
    font-size: 14px;
    font-weight: 600;
    color: #e1e4f0;
  }

  .close-btn {
    background: none;
    border: none;
    color: rgba(169, 177, 214, 0.4);
    font-size: 16px;
    cursor: pointer;
    padding: 4px 6px;
    line-height: 1;
    border-radius: 6px;
    transition: background 0.15s, color 0.15s;
  }
  .close-btn:hover {
    color: #e1e4f0;
    background: rgba(255, 255, 255, 0.06);
  }

  .tabs {
    display: flex;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    padding: 0 16px;
    gap: 2px;
  }
  .tabs button {
    background: none;
    border: none;
    border-bottom: 2px solid transparent;
    color: rgba(169, 177, 214, 0.45);
    padding: 10px 14px;
    cursor: pointer;
    font-size: 12px;
    font-weight: 500;
    font-family: inherit;
    transition: all 0.12s;
  }
  .tabs button:hover { color: rgba(169, 177, 214, 0.8); }
  .tabs button.active {
    color: #7aa2f7;
    border-bottom-color: #7aa2f7;
  }

  .content {
    flex: 1;
    overflow-y: auto;
    padding: 16px 20px;
  }

  section { display: flex; flex-direction: column; gap: 4px; }

  .setting-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 0;
    border-bottom: 1px solid rgba(255, 255, 255, 0.04);
  }
  .setting-row strong { color: rgba(169, 177, 214, 0.85); font-size: 12px; font-weight: 500; }

  .hint { color: rgba(169, 177, 214, 0.3); font-size: 11px; margin: 2px 0 0; }

  .toggle {
    padding: 4px 14px;
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 8px;
    background: rgba(255, 255, 255, 0.05);
    color: rgba(169, 177, 214, 0.5);
    cursor: pointer;
    font-size: 11px;
    font-weight: 500;
    font-family: inherit;
    min-width: 48px;
    transition: all 0.15s;
  }
  .toggle.on {
    background: rgba(122, 162, 247, 0.18);
    border-color: rgba(122, 162, 247, 0.35);
    color: #7aa2f7;
  }

  select {
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 8px;
    color: rgba(169, 177, 214, 0.85);
    padding: 4px 8px;
    font-size: 12px;
    font-family: inherit;
  }

  /* ── Hotkeys ── */
  .hotkey-row {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 8px 0;
    border-bottom: 1px solid rgba(255, 255, 255, 0.04);
  }
  .hk-shortcut { flex: 0 0 130px; }
  .hk-action { flex: 1; }
  .hk-value {
    flex: 0 0 72px;
    display: flex;
    align-items: center;
    gap: 3px;
  }
  .hk-value input {
    width: 44px;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    color: rgba(169, 177, 214, 0.85);
    padding: 3px 6px;
    font-size: 11px;
    font-family: inherit;
  }
  .unit { color: rgba(169, 177, 214, 0.3); font-size: 10px; }

  .shortcut-btn {
    width: 100%;
    padding: 4px 10px;
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    background: rgba(255, 255, 255, 0.03);
    color: rgba(169, 177, 214, 0.85);
    cursor: pointer;
    font-family: "SF Mono", "Cascadia Code", "Consolas", monospace;
    font-size: 11px;
    text-align: center;
    transition: border-color 0.15s;
  }
  .shortcut-btn:hover { border-color: rgba(122, 162, 247, 0.4); }
  .shortcut-btn.recording {
    border-color: #e0af68;
    color: #e0af68;
    animation: pulse 1s infinite;
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
  }

  .remove-btn {
    background: none;
    border: none;
    color: rgba(169, 177, 214, 0.25);
    cursor: pointer;
    font-size: 14px;
    padding: 4px 6px;
    font-family: inherit;
    border-radius: 6px;
    transition: color 0.15s, background 0.15s;
  }
  .remove-btn:hover { color: #f7768e; background: rgba(247, 118, 142, 0.08); }

  .add-btn {
    margin-top: 8px;
    padding: 5px 14px;
    border: 1px dashed rgba(255, 255, 255, 0.1);
    border-radius: 8px;
    background: transparent;
    color: rgba(169, 177, 214, 0.4);
    cursor: pointer;
    font-size: 11px;
    font-family: inherit;
    transition: all 0.15s;
  }
  .add-btn:hover { border-color: rgba(122, 162, 247, 0.4); color: #7aa2f7; }

  /* ── Presets ── */
  .preset-card {
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid rgba(255, 255, 255, 0.06);
    border-radius: 10px;
    padding: 12px;
  }
  .preset-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
  }
  .preset-name {
    background: transparent;
    border: none;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
    color: #e1e4f0;
    font-size: 13px;
    font-weight: 600;
    font-family: inherit;
    padding: 2px 0;
    flex: 1;
    margin-right: 8px;
  }
  .preset-name:focus { border-bottom-color: #7aa2f7; outline: none; }

  .preset-actions {
    margin-top: 8px;
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
  .action-row {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 12px;
  }
  .act-display {
    flex: 0 0 110px;
    color: rgba(169, 177, 214, 0.45);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .action-row label {
    display: flex;
    align-items: center;
    gap: 4px;
    color: rgba(169, 177, 214, 0.45);
    font-size: 11px;
  }
  .action-row input {
    width: 44px;
    background: rgba(255, 255, 255, 0.05);
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 6px;
    color: rgba(169, 177, 214, 0.85);
    padding: 3px;
    font-size: 11px;
    font-family: inherit;
  }
  .action-row select {
    font-size: 11px;
  }

  .collapse-btn {
    margin-top: 4px;
    background: none;
    border: none;
    color: rgba(169, 177, 214, 0.3);
    cursor: pointer;
    font-size: 11px;
    font-family: inherit;
    padding: 2px 0;
  }
  .collapse-btn:hover { color: #7aa2f7; }

  .status {
    padding: 8px 20px;
    font-size: 11px;
    color: #e0af68;
    background: rgba(224, 175, 104, 0.06);
  }

  footer {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
    padding: 12px 20px;
    border-top: 1px solid rgba(255, 255, 255, 0.06);
  }
  .cancel-btn {
    padding: 6px 16px;
    border: 1px solid rgba(255, 255, 255, 0.08);
    border-radius: 8px;
    background: rgba(255, 255, 255, 0.05);
    color: rgba(169, 177, 214, 0.6);
    cursor: pointer;
    font-size: 12px;
    font-family: inherit;
    transition: all 0.15s;
  }
  .cancel-btn:hover { background: rgba(255, 255, 255, 0.08); color: rgba(169, 177, 214, 0.8); }

  .save-btn {
    padding: 6px 16px;
    border: 1px solid rgba(122, 162, 247, 0.35);
    border-radius: 8px;
    background: rgba(122, 162, 247, 0.18);
    color: #7aa2f7;
    cursor: pointer;
    font-size: 12px;
    font-weight: 500;
    font-family: inherit;
    transition: all 0.15s;
  }
  .save-btn:hover { background: rgba(122, 162, 247, 0.25); }

  /* ── Light theme ── */
  :global(html.light) .panel {
    background: rgba(220, 220, 225, 0.96);
    border-color: rgba(0, 0, 0, 0.08);
  }
  :global(html.light) header {
    border-bottom-color: rgba(0, 0, 0, 0.06);
  }
  :global(html.light) h2 { color: #1a1b2e; }
  :global(html.light) .close-btn { color: rgba(52, 59, 88, 0.4); }
  :global(html.light) .close-btn:hover { color: #1a1b2e; background: rgba(0, 0, 0, 0.05); }
  :global(html.light) .tabs { border-bottom-color: rgba(0, 0, 0, 0.06); }
  :global(html.light) .tabs button { color: rgba(52, 59, 88, 0.4); }
  :global(html.light) .tabs button:hover { color: rgba(52, 59, 88, 0.8); }
  :global(html.light) .tabs button.active {
    color: #34548a;
    border-bottom-color: #34548a;
  }
  :global(html.light) .setting-row { border-bottom-color: rgba(0, 0, 0, 0.04); }
  :global(html.light) .setting-row strong { color: rgba(52, 59, 88, 0.85); }
  :global(html.light) .hint { color: rgba(52, 59, 88, 0.3); }
  :global(html.light) .toggle {
    background: rgba(0, 0, 0, 0.04);
    border-color: rgba(0, 0, 0, 0.08);
    color: rgba(52, 59, 88, 0.45);
  }
  :global(html.light) .toggle.on {
    background: rgba(52, 84, 138, 0.12);
    border-color: rgba(52, 84, 138, 0.3);
    color: #34548a;
  }
  :global(html.light) select {
    background: rgba(0, 0, 0, 0.04);
    border-color: rgba(0, 0, 0, 0.08);
    color: rgba(52, 59, 88, 0.85);
  }
  :global(html.light) .shortcut-btn {
    background: rgba(0, 0, 0, 0.03);
    border-color: rgba(0, 0, 0, 0.08);
    color: rgba(52, 59, 88, 0.85);
  }
  :global(html.light) .hk-value input {
    background: rgba(0, 0, 0, 0.04);
    border-color: rgba(0, 0, 0, 0.08);
    color: rgba(52, 59, 88, 0.85);
  }
  :global(html.light) .hotkey-row { border-bottom-color: rgba(0, 0, 0, 0.04); }
  :global(html.light) .preset-card {
    background: rgba(0, 0, 0, 0.03);
    border-color: rgba(0, 0, 0, 0.06);
  }
  :global(html.light) .preset-name { color: #1a1b2e; border-bottom-color: rgba(0, 0, 0, 0.06); }
  :global(html.light) .preset-name:focus { border-bottom-color: #34548a; }
  :global(html.light) .action-row input {
    background: rgba(0, 0, 0, 0.04);
    border-color: rgba(0, 0, 0, 0.08);
    color: rgba(52, 59, 88, 0.85);
  }
  :global(html.light) .add-btn { border-color: rgba(0, 0, 0, 0.1); color: rgba(52, 59, 88, 0.4); }
  :global(html.light) .add-btn:hover { border-color: rgba(52, 84, 138, 0.4); color: #34548a; }
  :global(html.light) .cancel-btn {
    background: rgba(0, 0, 0, 0.04);
    border-color: rgba(0, 0, 0, 0.08);
    color: rgba(52, 59, 88, 0.6);
  }
  :global(html.light) .cancel-btn:hover { background: rgba(0, 0, 0, 0.07); }
  :global(html.light) footer { border-top-color: rgba(0, 0, 0, 0.06); }
  :global(html.light) .status { color: #8f5e15; background: rgba(143, 94, 21, 0.06); }
</style>
